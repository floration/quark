// Game rule tests (owner answers 5.1-5.6, REVIEW batch 2): overtime, the syringe's modes, the shared alt-fire
// cooldown, size modifiers scaling the hull, the grapple, headshots, hide and seek scoring, tag eliminations,
// the dropped bomb, balance bots, spawn types, tick-rate changes and heal caps.
// Included by tests.cpp (it shares CHECK, pump, idle and TP).

// A server with n clients, connected and (if live) past the freeze. Returns false (and counts a failure) if it
// didn't start.
static bool rulesStart(Server& s, ServerConfig cfg, NetClient* c, int n, std::vector<NetClient*>& cls, bool live = true) {
    s.setLog([](const std::string&) {});
    std::string e;
    if (!s.start(cfg, &e)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e.c_str()); g_fail++; return false; }
    for (int i = 0; i < n; i++) { c[i].connect("127.0.0.1:" + std::to_string(cfg.port), "p" + std::to_string(i)); cls.push_back(&c[i]); }
    pump(s, cls, 0.6, idle);
    if (!live) return true;
    s.command("forcestart");
    for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE; i++) { if (s.ms_.phase == PHASE_FREEZE) s.phaseEnd_ = s.time(); pump(s, cls, 0.05, idle); }
    CHECK(s.ms_.phase == PHASE_LIVE);
    return s.ms_.phase == PHASE_LIVE;
}
static int rulesTeamPlayer(const Server& s, int team) {
    for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && !s.players_[i].isBot && s.players_[i].team == team) return i;
    return -1;
}

// 5.1: every mode with a site or a cart gets overtime. When the clock runs out it goes on while the side that needs
// time is on the objective, or touched it in the last 5 s, and ends 5 s after their last touch.
static void testOvertime(uint16_t port) {
    printf("overtime\n");
    struct Case { int mode; const char* map; };
    for (Case cs : {Case{MODE_PAYLOAD, "railyard"}, Case{MODE_POINTS, "lodestone"}, Case{MODE_BOMB, "depot"}, Case{MODE_NPAYLOAD, "canal"}}) {
        Server s;
        ServerConfig cfg;
        cfg.map = cs.map; cfg.mode = cs.mode; cfg.port = port++; cfg.balanceBots = false;
        NetClient c[2];
        std::vector<NetClient*> cls;
        if (!rulesStart(s, cfg, c, 2, cls)) { s.stop(); continue; }
        int att = rulesTeamPlayer(s, cs.mode == MODE_NPAYLOAD ? (int)TEAM_B : (int)s.ms_.attackers);
        int def = rulesTeamPlayer(s, cs.mode == MODE_NPAYLOAD ? (int)TEAM_A : (s.ms_.attackers == TEAM_A ? (int)TEAM_B : (int)TEAM_A));
        CHECK(att >= 0 && def >= 0);
        if (att < 0 || def < 0) { s.stop(); continue; }
        if (cs.mode == MODE_NPAYLOAD) s.ms_.payloadDist = s.ms_.payloadLen * 0.5f + 12.0f;   // team A is ahead: B needs time
        auto objective = [&]() -> Vec3 {
            if (isPayloadMode(cs.mode)) return s.map_.cartPoint(s.ms_.mode, s.ms_.attackers, s.ms_.payloadDist) + Vec3(0, 0, 2.5f);
            const AABB& z = cs.mode == MODE_POINTS ? s.map_.points[s.ms_.point] : s.map_.bombSites[0];
            return {z.center().x, z.mn.y + 0.05f, z.center().z};
        };
        Vec3 far = s.map_.spawns[0].pos;
        for (auto& sp : s.map_.spawns) if (length(sp.pos - objective()) > length(far - objective())) far = sp.pos;
        bool onIt = false;
        auto in = [&](int) {   // the attacker on (or off) the objective, the defender far away
            auto& a = s.players_[att]; auto& d = s.players_[def];
            a.move.pos = onIt ? objective() : far; a.move.vel = {};
            d.move.pos = far; d.move.vel = {};
            a.health = d.health = 100;
            return UserCmd();
        };
        auto lose = [&](double t) { s.phaseEnd_ = t; };
        // nobody touched it: time's up at 0
        onIt = false; pump(s, cls, 5.5, in);   // (pump times are game seconds)
        CHECK(!(s.ms_.flags & MS_OVERTIME));
        float dist0 = s.ms_.payloadDist;
        // on it when the clock runs out: overtime, which holds while they stay
        onIt = true; pump(s, cls, 0.1, in);
        lose(s.time());
        pump(s, cls, 0.5, in);
        printf("  %s: on it at 0:00 -> phase %d, overtime %d, %.2f s left\n", modeName(cs.mode), s.ms_.phase, (s.ms_.flags & MS_OVERTIME) != 0, s.ms_.timeLeft);
        CHECK(s.ms_.phase == PHASE_LIVE && (s.ms_.flags & MS_OVERTIME) && s.ms_.timeLeft > 4.5f);
        if (cs.mode == MODE_NPAYLOAD) s.ms_.payloadDist = dist0;   // (B pushed it back a little)
        // off it: it ends 5 s after the last touch, not before
        onIt = false;
        pump(s, cls, 2.5, in);
        CHECK(s.ms_.phase == PHASE_LIVE && (s.ms_.flags & MS_OVERTIME) && s.ms_.timeLeft < 3.0f);
        pump(s, cls, 3.0, in);
        CHECK(s.ms_.phase != PHASE_LIVE);
        if (cs.mode == MODE_NPAYLOAD) CHECK(s.ms_.phase == PHASE_MATCH_END);
        s.stop();
        if (cs.mode == MODE_NPAYLOAD) {   // the side that's ahead, at the cart at 0:00, doesn't hold the match open
            Server s3;
            cfg.port = port++;
            NetClient c3[2];
            std::vector<NetClient*> cls3;
            if (!rulesStart(s3, cfg, c3, 2, cls3)) { s3.stop(); continue; }
            int lead = rulesTeamPlayer(s3, TEAM_A);
            if (lead < 0) { g_fail++; s3.stop(); continue; }
            s3.ms_.payloadDist = s3.ms_.payloadLen * 0.5f + 12.0f;
            auto at = [&](int) { s3.players_[lead].move.pos = s3.map_.cartPoint(s3.ms_.mode, s3.ms_.attackers, s3.ms_.payloadDist) + Vec3(0, 0, 2.5f); return UserCmd(); };
            pump(s3, cls3, 0.2, at);
            s3.phaseEnd_ = s3.time();
            pump(s3, cls3, 0.2, at);
            CHECK(s3.ms_.phase == PHASE_MATCH_END && !(s3.ms_.flags & MS_OVERTIME));
            s3.stop();
        }
        if (cs.mode != MODE_PAYLOAD) continue;
        // touched 1 s before 0:00 (not on it at 0:00): overtime too
        Server s2;
        cfg.port = port++;
        NetClient c2[2];
        std::vector<NetClient*> cls2;
        if (!rulesStart(s2, cfg, c2, 2, cls2)) { s2.stop(); continue; }
        int a2 = rulesTeamPlayer(s2, s2.ms_.attackers);
        if (a2 < 0) { g_fail++; s2.stop(); continue; }
        Vec3 cart = s2.map_.cartPoint(s2.ms_.mode, s2.ms_.attackers, s2.ms_.payloadDist) + Vec3(0, 0, 2.5f);
        pump(s2, cls2, 0.1, [&](int) { s2.players_[a2].move.pos = cart; return UserCmd(); });
        pump(s2, cls2, 1.0, [&](int) { s2.players_[a2].move.pos = s2.map_.spawns[0].pos; return UserCmd(); });
        s2.phaseEnd_ = s2.time();
        pump(s2, cls2, 0.2, [&](int) { s2.players_[a2].move.pos = s2.map_.spawns[0].pos; return UserCmd(); });
        CHECK(s2.ms_.phase == PHASE_LIVE && (s2.ms_.flags & MS_OVERTIME));
        pump(s2, cls2, 4.0, [&](int) { s2.players_[a2].move.pos = s2.map_.spawns[0].pos; return UserCmd(); });
        CHECK(s2.ms_.phase != PHASE_LIVE);
        s2.stop();
    }
}

// 5.2: the syringe only exists in modes where the dead respawn, unless a custom mode lists it.
static void testSyringeModes() {
    printf("syringe only in respawn modes\n");
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        Server s;
        s.ms_.mode = (uint8_t)mode;
        s.cfg_.mode = mode;
        bool has = s.weaponAllowed(WPN_SYRINGE);
        bool want = modeRespawns(mode) && !isItMode(mode);   // (tag / hide and seek: the knife only)
        if (has != want) printf("  %s: syringe %s\n", modeName(mode), has ? "allowed" : "not allowed");
        CHECK(has == want);
        if (!isItMode(mode)) CHECK(s.weaponAllowed(WPN_ROCKET) || mode == MODE_ARENA || mode == MODE_TOURNEY);
    }
    Server s;   // a duel life with every weapon: still no syringe
    s.ms_.mode = MODE_DUEL; s.cfg_.mode = MODE_DUEL; s.ms_.duelMod = 4;   // DOUBLE SIZE (all weapons)
    CHECK(!s.weaponAllowed(WPN_SYRINGE) && s.weaponAllowed(WPN_ROCKET) && s.weaponAllowed(WPN_KNIFE));
    s.cfg_.custom.name = "medics";   // a custom VS mode that lists it keeps it
    s.ms_.mode = MODE_VS; s.cfg_.mode = MODE_VS;
    s.cfg_.custom.weapons = (uint16_t)((1u << WPN_SYRINGE) | (1u << WPN_ROCKET));
    CHECK(s.weaponAllowed(WPN_SYRINGE) && s.weaponAllowed(WPN_ROCKET) && !s.weaponAllowed(WPN_RIFLE));
    s.cfg_.custom.weapons = 0;       // ...one that allows "all" doesn't
    CHECK(!s.weaponAllowed(WPN_SYRINGE) && s.weaponAllowed(WPN_RIFLE));
}

// "quark_tests rules [name]": all of them, or the one named.
static void testGameRules(const std::string& only) {
    struct R { const char* name; std::function<void()> run; };
    std::vector<R> all = {
        {"overtime", [] { testOvertime(TP(29230)); }},
        {"syringe", [] { testSyringeModes(); }},
    };
    for (auto& r : all) if (only.empty() || only == r.name) r.run();
}
