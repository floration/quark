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

// 5.3: the knife throw and the syringe throw share one alt-fire cooldown; a knife throw makes the weapon you switch
// to wait a knife swing (no throw + instant crossbow headshot); a teammate's syringe resets the throw cooldown too.
static void testThrowCooldown(uint16_t port) {
    printf("shared throw cooldown\n");
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
    double t0 = s.time();
    printf("  after the knife throw: weapon %d ready in %.2f s, throws in %.2f s\n", pa.weapon, pa.nextFire - t0, pa.throwReadyAt - t0);
    CHECK(pa.weapon != WPN_KNIFE && pa.nextFire >= t0 + weaponDef(WPN_KNIFE).refire - 0.05);   // not instantly
    CHECK(pa.throwReadyAt >= t0 + kKnifeThrowCooldown - 0.05);
    ba = 0; wa = WPN_SYRINGE; pump(s, cls, 0.9, in);   // let go, take the syringe, wait out the weapon refire
    ba = BTN_ALTFIRE; pump(s, cls, 0.5, in);           // throw it: refused (shared cooldown)
    CHECK(count(WPN_SYRINGE, c[0].clientId()) == 0);
    ba = 0;
    // b throws a syringe at a (5 m in front): every cooldown resets, the throws' too
    Vec3 base = pb.move.pos;
    auto place = [&] { pb.move.pos = base; pb.move.vel = {}; pa.move.pos = base + Vec3(0, 0, -5); pa.move.vel = {}; };
    pa.health = 40;
    for (int k = 0; k < 5; k++) { place(); pump(s, cls, 0.02, in); }
    CHECK(pa.throwReadyAt > s.time() + 0.3);
    pa.throwReadyAt = s.time() + 30;   // (so only the reset can clear it)
    int tick = 0;
    pump(s, cls, 0.5, [&](int i) { place(); UserCmd u = in(i); if (i == 1 && tick++ < 30) u.buttons = BTN_ALTFIRE; return u; });
    printf("  healed: %.0f hp, throws ready in %.2f s\n", pa.health, pa.throwReadyAt - s.time());
    CHECK(pa.health > 60 && pa.throwReadyAt <= s.time());
    CHECK(pb.throwReadyAt > s.time());   // (the thrower's own cooldown runs)
    ba = BTN_ALTFIRE;
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

// 5.5: the head is a head-sized box on top, not the full-width top 20%: shoulders are body shots. The sniper does a
// flat 50.
static void testHeadBox(uint16_t port) {
    printf("headshots: the head, not the shoulders\n");
    CHECK(weaponDef(WPN_SNIPER).damage == 50 && headDamage(WPN_SNIPER) == 50);
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

// 5.6: hide and seek. Everyone seeks once (rounds = players, not clamp(players, 3, 6) in slot order), and a hider
// and the seeker split the points by how long the hider stayed hidden (no +2 just for surviving).
static void testHideScoring(uint16_t port) {
    printf("hide and seek: everyone seeks once, points by time hidden\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "hamlet"; cfg.mode = MODE_HIDE; cfg.port = port;
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls, false)) { s.stop(); return; }
    s.command("forcestart");
    pump(s, cls, 0.2, idle);
    auto itNow = [&] { for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].it) return i; return -1; };
    int first = itNow();
    CHECK(first >= 0);
    if (first < 0) { s.stop(); return; }
    int other = c[0].clientId() == first ? c[1].clientId() : c[0].clientId();
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);   // freeze over
    CHECK(s.ms_.phase == PHASE_LIVE);
    s.liveSince_ = s.time() - kHideRoundTime * 0.25;      // found a quarter of the way in: 1 of 4 to the hider, 3 to the seeker
    s.players_[first].itFreezeUntil = 0;
    for (int k = 0; k < 8 && s.players_[other].alive; k++) {
        s.players_[first].move.pos = {0, 0, 6}; s.players_[first].move.vel = {};
        s.players_[other].move.pos = {0, 0, 5}; s.players_[other].move.vel = {};
        pump(s, cls, 0.05, [&](int i) { UserCmd u; u.yaw = 0; u.pitch = -0.3f; if (cls[i]->clientId() == first) u.buttons = BTN_FIRE; return u; });
    }
    pump(s, cls, 0.1, idle);
    printf("  round 1: %s found at 1/4 -> seeker %d, hider %d\n", s.players_[other].name.c_str(), s.players_[first].score, s.players_[other].score);
    CHECK(s.ms_.phase == PHASE_ROUND_END && s.players_[first].score == 3 && s.players_[other].score == 1);
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);   // round 2: the other player seeks
    CHECK(s.ms_.round == 2 && itNow() == other);
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    CHECK(s.ms_.phase == PHASE_LIVE);
    s.liveSince_ = s.time() - kHideRoundTime;           // hidden the whole round: all 4
    s.phaseEnd_ = s.time();
    pump(s, cls, 0.1, idle);
    printf("  round 2: hidden to the end -> %d, seeker %d\n", s.players_[first].score, s.players_[other].score);
    CHECK(s.ms_.phase == PHASE_ROUND_END && s.players_[first].score == 7 && s.players_[other].score == 1);
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);   // two players, two rounds: over (it used to be 3, one seeking twice)
    CHECK(s.ms_.phase == PHASE_MATCH_END && s.ms_.round == 2);
    s.stop();
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
    // the eliminated player leaves and comes back: still out (by name; rank id when there is one)
    c[ci].disconnect();
    pump(s, cls, 0.3, idle);
    NetClient again;
    cls[ci] = &again;
    again.connect("127.0.0.1:" + std::to_string(cfg.port), itName);
    pump(s, cls, 0.5, idle);
    int back = again.clientId();
    CHECK(back >= 0 && s.players_[back].connected && s.players_[back].team == TEAM_SPECTATOR);   // joins mid-match as a spectator
    if (back >= 0) {
        s.players_[back].team = TEAM_A;    // (even if it were put on a side) it stays out
        pump(s, cls, 0.2, idle);
        CHECK(s.players_[back].out && !s.players_[back].alive);
    }
    // next round: two left. "It" disconnecting ends the match: the other one wins
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
    CHECK(s.ms_.phase == PHASE_LIVE);
    int it2 = itNow();
    CHECK(it2 >= 0 && it2 != back);
    for (int i = 0; i < 3; i++) if (it2 >= 0 && i != ci && c[i].clientId() == it2) c[i].disconnect();
    pump(s, cls, 0.3, idle);
    printf("  it left: phase %d\n", s.ms_.phase);
    CHECK(s.ms_.phase == PHASE_MATCH_END);
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

// "quark_tests rules [name]": all of them, or the one named.
static void testGameRules(const std::string& only) {
    struct R { const char* name; std::function<void()> run; };
    std::vector<R> all = {
        {"overtime", [] { testOvertime(TP(29230)); }},
        {"syringe", [] { testSyringeModes(); }},
        {"throws", [] { testThrowCooldown(TP(29240)); }},
        {"hull", [] { testSizeHull(TP(29242)); }},
        {"grapple", [] { testGrappleNoBrake(); }},
        {"head", [] { testHeadBox(TP(29246)); }},
        {"hide", [] { testHideScoring(TP(29247)); }},
        {"tag", [] { testTagEscape(TP(29248)); }},
        {"bomb", [] { testBombDrop(TP(29249)); }},
        {"botdefer", [] { testBalanceBotDefer(TP(29250)); }},
    };
    for (auto& r : all) if (only.empty() || only == r.name) r.run();
}
