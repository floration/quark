#include <atomic>
#include <fstream>
#include <map>
#include <regex>
#include <cstring>
#include <algorithm>
#include <queue>
#include <thread>
#include "../src/common/rng.h"
#include <functional>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <chrono>
#include <random>
#include <cstdlib>
#include "common/profile.h"
#include "../src/client/complete.h"
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
#include "common/sign.h"
#include "game/bots.h"
#include "common/proc.h"
#ifndef _WIN32
#include <fcntl.h>
#endif

// The source tree (tools/, tests/data/): where this was built, or QUARK_SOURCE_DIR in the environment when the binary
// runs on another machine (CI cross-builds the Windows tests on Linux and runs them on a Windows runner).
static std::string sourceDir() {
    const char* e = getenv("QUARK_SOURCE_DIR");
    if (e && *e) return e;
#ifdef QUARK_SOURCE_DIR
    return QUARK_SOURCE_DIR;
#else
    return "";
#endif
}
static std::string testDataDir() {
    const char* e = getenv("QUARK_SOURCE_DIR");
    return e && *e ? std::string(e) + "/tests/data" : std::string(QUARK_TEST_DATA);
}

static int g_fail = 0;
int launcherTests(); // tests/launcher_tests.cpp
// Game time runs this many times faster than real time in the network tests. Windows' socket and timer path can't
// keep 4x pace (checked under Wine: dozens of phase / weapon-switch checks miss at 4x, none at 1x), so it runs them
// in real time. QUARK_TEST_TIMESCALE overrides it (bot subprocesses inherit it, so they keep the same clock).
static double testTimeScale() {
    const char* e = getenv("QUARK_TEST_TIMESCALE");
    double v = e ? atof(e) : 0;
#ifdef _WIN32
    return v > 0 ? v : 1.0;
#else
    return v > 0 ? v : 4.0;
#endif
}
static const double kTestTimeScale = testTimeScale();
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

// Console tab completion: unique -> filled in + space; several -> common prefix and every option listed.
static void testCompletion() {
    printf("console completion\n");
    auto cands = [](const std::vector<std::string>& w) -> std::vector<std::string> {
        if (w.empty()) return {"fov", "fullbright", "sensitivity", "/callvote", "/team"};
        if (w[0] == "/team" && w.size() == 1) return {"a", "b", "spec"};
        if (w[0] == "/callvote" && w.size() == 1) return {"map", "mode", "start"};
        if (w[0] == "/callvote" && w.size() == 2 && w[1] == "map") return {"temple", "furnace", "foundry"};
        return {};
    };
    Completion c = completeLine("sen", cands);
    CHECK(c.line == "sensitivity " && c.options.empty());
    c = completeLine("f", cands);
    CHECK(c.line == "f" && c.options.size() == 2);          // fov, fullbright: nothing more in common, both listed
    c = completeLine("fu", cands);
    CHECK(c.line == "fullbright ");
    c = completeLine("/callvote m", cands);
    CHECK(c.line == "/callvote m" && c.options.size() == 2); // map, mode
    c = completeLine("/callvote map f", cands);
    CHECK(c.line == "/callvote map f" && c.options.size() == 2);
    c = completeLine("/callvote map fo", cands);
    CHECK(c.line == "/callvote map foundry ");
    c = completeLine("/callvote map ", cands);
    CHECK(c.line == "/callvote map " && c.options.size() == 3); // nothing typed yet: every option
    c = completeLine("/team S", cands);
    CHECK(c.line == "/team spec ");
    c = completeLine("xyz", cands);
    CHECK(c.line == "xyz" && c.options.empty());
    c = completeLine("/callvote fo", cands);
    CHECK(c.line == "/callvote fo" && c.options.empty());
    auto pre = [](const std::vector<std::string>&) -> std::vector<std::string> { return {"volume", "volume_music"}; };
    c = completeLine("vo", pre);
    CHECK(c.line == "volume" && c.options.size() == 2);
}

// sleepMs(1) is the frame cap's and pump()'s wait: it must sleep about 1 ms, not a 15 ms scheduler quantum
// (MinGW's sleep_for did, REVIEW PERF-14).
// procRun (curl for every HTTP request, xdg-open...): its pipes close on exec, so a child started meanwhile (on another
// thread) holds none of them; concurrent children all finish (FINAL_REVIEW 2.7: two POSTs could wait on each other's
// stdin EOF); SIGPIPE stays ignored for good instead of being swapped per call.
static void testProcPipes() {
#ifndef _WIN32
    printf("procRun: close-on-exec pipes, concurrent children\n");
    auto openFds = [] { std::vector<int> v; for (int fd = 0; fd < 1024; fd++) if (fcntl(fd, F_GETFD) != -1) v.push_back(fd); return v; };
    std::vector<int> before = openFds();
    std::string outA, leaked;
    size_t inFlight = 0;
    bool checked = false;
    int rc = procRun({"sh", "-c", "echo hello; sleep 0.1"}, "", [&](const char* d, size_t n) {
        outA.append(d, n);
        if (checked) return true;
        checked = true;   // the first child is running and its pipe is open here: start a second one, which mustn't have it
        std::string script = "for n in";
        for (int fd : openFds())
            if (std::find(before.begin(), before.end(), fd) == before.end()) { script += " " + std::to_string(fd); inFlight++; }
        script += "; do [ -e /dev/fd/$n ] && echo $n; done; true";
        procRun({"sh", "-c", script}, "", [&](const char* d2, size_t n2) { leaked.append(d2, n2); return true; });
        return true;
    });
    printf("  %zu pipe end(s) of a running child open, inherited by the next child: %s\n", inFlight, leaked.empty() ? "none" : leaked.c_str());
    CHECK(rc == 0 && outA == "hello\n" && inFlight >= 1 && leaked.empty());
    // several at once, each feeding its stdin and reading it back: every one finishes
    std::atomic<int> ok{0}, finished{0};
    const int kThreads = 6, kRounds = 15;
    std::string input(16000, 'x');
    for (size_t i = 0; i < input.size(); i++) input[i] = (char)('a' + i % 26);
    for (int t = 0; t < kThreads; t++)
        std::thread([&ok, &finished, input] {
            for (int r = 0; r < kRounds; r++) {
                std::string got;
                int c = procRun({"sh", "-c", "cat"}, input, [&](const char* d, size_t n) { got.append(d, n); return true; });
                if (c == 0 && got == input) ok++;
            }
            finished++;
        }).detach();
    double end = timeNow() + 60 * std::min(4.0, kTestTimeScale);
    while (finished < kThreads && timeNow() < end) sleepMs(5);
    printf("  %d threads x %d concurrent children: %d of %d finished and echoed their input\n", kThreads, kRounds, ok.load(), kThreads * kRounds);
    CHECK(finished == kThreads && ok == kThreads * kRounds);
    // SIGPIPE: still ignored after the calls (it used to be swapped back and forth around every write)
    struct sigaction sa {};
    sigaction(SIGPIPE, nullptr, &sa);
    CHECK(sa.sa_handler == SIG_IGN);
    // a child whose reader is gone: the write fails instead of killing us, and a missing program is -1
    CHECK(procRun({"sh", "-c", "exit 3"}, std::string(200000, 'y'), [](const char*, size_t) { return true; }) == 3);
    CHECK(procRun({"quark-no-such-program-xyz"}, "", [](const char*, size_t) { return true; }) == -1);
#endif
}

static void testSleepPrecision() {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; i++) sleepMs(1);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("  sleepMs(1): %.2f ms average\n", ms / 20);
    CHECK(ms < 20 * 5.0);
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

// The maps a test grades: listMaps() without the zz_* test maps other tests write (the forked groups each have their
// own data dir, so none should be there; this keeps a stray one from failing the map checks if one is).
static std::vector<std::string> testedMaps() {
    std::vector<std::string> v = listMaps();
    v.erase(std::remove_if(v.begin(), v.end(), [](const std::string& n) { return n.rfind("zz_", 0) == 0; }), v.end());
    return v;
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
    for (auto& name : testedMaps()) {
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

// One set of limits for every map, however it arrives (REVIEW CLI-3, NET-3, NET-15): files, downloads and demos are
// refused with an error instead of a 240 GB grid, a non-solid floor or brushes past 65535 dropping out of collision;
// edits that would break those limits, or leave a map that won't load again, are refused and change nothing.
static void testMapLimits() {
    printf("map limits: hostile maps and edits are refused\n");
    for (const char* f : {"map_hugegrid.qmap", "map_floatmax.qmap"}) {   // the review's repro files
        std::ifstream in(testDataDir() + "/" + f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        Map m;
        std::string err;
        CHECK(!ss.str().empty() && !m.parse(ss.str(), &err) && !err.empty());
    }
    auto refused = [](const std::string& text) { Map m; std::string err; return !m.parse(text, &err) && !err.empty(); };
    const std::string floor = "box -20 -1 -20 20 0 20\nspawn any 0 0 0\n";
    {   // 65537 boxes (the review's map_65k: indices past 65535 wrapped and those boxes lost collision)
        std::string t;
        for (int i = 0; i < 65537; i++) t += "box -1 -1 -1 1 0 1\n";
        CHECK(refused(t + "spawn any 0 0 0\n"));
    }
    {   // generated boxes count too: 60 custom groups of 1024 parts
        std::string t = floor;
        for (int g = 0; g < 60; g++) { t += "group custom 0 0 0 0 0 1 1 1\n"; for (int k = 0; k < 1024; k++) t += "part 0 0 0 0 1 1 1\n"; }
        CHECK(refused(t));
    }
    CHECK(refused(floor + "ladder 0 0 0 1 1e30 1 auto\n"));                 // its rungs would never end
    CHECK(refused(floor + "box 0 0 0 5000 1 1\n"));
    CHECK(refused(floor + "group custom 0 0 0 0 0 4000 1 1\npart 0 0 0 0 2 1 1\n"));   // in range until the part is placed
    CHECK(refused(floor + "light 0 1 0 1e9 1\n") && refused(floor + "spawn a 1e9 0 0\n"));
    CHECK(!refused(floor + "box 4000 0 -4096 4096 1 4096\n"));               // the edges of the range are fine
    {   // a light of intensity 0 is refused the same way by the parser, an edit and validate()
        Map m;
        CHECK(refused(floor + "light 0 1 0 5 0\n") && m.parse(floor + "light 0 1 0 5 1\n"));
        CHECK(!applyMapEdit(m, "l set 0 0 1 0 5 0") && applyMapEdit(m, "l set 0 0 1 0 5 0.1"));
        m.lights[0].intensity = 0;
        CHECK(!m.validate());
    }
    // edits, from the fuzzer's base map: the review's sequences each end in a map that loads again
    const std::string base = "title t\nbox -20 -1 -20 20 0 20\nbox 2 0 2 4 2 4\nladder 5 0 5 6 4 6\ngroup stairs 0 1 -8 0 -8 -4 2 -6\n"
                             "group custom 1 0 8 0 8 10 2 10\npart 2 0 0 0 1 0.5 1\nspawn a 0 0 0\nspawn b 1 0 1\nlight 0 5 0 10 1\n";
    for (const char* seq : {"g set 1 1 0 0 8 0 8 10 2 10",                       // custom -> room kept its parts: unloadable
                            "b del 0\nb del 0\nb del 0",                          // deleting every real box
                            "d set 0 1 5 0 5 6 4 6\nb del 0\nb del 0",
                            "b del 0\nb del 0\ng del 0\ng del 0"}) {
        Map m;
        CHECK(m.parse(base));
        std::istringstream ops(seq);
        std::string op, err;
        while (std::getline(ops, op)) applyMapEdit(m, op);
        Map again;
        CHECK(again.parse(m.serialize(), &err) && m.editableBrushes() >= 1);
        if (!err.empty()) printf("  \"%s\": %s\n", seq, err.c_str());
    }
    {   // the group bypass: parts are fractions of the group's box, so in-range numbers made boxes ~33,000 km out
        Map m;
        CHECK(m.parse(base));
        size_t n = m.brushes.size();
        uint32_t crc = crc32(m.serialize().data(), m.serialize().size());
        CHECK(applyMapEdit(m, "g set 1 0 0 0 -4096 -1 -4096 4096 1 4096"));   // a big custom group: fine
        CHECK(!applyMapEdit(m, "g part 1 0 -4096 0 -4096 4096 1 4096"));       // a part 4096 times its size: refused
        CHECK(!applyMapEdit(m, "b set 2 0 0 0 0 1 1 1"));                      // generated boxes aren't edited directly
        CHECK(applyMapEdit(m, "g set 1 0 1 0 8 0 8 10 2 10") && m.brushes.size() == n);   // (back as it was)
        std::string again = m.serialize();
        CHECK(crc32(again.data(), again.size()) == crc);                      // refused edits changed nothing
    }
}

// Custom rules come from servers (EV_RULES) and files: NaN / inf must not get through the clamps, and a name has to
// survive its own round trip (REVIEW CLI-6, the review's rules_nan*.txt and rules_spaces_name.txt).
static void testRulesValues() {
    printf("custom rules: finite values, names that round-trip\n");
    const CustomRules def;
    CustomRules r;
    CHECK(r.fromText("custommode=x\ngravity=nan\nspeed=inf\njump=-inf\nmove_air=nan\n"));
    // (UCRT reads "nan" as NaN and gets the default; msvcrt / some libcs read 0 and get the low bound: finite either way)
    auto in = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    CHECK(in(r.gravity, 0.1f, 3) && in(r.moveSpeed, 0.25f, 3) && in(r.jump, 0.25f, 3));
    for (float v : r.move) CHECK(in(v, 0.1f, 5));
    CHECK(r.fromText("custommode=x\nweapon=1,nan,nan,nan\n"));
    CHECK(in(r.fireRate[1], 0.1f, 10) && in(r.damage[1], 0, 10) && in(r.projSpeed[1], 0.1f, 5));
    (void)def;
    CHECK(r.fromText("custommode=x\ngravity=99\nweapon=2,0,99,0\n") && r.gravity == 3 && r.damage[2] == 10);   // in range still clamps
    // a map name with nothing safe in it is dropped, never kept as "" (the host would start on map ""; fuzz_rules)
    CHECK(r.fromText("custommode=x\nmaps= ,temple,   ,anvil\n") && r.maps == std::vector<std::string>({"temple", "anvil"}));
    CHECK(!r.fromText("custommode=" + std::string(40, ' ') + "x\n"));   // cut to 40 first: all spaces, so no mode
    CustomRules a, b;
    CHECK(a.fromText("custommode=  my mode  \ngravity=2\nweapon=3,2,0.5,1\nmaps=temple,../x\n"));
    CHECK(b.fromText(a.toText()) && b.toText() == a.toText());
    CHECK(clampf(NAN, 1, 2) == 1 && clampf(5, 1, 2) == 2 && clampf(1.5f, 1, 2) == 1.5f);
    CHECK(plainFileName("my hud.cfg") && plainFileName("mode-2.cfg"));
    for (const char* bad : {"", "../x.cfg", "a/b.cfg", "a\\b.cfg", "C:x.cfg", ".hidden", "tab\there"}) CHECK(!plainFileName(bad));
}

// Clip (material 9) blocks players only: light, sight and shots go through it (REVIEW MAP-1: raycastAny, used for
// the baked sun and for visibility, stopped at clip, so the outer clip walls shadowed most floors).
static void testClipRays() {
    printf("clip: blocks players, not light or sight\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -20 -1 -20 20 0 20\nclip 4 0 -20 5 10 20\nbox -8 0 -20 -7 10 20\nspawn any 0 0 0\n", &err));
    Vec3 o(0, 1, 0), east(1, 0, 0), west(-1, 0, 0);
    CHECK(!m.raycastAny(o, east, 10));                 // through the clip wall
    CHECK(m.raycastAny(o, east, 10, true));            // a movement probe still meets it
    CHECK(m.raycastAny(o, west, 10));                  // a real wall still blocks
    CHECK(m.raycast(o, east, 10) < 0);                 // (same as shots)
    CHECK(m.boxSolid(playerBox({4.5f, 0.01f, 0}, false)) && !m.boxSolid(playerBox({0, 0.01f, 0}, false)));   // players collide
    // a shipped map ringed by clip (Dojo: 85% of its floor was in clip shadow): sun rays as if the clip weren't there
    Map d, open;
    CHECK(d.load("dojo"));
    open = d;
    open.brushes.erase(std::remove_if(open.brushes.begin(), open.brushes.end(), [](const Brush& b) { return b.material == 9; }), open.brushes.end());
    open.rebuild();
    int differ = 0, clipMatters = 0;
    for (float x = d.bounds.mn.x; x <= d.bounds.mx.x; x += 2)
        for (float z = d.bounds.mn.z; z <= d.bounds.mx.z; z += 2) {
            Vec3 p(x, 1.0f, z);
            bool lit = !d.raycastAny(p, d.sunDir, 500);
            differ += lit != !open.raycastAny(p, d.sunDir, 500);
            clipMatters += lit != !d.raycastAny(p, d.sunDir, 500, true);
        }
    printf("  dojo: %d sun samples the clip would have shadowed, %d differ from a clip-free map\n", clipMatters, differ);
    CHECK(differ == 0 && clipMatters > 0);
}

// Settings, profiles, the ranked key, custom modes and maps are written whole or not at all (REVIEW COR-2: they were
// truncated, then written, so a crash or a full disk lost every bind, or the ranked identity).
static void testAtomicWrite() {
    printf("files are written whole or not at all\n");
    namespace fs = std::filesystem;
    std::string path = (fs::temp_directory_path() / ("quark_atomic_" + std::to_string(rand()) + ".cfg")).string();
    auto read = [&] { std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); };
    CHECK(writeFileAtomic(path, "first\n") && read() == "first\n");
    std::string big(100000, 'x');
    CHECK(writeFileAtomic(path, big) && read() == big);               // replaces the old file (Windows too)
    std::error_code ec;
    CHECK(!fs::exists(path + ".tmp", ec));
    CHECK(writeFileAtomic(path, "key\n", true) && read() == "key\n");
#ifndef _WIN32
    auto p = fs::status(path, ec).permissions();
    CHECK((p & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);   // the ranked key: 0600
#endif
    CHECK(!writeFileAtomic((fs::temp_directory_path() / "no_such_dir_quark" / "x.cfg").string(), "x"));   // a failed write says so
    fs::remove(path, ec);
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
    qsrand(7);
    auto rnd = [](float a, float b) { return a + (b - a) * ((qrand() >> 8) / (float)(1u << 24)); };
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
    CHECK(s.humanCount() == 2 && s.away(a.clientId()));   // the reconnect grace keeps the slot...
    pump(s, cls, kReconnectGrace + 0.5, idle);
    CHECK(s.humanCount() == 1); // ...then it's gone (a balance bot may take the empty slot)
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
    RankKey rk = rankKeyFromSecret(sha256Hex("time trial racer"));
    a.setRankKey(rk);
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
    // the race ticket goes out only with the racer's signature of its id (the Worker refuses it otherwise)
    for (int i = 0; i < 30 && !s.raceTicketSent_; i++) pump(s, cls, 0.1, idle);
    CHECK(s.players_[a.clientId()].rankId == rk.id && s.raceTicketFor_ == rk.id && s.raceTicketSent_);   // (the post then fails at once in tests, QUARK_MASTER=off, and frame() drops the ticket)
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
    s.phaseEnd_ = s.time() - 20;                      // B taken 20 s into overtime: the full bonus, from now
    s.ms_.capture = 0.98f;
    for (int k = 0; k < 10 && s.ms_.point == 1; k++) { standOn(att, 1); away(def); pump(s, cls, 0.1, idle); }
    CHECK(s.ms_.point == 2 && s.ms_.phase == PHASE_LIVE && s.phaseEnd_ > s.time() + 85);
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
    if (s.map_.cartChecks.size() >= 2) {   // a checkpoint 20 s into overtime: the full bonus, from now
        s.ms_.payloadDist = s.map_.cartChecks[1] * s.ms_.payloadLen - 0.5f;
        s.phaseEnd_ = s.time() - 20;
        for (int k = 0; k < 20 && s.ms_.point == 1 && s.ms_.phase == PHASE_LIVE; k++) {
            s.players_[att].move.pos = s.map_.payloadPoint(s.ms_.payloadDist) + Vec3(0, 0, 2.5f); pump(s, cls, 0.1, idle);
        }
        CHECK(s.ms_.point == 2 && s.ms_.phase == PHASE_LIVE && s.phaseEnd_ > s.time() + 55);
    }
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
    for (int wi : std::initializer_list<int>{WPN_SNIPER, -WPN_SNIPER - 1, -WPN_CROSSBOW - 1, -WPN_RIFLE - 1, WPN_LASER, 100, WPN_ROCKET, WPN_SHOTGUN, WPN_CROSSBOW, WPN_STICKY}) {
        bool head = wi < 0; // negative entries: the same weapon aimed at the head
        bool uptime = wi == 100; // the laser on a server that has been up for days (a float clock lost the 1 ms refire)
        int w = uptime ? WPN_LASER : head ? -wi - 1 : wi;
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true;
        cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port++;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; continue; }
        if (uptime) s.timeMs_ += 5u * 86400u * 1000u;
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
        if (w == WPN_SNIPER) { // one scoped shot: a flat 50, body or head
            printf("  sniper %s: %.0f damage\n", head ? "head" : "body", dealt);
            CHECK(std::fabs(dealt - (head ? kSniperHeadDamage : weaponDef(WPN_SNIPER).damage)) < 0.5f);
        }
        if (w == WPN_LASER) {                                     // ~5 dmg every 0.05 s
            float expect = (float)(elapsed / weaponDef(WPN_LASER).refire) * weaponDef(WPN_LASER).damage;
            printf("  laser%s: %.1f damage in %.2f s (expected ~%.1f)\n", uptime ? " (5 days up)" : "", dealt, elapsed, expect);
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

// Weapon spread is a fixed pattern (NET-9: it was seeded by the client's command number, and the best numbers landed
// about twice the pellets). The same blast whatever the command number; what the client's tracers show (spreadAim) is
// exactly what the server hits; single-shot weapons walk a fixed sequence that restarts when fire is let go.
static void testFixedSpread(uint16_t port) {
    printf("fixed spread pattern (NET-9)\n");
    const WeaponDef& sg = weaponDef(WPN_SHOTGUN);
    {   // the layout: one pellet dead center, all inside the spread, evenly apart, the old random spread's density
        std::vector<Vec3> pts;
        float sum = 0, maxR = 0;
        for (int k = 0; k < sg.pellets; k++) {
            float a, r;
            pelletOffset(k, sg.pellets, sg.spread, a, r);
            sum += r; maxR = std::max(maxR, r);
            pts.push_back({r * std::cos(a), r * std::sin(a), 0});
        }
        float minGap = 1e9f;
        for (size_t i = 0; i < pts.size(); i++)
            for (size_t j = i + 1; j < pts.size(); j++) minGap = std::min(minGap, length(pts[i] - pts[j]));
        float mean = sum / sg.pellets / sg.spread;
        printf("  %d pellets: mean radius %.2f of the spread (random: 0.67), widest %.2f, closest pair %.2f\n", sg.pellets, mean, maxR / sg.spread, minGap / sg.spread);
        CHECK(length(pts[0]) == 0 && maxR <= sg.spread && std::fabs(mean - 2.0f / 3.0f) < 0.05f && minGap > sg.spread * 0.2f);
        float a0, r0, a1, r1;
        burstOffset(0, 0.04f, a0, r0);
        CHECK(r0 == 0);   // a burst's first shot goes where you aim
        bool distinct = true;
        for (uint32_t i = 1; i < kBurstPattern; i++) {
            burstOffset(i, 0.04f, a1, r1);
            if (r1 <= 0 || r1 > 0.04f) distinct = false;
            for (uint32_t j = 1; j < i; j++) { burstOffset(j, 0.04f, a0, r0); if (std::fabs(r0 - r1) < 1e-6f && std::fabs(a0 - a1) < 1e-6f) distinct = false; }
        }
        CHECK(distinct);
    }
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
    pump(s, cls, 2.5, idle);
    auto& sa = s.players_[a.clientId()];
    auto& sb = s.players_[b.clientId()];
    sa.move.pos = {10, 0, 19}; sb.move.pos = {0, 0, 19};   // 10 m apart in the open courtyard (testWeapons)
    sa.move.vel = sb.move.vel = {};
    pump(s, cls, 0.4, [](int) { UserCmd c; c.weapon = WPN_SHOTGUN; return c; });   // (history: they stand there)
    sa.weapon = WPN_SHOTGUN;
    Vec3 eye = sa.move.pos + Vec3(0, kEyeHeight, 0);
    // aimed low (no pellet reaches the head) and further and further to the side: fewer pellets land
    bool same = true, matches = true;
    for (float side : {0.0f, 0.4f, 0.8f}) {
        Vec3 d = normalize(sb.move.pos + Vec3(0, 0.35f, side) - eye);
        UserCmd c;
        c.pitch = std::asin(d.y); c.yaw = std::atan2(-d.x, -d.z);
        c.weapon = WPN_SHOTGUN; c.buttons = BTN_FIRE; c.viewTick = s.timeMs_;
        Vec3 aim = angleForward(c.yaw, c.pitch);
        int predicted = 0;   // the client's view: its tracer directions against the target's hitbox
        for (int k = 0; k < sg.pellets; k++) {
            Vec3 pd = spreadAim(aim, sg.spread, k, sg.pellets, 0);
            float bang, wall = s.map_.raycastBullet(eye, pd, sg.range, &bang);
            float t = rayAABB(eye, safeInv(pd), s.hitBox(sb.move.pos, false), wall >= 0 ? wall : sg.range);
            if (t >= 0) { predicted++; CHECK((eye + pd * t).y < sb.move.pos.y + 1.4f); }
        }
        float first = -1;
        for (uint32_t seq : {1u, 2u, 3u, 77u, 1000u, 123456u, 0xFFFFFFF0u}) {
            c.seq = seq;
            sb.health = 100; sb.move.vel = {};
            s.fireWeaponForTest(a.clientId(), c);
            float dealt = 100 - sb.health;
            if (first < 0) first = dealt;
            if (dealt != first) same = false;
        }
        printf("  aimed %.1f m to the side: %.1f damage for every command number, %d pellets on target in the client's tracers\n", side, first, predicted);
        if (std::fabs(first - predicted * sg.damage) > 0.01f) matches = false;
    }
    CHECK(same);
    CHECK(matches);
    // the bubble (one projectile with spread): a held burst counts its shots; letting go starts over at the center
    uint32_t most = 0;
    pump(s, cls, 0.3, [](int) { UserCmd c; c.weapon = WPN_BUBBLE; return c; });   // (switched with nothing held)
    pump(s, cls, 1.2, [&](int i) { UserCmd c; c.weapon = WPN_BUBBLE; if (i == 0) c.buttons = BTN_FIRE; most = std::max(most, sa.burstShot); return c; });
    pump(s, cls, 0.4, [](int) { UserCmd c; c.weapon = WPN_BUBBLE; return c; });
    double last = sa.lastFire;
    pump(s, cls, 0.1, [](int i) { UserCmd c; c.weapon = WPN_BUBBLE; if (i == 0) c.buttons = BTN_FIRE; return c; });
    printf("  bubble burst: %u shots counted, after letting go: shot %u\n", most + 1, sa.burstShot);
    CHECK(most >= 3 && sa.lastFire != last && sa.burstShot == 0);
    s.stop();
}

// The rifle fires while held.
static void testSemiAuto(uint16_t port) {
    printf("rifle auto fire\n");
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
    double last = p.lastFire;
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

// Syringe: each use heals you 10 hp, with a 1 s cooldown (owner 1.17); a thrown one heals the teammate it hits by 30
// and resets their cooldowns; it does nothing to an enemy.
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
    auto heal = [](int i) { UserCmd c; c.weapon = WPN_SYRINGE; if (i == 0) c.buttons = BTN_FIRE; return c; };
    for (int k = 0; k < 50 && pa.health == 50; k++) pump(s, cls, 0.01, heal);
    double first = pa.lastFire;
    printf("  one use: %.0f hp (from 50), next in %.2f s\n", pa.health, pa.nextFire - first);
    CHECK(pa.health == 60 && std::fabs(pa.nextFire - (first + 1.0)) < 0.002);
    while (s.time() < first + 0.9) pump(s, cls, 0.01, heal);   // held: nothing more until the 1 s cooldown is over
    CHECK(pa.health == 60);
    while (s.time() < first + 1.2) pump(s, cls, 0.01, heal);
    printf("  held 1.2 s: %.0f hp\n", pa.health);
    CHECK(pa.health == 70);
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

// FINAL_REVIEW 5.1: trim (8) and clip (9) boxes, on every shipped map, go through every edit op and its undo like any
// other box (they were refused: a nudged clip wall stayed put, a deleted one couldn't come back), groups and parts
// take them too, and the editor's material cycle reaches them.
static void testTrimClipEdits() {
    printf("trim and clip boxes: set / ins / del and their undo\n");
    std::vector<int> seen;
    for (int k = 0, mat = 0; k < kMapMaterials; k++) { seen.push_back(mat); mat = nextMapMaterial(mat); }
    CHECK(seen.size() == 10 && seen[8] == 8 && seen[9] == 9 && nextMapMaterial(9) == 0);   // M: ..., lava, trim, clip, base
    for (const char* name : {"bastion", "orbital"}) {
        Map m;
        CHECK(m.load(name));
        if (m.brushes.empty()) continue;
        const std::string orig = m.serialize();
        const size_t refs0 = m.gridRefs;
        // bastion has no clip of its own: one is added (and the undo of that add is a del)
        int editable = (int)m.editableBrushes();
        int trimAt = -1, clipAt = -1;
        for (int i = 0; i < editable; i++) {
            if (m.brushes[i].material == 8 && trimAt < 0) trimAt = i;
            if (m.brushes[i].material == 9 && clipAt < 0) clipAt = i;
        }
        CHECK(trimAt >= 0);
        if (trimAt < 0) continue;
        std::vector<std::string> undo;   // (as the editor keeps it: the inverse of each op, newest last)
        auto apply = [&](const std::string& op, const std::string& inv) {
            bool ok = applyMapEdit(m, op);
            if (!ok) printf("  %s: refused: %s\n", name, op.c_str());
            CHECK(ok);
            if (ok) undo.push_back(inv);
            return ok;
        };
        if (clipAt < 0) {
            if (!apply("b ins " + std::to_string(editable) + " 9 -2 20 -2 2 24 2", "b del " + std::to_string(editable))) continue;
            clipAt = editable++;
            CHECK(m.brushes[clipAt].material == 9 && !m.brushes[clipAt].generated);
        }
        for (int at : {std::max(trimAt, clipAt), std::min(trimAt, clipAt)}) {   // (the higher first: deleting it moves nothing below)
            const int mat = m.brushes[at].material;
            // nudge it 0.25 m along x (the arrow keys): set, undone by a set back
            Map moved = m;
            moved.brushes[at].box.mn.x += 0.25f; moved.brushes[at].box.mx.x += 0.25f;
            CHECK(apply(describeMapObject(moved, 'b', "set", at), describeMapObject(m, 'b', "set", at)));
            CHECK(m.brushes[at].material == mat && m.brushes[at].box.mn.x == moved.brushes[at].box.mn.x);
            if (mat == 9) {   // still a wall for players where it went, still nothing to light and shots
                AABB in = m.brushes[at].box;
                Vec3 c = in.center();
                CHECK(m.boxSolid({c - Vec3(0.05f, 0.05f, 0.05f), c + Vec3(0.05f, 0.05f, 0.05f)}));
                CHECK(m.raycast(c + Vec3(0, 0, -0.01f), {0, 0, 1}, 0.02f) < 0);
            }
            // a copy inserted after the editable ones (ctrl+D), undone by a del
            int n = (int)m.editableBrushes();
            CHECK(apply(describeMapObject(m, 'b', "ins", at).replace(6, std::to_string(at).size(), std::to_string(n)), "b del " + std::to_string(n)));
            CHECK(m.brushes[n].material == mat && !m.brushes[n].generated);
            // delete it, undone by inserting it back
            CHECK(apply("b del " + std::to_string(at), describeMapObject(m, 'b', "ins", at)));
        }
        // the material cycle on a box (M): every step is an edit that goes through, trim and clip included
        {
            int at = 0;
            for (int k = 0; k < kMapMaterials; k++) {
                Map next = m;
                next.brushes[at].material = nextMapMaterial(m.brushes[at].material);
                CHECK(apply(describeMapObject(next, 'b', "set", at), describeMapObject(m, 'b', "set", at)));
            }
        }
        // groups and their parts take trim and clip as well, and save / load as they were
        {
            size_t gi = m.groups.size();
            std::string g = std::to_string(gi);
            CHECK(apply("g ins " + g + " 0 9 0 -3 30 -3 3 33 3", "g del " + g));
            CHECK(apply("g part " + g + " 8 0 0 0 0.5 0.5 0.5", "g set " + g + " 0 9 0 -3 30 -3 3 33 3"));   // (set: no parts)
            CHECK(apply("g part " + g + " 9 0.5 0.5 0.5 1 1 1", "g set " + g + " 0 9 0 -3 30 -3 3 33 3"));
            CHECK(apply("g set " + g + " 2 9 1 -3 30 -3 3 32 3", describeMapObject(m, 'g', "set", (int)gi)));   // clip stairs
            Map back;
            CHECK(back.parse(m.serialize()) && back.groups.size() == gi + 1 && back.groups[gi].material == 9 && back.serialize() == m.serialize());
            size_t clipSteps = 0;
            for (auto& b : m.brushes) clipSteps += b.generated && b.material == 9;
            CHECK(clipSteps >= 2);
        }
        // undo everything, newest first: back to the map as it loaded
        while (!undo.empty()) {
            std::string inv = undo.back();
            undo.pop_back();
            std::istringstream lines(inv);
            for (std::string op; std::getline(lines, op);) {
                bool ok = applyMapEdit(m, op);
                if (!ok) printf("  %s: undo refused: %s\n", name, op.c_str());
                CHECK(ok);
            }
        }
        CHECK(m.serialize() == orig && m.gridRefs == refs0);
        Map full = m;
        CHECK(full.rebuild() && full.gridRefs == m.gridRefs && full.brushes.size() == m.brushes.size());
    }
}

// MAP-12: brush and group-part edits update the map in place. After every op the map must be exactly what a full
// rebuild makes of it (brushes, bounds, wallbang flag, lava, every grid cell's brushes in order), and its text must
// load back to the same map.
static void testIncrementalEdits() {
    printf("map edits update the grid in place, same as a rebuild\n");
    Map m;
    CHECK(m.load("bastion"));
    if (m.brushes.empty()) return;
    std::mt19937 rng(7);
    auto r = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    auto same = [&](const Map& a, const Map& b) {
        if (a.brushes.size() != b.brushes.size() || a.gridW != b.gridW || a.gridH != b.gridH || a.gridX0 != b.gridX0 || a.gridZ0 != b.gridZ0 ||
            a.gridRefs != b.gridRefs || a.hasWallbang != b.hasWallbang || a.lava.size() != b.lava.size() || a.cells.size() != b.cells.size() ||
            memcmp(&a.bounds, &b.bounds, sizeof a.bounds) != 0)
            return false;
        for (size_t k = 0; k < a.brushes.size(); k++)
            if (memcmp(&a.brushes[k].box, &b.brushes[k].box, sizeof(AABB)) || a.brushes[k].material != b.brushes[k].material || a.brushes[k].generated != b.brushes[k].generated) return false;
        for (size_t k = 0; k < a.lava.size(); k++) if (memcmp(&a.lava[k], &b.lava[k], sizeof(AABB))) return false;
        for (size_t c = 0; c < a.cells.size(); c++) {
            uint32_t na = a.cells[c].end - a.cells[c].start, nb = b.cells[c].end - b.cells[c].start;
            if (na != nb || memcmp(&a.cellBrushes[a.cells[c].start], &b.cellBrushes[b.cells[c].start], na * sizeof(uint16_t))) return false;
        }
        return true;
    };
    int applied = 0, mismatches = 0;
    for (int it = 0; it < 1500; it++) {
        int editable = (int)m.editableBrushes(), k = (int)(rng() % (unsigned)editable);
        char op[200];
        int what = (int)(rng() % 10);
        int mat = (int)(rng() % kMapMaterials);   // (trim and clip too)
        float x = r(-40, 40), y = r(-2, 12), z = r(-40, 40);
        if (rng() % 25 == 0) x = r(-120, 120);   // now and then out past the edge: the grid has to move
        if (what < 4) snprintf(op, sizeof op, "b set %d %d %g %g %g %g %g %g", k, mat, x, y, z, x + r(0.1f, 9), y + r(0.1f, 4), z + r(0.1f, 9));
        else if (what < 6) snprintf(op, sizeof op, "b ins %d %d %g %g %g %g %g %g", (int)(rng() % (unsigned)(editable + 1)), mat, x, y, z, x + r(0.1f, 30), y + r(0.1f, 4), z + r(0.1f, 2));
        else if (what < 8) snprintf(op, sizeof op, "b del %d", k);
        else if (what == 8 && m.groups.size() < 6) snprintf(op, sizeof op, "g ins %d 0 %d 0 %g %g %g %g %g %g", (int)(rng() % (m.groups.size() + 1)), mat, x, y, z, x + 6, y + 3, z + 6);
        else if (!m.groups.empty()) snprintf(op, sizeof op, "g part %d %d %g %g %g %g %g %g", (int)(rng() % m.groups.size()), mat, r(0, 0.5f), r(0, 0.5f), r(0, 0.5f), r(0.5f, 1), r(0.5f, 1), r(0.5f, 1));
        else continue;
        if (!applyMapEdit(m, op)) continue;
        applied++;
        Map full = m;
        if (!full.rebuild() || !same(m, full)) { if (!mismatches++) printf("  differs from a rebuild after: %s\n", op); }
    }
    CHECK(applied > 1000 && mismatches == 0);
    CHECK(m.sourceStale);
    Map back;
    CHECK(back.parse(m.sourceText()) && back.serialize() == m.serialize() && !m.sourceStale);
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
    for (int i = 0; i < 30 && (s.ms_.phase == PHASE_WARMUP || s.ms_.phase == PHASE_FREEZE); i++) pump(s, cls, 0.2, idle);   // (a slow host's clock lags)
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
// The bots' nav graph uses a grid for neighbours and nodeAt, and A* for routes (REVIEW PERF-10): the same graph
// and equally short routes as checking every pair and running Dijkstra, on every map.
static void testNavGraph() {
    printf("nav graph: grid build and A* match the brute force on every map\n");
    size_t maps = 0, surfaces = 0, links = 0;
    double worstBuild = 0;
    for (auto& name : listMaps()) {
        Map m;
        if (!m.load(name)) continue;
        NavGraph g;
        double t0 = timeNow();
        g.build(m);
        double bt = (timeNow() - t0) * 1000;
        worstBuild = std::max(worstBuild, bt);
        size_t n = g.nodes.size(), bad = 0;
        maps++; surfaces += n;
        for (auto& l : g.links) links += l.size();
        for (size_t i = 0; i < n; i++) {   // links: every pair, the old way (ladder links come after, same as build)
            std::vector<std::pair<int, float>> want;
            for (size_t j = 0; j < n; j++) {
                if (i == j) continue;
                const auto &a = g.nodes[i], &b = g.nodes[j];
                float gx = std::max(0.0f, std::max(a.x0, b.x0) - std::min(a.x1, b.x1));
                float gz = std::max(0.0f, std::max(a.z0, b.z0) - std::min(a.z1, b.z1));
                float gap = std::sqrt(gx * gx + gz * gz), up = b.y - a.y;
                if (gap > 2.6f || up > 1.15f || up < -8.0f) continue;
                float cx = ((a.x0 + a.x1) - (b.x0 + b.x1)) * 0.5f, cz = ((a.z0 + a.z1) - (b.z0 + b.z1)) * 0.5f;
                want.push_back({(int)j, std::sqrt(cx * cx + cz * cz) + gap * 2 + std::max(up, 0.0f) * 2});
            }
            bool same = g.links[i].size() >= want.size() && std::equal(want.begin(), want.end(), g.links[i].begin());
            bad += !same;
        }
        CHECK(bad == 0);
        uint32_t r = 12345;
        auto rf = [&] { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.0f; };
        int nodeMismatch = 0, pathMismatch = 0;
        for (int k = 0; k < 400 && n; k++) {   // nodeAt: grid vs every surface
            const auto& nd = g.nodes[(size_t)(rf() * n) % n];
            Vec3 p = {nd.x0 - 0.5f + rf() * (nd.x1 - nd.x0 + 1.0f), nd.y + rf() * 0.5f, nd.z0 - 0.5f + rf() * (nd.z1 - nd.z0 + 1.0f)};
            int best = -1; float by = -1e9f;
            for (size_t i = 0; i < n; i++) {
                const auto& q = g.nodes[i];
                if (p.x < q.x0 - 0.3f || p.x > q.x1 + 0.3f || p.z < q.z0 - 0.3f || p.z > q.z1 + 0.3f || q.y > p.y + 0.6f || q.y < p.y - 6.0f) continue;
                if (q.y > by) { by = q.y; best = (int)i; }
            }
            nodeMismatch += best != g.nodeAt(p);
        }
        std::vector<int> path;
        for (int k = 0; k < 40 && n; k++) {    // A* route length == Dijkstra distance
            int a = (int)(rf() * n) % (int)n, b = (int)(rf() * n) % (int)n;
            std::vector<float> d(n, 1e30f);
            std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<>> q;
            d[a] = 0; q.push({0, a});
            while (!q.empty()) { auto [du, u] = q.top(); q.pop(); if (du > d[u]) continue; for (auto& [v, c] : g.links[u]) if (du + c < d[v]) { d[v] = du + c; q.push({d[v], v}); } }
            bool ok = g.findPath(a, b, path);
            if (ok != (d[b] < 1e29f)) { pathMismatch++; continue; }
            if (!ok || a == b) continue;
            float len = 0; int prev = a;
            for (int v : path) { float c = 1e30f; for (auto& l : g.links[prev]) if (l.first == v) c = std::min(c, l.second); len += c; prev = v; }
            pathMismatch += prev != b || std::fabs(len - d[b]) > 1e-3f * std::max(1.0f, d[b]);
        }
        if (nodeMismatch || pathMismatch || bad) printf("    %s: %zu link lists, %d nodeAt, %d routes differ\n", name.c_str(), bad, nodeMismatch, pathMismatch);
        CHECK(nodeMismatch == 0 && pathMismatch == 0);
    }
    printf("  %zu maps, %zu surfaces, %zu links; slowest build %.2f ms (in the background on a server)\n", maps, surfaces, links, worstBuild);
    CHECK(maps >= 20);
}

static constexpr float kAimSample = 75.0f; // s of game time per tick rate (all four levels at once)
// Bot aim by level, at three server tick rates: damage per second with the rifle against a player strafing (and
// juking) 14 m away. The target is a scripted player that moves on game time, so it moves the same way at every
// tick rate however loaded the machine is. One bot of each level stands side by side (2 m apart, facing the
// target) and they all shoot at it, so every level gets the same movement; their places rotate between rates.
// Levels must be clearly ordered, play the same at every tick rate (their timing is all game time), and even
// the expert must stay beatable (well under perfect aim).
static void testBotAim(uint16_t& port) {
    printf("bot aim: damage per second vs a strafing player, by level and server tick rate\n");
    int rates[] = {1000, 250, 125};
    if (getenv("AIM_RATE")) rates[0] = rates[1] = rates[2] = atoi(getenv("AIM_RATE")); // (noise check: three samples at one rate)
    float dps[3][BOT_SKILLS] = {}, acc[3][BOT_SKILLS] = {};
    for (int r = 0; r < 3; r++) {
        Server s;
        ServerConfig cfg;
        cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port++; cfg.maxTickRate = rates[r];
        s.setLog([](const std::string&) {});
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
        CHECK(s.tickRate() == rates[r]);
        int tid = s.addBot(TEAM_A, BOT_BEGINNER);      // the target (first: its script runs before the others think)
        int bot[BOT_SKILLS];
        for (int k = 0; k < BOT_SKILLS; k++) {
            bot[k] = s.addBot(TEAM_A, k);
            s.bots_[bot[k]].forceWeapon = WPN_RIFLE;
            s.bots_[bot[k]].aimYaw = PI / 2;          // (facing the target's side: no searching, aim only)
            s.bots_[bot[k]].onlyEnemy = tid;          // (side by side, the experts' wide view would take in the neighbours)
            s.bots_[bot[k]].rng = 0x51ED270Bu + 7919u * k + 104729u * r;
        }
        int dir = 127;
        double nextJuke = 0;
        s.bots_[tid].script = [&](Server& sv, int id) {
            for (int k = 0; k < BOT_SKILLS; k++) {     // bots 14 m from the target's line, 2 m apart, standing
                auto& bp = sv.players_[bot[k]];
                bp.move.pos = {7, 0, -27.0f + 2.0f * ((k + r) % BOT_SKILLS)}; bp.move.vel = {};
                bp.health = 100;
            }
            auto& p = sv.players_[id];
            UserCmd c;
            c.weapon = kNoWeaponChange;
            if (!p.alive) return c;
            p.health = 100;
            p.move.pos.x = -7;                                   // strafe back and forth along z, turning at +-4 m
            if (std::fabs(p.move.pos.z + 24) > 8) p.move.pos.z = -24;
            float off = p.move.pos.z + 24;                       // and every 1.2 s: jukes
            if ((off > 4 && p.move.vel.z > 0) || (off < -4 && p.move.vel.z < 0)) { dir = -dir; nextJuke = sv.time() + 1.2; }
            else if (sv.time() >= nextJuke) { dir = -dir; nextJuke = sv.time() + 1.2; }
            c.side = (int8_t)dir;
            c.yaw = -PI / 2;
            return c;
        };
        s.players_[tid].move.pos = {-7, 0, -24}; s.players_[tid].move.vel = {}; // (in front of them from the start)
        for (int k = 0; k < BOT_SKILLS; k++) s.players_[bot[k]].move.pos = {7, 0, -27.0f + 2.0f * ((k + r) % BOT_SKILLS)};
        std::vector<NetClient*> none;
        pump(s, none, 1.0, idle);
        float dealt0[BOT_SKILLS]; int fired0[BOT_SKILLS], hit0[BOT_SKILLS];
        for (int k = 0; k < BOT_SKILLS; k++) { auto& bp = s.players_[bot[k]]; dealt0[k] = bp.dmgDealt; fired0[k] = bp.shotsFired; hit0[k] = bp.shotsHit; }
        double t0 = s.time();
        pump(s, none, kAimSample, idle);
        double span = std::max(1.0, s.time() - t0);
        for (int k = 0; k < BOT_SKILLS; k++) {
            auto& bp = s.players_[bot[k]];
            int fired = bp.shotsFired - fired0[k], hit = bp.shotsHit - hit0[k];
            dps[r][k] = (float)((bp.dmgDealt - dealt0[k]) / span);
            acc[r][k] = fired ? (float)hit / fired : 0.0f;
            CHECK(hit <= fired && s.bots_[bot[k]].target == tid); // (nobody went for anybody else)
            printf("  %4d Hz  %-12s %5.1f dps, %.0f%% of %d shots hit\n", rates[r], botSkillName(k), dps[r][k], acc[r][k] * 100, fired);
        }
        CHECK(s.players_[tid].deaths == 0);
        s.bots_[tid].script = nullptr;
        s.stop();
    }
    const float perfect = weaponDef(WPN_RIFLE).damage / weaponDef(WPN_RIFLE).refire; // every shot a body hit
    float mean[BOT_SKILLS] = {};
    for (int k = 0; k < BOT_SKILLS; k++) {
        for (int r = 0; r < 3; r++) mean[k] += dps[r][k] / 3;
        printf("  %-12s %5.1f dps on average (%.0f%% of perfect aim)\n", botSkillName(k), mean[k], mean[k] / perfect * 100);
    }
    for (int k = 0; k + 1 < BOT_SKILLS; k++) CHECK(mean[k] * 1.25f < mean[k + 1]);   // every level clearly above the one below
    for (int r = 0; r < 3; r++) for (int k = 0; k + 1 < BOT_SKILLS; k++) CHECK(dps[r][k] < dps[r][k + 1]); // at every tick rate too
    for (int k = 0; k < BOT_SKILLS; k++)
        for (int r = 0; r < 3; r++) CHECK(std::fabs(dps[r][k] - mean[k]) <= std::max(1.5f, 0.3f * mean[k])); // the tick rate doesn't matter (beyond sampling noise)
    CHECK(mean[0] > 1);                          // beginners still land some hits
    CHECK(mean[BOT_SKILLS - 1] < perfect * 0.6f); // experts are strong, but a human can beat them
}

// Alt-fires: thrown knife hits at range, shotgun ricochets hit someone behind you (off the wall in front),
// scoped rifle fires at half rate.
// Every ladder on every map gets you to the top: stand at its foot, hold forward (into the wall) and you
// end up standing on whatever it leads to.
int mapReport(const char* outPath); // tests/mapreport.cpp (returns how many map scores are below 7)
int exportAssets(const char* dir);   // tests/assets_tests.cpp: every default asset as files + manifest.json
int assetTests();                    // tests/assets_tests.cpp (returns the failures)

static void testLadders() {
    printf("ladders: every one climbable\n");
    for (auto& name : testedMaps()) {
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
            int was = b.clientId();
            b.disconnect();
            pump(s, cls, 0.5, idle);
            CHECK(s.humanCount() == 3 && s.away(was));   // the slot waits for a reconnect (the grace)
            b.connect("127.0.0.1:" + std::to_string(cfg.port), "bob");
            pump(s, cls, 1.5, idle);
            bool ok = b.state() == ConnState::Connected && s.humanCount() == 3 && b.clientId() == was && !s.away(was);
            if (!ok) printf("  %s: rejoin %d failed: state %d, players %d, error '%s'\n", modeName(mode), round + 1, (int)b.state(), s.humanCount(), b.error().c_str());
            CHECK(ok);
        }
        s.stop();
    }
}

// A custom mode: the server plays by its rules, sends them to a joining client in pieces that rebuild the
// same rules, and moves on to the next map of its pool when a match ends.
// Rules are per thread: a server hosted on its own thread (as the game does) plays its custom rules there,
// the client thread predicts with the copy it got by EV_RULES, and stopping the server can't reset the client's.
static void testHostedRulesPerThread(uint16_t port) {
    printf("custom rules per thread (hosted server)\n");
    setActiveRules(CustomRules());
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true; cfg.map = "bastion"; cfg.port = port;
    cfg.custom.name = "thread rules"; cfg.custom.gravity = 0.25f; cfg.custom.damage[WPN_RIFLE] = 3;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    setActiveRules(CustomRules());   // the client thread starts on the standard rules whatever start() did here
    std::atomic<bool> quit{false};
    std::atomic<float> serverGravity{0};
    std::thread host([&] { s.run(quit); serverGravity = moveTuning().gravity; });
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "pred");
    std::vector<std::string> parts;
    for (int i = 0; i < 400 && (parts.empty() || std::any_of(parts.begin(), parts.end(), [](auto& p) { return p.empty(); })); i++) {
        a.poll(); UserCmd c{}; a.tick(c);
        for (auto& e : a.takeEvents()) if (e.type == EV_RULES && e.b > 0 && e.a < e.b) { parts.resize(e.b); parts[e.a] = e.text; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::string all; for (auto& p : parts) all += p;
    CustomRules got;
    CHECK(got.fromText(all));
    setActiveRules(got);   // what Game does on EV_RULES
    CHECK(std::fabs(moveTuning().gravity - 0.25f) < 1e-4f);
    quit = true; host.join(); s.stop();
    CHECK(std::fabs(serverGravity - 0.25f) < 1e-4f);                 // the server thread played by them
    CHECK(activeRules().name == "thread rules" && std::fabs(moveTuning().gravity - 0.25f) < 1e-4f
          && std::fabs(weaponDef(WPN_RIFLE).damage - baseWeaponDef(WPN_RIFLE).damage * 3) < 1e-3f);   // the client's: untouched
    setActiveRules(CustomRules());
}

// Constants written out in more than one language: the queue list (community.cpp, the master Worker, the
// Discord bot) and the UDP info query the bot speaks (quarkdata.py). Parsed from the sources; any mismatch fails.
static std::string readSource(const char* rel) {
    if (sourceDir().empty()) return "";
    std::ifstream f(sourceDir() + "/" + rel, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static std::string blockAfter(const std::string& s, const std::string& start, const std::string& end) {
    size_t a = s.find(start); if (a == std::string::npos) return "";
    size_t b = s.find(end, a + start.size()); return s.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
static void testCrossLanguageConstants(uint16_t port) {
    printf("cross-language constants\n");
    std::string cpp = readSource("src/client/community.cpp"), js = readSource("tools/master/worker.js"),
                bot = readSource("tools/discordbot/bot.py"), qd = readSource("tools/discordbot/quarkdata.py");
    if (cpp.empty() || js.empty() || bot.empty() || qd.empty()) { printf("  (sources not found: skipped)\n"); return; }
    // queues: key -> (title, size)
    std::map<std::string, std::pair<std::string, int>> qc, qb; std::map<std::string, int> qj;
    std::smatch m;
    std::string b = blockAfter(cpp, "Game::kQueues[] = {", "};");
    static const std::regex rx83946("\\{\"([\\w-]+)\", \"([^\"]*)\", (\\d+)\\}"); for (std::sregex_iterator it(b.begin(), b.end(), rx83946), e; it != e; ++it)
        qc[(*it)[1]] = {(*it)[2], std::stoi((*it)[3])};
    b = blockAfter(js, "const QUEUE_SIZES = {", "};");
    static const std::regex rx7847("\"?([\\w-]+)\"?: (\\d+)"); for (std::sregex_iterator it(b.begin(), b.end(), rx7847), e; it != e; ++it) qj[(*it)[1]] = std::stoi((*it)[2]);
    b = blockAfter(bot, "QUEUES: dict", "\n}");
    static const std::regex rx61002("\"([\\w-]+)\": \\(\"([^\"]*)\", (\\d+)\\)"); for (std::sregex_iterator it(b.begin(), b.end(), rx61002), e; it != e; ++it)
        qb[(*it)[1]] = {(*it)[2], std::stoi((*it)[3])};
    CHECK(qc.size() >= 10);
    CHECK(qj.size() == qc.size() && qb.size() == qc.size());
    for (auto& [k, v] : qc) {
        bool ok = qj.count(k) && qj[k] == v.second && qb.count(k) && qb[k] == v;
        if (!ok) { printf("  queue %s differs between community.cpp, worker.js and bot.py\n", k.c_str()); g_fail++; }
    }
    // quarkdata.py: the pinned Worker address (the bot never follows master.txt: it sends BOT_KEY there)
    {
        std::string mc = readSource("src/game/master.cpp"), a, b;
        std::smatch mm;
        static const std::regex rxPin("kPinnedMasterUrl = \"([^\"]+)\""), rxPy("\nMASTER_URL = \"([^\"]+)\"");
        if (std::regex_search(mc, mm, rxPin)) a = mm[1];
        if (std::regex_search(qd, mm, rxPy)) b = mm[1];
        CHECK(!a.empty() && a == b);
    }
    // quarkdata.py: protocol constants and mode names/titles
    auto pyInt = [&](const char* name) {
        std::regex r(std::string("\\n") + name + " = (0x[0-9A-Fa-f]+|\\d+)");
        return std::regex_search(qd, m, r) ? (long)std::stol(m[1].str(), nullptr, 0) : -1L;
    };
    CHECK(pyInt("PROTOCOL_MAGIC") == (long)kProtocolMagic);
    CHECK(pyInt("PKT_INFO_REQUEST") == PKT_INFO_REQUEST && pyInt("PKT_INFO_REPLY") == PKT_INFO_REPLY);
    CHECK(pyInt("INFO_REQUEST_BYTES") == kInfoRequestBytes);
    auto pyList = [&](const char* name) {
        std::vector<std::string> out;
        std::string l = blockAfter(qd, std::string("\n") + name + " = [", "]");
        static const std::regex rx13224("\"([^\"]*)\""); for (std::sregex_iterator it(l.begin(), l.end(), rx13224), e; it != e; ++it) out.push_back((*it)[1]);
        return out;
    };
    auto names = pyList("MODE_NAMES"), titles = pyList("MODE_TITLES");
    CHECK((int)names.size() == MODE_COUNT && (int)titles.size() == MODE_COUNT);
    for (int i = 0; i < MODE_COUNT && i < (int)names.size() && i < (int)titles.size(); i++)
        if (names[i] != modeName(i) || titles[i] != modeTitle(i)) { printf("  mode %d: quarkdata.py says %s / %s\n", i, names[i].c_str(), titles[i].c_str()); g_fail++; }
    // the info reply layout quarkdata.parse_info_reply reads: u32 magic, u8 type, str name, str map, u8 mode, u8 players, u8 max, u16 port
    Server s; ServerConfig cfg; cfg.map = "bastion"; cfg.mode = MODE_KOTH; cfg.port = port; cfg.maxPlayers = 7;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { printf("  FAIL: server start\n"); g_fail++; return; }
    UdpSocket q; CHECK(q.open(0));
    uint8_t req[kInfoRequestBytes] = {};
    uint32_t mg = kProtocolMagic; memcpy(req, &mg, 4); req[4] = (uint8_t)pyInt("PKT_INFO_REQUEST");   // little endian, as struct.pack("<IB")
    NetAddr to; NetAddr::parse("127.0.0.1", port, to);
    q.send(to, req, sizeof req);
    uint8_t rep[512]; int n = 0; NetAddr from;
    for (int i = 0; i < 500 && n <= 0; i++) { s.frame(); n = q.recv(from, rep, sizeof rep); sleepMs(1); }
    bool ok = n > 5 && rep[4] == (uint8_t)pyInt("PKT_INFO_REPLY");
    int off = 5; std::string str[2];
    for (int k = 0; ok && k < 2; k++) { int l = rep[off++]; ok = off + l <= n; if (ok) str[k].assign((char*)rep + off, l); off += l; }
    ok = ok && off + 5 <= n;
    CHECK(ok && str[1] == "bastion" && rep[off] == MODE_KOTH && rep[off + 1] == 0 && rep[off + 2] == 7 && (rep[off + 3] | rep[off + 4] << 8) == port);
    s.stop();
}

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
        auto speedAfter = [&](float accel) {   // run speed 0.02 s after starting from a standstill
            CustomRules g; g.name = "accel"; g.move[MV_GROUND_ACCEL] = accel; setActiveRules(g);
            MoveState q; q.pos = {0, 0.01f, 0};
            UserCmd u; u.msec = 1; u.forward = 127;
            for (int i = 0; i < 50; i++) pmove(q, UserCmd{}, open);
            for (int i = 0; i < 20; i++) pmove(q, u, open);
            return std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z);
        };
        float normal = speedAfter(1), quick = speedAfter(2);
        setActiveRules(CustomRules());
        printf("  ground accel x2: %.1f m/s after 0.02 s (normal %.1f)\n", quick, normal);
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
    setActiveRules(CustomRules());
    CHECK(!activeRules().active() && std::fabs(weaponDef(WPN_RIFLE).damage - baseWeaponDef(WPN_RIFLE).damage) < 1e-4f);
    testHostedRulesPerThread((uint16_t)(port + 100));
}

// Every staircase (recorded by tools/make_maps.py as "# stairs x0 z0 x1 z1 base top axis rising solid"):
// walked from the bottom to the top by the real movement code, and checked against the paths around it:
// no walkable surface may pass through a solid flight, or run underneath an open one with less than a
// player's height of clearance.
static void testStairs() {
    printf("stairs: every flight walkable, none cutting a path\n");
    int flights = 0;
    for (auto& name : testedMaps()) {
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

// Joined mid tag match, then a vote to 1v1: everyone is placed like a newcomer (one per side, the rest watch),
// not all put on team A.
static void testTagLateJoin(uint16_t port) {
    printf("tag late joiner, then 1v1\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = false; cfg.map = "hamlet"; cfg.mode = MODE_TAG; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    NetClient c[4];
    std::vector<NetClient*> cls;
    for (int i = 0; i < 3; i++) { c[i].connect("127.0.0.1:" + std::to_string(port), "p" + std::to_string(i)); cls.push_back(&c[i]); }
    pump(s, cls, 1.5, idle);
    s.command("forcestart");
    pump(s, cls, 0.3, idle);
    CHECK(rulesTeamLocked(s));
    c[3].connect("127.0.0.1:" + std::to_string(port), "late");
    cls.push_back(&c[3]);
    pump(s, cls, 1.0, idle);
    int late = c[3].clientId();
    CHECK(late >= 0 && late < kMaxPlayers);
    if (late < 0 || late >= kMaxPlayers) { s.stop(); return; }
    CHECK(s.players_[late].team == TEAM_SPECTATOR && s.players_[late].waitingToPlay);
    s.command("map temple 1v1");
    pump(s, cls, 0.5, idle);
    int a = 0, b = 0, spec = 0;
    for (auto* cl : cls) {
        int id = cl->clientId();
        if (id < 0 || id >= kMaxPlayers) continue;
        int t = s.players_[id].team;
        (t == TEAM_A ? a : t == TEAM_B ? b : spec)++;
    }
    CHECK(s.config().mode == MODE_ARENA && a == 1 && b == 1 && spec == 2);
    for (auto& p : s.players_) CHECK(!p.waitingToPlay);
    s.stop();
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
// "Reset maps": every shipped map is written back from the copy built into the game. Only the temp data dir is
// written: the install's maps folder is played by a copy of it there.
static void testResetMaps() {
    printf("reset built-in maps\n");
    namespace fs = std::filesystem;
    Map orig;
    CHECK(orig.load("temple"));
    Server s;   // (not started: just its map facts cache)
    CHECK(s.mapInfo("temple").ok && s.mapInfo("temple").ranked != 0);
    CHECK(saveMapFile(userMapsDir() + "/temple.qmap", orig.source + "box 0 0 0 1 1 1\n")); // your edit shadows the shipped one
    Map m;
    CHECK(m.load("temple") && !isOfficialMap(m));
    CHECK(s.mapInfo("temple").ok && s.mapInfo("temple").ranked == 0);   // votes / ranked see the file that's played
    std::string shipped = userDataPath("zz_install_maps");
    std::error_code ec;
    fs::remove_all(shipped, ec);
    fs::create_directories(shipped, ec);
    CHECK(saveMapFile(shipped + "/temple.qmap", orig.source + "box 0 0 0 1 1 1\n"));   // a shipped map changed in place
    CHECK(saveMapFile(shipped + "/crucible.qmap", orig.source) && saveMapFile(shipped + "/crucible.qlm", "x"));   // retired
    CHECK(resetOfficialMaps(shipped) >= 13);
    CHECK(!fs::exists(shipped + "/crucible.qmap", ec) && !fs::exists(shipped + "/crucible.qlm", ec));
    Map r;
    CHECK(r.load("temple") && isOfficialMap(r));   // (your edited copy is gone)
    CHECK(s.mapInfo("temple").ranked != 0);
    int n = 0;
    for (auto& e : fs::directory_iterator(shipped, ec)) {
        if (e.path().extension() != ".qmap") continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        Map k;
        k.name = e.path().stem().string();
        CHECK(k.parse(ss.str()) && isOfficialMap(k));
        n++;
    }
    CHECK(n >= 13);
    fs::remove_all(shipped, ec);
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
                double before = sa.lastFire;
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
            double lf = sa.lastFire;
            pump(s, cls, 0.05, [&](int i) { return aim(i, BTN_ALTFIRE); });       // ricochet comes back at the target
            pump(s, cls, 0.3, idle);
            CHECK(sa.lastFire != lf);
            CHECK(sb.health < hp0 && hp0 - sb.health <= 20 * weaponDef(WPN_SHOTGUN).damage * kShotgunBounceDamage + 0.01f);
            CHECK(sa.health == 100); // never your own ricochet
        } else {
            int shots = 0;
            double last = sa.lastFire;
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
            s.players_[b.clientId()].voteAt = 0;   // (a failed vote's caller waits before the next: not what this checks)
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
    snprintf(tag, sizeof tag, "%u", (unsigned)qrand());
    std::string name = std::string("zz_dltest") + tag, path = userMapsDir() + "/" + name + ".qmap";
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

// The Worker's /filled rows, as the game reads them (REVIEW COR-1: a Discord member "d:<name>:<hex>:<hex>"
// was split at the first ':' and its raw name hex-decoded with a throwing stoi, crashing every queued client).
static void testFilledParse() {
    FilledRow row;
    CHECK(parseFilledRow("7 ranked-vanilla 1700000000 8c722bbbb3748b9da426cedf20c3e396:7261636572:-,d:dima:64696d61:64696d61", row));
    CHECK(row.n == 7 && row.mode == "ranked-vanilla" && row.created == 1700000000 && row.members.size() == 2);
    if (row.members.size() == 2) {
        CHECK(row.members[0].who == "8c722bbbb3748b9da426cedf20c3e396" && row.members[0].name == "racer" && row.members[0].discord.empty());
        CHECK(row.members[1].who == "d:dima" && row.members[1].name == "dima" && row.members[1].discord == "dima");
    }
    CHECK(parseFilledRow("8 vs 1 d:a:b:zz:x1,d:two words:6869:-", row) && row.members.size() == 2);   // garbage hex, a spaced name
    if (row.members.size() == 2) CHECK(row.members[0].who == "d:a:b" && row.members[0].name.empty() && row.members[1].who == "d:two words" && row.members[1].name == "hi");
    CHECK(fromHexText("6869") == "hi" && fromHexText("686") == "" && fromHexText("6g") == "" && fromHexText("-") == "" && fromHexText("0a41") == "A");
    CHECK(parseFilledMembers("a,a:b,:6869:,").size() == 0);
    CHECK(!parseFilledRow("x vs 1 a:b:c", row) && !parseFilledRow("9 vs 1", row));
}

// Ranked: ids from keys, the server's result event, and the report each player's game builds from it
// (the Worker side is tested by tools/master/test.mjs).
static void testRanked(uint16_t port) {
    printf("ranked: keys, results, reports\n");
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex(std::string(1000, 'a')).substr(0, 16) == "41edece42d63e8d9");
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)qrand());
    std::string keyPath = userDataPath() + "/zz_test" + tag + ".key";
    RankKey k1 = loadOrCreateRankKey(keyPath), again = loadOrCreateRankKey(keyPath);
    std::remove(keyPath.c_str());
    CHECK(k1.secret.size() == 64 && k1.id == again.id && k1.pub.size() == 64 && k1.id == sha256Hex(k1.pub).substr(0, 32));
    CHECK(k1.id != legacyRankId(k1.secret) && legacyRankId(k1.secret) == sha256Hex(k1.secret).substr(0, 32));
    {   // the key derivation the Worker repeats (tools/master/test.mjs checks the same numbers)
        RankKey f = rankKeyFromSecret(std::string(64, 'a'));
        CHECK(f.pub == "7361c4bb035a82708b4b001bcfd4b748ff9f89b9647a51d2dc4a7f0dc20d497d" && f.id == "694e20014c510c34c129e731ef06fe1d");
        std::string sig;
        const std::string srv = "203.0.113.5:27500";
        CHECK(rankConnectMessage(0x1234abcd, 7, srv) == "quark-rankid-v2 1234abcd 00000007 203.0.113.5:27500");
        CHECK(rankTicketMessage("00112233aabbccdd", srv) == "quark-ticket-v2 00112233aabbccdd 203.0.113.5:27500");   // (test.mjs signs the same)
        CHECK(rankSign(f, rankConnectMessage(0x1234abcd, 7, srv), &sig) && rankVerify(f.pub, rankConnectMessage(0x1234abcd, 7, srv), sig));
        CHECK(!rankVerify(f.pub, rankConnectMessage(0x1234abcd, 8, srv), sig));        // another challenge
        CHECK(!rankVerify(k1.pub, rankConnectMessage(0x1234abcd, 7, srv), sig));       // another key
        CHECK(!rankVerify(f.pub, rankConnectMessage(0x1234abcd, 7, "203.0.113.6:27500"), sig));   // another server (REVIEW 2.2)
        CHECK(rankKeyFromSecret("nothex").id.empty());
    }
    {   // tourney seeding: seed 1 meets the last seed, top seeds get the byes, 1 and 2 only meet in the final
        CHECK((seededBracket({10, 11, 12, 13, 14, 15, 16, 17}) == std::vector<int>{10, 17, 13, 14, 11, 16, 12, 15}));
        CHECK((seededBracket({10, 11, 12, 13, 14}) == std::vector<int>{10, -1, 13, 14, 11, -1, 12, -1}));
        CHECK((seededBracket({5}) == std::vector<int>{5, -1}));
        CHECK((seededBracket({5, 6, 7}) == std::vector<int>{5, -1, 6, 7}));
    }
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
        std::string path = userMapsDir() + "/zz_rk" + tag + ".qmap";
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
            RankKey k = rankKeyFromSecret(sha256Hex("player" + std::to_string(i) + tag));
            keys.push_back(k);
            cls[i]->setRankKey(k);   // (the id is proven at connect: the client signs the server's challenge)
            cls[i]->connect("127.0.0.1:" + std::to_string(cfg.port), "p" + std::to_string(i));
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
        RankKey outsider = rankKeyFromSecret(sha256Hex("nobody"));
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
            // leavers: the first one loses whatever the score said; they may be gone from our view
            std::vector<SeenPlayer> one(kMaxPlayers);
            one[1] = {true, TEAM_B, 0, 0};
            CHECK(rankedResultMatchesView("0123456789abcdef vanilla 0:0:1,1:1:0 left=0", one, true, 3, 1));
            CHECK(!rankedResultMatchesView("0123456789abcdef vanilla 0:0:0,1:1:1 left=0", one, true, 3, 1));
            CHECK(!rankedResultMatchesView("0123456789abcdef vanilla 0:0:1,1:1:0", one, true, 3, 1));        // not playing, not a leaver
            CHECK(!rankedResultMatchesView("0123456789abcdef vanilla 0:0:1,1:1:0 left=5", one, true, 3, 1)); // a leaver not in the result
            std::vector<SeenPlayer> g(kMaxPlayers);
            g[1] = {true, 1, 5, 0}; g[2] = {true, 2, 3, 0};
            CHECK(rankedResultMatchesView("0123456789abcdef instagib 0:0:2,1:1:0,2:2:1 left=0", g, false, 0, 0));
            CHECK(!rankedResultMatchesView("0123456789abcdef instagib 0:0:0,1:1:1,2:2:2 left=0", g, false, 0, 0)); // the leaver first
            {
                std::vector<std::string> lids(kMaxPlayers);
                lids[0] = std::string(32, 'a'); lids[1] = std::string(32, 'b'); lids[2] = std::string(32, 'c');
                std::string lm, ll, lp = rankedResultIds("0123456789abcdef instagib 0:0:2,1:1:0,2:2:1 left=0,2", lids, &lm, nullptr, nullptr, &ll);
                CHECK(!lp.empty() && ll == std::string(32, 'a') + "," + std::string(32, 'c'));
            }
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

// A live ranked 1v1: players sign the match id for the ticket, nothing can change the match (votes, map, kicks, team
// switches), a dropped player gets the slot back within the reconnect grace, and the first one gone past it loses.
static void testRankedLeaver(uint16_t port) {
    printf("ranked: live match lock, reconnect grace, first leaver loses\n");
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)qrand());
    Server s;
    ServerConfig cfg;
    cfg.map = "temple"; cfg.mode = MODE_ARENA; cfg.port = port; cfg.ranked = true; cfg.arenaVariant = 0;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b, c;
    RankKey ka = rankKeyFromSecret(sha256Hex(std::string("leaverA") + tag)), kb = rankKeyFromSecret(sha256Hex(std::string("leaverB") + tag));
    a.setRankKey(ka); b.setRankKey(kb);
    std::string addr = "127.0.0.1:" + std::to_string(port);
    a.connect(addr, "stayer"); b.connect(addr, "leaver");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 1.0, idle);
    int ia = a.clientId(), ib = b.clientId();
    CHECK(ia >= 0 && ib >= 0 && s.players_[ia].rankId == ka.id && s.players_[ib].rankId == kb.id);   // proven at connect
    CHECK(a.rankId(ib) == kb.id);
    {   // a squatter: someone else's public key without its secret gets no ranked id (and doesn't take the slot)
        RankKey forged = kb;
        forged.secret = std::string(64, '1');   // signs with another key
        c.setRankKey(forged);
        c.connect(addr, "squatter");
        std::vector<NetClient*> cc = {&a, &b, &c};
        pump(s, cc, 1.0, idle);
        int ic = c.clientId();
        CHECK(c.state() == ConnState::Connected && ic != ib && ic >= 0 && s.players_[ic].rankId.empty());
        CHECK(s.players_[ib].connected && s.players_[ib].rankId == kb.id);
        s.command("kick " + std::to_string(ic));   // (kicked: no grace)
        CHECK(!s.players_[ic].connected);
        c.disconnect();
    }
    s.command("forcestart");
    for (int i = 0; i < 60 && s.ms_.phase != PHASE_LIVE; i++) pump(s, cls, 0.1, idle);
    CHECK(s.ms_.phase == PHASE_LIVE && s.rankedLive());
    pump(s, cls, 0.5, idle);
    CHECK(!s.players_[ia].ticketSig.empty() && !s.players_[ib].ticketSig.empty() && s.rankedTicketSent_);   // both signed
    CHECK(rankVerify(kb.pub, rankTicketMessage(s.rankedMatch_, addr), s.players_[ib].ticketSig) && s.players_[ib].dialled.toString() == addr);
    CHECK(!rankVerify(kb.pub, rankTicketMessage(s.rankedMatch_, "127.0.0.1:" + std::to_string(port + 1)), s.players_[ib].ticketSig));   // names this server only
    // locked while live: votes, map / mode / ranked changes, kicking a player of the match, switching teams
    a.sendCommand("callvote map furnace");
    a.sendCommand("callvote ranked off");
    b.sendCommand("team spec");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE && s.players_[ib].team != TEAM_SPECTATOR);
    s.command("map furnace");
    s.command("kick " + std::to_string(ib));
    s.command("kick leaver");   // (by name too)
    CHECK(s.config().map == "temple" && s.players_[ib].connected && s.config().ranked);
    // the leaver's connection drops: slot kept, and a reconnect within the grace gets it back
    int scoreB = s.players_[ib].score = 2;
    b.disconnect();
    pump(s, cls, 0.3, idle);
    CHECK(s.away(ib) && s.players_[ib].connected && s.rankedLive() && s.ms_.phase != PHASE_MATCH_END);
    b.connect(addr, "leaver");
    pump(s, cls, 1.0, idle);
    CHECK(b.state() == ConnState::Connected && b.clientId() == ib && !s.away(ib) && s.players_[ib].score == scoreB && s.rankedLive());
    // gone past the grace: the first leaver loses, the match ends there, and the stayer can report it
    b.disconnect();
    std::vector<NetClient*> onlyA = {&a};
    std::string ranked;
    double end = timeNow() + kReconnectGrace + 2.0;
    while (timeNow() < end && ranked.empty()) {
        pump(s, onlyA, 0.1, idle);
        for (auto& e : a.takeEvents()) if (e.type == EV_RANKED) ranked = e.text;
    }
    CHECK(!s.players_[ib].connected && s.ms_.phase == PHASE_MATCH_END && !s.rankedLive());
    CHECK(ranked.find(" left=" + std::to_string(ib)) != std::string::npos);
    {
        std::vector<std::string> ids, players;
        for (int k = 0; k < kMaxPlayers; k++) ids.push_back(a.rankId(k));
        std::string ladder, body = buildRankReport(ranked, ids, ka, &players, &ladder);
        CHECK(body.find("&p=") != std::string::npos && players.size() == 2 && ladder == "vanilla");
        CHECK(body.find(ka.id + ":" + std::to_string(s.players_[ia].team) + ":0") != std::string::npos);   // the stayer won
        CHECK(body.find(kb.id + ":" + std::to_string(1 - s.players_[ia].team) + ":1") != std::string::npos);
        std::vector<SeenPlayer> seen(kMaxPlayers);
        seen[ia] = {true, s.players_[ia].team, 0, 0};
        CHECK(rankedResultMatchesView(ranked, seen, true, s.players_[ia].team == TEAM_A ? 0 : 2, s.players_[ia].team == TEAM_A ? 2 : 0)); // even behind on score
        std::string match, left, list = rankedResultIds(ranked, ids, &match, nullptr, nullptr, &left);
        CHECK(left == kb.id);
    }
    // the stayer leaving afterwards changes nothing (the match is over)
    a.disconnect();
    pump(s, onlyA, kReconnectGrace + 0.5, idle);
    CHECK(s.rankedLeft_.size() == 1);
    s.stop();
}

// REVIEW 2.2: a server the player joined relays its connect to a real server (forwarding the real server's challenge
// to the player and the player's signed CONNECT back) to play there under the player's ranked id. The player's game
// signs the address it dialled - the relay's - so the real server gives the relayed session no ranked id.
static void testRankedRelay(uint16_t port) {
    printf("ranked: a relayed connect gets no ranked id (signatures name the server dialled)\n");
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)qrand());
    RankKey kv = rankKeyFromSecret(sha256Hex(std::string("relayV") + tag));
    UdpSocket front, back;   // the relay: players dial `front`; it talks to the real server from `back`
    CHECK(front.open(0) && back.open(0));
    const NetAddr real{0x7F000001u, port};
    const std::string relayAddr = "127.0.0.1:" + std::to_string(front.localPort());
    for (int pass = 0; pass < 2; pass++) {
        Server s;
        ServerConfig cfg;
        cfg.map = "temple"; cfg.mode = MODE_ARENA; cfg.port = port;
        if (pass == 1) cfg.ownAddrs = {relayAddr};   // (control: if the host says that address reaches it, the same connect is taken)
        std::vector<std::string> logs;
        s.setLog([&](const std::string& l) { logs.push_back(l); });
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
        NetClient v;
        v.setRankKey(kv);
        v.connect(relayAddr, "victim");
        NetAddr victim{};
        double end = timeNow() + 1.5, nextTick = timeNow();
        while (timeNow() < end && !(v.state() == ConnState::Connected && v.clientId() >= 0)) {
            s.frame();
            uint8_t buf[2048]; NetAddr from; int n;
            while ((n = front.recv(from, buf, sizeof buf)) > 0) { victim = from; back.send(real, buf, n); }
            while ((n = back.recv(from, buf, sizeof buf)) > 0) if (victim.port) front.send(victim, buf, n);
            v.poll();
            while (timeNow() >= nextTick) { v.tick(idle(0)); nextTick += v.tickPeriod(); }
            sleepMs(1);
        }
        int id = v.clientId();
        CHECK(v.state() == ConnState::Connected && id >= 0);
        if (id < 0) { s.stop(); continue; }
        if (pass == 0) {
            CHECK(s.players_[id].rankId.empty());   // joined, but not as the victim
            bool logged = false;
            for (auto& l : logs) logged |= l.find("is for " + relayAddr + ", not this server") != std::string::npos;
            CHECK(logged);
        } else CHECK(s.players_[id].rankId == kv.id && s.players_[id].dialled.toString() == relayAddr);
        // which addresses count as this server's
        const NetAddr lo{0x7F000001u, port}, outside{0xC6336401u, 5000};   // (198.51.100.1)
        CHECK(s.ownAddress(lo, NetAddr{0x7F000001u, 5000}));                       // loopback, from this machine
        CHECK(!s.ownAddress(lo, outside));                                          // loopback named from elsewhere: a relay on their machine
        CHECK(!s.ownAddress(NetAddr{0x7F000001u, (uint16_t)(port + 1)}, NetAddr{0x7F000001u, 5000}));   // another port
        CHECK(!s.ownAddress(NetAddr{0xC6336402u, port}, outside));                  // someone else's address
        for (uint32_t ip : localIpv4Addresses())
            if ((ip >> 24) != 127) CHECK(s.ownAddress(NetAddr{ip, port}, outside));   // an interface's (LAN) address
        s.publicIp_ = std::make_shared<Server::PublicIpLookup>();
        s.publicIp_->ip = 0xC6336402u;                                              // our public IP behind a NAT
        CHECK(s.ownAddress(NetAddr{0xC6336402u, port}, outside));
        v.disconnect();
        s.stop();
    }
}

// REVIEW 2.1: every way a live ranked player is dropped counts as leaving, not just the grace running out: a new
// session from their address without the token or key (the dodge), and the unacked-event cap. A reconnect within
// the grace doesn't count; a leaver who comes back watches (not counted twice).
static void testRankedLeaverPaths(uint16_t port) {
    printf("ranked: every drop path counts as leaving\n");
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)qrand());
    Server s;
    ServerConfig cfg;
    cfg.map = "gantry"; cfg.mode = MODE_FFA; cfg.port = port; cfg.ranked = true; cfg.arenaVariant = 5;   // instagib FFA: goes on after a leaver
    CHECK(rankedMode(5) == MODE_FFA);
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient c[3];
    RankKey k[3];
    std::vector<NetClient*> cls;
    std::string addr = "127.0.0.1:" + std::to_string(port);
    for (int i = 0; i < 3; i++) {
        k[i] = rankKeyFromSecret(sha256Hex("paths" + std::to_string(i) + tag));
        c[i].setRankKey(k[i]);
        c[i].connect(addr, "p" + std::to_string(i));
        cls.push_back(&c[i]);
    }
    pump(s, cls, 1.0, idle);
    int id[3];
    for (int i = 0; i < 3; i++) { id[i] = c[i].clientId(); CHECK(id[i] >= 0 && s.players_[id[i]].rankId == k[i].id); }
    s.command("forcestart");
    for (int i = 0; i < 60 && s.ms_.phase != PHASE_LIVE; i++) pump(s, cls, 0.1, idle);
    CHECK(s.ms_.phase == PHASE_LIVE && s.rankedLive());
    if (!s.rankedLive()) { s.stop(); return; }
    // dropped and back within the grace: restored, not a leaver
    c[2].disconnect();
    pump(s, cls, 0.3, idle);
    CHECK(s.away(id[2]));
    c[2].connect(addr, "p2");
    pump(s, cls, 1.0, idle);
    CHECK(c[2].clientId() == id[2] && !s.away(id[2]) && s.rankedLeft_.empty());
    // the dodge: a new session from p2's address with a new nonce, no token and no key takes the address over
    UdpSocket raw;
    CHECK(raw.open(0));
    s.players_[id[2]].addr = NetAddr{0x7F000001u, raw.localPort()};   // (p2's connection, now on this socket)
    NetAddr srv{0x7F000001u, port};
    auto connectPkt = [&](uint32_t cookie) {
        uint8_t b[128];
        Writer w(b, sizeof b);
        writeHeader(w, PKT_CONNECT); w.u16(kProtocolVersion); w.u32(0x5151); w.u32(cookie); w.str("p2 again");
        raw.send(srv, b, w.size());
    };
    connectPkt(0);
    uint32_t cookie = 0;
    for (int t = 0; t < 100 && !cookie; t++) {
        pump(s, cls, 0.01, idle);
        uint8_t buf[2048]; NetAddr from; int n;
        while ((n = raw.recv(from, buf, sizeof buf)) > 0) {
            Reader r(buf, n);
            if (r.u32() == kProtocolMagic && r.u8() == PKT_CHALLENGE) { r.u32(); cookie = r.u32(); }
        }
    }
    CHECK(cookie != 0);
    connectPkt(cookie);
    pump(s, cls, 0.3, idle);
    CHECK(!(s.players_[id[2]].connected && s.players_[id[2]].rankId == k[2].id));   // the old session is gone
    CHECK(s.rankedLeft_.size() == 1 && s.rankedLeft_[0].rankId == k[2].id);         // ... and it counted as leaving
    CHECK(s.rankedLive());                                                          // (FFA: two still play)
    // the leaver comes back with the key: watches, isn't counted again
    c[2].connect(addr, "p2");
    pump(s, cls, 1.0, idle);
    int back = c[2].clientId();
    CHECK(back >= 0 && back != id[2] && s.players_[back].rankId == k[2].id && s.players_[back].team == TEAM_SPECTATOR);
    CHECK(!s.isRankedParticipant(back) && s.rankedLeft_.size() == 1);
    // the unacked-event cap: p1 stops acknowledging (its backlog is filled), the next event drops it, a leaver too
    std::vector<NetClient*> two = {&c[0], &c[2]};
    GameEvent filler; filler.type = EV_MESSAGE; filler.text = "x";
    while (s.players_[id[1]].events.size() < 256) s.players_[id[1]].events.push_back(filler);
    s.message("over the cap");
    CHECK(s.players_[id[1]].connected);   // (dropped at the end of the frame, not inside the broadcast)
    std::string ranked;
    for (int t = 0; t < 20 && ranked.empty(); t++) {
        pump(s, two, 0.1, idle);
        for (auto& e : c[0].takeEvents()) if (e.type == EV_RANKED) ranked = e.text;
    }
    CHECK(!s.players_[id[1]].connected && s.rankedLeft_.size() == 2 && s.rankedLeft_[1].rankId == k[1].id);
    CHECK(s.ms_.phase == PHASE_MATCH_END && !s.rankedLive());   // one player left in it: over
    printf("  result: %s\n", ranked.c_str());
    CHECK(ranked.find(" left=" + std::to_string(id[2]) + "," + std::to_string(id[1])) != std::string::npos);
    {   // "match ladder slot:team:place,..." : the three players once each (the returned leaver and the dodger's session aren't in it)
        std::istringstream rs(ranked);
        std::string match, ladder, list;
        rs >> match >> ladder >> list;
        std::vector<int> slots;
        std::istringstream ls(list);
        for (std::string e; std::getline(ls, e, ',');) slots.push_back(atoi(e.c_str()));
        std::sort(slots.begin(), slots.end());
        std::vector<int> want = {id[0], id[1], id[2]};
        std::sort(want.begin(), want.end());
        CHECK(slots == want);
    }
    s.stop();
}

// REVIEW 4.1: "kick" takes a name (what the console completes) or an id; atoi("Bob") was 0 and kicked the host.
static void testKickByName(uint16_t port) {
    printf("kick by name or id\n");
    {   // resolving: id (a connected slot), exact name in any case, a unique prefix; else none
        Server s;
        const char* names[] = {"Host", "Bob", "bobby", "Alice Smith", "7"};
        for (int i = 0; i < 5; i++) { s.players_[i].connected = true; s.players_[i].name = names[i]; }
        std::string e;
        CHECK(s.playerFromArg("1", &e) == 1 && s.playerFromArg("4", &e) == 4);
        CHECK(s.playerFromArg("Bob", &e) == 1 && s.playerFromArg("BOB", &e) == 1);   // exact beats the prefix of "bobby"
        CHECK(s.playerFromArg("bobb", &e) == 2 && s.playerFromArg("al", &e) == 3 && s.playerFromArg("alice smith", &e) == 3);
        CHECK(s.playerFromArg("7", &e) == 4);                                        // slot 7 isn't connected: the name "7"
        e.clear(); CHECK(s.playerFromArg("b", &e) == -1 && e.find("more than one") != std::string::npos);
        e.clear(); CHECK(s.playerFromArg("zed", &e) == -1 && e.find("no player") != std::string::npos);
        e.clear(); CHECK(s.playerFromArg("", &e) == -1 && !e.empty());
        CHECK(s.playerFromArg("12", &e) == -1 && s.playerFromArg("0x1", &e) == -1);
        for (auto& p : s.players_) p.connected = false;
    }
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port; cfg.balanceBots = false;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient h, b, c;
    std::string addr = "127.0.0.1:" + std::to_string(port);
    h.connect(addr, "Host");
    std::vector<NetClient*> cls = {&h};
    pump(s, cls, 0.5, idle);
    b.connect(addr, "Bob"); c.connect(addr, "Carol");
    cls = {&h, &b, &c};
    pump(s, cls, 0.8, idle);
    int ih = h.clientId(), ib = b.clientId(), ic = c.clientId();
    CHECK(ih == 0 && ib > 0 && ic > 0);
    s.command("kick Bob ");   // (completion leaves a space after the name)
    pump(s, cls, 0.2, idle);
    CHECK(s.players_[ih].connected && !s.players_[ib].connected && s.players_[ic].connected);
    s.command("kick nobody");
    s.command("kick " + std::to_string(ic));
    pump(s, cls, 0.2, idle);
    CHECK(s.players_[ih].connected && !s.players_[ic].connected);
    s.stop();
}

// REVIEW 4.2: addbot [a|b] [skill], the skill as 0-3 or its name (the console completes a / b, then the names);
// "/addbot EXPERT" added a default-skill bot.
static void testAddbotArgs(uint16_t port) {
    printf("addbot team and skill\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_KOTH; cfg.port = port; cfg.balanceBots = false; cfg.botSkill = 1;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    auto added = [&](const std::string& line, int team, int skill) {
        int before = s.playerCount();
        s.command(line);
        int id = -1;
        for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].isBot) id = i;   // (the newest: highest free slot so far)
        bool ok = s.playerCount() == before + 1 && id >= 0 && s.players_[id].team == team && s.bots_[id].skill == skill;
        if (!ok) printf("  '%s': players %d -> %d, team %d, skill %d\n", line.c_str(), before, s.playerCount(), id >= 0 ? s.players_[id].team : -1, id >= 0 ? s.bots_[id].skill : -1);
        return ok;
    };
    CHECK(added("addbot", TEAM_A, 1));
    CHECK(added("addbot b EXPERT", TEAM_B, 3));
    CHECK(added("addbot a beginner", TEAM_A, 0));
    CHECK(added("addbot b 2", TEAM_B, 2));
    CHECK(added("addbot Advanced", TEAM_A, 2));   // (a skill alone: team a)
    int n = s.playerCount();
    s.command("addbot b nonsense");
    CHECK(s.playerCount() == n);
    s.stop();
}

// Every game: a dropped player keeps slot, team and score for the grace (found again by the reconnect token), and
// is gone after it; votes don't count players who are away.
static void testReconnectGrace(uint16_t port) {
    printf("reconnect grace (token)\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_VS; cfg.port = port; cfg.balanceBots = true;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    std::string addr = "127.0.0.1:" + std::to_string(port);
    a.connect(addr, "a"); b.connect(addr, "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 1.0, idle);
    int ib = b.clientId(), teamB = s.players_[ib].team;
    s.players_[ib].kills = 7;
    CHECK(s.players_[ib].reconnectToken != 0);   // (given in PKT_ACCEPT: what the reconnect below shows)
    b.disconnect();
    pump(s, cls, 0.3, idle);
    CHECK(s.away(ib) && s.humanCount() == 2);
    a.sendCommand("callvote bots off");   // b is away: a alone decides
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE && !s.config().balanceBots);
    b.connect(addr, "b");
    pump(s, cls, 1.0, idle);
    CHECK(b.state() == ConnState::Connected && b.clientId() == ib && s.players_[ib].kills == 7 && s.players_[ib].team == teamB && !s.away(ib));
    CHECK(b.hasSnapshot() && b.playerName(a.clientId()) == "a");   // a new session: it got everyone's info again
    // without the token (a stranger): a new slot, b's place stays b's until the grace is over
    b.disconnect();
    pump(s, cls, 0.3, idle);
    NetClient c;
    c.connect(addr, "c");
    std::vector<NetClient*> cc = {&a, &c};
    pump(s, cc, 1.0, idle);
    CHECK(c.clientId() != ib && s.away(ib));
    pump(s, cc, kReconnectGrace + 0.5, idle);
    CHECK(!s.players_[ib].connected && s.humanCount() == 2);
    s.stop();
}

// Tourney seeding by the average of the three ladder ratings (200 for a ladder without games or no ranked id).
static void testTourneySeeding(uint16_t port) {
    printf("tourney seeding\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "colosseum"; cfg.mode = MODE_TOURNEY; cfg.port = port; cfg.lanOnly = true;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    const int ratings[5] = {150, 900, -1, 400, 650};   // -1: no ranked id
    std::vector<int> entrants;
    for (int i = 0; i < 5; i++) {
        auto& p = s.players_[i];
        p.connected = true; p.isBot = false; p.team = TEAM_A; p.name = "p" + std::to_string(i);
        if (ratings[i] >= 0) { p.rankId = std::string(31, 'a') + (char)('0' + i); s.seedFixed_[p.rankId] = ratings[i]; }
        entrants.push_back(i);
    }
    CHECK(s.seedRating(2) == kUnratedSeed);
    std::vector<int> first = s.tourneySeeding(entrants);
    // seeds: 1 (900), 4 (650), 3 (400), 2 (200, no id), 0 (150) -> 8 slots, the top three get byes
    CHECK((first == std::vector<int>{1, -1, 2, 0, 4, -1, 3, -1}));
    for (auto& p : s.players_) p.connected = false;
    s.stop();
}

// REVIEW 3.6: POST /hello is retried (with backoff, on the ranked thread) until the Worker answers "id <id>", then
// never again. A tiny local HTTP server refuses the first one. (POSIX sockets: Linux / macOS only.)
static void testHelloRetry() {
    printf("ranked: /hello retried until it takes\n");
    CHECK(RankService::helloRetryDelay(1) == 10 && RankService::helloRetryDelay(2) == 20 && RankService::helloRetryDelay(8) == 900 &&
          RankService::helloRetryDelay(50) == 900);
#ifndef _WIN32
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    socklen_t al = sizeof a;
    if (ls < 0 || bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, 4) != 0 || getsockname(ls, (sockaddr*)&a, &al) != 0) {
        printf("  FAIL %s:%d: no local listener\n", __FILE__, __LINE__); g_fail++; if (ls >= 0) close(ls); return;
    }
    RankKey k = rankKeyFromSecret(sha256Hex("hello" + std::to_string(qrand())));
    std::atomic<int> requests{0};
    std::atomic<bool> quit{false};
    std::thread srv([&] {
        while (!quit) {
            fd_set fs; FD_ZERO(&fs); FD_SET(ls, &fs);
            timeval tv{0, 50000};
            if (select(ls + 1, &fs, nullptr, nullptr, &tv) <= 0) continue;
            int c = accept(ls, nullptr, nullptr);
            if (c < 0) continue;
            std::string req;
            char buf[4096];
            for (;;) {   // headers, then the form body (Content-Length)
                size_t h = req.find("\r\n\r\n");
                if (h != std::string::npos) {
                    size_t cl = req.find("Content-Length: ");
                    size_t len = cl == std::string::npos ? 0 : (size_t)atoi(req.c_str() + cl + 16);
                    if (req.size() >= h + 4 + len) break;
                }
                ssize_t n = recv(c, buf, sizeof buf, 0);
                if (n <= 0) break;
                req.append(buf, (size_t)n);
            }
            int nth = ++requests;
            bool ok = req.rfind("POST /hello ", 0) == 0 && req.find("secret=" + k.secret) != std::string::npos;
            std::string body = nth == 1 || !ok ? "busy" : "id " + k.id + "\n";
            std::string resp = std::string(nth == 1 || !ok ? "HTTP/1.1 503 Busy" : "HTTP/1.1 200 OK") + "\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            send(c, resp.data(), resp.size(), 0);
            close(c);
        }
    });
    const char* prev = getenv("QUARK_MASTER");
    std::string was = prev ? prev : "off";
    setenv("QUARK_MASTER", ("http://127.0.0.1:" + std::to_string(ntohs(a.sin_port))).c_str(), 1);
    {
        RankService rs;
        rs.hello(k);
        // (the retry comes 10 s later, real time: RankService delays are real seconds; a loaded CI box can be slow to start curl)
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (std::chrono::steady_clock::now() < end && !rs.helloDone()) sleepMs(20);
        if (!rs.helloDone() || requests != 2) printf("  hello: done %d after %d requests\n", (int)rs.helloDone(), requests.load());
        CHECK(rs.helloDone() && requests == 2);   // refused once, then taken
        double settle = timeNow() + 15;
        while (timeNow() < settle) sleepMs(20);
        CHECK(requests == 2);                      // and not sent again
    }
    setenv("QUARK_MASTER", was.c_str(), 1);
    quit = true;
    srv.join();
    close(ls);
#endif
}

// master.txt can move the Worker only with a release-key signature of "quark-master-v1\n" + exactly that file.
static void testMasterSignature() {
    printf("signed master.txt\n");
    std::string priv, pub, sig, url;
    CHECK(sign::keygen(&priv, &pub));
    std::string body = "https://quark-master.example.workers.dev/\n";
    CHECK(sign::signData(sign::kMasterSignPrefix + body, priv, &sig));
    CHECK(signedMasterUrl(body, sig, pub, &url) && url == "https://quark-master.example.workers.dev");
    // domain separation: a signature of the bare file (what `quark_sign sign` makes for packages and manifests) is
    // not a master.txt signature
    std::string bare;
    CHECK(sign::signData(body, priv, &bare) && !signedMasterUrl(body, bare, pub, &url));
    CHECK(std::string(sign::kMasterSignPrefix) == "quark-master-v1\n");
    CHECK(!signedMasterUrl("https://evil.example.dev\n", sig, pub, &url));                 // another body
    CHECK(!signedMasterUrl(body, sig, sign::kReleasePublicKey, &url));                     // not the release key
    CHECK(!signedMasterUrl(body, "", pub, &url));
    std::string sig2;
    CHECK(sign::signData(sign::kMasterSignPrefix + std::string("http://plain.example\n"), priv, &sig2) &&
          !signedMasterUrl("http://plain.example\n", sig2, pub, &url)); // https only
}

// REVIEW 2.3: a dedicated server (src/server/main.cpp: frame() and idle(), never run()) handles ranking server
// replies: a failed race ticket clears and the racer is told. And replies nobody takes are capped.
static void testMasterReplies(uint16_t port) {
    printf("ranking server replies on a dedicated server\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "viaduct"; cfg.mode = MODE_RACE; cfg.port = port;
    cfg.timeTrial = true; cfg.vsBots = true; cfg.maxPlayers = 1; cfg.botsEnemy = cfg.botsAlly = 0;
    std::vector<std::string> logs;
    std::mutex logMu;
    s.setLog([&](const std::string& l) { std::lock_guard<std::mutex> g(logMu); logs.push_back(l); });
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a;
    a.setRankKey(rankKeyFromSecret(sha256Hex("replies" + std::to_string(qrand()))));
    a.connect("127.0.0.1:" + std::to_string(port), "racer");
    auto dedicated = [&](double secs, const std::function<bool()>& until) {   // main.cpp's loop (and the client's)
        double end = timeNow() + secs, nextTick = timeNow();
        while (timeNow() < end && !until()) {
            double wait = s.frame();
            a.poll();
            while (timeNow() >= nextTick) { a.tick(UserCmd()); nextTick += a.tickPeriod(); }
            s.idle(std::min(wait, 0.002));
        }
    };
    dedicated(1.0, [&] { return a.state() == ConnState::Connected && a.hasSnapshot(); });
    CHECK(a.state() == ConnState::Connected);
    s.openRaceTicket();   // (what the race going live does; QUARK_MASTER=off in tests: the request fails at once)
    CHECK(!s.raceTicket_.empty());
    std::string told;
    dedicated(5.0, [&] {
        for (auto& e : a.takeEvents()) if (e.type == EV_MESSAGE && e.text.find("no race ticket") != std::string::npos) told = e.text;
        return s.raceTicket_.empty() && !told.empty();
    });
    CHECK(s.raceTicket_.empty() && !told.empty());
    {
        std::lock_guard<std::mutex> g(logMu);
        bool logged = false;
        for (auto& l : logs) logged |= l.find("ranking server (raceticket)") != std::string::npos;
        CHECK(logged);
    }
    s.stop();
    // nobody taking the replies: the oldest go, at most kMaxReplies are kept
    MasterAnnouncer m;
    m.start(0, false);
    for (int batch = 0; batch < 2; batch++) {
        for (int i = 0; i < 40; i++) m.post("k" + std::to_string(batch * 40 + i), "/x", "a=1");
        sleepMs(1000);
    }
    auto r = m.takeReplies();
    m.stop();
    printf("  %zu replies kept of 80\n", r.size());
    CHECK(!r.empty() && r.size() <= MasterAnnouncer::kMaxReplies);
    if (!r.empty() && r.back().first == "k79") CHECK(r.size() == MasterAnnouncer::kMaxReplies && r.front().first == "k16");   // (all done: the newest 64)
}

// One placement threshold: the game's kPlacementGames is the Worker's PLACEMENT_GAMES.
static void testPlacementConstant() {
    printf("placement threshold matches the Worker\n");
    std::string js = readSource("tools/master/worker.js");
    if (js.empty()) { printf("  (worker.js not found: skipped)\n"); return; }
    size_t at = js.find("const PLACEMENT_GAMES = ");
    CHECK(at != std::string::npos && atoi(js.c_str() + at + 24) == kPlacementGames);
}

// Voting ranked on / off, what ranked forbids, and the update restart flag (waits for the match to end).
static void testRankedVoteAndRestart(uint16_t port) {
    printf("ranked vote, restart for updates\n");
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)qrand());
    std::string flag = userDataPath() + "/zz_update" + tag + ".pending";
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
    // (the switch loads maps on the tick: give it time on slow machines instead of a fixed wait)
    for (int i = 0; i < 40 && !(a.hasSnapshot() && (a.mode().flags & MS_RANKED)); i++) pump(s, cls, 0.1, idle);
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
    a.sendCommand("callvote ranked off");       // refused: the ranked match is live (OWNER 3.4)
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE && s.rankedLive() && s.config().ranked);
    std::string mapBefore = s.config().map;
    s.command("map harbor ffa");                // not even the host: it waits until the match is over
    s.command("ranked off");
    CHECK(s.config().map == mapBefore && s.config().ranked);
    rulesEndMatch(s, "test: match over", -1);   // over: votes and changes are allowed again
    CHECK(!s.rankedLive());
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
    snprintf(tag, sizeof tag, "%u", (unsigned)qrand());
    std::string name = std::string("zz_edtest") + tag, path = userMapsDir() + "/" + name + ".qmap";
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
    // a custom group the size the editor sends (one op per part): in place on every side, nobody re-serializes,
    // and it gets through in well under a second (it used to go at 2 ops a tick)
    {
        size_t groupsBefore = s.map_.groups.size();
        b.sendCommand("ed g ins " + std::to_string(groupsBefore) + " 0 1 0 -6 0 -6 6 3 6");
        for (int k = 0; k < 200; k++) {
            char op[96];
            snprintf(op, sizeof op, "ed g part %zu %d %g 0 0 %g 0.5 0.1", groupsBefore, k % 4, (k % 20) * 0.05, (k % 20) * 0.05 + 0.04);
            b.sendCommand(op);
        }
        double t0 = timeNow();
        bool done = false;
        while (!done && timeNow() - t0 < 3.0) {
            pump(s, cls, 0.02, idle);
            done = s.map_.groups.size() == groupsBefore + 1 && s.map_.groups.back().parts.size() == 200 && b.pendingCommands() == 0;
        }
        printf("  200-part group: %.0f ms\n", (timeNow() - t0) * 1000);
        CHECK(done);
        pump(s, cls, 0.3, idle);
        CHECK(a.map().crc == s.map_.crc && b.map().crc == s.map_.crc && c.map().crc == s.map_.crc);
        CHECK(c.map().brushes.size() == s.map_.brushes.size() && c.map().gridRefs == s.map_.gridRefs);
        b.sendCommand("ed g del " + std::to_string(groupsBefore));   // (back as it was for the checks below)
        pump(s, cls, 0.3, idle);
        CHECK(s.map_.groups.size() == groupsBefore && c.map().crc == s.map_.crc);
    }
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
    // the host names the map in the session (the editor's SAVE AS): saved, and autosaved, under the typed name
    std::string name2 = name + "_renamed", path2 = userMapsDir() + "/" + name2 + ".qmap";
    s.command("editmode saveas ../evil");
    CHECK(s.cfg_.map == name);
    s.command("editmode saveas " + name2);
    Map renamed;
    CHECK(s.cfg_.map == name2 && renamed.load(name2) && renamed.crc == s.map_.sourceTextCrc() && renamed.brushes.size() == nb);
    a.sendCommand("ed t Renamed");
    pump(s, cls, 0.5, idle);
    s.stop(); // autosaves
    CHECK(renamed.load(name2) && renamed.title == "Renamed" && renamed.brushes.size() == nb);
    CHECK(saved.load(name) && saved.title == "Edited Arena"); // the old file keeps its last save
    {   // the first autosave kept the file as it was before the session (REVIEW NET-15)
        std::ifstream bf(path + ".bak", std::ios::binary);
        std::stringstream bs;
        bs << bf.rdbuf();
        Map bak;
        CHECK(bak.parse(bs.str()) && bak.brushes.size() == base.brushes.size());
    }
    std::remove(path.c_str());
    std::remove((path + ".bak").c_str());
    std::remove(path2.c_str());
    for (uint32_t crc : {a.map().crc, c.map().crc}) std::remove(downloadedMapPath(name, crc).c_str());
}

// Demos: record a match from one client, play it back into another: the map, players, names and events come back.
// Ids from a server or a demo index fixed kMaxPlayers arrays on the client (REVIEW CLI-1: a snapshot player id of 33
// wrote past a 32-entry stack array every frame). The decoder rejects them, and the client survives the review's demo.
// A test demo recorded by an older build, stamped with this build's protocol version (the bytes after it are what's
// being tested, not the version check).
static std::string stampedDemo(const std::string& name) {
    std::ifstream f(testDataDir() + "/" + name, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    std::string d = ss.str();
    if (d.size() >= 6) { uint16_t v = kProtocolVersion; memcpy(&d[4], &v, 2); }
    std::string out = (std::filesystem::temp_directory_path() / ("quark_" + std::to_string(getpid()) + "_" + name)).string();
    std::ofstream(out, std::ios::binary) << d;
    return out;
}

static void testSnapshotBounds() {
    printf("snapshot decode: ids in range\n");
    auto decodes = [](Snapshot s) {
        std::vector<uint8_t> b(64 * 1024);
        Writer w(b.data(), (int)b.size());
        writeSnapshotClient(w, s); writeSnapshotWorld(w, s, nullptr); writeSnapshotEvents(w, s.events);
        Reader r(b.data(), w.size());
        Snapshot out;
        return readSnapshotClient(r, out) && readSnapshotWorld(r, out, nullptr) && readSnapshotEvents(r, out);
    };
    Snapshot ok;
    ok.clientId = kMaxPlayers - 1;
    for (int id : {0, kMaxPlayers - 1}) { PlayerSnap p; p.id = (uint8_t)id; ok.players.push_back(p); }
    ProjectileSnap pr; pr.id = 1; pr.owner = kMaxPlayers - 1; ok.projectiles.push_back(pr);
    CHECK(decodes(ok));
    { Snapshot s = ok; s.players[1].id = 33; CHECK(!decodes(s)); }
    { Snapshot s = ok; s.players[1].id = 0; CHECK(!decodes(s)); }               // the same id twice
    { Snapshot s = ok; s.players[0].weapon = WPN_COUNT; CHECK(!decodes(s)); }
    { Snapshot s = ok; s.projectiles[0].owner = kMaxPlayers; CHECK(!decodes(s)); }
    { Snapshot s = ok; s.clientId = kMaxPlayers; CHECK(!decodes(s)); }
    { Snapshot s = ok; s.selfWeapon = WPN_COUNT; CHECK(!decodes(s)); }
    { Snapshot s = ok; s.ms.mode = MODE_COUNT; CHECK(!decodes(s)); }
    // the review's demo: a map, then snapshots with player id 33
    NetClient nc;
    std::string err;
    CHECK(nc.startPlayback(stampedDemo("demo_oob_id33.qdemo"), err));
    for (int k = 0; k < 10 && nc.playback(); k++) { nc.poll(); nc.interpolatedPlayers(); }
    CHECK(!nc.hasSnapshot());   // every snapshot in it is out of range
    // tourney bracket text: ids, the round and the match are all used as indices
    std::vector<std::vector<int>> rounds;
    int round = 0, match = 0;
    CHECK(parseBracket("0|1|31|99\n-2|-7\n@-5,999999", rounds, round, match) && rounds.size() == 2);
    CHECK(rounds[0] == std::vector<int>({0, 1, 31, -2}) && rounds[1] == std::vector<int>({-2, -2}) && round == 0 && match == 2 * kMaxPlayers);
    CHECK(parseBracket("0|1\n@7,0", rounds, round, match) && round == 1);
    CHECK(!parseBracket("1\n1\n1\n1\n1\n1\n1\n1\n1\n@0,0", rounds, round, match) && rounds.empty());
}

// After a client hitch, the burst of commands it sends to catch up stayed queued on the server for good: tokens refill
// at exactly the command rate, so +12 ms of input latency at 1000 Hz (+96 ms at 125 Hz) until something else cleared
// it (REVIEW PERF-1). A lasting backlog now drains a command a tick from a small allowance; a client sending faster
// than real time still gets no more than real time out of it.
static void testInputBacklog(uint16_t port) {
    printf("input backlog drains; a speedhack still doesn't pay\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 1.0, idle);
    int id = a.clientId();
    CHECK(a.hasSnapshot() && id >= 0 && id < kMaxPlayers);
    if (!a.hasSnapshot() || id < 0 || id >= kMaxPlayers) { s.stop(); return; }
    // a 150 ms stall (the network, or the server's machine) while the client keeps playing: its commands then arrive
    // all at once, more than the token burst covers
    double until = timeNow() + 0.15, next = timeNow();
    while (timeNow() < until) { a.poll(); while (timeNow() >= next) { a.tick(UserCmd()); next += a.tickPeriod(); } sleepMs(1); }
    s.frame();
    size_t after = s.players_[id].cmdQueue.size();
    // then normal play: over the last 3/4 of a second the queue empties again. (Before, it never went below 3: the
    // backlog stayed for good. Its average depth also depends on how bursty the client's sends are, i.e. machine load,
    // so the test looks at the lowest.)
    size_t lowest = 99;
    double sum = 0;
    int samples = 0;
    for (double stop = timeNow() + 1.0, first = timeNow() + 0.25; timeNow() < stop;) {
        s.frame(); a.poll();
        while (timeNow() >= next) { a.tick(UserCmd()); next += a.tickPeriod(); }
        if (timeNow() >= first) { size_t q = s.players_[id].cmdQueue.size(); lowest = std::min(lowest, q); sum += (double)q; samples++; }
        sleepMs(1);
    }
    printf("  queued after the stall %zu, later at least %zu (%.2f on average)\n", after, lowest, samples ? sum / samples : 0.0);
#ifdef QUARK_SANITIZED   // (the CI sanitizers job: several times slower, so how fast the queue drains isn't telling)
    if (!(after > 2 && lowest <= 1)) printf("  (sanitized build: drain timing not checked)\n");
#else
    CHECK(after > 2 && lowest <= 1);
#endif
    // twice real time: the server runs about real time's worth (plus its token burst and the catch-up allowance)
    uint32_t t0 = s.tick_, c0 = s.players_[id].cmdsRun;
    double end = timeNow() + 1.0;
    next = timeNow();
    while (timeNow() < end) {
        s.frame(); a.poll();
        while (timeNow() >= next) { a.tick(UserCmd()); a.tick(UserCmd()); next += a.tickPeriod(); }
        sleepMs(1);
    }
    double ran = (double)(s.players_[id].cmdsRun - c0), ticks = (double)(s.tick_ - t0);
    printf("  speedhack: %.0f commands run in %.0f ticks\n", ran, ticks);
    CHECK(ran <= ticks * 1.05 + 60);
    s.stop();
}

// Chat and votes can't flood everyone's reliable events or stall the tick (REVIEW NET-7, NET-6): chat is rate
// limited and printable only, votes check maps from a cache (no file parsing on the tick), a failed vote's caller
// waits, and string commands over the per-tick budget are handled later, not lost.
static void testChatAndVoteLimits(uint16_t port) {
    printf("chat and vote limits\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 1.0, idle);
    b.takeEvents();
    int ia = a.clientId();
    CHECK(ia >= 0 && ia < kMaxPlayers);
    if (ia < 0 || ia >= kMaxPlayers) { s.stop(); return; }
    for (int k = 0; k < 30; k++) a.sendCommand("say spam " + std::to_string(k) + " \x1b[2J\x07");
    pump(s, cls, 1.0, idle);
    int chats = 0;
    bool clean = true;
    for (auto& e : b.takeEvents()) if (e.type == EV_CHAT) { chats++; for (char c : e.text) clean &= c >= 32 && c < 127; }
    printf("  30 messages at once: %d went out\n", chats);
    CHECK(chats >= 5 && chats <= 8 && clean);   // a burst of 5, then one a second
    // tourney map lists: each map is read at most once, then comes from the cache
    a.sendCommand("callvote tmaps colosseum,lodestone,colosseum,lodestone");
    pump(s, cls, 0.3, idle);
    uint32_t loads = s.mapInfoLoads_;
    a.sendCommand("callvote tmaps lodestone,colosseum");   // (a vote is already running: refused, after the checks)
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_TMAPS && s.mapInfoLoads_ == loads);
    // made-up map names (callvote map <random>) aren't cached: the cache stays the size of the maps folder
    for (int k = 0; k < 20; k++) b.sendCommand("callvote map zz_nomap" + std::to_string(k));
    pump(s, cls, 0.5, idle);
    for (int k = 0; k < 1000; k++) CHECK(!s.mapInfo("zz_nomap_" + std::to_string(qrand())).ok);
    printf("  map facts cached after 1020 made-up names: %zu (%zu maps)\n", s.mapInfoCached(), listMaps().size());
    CHECK(s.mapInfoCached() <= listMaps().size() + 8);   // (+: test maps other groups write meanwhile)
    // the vote fails: its caller can't call another straight away
    b.sendCommand("vote no");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE);
    a.sendCommand("callvote bots off");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE);
    // more string commands than a tick's budget: all handled, a few a tick
    uint16_t before = s.players_[ia].lastStringCmd;
    for (int k = 0; k < 40; k++) a.sendCommand("nothing " + std::to_string(k));
    pump(s, cls, 0.5, idle);
    CHECK((uint16_t)(s.players_[ia].lastStringCmd - before) == 40);
    s.stop();
}

// A scripted server on a UDP socket for hostile-server tests: it answers the client's connect, then sends
// whatever the test builds. The client has to survive anything a server says.
struct FakeServer {
    UdpSocket sock;
    NetAddr client;
    uint32_t nonce = 0;
    bool heard = false;
    void poll() {   // the client's address and nonce, from its CONNECT
        uint8_t buf[2048];
        NetAddr from;
        int n;
        while ((n = sock.recv(from, buf, sizeof buf)) > 0) {
            Reader r(buf, n);
            if (r.u32() != kProtocolMagic || r.u8() != PKT_CONNECT) continue;
            r.u16();
            uint32_t nn = r.u32();
            if (!r.bad()) { nonce = nn; client = from; heard = true; }
        }
    }
    void accept(const std::string& map, uint32_t crc, int id = 0) {
        uint8_t out[512];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_ACCEPT); w.u32(nonce); w.u8((uint8_t)id); w.str(map); w.u32(crc); w.u8(MODE_FFA);
        sock.send(client, out, w.size());
    }
    void snapshot(Snapshot s) {
        std::vector<uint8_t> b(4096);
        Writer w(b.data(), (int)b.size());
        writeHeader(w, PKT_SNAPSHOT); w.u32(nonce);
        writeSnapshotClient(w, s); writeSnapshotWorld(w, s, nullptr); writeSnapshotEvents(w, s.events);
        sock.send(client, b.data(), w.size());
    }
    // pump the client (and listen) until done() or about a second of real time
    template <class F> bool until(NetClient& c, F done) {
        for (int i = 0; i < 1000 && !done(); i++) { c.poll(); poll(); sleepMs(1); }
        return done();
    }
};

// Prediction replays only what's still in the command ring: a snapshot acking a command far behind (a server or a
// demo can say anything) cost one loop iteration per sequence number since (REVIEW CLI-5).
static void testReplayBound(uint16_t port) {
    printf("prediction replay is bounded by the command ring\n");
    FakeServer f;
    CHECK(f.sock.open(port));
    NetClient c;
    c.connect("127.0.0.1:" + std::to_string(port), "x");
    CHECK(f.until(c, [&] { return f.heard; }));
    Map m;
    CHECK(m.load("bastion"));
    f.accept("bastion", m.crc);
    CHECK(f.until(c, [&] { return c.mapReady(); }));
    Snapshot s;
    s.tick = 1; s.timeMs = 1000; s.tickRate = 1000; s.snapRate = 500; s.clientId = 0;
    PlayerSnap me; me.id = 0; me.flags = PS_ALIVE; s.players.push_back(me);
    s.self.pos = m.spawns[0].pos;
    f.snapshot(s);
    CHECK(f.until(c, [&] { return c.hasSnapshot(); }));
    for (int i = 0; i < 3000; i++) { UserCmd cmd; cmd.forward = 127; c.tick(cmd); }
    s.tick = 2; s.timeMs = 1002; s.ackCmdSeq = 1; s.self.pos.y += 5;   // a mismatch: replay from command 2
    f.snapshot(s);
    CHECK(f.until(c, [&] { return c.hasSnapshot() && c.latest().tick == 2; }));
    printf("  replayed %u commands for an ack %u behind\n", c.lastReplayCount(), c.nextSeq() - 1 - s.ackCmdSeq);
    CHECK(c.lastReplayCount() > 0 && c.lastReplayCount() <= NetClient::kCmdRing);
    // 400 ms of round trip at 1000 Hz: every command since the ack is replayed (a 256 ring dropped the oldest 144, so
    // high-ping players rubber-banded; REVIEW PERF-6)
    s.tick = 3; s.timeMs = 1004; s.ackCmdSeq = c.nextSeq() - 1 - 400; s.self.pos.y += 1;
    f.snapshot(s);
    CHECK(f.until(c, [&] { return c.hasSnapshot() && c.latest().tick == 3; }));
    CHECK(c.lastReplayCount() == 400);
    c.disconnect();
    CHECK(c.nextSeq() == 1);   // a new connection or a demo starts with no old commands
}

// Map names from servers and demos become file names (map, download, light cache, demo): a name, never a path
// (REVIEW CLI-2: "../x" from PKT_ACCEPT reached the download cache; a demo's map record wasn't checked at all).
static void testMapNames(uint16_t port) {
    printf("map names: never paths\n");
    for (const char* bad : {"", "../x", "a/b", "a\\b", "C:x", "x.qmap", "con", "NUL", "com1", "Lpt9", "two words",
                            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}) {
        CHECK(!validMapName(bad));
        Map m;
        CHECK(!m.load(bad));
    }
    for (const char* good : {"bastion", "My_Map-2", "console", "com10", "a"}) CHECK(validMapName(good));
    // a server that names a path: refused before anything touches the disk
    FakeServer f;
    CHECK(f.sock.open(port));
    NetClient c;
    c.connect("127.0.0.1:" + std::to_string(port), "x");
    CHECK(f.until(c, [&] { return f.heard; }));
    f.accept("../../pwned", 1234);
    CHECK(f.until(c, [&] { return c.state() == ConnState::Disconnected; }));
    CHECK(!c.mapReady() && c.error().find("map name") != std::string::npos);
    // a demo whose map record names a path: no map, so it doesn't play
    std::string src = "box -20 -1 -20 20 0 20\nspawn any 0 0 0\n", d;
    uint32_t magic = 0x4D454451; uint16_t ver = kProtocolVersion;
    d.append((const char*)&magic, 4); d.append((const char*)&ver, 2);
    std::vector<uint8_t> b(src.size() + 64);
    Writer w(b.data(), (int)b.size());
    w.str("../../pwned"); w.u32((uint32_t)src.size()); for (char ch : src) w.u8((uint8_t)ch); w.u8(0);
    double t = 0; uint32_t n = (uint32_t)w.size();
    d.push_back(1); d.append((const char*)&t, 8); d.append((const char*)&n, 4); d.append((const char*)b.data(), n);
    std::string path = (std::filesystem::temp_directory_path() / ("quark_badname_" + std::to_string(port) + ".qdemo")).string();
    { std::ofstream o(path, std::ios::binary); o << d; }
    NetClient p;
    std::string err;
    CHECK(!p.startPlayback(path, err) && !p.mapReady());
    std::remove(path.c_str());
}

// A demo record's length near 2^32 made `13 + n` wrap to 0 and startPlayback scan forever (REVIEW CLI-4).
static void testDemoLengthWrap() {
    printf("demo: record lengths can't wrap\n");
    NetClient nc;
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    CHECK(!nc.startPlayback(stampedDemo("demo_len_wrap.qdemo"), err) && !nc.playback());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
}

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
    std::string path = userDataPath() + "/zz_test.qdemo";
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
// The client's interpolation clock is a double (FINAL_REVIEW 2.6): on a server that has been up for 5 days its render
// times aren't stuck on a float's 31 ms grid (interpolation stalled, then jumped), and the view tick it sends for lag
// compensation is the time it shows.
static void testUptimeClock(uint16_t port) {
    printf("client clock on a server up for 5 days\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    s.timeMs_ += 5u * 86400u * 1000u;
    NetClient a;
    a.setInterpFixed(0.05f);
    a.connect("127.0.0.1:" + std::to_string(port), "uptime");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 2.0, idle);
    CHECK(a.renderTime() > 5 * 86400.0);
    int samples = 0, onGrid = 0;
    double prev = 0;
    std::vector<int> lags;
    pump(s, cls, 1.0, [&](int) {
        double rt = a.renderTime(), g = rt * 32;
        if (rt != prev) { samples++; if (std::fabs(g - std::round(g)) < 1e-6) onGrid++; prev = rt; }
        // the view tick the server gets: the interpolation delay (plus the trip) behind its clock
        UserCmd c;
        uint32_t sent = s.players_[a.clientId()].lastViewTick;
        if (sent) lags.push_back((int)(s.timeMs_ - sent));
        return c;
    });
    std::sort(lags.begin(), lags.end());
    int median = lags.empty() ? -1 : lags[lags.size() / 2];
    printf("  %d of %d render times on a float's 1/32 s grid; view tick a median %d ms behind the server (interp 50)\n", onGrid, samples, median);
    CHECK(samples > 20 && onGrid * 4 < samples);
    CHECK(median >= 45 && median < 150);
    s.stop();
}

// Lag compensation covers the longest fixed interpolation delay a client can set (FINAL_REVIEW 2.5: with interp_ms 200
// the rewind was capped at ping + ~100 + 30 ms, so a rifle aimed exactly at a moving player, as drawn, hit behind him).
static void testInterpRewind(uint16_t port) {
    printf("lag compensation covers a 200 ms interpolation delay\n");
    for (bool reported : {true, false}) {
        Server s;
        ServerConfig cfg;
        cfg.autoStart = false;   // (warmup: free play, no round reset of the shot counts)
        cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = (uint16_t)(port + (reported ? 0 : 1));
        s.setLog([](const std::string&) {});
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
        NetClient a, b;
        a.setInterpFixed(kMaxInterpMs * 0.001f);
        a.connect("127.0.0.1:" + std::to_string(cfg.port), "interp200");
        b.connect("127.0.0.1:" + std::to_string(cfg.port), "mover");
        std::vector<NetClient*> cls = {&a, &b};
        pump(s, cls, 2.5, idle);
        auto& sa = s.players_[a.clientId()];
        auto& sb = s.players_[b.clientId()];
        if (!reported) sa.interpMs = 0;   // as before: the server doesn't know (the client already reported it once)
        // the mover: back and forth across the shooter's view at 10 m/s, 15 m away in the open courtyard
        auto moverAt = [](double t) { double ph = std::fmod(t, 1.0); return Vec3(-5, 0, 19 + 2.5f * (float)(ph < 0.5 ? ph * 4 - 1 : 3 - ph * 4)); };
        Vec3 shooterPos(10, 0, 19);
        auto input = [&](int i, bool fire) {
            sa.move.pos = shooterPos; sa.move.vel = {};
            sb.move.pos = moverAt(s.time()); sb.move.vel = {}; sb.health = 100;
            UserCmd c;
            c.weapon = WPN_RIFLE;
            if (i != 0) return c;
            NetClient::PlayerArray ps = a.interpolatedPlayers();   // where the shooter sees the mover
            const InterpPlayer& ip = ps[b.clientId()];
            if (!ip.valid) return c;
            Vec3 d = normalize(ip.snap.pos + Vec3(0, 1.0f, 0) - (shooterPos + Vec3(0, kEyeHeight, 0)));
            c.pitch = std::asin(d.y); c.yaw = std::atan2(-d.x, -d.z);
            if (fire) c.buttons = BTN_FIRE;
            return c;
        };
        pump(s, cls, 0.6, [&](int i) { return input(i, false); });
        int fired0 = sa.shotsFired, hit0 = sa.shotsHit;
        if (reported) CHECK(sa.interpMs == kMaxInterpMs);
        pump(s, cls, 2.5, [&](int i) { return input(i, true); });
        int fired = sa.shotsFired - fired0, hit = sa.shotsHit - hit0;
        printf("  interp 200 %s: %d of %d rifle shots hit a player moving at 10 m/s\n", reported ? "reported" : "unknown to the server", hit, fired);
        if (reported) CHECK(fired >= 8 && hit * 10 >= fired * 8);
        s.stop();
    }
}

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
        CHECK(bots == 1);                                  // second human evens the teams, but a bot alive in this round stays (GAME-10)...
        for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].isBot) s.killPlayer(i, -1, 254);
        pump(s, cls, 1.5, idle);
        bots = 0;
        for (auto& p : s.players_) bots += p.connected && p.isBot;
        CHECK(bots == 0);                                  // ...until it's dead
        b.disconnect();
        cls.pop_back();
        pump(s, cls, kReconnectGrace + 1.5, idle);   // (gone once the reconnect grace is over)
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
        for (int i = 0; i < 15 && s.ms_.phase != PHASE_LIVE; i++) pump(s, cls, 0.2, idle);   // (a slow host's clock lags)
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
    { // the hand-written line reader reads exactly what std::istringstream read (checked against it on 60k fuzzed maps)
        auto one = [&](const std::string& line, Map& m) { return m.parse(base + line + "\n"); };
        Map m;
        CHECK(one("box 1.2.3 0 0 1 1", m) && m.brushes[1].box.mn.y == 0.3f && m.brushes[1].box.mx.x == 1.2f);   // "1.2" then ".3"
        CHECK(one("box 0 0 0 1 1 1abc", m));     // trailing junk after the last number: ignored, as before
        CHECK(!one("box 1e 0 0 1 1 1", m) && !one("box 0x10 0 0 1 1 1", m) && !one("box -+1 0 0 1 1 1", m) && !one("box 1.5abc 0 0 1 1 1", m));
        CHECK(one("box .5 -.5 +.5 1. 1e1 1E+1", m) && m.brushes[1].box.mn.y == -0.5f && m.brushes[1].box.mx.y == 10.0f);
        CHECK(one("box 0.1 1234.567 -8.25 3402.8234 1e-30 0.000000000012345", m) && m.brushes[1].box.mn.x == strtof("0.1", nullptr) &&
              m.brushes[1].box.mx.y == strtof("1234.567", nullptr) && m.brushes[1].box.mn.z == -8.25f && m.brushes[1].box.mx.x == strtof("3402.8234", nullptr) &&
              m.brushes[1].box.mn.y == strtof("1e-30", nullptr) && m.brushes[1].box.mx.z == strtof("0.000000000012345", nullptr));
        CHECK(one("spawn a 0 0 0 xyz", m) && m.spawns[1].yaw == 0);     // a bad optional yaw reads as 0
        CHECK(one("light 0 2 0 5", m) && m.lights[0].intensity == 1);   // a missing one keeps its default
        CHECK(!one("light 0 2 0 5 x", m));
        CHECK(one("group custom 1 0 0 0 0 4 4 4\npart +2 0 0 0 1 1 1", m) && m.groups[0].parts[0].material == 2);
        CHECK(!one("group custom 99999999999 0 0 0 0 1 1 1", m) && !one("group custom 1.5 0 0 0 0 1 1 1", m));
        m.title = "kept";
        CHECK(m.parse("title\n" + base) && m.title == "kept");      // a bare "title" leaves it alone, as getline did
        CHECK(m.parse("title  Hi there\t\r\n" + base) && m.title == "Hi there\t\r");
        CHECK(m.parse(base + "box 1 2 3 4 5 6 # a comment\n  \t accent\t1 2 3 4 5 6\r") && m.brushes.size() == 3 && m.brushes[2].material == 1);
        std::string err;
        CHECK(!m.parse(base + "box 1 2 3 4 5 x # why\n", &err) && err == "parse error on line 3: box 1 2 3 4 5 x ");
    }
    // map names: a stem, never a path
    CHECK(validMapName("temple") && validMapName("my-map_2"));
    // the editor's name field: what you type is what it saves as (a space used to make every save fail)
    CHECK(typedMapName("My Castle 2") == "My_Castle_2" && validMapName(typedMapName("My Castle 2")));
    CHECK(typedMapName("../x.qmap") == "xqmap" && typedMapName("").empty() && typedMapName(std::string(60, 'a')).size() == 40);
    CHECK(!validMapName("") && !validMapName("../x") && !validMapName("a/b") && !validMapName("a\\b") && !validMapName("c:x")
          && !validMapName("a.b") && !validMapName(std::string(41, 'a')) && !validMapName(std::string("a\0b", 3)));
    CHECK(safeMapName("../../etc/x") == "______etc_x" && safeMapName("") == "map" && safeMapName("temple") == "temple");
    CHECK(downloadedMapPath("../../x", 1).find("..") == std::string::npos);
    // custom rules: NaN / inf keep the default, out of range is clamped
    CustomRules def, r;
    r.fromText("custommode=x\ngravity=nan\nspeed=inf\njump=-inf\nknockback=99\nweapon=0,nan,nan,nan\n");
    CHECK(r.gravity == def.gravity && r.moveSpeed == def.moveSpeed && r.jump == def.jump && r.knockback == 5);
    CHECK(r.fireRate[0] == def.fireRate[0] && r.damage[0] == def.damage[0] && r.projSpeed[0] == def.projSpeed[0]);
}

// Test ports: the numbers in main() are offsets into a per-run block (TP), so two test runs on one machine
// (two checkouts, CI + local) don't fight over 28530 and friends. Picked once before the groups fork.
static int g_portShift = 0;
static uint16_t TP(int port) { return (uint16_t)(port + g_portShift); }
static void pickPortBlock() {
#ifdef _WIN32
    unsigned seed = (unsigned)std::chrono::steady_clock::now().time_since_epoch().count();
#else
    unsigned seed = (unsigned)getpid();
#endif
    for (int k = 0; k < 16; k++) {   // blocks of 2000 from 28500 to ~60000; the first whose sample ports are free
        int shift = (int)((seed + k) % 16) * 2000;
        bool ok = true;
        for (int p : {28520, 28700, 28900, 29300, 29600, 29997}) { UdpSocket s; if (!s.open((uint16_t)(p + shift))) { ok = false; break; } }
        if (ok) { g_portShift = shift; return; }
    }
}

// Update-package signatures (docs/SIGNING.md): round trip, tampering and wrong keys are rejected.
void testSigning() {
    printf("signing\n");
    std::string priv, pub, priv2, pub2, sig, err;
    CHECK(sign::keygen(&priv, &pub) && sign::keygen(&priv2, &pub2));
    CHECK(priv.size() == 64 && pub.size() == 64 && pub != pub2);
    std::string pubAgain;
    CHECK(sign::publicFromPrivate(priv, &pubAgain) && pubAgain == pub);
    CHECK(std::string(sign::kReleasePublicKey).size() == 64);
    std::string data = "QPAK1\n";
    for (int i = 0; i < 200000; i++) data += (char)(i * 31 + 7);
    std::string path = (std::filesystem::temp_directory_path() / ("quark_sign_" + std::to_string(getpid()) + ".qpak")).string();
    { std::ofstream(path, std::ios::binary) << data; }
    CHECK(sign::signFile(path, priv, &sig, &err) && sig.size() == 128);
    CHECK(sign::verifyFile(path, sig + "\n", pub, &err));             // round trip (.sig files end in a newline)
    CHECK(sign::verifyData(data, sig, pub));
    CHECK(!sign::verifyFile(path, sig, pub2, &err));                   // wrong key
    std::string bad = data;
    bad[100000] ^= 1;
    { std::ofstream(path, std::ios::binary | std::ios::trunc) << bad; }
    CHECK(!sign::verifyFile(path, sig, pub, &err));                    // one tampered byte
    CHECK(!sign::verifyData(data + "x", sig, pub));                    // appended byte
    std::string badSig = sig;
    badSig[5] = badSig[5] == '0' ? '1' : '0';
    CHECK(!sign::verifyData(data, badSig, pub));                       // tampered signature
    CHECK(!sign::verifyData(data, "", pub) && !sign::verifyData(data, sig.substr(2), pub) && !sign::verifyData(data, sig, "zz"));
    CHECK(!sign::verifyFile(path + ".missing", sig, pub, &err));
    std::filesystem::remove(path);
}

// Per-user data: every user file goes under userDataPath(); the first use of the default folder copies the old
// install-folder files over without touching them, and shipped maps stay in the install.
static void testUserData() {
    printf("user data folder + migration\n");
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path root = fs::u8path(userDataPath());
    fs::path old = root / "zz_oldinstall", dst = root / "zz_newdata";
    fs::create_directories(old / "maps" / "downloads", ec);
    fs::create_directories(old / "demos", ec);
    { std::ofstream(old / "quark.cfg") << "sens=2\n"; }
    { std::ofstream(old / "quark.key") << "secret\n"; }
    { std::ofstream(old / "demos" / "a.qdemo") << "demo"; }
    { std::ofstream(old / "maps" / "mine.qmap") << "box 0 0 0 1 1 1\n"; }
    { std::ofstream(old / "maps" / "downloads" / "x-0000.qmap") << "dl"; }
    { std::ofstream(old / "maps" / "mine.qlm") << "cache"; }
    Map t;
    CHECK(t.load("temple"));
    { std::ofstream(old / "maps" / "temple.qmap", std::ios::binary) << t.source; }          // untouched shipped map
    { std::ofstream(old / "maps" / "canal.qmap", std::ios::binary) << t.source << "\n"; }   // edited one
    fs::create_directories(dst, ec);
    { std::ofstream(dst / "quark.key") << "newer\n"; } // never overwritten
    CHECK(migrateUserData(old.string(), dst.string()) == 5);
    auto rd = [](const fs::path& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); };
    CHECK(rd(dst / "quark.cfg") == "sens=2\n" && rd(dst / "quark.key") == "newer\n" && rd(dst / "demos" / "a.qdemo") == "demo");
    CHECK(fs::exists(dst / "maps" / "mine.qmap") && fs::exists(dst / "maps" / "canal.qmap") && fs::exists(dst / "maps" / "downloads" / "x-0000.qmap"));
    CHECK(!fs::exists(dst / "maps" / "temple.qmap") && !fs::exists(dst / "maps" / "mine.qlm"));
    CHECK(fs::exists(old / "quark.cfg") && fs::exists(old / "maps" / "mine.qmap")); // originals kept
    CHECK(migrateUserData(old.string(), dst.string()) == 0);                         // idempotent
    fs::remove_all(old, ec);
    fs::remove_all(dst, ec);
    // shipped maps are read from the install; your maps dir is searched first and listed too
    CHECK(userMapsDir().find(userDataPath()) == 0 && downloadedMapPath("a", 1).find(userDataPath()) == 0);
    std::vector<std::string> ml = listMaps();
    CHECK(std::find(ml.begin(), ml.end(), "temple") != ml.end());
}

// Reachability probe: a host asks a prober (any server) to send one packet to its game port from outside.
static void testReachProbe(uint16_t port) {
    printf("reachability probe: reachable, unreachable, prober hardening\n");
    Server prober;
    ServerConfig pc; pc.map = "bastion"; pc.port = port;
    prober.setLog([](const std::string&) {});
    { std::string e_; if (!prober.start(pc, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    auto run = [&](Server& h, double secs) { double t = timeNow() + secs; while (timeNow() < t) { prober.frame(); h.frame(); sleepMs(1); } };
    {   // reachable: the reply lands on the host's game socket
        Server h; ServerConfig hc; hc.map = "bastion"; hc.port = port + 1; hc.probe = true; hc.prober = "127.0.0.1:" + std::to_string(port);
        h.setLog([](const std::string&) {});
        CHECK(h.start(hc));
        CHECK(h.reachability() == Server::REACH_CHECKING);
        for (int i = 0; i < 400 && h.reachability() != Server::REACH_OK; i++) run(h, 0.02);
        CHECK(h.reachability() == Server::REACH_OK);
        CHECK(prober.playerCount() == 0 && h.playerCount() == 0);   // no player state anywhere
        h.stop();
    }
    {   // unreachable: the prober answers a port nobody listens on (as an unforwarded port would swallow it)
        Server h; ServerConfig hc; hc.map = "bastion"; hc.port = port + 2; hc.probe = true; hc.prober = "127.0.0.1:" + std::to_string(port);
        hc.probeAskPort = port + 3;
        h.setLog([](const std::string&) {});
        CHECK(h.start(hc));
        for (int i = 0; i < 400 && h.reachability() == Server::REACH_CHECKING; i++) run(h, 0.02);
        CHECK(h.reachability() == Server::REACH_BLOCKED);
        CHECK(h.reachStatus().find("forward UDP " + std::to_string(port + 3)) != std::string::npos);
        h.stop();
    }
    {   // prober hardening: replies only to the request's own source IP, never bigger than the request, rate limited
        UdpSocket target; CHECK(target.open(port + 4));
        auto req = [&](int len, uint32_t nonce) {
            std::vector<uint8_t> b(kProbeRequestBytes, 0);
            Writer w(b.data(), b.size()); writeHeader(w, PKT_PROBE_REQUEST); w.u32(nonce); w.u16((uint16_t)(port + 4));
            b.resize(len); return b;
        };
        auto got = [&](uint32_t want) { uint8_t buf[256]; NetAddr f; int n, hits = 0; prober.frame();
            for (int k = 0; k < 50; k++) { while ((n = target.recv(f, buf, sizeof buf)) > 0) { CHECK(n <= kProbeRequestBytes);
                Reader r(buf, n); r.u32(); r.u8(); if (r.u32() == want) hits++; } sleepMs(2); } return hits; };
        auto spoofed = req(kProbeRequestBytes, 111);   // a request "from" another host: the reply goes there, not to us
        prober.handlePacket(NetAddr{0x0A0B0C0Du, 5555}, spoofed.data(), (int)spoofed.size());
        CHECK(got(111) == 0);
        auto shortReq = req(kProbeRequestBytes - 1, 222);   // unpadded: ignored
        prober.handlePacket(NetAddr{0x7F000001u, 5555}, shortReq.data(), (int)shortReq.size());
        CHECK(got(222) == 0);
        auto ok = req(kProbeRequestBytes, 333);
        prober.handlePacket(NetAddr{0x7F000001u, 5555}, ok.data(), (int)ok.size());
        CHECK(got(333) == 1);
        auto again = req(kProbeRequestBytes, 444);     // same source right away: rate limited
        prober.handlePacket(NetAddr{0x7F000001u, 5555}, again.data(), (int)again.size());
        CHECK(got(444) == 0);
    }
    CHECK(prober.playerCount() == 0);
    prober.stop();
}

#include "rules_tests.h"   // game rule tests (owner answers 5.x, REVIEW batch 2)
#include "server_tests.h"  // server / netcode tests (FINAL_REVIEW 2.8-2.11, 6.4, 6.5, clock, PERF-8 / 9, ENG-2)

int main(int argc, char** argv) {
    setExecutableDir(argv[0]);
    { // never touch the real per-user folder: a fresh temp one per run, removed at exit by this (not a forked) process
        static std::string dir = (std::filesystem::temp_directory_path() / ("quark_tests_data_" + std::to_string((long)getpid()))).string();
        static long owner = (long)getpid();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        setUserDataDir(dir);
        atexit([] { if ((long)getpid() == owner) { std::error_code e; std::filesystem::remove_all(dir, e); } });
    }
    netInit();
    pickPortBlock();
    setTimeScale(kTestTimeScale); // bot subprocesses share the test clock speed
    if (argc >= 4 && std::string(argv[1]) == "bot") return runBot(argv[2], atof(argv[3]), argc > 4 ? argv[4] : "bot");
    qsrand(1234);
#ifdef _WIN32
    setvbuf(stdout, nullptr, _IONBF, 0); // (CI: see how far it got; the Windows CRT has no line buffering, and _IOLBF
                                         //  with size 0 is an invalid-parameter abort there, REVIEW ENG-1)
#else
    setvbuf(stdout, nullptr, _IOLBF, 0); // (CI: see how far it got)
#endif
    std::thread([] { // a hung test fails the run instead of stalling CI for an hour
        std::this_thread::sleep_for(std::chrono::duration<double>(25 * 60 * 4.0 / std::min(4.0, kTestTimeScale)));   // (25 min at 4x)
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
    if (!getenv("QUARK_PUBLIC_IP")) { // servers look up their public IPv4 at start (ranked signatures): not from tests
#ifdef _WIN32
        _putenv("QUARK_PUBLIC_IP=off");
#else
        setenv("QUARK_PUBLIC_IP", "off", 1);
#endif
    }
    if (argc >= 2 && std::string(argv[1]) == "playtest") { playtestMaps(TP(29400), argc > 2 ? atof(argv[2]) : 40); return 0; }
    if (argc >= 2 && std::string(argv[1]) == "serverbench") {
        serverBench(TP(29300), 12, 4, argc > 2 ? atof(argv[2]) : 15, "harbor");
        serverBench(TP(29301), 24, 8, argc > 2 ? atof(argv[2]) : 15, "hamlet");
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "signing") { testSigning(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "launcher") { g_fail += launcherTests(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "racefloors") { // the RACE_FLOORS table for tools/master/worker.js
        Map m;
        if (!m.load("viaduct")) return 1;
        std::vector<int> f = raceSplitFloorsMs(m);
        printf("viaduct: [");
        for (size_t i = 0; i < f.size(); i++) printf("%s%d", i ? ", " : "", f[i]);
        printf("]\n");
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "proc") { testProcPipes(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "hostile") { testHostileInput(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "userdata") { testUserData(); testResetMaps(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "ladders") { testLadders(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "stairs") { testStairs(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "points") { setTimeScale(kTestTimeScale); testPoints(TP(29995)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "syringe") { setTimeScale(kTestTimeScale); testSyringe(TP(29996)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "edladder") { testEditorLadder(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "edgroups") { testEditorGroups(); testTrimClipEdits(); testIncrementalEdits(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "cartchecks") { setTimeScale(kTestTimeScale); testCartChecks(TP(29997)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "allmaps") { testAllMaps(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "chat") { testChatAndVoteLimits(TP(29592)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "limits") { testMapLimits(); testClipRays(); testAtomicWrite(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "custom") { testRulesValues(); testCustomMode(TP(29990)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "rejoin") { testRejoin(TP(29980)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "kick") { testKickByName(TP(29981)); testAddbotArgs(TP(29982)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "it") { testItModes(TP(29450)); testTagLateJoin(TP(29455)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "aim") { uint16_t p = TP(29500); testBotAim(p); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "nav") { testNavGraph(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "ranked") { testFilledParse(); testRanked(TP(29610)); testRankedVoteAndRestart(TP(29620)); testRankedLeaver(TP(29625)); testRankedLeaverPaths(TP(29628)); testRankedRelay(TP(29630)); testReconnectGrace(TP(29626)); testTourneySeeding(TP(29627)); testMasterReplies(TP(29629)); testHelloRetry(); testMasterSignature(); testPlacementConstant(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "edit") { testMapDownload(TP(29600)); testEditSession(TP(29601)); testVotedEdit(TP(29602)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "mapreport") return mapReport(argc > 2 ? argv[2] : "docs/MAP_REPORT.md") ? 1 : 0;
    if (argc >= 2 && std::string(argv[1]) == "exportassets") return exportAssets(argc > 2 ? argv[2] : "assets_export") ? 1 : 0;
    if (argc >= 2 && std::string(argv[1]) == "assets") { g_fail += assetTests(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "demo") { testSnapshotBounds(); testDemoLengthWrap(); testReplayBound(TP(29604)); testMapNames(TP(29605)); testDemo(TP(29603)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "trial") { testTimeTrial(TP(29960)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "rate") { testTickRateFallback(TP(29950)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "net") { testInterpRewind(TP(29903)); testUptimeClock(TP(29902)); testNetcode(TP(29900)); testInputBacklog(TP(29901)); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "wallbang") { testWallbang(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "tourney") { uint16_t p = TP(29800); testArenaAndTourney(p); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "bots") { uint16_t p = TP(28900); testBots(p); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "server") { serverTests(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "snapbench") {   // PERF-8 / PERF-9: snapshot + visibility CPU per send
        double secs = argc > 2 ? atof(argv[2]) : 8;
        snapBench("midtown", 32, secs, TP(28820));
        snapBench("midtown", 8, secs, TP(28821));
        snapBench("harbor", 16, secs, TP(28822));
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "rules") { testGameRules(argc > 2 ? argv[2] : ""); return g_fail ? 1 : 0; }    if (argc >= 2 && std::string(argv[1]) == "weapons") { uint16_t p = TP(29700); testFixedSpread(p + 50); testWeapons(p); testRocketSelfDamage(p + 20); testAltFires(p + 30); return g_fail ? 1 : 0; }
    // ---- the whole suite. Most tests wait on real-time network play rather than the CPU, so each runs in its own
    // process, several at once (output kept in order); a few timing-sensitive ones run alone afterwards.
    // "quark_tests serial" runs everything in this process, one after another, as before.
    struct T { const char* name; std::function<void()> run; };
    std::vector<T> par = {   // (longest first: they set the wall time)
        {"bots", [] { uint16_t p = TP(28900); testBots(p); }},
        {"botaim", [] { uint16_t p = TP(29000); testNavGraph(); testBotAim(p); }},
        {"tourney", [] { uint16_t p = TP(29100); testArenaAndTourney(p); }},
        {"duel", [] { testDuel(TP(28520)); testDuel2v2(TP(28521)); }},
        {"weapons", [] { testFixedSpread(TP(28555)); testWeapons(TP(28530)); testRocketSelfDamage(TP(28540)); testAltFires(TP(28545)); testRocketLob(TP(28550)); testTimeToKill(); testWallbang(); }},
        {"maps", [] {
            testMaps(); testAllMaps(); testStairs(); testLadders();
            printf("map scores\n");   // the map bar: every map scores at least 7 in every category but originality
            std::string p = (std::filesystem::temp_directory_path() / ("quark_map_report_" + std::to_string(getpid()) + ".md")).string();
            if (mapReport(p.c_str())) g_fail++;
        }},
        {"movement", [] { testProcPipes(); testSleepPrecision(); testCompletion(); testSigning(); g_fail += launcherTests(); testHostileInput(); testMovement(); testTickRateIndependence(); testMapLimits(); testRulesValues(); testClipRays(); testAtomicWrite(); testGridQueries(); testEditorLadder(); testEditorGroups(); testTrimClipEdits(); testIncrementalEdits(); }},
        {"sessions", [] { testSession(MODE_VS, "bastion", TP(28560)); testSession(MODE_BOMB, "bastion", TP(28561)); testSession(MODE_KOTH, "bastion", TP(28562)); }},
        {"sessions2", [] { testSession(MODE_PAYLOAD, "bastion", TP(28563)); testSession(MODE_FFA, "bastion", TP(28564)); testSession(MODE_RACE, "foundry", TP(28565)); }},
        {"modes", [] { testCustomMode(TP(28570)); testPoints(TP(28571)); testSyringe(TP(28572)); testCartChecks(TP(28573)); testItModes(TP(28574)); testTagLateJoin(TP(28579)); }},
        {"race", [] { testRaceFloors(); testTimeTrial(TP(28580)); testPayloadPush(TP(28581)); testPacketLoss(TP(28582)); testCartDash(TP(28583)); testSemiAuto(TP(28584)); }},
        {"votes", [] { testRejoin(TP(28590)); testMapVote(TP(28591)); testChatAndVoteLimits(TP(28592)); testHandshake(TP(28594)); testReachProbe(TP(28595)); testKickByName(TP(28596)); testAddbotArgs(TP(28597)); }},
        {"edit", [] { testSnapshotBounds(); testDemoLengthWrap(); testReplayBound(TP(28604)); testMapNames(TP(28605)); testMapDownload(TP(28600)); testEditSession(TP(28601)); testVotedEdit(TP(28602)); testDemo(TP(28603)); }},
        {"ranked", [] { testFilledParse(); testRanked(TP(28610)); testRankedVoteAndRestart(TP(28620)); testInternetList(TP(28625)); testMasterReplies(TP(28627)); testHelloRetry(); testMasterSignature(); testPlacementConstant(); }},
        {"ranked2", [] { testRankedLeaver(TP(28636)); testRankedLeaverPaths(TP(28639)); testRankedRelay(TP(28640)); testReconnectGrace(TP(28637)); testTourneySeeding(TP(28638)); }},
        {"rules", [] { testGameRules(""); }},
        {"assets", [] { g_fail += assetTests(); }},
        {"server", [] { serverTests(); }},
    };
    std::vector<T> alone = {   // timing-sensitive (or rewrites shared files): one at a time, on an otherwise idle machine
        {"netcode", [] { testInterpRewind(TP(28703)); testUptimeClock(TP(28702)); testNetcode(TP(28700)); testInputBacklog(TP(28701)); testCrossLanguageConstants(TP(28705)); }},
        {"tickrate", [] { testTickRateFallback(TP(28710)); testRateBetweenRounds(TP(28711)); testRateUndoStepUp(TP(28712)); }},
        {"resetmaps", [] { testResetMaps(); }},
        {"userdata", [] { testUserData(); }},
    };
    [[maybe_unused]] bool serial = argc >= 2 && std::string(argv[1]) == "serial";   // (Windows: always serial)
#ifndef _WIN32
    if (!serial) {
        unsigned jobs = std::max(2u, std::thread::hardware_concurrency());
        struct Job { pid_t pid; std::string out; size_t idx; };
        std::vector<Job> running;
        std::vector<std::string> outputs(par.size());
        std::vector<int> done(par.size(), -1);
        size_t next = 0, printed = 0;
        auto tmp = std::filesystem::temp_directory_path();
        // each group gets its own user data dir (maps, demos, keys) under this run's: the edit and ranked groups write
        // zz_* maps that the maps group would otherwise list and grade. Removed when the group ends.
        auto groupDir = [&](size_t i) { return userDataPath(std::string("groups/") + par[i].name); };
        auto start = [&](size_t i) {
            std::string out = (tmp / ("quark_test_" + std::to_string(getpid()) + "_" + par[i].name + ".log")).string();
            std::string data = groupDir(i);
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {   // the child: one test group, its output to a file
                if (!freopen(out.c_str(), "w", stdout)) std::_Exit(99);
                dup2(fileno(stdout), 2);
                setvbuf(stdout, nullptr, _IOLBF, 0);
                std::error_code ec;
                std::filesystem::remove_all(data, ec);
                std::filesystem::create_directories(data, ec);   // (the parent made the root already: set it, then make it)
                setUserDataDir(data);
                qsrand(1234);
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
                { std::error_code ec; std::filesystem::remove_all(groupDir(i), ec); }
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
