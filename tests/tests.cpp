// Headless tests: movement, map parsing, and real client/server sessions over UDP
// on localhost, including every game mode.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "game/server.h"
#include "game/netclient.h"
#include "game/gamemodes.h"
#include "game/bots.h"

static int g_fail = 0;
// Multiplayer tests run on a clock this many times faster than real time.
static constexpr double kTestTimeScale = 4.0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

// Pump a server and some clients for `seconds` of real time, feeding each client an input.
template <class F>
static void pump(Server& s, std::vector<NetClient*>& cls, double seconds, F input) {
    double end = timeNow() + seconds;
    double nextTick = timeNow();
    while (timeNow() < end) {
        s.frame();
        for (auto* c : cls) c->poll();
        while (timeNow() >= nextTick) {
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

// Every map: loads, supports the mode it was built for, no spawn inside a wall, and bots can
// navigate from every spawn to every objective.
static void testAllMaps() {
    printf("all maps: modes, spawns, bot navigation\n");
    for (auto& name : listMaps()) {
        Map m;
        std::string err;
        if (!m.load(name, &err)) { printf("  %s: %s\n", name.c_str(), err.c_str()); g_fail++; continue; }
        if (m.preferredMode >= 0) CHECK(m.supportsMode(m.preferredMode));
        for (auto& sp : m.spawns) CHECK(!m.boxSolid(playerBox(sp.pos + Vec3(0, 0.02f, 0), false)));
        NavGraph g;
        g.build(m);
        std::vector<Vec3> goals;
        if (m.hasHill) goals.push_back({m.hill.center().x, m.hill.mn.y, m.hill.center().z});
        for (auto& s : m.bombSites) goals.push_back(s.center());
        if (m.payloadPath.size() >= 2) { goals.push_back(m.payloadPath.front()); goals.push_back(m.payloadPath.back()); }
        for (auto& c : m.checkpoints) goals.push_back(c.center());
        int unreachable = 0;
        for (auto& sp : m.spawns)
            for (auto& gl : goals) {
                int a = g.nodeAt(sp.pos + Vec3(0, 0.1f, 0)), b = g.nodeAt(gl + Vec3(0, 0.5f, 0));
                if (a < 0 || b < 0) { unreachable++; continue; }
                std::vector<char> seen(g.nodes.size(), 0);
                std::vector<int> q = {a};
                seen[a] = 1;
                for (size_t i = 0; i < q.size(); i++)
                    for (auto& l : g.links[q[i]]) if (!seen[l.first]) { seen[l.first] = 1; q.push_back(l.first); }
                unreachable += !seen[b];
            }
        if (unreachable) printf("  %s: %d spawn->objective routes missing\n", name.c_str(), unreachable);
        CHECK(unreachable == 0);
    }
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
    cfg.autoStart = true;
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
    pump(s, cls, 0.5, idle);
    CHECK(s.humanCount() == 1); // a balance bot may take the empty slot
    s.stop();
}

static void testPayloadPush(uint16_t port) {
    printf("payload push\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true;
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
    cfg.autoStart = true;
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
        cfg.autoStart = true;
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
            const WeaponDef& wd = weaponDef(w);
            if (wd.projectile && wd.gravity > 0) { // aim above for the drop
                float t = length(tgt - eye) / wd.projSpeed;
                tgt.y += 0.5f * wd.gravity * t * t;
            }
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
        if (w == WPN_SNIPER) CHECK(dealt >= weaponDef(WPN_SNIPER).damage); // one scoped shot
        if (w == WPN_LASER) {                                     // ~5 dmg every 0.05 s
            float expect = (float)(elapsed / weaponDef(WPN_LASER).refire) * weaponDef(WPN_LASER).damage;
            printf("  laser: %.1f damage in %.2f s (expected ~%.1f)\n", dealt, elapsed, expect);
            CHECK(dealt > expect * 0.6f && dealt < expect * 1.4f + 5);
        }
        if (w == WPN_ROCKET) CHECK(dealt > 15);
        if (w == WPN_SHOTGUN) CHECK(dealt >= 20);      // most of 20 x 2 pellets at 8 m
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

// Stickies stuck to the payload cart ride
// along with it; dashing while looking up rises a little.
static void testCartDash(uint16_t port) {
    printf("stickies ride the cart, dash tilts with pitch\n");
    {
        MoveState m; Map map; map.load("arena");
        m.pos = {9, 0, 6}; m.flags = MF_ONGROUND;
        UserCmd c; c.msec = 1; c.pitch = 1.0f; c.buttons = BTN_DASH;
        for (int i = 0; i < 150; i++) pmove(m, c, map);
        CHECK(m.pos.y > 0.4f);
        MoveState f; f.pos = {9, 0, 6}; f.flags = MF_ONGROUND;
        c.pitch = 0;
        for (int i = 0; i < 150; i++) pmove(f, c, map);
        CHECK(f.pos.y < 0.05f);
    }
    Server s;
    ServerConfig cfg;
    cfg.map = "railyard"; cfg.mode = MODE_PAYLOAD; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    std::vector<NetClient*> none;
    pump(s, none, 0.2, idle);
    // sticky on the cart moves with it
    s.ms_.payloadDist = 5;
    pump(s, none, 0.05, idle);
    AABB box = s.map_.payloadCartBox(s.ms_.payloadDist);
    Projectile& st = s.projectiles_[1];
    st = Projectile();
    st.active = true; st.type = WPN_STICKY; st.owner = 0; st.id = 1000;
    st.pos = Vec3(box.center().x, box.mx.y + 1.0f, box.center().z); st.vel = {0, -10, 0}; st.dieAt = s.time() + 20;
    pump(s, none, 0.3, idle);
    CHECK(st.stuck && st.onCart);
    Vec3 before = st.pos;
    s.ms_.payloadDist = 9;
    pump(s, none, 0.05, idle);
    Vec3 moved = st.pos - before, cartMoved = s.map_.payloadPoint(9) - s.map_.payloadPoint(5);
    CHECK(length(moved - cartMoved) < 0.05f && length(cartMoved) > 1);
    s.stop();
}

// Shot spread is seeded per command, so client tracers match the server. The rifle fires while held.
static void testSemiAuto(uint16_t port) {
    printf("seeded spread, rifle auto fire\n");
    float a1, r1, a2, r2;
    spreadOffset(42, 0, 0.1f, a1, r1); spreadOffset(42, 0, 0.1f, a2, r2);
    CHECK(a1 == a2 && r1 == r2 && r1 <= 0.1f);
    spreadOffset(43, 0, 0.1f, a2, r2);
    CHECK(a1 != a2);
    Server s;
    ServerConfig cfg;
    cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "holder");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 2.0, idle);
    auto& p = s.players_[a.clientId()];
    pump(s, cls, 0.3, [](int) { UserCmd c; c.weapon = WPN_RIFLE; return c; });
    float last = p.lastFire;
    int shots = 0;
    pump(s, cls, 1.5, [&](int) { UserCmd c; c.weapon = WPN_RIFLE; c.buttons = BTN_FIRE; if (p.lastFire != last) { shots++; last = p.lastFire; } return c; });
    printf("  held 1.5 s: %d shots\n", shots);
    CHECK(shots >= 5);
    s.stop();
}

static void testTickRateFallback(uint16_t port) {
    printf("tick rate starts at 1000 Hz and steps down when the host can't keep up\n");
    setTimeScale(1.0);
    g_serverRateCheck = true;
    struct Restore { ~Restore() { setTimeScale(kTestTimeScale); g_serverRateCheck = false; } } restore;
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true;
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
    cfg.autoStart = true;
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
        pump(s, cls, weaponDef(WPN_ROCKET).refire + 0.2, [&](int) { UserCmd c; c.yaw = yaw; c.pitch = pitch; c.weapon = WPN_ROCKET; return c; });
        p.health = 100; p.move.pos = at; p.move.vel = {};
        pump(s, cls, 0.12, [&](int) { UserCmd c; c.yaw = yaw; c.pitch = pitch; c.weapon = WPN_ROCKET; c.buttons = BTN_FIRE; return c; });
        pump(s, cls, 0.5, [&](int) { UserCmd c; c.yaw = yaw; c.pitch = pitch; return c; });
        return 100 - p.health;
    };
    float down = shoot(-1.5f, {9, 0, 0});              // straight at the floor
    float level = shoot(0.0f, {0, 0, 30.5f}, PI);      // into the wall in front (+z), at eye height
    printf("  floor blast %.1f damage, eye-level blast %.1f damage\n", down, level);
    CHECK(down > 0 && down <= weaponDef(WPN_ROCKET).damage * 0.1f + 0.01f);
    s.killPlayer(a.clientId(), -1, 255); // respawns with the last selected weapon
    pump(s, cls, 2.5, idle);
    CHECK(p.alive && p.weapon == WPN_ROCKET);
    CHECK(level > 10);
    s.stop();
}

static void testWallbang() {
    printf("wallbang: bullets pass through, movement and normal rays don't\n");
    Map m;
    CHECK(m.parse("box -10 -1 -10 10 0 10\nwallbang -1 0 -5 1 3 5\nbox 4 0 -5 5 3 5\nspawn any -5 0 0\n"));
    float bang = -2;
    float t = m.raycastBullet({-5, 1.5f, 0}, {1, 0, 0}, 50, &bang);
    CHECK(std::fabs(bang - 4) < 0.01f);   // went through the wallbang at x=-1
    CHECK(std::fabs(t - 9) < 0.01f);      // stopped by the solid box at x=4
    CHECK(std::fabs(m.raycast({-5, 1.5f, 0}, {1, 0, 0}, 50) - 4) < 0.01f); // normal rays (sight, splash) stop
    CHECK(m.boxSolid(playerBox({0, 0, 0}, false)));                        // and you can't walk through it
}

static void testTimeToKill() {
    printf("time to kill with perfect aim (1-4.5 s), no shot over 50\n");
    for (int w = 0; w < WPN_COUNT; w++) {
        const WeaponDef& d = weaponDef(w);
        float perShot = d.damage * d.pellets;
        int shots = (int)std::ceil(kMaxHealth / perShot - 1e-4f);
        float ttk = (shots - 1) * d.refire;
        printf("  %-9s %3d x %5.2f dmg, ttk %.3f s\n", d.name, shots, perShot, ttk);
        CHECK(ttk >= 1.0f && ttk <= 4.5f);
        CHECK(perShot <= 50);
    }
}

// 1v1: per-round modifiers, forced weapons, vampire healing, 1 hp, double-size hitboxes, grapple-only
// movement, a third player spectates. Also checks the extreme game log.
static void testDuel(uint16_t port) {
    printf("duel 1v1: a modifier per life, spectators, game log\n");
    std::string logPath = "quark_test_duel.log";
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true;
    cfg.map = "crucible"; cfg.mode = MODE_DUEL; cfg.port = port;
    cfg.logLevel = LOG_EXTREME; cfg.logFile = logPath;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    NetClient a, b, c;
    a.connect("127.0.0.1:" + std::to_string(port), "ann");
    b.connect("127.0.0.1:" + std::to_string(port), "bob");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 1.0, idle);
    c.connect("127.0.0.1:" + std::to_string(port), "cat");
    cls.push_back(&c);
    pump(s, cls, 0.5, idle);
    CHECK(s.players_[a.clientId()].team != s.players_[b.clientId()].team);
    CHECK(s.players_[c.clientId()].team == TEAM_SPECTATOR);
    pump(s, cls, 6.0, idle); // warmup countdown -> round 1
    CHECK(s.ms_.phase == PHASE_FREEZE || s.ms_.phase == PHASE_LIVE);
    int seen = 0, prev = -1;
    for (int round = 0; round < 12 && s.ms_.phase != PHASE_MATCH_END; round++) {
        while (s.ms_.phase != PHASE_LIVE && s.ms_.phase != PHASE_MATCH_END) pump(s, cls, 0.2, idle);
        if (s.ms_.phase == PHASE_MATCH_END) break;
        int mod = s.ms_.duelMod;
        CHECK(mod < DUEL_MOD_COUNT && mod != prev);
        prev = mod;
        seen |= 1 << mod;
        int ia = a.clientId(), ib = b.clientId();
        auto& pa = s.players_[ia];
        auto& pb = s.players_[ib];
        // the modifier is applied to both players, and the forced weapon can't be switched away
        pump(s, cls, 0.1, [&](int) { UserCmd u; u.weapon = WPN_SNIPER; return u; });
        if (duelModWeapon(mod) >= 0) CHECK(pa.weapon == duelModWeapon(mod) && pb.weapon == duelModWeapon(mod));
        else CHECK(pa.weapon == WPN_SNIPER);
        CHECK(pa.selectedWeapon == WPN_SNIPER);
        if (mod == DUEL_ONE_HP) CHECK(pa.health == 1 && pb.health == 1);
        if (mod == DUEL_GRAPPLE) {
            CHECK(pa.move.mods & MOVE_GRAPPLE_ONLY);
            CHECK(a.predicted().mods & MOVE_GRAPPLE_ONLY);
            Vec3 start = pa.move.pos;
            pump(s, cls, 0.5, [&](int) { UserCmd u; u.forward = 127; u.buttons = BTN_JUMP | BTN_DASH; return u; });
            CHECK(std::fabs(pa.move.pos.x - start.x) + std::fabs(pa.move.pos.z - start.z) < 0.05f);
        } else {
            CHECK(!(pa.move.mods & MOVE_GRAPPLE_ONLY));
        }
        if (mod == DUEL_BIG) {
            AABB h = s.hitBox(pa.move.pos, false);
            CHECK(std::fabs((h.mx.y - h.mn.y) - kPlayerHeight * 2) < 0.01f);
        }
        if (mod == DUEL_VAMPIRE_LASER) {
            pa.health = 50;
            s.damagePlayer(ib, ia, 20, WPN_LASER, {});
            CHECK(pa.health == 70 && pb.health == 80);
        }
        s.killPlayer(round % 2 ? ia : ib, round % 2 ? ib : ia, 0);
        pump(s, cls, 0.3, idle);
        CHECK(s.ms_.phase == PHASE_ROUND_END || s.ms_.phase == PHASE_MATCH_END);
    }
    printf("  modifiers seen: %02x, score %d-%d\n", seen, s.ms_.scoreA, s.ms_.scoreB);
    CHECK(s.ms_.phase == PHASE_MATCH_END || s.ms_.phase == PHASE_ROUND_END);
    CHECK(__builtin_popcount(seen) >= 4);
    s.handleStringCmdForTest(a.clientId(), "say gg");
    s.stop();
    FILE* f = fopen(logPath.c_str(), "r");
    CHECK(f);
    if (f) {
        std::string all;
        char line[512];
        int ticks = 0;
        while (fgets(line, sizeof line, f)) { all += line; if (strstr(line, " tick ")) ticks++; }
        fclose(f);
        CHECK(all.find("map crucible mode duel") != std::string::npos);
        CHECK(all.find("kill killer=") != std::string::npos);
        CHECK(all.find("round 1 modifier") != std::string::npos);
        CHECK(all.find("chat ") != std::string::npos);
        CHECK(all.find("  p") != std::string::npos);
        CHECK(ticks > 1000); // extreme: every tick
        printf("  log: %zu bytes, %d tick records\n", all.size(), ticks);
    }
    remove(logPath.c_str());
}

static void testDuel2v2(uint16_t port) {
    printf("duel 2v2: two per side, the fifth spectates, modifier applies to all\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true;
    cfg.map = "crucible"; cfg.mode = MODE_DUEL; cfg.port = port; cfg.duelTeamSize = 2;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    std::vector<std::unique_ptr<NetClient>> c;
    std::vector<NetClient*> cls;
    for (int i = 0; i < 5; i++) {
        c.push_back(std::make_unique<NetClient>());
        c[i]->connect("127.0.0.1:" + std::to_string(port), "p" + std::to_string(i));
        cls.push_back(c[i].get());
        pump(s, cls, 0.3, idle);
    }
    int a = 0, b = 0, spec = 0;
    for (auto* cl : cls) {
        int t = s.players_[cl->clientId()].team;
        a += t == TEAM_A; b += t == TEAM_B; spec += t == TEAM_SPECTATOR;
    }
    CHECK(a == 2 && b == 2 && spec == 1);
    pump(s, cls, 6.0, idle);
    while (s.ms_.phase == PHASE_FREEZE) pump(s, cls, 0.2, idle);
    CHECK(s.ms_.phase == PHASE_LIVE && s.ms_.duelMod < DUEL_MOD_COUNT);
    int alive = 0;
    for (auto& p : s.players_) alive += p.connected && p.alive;
    CHECK(alive == 4);
    s.stop();
}

// Internet list end to end; only runs with QUARK_MASTER pointing at a test copy of tools/master/worker.js.
static void testInternetList(uint16_t port) {
    if (!getenv("QUARK_MASTER")) return;
    printf("internet server list via %s\n", getenv("QUARK_MASTER"));
    Server s;
    ServerConfig cfg;
    cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port; cfg.publicList = true; cfg.name = "listed";
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    std::vector<NetClient*> none;
    pump(s, none, 3.0, idle); // announce
    ServerBrowser b;
    b.refresh();
    bool found = false;
    for (int i = 0; i < 400 && !found; i++) {
        s.frame(); b.poll(); sleepMs(10);
        for (auto& sv : b.servers()) found = found || (sv.name == "listed" && sv.addr.port == port);
    }
    CHECK(found);
    s.stop();
}

// Each bot level shoots at a strafing player for a while; damage per second must rise with skill.
static void testBotAim(uint16_t& port) {
    printf("bot aim: damage per second vs a strafing player, by level\n");
    float dps[BOT_SKILLS] = {};
    for (int k = 0; k < BOT_SKILLS; k++) {
        Server s;
        ServerConfig cfg;
        cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port++;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; return; }
        NetClient a;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "target");
        std::vector<NetClient*> cls = {&a};
        pump(s, cls, 2.0, idle);
        int bot = s.addBot(TEAM_A, k);
        s.bots_[bot].forceWeapon = WPN_RIFLE;
        pump(s, cls, 1.0, idle);
        auto& p = s.players_[a.clientId()];
        auto& bp = s.players_[bot];
        float dealt = 0, lastShot = -1;
        int shots = 0;
        double t0 = timeNow();
        pump(s, cls, 15.0, [&](int) {
            if (!p.alive || !bp.alive) return UserCmd();
            dealt += 100 - p.health;
            if (bp.lastFire != lastShot) { shots++; lastShot = bp.lastFire; }
            p.health = 100;
            p.move.pos.x = -7;                                   // strafe back and forth along z
            if (std::fabs(p.move.pos.z + 24) > 4) p.move.pos.z = -24;
            bp.move.pos = {7, 0, -24}; bp.move.vel = {};          // bot 14 m away, open floor, standing (aim only)
            UserCmd c;
            c.side = (int)((timeNow() - t0) / 0.6) % 2 ? 127 : -127;
            c.yaw = -PI / 2;
            return c;
        });
        dps[k] = dealt / 15.0f;
        printf("  %-12s %5.1f dps, %.0f%% of shots hit\n", botSkillName(k), dps[k], shots ? dealt / weaponDef(WPN_RIFLE).damage / shots * 100 : 0.0f);
        s.stop();
    }
    // short samples are noisy between neighbouring levels; the spread across levels must hold
    CHECK(dps[0] < dps[2] && dps[1] < dps[3] && dps[2] < dps[3]);
    CHECK(dps[0] > 2); // beginners still land some hits
}

static void testMapVote(uint16_t port) {
    printf("vote to change map / mode\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 1.0, idle);
    b.sendCommand("callvote map nosuchmap ffa");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE);
    b.sendCommand("callvote map summit koth");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_MAP);
    a.sendCommand("vote yes");
    pump(s, cls, 1.0, idle);
    CHECK(s.config().map == "summit" && s.config().mode == MODE_KOTH);
    s.stop();
}

// A server running a map the client doesn't have (or has a different copy of): the client downloads it,
// keeps it under maps/downloads, and the editor's save format round-trips every map.
static void testMapDownload(uint16_t port) {
    printf("map download + editor save format\n");
    for (auto& name : listMaps()) {
        Map m, r;
        CHECK(m.load(name));
        CHECK(r.parse(m.serialize()));
        CHECK(r.brushes.size() == m.brushes.size() && r.spawns.size() == m.spawns.size() && r.lights.size() == m.lights.size());
        CHECK(r.checkpoints.size() == m.checkpoints.size() && r.payloadPath.size() == m.payloadPath.size() && r.hasHill == m.hasHill);
        for (int k = 0; k < MODE_COUNT; k++) CHECK(r.supportsMode(k) == m.supportsMode(k));
        CHECK(r.serialize() == m.serialize());
    }
    { // allowed modes: kept through save/load, and they limit supportsMode
        Map m;
        CHECK(m.load("arena"));
        CHECK(applyMapEdit(m, "m " + std::to_string((1u << MODE_FFA) | (1u << MODE_DUEL))));
        Map r;
        CHECK(r.parse(m.serialize()) && r.allowedModes == m.allowedModes);
        CHECK(r.supportsMode(MODE_FFA) && r.supportsMode(MODE_DUEL) && !r.supportsMode(MODE_KOTH) && r.canSupportMode(MODE_KOTH));
    }
    Map base;
    CHECK(base.load("arena"));
    std::string text = base.serialize() + "accent 0 8 0 1 9 1\n";
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)rand());
    std::string name = std::string("zz_dltest") + tag, path = findDataPath("maps") + "/" + name + ".qmap";
    CHECK(saveMapFile(path, text));
    Server s;
    ServerConfig cfg;
    cfg.map = name; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    bool started = s.start(cfg);
    std::remove(path.c_str()); // the client must not find it on disk
    CHECK(started);
    if (!started) return;
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 1.5, idle);
    CHECK(a.state() == ConnState::Connected && a.mapReady());
    CHECK(a.map().crc == s.map_.crc && a.map().brushes.size() == base.brushes.size() + 1);
    std::string dl = downloadedMapPath(name, s.map_.crc);
    FILE* f = fopen(dl.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) fclose(f);
    // a second connection uses the saved download instead of fetching again
    NetClient b;
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    cls.push_back(&b);
    pump(s, cls, 0.2, idle);
    CHECK(b.mapReady() && b.downloadProgress() < 0 && b.map().crc == s.map_.crc);
    std::remove(dl.c_str());
    s.stop();
}

// Map edit session: edits from any client reach everyone (and late joiners), the host's file is autosaved,
// and the session switches to playing the map and back.
static void testEditSession(uint16_t port) {
    printf("multiplayer map editing\n");
    Map base;
    CHECK(base.load("arena"));
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)rand());
    std::string name = std::string("zz_edtest") + tag, path = findDataPath("maps") + "/" + name + ".qmap";
    CHECK(saveMapFile(path, base.serialize()));
    Server s;
    ServerConfig cfg;
    cfg.map = name; cfg.mode = MODE_FFA; cfg.port = port; cfg.editMode = true;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; std::remove(path.c_str()); return; }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 1.0, idle);
    CHECK(a.mapReady() && b.mapReady() && a.hasSnapshot() && (a.mode().flags & MS_EDITING));
    size_t nb = base.brushes.size();
    a.sendCommand("ed b ins " + std::to_string(nb) + " 1 0 10 0 2 11 2");
    b.sendCommand("ed l ins 0 0 5 0 12 1.5");
    b.sendCommand("ed b set 0 2 -34 -1 -34 34 0 34");
    a.sendCommand("ed s del 999"); // refused
    pump(s, cls, 0.5, idle);
    CHECK(s.map_.brushes.size() == nb + 1 && s.map_.lights.size() == base.lights.size() + 1 && s.map_.brushes[0].material == 2);
    CHECK(a.map().crc == s.map_.crc && b.map().crc == s.map_.crc);
    CHECK(a.map().brushes.size() == nb + 1 && b.map().lights.size() == s.map_.lights.size());
    // a late joiner (its disk copy is the old file) gets the edited map
    NetClient c;
    c.connect("127.0.0.1:" + std::to_string(port), "c");
    cls.push_back(&c);
    a.sendCommand("ed t Edited Arena");
    pump(s, cls, 1.5, idle);
    CHECK(c.mapReady() && c.map().crc == s.map_.crc && c.map().title == "Edited Arena" && a.map().title == "Edited Arena");
    // a burst of edits while c is connected
    for (int k = 0; k < 20; k++) b.sendCommand("ed p ins " + std::to_string(k) + " " + std::to_string(k) + " 0 0");
    pump(s, cls, 1.0, idle);
    CHECK(a.map().crc == s.map_.crc && b.map().crc == s.map_.crc && c.map().crc == s.map_.crc);
    // switch to playing it: autosaved first, everyone spawns, then back to editing
    s.command("editmode off");
    pump(s, cls, 7.0, idle);
    CHECK(!(a.mode().flags & MS_EDITING) && !s.editing_);
    Map saved;
    CHECK(saved.load(name) && saved.crc == s.map_.crc && saved.title == "Edited Arena" && saved.brushes.size() == nb + 1);
    CHECK(a.map().crc == s.map_.crc);
    bool anyAlive = false;
    for (auto& p : s.players_) anyAlive |= p.connected && p.alive;
    CHECK(anyAlive);
    s.command("editmode on");
    pump(s, cls, 0.5, idle);
    CHECK((a.mode().flags & MS_EDITING) && s.editing_);
    for (auto& p : s.players_) CHECK(!p.alive);
    a.sendCommand("ed b del 0");
    pump(s, cls, 0.5, idle);
    CHECK(c.map().crc == s.map_.crc && c.map().brushes.size() == nb);
    s.stop(); // autosaves
    CHECK(saved.load(name) && saved.brushes.size() == nb);
    std::remove(path.c_str());
    for (uint32_t crc : {a.map().crc, c.map().crc}) std::remove(downloadedMapPath(name, crc).c_str());
}

// 1v1 rulesets: weapon pools, instagib, no self damage, vampire. Tourney: 5 players (byes), the bracket
// runs to a champion with map and ruleset rotation, non-players spectate.
static void testArenaAndTourney(uint16_t& port) {
    printf("1v1 rulesets + tourney bracket\n");
    {
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true;
        cfg.map = "crucible"; cfg.mode = MODE_ARENA; cfg.port = port++; cfg.arenaVariant = arenaVariantFromName("classic");
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; return; }
        NetClient a, b;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "a");
        b.connect("127.0.0.1:" + std::to_string(cfg.port), "b");
        std::vector<NetClient*> cls = {&a, &b};
        pump(s, cls, 1.0, idle);
        pump(s, cls, 6.0, idle);
        auto& pa = s.players_[a.clientId()];
        CHECK(s.ms_.weaponMask == ((1 << WPN_ROCKET) | (1 << WPN_LASER) | (1 << WPN_KNIFE)));
        CHECK(pa.weapon == WPN_ROCKET || pa.weapon == WPN_LASER || pa.weapon == WPN_KNIFE);
        pump(s, cls, 0.2, [](int) { UserCmd c; c.weapon = WPN_SNIPER; return c; });
        CHECK(pa.weapon != WPN_SNIPER); // not in the pool
        pump(s, cls, 0.2, [](int) { UserCmd c; c.weapon = WPN_KNIFE; return c; });
        CHECK(pa.weapon == WPN_KNIFE);
        s.cfg_.arenaVariant = s.ms_.arenaVariant = (uint8_t)arenaVariantFromName("instagib");
        while (s.ms_.phase != PHASE_LIVE) pump(s, cls, 0.1, idle);
        auto& pb = s.players_[b.clientId()];
        s.damagePlayer(b.clientId(), a.clientId(), 1, WPN_SNIPER, {});
        CHECK(!pb.alive); // instagib
        s.stop();
    }
    {
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true;
        cfg.map = "crucible"; cfg.mode = MODE_TOURNEY; cfg.port = port++;
        cfg.tourneyMaps = {"crucible", "duel"};
        cfg.tourneyVariants = {arenaVariantFromName("classic"), arenaVariantFromName("hitscan")};
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; return; }
        NetClient h;
        h.connect("127.0.0.1:" + std::to_string(cfg.port), "human");
        std::vector<NetClient*> cls = {&h};
        pump(s, cls, 1.0, idle);
        for (int i = 0; i < 4; i++) s.addBot(TEAM_A, BOT_BEGINNER);
        int brackets = 0, matches = 0, maxPlaying = 0;
        std::string lastMatch;
        double end = timeNow() + 300;
        while (timeNow() < end && s.ms_.phase != PHASE_MATCH_END) {
            pump(s, cls, 0.1, idle);
            for (auto& e : h.takeEvents()) brackets += e.type == EV_BRACKET;
            int playing = 0;
            for (auto& p : s.players_) playing += p.connected && p.team != TEAM_SPECTATOR;
            if (s.tourney_.running) maxPlaying = std::max(maxPlaying, playing);
            if (s.ms_.phase == PHASE_LIVE) { // decide rounds fast: team B's player dies
                std::string mt = s.cfg_.map + std::to_string(s.tourney_.round) + "," + std::to_string(s.tourney_.match);
                if (mt != lastMatch) { lastMatch = mt; matches++; }
                for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].alive && s.players_[i].team == TEAM_B) s.killPlayer(i, -1, 255);
            }
        }
        printf("  tourney: %d matches, %d bracket updates, phase %d\n", matches, brackets, s.ms_.phase);
        CHECK(s.ms_.phase == PHASE_MATCH_END);
        CHECK(matches == 4);            // 5 entrants in an 8 slot bracket: 4 played matches (3 byes)
        CHECK(brackets >= 4);
        CHECK(maxPlaying == 2);         // everyone else spectates
        s.stop();
    }
}

// Delta snapshots decode (no misses) and match the server; snapshots shrink; enemies out of sight are
// sent hidden, in sight they're sent.
static void testNetcode(uint16_t port) {
    printf("netcode: delta snapshots, visibility\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    std::vector<std::unique_ptr<NetClient>> owned;
    std::vector<NetClient*> cls;
    for (int i = 0; i < 4; i++) {
        owned.push_back(std::make_unique<NetClient>());
        owned.back()->connect("127.0.0.1:" + std::to_string(port), "c" + std::to_string(i));
        cls.push_back(owned.back().get());
    }
    for (int i = 0; i < 4; i++) s.addBot(TEAM_A, BOT_ADVANCED);
    pump(s, cls, 2.0, idle);
    s.snapBytes_ = s.snapCount_ = 0;
    pump(s, cls, 4.0, [](int i) { UserCmd c; c.forward = 127; c.yaw = i * 1.5f + (float)timeNow(); c.buttons = BTN_JUMP; return c; });
    double avg = s.snapCount_ ? (double)s.snapBytes_ / s.snapCount_ : 0;
    uint32_t misses = 0;
    for (auto* c : cls) misses += c->baseMisses();
    printf("  8 players moving: %.0f bytes per snapshot, %u undecodable deltas, interpolation delay %.1f ms (was %.1f)\n", avg, misses,
           cls[0]->interpDelay() * 1000, (3.0f / cls[0]->tickRate() + 0.010f) * 1000);
    CHECK(cls[0]->interpDelay() < 3.0f / cls[0]->tickRate() + 0.010f);
    CHECK(avg > 0 && avg < 400);   // full snapshots were ~420 bytes for 8 players
    CHECK(misses < 5);
    // decoded state matches the server (within position quantization)
    for (auto& p : cls[0]->latest().players) {
        if (p.hidden || !(p.flags & PS_ALIVE)) continue;
        Vec3 sp = s.players_[p.id].move.pos;
        CHECK(length(p.pos - sp) < 2.0f); // a snapshot or two behind the server at most
    }
    s.stop();

    // visibility: the center walls block the line from one end of arena to the other
    Server v;
    cfg.port = port + 1;
    v.setLog([](const std::string&) {});
    if (!v.start(cfg)) { g_fail++; return; }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(cfg.port), "a");
    b.connect("127.0.0.1:" + std::to_string(cfg.port), "b");
    std::vector<NetClient*> two = {&a, &b};
    pump(v, two, 2.0, idle);
    auto place = [&](Vec3 pa, Vec3 pb) {
        for (int k = 0; k < 6; k++) {
            v.players_[a.clientId()].move.pos = pa; v.players_[a.clientId()].move.vel = {};
            v.players_[b.clientId()].move.pos = pb; v.players_[b.clientId()].move.vel = {};
            pump(v, two, 0.1, idle);
        }
    };
    auto hiddenToA = [&]() { for (auto& p : a.latest().players) if (p.id == b.clientId()) return p.hidden; return false; };
    place({0, 0, -28}, {0, 0, 28});
    bool hid = hiddenToA();
    place({-22, 0, -28}, {-22, 0, 28});
    bool open = !hiddenToA();
    printf("  behind walls: %s, in the open: %s\n", hid ? "hidden" : "SENT", open ? "sent" : "HIDDEN");
    CHECK(hid && open);
    v.stop();
}

static void testBots(uint16_t& port) {
    printf("bots: vs-bots fights, balance bots, votes, every mode\n");
    { // FFA vs 3 expert bots: they should find and kill each other / us
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true;
        cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port++;
        cfg.vsBots = true; cfg.botsEnemy = 3; cfg.botSkill = BOT_EXPERT; cfg.timeLimit = 60;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; return; }
        CHECK(s.playerCount() == 3);
        NetClient a;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "human");
        std::vector<NetClient*> cls = {&a};
        pump(s, cls, 40.0, idle);
        int kills = 0;
        for (auto& p : s.players_) if (p.connected) kills += p.kills;
        printf("  ffa vs 3 expert bots: %d kills in 40 s (phase %d)\n", kills, s.ms_.phase);
        CHECK(kills >= 2);
        s.stop();
    }
    { // pregame (no bots) -> vote start -> vote balance bots -> bots join; then votes
        Server s;
        ServerConfig cfg;
        cfg.map = "arena"; cfg.mode = MODE_VS; cfg.port = port++;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; return; }
        NetClient a, b;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "one");
        std::vector<NetClient*> cls = {&a};
        pump(s, cls, 6.5, idle);
        int bots = 0;
        for (auto& p : s.players_) bots += p.connected && p.isBot;
        CHECK(bots == 0 && s.ms_.phase == PHASE_WARMUP);  // warmup: no bots, no automatic start
        a.sendCommand("callvote start");                  // passes (only voter) -> asks about bots
        pump(s, cls, 0.4, idle);
        CHECK(s.vote_.kind == Server::VOTE_STARTBOTS);
        a.sendCommand("vote yes");
        pump(s, cls, 0.6, idle);
        bots = 0;
        for (auto& p : s.players_) bots += p.connected && p.isBot && p.alive;
        CHECK(bots == 1 && s.ms_.phase == PHASE_FREEZE);   // match started with one bot on team B
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
    { // neutral payload: starts in the middle, either team pushes it; the cart is solid
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true;
        cfg.map = "arena"; cfg.mode = MODE_NPAYLOAD; cfg.port = port++;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; return; }
        NetClient a, b;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "a");
        b.connect("127.0.0.1:" + std::to_string(cfg.port), "b");
        std::vector<NetClient*> cls = {&a, &b};
        pump(s, cls, 0.5, idle);
        s.command("forcestart");
        pump(s, cls, 3.3, idle);
        CHECK(s.ms_.phase == PHASE_LIVE);
        float mid = s.ms_.payloadLen * 0.5f;
        CHECK(std::fabs(s.ms_.payloadDist - mid) < 0.5f);
        int ia = s.players_[a.clientId()].team == TEAM_A ? a.clientId() : b.clientId();
        int ib = ia == a.clientId() ? b.clientId() : a.clientId();
        auto standAt = [&](int id, float off) {
            Vec3 c = s.map_.payloadPoint(s.ms_.payloadDist);
            s.players_[id].move.pos = c + Vec3(off, 0, 0);
            s.players_[id].move.vel = {};
        };
        for (int k = 0; k < 15; k++) { standAt(ia, 2.0f); s.players_[ib].move.pos = {-25, 0, 25}; pump(s, cls, 0.1, idle); }
        float afterA = s.ms_.payloadDist;
        CHECK(afterA > mid + 1.0f);                      // team A pushes toward the end
        for (int k = 0; k < 30; k++) { standAt(ib, 2.0f); s.players_[ia].move.pos = {-25, 0, 25}; pump(s, cls, 0.1, idle); }
        CHECK(s.ms_.payloadDist < afterA - 2.0f);        // team B pushes it back
        AABB cart = s.map_.payloadCartBox(s.ms_.payloadDist);
        Vec3 c = cart.center();
        float t = s.map_.raycast(c + Vec3(0, 0, 6), {0, 0, -1}, 12);
        CHECK(t > 0 && t < 6);                           // the cart blocks shots
        CHECK(a.map().dynSolids.size() == 1);            // and the client predicts it as solid
        s.stop();
    }
    // every mode with bots on both sides: no crashes, things happen
    const char* maps[MODE_COUNT] = {"arena", "arena", "sprint", "arena", "arena", "arena", "arena", "crucible", "crucible", "crucible"};
    static_assert(MODE_COUNT == 10, "add the new mode's map here");
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true;
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
    setTimeScale(kTestTimeScale); // bot subprocesses share the test clock speed
    if (argc >= 4 && std::string(argv[1]) == "bot") return runBot(argv[2], atof(argv[3]), argc > 4 ? argv[4] : "bot");
    srand(1234);
    setTimeScale(kTestTimeScale);
    g_serverRateCheck = false;
    if (argc >= 2 && std::string(argv[1]) == "aim") { uint16_t p = 29500; testBotAim(p); return g_fail ? 1 : 0; }
    testMovement();
    testMaps();
    testTickRateIndependence();
    testAllMaps();
    testGridQueries();
    uint16_t port = 28500;
    if (argc >= 2 && std::string(argv[1]) == "edit") { testMapDownload(29600); testEditSession(29601); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "net") { testNetcode(29900); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "wallbang") { testWallbang(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "tourney") { uint16_t p = 29800; testArenaAndTourney(p); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "weapons") { uint16_t p = 29700; testWeapons(p); testRocketSelfDamage(p + 20); return g_fail ? 1 : 0; }
    testSession(MODE_VS, "arena", port++);
    testSession(MODE_BOMB, "arena", port++);
    testSession(MODE_KOTH, "arena", port++);
    testSession(MODE_PAYLOAD, "arena", port++);
    testSession(MODE_FFA, "arena", port++);
    testSession(MODE_RACE, "sprint", port++);
    testPayloadPush(port++);
    testPacketLoss(port++);
    testWeapons(port); port += 8;
    testCartDash(port++);
    testSemiAuto(port++);
    testMapVote(port++);
    testMapDownload(port++);
    testEditSession(port++);
    testArenaAndTourney(port);
    testNetcode(port); port += 2;
    testTickRateFallback(port++);
    testRocketSelfDamage(port++);
    testTimeToKill();
    testWallbang();
    testDuel(port++);
    testDuel2v2(port++);
    testInternetList(port++);
    testBotAim(port);
    testBots(port);
    netShutdown();
    printf(g_fail ? "%d FAILURE(S)\n" : "all tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
