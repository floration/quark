// Headless tests: movement, map parsing, and real client/server sessions over UDP
// on localhost, including every game mode.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "game/server.h"
#include "game/netclient.h"
#include "game/gamemodes.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

// Pump a server and some clients for `seconds` of real time, feeding each client an input.
template <class F>
static void pump(Server& s, std::vector<NetClient*>& cls, double seconds, F input) {
    double end = timeNow() + seconds;
    double nextTick = timeNow();
    while (timeNow() < end) {
        s.frame();
        for (auto* c : cls) c->poll();
        if (timeNow() >= nextTick) {
            for (size_t i = 0; i < cls.size(); i++) cls[i]->tick(input((int)i));
            nextTick += cls.empty() ? 0.001 : cls[0]->tickPeriod();
        }
        sleepMs(1);
    }
}

static void testMovement() {
    printf("movement\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -50 -1 -50 50 0 50\nbox 5 0 -50 6 3 50\nbox -3 0 -50 -2 0.4 50\nspawn any 0 0 0\n", &err));
    MoveState s;
    s.pos = {0, 2, 0};
    UserCmd c;
    c.msec = 8;
    for (int i = 0; i < 250; i++) pmove(s, c, m);
    CHECK(std::fabs(s.pos.y) < 0.01f);
    CHECK(s.flags & MF_ONGROUND);
    // walk +x into the wall
    c.yaw = -PI / 2; // facing +x
    c.forward = 127;
    for (int i = 0; i < 500; i++) pmove(s, c, m);
    CHECK(s.pos.x < 5.0f - kPlayerHalfWidth + 0.01f && s.pos.x > 4.0f);
    // walk -x over the small step
    c.yaw = PI / 2;
    for (int i = 0; i < 375; i++) pmove(s, c, m);
    CHECK(s.pos.x < -3.5f);
    // jump
    c.forward = 0;
    c.buttons = BTN_JUMP;
    float maxY = 0;
    for (int i = 0; i < 125; i++) { pmove(s, c, m); maxY = std::max(maxY, s.pos.y); }
    CHECK(maxY > 1.0f);
}

// The same inputs must give bit-identical movement at 1000, 500, 250 and 125 Hz.
static void testTickRateIndependence() {
    printf("movement identical at every tick rate (incl. dash, bhop, grapple)\n");
    Map m;
    CHECK(m.load("arena"));
    MoveState results[4];
    for (int level = 0; level < kTickRateLevels; level++) {
        int msec = 1000 / kTickRates[level];
        MoveState s;
        s.pos = {-20, 0, 20};
        for (int t = 0; t < 3000; t += msec) {   // 3 s, input changes every 8 ms
            int win = t / 8;
            UserCmd c;
            c.msec = (uint8_t)msec;
            c.yaw = 0.3f + win * 0.004f;
            c.pitch = 0.15f;
            c.forward = 127;
            c.side = (int8_t)((win / 40) % 2 ? 90 : -90);
            if (win > 20) c.buttons |= BTN_JUMP;                    // hold space: auto bhop
            if (win % 150 == 30) c.buttons |= BTN_DASH;            // periodic dash
            if (win >= 200 && win < 260) c.buttons |= BTN_GRAPPLE; // grapple for ~0.5 s
            pmove(s, c, m);
        }
        results[level] = s;
    }
    for (int level = 1; level < kTickRateLevels; level++) {
        CHECK(results[level].pos.x == results[0].pos.x && results[level].pos.y == results[0].pos.y && results[level].pos.z == results[0].pos.z);
        CHECK(results[level].vel.x == results[0].vel.x && results[level].flags == results[0].flags);
    }
    printf("  end position (%.3f, %.3f, %.3f)\n", results[0].pos.x, results[0].pos.y, results[0].pos.z);
}

static void testMaps() {
    printf("maps\n");
    Map arena, sprint;
    std::string err;
    CHECK(arena.load("arena", &err));
    if (!err.empty()) printf("  %s\n", err.c_str());
    CHECK(sprint.load("sprint", &err));
    for (int m : {MODE_VS, MODE_BOMB, MODE_KOTH, MODE_PAYLOAD, MODE_FFA}) CHECK(arena.supportsMode(m));
    CHECK(sprint.supportsMode(MODE_RACE));
    CHECK(arena.payloadLength() > 20);
}

static void testGridQueries() {
    printf("grid raycast / collision vs brute force\n");
    Map m;
    CHECK(m.load("arena"));
    auto brute = [&](const Vec3& o, const Vec3& d, float maxT) {
        Vec3 inv = safeInv(d);
        float best = -1;
        for (auto& b : m.brushes) { float t = rayAABB(o, inv, b.box, best < 0 ? maxT : best); if (t >= 0 && (best < 0 || t < best)) best = t; }
        return best;
    };
    srand(7);
    auto rnd = [](float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); };
    int mism = 0;
    double tGrid = 0, tBrute = 0;
    for (int i = 0; i < 20000; i++) {
        Vec3 o = {rnd(-40, 40), rnd(-2, 10), rnd(-40, 40)};
        Vec3 d = normalize(Vec3(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)));
        if (i % 10 == 0) d = {0, -1, 0};
        double t0 = timeNow();
        float a = m.raycast(o, d, 120);
        double t1 = timeNow();
        float b = brute(o, d, 120);
        double t2 = timeNow();
        tGrid += t1 - t0; tBrute += t2 - t1;
        if ((a < 0) != (b < 0) || (a >= 0 && std::fabs(a - b) > 1e-3f)) mism++;
        if (m.raycastAny(o, d, 120) != (b >= 0)) mism++;
        AABB box = playerBox(o, false);
        bool solidBrute = false;
        for (auto& br : m.brushes) solidBrute |= box.overlaps(br.box);
        if (m.boxSolid(box) != solidBrute) mism++;
    }
    CHECK(mism == 0);
    printf("  raycast: grid %.2fus, brute %.2fus per ray\n", tGrid / 20000 * 1e6, tBrute / 20000 * 1e6);
}

static UserCmd idle(int) { return UserCmd(); }

static void testSession(int mode, const char* mapName, uint16_t port) {
    printf("session: %s on %s\n", modeName(mode), mapName);
    Server s;
    ServerConfig cfg;
    cfg.map = mapName;
    cfg.mode = mode;
    cfg.port = port;
    cfg.roundsToWin = mode == MODE_BOMB ? 2 : 1;
    cfg.timeLimit = 3;
    cfg.scoreLimit = 3;
    s.setLog([](const std::string&) {});
    std::string err;
    if (!s.start(cfg, &err)) { printf("  server start failed: %s\n", err.c_str()); g_fail++; return; }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "alpha");
    b.connect("127.0.0.1:" + std::to_string(port), "bravo");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 0.5, idle);
    CHECK(a.state() == ConnState::Connected);
    CHECK(b.state() == ConnState::Connected);
    CHECK(s.playerCount() == 2);
    CHECK(a.hasSnapshot() && b.hasSnapshot());
    if (!a.hasSnapshot() || !b.hasSnapshot()) { s.stop(); return; }
    CHECK(a.playerName(b.clientId()) == "bravo");
    CHECK(a.mode().phase == PHASE_WARMUP || a.mode().phase == PHASE_FREEZE);

    // Warmup countdown (5s) then freeze (3s); then live.
    pump(s, cls, 8.6, idle);
    CHECK(s.ms_.phase == PHASE_LIVE);
    CHECK(a.mode().phase == PHASE_LIVE);

    // Prediction must agree with the server for a moving player.
    auto walk = [](int i) { UserCmd c; c.forward = i == 0 ? 127 : 0; c.side = 40; return c; };
    pump(s, cls, 1.0, walk);
    pump(s, cls, 0.3, idle);
    const ServerPlayer& sp = s.players_[a.clientId()];
    if (sp.alive) CHECK(length(sp.move.pos - a.predicted().pos) < 0.05f);

    // Mode specific: make something happen.
    if (mode == MODE_VS || mode == MODE_BOMB || mode == MODE_FFA || mode == MODE_PAYLOAD) {
        int victim = b.clientId();
        s.damagePlayer(victim, a.clientId(), 500, WPN_RIFLE, {});
        pump(s, cls, 0.3, idle);
        bool sawKill = false;
        for (auto& e : a.takeEvents()) if (e.type == EV_KILL && e.b == victim) sawKill = true;
        CHECK(sawKill);
    }
    if (mode == MODE_VS) {
        pump(s, cls, 0.2, idle);
        CHECK(s.ms_.phase == PHASE_ROUND_END || s.ms_.phase == PHASE_MATCH_END);
        CHECK(s.ms_.scoreA + s.ms_.scoreB == 1);
    }
    if (mode == MODE_FFA) {
        pump(s, cls, 3.0, idle); // time limit 3s
        CHECK(s.ms_.phase == PHASE_MATCH_END);
    }
    if (mode == MODE_KOTH) {
        // Put alpha on the hill, alone.
        auto& p = s.players_[a.clientId()];
        p.move.pos = s.map_.hill.center();
        p.move.pos.y = s.map_.hill.mn.y;
        pump(s, cls, 3.5, idle);
        CHECK(s.ms_.phase == PHASE_MATCH_END);
    }
    if (mode == MODE_BOMB) {
        // bravo was killed; wait for next round then plant with the carrier.
        pump(s, cls, 4.5 + 3.2, idle);
        CHECK(s.ms_.phase == PHASE_LIVE);
        int carrier = s.ms_.bombCarrier;
        CHECK(carrier != 255);
        if (carrier != 255) {
            auto& p = s.players_[carrier];
            Vec3 c = s.map_.bombSites[0].center();
            p.move.pos = {c.x, 0.1f, c.z};
            NetClient* cc = carrier == a.clientId() ? &a : &b;
            auto plant = [&](int i) { UserCmd u; if (cls[i] == cc) u.buttons = BTN_USE; return u; };
            pump(s, cls, 3.5, plant);
            CHECK(s.ms_.bombState == BOMB_PLANTED);
            CHECK(cc->mode().bombState == BOMB_PLANTED);
        }
    }
    if (mode == MODE_PAYLOAD) {
        pump(s, cls, 4.5 + 3.2, idle); // round 2 starts after the kill ended round 1? (no: respawn mode)
    }
    if (mode == MODE_RACE) {
        auto& p = s.players_[a.clientId()];
        for (size_t i = 0; i < s.map_.checkpoints.size(); i++) {
            const AABB& c = s.map_.checkpoints[i];
            p.move.pos = {c.center().x, c.mn.y + 0.01f, c.center().z};
            p.move.vel = {};
            pump(s, cls, 0.1, idle);
        }
        CHECK(s.ms_.phase == PHASE_ROUND_END || s.ms_.phase == PHASE_MATCH_END);
        CHECK(s.players_[a.clientId()].score == 1);
    }

    // String commands + chat delivered reliably.
    a.sendCommand("say hello there");
    pump(s, cls, 0.3, idle);
    bool chat = false;
    for (auto& e : b.takeEvents()) if (e.type == EV_CHAT && e.text == "hello there") chat = true;
    CHECK(chat);

    a.disconnect();
    pump(s, cls, 0.2, idle);
    CHECK(s.playerCount() == 1);
    s.stop();
}

static void testPayloadPush(uint16_t port) {
    printf("payload push\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "arena"; cfg.mode = MODE_PAYLOAD; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 0.4, idle);
    s.command("forcestart");
    pump(s, cls, 3.2, idle);
    CHECK(s.ms_.phase == PHASE_LIVE);
    int att = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].team == s.ms_.attackers) att = i;
    CHECK(att >= 0);
    if (att >= 0) {
        for (int k = 0; k < 20; k++) {
            s.players_[att].move.pos = s.map_.payloadPoint(s.ms_.payloadDist);
            pump(s, cls, 0.1, idle);
        }
        CHECK(s.ms_.payloadDist > 2.0f);
    }
    s.stop();
}

static void testPacketLoss(uint16_t port) {
    printf("lag compensation hitscan\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "shooter");
    b.connect("127.0.0.1:" + std::to_string(port), "target");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 2.5, idle); // warmup spawn
    auto& sa = s.players_[a.clientId()];
    auto& sb = s.players_[b.clientId()];
    CHECK(sa.alive && sb.alive);
    sa.move.pos = {9, 0, 3}; sb.move.pos = {9, 0, -3};
    sa.move.vel = sb.move.vel = {};
    pump(s, cls, 0.4, idle);
    // Aim from a's eye toward b's chest, using the interpolated view a sees.
    auto fire = [&](int i) {
        UserCmd c;
        if (i == 0) {
            c.yaw = 0; // looking down -z
            const PlayerSnap* t = nullptr;
            auto ip = a.interpolatedPlayers();
            if (ip[b.clientId()].valid) t = &ip[b.clientId()].snap;
            Vec3 eye = a.predicted().pos + Vec3(0, kEyeHeight, 0);
            Vec3 tgt = t ? t->pos + Vec3(0, 1.0f, 0) : Vec3(0, 2, -3);
            Vec3 d = normalize(tgt - eye);
            c.pitch = std::asin(d.y);
            c.yaw = std::atan2(-d.x, -d.z);
            c.buttons = BTN_FIRE;
        }
        return c;
    };
    float hp0 = sb.health;
    pump(s, cls, 0.5, fire);
    CHECK(sb.health < hp0 || !sb.alive);
    s.stop();
}

// `quark_tests bot <addr> <seconds> [name]`: a headless player that wanders and shoots (for manual/visual testing).
static int runBot(const std::string& addr, double seconds, const std::string& name) {
    NetClient c;
    c.connect(addr, name);
    double end = timeNow() + seconds, next = timeNow();
    float yaw = 0;
    while (timeNow() < end) {
        c.poll();
        if (c.state() == ConnState::Disconnected) { printf("bot: %s\n", c.error().c_str()); return 1; }
        if (timeNow() >= next) {
            UserCmd u;
            yaw += 0.01f;
            u.yaw = yaw;
            u.forward = 100;
            u.side = (int8_t)(std::sin(timeNow()) * 100);
            if (((int)timeNow()) % 3 == 0) u.buttons |= BTN_JUMP;
            c.tick(u);
            next += c.tickPeriod();
        }
        sleepMs(1);
    }
    c.disconnect();
    return 0;
}

static void testWeapons(uint16_t port) {
    printf("weapons: sniper (scoped/unscoped), laser, rocket\n");
    for (int w : {WPN_SNIPER, WPN_LASER, WPN_ROCKET, WPN_SHOTGUN, WPN_CROSSBOW, WPN_STICKY}) {
        Server s;
        ServerConfig cfg;
        cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port++;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; continue; }
        NetClient a, b;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "shooter");
        b.connect("127.0.0.1:" + std::to_string(cfg.port), "target");
        std::vector<NetClient*> cls = {&a, &b};
        pump(s, cls, 2.5, idle);
        auto& sa = s.players_[a.clientId()];
        auto& sb = s.players_[b.clientId()];
        float dist = (w == WPN_LASER || w == WPN_SHOTGUN) ? 8.0f : 20.0f;
        sa.move.pos = {9, 0, 6}; sb.move.pos = {9, 0, 6 - dist};
        sa.move.vel = sb.move.vel = {};
        pump(s, cls, 0.3, idle);
        auto aim = [&](int i, uint16_t extra) {
            UserCmd c;
            if (i != 0) return c;
            Vec3 eye = a.predicted().pos + Vec3(0, kEyeHeight, 0);
            Vec3 tgt = sb.move.pos + Vec3(0, w == WPN_ROCKET ? 0.2f : 1.0f, 0);
            Vec3 d = normalize(tgt - eye);
            c.pitch = std::asin(d.y);
            c.yaw = std::atan2(-d.x, -d.z);
            c.weapon = (uint8_t)w;
            c.buttons = extra;
            return c;
        };
        pump(s, cls, 0.3, [&](int i) { return aim(i, 0); }); // switch weapon
        CHECK(sa.weapon == w);
        int deaths0 = sb.deaths;
        float hp0 = sb.health;
        double t0 = timeNow();
        pump(s, cls, w == WPN_LASER ? 0.5 : (w == WPN_ROCKET || w == WPN_CROSSBOW || w == WPN_STICKY || w == WPN_SHOTGUN) ? 0.2 : 0.3, [&](int i) { return aim(i, BTN_FIRE | (w == WPN_SNIPER ? BTN_ALTFIRE : 0)); });
        double elapsed = timeNow() - t0;
        if (w == WPN_ROCKET || w == WPN_CROSSBOW || w == WPN_STICKY) pump(s, cls, 1.0, idle); // let it fly
        float dealt = (sb.deaths > deaths0) ? 100.0f : hp0 - sb.health;
        if (w == WPN_SNIPER) CHECK(dealt >= 50);                 // one scoped shot
        if (w == WPN_LASER) {                                     // ~5 dmg every 0.05 s
            float expect = (float)(elapsed / weaponDef(WPN_LASER).refire) * weaponDef(WPN_LASER).damage;
            CHECK(dealt > expect * 0.6f && dealt < expect * 1.4f + 5);
        }
        if (w == WPN_ROCKET) CHECK(dealt > 15);
        if (w == WPN_SHOTGUN) CHECK(dealt >= 25);      // most of 10 x 5 pellets at 8 m
        if (w == WPN_CROSSBOW) CHECK(dealt >= 50);
        if (w == WPN_STICKY) {                          // lobbed: lands near the target, then detonate
            int stuck = 0;
            for (auto& pr : s.projectiles_) stuck += pr.active && pr.type == WPN_STICKY;
            CHECK(stuck >= 1);
            float before = sb.health; int d0 = sb.deaths;
            for (auto& pr : s.projectiles_) if (pr.active && pr.type == WPN_STICKY) pr.pos = sb.move.pos + Vec3(0, 0.1f, 0.5f);
            pump(s, cls, 0.2, [&](int i) { UserCmd c; c.weapon = WPN_STICKY; if (i == 0) c.buttons = BTN_ALTFIRE; return c; });
            CHECK(sb.deaths > d0 || sb.health < before - 15);
        }
        s.stop();
    }
}

static void testTickRateFallback(uint16_t port) {
    printf("tick rate starts at 1000 Hz and steps down when the host can't keep up\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    CHECK(s.tickRate() == 1000);
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "solo");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 1.0, idle);
    CHECK(a.tickRate() == s.tickRate());
    // Simulate a client reporting heavy snapshot loss during warmup.
    a.overrideReportedLoss(50);
    pump(s, cls, 3.2 * 4, idle);
    CHECK(s.tickRate() == 125); // floor: never below 125 Hz
    CHECK(a.tickRate() == 125);
    s.stop();
}

static void testRocketSelfDamage(uint16_t port) {
    printf("rocket self damage: 10%% below you, full when level or above\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "jumper");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 2.5, idle);
    auto& p = s.players_[a.clientId()];
    auto shoot = [&](float pitch, Vec3 at, float yaw = 0) {
        p.health = 100; p.move.pos = at; p.move.vel = {};
        pump(s, cls, 1.0, [&](int) { UserCmd c; c.yaw = yaw; c.pitch = pitch; c.weapon = WPN_ROCKET; return c; });
        p.health = 100; p.move.pos = at; p.move.vel = {};
        pump(s, cls, 0.12, [&](int) { UserCmd c; c.yaw = yaw; c.pitch = pitch; c.weapon = WPN_ROCKET; c.buttons = BTN_FIRE; return c; });
        pump(s, cls, 0.5, [&](int) { UserCmd c; c.yaw = yaw; c.pitch = pitch; return c; });
        return 100 - p.health;
    };
    float down = shoot(-1.5f, {9, 0, 0});              // straight at the floor
    float level = shoot(0.0f, {0, 0, 30.5f}, PI);      // into the wall in front (+z), at eye height
    printf("  floor blast %.1f damage, eye-level blast %.1f damage\n", down, level);
    CHECK(down > 0 && down <= 4);
    CHECK(level > 15);
    s.stop();
}

static void testBots(uint16_t& port) {
    printf("bots: vs-bots fights, balance bots, votes, every mode\n");
    { // FFA vs 3 expert bots: they should find and kill each other / us
        Server s;
        ServerConfig cfg;
        cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port++;
        cfg.vsBots = true; cfg.botsEnemy = 3; cfg.botSkill = BOT_EXPERT; cfg.timeLimit = 60;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; return; }
        CHECK(s.playerCount() == 3);
        NetClient a;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "human");
        std::vector<NetClient*> cls = {&a};
        pump(s, cls, 25.0, idle);
        int kills = 0;
        for (auto& p : s.players_) if (p.connected) kills += p.kills;
        printf("  ffa vs 3 expert bots: %d kills in 25 s (phase %d)\n", kills, s.ms_.phase);
        CHECK(kills >= 3);
        s.stop();
    }
    { // balance bots + vote
        Server s;
        ServerConfig cfg;
        cfg.map = "arena"; cfg.mode = MODE_VS; cfg.port = port++;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; return; }
        NetClient a, b;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "one");
        std::vector<NetClient*> cls = {&a};
        pump(s, cls, 1.5, idle);
        int bots = 0;
        for (auto& p : s.players_) bots += p.connected && p.isBot;
        CHECK(bots == 1);                                  // 1 human vs 0 -> one bot on the other team
        b.connect("127.0.0.1:" + std::to_string(cfg.port), "two");
        cls.push_back(&b);
        pump(s, cls, 1.5, idle);
        bots = 0;
        for (auto& p : s.players_) bots += p.connected && p.isBot;
        CHECK(bots == 0);                                  // second human evens the teams
        b.disconnect();
        cls.pop_back();
        pump(s, cls, 1.5, idle);
        bots = 0;
        for (auto& p : s.players_) bots += p.connected && p.isBot;
        CHECK(bots == 1);
        a.sendCommand("callvote botskill expert");
        pump(s, cls, 0.5, idle);
        for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].isBot) CHECK(s.bots_[i].skill == BOT_EXPERT);
        a.sendCommand("callvote bots off");
        pump(s, cls, 1.5, idle);
        bots = 0;
        for (auto& p : s.players_) bots += p.connected && p.isBot;
        CHECK(bots == 0 && !s.config().balanceBots);
        s.stop();
    }
    // every mode with bots on both sides: no crashes, things happen
    const char* maps[MODE_COUNT] = {"arena", "arena", "sprint", "arena", "arena", "arena"};
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        Server s;
        ServerConfig cfg;
        cfg.map = maps[mode]; cfg.mode = mode; cfg.port = port++;
        cfg.vsBots = true; cfg.botsEnemy = 3; cfg.botsAlly = 2; cfg.botSkill = BOT_ADVANCED;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; continue; }
        std::vector<NetClient*> cls;
        pump(s, cls, 14.0, idle);
        int kills = 0, cps = 0;
        for (auto& p : s.players_) { kills += p.kills; cps += p.checkpoint; }
        printf("  %-8s with 5 bots: phase %d, round %d, kills %d, score %d-%d, cart %.1f, bomb %d, race checkpoints %d\n",
               modeName(mode), s.ms_.phase, s.ms_.round, kills, s.ms_.scoreA, s.ms_.scoreB, s.ms_.payloadDist, s.ms_.bombState, cps);
        CHECK(s.ms_.phase != PHASE_WARMUP); // bots alone start a match
        s.stop();
    }
}

int main(int argc, char** argv) {
    setExecutableDir(argv[0]);
    netInit();
    if (argc >= 4 && std::string(argv[1]) == "bot") return runBot(argv[2], atof(argv[3]), argc > 4 ? argv[4] : "bot");
    srand(1234);
    testMovement();
    testMaps();
    testTickRateIndependence();
    testGridQueries();
    uint16_t port = 28500;
    testSession(MODE_VS, "arena", port++);
    testSession(MODE_BOMB, "arena", port++);
    testSession(MODE_KOTH, "arena", port++);
    testSession(MODE_PAYLOAD, "arena", port++);
    testSession(MODE_FFA, "arena", port++);
    testSession(MODE_RACE, "sprint", port++);
    testPayloadPush(port++);
    testPacketLoss(port++);
    testWeapons(port); port += 6;
    testTickRateFallback(port++);
    testRocketSelfDamage(port++);
    testBots(port);
    netShutdown();
    printf(g_fail ? "%d FAILURE(S)\n" : "all tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
