// Server and netcode tests (FINAL_REVIEW 2.8-2.11, 6.4, 6.5, the 64-bit clock, PERF-8 / PERF-9, autosave,
// ENG-2). Included by tests.cpp after its helpers (pump, idle, CHECK, TP).

// REVIEW 2.9: a CONNECT with another protocol version is answered before any cookie check, so its source may be
// spoofed: the reject must never be bigger than the request (a bare 11 byte one gets nothing).
static void testVersionReject(uint16_t port) {
    printf("version mismatch reject: never bigger than the request\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetAddr srv{0x7F000001u, port};
    UdpSocket raw;
    CHECK(raw.open(0));
    auto ask = [&](const std::string& name, bool bare, int& got, std::string& why) {
        uint8_t b[300];
        Writer w(b, sizeof b);
        writeHeader(w, PKT_CONNECT); w.u16((uint16_t)(kProtocolVersion - 1)); w.u32(0x4242);
        if (!bare) { w.u32(0); w.str(name); }
        raw.send(srv, b, w.size());
        got = 0; why.clear();
        double end = timeNow() + 0.3;
        int biggest = 0;
        while (timeNow() < end) {
            s.frame();
            uint8_t buf[2048]; NetAddr from; int n;
            while ((n = raw.recv(from, buf, sizeof buf)) > 0) {
                biggest = std::max(biggest, n);
                Reader r(buf, n);
                if (r.u32() == kProtocolMagic && r.u8() == PKT_REJECT) { got++; why = r.str(); }
            }
            sleepMs(1);
        }
        CHECK(biggest <= w.size());
        return w.size();
    };
    int got = 0;
    std::string why;
    int sz = ask("", true, got, why);
    CHECK(sz == 11 && got == 0);                                   // the spoofable minimum: no reply at all
    ask("", false, got, why);                                      // an old client with an empty name: 16 bytes
    CHECK(got == 1 && why == "version " + std::to_string(kProtocolVersion));
    ask("someone", false, got, why);
    CHECK(got == 1 && why.find(std::to_string(kProtocolVersion)) != std::string::npos);
    ask("a rather long player name", false, got, why);           // room for the whole sentence
    CHECK(got == 1 && why == "version mismatch (server " + std::to_string(kProtocolVersion) + ")");
    s.stop();
}

#ifndef _WIN32
// A local HTTP server that holds each request holdMs before answering 200 with `body`, counting them (POSIX only).
struct SlowHttp {
    int ls = -1;
    uint16_t port = 0;
    std::atomic<int> requests{0};
    std::atomic<bool> quit{false};
    std::thread th;
    std::vector<std::thread> conns;  // one per connection (answered concurrently)
    std::mutex mu;
    std::vector<std::string> seen;   // the request lines
    SlowHttp(int holdMs, std::string body) {
        ls = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
        socklen_t al = sizeof a;
        if (ls < 0 || bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, 16) != 0 || getsockname(ls, (sockaddr*)&a, &al) != 0) return;
        port = ntohs(a.sin_port);
        th = std::thread([this, holdMs, body] {
            while (!quit) {
                fd_set fs; FD_ZERO(&fs); FD_SET(ls, &fs);
                timeval tv{0, 20000};
                if (select(ls + 1, &fs, nullptr, nullptr, &tv) <= 0) continue;
                int c = accept(ls, nullptr, nullptr);
                if (c < 0) continue;
                conns.emplace_back([this, c, holdMs, body] {
                    std::string req;
                    char buf[4096];
                    while (req.find("\r\n\r\n") == std::string::npos) { ssize_t n = recv(c, buf, sizeof buf, 0); if (n <= 0) break; req.append(buf, (size_t)n); }
                    requests++;
                    { std::lock_guard<std::mutex> l(mu); seen.push_back(req.substr(0, req.find("\r\n"))); }
                    for (int t = 0; t < holdMs / 10 && !quit; t++) sleepMs(10);
                    std::string resp = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                    send(c, resp.data(), resp.size(), 0);
                    close(c);
                });
            }
            for (auto& t : conns) t.join();
        });
    }
    bool ok() const { return port != 0; }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port); }
    ~SlowHttp() { quit = true; if (th.joinable()) th.join(); if (ls >= 0) close(ls); }
};
#endif

// FINAL_REVIEW 2.8: tourney seed ratings are fetched whenever someone with a ranked id joins; rotating keys could
// queue ~150 lookups a second. Now one lookup per ladder waits behind the one in flight, with every new id in it.
static void testSeedFetchDedupe() {
    printf("tourney seed ratings: one pending lookup per ladder\n");
#ifndef _WIN32
    SlowHttp http(400, "");
    CHECK(http.ok());
    if (!http.ok()) return;
    setenv("QUARK_MASTER", http.url().c_str(), 1);
    {
        Server s;
        s.cfg_.mode = MODE_TOURNEY;
        auto& p = s.players_[0];
        p.connected = true; p.isBot = false; p.team = TEAM_A;
        int ladders = 0;
        for (int v = 0; v < kArenaVariants; v++) ladders += arenaVariantRanked(v);
        size_t worst = 0;
        for (int k = 0; k < 300; k++) {   // a new key every time (each join would do this)
            p.rankId = sha256Hex("seed" + std::to_string(k)).substr(0, 32);
            s.prefetchSeedRatings();
            worst = std::max(worst, s.seedRatings_->pendingJobs());
            sleepMs(1);
        }
        printf("  300 joins: at most %zu lookups queued (%d ladders), %d sent so far\n", worst, ladders, http.requests.load());
        CHECK(worst <= (size_t)ladders);
        double end = timeNow() + 20;
        while (timeNow() < end && s.seedRatings_->pendingJobs() > 0) sleepMs(20);
        sleepMs(500);
        CHECK(http.requests <= 3 * ladders);   // ~the first per ladder, and the merged one behind it
        bool sawLast = false;   // the newest key made it into a lookup
        { std::lock_guard<std::mutex> l(http.mu); for (auto& r : http.seen) sawLast |= r.find(p.rankId) != std::string::npos; }
        CHECK(sawLast);
        p.connected = false;
    }
    setenv("QUARK_MASTER", "off", 1);
#endif
}

// FINAL_REVIEW 2.10: leaving a hosted game ran Server::stop() on the UI thread, which joined the reachability
// thread (internet list + public IP lookups), the seed-rating thread, the list announcer and UPnP: up to ~15 s frozen.
// Here every one of them is stuck on a slow ranking server when the host stops.
static void testStopDoesNotBlock(uint16_t port) {
    printf("stopping a hosted game doesn't wait on the network\n");
#ifndef _WIN32
    SlowHttp http(2500, "");
    CHECK(http.ok());
    if (!http.ok()) return;
    setenv("QUARK_MASTER", http.url().c_str(), 1);
    unsetenv("QUARK_PROBER");
    {
        Server s;
        ServerConfig cfg;
        cfg.map = "colosseum"; cfg.mode = MODE_TOURNEY; cfg.port = port;
        cfg.probe = true; cfg.publicList = true;   // reachability check (list lookup) + announcer
        s.setLog([](const std::string&) {});
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; setenv("QUARK_MASTER", "off", 1); return; } }
        s.players_[0].connected = true; s.players_[0].rankId = std::string(32, 'c');
        s.prefetchSeedRatings();   // the seed-rating thread
        double end = timeNow() + 0.6;
        while (timeNow() < end) { s.frame(); sleepMs(2); }
        int inFlight = http.requests;
        s.players_[0].connected = false;
        auto t0 = std::chrono::steady_clock::now();
        s.stop();
        double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  %d requests in flight; stop() took %.0f ms (%d cleanup jobs left in the background)\n", inFlight, took * 1000, Server::backgroundJobs());
        CHECK(inFlight >= 3);   // the list (prober), the announce, a rating lookup
        CHECK(took < 0.3);
    }
    CHECK(Server::waitBackground(30));   // and they do finish (an exiting game gives them a few seconds)
    setenv("QUARK_MASTER", "off", 1);
#endif
}

// FINAL_REVIEW 2.11: a reconnect matched by its token but proving another ranked key (or none) used to take the slot
// back with the old ranked id. And a free for all leaver back after the grace was counted twice in the result.
static void testRestoreKeepsIdentity(uint16_t port) {
    printf("reconnect token never carries a ranked id the connect didn't prove\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)qrand());
    RankKey kb = rankKeyFromSecret(sha256Hex(std::string("restoreB") + tag)), kc = rankKeyFromSecret(sha256Hex(std::string("restoreC") + tag));
    std::string addr = "127.0.0.1:" + std::to_string(port);
    NetClient b;
    b.setRankKey(kb);
    b.connect(addr, "b");
    std::vector<NetClient*> cls = {&b};
    pump(s, cls, 1.0, idle);
    int ib = b.clientId();
    CHECK(ib >= 0 && ib < kMaxPlayers && s.players_[ib].rankId == kb.id);
    if (ib < 0 || ib >= kMaxPlayers) { s.stop(); return; }
    // same game (same token), another key: a new session, and b's slot keeps b's id and key
    b.disconnect();
    pump(s, cls, 0.3, idle);
    b.setRankKey(kc);
    b.connect(addr, "b");
    pump(s, cls, 1.0, idle);
    int ic = b.clientId();
    CHECK(b.state() == ConnState::Connected && ic != ib && s.players_[ic].rankId == kc.id);
    CHECK(s.players_[ib].connected && s.away(ib) && s.players_[ib].rankId == kb.id && s.players_[ib].rankPub == kb.pub);
    // and no key at all: no ranked id (a new session again)
    b.disconnect();
    pump(s, cls, 0.3, idle);
    b.setRankKey(RankKey());
    b.connect(addr, "b");
    pump(s, cls, 1.0, idle);
    int in = b.clientId();
    CHECK(b.state() == ConnState::Connected && in != ib && in >= 0 && in < kMaxPlayers && s.players_[in].rankId.empty());
    CHECK(s.players_[ib].rankId == kb.id);
    // the right key (token or not) gets the slot back
    b.disconnect();
    pump(s, cls, 0.3, idle);
    b.setRankKey(kb);
    b.connect(addr, "b");
    pump(s, cls, 1.0, idle);
    CHECK(b.clientId() == ib && !s.away(ib) && s.players_[ib].rankId == kb.id);
    s.stop();

    // the result: a free for all leaver who came back (here: playing again) counts once, as the leaver
    printf("ranked free for all: a leaver back after the grace counts once\n");
    Server r;
    int v = arenaVariantFromName("instagib");
    CHECK(v >= 0 && arenaVariantRanked(v));
    if (v < 0) return;
    r.cfg_.ranked = true; r.cfg_.mode = rankedMode(v); r.ms_.arenaVariant = (uint8_t)v;
    r.rankedMatch_ = "0123456789abcdef";
    const char* ids[3] = {"a", "b", "c"};
    for (int i = 0; i < 3; i++) {
        auto& p = r.players_[i];
        p.connected = true; p.team = TEAM_A; p.rankId = std::string(32, ids[i][0]); p.name = ids[i]; p.score = 3 - i;
    }
    r.rankedLeft_.push_back({7, std::string(32, 'c'), TEAM_A, 0, 0});   // c left from slot 7, rejoined as slot 2
    std::string why, text = r.rankedResult(-1, &why);
    printf("  result: %s\n", text.c_str());
    int entries = 0;
    for (char ch : text.substr(0, text.find(" left="))) entries += ch == ':';
    CHECK(!text.empty() && entries == 2 * 3);                         // a, b and c (once)
    CHECK(text.find(",2:") == std::string::npos && text.find(" 2:") == std::string::npos && text.find("7:") != std::string::npos);
    CHECK(text.find(" left=7") != std::string::npos);
    for (auto& p : r.players_) p.connected = false;
}

// FINAL_REVIEW 6.4: a giant (duel "double size") spawned at a random spawn when none had a giant's room, inside the
// geometry on maps not built for duel (anvil's a / b spawns, some on furnace and summit). Every map, both sides,
// every size: the hull a player spawns with is never in a wall.
static void testGiantSpawns() {
    printf("giant / tiny spawns: never inside a wall\n");
    int giant = -1, tiny = -1;
    for (int m = 0; m < DUEL_MOD_COUNT; m++) {
        if (giant < 0 && duelModDef(m).size > 1.01f) giant = m;
        if (tiny < 0 && duelModDef(m).size < 0.99f) tiny = m;
    }
    CHECK(giant >= 0 && tiny >= 0);
    int spawns = 0, blocked = 0, crouched = 0, shrunk = 0;
    std::string where;
    for (const std::string& name : listMaps()) {
        if (name.rfind("zz_", 0) == 0) continue;
        Server s;
        s.setLog([](const std::string&) {});
        if (!s.map_.load(name)) continue;
        s.cfg_.mode = s.ms_.mode = MODE_DUEL;
        s.ms_.phase = PHASE_LIVE;
        for (int i = 0; i < 2; i++) { auto& p = s.players_[i]; p.connected = true; p.isBot = true; p.team = i ? TEAM_B : TEAM_A; }
        for (int mod : {giant, tiny, 255}) {
            s.ms_.duelMod = (uint8_t)mod;
            for (int k = 0; k < 24; k++) {
                int id = k & 1;
                auto& p = s.players_[id];
                p.alive = false;
                s.spawnPlayer(id);
                spawns++;
                if (s.map_.boxSolid(playerBox(p.move))) { blocked++; if (where.find(name) == std::string::npos) where += " " + name; }
                if (p.move.flags & MF_CROUCHED) crouched++;
                if (mod == giant && !(p.move.mods & MOVE_BIG)) shrunk++;
                p.alive = false;   // (out of the way of the next spawn's checks)
            }
        }
        for (auto& p : s.players_) p.connected = false;
    }
    printf("  %d spawns: %d in a wall%s, %d crouched for room, %d giants at normal size for want of any\n", spawns, blocked, where.c_str(), crouched, shrunk);
    CHECK(spawns > 0 && blocked == 0);
    // the fallbacks, on made-up maps: a spawn 0.5 m from a wall (moved aside), one under a 2 m ceiling (normal size)
    for (int room = 0; room < 2; room++) {
        Server s;
        s.setLog([](const std::string&) {});
        std::string text = "box -50 -1 -50 50 0 50\n" + std::string(room == 0 ? "box 1 0 -50 2 6 50\n" : "box -50 2 -50 50 3 50\n") + "spawn any 0.5 0 0\n";
        std::string err;
        CHECK(s.map_.parse(text, &err));
        s.cfg_.mode = s.ms_.mode = MODE_DUEL; s.ms_.phase = PHASE_LIVE; s.ms_.duelMod = (uint8_t)giant;
        auto& p = s.players_[0];
        p.connected = true; p.isBot = true; p.team = TEAM_A;
        s.spawnPlayer(0);
        bool big = (p.move.mods & MOVE_BIG) != 0;
        CHECK(!s.map_.boxSolid(playerBox(p.move)));
        CHECK(room == 0 ? big && p.move.pos.x <= 0.2f + 1e-4f : !big);
        p.connected = false;
    }
}

// FINAL_REVIEW 6.5: an admin's addbot during a tag match put the bot straight on a side (past the team lock), and a
// bot reusing an eliminated bot's name was marked out at once (tag-outs were matched by name).
static void testTagAddbot(uint16_t port) {
    printf("tag: addbot mid-match waits for the next match; bots aren't matched by name\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "hamlet"; cfg.mode = MODE_TAG; cfg.port = port; cfg.autoStart = false;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient c[3];
    std::vector<NetClient*> cls;
    for (int i = 0; i < 3; i++) { c[i].connect("127.0.0.1:" + std::to_string(port), "p" + std::to_string(i)); cls.push_back(&c[i]); }
    pump(s, cls, 0.6, idle);
    int early = s.addBot(TEAM_A, BOT_BEGINNER);
    CHECK(early >= 0);
    s.command("forcestart");
    for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE; i++) { if (s.ms_.phase == PHASE_FREEZE) s.phaseEnd_ = s.time(); pump(s, cls, 0.05, idle); }
    CHECK(s.ms_.phase == PHASE_LIVE && rulesTeamLocked(s));
    if (early < 0 || s.ms_.phase != PHASE_LIVE) { s.stop(); return; }
    if (s.players_[early].it) { s.players_[early].it = false; s.players_[c[0].clientId()].it = true; }   // (a human is it)
    std::string outName = s.players_[early].name;
    s.removeBot(early);   // a bot leaving mid-match: out (and remembered, if it were a human)
    pump(s, cls, 0.1, idle);
    s.command("addbot");  // the admin's
    int late = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].isBot) late = i;
    CHECK(late >= 0);
    if (late < 0) { s.stop(); return; }
    pump(s, cls, 0.2, idle);
    CHECK(s.players_[late].team == TEAM_SPECTATOR && s.players_[late].waitingToPlay && !s.players_[late].alive);
    // even a bot named like the eliminated one, on a side (as before the lock), isn't out
    s.players_[late].name = outName;
    s.players_[late].team = TEAM_A;
    pump(s, cls, 0.2, idle);
    CHECK(!s.players_[late].out);
    s.players_[late].team = TEAM_SPECTATOR;
    // the match over: it plays the next one
    s.command("restart");
    pump(s, cls, 0.2, idle);
    CHECK(s.players_[late].connected && s.players_[late].team != TEAM_SPECTATOR && !s.players_[late].waitingToPlay);
    s.stop();
}

static void serverTests() {
    testVersionReject(TP(28800));
    testSeedFetchDedupe();
    testStopDoesNotBlock(TP(28801));
    testRestoreKeepsIdentity(TP(28802));
    testGiantSpawns();
    testTagAddbot(TP(28803));
}
