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
static bool rpsSetup(Server& s, ServerConfig cfg, NetClient* c, int n, std::vector<NetClient*>& cls) {
    cfg.map = "hamlet"; cfg.mode = MODE_HIDE;
    if (!rulesStart(s, cfg, c, n, cls, false)) return false;
    s.command("forcestart");
    pump(s, cls, 0.2, idle);
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);   // live
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);   // time's up: round end
    CHECK(s.ms_.phase == PHASE_ROUND_END);
    for (auto& p : s.players_) if (p.connected) { p.sought = true; p.score = 3; }   // every round played, all on 3
    s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);
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
        for (auto& p : s.players_) if (p.connected) { p.sought = true; p.score = 2; }
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
        CHECK(locates(oc) == 0);                           // (only to "it"; the hider's locator does nothing)
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
    // the eliminated player leaves and comes back: still out (by name; rank id when there is one)
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
    pump(s, cls, 0.1, idle);
    for (auto& sp : s.players_) if (sp.awayUntil > 0) sp.awayUntil = 1e-9;   // (skip the 10 s reconnect grace: gone for good)
    pump(s, cls, 0.2, idle);
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

// GAME-11: the tick rate doesn't change mid-round; a host that falls behind steps down at the next break.
static void testRateBetweenRounds(uint16_t port) {
    printf("tick rate changes only between rounds\n");
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
    c[0].overrideReportedLoss(50);           // (what a struggling host looks like: clients losing snapshots)
    pump(s, cls, 3.0 * 2.5, idle);           // two and a half rate windows, mid-match
    printf("  mid-match, losing snapshots: %d Hz (phase %d)\n", s.tickRate(), s.ms_.phase);
    CHECK(s.ms_.phase == PHASE_LIVE && s.tickRate() == 1000);
    s.phaseEnd_ = s.time();                  // time's up: the match is over
    pump(s, cls, 0.5, idle);
    printf("  match over: %d Hz (phase %d)\n", s.tickRate(), s.ms_.phase);
    CHECK(s.ms_.phase == PHASE_MATCH_END && s.tickRate() == 500);   // stepped down at once, at the break
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
    c[0].overrideReportedLoss(50);           // the raise doesn't hold
    for (int k = 0; k < 30 && s.tickRate() != 500; k++) pump(s, cls, 0.25, idle);
    printf("  live, losing snapshots after the raise: %d Hz (phase %d)\n", s.tickRate(), s.ms_.phase);
    CHECK(s.ms_.phase == PHASE_LIVE && s.tickRate() == 500);   // undone mid-round, not after it
    pump(s, cls, 3.0 * 2.5, idle);           // still losing, but that's no longer our own raise: waits for the break
    printf("  still losing: %d Hz (phase %d)\n", s.tickRate(), s.ms_.phase);
    CHECK(s.ms_.phase == PHASE_LIVE && s.tickRate() == 500 && s.rateDownPending_);
    s.phaseEnd_ = s.time();                  // time's up: the match is over
    pump(s, cls, 0.5, idle);
    CHECK(s.ms_.phase == PHASE_MATCH_END && s.tickRate() == 250);   // (the pending step down, at the break)
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
        {"rps", [] { testRpsTwo(TP(29255)); testRpsMany(TP(29256)); }},
        {"locator", [] { testLocator(TP(29260)); }},
        {"aware", [] { testItAwareness(TP(29263)); }},
        {"bomb", [] { testBombDrop(TP(29249)); }},
        {"botdefer", [] { testBalanceBotDefer(TP(29250)); }},
        {"spawns", [] { testSpawnTypes(TP(29251)); }},
        {"seeds", [] { testRandomSeeds(); }},
        {"heal", [] { testLifeHealCap(TP(29253)); }},
        {"scoreboard", [] { testScoreboardValue(); }},
    };
    for (auto& r : all) if (only.empty() || only == r.name) r.run();
    if (only == "rate") { testRateBetweenRounds(TP(29252)); testRateUndoStepUp(TP(29254)); }   // (timing-sensitive: the suite runs it alone, with "tickrate")
}
