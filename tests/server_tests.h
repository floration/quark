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
    std::vector<std::chrono::steady_clock::time_point> seenAt;   // and when each arrived (real time)
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
                    { std::lock_guard<std::mutex> l(mu); seen.push_back(req.substr(0, req.find("\r\n"))); seenAt.push_back(std::chrono::steady_clock::now()); }
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

// The server clock is 64 bits (timeMs_ wrapped after 49.7 days at 32); the wire keeps 32 and each side unwraps.
// A server 1.5 s short of 2^32 ms: a client plays through the wrap - clock, ping, delta snapshots and lag-compensated
// hits on a mover all carry on.
static void testClockWrap(uint16_t port) {
    printf("server clock past 2^32 ms (49.7 days)\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = false;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    const uint64_t wrap = 1ull << 32;
    s.timeMs_ = wrap - 2500;
    CHECK(s.fromWireMs((uint32_t)(wrap + 5)) == wrap + 5 && s.fromWireMs((uint32_t)(wrap - 2600)) == wrap - 2600);
    NetClient a, b;
    a.setInterpFixed(0.05f);
    a.connect("127.0.0.1:" + std::to_string(port), "shooter");
    b.connect("127.0.0.1:" + std::to_string(port), "mover");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 1.0, idle);
    CHECK(a.state() == ConnState::Connected && b.state() == ConnState::Connected);
    if (a.clientId() < 0 || b.clientId() < 0) { s.stop(); return; }
    auto& sa = s.players_[a.clientId()];
    auto& sb = s.players_[b.clientId()];
    auto moverAt = [](double t) { double ph = std::fmod(t, 1.0); return Vec3(-5, 0, 19 + 2.5f * (float)(ph < 0.5 ? ph * 4 - 1 : 3 - ph * 4)); };
    Vec3 shooterPos(10, 0, 19);
    double prevRender = 0;
    bool monotonic = true;
    std::vector<int> lags;
    auto input = [&](int i, bool fire) {
        sa.move.pos = shooterPos; sa.move.vel = {};
        sb.move.pos = moverAt(s.time()); sb.move.vel = {}; sb.health = 100;
        UserCmd c;
        c.weapon = WPN_RIFLE;
        if (i != 0) return c;
        if (a.renderTime() < prevRender) monotonic = false;
        prevRender = a.renderTime();
        if (sa.lastViewTick) lags.push_back((int)((int64_t)s.timeMs_ - (int64_t)sa.lastViewTick));
        NetClient::PlayerArray ps = a.interpolatedPlayers();
        const InterpPlayer& ip = ps[b.clientId()];
        if (!ip.valid) return c;
        Vec3 d = normalize(ip.snap.pos + Vec3(0, 1.0f, 0) - (shooterPos + Vec3(0, kEyeHeight, 0)));
        c.pitch = std::asin(d.y); c.yaw = std::atan2(-d.x, -d.z);
        if (fire) c.buttons = BTN_FIRE;
        return c;
    };
    pump(s, cls, 0.4, [&](int i) { return input(i, false); });
    int fired0 = sa.shotsFired, hit0 = sa.shotsHit;
    pump(s, cls, 2.5, [&](int i) { return input(i, true); });   // through the wrap
    int fired = sa.shotsFired - fired0, hit = sa.shotsHit - hit0;
    std::sort(lags.begin(), lags.end());
    int median = lags.empty() ? -1 : lags[lags.size() / 2];
    printf("  clock %llu ms (past 2^32 by %lld), render time %.3f s, ping %.0f ms, view tick a median %d ms behind, %d of %d rifle shots hit, %u undecodable deltas\n",
           (unsigned long long)s.timeMs_, (long long)s.timeMs_ - (long long)wrap, a.renderTime(), sa.ping, median, hit, fired, a.baseMisses());
    CHECK(s.timeMs_ > wrap + 500);
    CHECK(a.state() == ConnState::Connected && a.renderTime() > wrap * 0.001 && monotonic);
    CHECK(sa.ping >= 0 && sa.ping < 100);
    CHECK(median >= 40 && median < 200);
    CHECK(fired >= 8 && hit * 10 >= fired * 8);
    CHECK(a.baseMisses() < 5);
    s.stop();
}

// ---- snapshot / visibility benchmark (PERF-8, PERF-9) ----
// Counts heap allocations made on a thread while g_countAllocs is set (the whole test binary uses this operator new).
static thread_local bool g_countAllocs = false;
static std::atomic<long long> g_allocs{0};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
// Fault injection (ENG-2 tests): >= 0, the allocation that many from now on this thread throws bad_alloc.
static thread_local long long g_failAllocIn = -1;
static std::atomic<long long> g_injected{0};
void* operator new(size_t n) {
    if (g_failAllocIn >= 0 && g_failAllocIn-- == 0) { g_injected++; throw std::bad_alloc(); }
    if (g_countAllocs) g_allocs.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
// The nothrow forms too: their library defaults call the throwing operator new above, but AddressSanitizer replaces
// them with its own, so without these a nothrow allocation (std::stable_sort's temporary buffer, libstdc++ 14) came
// from ASan's new and went back through the free() above: alloc-dealloc-mismatch in every group (CI sanitizers job).
void* operator new(size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

static double threadCpu() {
#ifndef _WIN32
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#else
    return timeNow();
#endif
}

struct SnapBenchResult { double avgUs = 0, p50Us = 0, p99Us = 0, maxUs = 0, allocsPerSend = 0, tickAvgUs = 0, tickP999Us = 0, tickMaxUs = 0, raysPerSec = 0; };
// `humans` fake clients (snapshots go to closed loopback ports) walking around a map's spawns, 1000 Hz ticks with a
// snapshot every 4th (32 players: 250 a second) for `seconds`, each client acking the one sent 48 ms before. CPU
// time of each send and of each tick's visibility + send, and heap allocations per send, after the first 2.4 s.
// `rockets`: that many projectiles in flight the whole time, circling the players' spawns (each one's line of sight
// to each client is checked too).
static SnapBenchResult snapBench(const char* mapName, int humans, double seconds, uint16_t port, bool print = true, int rockets = 0) {
    SnapBenchResult res;
    Server s;
    ServerConfig cfg;
    cfg.map = mapName; cfg.mode = MODE_FFA; cfg.port = port; cfg.maxPlayers = kMaxPlayers;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { printf("  FAIL %s:%d: server start\n", __FILE__, __LINE__); g_fail++; return res; }
    uint32_t rng = 12345;
    auto rnd = [&] { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.0f / 16777216.0f); };
    std::vector<Vec3> home(kMaxPlayers);
    for (int i = 0; i < humans && i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        p = ServerPlayer();
        p.connected = true; p.isBot = false; p.team = TEAM_A; p.name = "h" + std::to_string(i);
        p.addr = NetAddr{0x7F000001u, (uint16_t)(port + 100 + i)};
        p.nonce = 1000 + i;
        for (auto& h : p.history) h.tick = ServerPlayer::kNoHistory;
        s.spawnPlayer(i);
        home[i] = p.move.pos;
        p.events.clear();
    }
    rockets = std::min(rockets, kMaxProjectiles);
    for (int r = 0; r < rockets; r++) {
        auto& pr = s.projectiles_[r];
        pr = Projectile();
        pr.active = true; pr.id = (uint16_t)(5000 + r); pr.type = WPN_ROCKET; pr.owner = r % std::max(1, humans); pr.dieAt = 1e9;
    }
    std::vector<double> t, ticks;
    long long allocs = 0;
    uint64_t rays0 = 0;
    int counted = 0;
    const int sends = (int)(seconds * 250);
    for (int k = 0; k < sends; k++) {
        for (int i = 0; i < humans; i++) {   // a slow random walk around each one's spawn (positions and velocities)
            auto& p = s.players_[i];
            float a = (float)(k * 0.004 * (0.5 + (i % 5) * 0.2) + i);
            Vec3 want = home[i] + Vec3(std::cos(a) * 3.0f, 0, std::sin(a) * 3.0f);
            p.move.vel = (want - p.move.pos) * 250.0f;
            p.move.pos = want;
            p.yaw = a; p.health = 100 - (k / 50) % 50;
            p.ackSnapMs = s.timeMs_ >= 48 ? s.timeMs_ - 48 : 0;
            if (rnd() < 0.002f) p.kills++;
        }
        for (int r = 0; r < rockets; r++) {   // circling 8 m around a spawn, 1.5 m up (a different one for each)
            auto& pr = s.projectiles_[r];
            float a = (float)(k * 0.02 + r);
            Vec3 at = home[(r * 7) % std::max(1, humans)] + Vec3(std::cos(a) * 8.0f, 1.5f, std::sin(a) * 8.0f);
            pr.vel = (at - pr.pos) * 250.0f;
            pr.pos = at;
        }
        if (k % 20 == 0) s.message("bench: an event with a text longer than the short-string buffer " + std::to_string(k));
        for (int i = 0; i < humans; i++) {   // (each client acks events a few behind, like a real one a round trip later)
            auto& p = s.players_[i];
            while (!p.events.empty() && p.events.front().id + 8 < p.nextEventId) p.events.pop_front();
        }
        bool count = k >= 600;   // (after the delta-base rings have filled)
        if (k == 600) rays0 = s.visRays_;
        for (int tk = 0; tk < 4; tk++) {   // 4 ticks: visibility every tick, the snapshot on the 4th
            s.timeMs_ += 1;
            s.tick_ += 1;
            double c0 = threadCpu();
            s.updateVisibilityForTest();
            double cv = threadCpu();
            if (tk == 3) {
                g_countAllocs = count;
                long long a0 = g_allocs.load();
                s.sendSnapshotsForTest();
                g_countAllocs = false;
                if (count) allocs += g_allocs.load() - a0;
            }
            double c1 = threadCpu();
            if (count) { ticks.push_back((c1 - c0) * 1e6); if (tk == 3) { counted++; t.push_back((c1 - cv) * 1e6); } }
        }
    }
    std::sort(ticks.begin(), ticks.end());
    if (!ticks.empty()) {
        double ts = 0;
        for (double v : ticks) ts += v;
        res.tickAvgUs = ts / ticks.size();
        res.tickP999Us = ticks[std::min(ticks.size() - 1, (size_t)(ticks.size() * 0.999))];
        res.tickMaxUs = ticks.back();
        res.raysPerSec = (double)(s.visRays_ - rays0) / (ticks.size() * 0.001);
    }
    std::sort(t.begin(), t.end());
    double sum = 0;
    for (double v : t) sum += v;
    if (!t.empty()) {
        res.avgUs = sum / t.size();
        res.p50Us = t[t.size() / 2];
        res.p99Us = t[std::min(t.size() - 1, (size_t)(t.size() * 0.99))];
        res.maxUs = t.back();
    }
    res.allocsPerSend = counted ? (double)allocs / counted : 0;
    if (print)
        printf("  SNAPBENCH %s: %d clients: ticks (visibility + send) avg %.1f us  p99.9 %.1f us  worst %.1f us, %.0f rays/s; sends: avg %.1f us  p50 %.1f us"
               "  worst %.1f us, %.2f heap allocations per send\n", mapName, humans, res.tickAvgUs, res.tickP999Us, res.tickMaxUs, res.raysPerSec, res.avgUs,
               res.p50Us, res.maxUs, res.allocsPerSend);
    if (print && rockets) printf("    (with %d rockets in flight)\n", rockets);
    for (auto& p : s.players_) p.connected = false;
    s.stop();
    return res;
}

// PERF-9: lines of sight are checked a share per tick (never more than the ray budget in one tick, which was ~31000
// rays in one ~10 ms tick on Midtown with 32 players), yet every line still gets checked about every 50 ms.
static void testVisibilitySpread(uint16_t port) {
    printf("visibility: spread over the ticks, every line still fresh\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "midtown"; cfg.mode = MODE_FFA; cfg.port = port; cfg.maxPlayers = kMaxPlayers;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { printf("  FAIL %s:%d: server start\n", __FILE__, __LINE__); g_fail++; return; }
    std::vector<Vec3> home(kMaxPlayers);
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = s.players_[i];
        p = ServerPlayer();
        p.connected = true; p.isBot = false; p.team = TEAM_A; p.name = "v" + std::to_string(i);
        p.addr = NetAddr{0x7F000001u, (uint16_t)(port + 100 + i)};
        for (auto& h : p.history) h.tick = ServerPlayer::kNoHistory;
        s.spawnPlayer(i);
        home[i] = p.move.pos;
    }
    const int cap = std::max(64, 1000000 / s.tickRate()) + 31;   // (server.cpp kVisRaysPerSecond; a line's 32 rays can straddle it)
    uint64_t maxRays = 0;
    double worstAge = 0;
    int pairsNear = 0;
    for (int k = 0; k < 1500; k++) {   // 1.5 s of 1000 Hz ticks
        s.timeMs_ += 1; s.tick_ += 1;
        for (int i = 0; i < kMaxPlayers; i++) {
            auto& p = s.players_[i];
            float a = (float)(k * 0.001 * (0.5 + (i % 5) * 0.2) + i);
            Vec3 want = home[i] + Vec3(std::cos(a) * 3.0f, 0, std::sin(a) * 3.0f);
            p.move.vel = (want - p.move.pos) * 1000.0f;
            p.move.pos = want;
        }
        uint64_t r0 = s.visRays_;
        s.updateVisibilityForTest();
        maxRays = std::max(maxRays, s.visRays_ - r0);
        if (k < 200) continue;
        for (int v = 0; v < kMaxPlayers; v++)
            for (int t = 0; t < kMaxPlayers; t++) {
                if (v == t || length(s.players_[t].move.pos - s.players_[v].move.pos) >= 19.0f) continue;   // (near: always seen)
                pairsNear++;
                worstAge = std::max(worstAge, s.time() - s.players_[v].lastSeen[t]);
            }
    }
    printf("  32 players on midtown: at most %llu rays in a tick (cap %d), near lines at most %.0f ms old (%d samples)\n", (unsigned long long)maxRays, cap,
           worstAge * 1000, pairsNear);
    CHECK(maxRays <= (uint64_t)cap);
    CHECK(pairsNear > 0 && worstAge <= 0.075);
    for (auto& p : s.players_) p.connected = false;
    s.stop();
}

// The edit session's autosave ran on the tick: serializing Midtown (~10 ms) and parse-checking it (~2.5 ms) every
// 5 s while edited. Now the tick only copies the map; the rest runs in the background, and a save that an edit
// overtook leaves the session dirty for the next one.
static void testAutosaveOffTick(uint16_t port) {
    printf("edit session autosave off the tick\n");
    Map base;
    CHECK(base.load("midtown"));
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)qrand());
    std::string name = std::string("zz_autosave") + tag, path = userMapsDir() + "/" + name + ".qmap";
    CHECK(saveMapFile(path, base.serialize()));
    Server s;
    ServerConfig cfg;
    cfg.map = name; cfg.mode = MODE_FFA; cfg.port = port; cfg.editMode = true;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; std::remove(path.c_str()); return; }
    auto fileText = [&] { std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); };
    // the old way, for the record: serialize + check + write on the calling thread
    CHECK(s.applyEdit(-1, "t Edited 1"));
    double c0 = threadCpu();
    CHECK(s.autosave());
    double syncMs = (threadCpu() - c0) * 1000;
    // an edit overtakes a background save: still dirty after it
    CHECK(s.applyEdit(-1, "t Edited 2"));
    c0 = threadCpu();
    s.startAutosave();
    double startMs = (threadCpu() - c0) * 1000;
    CHECK(s.applyEdit(-1, "t Edited 3"));
    s.waitAutosave();
    s.pollAutosave();
    CHECK(s.editDirty_);
    // the next one: saved, clean, and the saved text is the map's (a download doesn't serialize it again)
    c0 = threadCpu();
    s.startAutosave();
    double start2Ms = (threadCpu() - c0) * 1000;
    s.waitAutosave();
    s.pollAutosave();
    CHECK(!s.editDirty_ && !s.map_.sourceStale);
    std::string disk = fileText();
    CHECK(disk == s.map_.serialize());
    Map back;
    CHECK(back.parse(disk) && back.title == "Edited 3");
    printf("  midtown: a save on the calling thread %.1f ms CPU; starting a background one %.2f / %.2f ms\n", syncMs, startMs, start2Ms);
    CHECK(startMs < 3.0 && start2Ms < 3.0);
    s.stop();
    std::remove(path.c_str());
    std::remove((path + ".bak").c_str());
}

// RankService's delays are real seconds (REMAINING 6: they were game time, slept as real time): a race board asked
// for 2 s from now goes out 2 s later on the wall clock, even with the tests' 4x game clock and another job waking
// the thread in between (which made it go at once).
static void testRankDelaysRealTime() {
    printf("ranked service: delays in real time (game clock x%.0f)\n", kTestTimeScale);
#ifndef _WIN32
    SlowHttp http(0, "");
    CHECK(http.ok());
    if (!http.ok()) return;
    setenv("QUARK_MASTER", http.url().c_str(), 1);
    {
        RankService rs;
        auto t0 = std::chrono::steady_clock::now();
        rs.fetchRaceBoard("viaduct", 2.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        rs.fetch("vanilla", {std::string(32, 'd')});   // wakes the service's thread
        double boardAt = -1;
        auto end = t0 + std::chrono::seconds(8);
        while (std::chrono::steady_clock::now() < end && boardAt < 0) {
            {
                std::lock_guard<std::mutex> l(http.mu);
                for (size_t k = 0; k < http.seen.size(); k++)
                    if (http.seen[k].find("/raceboard") != std::string::npos) boardAt = std::chrono::duration<double>(http.seenAt[k] - t0).count();
            }
            sleepMs(10);
        }
        printf("  a board due in 2 s went out after %.2f s\n", boardAt);
        CHECK(boardAt >= 1.9 && boardAt < 3.5);
    }
    setenv("QUARK_MASTER", "off", 1);
#endif
}

// ENG-2: exception boundaries. Every allocation in turn fails (bad_alloc, injected on this thread) while a demo is
// opened and played, and while the server takes connects and input: each ends in an error or a dropped packet,
// never std::terminate, and both carry on afterwards.
static void testExceptionBoundaries(uint16_t port) {
    printf("exception boundaries: demos and packets with allocations failing\n");
    std::string path = userDataPath() + "/zz_eng2.qdemo";
    {   // a short demo
        Server s;
        ServerConfig cfg;
        cfg.map = "harbor"; cfg.mode = MODE_FFA; cfg.port = port;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { printf("  FAIL %s:%d: server start\n", __FILE__, __LINE__); g_fail++; return; }
        NetClient a;
        a.connect("127.0.0.1:" + std::to_string(port), "rec");
        std::vector<NetClient*> cls = {&a};
        pump(s, cls, 1.0, idle);
        s.addBot(TEAM_A, 1);
        CHECK(a.startRecording(path));
        a.sendCommand("say an event in the demo");
        pump(s, cls, 0.8, [](int) { UserCmd c; c.forward = 127; return c; });
        a.stopRecording();
        s.stop();
    }
    long long injected0 = g_injected;
    int opened = 0, refused = 0, tries = 0;
    for (long long n = 0; n < 400; n += (n < 100 ? 1 : 7)) {   // opening
        NetClient p;
        std::string err;
        g_failAllocIn = n;
        bool ok = p.startPlayback(path, err);
        g_failAllocIn = -1;
        tries++;
        if (ok) opened++; else { refused++; CHECK(!err.empty()); }
    }
    int polls = 0, stopped = 0;
    {   // playing (a failure ends the demo with the reason: start it again)
        NetClient p;
        for (long long n = 0; n < 600; n += 3) {
            std::string err;
            if (!p.playback() && !p.startPlayback(path, err)) { CHECK(false); break; }
            g_failAllocIn = n;
            p.poll();
            g_failAllocIn = -1;
            polls++;
            if (!p.playback()) { stopped++; CHECK(p.error().rfind("demo stopped: ", 0) == 0 || p.error() == "demo finished"); }
            sleepMs(1);
        }
    }
    NetClient fine;
    std::string err;
    CHECK(fine.startPlayback(path, err) && fine.mapReady());   // and nothing is left broken
    std::remove(path.c_str());
    // the server: connects (challenge, then the real one) and input, each allocation failing in turn
    Server s;
    ServerConfig cfg;
    cfg.map = "harbor"; cfg.mode = MODE_FFA; cfg.port = (uint16_t)(port + 1);
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { printf("  FAIL %s:%d: server start\n", __FILE__, __LINE__); g_fail++; return; }
    UdpSocket raw;
    CHECK(raw.open(0));
    NetAddr me{0x7F000001u, raw.localPort()};
    auto connectPkt = [&cfg](uint32_t nonce, uint32_t cookie, std::vector<uint8_t>& out) {
        uint8_t b[256];
        Writer w(b, sizeof b);
        writeHeader(w, PKT_CONNECT); w.u16(kProtocolVersion); w.u32(nonce); w.u32(cookie); w.str("injected");
        w.u32(0); w.u32(0); w.str(""); w.str(""); w.u32(0x7F000001u); w.u16(cfg.port);
        out.assign(b, b + w.size());
    };
    int serverTries = 0;
    for (long long n = 0; n < 160; n++) {
        uint32_t nonce = 0x5000 + (uint32_t)n;
        std::vector<uint8_t> pkt;
        connectPkt(nonce, 0, pkt);
        g_failAllocIn = n;
        s.receivePacket(me, pkt.data(), (int)pkt.size());   // the challenge
        g_failAllocIn = -1;
        uint32_t cookie = 0;
        uint8_t buf[2048]; NetAddr from; int len;
        for (int k = 0; k < 50 && !cookie; k++) {
            while ((len = raw.recv(from, buf, sizeof buf)) > 0) { Reader r(buf, len); if (r.u32() == kProtocolMagic && r.u8() == PKT_CHALLENGE) { r.u32(); cookie = r.u32(); } }
            if (!cookie) sleepMs(1);
        }
        if (!cookie) { connectPkt(nonce, 0, pkt); s.receivePacket(me, pkt.data(), (int)pkt.size()); continue; }   // (failed: next n)
        connectPkt(nonce, cookie, pkt);
        g_failAllocIn = n;
        s.receivePacket(me, pkt.data(), (int)pkt.size());   // the real connect
        g_failAllocIn = -1;
        serverTries++;
        int id = -1;
        for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].nonce == nonce) id = i;
        if (id >= 0) {   // input, a string command or two
            uint8_t in[512];
            Writer w(in, sizeof in);
            writeHeader(w, PKT_INPUT); w.u32(nonce); w.u32(0); w.u32(0); w.u8(0); w.u8(1);
            UserCmd c; c.seq = 1; c.msec = 1; writeCmd(w, c);
            w.u8(2); w.u16(1); w.str("say a long chat line that has to be allocated somewhere"); w.str("name someone else entirely");
            g_failAllocIn = n / 2;
            s.receivePacket(me, in, w.size());
            g_failAllocIn = -1;
            s.command("kick " + std::to_string(id));
        }
        while (raw.recv(from, buf, sizeof buf) > 0) {}
    }
    long long injected = g_injected - injected0;
    printf("  %lld allocations failed: demo opened %d / refused %d of %d tries, %d of %d polls stopped it; %d server connects\n", injected, opened, refused,
           tries, stopped, polls, serverTries);
    CHECK(injected > 50 && refused > 0 && opened > 0 && stopped > 0);
    NetClient after;   // the server still takes a real player
    after.connect("127.0.0.1:" + std::to_string(cfg.port), "after");
    std::vector<NetClient*> cls = {&after};
    pump(s, cls, 1.0, idle);
    CHECK(after.state() == ConnState::Connected && after.hasSnapshot());
    s.stop();
}

// Dedicated servers pause with no humans on (cfg pauseWithoutHumans): bots alone don't tick, the game clock stays,
// and the first human to connect gets a normal game at once. Off (hosted, these tests' servers): bots play on.
static void testPauseWithoutHumans(uint16_t port) {
    printf("dedicated server: paused with bots alone, resumed by a connect\n");
    std::vector<std::string> lines;
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port; cfg.vsBots = true; cfg.botsEnemy = 2; cfg.pauseWithoutHumans = true;
    s.setLog([&](const std::string& l) { lines.push_back(l); });
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    std::vector<NetClient*> none;
    pump(s, none, 0.5, idle);
    CHECK(s.paused_ && s.tick_ == 0 && s.timeMs_ == 0 && s.playerCount() == 2);
    double t0 = timeNow(), w = s.frame();
    s.idle(w);
    printf("  paused: tick %llu, clock %llu ms; an idle() pass slept %.0f ms\n", (unsigned long long)s.tick_, (unsigned long long)s.timeMs_, (timeNow() - t0) * 1000);
    CHECK(w >= 0.04 && timeNow() - t0 >= 0.03);   // (sleeps longer than the 5 ms of an unpaused bots-only server)
    NetClient c;
    c.connect("127.0.0.1:" + std::to_string(port), "human");
    std::vector<NetClient*> cls = {&c};
    pump(s, cls, 1.0, idle);
    CHECK(!s.paused_ && c.state() == ConnState::Connected && c.hasSnapshot() && s.tick_ > 0 && s.timeMs_ > 0);
    CHECK(s.ms_.phase == PHASE_WARMUP || s.ms_.phase == PHASE_FREEZE || s.ms_.phase == PHASE_LIVE);
    // the human gone (past the grace): paused again, the bots' match dropped for warmup, and the clock stays
    const int id = c.clientId();
    c.disconnect();
    pump(s, cls, 0.3, idle);
    CHECK(!s.paused_);   // (away, in the reconnect grace: still a human)
    if (id >= 0 && id < kMaxPlayers && s.players_[id].awayUntil > 0) s.players_[id].awayUntil = timeNow() - 1;   // the grace is over
    pump(s, none, 0.3, idle);
    uint64_t tick = s.tick_, ms = s.timeMs_;
    pump(s, none, 0.3, idle);
    CHECK(s.paused_ && s.tick_ == tick && s.timeMs_ == ms && s.ms_.phase == PHASE_WARMUP);
    int said = 0;
    for (auto& l : lines) said += l.find("paused: no humans") != std::string::npos ? 1 : l.find("resumed") != std::string::npos ? 100 : 0;
    CHECK(said == 2 + 100);   // (once each way, each time)
    s.stop();
    // off: bots alone keep simulating
    Server b;
    cfg.pauseWithoutHumans = false;
    b.setLog([](const std::string&) {});
    if (!b.start(cfg)) { printf("  FAIL %s:%d: server start\n", __FILE__, __LINE__); g_fail++; return; }
    pump(b, none, 0.3, idle);
    printf("  flag off: %llu ticks with bots alone\n", (unsigned long long)b.tick_);
    CHECK(!b.paused_ && b.tick_ > 50 && b.timeMs_ > 50);
    b.stop();
}

static void serverTests() {
    testVersionReject(TP(28800));
    testSeedFetchDedupe();
    testStopDoesNotBlock(TP(28801));
    testRestoreKeepsIdentity(TP(28802));
    testGiantSpawns();
    testTagAddbot(TP(28803));
    testClockWrap(TP(28804));
    {   // PERF-8: a snapshot send allocates nothing once the delta-base rings have grown (events included)
        printf("snapshots: no heap allocations per send\n");
        SnapBenchResult r = snapBench("harbor", 8, 3.0, TP(28805), false);
        printf("  %.2f allocations per send, CPU p50 %.1f us\n", r.allocsPerSend, r.p50Us);
        CHECK(r.allocsPerSend == 0);
    }
    testVisibilitySpread(TP(28806));
    testAutosaveOffTick(TP(28807));
    testRankDelaysRealTime();
    testExceptionBoundaries(TP(28808));
    testPauseWithoutHumans(TP(28809));
}
