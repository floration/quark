#include <thread>
#include <functional>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <chrono>
#include <cstdlib>
#include "common/profile.h"
#include <filesystem>
#include <sstream>
#include <fstream>
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
#include "game/customrules.h"
#include "game/netclient.h"
#include "game/gamemodes.h"
#include "game/ranked.h"
#include "common/sha256.h"
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

    // momentum: strafe-jumping (strafe key + turning) builds speed past ground speed without the grapple
    Map open;
    CHECK(open.parse("box -500 -1 -500 500 0 500\nspawn any 0 0 0\n", &err));
    auto hspeed = [](const MoveState& q) { return std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z); };
    MoveState b; b.pos = {0, 0, 0}; b.flags = MF_ONGROUND;
    UserCmd k; k.msec = 1; k.forward = 127;
    for (int i = 0; i < 1000; i++) pmove(b, k, open);        // run up to ground speed
    float run = hspeed(b);
    k.forward = 0; k.side = 127; k.buttons = BTN_JUMP;
    for (int i = 0; i < 4000; i++) {                           // hold jump + strafe right, turning right 90 deg/s
        k.yaw -= 1.5708f * 0.001f;
        pmove(b, k, open);
    }
    printf("  strafe-jumping: %.1f -> %.1f m/s in 4 s\n", run, hspeed(b));
    CHECK(hspeed(b) > run * 1.3f);
    { // the strafe window: turning at a range of speeds all gain; turning far too fast (overstrafing) costs only a little
        auto strafeAt = [&](float degPerSec) {
            MoveState q; q.pos = {0, 0, 0}; q.flags = MF_ONGROUND;
            UserCmd u; u.msec = 1; u.forward = 127;
            for (int i = 0; i < 1000; i++) pmove(q, u, open);
            u.forward = 0; u.side = 127; u.buttons = BTN_JUMP;
            for (int i = 0; i < 2000; i++) { u.yaw -= degPerSec * PI / 180 * 0.001f; pmove(q, u, open); }
            return hspeed(q);
        };
        printf("  strafe window (2 s): ");
        for (float r : {30.0f, 60.0f, 90.0f, 180.0f, 360.0f, 720.0f}) printf("%.0f deg/s %.1f  ", r, strafeAt(r));
        printf("\n");
        for (float r : {45.0f, 90.0f, 180.0f}) CHECK(strafeAt(r) > run * 1.1f);   // a wide range of turn speeds gains
        CHECK(strafeAt(720.0f) > run * 0.7f);    // overstrafing (way too fast) loses some speed (the old air control: 5.0 m/s of 8)
    }
    // a dash leaves you faster than you started (it used to settle back down)
    MoveState d = b; d.dashCooldown = 0; d.flags &= ~MF_DASHHELD;
    float before = hspeed(d);
    UserCmd dc; dc.msec = 1; dc.yaw = k.yaw; dc.forward = 127; dc.buttons = BTN_DASH;
    for (int i = 0; i < 200; i++) pmove(d, dc, open);
    printf("  dash: %.1f -> %.1f m/s after the burst\n", before, hspeed(d));
    CHECK(hspeed(d) > before + 2.0f);
    // landing hard while holding jump bounces higher than a normal jump
    auto peak = [&](float dropFrom) {
        MoveState q; q.pos = {0, dropFrom, 0};
        UserCmd j; j.msec = 1; j.buttons = BTN_JUMP;
        bool landed = false; float top = 0;
        for (int i = 0; i < 6000; i++) {
            float vy = q.vel.y;
            pmove(q, j, open);
            if (!landed && vy <= 0 && q.vel.y > 0 && i > 0) landed = true;  // the (bounce) jump
            if (landed) top = std::max(top, q.pos.y);
            if (landed && q.vel.y < 0) break;
        }
        return top;
    };
    float low = peak(0.5f), high = peak(20.0f);
    printf("  bhop bounce: %.2f m after a short drop, %.2f m after a 20 m fall\n", low, high);
    CHECK(high > low * 1.5f);
}

// The same inputs must give bit-identical movement at 1000, 500, 250 and 125 Hz.
static void testTickRateIndependence() {
    printf("movement identical at every tick rate (incl. dash, bhop, grapple)\n");
    Map m;
    CHECK(m.load("bastion"));
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
    std::string err;
    // Lodestone is the map built for every mode
    { Map v; CHECK(v.load("viaduct") && v.supportsMode(MODE_RACE) && v.preferredMode == MODE_RACE && v.hasVoid && v.checkpoints.size() >= 5); }
    for (const char* name : {"lodestone"}) {
        Map m;
        CHECK(m.load(name, &err));
        if (!err.empty()) printf("  %s\n", err.c_str());
        for (int k = 0; k < MODE_COUNT; k++) if (!m.supportsMode(k)) { printf("  %s: no %s\n", name, modeName(k)); g_fail++; }
        CHECK(m.payloadLength() > 20);
    }
    // ranked: a map of its own for each ladder plus one for all three; every ranked map is point-symmetric
    // (each solid has a mirrored twin through the center, and each team A spawn a team B one)
    int perLadder[3] = {0, 0, 0}, ownMaps = 0;
    for (auto& name : listMaps()) {
        Map m;
        if (!m.load(name) || !m.rankedLadders) continue;
        CHECK(isRankedMap(m, 0) || isRankedMap(m, 1) || isRankedMap(m, 5));
        for (int v : {0, 1, 5}) if (m.rankedLadders >> v & 1) CHECK(m.supportsMode(rankedMode(v)));   // (instagib: free for all)
        int k = 0;
        for (int v : {0, 1, 5}) if (m.rankedLadders >> v & 1) perLadder[k++ < 3 ? (v == 0 ? 0 : v == 1 ? 1 : 2) : 0]++;
        if (__builtin_popcount(m.rankedLadders) == 1) ownMaps++;
        auto mirrored = [](const AABB& b) { return AABB{{-b.mx.x, b.mn.y, -b.mx.z}, {-b.mn.x, b.mx.y, -b.mn.z}}; };
        int lonely = 0;
        for (auto& br : m.brushes) {
            AABB mb = mirrored(br.box);
            bool found = false;
            for (auto& o : m.brushes)
                if (o.material == br.material && length(o.box.mn - mb.mn) < 0.01f && length(o.box.mx - mb.mx) < 0.01f) { found = true; break; }
            lonely += !found;
        }
        if (lonely) printf("  %s: %d solids without a mirrored twin\n", name.c_str(), lonely);
        CHECK(lonely == 0);
        int a = 0, b = 0;
        for (auto& sp : m.spawns) { a += sp.team == SPAWN_A; b += sp.team == SPAWN_B; }
        bool duel = (m.rankedLadders & 3) != 0;   // a ladder played 1v1 (vanilla, vampire): a spawn for each side
        CHECK(a == b && (a >= 1 || !duel));
    }
    CHECK(perLadder[0] >= 2 && perLadder[1] >= 2 && perLadder[2] >= 2 && ownMaps >= 3);
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
        if (name == "dojo") { CHECK(!m.lava.empty() && m.inLava(playerBox({14, -1.5f, 0}, false)) && !m.inLava(playerBox({0, 0.1f, 0}, false))); }   // lava kills on touch
        for (auto& sp : m.spawns) CHECK(!m.boxSolid(playerBox(sp.pos + Vec3(0, 0.02f, 0), false)));
        NavGraph g;
        g.build(m);
        std::vector<Vec3> goals;
        if (m.hasHill) { // the hill's centre can be a pit: any standable floor inside it will do
            Vec3 hp = {m.hill.center().x, m.hill.mn.y, m.hill.center().z};
            for (float fx = 0.1f; fx < 1; fx += 0.2f)
                for (float fz = 0.1f; fz < 1; fz += 0.2f) {
                    Vec3 q = {m.hill.mn.x + (m.hill.mx.x - m.hill.mn.x) * fx, m.hill.mn.y, m.hill.mn.z + (m.hill.mx.z - m.hill.mn.z) * fz};
                    if (g.nodeAt(q + Vec3(0, 0.5f, 0)) >= 0 && g.nodeAt(hp + Vec3(0, 0.5f, 0)) < 0) hp = q;
                }
            goals.push_back(hp);
        }
        for (auto& s : m.bombSites) goals.push_back(s.center());
        if (m.payloadPath.size() >= 2) { goals.push_back(m.payloadPath.front()); goals.push_back(m.payloadPath.back()); }
        for (auto& c : m.checkpoints) goals.push_back(c.center());
        for (auto& p : m.points) goals.push_back({p.center().x, p.mn.y, p.center().z});   // (each point's floor)
        if (m.preferredMode == MODE_HIDE || m.preferredMode == MODE_TAG) for (auto& sp2 : m.spawns) goals.push_back(sp2.pos); // every spawn reaches every other
        int unreachable = 0;
        for (auto& sp : m.spawns)
            for (auto& gl : goals) {
                int a = g.nodeAt(sp.pos + Vec3(0, 0.1f, 0)), b = g.nodeAt(gl + Vec3(0, 0.5f, 0));
                if (a < 0 || b < 0) {
                    unreachable++;
                    printf("    %s: %s at %.1f %.1f %.1f is off the walkable surfaces\n", name.c_str(), a < 0 ? "spawn" : "goal", a < 0 ? sp.pos.x : gl.x, a < 0 ? sp.pos.y : gl.y, a < 0 ? sp.pos.z : gl.z);
                    continue;
                }
                std::vector<char> seen(g.nodes.size(), 0);
                std::vector<int> q = {a};
                seen[a] = 1;
                for (size_t i = 0; i < q.size(); i++)
                    for (auto& l : g.links[q[i]]) if (!seen[l.first]) { seen[l.first] = 1; q.push_back(l.first); }
                if (!seen[b]) { unreachable++; printf("    %s: spawn %.1f %.1f %.1f can't reach %.1f %.1f %.1f\n", name.c_str(), sp.pos.x, sp.pos.y, sp.pos.z, gl.x, gl.y, gl.z); }
            }
        if (unreachable) printf("  %s: %d spawn->objective routes missing\n", name.c_str(), unreachable);
        CHECK(unreachable == 0);
    }
}

static void testGridQueries() {
    printf("grid raycast / collision vs brute force\n");
    Map m;
    CHECK(m.load("bastion"));
    auto brute = [&](const Vec3& o, const Vec3& d, float maxT) {
        Vec3 inv = safeInv(d);
        float best = -1;
        for (auto& b : m.brushes) { if (b.material >= 5) continue; float t = rayAABB(o, inv, b.box, best < 0 ? maxT : best); if (t >= 0 && (best < 0 || t < best)) best = t; }
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
        for (auto& br : m.brushes) solidBrute |= br.material < 5 && box.overlaps(br.box);
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

    // Warmup countdown (5s) then freeze (3s); then live. (Race waits in practice until someone starts it.)
    if (mode == MODE_RACE) { pump(s, cls, 1.0, idle); CHECK(s.ms_.phase == PHASE_WARMUP); s.command("forcestart"); }
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

// Time trial: solo Viaduct race, every checkpoint sends the racer a split (increasing ms, last = finish).
// Race runs: the fastest plausible split per leg (path gap at the speed ceiling); the Worker keeps the same table.
static void testRaceFloors() {
    printf("race split floors (and the Worker's copy)\n");
    Map m;
    CHECK(m.load("viaduct"));
    std::vector<int> f = raceSplitFloorsMs(m);
    CHECK(f.size() == m.checkpoints.size() && !f.empty());
    int total = 0;
    for (int x : f) { CHECK(x >= 0); total += x; }
    CHECK(total > 1000);   // the course is long: well over a second even flat out
    std::vector<int> ok, fast;
    int t = 0;
    for (int x : f) { t += x * 3 + 200; ok.push_back(t); }
    CHECK(raceSplitsPlausible(ok, f));
    fast = ok;
    size_t big = std::max_element(f.begin(), f.end()) - f.begin();
    for (size_t i = big; i < fast.size(); i++) fast[i] -= ok[big] - (big ? ok[big - 1] : 0) - f[big] / 2; // one leg at half the floor
    CHECK(!raceSplitsPlausible(fast, f));
    CHECK(!raceSplitsPlausible({}, f));
    CHECK(!raceSplitsPlausible(std::vector<int>(ok.begin(), ok.end() - 1), f));
    // tools/master/worker.js: RACE_FLOORS = { viaduct: [a, b, ...] } must be this table (re-run "quark_tests racefloors")
    std::ifstream w(findDataPath("maps") + "/../tools/master/worker.js");
    std::stringstream ws;
    ws << w.rdbuf();
    std::string js = ws.str(), want = "viaduct: [";
    for (size_t i = 0; i < f.size(); i++) want += (i ? ", " : "") + std::to_string(f[i]);
    want += "]";
    if (!js.empty()) CHECK(js.find(want) != std::string::npos);
    else printf("  (tools/master/worker.js not found next to the maps: skipped the table check)\n");
    CHECK(buildRaceRunForm("0123456789abcdef", "viaduct", std::string(32, 'a'), ok.back(), ok, "Racer").find("ticket=0123456789abcdef&map=viaduct&id=") == 0);
    CHECK(buildRaceRunForm("0123456789abcdef", "viaduct", std::string(32, 'a'), ok.back() + 1, ok, "Racer").empty());
}

static void testTimeTrial(uint16_t port) {
    printf("time trial: solo race splits on viaduct\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "viaduct"; cfg.mode = MODE_RACE; cfg.port = port;
    cfg.timeTrial = true; cfg.vsBots = true; cfg.autoStart = true; cfg.maxPlayers = 1; cfg.roundsToWin = 10000;
    cfg.botsEnemy = cfg.botsAlly = 0;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "racer");
    std::vector<NetClient*> cls = {&a};
    // race starts in practice: untimed, never starts by itself; the course works, nothing is timed or sent
    pump(s, cls, 3.0, idle);
    CHECK(a.hasSnapshot() && a.mode().phase == PHASE_WARMUP);
    CHECK(a.hasSnapshot() && (a.mode().flags & MS_TIMETRIAL));   // (the shipped viaduct: it counts)
    const auto& cps = s.map_.checkpoints;
    auto& p = s.players_[a.clientId()];
    a.takeEvents();
    for (size_t c = 0; c < 2; c++) // (each until it counts: a loaded machine can miss a short pump)
        for (int k = 0; k < 30 && p.checkpoint <= (int)c; k++) { p.move.pos = cps[c].center(); p.move.pos.y = cps[c].mn.y + 0.05f; p.move.vel = {}; pump(s, cls, 0.1, idle); }
    CHECK(p.checkpoint == 2);
    for (auto& e : a.takeEvents()) CHECK(e.type != EV_RACE_SPLIT);
    a.sendCommand("callvote start");                               // starting it: the race proper
    for (int i = 0; i < 300 && !(a.hasSnapshot() && a.mode().phase == PHASE_LIVE); i++) pump(s, cls, 0.1, idle);
    CHECK(a.hasSnapshot() && a.mode().phase == PHASE_LIVE);
    a.takeEvents();
    std::vector<int> splits;
    for (size_t c = 0; c < cps.size(); c++) {
        for (int k = 0; k < (c ? 30 : 120) && splits.size() <= c; k++) { // (the first may wait out the start on a slow machine)
            p.move.pos = cps[c].center(); p.move.pos.y = cps[c].mn.y + 0.05f; p.move.vel = {};
            pump(s, cls, 0.1, idle);
            for (auto& e : a.takeEvents()) if (e.type == EV_RACE_SPLIT && e.a == a.clientId()) splits.push_back(atoi(e.text.c_str()));
        }
    }
    printf("  %zu checkpoints, %zu splits, finish %d ms (at checkpoint %d, phase %d)\n", cps.size(), splits.size(), splits.empty() ? 0 : splits.back(), p.checkpoint, (int)a.mode().phase);
    CHECK(splits.size() == cps.size() && cps.size() >= 8);
    for (size_t i = 1; i < splits.size(); i++) CHECK(splits[i] > splits[i - 1]);
    s.stop();
    // a "time trial" anywhere but the shipped Viaduct doesn't count for the board
    Server s2;
    cfg.map = "foundry"; cfg.port = port + 1;
    { std::string e_; if (!s2.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient b;
    b.connect("127.0.0.1:" + std::to_string(port + 1), "racer");
    std::vector<NetClient*> cls2 = {&b};
    pump(s2, cls2, 1.0, idle);
    CHECK(b.hasSnapshot() && !(b.mode().flags & MS_TIMETRIAL));
    s2.stop();
}

// Multi point: attackers capture the points in order (a defender on the point holds it), each capture adds time,
// and the spawns move up a stage.
static void testPoints(uint16_t port) {
    printf("multi point\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true;
    cfg.map = "lodestone"; cfg.mode = MODE_POINTS; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 0.4, idle);
    s.command("forcestart");
    for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE; i++) pump(s, cls, 0.1, idle);   // (a slow machine takes longer)
    CHECK(s.ms_.phase == PHASE_LIVE && s.ms_.pointCount == 5 && s.ms_.point == 0);
    int att = -1, def = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected) (s.players_[i].team == s.ms_.attackers ? att : def) = i;
    CHECK(att >= 0 && def >= 0);
    if (att < 0 || def < 0) { s.stop(); return; }
    auto standOn = [&](int id, int pt) { const AABB& z = s.map_.points[pt]; s.players_[id].move.pos = {z.center().x, z.mn.y + 0.6f, z.center().z}; s.players_[id].move.vel = {}; };
    auto away = [&](int id) { s.players_[id].move.pos = s.map_.spawns[0].pos; };
    for (int k = 0; k < 6; k++) { standOn(def, 0); away(att); pump(s, cls, 0.1, idle); }
    CHECK(s.players_[def].objTime == 0);              // a defender alone on it isn't contesting: no objective time
    for (int k = 0; k < 10; k++) { standOn(att, 0); away(def); pump(s, cls, 0.1, idle); }
    CHECK(s.ms_.capture > 0.02f);                     // filling
    CHECK(s.players_[att].objTime > 0);
    float c0 = s.ms_.capture;
    for (int k = 0; k < 10; k++) { standOn(att, 0); standOn(def, 0); pump(s, cls, 0.1, idle); }
    CHECK(s.ms_.flags & MS_CONTESTED);                // a defender on it holds it
    CHECK(s.players_[def].objTime > 0);               // (and contesting earns them time)
    CHECK(s.ms_.capture <= c0 + 0.01f);
    float end0 = s.phaseEnd_;
    s.ms_.capture = 0.98f;
    for (int k = 0; k < 10 && s.ms_.point == 0; k++) { standOn(att, 0); away(def); pump(s, cls, 0.1, idle); }
    CHECK(s.ms_.point == 1 && s.ms_.capture < 0.5f);  // A taken: on to B
    CHECK(s.phaseEnd_ > end0 + 60);                   // with time added
    s.killPlayer(att, -1, 254);                        // respawn: at the stage-1 attacker spawns
    pump(s, cls, 6.0, idle);
    CHECK(s.players_[att].alive);
    bool stage1 = false;
    for (auto& sp : s.map_.spawns)
        if (sp.team == stagedSpawn(1, false) && length(sp.pos - s.players_[att].move.pos) < 1.5f) stage1 = true;
    CHECK(stage1);
    pump(s, cls, 0.3, idle);
    CHECK(a.mode().point == 1);                       // the clients know
    s.stop();
}

// Payload checkpoints ("cartcheck 0.33"): reaching one adds time, and the cart never rolls back past it.
static void testCartChecks(uint16_t port) {
    printf("payload checkpoints\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true;
    cfg.map = "railyard"; cfg.mode = MODE_PAYLOAD; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 0.4, idle);
    s.command("forcestart");
    pump(s, cls, 3.2, idle);
    CHECK(s.ms_.phase == PHASE_LIVE && s.ms_.pointCount == 2);
    int att = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].team == s.ms_.attackers) att = i;
    if (att < 0) { g_fail++; s.stop(); return; }
    float first = s.map_.cartChecks[0] * s.ms_.payloadLen, end0 = s.phaseEnd_;
    s.ms_.payloadDist = first - 0.5f;
    for (int k = 0; k < 20 && s.ms_.point == 0; k++) { s.players_[att].move.pos = s.map_.payloadPoint(s.ms_.payloadDist) + Vec3(0, 0, 2.5f); pump(s, cls, 0.1, idle); }
    CHECK(s.ms_.point == 1 && s.phaseEnd_ > end0 + 30);
    s.players_[att].move.pos = s.map_.spawns[0].pos;          // walk off: after the idle delay it rolls back, but only to the checkpoint
    s.payloadIdleSince_ = s.time() - 100;
    pump(s, cls, 2.0, [&](int) { s.players_[att].move.pos = s.map_.spawns[0].pos; return UserCmd(); });
    CHECK(s.ms_.payloadDist >= first - 0.01f);
    s.stop();
}

static void testPayloadPush(uint16_t port) {
    printf("payload push\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true;
    cfg.map = "bastion"; cfg.mode = MODE_PAYLOAD; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    for (int wi : std::initializer_list<int>{WPN_SNIPER, -WPN_SNIPER - 1, -WPN_CROSSBOW - 1, -WPN_RIFLE - 1, WPN_LASER, WPN_ROCKET, WPN_SHOTGUN, WPN_CROSSBOW, WPN_STICKY}) {
        bool head = wi < 0; // negative entries: the same weapon aimed at the head
        int w = head ? -wi - 1 : wi;
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true;
        cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port++;
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
        sa.move.pos = {10, 0, 19}; sb.move.pos = {10 - dist, 0, 19}; // open courtyard between the inner ward wall and the spawns
        sa.move.vel = sb.move.vel = {};
        pump(s, cls, 0.3, idle);
        auto aim = [&](int i, uint16_t extra) {
            UserCmd c;
            if (i != 0) return c;
            Vec3 eye = a.predicted().pos + Vec3(0, kEyeHeight, 0);
            Vec3 tgt = sb.move.pos + Vec3(0, w == WPN_ROCKET ? 0.2f : head ? 1.62f : 1.0f, 0);
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
        if (w == WPN_SNIPER) { // one scoped shot: 40 on the body, 50 on the head
            printf("  sniper %s: %.0f damage\n", head ? "head" : "body", dealt);
            CHECK(std::fabs(dealt - (head ? kSniperHeadDamage : weaponDef(WPN_SNIPER).damage)) < 0.5f);
        }
        if (w == WPN_LASER) {                                     // ~5 dmg every 0.05 s
            float expect = (float)(elapsed / weaponDef(WPN_LASER).refire) * weaponDef(WPN_LASER).damage;
            printf("  laser: %.1f damage in %.2f s (expected ~%.1f)\n", dealt, elapsed, expect);
            CHECK(dealt > expect * 0.6f && dealt < expect * 1.4f + 5);
        }
        if (w == WPN_ROCKET) { printf("  rocket direct hit: %.0f damage\n", dealt); CHECK(std::fabs(dealt - kRocketDirectDamage) < 0.5f); }   // aimed at the body: a direct hit
        if (w == WPN_SHOTGUN) CHECK(dealt >= 20);      // most of 20 x 2 pellets at 8 m
        if (w == WPN_CROSSBOW) { // one bolt: 40 body, 50 head
            printf("  crossbow %s: %.0f damage\n", head ? "head" : "body", dealt);
            CHECK(std::fabs(dealt - (head ? kCrossbowHeadDamage : weaponDef(WPN_CROSSBOW).damage)) < 0.5f);
        }
        if (w == WPN_RIFLE && head) { // every bullet that lands is a 10 on the head
            printf("  rifle head: %.0f damage\n", dealt);
            CHECK(dealt > 0 && std::fmod(dealt, kRifleHeadDamage) < 0.5f);
        }
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
        MoveState m; Map map; map.load("bastion");
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
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    // switching weapons with fire or alt-fire still held doesn't fire the new one: let go and press again
    for (uint16_t btn : {(uint16_t)BTN_ALTFIRE, (uint16_t)BTN_FIRE}) {
        pump(s, cls, 0.6, [&](int) { UserCmd c; c.weapon = WPN_SNIPER; c.buttons = btn; return c; });
        last = p.lastFire;
        pump(s, cls, 0.6, [&](int) { UserCmd c; c.weapon = WPN_RIFLE; c.buttons = btn; return c; });
        bool fired = p.lastFire != last;
        if (fired) printf("  switching with button %d held fired the new weapon\n", btn);
        CHECK(!fired);
        pump(s, cls, 0.1, [](int) { UserCmd c; c.weapon = WPN_RIFLE; return c; });
        pump(s, cls, 0.3, [](int) { UserCmd c; c.weapon = WPN_RIFLE; c.buttons = BTN_FIRE; return c; });
        CHECK(p.lastFire != last);   // a fresh press fires
    }
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
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    CHECK(s.tickRate() == 1000);
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "solo");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 1.0, idle);
    CHECK(a.tickRate() == s.tickRate());
    // A healthy loopback client reports no loss and the server stays at 1000 Hz (snapshots go out every
    // other tick: counting ticks instead of snapshots once read as exactly 50% loss and dropped the rate).
    pump(s, cls, 3.2 * 1.5, idle);
    printf("  loopback: reported loss %d%%, tick rate %d, snapshot rate %d\n", a.snapshotLoss(), s.tickRate(), a.snapRate());
    CHECK(a.snapshotLoss() <= 2);
    CHECK(s.tickRate() == 1000);
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
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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

// Syringe: holding fire heals you 1 hp per 0.1 s; a thrown one heals the teammate it hits by 30 and resets their
// cooldowns; it does nothing to an enemy.
static void testSyringe(uint16_t port) {
    printf("syringe\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_KOTH; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 1.0, idle);
    b.sendCommand("team " + std::string(s.players_[a.clientId()].team == TEAM_A ? "a" : "b"));
    pump(s, cls, 2.5, idle);   // (respawns on the new team)
    auto& pa = s.players_[a.clientId()];
    auto& pb = s.players_[b.clientId()];
    CHECK(pa.team == pb.team && pa.alive && pb.alive);
    pump(s, cls, 0.2, [](int) { UserCmd c; c.weapon = WPN_SYRINGE; return c; });   // (out first: switching with fire held doesn't fire)
    pa.health = 50;
    pump(s, cls, 1.0, [](int i) { UserCmd c; c.weapon = WPN_SYRINGE; if (i == 0) c.buttons = BTN_FIRE; return c; });
    printf("  held 1 s: %.0f hp (from 50)\n", pa.health);
    CHECK(pa.health >= 55 && pa.health <= 62);
    // b throws at a, 5 m in front of it (yaw 0 looks down -z)
    pa.health = 40; pa.move.dashCooldown = 1.5f; pa.nextFire = s.time() + 5;
    Vec3 base = pb.move.pos;
    auto place = [&] { pb.move.pos = base; pb.move.vel = {}; pa.move.pos = base + Vec3(0, 0, -5); pa.move.vel = {}; };
    for (int k = 0; k < 5; k++) { place(); pump(s, cls, 0.02, idle); }
    int tick = 0;
    pump(s, cls, 0.5, [&](int i) { UserCmd c; c.weapon = WPN_SYRINGE; if (i == 1) { c.yaw = 0; c.pitch = -0.05f; if (tick++ < 30) c.buttons = BTN_ALTFIRE; } if (i == 0) place(); return c; });
    printf("  thrown: %.0f hp (from 40), dash cooldown %.2f\n", pa.health, pa.move.dashCooldown);
    CHECK(pa.health >= 69 && pa.health <= 72);
    CHECK(pa.move.dashCooldown <= 0.01f && pa.nextFire <= s.time());
    s.stop();
}

// An editor ladder is one object: its rails and rungs are generated from the volume (re-spaced when it's stretched),
// never saved as boxes, and climbable like any other ladder.
static void testEditorLadder() {
    printf("editor ladder\n");
    Map m;
    CHECK(m.parse("box -10 -1 -10 10 0 10\nbox -10 0 -1 10 6 0\nladder -0.45 0 0 0.45 5 0.7 auto\nspawn any 0 0 4\n"));
    size_t made = m.brushes.size() - m.editableBrushes();
    CHECK(m.editableBrushes() == 2 && made >= 10);             // 2 rails + a rung every 0.4 m
    CHECK(m.serialize().find("ladder -0.45 0 0 0.45 5 0.7 auto") != std::string::npos);
    CHECK(m.serialize().find("trim") == std::string::npos);    // (the rungs aren't saved)
    CHECK(applyMapEdit(m, "d set 0 1 -0.45 0 0 0.45 9 0.7"));   // stretch it 4 m taller
    CHECK(m.brushes.size() - m.editableBrushes() > made + 8);
    CHECK(applyMapEdit(m, "b ins 2 0 3 0 3 4 1 4"));            // a new box goes before the generated ones
    CHECK(m.editableBrushes() == 3 && !m.brushes[2].generated);
    CHECK(applyMapEdit(m, "d del 0") && m.brushes.size() == 3);
    MoveState ms; ms.pos = {0, 0, 0.45f};                       // and it climbs (put one back, plain volume)
    CHECK(applyMapEdit(m, "d ins 0 1 -0.45 0 0 0.45 7 0.7"));
    for (int t = 0; t < 600 && ms.pos.y <= 5.0f; t++) { UserCmd c; c.forward = 127; c.yaw = 0; c.pitch = 0.3f; c.msec = 8; pmove(ms, c, m); }
    if (ms.pos.y <= 5.0f) printf("  climbed to %.2f %.2f %.2f\n", ms.pos.x, ms.pos.y, ms.pos.z);
    CHECK(ms.pos.y > 5.0f);
}

// Editor groups (room / stairs / custom): one object whose boxes are made from its box, saved as the group, stretched as one.
static void testEditorGroups() {
    printf("editor groups\n");
    Map m;
    CHECK(m.parse("box -20 -1 -20 20 0 20\nspawn any 0 0 -10\n"));
    CHECK(applyMapEdit(m, "g ins 0 2 0 0 0 0 0 2 2 4"));       // stairs 2 up over 2 wide, rising toward +x
    size_t steps = m.brushes.size() - m.editableBrushes();
    CHECK(steps == 4);
    for (auto& b : m.brushes) if (b.generated) CHECK(b.box.mx.y - b.box.mn.y <= 2.0f);
    CHECK(applyMapEdit(m, "g set 0 2 0 0 0 0 0 8 5 4"));       // stretched: more steps, each still <= a step up
    CHECK(m.brushes.size() - m.editableBrushes() == 10);
    MoveState ms; ms.pos = {-1, 0, 3};                          // and you can walk up it
    float top = 0;
    for (int t = 0; t < 600 && ms.pos.x < 7.5f; t++) { UserCmd c; c.forward = 127; c.yaw = -PI / 2; c.msec = 8; pmove(ms, c, m); top = std::max(top, ms.pos.y); }
    if (top < 4.9f) printf("  walked to %.2f %.2f %.2f\n", ms.pos.x, ms.pos.y, ms.pos.z);
    CHECK(top > 4.9f);
    CHECK(applyMapEdit(m, "g ins 1 1 3 0 -10 0 -10 -2 3.5 -2")); // a room: floor, ceiling, 4 walls of 3 pieces (a door each)
    CHECK(m.brushes.size() - m.editableBrushes() == 10 + 2 + 12);
    CHECK(!m.boxSolid({{-6.4f, 0.3f, -10.5f}, {-5.6f, 2.0f, -9.5f}})); // the doorway on the -z wall is open
    CHECK(applyMapEdit(m, "g ins 2 0 0 0 5 0 5 7 2 7") && applyMapEdit(m, "g part 2 1 0 0 0 0.5 0.5 1") && applyMapEdit(m, "g part 2 0 0.5 0.5 0 1 1 1"));
    CHECK(!applyMapEdit(m, "g part 0 1 0 0 0 1 1 1"));           // only custom groups take parts
    CHECK(applyMapEdit(m, "g set 2 0 0 0 5 0 5 9 4 7"));          // stretched: its parts follow
    bool found = false;
    for (auto& b : m.brushes) found |= b.generated && b.material == 1 && b.box.mx.x == 7 && b.box.mx.y == 2;
    CHECK(found);
    Map back;
    CHECK(back.parse(m.serialize()) && back.groups.size() == 3 && back.groups[2].parts.size() == 2 && back.brushes.size() == m.brushes.size());
    CHECK(applyMapEdit(m, "g del 0") && m.groups.size() == 2);
}

static void testTimeToKill() {
    printf("time to kill with perfect aim (1-4.5 s), no shot over 50\n");
    for (int w = 0; w < WPN_COUNT; w++) {
        if (w == WPN_SYRINGE) continue;    // (it heals)
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
    cfg.map = "clockwork"; cfg.mode = MODE_DUEL; cfg.port = port;
    cfg.logLevel = LOG_EXTREME; cfg.logFile = logPath;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    {   // the sides are even and full: joining calls a vote to let them in; passing it makes the teams uneven
        c.sendCommand("team a");
        pump(s, cls, 0.3, idle);
        CHECK(s.vote_.kind == Server::VOTE_LETIN);
        a.sendCommand("vote yes");
        pump(s, cls, 0.3, idle);
        CHECK(s.players_[c.clientId()].team == TEAM_A || s.players_[c.clientId()].team == TEAM_B);
        CHECK(s.players_[c.clientId()].letIn);
        c.sendCommand("team spec");                  // (back out, so the rest is a plain 1v1)
        pump(s, cls, 0.3, idle);
        CHECK(s.players_[c.clientId()].team == TEAM_SPECTATOR && !s.players_[c.clientId()].letIn);
    }
    pump(s, cls, 6.0, idle); // warmup countdown -> round 1
    CHECK(s.ms_.phase == PHASE_FREEZE || s.ms_.phase == PHASE_LIVE);
    unsigned seen = 0; int prev = -1;
    for (int round = 0; round < 12 && s.ms_.phase != PHASE_MATCH_END; round++) {
        while (s.ms_.phase != PHASE_LIVE && s.ms_.phase != PHASE_MATCH_END) pump(s, cls, 0.2, idle);
        if (s.ms_.phase == PHASE_MATCH_END) break;
        int mod = s.ms_.duelMod;
        CHECK(mod < DUEL_MOD_COUNT && mod != prev && !(seen >> mod & 1)); // never a repeat within the match
        prev = mod;
        seen |= 1u << mod;
        int ia = a.clientId(), ib = b.clientId();
        auto& pa = s.players_[ia];
        auto& pb = s.players_[ib];
        // the modifier is applied to both players, and the forced weapon can't be switched away
        pump(s, cls, 0.1, [&](int) { UserCmd u; u.weapon = WPN_SNIPER; return u; });
        const DuelModDef& dm = duelModDef(mod);
        if (dm.weapons) CHECK((dm.weapons >> pa.weapon & 1) && (dm.weapons >> pb.weapon & 1));
        else CHECK(pa.weapon == WPN_SNIPER);
        CHECK(pa.selectedWeapon == WPN_SNIPER);
        CHECK(pa.health == dm.health && pb.health == dm.health);
        if (dm.moveMods & MOVE_GRAPPLE_ONLY) {
            CHECK(pa.move.mods & MOVE_GRAPPLE_ONLY);
            CHECK(a.predicted().mods & MOVE_GRAPPLE_ONLY);
            Vec3 start = pa.move.pos;
            pump(s, cls, 0.5, [&](int) { UserCmd u; u.forward = 127; u.buttons = BTN_JUMP | BTN_DASH; return u; });
            CHECK(std::fabs(pa.move.pos.x - start.x) + std::fabs(pa.move.pos.z - start.z) < 0.05f);
        } else {
            CHECK(!(pa.move.mods & MOVE_GRAPPLE_ONLY));
        }
        {
            AABB h = s.hitBox(pa.move.pos, false);
            CHECK(std::fabs((h.mx.y - h.mn.y) - kPlayerHeight * dm.size) < 0.01f);
        }
        if ((dm.rules & AR_VAMPIRE) && dm.damageMul == 1.0f) {
            pa.health = 50;
            s.damagePlayer(ib, ia, 20, WPN_LASER, {});
            CHECK(pa.health > 69.0f && pa.health <= 70.0f && pb.health == 80); // early in the round: (nearly) full healing
            float rs = s.roundStart_;
            s.roundStart_ = s.time() - kVampireFadeTime;                     // a minute in: healing down to half
            pa.health = 50; pb.health = 80;
            s.damagePlayer(ib, ia, 20, WPN_LASER, {});
            CHECK(std::fabs(pa.health - 60.0f) < 0.01f);
            s.roundStart_ = rs;
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
        CHECK(all.find("map clockwork mode duel") != std::string::npos);
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
    cfg.map = "clockwork"; cfg.mode = MODE_DUEL; cfg.port = port; cfg.duelTeamSize = 2;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    if (!getenv("QUARK_MASTER") || strncmp(getenv("QUARK_MASTER"), "http", 4) != 0) return;
    printf("internet server list via %s\n", getenv("QUARK_MASTER"));
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port; cfg.publicList = true; cfg.name = "listed";
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
static constexpr float kAimSample = 25.0f; // s per level: shorter samples let neighbouring levels swap places
static void testBotAim(uint16_t& port) {
    printf("bot aim: damage per second vs a strafing player, by level\n");
    float dps[BOT_SKILLS] = {};
    for (int k = 0; k < BOT_SKILLS; k++) {
        Server s;
        ServerConfig cfg;
        cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port++;
        s.setLog([](const std::string&) {});
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
        int dir = 127;
        float nextJuke = s.time() + 1.2f;
        pump(s, cls, kAimSample, [&](int) {
            if (!p.alive || !bp.alive) return UserCmd();
            dealt += 100 - p.health;
            if (bp.lastFire != lastShot) { shots++; lastShot = bp.lastFire; }
            p.health = 100;
            p.move.pos.x = -7;                                   // strafe back and forth along z, turning at +-4 m
            if (std::fabs(p.move.pos.z + 24) > 8) p.move.pos.z = -24; // (from its spawn: put it in front of the bot)
            float off = p.move.pos.z + 24;                       // (and every 1.2 s of game time: jukes. Game time,
            if ((off > 4 && p.move.vel.z > 0) || (off < -4 && p.move.vel.z < 0)) { dir = -dir; nextJuke = s.time() + 1.2f; } // so the same however loaded the machine)
            else if (s.time() >= nextJuke) { dir = -dir; nextJuke = s.time() + 1.2f; }
            bp.move.pos = {7, 0, -24}; bp.move.vel = {};          // bot 14 m away, open floor, standing (aim only)
            UserCmd c;
            c.side = (int8_t)dir;
            c.yaw = -PI / 2;
            return c;
        });
        dps[k] = dealt / kAimSample;
        CHECK(bp.shotsFired + 1 >= shots && (dealt <= 0 || (bp.shotsHit > 0 && bp.shotsHit <= bp.shotsFired))); // scoreboard accuracy counts them
        printf("  %-12s %5.1f dps, %.0f%% of shots hit\n", botSkillName(k), dps[k], shots ? dealt / weaponDef(WPN_RIFLE).damage / shots * 100 : 0.0f);
        s.stop();
    }
    // short samples are noisy between neighbouring levels; the spread across levels must hold
    CHECK(dps[0] < dps[2] && dps[1] < dps[3] && dps[2] < dps[3]);
    CHECK(dps[0] > 1); // beginners still land some hits (15 s samples vary: about 2-5 dps)
}

// Alt-fires: thrown knife hits at range, shotgun ricochets hit someone behind you (off the wall in front),
// scoped rifle fires at half rate.
// Every ladder on every map gets you to the top: stand at its foot, hold forward (into the wall) and you
// end up standing on whatever it leads to.
int mapReport(const char* outPath); // tests/mapreport.cpp (returns how many map scores are below 7)

static void testLadders() {
    printf("ladders: every one climbable\n");
    for (auto& name : listMaps()) {
        Map m;
        if (!m.load(name)) continue;
        for (auto& l : m.ladders) {
            bool ok = false;
            for (float yaw : {0.0f, PI, PI * 0.5f, -PI * 0.5f}) {
                MoveState ms;
                Vec3 c = l.center();
                Vec3 back = -angleForward(yaw, 0) * 0.3f;          // start a step away from the wall
                ms.pos = {c.x + back.x, l.mn.y + 0.05f, c.z + back.z};
                if (m.boxSolid(playerBox(ms.pos, false))) continue;
                UserCmd cmd; cmd.yaw = yaw; cmd.pitch = 0.2f; cmd.forward = 127; cmd.msec = 8;
                for (int t = 0; t < 1000 && !ok; t++) {   // up to 8 s: standing on top at any point counts
                    cmd.forward = t < 700 ? 127 : -127;            // a ladder that ends at a wall: step back off it
                    pmove(ms, cmd, m);
                    if (getenv("LADDER_TRACE") && t % 50 == 0 && name == getenv("LADDER_TRACE") && (!getenv("LADDER_AT") || std::fabs(c.x - atof(getenv("LADDER_AT"))) < 0.5f)) printf("      t %d yaw %.2f pos %.2f %.2f %.2f vel %.2f %.2f %.2f ladder %d\n", t, yaw, ms.pos.x, ms.pos.y, ms.pos.z, ms.vel.x, ms.vel.y, ms.vel.z, (int)m.onLadder(playerBox(ms.pos, false)));
                    ok = ms.pos.y > l.mx.y - 1.3f && (ms.flags & MF_ONGROUND) && !m.onLadder(playerBox(ms.pos, false));
                }
                if (ok) break;
                if (getenv("LADDER_DEBUG")) printf("    yaw %.2f -> %.2f %.2f %.2f ground %d\n", yaw, ms.pos.x, ms.pos.y, ms.pos.z, ms.flags & MF_ONGROUND);
            }
            if (!ok) printf("  %s: ladder at %.1f %.1f %.1f can't be climbed\n", name.c_str(), l.center().x, l.mn.y, l.center().z);
            CHECK(ok);
        }
    }
}

// Leaving and rejoining a server that others are still playing on: the same client object reconnects
// (as the game does), in warmup and mid-match, in a few modes.
static void testRejoin(uint16_t port) {
    printf("rejoin after disconnecting\n");
    for (int mode : {MODE_FFA, MODE_VS, MODE_DUEL}) {
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true; cfg.map = mode == MODE_DUEL ? "clockwork" : "bastion"; cfg.mode = mode; cfg.port = port++;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; continue; }
        NetClient a, b, c;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "ann");
        b.connect("127.0.0.1:" + std::to_string(cfg.port), "bob");
        c.connect("127.0.0.1:" + std::to_string(cfg.port), "cat");
        std::vector<NetClient*> cls = {&a, &b, &c};
        pump(s, cls, 1.0, idle);
        s.command("forcestart");
        pump(s, cls, 2.0, idle);
        for (int round = 0; round < 2; round++) {
            b.disconnect();
            pump(s, cls, 0.5, idle);
            CHECK(s.humanCount() == 2);
            b.connect("127.0.0.1:" + std::to_string(cfg.port), "bob");
            pump(s, cls, 1.5, idle);
            bool ok = b.state() == ConnState::Connected && s.humanCount() == 3;
            if (!ok) printf("  %s: rejoin %d failed: state %d, players %d, error '%s'\n", modeName(mode), round + 1, (int)b.state(), s.humanCount(), b.error().c_str());
            CHECK(ok);
        }
        s.stop();
    }
}

// A custom mode: the server plays by its rules, sends them to a joining client in pieces that rebuild the
// same rules, and moves on to the next map of its pool when a match ends.
static void testCustomMode(uint16_t port) {
    printf("custom mode\n");
    {   // the MOVEMENT page: every constant round-trips through the file / network text, and actually applies
        CustomRules r; r.name = "move test";
        for (int k = 0; k < MV_COUNT; k++) r.move[k] = 1.0f + 0.05f * (k + 1);
        CustomRules back;
        CHECK(back.fromText(r.toText()));
        bool same = true;
        for (int k = 0; k < MV_COUNT; k++) same &= std::fabs(back.move[k] - r.move[k]) < 1e-4f;
        CHECK(same);
        Map open;
        open.brushes.push_back({{{-200, -1, -200}, {200, 0, 200}}, 0});
        open.rebuild();
        auto speedAfter = [&](float accel) {   // run speed 0.05 s after starting from a standstill
            CustomRules g; g.name = "accel"; g.move[MV_GROUND_ACCEL] = accel; setActiveRules(g);
            MoveState q; q.pos = {0, 0.01f, 0};
            UserCmd u; u.msec = 1; u.forward = 127;
            for (int i = 0; i < 50; i++) pmove(q, UserCmd{}, open);
            for (int i = 0; i < 50; i++) pmove(q, u, open);
            return std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z);
        };
        float normal = speedAfter(1), quick = speedAfter(2);
        setActiveRules(CustomRules());
        printf("  ground accel x2: %.1f m/s after 0.05 s (normal %.1f)\n", quick, normal);
        CHECK(quick > normal + 1.0f);
    }
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true; cfg.map = "bastion"; cfg.port = port;
    CustomRules& c = cfg.custom;
    c.name = "test rules"; c.baseMode = MODE_FFA; c.health = 150; c.gravity = 0.5f; c.timeLimit = 5;
    c.damage[WPN_RIFLE] = 2; c.fireRate[WPN_ROCKET] = 2;
    c.weapons = (uint16_t)(1u << WPN_RIFLE | 1u << WPN_ROCKET);
    c.maps = {"harbor", "bastion"};
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    CHECK(s.config().map == "bastion" && s.config().mode == MODE_FFA);   // the host's map is in the pool: it stays
    CHECK(std::fabs(weaponDef(WPN_RIFLE).damage - baseWeaponDef(WPN_RIFLE).damage * 2) < 1e-3f);
    CHECK(std::fabs(weaponDef(WPN_ROCKET).refire - baseWeaponDef(WPN_ROCKET).refire / 2) < 1e-4f);
    CHECK(std::fabs(moveTuning().gravity - 0.5f) < 1e-4f);
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "ann");
    std::vector<NetClient*> cls = {&a};
    std::vector<std::string> parts;
    auto collect = [&] { for (auto& e : a.takeEvents()) if (e.type == EV_RULES && e.b > 0 && e.a < e.b) { parts.resize(e.b); parts[e.a] = e.text; } };
    pump(s, cls, 1.0, idle);
    collect();
    std::string all;
    for (auto& p : parts) all += p;
    CustomRules got;
    CHECK(!parts.empty() && got.fromText(all));
    CHECK(got.name == c.name && got.health == 150 && got.weapons == c.weapons && got.maps == c.maps);
    CHECK(std::fabs(got.damage[WPN_RIFLE] - 2) < 1e-3f && std::fabs(got.gravity - 0.5f) < 1e-3f);
    s.command("forcestart");
    for (int i = 0; i < 70 && s.config().map == "bastion"; i++) { pump(s, cls, 0.5, idle); collect(); }
    CHECK(s.config().map == "harbor");   // the pool rotated
    s.stop();
    CHECK(!activeRules().active() && std::fabs(weaponDef(WPN_RIFLE).damage - baseWeaponDef(WPN_RIFLE).damage) < 1e-4f);
}

// Every staircase (recorded by tools/make_maps.py as "# stairs x0 z0 x1 z1 base top axis rising solid"):
// walked from the bottom to the top by the real movement code, and checked against the paths around it:
// no walkable surface may pass through a solid flight, or run underneath an open one with less than a
// player's height of clearance.
static void testStairs() {
    printf("stairs: every flight walkable, none cutting a path\n");
    int flights = 0;
    for (auto& name : listMaps()) {
        Map m;
        if (!m.load(name)) continue;
        NavGraph g;
        g.build(m);
        std::istringstream src(m.source);
        std::string line;
        while (std::getline(src, line)) {
            if (line.rfind("# stairs ", 0) != 0) continue;
            std::istringstream ls(line.substr(9));
            float x0, z0, x1, z1, base, top, rising; std::string axis; int solid;
            if (!(ls >> x0 >> z0 >> x1 >> z1 >> base >> top >> axis >> rising >> solid)) continue;
            flights++;
            bool ax = axis == "x";
            // walk it: from in front of the low end, or from either side of the lowest step, run at the top
            // of the flight (the real movement code, 8 ms ticks)
            float lo = ax ? (rising > 0 ? x0 : x1) : (rising > 0 ? z0 : z1), hi = ax ? (rising > 0 ? x1 : x0) : (rising > 0 ? z1 : z0);
            float dir = rising > 0 ? 1.0f : -1.0f, mid = ax ? (z0 + z1) / 2 : (x0 + x1) / 2;
            float half = (ax ? (z1 - z0) : (x1 - x0)) / 2, step0 = lo + dir * 0.4f;
            auto P = [&](float along, float across, float y) { return ax ? Vec3(along, y, across) : Vec3(across, y, along); };
            bool up = false;
            MoveState ms;
            for (Vec3 start : {P(lo - dir * 0.7f, mid, base + 0.05f), P(lo - dir * 0.7f, mid - half * 0.6f, base + 0.05f), P(lo - dir * 0.7f, mid + half * 0.6f, base + 0.05f),
                               P(step0, mid - half - 0.6f, base + 0.05f), P(step0, mid + half + 0.6f, base + 0.05f)}) {
                float across = ax ? start.z : start.x, a0 = mid - half + 0.45f, a1 = mid + half - 0.45f;
                Vec3 goal = P(hi - dir * 0.3f, a0 < a1 ? std::clamp(across, a0, a1) : mid, top);
                ms = MoveState();
                ms.pos = start;
                if (m.boxSolid(playerBox(ms.pos, false))) continue;
                for (int t = 0; t < 40; t++) { UserCmd idle; idle.msec = 8; pmove(ms, idle, m); }   // settle: is there floor here?
                if (getenv("STAIRS_TRACE") && name == getenv("STAIRS_TRACE")) printf("    start %.2f %.2f %.2f ground %d\n", ms.pos.x, ms.pos.y, ms.pos.z, (int)(ms.flags & MF_ONGROUND));
                if (!(ms.flags & MF_ONGROUND) || std::fabs(ms.pos.y - base) > 0.3f) continue;
                bool side = std::fabs(across - mid) > half;   // from the side: step onto the lowest step first
                for (int t = 0; t < 700 && !up; t++) {
                    Vec3 to = (side && t < 50) ? P(step0, mid, base) - ms.pos : goal - ms.pos;
                    UserCmd cmd; cmd.yaw = std::atan2(-to.x, -to.z); cmd.pitch = 0; cmd.forward = 127; cmd.msec = 8;
                    if (dot(angleForward(cmd.yaw, 0), Vec3(to.x, 0, to.z)) < 0) cmd.yaw += PI;
                    pmove(ms, cmd, m);
                    if (getenv("STAIRS_TRACE") && name == getenv("STAIRS_TRACE") && (t % 40 == 0 || (getenv("STAIRS_ALL") && t > 110 && t < 150))) printf("      t %d pos %.2f %.2f %.2f vel %.2f %.2f %.2f\n", t, ms.pos.x, ms.pos.y, ms.pos.z, ms.vel.x, ms.vel.y, ms.vel.z);
                    up = ms.pos.y > top - 0.1f && (ms.flags & MF_ONGROUND);
                    if (ms.pos.y < base - 1.0f) break;
                }
                if (up) break;
            }
            CHECK(up);
            {   // jumping up it (and falling onto it) must carry you over the risers like walking: never stopped dead in the air
                MoveState js;
                js.pos = P(lo - dir * 0.7f, mid, base + 0.05f);
                int bonks = 0;
                if (!m.boxSolid(playerBox(js.pos, false))) {
                    for (int t = 0; t < 40; t++) { UserCmd idle; idle.msec = 8; pmove(js, idle, m); }
                    Vec3 goal = P(hi - dir * 0.3f, mid, top);
                    for (int t = 0; t < 500; t++) {
                        Vec3 to = goal - js.pos;
                        if (length(Vec3(to.x, 0, to.z)) < 0.4f) break;
                        UserCmd cmd; cmd.yaw = std::atan2(-to.x, -to.z); cmd.forward = 127; cmd.msec = 8;
                        cmd.buttons = (t / 30) % 2 ? 0 : BTN_JUMP;
                        float h0 = std::sqrt(js.vel.x * js.vel.x + js.vel.z * js.vel.z);
                        pmove(js, cmd, m);
                        float h1 = std::sqrt(js.vel.x * js.vel.x + js.vel.z * js.vel.z);
                        if (!(js.flags & MF_ONGROUND) && h0 > 3 && h1 < h0 * 0.5f) {
                            bonks++;
                            if (getenv("STAIRS_TRACE")) printf("    bonk t %d at %.2f %.2f %.2f vel %.2f %.2f %.2f (h %.1f -> %.1f)\n", t, js.pos.x, js.pos.y, js.pos.z, js.vel.x, js.vel.y, js.vel.z, h0, h1);
                        }
                        if (js.pos.y < base - 1.0f) break;
                    }
                }
                CHECK(bonks == 0);
                if (bonks) printf("  %s: stairs %g %g - %g %g: jumping up them stops you dead %d times\n", name.c_str(), x0, z0, x1, z1, bonks);
            }
            if (!up) printf("  %s: stairs %g %g - %g %g (%g to %g) can't be walked up (got to %.1f %.1f %.1f)\n", name.c_str(), x0, z0, x1, z1, base, top, ms.pos.x, ms.pos.y, ms.pos.z);
            // paths through it or under it
            int n = std::max(1, (int)std::ceil((top - base) / 0.5f - 1e-4f));
            for (size_t k = 0; k < g.nodes.size(); k++) {
                auto& nd = g.nodes[k];
                float ox = std::min(nd.x1, x1) - std::max(nd.x0, x0), oz = std::min(nd.z1, z1) - std::max(nd.z0, z0);
                if (ox < 0.3f || oz < 0.3f) continue;
                bool inside = nd.x0 >= x0 - 0.2f && nd.x1 <= x1 + 0.2f && nd.z0 >= z0 - 0.2f && nd.z1 <= z1 + 0.2f;
                if (inside && nd.y >= base - 0.01f && nd.y <= top + 0.01f) continue; // its own steps (or what stands on them)
                if (std::fabs(nd.y - base) < 0.3f || nd.y >= top - 0.01f) continue;  // the floor it stands on / the landing
                bool bad = false;
                if (solid) bad = nd.y > base && nd.y < top; // a surface inside the block
                else {
                    // under an open flight: the lowest step bottom above this surface must clear a player
                    for (int i = 0; i < n && !bad; i++) {
                        float a = (float)i / n, b = (float)(i + 1) / n;
                        float h = base + (top - base) * (rising > 0 ? (i + 1) : (n - i)) / n;
                        float s0 = (ax ? x0 : z0) + ((ax ? x1 : z1) - (ax ? x0 : z0)) * a, s1 = (ax ? x0 : z0) + ((ax ? x1 : z1) - (ax ? x0 : z0)) * b;
                        float n0 = ax ? nd.x0 : nd.z0, n1 = ax ? nd.x1 : nd.z1;
                        if (std::min(n1, s1) - std::max(n0, s0) < 0.3f) continue;
                        float bottom = h - 1.0f;
                        bad = nd.y < bottom && bottom - nd.y < kPlayerHeight;
                    }
                }
                if (bad) printf("  %s: stairs %g %g - %g %g (%g to %g) cut the path at %.1f..%.1f %.1f..%.1f, y %.1f\n", name.c_str(), x0, z0, x1, z1, base, top, nd.x0, nd.x1, nd.z0, nd.z1, nd.y);
                CHECK(!bad);
            }
        }
    }
    printf("  %d flights\n", flights);
}

// Hide and seek and tag: one seeker / "it" with a knife, run-and-jump only, the seeker waits out the
// hiders' head start, one stab finds a hider (tag: passes "it" on), time up eliminates "it" in tag.
static void testItModes(uint16_t port) {
    printf("hide and seek, tag\n");
    for (int mode : {MODE_HIDE, MODE_TAG}) {
        Server s;
        ServerConfig cfg;
        cfg.autoStart = false; cfg.map = "hamlet"; cfg.mode = mode; cfg.port = port++;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; continue; }
        NetClient c[3];
        std::vector<NetClient*> cls;
        for (int i = 0; i < 3; i++) { c[i].connect("127.0.0.1:" + std::to_string(cfg.port), "p" + std::to_string(i)); cls.push_back(&c[i]); }
        pump(s, cls, 1.5, idle);
        s.command("forcestart");
        pump(s, cls, 0.3, idle);
        int it = -1, n = 0;
        for (int i = 0; i < 3; i++) if (s.players_[c[i].clientId()].it) { it = c[i].clientId(); n++; }
        CHECK(n == 1);
        if (it < 0) { s.stop(); continue; }
        auto& seeker = s.players_[it];
        CHECK(seeker.weapon == WPN_KNIFE && !(seeker.move.mods & MOVE_RUN_ONLY));
        int h1 = c[0].clientId() == it ? c[1].clientId() : c[0].clientId();
        int h2 = 0; for (int i = 0; i < 3; i++) if (c[i].clientId() != it && c[i].clientId() != h1) h2 = c[i].clientId();
        if (mode == MODE_HIDE) {                       // head start: the seeker can't move, hiders can
            CHECK(s.ms_.phase == PHASE_FREEZE);
            Vec3 p0 = seeker.move.pos, q0 = s.players_[h1].move.pos;
            pump(s, cls, 0.5, [](int) { UserCmd u; u.forward = 127; return u; });
            CHECK(length(seeker.move.pos - p0) < 0.05f && length(s.players_[h1].move.pos - q0) > 1.0f);
        }
        s.phaseEnd_ = s.time();                        // skip to the live round
        pump(s, cls, 0.1, idle);
        CHECK(s.ms_.phase == PHASE_LIVE);
        auto stab = [&](int from, int to) {            // stand 1 m apart, the attacker facing the victim
            for (int k = 0; k < 8; k++) {
                s.players_[from].move.pos = {0, 0, 6}; s.players_[from].move.vel = {};
                s.players_[to].move.pos = {0, 0, 5}; s.players_[to].move.vel = {};
                pump(s, cls, 0.05, [&](int i) { UserCmd u; u.yaw = 0; u.pitch = -0.3f; if (cls[i]->clientId() == from) u.buttons = BTN_FIRE; return u; });
            }
        };
        {   // no knife throws, not even holding alt-fire while clicking fire (that once threw endlessly)
            pump(s, cls, 0.5, [&](int i) { UserCmd u; if (cls[i]->clientId() == it) u.buttons = BTN_FIRE | BTN_ALTFIRE; return u; });
            int thrown = 0;
            for (auto& pr : s.projectiles_) thrown += pr.active && pr.type == WPN_KNIFE;
            CHECK(thrown == 0 && s.players_[it].weapon == WPN_KNIFE);
        }
        stab(h1, it);                                  // hiders have nothing to hit with
        CHECK(s.players_[it].alive && s.players_[it].it);
        stab(it, h1);
        if (mode == MODE_HIDE) {
            CHECK(!s.players_[h1].alive);              // found with one stab
            s.players_[it].itFreezeUntil = 0;
            stab(it, h2);
            pump(s, cls, 0.2, idle);
            CHECK(s.ms_.phase == PHASE_ROUND_END && s.players_[it].score >= 3);
        } else {
            CHECK(s.players_[h1].it && !s.players_[it].it && s.players_[h1].alive);   // tagged: "it" moved on
            CHECK(rulesFrozen(s, h1));                 // the new "it" waits a moment
            s.phaseEnd_ = s.time();                    // time's up: h1 is out
            pump(s, cls, 0.2, idle);
            CHECK(s.players_[h1].out && s.ms_.phase == PHASE_ROUND_END);
            s.phaseEnd_ = s.time(); pump(s, cls, 0.2, idle);           // next round: two left
            int it2 = -1; for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].it) it2 = i;
            CHECK(it2 >= 0 && it2 != h1 && !s.players_[h1].alive);
            s.phaseEnd_ = s.time(); pump(s, cls, 0.1, idle);           // freeze over
            s.phaseEnd_ = s.time(); pump(s, cls, 0.2, idle);           // time's up again: one left wins
            CHECK(s.ms_.phase == PHASE_MATCH_END);
        }
        s.stop();
    }
}

// Rocket alt-fire: a lob at half speed that falls 3x faster.
// "Reset maps": every shipped map is written back from the copy built into the game.
static void testResetMaps() {
    printf("reset built-in maps\n");
    std::string path = findDataPath("maps") + "/temple.qmap";
    { std::ofstream f(path, std::ios::app); f << "box 0 0 0 1 1 1\n"; }
    Map m;
    CHECK(m.load("temple") && !isOfficialMap(m));
    CHECK(resetOfficialMaps() >= 13);
    Map r;
    CHECK(r.load("temple") && isOfficialMap(r));
    for (auto& n : listMaps()) { Map k; CHECK(k.load(n) && isOfficialMap(k)); }
}

static void testRocketLob(uint16_t port) {
    printf("rocket lob (alt-fire)\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = false; cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "lobber");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 2.0, idle);
    auto fire = [&](uint16_t b) { return [&, b](int) { UserCmd c; c.weapon = WPN_ROCKET; c.buttons = b; c.pitch = 0.3f; return c; }; };
    pump(s, cls, 0.3, fire(0));
    pump(s, cls, 0.03, fire(BTN_ALTFIRE));
    const Projectile* lob = nullptr;
    for (auto& pr : s.projectiles_) if (pr.active && pr.type == WPN_ROCKET) lob = &pr;
    CHECK(lob && lob->gravMul == kRocketLobGravity);
    if (lob) {
        float sp = weaponDef(WPN_ROCKET).projSpeed;
        CHECK(length(Vec3(lob->vel.x, 0, lob->vel.z)) < sp * kRocketLobSpeed + 0.5f);
    }
    s.stop();
}

static void testAltFires(uint16_t port) {
    printf("alt-fires: knife throw, shotgun ricochet, scoped rifle\n");
    for (int kase = 0; kase < 3; kase++) {
        Server s;
        ServerConfig cfg;
        cfg.autoStart = false; // stay in warmup: a match start would move everyone back to spawns
        cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = (uint16_t)(port + kase);
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; continue; }
        NetClient a, b;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "shooter");
        b.connect("127.0.0.1:" + std::to_string(cfg.port), "target");
        std::vector<NetClient*> cls = {&a, &b};
        pump(s, cls, 2.5, idle);
        auto& sa = s.players_[a.clientId()];
        auto& sb = s.players_[b.clientId()];
        int w = kase == 0 ? WPN_KNIFE : kase == 1 ? WPN_SHOTGUN : WPN_RIFLE;
        // knife: target 12 m ahead. shotgun: facing the +x curtain wall (x = 30) from 3.5 m, target 4.5 m behind.
        if (kase == 1) { sa.move.pos = {26.5f, 0, -20}; sb.move.pos = {22, 0, -20}; }
        else { sa.move.pos = {9, 0, 19}; sb.move.pos = {-3, 0, 19}; }
        sa.move.vel = sb.move.vel = {};
        pump(s, cls, 0.3, idle);
        auto aim = [&](int i, uint16_t buttons) {
            UserCmd c;
            if (i != 0) return c;
            c.weapon = (uint8_t)w;
            c.buttons = buttons;
            if (kase == 1) { c.yaw = -PI / 2; c.pitch = 0; return c; } // facing +x
            Vec3 eye = a.predicted().pos + Vec3(0, kEyeHeight, 0), tgt = sb.move.pos + Vec3(0, 1.0f, 0);
            if (kase == 0) { float t = length(tgt - eye) / kKnifeThrowSpeed; tgt.y += 0.5f * kKnifeThrowGravity * t * t; }
            Vec3 d = normalize(tgt - eye);
            c.pitch = std::asin(d.y); c.yaw = std::atan2(-d.x, -d.z);
            return c;
        };
        pump(s, cls, 0.3, [&](int i) { return aim(i, 0); });
        float hp0 = sb.health;
        if (kase == 0) {
            pump(s, cls, 0.05, [&](int i) { return aim(i, BTN_ALTFIRE); });
            CHECK(sa.weapon != WPN_KNIFE && sa.knifeBackAt > s.time());          // knife away: switched weapon
            { // still holding the button: the weapon we switched to must not fire until it's let go
                float before = sa.lastFire;
                pump(s, cls, 0.3, [&](int i) { UserCmd c = aim(i, BTN_ALTFIRE); c.weapon = kNoWeaponChange; return c; });
                CHECK(sa.lastFire == before);
            }
            CHECK(a.hasSnapshot());
            pump(s, cls, 1.0, idle);
            CHECK(hp0 - sb.health >= weaponDef(WPN_KNIFE).damage - 0.01f);
            pump(s, cls, 0.2, [&](int i) { return aim(i, 0); });              // asking for the knife back too soon
            CHECK(sa.weapon != WPN_KNIFE && a.latest().knifeAway > 0);
            pump(s, cls, kKnifeAway, idle);
            pump(s, cls, 0.2, [&](int i) { return aim(i, 0); });
            CHECK(sa.weapon == WPN_KNIFE);
        } else if (kase == 1) {
            pump(s, cls, 0.05, [&](int i) { return aim(i, BTN_FIRE); });          // normal blast: hits the wall only
            pump(s, cls, 0.3, idle);
            CHECK(sb.health == hp0);
            pump(s, cls, 1.2, idle);
            sa.move.pos = {26.5f, 0, -20}; sb.move.pos = {22, 0, -20}; sa.move.vel = sb.move.vel = {}; // the match may have started: put them back
            hp0 = sb.health;
            float lf = sa.lastFire;
            pump(s, cls, 0.05, [&](int i) { return aim(i, BTN_ALTFIRE); });       // ricochet comes back at the target
            pump(s, cls, 0.3, idle);
            CHECK(sa.lastFire != lf);
            CHECK(sb.health < hp0 && hp0 - sb.health <= 20 * weaponDef(WPN_SHOTGUN).damage * kShotgunBounceDamage + 0.01f);
            CHECK(sa.health == 100); // never your own ricochet
        } else {
            int shots = 0;
            float last = sa.lastFire;
            pump(s, cls, 2.0, [&](int i) {
                if (sa.lastFire != last) { shots++; last = sa.lastFire; }
                return aim(i, BTN_FIRE | BTN_ALTFIRE);
            });
            float expect = 2.0f / (weaponDef(WPN_RIFLE).refire * kRifleScopedRefire);
            CHECK(shots >= expect - 2 && shots <= expect + 1);
        }
        s.stop();
    }
}

static void testMapVote(uint16_t port) {
    printf("vote to change map / mode\n");
    Server s;
    s.voteCooldown_ = 0;   // (this test calls vote after failed vote)
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    // every map / mode pair the vote menu offers must be accepted
    for (auto& name : listMaps()) {
        Map m;
        if (!m.load(name)) continue;
        for (int k = 0; k < MODE_COUNT; k++) {
            if (!m.supportsMode(k)) continue;
            b.sendCommand(std::string("callvote map ") + name + " " + modeName(k) + (k == MODE_ARENA || k == MODE_TOURNEY ? " classic" : ""));
            pump(s, cls, 0.2, idle);
            if (s.vote_.kind != Server::VOTE_MAP) { printf("  FAIL vote refused: %s %s\n", name.c_str(), modeName(k)); g_fail++; }
            a.sendCommand("vote no"); b.sendCommand("vote no");
            pump(s, cls, 0.2, idle);
            s.vote_.kind = Server::VOTE_NONE;
        }
    }
    s.stop();
}

// Handshake hardening: a CONNECT without the address cookie only gets a tiny challenge (no slot, no
// snapshots), a spoofed CONNECT from a connected player's address can't kick them, info requests must be
// padded, tmaps only takes known names, a failed vote has a cooldown, and chat is printable only.
static void testHandshake(uint16_t port) {
    printf("handshake: connect cookie, spoofed kick, info padding, vote limits\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetAddr srv{0x7F000001u, port};
    UdpSocket raw;
    CHECK(raw.open(0));
    auto connectPkt = [](uint32_t nonce, uint32_t cookie, std::vector<uint8_t>& out) {
        uint8_t b[128];
        Writer w(b, sizeof b);
        writeHeader(w, PKT_CONNECT); w.u16(kProtocolVersion); w.u32(nonce); w.u32(cookie); w.str("spoof");
        out.assign(b, b + w.size());
    };
    // drain what the raw socket got while the server runs: returns {packets, biggest, last cookie}
    auto collect = [&](double secs, int& packets, int& biggest, uint32_t& cookie) {
        packets = biggest = 0;
        double end = timeNow() + secs;
        while (timeNow() < end) {
            s.frame();
            uint8_t buf[2048]; NetAddr from; int n;
            while ((n = raw.recv(from, buf, sizeof buf)) > 0) {
                packets++; biggest = std::max(biggest, n);
                Reader r(buf, n);
                if (r.u32() == kProtocolMagic && r.u8() == PKT_CHALLENGE) { r.u32(); cookie = r.u32(); }
            }
            sleepMs(1);
        }
    };
    std::vector<uint8_t> pkt;
    int packets = 0, biggest = 0;
    uint32_t cookie = 0;
    connectPkt(0x1234, 0, pkt);
    raw.send(srv, pkt.data(), pkt.size());
    collect(0.4, packets, biggest, cookie);
    CHECK(packets == 1 && biggest <= (int)pkt.size() && cookie != 0);   // only the small challenge
    CHECK(s.playerCount() == 0);                                        // and no slot
    connectPkt(0x1234, cookie ^ 0x5555, pkt);                           // a wrong cookie: still nothing
    raw.send(srv, pkt.data(), pkt.size());
    collect(0.3, packets, biggest, cookie);
    CHECK(packets == 1 && biggest <= (int)pkt.size() && s.playerCount() == 0);
    uint32_t myCookie = cookie;

    // a real client gets through (two round trips) and plays
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "victim");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 1.0, idle);
    CHECK(a.state() == ConnState::Connected && a.hasSnapshot() && s.playerCount() == 1);
    int vid = a.clientId();
    NetAddr victim = s.players_[vid].addr;
    uint32_t vnonce = s.players_[vid].nonce;
    // spoofed CONNECTs from the victim's address with another nonce: no cookie, and a cookie issued to our own address
    connectPkt(0x9999, 0, pkt);
    s.handlePacket(victim, pkt.data(), (int)pkt.size());
    connectPkt(0x1234, myCookie, pkt);
    s.handlePacket(victim, pkt.data(), (int)pkt.size());
    pump(s, cls, 0.5, idle);
    CHECK(s.players_[vid].connected && s.players_[vid].nonce == vnonce && a.state() == ConnState::Connected);

    // info requests: unpadded ones get nothing, padded ones a reply no bigger than the request
    {
        uint8_t b[kInfoRequestBytes] = {};
        Writer w(b, sizeof b);
        writeHeader(w, PKT_INFO_REQUEST);
        raw.send(srv, b, w.size());
        collect(0.2, packets, biggest, cookie);
        CHECK(packets == 0);
        raw.send(srv, b, sizeof b);
        collect(0.2, packets, biggest, cookie);
        CHECK(packets == 1 && biggest <= kInfoRequestBytes);
    }

    // admin: a loopback player isn't admin unless it's the in-process host's session
    a.sendCommand("addbot");
    pump(s, cls, 0.3, idle);
    CHECK(s.playerCount() == 1);
    s.setHostNonce(a.sessionNonce());
    a.sendCommand("addbot");
    pump(s, cls, 0.3, idle);
    CHECK(s.playerCount() == 2);
    s.command("kickbots");

    // chat is printable only; votes: unknown / too many tmaps refused, cooldown after a failed vote
    a.sendCommand(std::string("say hi\x1b[2Jthere"));
    a.sendCommand("callvote tmaps nosuchmap");
    a.sendCommand("callvote tmaps temple,temple,temple,temple,temple,temple,temple,temple,temple");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE);
    bool sawChat = false;
    for (auto& e : a.takeEvents()) if (e.type == EV_CHAT) { sawChat = true; CHECK(e.text == "hi?[2Jthere"); }
    CHECK(sawChat);
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
        CHECK(m.load("bastion"));
        CHECK(applyMapEdit(m, "m " + std::to_string((1u << MODE_FFA) | (1u << MODE_DUEL))));
        Map r;
        CHECK(r.parse(m.serialize()) && r.allowedModes == m.allowedModes);
        CHECK(r.supportsMode(MODE_FFA) && r.supportsMode(MODE_DUEL) && !r.supportsMode(MODE_KOTH) && r.canSupportMode(MODE_KOTH));
    }
    Map base;
    CHECK(base.load("bastion"));
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

// Ranked: ids from keys, the server's result event, and the report each player's game builds from it
// (the Worker side is tested by tools/master/test.mjs).
static void testRanked(uint16_t port) {
    printf("ranked: keys, results, reports\n");
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex(std::string(1000, 'a')).substr(0, 16) == "41edece42d63e8d9");
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)rand());
    std::string keyPath = findDataPath("maps") + "/../zz_test" + tag + ".key";
    RankKey k1 = loadOrCreateRankKey(keyPath), again = loadOrCreateRankKey(keyPath);
    std::remove(keyPath.c_str());
    CHECK(k1.secret.size() == 64 && k1.id == again.id && k1.id == sha256Hex(k1.secret).substr(0, 32));
    // official maps: every shipped map hashes the same as at build time; an edited copy doesn't count
    for (auto& name : listMaps()) {
        if (name.rfind("zz_", 0) == 0) continue;
        Map m;
        CHECK(m.load(name) && isOfficialMap(m));
    }
    {
        Map m;
        CHECK(m.load("temple"));
        Map edited = m;
        edited.source += "box 0 50 0 1 51 1\n";
        CHECK(!isOfficialMap(edited));
        std::string path = findDataPath("maps") + "/zz_rk" + tag + ".qmap";
        CHECK(saveMapFile(path, edited.source));
        Server s;
        ServerConfig cfg;
        cfg.map = "zz_rk" + std::string(tag); cfg.ranked = true; cfg.port = port;
        s.setLog([](const std::string&) {});
        std::string err;
        CHECK(!s.start(cfg, &err) && err.find("ranked maps") != std::string::npos);
        std::remove(path.c_str());
    }
    for (int variant : {1, 3}) { // laser vampire (a ladder), quirky (not one)
        const int teamSize = 1;
        Server s;
        ServerConfig cfg;
        cfg.map = "anvil"; cfg.mode = MODE_FFA; cfg.port = (uint16_t)(port + variant); cfg.ranked = true; cfg.arenaVariant = variant;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; continue; }
        std::vector<std::unique_ptr<NetClient>> owned;
        std::vector<NetClient*> cls;
        std::vector<RankKey> keys;
        for (int i = 0; i < 2 * teamSize; i++) {
            owned.push_back(std::make_unique<NetClient>());
            cls.push_back(owned.back().get());
            RankKey k;
            k.secret = sha256Hex("player" + std::to_string(i) + tag);
            k.id = rankIdFromSecret(k.secret);
            keys.push_back(k);
            cls[i]->connect("127.0.0.1:" + std::to_string(cfg.port), "p" + std::to_string(i));
            cls[i]->sendCommand("rankid " + k.id);
        }
        pump(s, cls, 1.0, idle);
        CHECK(cls[0]->hasSnapshot() && (cls[0]->mode().flags & MS_RANKED) && s.config().mode == MODE_ARENA); // ranked is always 1v1
        CHECK(cls[0]->mode().arenaVariant == (variant == 1 ? 1 : 0)); // a ruleset that isn't a ladder falls back to vanilla
        for (int i = 0; i < 2 * teamSize; i++) CHECK(cls[0]->rankId(cls[i]->clientId()) == keys[i].id);
        int teamA = 0;
        for (auto& p : s.players_) teamA += p.connected && p.team == TEAM_A;
        CHECK(teamA == teamSize);
        s.openRankedTicket();   // (what going live does; there's no list in tests, so the ticket request just fails)
        std::string why, res = s.rankedResult(TEAM_A, &why);
        CHECK(!res.empty() && res.find(variant == 1 ? " vampire " : " vanilla ") == 16);
        // every player builds the same list of results (only the secret differs)
        std::string expectP;
        for (int i = 0; i < 2 * teamSize; i++) {
            std::vector<std::string> ids, players;
            for (int k = 0; k < kMaxPlayers; k++) ids.push_back(cls[i]->rankId(k));
            std::string ladder, body = buildRankReport(res, ids, keys[i], &players, &ladder);
            CHECK(body.find("secret=" + keys[i].secret + "&") == 0 && (int)players.size() == 2 * teamSize && ladder == (variant == 1 ? "vampire" : "vanilla"));
            std::string p = body.substr(body.find("&p=") + 3);
            if (i == 0) expectP = p;
            CHECK(p == expectP);
            int me = cls[i]->clientId(), team = s.players_[me].team;
            CHECK(p.find(keys[i].id + ":" + std::to_string(team) + ":" + (team == TEAM_A ? "0" : "1")) != std::string::npos);
        }
        RankKey outsider;
        outsider.secret = sha256Hex("nobody");
        outsider.id = rankIdFromSecret(outsider.secret);
        std::vector<std::string> ids;
        for (int k = 0; k < kMaxPlayers; k++) ids.push_back(cls[0]->rankId(k));
        CHECK(buildRankReport(res, ids, outsider).empty());
        {   // each client checks the host's result against the score it saw before reporting it
            std::vector<SeenPlayer> seen(kMaxPlayers);
            for (auto* c : cls) seen[c->clientId()] = {true, s.players_[c->clientId()].team, 0, 0};
            CHECK(rankedResultMatchesView(res, seen, true, 3, 1));          // team A won 3-1: the host's "A won" agrees
            std::string why;
            CHECK(!rankedResultMatchesView(res, seen, true, 1, 3, &why) && why.find("score") != std::string::npos); // B won: a lie
            CHECK(!rankedResultMatchesView(res, seen, true, 2, 2));         // a draw reported as a win
            std::vector<SeenPlayer> more = seen;
            more[kMaxPlayers - 1] = {true, TEAM_B, 0, 0};                    // someone played who isn't in the result
            CHECK(!rankedResultMatchesView(res, more, true, 3, 1));
            std::vector<SeenPlayer> swapped = seen;
            for (auto& p : swapped) if (p.playing) p.team = 1 - p.team;    // sides swapped
            CHECK(!rankedResultMatchesView(res, swapped, true, 3, 1));
            // free for all: places come from score (then kills)
            std::vector<SeenPlayer> f(kMaxPlayers);
            f[0] = {true, 0, 5, 9}; f[1] = {true, 1, 7, 2}; f[2] = {true, 2, 5, 9};
            CHECK(rankedResultMatchesView("0123456789abcdef instagib 0:0:1,1:1:0,2:2:1", f, false, 0, 0));
            CHECK(!rankedResultMatchesView("0123456789abcdef instagib 0:0:0,1:1:1,2:2:1", f, false, 0, 0));
            CHECK(!rankedResultMatchesView("0123456789abcdef instagib 0:0:1,1:1:0", f, false, 0, 0));
            CHECK(!rankedResultMatchesView("0123456789abcdef instagib 0:0:1,0:0:1,1:1:0,2:2:1", f, false, 0, 0)); // a slot twice
            // the server's own result for the Worker: ids in place of slots, and the ticket's match id
            CHECK(s.rankedMatch_.size() == 16 && res.compare(0, 16, s.rankedMatch_) == 0);
            std::string match, list = rankedResultIds(res, ids, &match);
            CHECK(match == s.rankedMatch_ && list == expectP);
        }
        s.addBot(TEAM_B, 1); // a bot in the match: not rated
        CHECK(s.rankedResult(TEAM_A, &why).empty());
        s.stop();
    }
}

// Voting ranked on / off, what ranked forbids, and the update restart flag (waits for the match to end).
static void testRankedVoteAndRestart(uint16_t port) {
    printf("ranked vote, restart for updates\n");
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)rand());
    std::string flag = findDataPath("maps") + "/../zz_update" + tag + ".pending";
    Server s;
    ServerConfig cfg;
    cfg.map = "foundry"; cfg.mode = MODE_FFA; cfg.port = port; cfg.restartFlag = flag;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 0.5, idle);
    s.addBot(TEAM_A, 1);
    a.sendCommand("callvote ranked on instagib");
    pump(s, cls, 0.2, idle);
    b.sendCommand("vote yes");
    pump(s, cls, 0.5, idle);
    CHECK(s.config().ranked && s.config().mode == MODE_FFA && s.ms_.arenaVariant == 5 && isOfficialMap(s.map_));
    CHECK(a.hasSnapshot() && (a.mode().flags & MS_RANKED));
    CHECK((s.arenaRules() & AR_INSTAGIB) && s.weaponMask() == arenaVariant(5).weapons);   // ranked instagib: a free for all on instagib rules
    s.command("forcestart");
    for (int i = 0; i < 50 && s.ms_.phase != PHASE_LIVE; i++) pump(s, cls, 0.1, idle);
    CHECK(s.ms_.phase == PHASE_LIVE && s.ms_.timeLeft > 290 && s.ms_.timeLeft <= 300.5f);   // ... for 5 minutes
    bool bots = false;
    for (auto& p : s.players_) bots |= p.connected && p.isBot;
    CHECK(!bots);
    a.sendCommand("callvote bots on");          // refused while ranked
    a.sendCommand("callvote map harbor ffa");   // refused: not 1v1
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE);
    a.sendCommand("callvote ranked off");
    pump(s, cls, 0.2, idle);
    b.sendCommand("vote yes");
    pump(s, cls, 0.3, idle);
    CHECK(!s.config().ranked && !(a.mode().flags & MS_RANKED));
    // voted editing on a normal server: edits are played, never saved, and gone at the next map change
    Map disk;
    CHECK(disk.load(s.config().map));
    a.sendCommand("callvote edit on");
    pump(s, cls, 0.2, idle);
    b.sendCommand("vote yes");
    pump(s, cls, 0.5, idle);
    CHECK(s.editing_ && s.tempEdit_ && (a.mode().flags & MS_TEMP_EDIT));
    a.sendCommand("ed b ins 0 1 0 20 0 1 21 1");
    pump(s, cls, 0.5, idle);
    CHECK(a.map().crc == s.map_.crc && s.map_.brushes.size() == disk.brushes.size() + 1);
    b.sendCommand("callvote edit off");
    pump(s, cls, 0.2, idle);
    a.sendCommand("vote yes");
    pump(s, cls, 0.5, idle);
    CHECK(!s.editing_ && s.mapEdited_ && s.map_.brushes.size() == disk.brushes.size() + 1 && !(a.mode().flags & MS_EDITING));
    Map after;
    CHECK(after.load(s.config().map) && after.crc == disk.crc);   // the file was never touched
    s.command("map " + s.config().map);
    pump(s, cls, 0.5, idle);
    CHECK(s.map_.crc == disk.crc && a.map().crc == disk.crc && !s.mapEdited_); // back to the original
    // update installed mid-match: announced, but no restart until the match is over
    s.ms_.phase = PHASE_LIVE;
    FILE* f = fopen(flag.c_str(), "w");
    if (f) fclose(f);
    pump(s, cls, 3.0, idle);
    CHECK(!s.restartRequested() && s.restartAnnounced_);
    s.ms_.phase = PHASE_MATCH_END;
    pump(s, cls, 4.0, idle);
    CHECK(s.restartRequested());
    f = fopen(flag.c_str(), "r");
    CHECK(f == nullptr); // consumed
    if (f) fclose(f);
    std::remove(flag.c_str());
    s.stop();
}

// Map edit session: edits from any client reach everyone (and late joiners), the host's file is autosaved,
// and the session switches to playing the map and back.
static void testEditSession(uint16_t port) {
    printf("multiplayer map editing\n");
    Map base;
    CHECK(base.load("bastion"));
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

// Demos: record a match from one client, play it back into another: the map, players, names and events come back.
static void testDemo(uint16_t port) {
    printf("demo record + playback\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "harbor"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "recorder");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 1.0, idle);
    s.addBot(TEAM_A, 2); s.addBot(TEAM_A, 2);
    pump(s, cls, 0.5, idle);
    std::string path = findDataPath("maps") + "/../zz_test.qdemo";
    CHECK(a.startRecording(path) && a.recording());
    std::vector<Vec3> botPos;
    a.sendCommand("say hello demo");
    pump(s, cls, 2.0, [](int) { UserCmd c; c.forward = 127; return c; });
    a.stopRecording();
    s.stop();
    NetClient p;
    std::string err;
    CHECK(p.startPlayback(path, err) && p.playback() && p.mapReady() && p.map().name == "harbor");
    if (!err.empty()) printf("  %s\n", err.c_str());
    std::vector<NetClient*> none;
    int snaps = 0, maxPlayers = 0;
    bool chat = false, moved = false;
    Vec3 first;
    double end = timeNow() + 6;
    while (p.playback() && timeNow() < end) {
        p.poll();
        if (p.hasSnapshot()) {
            snaps++;
            maxPlayers = std::max(maxPlayers, (int)p.latest().players.size());
            if (const PlayerSnap* me = p.findPlayer(p.clientId())) { if (snaps == 1) first = me->pos; else moved |= length(me->pos - first) > 1.0f; }
        }
        for (auto& e : p.takeEvents()) chat |= e.type == EV_CHAT && e.text == "hello demo";
        sleepMs(5);
    }
    printf("  %d polls with snapshots, %d players, %.1f kB\n", snaps, maxPlayers, std::filesystem::file_size(path) / 1024.0);
    CHECK(!p.playback() && p.error() == "demo finished");   // played to the end
    CHECK(snaps > 50 && maxPlayers == 3 && chat && moved && p.playerName(p.clientId()) == "recorder");
    if (!getenv("QUARK_KEEP_DEMO")) std::remove(path.c_str());   // (keep it to watch: build/quark --demo <it>)
}

// Edit by vote on a normal server, then "VOTE: PLAY IT" (callvote edit off): everyone plays the edited copy.
static void testVotedEdit(uint16_t port) {
    printf("voted map editing, then vote to play it\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_VS; cfg.port = port; cfg.autoStart = true;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    std::vector<std::string> msgs;
    auto pumpMsgs = [&](double t) {
        pump(s, cls, t, idle);
        for (auto* c : cls) for (auto& e : c->takeEvents()) if (e.type == EV_MESSAGE || e.type == EV_CHAT) msgs.push_back(e.text);
    };
    pumpMsgs(1.5);
    a.sendCommand("callvote edit on");
    pumpMsgs(0.3);
    b.sendCommand("callvote edit on");   // (calling the same vote again counts as a yes: no F1 needed)
    pumpMsgs(1.0);
    CHECK(s.editing_ && (a.mode().flags & MS_EDITING));
    size_t nb = s.map_.brushes.size();
    a.sendCommand("ed b ins " + std::to_string(nb) + " 1 0 10 0 2 11 2");
    pumpMsgs(0.5);
    CHECK(s.map_.brushes.size() == nb + 1 && a.map().crc == s.map_.crc && b.map().crc == s.map_.crc);
    uint32_t edited = s.map_.crc;
    b.sendCommand("callvote edit off");   // (nobody else votes: "play it" passes when the 30 s are up, as nobody said no)
    pumpMsgs(1.0);
    CHECK(s.editing_);                    // (not yet: one yes of two)
    s.vote_.endsAt = s.time();            // (the 30 s are up)
    pumpMsgs(5.0);
    CHECK(!s.editing_ && !(a.mode().flags & MS_EDITING) && !(b.mode().flags & MS_EDITING));
    CHECK(s.map_.crc == edited && a.map().crc == edited && b.map().crc == edited && a.mapReady() && b.mapReady());
    CHECK(a.hasSnapshot() && b.hasSnapshot() && a.clientId() >= 0 && b.clientId() >= 0);
    bool anyAlive = false;
    for (auto& p : s.players_) anyAlive |= p.connected && p.alive;
    CHECK(anyAlive);
    for (auto& m : msgs) printf("    | %s\n", m.c_str());
    s.stop();
}

// 1v1 rulesets: weapon pools, instagib, no self damage, vampire. Tourney: 5 players (byes), the bracket
// runs to a champion with map and ruleset rotation, non-players spectate.
static void testArenaAndTourney(uint16_t& port) {
    printf("1v1 rulesets + tourney bracket\n");
    {
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true;
        cfg.map = "clockwork"; cfg.mode = MODE_ARENA; cfg.port = port++; cfg.arenaVariant = arenaVariantFromName("classic");
        s.setLog([](const std::string&) {});
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
        cfg.map = "clockwork"; cfg.mode = MODE_TOURNEY; cfg.port = port++;
        cfg.tourneyMaps = {"clockwork", "temple"};
        cfg.tourneyVariants = {arenaVariantFromName("classic"), arenaVariantFromName("hitscan")};
        s.setLog([](const std::string&) {});
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    // limit: two snapshot intervals plus up to 15 ms of measured timing jitter (machine dependent: shared CI
    // runners and cloud VMs measure 4-10 ms on their own; snapshots at 125/s would already be 17+ ms)
    float limit = 2.0f / cls[0]->snapRate() + 0.015f;
    // one scheduling hiccup on a shared machine inflates the jitter estimate for a moment (the released 1.14.0 shows
    // it too): over the limit, keep playing 3 s more and measure again - a real regression stays over it
    if (cls[0]->interpDelay() >= limit) {
        printf("  (interpolation delay %.1f ms: measuring again)\n", cls[0]->interpDelay() * 1000);
        pump(s, cls, 3.0, [](int i) { UserCmd c; c.forward = 127; c.yaw = i * 1.5f + (float)timeNow(); c.buttons = BTN_JUMP; return c; });
    }
    printf("  8 players moving: %.0f bytes per snapshot, %u undecodable deltas, interpolation delay %.1f ms (limit %.1f)\n", avg, misses,
           cls[0]->interpDelay() * 1000, limit * 1000);
    CHECK(cls[0]->interpDelay() < limit);
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
    { std::string e_; if (!v.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    place({-9, 0, -26}, {-9, 0, 26});
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
        cfg.map = "clockwork"; cfg.mode = MODE_FFA; cfg.port = port++;   // (a compact map: bastion is a big team map now)
        cfg.vsBots = true; cfg.botsEnemy = 3; cfg.botSkill = BOT_EXPERT; cfg.timeLimit = 60;
        s.setLog([](const std::string&) {});
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
        CHECK(s.playerCount() == 3);
        NetClient a;
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "human");
        std::vector<NetClient*> cls = {&a};
        int hooks = 0;
        bool was[kMaxPlayers] = {};
        for (int i = 0; i < 400; i++) { // 40 s, counting grapple grabs as we go
            pump(s, cls, 0.1, idle);
            for (int k = 0; k < kMaxPlayers; k++) {
                bool h = s.players_[k].connected && s.players_[k].isBot && (s.players_[k].move.flags & MF_HOOKED);
                hooks += h && !was[k];
                was[k] = h;
            }
        }
        int kills = 0;
        for (auto& p : s.players_) if (p.connected) kills += p.kills;
        printf("  ffa vs 3 expert bots: %d kills, %d grapples in 40 s (phase %d)\n", kills, hooks, s.ms_.phase);
        CHECK(kills >= 2);
        CHECK(hooks >= 1); // bots use the grapple
        s.stop();
    }
    { // pregame (no bots) -> vote start -> vote balance bots -> bots join; then votes
        Server s;
        ServerConfig cfg;
        cfg.map = "bastion"; cfg.mode = MODE_VS; cfg.port = port++;
        s.setLog([](const std::string&) {});
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
        cfg.map = "bastion"; cfg.mode = MODE_NPAYLOAD; cfg.port = port++;
        s.setLog([](const std::string&) {});
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
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
    const char* maps[MODE_COUNT] = {"bastion", "bastion", "foundry", "bastion", "bastion", "bastion", "bastion", "clockwork", "clockwork", "clockwork", "hamlet", "hamlet", "lodestone"};
    static_assert(MODE_COUNT == 13, "add the new mode's map here");
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
        CHECK(mode == MODE_RACE ? s.ms_.phase == PHASE_WARMUP : s.ms_.phase != PHASE_WARMUP); // bots alone start a match (race: practice until someone starts it)
        s.stop();
    }
}

// Server CPU benchmark: a full server (bots fighting + human clients receiving snapshots) on a busy map;
// prints the time per tick (simulate + snapshots) against the 1 ms budget of a 1000 Hz server.
static void serverBench(uint16_t port, int bots, int humans, double seconds, const char* map) {
    g_serverRateCheck = false; // measure at 1000 Hz, don't step down
    Server s;
    ServerConfig cfg;
    cfg.map = map; cfg.mode = MODE_FFA; cfg.port = port; cfg.autoStart = true; cfg.maxPlayers = kMaxPlayers;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { printf("  start failed\n"); return; }
    std::vector<std::unique_ptr<NetClient>> owned;
    std::vector<NetClient*> cls;
    for (int i = 0; i < humans; i++) {
        owned.push_back(std::make_unique<NetClient>());
        owned.back()->connect("127.0.0.1:" + std::to_string(port), "h" + std::to_string(i));
        cls.push_back(owned.back().get());
    }
    for (int i = 0; i < bots; i++) s.addBot(TEAM_A, BOT_ADVANCED);
    pump(s, cls, 2.0, idle);
    std::vector<float> t;
    t.reserve(200000);
    s.tickTimes_ = &t;
    pump(s, cls, seconds, [](int i) { UserCmd c; c.forward = 127; c.yaw = i * 1.3f + (float)timeNow(); c.buttons = BTN_JUMP | BTN_FIRE; return c; });
    s.tickTimes_ = nullptr;
    std::sort(t.begin(), t.end());
    double sum = 0;
    for (float v : t) sum += v;
    auto pct = [&](double p) { return t.empty() ? 0.0 : t[std::min(t.size() - 1, (size_t)(t.size() * p))] * 1e6; };
    printf("  SERVERBENCH %s: %d bots + %d clients, %zu ticks at %d Hz: avg %.1f us  p99 %.1f us  p99.9 %.1f us  worst %.1f us  (%.1f%% of the tick budget)\n",
           map, bots, humans, t.size(), s.tickRate(), t.empty() ? 0 : sum / t.size() * 1e6, pct(0.99), pct(0.999), t.empty() ? 0 : t.back() * 1e6,
           t.empty() ? 0 : sum / t.size() * s.tickRate() * 100);
    qzoneDump();
    s.stop();
}

// Bots play every map in the mode it's built for; prints kills / deaths (deaths beyond kills = falls and
// self-kills) and the objective state, to spot layouts where nothing happens or bots keep falling.
static void playtestMaps(uint16_t port, double seconds) {
    for (auto& name : listMaps()) {
        if (getenv("PT_MAP") && name != getenv("PT_MAP")) continue;
        Map m;
        if (!m.load(name)) continue;
        int mode = m.preferredMode >= 0 ? m.preferredMode : MODE_FFA;
        if (getenv("PT_MODE")) mode = modeFromName(getenv("PT_MODE"));
        Server s;
        ServerConfig cfg;
        cfg.map = name; cfg.mode = mode; cfg.port = port++; cfg.autoStart = true;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { printf("  %s: start failed\n", name.c_str()); continue; }
        bool duo = mode == MODE_ARENA || mode == MODE_DUEL || mode == MODE_TOURNEY;
        int n = duo ? 1 : mode == MODE_RACE ? 3 : 4;
        for (int i = 0; i < n; i++) { s.addBot(TEAM_A, BOT_ADVANCED); if (mode != MODE_RACE) s.addBot(TEAM_B, BOT_ADVANCED); }
        std::vector<NetClient*> none;
        pump(s, none, 1.0, idle);
        s.command("forcestart");
        if (getenv("PT_TRACE")) {   // where do bots die?
            Vec3 last[kMaxPlayers]; bool was[kMaxPlayers] = {};
            double end = timeNow() + seconds;
            while (timeNow() < end) {
                pump(s, none, 0.05, idle);
                for (int i = 0; i < kMaxPlayers; i++) {
                    auto& p = s.players_[i];
                    if (!p.connected) continue;
                    if (was[i] && !p.alive) printf("    died at %.1f %.1f %.1f\n", last[i].x, last[i].y, last[i].z);
                    was[i] = p.alive; if (p.alive) last[i] = p.move.pos;
                }
            }
        } else pump(s, none, seconds, idle);
        int kills = 0, deaths = 0;
        for (auto& p : s.players_) if (p.connected) { kills += p.kills; deaths += p.deaths; }
        if (isItMode(mode)) { int out = 0; for (auto& p : s.players_) out += p.connected && p.out; printf("  round %d phase %d, eliminated %d\n", s.ms_.round, s.ms_.phase, out); }
        printf("  %-10s %-8s kills %3d  deaths %3d  (falls/self %d)  score %d-%d  payload %.0f/%.0f  cp %d\n", name.c_str(), modeName(mode), kills, deaths,
               deaths - kills, s.ms_.scoreA, s.ms_.scoreB, s.ms_.payloadDist, s.ms_.payloadLen, [&] { int b = 0; for (auto& p : s.players_) if (p.connected) b = std::max(b, (int)p.checkpoint); return b; }());
        s.stop();
    }
}

// Maps and rules arrive from untrusted servers: junk must be refused (or clamped), never crash or blow up memory.
static void testHostileInput() {
    printf("hostile input\n");
    const std::string base = "box -10 -1 -10 10 0 10\nspawn any 0 0 0\n";
    { Map m; std::string err; CHECK(!m.parse(base + "box 0 0 0 400000 1 400000\n", &err) && !err.empty()); }
    { Map m; CHECK(!m.parse(base + "box 0 0 0 nan 1 1\n")); }
    { Map m; CHECK(!m.parse(base + "box 0 0 0 inf 1 1\n")); }
    { Map m; CHECK(!m.parse("box -10 -1 -10 10 0 10\nspawn any nan 0 0\n")); }
    { Map m; CHECK(!m.parse(base + "light 0 2 0 1e30 1\n")); }
    { Map m; CHECK(!m.parse(base + "path 0 0 1e9\n")); }
    { Map m; CHECK(!m.parse(base + "sun nan 1 0\n")); }
    { // too many brushes
        std::string t = base;
        for (int i = 0; i < 60001; i++) t += "box 0 0 0 1 1 1\n";
        Map m; std::string err; CHECK(!m.parse(t, &err) && m.brushes.empty());
    }
    { // ladders: capped in count (each would also generate rails and rungs)
        std::string t = base;
        for (int i = 0; i < 300; i++) t += "ladder 0 0 0 1 60 0.5 auto\n";
        Map m; CHECK(!m.parse(t));
    }
    { // too dense for the collision grid: 1000 boxes covering the whole 8 km map
        std::string t = base;
        for (int i = 0; i < 1000; i++) t += "box -4096 0 -4096 4096 1 4096\n";
        Map m; std::string err; CHECK(!m.parse(t, &err));
    }
    { Map m; CHECK(m.parse(base) && m.brushes.size() == 1); } // a sane map still parses after all that
    // map names: a stem, never a path
    CHECK(isValidMapName("temple") && isValidMapName("my-map_2"));
    CHECK(!isValidMapName("") && !isValidMapName("../x") && !isValidMapName("a/b") && !isValidMapName("a\\b") && !isValidMapName("c:x")
          && !isValidMapName("a.b") && !isValidMapName(std::string(41, 'a')) && !isValidMapName(std::string("a\0b", 3)));
    CHECK(safeMapName("../../etc/x") == "______etc_x" && safeMapName("") == "map" && safeMapName("temple") == "temple");
    CHECK(downloadedMapPath("../../x", 1).find("..") == std::string::npos);
    // custom rules: NaN / inf keep the default, out of range is clamped
    CustomRules def, r;
    r.fromText("custommode=x\ngravity=nan\nspeed=inf\njump=-inf\nknockback=99\nweapon=0,nan,nan,nan\n");
    CHECK(r.gravity == def.gravity && r.moveSpeed == def.moveSpeed && r.jump == def.jump && r.knockback == 5);
    CHECK(r.fireRate[0] == def.fireRate[0] && r.damage[0] == def.damage[0] && r.projSpeed[0] == def.projSpeed[0]);
}

int main(int argc, char** argv) {
    setExecutableDir(argv[0]);
    netInit();
    setTimeScale(kTestTimeScale); // bot subprocesses share the test clock speed
    if (argc >= 4 && std::string(argv[1]) == "bot") return runBot(argv[2], atof(argv[3]), argc > 4 ? argv[4] : "bot");
    srand(1234);
    setvbuf(stdout, nullptr, _IOLBF, 0); // (CI: see how far it got)
    std::thread([] { // a hung test fails the run instead of stalling CI for an hour
        std::this_thread::sleep_for(std::chrono::minutes(25));
        printf("TIMEOUT: the test above never finished\n"); fflush(stdout); std::_Exit(2);
    }).detach();
    setTimeScale(kTestTimeScale);
    g_serverRateCheck = false;
    if (!getenv("QUARK_MASTER")) { // ranked / time trial servers register with the list: not from tests ("off" = no list)
#ifdef _WIN32
        _putenv("QUARK_MASTER=off");
#else
        setenv("QUARK_MASTER", "off", 1);
#endif
    }
    if (argc >= 2 && std::string(argv[1]) == "playtest") { playtestMaps(29400, argc > 2 ? atof(argv[2]) : 40); return 0; }
    if (argc >= 2 && std::string(argv[1]) == "serverbench") {
        serverBench(29300, 12, 4, argc > 2 ? atof(argv[2]) : 15, "harbor");
        serverBench(29301, 24, 8, argc > 2 ? atof(argv[2]) : 15, "hamlet");
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "racefloors") { // the RACE_FLOORS table for tools/master/worker.js
        Map m;
        if (!m.load("viaduct")) return 1;
        std::vector<int> f = raceSplitFloorsMs(m);
        printf("viaduct: [");
        for (size_t i = 0; i < f.size(); i++) printf("%s%d", i ? ", " : "", f[i]);
        printf("]\n");
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "hostile") { testHostileInput(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "ladders") { testLadders(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "stairs") { testStairs(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "points") { setTimeScale(kTestTimeScale); testPoints(29995); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "syringe") { setTimeScale(kTestTimeScale); testSyringe(29996); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "edladder") { testEditorLadder(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "edgroups") { testEditorGroups(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "cartchecks") { setTimeScale(kTestTimeScale); testCartChecks(29997); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "allmaps") { testAllMaps(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "custom") { testCustomMode(29990); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "rejoin") { testRejoin(29980); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "it") { testItModes(29450); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "aim") { uint16_t p = 29500; testBotAim(p); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "ranked") { testRanked(29610); testRankedVoteAndRestart(29620); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "edit") { testMapDownload(29600); testEditSession(29601); testVotedEdit(29602); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "mapreport") return mapReport(argc > 2 ? argv[2] : "docs/MAP_REPORT.md") ? 1 : 0;
    if (argc >= 2 && std::string(argv[1]) == "demo") { testDemo(29603); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "trial") { testTimeTrial(29960); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "rate") { testTickRateFallback(29950); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "net") { testNetcode(29900); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "wallbang") { testWallbang(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "tourney") { uint16_t p = 29800; testArenaAndTourney(p); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "weapons") { uint16_t p = 29700; testWeapons(p); testRocketSelfDamage(p + 20); testAltFires(p + 30); return g_fail ? 1 : 0; }
    // ---- the whole suite. Most tests wait on real-time network play rather than the CPU, so each runs in its own
    // process, several at once (output kept in order); a few timing-sensitive ones run alone afterwards.
    // "quark_tests serial" runs everything in this process, one after another, as before.
    struct T { const char* name; std::function<void()> run; };
    std::vector<T> par = {   // (longest first: they set the wall time)
        {"bots", [] { uint16_t p = 28900; testBots(p); }},
        {"botaim", [] { uint16_t p = 29000; testBotAim(p); }},
        {"tourney", [] { uint16_t p = 29100; testArenaAndTourney(p); }},
        {"duel", [] { testDuel(28520); testDuel2v2(28521); }},
        {"weapons", [] { testWeapons(28530); testRocketSelfDamage(28540); testAltFires(28545); testRocketLob(28550); testTimeToKill(); testWallbang(); }},
        {"maps", [] {
            testMaps(); testAllMaps(); testStairs(); testLadders();
            printf("map scores\n");   // the map bar: every map scores at least 7 in every category but originality
            std::string p = (std::filesystem::temp_directory_path() / ("quark_map_report_" + std::to_string(getpid()) + ".md")).string();
            if (mapReport(p.c_str())) g_fail++;
        }},
        {"movement", [] { testHostileInput(); testMovement(); testTickRateIndependence(); testGridQueries(); testEditorLadder(); testEditorGroups(); }},
        {"sessions", [] { testSession(MODE_VS, "bastion", 28560); testSession(MODE_BOMB, "bastion", 28561); testSession(MODE_KOTH, "bastion", 28562); }},
        {"sessions2", [] { testSession(MODE_PAYLOAD, "bastion", 28563); testSession(MODE_FFA, "bastion", 28564); testSession(MODE_RACE, "foundry", 28565); }},
        {"modes", [] { testCustomMode(28570); testPoints(28571); testSyringe(28572); testCartChecks(28573); testItModes(28574); }},
        {"race", [] { testRaceFloors(); testTimeTrial(28580); testPayloadPush(28581); testPacketLoss(28582); testCartDash(28583); testSemiAuto(28584); }},
        {"votes", [] { testRejoin(28590); testMapVote(28591); testHandshake(28592); }},
        {"edit", [] { testMapDownload(28600); testEditSession(28601); testVotedEdit(28602); testDemo(28603); }},
        {"ranked", [] { testRanked(28610); testRankedVoteAndRestart(28620); testInternetList(28625); }},
    };
    std::vector<T> alone = {   // timing-sensitive (or rewrites shared files): one at a time, on an otherwise idle machine
        {"netcode", [] { testNetcode(28700); }},
        {"tickrate", [] { testTickRateFallback(28710); }},
        {"resetmaps", [] { testResetMaps(); }},
    };
    bool serial = argc >= 2 && std::string(argv[1]) == "serial";
#ifndef _WIN32
    if (!serial) {
        unsigned jobs = std::max(2u, std::thread::hardware_concurrency());
        struct Job { pid_t pid; std::string out; size_t idx; };
        std::vector<Job> running;
        std::vector<std::string> outputs(par.size());
        std::vector<int> done(par.size(), -1);
        size_t next = 0, printed = 0;
        auto tmp = std::filesystem::temp_directory_path();
        auto start = [&](size_t i) {
            std::string out = (tmp / ("quark_test_" + std::to_string(getpid()) + "_" + par[i].name + ".log")).string();
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {   // the child: one test group, its output to a file
                if (!freopen(out.c_str(), "w", stdout)) std::_Exit(99);
                dup2(fileno(stdout), 2);
                setvbuf(stdout, nullptr, _IOLBF, 0);
                srand(1234);
                par[i].run();
                fflush(stdout);
                std::_Exit(std::min(g_fail, 100));
            }
            running.push_back({pid, out, i});
        };
        while (next < par.size() && running.size() < jobs) start(next++);
        while (!running.empty()) {
            int st = 0;
            pid_t pid = wait(&st);
            if (pid <= 0) break;
            for (size_t k = 0; k < running.size(); k++) {
                if (running[k].pid != pid) continue;
                size_t i = running[k].idx;
                std::ifstream f(running[k].out);
                std::stringstream ss; ss << f.rdbuf();
                outputs[i] = ss.str();
                std::remove(running[k].out.c_str());
                done[i] = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
                if (!WIFEXITED(st)) outputs[i] += std::string("  FAIL ") + par[i].name + ": crashed\n";
                running.erase(running.begin() + k);
                break;
            }
            while (printed < par.size() && done[printed] >= 0) { fputs(outputs[printed].c_str(), stdout); g_fail += done[printed]; printed++; }
            if (next < par.size()) start(next++);
        }
        for (auto& t : alone) t.run();
    } else
#endif
    {
        for (auto& t : par) t.run();
        for (auto& t : alone) t.run();
    }
    netShutdown();
    printf(g_fail ? "%d FAILURE(S)\n" : "all tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
