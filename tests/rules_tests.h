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
// pump() runs for wall-clock (test-clock) time; under load the server's own clock can fall behind it (it skips
// ahead rather than spiral). This pumps until the SERVER clock has moved on by `seconds` (at most 4x as long).
template <class F>
static void pumpServerTime(Server& s, std::vector<NetClient*>& cls, double seconds, F input) {
    double end = s.time() + seconds;
    for (int k = 0; k < 400 && s.time() < end; k++) pump(s, cls, std::min(0.05, end - s.time() + 0.001), input);
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
        onIt = false; pumpServerTime(s, cls, 5.5, in);   // (pump times are game seconds)
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
        pumpServerTime(s, cls, 2.5, in);
        CHECK(s.ms_.phase == PHASE_LIVE && (s.ms_.flags & MS_OVERTIME) && s.ms_.timeLeft < 3.0f);
        pumpServerTime(s, cls, 3.0, in);
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
        Vec3 away = s2.map_.spawns[0].pos;
        for (auto& sp : s2.map_.spawns) if (length(sp.pos - cart) > length(away - cart)) away = sp.pos;
        pump(s2, cls2, 0.1, [&](int) { s2.players_[a2].move.pos = cart; return UserCmd(); });
        pumpServerTime(s2, cls2, 1.0, [&](int) { s2.players_[a2].move.pos = away; return UserCmd(); });
        s2.phaseEnd_ = s2.time();
        pump(s2, cls2, 0.2, [&](int) { s2.players_[a2].move.pos = away; return UserCmd(); });
        CHECK(s2.ms_.phase == PHASE_LIVE && (s2.ms_.flags & MS_OVERTIME));
        pumpServerTime(s2, cls2, 4.0, [&](int) { s2.players_[a2].move.pos = away; return UserCmd(); });
        CHECK(s2.ms_.phase != PHASE_LIVE);
        s2.stop();
    }
}

// Bomb: a bomb planted in overtime ends the overtime. The fuse is the clock from then on, and the attackers win when it
// goes off (owner, checked before 1.17.0).
static void testBombOvertimePlant(uint16_t port) {
    printf("bomb planted in overtime\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "depot"; cfg.mode = MODE_BOMB; cfg.port = port; cfg.balanceBots = false;
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls)) { s.stop(); g_fail++; return; }
    const int attTeam = s.ms_.attackers;
    int att = rulesTeamPlayer(s, attTeam), def = rulesTeamPlayer(s, attTeam == TEAM_A ? (int)TEAM_B : (int)TEAM_A);
    if (att < 0 || def < 0) { s.stop(); g_fail++; return; }
    const AABB& site = s.map_.bombSites[0];
    Vec3 onSite = {site.center().x, site.mn.y + 0.05f, site.center().z};
    Vec3 far = s.map_.spawns[0].pos;
    for (auto& sp : s.map_.spawns) if (length(sp.pos - onSite) > length(far - onSite)) far = sp.pos;
    bool onIt = true;
    auto in = [&](int) {
        auto& a = s.players_[att]; auto& d = s.players_[def];
        a.move.pos = onIt ? onSite : far; a.move.vel = {};
        d.move.pos = far; d.move.vel = {};   // (the defender never comes to defuse)
        a.health = d.health = 100;
        return UserCmd();
    };
    pump(s, cls, 0.1, in);
    s.phaseEnd_ = s.time();
    pump(s, cls, 0.5, in);
    CHECK(s.ms_.phase == PHASE_LIVE && (s.ms_.flags & MS_OVERTIME));
    // planted during overtime: overtime is over, the clock is the fuse
    const int scoreBefore = attTeam == TEAM_A ? s.ms_.scoreA : s.ms_.scoreB;
    s.ms_.bombState = BOMB_PLANTED;
    s.ms_.bombPos = onSite;
    s.ms_.bombTimer = 3.0f;
    onIt = false;
    pump(s, cls, 0.2, in);
    printf("  planted in overtime: overtime %d, clock %.2f s (fuse %.2f s)\n", (s.ms_.flags & MS_OVERTIME) != 0, s.ms_.timeLeft, s.ms_.bombTimer);
    CHECK(s.ms_.phase == PHASE_LIVE && !(s.ms_.flags & MS_OVERTIME) && std::fabs(s.ms_.timeLeft - s.ms_.bombTimer) < 0.05f);
    pumpServerTime(s, cls, 3.5, in);
    const int scoreAfter = attTeam == TEAM_A ? s.ms_.scoreA : s.ms_.scoreB;
    CHECK(s.ms_.phase != PHASE_LIVE && s.ms_.bombState == BOMB_EXPLODED && scoreAfter == scoreBefore + 1);
    s.stop();
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

// Owner 1.17 (B4): no special or shared throw cooldown. A knife throw is the knife's normal swing cooldown, a syringe
// throw the syringe's 1 s, during which no weapon can be used (no throw + instant crossbow headshot); the thrown
// weapon is then disabled for 4 s. A teammate's syringe resets all of it.
static void testThrowCooldown(uint16_t port) {
    printf("throws: the weapon's own cooldown, then disabled for 4 s\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_KOTH; cfg.port = port; cfg.balanceBots = false;
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls, false)) { s.stop(); return; }
    c[1].sendCommand("team " + std::string(s.players_[c[0].clientId()].team == TEAM_A ? "a" : "b"));
    pump(s, cls, 2.5, idle);
    auto& pa = s.players_[c[0].clientId()];
    auto& pb = s.players_[c[1].clientId()];
    CHECK(pa.team == pb.team && pa.alive && pb.alive);
    auto count = [&](int type, int owner) { int n = 0; for (auto& pr : s.projectiles_) n += pr.active && pr.type == type && pr.owner == owner; return n; };
    int wa = WPN_KNIFE, wb = WPN_SYRINGE;
    uint16_t ba = 0, bb = 0;
    auto in = [&](int i) { UserCmd u; u.weapon = (uint8_t)(i == 0 ? wa : wb); u.buttons = i == 0 ? ba : bb; u.yaw = 0; u.pitch = -0.05f; return u; };
    pump(s, cls, 0.3, in);
    ba = BTN_ALTFIRE;
    for (int k = 0; k < 20 && count(WPN_KNIFE, c[0].clientId()) == 0; k++) pump(s, cls, 0.02, in);   // throw the knife
    CHECK(count(WPN_KNIFE, c[0].clientId()) == 1);
    double t0 = pa.lastFire;
    printf("  after the knife throw: weapon %d ready in %.2f s, knife back in %.2f s\n", pa.weapon, pa.nextFire - t0, pa.knifeBackAt - t0);
    CHECK(pa.weapon != WPN_KNIFE);   // switched away
    // the knife's swing cooldown, no more: from the throw's command, on the command clock (F22), which is the server
    // clock or up to 0.1 s behind it (here the test's pauses between pumps leave the client's commands short)
    auto fromCmd = [&](double next, double refire, double t) { double c = next - refire; return c <= t + 0.002 && c >= t - 0.1 - 0.002; };
    CHECK(fromCmd(pa.nextFire, weaponDef(WPN_KNIFE).refire, t0));
    CHECK(std::fabs(pa.knifeBackAt - (t0 + 4.0)) < 0.002);                         // the knife: disabled 4 s
    int other = pa.weapon;
    ba = 0; wa = other; pump(s, cls, 0.05, in);              // let go, then fire the weapon we got: nothing until the cooldown is over
    ba = BTN_FIRE;
    while (s.time() < t0 + weaponDef(WPN_KNIFE).refire - 0.1) pump(s, cls, 0.01, in);
    CHECK(pa.lastFire == t0);
    wa = WPN_KNIFE; ba = 0; pump(s, cls, 0.1, in);           // asking for the knife back: refused while it's away
    CHECK(pa.weapon != WPN_KNIFE && c[0].latest().knifeAway > 0);
    // the syringe can be thrown as soon as the knife's swing cooldown is over (no shared throw cooldown)
    while (s.time() < t0 + weaponDef(WPN_KNIFE).refire + 0.05) pump(s, cls, 0.01, in);
    wa = WPN_SYRINGE; pump(s, cls, 0.1, in);
    CHECK(pa.weapon == WPN_SYRINGE);
    ba = BTN_ALTFIRE;
    for (int k = 0; k < 20 && count(WPN_SYRINGE, c[0].clientId()) == 0; k++) pump(s, cls, 0.02, in);
    CHECK(count(WPN_SYRINGE, c[0].clientId()) == 1);
    double t1 = pa.lastFire;
    printf("  after the syringe throw: weapon %d ready in %.2f s, syringe back in %.2f s\n", pa.weapon, pa.nextFire - t1, pa.syringeBackAt - t1);
    CHECK(pa.weapon != WPN_SYRINGE && pa.weapon != WPN_KNIFE);
    CHECK(fromCmd(pa.nextFire, 1.0, t1));                      // the syringe's normal cooldown, now 1 s
    CHECK(std::fabs(pa.syringeBackAt - (t1 + 4.0)) < 0.002);   // the syringe: disabled 4 s
    CHECK(pa.knifeBackAt > s.time());                          // (the knife is still away)
    ba = 0; wa = other; pump(s, cls, 0.05, in);
    ba = BTN_FIRE;
    while (s.time() < t1 + 0.9) pump(s, cls, 0.01, in);
    CHECK(pa.lastFire == t1);                                  // no weapon during the syringe's cooldown
    ba = 0; wa = WPN_SYRINGE; pump(s, cls, 0.1, in);
    CHECK(pa.weapon != WPN_SYRINGE && c[0].latest().syringeAway > 0);
    // b throws a syringe at a (5 m in front): every cooldown resets, the thrown weapons are back
    Vec3 base = openLane(s.map_, pb.move.pos);
    auto place = [&] { pb.move.pos = base; pb.move.vel = {}; pa.move.pos = base + Vec3(0, 0, -5); pa.move.vel = {}; };
    pa.health = 40;
    for (int k = 0; k < 5; k++) { place(); pump(s, cls, 0.02, in); }
    pa.knifeBackAt = pa.syringeBackAt = pa.nextFire = s.time() + 30;   // (so only the reset can clear them)
    int tick = 0;
    pump(s, cls, 0.5, [&](int i) { place(); UserCmd u = in(i); if (i == 1 && tick++ < 30) u.buttons = BTN_ALTFIRE; return u; });
    printf("  healed: %.0f hp, knife back in %.2f s, syringe in %.2f s\n", pa.health, pa.knifeBackAt - s.time(), pa.syringeBackAt - s.time());
    CHECK(pa.health > 60 && pa.knifeBackAt <= s.time() && pa.syringeBackAt <= s.time() && pa.nextFire <= s.time());
    CHECK(pb.syringeBackAt > s.time());   // (the thrower's own syringe is away)
    wa = WPN_SYRINGE; ba = BTN_ALTFIRE;
    for (int k = 0; k < 20 && count(WPN_SYRINGE, c[0].clientId()) == 0; k++) pump(s, cls, 0.02, [&](int i) { place(); return in(i); });
    CHECK(count(WPN_SYRINGE, c[0].clientId()) == 1);   // a can throw right away
    s.stop();
}

// 5.3: size modifiers scale the hull (collision), not just the hitbox and eyes; movement stays tick-rate independent.
static void testSizeHull(uint16_t port) {
    printf("size modifiers scale the hull\n");
    for (int d = 0; d < DUEL_MOD_COUNT; d++) CHECK(moveScale(moveScaleMods(duelModDef(d).size)) == duelModDef(d).size);
    Map m;
    std::string err;
    // a floor, a 2.5 m high slab to walk under at x 3..6, and a wall at x = 10
    CHECK(m.parse("box -20 -1 -20 20 0 20\nbox 3 2.5 -20 6 3 20\nbox 10 0 -20 11 8 20\nspawn any 0 0 0\n", &err));
    auto walk = [&](uint8_t mods, int msec) {
        MoveState s;
        s.pos = {0, 0, 0};
        s.mods = mods;
        for (int t = 0; t < 2500; t += msec) {
            UserCmd c; c.msec = (uint8_t)msec; c.yaw = -PI / 2; c.forward = 127;   // +x
            pmove(s, c, m);
        }
        return s;
    };
    MoveState n = walk(0, 8), big = walk(MOVE_BIG, 8), tiny = walk(MOVE_TINY, 8);
    printf("  walked to x %.2f (normal), %.2f (double size), %.2f (tiny)\n", n.pos.x, big.pos.x, tiny.pos.x);
    CHECK(std::fabs(n.pos.x - (10 - kPlayerHalfWidth)) < 0.02f);              // under the slab, up to the wall
    CHECK(std::fabs(big.pos.x - (3 - kPlayerHalfWidth * kBigScale)) < 0.02f); // a giant's head hits the slab
    CHECK(std::fabs(tiny.pos.x - (10 - kPlayerHalfWidth * kTinyScale)) < 0.02f);
    for (int msec : {1, 2, 4}) {                                               // tick-rate independent, scaled too
        MoveState b2 = walk(MOVE_BIG, msec);
        CHECK(b2.pos.x == big.pos.x && b2.pos.y == big.pos.y);
    }
    CHECK(playerBox(big).mx.y - playerBox(big).mn.y == kPlayerHeight * kBigScale);
    // a duel "double size" life: the server spawns a giant hull, its hitbox is the same box, and it fits the spawn
    for (int mod : {4, 19}) {   // DOUBLE SIZE, TINY PLAYERS
        Server s;
        ServerConfig cfg;
        cfg.map = "clockwork"; cfg.mode = MODE_DUEL; cfg.port = port++; cfg.balanceBots = false;
        NetClient c[2];
        std::vector<NetClient*> cls;
        if (!rulesStart(s, cfg, c, 2, cls, false)) { s.stop(); continue; }
        s.duelUsed_ = ~(1u << mod);   // every other modifier played: the next life is this one
        s.command("forcestart");
        pump(s, cls, 0.3, idle);
        CHECK(s.ms_.duelMod == mod);
        float k = duelModDef(mod).size;
        for (int i = 0; i < 2; i++) {
            auto& p = s.players_[c[i].clientId()];
            CHECK(p.alive && moveScale(p.move.mods) == k);
            AABB hb = s.hitBox(p.move.pos, false), hull = playerBox(p.move);
            CHECK(hb.mn.x == hull.mn.x && hb.mx.y == hull.mx.y && std::fabs(hull.mx.y - hull.mn.y - kPlayerHeight * k) < 1e-4f);
            CHECK(!s.map_.boxSolid(hull));   // spawned with room for the hull
        }
        // the client predicts with the same hull: its predicted state carries the size
        pump(s, cls, 0.5, idle);
        CHECK(moveScale(c[0].predicted().mods) == k);
        s.stop();
    }
}

// 5.4: the grapple doesn't brake you when it catches: faster than its top speed, the pull only bends your path.
static void testGrappleNoBrake() {
    printf("grapple never brakes\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -200 -1 -200 200 0 200\nbox -200 20 -200 200 21 200\nspawn any 0 0 0\n", &err));   // floor, ceiling at 20 m
    for (float v0 : {40.0f, 5.0f}) {
        MoveState s;
        s.pos = {0, 5, 0};
        s.vel = {v0, 0, 0};
        UserCmd c; c.msec = 1; c.yaw = -PI / 2; c.pitch = 1.2f; c.buttons = BTN_GRAPPLE;   // look up at the ceiling, hook it
        float lowest = 1e9f, highest = 0;
        for (int t = 0; t < 400; t++) {
            pmove(s, c, m);
            if (!(s.flags & MF_HOOKED)) break;
            lowest = std::min(lowest, length(s.vel));
            highest = std::max(highest, length(s.vel));
        }
        printf("  from %.0f m/s: speed while hooked %.1f..%.1f m/s\n", v0, lowest, highest);
        CHECK(lowest < 1e8f);   // (it caught)
        if (v0 > 24) CHECK(lowest >= v0 - 3.0f);   // no brake on the catch (it used to clamp to 24 at once; what's lost is gravity on the way up)
        else CHECK(highest <= 24.0f + 1e-3f && highest > v0 + 1);   // the pull itself still tops out at 24
    }
}

// The grapple latches onto players (owner: "if the tip of the hook hits a player it sticks to them, 10 damage, 15 if
// head, then pulls both players together in the middle"). pmove alone: the nearest of the map and the players catches it.
static UserCmd hookAim(const Vec3& eye, const Vec3& tgt, uint16_t buttons) {
    Vec3 d = normalize(tgt - eye);
    UserCmd u; u.msec = 1; u.pitch = std::asin(d.y); u.yaw = std::atan2(-d.x, -d.z); u.buttons = buttons; u.weapon = WPN_RIFLE;
    return u;
}
static void testGrapplePmove() {
    printf("grapple latches onto players: the hook's ray\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -50 -1 -50 50 0 50\nbox -0.5 0 -20 0.5 6 20\nspawn any 8 0 0\n", &err));   // a floor, a wall across x = 0
    const Vec3 shooter(8, 0, 0), eye = shooter + Vec3(0, kEyeHeight, 0);
    struct Case { Vec3 feet; bool aimHead; int hit; bool head; };
    for (Case cs : {Case{{3, 0, 0}, false, 0, false}, Case{{3, 0, 0}, true, 0, true}, Case{{-3, 0, 0}, false, -1, false}}) {
        MoveState s; s.pos = shooter;
        HookProbe hp;
        hp.box[0] = playerBox(cs.feet, false); hp.id[0] = 5; hp.n = 1;
        Vec3 tgt = cs.aimHead ? headBox(hp.box[0]).center() : cs.feet + Vec3(0, 1.0f, 0);
        pmove(s, hookAim(eye, tgt, BTN_GRAPPLE), m, &hp);
        CHECK(s.flags & MF_HOOKED);   // (the player, or the wall in front of the one behind it)
        CHECK(hp.hit == cs.hit && hp.head == cs.head);
        CHECK(s.hookTarget == (cs.hit >= 0 ? 6 : 0));
        if (cs.hit < 0) CHECK(std::fabs(s.hookPoint.x - 0.5f) < 0.01f);   // caught on the wall: the player behind it is safe
    }
    // held by someone's grapple: pulled toward them (momentum kept, half gravity); a fresh dash breaks free
    MoveState v; v.pos = {3, 0, 10}; v.flags = MF_PULLED; v.pullPoint = {13, 1, 10};
    UserCmd idleCmd; idleCmd.msec = 1;
    for (int k = 0; k < 100; k++) pmove(v, idleCmd, m);
    printf("  pulled for 0.1 s: %.2f m/s toward the puller, %.2f m\n", v.vel.x, v.pos.x - 3);
    CHECK((v.flags & MF_PULLED) && v.vel.x > 3.0f && v.pos.x > 3.1f && !(v.flags & MF_ONGROUND));
    UserCmd dash = idleCmd; dash.buttons = BTN_DASH; dash.side = 127;
    pmove(v, dash, m);
    CHECK(!(v.flags & MF_PULLED) && v.dashTimer > 0);
    MoveState w; w.pos = {3, 0, 10}; w.flags = MF_PULLED; w.pullPoint = {13, 1, 10};   // ...or a grab of your own
    pmove(w, hookAim(w.pos + Vec3(0, kEyeHeight, 0), Vec3(0.5f, 3, 10), BTN_GRAPPLE), m);
    CHECK((w.flags & MF_HOOKED) && !(w.flags & MF_PULLED));
}

static void testGrapplePlayers(uint16_t port) {
    printf("grapple latches onto players: in play\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port; cfg.autoStart = false; cfg.balanceBots = false;   // (warmup: free play)
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls, false)) { s.stop(); return; }
    pump(s, cls, 1.0, idle);
    const int ia = c[0].clientId(), ib = c[1].clientId();
    auto& sa = s.players_[ia];
    auto& sb = s.players_[ib];
    const Vec3 A0(10, 0, 19), B0(-10, 0, 19);
    bool pin = true;
    uint16_t btn = 0;
    Vec3 aimAt = B0 + Vec3(0, 1.0f, 0);
    float maxErr[2] = {};   // the largest prediction correction being smoothed out (attacker, victim) while latched
    auto in = [&](int i) {
        if (pin) { sa.move.pos = A0; sb.move.pos = B0; sa.move.vel = sb.move.vel = {}; }
        if (sa.hookVictim == ib) maxErr[i] = std::max(maxErr[i], length(c[i].smoothedPos(1.0f) - c[i].predicted().pos));
        UserCmd u; u.weapon = WPN_RIFLE;
        if (i != 0) return u;
        return hookAim(A0 + Vec3(0, kEyeHeight, 0), aimAt, btn);
    };
    auto latch = [&]() {   // fire from A0 at aimAt; true once it caught them
        for (int k = 0; k < 50 && sa.hookVictim != ib; k++) pump(s, cls, 0.01, in);
        return sa.hookVictim == ib;
    };
    auto rearm = [&]() {   // let go, wait out the cooldown, back in place (alive)
        btn = 0; pin = true;
        for (int k = 0; k < 80 && !sb.alive; k++) pump(s, cls, 0.1, in);
        pumpServerTime(s, cls, 1.2, in);
        sb.health = 100;
        c[0].takeEvents();
    };
    // 1. a body hit: 10 damage, both pulled toward the middle until they meet
    rearm();
    btn = BTN_GRAPPLE;
    CHECK(latch());
    printf("  body: %.0f damage, attacker hooked %d (target %d), victim pulled %d\n", 100 - sb.health, (sa.move.flags & MF_HOOKED) != 0,
           sa.move.hookTarget - 1, (sb.move.flags & MF_PULLED) != 0);
    CHECK(std::fabs(100 - sb.health - kGrappleDamage) < 1e-3f);
    CHECK((sa.move.flags & MF_HOOKED) && sa.move.hookTarget == ib + 1 && (sb.move.flags & MF_PULLED) && sb.hookedBy == ia);
    pin = false;
    const double caught = s.time();
    Vec3 mid0 = (A0 + B0) * 0.5f;
    pumpServerTime(s, cls, 0.12, in);
    Vec3 pa = sa.move.pos, pb = sb.move.pos;
    printf("  after 0.12 s: attacker %.2f -> %.2f, victim %.2f -> %.2f (middle %.2f)\n", A0.x, pa.x, B0.x, pb.x, (pa.x + pb.x) * 0.5f);
    CHECK(pa.x < A0.x - 0.1f && pb.x > B0.x + 0.1f && std::fabs((pa.x + pb.x) * 0.5f - mid0.x) < 1.0f);
    CHECK(sa.hookVictim == ib);
    for (int k = 0; k < 150 && sa.hookVictim == ib; k++) pump(s, cls, 0.01, in);
    Vec3 gap = (sa.move.pos + Vec3(0, 1, 0)) - (sb.move.pos + Vec3(0, 1, 0));
    printf("  let go after %.2f s, %.2f m apart; prediction corrections while latched: attacker %.3f m, victim %.3f m\n",
           s.time() - caught, length(gap), maxErr[0], maxErr[1]);
    CHECK(sa.hookVictim < 0 && sb.hookedBy < 0 && !(sb.move.flags & MF_PULLED) && !(sa.move.flags & MF_HOOKED));
    CHECK(length(gap) < 2.5f && s.time() - caught < kGrappleLatchMax);   // they met (not the time limit)
    CHECK(sa.move.hookCooldown > 0);
    CHECK(maxErr[0] < 0.15f && maxErr[1] < 0.15f);
    pumpServerTime(s, cls, 0.3, [&](int i) { btn = 0; return in(i); });
    CHECK(length(sa.move.pos - c[0].predicted().pos) < 0.05f && length(sb.move.pos - c[1].predicted().pos) < 0.05f);
    // 2. through the head: 15 (a headshot hit event); letting go of the button ends it
    rearm();
    aimAt = headBox(s.hitBox(B0, false)).center();
    btn = BTN_GRAPPLE;
    CHECK(latch());
    pump(s, cls, 0.1, in);
    int heads = 0;
    for (auto& e : c[0].takeEvents()) if (e.type == EV_HIT && e.b == ib && e.text == "h") heads++;
    printf("  head: %.0f damage, %d headshot hit event(s)\n", 100 - sb.health, heads);
    CHECK(std::fabs(100 - sb.health - kGrappleHeadDamage) < 1e-3f && heads == 1);
    btn = 0;
    pump(s, cls, 0.05, in);
    CHECK(sa.hookVictim < 0 && !(sb.move.flags & MF_PULLED) && !(sa.move.flags & MF_HOOKED));
    aimAt = B0 + Vec3(0, 1.0f, 0);
    // 3. a catch that kills: the kill feed says grapple, nothing is held
    rearm();
    sb.health = 8;
    btn = BTN_GRAPPLE;
    for (int k = 0; k < 50 && sb.alive; k++) pump(s, cls, 0.01, in);
    pump(s, cls, 0.1, in);
    int grappleKills = 0;
    for (auto& e : c[0].takeEvents()) if (e.type == EV_KILL && e.b == ib && (e.c & KILL_WEAPON) == kDamageGrapple) grappleKills++;
    CHECK(!sb.alive && grappleKills == 1 && sa.hookVictim < 0 && !(sa.move.flags & MF_HOOKED));
    // 4. a death while latched lets go (with the cooldown)
    rearm();
    btn = BTN_GRAPPLE;
    CHECK(latch());
    s.killPlayer(ib, -1, 252);
    pump(s, cls, 0.05, in);
    CHECK(sa.hookVictim < 0 && !(sa.move.flags & MF_HOOKED) && sa.move.hookCooldown > 0);
    // 5. the map comes between them: it lets go
    rearm();
    btn = BTN_GRAPPLE;
    CHECK(latch());
    Vec3 hidden;
    bool found = false;
    for (auto& sp : s.map_.spawns) {
        Vec3 from = A0 + Vec3(0, 1, 0), to = sp.pos + Vec3(0, 1, 0), d = to - from;
        if (length(d) < 30 && s.map_.raycast(from, normalize(d), length(d)) >= 0) { hidden = sp.pos; found = true; break; }
    }
    CHECK(found);
    pin = false;
    sa.move.pos = A0; sb.move.pos = hidden; sa.move.vel = sb.move.vel = {};
    pump(s, cls, 0.05, in);
    CHECK(sa.hookVictim < 0 && !(sb.move.flags & MF_PULLED));
    s.stop();
}

// Teammates let the hook through (it goes on to the wall); enemies are caught.
static void testGrappleTeammate(uint16_t port) {
    printf("grapple latches onto players: teammates let it through\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_VS; cfg.port = port; cfg.autoStart = false; cfg.balanceBots = false;
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls, false)) { s.stop(); return; }
    pump(s, cls, 1.0, idle);
    const int ia = c[0].clientId(), ib = c[1].clientId();
    auto& sa = s.players_[ia];
    auto& sb = s.players_[ib];
    const Vec3 A0(10, 0, 19), B0(-10, 0, 19);
    for (bool sameTeam : {true, false}) {
        sb.team = sameTeam ? sa.team : (sa.team == TEAM_A ? TEAM_B : TEAM_A);
        auto in = [&](int i, uint16_t b) {
            sa.move.pos = A0; sb.move.pos = B0; sa.move.vel = sb.move.vel = {};
            if (i != 0) return UserCmd();
            return hookAim(A0 + Vec3(0, kEyeHeight, 0), B0 + Vec3(0, 1.0f, 0), b);
        };
        pumpServerTime(s, cls, 1.2, [&](int i) { return in(i, 0); });
        if (!sb.alive || !sa.alive) { CHECK(sa.alive && sb.alive); continue; }
        sb.health = 100;
        pump(s, cls, 0.2, [&](int i) { return in(i, BTN_GRAPPLE); });
        printf("  %s: latched %d, %.0f damage, hook on the map %d\n", sameTeam ? "teammate" : "enemy", sa.hookVictim == ib, 100 - sb.health,
               (sa.move.flags & MF_HOOKED) && !sa.move.hookTarget);
        if (sameTeam) CHECK(sa.hookVictim < 0 && sb.health == 100 && !(sb.move.flags & MF_PULLED));
        else CHECK(sa.hookVictim == ib && std::fabs(100 - sb.health - kGrappleDamage) < 1e-3f);
        pump(s, cls, 0.05, [&](int i) { return in(i, 0); });
    }
    s.stop();
}

// Expert bots with a close-range weapon hook their target now and then (and nothing breaks while they're latched).
static void testGrappleBots(uint16_t port) {
    printf("grapple latches onto players: bots\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "";   // a map without pits or lava (there the bots only hook when the pull ends on safe ground)
    for (const char* name : {"downtown", "railyard"}) {
        Map m;
        if (m.load(name) && !m.hasVoid && m.lava.empty()) { cfg.map = name; break; }
    }
    CHECK(!cfg.map.empty());
    if (cfg.map.empty()) return;
    cfg.mode = MODE_FFA; cfg.port = port; cfg.autoStart = false; cfg.balanceBots = false;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    int bot[2] = {s.addBot(TEAM_A, BOT_EXPERT), s.addBot(TEAM_A, BOT_EXPERT)};
    for (int k : bot) if (k >= 0) s.bots_[k].forceWeapon = WPN_SHOTGUN;
    std::vector<NetClient*> none;
    int latches = 0, bad = 0;
    bool was[2] = {};
    // Until the first latch and 5 s on (nothing breaks while latched), at most 90 s: about one latch in 30 s, so a
    // fixed 30 s sometimes saw none (CI, and a busy PC; the build before 1.18.0 too)
    double firstAt = -1;
    for (int t = 0; t < 1800 && (firstAt < 0 || t * 0.05 < firstAt + 5); t++) {
        if (latches && firstAt < 0) firstAt = t * 0.05;
        pump(s, none, 0.05, idle);
        for (int k = 0; k < 2; k++) {
            if (bot[k] < 0) continue;
            const ServerPlayer& p = s.players_[bot[k]];
            bool l = p.hookVictim >= 0;
            latches += l && !was[k];
            was[k] = l;
            if (l && !(p.move.flags & MF_HOOKED)) bad++;
        }
    }
    printf("  2 expert bots with shotguns on %s: %d latch(es), the first after %.1f s\n", cfg.map.c_str(), latches, firstAt);
    CHECK(bot[0] >= 0 && bot[1] >= 0 && latches >= 1 && bad == 0);
    s.stop();
}

// Lag compensated like hitscan: aimed at where the shooter sees a mover (200 ms of interpolation), it catches.
static void testGrappleLagComp(uint16_t port) {
    printf("grapple latches onto players: lag compensated\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = false;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.setInterpFixed(kMaxInterpMs * 0.001f);
    a.connect("127.0.0.1:" + std::to_string(cfg.port), "hooker");
    b.connect("127.0.0.1:" + std::to_string(cfg.port), "mover");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 2.5, idle);
    auto& sa = s.players_[a.clientId()];
    auto& sb = s.players_[b.clientId()];
    auto moverAt = [](double t) { double ph = std::fmod(t, 1.0); return Vec3(-5, 0, 19 + 2.5f * (float)(ph < 0.5 ? ph * 4 - 1 : 3 - ph * 4)); };
    const Vec3 shooterPos(10, 0, 19);
    uint16_t btn = 0;
    int missedIfNow = 0;   // catches the shooter's aim would have missed at the mover's current position (rewind needed)
    auto input = [&](int i) {
        sa.move.pos = shooterPos; sa.move.vel = {};
        sb.move.pos = moverAt(s.time()); sb.move.vel = {}; sb.health = 100;
        UserCmd c; c.weapon = WPN_RIFLE;
        if (i != 0) return c;
        NetClient::PlayerArray ps = a.interpolatedPlayers();   // where the shooter sees the mover
        const InterpPlayer& ip = ps[b.clientId()];
        if (!ip.valid) return c;
        return hookAim(shooterPos + Vec3(0, kEyeHeight, 0), ip.snap.pos + Vec3(0, 1.0f, 0), btn);
    };
    pump(s, cls, 0.6, input);
    int tries = 0, caught = 0;
    for (int k = 0; k < 6; k++) {
        btn = 0;
        pumpServerTime(s, cls, 1.15, input);
        btn = BTN_GRAPPLE;
        tries++;
        bool got = false;
        for (int j = 0; j < 15 && !got; j++) {
            pump(s, cls, 0.01, input);
            got = sa.hookVictim == b.clientId();
            if (got) {
                UserCmd u = input(0);
                Vec3 eye = shooterPos + Vec3(0, kEyeHeight, 0);
                if (rayAABB(eye, safeInv(angleForward(u.yaw, u.pitch)), s.hitBox(sb.move.pos, false), 100.0f) < 0) missedIfNow++;
            }
        }
        caught += got;
    }
    printf("  interp 200: %d of %d hooks caught a player moving at 10 m/s (%d of them aimed where the mover no longer was)\n", caught, tries, missedIfNow);
    CHECK(caught >= tries - 1);
    s.stop();
}

// 5.5: the head is a head-sized box on top, not the full-width top 20%: shoulders are body shots. The sniper does 40
// on the body, 50 on the head (owner 1.18; 1.17 a flat 50).
static void testHeadBox(uint16_t port) {
    printf("headshots: the head, not the shoulders\n");
    CHECK(weaponDef(WPN_SNIPER).damage == 40 && headDamage(WPN_SNIPER) == 50);
    AABB hb = playerBox(Vec3(0, 0, 0), false);
    AABB h = headBox(hb);
    CHECK(h.mx.y == hb.mx.y && std::fabs(h.mn.y - (hb.mx.y - kPlayerHeight * kHeadFraction)) < 1e-5f);
    CHECK(h.mx.x - h.mn.x < (hb.mx.x - hb.mn.x) * 0.5f && std::fabs(h.center().x) < 1e-6f);
    Vec3 o(0, 1.62f, 10);   // level shots from 10 m in front (+z), at head height
    CHECK(rayHitsHead(o, normalize(Vec3(0, 1.62f, 0) - o), hb));          // the middle of the head
    CHECK(!rayHitsHead(o + Vec3(0.3f, 0, 0), Vec3(0, 0, -1), hb));       // the shoulder, at the same height
    CHECK(!rayHitsHead(Vec3(0.3f, 5, 0), Vec3(0, -1, 0), hb));           // straight down onto the shoulder
    CHECK(rayHitsHead(Vec3(0.05f, 5, 0), Vec3(0, -1, 0), hb));           // ...or onto the head
    CHECK(!rayHitsHead(o + Vec3(0, -0.6f, 0), Vec3(0, 0, -1), hb));      // the chest
    // in play: rifle bullets at the shoulder are body shots
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls, false)) { s.stop(); return; }
    pump(s, cls, 2.0, idle);
    auto& sa = s.players_[c[0].clientId()];
    auto& sb = s.players_[c[1].clientId()];
    for (float side : {0.0f, 0.3f}) {   // the head, then the shoulder
        sa.move.pos = {10, 0, 19}; sb.move.pos = {-10, 0, 19}; sa.move.vel = sb.move.vel = {};
        sb.health = 100;
        auto aim = [&](int i, uint16_t b) {
            UserCmd u; if (i != 0) return u;
            Vec3 eye = sa.move.pos + Vec3(0, kEyeHeight, 0), tgt = sb.move.pos + Vec3(0, 1.62f, side);
            Vec3 d = normalize(tgt - eye);
            u.pitch = std::asin(d.y); u.yaw = std::atan2(-d.x, -d.z); u.weapon = WPN_RIFLE; u.buttons = b;
            return u;
        };
        pump(s, cls, 0.4, [&](int i) { sb.move.pos = {-10, 0, 19}; return aim(i, 0); });
        c[0].takeEvents();
        float hp0 = sb.health;
        int d0 = sb.deaths;
        pump(s, cls, 0.25, [&](int i) { sb.move.pos = {-10, 0, 19}; return aim(i, BTN_FIRE); });
        float dealt = hp0 - sb.health;
        float per = side == 0 ? kRifleHeadDamage : weaponDef(WPN_RIFLE).damage;
        float n = dealt / per;
        printf("  rifle at the %s: %.2f damage (%.2f bullets of %.2f)\n", side == 0 ? "head" : "shoulder", dealt, n, per);
        CHECK(sb.deaths == d0 && dealt > 0 && std::fabs(n - std::round(n)) < 0.02f);
        pump(s, cls, 0.2, idle);
        int heads = 0, bodies = 0;   // the hit events: headshots marked "h" (the client draws them red)
        for (auto& e : c[0].takeEvents()) if (e.type == EV_HIT && e.b == c[1].clientId()) (e.text == "h" ? heads : bodies)++;
        printf("  hit events: %d head, %d body\n", heads, bodies);
        CHECK(side == 0 ? (heads > 0 && bodies == 0) : (heads == 0 && bodies > 0));
    }
    s.stop();
}

// 5.6 / B8: hide and seek. Everyone seeks once (rounds = players), a hider unfound when the round ends scores 1,
// the seeker 1 per hider found, a found hider 0, and a fall 0 for the hider and nothing for the seeker.
static void testHideScoring(uint16_t port) {
    printf("hide and seek: everyone seeks once, 1 per find, 1 for staying hidden\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "hamlet"; cfg.mode = MODE_HIDE; cfg.port = port;
    NetClient c[3];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 3, cls, false)) { s.stop(); return; }
    s.command("forcestart");
    pump(s, cls, 0.2, idle);
    auto itNow = [&] { for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].it) return i; return -1; };
    auto score = [&](int i) { return s.players_[i].score; };
    auto stab = [&](int from, int to) {            // stand 1 m apart, the seeker facing the hider
        for (int k = 0; k < 40 && s.players_[to].alive; k++) {
            s.players_[from].move.pos = {0, 0, 6}; s.players_[from].move.vel = {};
            s.players_[to].move.pos = {0, 0, 5}; s.players_[to].move.vel = {};
            pump(s, cls, 0.05, [&](int i) { UserCmd u; u.yaw = 0; u.pitch = -0.3f; if (cls[i]->clientId() == from) u.buttons = BTN_FIRE; return u; });
        }
    };
    auto goLive = [&] { s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle); CHECK(s.ms_.phase == PHASE_LIVE); };
    int seekers[3] = {-1, -1, -1};
    // round 1: the seeker finds one hider, the other falls off the map
    seekers[0] = itNow();
    CHECK(seekers[0] >= 0);
    if (seekers[0] < 0) { s.stop(); return; }
    int h[2], n = 0;
    for (auto* cl : cls) if (cl->clientId() != seekers[0]) h[n++] = cl->clientId();
    goLive();
    stab(seekers[0], h[0]);
    CHECK(!s.players_[h[0]].alive && s.players_[h[0]].found);
    s.players_[h[1]].move.pos.y = s.map_.killY() - 5;   // a fall
    pump(s, cls, 0.2, idle);
    printf("  round 1: seeker %d, found %d, fell %d\n", score(seekers[0]), score(h[0]), score(h[1]));
    CHECK(s.ms_.phase == PHASE_ROUND_END && score(seekers[0]) == 1 && score(h[0]) == 0 && score(h[1]) == 0);
    // round 2: nobody found, time runs out: both hiders +1, the seeker nothing
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    seekers[1] = itNow();
    CHECK(s.ms_.round == 2 && seekers[1] >= 0 && seekers[1] != seekers[0]);
    goLive();
    int before[kMaxPlayers];
    for (int i = 0; i < kMaxPlayers; i++) before[i] = score(i);
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    for (auto* cl : cls) {
        int i = cl->clientId();
        CHECK(score(i) - before[i] == (i == seekers[1] ? 0 : 1));
    }
    CHECK(s.ms_.phase == PHASE_ROUND_END);
    // round 3: the last player seeks and finds both: +2
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    seekers[2] = itNow();
    CHECK(s.ms_.round == 3 && seekers[2] >= 0 && seekers[2] != seekers[0] && seekers[2] != seekers[1]);
    goLive();
    for (int i = 0; i < kMaxPlayers; i++) before[i] = score(i);
    for (auto* cl : cls) if (cl->clientId() != seekers[2]) { s.players_[seekers[2]].itFreezeUntil = 0; stab(seekers[2], cl->clientId()); }
    pump(s, cls, 0.2, idle);
    CHECK(s.ms_.phase == PHASE_ROUND_END && score(seekers[2]) - before[seekers[2]] == 2);
    for (auto* cl : cls) if (cl->clientId() != seekers[2]) CHECK(score(cl->clientId()) == before[cl->clientId()]);
    printf("  totals: %d %d %d\n", score(seekers[0]), score(seekers[1]), score(seekers[2]));
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);   // three players, three rounds: over
    CHECK(s.ms_.phase == PHASE_MATCH_END && s.ms_.round == 3);
    s.stop();
}

// B8: hide and seek ends tied for first -> rock paper scissors among the tied, synced to every client. `n` humans
// (all tied); the server and the clients see the same throws. Returns false if the setup failed.
static bool rpsSetup(Server& s, ServerConfig cfg, NetClient* c, int n, std::vector<NetClient*>& cls, const std::vector<double>& hidden = {}) {
    cfg.map = "hamlet"; cfg.mode = MODE_HIDE;
    if (!rulesStart(s, cfg, c, n, cls, false)) return false;
    s.command("forcestart");
    pump(s, cls, 0.2, idle);
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);   // live
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);   // time's up: round end
    CHECK(s.ms_.phase == PHASE_ROUND_END);
    for (auto& p : s.players_) if (p.connected) { p.sought = true; p.score = 3; p.hideSurvived = 0; }   // every round played, all on 3
    for (size_t k = 0; k < hidden.size() && k < (size_t)n; k++) s.players_[c[k].clientId()].hideSurvived = hidden[k];   // (the time tiebreak, T23)
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    if (!hidden.empty()) return true;   // (the caller checks what the time decided)
    CHECK(s.ms_.phase == PHASE_ROUND_END && (s.ms_.flags & MS_RPS) && s.rps_.stage == RPS_PICK && s.rps_.throwNo == 1);
    for (auto* cl : cls) CHECK(cl->mode().flags & MS_RPS);
    return (s.ms_.flags & MS_RPS) != 0;
}
// Every client throws (shape per client, 0 = no pick), then the reveal; returns the reveal each client was sent.
static std::vector<GameEvent> rpsThrow(Server& s, std::vector<NetClient*>& cls, const std::vector<int>& shapes) {
    for (auto* cl : cls) cl->takeEvents();
    for (size_t i = 0; i < cls.size(); i++) if (shapes[i]) cls[i]->sendCommand(std::string("rps ") + rpsName(shapes[i]));
    pump(s, cls, 0.2, idle);
    if (s.rps_.stage == RPS_PICK) { s.rps_.stageEnd = s.time(); pump(s, cls, 0.1, idle); }   // (someone didn't pick: the clock runs out)
    pump(s, cls, 0.1, idle);
    std::vector<GameEvent> got;
    for (auto* cl : cls) {
        GameEvent r; r.type = 255;
        for (auto& e : cl->takeEvents()) if (e.type == EV_RPS && e.a == RPS_REVEAL) r = e;
        got.push_back(r);
    }
    return got;
}
static void rpsNext(Server& s, std::vector<NetClient*>& cls) { s.rps_.stageEnd = s.time(); pump(s, cls, 0.1, idle); }   // (past the reveal)
static int rpsShapeIn(const GameEvent& e, int id) {   // a player's shape in an EV_RPS text
    std::istringstream ss(e.text.substr(0, e.text.find('\n')));
    std::string t; ss >> t >> t;
    while (ss >> t) { int i, sh, w, o; if (sscanf(t.c_str(), "%d:%d:%d:%d", &i, &sh, &w, &o) == 4 && i == id) return sh; }
    return -1;
}

// T23: hide and seek tied on score: the most time hidden wins first; only the ones level on that too play rock paper
// scissors.
static void testHideTimeTiebreak(uint16_t port) {
    printf("hide and seek tiebreak: time hidden first, then rock paper scissors\n");
    {
        Server s; ServerConfig cfg; cfg.port = port; NetClient c[3]; std::vector<NetClient*> cls;
        if (!rpsSetup(s, cfg, c, 3, cls, {50, 10, 20})) { s.stop(); return; }
        printf("  3 tied on score, hidden 50 / 10 / 20 s: %s\n", s.ms_.phase == PHASE_MATCH_END ? "match over, no rock paper scissors" : "still going");
        CHECK(s.ms_.phase == PHASE_MATCH_END && !(s.ms_.flags & MS_RPS));
        s.stop();
    }
    {
        Server s; ServerConfig cfg; cfg.port = (uint16_t)(port + 1); NetClient c[3]; std::vector<NetClient*> cls;
        if (!rpsSetup(s, cfg, c, 3, cls, {40, 40, 20})) { s.stop(); return; }
        int in = 0; for (int i = 0; i < kMaxPlayers; i++) in += s.rps_.in >> i & 1;
        CHECK((s.ms_.flags & MS_RPS) && in == 2 && !(s.rps_.in >> c[2].clientId() & 1) && s.rps_.bestOf3);
        s.stop();
    }
}
static void testRpsTwo(uint16_t port) {
    printf("hide and seek tiebreak: two tied, best of 3, a draw is thrown again\n");
    Server s;
    ServerConfig cfg; cfg.port = port;
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rpsSetup(s, cfg, c, 2, cls)) { s.stop(); return; }
    int a = c[0].clientId(), b = c[1].clientId();
    CHECK(s.rps_.bestOf3 && (s.rps_.in >> a & 1) && (s.rps_.in >> b & 1));
    auto ev = rpsThrow(s, cls, {RPS_ROCK, RPS_ROCK});   // a draw: nobody scores, throw again
    CHECK(s.rps_.stage == RPS_REVEAL && s.rps_.wins[a] == 0 && s.rps_.wins[b] == 0 && s.rps_.winner < 0);
    for (auto& e : ev) CHECK(e.type == EV_RPS && rpsShapeIn(e, a) == RPS_ROCK && rpsShapeIn(e, b) == RPS_ROCK);   // everyone saw the same reveal
    rpsNext(s, cls);
    CHECK(s.rps_.stage == RPS_PICK && s.rps_.throwNo == 2);
    rpsThrow(s, cls, {RPS_PAPER, RPS_ROCK});            // a 1-0
    CHECK(s.rps_.wins[a] == 1 && s.rps_.wins[b] == 0);
    rpsNext(s, cls);
    rpsThrow(s, cls, {RPS_SCISSORS, 0});                // b doesn't pick: a random one
    CHECK(s.rps_.pick[b] >= RPS_ROCK && s.rps_.pick[b] <= RPS_SCISSORS);
    int wa = s.rps_.wins[a], wb = s.rps_.wins[b];
    printf("  throw 3: %s vs %s (random) -> %d-%d\n", rpsName(s.rps_.pick[a]), rpsName(s.rps_.pick[b]), wa, wb);
    for (int k = 0; k < 6 && s.rps_.winner < 0; k++) { rpsNext(s, cls); rpsThrow(s, cls, {RPS_ROCK, RPS_SCISSORS}); }   // a wins the rest
    CHECK(s.rps_.winner == a);
    for (auto* cl : cls) cl->takeEvents();
    rpsNext(s, cls);
    CHECK(s.ms_.phase == PHASE_MATCH_END && !(s.ms_.flags & MS_RPS));
    bool told = false;
    for (auto& e : c[1].takeEvents()) told |= e.type == EV_ROUND_WIN && e.b == 1 && e.a == a;
    CHECK(told);   // the tiebreak's winner is the match winner
    s.stop();
}

static void testRpsMany(uint16_t port) {
    printf("hide and seek tiebreak: three or more throw at once, the losing shape is out\n");
    {   // a clear loser: two shapes, the scissors are out; two left play best of 3
        Server s;
        ServerConfig cfg; cfg.port = port;
        NetClient c[3];
        std::vector<NetClient*> cls;
        if (!rpsSetup(s, cfg, c, 3, cls)) { s.stop(); return; }
        CHECK(!s.rps_.bestOf3);
        int x = c[2].clientId();
        rpsThrow(s, cls, {RPS_ROCK, RPS_ROCK, RPS_SCISSORS});
        CHECK((s.rps_.out >> x & 1) && s.rps_.bestOf3 && s.rps_.winner < 0);
        rpsNext(s, cls);
        CHECK(s.rps_.stage == RPS_PICK && s.rps_.throwNo == 2);
        c[2].sendCommand("rps paper");   // out: no say any more
        rpsThrow(s, cls, {RPS_PAPER, RPS_ROCK, 0});
        CHECK(s.rps_.pick[x] == RPS_NONE && s.rps_.wins[c[0].clientId()] == 1);
        rpsNext(s, cls);
        rpsThrow(s, cls, {RPS_PAPER, RPS_ROCK, 0});
        CHECK(s.rps_.winner == c[0].clientId());
        rpsNext(s, cls);
        CHECK(s.ms_.phase == PHASE_MATCH_END);
        s.stop();
    }
    {   // nobody clearly loses (all the same, or all three shapes): nobody is out, throw again; one clear winner takes it
        Server s;
        ServerConfig cfg; cfg.port = (uint16_t)(port + 1);
        NetClient c[4];
        std::vector<NetClient*> cls;
        if (!rpsSetup(s, cfg, c, 4, cls)) { s.stop(); return; }
        rpsThrow(s, cls, {RPS_ROCK, RPS_ROCK, RPS_ROCK, RPS_ROCK});
        CHECK(s.rps_.out == 0 && s.rps_.winner < 0 && s.rps_.result.find("nobody") != std::string::npos);
        rpsNext(s, cls);
        CHECK(s.rps_.stage == RPS_PICK && s.rps_.throwNo == 2);
        rpsThrow(s, cls, {RPS_ROCK, RPS_PAPER, RPS_SCISSORS, RPS_ROCK});
        CHECK(s.rps_.out == 0 && s.rps_.winner < 0);
        rpsNext(s, cls);
        rpsThrow(s, cls, {RPS_SCISSORS, RPS_PAPER, RPS_PAPER, RPS_PAPER});   // one beats everyone: wins it outright
        CHECK(s.rps_.winner == c[0].clientId());
        rpsNext(s, cls);
        CHECK(s.ms_.phase == PHASE_MATCH_END);
        s.stop();
    }
    {   // a bot in the tie throws at random on its own; nobody tied, no tiebreak
        Server s;
        ServerConfig cfg; cfg.port = (uint16_t)(port + 2);
        cfg.map = "hamlet"; cfg.mode = MODE_HIDE;
        NetClient c[1];
        std::vector<NetClient*> cls;
        if (!rulesStart(s, cfg, c, 1, cls, false)) { s.stop(); return; }
        int bot = s.addBot(TEAM_A, 1);
        pump(s, cls, 0.2, idle);
        s.command("forcestart");
        pump(s, cls, 0.2, idle);
        s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
        s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
        for (auto& p : s.players_) if (p.connected) { p.sought = true; p.score = 2; p.hideSurvived = 0; }   // (level on time too)
        s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
        CHECK((s.ms_.flags & MS_RPS) && bot >= 0 && s.rps_.pick[bot] >= RPS_ROCK && s.rps_.pick[bot] <= RPS_SCISSORS);
        s.stop();
        Server t;
        ServerConfig cfg2; cfg2.port = (uint16_t)(port + 3);
        cfg2.map = "hamlet"; cfg2.mode = MODE_HIDE;
        NetClient d[2];
        std::vector<NetClient*> dls;
        if (!rulesStart(t, cfg2, d, 2, dls, false)) { t.stop(); return; }
        t.command("forcestart");
        pump(t, dls, 0.2, idle);
        t.phaseEnd_ = t.time(); pump(t, dls, 0.1, idle);
        t.phaseEnd_ = t.time(); pump(t, dls, 0.1, idle);
        for (auto& p : t.players_) if (p.connected) { p.sought = true; p.score = 2; }
        t.players_[d[1].clientId()].score = 1;
        t.phaseEnd_ = t.time(); pump(t, dls, 0.1, idle);
        CHECK(t.ms_.phase == PHASE_MATCH_END && !(t.ms_.flags & MS_RPS));
        t.stop();
    }
    uint8_t three[3] = {RPS_ROCK, RPS_PAPER, RPS_SCISSORS}, same[3] = {RPS_PAPER, RPS_PAPER, RPS_PAPER}, two[4] = {RPS_PAPER, RPS_ROCK, RPS_ROCK, RPS_PAPER};
    CHECK(rpsLosingShape(three, 3) == RPS_NONE && rpsLosingShape(same, 3) == RPS_NONE && rpsLosingShape(two, 4) == RPS_ROCK);
}

// B8: the locator is a weapon held only by the seeker (hide and seek) and the tagger (tag), and nowhere else: firing
// it shows "it" one random hider / runner, then it recharges for kLocatorCooldown. The old "locate" command is gone.
static void testLocator(uint16_t port) {
    printf("locator: a weapon, only the seeker's / tagger's, only in hide and seek and tag\n");
    {   // every mode's pool (custom modes and 1v1 rulesets that would allow "everything" too)
        Server s;
        for (int m = 0; m < MODE_COUNT; m++) {
            s.ms_.mode = (uint8_t)m;
            CHECK(s.weaponAllowed(WPN_LOCATOR) == isItMode(m));
            if (isItMode(m)) CHECK(s.weaponAllowed(WPN_KNIFE) && !s.weaponAllowed(WPN_ROCKET));
        }
        s.ms_.mode = MODE_FFA;
        s.cfg_.custom.name = "everything"; s.cfg_.custom.baseMode = MODE_FFA;
        s.cfg_.custom.weapons = (uint16_t)((1u << WPN_COUNT) - 1);
        CHECK(!s.weaponAllowed(WPN_LOCATOR) && s.weaponAllowed(WPN_ROCKET));
        s.cfg_.custom = CustomRules();
        s.ms_.mode = MODE_ARENA; s.ms_.arenaVariant = 0;
        CHECK(!s.weaponAllowed(WPN_LOCATOR));
    }
    for (int mode : {MODE_HIDE, MODE_TAG}) {
        Server s;
        ServerConfig cfg;
        cfg.map = "hamlet"; cfg.mode = mode; cfg.port = port++;
        NetClient c[2];
        std::vector<NetClient*> cls;
        if (!rulesStart(s, cfg, c, 2, cls)) { s.stop(); return; }
        int it = -1;
        for (auto* cl : cls) if (s.players_[cl->clientId()].it) it = cl->clientId();
        CHECK(it >= 0);
        if (it < 0) { s.stop(); return; }
        int other = c[0].clientId() == it ? c[1].clientId() : c[0].clientId();
        s.players_[it].itFreezeUntil = 0;
        CHECK(s.players_[it].weapon == WPN_KNIFE);   // ("it" starts with the knife out)
        auto locates = [&](NetClient& cl) { int n = 0; for (auto& e : cl.takeEvents()) n += e.type == EV_LOCATE; return n; };
        for (auto* cl : cls) cl->takeEvents();
        for (auto* cl : cls) cl->sendCommand("locate");   // the old use-key command does nothing now
        pump(s, cls, 0.2, idle);
        for (auto* cl : cls) CHECK(locates(*cl) == 0);
        bool fire = false;
        auto hold = [&](int i) { UserCmd u; u.weapon = WPN_LOCATOR; if (fire) u.buttons = BTN_FIRE; (void)i; return u; };
        pump(s, cls, 0.2, hold);                           // everyone takes out the locator (the hider can hold it, not use it)
        CHECK(s.players_[it].weapon == WPN_LOCATOR);
        fire = true;
        pump(s, cls, 0.3, hold);                           // fire held: one ping (5 s cooldown)
        NetClient& itc = c[0].clientId() == it ? c[0] : c[1];
        NetClient& oc = c[0].clientId() == it ? c[1] : c[0];
        std::vector<GameEvent> got = itc.takeEvents();
        int n = 0, who = -1;
        for (auto& e : got) if (e.type == EV_LOCATE) { n++; who = e.a; }
        CHECK(n == 1 && who == other);
        for (auto& e : got) CHECK(e.type != EV_SOUND);     // ("it" plays its own ping)
        int pings = 0, locs = 0;
        for (auto& e : oc.takeEvents()) {
            locs += e.type == EV_LOCATE;
            if (e.type != EV_SOUND) continue;
            pings++;
            Vec3 at; std::istringstream ps(e.text);
            CHECK(e.a == it && e.c == WPN_LOCATOR && (ps >> at.x >> at.y >> at.z));
            CHECK(length(at - s.players_[it].move.pos) < 2.0f);   // from where "it" stands
        }
        CHECK(locs == 0);                                  // (only to "it"; the hider's locator does nothing)
        CHECK(pings == 1);                                 // the hider hears the ping
        CHECK(s.players_[it].locateAt > s.time() + kLocatorCooldown - 1.0);
        fire = false; pump(s, cls, 0.1, hold);
        s.players_[it].locateAt = s.time();                // recharged: fires again
        fire = true; pump(s, cls, 0.2, hold);
        CHECK(locates(itc) == 1);
        // no damage from it, and the knife is back with a key press
        CHECK(s.players_[other].alive && s.players_[other].health >= kMaxHealth);
        pump(s, cls, 0.1, [&](int) { UserCmd u; u.weapon = WPN_KNIFE; return u; });
        CHECK(s.players_[it].weapon == WPN_KNIFE);
        s.stop();
    }
    {   // anywhere else it can't be taken out
        Server s;
        ServerConfig cfg;
        cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port++;
        NetClient c[2];
        std::vector<NetClient*> cls;
        if (!rulesStart(s, cfg, c, 2, cls)) { s.stop(); return; }
        pump(s, cls, 0.3, [](int) { UserCmd u; u.weapon = WPN_LOCATOR; u.buttons = BTN_FIRE; return u; });
        for (auto* cl : cls) {
            CHECK(s.players_[cl->clientId()].weapon != WPN_LOCATOR);
            CHECK(!(cl->mode().weaponMask >> WPN_LOCATOR & 1));
            for (auto& e : cl->takeEvents()) CHECK(e.type != EV_LOCATE);
        }
        s.stop();
    }
}

// B8: tag runners always know where the tagger is (the server never hides the tagger from them, even behind walls);
// hide and seek hiders are never shown the seeker: out of sight, the seeker is hidden like anyone else.
static void testItAwareness(uint16_t port) {
    printf("tag: the tagger is always sent; hide and seek: the seeker isn't, out of sight\n");
    for (int mode : {MODE_TAG, MODE_HIDE}) {
        Server s;
        ServerConfig cfg;
        cfg.map = "bastion"; cfg.mode = mode; cfg.port = port++;   // (the centre walls block one end from the other)
        NetClient c[2];
        std::vector<NetClient*> cls;
        if (!rulesStart(s, cfg, c, 2, cls)) { s.stop(); return; }
        int it = s.players_[c[0].clientId()].it ? c[0].clientId() : c[1].clientId();
        NetClient& runner = s.players_[c[0].clientId()].it ? c[1] : c[0];
        NetClient& itc = s.players_[c[0].clientId()].it ? c[0] : c[1];
        int other = runner.clientId();
        for (int k = 0; k < 8; k++) {
            s.players_[it].move.pos = {0, 0, 28}; s.players_[it].move.vel = {};
            s.players_[other].move.pos = {0, 0, -28}; s.players_[other].move.vel = {};
            s.players_[it].itFreezeUntil = s.time() + 10;   // (stays put)
            pump(s, cls, 0.1, idle);
        }
        auto hiddenTo = [&](NetClient& cl, int id) { for (auto& p : cl.latest().players) if (p.id == id) return p.hidden; return false; };
        bool itHidden = hiddenTo(runner, it), runnerHidden = hiddenTo(itc, other);
        printf("  %s: \"it\" %s to the %s, they're %s to \"it\"\n", modeName(mode), itHidden ? "hidden" : "sent", mode == MODE_TAG ? "runner" : "hider", runnerHidden ? "hidden" : "sent");
        CHECK(runnerHidden);                               // (behind the walls: the usual)
        CHECK(itHidden == (mode == MODE_HIDE));            // tag: always sent; hide and seek: hidden
        if (mode == MODE_TAG) {
            bool seen = false;
            for (auto& p : runner.latest().players) if (p.id == it) seen = length(p.pos - s.players_[it].move.pos) < 1.0f;
            CHECK(seen);                                   // (where the tagger really is)
        }
        s.stop();
    }
}

// GAME-1: in tag, "it" going to spectate or leaving is eliminated (the round ends as if time ran out); nobody joins a
// side mid-match; someone eliminated who reconnects is still out.
static void testTagEscape(uint16_t port) {
    printf("tag: no escaping elimination\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "hamlet"; cfg.mode = MODE_TAG; cfg.port = port;
    NetClient c[3];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 3, cls, false)) { s.stop(); return; }
    s.command("forcestart");
    pump(s, cls, 0.1, idle);
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    CHECK(s.ms_.phase == PHASE_LIVE);
    auto itNow = [&] { for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].it) return i; return -1; };
    int it = itNow();
    CHECK(it >= 0);
    if (it < 0) { s.stop(); return; }
    int ci = 0; for (int i = 0; i < 3; i++) if (c[i].clientId() == it) ci = i;
    std::string itName = s.players_[it].name;
    c[ci].sendCommand("team spec");   // "it" runs to spectator: out, and the others survive the round
    pump(s, cls, 0.3, idle);
    printf("  it spectated: out %d, phase %d, others' scores %d %d\n", s.players_[it].out, s.ms_.phase, s.players_[c[(ci + 1) % 3].clientId()].score, s.players_[c[(ci + 2) % 3].clientId()].score);
    CHECK(s.players_[it].out && s.players_[it].team == TEAM_SPECTATOR && s.ms_.phase == PHASE_ROUND_END);
    CHECK(s.players_[c[(ci + 1) % 3].clientId()].score >= 1 && s.players_[c[(ci + 2) % 3].clientId()].score >= 1);
    c[ci].sendCommand("team a");      // no rejoining mid-match
    pump(s, cls, 0.3, idle);
    CHECK(s.players_[it].team == TEAM_SPECTATOR);
    // the eliminated player leaves and comes back after the reconnect grace: a newcomer, who watches the rest of the match
    c[ci].disconnect();
    pump(s, cls, 0.1, idle);
    for (auto& sp : s.players_) if (sp.awayUntil > 0) sp.awayUntil = 1e-9;   // (skip the 10 s reconnect grace: gone for good)
    pump(s, cls, 0.2, idle);
    NetClient again;
    cls[ci] = &again;
    again.connect("127.0.0.1:" + std::to_string(cfg.port), itName);
    pump(s, cls, 0.5, idle);
    int back = again.clientId();
    CHECK(back >= 0 && s.players_[back].connected && s.players_[back].team == TEAM_SPECTATOR);   // joins mid-match as a spectator
    if (back >= 0) {
        again.sendCommand("team a");      // and can't join a side until it's over
        pump(s, cls, 0.3, idle);
        CHECK(s.players_[back].team == TEAM_SPECTATOR && !s.players_[back].alive && s.players_[back].waitingToPlay);
    }
    // next round: two left. "It" disconnecting ends the match: the other one wins
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    CHECK(s.ms_.phase == PHASE_LIVE);
    int it2 = itNow();
    CHECK(it2 >= 0 && it2 != back);
    for (int i = 0; i < 3; i++) if (it2 >= 0 && i != ci && c[i].clientId() == it2) c[i].disconnect();
    pump(s, cls, 0.1, idle);
    for (auto& sp : s.players_) if (sp.awayUntil > 0) sp.awayUntil = 1e-9;   // (skip the 10 s reconnect grace: gone for good)
    pump(s, cls, 0.2, idle);
    printf("  it left: phase %d\n", s.ms_.phase);
    CHECK(s.ms_.phase == PHASE_MATCH_END);
    s.stop();
}

// Tag knockouts last one match (Server::tagOut_ was only ever appended to: in the next tag match on the same server
// everyone knocked out before was out again at once). A match played to the end, then a new one: all in; a knockout,
// then a forcestart mid-match (startMatch without the warmup in between): all in again.
static void testTagNextMatch(uint16_t port) {
    printf("tag: knockouts don't carry into the next match\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "hamlet"; cfg.mode = MODE_TAG; cfg.port = port;
    NetClient c[3];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 3, cls)) { s.stop(); return; }
    auto outs = [&] { int n = 0; for (int i = 0; i < 3; i++) if (s.players_[c[i].clientId()].out) n++; return n; };
    auto playing = [&] { int n = 0; for (int i = 0; i < 3; i++) { auto& p = s.players_[c[i].clientId()]; if (p.alive && !p.out && p.team != TEAM_SPECTATOR) n++; } return n; };
    auto toLive = [&] { for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE && s.ms_.phase != PHASE_MATCH_END; i++) { if (s.ms_.phase == PHASE_FREEZE || s.ms_.phase == PHASE_ROUND_END) s.phaseEnd_ = s.time(); pump(s, cls, 0.05, idle); } };
    // play it out: time runs out on "it" twice, two out, one wins
    for (int k = 0; k < 2 && s.ms_.phase == PHASE_LIVE; k++) { s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle); toLive(); }
    printf("  first match: phase %d, %d out\n", s.ms_.phase, outs());
    CHECK(s.ms_.phase == PHASE_MATCH_END && outs() == 2);
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);   // match over: back to warmup
    CHECK(s.ms_.phase == PHASE_WARMUP && s.tagOut_.empty());
    s.command("forcestart");
    toLive();
    pump(s, cls, 0.2, idle);
    printf("  second match: phase %d, %d out, %d in play\n", s.ms_.phase, outs(), playing());
    CHECK(s.ms_.phase == PHASE_LIVE && outs() == 0 && playing() == 3);
    // one knocked out, then a forcestart mid-match: a new match, everyone in
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    CHECK(outs() == 1 && !s.tagOut_.empty());
    s.command("forcestart");
    toLive();
    pump(s, cls, 0.2, idle);
    printf("  forcestart mid-match: phase %d, %d out\n", s.ms_.phase, outs());
    CHECK(s.ms_.phase == PHASE_LIVE && outs() == 0 && playing() == 3);
    s.stop();
}

// The payload cart's collision box is where the visible cart is in both halves. On a map without staged spawns
// (railyard) team B's attack runs the track from the other end (Map::cartTrack); the box used the raw distance, so
// in even rounds it sat at the far end of the track and the cart itself had none, on the server and in the client's
// prediction alike.
static void testCartBoxPlace(uint16_t port) {
    printf("payload: the cart's collision box follows the visible cart\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "railyard"; cfg.mode = MODE_PAYLOAD; cfg.port = port; cfg.balanceBots = false;
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls)) { s.stop(); return; }
    bool staged = false;
    for (auto& sp : s.map_.spawns) if (isStagedSpawn(sp.team)) staged = true;
    CHECK(!staged);   // (the case: an unstaged map, where team B's cart runs the other way)
    auto checkBox = [&](const char* what) {
        s.ms_.payloadDist = s.ms_.payloadLen * 0.25f;   // (a quarter along: the two ends of the track are far apart)
        auto away = [&](int) { s.payloadIdleSince_ = s.time(); for (int i = 0; i < 2; i++) s.players_[c[i].clientId()].move.pos = s.map_.spawns[0].pos + Vec3(0, 0.5f, 0); return UserCmd(); };
        pump(s, cls, 0.3, away);
        Vec3 cart = s.map_.cartPoint(s.ms_.mode, s.ms_.attackers, s.ms_.payloadDist);
        CHECK(s.map_.dynSolids.size() == 1 && c[0].map().dynSolids.size() == 1);
        if (s.map_.dynSolids.size() != 1 || c[0].map().dynSolids.size() != 1) return;
        Vec3 sv = s.map_.dynSolids[0].center(), cl = c[0].map().dynSolids[0].center();
        float off = length(Vec3(sv.x - cart.x, 0, sv.z - cart.z)), clOff = length(cl - sv);
        printf("  %s (attackers %s): box %.2f m from the cart, the client's %.2f m from the server's\n", what, s.ms_.attackers == TEAM_A ? "A" : "B", off, clOff);
        CHECK(off < 0.05f && clOff < 0.3f);
        float t = s.map_.raycast(cart + Vec3(0, 6, 0), {0, -1, 0}, 12);   // shots stop on the visible cart
        CHECK(t > 0 && t < 6);
    };
    CHECK(s.ms_.attackers == TEAM_A);
    checkBox("round 1");
    // team A delivers it; on to round 2, team B attacking
    s.ms_.payloadDist = s.ms_.payloadLen;
    pump(s, cls, 0.1, idle);
    for (int i = 0; i < 100 && !(s.ms_.phase == PHASE_LIVE && s.ms_.round == 2); i++) { if (s.ms_.phase != PHASE_LIVE) s.phaseEnd_ = s.time(); pump(s, cls, 0.05, idle); }
    CHECK(s.ms_.phase == PHASE_LIVE && s.ms_.round == 2 && s.ms_.attackers == TEAM_B);
    if (s.ms_.attackers != TEAM_B) { s.stop(); return; }
    CHECK(length(s.map_.cartPoint(s.ms_.mode, TEAM_B, s.ms_.payloadLen * 0.25f) - s.map_.payloadPoint(s.ms_.payloadLen * 0.25f)) > 5);
    checkBox("round 2");
    s.stop();
}

// GAME-5: a bomb carrier killed in mid-air drops the bomb on the floor below, not in the air; a bomb nobody picks up
// goes back to an attacker.
static void testBombDrop(uint16_t port) {
    printf("bomb: dropped on the floor, returned if left\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "depot"; cfg.mode = MODE_BOMB; cfg.port = port; cfg.balanceBots = false;
    NetClient c[3];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 3, cls)) { s.stop(); return; }
    int carrier = s.ms_.bombCarrier, other = -1;
    for (int i = 0; i < 3; i++) { int id = c[i].clientId(); if (id != carrier && s.players_[id].team == s.ms_.attackers) other = id; }
    CHECK(s.ms_.bombState == BOMB_CARRIED && carrier < kMaxPlayers && other >= 0);
    if (carrier >= kMaxPlayers || other < 0) { s.stop(); return; }
    Vec3 ground = s.players_[carrier].move.pos;   // (standing at its spawn)
    Vec3 away = s.players_[other].move.pos + Vec3(0, 0, 0);
    if (length(away - ground) < 3) away = s.map_.spawns[0].pos;
    float up = s.map_.raycast(ground + Vec3(0, 0.1f, 0), Vec3(0, 1, 0), 50);
    float lift = up < 0 ? 6.0f : std::min(6.0f, up - 0.5f);   // (under the ceiling, if there is one)
    CHECK(lift > 2.0f);
    s.players_[carrier].move.pos = ground + Vec3(0, lift, 0);   // blown into the air...
    s.killPlayer(carrier, -1, 254);                          // ...and killed there
    printf("  died %.1f m up: bomb at %.2f (floor %.2f)\n", lift, s.ms_.bombPos.y, ground.y);
    CHECK(s.ms_.bombState == BOMB_DROPPED && std::fabs(s.ms_.bombPos.y - ground.y) < 0.15f);
    auto keepAway = [&](int) { s.players_[other].move.pos = away; s.players_[other].move.vel = {}; return UserCmd(); };
    pumpServerTime(s, cls, 10.0, keepAway);
    CHECK(s.ms_.bombState == BOMB_DROPPED);                 // still lying there
    pumpServerTime(s, cls, 6.0, keepAway);
    CHECK(s.ms_.phase == PHASE_LIVE && s.ms_.bombState == BOMB_CARRIED && s.ms_.bombCarrier == other);   // back to the attack
    s.stop();
}

// GAME-10: a human joining doesn't remove a balance bot that's alive in a live round (that could hand the round
// to the other side); it goes when it dies or the round ends.
static void testBalanceBotDefer(uint16_t port) {
    printf("balance bots: none removed alive mid-round\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_VS; cfg.port = port; cfg.balanceBots = true;
    NetClient c[1];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 1, cls, false)) { s.stop(); return; }
    s.updateBalanceBots(true);   // (what a passed start vote does: the bot for the empty side, then the match)
    s.command("forcestart");
    int bot = -1;
    for (int k = 0; k < 200; k++) {   // until a round is live with the bot alive in it (it joins round 1 dead)
        bot = -1;
        for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].balanceBot) bot = i;
        if (bot >= 0 && s.ms_.phase == PHASE_LIVE && s.players_[bot].alive && s.players_[c[0].clientId()].alive) break;
        if (s.ms_.phase == PHASE_FREEZE || s.ms_.phase == PHASE_ROUND_END) s.phaseEnd_ = s.time();
        pump(s, cls, 0.05, [&](int) { if (bot >= 0) s.players_[bot].health = 100; s.players_[c[0].clientId()].health = 100; return UserCmd(); });
    }
    if (bot < 0 || s.ms_.phase != PHASE_LIVE || !s.players_[bot].alive)
        printf("  bot %d phase %d round %d bot alive %d human alive %d\n", bot, s.ms_.phase, s.ms_.round, bot >= 0 && s.players_[bot].alive, s.players_[c[0].clientId()].alive);
    CHECK(bot >= 0 && s.ms_.phase == PHASE_LIVE && s.players_[bot].alive);
    if (bot < 0) { s.stop(); return; }
    int round = s.ms_.round;
    NetClient late;
    cls.push_back(&late);
    late.connect("127.0.0.1:" + std::to_string(cfg.port), "late");
    auto calm = [&](int) { s.players_[bot].health = 100; s.players_[c[0].clientId()].health = 100; s.players_[bot].move.pos = s.map_.spawns[0].pos; return UserCmd(); };
    pumpServerTime(s, cls, 3.0, calm);   // (balance checks run every second)
    printf("  a human joined team %d: the bot is %s\n", late.clientId() >= 0 ? s.players_[late.clientId()].team : -1, s.players_[bot].connected ? "still in (alive)" : "gone");
    CHECK(s.players_[bot].connected && s.players_[bot].balanceBot && s.ms_.round == round && s.ms_.phase == PHASE_LIVE);
    s.killPlayer(bot, -1, 254);
    pumpServerTime(s, cls, 1.5, idle);
    CHECK(!s.players_[bot].connected || !s.players_[bot].balanceBot);   // dead: now it goes
    s.stop();
}

// MAP-7: the spawn type depends on whether the map has those spawns (canSupportMode), not on its "modes" line:
// Colosseum (tourney, doesn't offer vs) uses its a/b spawns for tourney matches.
static void testSpawnTypes(uint16_t port) {
    printf("spawn types from the spawns a map has\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "colosseum"; cfg.mode = MODE_TOURNEY; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    CHECK(!s.map_.supportsMode(MODE_VS) && s.map_.canSupportMode(MODE_VS));   // (the case: a/b spawns, no "vs")
    s.tourney_.running = true;
    s.players_[0].connected = s.players_[1].connected = true;
    s.players_[0].team = TEAM_A; s.players_[1].team = TEAM_B;
    CHECK(rulesSpawnType(s, 0) == SPAWN_A && rulesSpawnType(s, 1) == SPAWN_B);
    s.spawnPlayer(0); s.spawnPlayer(1);   // at opposite ends (the a / b spawns are 34 m apart)
    CHECK(length(s.players_[0].move.pos - s.players_[1].move.pos) > 30);
    s.players_[0].connected = s.players_[1].connected = false;
    s.stop();
}

// What a shared / CPU-capped VM does to a server that is otherwise idle: every `every` seconds the OS doesn't run it
// for `ms` (the pump's input callback sleeps: frame() comes that much late). Busy stays near 0.
struct Stalls {
    double every, next = 0;
    int ms;
    UserCmd operator()(int) { if (timeNow() >= next) { next = timeNow() + every; sleepMs(ms); } return UserCmd(); }
};

// GAME-11: the tick rate doesn't change mid-round; a host that falls behind steps down at the next break. Here the
// host's ticks stall (14 ms every 0.25 s: ~4% of 1000 Hz ticks over 4 ms late) with almost no load, as the Oracle box
// did: that alone now steps the tick rate down. (14 ms whatever the phase: at 250 Hz the tick due in the stall is at
// least 10 ms late, over its 8 ms; at 125 Hz at most 14, under its 16. A 12 ms stall that starts just after a 125 Hz
// tick holds the next one only 4 ms, so with Linux's exact sleeps 250 Hz looked clean and it stepped up.)
static void testRateBetweenRounds(uint16_t port) {
    printf("tick rate changes only between rounds; stalled ticks with no load step it down\n");
    setTimeScale(1.0);
    g_serverRateCheck = true;
    struct Restore { ~Restore() { setTimeScale(kTestTimeScale); g_serverRateCheck = false; } } restore;
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    NetClient c[1];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 1, cls)) { s.stop(); return; }
    CHECK(s.tickRate() == 1000);
    pump(s, cls, 3.0 * 2.5, Stalls{0.25, 0, 14});   // two and a half rate windows, mid-match
    printf("  mid-match, stalling (%.1f%% of ticks over %.1f ms late): %d Hz (phase %d)\n", s.stallPct_, s.stallAfter_ * 1e3, s.tickRate(), s.ms_.phase);
    CHECK(s.ms_.phase == PHASE_LIVE && s.tickRate() == 1000 && s.rateDownPending_);
    CHECK(s.stallPct_ > 1 && s.snapRate() == 500);   // (late ticks aren't a bandwidth problem: the snapshot rate stays)
    s.phaseEnd_ = s.time();                  // time's up: the match is over
    pump(s, cls, 0.5, idle);
    printf("  match over: %d Hz (phase %d), %s\n", s.tickRate(), s.ms_.phase, s.rateWhy_.c_str());
    CHECK(s.ms_.phase == PHASE_MATCH_END && s.tickRate() == 500);   // stepped down at once, at the break
    // between rounds the stalls go on stepping it down until a tick period absorbs them (at 125 Hz a stall is 16 ms)
    for (int k = 0; k < 60 && s.tickRate() > 125; k++) pump(s, cls, 0.25, Stalls{0.25, 0, 14});
    printf("  still stalling, match end: %d Hz\n", s.tickRate());
    CHECK(s.tickRate() == 125);
    s.rateUpAt_ = 0;   // (the wait before trying the next rate up: over)
    pump(s, cls, 3.0 * 1.5, Stalls{0.25, 0, 14});
    printf("  at 125 Hz, same stalls: %.1f%% over %.1f ms, %d Hz\n", s.stallPct_, s.stallAfter_ * 1e3, s.tickRate());
    CHECK(s.tickRate() == 125 && s.stallPct_ < 1);   // clean at 125, but 250 Hz would stall: it stays
    s.stop();
}

// REVIEW 6.1: a step up (made in a break) that doesn't hold is undone at once, even mid-round; any other step down
// still waits for the break.
static void testRateUndoStepUp(uint16_t port) {
    printf("tick rate: a step up that doesn't hold is undone mid-round\n");
    setTimeScale(1.0);
    g_serverRateCheck = true;
    struct Restore { ~Restore() { setTimeScale(kTestTimeScale); g_serverRateCheck = false; } } restore;
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    NetClient c[1];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 1, cls, false)) { s.stop(); return; }
    s.rateLevel_ = 1;   // stepped down earlier (500 Hz); the wait before trying 1000 again is over
    s.rateUpAt_ = 0;
    for (int k = 0; k < 60 && s.tickRate() != 1000; k++) pump(s, cls, 0.25, idle);   // a clean warmup window: up
    printf("  warmup: %d Hz\n", s.tickRate());
    CHECK(s.ms_.phase == PHASE_WARMUP && s.tickRate() == 1000);
    s.command("forcestart");
    for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE; i++) { if (s.ms_.phase == PHASE_FREEZE) s.phaseEnd_ = s.time(); pump(s, cls, 0.05, idle); }
    CHECK(s.ms_.phase == PHASE_LIVE);
    Stalls st{0.25, 0, 12};                  // the raise doesn't hold
    for (int k = 0; k < 30 && s.tickRate() != 500; k++) pump(s, cls, 0.25, st);
    printf("  live, stalling after the raise: %d Hz (phase %d)\n", s.tickRate(), s.ms_.phase);
    CHECK(s.ms_.phase == PHASE_LIVE && s.tickRate() == 500);   // undone mid-round, not after it
    pump(s, cls, 3.0 * 2.5, st);             // still stalling, but that's no longer our own raise: waits for the break
    printf("  still stalling: %d Hz (phase %d)\n", s.tickRate(), s.ms_.phase);
    CHECK(s.ms_.phase == PHASE_LIVE && s.tickRate() == 500 && s.rateDownPending_);
    s.phaseEnd_ = s.time();                  // time's up: the match is over
    pump(s, cls, 0.5, idle);
    CHECK(s.ms_.phase == PHASE_MATCH_END && s.tickRate() == 250);   // (the pending step down, at the break)
    s.stop();
}

// A healthy host's jitter is no stall: Windows' 1 ms timer grid wakes a 1000 Hz tick up to a period late (p99 ~0.7-1
// ms), and now and then later. Here the server runs its own loop (frame, idle: as quark_server does) and every wake
// comes 0-1 ms late, one in 50 1-3 ms late: 1000 Hz and 500 snapshots/s stay.
static void testRateJitter(uint16_t port) {
    printf("tick rate: a healthy host's timer jitter doesn't step it down\n");
    setTimeScale(1.0);
    g_serverRateCheck = true;
    struct Restore { ~Restore() { setTimeScale(kTestTimeScale); g_serverRateCheck = false; } } restore;
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    NetClient c[1];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 1, cls, false)) { s.stop(); return; }
    uint32_t r = 12345;
    auto rnd = [&] { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.0; };
    double end = timeNow() + 3.0 * 3.3, next = timeNow();
    while (timeNow() < end) {
        s.idle(s.frame());
        double late = rnd() < 0.02 ? 0.001 + 0.002 * rnd() : 0.001 * rnd();
        for (double t = timeNow() + late; timeNow() < t;) {}   // (woken that late)
        c[0].poll();
        while (timeNow() >= next) { c[0].tick(UserCmd()); next += c[0].tickPeriod(); }
    }
    std::string st = s.statusText();
    printf("  %s\n", st.substr(0, st.find("\n  #")).c_str());
    CHECK(s.tickRate() == 1000 && s.snapRate() == 500 && s.rateWhy_.empty());
    CHECK(s.stallPct_ < 0.2);
    s.stop();
}

// --max-upload-kbps: past 90% of it the snapshot rate steps down (500 333 250 125/s), at once (any phase), then the
// tick rate; with the cap gone (and the wait over) it climbs back: the tick rate first, then the snapshot rate.
static void testUploadCap(uint16_t port) {
    printf("upload cap: snapshot rate first, then tick rate; back up when it fits\n");
    setTimeScale(1.0);
    g_serverRateCheck = true;
    struct Restore { ~Restore() { setTimeScale(kTestTimeScale); g_serverRateCheck = false; } } restore;
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    NetClient c[1];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 1, cls, false)) { s.stop(); return; }
    s.rateWindow_ = 1.0;
    pump(s, cls, 2.2, idle);
    double full = s.upKbps_;   // one player at 500/s
    printf("  one player: upload %.0f kbps at %d/s\n", full, s.snapRate());
    CHECK(full > 50 && s.snapRate() == 500 && s.tickRate() == 1000);
    // a cap the 333/s rate fits under (with room): one step, the tick rate untouched
    s.cfg_.maxUploadKbps = (int)(full * 0.85);
    pump(s, cls, 4.2, idle);
    printf("  cap %d kbps: %d/s at %d Hz, upload %.0f kbps; %s\n", s.cfg_.maxUploadKbps, s.snapRate(), s.tickRate(), s.upKbps_, s.rateWhy_.c_str());
    CHECK(s.snapRate() == 333 && s.tickRate() == 1000);
    CHECK(s.rateWhy_.find("upload") != std::string::npos);
    // a cap nothing fits under: every snapshot step, then every tick step
    s.cfg_.maxUploadKbps = 1;
    std::vector<std::pair<int, int>> steps;
    std::pair<int, int> was{s.tickRate(), s.snapRate()};
    for (int k = 0; k < 120 && s.tickRate() > 125; k++) {
        pump(s, cls, 0.1, idle);
        std::pair<int, int> is{s.tickRate(), s.snapRate()};
        if (is != was) steps.push_back(was = is);
    }
    for (auto& st : steps) printf("  -> %d Hz, %d/s\n", st.first, st.second);
    std::vector<std::pair<int, int>> want = {{1000, 250}, {1000, 125}, {500, 125}, {250, 125}, {125, 125}};
    CHECK(steps == want);
    CHECK(c[0].snapRate() == 125);
    // cap gone, the backoff wait over: one step up per clean window, the tick rate first
    s.cfg_.maxUploadKbps = 0;
    s.rateUpAt_ = 0;
    steps.clear();
    for (int k = 0; k < 150 && !(s.tickRate() == 1000 && s.snapRate() == 500); k++) {
        pump(s, cls, 0.1, idle);
        std::pair<int, int> is{s.tickRate(), s.snapRate()};
        if (is != was) steps.push_back(was = is);
    }
    for (auto& st : steps) printf("  <- %d Hz, %d/s\n", st.first, st.second);
    want = {{250, 125}, {500, 125}, {1000, 125}, {1000, 250}, {1000, 333}, {1000, 500}};
    CHECK(steps == want);
    CHECK(s.snapCap_ == 0);
    pump(s, cls, 0.3, idle);
    CHECK(c[0].snapRate() == 500 && c[0].tickRate() == 1000);
    s.stop();
}

// No cap: congestion on the server's uplink shows in most players at once (losing snapshots, or their ping rising
// together: the queue delays everyone). One player's bad connection never moves the server; two of three do.
static void testSharedCongestion(uint16_t port) {
    printf("congestion: most players at once lower the snapshot rate, one player never\n");
    setTimeScale(1.0);
    g_serverRateCheck = true;
    struct Restore { ~Restore() { setTimeScale(kTestTimeScale); g_serverRateCheck = false; } } restore;
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    NetClient c[3];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 3, cls)) { s.stop(); return; }   // (live: snapshot steps don't wait for a break)
    s.rateWindow_ = 1.0;
    // slow: these clients read their socket only every 150 ms (their acks age: each ping, measured by the server, up)
    auto run = [&](double secs, int slow) {
        double end = timeNow() + secs, next = timeNow(), read[3] = {};
        while (timeNow() < end) {
            s.frame();
            for (int i = 0; i < 3; i++) if (i >= slow || timeNow() - read[i] >= 0.15) { c[i].poll(); read[i] = timeNow(); }
            while (timeNow() >= next) { for (auto* cl : cls) cl->tick(UserCmd()); next += c[2].tickPeriod(); }
            sleepMs(1);
        }
    };
    run(2.2, 0);   // (each player's uncongested ping)
    c[0].overrideReportedLoss(50);
    run(3.2, 0);
    printf("  one player losing snapshots: %d Hz, %d/s\n", s.tickRate(), s.snapRate());
    CHECK(s.tickRate() == 1000 && s.snapRate() == 500 && !s.rateDownPending_);
    c[0].overrideReportedLoss(0);
    run(3.2, 1);
    printf("  one player's ping up (%.0f ms, was %.0f): %d Hz, %d/s\n", s.players_[c[0].clientId()].ping, s.players_[c[0].clientId()].pingBase, s.tickRate(), s.snapRate());
    CHECK(s.tickRate() == 1000 && s.snapRate() == 500 && !s.rateDownPending_);
    c[0].overrideReportedLoss(50); c[1].overrideReportedLoss(50);
    for (int k = 0; k < 30 && s.snapRate() == 500; k++) run(0.1, 0);
    printf("  two of three losing snapshots: %d Hz, %d/s (phase %d); %s\n", s.tickRate(), s.snapRate(), s.ms_.phase, s.rateWhy_.c_str());
    CHECK(s.snapRate() == 333 && s.tickRate() == 1000 && s.ms_.phase == PHASE_LIVE);
    CHECK(s.rateWhy_.find("2 of 3 players losing") != std::string::npos);
    c[0].overrideReportedLoss(0); c[1].overrideReportedLoss(0);
    run(2.2, 0);   // (the window that still had their loss reports)
    int was = s.snapRate();
    for (int k = 0; k < 40 && s.snapRate() == was; k++) run(0.1, 2);
    printf("  two of three with their ping up: %d Hz, %d/s; %s\n", s.tickRate(), s.snapRate(), s.rateWhy_.c_str());
    CHECK(s.snapRate() < was && s.tickRate() == 1000);
    CHECK(s.rateWhy_.find("ping up on 2 of 3") != std::string::npos);
    s.stop();
}

// GAME-12: nothing uses the unseeded rand(); each thread's qrand starts from its own random seed (a hosted server's
// thread differs from the client's, and from the last run's), so modifiers, seeding, the first carrier / "it" vary.
static void testRandomSeeds() {
    printf("random seeds per thread\n");
    uint32_t first[4] = {};
    std::vector<std::thread> ts;
    for (int i = 0; i < 4; i++) ts.emplace_back([&first, i] { first[i] = qrand(); });
    for (auto& t : ts) t.join();
    int same = 0;
    for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) same += first[i] == first[j];
    CHECK(same == 0);
    // (the source has no rand() / srand() calls left: checked by grep in review; qrand is the only generator)
}

// GAME-2a: heals (syringe, vampire, regeneration) stop at this life's full health, not the standard 100: a "tanks"
// life heals to 250, "1 HP" / "glass cannons" lives never above 1 / 50.
static void testLifeHealCap(uint16_t port) {
    printf("heals cap at the life's health\n");
    {
        Server s;
        s.ms_.mode = MODE_DUEL; s.cfg_.mode = MODE_DUEL;
        for (int d = 0; d < DUEL_MOD_COUNT; d++) { s.ms_.duelMod = (uint8_t)d; CHECK(s.lifeMaxHealth() == duelModDef(d).health); }
        s.ms_.mode = MODE_FFA; s.cfg_.mode = MODE_FFA;
        CHECK(s.lifeMaxHealth() == 100);
    }
    Server s;
    ServerConfig cfg;
    cfg.map = "clockwork"; cfg.mode = MODE_DUEL; cfg.port = port; cfg.balanceBots = false;
    cfg.custom.name = "regen duel"; cfg.custom.baseMode = MODE_DUEL; cfg.custom.regen = true;   // (regeneration on every life)
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls, false)) { s.stop(); return; }
    s.duelUsed_ = ~(1u << 22);   // the next life: TANKS (250)
    s.command("forcestart");
    for (int k = 0; k < 40 && s.ms_.phase != PHASE_LIVE; k++) { s.phaseEnd_ = s.time(); pump(s, cls, 0.05, idle); }
    auto& p = s.players_[c[0].clientId()];
    CHECK(s.ms_.duelMod == 22 && p.alive && p.health == 250);
    p.health = 150;
    p.lastDamagedAt = s.time() - 10;
    auto apart = [&](int) { s.players_[c[0].clientId()].nextFire = s.players_[c[1].clientId()].nextFire = s.time() + 1; return UserCmd(); };
    pumpServerTime(s, cls, 3.0, apart);
    printf("  tanks + regeneration: 150 -> %.0f hp in 3 s\n", p.health);
    CHECK(p.health > 170 && p.health <= 250);   // regenerates past 100 (it stopped at 100 before)
    s.stop();
}

// M6: the scoreboard shows the number it sorts by; in free for all that's frags (the server's score, which decides
// the match), not damage / 10 + objective.
static void testScoreboardValue() {
    printf("scoreboard: shows what it sorts by\n");
    PlayerSnap fragger, damager;
    fragger.score = 5; fragger.kills = 5; fragger.dmg = 300;
    damager.score = 2; damager.kills = 2; damager.dmg = 900;
    ModeState ms;
    ms.mode = MODE_FFA;
    CHECK(scoreboardValue(fragger, ms) == 5 && scoreboardValue(damager, ms) == 2);   // the winner on top, showing frags
    ms.mode = MODE_KOTH;
    damager.obj = 7;
    CHECK(scoreboardValue(damager, ms) == 97 && scoreboardValue(fragger, ms) == 30);
    ms.mode = MODE_FFA; ms.flags = MS_RANKED; ms.arenaVariant = 5;   // ranked instagib: gibs
    CHECK(scoreboardGibs(ms) && scoreboardValue(fragger, ms) == 5);
    ms.mode = MODE_HIDE; ms.flags = 0; ms.arenaVariant = 0;
    CHECK(!scoreboardGibs(ms) && scoreboardValue(fragger, ms) == 5);
}

// "quark_tests rules [name]": all of them, or the one named.
// The HUD's live line (src/client/hudclock.h): every mode with a running round clock shows it on the default HUD
// (1.17.0-1.18.2 showed it only in overtime, KOTH, the drain modes and the time trial: FFA, tag, hide, bomb,
// payload, both-ways payload, points and race had no clock). Checked on what a client receives from a real server
// in each mode, plus the special cases by hand.
static void testHudClock(uint16_t port) {
    printf("hud clock: every timed mode shows its clock while live\n");
    char line[32];
    auto fmt = [](float sec) { int t = (int)std::ceil(std::max(0.0f, sec)); char b[16]; snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60); return std::string(b); };
    struct Case { int mode; const char* map; int players; };
    for (Case cs : {Case{MODE_FFA, "harbor", 2}, Case{MODE_TAG, "foundry", 3}, Case{MODE_HIDE, "hamlet", 3}, Case{MODE_BOMB, "depot", 2},
                    Case{MODE_PAYLOAD, "railyard", 2}, Case{MODE_NPAYLOAD, "canal", 2}, Case{MODE_POINTS, "midtown", 2}, Case{MODE_RACE, "viaduct", 2},
                    Case{MODE_KOTH, "summit", 2}, Case{MODE_VS, "bastion", 2}, Case{MODE_DUEL, "clockwork", 2}, Case{MODE_ARENA, "temple", 2}}) {
        Server s;
        ServerConfig cfg;
        cfg.map = cs.map; cfg.mode = cs.mode; cfg.port = port++; cfg.balanceBots = false;
        NetClient c[3];
        std::vector<NetClient*> cls;
        if (!rulesStart(s, cfg, c, cs.players, cls)) { s.stop(); continue; }
        pump(s, cls, 0.3, idle);
        const ModeState& ms = c[0].mode();   // (what the HUD draws from)
        hudLiveLine(ms, line, sizeof line);
        const bool drainMode = cs.mode == MODE_VS || cs.mode == MODE_DUEL || cs.mode == MODE_ARENA;
        printf("  %s: phase %d, time left %.1f -> \"%s\"\n", modeName(cs.mode), ms.phase, ms.timeLeft, line);
        CHECK(ms.phase == PHASE_LIVE);
        if (cs.mode == MODE_KOTH) CHECK(ms.timeLeft == 0 && std::string(line) == "FIRST TO " + std::to_string(ms.target));   // (untimed)
        else if (drainMode) CHECK(ms.timeLeft > 0 && std::string(line) == "DRAIN IN " + fmt(ms.timeLeft));
        else CHECK(ms.timeLeft > 10 && std::string(line) == fmt(ms.timeLeft));   // the round clock itself
        s.stop();
    }
    // the special cases
    ModeState m;
    m.phase = PHASE_LIVE;
    m.mode = MODE_BOMB; m.timeLeft = 61.2f;
    hudLiveLine(m, line, sizeof line); CHECK(std::string(line) == "1:02");
    m.bombState = BOMB_PLANTED; m.bombTimer = 34.5f; m.timeLeft = 34.5f;   // planted: the fuse is the clock
    hudLiveLine(m, line, sizeof line); CHECK(std::string(line) == "BOMB 0:35");
    m = ModeState(); m.phase = PHASE_LIVE; m.mode = MODE_PAYLOAD; m.flags = MS_OVERTIME; m.timeLeft = 3.2f;
    hudLiveLine(m, line, sizeof line); CHECK(std::string(line) == "OVERTIME 0:04");
    m.mode = MODE_FFA; m.timeLeft = 0;
    hudLiveLine(m, line, sizeof line); CHECK(std::string(line) == "OVERTIME - NEXT KILL WINS");
    m.flags = MS_TIMETRIAL; m.mode = MODE_RACE; m.timeLeft = 300.0f - 72.345f;   // the run time, counting up
    hudLiveLine(m, line, sizeof line); CHECK(std::string(line) == "1:12.34");
    m.flags = 0; m.mode = MODE_TAG; m.timeLeft = 0;   // no clock running: nothing (never a frozen 0:00)
    hudLiveLine(m, line, sizeof line); CHECK(line[0] == 0);
    m.mode = MODE_VS; m.flags = MS_DRAIN;
    hudLiveLine(m, line, sizeof line); CHECK(std::string(line) == "STALEMATE - HEALTH DRAINING");
}

// The predicted weapon (src/client/weaponpredict.h, what the client draws and fires locally): after every command,
// the weapon the server holds is the one that command predicted, and the fire lock matches the server's
// (runPlayerCmd: switching with fire held locks fire until it's let go). Before 1.18.7 the client showed the old
// weapon until a snapshot confirmed the switch, and the confirmation itself (the command going back to "no change")
// locked local fire while the server kept firing. A refusal the client didn't see coming falls back to the server's
// weapon once the server has run the asking command; dead, nothing switches.
static void testWeaponPredict(uint16_t port) {
    printf("weapon prediction: each command's predicted weapon and fire lock are the server's\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port; cfg.autoStart = false; cfg.balanceBots = false;   // (warmup: no restarts)
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls, false)) { s.stop(); return; }
    pump(s, cls, 1.0, idle);
    NetClient& a = c[0];
    ServerPlayer& sp = s.players_[a.clientId()];
    CHECK(sp.alive && a.hasSnapshot());
    WeaponPredictor wp;
    struct Pred { uint32_t seq = 0; int weapon = -1; bool locked = false, alive = false; };   // (alive: as the client saw it)
    std::vector<Pred> ring(NetClient::kCmdRing);
    bool locked = false, verify = true;
    uint16_t buttons = 0, localAway = 0, maskOverride = 0;
    int checked = 0, wrong = 0, lockWrong = 0, lastPred = -1, switches = 0;
    uint8_t lastCmdWeapon = kNoWeaponChange;
    auto view = [&] {
        WeaponView v;
        const Snapshot& sn = a.latest();
        v.serverWeapon = sn.selfWeapon; v.ackSeq = sn.ackCmdSeq; v.alive = a.selfAlive();
        v.mask = maskOverride ? maskOverride : sn.ms.weaponMask;
        v.away = (uint16_t)(localAway | (sn.knifeAway ? 1u << WPN_KNIFE : 0u) | (sn.syringeAway ? 1u << WPN_SYRINGE : 0u));
        return v;
    };
    auto throwNext = false;
    auto input = [&](int i) {
        UserCmd u;
        if (i != 0) return u;
        u.pitch = 1.3f;   // (at the sky: nobody hit)
        // the server has run sp.lastCmdSeq: what it holds now is what that command predicted
        const Pred& p = ring[sp.lastCmdSeq % ring.size()];
        if (verify && sp.alive && p.alive && sp.lastCmdSeq && p.seq == sp.lastCmdSeq) {   // (a respawn sets the weapon outside any command)
            checked++;
            if (p.weapon != sp.weapon && wrong++ < 3) printf("  seq %u: predicted %d, server holds %d\n", p.seq, p.weapon, sp.weapon);
            if (p.locked != sp.fireLocked && lockWrong++ < 3) printf("  seq %u: predicted lock %d, server %d\n", p.seq, p.locked, sp.fireLocked);
        }
        if (localAway && a.latest().knifeAway) localAway = 0;   // (the server's word has it now)
        u.buttons = buttons;
        wp.fill(u, a.latest().ackCmdSeq, a.nextSeq());
        bool sl = false;
        int w = wp.run(u, view(), &sl);   // (as Game's tick loop does)
        if (sl) locked = true;
        if (throwNext && (u.buttons & BTN_ALTFIRE)) { localAway = 1u << WPN_KNIFE; locked = true; w = wp.thrown(view()); throwNext = false; }
        if (locked && !(u.buttons & (BTN_FIRE | BTN_ALTFIRE))) locked = false;
        if (lastPred >= 0 && w != lastPred) switches++;
        lastPred = w; lastCmdWeapon = u.weapon;
        ring[a.nextSeq() % ring.size()] = {a.nextSeq(), w, locked, a.selfAlive()};
        return u;
    };
    // 1. a plain switch: predicted from the first command on
    wp.want(WPN_RIFLE);
    pump(s, cls, 0.4, input);
    CHECK(sp.weapon == WPN_RIFLE && lastPred == WPN_RIFLE && wp.desired < 0 && lastCmdWeapon == kNoWeaponChange);
    // 2. fire pressed right after the key, held through the confirmation: never locked (the 1.18.2 bug), firing all along
    wp.want(WPN_SHOTGUN);
    pump(s, cls, 0.002, input);
    CHECK(lastPred == WPN_SHOTGUN);
    buttons = BTN_FIRE;
    double lf = sp.lastFire;
    pump(s, cls, 0.4, input);
    CHECK(sp.weapon == WPN_SHOTGUN && !sp.fireLocked && !locked && sp.lastFire != lf && wp.desired < 0);
    // 3. switching with fire held: locked on both sides until it's let go
    wp.want(WPN_SNIPER);
    pump(s, cls, 0.3, input);
    CHECK(sp.weapon == WPN_SNIPER && sp.fireLocked && locked);
    buttons = 0;
    pump(s, cls, 0.1, input);
    CHECK(!sp.fireLocked && !locked);
    // 4. a refusal the client didn't see coming (it thinks the pool has the locator): the server's weapon comes back
    //    once it has run the asking command, and the command stops asking
    maskOverride = 0xFFFF; verify = false;
    int before = switches;
    wp.want(WPN_LOCATOR);
    pump(s, cls, 0.002, input);
    CHECK(lastPred == WPN_LOCATOR);
    pump(s, cls, 0.3, input);
    CHECK(sp.weapon == WPN_SNIPER && lastPred == WPN_SNIPER && wp.desired < 0 && lastCmdWeapon == kNoWeaponChange && switches - before == 2);
    maskOverride = 0; verify = true;
    // ...and one the client does see coming (outside the pool): no switch at all
    before = switches;
    wp.want(WPN_LOCATOR);
    pump(s, cls, 0.2, input);
    CHECK(sp.weapon == WPN_SNIPER && switches == before);
    // 5. the knife thrown: the weapon held before it, in the same command; asking for the knife while it's away: refused
    wp.want(WPN_KNIFE);
    pump(s, cls, 1.2, input);   // (out, and past the switch's refire)
    CHECK(sp.weapon == WPN_KNIFE);
    throwNext = true;
    buttons = BTN_ALTFIRE;
    pump(s, cls, 0.2, input);
    CHECK(!throwNext && sp.knifeBackAt > s.time() && sp.weapon != WPN_KNIFE && lastPred == sp.weapon && locked && sp.fireLocked);
    CHECK(sp.weapon == WPN_SNIPER);   // back to the weapon held before the knife (T14; it was the first slot, the rocket)
    buttons = 0;
    wp.want(WPN_KNIFE);
    pump(s, cls, 0.3, input);
    CHECK(sp.weapon != WPN_KNIFE && lastPred == sp.weapon && wp.desired < 0);
    // 6. dead: nothing switches (the server keeps the choice for the respawn)
    s.killPlayer(a.clientId(), -1, 252);
    pump(s, cls, 0.1, input);
    wp.want(WPN_LASER);
    pump(s, cls, 0.3, input);
    CHECK(!sp.alive && lastPred == a.selfWeapon() && wp.desired < 0);
    for (int k = 0; k < 100 && !sp.alive; k++) pump(s, cls, 0.05, input);
    pump(s, cls, 0.2, input);
    CHECK(sp.alive && sp.weapon == WPN_LASER && lastPred == WPN_LASER);
    // 7. a round trip like online play (the server and the client's network only every 40 ms): the switch shows from
    //    the key on while the snapshots still say the old weapon, and fire pressed in that window isn't locked when
    //    the confirmation comes
    auto lagPump = [&](double seconds) {
        double end = timeNow() + seconds, nextTick = timeNow(), nextNet = timeNow();
        while (timeNow() < end) {
            if (timeNow() >= nextNet) { s.frame(); for (auto* cl : cls) cl->poll(); nextNet += 0.04; }
            while (timeNow() >= nextTick) { for (size_t i = 0; i < cls.size(); i++) cls[i]->tick(input((int)i)); nextTick += a.tickPeriod(); }
            sleepMs(1);
        }
    };
    lagPump(0.3);
    int was = sp.weapon;
    CHECK(was == WPN_LASER);
    wp.want(WPN_RIFLE);
    lagPump(0.01);
    CHECK(lastPred == WPN_RIFLE && a.selfWeapon() == was);   // (shown now; the server's word still the old one)
    buttons = BTN_FIRE;
    lf = sp.lastFire;
    lagPump(0.8);   // (a few round trips: a loaded machine (the full run) sometimes missed it in 0.4 s)
    CHECK(sp.weapon == WPN_RIFLE && a.selfWeapon() == WPN_RIFLE && !locked && !sp.fireLocked && sp.lastFire != lf && wp.desired < 0);
    wp.want(WPN_SHOTGUN);   // (fire still held: locked)
    lagPump(0.3);
    CHECK(sp.weapon == WPN_SHOTGUN && locked && sp.fireLocked);
    buttons = 0;
    lagPump(0.2);
    CHECK(!locked && !sp.fireLocked);
    printf("  %d commands checked, %d weapons and %d fire locks predicted wrong\n", checked, wrong, lockWrong);
    CHECK(checked > 500 && wrong == 0 && lockWrong == 0);
    s.stop();
}

// F6: a hide and seek seeker who falls is back 2 s later (it stayed dead and the round idled out its 300 s).
static void testSeekerRespawn(uint16_t port) {
    printf("hide and seek: a fallen seeker respawns\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "hamlet"; cfg.mode = MODE_HIDE; cfg.port = port;
    NetClient c[3];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 3, cls)) { s.stop(); return; }
    int it = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].it) it = i;
    CHECK(it >= 0);
    if (it < 0) { s.stop(); return; }
    s.players_[it].move.pos.y = s.map_.killY() - 5;   // a fall
    pump(s, cls, 0.1, idle);
    CHECK(!s.players_[it].alive);
    pumpServerTime(s, cls, 1.5, idle);
    CHECK(!s.players_[it].alive);
    pumpServerTime(s, cls, 0.7, idle);
    printf("  seeker alive after 2.2 s: %d, still it: %d, phase %d\n", s.players_[it].alive, s.players_[it].it, s.ms_.phase);
    CHECK(s.players_[it].alive && s.players_[it].it && s.ms_.phase == PHASE_LIVE);
    s.stop();
}

// F5: not ranked, a match that loses a side holds still (no rounds handed to the side that's left), then after 10 s
// it's a forfeit win; an emptied server goes back to warmup. A forced short-handed start plays on.
static void testShortHanded(uint16_t port) {
    printf("short-handed matches: forfeit or warmup after 10 s\n");
    std::vector<std::string> logs;
    for (int mode : {MODE_ARENA, MODE_VS, MODE_KOTH}) {
        Server s;
        ServerConfig cfg;
        cfg.map = mode == MODE_KOTH ? "summit" : mode == MODE_ARENA ? "temple" : "bastion"; cfg.mode = mode; cfg.port = port++; cfg.balanceBots = false;
        NetClient c[2];
        std::vector<NetClient*> cls;
        if (!rulesStart(s, cfg, c, 2, cls)) { s.stop(); continue; }
        logs.clear();
        s.setLog([&](const std::string& l) { logs.push_back(l); });
        int stay = c[0].clientId();
        s.command("kick " + s.players_[c[1].clientId()].name);
        std::vector<NetClient*> one = {&c[0]};
        pumpServerTime(s, one, 2.0, idle);
        int scored = s.ms_.scoreA + s.ms_.scoreB;
        CHECK(s.short_ && s.ms_.phase != PHASE_MATCH_END && (mode == MODE_KOTH || scored == 0));
        s.shortSince_ = s.time() - 9.95;   // (most of the 10 s gone)
        pumpServerTime(s, one, 0.2, idle);
        bool named = false;
        for (auto& l : logs) named |= l.find(s.players_[stay].name + " wins") != std::string::npos || l.find("TEAM A wins") != std::string::npos || l.find("TEAM B wins") != std::string::npos;
        printf("  %s: one side left -> phase %d, %d rounds awarded on hold, forfeit called %d\n", modeName(mode), s.ms_.phase, scored, named);
        CHECK(s.ms_.phase == PHASE_MATCH_END && named);
        if (mode == MODE_ARENA) {   // 1v1: the call names the player, not TEAM A
            bool byName = false;
            for (auto& l : logs) byName |= l.find(s.players_[stay].name + " wins") != std::string::npos;
            CHECK(byName);
        }
        // everyone gone: back to warmup
        pumpServerTime(s, one, 0.1, idle);
        s.command("forcestart");
        pump(s, one, 0.1, idle);
        s.command("kick " + s.players_[stay].name);
        std::vector<NetClient*> none;
        pump(s, none, 0.3, idle);
        if (s.short_) s.shortSince_ = s.time() - 9.95;
        pumpServerTime(s, none, 0.2, idle);
        CHECK(s.ms_.phase == PHASE_WARMUP);
        s.stop();
    }
    {   // forcestart with one player (a host trying a map alone): it plays on
        Server s;
        ServerConfig cfg;
        cfg.map = "summit"; cfg.mode = MODE_KOTH; cfg.port = port++; cfg.balanceBots = false;
        NetClient c[1];
        std::vector<NetClient*> cls;
        if (rulesStart(s, cfg, c, 1, cls)) {
            pumpServerTime(s, cls, 0.5, idle);
            CHECK(!s.short_ && s.ms_.phase != PHASE_WARMUP && s.ms_.phase != PHASE_MATCH_END);
        }
        s.stop();
    }
}

// F7: play vs bots in 1V1 is one enemy and no allies whatever the team sliders say; duel its team size a side.
static void testVsBotsDuo(uint16_t port) {
    printf("play vs bots: 1v1 and duel sizes\n");
    for (int mode : {MODE_ARENA, MODE_DUEL}) {
        Server s;
        ServerConfig cfg;
        cfg.map = mode == MODE_ARENA ? "temple" : "clockwork"; cfg.mode = mode; cfg.port = port++;
        cfg.vsBots = true; cfg.botsEnemy = 3; cfg.botsAlly = 2; cfg.duelTeamSize = 2;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { CHECK(false); continue; }
        int a = 0, b = 0;
        for (auto& p : s.players_) if (p.connected && p.isBot) (p.team == TEAM_A ? a : b)++;
        printf("  %s: %d allies, %d enemies\n", modeName(mode), a, b);
        CHECK(mode == MODE_ARENA ? (a == 0 && b == 1) : (a == 1 && b == 2));
        s.stop();
    }
}

// F9 + T15: payload / multi point stopwatch. The first round's call is the mark to beat (no ROUND WON for either
// side); the second ends as soon as the attackers pass it, with a call that agrees with the match result.
static void testStopwatch(uint16_t port) {
    printf("payload / multi point: stopwatch calls and early end\n");
    for (int mode : {MODE_PAYLOAD, MODE_POINTS}) {
        Server s;
        ServerConfig cfg;
        cfg.map = mode == MODE_PAYLOAD ? "railyard" : "midtown"; cfg.mode = mode; cfg.port = port++; cfg.balanceBots = false;
        NetClient c[2];
        std::vector<NetClient*> cls;
        if (!rulesStart(s, cfg, c, 2, cls)) { s.stop(); continue; }
        std::vector<std::string> logs;
        s.setLog([&](const std::string& l) { logs.push_back(l); });
        int first = s.ms_.attackers;
        // round 1: some progress, then time out
        if (mode == MODE_PAYLOAD) s.ms_.payloadDist = 20.0f; else s.ms_.capture = 0.5f;
        pump(s, cls, 0.1, idle);
        float mark = s.bestProgress_[first];
        s.phaseEnd_ = s.time() - 10;
        pumpServerTime(s, cls, 0.2, idle);
        CHECK(s.ms_.phase == PHASE_ROUND_END);
        bool beat = false;
        for (auto& l : logs) beat |= l.find("must beat it") != std::string::npos;
        printf("  %s round 1: \"%s\"\n", modeName(mode), logs.empty() ? "" : logs.back().c_str());
        CHECK(beat && mark > 0);
        // round 2: the other side passes the mark early: it ends right there, theirs
        s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
        for (int k = 0; k < 40 && s.ms_.phase != PHASE_LIVE; k++) { s.phaseEnd_ = s.time(); pump(s, cls, 0.05, idle); }
        CHECK(s.ms_.round == 2 && s.ms_.attackers != first && s.ms_.phase == PHASE_LIVE);
        pump(s, cls, 0.2, idle);
        CHECK(s.ms_.phase == PHASE_LIVE);   // (not yet past it)
        logs.clear();
        if (mode == MODE_PAYLOAD) s.ms_.payloadDist = mark + 1.0f; else s.ms_.capture = std::min(0.99f, mark + 0.2f);
        pump(s, cls, 0.1, idle);
        printf("  %s round 2: phase %d \"%s\"\n", modeName(mode), s.ms_.phase, logs.empty() ? "" : logs.back().c_str());
        CHECK(s.ms_.phase == PHASE_ROUND_END && !logs.empty() && logs.back().find("beats") != std::string::npos);
        s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
        CHECK(s.ms_.phase == PHASE_MATCH_END);
        bool won = false;
        for (auto& l : logs) won |= l.find(std::string(first == TEAM_A ? "TEAM B" : "TEAM A") + " wins the match") != std::string::npos;
        CHECK(won);
        s.stop();
    }
    {   // against a finished attack: once the clock is past its time the round ends, the first side's
        Server s;
        ServerConfig cfg;
        cfg.map = "railyard"; cfg.mode = MODE_PAYLOAD; cfg.port = port++; cfg.balanceBots = false;
        NetClient c[2];
        std::vector<NetClient*> cls;
        if (!rulesStart(s, cfg, c, 2, cls)) { s.stop(); return; }
        int first = s.ms_.attackers;
        s.ms_.payloadDist = s.ms_.payloadLen;
        pump(s, cls, 0.1, idle);
        CHECK(s.ms_.phase == PHASE_ROUND_END && s.completeTime_[first] > 0);
        s.completeTime_[first] = 100;   // (a 1:40 delivery: the instant one here would lock round 2 before it's checked)
        for (int k = 0; k < 40 && !(s.ms_.round == 2 && s.ms_.phase == PHASE_LIVE); k++) { s.phaseEnd_ = s.time(); pump(s, cls, 0.05, idle); }
        CHECK(s.ms_.round == 2 && s.ms_.phase == PHASE_LIVE);
        s.roundStart_ = s.time() - s.completeTime_[first] - 0.5;   // (their clock is past the first delivery)
        pump(s, cls, 0.1, idle);
        CHECK(s.ms_.phase == PHASE_ROUND_END);
        s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
        CHECK(s.ms_.phase == PHASE_MATCH_END && s.ms_.scoreA + s.ms_.scoreB == 1);
        s.stop();
    }
}

// F23: multi point on a map without points can't start (it never ended).
static void testPointsNoPoints(uint16_t port) {
    printf("multi point on a map without points\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "harbor"; cfg.mode = MODE_POINTS; cfg.port = port; cfg.balanceBots = false;
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls, false)) { s.stop(); return; }
    s.command("forcestart");
    pump(s, cls, 0.2, idle);
    CHECK(s.map_.points.empty() && s.ms_.phase == PHASE_WARMUP);
    s.stop();
}

static void testGameRules(const std::string& only) {
    struct R { const char* name; std::function<void()> run; };
    std::vector<R> all = {
        {"overtime", [] { testOvertime(TP(29230)); testBombOvertimePlant(TP(29245)); }},
        {"syringe", [] { testSyringeModes(); }},
        {"throws", [] { testThrowCooldown(TP(29240)); }},
        {"hull", [] { testSizeHull(TP(29242)); }},
        {"grapple", [] { testGrappleNoBrake(); testGrapplePmove(); }},
        {"grapplep", [] { testGrapplePlayers(TP(29270)); testGrappleTeammate(TP(29271)); testGrappleLagComp(TP(29272)); testGrappleBots(TP(29273)); }},
        {"head", [] { testHeadBox(TP(29246)); }},
        {"hide", [] { testHideScoring(TP(29247)); testSeekerRespawn(TP(29287)); }},
        {"shorthanded", [] { testShortHanded(TP(29288)); }},
        {"vsbots", [] { testVsBotsDuo(TP(29294)); }},
        {"stopwatch", [] { testStopwatch(TP(29296)); testPointsNoPoints(TP(29299)); }},
        {"tag", [] { testTagEscape(TP(29248)); testTagNextMatch(TP(29274)); }},
        {"rps", [] { testRpsTwo(TP(29255)); testRpsMany(TP(29256)); testHideTimeTiebreak(TP(29290)); }},
        {"locator", [] { testLocator(TP(29260)); }},
        {"aware", [] { testItAwareness(TP(29263)); }},
        {"bomb", [] { testBombDrop(TP(29249)); }},
        {"cartbox", [] { testCartBoxPlace(TP(29275)); }},
        {"botdefer", [] { testBalanceBotDefer(TP(29250)); }},
        {"spawns", [] { testSpawnTypes(TP(29251)); }},
        {"seeds", [] { testRandomSeeds(); }},
        {"heal", [] { testLifeHealCap(TP(29253)); }},
        {"scoreboard", [] { testScoreboardValue(); }},
        {"hudclock", [] { testHudClock(TP(29280)); }},
        {"weaponpredict", [] { testWeaponPredict(TP(29286)); }},
    };
    for (auto& r : all) if (only.empty() || only == r.name) r.run();
    if (only == "rate") { testRateBetweenRounds(TP(29252)); testRateUndoStepUp(TP(29254)); testRateJitter(TP(29257)); testUploadCap(TP(29258)); testSharedCongestion(TP(29259)); }   // (timing-sensitive: the suite runs it alone, with "tickrate")
}
