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

static void serverTests() {
    testVersionReject(TP(28800));
    testSeedFetchDedupe();
    testStopDoesNotBlock(TP(28801));
}
