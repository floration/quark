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

static void serverTests() {
    testVersionReject(TP(28800));
}
