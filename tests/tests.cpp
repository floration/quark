#include <atomic>
#include <fstream>
#include <map>
#include <regex>
#include <cstring>
#include <algorithm>
#include <csignal>
#include <numeric>
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
#include "game/demo.h"
#include "game/glass.h"
#include "../src/client/complete.h"
#include "../src/client/lightmap.h"
#include "../src/client/hudclock.h"
#include "../src/client/modecalls.h"
#include "../src/client/weaponpredict.h"
#include "../src/client/clientfeel.h"
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
#include "game/benchmark.h"
#include "walkgrid.h"
#include "common/proc.h"
#include "groupproc.h"
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

// Human-made maps (the owner's rule, docs/JUDGMENT_CALLS.md): the owner will hand-make the maps; those don't have to
// pass the automated map checks, but they're still checked and their numbers reported, for balancing. A map is
// human-made when it says so ("handmade", which the editor writes), when tools/make_maps.py didn't write it, or when
// it's the owner's override of a generated one (assets/overrides/maps). Generated maps keep the strict bar.
bool isHumanMadeMap(const std::string& name, const Map& m) {   // (mapbounds.cpp too)
    std::error_code ec;
    return m.humanMade() || (!sourceDir().empty() && std::filesystem::exists(std::filesystem::path(sourceDir()) / "assets" / "overrides" / "maps" / (name + ".qmap"), ec));
}
// Put one at the top of a per-map check, after the map loads: whatever that map fails is printed as usual, and when
// the map is human-made, reported as such and taken off the failure count as the check ends.
struct HumanMadeGrace {
    std::string name;
    bool human;
    int before;
    HumanMadeGrace(const std::string& n, const Map& m) : name(n), human(isHumanMadeMap(n, m)), before(g_fail) {}
    ~HumanMadeGrace() {
        if (!human || g_fail <= before) return;
        printf("  %s is HUMAN-MADE: %d check(s) above failed - reported, not failing the run\n", name.c_str(), g_fail - before);
        g_fail = before;
    }
};

// Host pauses. The tick-rate tests run the server and its clients in real time on this one thread, and the server
// rightly counts a tick more than its stall threshold late (4 ms at 1000 Hz / 500 snapshots/s) as a stall players see:
// 1% of a window's ticks, in two windows in a row (kLateWindows), steps it down; the tests also check single windows
// (one arms the test, a clean one disarms it, a healthy host's window has under 0.2%). A cloud VM sometimes doesn't run
// the thread at all: on a 4 vCPU sandbox with nothing else running, a plain 1 ms sleep woke more than 4 ms late 25
// times a minute and up to 21 ms late (a spin: 51 ms; steal time rising), and one 20 ms pause is 16 ticks over 4 ms,
// 1.6% of a 1 s window. That isn't the code under test. So the harness measures its own waits (the time asleep, or spinning, past what it asked for: nothing
// the server's code does can lengthen it), and an attempt of a timing test that failed while the host paused past the
// stall threshold is set aside and run again (timingTest). Every check is the same; the third attempt always counts.
static double g_hostPause = 0;   // the longest host pause in this attempt (real seconds)
static double wallNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static void hostWaited(double t0, double asked) { g_hostPause = std::max(g_hostPause, wallNow() - t0 - std::max(asked, 0.0)); }
static void hostSleepMs(int ms) { double t0 = wallNow(); sleepMs(ms); hostWaited(t0, ms * 1e-3); }
constexpr double kHostPauseVoid = 0.004;   // (the server's stall threshold at 1000 Hz, 500/s: kStallSnaps x 2 ms)
static void timingTest(const char* name, const std::function<void()>& run) {
    for (int attempt = 1;; attempt++) {
        int before = g_fail;
        g_hostPause = 0;
        run();
        if (g_fail == before || attempt == 3 || g_hostPause <= kHostPauseVoid) return;
        printf("  %s: the host paused this thread %.1f ms (over the %.0f ms stall threshold) in this attempt: its %d failure(s) "
               "are set aside, attempt %d of 3\n", name, g_hostPause * 1e3, kHostPauseVoid * 1e3, g_fail - before, attempt + 1);
        g_fail = before;
    }
}

// Real seconds this thread has spent off the CPU: wall time minus the thread's own CPU time. Its growth across a stretch
// that only computes, or beyond a sleep's length, is the machine not running the test (another process, a VM's host),
// never the game's code, which counts as CPU time however slow it is. (Windows: the thread CPU clock is 15.6 ms
// coarse, so there it's how late sleeps woke only: offCpu() is wall time, and a pump's frames count as off the CPU.)
static double offCpu() {
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
#ifdef _WIN32
    return wall;
#else
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return wall - (ts.tv_sec + ts.tv_nsec * 1e-9);
#endif
}

// Pump a server and some clients for `seconds` of real time, feeding each client an input. With `stalled`, it keeps the
// longest the machine left the loop unrun in real seconds (beyond its 1 ms sleep; see offCpu): where a check's premise
// is a machine that runs the test when it asks, a run that this breaks is the machine's, not the game's.
template <class F>
static void pump(Server& s, std::vector<NetClient*>& cls, double seconds, F input, double* stalled = nullptr) {
    double end = timeNow() + seconds;
    double nextTick = timeNow();
    double off = stalled ? offCpu() : 0;
    while (timeNow() < end) {
        s.frame();
        for (auto* c : cls) c->poll();
        while (timeNow() >= nextTick) {
            for (size_t i = 0; i < cls.size(); i++) cls[i]->tick(input((int)i));
            nextTick += cls.empty() ? 0.001 : cls[0]->tickPeriod();
        }
        hostSleepMs(1);
        if (stalled) { double o = offCpu(); *stalled = std::max(*stalled, o - off - 0.001); off = o; }
    }
}

// Checks whose premise is a machine that runs the test when it asks (pump's `stalled`) run again when it didn't, at
// most this many times; the last one counts as it is, and nothing they assert is loosened (ratePump does the same).
static constexpr int kStallTries = 3;
// The longest stall, in real seconds, after which the server still runs every tick: past 20 periods behind it skips
// ahead (Server::frame), so its clock and the commands' tokens for that time are gone.
static double serverSkipStall(const Server& s) { return 20.0 / s.tickRate() / timeScale(); }

// A server with only in-process bots (no NetClients) runs on a manual clock instead (net.h; a SimClock for the test's
// scope): simRun steps the clock to the server's next tick and runs frame(), one tick per call. Every tick runs,
// however loaded the machine is (pump's real time skips ticks past 20 a frame), and no real time is waited: the same
// game as fast as the CPU goes, and the same game every run for the same seeds. perTick runs after each tick.
struct SimClock {
    bool was = manualClock();
    SimClock() { setManualClock(true); }
    ~SimClock() { setManualClock(was); }
};
template <class F>
static void simRun(Server& s, double seconds, F perTick) {
    const uint64_t end = s.timeMs_ + (uint64_t)std::llround(seconds * 1000);
    while (s.timeMs_ < end) {
        advanceClockTo(s.nextTickForTest());
        const uint64_t before = s.timeMs_;
        s.frame();
        if (s.timeMs_ == before) { printf("  FAIL %s:%d: simRun: no tick ran (no SimClock?)\n", __FILE__, __LINE__); g_fail++; return; }
        perTick();
    }
}
static void simRun(Server& s, double seconds) { simRun(s, seconds, [] {}); }
// Real seconds (how long a test group took): timeNow() is game time, 4x real in the tests and stepped by SimClock
static double wallSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
// The bots' navigation graph builds on a thread of its own, in real time: wait for it before stepping (up to 20 s), or
// when the bots start using it would depend on the machine's speed.
static bool simNavReady(Server& s) {
    for (int k = 0; k < 20000; k++) { if (s.nav_.ready(s.map_)) return true; sleepMs(1); }
    printf("  FAIL %s:%d: the navigation graph never finished building\n", __FILE__, __LINE__); g_fail++;
    return false;
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
    // (signal(), not sigaction(): Apple's clang parses `sigaction(...)` next to a `struct sigaction` as the struct.
    // Setting SIG_IGN again is what it must already be.)
    CHECK(signal(SIGPIPE, SIG_IGN) == SIG_IGN);
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

// The feel fixes' client sound rules (clientfeel.h).
static void testFeelSounds() {
    printf("feel sounds\n");
    {   // F3: your hurt sound once per 0.15 s at most, louder for more damage; the laser's hits every 10 ms add up
        HurtSoundGate g;
        int plays = 0;
        float loudest = 0;
        for (int ms = 0; ms < 1000; ms += 10) { float v = g.frame(ms / 1000.0, 3); if (v > 0) { plays++; loudest = std::max(loudest, v); } }
        printf("  laser for 1 s: %d hurt sounds (one per hit was 100)\n", plays);
        CHECK(plays >= 6 && plays <= 7);
        CHECK(loudest > HurtSoundGate::volumeFor(3));   // (the 15 damage of a gap, not one hit's 3)
        HurtSoundGate h;
        CHECK(h.frame(0, 10) > 0 && h.frame(0.05, 0) == 0 && h.frame(0.16, 0) == 0);   // nothing pending: silent
        CHECK(HurtSoundGate::volumeFor(50) > HurtSoundGate::volumeFor(10) && HurtSoundGate::volumeFor(200) <= 1.0f);
        CHECK(h.frame(0.2, 5) > 0);   // a fresh hit after the gap: at once
    }
    {   // F8: movement reports what each command did, so sounds don't depend on what a frame's state compare sees
        Map m;
        std::string err;
        CHECK(m.parse("box -50 -1 -50 50 0 50\nspawn any 0 0 0\n", &err));
        auto settle = [&](MoveState& q) { q = MoveState(); q.pos = {0, 0.5f, 0}; for (int i = 0; i < 400; i++) pmove(q, UserCmd(), m); CHECK(q.flags & MF_ONGROUND); };
        MoveState q;
        // a bhop: falling onto the floor with jump held bounces straight off it in one step: a landing AND a jump
        settle(q);
        q.pos.y = 0.004f; q.vel = {6, -12, 0}; q.flags = MF_JUMPHELD;
        UserCmd j; j.msec = 1; j.buttons = BTN_JUMP;
        pmove(q, j, m);
        CHECK((q.events & (ME_LAND | ME_JUMP)) == (ME_LAND | ME_JUMP) && q.landSpeed > 11.9f && q.vel.y > 0);
        pmove(q, j, m);
        CHECK(q.events == 0);   // (each call reports its own)
        // jump and dash pressed together on the floor: the dash and its dash jump in one command (it was silent)
        settle(q);
        UserCmd d; d.msec = 8; d.forward = 127; d.buttons = BTN_JUMP | BTN_DASH;
        pmove(q, d, m);
        CHECK((q.events & ME_DASH) && (q.events & ME_SUPER) && !(q.events & ME_HYPER));
        // the same aimed down: a hyper
        settle(q);
        d.pitch = -0.8f;
        pmove(q, d, m);
        CHECK((q.events & ME_DASH) && (q.events & ME_HYPER) && !(q.events & ME_SUPER));
        // walking: nothing
        settle(q);
        UserCmd w; w.msec = 8; w.forward = 127;
        uint8_t any = 0;
        for (int i = 0; i < 50; i++) { pmove(q, w, m); any |= q.events; }
        CHECK(any == 0);
    }
}

// X2: everyone else's jumps, landings and dashes reach the other clients (PlayerSnap::keys' KEY_EV_* bits), once each.
static void testRemoteMoveSounds(uint16_t port) {
    printf("remote movement sounds\n");
    auto idle = [](int) { return UserCmd(); };
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "hopper");
    b.connect("127.0.0.1:" + std::to_string(port), "listener");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 2.0, idle);
    const int ia = a.clientId(), ib = b.clientId();
    CHECK(ia >= 0 && ib >= 0 && s.players_[ia].alive && s.players_[ib].alive);
    if (ia < 0 || ib < 0) { s.stop(); return; }
    s.players_[ib].move.pos = s.players_[ia].move.pos;   // (side by side: in sight of each other)
    pump(s, cls, 0.5, idle);
    NetClient::RemoteMoveEvent e;
    while (b.nextRemoteMoveEvent(1e18, &e)) {}
    // hop for 2 s, then a dash
    pump(s, cls, 2.0, [&](int i) { UserCmd c; if (i == 0) c.buttons = BTN_JUMP; return c; });
    pump(s, cls, 0.4, idle);
    pump(s, cls, 0.3, [&](int i) { UserCmd c; if (i == 0) { c.buttons = BTN_DASH; c.forward = 127; } return c; });
    pump(s, cls, 0.3, idle);
    int jumps = 0, lands = 0, dashes = 0, mine = 0;
    while (b.nextRemoteMoveEvent(1e18, &e)) {
        if (e.id != ia) { mine++; continue; }
        const uint16_t ev = e.ev >> kKeyEvShift;
        jumps += (ev & ME_JUMP) != 0; lands += (ev & ME_LAND) != 0; dashes += (ev & ME_DASH) != 0;
    }
    printf("  2 s of hopping and a dash, heard by the other client: %d jumps, %d landings, %d dashes (%d of its own)\n", jumps, lands, dashes, mine);
    CHECK(jumps >= 3 && jumps <= 6 && lands >= 2 && dashes == 1 && mine == 0);
    // X3: a hit also tells its victim where it came from (EV_HIT "@x z" in decimetres), for the damage arc
    b.takeEvents();
    s.ms_.phase = PHASE_WARMUP;   // (damage on)
    const Vec3 from = s.players_[ia].move.pos;
    s.damagePlayer(ib, ia, 10, WPN_RIFLE, Vec3());
    pump(s, cls, 0.2, idle);
    bool told = false;
    for (auto& ev : b.takeEvents()) {
        long x, z;
        if (ev.type == EV_HIT && ev.a == ia && ev.b == ib && sscanf(ev.text.c_str(), "@%ld %ld", &x, &z) == 2)
            told = std::fabs(x * 0.1f - from.x) < 0.11f && std::fabs(z * 0.1f - from.z) < 0.11f;
    }
    CHECK(told);
    s.stop();
}

// T1: a jump tapped (and let go) up to 0.1 s before landing jumps on landing like holding jump does.
static void testJumpTapBuffer() {
    printf("jump tap buffer\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -50 -1 -50 50 0 50\nspawn any 0 0 0\n", &err));
    auto start = [] { MoveState q; q.pos = {0, 3, 0}; q.vel = {8, 0, 0}; return q; };
    int land = -1;   // the step that lands, falling 3 m with no input
    { MoveState q = start(); for (int i = 0; i < 2000 && land < 0; i++) { pmove(q, UserCmd(), m); if (q.flags & MF_ONGROUND) land = i; } }
    CHECK(land > 100);
    // jump pressed for 5 ms starting `early` ms before the landing step (or held from the start: early < 0)
    auto run = [&](int early) {
        MoveState q = start();
        for (int i = 0; i <= land + 20; i++) {
            UserCmd c;
            if (early < 0 || (i >= land - early && i < land - early + 5)) c.buttons = BTN_JUMP;
            pmove(q, c, m);
        }
        return q;
    };
    const MoveState held = run(-1);
    CHECK(held.vel.y > 5);   // (bounced off the landing)
    for (int early : {10, 30, 60, 90}) {
        MoveState t = run(early);
        if (std::fabs(t.vel.y - held.vel.y) > 1e-4f || std::fabs(t.pos.y - held.pos.y) > 1e-4f) printf("  tapped %d ms early: vy %.3f, held %.3f\n", early, t.vel.y, held.vel.y);
        CHECK(std::fabs(t.vel.y - held.vel.y) < 1e-4f && std::fabs(t.pos.y - held.pos.y) < 1e-4f && std::fabs(t.vel.x - held.vel.x) < 1e-4f);
    }
    CHECK(run(150).vel.y <= 0.0f);   // (past the buffer: no jump)
}

// T2: holding dash fires each dash the moment it's allowed (like the held grapple): the charges chain 0.6 s apart (the
// cooldown from each dash's end), each charge back 1.2 s after its dash; in the air the charge (one by default), then none.
static void testHeldDash() {
    printf("held dash\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -900 -1 -900 900 0 900\nspawn any 0 0 0\n", &err));
    auto starts = [&](MoveState q, int ms) {
        std::vector<int> at;
        UserCmd c; c.msec = 1; c.buttons = BTN_DASH; c.forward = 127;
        for (int i = 0; i < ms; i++) { pmove(q, c, m); if (q.events & ME_DASH) at.push_back(i); }
        return at;
    };
    MoveState g; g.flags = MF_ONGROUND;
    for (int i = 0; i < 50; i++) pmove(g, UserCmd(), m);
    std::vector<int> a = starts(g, 3000);
    printf("  held on the floor for 3 s: %zu dashes, at", a.size());
    for (int t : a) printf(" %d", t);
    printf(" ms\n");
    // (one charge, owner 2026-11-01: each waits for the charge, back 1.2 s after the dash's 0.12 s)
    CHECK(a.size() == 3 && a[0] == 0);
    for (size_t k = 1; k < a.size(); k++) CHECK(std::abs(a[k] - a[k - 1] - 1320) <= 1);
    MoveState air; air.pos = {0, 400, 0};   // (falling the whole time: no floor to refill them)
    std::vector<int> b = starts(air, 3000);
    printf("  held in the air: %zu dashes\n", b.size());
    CHECK(b.size() == 1);   // (the one charge, then none: only the floor gives it back)
}

// F18: air-strafe speed doesn't depend on the frame rate: the frame's turn spread over its ticks (spreadYaw).
static void testStrafeFps() {
    printf("strafe speed vs fps\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -900 -1 -900 900 0 900\nspawn any 0 0 0\n", &err));
    // 1.5 s of A/D strafing in the air at a steady turn, 1000 Hz ticks, the mouse read once a frame
    auto run = [&](int fps, float rate, bool spread) {
        MoveState q; q.pos = {0, 300, 0}; q.vel = {0, 0, -10};
        float yaw = 0, sent = 0;
        int msLeft = 1500;
        double acc = 0;
        while (msLeft > 0) {
            acc += 1000.0 / fps;
            const int n = std::min(msLeft, (int)acc);
            acc -= n;
            yaw -= rate / fps;
            for (int k = 1; k <= n; k++) {
                UserCmd c; c.msec = 1; c.side = 127;
                c.yaw = spread ? spreadYaw(sent, yaw, k, n) : yaw;
                pmove(q, c, m);
            }
            if (n > 0) sent = yaw;
            msLeft -= n;
        }
        return std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z);
    };
    float worstOld = 1, worstNew = 1;
    for (float rate : {1.5f, 2.5f, 3.5f}) {
        const float ref = run(1000, rate, false);
        for (int fps : {30, 60, 144}) {
            const float o = run(fps, rate, false) / ref, nw = run(fps, rate, true) / ref;
            printf("  turn %.1f rad/s, %d fps: %.1f%% before, %.2f%% now (1000 fps: %.2f m/s)\n", rate, fps, (o - 1) * 100, (nw - 1) * 100, ref);
            worstOld = std::min(worstOld, o); worstNew = std::min(worstNew, nw);
        }
    }
    CHECK(worstNew > 0.995f && worstOld < worstNew);
    CHECK(spreadYaw(3.1f, -3.1f, 1, 2) > 3.1f || spreadYaw(3.1f, -3.1f, 1, 2) < -3.1f);   // (across the wrap: the short way)
    CHECK(spreadYaw(0.2f, 0.6f, 4, 4) == 0.6f && spreadYaw(0.2f, 0.6f, 1, 1) == 0.6f);
}

// F19: a refill the server makes between commands (a teammate's syringe) reaches the prediction with the next snapshot.
static void testRefillPrediction(uint16_t port) {
    printf("syringe refill predicted\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "dasher");
    std::vector<NetClient*> cls = {&a};
    auto idle = [](int) { return UserCmd(); };
    pump(s, cls, 2.0, idle);
    const int id = a.clientId();
    CHECK(id >= 0 && s.players_[id].alive);
    if (id < 0) { s.stop(); return; }
    // both charges spent (held dash: 0.72 s apart)
    pump(s, cls, 0.85, [](int) { UserCmd c; c.buttons = BTN_DASH; c.forward = 127; return c; });
    pump(s, cls, 0.05, idle);
    const int before = a.predicted().dashCharges;
    dashRefillAll(s.players_[id].move);   // (what the syringe does)
    s.players_[id].move.hookCooldown = 0;
    pump(s, cls, 0.1, idle);
    printf("  charges: %d before the refill, predicted %d after it (server %d)\n", before, (int)a.predicted().dashCharges, (int)s.players_[id].move.dashCharges);
    CHECK(before < 2 && a.predicted().dashCharges == s.players_[id].move.dashCharges && a.predicted().dashCooldownMs == s.players_[id].move.dashCooldownMs);
    // and no replay while nothing differs
    pump(s, cls, 0.3, idle);
    CHECK(a.lastReplayCount() == 0);
    s.stop();
}

// F20: a grapple fired at an enemy you see is predicted latched onto them (the server's own pick), not on the wall behind.
static void testHookPrediction(uint16_t port) {
    printf("grapple on a player predicted\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "hooker");
    b.connect("127.0.0.1:" + std::to_string(port), "target");
    std::vector<NetClient*> cls = {&a, &b};
    auto idle = [](int) { return UserCmd(); };
    pump(s, cls, 2.0, idle);
    const int ia = a.clientId(), ib = b.clientId();
    CHECK(ia >= 0 && ib >= 0);
    if (ia < 0 || ib < 0) { s.stop(); return; }
    // an open direction from a's spot: b stands 6 m along it
    const Vec3 pa = s.players_[ia].move.pos, eye = pa + Vec3(0, kEyeHeight, 0);
    float yaw = 0;
    bool found = false;
    for (int k = 0; k < 16 && !found; k++) {
        yaw = k * PI / 8;
        Vec3 d = angleForward(yaw, 0);
        found = s.map_.raycast(eye, d, 12) < 0 && s.map_.raycast(pa + d * 6 + Vec3(0, 0.5f, 0), Vec3(0, -1, 0), 1.0f) >= 0;
    }
    CHECK(found);
    const Vec3 pb = pa + angleForward(yaw, 0) * 6;
    const float pitch = std::atan2(pb.y + 0.9f - eye.y, 6.0f);
    auto place = [&] { s.players_[ib].move.pos = pb; s.players_[ib].move.vel = {}; s.players_[ia].move.pos = pa; s.players_[ia].move.vel = {}; };
    for (int k = 0; k < 40; k++) { place(); pump(s, cls, 0.01, [&](int i) { UserCmd c; if (i == 0) { c.yaw = yaw; c.pitch = pitch; } return c; }); }
    bool caught = false;
    uint32_t replays = 0;
    double end = timeNow() + 0.5;
    while (timeNow() < end && !caught) {
        place();
        pump(s, cls, 0.002, [&](int i) { UserCmd c; if (i == 0) { c.yaw = yaw; c.pitch = pitch; c.buttons = BTN_GRAPPLE; } return c; });
        caught = (a.predicted().flags & MF_HOOKED) != 0;
    }
    const uint8_t predicted = a.predicted().hookTarget;
    pump(s, cls, 0.1, [&](int i) { UserCmd c; if (i == 0) { c.yaw = yaw; c.pitch = pitch; c.buttons = BTN_GRAPPLE; } return c; });
    replays = a.lastReplayCount();
    printf("  predicted hook target %d (want %d), server %d, last replay %u\n", (int)predicted, ib + 1, (int)s.players_[ia].move.hookTarget, replays);
    CHECK(caught && predicted == ib + 1 && s.players_[ia].move.hookTarget == ib + 1);
    s.stop();
}

// T3 / T4: the dash jump's 0.1 s window for a ground dash aimed a hair up, and for held-jump hoppers.
static void testDashJumpWindows() {
    printf("dash jump windows\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -900 -1 -900 900 0 900\nspawn any 0 0 0\n", &err));
    auto hs = [](const MoveState& q) { return std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z); };
    auto step = [&](MoveState& q, UserCmd c) { c.msec = 1; pmove(q, c, m); };
    auto standing = [&] { MoveState q; q.flags = MF_ONGROUND; for (int i = 0; i < 50; i++) step(q, UserCmd()); return q; };
    // T3: dash on the floor aimed 0.5 deg up (it leaves the floor at once), jump 30 ms in: the 24 m/s dash jump
    for (float up : {0.0087f, 0.05f}) {
        MoveState q = standing();
        UserCmd d; d.buttons = BTN_DASH; d.pitch = up;
        for (int i = 0; i < 30; i++) step(q, d);
        CHECK(!(q.flags & MF_ONGROUND) && q.dashTimer > 0);
        d.buttons |= BTN_JUMP;
        step(q, d);
        printf("  ground dash %.1f deg up, jump 30 ms in: %.2f m/s, %.2f up\n", up * 57.3f, hs(q), q.vel.y);
        CHECK(q.dashTimer == 0 && std::fabs(hs(q) - 24.0f) < 0.01f && q.vel.y > 7.0f && (q.events & ME_SUPER));
    }
    {   // ...but not past the window (a dash started in the air has none)
        MoveState q = standing();
        q.pos.y = 3; q.flags = 0;
        UserCmd d; d.buttons = BTN_DASH; d.pitch = 0.05f;
        for (int i = 0; i < 30; i++) step(q, d);
        d.buttons |= BTN_JUMP;
        step(q, d);
        CHECK(q.dashTimer > 0);
    }
    // T4: holding jump (a hop off the floor, no fresh press), dash 60 ms after leaving it: a dash jump, as after a press
    {
        MoveState q = standing();
        q.flags |= MF_JUMPHELD;   // (jump held from before: this hop is no fresh press)
        UserCmd j; j.buttons = BTN_JUMP;
        step(q, j);
        CHECK((q.events & ME_JUMP) && !(q.flags & MF_ONGROUND));
        for (int i = 0; i < 60; i++) step(q, j);   // 60 ms up
        UserCmd dj; dj.buttons = BTN_JUMP | BTN_DASH;
        step(q, dj);
        printf("  held-jump hop, dash 60 ms after: %.2f m/s\n", hs(q));
        CHECK(q.dashTimer == 0 && std::fabs(hs(q) - 24.0f) < 0.01f && (q.events & ME_SUPER));
    }
}

// Grapple held against a wall (owner 1.18.32: "it plays the sound like 100 times per second"): the anchor is inside the
// release distance, so the grab lets go in its own step; it must start the cooldown, not grab again every 1 ms step.
static void testGrappleAgainstWall() {
    printf("grapple against a wall: one grab per cooldown\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -10 -1 -10 10 0 10\nbox -10 0 -1.5 10 4 -1\nspawn any 0 0 0\n", &err));   // (a wall 1 m ahead, along -z)
    MoveState s; s.flags = MF_ONGROUND;
    UserCmd c; c.msec = 1; c.buttons = BTN_GRAPPLE;   // (yaw 0, level: straight at the wall)
    int grabs = 0, firstMs = -1, secondMs = -1;
    for (int t = 0; t < 1500; t++) {
        s.events = 0;
        pmove(s, c, m);
        if (s.events & ME_HOOK) { if (grabs == 0) firstMs = t; else if (grabs == 1) secondMs = t; grabs++; }
    }
    printf("  %d grabs in 1.5 s held (at %d ms and %d ms)\n", grabs, firstMs, secondMs);
    CHECK(grabs == 2 && firstMs == 0 && secondMs >= 990 && secondMs <= 1010);   // (the 1 s cooldown between them)
    CHECK(!(s.flags & MF_HOOKED));
    {   // nograpple: the same wall made of it is never grabbed, but still blocks you like a wall
        Map n;
        CHECK(n.parse("box -10 -1 -10 10 0 10\nnograpple -10 0 -1.5 10 4 -1\nspawn any 0 0 0\n", &err) && n.brushes[1].material == kMatNoGrapple);
        MoveState q; q.flags = MF_ONGROUND;
        int g = 0;
        for (int t = 0; t < 1500; t++) { q.events = 0; pmove(q, c, n); g += (q.events & ME_HOOK) != 0; }
        CHECK(g == 0 && !(q.flags & MF_HOOKED));
        UserCmd w; w.msec = 8; w.forward = 127;   // walk into it
        for (int t = 0; t < 300; t++) pmove(q, w, n);
        CHECK(q.pos.z > -1.0f + kPlayerHalfWidth - 0.01f);
        CHECK(n.raycast(Vec3(0, 1, 0), Vec3(0, 0, -1), 50) > 0.9f);   // shots and sight stop at it
    }
    {   // clip refuses it too (owner, 2026-10-30): a clip wall in front of a grabbable one, the hook hits the clip and lets go
        Map n;
        CHECK(n.parse("box -10 -1 -10 10 0 10\nclip -10 0 -1.5 10 4 -1\nbox -10 0 -4 10 4 -3.5\nspawn any 0 0 0\n", &err) && n.brushes[1].material == 9);
        MoveState q; q.flags = MF_ONGROUND;
        int g = 0;
        for (int t = 0; t < 1500; t++) { q.events = 0; pmove(q, c, n); g += (q.events & ME_HOOK) != 0; }
        CHECK(g == 0 && !(q.flags & MF_HOOKED));
        CHECK(n.raycast(Vec3(0, 1, 0), Vec3(0, 0, -1), 50) > 2.9f);   // (shots still go through it)
    }
}

// Liquids (472-496, owner 2026-10-29): not solid; each moves you its own way in pmove's 1 ms steps (kLiquids), damage
// and healing counted like acid, the killing ones are lava.
// The see-through looks (537-561, owner 2026-10-29): each stops what kSemi says. Glass stops players and shots, not sight;
// a force field players only, a laser barrier shots only, smoke sight only, a water curtain nothing.
static void testSeeThrough() {
    printf("see-through: glass, force field, laser barrier, smoke, water curtain\n");
    Map m;
    CHECK(m.parse("box -40 -1 -40 40 0 40\nbluetintedglass -1 0 -10 1 3 -9.9\nhexforcefield -1 0 -5 1 3 -4.9\nredlaserbarrier -1 0 0 1 3 0.1\n"
                  "smokewall -1 0 5 1 3 5.1\nwatercurtain -1 0 10 1 3 10.1\nspawn any 0 0 -20\n"));
    auto blocksPlayer = [&](float z) { return m.boxSolid({{-0.3f, 0.5f, z - 0.3f}, {0.3f, 1.5f, z + 0.3f}}); };
    auto blocksShot = [&](float z) { return m.raycast({0, 1, z - 1}, {0, 0, 1}, 2) >= 0; };
    auto blocksSight = [&](float z) { return m.raycastAny({0, 1, z - 1}, {0, 0, 1}, 2); };
    CHECK(blocksPlayer(-9.95f) && blocksShot(-9.95f) && !blocksSight(-9.95f));   // glass
    CHECK(blocksPlayer(-4.95f) && !blocksShot(-4.95f) && !blocksSight(-4.95f));   // force field
    CHECK(!blocksPlayer(0.05f) && blocksShot(0.05f) && !blocksSight(0.05f));      // laser barrier
    CHECK(!blocksPlayer(5.05f) && !blocksShot(5.05f) && blocksSight(5.05f));      // smoke
    CHECK(!blocksPlayer(10.05f) && !blocksShot(10.05f) && !blocksSight(10.05f));  // water curtain
    CHECK(!litMaterial(kMatSemi) && !bakedMaterial(kMatSemi) && semiMaterial(kMatSemi + kSemiCount - 1) && !semiMaterial(kMatBreak));
}

// The 10 feature blocks (586-595, owner 2026-10-29): healing / spikes underfoot, low gravity, the gravity flip, the
// shield wall, the checkpoint gate, slow-mo projectiles and the teleporter (players; its link and face in the file)
static void testFeatureBlocks() {
    printf("feature blocks: heal, spikes, low gravity, gravity flip, shield, gate, slow-mo, teleporter\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -60 -1 -60 60 0 60\nhealingtile -30 0 -3 -20 0.1 3\nspikefloor -10 0 -3 0 0.1 3\nlowgravityfield 10 0 -3 20 20 3\n"
                  "gravityflip 25 0 -3 35 0.1 3\nbox 25 5 -3 35 6 3\nshieldwall -1 0 20 1 3 21\ncheckpointgate -5 0 40 5 4 41\n"
                  "slowmofield -50 0 -50 -40 10 -40\nteleporter -30 0 30 -29 3 32 tp 7 0\nteleporter 40 0 30 42 1 32 tp 7 2\n"
                  "spawn any 0 0 -20\nspawn race 0 0 -20\n", &err));
    auto stand = [&](Vec3 at, int ms) {
        MoveState s; s.flags = MF_ONGROUND; s.pos = at;
        UserCmd c; c.msec = 8;
        for (int t = 0; t < ms; t += 8) pmove(s, c, m);
        return s;
    };
    CHECK(std::fabs(stand({-25, 0.1f, 0}, 1000).acidTaken + kHealTilePerSec) < 0.01f);
    CHECK(std::fabs(stand({-5, 0.1f, 0}, 1000).acidTaken - kSpikePerSec) < 0.01f);
    auto peak = [&](Vec3 at) {
        MoveState s; s.flags = MF_ONGROUND; s.pos = at;
        UserCmd c; c.msec = 4; c.buttons = BTN_JUMP;
        float top = at.y;
        for (int t = 0; t < 2000; t += 4) { pmove(s, c, m); c.buttons = 0; top = std::max(top, s.pos.y); }
        return top - at.y;
    };
    const float hiLow = peak({15, 0, 0}), hi = peak({0, 0, -40});
    printf("  jump: %.2f m, in low gravity %.2f m\n", hi, hiLow);
    CHECK(hiLow > 1.8f * hi);
    {   // the flip: up to the ceiling over it and standing there
        const MoveState s = stand({30, 0.1f, 0}, 1500);
        const AABB b = playerBox(s.pos, false, 1.0f);
        printf("  gravity flip: top of the hull %.2f (ceiling 5), on it %d\n", b.mx.y, (int)((s.flags & MF_ONGROUND) != 0));
        CHECK(std::fabs(b.mx.y - 5.0f) < 0.06f && (s.flags & MF_ONGROUND));
    }
    CHECK(solidMaterial(kMatShield) && !stopsPlayers(kMatShield) && stopsShots(kMatShield) && !stopsSight(kMatShield));
    CHECK(!m.boxSolid({{-0.5f, 0.1f, 20.2f}, {0.5f, 1.5f, 20.8f}}));
    { const float t = m.raycast({0, 1, 15}, {0, 0, 1}, 10.0f); CHECK(t > 4.9f && t < 5.1f); }
    CHECK(!solidMaterial(kMatGate) && m.raceCheckpoints.size() == 1 && m.checkpoints.empty() && m.supportsMode(MODE_RACE));
    CHECK(m.projectileRate({-45, 5, -45}) == kSlowMoRate && m.projectileRate({0, 5, 0}) == 1.0f);
    {   // flyProjectile: through the slow-mo field half as far
        Vec3 a = {-49, 5, -45}, va = {4, 0, 0}, b = {-49, 5, 0}, vb = {4, 0, 0};
        m.flyProjectile(a, va, 0, 1.0f); m.flyProjectile(b, vb, 0, 1.0f);
        CHECK(std::fabs(a.x + 47) < 0.05f && std::fabs(b.x + 45) < 0.05f);
    }
    {   // a teleporter: walk into it, out of the other one's top going up
        MoveState s; s.flags = MF_ONGROUND; s.pos = {-31, 0, 31}; s.vel = {8, 0, 0};
        UserCmd c; c.msec = 8; c.forward = 127; c.yaw = -1.5708f;
        for (int t = 0; t < 400 && s.pos.x < 0; t += 8) pmove(s, c, m);
        printf("  teleporter: at %.2f %.2f %.2f, vel y %.2f\n", s.pos.x, s.pos.y, s.pos.z, s.vel.y);
        CHECK(s.pos.x > 40 && s.pos.x < 42 && s.pos.y >= 1.0f);
    }
    {   // into its wall at 60 m/s (in the air): out at 60, not the speed the slide left after clipping it (owner, 2026-10-30)
        int kept = 0;
        float worst = 1e9f;
        for (int k = 0; k < 8; k++) {   // (from 8 starting points: whether a step ends just short of the wall or in it)
            MoveState s; s.pos = {-32.0f + k * 0.0075f, 0.5f, 31}; s.vel = {60, 0, 0};
            UserCmd c; c.msec = 1;
            bool out = false;
            for (int t = 0; t < 60 && !out; t++) { pmove(s, c, m); out = s.pos.x > 30; }
            kept += out && length(s.vel) > 59.0f;
            worst = std::min(worst, out ? length(s.vel) : 0.0f);
        }
        printf("  teleporter at 60 m/s: %d of 8 kept their speed (slowest %.2f)\n", kept, worst);
        CHECK(kept == 8);
    }
    {   // a walk teleporter (map format 12, owner 2026-10-30): the same run into it at 60 m/s comes out at the run speed,
        // along the exit face; walked into at less, out at no more than you went in
        Map w;
        std::string src = m.serialize();
        const size_t at = src.find("tp 7 0\n");
        CHECK(at != std::string::npos);
        src.insert(at + 6, " walk");
        CHECK(w.parse(src, &err) && w.brushes[9].tpWalk && !w.brushes[10].tpWalk);
        const float run = moveBaseSpeed();
        int capped = 0;
        float lo = 1e9f, hi = 0;
        for (int k = 0; k < 8; k++) {
            MoveState s; s.pos = {-32.0f + k * 0.0075f, 0.5f, 31}; s.vel = {60, 0, 0};
            UserCmd c; c.msec = 1;
            bool out = false;
            for (int t = 0; t < 60 && !out; t++) { pmove(s, c, w); out = s.pos.x > 30; }
            const float v = out ? length(s.vel) : 0.0f;
            capped += out && std::fabs(v - run) < 0.3f && s.vel.y > 0.9f * v;
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        printf("  walk teleporter at 60 m/s: %d of 8 out at the run speed %.2f (%.2f..%.2f)\n", capped, run, lo, hi);
        CHECK(capped == 8);
        {   // slower than the run speed: kept (never faster than you went in)
            MoveState s; s.pos = {-31.2f, 0.5f, 31}; s.vel = {4, 0, 0};
            UserCmd c; c.msec = 1;
            bool out = false;
            float in = 0;   // (the speed going in: 4 m/s and what the fall added)
            for (int t = 0; t < 400 && !out; t++) { in = length(s.vel); pmove(s, c, w); out = s.pos.x > 30; }
            printf("  walk teleporter at 4 m/s: out %d at %.2f (in at %.2f)\n", (int)out, length(s.vel), in);
            CHECK(out && in < moveBaseSpeed() && length(s.vel) < in + 0.05f);
        }
        Map r;   // written back with the word; an edit carries it
        CHECK(w.serialize().find("tp 7 0 walk\n") != std::string::npos && r.parse(w.serialize(), &err) && r.brushes[9].tpWalk);
        CHECK(describeMapObject(w, 'b', "set", 9).find(" p 7 0 walk") != std::string::npos);
        Map e = m;
        CHECK(applyMapEdit(e, describeMapObject(w, 'b', "set", 9)) && e.brushes[9].tpWalk);
        CHECK(applyMapEdit(e, describeMapObject(m, 'b', "set", 9)) && !e.brushes[9].tpWalk);
        Map bad;
        CHECK(!bad.parse("box 0 0 0 1 1 1\nteleporter 0 0 0 1 1 1 tp 7 2 run\nspawn any 0 0 0\n", &err));   // (only "walk")
    }
    {   // its link and face in the file, and in an edit
        Map r;
        CHECK(r.parse(m.serialize(), &err) && r.brushes[10].link == 7 && r.brushes[10].exitFace == 2 && r.brushes[9].exitFace == 0);
        const std::string d = describeMapObject(m, 'b', "set", 10);
        CHECK(d.find(" p 7 2") != std::string::npos);
        Map bad;
        CHECK(!bad.parse("box 0 0 0 1 1 1\nteleporter 0 0 0 1 1 1 tp 7 6\nspawn any 0 0 0\n", &err));   // (no face 6)
    }
}

// A/D strafing at speed (owner, 2026-10-29): a perfect strafer (each ms the yaw that gains most) from 15 m/s, in the
// air: the acceleration's fall-off is gentler above kStrafeEaseFrom, more so in surf (MOVE_RUN_ONLY)
static void testStrafeEase() {
    printf("strafe ease: a perfect A/D strafer's speed, normal and surf\n");
    Map open;
    std::string err;
    CHECK(open.parse("box -4000 -1 -4000 4000 0 4000\nspawn any 0 0 0\n", &err));
    auto run = [&](uint8_t mods, float secs) {
        MoveState s; s.pos = {0, 3000, 0}; s.vel = {0, 0, -15.0f}; s.mods = mods;
        UserCmd c; c.msec = 1; c.side = 127;
        float yaw = 0;
        for (int t = 0; t < (int)(secs * 1000); t++) {
            float best = -1, by = yaw;
            for (int k = 0; k <= 40; k++) {   // (around the velocity's own heading: the best of 41 yaws)
                const float y = std::atan2(-s.vel.x, -s.vel.z) + (float)(k - 20) * 0.004f;
                MoveState q = s; UserCmd u = c; u.yaw = y;
                pmove(q, u, open);
                const float h = std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z);
                if (h > best) { best = h; by = y; }
            }
            yaw = by; c.yaw = yaw;
            pmove(s, c, open);
            s.pos = {0, 3000, 0}; s.vel.y = 0;   // (stays in the air, in the map)
        }
        return std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
    };
    const float n5 = run(0, 5), n15 = run(0, 15), s5 = run(MOVE_RUN_ONLY, 5), s15 = run(MOVE_RUN_ONLY, 15);
    printf("  from 15 m/s: normal %.1f m/s after 5 s, %.1f after 15 s; surf %.1f, %.1f\n", n5, n15, s5, s15);
    CHECK(n15 > n5 && s5 > n5 && s15 > n15 * 1.2f);
}

static void testLiquids() {
    printf("liquids: slower / slippery / current / bounce / heal / hurt / kill, walk-through\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -40 -1 -40 40 0 40\nhoney -30 0 -3 -20 1 3\noilslick -10 0 -3 0 1 3\nwhiterapids 10 0 -3 30 1 3\n"
                  "pinkslime -30 0 10 -20 3 20\nhotspring 0 0 10 5 1 15\nplasmapool 10 0 10 15 1 15\nmoltengold 20 0 10 25 1 15\nspawn any 0 0 -20\n", &err));
    CHECK(m.liquids.size() == 7 && m.lava.size() == 1 && !solidMaterial(kMatLiquids) && solidMaterial(kMatLiquids - 1));
    CHECK(!m.boxSolid({{-26, 0.1f, -1}, {-24, 1.5f, 1}}));   // (you wade in)
    auto run = [&](Vec3 at, int ms, int forward) {
        MoveState s; s.flags = MF_ONGROUND; s.pos = at;
        UserCmd c; c.msec = 8; c.forward = (int8_t)forward; c.yaw = 1.5708f;   // (yaw 90 deg: forward is -x)
        for (int t = 0; t < ms; t += 8) pmove(s, c, m);
        return s;
    };
    const MoveState dry = run({0, 0, -30}, 1000, 127), honey = run({-22, 0, 0}, 1000, 127);
    printf("  run 1 s: dry %.2f m/s, honey %.2f m/s\n", length(dry.vel), length(honey.vel));
    CHECK(length(honey.vel) < 0.6f * length(dry.vel));
    {   // oil: let go at speed, you keep sliding far longer than on the floor
        MoveState a = run({-1, 0, 0}, 500, 127), b = run({-1, 0, -30}, 500, 127);
        a = [&] { MoveState s = a; UserCmd c; c.msec = 8; for (int t = 0; t < 300; t += 8) pmove(s, c, m); return s; }();
        b = [&] { MoveState s = b; UserCmd c; c.msec = 8; for (int t = 0; t < 300; t += 8) pmove(s, c, m); return s; }();
        CHECK(length(a.vel) > 2.0f * length(b.vel) + 0.5f);
    }
    { const MoveState r = run({12, 0, 0}, 1000, 0); printf("  rapids: x %.2f vx %.2f\n", r.pos.x, r.vel.x); CHECK(r.pos.x > 15.0f); }   // the rapids carry you along +x
    {   // slime: a fall into it comes back up
        MoveState s; s.pos = {-25, 2.9f, 15}; s.vel = {0, -8, 0};
        UserCmd c; c.msec = 1; pmove(s, c, m);
        CHECK(s.vel.y > 5.0f);
    }
    CHECK(run({2, 0, 12}, 1000, 0).acidTaken < -0.04f);   // hot spring: 5% healed a second
    CHECK(std::fabs(run({12, 0, 12}, 1000, 0).acidTaken - 0.2f) < 0.01f);   // plasma: 20% a second
    CHECK(m.inLava({{21, 0.1f, 11}, {22, 1, 12}}));   // molten gold kills
}

// Acid (map material 11): 10% of max health on contact, then 10% a second while touching it, counted in pmove's 1 ms
// steps (the same at every tick rate, and the same on the client's prediction as on the server); a new contact only
// after kAcidRearmMs fully out; on the wire while it runs (protocol 45); the server takes the health, pauses regen,
// and alone decides the death ("dissolved", the knocker's kill however long it took). Not in the freeze.
static void testAcid() {
    printf("acid: contact hit, 10%% a second at any tick rate, re-contact, the wire, the server's health and death\n");
    const char* text = "box -20 -1 -20 20 0 20\nacid -3 0 -3 3 0.25 3\nspawn any 0 0 -10\n";
    Map m;
    std::string err;
    CHECK(m.parse(text, &err) && m.acid.size() == 1);
    auto still = [](int ms) { UserCmd c; c.msec = (uint8_t)ms; return c; };
    auto stand = [&](MoveState& s, int ms, int cmdMs) { for (int t = 0; t < ms; t += cmdMs) pmove(s, still(cmdMs), m); };
    for (int rate : {1000, 500, 250, 125}) {   // one second standing in it, at each tick rate's command length
        MoveState s; s.flags = MF_ONGROUND;
        stand(s, 1000, 1000 / rate);
        printf("  %4d Hz: %.5f of max health in 1 s (contact + 1 s)\n", rate, s.acidTaken);
        CHECK(std::fabs(s.acidTaken - (kAcidContact + kAcidPerSec)) < 1e-4f && s.acidOutMs == 0);
    }
    {   // the contact hit alone on entry; nothing outside it; back in after 0.4 s out: no new contact, after 0.5 s: one
        MoveState s; s.flags = MF_ONGROUND; s.pos = {0, 0, 10};
        stand(s, 200, 8);
        CHECK(s.acidTaken == 0 && s.acidOutMs == kAcidRearmMs);
        s.pos = {0, 0, 0}; stand(s, 1, 1);
        CHECK(std::fabs(s.acidTaken - (kAcidContact + kAcidPerSec * 0.001f)) < 1e-6f);
        float before = s.acidTaken;
        s.pos = {0, 0, 10}; stand(s, 400, 8);
        CHECK(s.acidTaken == before && s.acidOutMs == 400);
        s.pos = {0, 0, 0}; stand(s, 1, 1);
        CHECK(std::fabs(s.acidTaken - before - kAcidPerSec * 0.001f) < 1e-6f);   // (a bumpy edge: no second contact)
        before = s.acidTaken;
        s.pos = {0, 0, 10}; stand(s, 504, 8);
        CHECK(s.acidOutMs == kAcidRearmMs);
        s.pos = {0, 0, 0}; stand(s, 1, 1);
        CHECK(std::fabs(s.acidTaken - before - kAcidContact - kAcidPerSec * 0.001f) < 1e-6f);
    }
    {   // the wire: acidOutMs rides on mods bit 64 while it's below kAcidRearmMs (2 bytes); protocol 44 reads as before
        auto roundTrip = [](const MoveState& in, uint16_t proto, MoveState& out, int& size) {
            Snapshot sn; sn.tickRate = 1000; sn.self = in;
            uint8_t buf[512];
            Writer w(buf, sizeof buf);
            writeSnapshotClient(w, sn);
            size = w.size();
            Reader r(buf, w.size());
            Snapshot back;
            bool ok = readSnapshotClient(r, back, proto);
            out = back.self;
            return ok;
        };
        MoveState a; a.mods = MOVE_TINY;
        MoveState got; int n0 = 0, n1 = 0;
        CHECK(roundTrip(a, kProtocolVersion, got, n0) && got.acidOutMs == kAcidRearmMs && got.mods == MOVE_TINY);
        CHECK(roundTrip(a, 44, got, n0) && got.acidOutMs == kAcidRearmMs && got.mods == MOVE_TINY);   // (the bytes are protocol 44's)
        a.acidOutMs = 123; a.acidTaken = 0.5f;
        CHECK(roundTrip(a, kProtocolVersion, got, n1) && got.acidOutMs == 123 && got.mods == MOVE_TINY && got.acidTaken == 0 && n1 == n0 + 2);
        CHECK(demo::convertible(44) && kSnapAcidSince <= kProtocolVersion);
    }
    auto fresh = [&](Server& s, int n) {
        s.setLog([](const std::string&) {});
        CHECK(s.map_.parse(text));
        s.cfg_.mode = s.ms_.mode = MODE_FFA;
        s.ms_.phase = PHASE_LIVE;
        s.timeMs_ = 10000;
        for (int i = 0; i < n; i++) {
            auto& p = s.players_[i];
            p.connected = true; p.alive = true; p.health = s.lifeMaxHealth(); p.team = TEAM_A; p.joinId = 100 + i;
            p.move = MoveState(); p.move.flags = MF_ONGROUND; p.move.pos = {0, 0, 10.0f + 2 * i};
        }
    };
    auto run = [](Server& s, int id, int ms, int cmdMs, MoveState* client = nullptr) {   // standing still, ms of it
        for (int t = 0; t < ms && s.players_[id].alive; t += cmdMs) {
            UserCmd c; c.msec = (uint8_t)cmdMs;
            s.timeMs_ += (uint64_t)cmdMs;
            s.runPlayerCmdForTest(id, c);
            if (client) pmove(*client, c, s.map_);   // (the client's prediction of the same commands)
        }
    };
    for (float maxHp : {100.0f, 500.0f})
        for (int cmdMs : {1, 8}) {   // the server takes what pmove counted: the same health at 1000 Hz and 125 Hz
            Server s;
            if (maxHp != 100.0f) { s.cfg_.custom.name = "acidtest"; s.cfg_.custom.health = (int)maxHp; }
            fresh(s, 1);
            CHECK(s.lifeMaxHealth() == maxHp && s.players_[0].health == maxHp);
            s.players_[0].move.pos = {0, 0, 0};
            MoveState client = s.players_[0].move;
            run(s, 0, 1000, cmdMs, &client);
            const float lost = maxHp - s.players_[0].health;
            printf("  %3.0f max health, %d ms commands: %.3f lost in 1 s (predicted %.3f)\n", maxHp, cmdMs, lost, client.acidTaken * maxHp);
            CHECK(std::fabs(lost - maxHp * (kAcidContact + kAcidPerSec)) < maxHp * 1e-4f);
            CHECK(std::fabs(client.acidTaken * maxHp - lost) < maxHp * 1e-4f);   // the prediction showed the same
            CHECK(s.players_[0].move.acidTaken == 0 && s.players_[0].lastDamagedAt >= s.time() - 0.01);   // (regen waits)
        }
    {   // the death: about 9 s in (10% then 10% a second), the server's alone, "dissolved", nobody's kill
        Server s; fresh(s, 1);
        s.players_[0].move.pos = {0, 0, 0};
        const uint64_t t0 = s.timeMs_;
        run(s, 0, 20000, 1);
        const double took = (s.timeMs_ - t0) / 1000.0;
        printf("  dissolved after %.3f s in it\n", took);
        CHECK(!s.players_[0].alive && std::fabs(took - 9.0) < 0.01);
        int kills = 0;
        for (auto& e : s.players_[0].events)
            if (e.type == EV_KILL) { kills++; CHECK(e.a == 255 && (e.c & KILL_WEAPON) == kDamageAcid); }
        CHECK(kills == 1 && s.players_[0].deaths == 1);
    }
    {   // knocked in: the knocker's kill, though the death comes long after kRingOutCredit
        Server s; fresh(s, 2);
        s.damagePlayer(0, 1, 5, WPN_ROCKET, Vec3());
        s.timeMs_ += 1000;
        s.players_[0].move.pos = {0, 0, 0};
        run(s, 0, 20000, 8);
        CHECK(!s.players_[0].alive && s.players_[1].kills == 1);
        bool credited = false;
        for (auto& e : s.players_[1].events) credited |= e.type == EV_KILL && e.a == 1 && e.b == 0 && (e.c & KILL_WEAPON) == kDamageAcid;
        CHECK(credited);
    }
    {   // the freeze: no damage (nativeCanDamage's phases), and nothing saved up for later
        Server s; fresh(s, 1);
        s.ms_.phase = PHASE_FREEZE;
        s.players_[0].move.pos = {0, 0, 0};
        run(s, 0, 500, 8);
        CHECK(s.players_[0].health == 100 && s.players_[0].move.acidTaken == 0);
    }
    {   // bots: a pool on the floor is wet ground (dearer), not a hole: routes still cross it when there's no way round
        NavGraph g;
        g.build(m);
        int wet = 0;
        for (auto& nd : g.nodes) wet += nd.wet;
        const int a = g.nodeAt({0, 0, -10}), b = g.nodeAt({0, 0, 0});
        std::vector<int> path;
        CHECK(wet >= 1 && a >= 0 && b >= 0 && g.nodes[b].wet && !g.nodes[a].wet && g.findPath(a, b, path));
        Map corridor;   // a 3 m wide corridor flooded across: the only way, so the route goes through
        CHECK(corridor.parse("box -1.5 -1 -20 1.5 0 20\nbox -2.5 0 -20 -1.5 4 20\nbox 1.5 0 -20 2.5 4 20\nacid -1.5 0 -2 1.5 0.2 2\nspawn any 0 0 -10\n"));
        NavGraph h;
        h.build(corridor);
        const int c0 = h.nodeAt({0, 0, -10}), c1 = h.nodeAt({0, 0, 10});
        CHECK(c0 >= 0 && c1 >= 0 && h.findPath(c0, c1, path));
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
        CHECK(strafeAt(720.0f) > run * 0.65f);   // overstrafing (way too fast) loses some speed (the old air control: 5.0 m/s of 8; 1.17: 6.6 of 9.5)
    }
    // a dash leaves you faster than you started (it used to settle back down): in the air your speed + 35% of what's left
    // up to 32 (owner 2026-10-28), at least + 1 (1.17: was + 3)
    MoveState d = b; d.dashCharges = 2; d.dashCooldownMs = 0; d.flags &= ~MF_DASHHELD;
    float before = hspeed(d);
    UserCmd dc; dc.msec = 1; dc.yaw = k.yaw; dc.forward = 127; dc.buttons = BTN_DASH;
    for (int i = 0; i < 200; i++) pmove(d, dc, open);
    printf("  dash: %.1f -> %.1f m/s after the burst\n", before, hspeed(d));
    { const float want = before + std::max(1.0f, 0.35f * (32.0f - before)); CHECK(hspeed(d) > want - 0.1f && hspeed(d) < want + 0.1f); }
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


// The 1.17 movement numbers (owner): run 9.5 m/s at 16 accel, ladders 6.75 / 4.5, dash keep 1, air control capped
// at what strafing turns (falls off as 1 / speed above 9.5 m/s), air drift 8 m/s2 under run speed, strafe boost 0.1.
// 1.18 (owner): W-only air control x 0.9; a strafe key held: the air strafe cap x 1.1 (2.53) and air acceleration
// x 1.15 (the boost ramp still from 12.65 m/s).
static void testMovementTuning() {
    printf("movement tuning (1.17)\n");
    std::string err;
    Map open;
    CHECK(open.parse("box -500 -1 -500 500 0 500\nspawn any 0 0 0\n", &err));
    auto hs = [](const MoveState& q) { return std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z); };
    auto step = [&](MoveState& q, UserCmd c, const Map& m) { c.msec = 1; pmove(q, c, m); };
    auto standing = [&] { MoveState q; q.flags = MF_ONGROUND; for (int i = 0; i < 50; i++) step(q, UserCmd(), open); return q; };
    { // the run: 9.5 m/s top speed; 90% of it in ~72 ms (dv/dt = 16 x 9.5 - 7 v)
        MoveState q = standing();
        UserCmd r; r.forward = 127;
        int t90 = -1, tTop = -1;
        for (int t = 1; t <= 1000; t++) {
            step(q, r, open);
            if (t90 < 0 && hs(q) >= 0.9f * 9.5f) t90 = t;
            if (tTop < 0 && hs(q) >= 9.5f - 1e-4f) tTop = t;
        }
        printf("  run: %.4f m/s, 90%% in %d ms, top in %d ms\n", hs(q), t90, tTop);
        CHECK(std::fabs(hs(q) - 9.5f) < 1e-4f && t90 >= 68 && t90 <= 76 && tTop >= 78 && tTop <= 88);
        // a running jump: 0.667 s in the air at 9.5 m/s, ~6.3 m (it was 5.3 at 8)
        UserCmd j = r; j.buttons = BTN_JUMP;
        Vec3 p0 = q.pos;
        step(q, j, open);
        for (int i = 0; i < 2000 && !(q.flags & MF_ONGROUND); i++) step(q, r, open);
        float reach = length(q.pos - p0);
        printf("  running jump: %.2f m, landing at %.3f m/s\n", reach, hs(q));
        CHECK(reach > 5.85f && reach < 6.15f && std::fabs(hs(q) - 9.5f) < 1e-3f);
    }
    { // ladders: climb at 6.75 m/s, sideways at 4.5
        Map m;
        CHECK(m.parse("box -20 -1 -20 20 0 20\nbox -20 0 -3 20 30 -2\nladder -5 0 -2 5 30 -1.5\nspawn any 0 0 0\n", &err));
        MoveState q; q.pos = {0, 1, -1.6f};
        UserCmd c; c.forward = 127;   // (yaw 0: into the ladder, -z)
        for (int i = 0; i < 20; i++) step(q, c, m);
        printf("  ladder: climb %.3f m/s", q.vel.y);
        CHECK(std::fabs(q.vel.y - 6.75f) < 1e-4f);
        c.forward = 0; c.side = 127;
        step(q, c, m);
        printf(", sideways %.3f m/s\n", q.vel.x);
        CHECK(std::fabs(q.vel.x - 4.5f) < 1e-4f && q.vel.y == 0);   // (T6: A/D only sideways; any key climbed before)
        c.side = 0; c.forward = -127;   // T6: S climbs down (and backs you off the wall, as before)
        step(q, c, m);
        CHECK(std::fabs(q.vel.y + 6.75f) < 1e-4f && std::fabs(q.vel.z - 4.5f) < 1e-4f);   // (and backs off the wall, as before)
        c.forward = 0; c.buttons = BTN_JUMP;   // the kick-off is unchanged: 4 m/s back, 0.7 of a jump up
        step(q, c, m);
        CHECK(std::fabs(q.vel.z - 4.0f) < 1e-3f && q.vel.y > moveBaseJump() * 0.7f - 0.05f && q.vel.y <= moveBaseJump() * 0.7f);
    }
    { // an air dash keeps 35% of the gap to 32 (at least 1 m/s): the exit is max(11, speed + max(1, 0.35 (32 - speed)))
      // up to 32; chained from a standstill: 11.2, 18.48, 23.21, 26.29 (owner 2026-10-28; it was 11, 12, 13, 14)
        MoveState q = standing();
        UserCmd d; d.buttons = BTN_DASH;
        std::vector<float> exits;
        for (int k = 0; k < 4; k++) {
            q.pos.y = 20; q.flags = 0; q.vel.y = 0; q.dashCharges = 2; q.dashCooldownMs = 0; q.flags &= ~MF_DASHHELD;   // (in the air: no friction between dashes; the cooldown skipped)
            step(q, d, open);
            UserCmd none;
            while (q.dashTimer > 0) step(q, none, open);
            exits.push_back(hs(q));
            step(q, none, open);   // (release the key)
        }
        printf("  chained dashes: %.2f %.2f %.2f %.2f m/s\n", exits[0], exits[1], exits[2], exits[3]);
        float want = 0;
        for (int k = 0; k < 4; k++) { want = std::max(11.0f, want + std::max(1.0f, 0.35f * (32.0f - want))); CHECK(std::fabs(exits[k] - want) < 0.02f); }
        MoveState f; f.pos = {0, 20, 0}; f.vel = {0, 0, -31.5f};
        step(f, d, open);
        while (f.dashTimer > 0) step(f, UserCmd(), open);
        CHECK(std::fabs(hs(f) - 32.0f) < 0.02f);   // (the cap)
    }
    { // air control: W-only bends momentum at 0.9 x min(4, 4 x 9.5 / speed) rad/s (owner: it loses to A/D from 9.5 m/s;
      // 1.18: 10% slower): 3.42 at 10 m/s, 2.14 at 16, 1.07 at 32
        for (float v : {10.0f, 16.0f, 32.0f}) {
            MoveState q; q.pos = {0, 50, 0}; q.vel = {0, 0, -v};
            UserCmd c; c.forward = 127; c.yaw = 30 * PI / 180;   // wish 30 deg off: no strafe push (cur > 2.3), no drift (over 9.5)
            Vec3 w = {-std::sin(c.yaw), 0, -std::cos(c.yaw)};
            float rate = 0.9f * std::min(4.0f, 4.0f * 9.5f / v), k = rate * 0.001f;
            Vec3 n = Vec3(0, 0, -1) + (w - Vec3(0, 0, -1)) * k;
            n = n / length(n) * v;
            step(q, c, open);
            float turned = std::atan2(-q.vel.x, -q.vel.z);   // rad in 1 ms
            printf("  air control at %.0f m/s: %.3f rad/s (%.1f deg/s toward a 30 deg target)\n", v, rate, turned * 1000 * 180 / PI);
            CHECK(std::fabs(q.vel.x - n.x) < 1e-4f && std::fabs(q.vel.z - n.z) < 1e-4f && std::fabs(hs(q) - v) < 1e-3f);
        }
    }
    { // drift: a standing jump holding W drifts to ~7 m/s by landing (2.3 of strafe push plus 8 m/s2), never past 9.5
        MoveState q = standing();
        UserCmd j; j.forward = 127; j.buttons = BTN_JUMP;
        step(q, j, open);
        j.buttons = 0;
        float top = 0;
        int air = 1;
        for (; air < 2000 && !(q.flags & MF_ONGROUND); air++) { step(q, j, open); top = std::max(top, hs(q)); }
        printf("  standing jump + W: %.2f m/s by landing (%d ms in the air)\n", top, air);
        CHECK(top > 6.8f && top < 7.5f && air > 610 && air < 660);
        // a long fall holding W: up to run speed and no further
        MoveState f; f.pos = {0, 30, 0};
        UserCmd w; w.forward = 127;
        float most = 0;
        for (int i = 0; i < 2000 && !(f.flags & MF_ONGROUND); i++) { step(f, w, open); most = std::max(most, hs(f)); }
        printf("  falling 30 m holding W: %.4f m/s at most\n", most);
        CHECK(most <= 9.5f + 1e-4f && most > 9.49f);
        // sideways to your momentum under run speed: it pushes along the wish, the total held to 9.5
        MoveState g; g.pos = {0, 30, 0}; g.vel = {0, 0, -9.0f};
        UserCmd sd; sd.side = 127;   // (right: +x)
        for (int i = 0; i < 400; i++) { step(g, sd, open); CHECK(hs(g) <= 9.5f + 1e-4f); }
        CHECK(g.vel.x > 3.0f);
        // over run speed: no drift at all (only the strafe push)
        MoveState o; o.pos = {0, 30, 0}; o.vel = {0, 0, -9.6f};
        UserCmd b; b.forward = -127;   // S: straight against momentum
        step(o, b, open);
        CHECK(std::fabs(o.vel.z - (-9.6f + 22.0f * 2.3f * 0.001f * 0.35f)) < 1e-4f);   // (the overstrafe-softened push only)
    }
    { // strafe boost: a strafe key's push x 1.15 x 1.1 (1.18: the acceleration and the 2.53 cap) x (1 + 0.1 t), t ramping
      // 12.65 -> 15.81 m/s (from the 2.3 cap and the unscaled air control): 1.265 at 12, 1.319 at 14, 1.392 at 16
        for (float v : {12.0f, 14.0f, 16.0f}) {
            MoveState q; q.pos = {0, 50, 0}; q.vel = {0, 0, -v};
            UserCmd c; c.side = 127;   // (yaw 0: right is +x, square to the momentum)
            step(q, c, open);
            // (the push square to the momentum: |v'|^2 = v^2 + push^2, the A/D turn on top only turns it; above 15 m/s the
            // ease, 2026-10-29: x (v / 15)^0.35)
            float gain = std::sqrt(std::max(0.0f, q.vel.x * q.vel.x + q.vel.z * q.vel.z - v * v)) / (22.0f * 2.3f * 0.001f);
            float cross = 22.0f * 2.3f / 4.0f, want = 1.15f * 1.1f * (1 + 0.1f * std::clamp((v - cross) / (0.25f * cross), 0.0f, 1.0f)) *
                                                      (v > 15.0f ? std::pow(v / 15.0f, 0.35f) : 1.0f);
            printf("  strafe push at %.0f m/s: x%.4f (want %.4f)\n", v, gain, want);
            CHECK(std::fabs(gain - want) < 5e-3f);
            // the turn it makes, against W-only air control's (0.9 x min(4, 38 / v) rad/s): A/D ahead from 9.5 m/s (owner)
            float strafeTurn = q.vel.x / v * 1000, control = 0.9f * std::min(4.0f, 4.0f * 9.5f / v);
            printf("    turning: strafe %.0f deg/s, W-only air control %.0f deg/s\n", strafeTurn * 180 / PI, control * 180 / PI);
            CHECK(strafeTurn > control * 1.03f);
        }
        // W-only (no strafe key) keeps the 2.3 cap and the plain acceleration: W square to the momentum pushes 22 x 2.3
        // x 1 ms (A/D's would be 10% more), then air control bends the result toward W at 0.9 x 38 / speed
        MoveState q; q.pos = {0, 50, 0}; q.vel = {0, 0, -12.0f};
        UserCmd c; c.forward = 127; c.yaw = -PI / 2;   // (W facing +x, square to the momentum)
        step(q, c, open);
        const float p = 22.0f * 2.3f * 0.001f, h1 = std::sqrt(p * p + 144.0f), k = 0.9f * std::min(4.0f, 38.0f / h1) * 0.001f;
        Vec3 n = Vec3(p / h1, 0, -12.0f / h1) + (Vec3(1, 0, 0) - Vec3(p / h1, 0, -12.0f / h1)) * k;
        n = n / length(n) * h1;
        printf("  W-only square to 12 m/s: %.5f, %.5f (want %.5f, %.5f)\n", q.vel.x, q.vel.z, n.x, n.z);
        CHECK(std::fabs(q.vel.x - n.x) < 1e-4f && std::fabs(q.vel.z - n.z) < 1e-4f);
    }
}

// Dash charges (owner 1.17): two; a dash spends one; no dash while one runs (the press is dropped, not kept), the next
// press after it ends spends the next; the ground refills both, holding jump (a bhop landing) only up to one; a dash jump
// refills all, a super dash leaves exactly one, a wall bounce exactly one; the super dash's own floor contact is no refill;
// ladders refill nothing. 1.18.7 (owner: "dashes take 1.2 seconds to recharge, but you can use them 0.6 seconds apart,
// so when you jump in the air with 2 dashes, you can use both of them within 1 second, without recharging any
// charges"): a dash waits 0.6 s from the end of the last one, and a spent charge only comes back by those refills
// once 1.2 s have passed since the dash that spent it ended. Quick dash (1.18): the usual two charges, a third of
// both (0.2 s apart, 0.4 s to recharge).
static void testDashCharges() {
    printf("dash charges\n");
    {   // the default: one charge (owner, 2026-11-01: "only 1 dash charge default")
        MoveState d;
        CHECK(kDashCharges == 1 && d.dashCharges == 1 && dashMaxCharges(0) == 1);
    }
    // the charges' rules below with two of them (a custom mode's dash_charges x2): what they do with more than one
    CustomRules twoRules; twoRules.name = "two charges"; twoRules.move[MV_DASH_CHARGES] = 2;
    setActiveRules(twoRules);
    struct Restore { ~Restore() { setActiveRules(CustomRules()); } } restore;
    auto two = [] { MoveState m; m.dashCharges = 2; return m; };
    std::string err;
    Map open;
    CHECK(open.parse("box -500 -1 -500 500 0 500\nspawn any 0 0 0\n", &err));
    auto step = [&](MoveState& q, UserCmd c, const Map& m) { c.msec = 1; pmove(q, c, m); };
    UserCmd none, dash; dash.buttons = BTN_DASH;
    // a press every other ms (fresh each time) for `ms`: the dashes' start and end steps
    auto spam = [&](MoveState& q, int ms, std::vector<int>* starts, std::vector<int>* ends, UserCmd base = UserCmd()) {
        bool was = q.dashTimer > 0;
        for (int t = 0; t < ms; t++) {
            UserCmd c = base; if (t % 2 == 0) c.buttons |= BTN_DASH;
            const uint16_t cd = q.dashCooldownMs;
            step(q, c, open);
            const bool on = q.dashTimer > 0;
            const bool started = on ? !was : (cd == 0 && q.dashCooldownMs > 0 && !was);   // (a super dash can start and end in one step)
            if (started) starts->push_back(t);
            if (q.dashCooldownMs > cd) ends->push_back(t);   // (a dash ended this step: the cooldown restarted)
            was = on;
        }
    };
    auto minGap = [](const std::vector<int>& a, const std::vector<int>& b) {   // the least from an end to the next start
        int g = 1 << 30;
        for (size_t i = 0; i + 1 < a.size() && i < b.size(); i++) g = std::min(g, a[i + 1] - b[i]);
        return g;
    };
    auto minStep = [](const std::vector<int>& a) { int g = 1 << 30; for (size_t i = 1; i < a.size(); i++) g = std::min(g, a[i] - a[i - 1]); return g; };
    { // in the air from full: press, press again mid-burst (dropped), press right after it ends (the cooldown: nothing), press
      // exactly 0.6 s after it ENDED (the second), then none left: both recharge in the air but only the floor gives them back
        MoveState q = two(); q.pos = {0, 200, 0};
        CHECK(q.dashCharges == 2 && dashMaxCharges(0) == 2 && dashMaxCharges(MOVE_QUICK_DASH) == 2 && dashCooldownMs() == 600 &&
              dashCooldownMs(MOVE_QUICK_DASH) == 200 && dashRechargeMs() == 1200 && dashRechargeMs(MOVE_QUICK_DASH) == 400 && dashReady(q));
        step(q, dash, open); step(q, none, open);
        CHECK(q.dashCharges == 1 && q.dashTimer > 0 && q.dashCooldownMs == 0 && !dashReady(q));   // (no cooldown while the dash runs)
        CHECK(q.dashRecharging == 1 && q.dashRechargeMs[0] == 1200);   // (the spent charge's recharge waits for the dash to end)
        for (int i = 0; i < 40; i++) step(q, none, open);
        CHECK(q.dashRechargeMs[0] == 1200);
        float left = q.dashTimer;
        step(q, dash, open);                     // mid-burst: nothing (the timer runs on, the charge stays)
        CHECK(q.dashCharges == 1 && q.dashTimer < left && q.dashTimer > 0 && q.dashCooldownMs == 0);
        while (q.dashTimer > 0) step(q, dash, open);   // (held through the end: still the same press, not a new one)
        printf("  cooldown at the dash's end: %d ms, its charge recharging %d ms\n", (int)q.dashCooldownMs, (int)q.dashRechargeMs[0]);
        CHECK(q.dashCharges == 1 && q.dashCooldownMs == 600 && q.dashRecharging == 1 && q.dashRechargeMs[0] == 1200);   // both start when the dash ENDS
        { MoveState p = q; step(p, none, open); step(p, dash, open);   // two dashes in a row: no (a charge is left)
          CHECK(p.dashCharges == 1 && p.dashTimer == 0 && p.dashCooldownMs == 598 && p.dashRechargeMs[0] == 1198); }
        for (int k = 1; k <= 598; k++) step(q, none, open);
        CHECK(!dashReady(q));   // (dashReady: would a press in the next step go)
        { MoveState p = q; step(p, dash, open);   // 0.599 s after the end: still nothing
          CHECK(p.dashCharges == 1 && p.dashTimer == 0 && p.dashCooldownMs == 1); }
        step(q, none, open);
        CHECK(dashReady(q) && q.dashCooldownMs == 1);
        step(q, dash, open);   // 0.6 s after the end: the second
        printf("  air: %d charge(s) after two dashes 0.6 s apart (the mid-burst press dropped)\n", (int)q.dashCharges);
        CHECK(q.dashCharges == 0 && q.dashTimer > 0 && q.dashRecharging == 2 && q.dashRechargeMs[0] == 600 && q.dashRechargeMs[1] == 1200);
        while (q.dashTimer > 0) step(q, none, open);
        CHECK(q.dashRecharging == 2 && q.dashRechargeMs[0] < 600 && q.dashRechargeMs[0] > 470 && q.dashRechargeMs[1] == 1200);   // (the first counts on during the second)
        for (int k = 0; k < 1300; k++) step(q, none, open);
        CHECK(q.dashCooldownMs == 0 && q.dashRecharging == 0 && !(q.flags & MF_ONGROUND) && !dashReady(q));
        step(q, dash, open);                     // the cooldown and recharges are over but no charge came back (in the air)
        CHECK(q.dashCharges == 0 && q.dashTimer == 0);
        // landing without jump: both back
        for (int i = 0; i < 10000 && !(q.flags & MF_ONGROUND); i++) step(q, none, open);
        CHECK((q.flags & MF_ONGROUND) && q.dashCharges == 2);
    }
    for (int mods : {0, (int)MOVE_QUICK_DASH}) { // in the air from full, pressing every other ms: both within a second
        MoveState a = two(); a.pos = {0, 200, 0}; a.mods = (uint8_t)mods;
        std::vector<int> st, en;
        spam(a, 3000, &st, &en);
        const int want = mods ? 200 : 600;
        printf("  air%s from full, pressing: %d dashes, %d ms start to start (%d ms from the first's end)\n", mods ? " (quick dash)" : "",
               (int)st.size(), st.size() > 1 ? st[1] - st[0] : -1, st.size() > 1 && !en.empty() ? st[1] - en[0] : -1);
        CHECK(st.size() == 2 && st[1] - st[0] < 1000 && st[1] - en[0] >= want && st[1] - en[0] <= want + 1 && a.dashCharges == 0);
    }
    for (int mods : {0, (int)MOVE_QUICK_DASH}) { // on the floor the charges refill once recharged: the cooldown alone spaces the dashes
        MoveState q = two(); q.flags = MF_ONGROUND; q.mods = (uint8_t)mods;
        for (int i = 0; i < 20; i++) step(q, none, open);
        std::vector<int> st, en;
        spam(q, 4000, &st, &en);
        const int want = mods ? 200 : 600;
        printf("  ground%s dash spam over 4 s: %d dashes, %d ms start to start, %d ms at least from one's end to the next\n",
               mods ? " (quick dash)" : "", (int)st.size(), minStep(st), minGap(st, en));
        CHECK(minGap(st, en) >= want && minGap(st, en) <= want + 1 && (int)st.size() == (mods ? 13 : 6));
    }
    { // on the floor a spent charge comes back 1.2 s after its dash ended, not before
        MoveState q = two(); q.flags = MF_ONGROUND;
        for (int i = 0; i < 20; i++) step(q, none, open);
        step(q, dash, open);
        int t = 0;
        while (q.dashTimer > 0) { step(q, none, open); t++; }
        CHECK(q.dashCharges == 1 && q.dashRecharging == 1);
        int back = 0;
        for (int k = 1; k <= 1300 && !back; k++) { step(q, none, open); if (q.dashCharges == 2) back = k; }
        printf("  ground: the spent charge back %d ms after its dash ended\n", back);
        CHECK(back == 1200 && q.dashRecharging == 0);
    }
    { // holding jump (bhop landings, a hard-landing bounce) refills only up to ONE charge (owner): letting go of jump on
      // the floor gives both back
        UserCmd hop; hop.buttons = BTN_JUMP;
        MoveState q = two(); q.pos = {0, 0.6f, 0}; q.dashCharges = 0;
        int landings = 0; uint8_t seen[3] = {};
        for (int i = 0; i < 3000 && landings < 3; i++) {
            bool was = q.flags & MF_ONGROUND;
            step(q, hop, open);
            if (!was && (q.flags & MF_ONGROUND)) seen[landings++] = q.dashCharges;
        }
        printf("  bhop landings from 0: %d %d %d\n", seen[0], seen[1], seen[2]);
        CHECK(landings == 3 && seen[0] == 1 && seen[1] == 1 && seen[2] == 1);
        MoveState f = two(); f.pos = {0, 20, 0}; f.dashCharges = 0;
        for (int i = 0; i < 3000 && f.vel.y <= 0; i++) step(f, hop, open);   // falls, bounces off the floor (no ground tick)
        CHECK(f.vel.y > moveBaseJump() && f.dashCharges == 1);
        for (int i = 0; i < 3000 && !(q.flags & MF_ONGROUND && q.vel.y <= 0); i++) step(q, none, open);   // let go: land
        step(q, none, open);
        printf("  let go of jump and land: %d charge(s)\n", q.dashCharges);
        CHECK(q.dashCharges == 2);
    }
    for (int mods : {0, (int)MOVE_QUICK_DASH}) { // bunny-hopping (jump held) and pressing dash all the while: a landing gives one back once recharged
        MoveState q = two(); q.flags = MF_ONGROUND; q.mods = (uint8_t)mods;
        for (int i = 0; i < 20; i++) step(q, none, open);
        UserCmd hop; hop.buttons = BTN_JUMP; hop.forward = 127;
        std::vector<int> st, en;
        spam(q, 4000, &st, &en, hop);
        printf("  bhop%s dash spam over 4 s: %d dashes, %d ms start to start at least, %d ms from one's end to the next\n",
               mods ? " (quick dash)" : "", (int)st.size(), minStep(st), minGap(st, en));
        CHECK(minGap(st, en) >= (mods ? 200 : 600) && st.size() >= 3);
    }
    { // a dash jump refills all (it's off the floor), the charge its own dash just spent too (owner 1.18.7: "exempt the
      // dash jump"); an older charge still recharging stays out: a ground dash, 0.6 s later a ground dash + jump
        MoveState q = two(); q.flags = MF_ONGROUND;
        for (int i = 0; i < 20; i++) step(q, none, open);
        step(q, dash, open);
        while (q.dashTimer > 0) step(q, none, open);
        for (int k = 0; k < 600; k++) step(q, none, open);
        CHECK(q.dashCharges == 1 && q.dashRecharging == 1 && q.dashCooldownMs == 0);
        step(q, dash, open);
        CHECK(q.dashCharges == 0 && q.dashTimer > 0 && q.dashRecharging == 2);
        UserCmd dj; dj.buttons = BTN_DASH | BTN_JUMP;
        step(q, dj, open);
        printf("  dash jump with the other charge recharging: %d charge(s) (%d recharging), %.1f m/s\n", (int)q.dashCharges, (int)q.dashRecharging, std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z));
        CHECK(q.dashTimer == 0 && q.dashCharges == 1 && q.dashRecharging == 1 && q.vel.y > 7.0f && q.dashCooldownMs == 600);   // (ended by the jump: the cooldown from there)
        MoveState f = two(); f.flags = MF_ONGROUND;   // from full: both kept, nothing recharging
        for (int i = 0; i < 20; i++) step(f, none, open);
        step(f, dash, open); step(f, dj, open);
        printf("  dash jump from full: %d charge(s) (%d recharging)\n", (int)f.dashCharges, (int)f.dashRecharging);
        CHECK(f.dashTimer == 0 && f.dashCharges == 2 && f.dashRecharging == 0 && f.dashCooldownMs == 600);
    }
    { // a super dash leaves exactly one, from two or from one; its floor contact refills nothing on top
        for (int start : {2, 1}) {
            MoveState q = two(); q.pos = {0, 1.0f, 0}; q.dashCharges = (uint8_t)start;
            UserCmd sd; sd.pitch = -1.2f; sd.buttons = BTN_DASH | BTN_JUMP;
            int t = 0;
            for (; t < 120 && (t == 0 || q.dashTimer > 0); t++) step(q, sd, open);
            printf("  super dash from %d: %d charge(s), %.1f m/s\n", start, (int)q.dashCharges, std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z));
            const float up = std::sin(1.2f) * 0.9f;   // (1.18.31: the dash's down share of the 11 goes up, the rest level)
            CHECK(t < 120 && std::fabs(std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z) - 11.0f * std::sqrt(1 - up * up)) < 1e-3f && q.dashCharges == 1);   // (from a standstill: the dash's own 11)
            // dash still held through the hop: nothing until the cooldown is over, then (T2: a held dash fires the moment
            // it's allowed) the next dash at once, on the one charge left
            UserCmd fly; fly.buttons = BTN_DASH;
            bool again = false;
            for (int i = 0; i < 2000 && !(q.flags & MF_ONGROUND) && !again; i++) {
                const uint16_t cd = q.dashCooldownMs;
                step(q, fly, open);
                again = q.dashTimer > 0;
                if (!again) CHECK(q.dashCharges == 1);
                else CHECK(cd <= 1 && q.dashCharges == 0);
            }
            CHECK(again);
        }
        // chained super dashes: each needs a charge (the hop's bhop landing gives one back once it has recharged: never
        // more than that) and the cooldown since the last one ended; each adds 5 m/s to the speed it starts from, up to 36
        for (int mods : {0, (int)MOVE_QUICK_DASH}) {
            MoveState q = two(); q.pos = {0, 1.0f, 0}; q.mods = (uint8_t)mods;
            int supers = 0, maxSeen = 0;
            float top = 0;
            std::vector<int> st, en;
            bool was = false;
            for (int i = 0; i < 4000; i++) {
                UserCmd c; c.pitch = -1.2f; c.buttons = BTN_JUMP; c.forward = 127;
                if (q.dashTimer <= 0 && q.vel.y < 0 && i % 2 == 0) c.buttons |= BTN_DASH;   // (a fresh press every other ms on the way down)
                const uint16_t cd = q.dashCooldownMs;
                step(q, c, open);
                if (q.dashCooldownMs > cd && q.vel.y > 2.0f) { supers++; top = std::max(top, std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z)); }   // (a dash ending in a hop)
                const bool started = q.dashTimer > 0 ? !was : (cd == 0 && q.dashCooldownMs > 0 && !was);
                if (started) st.push_back(i);
                if (q.dashCooldownMs > cd) en.push_back(i);
                was = q.dashTimer > 0;
                maxSeen = std::max(maxSeen, (int)q.dashCharges);
            }
            printf("  chained super dashes%s over 4 s: %d supers of %d dashes (fastest %.1f m/s), %d ms start to start at least, %d ms from one's end to the next, at most %d charge(s) held\n",
                   mods ? " (quick dash)" : "", supers, (int)st.size(), top, minStep(st), minGap(st, en), maxSeen);
            CHECK(supers >= 2 && top <= 36.0f + 1e-3f && minGap(st, en) >= (mods ? 200 : 600) && maxSeen <= 2);
        }
    }
    { // a wall bounce leaves exactly one (twice in a corridor: still one)
        Map corridor;
        CHECK(corridor.parse("box -50 -1 -50 50 0 50\nbox -50 0 -3 50 4 -0.8\nbox -50 0 0.8 50 4 3\nspawn any 0 0 0\n", &err));
        MoveState c = two(); c.flags = MF_ONGROUND;
        for (int i = 0; i < 20; i++) step(c, none, corridor);
        c.dashCharges = 1;
        int b1 = -1, b2 = -1;
        for (int i = 0; i < 120 && (i == 0 || c.dashTimer > 0); i++) {
            step(c, dash, corridor);
            if (c.dashBounces == 1 && b1 < 0) b1 = c.dashCharges;
            if (c.dashBounces == 2 && b2 < 0) b2 = c.dashCharges;
        }
        printf("  wall bounces: %d then %d charge(s)\n", b1, b2);
        CHECK(b1 == 1 && b2 == 1);
    }
    { // a ladder refills nothing
        Map m;
        CHECK(m.parse("box -20 -1 -20 20 0 20\nbox -20 0 -3 20 30 -2\nladder -5 0 -2 5 30 -1.5\nspawn any 0 0 0\n", &err));
        MoveState q = two(); q.pos = {0, 3, -1.6f}; q.dashCharges = 0;
        UserCmd up; up.forward = 127;
        for (int i = 0; i < 500; i++) { step(q, up, m); CHECK(q.dashCharges == 0); }
        MoveState g = two(); g.flags = MF_ONGROUND; g.mods = MOVE_QUICK_DASH; g.dashCharges = 0;
        step(g, none, open);
        CHECK(g.dashCharges == 2);
    }
    { // the wire: the charges, cooldown and recharges go exactly (protocol 43), and nothing extra when nothing runs
        auto roundTrip = [](const MoveState& m, int* bytes) {
            Snapshot s; s.self = m;
            uint8_t buf[512]; Writer w(buf, sizeof buf); writeSnapshotClient(w, s);
            *bytes = (int)w.size();
            Reader r(buf, (int)w.size()); Snapshot o; CHECK(readSnapshotClient(r, o)); return o.self;
        };
        MoveState q = two(); q.pos = {0, 200, 0};
        int idle = 0, busy = 0;
        { MoveState d; d.pos = q.pos; roundTrip(d, &idle); }   // (idle: the default, one charge: nothing sent for it)
        step(q, dash, open); while (q.dashTimer > 0) step(q, none, open);
        for (int k = 0; k < 600; k++) step(q, none, open);
        step(q, dash, open); while (q.dashTimer > 0) step(q, none, open);
        MoveState o = roundTrip(q, &busy);
        printf("  own state: %d bytes idle, %d with two charges recharging\n", idle, busy);
        CHECK(o.dashCharges == q.dashCharges && o.dashCooldownMs == q.dashCooldownMs && o.dashRecharging == 2 &&
              o.dashRechargeMs[0] == q.dashRechargeMs[0] && o.dashRechargeMs[1] == q.dashRechargeMs[1] && busy == idle + 1 + 2 + 4);
    }
}

// Dash jump (Celeste's super) and super dash (Celeste's hyper), owner: exact speeds, the input buffer, no effect
// without jump, no bhop bounce on top, and prediction replaying them from the snapshot's movement state.
static void testDashTech() {
    printf("dash jump and super dash\n");
    // (with two charges, a custom mode's dash_charges x2: what a dash jump and a super dash leave of them)
    CustomRules twoRules; twoRules.name = "two charges"; twoRules.move[MV_DASH_CHARGES] = 2;
    setActiveRules(twoRules);
    struct Restore { ~Restore() { setActiveRules(CustomRules()); } } restore;
    auto two = [] { MoveState m; m.dashCharges = 2; return m; };
    std::string err;
    Map open;
    CHECK(open.parse("box -500 -1 -500 500 0 500\nspawn any 0 0 0\n", &err));
    auto hs = [](const MoveState& q) { return std::sqrt(q.vel.x * q.vel.x + q.vel.z * q.vel.z); };
    auto step = [&](MoveState& q, UserCmd c) { c.msec = 1; pmove(q, c, open); };
    auto standing = [&] { MoveState q = two(); q.flags = MF_ONGROUND; for (int i = 0; i < 50; i++) step(q, UserCmd()); return q; };
    const float g1 = moveBaseGravity() * 0.001f;   // one step of gravity (the jump's own step applies it)
    // a super dash (hyper) at a look pitch leaves off the floor like the dash bounced (1.18.31): the dash's down share
    // (the sine x 0.9, the most a dash points down) of its speed goes up, the rest stays level
    auto hyperUp = [](float pitch) { return std::min(0.9f, std::sin(-pitch) * 0.9f); };
    auto hyperLevel = [&](float pitch) { const float u = hyperUp(pitch); return std::sqrt(1 - u * u); };
    // flies a state until it lands: horizontal distance and the highest point
    auto flight = [&](MoveState q, uint16_t buttons, float* peak) {
        Vec3 p0 = q.pos; float top = q.pos.y;
        UserCmd c; c.buttons = buttons;
        for (int i = 0; i < 3000 && !(q.flags & MF_ONGROUND); i++) { step(q, c); top = std::max(top, q.pos.y); }
        *peak = top - p0.y;
        return std::sqrt((q.pos.x - p0.x) * (q.pos.x - p0.x) + (q.pos.z - p0.z) * (q.pos.z - p0.z));
    };
    { // dash jump from a standstill: jump 30 ms into a dash along the floor
        MoveState q = standing();
        UserCmd d; d.buttons = BTN_DASH;   // (yaw 0: along -z)
        for (int i = 0; i < 30; i++) step(q, d);
        CHECK(q.dashTimer > 0 && (q.flags & MF_ONGROUND) && std::fabs(hs(q) - 24.0f) < 1e-3f);
        d.buttons = BTN_DASH | BTN_JUMP;
        step(q, d);
        printf("  dash jump: %.3f m/s, %.3f up\n", hs(q), q.vel.y);
        CHECK(q.dashTimer == 0 && std::fabs(hs(q) - 24.0f) < 1e-4f && std::fabs(q.vel.y - (moveBaseJump() - g1)) < 1e-4f);
        { MoveState p = q; step(p, UserCmd()); step(p, UserCmd()); UserCmd again; again.buttons = BTN_DASH; step(p, again);
          CHECK(p.dashTimer == 0 && p.dashCharges == 2 && p.dashRecharging == 0 && p.dashCooldownMs == 597); }   // both charges (a dash jump gives its own back), no dash for 0.6 s after the jump ended it
        float peak, dist = flight(q, 0, &peak);   // (dash let go: held, T2 dashes again 0.6 s on)
        printf("  dash jump: %.1f m across, %.2f m up (a jump: 1.22)\n", dist, peak);
        CHECK(peak > 1.18f && peak < 1.26f && dist > 14.6f && dist < 15.6f);
        // the same dash without jump: settles to the exit speed as before, on the floor
        MoveState n = standing();
        d.buttons = BTN_DASH;
        for (int i = 0; i < 200 && (i == 0 || n.dashTimer > 0); i++) step(n, d);
        CHECK(std::fabs(hs(n) - 11.0f) < 1e-3f && (n.flags & MF_ONGROUND) && n.vel.y == 0);
    }
    { // pressing jump a hair before the dash (within the window) still makes a dash jump; the jump's rise is kept
        for (int gap : {40, 150}) {
            MoveState q = standing();
            UserCmd j; j.buttons = BTN_JUMP;
            step(q, j);   // a tap: one ms of jump
            for (int i = 1; i < gap; i++) step(q, UserCmd());
            float vy = q.vel.y;
            UserCmd d; d.buttons = BTN_DASH;
            step(q, d);
            if (gap < 100) {
                CHECK(q.dashTimer == 0 && std::fabs(hs(q) - 24.0f) < 1e-4f && std::fabs(q.vel.y - (vy - g1)) < 1e-4f);
                float peak;
                flight(q, 0, &peak);
                CHECK(q.pos.y + peak < 1.26f);   // (no higher than a jump)
            } else {
                CHECK(q.dashTimer > 0);   // too late: an ordinary air dash
            }
        }
    }
    { // super dash from a run: dash looking down with jump held, on the floor
        MoveState q = standing();
        UserCmd r; r.forward = 127;
        for (int i = 0; i < 1000; i++) step(q, r);
        CHECK(std::fabs(hs(q) - 9.5f) < 0.05f);
        const float h0 = hs(q);
        r.pitch = -1.0f; r.buttons = BTN_DASH | BTN_JUMP;
        step(q, r);
        printf("  super dash from a run (%.2f m/s): %.3f m/s, %.3f up\n", h0, hs(q), q.vel.y);
        const float vy = (h0 + 5.0f) * hyperUp(-1.0f), level = (h0 + 5.0f) * hyperLevel(-1.0f);   // (57.3 deg down: 76% up)
        CHECK(q.dashTimer == 0 && std::fabs(hs(q) - level) < 0.02f && std::fabs(q.vel.y - (vy - g1)) < 1e-3f);   // (owner 1.18.7: + 5; forward held: the level part is under the run cap, so air control adds a little)
        float peak, dist = flight(q, 0, &peak), air = 2 * vy / moveBaseGravity();   // (dash let go: held, it fires again mid-air, T2)
        printf("  super dash: %.1f m across, %.2f m up (%.2f expected)\n", dist, peak, vy * vy / (2 * moveBaseGravity()));
        CHECK(std::fabs(peak - vy * vy / (2 * moveBaseGravity())) < 0.03f && std::fabs(dist - level * air) < 0.6f);
    }
    { // the hop grows with how far down the dash is aimed (owner 1.18: "the lower you look the higher you should
      // bounce"; 1.18.31: "up to 90% of the speed on vertical"): 22.5% of the speed up just past the 14.5 deg
      // threshold (a skim), 90% straight down
        auto superAt = [&](float pitch, float* peak) {
            MoveState q = standing();
            UserCmd d; d.pitch = pitch; d.buttons = BTN_DASH | BTN_JUMP;
            step(q, d);
            CHECK(q.dashTimer == 0 && q.dashCharges == 1 && std::fabs(hs(q) - 11.0f * hyperLevel(pitch)) < 1e-3f);   // (a super dash: from a standstill, the dash's own 11)
            flight(q, 0, peak);   // (dash let go: the high hops outlast the 0.6 s, and a held dash would fire, T2)
            return q.vel.y;   // (landed: the hop's peak is in *peak)
        };
        const float threshold = -std::asin(0.25f) - 1e-4f;   // just past 14.48 deg down
        float pT, p20, p45, p90, p155;
        superAt(threshold, &pT);
        superAt(-20 * 0.0174533f, &p20);
        superAt(-45 * 0.0174533f, &p45);
        superAt(-1.5707963f, &p90);
        superAt(-1.55f, &p155);   // (the mouse's limit, 88.8 deg)
        printf("  super dash hop: %.3f m at the threshold, %.3f at 20 deg, %.3f at 45, %.3f at 88.8, %.3f straight down\n", pT, p20, p45, p155, p90);
        CHECK(std::fabs(hyperUp(threshold) - 0.225f) < 1e-3f);    // at the threshold: 22.5% of the speed up
        CHECK(std::fabs(hyperUp(-1.5707963f) - 0.9f) < 1e-6f);    // straight down: 90%
        CHECK(pT > 0.11f && pT < 0.17f);                          // (0.14 m: a skim)
        CHECK(p90 > 1.97f && p90 < 2.11f && p155 > 1.97f);        // (2.04 m: 9.9 of the 11 up, gravity 24)
        CHECK(p90 > p20 + 0.5f && p20 > pT && p45 > p20 && p90 > p45);
        { MoveState q = standing(); UserCmd d; d.pitch = -1.5707963f; d.buttons = BTN_DASH | BTN_JUMP; step(q, d);
          CHECK(std::fabs(q.vel.y - (11.0f * 0.9f - g1)) < 1e-4f); }   // (exactly: the step's own gravity off 90% of 11)
        { MoveState q = standing(); UserCmd d; d.pitch = threshold; d.buttons = BTN_DASH | BTN_JUMP; step(q, d);
          CHECK(std::fabs(q.vel.y - (11.0f * 0.225f - g1)) < 1e-3f); }
        // shallower than the threshold: a dash jump (a full jump at the burst speed), not a super dash
        { MoveState q = standing(); UserCmd d; d.pitch = -10 * 0.0174533f; d.buttons = BTN_DASH | BTN_JUMP; step(q, d);
          CHECK(q.dashCharges == 2 && q.dashRecharging == 0 && std::fabs(hs(q) - 24.0f) < 1e-4f && std::fabs(q.vel.y - (moveBaseJump() - g1)) < 1e-4f); }
    }
    { // super dash out of a bhop: in the air, dash down into the floor holding jump; it converts as it lands
        MoveState q = standing();
        UserCmd r; r.forward = 127;
        for (int i = 0; i < 1000; i++) step(q, r);
        r.buttons = BTN_JUMP;
        for (int i = 0; i < 1000 || q.vel.y > -1.0f; i++) step(q, r);   // bhopping, now on the way down
        CHECK(!(q.flags & MF_ONGROUND) && q.pos.y > 0.2f);
        r.pitch = -1.2f; r.buttons = BTN_JUMP | BTN_DASH;
        const float h0 = hs(q);
        int t = 0;
        for (; t < 120 && (t == 0 || q.dashTimer > 0); t++) step(q, r);
        printf("  super dash out of a bhop (%.2f m/s): %.3f m/s, %.3f up after %d ms\n", h0, hs(q), q.vel.y, t);
        const float sp = std::max(11.0f, h0 + 5.0f), vy = sp * hyperUp(-1.2f);   // (68.8 deg down: 84% up)
        CHECK(t < 120 && std::fabs(hs(q) - sp * hyperLevel(-1.2f)) < 0.02f && std::fabs(q.vel.y - (vy - g1)) < 1e-3f);   // (jump held: air control adds a little, as above)
        float peak;
        flight(q, BTN_DASH, &peak);
        CHECK(peak < vy * vy / 44.0f + 0.02f);   // (no bhop bounce off the dash's push into the floor on top)
        // at speed: the super dash is the speed it started from + 5 (30 -> 35), capped at 36 (33 -> 36)
        MoveState f = standing();
        f.vel = {0, 0, -30};
        UserCmd d; d.pitch = -1.0f; d.buttons = BTN_DASH | BTN_JUMP;
        step(f, d);
        MoveState f3 = standing();
        f3.vel = {0, 0, -33};
        step(f3, d);
        printf("  super dash at 30 m/s: %.3f m/s; at 33: %.3f\n", hs(f), hs(f3));
        CHECK(std::fabs(hs(f) - 35.0f * hyperLevel(-1.0f)) < 1e-3f && std::fabs(hs(f3) - 36.0f * hyperLevel(-1.0f)) < 1e-3f);
        CHECK(std::fabs(f.vel.y - (35.0f * hyperUp(-1.0f) - g1)) < 1e-3f);   // (the speed is the same, part of it up)
        MoveState f2 = standing();
        f2.vel = {0, 0, -30};
        d.pitch = 0;
        step(f2, d);
        CHECK(std::fabs(hs(f2) - 31.0f) < 1e-4f && std::fabs(f2.vel.y - (moveBaseJump() - g1)) < 1e-4f);   // the dash jump keeps 31
    }
    { // a down dash into the floor without jump: no boost, no bounce
        MoveState q = two(); q.pos = {0, 1.0f, 0};
        UserCmd d; d.pitch = -1.2f; d.buttons = BTN_DASH;
        for (int i = 0; i < 300; i++) step(q, d);
        CHECK((q.flags & MF_ONGROUND) && q.vel.y == 0 && hs(q) < 11.01f);
    }
    { // the buffer: jump tapped in the air 60 ms before a down dash counts when it lands; 150 ms before doesn't
        for (int gap : {60, 150}) {
            MoveState q = two(); q.pos = {0, 1.0f, 0};
            UserCmd j; j.buttons = BTN_JUMP;
            step(q, j);
            for (int i = 1; i < gap; i++) step(q, UserCmd());
            UserCmd d; d.pitch = -1.2f; d.buttons = BTN_DASH;
            int t = 0;
            for (; t < 120 && (t == 0 || q.dashTimer > 0); t++) step(q, d);
            if (gap < 100) CHECK(t < 120 && std::fabs(hs(q) - 11.0f * hyperLevel(-1.2f)) < 1e-3f && q.vel.y > 3.0f);   // (a super dash from a standstill: 11)
            else CHECK(t == 120 && hs(q) < 11.01f);
        }
    }
    { // dash wall bounce: a dash 30 deg off a wall's normal reflects at 16 m/s (owner 1.18.7; it was the dash's speed); 50 deg (glancing) slides
        Map wall;   // a wall across -z, its face 1.5 m ahead
        CHECK(wall.parse("box -50 -1 -50 50 0 50\nbox -50 0 -3 50 4 -1.5\nspawn any 0 0 0\n", &err));
        auto dashAt = [&](float deg, bool jumpAfter, MoveState* out) {
            MoveState q = two(); q.flags = MF_ONGROUND;
            UserCmd d; d.msec = 1; d.yaw = deg * PI / 180; d.buttons = BTN_DASH;   // (yaw 0 faces -z: the wall's normal)
            for (int i = 0; i < 20; i++) pmove(q, UserCmd(), wall);
            int t = 0;
            for (; t < 120 && (t == 0 || q.dashTimer > 0) && q.dashBounces == 0 && q.pos.z > -1.098f; t++) pmove(q, d, wall);
            if (jumpAfter && q.dashTimer > 0) { d.buttons |= BTN_JUMP; pmove(q, d, wall); }
            *out = q;
            return t;
        };
        MoveState q = two();
        int t = dashAt(30, false, &q);
        printf("  wall bounce at 30 deg after %d ms: (%.3f, %.3f) m/s, %d bounce\n", t, q.vel.x, q.vel.z, q.dashBounces);
        CHECK(q.dashBounces == 1 && q.dashTimer > 0 && std::fabs(q.vel.x - (-8.0f)) < 1e-3f && std::fabs(q.vel.z - 13.8564f) < 1e-3f);
        UserCmd d; d.msec = 1; d.yaw = 30 * PI / 180; d.buttons = BTN_DASH;
        while (q.dashTimer > 0) pmove(q, d, wall);
        printf("  wall bounce: the rest of the dash and its exit at %.3f m/s\n", hs(q));
        CHECK(std::fabs(hs(q) - 16.0f) < 1e-3f && q.vel.z > 0);   // runs on and leaves at the bounce's 16, reflected (not the 11 m/s exit)
        t = dashAt(50, false, &q);
        printf("  wall at 50 deg after %d ms: z %.5f (%.3f, %.3f) m/s, %d bounces\n", t, q.pos.z, q.vel.x, q.vel.z, q.dashBounces);
        CHECK(q.dashBounces == 0 && q.vel.z == 0 && std::fabs(q.vel.x - (-24.0f * std::sin(50 * PI / 180))) < 1e-3f);   // slides along
        dashAt(30, true, &q);   // bounce, then jump during the rest of the dash on the floor: a dash jump the reflected way
        CHECK(q.dashTimer == 0 && std::fabs(hs(q) - 16.0f) < 1e-3f && q.vel.z > 0 && q.vel.y > 7.0f);   // (keeps the bounce's 16)
        // head-on between two walls 2.4 m apart: two bounces, the third wall stops it (at most 2 per dash)
        MoveState c = two(); c.flags = MF_ONGROUND;
        for (int i = 0; i < 20; i++) pmove(c, UserCmd(), wall);
        d.yaw = 0; d.buttons = BTN_DASH;
        c.pos.z = 0;
        Map corridor;
        CHECK(corridor.parse("box -50 -1 -50 50 0 50\nbox -50 0 -3 50 4 -0.8\nbox -50 0 0.8 50 4 3\nspawn any 0 0 0\n", &err));
        for (int i = 0; i < 20; i++) pmove(c, UserCmd(), corridor);
        int maxB = 0; bool stopped = false;
        for (int i = 0; i < 120; i++) {
            pmove(c, d, corridor);
            maxB = std::max(maxB, (int)c.dashBounces);
            if (c.dashBounces == 2 && c.vel.z == 0 && c.dashTimer > 0) stopped = true;
        }
        CHECK(maxB == 2 && stopped);
    }
    { // prediction: every state the techs need travels in the snapshot; replaying from any snapshot point is exact
        Map m;
        CHECK(m.load("bastion"));
        std::vector<UserCmd> cmds;
        for (int t = 0; t < 2500; t++) {
            int win = t / 8;
            UserCmd c;
            c.msec = 1;
            c.yaw = 0.3f + win * 0.004f;
            c.pitch = (win / 25) % 3 == 0 ? -0.9f : (win / 25) % 3 == 1 ? 0.0f : 0.3f;
            c.forward = 127;
            if ((win % 30) == 4 || ((win / 60) % 2 && win % 7 == 0)) c.buttons |= BTN_JUMP;   // taps, and held stretches
            if (win % 40 >= 10 && win % 40 < 12) c.buttons |= BTN_DASH;
            cmds.push_back(c);
        }
        std::vector<MoveState> states;
        MoveState s = two();
        s.pos = {-20, 0, 20};
        for (const UserCmd& c : cmds) { pmove(s, c, m); states.push_back(s); }
        int bad = 0, buffered = 0, cooling = 0, recharging = 0;
        for (size_t k = 0; k + 1 < cmds.size(); k += 7) {
            Snapshot snap;
            snap.self = states[k];
            buffered += states[k].jumpBuffer > 0 || states[k].jumpGrace > 0;
            uint8_t buf[512];
            Writer w(buf, sizeof buf);
            writeSnapshotClient(w, snap);
            Reader r(buf, w.size());
            Snapshot back;
            if (!readSnapshotClient(r, back)) { bad++; continue; }
            MoveState q = back.self;
            for (size_t i = k + 1; i < cmds.size(); i++) pmove(q, cmds[i], m);
            const MoveState& e = states.back();
            if (!(q.pos.x == e.pos.x && q.pos.y == e.pos.y && q.pos.z == e.pos.z && q.vel.x == e.vel.x && q.vel.y == e.vel.y && q.vel.z == e.vel.z && q.flags == e.flags && q.dashCharges == e.dashCharges && q.dashCooldownMs == e.dashCooldownMs &&
                  q.dashRecharging == e.dashRecharging && !memcmp(q.dashRechargeMs, e.dashRechargeMs, sizeof q.dashRechargeMs))) bad++;
            cooling += states[k].dashCooldownMs > 0;
            recharging += states[k].dashRecharging > 0;
        }
        int bounced = 0;
        for (const MoveState& st : states) bounced += st.dashTimer > 0 && st.dashBounces > 0;
        printf("  replay from %zu snapshot points (%d with a jump buffered, %d with the dash cooldown running, %d with a charge recharging; %d ms of bounced dashes): %d differ\n",
               cmds.size() / 7, buffered, cooling, recharging, bounced, bad);
        CHECK(bad == 0 && buffered > 0 && cooling > 0 && recharging > 0);
    }
}

// The same inputs must give bit-identical movement at 1000, 500, 250 and 125 Hz.
static void testTickRateIndependence() {
    printf("movement identical at every tick rate (incl. dash, bhop, grapple)\n");
    Map m;
    CHECK(m.load("bastion"));
    for (int scenario = 0; scenario < 2; scenario++) {   // 1: dash jumps and super dashes (looking down, jump tapped)
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
            c.pitch = scenario && (win / 50) % 2 ? -0.8f : 0.15f;
            c.forward = 127;
            c.side = (int8_t)((win / 40) % 2 ? 90 : -90);
            if (win > 20 && (!scenario || win % 9 < 2 || (win / 30) % 4 == 1)) c.buttons |= BTN_JUMP;   // hold space: auto bhop (1: taps too)
            if (win % (scenario ? 45 : 150) == 30) c.buttons |= BTN_DASH;            // periodic dash
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
}

// The maps a test checks: listMaps() without the zz_* test maps other tests write (the forked groups each have their
// own data dir, so none should be there; this keeps a stray one from failing the map checks if one is).
static std::vector<std::string> testedMaps() {
    std::vector<std::string> v = listMaps();
    v.erase(std::remove_if(v.begin(), v.end(), [](const std::string& n) { return n.rfind("zz_", 0) == 0; }), v.end());
    return v;
}

static void testMaps() {
    printf("maps\n");
    std::string err;
    // Lodestone is offered in every mode but the four that want a compact arena (owner, 1.17)
    { Map v; CHECK(v.load("viaduct") && v.supportsMode(MODE_RACE) && v.preferredMode == MODE_RACE && v.hasVoid && v.checkpoints.size() >= 5); }
    for (const char* name : {"lodestone"}) {
        Map m;
        CHECK(m.load(name, &err));
        if (!err.empty()) printf("  %s\n", err.c_str());
        for (int k = 0; k < MODE_COUNT; k++) {
            if (k == MODE_SURF) continue;   // (a surf course is ramps: Lodestone has none)
            bool compact = k == MODE_VS || k == MODE_DUEL || k == MODE_ARENA || k == MODE_TOURNEY;
            if (m.supportsMode(k) == compact) { printf("  %s: %s %s\n", name, compact ? "offers" : "no", modeName(k)); g_fail++; }
        }
        CHECK(m.payloadLength() > 20);
    }
    // ranked: at least two maps per ladder, and each ladder's designed-for map (rankedDefaultMap) is ranked for it.
    // Ranked maps needn't be symmetric (q, 2026-10-30: "would it be fair even though it's asymmetrical?"), only fair:
    // on a ladder played 1v1 (vanilla, vampire) each side has as many spawns, and on a plain run the two sides are
    // within 1 s of each other to the enemy spawns and to the middle of the map. (Instagib's free for all spawns
    // everyone from one pool: fair as it is. Spawn sight and spacing: testSpawnSanity, for every map.)
    int perLadder[3] = {0, 0, 0};
    for (auto& name : listMaps()) {
        Map m;
        if (!m.load(name) || !m.rankedLadders) continue;
        HumanMadeGrace hg(name, m);
        CHECK(isRankedMap(m, 0) || isRankedMap(m, 1) || isRankedMap(m, 5));
        for (int v : {0, 1, 5}) if (m.rankedLadders >> v & 1) CHECK(m.supportsMode(rankedMode(v)));   // (instagib: free for all)
        for (int v : {0, 1, 5}) if (m.rankedLadders >> v & 1) perLadder[v == 0 ? 0 : v == 1 ? 1 : 2]++;
        int a = 0, b = 0;
        for (auto& sp : m.spawns) { a += sp.team == SPAWN_A; b += sp.team == SPAWN_B; }
        bool duel = (m.rankedLadders & 3) != 0;   // a ladder played 1v1 (vanilla, vampire): a spawn for each side
        CHECK(a == b && (a >= 1 || !duel));
        if (!duel || !a) continue;
        WalkGrid w;
        w.build(m);
        std::vector<int> sa, sb;
        Vec3 mid{0, 0, 0};
        for (auto& sp : m.spawns) if (sp.team == SPAWN_A || sp.team == SPAWN_B) {
            int n = w.nodeAt(sp.pos);
            (sp.team == SPAWN_A ? sa : sb).push_back(n);
            mid = mid + sp.pos * (1.0f / (2 * a));
        }
        // each spawn's runs, once; the middle: every spot within r of the spawns' centre (across), r growing from
        // 4 m until every spawn reaches one (a void or a pillar in the very middle: the nearest floor round it)
        std::vector<std::vector<float>> ta, tb;
        for (int n : sa) ta.push_back(walkSeconds(w, n));
        for (int n : sb) tb.push_back(walkSeconds(w, n));
        auto meanTo = [](const std::vector<std::vector<float>>& own, const std::vector<int>& goals) {
            float sum = 0;
            for (auto& t : own) { float f = INFINITY; for (int g : goals) if (g >= 0) f = std::min(f, t[g]); sum += f; }
            return sum / own.size();
        };
        float aFoe = meanTo(ta, sb), bFoe = meanTo(tb, sa), aMid = INFINITY, bMid = INFINITY;
        for (float r = 4; r <= 32 && !(std::isfinite(aMid) && std::isfinite(bMid)); r += 4) {
            std::vector<int> mids;
            for (size_t n = 0; n + w.ladderHubs < w.nodes.size(); n++) {
                const Vec3& p = w.nodes[n].p;
                if ((p.x - mid.x) * (p.x - mid.x) + (p.z - mid.z) * (p.z - mid.z) <= r * r) mids.push_back((int)n);
            }
            aMid = meanTo(ta, mids); bMid = meanTo(tb, mids);
        }
        bool fair = std::isfinite(aFoe + aMid + bFoe + bMid) && std::fabs(aFoe - bFoe) <= 1.0f && std::fabs(aMid - bMid) <= 1.0f;
        if (!fair) printf("  %s: not fair: to the enemy A %.1f s / B %.1f s, to the middle A %.1f s / B %.1f s\n", name.c_str(), aFoe, bFoe, aMid, bMid);
        CHECK(fair);
    }
    CHECK(perLadder[0] >= 2 && perLadder[1] >= 2 && perLadder[2] >= 2);
    for (int v : {0, 1, 5}) {
        Map d;
        bool ok = d.load(rankedDefaultMap(v)) && isRankedMap(d, v);
        if (!ok) printf("  ladder %s: its designed-for map %s isn't ranked for it\n", arenaVariant(v).name, rankedDefaultMap(v));
        CHECK(ok);
    }
}

// The payload cart drives the whole track without going through anything (REVIEW MAP-5: a box on Railyard's track at
// 102-106 m, a pillar and a dais on Lodestone's at 117.5 m and 127.5 m): its box, every 10 cm along the track, against
// every brush you can see or bump into (solid, clip, trim; not far scenery, water or lava surfaces).
static void testCartSweep() {
    printf("payload cart: the whole track clear of the map\n");
    for (auto& name : testedMaps()) {
        Map m;
        if (!m.load(name) || !(m.supportsMode(MODE_PAYLOAD) || m.supportsMode(MODE_NPAYLOAD))) continue;   // (the maps that offer it)
        float len = m.payloadLength();
        int hits = 0;
        std::vector<char> reported(m.brushes.size(), 0);
        for (float d = 0; d <= len + 1e-3f; d += 0.1f) {
            AABB cart = m.payloadCartBox(std::min(d, len));
            cart.mn = cart.mn + Vec3(0.02f, 0.02f, 0.02f);   // (touching is fine: the cart rides on the floor)
            cart.mx = cart.mx - Vec3(0.02f, 0.02f, 0.02f);
            for (size_t i = 0; i < m.brushes.size(); i++) {
                const Brush& b = m.brushes[i];
                if (b.material == 5 || b.material == 6 || b.material == 7 || !cart.overlaps(b.box)) continue;
                hits++;
                if (reported[i]) continue;
                reported[i] = 1;
                printf("  %s: the cart at %.1f m of %.1f goes through material %d %.2f %.2f %.2f .. %.2f %.2f %.2f\n", name.c_str(), d, len, b.material,
                       b.box.mn.x, b.box.mn.y, b.box.mn.z, b.box.mx.x, b.box.mx.y, b.box.mx.z);
            }
        }
        printf("  %-10s %.1f m of track%s\n", name.c_str(), len, hits ? ": BLOCKED" : ", clear");
        CHECK(hits == 0);
    }
}

// Every mode a map offers has a spawn for every player it can have (REVIEW MAP-2: eight maps had 2 free-for-all
// spawns, Anvil's ranked 4-player instagib among them, and players spawned inside each other): the pools
// Server::spawnPlayer really draws from (spawnPool), each at least as big as the players who draw from it in a full
// default-size server (8: free for all, hide, tag, race, a tourney's warm-up; 4 a side in team modes, 2 in duel,
// 1 in 1v1 and a tourney match), every stage of a staged map, and no two of a pool's spawns closer than 1.5 m.
static void testSpawnPools() {
    printf("spawn pools: a spawn per player in every offered mode, 1.5 m apart\n");
    const int full = ServerConfig().maxPlayers;
    for (auto& name : testedMaps()) {
        Map m;
        if (!m.load(name)) continue;
        HumanMadeGrace grace(name, m);
        int maxStage = 0;
        for (auto& sp : m.spawns) if (isStagedSpawn(sp.team)) maxStage = std::max(maxStage, spawnStage(sp.team));
        int worst = 1 << 30, pools = 0;
        std::string worstWhat;
        for (int mode = 0; mode < MODE_COUNT; mode++) {
            if (!m.supportsMode(mode)) continue;
            struct Use { const char* who; int team; bool match; int need; };
            std::vector<Use> uses;
            if (mode == MODE_ARENA) uses = {{"a", TEAM_A, false, 1}, {"b", TEAM_B, false, 1}};
            else if (mode == MODE_DUEL) uses = {{"a", TEAM_A, false, 2}, {"b", TEAM_B, false, 2}};
            else if (mode == MODE_TOURNEY) uses = {{"warm-up", TEAM_A, false, full}, {"match a", TEAM_A, true, 1}, {"match b", TEAM_B, true, 1}};
            else if (modeIsTeamBased(mode)) uses = {{"a", TEAM_A, false, full / 2}, {"b", TEAM_B, false, full / 2}};
            else uses = {{"all", TEAM_A, false, full}};
            for (auto& u : uses)
                for (int stage = 0; stage <= maxStage; stage++) {
                    int want = modeSpawnType(m, mode, u.team, TEAM_A, u.match);
                    if (!isStagedSpawn(want) && stage > 0) break;
                    std::vector<const SpawnPoint*> pool = spawnPool(m, want, stage);
                    float closest = 1e9f;
                    for (size_t i = 0; i < pool.size(); i++)
                        for (size_t j = i + 1; j < pool.size(); j++) closest = std::min(closest, length(pool[i]->pos - pool[j]->pos));
                    pools++;
                    char what[96];
                    snprintf(what, sizeof what, "%s %s%s", modeName(mode), u.who, isStagedSpawn(want) ? (" stage " + std::to_string(stage)).c_str() : "");
                    if ((int)pool.size() - u.need < worst) { worst = (int)pool.size() - u.need; worstWhat = what; }
                    if ((int)pool.size() < u.need || closest < 1.5f) {
                        printf("    %s: %s draws from %zu spawns (needs %d), the closest two %.2f m apart\n", name.c_str(), what, pool.size(), u.need, closest);
                        g_fail++;
                    }
                }
        }
        printf("  %-10s %3zu spawns, %2d pools, fewest to spare: %d (%s)\n", name.c_str(), m.spawns.size(), pools, worst, worstWhat.c_str());
    }
}

// Spawn sanity (REVIEW MAP-16): every spawn faces into the map (2 m clear ahead at eye height: Summit's faced rock
// 0.5 m away), no two spawns that can be in use together stand closer than 1.5 m (only one side's staged spawns of
// different stages may share a spot), and in team modes no spawn sees an enemy spawn (eye to eye, at any range: a
// sniper reaches across any map).
static void testSpawnSanity() {
    printf("spawns: facing into the map, apart, out of the enemy's sight\n");
    for (auto& name : testedMaps()) {
        Map m;
        if (!m.load(name)) continue;
        int bad = 0;
        for (auto& sp : m.spawns) {
            Vec3 eye = sp.pos + Vec3(0, kEyeHeight, 0);
            float t = m.raycast(eye, angleForward(sp.yaw, 0), 2.0f);
            if (t >= 0) { printf("    %s: spawn %.1f %.1f %.1f faces a wall %.1f m away\n", name.c_str(), sp.pos.x, sp.pos.y, sp.pos.z, t); bad++; }
        }
        // (spawns used together: a mode's two teams; the free-for-all pool; the race grid; a staged map's attackers and
        // defenders, but never one side's stages with each other. Lodestone's race grid may share a pad with a multi
        // point stage: no mode uses both.)
        auto group = [](int t) { return isModeSpawn(t) ? 4 + spawnSetMode(t) : t == SPAWN_A || t == SPAWN_B ? 0 : t == SPAWN_ANY ? 1 : t == SPAWN_RACE ? 2 : 3; };   // (a mode set: its own group)
        for (size_t i = 0; i < m.spawns.size(); i++)
            for (size_t j = i + 1; j < m.spawns.size(); j++) {
                auto &p = m.spawns[i], &q = m.spawns[j];
                bool stagesApart = isStagedSpawn(p.team) && isStagedSpawn(q.team) && spawnDefends(p.team) == spawnDefends(q.team) && spawnStage(p.team) != spawnStage(q.team);
                float d = length(p.pos - q.pos);
                if (d < 1.5f && !stagesApart && group(p.team) == group(q.team)) { printf("    %s: spawns %.1f %.1f %.1f and %.1f %.1f %.1f are %.2f m apart\n", name.c_str(), p.pos.x, p.pos.y, p.pos.z, q.pos.x, q.pos.y, q.pos.z, d); bad++; }
            }
        // enemy spawns in sight: the two sides' spawns in every offered team mode (each stage's attackers and defenders)
        std::vector<std::pair<int, int>> sides;   // (spawn type, enemy spawn type)
        for (int mode = 0; mode < MODE_COUNT; mode++) {
            if (!m.supportsMode(mode) || !modeIsTeamBased(mode)) continue;
            int sa = modeSpawnType(m, mode, TEAM_A, TEAM_A, false), sb = modeSpawnType(m, mode, TEAM_B, TEAM_A, false);
            if (isStagedSpawn(sa)) for (int st = 0; st <= kMaxSpawnStage; st++) sides.push_back({stagedSpawn(st, false), stagedSpawn(st, true)});
            else sides.push_back({sa, sb});
        }
        std::sort(sides.begin(), sides.end());
        sides.erase(std::unique(sides.begin(), sides.end()), sides.end());
        if (m.holdFire > 0) sides.clear();   // (a "holdfire" map starts both sides face to face on purpose: Dojo's bow)
        int pairs = 0, seen = 0;
        for (auto [ta, tb] : sides)
            for (auto& p : m.spawns)
                for (auto& q : m.spawns) {
                    if (p.team != ta || q.team != tb) continue;
                    Vec3 a = p.pos + Vec3(0, kEyeHeight, 0), b = q.pos + Vec3(0, kEyeHeight, 0), d = b - a;
                    float len = length(d);
                    pairs++;
                    if (len > 0.1f && !m.raycastAny(a, d / len, len)) {
                        seen++;
                        printf("    %s: enemy spawns %.1f %.1f %.1f and %.1f %.1f %.1f see each other (%.0f m)\n", name.c_str(), p.pos.x, p.pos.y, p.pos.z, q.pos.x, q.pos.y, q.pos.z, len);
                    }
                }
        printf("  %-10s %3zu spawns, %d enemy pairs, %d in sight%s\n", name.c_str(), m.spawns.size(), pairs, seen, bad + seen ? ": FAIL" : "");
        g_fail += bad + seen;
    }
}

// The collision-checked walk (WalkGrid, REVIEW MAP-3) on small maps: what a player can and can't get past.
static void testWalkGrid() {
    printf("walk grid: sealed rooms, slots, steps, gaps, ladders, crouching, clip, lava, ramps\n");
    const std::string F = "box -20 -1 -20 20 0 20\nspawn any 0 0 0\n";   // a 40 m floor
    auto reaches = [](const std::string& text, Vec3 a, Vec3 b) {
        Map m;
        std::string err;
        if (!m.parse(text, &err)) { printf("  parse: %s\n", err.c_str()); return -1; }
        WalkGrid w;
        w.build(m);
        w.components();
        int na = w.nodeAt(a), nb = w.nodeAt(b);
        if (na < 0 || nb < 0) return -2;   // (nowhere to stand there)
        return (int)w.reach(na)[w.comp[nb]];
    };
    const Vec3 W(-10, 0, 0), E(10, 0, 0);
    CHECK(reaches(F, W, E) == 1);
    // a room with no way out (3 m walls: higher than a jump), then the same room with a 1 m doorway
    const std::string room = "box -13 0 -2.4 -12.6 3 2.4\nbox -7.4 0 -2.4 -7 3 2.4\nbox -12.6 0 -2.4 -7.4 3 -2\n";
    CHECK(reaches(F + room + "box -12.6 0 2 -7.4 3 2.4\n", W, E) == 0 && reaches(F + room + "box -12.6 0 2 -7.4 3 2.4\n", E, W) == 0);
    CHECK(reaches(F + room + "box -12.6 0 2 -10.5 3 2.4\nbox -9.5 0 2 -7.4 3 2.4\n", W, E) == 1);
    // a wall right across with a slot (off the grid's 0.25 m spacing): 0.9 m lets the 0.8 m hull through, 0.7 m doesn't
    auto slot = [&](float w) { char t[200]; snprintf(t, sizeof t, "box -0.2 0 -20 0.2 4 3.07\nbox -0.2 0 %g 0.2 4 20\n", 3.07 + w); return F + t; };
    CHECK(reaches(slot(0.9f), W, E) == 1 && reaches(slot(0.7f), W, E) == 0);
    // steps up: 0.5 m walks, 1.1 m is a jump, 1.4 m is too high; down is always fine
    auto ledge = [&](float h) { char t[100]; snprintf(t, sizeof t, "box 2 0 -20 20 %g 20\n", h); return F + t; };
    CHECK(reaches(ledge(0.5f), W, {10, 0.5f, 0}) == 1 && reaches(ledge(1.1f), W, {10, 1.1f, 0}) == 1);
    CHECK(reaches(ledge(1.4f), W, {10, 1.4f, 0}) == 0 && reaches(ledge(1.4f), {10, 1.4f, 0}, W) == 1);
    // a gap with nothing under it: 2 m is jumped, 3.6 m isn't
    auto gap = [](float g) { char t[160]; snprintf(t, sizeof t, "box -20 -1 -20 -1 0 20\nbox %g -1 -20 20 0 20\nspawn any -10 0 0\n", -1 + g); return std::string(t); };
    CHECK(reaches(gap(2.0f), W, E) == 1 && reaches(gap(3.6f), W, E) == 0);
    // a 5 m platform: only up its ladder
    const std::string tower = "box 5 0 -2 9 5 2\n";
    CHECK(reaches(F + tower, W, {7, 5, 0}) == 0 && reaches(F + tower + "ladder 4.3 0 -0.45 5 6 0.45\n", W, {7, 5, 0}) == 1);
    // a hole under a wall: 1.4 m high is crawled through, 1 m isn't
    auto hole = [&](float h) { char t[200]; snprintf(t, sizeof t, "box -0.2 0 -20 0.2 4 -1\nbox -0.2 0 1 0.2 4 20\nbox -0.2 %g -1 0.2 4 1\n", h); return F + t; };
    CHECK(reaches(hole(1.4f), W, E) == 1 && reaches(hole(1.0f), W, E) == 0);
    // clip blocks players (an invisible wall in a doorway)
    CHECK(reaches(slot(1.2f), W, E) == 1 && reaches(slot(1.2f) + "clip -0.2 0 3.07 0.2 4 4.27\n", W, E) == 0);
    // lava across the floor: 1 m is jumped over, 3 m isn't
    CHECK(reaches(F + "lava -0.5 0 -20 0.5 0.2 20\n", W, E) == 1 && reaches(F + "lava -1.5 0 -20 1.5 0.2 20\n", W, E) == 0);
    // ramps up onto a 4 m platform (too high to jump): a wedge, a cut box and a corner up to 45 degrees are walked up
    // (their surface is the floor, not their box); a 63 degree wedge is a wall; the way down a gentle one walks too
    const std::string deck = "box 8 0 -3 14 4 3\n";
    const Vec3 top(11, 4, 0);
    CHECK(reaches(F + deck, W, top) == 0);
    CHECK(reaches(F + deck + "wedge 0 0 0 0 -3 8 4 3\n", W, top) == 1 && reaches(F + deck + "wedge 0 0 0 0 -3 8 4 3\n", top, W) == 1);
    CHECK(reaches(F + deck + "wedge 0 0 4 0 -3 8 4 3\n", W, top) == 1);                    // (45 degrees: the steepest floor)
    CHECK(reaches(F + deck + "wedge 0 0 6 0 -3 8 4 3\n", W, top) == 0);                    // (63 degrees)
    CHECK(reaches(F + deck + "cut 0 1 -1 1 0 0 0 0 -3 8 4 3\n", W, top) == 1);            // (y <= x over its box: 27 degrees)
    CHECK(reaches(F + deck + "wedge 0 4 0 0 -3 8 4 3\n", W, top) == 0);                    // (hung from the ceiling: no floor)
    // a 4 m wedge rising along z beside the deck, then a corner ramp (high at +x+z) into its corner
    CHECK(reaches(F + "box 8 0 3 14 4 9\nwedge 0 2 8 0 -5 14 4 3\n", W, {11, 4, 6}) == 1);
    CHECK(reaches(F + deck + "corner 0 2 0 0 -3 8 4 3\n", W, top) == 1);
}

// Every map: loads, supports the mode it was built for, no spawn inside a wall, players can get from every spawn to
// every objective (and the enemy) in every mode it offers, walking the real collision (WalkGrid), and the bots' nav
// graph links them too.
static void testAllMaps() {
    printf("all maps: modes, spawns, collision-checked routes, bot navigation\n");
    for (auto& name : testedMaps()) {
        Map m;
        std::string err;
        if (!m.load(name, &err)) { printf("  %s: %s\n", name.c_str(), err.c_str()); g_fail++; continue; }
        HumanMadeGrace hg(name, m);
        if (m.preferredMode < 0) printf("  %s: not built for one mode (no \"for <mode>\" line)\n", name.c_str());
        CHECK(m.preferredMode >= 0 && m.supportsMode(m.preferredMode));   // (built for one mode, and it offers that one)
        if (name == "dojo") {   // (1.19.6) the centre mat flat and empty (q): nothing on it or over it up to a duel giant's 3.6 m, and the bow
            const AABB mat = {{-9, 0.51f, -9}, {9, 0.5f + 3.6f, 9}};
            int over = 0;
            for (auto& b : m.brushes) over += b.box.overlaps(mat);
            if (over) printf("  dojo: %d boxes on or over the mat\n", over);
            CHECK(over == 0 && m.holdFire == 5);
            // (q, 1.19.6: the new dojo replaces the old one on the ranked vampire ladder, under the same name: ratings are
            //  per ladder, and the build embeds its new sha256, so it stays a ranked map without a change to ranking)
            CHECK(m.rankedLadders == (1u << arenaVariantFromName("vampire")) && isRankedMap(m, arenaVariantFromName("vampire")));
        }
        for (auto& sp : m.spawns) CHECK(!m.boxSolid(playerBox(sp.pos + Vec3(0, 0.02f, 0), false)));
        {   // walked with the player's hull against the map's collision: a route that isn't there fails
            double t0 = timeNow();
            WalkGrid w;
            w.build(m);
            int routes = 0;
            std::vector<std::string> missing = walkRoutesMissing(m, w, &routes);
            printf("  %-10s %7zu standing spots, %4d routes walked (%.0f ms)%s\n", name.c_str(), w.nodes.size(), routes, (timeNow() - t0) * 1000,
                   missing.empty() ? "" : ": MISSING");
            for (size_t k = 0; k < missing.size() && k < 12; k++) printf("    %s\n", missing[k].c_str());
            if (missing.size() > 12) printf("    (%zu more)\n", missing.size() - 12);
            CHECK(missing.empty());
        }
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
        if (m.preferredMode != MODE_SURF) for (auto& c : m.checkpoints) goals.push_back(c.center());   // (surf: ridden, not walked: testSurf)
        for (auto& p : m.points) goals.push_back({p.center().x, p.mn.y, p.center().z});   // (each point's floor)
        if (m.preferredMode == MODE_HIDE || m.preferredMode == MODE_TAG) for (auto& sp2 : m.spawns) goals.push_back(sp2.pos); // every spawn reaches every other
        int unreachable = 0;
        for (auto& sp : m.spawns)
            for (auto& gl : goals) {
                int a = g.nodeNear(sp.pos + Vec3(0, 0.1f, 0), NavGraph::kStandNear), b = g.nodeNear(gl + Vec3(0, 0.5f, 0), NavGraph::kGoalNear);   // (as bots take them)
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

// A saved map is exactly the map its edits made (FINAL_REVIEW 5.5: saves kept 6 significant digits while edits carry 9,
// so an edit at x = 1000.125 reloaded as 1000.12), and parsing starts from a fresh map every time (5.6).
static void testMapSaveExact() {
    printf("map save: exact numbers, fresh parse, title rule\n");
    auto same = [](const Map& a, const Map& b) {
        if (a.brushes.size() != b.brushes.size() || a.spawns.size() != b.spawns.size() || a.lights.size() != b.lights.size() ||
            a.groups.size() != b.groups.size() || a.ladders.size() != b.ladders.size() || a.payloadPath.size() != b.payloadPath.size()) return false;
        auto eq = [](const AABB& x, const AABB& y) { return x.mn.x == y.mn.x && x.mn.y == y.mn.y && x.mn.z == y.mn.z && x.mx.x == y.mx.x && x.mx.y == y.mx.y && x.mx.z == y.mx.z; };
        for (size_t i = 0; i < a.brushes.size(); i++) if (!eq(a.brushes[i].box, b.brushes[i].box) || a.brushes[i].material != b.brushes[i].material) return false;
        for (size_t i = 0; i < a.spawns.size(); i++) {
            auto &p = a.spawns[i], &q = b.spawns[i];
            if (p.pos.x != q.pos.x || p.pos.y != q.pos.y || p.pos.z != q.pos.z || p.yaw != q.yaw || p.team != q.team) return false;
        }
        for (size_t i = 0; i < a.lights.size(); i++)
            if (a.lights[i].pos.x != b.lights[i].pos.x || a.lights[i].pos.z != b.lights[i].pos.z || a.lights[i].radius != b.lights[i].radius || a.lights[i].intensity != b.lights[i].intensity) return false;
        for (size_t i = 0; i < a.groups.size(); i++) {
            if (!eq(a.groups[i].box, b.groups[i].box) || a.groups[i].parts.size() != b.groups[i].parts.size()) return false;
            for (size_t k = 0; k < a.groups[i].parts.size(); k++) if (!eq(a.groups[i].parts[k].box, b.groups[i].parts[k].box)) return false;
        }
        for (size_t i = 0; i < a.ladders.size(); i++) if (!eq(a.ladders[i], b.ladders[i])) return false;
        for (size_t i = 0; i < a.payloadPath.size(); i++) if (a.payloadPath[i].x != b.payloadPath[i].x || a.payloadPath[i].z != b.payloadPath[i].z) return false;
        return a.title == b.title && a.gridRefs == b.gridRefs;
    };
    const std::string base = "title t\nbox -20 -1 -20 20 0 20\ngroup custom 1 0 8 0 8 10 2 10\npart 2 0 0 0 1 0.5 1\nspawn a 0 0 0\nlight 0 5 0 10 1\n";
    Map m;
    CHECK(m.parse(base));
    for (const char* op : {"b ins 1 0 1000.125 0 -3.0000001 1001.0625 2.123456789 -2", "s set 0 0 1.23456789 0 -7.00390625 37.12345",
                           "s ins 1 1 3 0 3 -123.456787", "l set 0 0.333333343 5.1 0.1 9.87654305 0.123456791", "g part 0 1 0.123456789 0 0.3333333 0.9 0.5 0.987654321",
                           "p ins 0 1.00000012 0 2.5"})
        CHECK(applyMapEdit(m, op));
    Map again;
    std::string text = m.serialize();
    CHECK(again.parse(text) && same(m, again) && again.serialize() == text);
    CHECK(text.find("1000.125 ") != std::string::npos && text.find("1000.12 ") == std::string::npos);
    // shipped maps: save -> load gives the same map, and short numbers stay short (no 0.100000001)
    for (auto& name : testedMaps()) {
        Map a, b;
        if (!a.load(name)) continue;
        HumanMadeGrace grace(name, a);   // (editor-made pieces may carry float noise)
        std::string t = a.serialize();
        if (!b.parse(t) || !same(a, b) || b.serialize() != t) { printf("  %s: save -> load changed the map\n", name.c_str()); g_fail++; }
        if (t.find("00000") != std::string::npos) { printf("  %s: saved with float noise\n", name.c_str()); g_fail++; }
    }
    // parse starts from a fresh map: nothing of the previous map or of a failed parse is left
    Map f;
    f.name = "kept";
    CHECK(f.parse("title Old\nsun 1 1 0\nvoid -3\nindoor 9\nholdfire 4\npalette base 1 0 0 accent 0 1 0 metal 0 0 1 glow 1 1 0 sky 0 1 1\n"
                  "for vs\nmodes vs ffa\nranked vanilla\nbox -5 -1 -5 5 0 5\nspawn a 0 0 0\nspawn b 1 0 1\nhill -1 0 -1 1 2 1\n"));
    CHECK(f.hasVoid && f.indoor && f.holdFire == 4 && f.hasPalette && f.hasHill && f.rankedLadders && f.allowedModes);
    CHECK(!f.parse("title Bad\nvoid -3\nbox 0 0 0 1 1 x\n"));
    Map fresh;
    CHECK(f.title.empty() && !f.hasVoid && f.voidY == 0 && !f.indoor && f.ceilingY == 0 && f.holdFire == 0 && !f.hasPalette && !f.hasHill &&
          f.rankedLadders == 0 && f.allowedModes == 0 && f.preferredMode == -1 && f.brushes.empty() && f.cells.empty() && f.name == "kept");
    CHECK(f.sunDir.x == fresh.sunDir.x && f.sunDir.y == fresh.sunDir.y && f.palBase.x == fresh.palBase.x);
    CHECK(f.parse("box -5 -1 -5 5 0 5\nspawn any 0 0 0\n") && f.title.empty() && !f.hasVoid && f.sunDir.x == fresh.sunDir.x && f.name == "kept");
    // the title: the same rule for a file and an edit (at most 40 printable characters, no '#'; spaces around dropped)
    const std::string rest = "\nbox -5 -1 -5 5 0 5\nspawn any 0 0 0\n";
    CHECK(f.parse("title   My Arena  \r" + rest) && f.title == "My Arena");
    CHECK(f.parse("title " + std::string(40, 'x') + rest) && !f.parse("title " + std::string(41, 'x') + rest) && !f.parse("title a\tb" + rest));
    CHECK(f.parse("title x" + rest) && applyMapEdit(f, "t  Edited  ") && f.title == "Edited");
    CHECK(!applyMapEdit(f, "t " + std::string(41, 'y')) && !applyMapEdit(f, "t a\x01" "b") && f.title == "Edited");
    CHECK(applyMapEdit(f, "t " + std::string(40, 'z')) && again.parse(f.serialize()) && again.title == f.title);
}

// One set of limits for every map, however it arrives (REVIEW CLI-3, NET-3, NET-15): files, downloads and demos are
// refused with an error instead of a 240 GB grid, a non-solid floor or brushes past 65535 dropping out of collision;
// edits that would break those limits, or leave a map that won't load again, are refused and change nothing.
// A map is named by the sha256 of its content (REVIEW MAP-13, JUDGMENT C2): CRLF and LF copies are one map, the name
// doesn't matter to the ranked allowlist, an edit session chains the id over its ops, downloads are cached by it.
static void testMapIdentity() {
    printf("map identity: sha256 of the content\n");
    CHECK(mapContentId("").hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");   // (FIPS 180-4)
    CHECK(mapContentId("box 0 0 0 1 1 1\r\nspawn any 0 0 0\r\n") == mapContentId("box 0 0 0 1 1 1\nspawn any 0 0 0\n"));
    Map m;
    CHECK(m.load("qk1"));
    std::string lf;
    for (char c : m.source) if (c != '\r') lf += c;
    CHECK(m.id().hex() == sha256Hex(lf) && m.id() == m.contentId() && isOfficialMap(m));
    Map renamed;
    CHECK(renamed.parse(m.source) && isOfficialMap(renamed) && renamed.id() == m.id());   // by content, whatever it's called
    Map e = m;
    MapId before = e.id();
    std::string op = "b ins 0 1 0 10 0 2 11 2";
    CHECK(applyMapEdit(e, op));
    e.setEditId(mapEditId(before, op));
    CHECK(e.id() == mapEditId(m.id(), op) && e.id() != before && e.contentId() != m.contentId() && !isOfficialMap(e));
    CHECK(e.id().shortHex().size() == 2 * MapId::kShortBytes && e.id().startsWith(e.id().shortHex()) && !m.id().startsWith(e.id().shortHex()));
    CHECK(downloadedMapPath(m.id()).find(m.id().hex() + ".qmap") != std::string::npos);
}

// The .qmap format (REVIEW MAP-10/11, JUDGMENT C7): a "version" header, real "stairs" / "setting" keywords that the
// editor keeps, version 1's comment forms still read, unknown optional parts warned about and skipped, bad gameplay
// parts errors. Every shipped map is the current version and parses without a warning.
static void testMapFormat() {
    printf("map format: version, stairs and setting keywords, unknown keys warn\n");
    const std::string base = "box -20 -1 -20 20 0 20\nspawn any 0 0 0\n";
    {   // version 1 (no header): the comment forms are read
        Map m;
        CHECK(m.parse("# stairs 0 0 4 2 0 2 x 1 1\n# setting old town\n" + base) && m.formatVersion == 1 && m.warnings.empty());
        CHECK(m.stairs.size() == 1 && m.stairs[0].x1 == 4 && m.stairs[0].axis == 'x' && m.stairs[0].solid && m.setting == "old town");
        CHECK(m.parse("version 2\n# stairs 0 0 4 2 0 2 x 1 1\n# setting old town\n" + base) && m.stairs.empty() && m.setting.empty());   // (just comments now)
    }
    {   // version 2: keywords, kept by the editor's save
        Map m;
        CHECK(m.parse("version 2\nsetting  a city block \nstairs -3 1 0 5 0.5 3 z -1 0\n" + base) && m.formatVersion == 2 && m.warnings.empty());
        CHECK(m.setting == "a city block" && m.stairs.size() == 1 && m.stairs[0].axis == 'z' && m.stairs[0].rising == -1 && !m.stairs[0].solid && m.stairs[0].base == 0.5f);
        Map again;
        std::string text = m.serialize();
        CHECK(text.find("version " + std::to_string(kMapFormatVersion) + "\n") != std::string::npos);
        CHECK(again.parse(text) && again.setting == m.setting && again.stairs.size() == 1 && again.stairs[0].x0 == -3 && again.stairs[0].top == 3 &&
              again.stairs[0].axis == 'z' && again.stairs[0].rising == -1 && !again.stairs[0].solid && again.warnings.empty());
        CHECK(!m.parse("version 2\nstairs 0 0 4 2 0 2 y 1 1\n" + base) && !m.parse("version 2\nstairs 0 0 4 2 0 2 x 1\n" + base) &&
              !m.parse("version 0\n" + base) && !m.parse("version 2\nsetting\n" + base));
    }
    {   // unknown optional parts: warned about and skipped (an older build still loads a newer map)
        Map m;
        CHECK(m.parse("version " + std::to_string(kMapFormatVersion + 1) + "\nfog 0.1 0.2 0.3\n" + base + "modes ffa teleball\nranked vanilla chess\nfor teleball\npalette base 1 1 1 rim 1 0 0\n"));
        CHECK(m.formatVersion == kMapFormatVersion + 1 && m.warnings.size() == 6 && m.allowedModes == 1u << MODE_FFA && m.rankedLadders == 1 && m.preferredMode == -1);
        CHECK(m.parse(base + "weather rain\n") && m.warnings.size() == 1 && m.warnings[0].find("weather") != std::string::npos);
    }
    {   // gameplay parts stay strict
        Map m;
        CHECK(!m.parse(base + "spawn c 1 0 1\n"));                                        // (an unknown team used to become "any")
        CHECK(!m.parse(base + "hill 0 0 0 4 3 4\nhill 8 0 8 12 3 12\n"));                // (the second used to replace the first)
        CHECK(!m.parse(base + "group pyramid 0 0 0 0 0 4 4 4\n") && !m.parse(base + "box 0 0 0 1 1\n"));
    }
    for (auto& name : testedMaps()) {   // the generator writes the current format, nothing it has to skip
        Map m;
        if (!m.load(name)) { printf("  %s doesn't load\n", name.c_str()); g_fail++; continue; }
        HumanMadeGrace hg(name, m);
        if (m.formatVersion != kMapFormatVersion || !m.warnings.empty() || m.setting.empty())
            printf("  %s: format %d, %zu warnings%s%s\n", name.c_str(), m.formatVersion, m.warnings.size(), m.warnings.empty() ? "" : ": ", m.warnings.empty() ? "" : m.warnings[0].c_str());
        CHECK(m.formatVersion == kMapFormatVersion && m.warnings.empty() && !m.setting.empty());
    }
}

// Map format converters (owner rule): every older format converts step by step to the current one, the parser reads
// only that, and a player's maps on disk are converted once with the originals kept (maps/old/v<N>/).
static void testMapFix() {
    printf("editor FIX MAP / FIX GAPS: merges, hidden boxes, small gaps\n");
    auto load = [](const std::string& body) { Map m; std::string err; bool ok = m.parse("box -20 -1 -20 20 0 20\nspawn any 0 0 0\n" + body, &err); CHECK(ok); return m; };
    {   // two flush boxes make one; a third of another material stays; one inside the first goes
        Map m = load("box 0 0 0 1 2 1\nbox 1 0 0 3 2 1\nmetal 3 0 0 4 2 1\nbox 0.2 0.2 0.2 0.5 0.5 0.5\n");
        MapFix f = mapFixRedundant(m);
        CHECK(f.merged == 2 && f.hidden == 0);   // (the small one is inside box 1, its material: merged into it)
        CHECK(!f.del[1] && f.del[2] && !f.del[3] && f.del[4]);   // (brush 0 is the floor)
        CHECK(std::fabs(f.box[1].mx.x - 3) < 1e-4f && std::fabs(f.box[1].mn.x) < 1e-4f);
    }
    {   // a metal box inside two base boxes goes; behind glass or inside thin brick it stays
        Map m = load("box 0 0 0 2 2 2\nbox 2 0 0 4 2 2\nmetal 1 0.5 0.5 3 1.5 1.5\nglass 10 0 0 12 2 2\nmetal 10.5 0.5 0.5 11.5 1.5 1.5\n");
        MapFix f = mapFixRedundant(m);
        CHECK(f.del[3] && f.hidden == 1 && !f.del[4] && !f.del[5]);
    }
    {   // a 1/16 gap: the off-grid side stretches to the on-grid one; an on-grid gap stays; never into a third box
        Map m = load("box 0 0 0 1 2 1\nbox 1.0625 0 0 2 2 1\nbox 5 0 0 6 2 1\nbox 6.125 0 0 7 2 1\n");
        MapFix f = mapFixGaps(m);
        CHECK(f.gaps == 1 && std::fabs(f.box[2].mn.x - 1) < 1e-4f && std::fabs(f.box[1].mx.x - 1) < 1e-4f && std::fabs(f.box[4].mn.x - 6.125f) < 1e-4f);
    }
}

static void testMapConverter() {
    printf("map format converters: every older version, files on disk\n");
    CHECK(mapTextVersion("") == 1 && mapTextVersion("box 0 0 0 1 1 1\n") == 1 && mapTextVersion("# version 5\n") == 1);
    CHECK(mapTextVersion("# x\nversion 2\nbox\n") == 2 && mapTextVersion("  version 7\n") == 7 && mapTextVersion("version x\n") == 0 && mapTextVersion("version 0\n") == 0);
    const std::string base = "box -20 -1 -20 20 0 20\nspawn any 0 0 0\n";
    const std::string v1 = "# generated by tools/make_maps.py\n# a note\ntitle Old\n# stairs 0 0 4 2 0 2 x 1 1\n# setting old town\n" + base;
    std::string out;
    CHECK(upgradeMapText(v1, &out) && mapTextVersion(out) == kMapFormatVersion);
    CHECK(out == "# generated by tools/make_maps.py\n# a note\ntitle Old\nstairs 0 0 4 2 0 2 x 1 1\nsetting old town\n" + base + "version " + std::to_string(kMapFormatVersion) + "\n");   // (last: every line keeps its number)
    {   // a parse error in a converted map names the file's line
        Map m; std::string err;
        CHECK(!m.parse("# old\n" + base + "box 1 2 3\n", &err) && err.rfind("parse error on line 4:", 0) == 0);
    }
    std::string again;
    CHECK(!upgradeMapText(out, &again) && again.empty());                                    // current: nothing to do
    CHECK(!upgradeMapText("version " + std::to_string(kMapFormatVersion + 1) + "\n" + base, &again));   // newer: parsed as it is
    {   // the parser reads the converted text: same map either way, and it remembers what the file was
        Map a, b;
        CHECK(a.parse(v1) && b.parse(out) && a.formatVersion == 1 && b.formatVersion == kMapFormatVersion);
        CHECK(a.stairs.size() == 1 && b.stairs.size() == 1 && a.setting == "old town" && b.setting == a.setting && a.title == "Old");
        CHECK(a.id() == b.id() && a.sourceText() == out);                                     // (one identity for a map, whatever copy a peer holds)
    }
    {   // version 3 (nograpple, before acid): only the version line changes, and the map is the same
        const std::string v3 = "version 3\ntitle Three\nnograpple -1 0 -1 1 2 1\n" + base;
        std::string o;
        CHECK(upgradeMapText(v3, &o) && mapTextVersion(o) == kMapFormatVersion && o == "version " + std::to_string(kMapFormatVersion) + "\ntitle Three\nnograpple -1 0 -1 1 2 1\n" + base);
        Map a, b;
        CHECK(a.parse(v3) && b.parse(o) && a.formatVersion == 3 && b.formatVersion == kMapFormatVersion && a.warnings.empty() && a.acid.empty());
        CHECK(a.brushes.size() == b.brushes.size() && a.brushes[0].material == kMatNoGrapple && a.serialize() == b.serialize());
        Map c;   // and acid itself, from version 4 on: read, written back, kept out of the bounds and the collision grid
        CHECK(c.parse("version 4\nacid -2 0 -2 2 0.3 2\n" + base) && c.warnings.empty() && c.brushes[0].material == kMatAcid && c.acid.size() == 1);
        CHECK(c.serialize().find("acid -2 0 -2 2 0.3 2\n") != std::string::npos && !c.boxSolid(playerBox({0, 0, 0}, false)) && c.inAcid(playerBox({0, 0, 0}, false)));
        CHECK(c.bounds.mx.y == 0 && c.raycast({0, 2, 0}, {0, -1, 0}, 10) > 1.99f);   // (shots and sight go through it to the floor)
    }
    {   // version 11 (before a teleporter's speed): only the version line changes; its teleporters carry all speed
        const std::string v11 = "version 11\nteleporter -3 0 -3 -2 2 3 tp 4 0\nteleporter 2 0 -3 3 2 3 tp 4 1\n" + base;
        std::string o;
        CHECK(upgradeMapText(v11, &o) && o == "version " + std::to_string(kMapFormatVersion) + "\nteleporter -3 0 -3 -2 2 3 tp 4 0\nteleporter 2 0 -3 3 2 3 tp 4 1\n" + base);
        Map a, b;
        CHECK(a.parse(v11) && b.parse(o) && a.formatVersion == 11 && a.warnings.empty() && a.brushes.size() == b.brushes.size());
        CHECK(a.brushes[0].material == kMatTeleport && !a.brushes[0].tpWalk && !b.brushes[1].tpWalk && b.brushes[1].link == 4 && a.serialize() == b.serialize());
    }
    {   // version 12 (before a race map's default time): only the version line changes; it has no default time
        const std::string v12 = "version 12\nfor race\ncheckpoint -2 0 -2 2 2 2\n" + base;
        std::string o;
        CHECK(upgradeMapText(v12, &o) && o == "version " + std::to_string(kMapFormatVersion) + "\nfor race\ncheckpoint -2 0 -2 2 2 2\n" + base);
        Map a, b;
        CHECK(a.parse(v12) && b.parse(o) && a.formatVersion == 12 && a.warnings.empty() && a.raceDefault == 0 && b.raceDefault == 0 && a.serialize() == b.serialize());
        // and the line itself, from version 13 on: read, written back (the editor and quark_maptool keep it), bounded
        Map c, d, e;
        CHECK(c.parse(o + "racedefault 187.5\n") && c.warnings.empty() && c.raceDefault == 187.5f);
        CHECK(c.serialize().find("\nracedefault 187.5\n") != std::string::npos && d.parse(c.serialize()) && d.raceDefault == 187.5f);
        CHECK(!e.parse(o + "racedefault 0\n") && !e.parse(o + "racedefault 4000\n") && !e.parse(o + "racedefault x\n"));
    }
    for (int k = 1; k < kMapFormatVersion; k++) {   // every older version has a way to today's (a missing step doesn't compile)
        std::string o;
        CHECK(upgradeMapText((k > 1 ? "version " + std::to_string(k) + "\n" : std::string()) + base, &o) && mapTextVersion(o) == kMapFormatVersion);
        Map m;
        CHECK(m.parse(o) && m.warnings.empty());
    }
    {   // a player's maps folder
        namespace fs = std::filesystem;
        fs::path d = fs::temp_directory_path() / ("quark_mapconv_" + std::to_string(getpid()));
        std::error_code ec;
        fs::remove_all(d, ec);
        fs::create_directories(d);
        auto put = [&](const std::string& n, const std::string& t) { std::ofstream(d / n, std::ios::binary) << t; };
        auto rd = [&](const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); };
        std::string cur = "version " + std::to_string(kMapFormatVersion) + "\n" + base;
        put("old.qmap", v1); put("cur.qmap", cur); put("bad.qmap", "# stairs 0 0 4 2 0 2 x 1 1\n"); put("notes.txt", "# stairs x\n");
        MapUpgradeReport r = upgradeMapDir(d.string());
        for (auto& l : r.log) printf("  %s\n", l.c_str());
        CHECK(r.converted == 1 && r.failed == 1);
        CHECK(rd(d / "old.qmap") == out && rd(d / "old" / "v1" / "old.qmap") == v1);      // converted, the original kept
        CHECK(rd(d / "cur.qmap") == cur && rd(d / "bad.qmap") == "# stairs 0 0 4 2 0 2 x 1 1\n" && rd(d / "notes.txt") == "# stairs x\n");
        CHECK(rd(d / "old" / "convert.log").find("old.qmap: converted from map format 1") != std::string::npos);
        r = upgradeMapDir(d.string());                                                        // the next start: nothing
        CHECK(r.converted == 0 && r.failed == 0 && r.log.empty());
        put("old.qmap", v1);                                                                  // the same name again: both originals kept
        r = upgradeMapDir(d.string());
        CHECK(r.converted == 1 && rd(d / "old" / "v1" / "old-1.qmap") == v1);
        CHECK(upgradeMapDir((d / "nowhere").string()).log.empty());
        fs::remove_all(d, ec);
    }
}

// Mode spawn sets (map format 11): "spawn koth-a ..." spawns only that mode uses (Mine's hill apart from its bomb
// sides); the mode starts on them when the map has them, every other mode never does; the walk check routes them.
static void testModeSpawnSets() {
    printf("mode spawn sets: koth-a / koth-b, read and written, used only by their mode, walked\n");
    const std::string base = "box -40 -1 -40 40 0 40\nhill -2 0 -2 2 3 2\nsite 20 0 -2 24 3 2\n"
                             "spawn a -30 0 0 90\nspawn b 30 0 0 -90\n";
    const std::string sets = "spawn koth-a -10 0 10 0\nspawn koth-b 10 0 -10 180\n";
    Map m;
    std::string err;
    CHECK(m.parse(base + sets, &err) && m.spawns.size() == 4);
    if (m.spawns.size() != 4) { printf("  %s\n", err.c_str()); return; }
    CHECK(m.spawns[2].team == modeSpawn(MODE_KOTH, SPAWN_A) && m.spawns[3].team == modeSpawn(MODE_KOTH, SPAWN_B));
    CHECK(isModeSpawn(m.spawns[2].team) && !isStagedSpawn(m.spawns[2].team) && spawnSide(m.spawns[3].team) == SPAWN_B && spawnSetMode(m.spawns[3].team) == MODE_KOTH);
    const std::string text = m.serialize();
    CHECK(text.find("spawn koth-a -10 0 10 0\n") != std::string::npos && text.find("spawn koth-b 10 0 -10 180\n") != std::string::npos);
    { Map r; CHECK(r.parse(text) && r.serialize() == text); }
    // koth starts on its own set; bomb and vs on the map's a / b; each pool holds only its own
    CHECK(modeSpawnType(m, MODE_KOTH, TEAM_A, TEAM_A, false) == modeSpawn(MODE_KOTH, SPAWN_A));
    CHECK(modeSpawnType(m, MODE_KOTH, TEAM_B, TEAM_A, false) == modeSpawn(MODE_KOTH, SPAWN_B));
    CHECK(modeSpawnType(m, MODE_BOMB, TEAM_A, TEAM_A, false) == SPAWN_A && modeSpawnType(m, MODE_VS, TEAM_B, TEAM_A, false) == SPAWN_B);
    { auto p = spawnPool(m, modeSpawnType(m, MODE_KOTH, TEAM_A, TEAM_A, false), 0); CHECK(p.size() == 1 && p[0] == &m.spawns[2]); }
    { auto p = spawnPool(m, modeSpawnType(m, MODE_BOMB, TEAM_B, TEAM_A, false), 0); CHECK(p.size() == 1 && p[0] == &m.spawns[1]); }
    // a map without the set: koth falls back to a / b (and one side's set alone: the other side falls back)
    { Map n; CHECK(n.parse(base) && modeSpawnType(n, MODE_KOTH, TEAM_A, TEAM_A, false) == SPAWN_A); }
    { Map n; CHECK(n.parse(base + "spawn koth-a -10 0 10 0\n") && modeSpawnType(n, MODE_KOTH, TEAM_A, TEAM_A, false) == modeSpawn(MODE_KOTH, SPAWN_A) &&
                   modeSpawnType(n, MODE_KOTH, TEAM_B, TEAM_A, false) == SPAWN_B); }
    // a mode's set makes the mode playable on its own: koth on a map with only koth spawns, but not vs
    { Map n; CHECK(n.parse("box -40 -1 -40 40 0 40\nhill -2 0 -2 2 3 2\n" + sets) && n.canSupportMode(MODE_KOTH) && !n.canSupportMode(MODE_VS)); }
    { Map n; CHECK(n.parse(base + "spawn ffa-any 5 0 5 0\n") && modeSpawnType(n, MODE_FFA, TEAM_A, TEAM_A, false) == modeSpawn(MODE_FFA, SPAWN_ANY)); }
    // refused: an unknown mode or side, upper case, race and multi point (they have their own spawns)
    for (const char* bad : {"koth-c", "kingofthehill-a", "KOTH-a", "race-a", "points-a", "koth-", "-a", "koth-race"}) {
        Map n;
        CHECK(!n.parse(base + "spawn " + bad + " 0 0 0\n"));
    }
    CHECK(spawnTeamFromWord("koth-b") == modeSpawn(MODE_KOTH, SPAWN_B) && spawnTeamWord(modeSpawn(MODE_BOMB, SPAWN_ANY)) == "bomb-any" && spawnTeamWord(stagedSpawn(2, true)) == "def2");
    // an editor edit can set one (and only a valid one)
    { Map n; CHECK(n.parse(base)); std::string op = "s set 0 " + std::to_string(modeSpawn(MODE_KOTH, SPAWN_A)) + " -30 0 0 90";
      CHECK(applyMapEdit(n, op) && n.spawns[0].team == modeSpawn(MODE_KOTH, SPAWN_A));
      CHECK(!applyMapEdit(n, "s set 0 " + std::to_string(modeSpawn(MODE_RACE, SPAWN_A)) + " -30 0 0 90") && !applyMapEdit(n, "s set 0 " + std::to_string(kMaxSpawnTeam + 1) + " -30 0 0 90")); }
    // the walk check routes koth from the koth set: sealed in a box, the hill is out of reach (and only koth says so)
    {
        Map n;
        CHECK(n.parse(base + "spawn koth-a -10 0 10 0\nspawn koth-b 10 0 -10 180\nbox -12 0 8 -8 4 8.4\nbox -12 0 11.6 -8 4 12\nbox -12 0 8.4 -11.6 4 11.6\nbox -8.4 0 8.4 -8 4 11.6\n"));
        WalkGrid w;
        w.build(n);
        std::vector<std::string> miss = walkRoutesMissing(n, w);
        bool hill = false, other = false;
        for (auto& l : miss) (l.find("spawn -10.0 0.0 10.0 can't reach hill") != std::string::npos ? hill : other) = true;
        if (!hill || other) for (auto& l : miss) printf("    %s\n", l.c_str());
        CHECK(hill && !other);
        Map open;
        CHECK(open.parse(base + sets));
        WalkGrid w2;
        w2.build(open);
        CHECK(walkRoutesMissing(open, w2).empty());
    }
}

// Human-made maps: what counts as one, and their failed map checks are reported but taken off the count (a generated
// map's stay). The out-of-bounds check does the same (mapbounds.cpp).
static void testHumanMadeMaps() {
    printf("human-made maps: checked and reported, not failing the run\n");
    Map gen;
    CHECK(gen.load("temple") && !gen.handmade && !gen.humanMade() && !isHumanMadeMap("temple", gen));
    Map saved;
    CHECK(saved.parse(gen.serialize()) && saved.handmade && saved.humanMade());   // what the editor saves is a person's
    Map plain;
    CHECK(plain.parse("box -20 -1 -20 20 0 20\nspawn any 0 0 0\n") && !plain.handmade && plain.humanMade());   // not the generator's
    Map marked;
    CHECK(marked.parse(gen.source + "handmade\n") && marked.humanMade() && isHumanMadeMap("temple", marked));
    int before = g_fail;
    { HumanMadeGrace hg("probe", plain); g_fail += 2; }   // (two checks failed on it)
    CHECK(g_fail == before);
    { HumanMadeGrace hg("temple", gen); g_fail++; }
    bool kept = g_fail == before + 1;
    g_fail = before;
    CHECK(kept);
}

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
        MapId before = mapContentId(m.serialize());
        CHECK(applyMapEdit(m, "g set 1 0 0 0 -4096 -1 -4096 4096 1 4096"));   // a big custom group: fine
        CHECK(!applyMapEdit(m, "g part 1 0 -4096 0 -4096 4096 1 4096"));       // a part 4096 times its size: refused
        CHECK(!applyMapEdit(m, "b set 2 0 0 0 0 1 1 1"));                      // generated boxes aren't edited directly
        CHECK(applyMapEdit(m, "g set 1 0 1 0 8 0 8 10 2 10") && m.brushes.size() == n);   // (back as it was)
        std::string again = m.serialize();
        CHECK(mapContentId(again) == before);                                 // refused edits changed nothing
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

// Shipped light bakes (maps/lightcache/<sha256>.qlbz: quark --bake-maps' raw .qlb, zlib'd by tools/pack_lightcache.py):
// a package's bake of a map is used when the user's cache misses (no bake, nothing written to the user's cache, the
// shipped file left alone); a stale one (another bake version, another map's, cut short, not compressed) is turned
// down and the map baked into the user's cache as before.
static void testShippedLightCache() {
    printf("shipped light bakes\n");
    namespace fs = std::filesystem;
    std::error_code ec;
    // a zlib stream of stored blocks (what pack_lightcache.py writes, compressed: the same decoder reads both)
    auto zlibStored = [](const std::string& d) {
        std::string z = "\x78\x01";
        size_t at = 0;
        do {
            const size_t n = std::min<size_t>(65535, d.size() - at);
            z += (char)(at + n == d.size());
            z += (char)(n & 255); z += (char)(n >> 8); z += (char)(~n & 255); z += (char)((~n >> 8) & 255);
            z.append(d, at, n);
            at += n;
        } while (at < d.size());
        uint32_t s1 = 1, s2 = 0;
        for (unsigned char c : d) { s1 = (s1 + c) % 65521; s2 = (s2 + s1) % 65521; }
        const uint32_t ad = s2 << 16 | s1;
        for (int k = 3; k >= 0; k--) z += (char)(ad >> (8 * k));
        return z;
    };
    auto readFile = [](const std::string& path) { std::ifstream f(fs::u8path(path), std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); };
    auto writeFile = [](const std::string& path, const std::string& d) { std::ofstream(fs::u8path(path), std::ios::binary | std::ios::trunc) << d; };
    const std::string shipped = userDataPath("zz_lightcache"), user = userDataPath("cache");
    fs::remove_all(fs::u8path(shipped), ec);
    fs::create_directories(fs::u8path(shipped), ec);
    setShippedLightCacheDir(shipped);
    Map m;
    std::string err;
    CHECK(m.parse("box -8 -1 -8 8 0 8\nbox 2 0 -2 3 3 2\nbox -4 2 -4 -3 2.5 4\nlight 0 2 0 6 1\nspawn any 0 0 0\n", &err));
    const std::string raw = shipped + "/raw.qlb", file = shipped + "/" + lightCacheName(m, true), mine = user + "/" + lightCacheName(m);
    CHECK(lightCacheName(m) == m.contentId().hex() + ".qlm" && lightCacheName(m, true) == m.contentId().hex() + ".qlbz");
    bool kept = true;
    CHECK(bakeLightCacheFile(m, raw, &kept) && !kept && fs::exists(fs::u8path(raw), ec));
    CHECK(bakeLightCacheFile(m, raw, &kept) && kept);   // (a valid one is kept: --bake-maps redoes only changed maps)
    const std::string bytes = readFile(raw);
    writeFile(file, zlibStored(bytes));
    const auto stamp = fs::last_write_time(fs::u8path(file), ec);
    BakedWorld a;
    buildWorld(m, a);
    CHECK(a.fromCache && bytes.size() == 20 + a.texels.size() && !fs::exists(fs::u8path(mine), ec));   // no bake, no copy
    CHECK(a.texels == std::vector<uint8_t>(bytes.begin() + 20, bytes.end()));
    CHECK(fs::last_write_time(fs::u8path(file), ec) == stamp);                                          // read-only: not touched
    // stale: another bake version's file is a miss, baked into the user's cache (the same light)
    std::string stale = bytes;
    stale[4]++;   // (the header's second word: kBakeVersion)
    writeFile(file, zlibStored(stale));
    BakedWorld b;
    buildWorld(m, b);
    CHECK(!b.fromCache && b.texels == a.texels && fs::exists(fs::u8path(mine), ec));
    fs::remove(fs::u8path(mine), ec);
    // another map's bake under this map's name, a stream cut short, the raw bytes not compressed: turned down too
    Map other;
    CHECK(other.parse("box -9 -1 -9 9 0 9\nspawn any 0 0 0\n", &err));
    CHECK(bakeLightCacheFile(other, raw, &kept) && !kept);   // (written over raw.qlb: its header names the other map)
    const std::string z = zlibStored(bytes);
    for (const std::string& bad : {zlibStored(readFile(raw)), z.substr(0, z.size() / 2), bytes}) {
        writeFile(file, bad);
        BakedWorld c;
        buildWorld(m, c);
        CHECK(!c.fromCache && c.texels == a.texels);
        fs::remove(fs::u8path(mine), ec);
    }
    setShippedLightCacheDir("");
    fs::remove_all(fs::u8path(shipped), ec);
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
// The countdowns between the parts of a match (the warmup's once it has its players, a round's freeze, its end) only
// wait in game time: a test that isn't about them ends each now, before its next pump; the server still goes from one
// part to the next its own way. (rulesStart does the same for the freeze.)
static void skipCountdowns(Server& s) {
    if (s.ms_.phase == PHASE_FREEZE || s.ms_.phase == PHASE_ROUND_END || (s.ms_.phase == PHASE_WARMUP && s.phaseEnd_ > 0)) s.phaseEnd_ = s.time();
}
// Pump until every client is in, has a snapshot and is spawned (a warmup spawn comes ~0.5 s after the connect), then
// `after` more for the clients' views: instead of a fixed settle sized for a slow machine.
static void pumpUntilSpawned(Server& s, std::vector<NetClient*>& cls, double after = 0.2) {
    auto in = [&] {
        for (auto* c : cls) if (!c->hasSnapshot() || c->clientId() < 0 || c->clientId() >= kMaxPlayers || !s.players_[c->clientId()].alive) return false;
        return true;
    };
    for (int i = 0; i < 200 && !in(); i++) pump(s, cls, 0.05, idle);
    pump(s, cls, after, idle);
}

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

    // Warmup countdown (5s) then freeze (3s); then live. (Race waits in practice until someone starts it.) One session
    // waits them out, and the reconnect grace at the end; the others start at once and skip the freeze, and end the
    // grace (it has its own test: testReconnectGrace).
    const bool natural = mode == MODE_VS;
    if (mode == MODE_RACE) { pump(s, cls, 1.0, idle); CHECK(s.ms_.phase == PHASE_WARMUP); s.command("forcestart"); }
    if (natural) pump(s, cls, 8.6, idle);
    else {
        if (mode != MODE_RACE) s.command("forcestart");
        for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE; i++) { skipCountdowns(s); pump(s, cls, 0.05, idle); }
        pump(s, cls, 0.1, idle);   // (the clients see it)
    }
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
        // bravo was killed; next round (its round end and freeze skipped) then plant with the carrier.
        pump(s, cls, 0.1, idle);
        CHECK(s.ms_.phase == PHASE_ROUND_END);
        for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE; i++) { skipCountdowns(s); pump(s, cls, 0.05, idle); }
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
    if (!natural) s.players_[a.clientId()].awayUntil = timeNow();
    pump(s, cls, natural ? kReconnectGrace + 0.5 : 0.2, idle);
    CHECK(s.humanCount() == 1); // ...then it's gone (a balance bot may take the empty slot)
    s.stop();
}

// Time trial: solo Viaduct race, every checkpoint sends the racer a split (increasing ms, last = finish).
// The Worker's race map table (tools/master/worker.js RACE_MAPS): every ranked race map's default time and split
// floors, as "quark_tests racefloors" prints it.
static std::string raceMapsJs() {
    std::string o = "const RACE_MAPS = {\n";
    for (auto& r : rankedRaceMaps()) {
        o += "  " + r.name + ": { defaultMs: " + std::to_string(r.defaultMs) + ", floors: [";
        for (size_t i = 0; i < r.floorsMs.size(); i++) o += (i ? ", " : "") + std::to_string(r.floorsMs[i]);
        o += "] },\n";
    }
    return o + "};\n";
}

// Race runs: the fastest plausible split per leg (path gap at the speed ceiling); the Worker keeps the same table.
// Ranked race maps: the official "for race" maps with a "racedefault" (map format 13); the race total sums them.
static void testRaceFloors() {
    printf("race split floors, ranked race and surf maps, the race total (and the Worker's copy)\n");
    const auto& rm = rankedRaceMaps();
    CHECK(!rm.empty());
    std::string names;
    for (auto& r : rm) names += " " + r.name + " (" + std::string(r.mode == MODE_SURF ? "surf, " : "") + std::to_string(r.defaultMs / 1000) + " s" + (r.fallbackDefault ? ", no racedefault yet" : "") + ")";
    printf("  ranked race maps:%s\n", names.c_str());
    for (size_t k = 1; k < rm.size(); k++) CHECK(rm[k - 1].name < rm[k].name);   // (sorted: the Worker's table order)
    for (auto& r : rm) {
        Map m;
        CHECK(m.load(r.name) && m.preferredMode == r.mode && isRaceMode(r.mode) && isOfficialMap(m) && rankedRaceMap(r.name) == &r);
        CHECK(r.fallbackDefault == (m.raceDefault <= 0) && r.defaultMs == (r.fallbackDefault ? kRaceFallbackDefaultS * 1000 : (int)std::lround(m.raceDefault * 1000)));
        CHECK(r.floorsMs == raceSplitFloorsMs(m) && r.floorsMs.size() == m.raceCheckpoints.size() && !r.floorsMs.empty());
        int total = 0;
        for (int x : r.floorsMs) { CHECK(x >= 0); total += x; }
        CHECK(total > 0);                    // (Surfy has one checkpoint, its finish, a few metres from the start as the crow flies)
        CHECK(r.defaultMs > total);          // a default no plausible run can't beat would make running pointless
        CHECK(r.defaultMs <= 3600000);
    }
    CHECK(!rankedRaceMap("foundry") && !rankedRaceMap(""));
    for (auto& [name, text] : officialMapTexts()) {   // every official race and surf map is on it (q: "the total race time also counts the 2 surf maps")
        Map m;
        if (m.parse(text) && isRaceMode(m.preferredMode)) CHECK(rankedRaceMap(name) != nullptr);
    }
    {   // the total: your best on each map, its default where you have none
        long long all = 0;
        for (auto& r : rm) all += r.defaultMs;
        int played = -1;
        CHECK(raceTotalMs({}, &played) == all && played == 0);
        CHECK(raceTotalMs({{rm[0].name, 30000}, {"foundry", 1}, {rm.back().name, 0}}, &played) == all - rm[0].defaultMs + 30000 && played == 1);
    }
    const std::vector<int>& f = rm[0].floorsMs;
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
    // tools/master/worker.js: its RACE_MAPS table must be this one (re-run "quark_tests racefloors" and paste it)
    std::ifstream w(findDataPath("maps") + "/../tools/master/worker.js");
    std::stringstream ws;
    ws << w.rdbuf();
    const std::string js = ws.str();
    if (!js.empty()) {
        const bool same = js.find(raceMapsJs()) != std::string::npos;
        CHECK(same);
        if (!same) printf("  tools/master/worker.js RACE_MAPS is out of date: paste the output of \"quark_tests racefloors\"\n");
    } else printf("  (tools/master/worker.js not found next to the maps: skipped the table check)\n");
    const std::string& map = rm[0].name;
    CHECK(buildRaceRunForm("0123456789abcdef", map, std::string(32, 'a'), ok.back(), ok, "Racer").find("ticket=0123456789abcdef&map=" + map + "&id=") == 0);
    CHECK(buildRaceRunForm("0123456789abcdef", map, std::string(32, 'a'), ok.back() + 1, ok, "Racer").empty());
}

static void testTimeTrial(uint16_t port) {
    if (rankedRaceMaps().empty()) { CHECK(!"no ranked race map"); return; }
    const std::string map = rankedRaceMaps()[0].name;
    printf("time trial: solo race splits on %s\n", map.c_str());
    Server s;
    ServerConfig cfg;
    cfg.map = map; cfg.mode = rankedRaceMaps()[0].mode; cfg.port = port;
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
    CHECK(a.hasSnapshot() && (a.mode().flags & MS_TIMETRIAL));   // (a shipped ranked race map: it counts)
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
    // a "time trial" anywhere but a shipped ranked race map doesn't count for the board
    Server s2;
    cfg.map = "foundry"; cfg.port = port + 1;
    { std::string e_; if (!s2.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient b;
    b.connect("127.0.0.1:" + std::to_string(port + 1), "racer");
    std::vector<NetClient*> cls2 = {&b};
    pump(s2, cls2, 1.0, idle);
    CHECK(b.hasSnapshot() && !(b.mode().flags & MS_TIMETRIAL));
    s2.stop();
    for (auto& r : rankedRaceMaps()) {   // a surf map's time trial counts only in surf (race's dash and grapple would beat its times)
        if (r.mode != MODE_SURF) continue;
        cfg.map = r.name; cfg.port = port + 2;
        for (int mode : {MODE_SURF, MODE_RACE}) {
            Server s3;
            cfg.mode = mode;
            { std::string e_; if (!s3.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
            CHECK(s3.timeTrialCounts() == (mode == MODE_SURF));
            s3.stop();
        }
        break;
    }
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
    for (int k = 0; k < 30 && s.ms_.phase != PHASE_LIVE; k++) pump(s, cls, 0.1, idle);   // (a loaded machine: the countdown's last ticks)
    CHECK(s.ms_.phase == PHASE_LIVE && s.ms_.pointCount >= 2 && s.ms_.pointCount == (int)s.map_.cartChecks.size());   // (Railyard: 3 since its 1.19.6 rebuild)
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
    pump(s, cls, 3.2, idle);   // (the freeze; a loaded machine (the full run) sometimes needs a little longer)
    for (int k = 0; k < 20 && s.ms_.phase != PHASE_LIVE; k++) pump(s, cls, 0.1, idle);
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
        // both in and spawned (all of it is in the warmup's free play: its 5 s countdown is still running at the end)
        pumpUntilSpawned(s, cls);
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
        if (w == WPN_SNIPER) { // one scoped shot: 40 body, 50 head (owner 1.18)
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
    pumpUntilSpawned(s, cls);
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
    // ...but a scope held over a switch locks nothing: fire on the laser shoots while the scope is still held (q, 2026-10-31)
    pump(s, cls, 0.6, [&](int) { UserCmd c; c.weapon = WPN_RIFLE; c.buttons = BTN_ALTFIRE; return c; });
    pump(s, cls, 0.6, [&](int) { UserCmd c; c.weapon = WPN_LASER; c.buttons = BTN_ALTFIRE; return c; });
    last = p.lastFire;
    pump(s, cls, 0.3, [&](int) { UserCmd c; c.weapon = WPN_LASER; c.buttons = BTN_ALTFIRE | BTN_FIRE; return c; });
    printf("  scope held over rifle -> laser, then fire: %s\n", p.lastFire != last ? "fired" : "locked");
    CHECK(p.weapon == WPN_LASER && !p.fireLocked && p.lastFire != last);
    s.stop();
}

static void testTickRateFallback(uint16_t port) {
    printf("tick rate starts at 1000 Hz; players losing snapshots lower the snapshot rate, then the tick rate\n");
    setTimeScale(1.0);
    g_serverRateCheck = true;
    struct Restore { ~Restore() { setTimeScale(kTestTimeScale); g_serverRateCheck = false; } } restore;
    Server s;
    ServerConfig cfg;
    cfg.autoStart = false;   // (warmup throughout: tick-rate steps go at once)
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    CHECK(s.tickRate() == 1000);
    // Loss and bandwidth only: this machine's own stalls (a busy CI box) would step the tick rate down out of turn.
    // Stalls are tested where the test makes them (testRateBetweenRounds, testRateUndoStepUp, testRateJitter).
    s.rateStalls_ = false;
    s.rateWindow_ = 1.0;
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "solo");
    b.connect("127.0.0.1:" + std::to_string(port), "duo");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 1.0, idle);
    CHECK(a.tickRate() == s.tickRate());
    // Healthy loopback clients report no loss and the server stays at 1000 Hz (snapshots go out every
    // other tick: counting ticks instead of snapshots once read as exactly 50% loss and dropped the rate).
    pump(s, cls, s.rateWindow_ * 1.6, idle);
    printf("  loopback: reported loss %d%%, tick rate %d, snapshot rate %d, stalls %.2f%%\n", a.snapshotLoss(), s.tickRate(), a.snapRate(), s.stallPct_);
    CHECK(a.snapshotLoss() <= 2);
    CHECK(s.tickRate() == 1000 && s.snapRate() == 500);
    // One of them losing snapshots (its own wifi): nothing changes.
    a.overrideReportedLoss(50);
    pump(s, cls, s.rateWindow_ * 2.2, idle);
    printf("  one player losing snapshots: %d Hz, %d snapshots/s\n", s.tickRate(), s.snapRate());
    CHECK(s.tickRate() == 1000 && s.snapRate() == 500);
    // Both (the server's uplink): the snapshot rate steps down first, to its floor, then the tick rate.
    b.overrideReportedLoss(50);
    std::vector<std::pair<int, int>> steps;   // (tick rate, snapshot rate) after each change
    std::pair<int, int> was{s.tickRate(), s.snapRate()};
    for (int k = 0; k < 160 && s.tickRate() > 125; k++) {
        pump(s, cls, 0.1, idle);
        std::pair<int, int> is{s.tickRate(), s.snapRate()};
        if (is != was) steps.push_back(was = is);
    }
    for (auto& st : steps) printf("  -> %d Hz, %d/s\n", st.first, st.second);
    std::vector<std::pair<int, int>> want = {{1000, 333}, {1000, 250}, {1000, 125}, {500, 125}, {250, 125}, {125, 125}};
    CHECK(steps == want);
    pump(s, cls, 0.3, idle);
    CHECK(s.tickRate() == 125); // floor: never below 125 Hz
    CHECK(a.tickRate() == 125 && a.snapRate() == 125);
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
    pumpUntilSpawned(s, cls);
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
    pumpUntilSpawned(s, cls);
    CHECK(p.alive && p.weapon == WPN_ROCKET);
    CHECK(level > 10);
    s.stop();
}

// Shooter-side damage (owner, 2026-10-28: "all damage should be client side ... this would only happen if you die on
// the server before the server gets your damage packet"): a game's "hit" report counts unless the shooter is dead
// there; its own hitscan and rocket hits on others aren't the server's any more (its blast on itself still is).
static void testClientHits() {
    printf("client hits: reports count, a dead shooter's don't, the server leaves the reporter's hits to it\n");
    auto fresh = [](Server& s, bool reports) {
        s.setLog([](const std::string&) {});
        CHECK(s.map_.parse("box -40 -1 -40 40 0 40\nspawn any 0 0 0\n"));
        s.cfg_.mode = s.ms_.mode = MODE_FFA;
        s.ms_.phase = PHASE_WARMUP;
        s.timeMs_ = 10000;
        for (int i = 0; i < 2; i++) {
            auto& p = s.players_[i];
            p.connected = true; p.alive = true; p.health = 100; p.team = TEAM_A; p.joinId = 100 + i;
            p.move.flags = MF_ONGROUND;
        }
        s.players_[1].move.pos = {0, 0, -5};
        if (reports) s.handleStringCmdForTest(0, "clienthits");
        CHECK(s.players_[0].clientHits == reports);
    };
    const std::string rifle = "hit " + std::to_string(WPN_RIFLE) + " 0 1 1 30 0 0 1 5 0 0 0";
    {   // a report counts, with its damage
        Server s; fresh(s, true);
        s.handleStringCmdForTest(0, rifle);
        CHECK(std::fabs(s.players_[1].health - 70) < 0.01f && s.players_[0].shotsHit == 1);
    }
    {   // the shooter dead on the server by the time it comes: nothing
        Server s; fresh(s, true);
        s.players_[0].alive = false;
        s.handleStringCmdForTest(0, rifle);
        CHECK(s.players_[1].health == 100);
    }
    {   // a client that doesn't report (a bot, an old game): its reports are ignored, the server's hits are its
        Server s; fresh(s, false);
        s.handleStringCmdForTest(0, rifle);
        CHECK(s.players_[1].health == 100);
    }
    {   // nonsense is dropped: yourself, a bad id, too many
        Server s; fresh(s, true);
        s.handleStringCmdForTest(0, "hit " + std::to_string(WPN_RIFLE) + " 0 1 0 30 0 0 1 5 0 0 0");
        s.handleStringCmdForTest(0, "hit " + std::to_string(WPN_RIFLE) + " 0 1 9 30 0 0 1 5 0 0 0");
        s.handleStringCmdForTest(0, "hit " + std::to_string(WPN_RIFLE) + " 0 99 1 30 0 0 1 5 0 0 0");
        CHECK(s.players_[0].health == 100 && s.players_[1].health == 100);
    }
    for (int reports = 0; reports < 2; reports++) {   // a rocket at both: the other's damage is the report's, your own the server's
        Server s; fresh(s, reports != 0);
        s.players_[1].move.pos = {1.5f, 0, 0};
        s.explodeForTest({0.75f, 0.5f, 0}, 0, WPN_ROCKET);
        CHECK(s.players_[0].health < 100);
        CHECK((s.players_[1].health < 100) == !reports);
    }
    {   // q, 2026-10-30, 1.19.3: "explosions aren't registering" - only direct rocket hits did damage online. The game
        // reported its blast exactly ON the floor / wall it met, so every cover ray started on that surface, hit it at
        // t = 0 and called each splash victim "behind cover". The blast goes off 5 cm short, as the server's does.
        Map m;
        CHECK(m.parse("box -40 -1 -40 40 0 40\nspawn any 0 0 0\n"));
        const Vec3 from(0, 1.6f, -10), aim = normalize(Vec3(1.2f, 0, 9.97f) - from);
        const float hit = m.raycast(from, aim, 100.0f);
        CHECK(hit > 0);
        const Vec3 onFloor = from + aim * hit, at = blastPoint(from, aim, hit);
        const AABB victim = playerBox(Vec3(0, 0, 10), false);   // 0.8 m from the blast: splash, not a direct hit
        CHECK(length(hitboxClosest(victim, at) - at) < weaponDef(WPN_ROCKET).splashRadius);
        // (the bug: a point on the surface - as rounding leaves it, exactly on or a hair inside - sees nobody)
        CHECK(!blastReaches(m, Vec3(onFloor.x, 0.0f, onFloor.z), victim));
        CHECK(!blastReaches(m, Vec3(onFloor.x, -1e-5f, onFloor.z), victim));
        CHECK(blastReaches(m, blastPoint(from, aim, length(Vec3(onFloor.x, -1e-5f, onFloor.z) - from)), victim));
        CHECK(blastReaches(m, at, victim));
        CHECK(std::fabs(length(at - onFloor) - kBlastBackoff) < 1e-4f && at.y > 0);
        CHECK(!blastReaches(m, at, playerBox(Vec3(0, -3, 10), false)));   // (under the floor: still behind cover)
        // and the server's own blast from the same spot (a game that doesn't report) still hurts both, as before
        Server s; fresh(s, false);
        s.players_[0].move.pos = {0, 0, -10};
        s.players_[1].move.pos = {0, 0, 10};
        s.explodeForTest(at, 0, WPN_ROCKET);
        CHECK(s.players_[1].health < 100);
    }
}

// The 2026-10 feel review's server fixes, on a server that isn't started (no network: the players are set up by hand
// and the server's own functions are called): the hit before the kill (F13), shotgun headshots (F14), the bubble
// stream's count (F15), splash round cover (F16), the killer cam (F11), lag compensation between records (F21), refire
// on the command clock (F22), the near-miss kills (T9), ring-out credit (T21) and the death screen's text (X1).
static void testFeelFixes() {
    printf("feel fixes: hit before kill, shotgun headshots, bubble count, splash, killer cam, rewind lerp, command clock, ring-outs\n");
    // T9: 20 rifle body shots kill; two point-blank head blasts (18 of 20 pellets on the head) kill
    CHECK(weaponDef(WPN_RIFLE).damage * 20 >= kMaxHealth);
    CHECK(2 * (18 * headDamage(WPN_SHOTGUN) + 2 * weaponDef(WPN_SHOTGUN).damage) >= kMaxHealth);
    // T10 dropped (owner): the crossbow stays 50 body, two body bolts kill
    CHECK(2 * weaponDef(WPN_CROSSBOW).damage >= kMaxHealth);
    auto fresh = [](Server& s, const char* mapText, int n) {
        s.setLog([](const std::string&) {});
        CHECK(s.map_.parse(mapText));
        s.cfg_.mode = s.ms_.mode = MODE_FFA;
        s.ms_.phase = PHASE_WARMUP;
        s.timeMs_ = 10000;
        for (int i = 0; i < n; i++) {
            auto& p = s.players_[i];
            p.connected = true; p.alive = true; p.health = 100; p.team = TEAM_A; p.joinId = 100 + i;
            p.move.flags = MF_ONGROUND;
        }
    };
    const char* open = "box -40 -1 -40 40 0 40\nspawn any 0 0 0\n";
    auto events = [](Server& s, int i, int type) {
        std::vector<int> at;
        for (size_t k = 0; k < s.players_[i].events.size(); k++) if (s.players_[i].events[k].type == type) at.push_back((int)k);
        return at;
    };
    {   // F13: batched laser damage, then the killing hit: the attacker's last EV_HIT comes before the EV_KILL
        Server s; fresh(s, open, 2);
        s.players_[1].move.pos = {5, 0, 0};
        s.damagePlayer(1, 0, 1, WPN_LASER, Vec3());    // goes out at once (none for 50 ms before)
        s.damagePlayer(1, 0, 1, WPN_LASER, Vec3());    // batched
        s.damagePlayer(1, 0, 98, WPN_LASER, Vec3());   // the kill
        auto hits = events(s, 0, EV_HIT), kills = events(s, 0, EV_KILL);
        CHECK(!s.players_[1].alive && kills.size() == 1 && hits.size() == 2 && hits.back() < kills[0]);
        float total = 0;
        for (int k : hits) total += s.players_[0].events[k].c / 100.0f;
        CHECK(std::fabs(total - 100) < 0.05f);   // every point of it shown, none left for later
        // X1: the victim's copy says the killer's health and the respawn delay (warmup: 1.5 s); nobody else's does
        auto vk = events(s, 1, EV_KILL);
        CHECK(vk.size() == 1 && s.players_[1].events[vk[0]].text == "100 1500" && s.players_[0].events[kills[0]].text.empty());
        // F11 (free for all): the killer is watched
        CHECK(s.players_[1].specTarget == 0 && (s.perspectivesForTest(1) & 1u));
    }
    {   // X1 in ranked: no health
        Server s; fresh(s, open, 2);
        s.cfg_.ranked = true;
        s.damagePlayer(1, 0, 150, WPN_ROCKET, Vec3());
        auto vk = events(s, 1, EV_KILL);
        CHECK(vk.size() == 1 && s.players_[1].events[vk[0]].text.rfind("-1 ", 0) == 0);
    }
    {   // F11 (team mode): the killer's view for kKillCamTime, then teammates only
        Server s; fresh(s, open, 3);
        s.cfg_.mode = s.ms_.mode = MODE_KOTH;
        s.players_[0].team = TEAM_B;   // the killer; 1 the victim, 2 their teammate
        s.players_[2].move.pos = {-5, 0, 0};
        s.damagePlayer(1, 0, 150, WPN_ROCKET, Vec3());
        uint32_t m = s.perspectivesForTest(1);
        CHECK((m & 1u) && (m & 4u));
        s.timeMs_ += (uint64_t)(kKillCamTime * 1000) + 10;
        m = s.perspectivesForTest(1);
        CHECK(!(m & 1u) && (m & 4u));
        s.handleStringCmdForTest(1, "spectate 0");   // asking for the enemy again changes nothing
        CHECK(!(s.perspectivesForTest(1) & 1u));
    }
    {   // T21: knocked off within 3 s: the attacker's kill (no suicide point lost); later than that: nobody's
        Server s; fresh(s, open, 2);
        s.ms_.phase = PHASE_LIVE;
        s.damagePlayer(1, 0, 10, WPN_ROCKET, Vec3(5, 4, 0));
        s.timeMs_ += 2500;
        CHECK(s.ringOutKiller(1) == 0);
        s.players_[1].move.pos.y = s.map_.killY() - 1;
        s.simulateForTest();
        CHECK(!s.players_[1].alive && s.players_[0].kills == 1 && s.players_[1].score == 0);
        auto vk = events(s, 0, EV_KILL);
        CHECK(!vk.empty() && s.players_[0].events[vk.back()].a == 0 && (s.players_[0].events[vk.back()].c & KILL_WEAPON) == 252);
        s.spawnPlayer(1);
        s.players_[1].health = 100;
        s.damagePlayer(1, 0, 10, WPN_CROSSBOW, Vec3(5, 4, 0));
        s.timeMs_ += (uint64_t)(kRingOutCredit * 1000) + 100;
        CHECK(s.ringOutKiller(1) == -1);
        s.players_[0].joinId = 999;    // (and a newcomer in the attacker's slot is never credited)
        s.timeMs_ -= 1000;
        CHECK(s.ringOutKiller(1) == -1);
    }
    {   // F16: splash reaches a player whose body centre is behind a crate but whose head shows; full cover still blocks
        Server s; fresh(s, "box -40 -1 -40 40 0 40\nbox 0 0 -5 1 1.2 5\nbox 10 0 -5 11 3 5\nspawn any 0 0 20\n", 3);
        s.players_[0].move.pos = {0, 0, 30};    // the shooter, far off
        s.players_[1].move.pos = {1.5f, 0, 0};  // behind the crate
        s.players_[2].move.pos = {11.5f, 0, 0}; // behind the wall
        s.explodeForTest({-2, 1, 0}, 0, WPN_ROCKET);
        s.explodeForTest({8.6f, 1, 0}, 0, WPN_ROCKET);
        printf("  splash over a crate: %.1f damage; behind a wall: %.1f\n", 100 - s.players_[1].health, 100 - s.players_[2].health);
        CHECK(s.players_[1].health < 100 && s.players_[2].health == 100);
    }
    {   // T11: own splash rises evenly with the blast's height: 10% up to feet + 0.3 m, full from chest height
        float prev = -1, at3 = 0, at31 = 0;
        for (float h : {0.0f, 0.3f, 0.31f, 0.5f, 0.75f, 1.0f, 1.2f, 1.5f}) {
            Server s; fresh(s, open, 1);
            s.explodeForTest({0, h, 0}, 0, WPN_ROCKET);
            float d = 100 - s.players_[0].health;
            CHECK(d >= prev - 1e-3f);
            prev = d;
            if (h == 0.3f) at3 = d;
            if (h == 0.31f) at31 = d;
            if (h <= 0.3f) CHECK(std::fabs(d - weaponDef(WPN_ROCKET).damage * 0.1f) < 0.01f);
            if (h >= 1.2f) CHECK(std::fabs(d - weaponDef(WPN_ROCKET).damage) < 0.01f);
            if (h == 0.5f) printf("  own rocket at knee height (0.5 m): %.1f damage\n", d);
        }
        CHECK(at31 - at3 < 1.0f);   // no step: a hair higher is a hair more
    }
    {   // F14: a blast with a pellet or two on the head isn't a headshot; one that's mostly head is
        for (bool high : {false, true}) {
            Server s; fresh(s, open, 2);
            s.players_[0].weapon = WPN_SHOTGUN;
            const float dist = high ? 1.2f : 3.0f;
            s.players_[1].move.pos = {-dist, 0, 0};
            s.simulateForTest();   // (a history record where they stand)
            Vec3 eye = s.players_[0].move.pos + Vec3(0, kEyeHeight, 0);
            Vec3 d = normalize(s.players_[1].move.pos + Vec3(0, high ? 1.68f : 1.3f, 0) - eye);
            UserCmd c; c.pitch = std::asin(d.y); c.yaw = std::atan2(-d.x, -d.z); c.weapon = WPN_SHOTGUN; c.buttons = BTN_FIRE;
            c.viewTick = (uint32_t)s.timeMs_;
            s.players_[0].events.clear();
            CHECK(s.fireWeaponForTest(0, c));
            auto hits = events(s, 0, EV_HIT);
            CHECK(!hits.empty());
            bool head = !hits.empty() && s.players_[0].events[hits.back()].text == "h";
            printf("  shotgun at the %s: %.1f damage, %s\n", high ? "head, point blank" : "neck, 3 m", 100 - s.players_[1].health, head ? "headshot" : "body");
            CHECK(head == high);
        }
    }
    {   // F15: an enemy 3 m away behind a wall doesn't shrink the bubble stream; one in sight does
        auto stream = [&](bool wall) {
            Server s; fresh(s, wall ? "box -40 -1 -40 40 0 40\nbox -2 0 -5 -1.5 4 5\nspawn any 0 0 20\n" : open, 2);
            s.players_[0].weapon = WPN_BUBBLE;
            s.players_[1].move.pos = {-3, 0, 0};
            UserCmd c; c.yaw = PI * 0.5f; c.weapon = WPN_BUBBLE; c.buttons = BTN_ALTFIRE;
            s.fireWeaponForTest(0, c);
            int n = 0;
            for (auto& pr : s.projectiles_) n += pr.active && pr.type == WPN_BUBBLE;
            return n;
        };
        int seen = stream(false), hidden = stream(true);
        printf("  bubble stream: %d bubbles with an enemy 3 m away in sight, %d behind a wall\n", seen, hidden);
        CHECK(seen == bubbleStreamCount(3) && hidden == kBubbleStreamMax);
    }
    {   // F21: a rewind between two records lands between them, not on the older one
        Server s; fresh(s, open, 1);
        auto& p = s.players_[0];
        for (int k = 0; k < 4; k++) {   // records 8 ms apart, moving 0.08 m each
            p.move.pos = {0.08f * k, 0, 0};
            s.simulateForTest();
            s.tick_++; s.timeMs_ += 8;
        }
        uint64_t t0 = s.timeMs_ - 8 * 4;   // (the first record)
        bool cr;
        Vec3 at = s.rewoundPosForTest(0, t0 + 8 + 4, &cr);
        printf("  rewind 4 ms past a record: x %.3f (records at 0.080 and 0.160)\n", at.x);
        CHECK(std::fabs(at.x - 0.12f) < 0.002f);
        CHECK(std::fabs(s.rewoundPosForTest(0, t0 + 16, &cr).x - 0.16f) < 1e-4f);   // on a record: that record
    }
    {   // T22: the round end is 2.5 s in 1v1 (and duel / tourney), still 4 s in team vs
        for (int mode : {MODE_ARENA, MODE_VS}) {
            Server s; fresh(s, open, 2);
            s.cfg_.mode = s.ms_.mode = (uint8_t)mode;
            s.mp_ = &builtinModeParams(mode);   // (the mode's numbers, as rulesReset sets them: roundEndTime)
            s.players_[1].team = TEAM_B;
            s.ms_.phase = PHASE_LIVE;
            s.phaseEnd_ = s.time() + 60;
            s.damagePlayer(1, 0, 150, WPN_ROCKET, Vec3());
            for (int k = 0; k < 3 && s.ms_.phase == PHASE_LIVE; k++) s.simulateForTest();   // (the rules see one side out)
            printf("  %s: round end %.1f s\n", mode == MODE_ARENA ? "1v1" : "vs", s.phaseEnd_ - s.time());
            CHECK(s.ms_.phase == PHASE_ROUND_END && std::fabs(s.phaseEnd_ - s.time() - (mode == MODE_ARENA ? 2.5 : 4.0)) < 1e-6);
        }
    }
    {   // X9: a shot out of a player's sight within 60 m is sent to them muffled (roughly where, no shooter), at most
        // every 0.3 s; one they can see, or farther off, isn't
        Server s; fresh(s, open, 4);
        auto& p = s.players_[0];
        p.weapon = WPN_RIFLE; p.move.pos = {1, 0, 1};
        s.players_[1].move.pos = {40, 0, 1};    // out of sight (never seen), 39 m
        s.players_[2].move.pos = {-39, 0, 0};   // in sight
        s.players_[3].move.pos = {0, 0, -70};   // too far
        for (int i = 1; i < 4; i++) s.players_[i].events.clear();
        UserCmd c; c.weapon = WPN_RIFLE; c.msec = 1; c.pitch = 1.3f;
        int sent[4] = {};
        for (int t = 0; t < 100; t++) {   // 0.8 s of holding fire
            s.timeMs_ += 8;
            s.players_[2].lastSeen[0] = s.time();
            for (int k = 0; k < 8; k++) { c.buttons = BTN_FIRE; s.runPlayerCmdForTest(0, c); }
        }
        std::string where;
        for (int i = 1; i < 4; i++)
            for (auto& e : s.players_[i].events) if (e.type == EV_GUNFIRE) { sent[i]++; where = e.text; CHECK(e.a == 255 && e.c == WPN_RIFLE); }
        printf("  muffled shots: %d to the one out of sight (at \"%s\"), %d in sight, %d at 70 m; %d shots fired\n", sent[1], where.c_str(), sent[2], sent[3], p.shotsFired);
        CHECK(sent[1] >= 2 && sent[1] <= 3 && sent[2] == 0 && sent[3] == 0 && where == "0 0 0");
    }
    {   // F22: refire on the command clock. 1 ms commands, 8 a tick (125 Hz): a held rifle fires on the command that
        // ends its cooldown (not the first of the tick that does), a tap there still fires, and a burst of commands
        // never fires faster than real time
        Server s; fresh(s, open, 1);
        auto& p = s.players_[0];
        p.weapon = WPN_RIFLE;
        UserCmd c; c.weapon = WPN_RIFLE; c.msec = 1;
        int early = 0, late = 0, shots = 0;
        for (int t = 0; t < 120; t++) {   // ~1 s of ticks, fire held
            s.timeMs_ += 8;
            for (int k = 0; k < 8; k++) {
                c.buttons = BTN_FIRE;
                double nf = p.nextFire;
                int before = p.shotsFired;
                c.viewTick = (uint32_t)s.timeMs_;
                s.runPlayerCmdForTest(0, c);
                if (p.shotsFired > before && shots++ > 0) {
                    early += p.cmdTime < nf - 1e-9;          // fired before its cooldown was up on the command clock
                    late += p.cmdTime - 0.001 >= nf - 1e-9;  // ... or a command after the one that ended it
                }
            }
        }
        printf("  held rifle: %d shots in ~0.96 s, %d early, %d late\n", shots, early, late);
        CHECK(early == 0 && late == 0 && shots >= 6 && shots <= 8);
        // a tap of 2 commands that starts on the command ending the cooldown fires (it fell between ticks before)
        for (int t = 0; t < 40; t++) { s.timeMs_ += 8; for (int k = 0; k < 8; k++) { c.buttons = 0; s.runPlayerCmdForTest(0, c); } }
        int before = p.shotsFired;
        for (int k = 0; k < 2; k++) { c.buttons = BTN_FIRE; s.runPlayerCmdForTest(0, c); }
        CHECK(p.shotsFired == before + 1);
        double nf = p.nextFire;
        c.buttons = 0;
        while (p.cmdTime + 0.001 < nf - 1e-9) { if (p.cmdTime >= s.time() - 1e-9) s.timeMs_ += 8; s.runPlayerCmdForTest(0, c); }
        if (p.cmdTime >= s.time() - 1e-9) s.timeMs_ += 8;   // (the next command's tick)
        before = p.shotsFired;
        c.buttons = BTN_FIRE; s.runPlayerCmdForTest(0, c); s.runPlayerCmdForTest(0, c);   // tapped right at the end
        CHECK(p.shotsFired == before + 1);
        // 200 commands of 8 ms in one tick: the command clock stops at the server's, so one shot at most
        for (int t = 0; t < 40; t++) { s.timeMs_ += 8; for (int k = 0; k < 8; k++) { c.buttons = 0; s.runPlayerCmdForTest(0, c); } }
        before = p.shotsFired;
        c.msec = 8; c.buttons = BTN_FIRE;
        for (int k = 0; k < 200; k++) s.runPlayerCmdForTest(0, c);
        CHECK(p.shotsFired - before == 1 && p.cmdTime <= s.time() + 1e-9);
    }
}

// T13: a click on the sniper / crossbow / rocket up to kTapBuffer before it's ready fires when it is; an earlier one,
// the rocket's alt-fire (lob) and other weapons aren't kept.
static void testTapBuffer() {
    printf("tap buffer: slow weapons keep a click up to 0.1 s early\n");
    for (int w : {WPN_SNIPER, WPN_CROSSBOW, WPN_ROCKET, WPN_RIFLE}) {
        for (double early : {0.05, 0.15}) {
            for (bool alt : {false, true}) {
                if (alt && w != WPN_ROCKET) continue;
                Server s;
                s.setLog([](const std::string&) {});
                CHECK(s.map_.parse("box -40 -1 -40 40 0 40\nspawn any 0 0 0\n"));
                s.cfg_.mode = s.ms_.mode = MODE_FFA;
                s.timeMs_ = 10000;
                auto& p = s.players_[0];
                p.connected = p.alive = true; p.health = 100; p.move.flags = MF_ONGROUND; p.weapon = w;
                UserCmd c; c.weapon = (uint8_t)w; c.msec = 1; c.pitch = 1.2f;
                auto run = [&](uint16_t b, int n) {
                    for (int k = 0; k < n; k++) { if (p.cmdTime >= s.time() - 1e-9) s.timeMs_ += 1; c.buttons = b; s.runPlayerCmdForTest(0, c); }
                };
                const uint16_t btn = alt ? BTN_ALTFIRE : BTN_FIRE;
                run(btn, 1);
                run(0, 1);
                const int shots0 = p.shotsFired;
                const double ready = p.nextFire;
                while (p.cmdTime + 0.001 < ready - early) run(0, 1);
                run(btn, 3);   // a 3 ms tap, early
                run(0, (int)((early + 0.05) * 1000));
                const int got = p.shotsFired - shots0;
                const bool want = tapBuffered(w) && early <= kTapBuffer && !alt;
                printf("  %-8s%s tapped %.2f s early: %d shot(s)\n", weaponDef(w).name, alt ? " (alt)" : "", early, got);
                CHECK(got == (want ? 1 : 0));
            }
        }
    }
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
    // any brush 1/8 m thin is wallbang too (owner, 2026-10-30), whatever its material; a thicker one isn't
    Map t2;
    CHECK(t2.parse("box -10 -1 -10 10 0 10\nbox -1 0 -5 -0.875 3 5\nbox 4 0 -5 5 3 5\nspawn any -5 0 0\n"));
    bang = -2;
    t = t2.raycastBullet({-5, 1.5f, 0}, {1, 0, 0}, 50, &bang);
    CHECK(std::fabs(bang - 4) < 0.01f && std::fabs(t - 9) < 0.01f);
    Map t3;
    CHECK(t3.parse("box -10 -1 -10 10 0 10\nbox -1 0 -5 -0.75 3 5\nspawn any -5 0 0\n"));
    bang = -2;
    t = t3.raycastBullet({-5, 1.5f, 0}, {1, 0, 0}, 50, &bang);
    CHECK(bang < 0 && std::fabs(t - 4) < 0.01f);
}

// Syringe: each use heals you 5 hp, with a 1 s cooldown (owner 1.17; halved 2026-10-31); a thrown one heals the teammate it hits by 15
// and resets their cooldowns; it does nothing to an enemy.
// A spawn with an open 5 m lane in front of it (-z) and floor at its end: where the syringe tests throw (spawns no
// longer keep teammates apart, T16, so whichever one b got may face a wall)
static Vec3 openLane(const Map& m, Vec3 fallback) {
    for (auto& sp : m.spawns) {
        Vec3 p = sp.pos, end = p + Vec3(0, 0, -5);
        bool clear = m.raycast(p + Vec3(0, 0.5f, 0), {0, 0, -1}, 6) < 0 && m.raycast(p + Vec3(0, 1.6f, 0), {0, 0, -1}, 6) < 0;
        float down = m.raycast(end + Vec3(0, 1, 0), {0, -1, 0}, 3);
        if (clear && down > 0.9f && down < 1.1f) return p;
    }
    return fallback;
}
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
    // (the cooldown runs from the use's command on the command clock, F22: the server clock or up to 0.1 s behind it)
    CHECK(pa.health == 55 && pa.nextFire - 1.0 <= first + 0.002 && pa.nextFire - 1.0 >= first - 0.102);
    while (s.time() < first + 0.9) pump(s, cls, 0.01, heal);   // held: nothing more until the 1 s cooldown is over
    CHECK(pa.health == 55);
    while (s.time() < first + 1.2) pump(s, cls, 0.01, heal);
    printf("  held 1.2 s: %.0f hp\n", pa.health);
    CHECK(pa.health == 60);
    // b throws at a, 5 m in front of it (yaw 0 looks down -z)
    pa.health = 40; pa.move.dashCharges = 0; pa.nextFire = s.time() + 5;
    pa.move.dashCooldownMs = 30000; pa.move.dashRecharging = 2; pa.move.dashRechargeMs[0] = pa.move.dashRechargeMs[1] = 30000;   // (nothing comes back on its own)
    Vec3 base = openLane(s.map_, pb.move.pos);
    auto place = [&] { pb.move.pos = base; pb.move.vel = {}; pa.move.pos = base + Vec3(0, 0, -5); pa.move.vel = {}; };
    for (int k = 0; k < 5; k++) { place(); pump(s, cls, 0.02, idle); }
    int tick = 0;
    pump(s, cls, 0.5, [&](int i) { UserCmd c; c.weapon = WPN_SYRINGE; if (i == 1) { c.yaw = 0; c.pitch = -0.05f; if (tick++ < 30) c.buttons = BTN_ALTFIRE; } if (i == 0) place(); return c; });
    printf("  thrown: %.0f hp (from 40), dash charges %d (%d recharging, cooldown %d ms)\n", pa.health, (int)pa.move.dashCharges,
           (int)pa.move.dashRecharging, (int)pa.move.dashCooldownMs);
    CHECK(pa.health >= 54 && pa.health <= 57);
    CHECK(pa.move.dashCharges == 2 && pa.move.dashRecharging == 0 && pa.move.dashCooldownMs == 0 && pa.nextFire <= s.time());
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
    CHECK(seen.size() == (size_t)kMapMaterials && seen[8] == 8 && seen[9] == 9 && seen[10] == kMatNoGrapple && seen[11] == kMatAcid && nextMapMaterial(kMatAcid) == kMatIce && nextMapMaterial(kMatBrick) == kMatGlass && nextMapMaterial(kMatTerracotta) == kMatBrickBreak && nextMapMaterial(kMatCobbleBreak) == kMatBlocks && nextMapMaterial(kMapMaterials - 1) == 0);   // M: ..., lava, trim, clip, nograpple, acid, ice .. diamond plate, brick, glass .. terracotta, breakable brick, glass, cobblestone, the 100 block looks, base
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
            a.gridRefs != b.gridRefs || a.hasWallbang != b.hasWallbang || a.lava.size() != b.lava.size() || a.acid.size() != b.acid.size() || a.cells.size() != b.cells.size() ||
            memcmp(&a.bounds, &b.bounds, sizeof a.bounds) != 0)
            return false;
        for (size_t k = 0; k < a.brushes.size(); k++)
            if (memcmp(&a.brushes[k].box, &b.brushes[k].box, sizeof(AABB)) || a.brushes[k].material != b.brushes[k].material || a.brushes[k].generated != b.brushes[k].generated) return false;
        for (size_t k = 0; k < a.lava.size(); k++) if (memcmp(&a.lava[k], &b.lava[k], sizeof(AABB))) return false;
        for (size_t k = 0; k < a.acid.size(); k++) if (memcmp(&a.acid[k], &b.acid[k], sizeof(AABB))) return false;
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
        if (w == WPN_SYRINGE || w == WPN_LOCATOR) continue;    // (it heals / does no damage)
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
    std::string logPath = (std::filesystem::temp_directory_path() / ("quark_test_duel_" + std::to_string(getpid()) + ".log")).string();   // (not in the working folder)
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
    for (int i = 0; i < 100 && s.ms_.phase == PHASE_WARMUP; i++) { skipCountdowns(s); pump(s, cls, 0.05, idle); }   // -> round 1
    CHECK(s.ms_.phase == PHASE_FREEZE || s.ms_.phase == PHASE_LIVE);
    unsigned seen = 0; int prev = -1;
    for (int round = 0; round < 12 && s.ms_.phase != PHASE_MATCH_END; round++) {
        for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE && s.ms_.phase != PHASE_MATCH_END; i++) { skipCountdowns(s); pump(s, cls, 0.05, idle); }
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
    for (int i = 0; i < 100 && (s.ms_.phase == PHASE_WARMUP || s.ms_.phase == PHASE_FREEZE); i++) { skipCountdowns(s); pump(s, cls, 0.05, idle); }
    CHECK(s.ms_.phase == PHASE_LIVE && s.ms_.duelMod < DUEL_MOD_COUNT);
    int alive = 0;
    for (auto& p : s.players_) alive += p.connected && p.alive;
    CHECK(alive == 4);
    s.stop();
}

// 1v1 winner stays (owner, 2026-11-01: "make 1v1 modes swap in specators cyclicly winner stays, even ranked"): the
// spectators wait in line in join order; at each match end the loser goes to the back and the first in line plays the
// winner. A draw keeps both; "team spec" leaves the line; a seat left open in warmup is filled from the line.
static void testWinnerStays(uint16_t port) {
    printf("1v1 winner stays: spectators rotate in, the loser to the back of the line\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "dojo"; cfg.mode = MODE_ARENA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b, c, d;
    std::vector<NetClient*> cls;
    const char* names[] = {"ann", "bob", "cat", "dan"};
    NetClient* all[] = {&a, &b, &c, &d};
    for (int i = 0; i < 4; i++) { all[i]->connect("127.0.0.1:" + std::to_string(port), names[i]); cls.push_back(all[i]); pump(s, cls, 0.4, idle); }
    auto P = [&](NetClient& n) -> ServerPlayer& { return s.players_[n.clientId()]; };
    CHECK(P(a).team != TEAM_SPECTATOR && P(b).team != TEAM_SPECTATOR && P(a).team != P(b).team);
    CHECK(P(c).team == TEAM_SPECTATOR && P(d).team == TEAM_SPECTATOR);
    CHECK(P(c).queueSeq > 0 && P(d).queueSeq > P(c).queueSeq);   // (join order)
    auto endMatch = [&](int winner) {   // the match end screen, then its end
        s.matchWinner_ = winner;
        s.ms_.phase = PHASE_MATCH_END;
        s.phaseEnd_ = s.time();
        pump(s, cls, 0.3, idle);
    };
    // ann wins: bob to the back of the line, cat (first in line) plays ann
    const int annTeam = P(a).team, bobTeam = P(b).team;
    endMatch(annTeam);
    CHECK(P(a).team == annTeam);
    CHECK(P(c).team == bobTeam && P(c).queueSeq == 0);
    CHECK(P(b).team == TEAM_SPECTATOR && P(b).queueSeq > P(d).queueSeq);
    CHECK(s.ms_.phase == PHASE_WARMUP);
    // a draw: the same two again
    endMatch(-1);
    CHECK(P(a).team == annTeam && P(c).team == bobTeam);
    // cat wins: ann out to the back, dan in (then bob, then ann: cyclic)
    endMatch(bobTeam);
    CHECK(P(c).team == bobTeam && P(d).team == annTeam && P(a).team == TEAM_SPECTATOR);
    CHECK(P(b).queueSeq > 0 && P(a).queueSeq > P(b).queueSeq);
    // choosing to watch leaves the line: bob is skipped
    b.sendCommand("team spec");
    pump(s, cls, 0.3, idle);
    CHECK(P(b).queueSeq == 0);
    endMatch(bobTeam);   // cat wins again: dan out, ann (next in line, bob left it) in
    CHECK(P(a).team == annTeam && P(d).team == TEAM_SPECTATOR && P(b).team == TEAM_SPECTATOR);
    // a leaver's seat in warmup goes to the first in line (dan), once the leaver is gone (their reconnect grace over)
    const int cid = c.clientId();
    c.disconnect();
    std::vector<NetClient*> rest = {&a, &b, &d};
    pump(s, rest, 0.2, idle);
    s.players_[cid].awayUntil = 1e-6;   // (the grace over now)
    pump(s, rest, 0.5, idle);
    CHECK(P(d).team == bobTeam);
    s.stop();
}

// Winner stays mode (MODE_WINNER): rounds, the round's loser to the back of the line, round wins are the score, first to 10.
static void testWinnerStaysMode(uint16_t port) {
    printf("winner stays mode: each round's loser to the back of the line, first to 10 round wins\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true;
    cfg.map = "dojo"; cfg.mode = MODE_WINNER; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    CHECK(s.map_.supportsMode(MODE_WINNER));   // (a 1v1 map offers it: prefabs.py)
    NetClient a, b, c;
    std::vector<NetClient*> cls;
    const char* names[] = {"ann", "bob", "cat"};
    NetClient* all[] = {&a, &b, &c};
    for (int i = 0; i < 3; i++) { all[i]->connect("127.0.0.1:" + std::to_string(port), names[i]); cls.push_back(all[i]); pump(s, cls, 0.4, idle); }
    auto P = [&](NetClient& n) -> ServerPlayer& { return s.players_[n.clientId()]; };
    CHECK(P(c).team == TEAM_SPECTATOR && P(c).queueSeq > 0);
    const int annTeam = P(a).team, other = annTeam == TEAM_A ? TEAM_B : TEAM_A;
    auto live = [&] { for (int i = 0; i < 200 && s.ms_.phase != PHASE_LIVE && s.ms_.phase != PHASE_MATCH_END; i++) { skipCountdowns(s); pump(s, cls, 0.05, idle); } };
    auto seated = [&](int team) { for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].team == team) return i; return -1; };
    live();
    CHECK(s.ms_.phase == PHASE_LIVE);
    CHECK(s.ms_.roundsToWin == 10);
    // round 1: ann beats bob; bob to the back, cat in
    s.killPlayer(b.clientId(), a.clientId(), WPN_RIFLE);
    pump(s, cls, 0.1, idle);
    CHECK(s.ms_.phase == PHASE_ROUND_END);
    live();
    CHECK(P(a).team == annTeam && P(c).team == other && P(b).team == TEAM_SPECTATOR && P(b).queueSeq > 0);
    CHECK(P(a).score == 1 && P(b).score == 0);   // (a round win, not the kill: 1)
    CHECK((annTeam == TEAM_A ? s.ms_.scoreA : s.ms_.scoreB) == 1 && (annTeam == TEAM_A ? s.ms_.scoreB : s.ms_.scoreA) == 0);
    // round 2: cat beats ann: ann out, bob in; cat's 1 against bob's 0
    s.killPlayer(a.clientId(), c.clientId(), WPN_RIFLE);
    pump(s, cls, 0.1, idle);
    live();
    CHECK(P(c).team == other && P(b).team == annTeam && P(a).team == TEAM_SPECTATOR);
    CHECK((annTeam == TEAM_A ? s.ms_.scoreB : s.ms_.scoreA) == 1 && P(a).score == 1);
    // cat wins every round from here: first to 10 ends the match, after 9 more rounds won
    int rounds = 0;
    for (; rounds < 20 && s.ms_.phase == PHASE_LIVE; rounds++) {
        const int foe = seated(annTeam);
        CHECK(foe >= 0);
        if (foe < 0) break;
        s.killPlayer(foe, c.clientId(), WPN_RIFLE);
        pump(s, cls, 0.1, idle);
        live();
    }
    CHECK(rounds == 9);
    CHECK(s.ms_.phase == PHASE_MATCH_END && s.matchWinner_ == other && P(c).score == 10);
    s.stop();
}

// Accuracy is the mean of each weapon's own (owner, 2026-11-01): a second of the laser doesn't swamp the rest.
static void testAccuracyPerWeapon() {
    printf("accuracy: the average of each fired weapon's hits / shots, reset each match\n");
    ServerPlayer p;
    for (int i = 0; i < 100; i++) { p.addShot(WPN_LASER); if (i < 30) p.addHit(WPN_LASER); }
    for (int i = 0; i < 10; i++) { p.addShot(WPN_RIFLE); if (i < 8) p.addHit(WPN_RIFLE); }
    CHECK(p.accuracy() == 55);   // (30% and 80%; by total shots it was 34%)
    p.resetAccuracy();
    CHECK(p.accuracy() == 0 && p.shotsFired == 0);
}

// Planting / defusing roots your own movement and fire but keeps knockback (owner, 2026-11-01): the round start's full
// freeze used to throw the planter's velocity away.
static void testPlantKnockback(uint16_t port) {
    printf("bomb: a planter can't move or shoot, but knockback still moves them\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "depot"; cfg.mode = MODE_BOMB; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a;
    std::vector<NetClient*> cls = {&a};
    a.connect("127.0.0.1:" + std::to_string(port), "ann");
    pumpUntilSpawned(s, cls);
    const int id = a.clientId();
    ServerPlayer& p = s.players_[id];
    CHECK(p.alive);
    s.ms_.phase = PHASE_LIVE;
    s.ms_.actionPlayer = (uint8_t)id;   // (planting)
    p.move.vel = {12, 0, 0};             // (a rocket's push)
    const Vec3 start = p.move.pos;
    UserCmd c; c.msec = 8; c.forward = -127; c.buttons = BTN_USE | BTN_JUMP | BTN_FIRE; c.yaw = 0;
    c.seq = 1000;
    s.runPlayerCmdForTest(id, c);
    printf("  planting, pushed at 12 m/s: x moved %.3f m, vel x %.2f, vel y %.2f\n", p.move.pos.x - start.x, p.move.vel.x, p.move.vel.y);
    CHECK(p.move.pos.x - start.x > 0.05f && p.move.vel.x > 5.0f);   // the push carries them (frozen, it was dropped)
    CHECK(p.move.vel.y <= 0.0f);                                     // no jump of their own
    CHECK(!(p.move.flags & MF_FROZEN));
    s.ms_.actionPlayer = 255;   // not planting: their own input moves them again
    p.move.vel = {};
    c.seq = 1001; c.buttons = BTN_JUMP;
    s.runPlayerCmdForTest(id, c);
    CHECK(p.move.vel.y > 0.0f);
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
    printf("nav graph: grid build matches the brute force, region A* is exact, routes are real and short, on every map\n");
    size_t maps = 0, surfaces = 0, links = 0, regionsN = 0;
    double worstBuild = 0, worstSearch = 0, sumSearch = 0, worstRatio = 1, sumRatio = 0;
    int searches = 0, routes = 0;
    const bool verbose = getenv("QUARK_NAV_VERBOSE") != nullptr;
    for (auto& name : listMaps()) {
        Map m;
        if (!m.load(name)) continue;
        NavGraph g;
        double t0 = wallSeconds();
        g.build(m);
        double bt = (wallSeconds() - t0) * 1000;
        worstBuild = std::max(worstBuild, bt);
        const size_t n = g.nodes.size(), R = g.regions.size();
        size_t bad = 0, nl = 0;
        maps++; surfaces += n; regionsN += R;
        for (auto& l : g.links) nl += l.size();
        links += nl;
        for (size_t i = 0; i < n; i++) {   // links: every pair, the old way (ladder links come after, same as build)
            std::vector<std::pair<int, float>> want;
            for (size_t j = 0; j < n; j++) {
                float c;
                if (g.linkCost(m, (int)i, (int)j, c)) want.push_back({(int)j, c});
            }
            bool same = g.links[i].size() >= want.size() && std::equal(want.begin(), want.end(), g.links[i].begin());
            bad += !same;
        }
        CHECK(bad == 0);
        uint32_t r = 12345;
        auto rf = [&] { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.0f; };
        int nodeMismatch = 0, pathMismatch = 0, regionMismatch = 0;
        for (int k = 0; k < 400 && n; k++) {   // nodeAt: grid vs every surface (the highest; of equal ones, the nearest)
            const auto& nd = g.nodes[(size_t)(rf() * n) % n];
            Vec3 p = {nd.x0 - 0.5f + rf() * (nd.x1 - nd.x0 + 1.0f), nd.y + rf() * 0.5f, nd.z0 - 0.5f + rf() * (nd.z1 - nd.z0 + 1.0f)};
            int best = -1; float by = -1e9f, bo = 1e9f;
            for (size_t i = 0; i < n; i++) {
                const auto& q = g.nodes[i];
                if (p.x < q.x0 - 0.3f || p.x > q.x1 + 0.3f || p.z < q.z0 - 0.3f || p.z > q.z1 + 0.3f || q.y > p.y + 0.6f || q.y < p.y - 6.0f) continue;
                float out = std::max({q.x0 - p.x, p.x - q.x1, 0.0f}) + std::max({q.z0 - p.z, p.z - q.z1, 0.0f});
                if (q.y > by + 1e-4f || (q.y > by - 1e-4f && out < bo)) { by = q.y; bo = out; best = (int)i; }
            }
            nodeMismatch += best != g.nodeAt(p);
        }
        auto dijkstra = [](const std::vector<std::vector<std::pair<int, float>>>& adj, int a) {
            std::vector<float> d(adj.size(), 1e30f);
            std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<>> q;
            d[a] = 0; q.push({0, a});
            while (!q.empty()) { auto [du, u] = q.top(); q.pop(); if (du > d[u]) continue; for (auto& [v, c] : adj[u]) if (du + c < d[v]) { d[v] = du + c; q.push({d[v], v}); } }
            return d;
        };
        std::vector<int> path;
        for (int k = 0; k < 40 && R; k++) {    // region A* route length == Dijkstra distance
            int a = (int)(rf() * R) % (int)R, b = (int)(rf() * R) % (int)R;
            auto d = dijkstra(g.rlinks, a);
            bool ok = g.findRegionPath(a, b, path);
            if (ok != (d[b] < 1e29f)) { regionMismatch++; continue; }
            if (!ok || a == b) continue;
            float len = 0; int prev = a;
            for (int v : path) { float c = 1e30f; for (auto& l : g.rlinks[prev]) if (l.first == v) c = std::min(c, l.second); len += c; prev = v; }
            regionMismatch += prev != b || std::fabs(len - d[b]) > 1e-3f * std::max(1.0f, d[b]);
        }
        // routes over the surfaces: found exactly when there is one, every step a link, ending at b; how much longer
        // than the shortest (the region plan is coarse) and how long each search takes
        double mapWorst = 0;
        for (int k = 0; k < 60 && n; k++) {
            int a = (int)(rf() * n) % (int)n, b = (int)(rf() * n) % (int)n;
            if (k % 2) {   // (half of them from and to where players stand: the big regions)
                for (int t = 0; t < 8 && g.regions[g.region[a]].x1 - g.regions[g.region[a]].x0 < 8; t++) a = (int)(rf() * n) % (int)n;
                for (int t = 0; t < 8 && g.regions[g.region[b]].x1 - g.regions[g.region[b]].x0 < 8; t++) b = (int)(rf() * n) % (int)n;
            }
            auto d = dijkstra(g.links, a);
            double t1 = timeNow();
            bool ok = g.findPath(a, b, path);
            double dt = (timeNow() - t1) * 1e6;
            searches++; sumSearch += dt; worstSearch = std::max(worstSearch, dt); mapWorst = std::max(mapWorst, dt);
            if (ok != (d[b] < 1e29f)) { pathMismatch++; continue; }
            if (!ok || a == b) continue;
            float len = 0; int prev = a; bool linked = true;
            for (int v : path) { float c = 1e30f; for (auto& l : g.links[prev]) if (l.first == v) c = std::min(c, l.second); linked = linked && c < 1e29f; len += c; prev = v; }
            if (!linked || prev != b) { pathMismatch++; continue; }
            double ratio = d[b] > 0.5f ? len / d[b] : 1.0;
            routes++; sumRatio += ratio; worstRatio = std::max(worstRatio, ratio);
        }
        if (verbose) printf("    %-10s %6zu surfaces %5zu regions %7zu links  build %6.1f ms  worst search %6.0f us\n", name.c_str(), n, R, nl, bt, mapWorst);
        if (nodeMismatch || pathMismatch || regionMismatch || bad) printf("    %s: %zu link lists, %d nodeAt, %d region routes, %d routes differ\n", name.c_str(), bad, nodeMismatch, regionMismatch, pathMismatch);
        CHECK(nodeMismatch == 0 && pathMismatch == 0 && regionMismatch == 0);
    }
    printf("  %zu maps, %zu surfaces in %zu regions, %zu links; slowest build %.1f ms (in the background on a server)\n", maps, surfaces, regionsN, links, worstBuild);
    printf("  %d routes: search avg %.0f us, worst %.0f us; length vs the shortest avg x%.3f, worst x%.2f\n", searches, searches ? sumSearch / searches : 0, worstSearch,
           routes ? sumRatio / routes : 1, worstRatio);
    CHECK(maps >= 20);
    CHECK(routes == 0 || sumRatio / routes < 1.25);   // (the region plan costs a little length, not a detour)
}

// Bots walk round what stands on a floor (owner, 2026-10-24): nav nodes are walkable tops, and before they were cut round
// the walls and crates standing on them, a bot and its goal on one floor with a wall between had an empty route, and
// the bot walked into the wall. Each case is one bot, alone (nothing to fight), put where a bot got stuck and sent where
// it was going; it must get there within the time a walk round takes, plus slack.
static void testBotNavBlockers(uint16_t port) {
    printf("bot navigation round walls and crates on a floor\n");
    struct Case { const char* what; const char* map; int mode; Vec3 from, to; double limit; };
    const Case cases[] = {
        // Depot (bomb): the attackers' gatehouse has a front wall (z 24.6-25, x -8.4..8.4); the bomb carrier stayed behind it
        {"depot: from the attackers' gate round its front wall to the west site", "depot", MODE_FFA, {-2, 0, 28.5}, {-18, 0, 0}, 16},
        // Depot: the defenders' yard, east corner to west corner past the perch (x -4..4, 2 m high); blocked at x = 4.4
        {"depot: across the defenders' yard round the perch", "depot", MODE_FFA, {16, 0, -31}, {-16, 0, -31}, 14},
        // Summit (KOTH): the west attackers' spawn behind a staircase and a pillar; stuck there ~20 s on the way to the hill
        {"summit: from the west spawn up to the hill", "summit", MODE_KOTH, {-8, 0, 23}, {0, 4.5f, 0}, 16},
    };
    for (const Case& c : cases) {
        Server s;
        ServerConfig cfg;
        cfg.autoStart = false; cfg.map = c.map; cfg.mode = c.mode; cfg.port = port++;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { printf("  %s: server didn't start\n", c.what); g_fail++; continue; }
        int bot = s.addBot(TEAM_A, BOT_ADVANCED);
        std::vector<NetClient*> none;
        for (int k = 0; k < 100 && (!s.nav_.ready(s.map_) || !s.players_[bot].alive); k++) pump(s, none, 0.05, idle);
        CHECK(bot >= 0 && s.nav_.ready(s.map_) && s.players_[bot].alive);
        if (bot < 0 || !s.players_[bot].alive) { s.stop(); continue; }
        ServerPlayer& p = s.players_[bot];
        BotBrain& b = s.bots_[bot];
        p.move.pos = c.from; p.move.vel = {};
        b.path.clear(); b.pathPrev = -1; b.needPath = true;
        const bool hill = c.mode == MODE_KOTH;
        auto there = [&] {
            if (hill) return playerBox(p.move).overlaps(s.map_.hill);
            Vec3 d = p.move.pos - c.to;
            return d.x * d.x + d.z * d.z < 2.0f * 2.0f && std::fabs(d.y) < 1.5f;
        };
        const double t0 = s.time();
        double took = -1;
        Vec3 maxStill = p.move.pos;
        // ...and never held up anywhere on the way: a bot walking into a wall stays within a metre or so of one spot
        // (jumping at it) until its unstick kicks in, which can still get it there in time
        Vec3 anchor = p.move.pos; double anchorAt = t0, held = 0; Vec3 heldAt = anchor;
        while (s.time() - t0 < c.limit && p.alive) {
            if (!hill) { b.goal = c.to; b.goalUntil = s.time() + 60; }   // (free for all: the goal a roaming bot would have)
            pump(s, none, 0.02, idle);
            if (there()) { took = s.time() - t0; break; }
            maxStill = p.move.pos;
            { float dx = p.move.pos.x - anchor.x, dz = p.move.pos.z - anchor.z;
              if (dx * dx + dz * dz > 1.5f * 1.5f) { anchor = p.move.pos; anchorAt = s.time(); }
              else if (s.time() - anchorAt > held) { held = s.time() - anchorAt; heldAt = anchor; } }
            if (getenv("QUARK_NAV_TRACE") && (int)((s.time() - t0) * 50) % 25 == 0) printf("    t %.1f pos %.2f %.2f %.2f path %zu\n", s.time() - t0, p.move.pos.x, p.move.pos.y, p.move.pos.z, b.path.size());
        }
        printf("  %s: %s (%.1f s; limit %.0f s)%s\n", c.what, took >= 0 ? "got there" : "STUCK", took >= 0 ? took : s.time() - t0, c.limit,
               took >= 0 ? "" : (" at " + std::to_string(maxStill.x).substr(0, 6) + " " + std::to_string(maxStill.y).substr(0, 5) + " " + std::to_string(maxStill.z).substr(0, 6)).c_str());
        if (held > 2.5) printf("    held up %.1f s at %.1f %.1f %.1f\n", held, heldAt.x, heldAt.y, heldAt.z);
        CHECK(took >= 0);
        CHECK(held <= 2.5);
        s.stop();
    }
}

// quark_tests navbench [map] [kind]: the bots' nav graph per map: build time, size, and route searches between random
// spots players stand on (avg / p99 / worst), for perf work on the graph (kind: bots, the default, or surfaces)
// (surfaces: the search as it was before the graph was cut up and split into regions, A* with a straight-line
// heuristic over every top: the "before" numbers)
static bool oldAStar(const NavGraph& G, int a, int b, std::vector<int>& out) {
    static std::vector<float> g; static std::vector<int> prev; static std::vector<uint32_t> stamp; static std::vector<std::pair<float, int>> heap; static uint32_t cur = 0;
    const auto& nodes = G.nodes;
    out.clear();
    if (a == b) return true;
    if (g.size() < nodes.size()) { g.resize(nodes.size()); prev.resize(nodes.size()); stamp.assign(nodes.size(), 0); cur = 0; }
    if (++cur == 0) { std::fill(stamp.begin(), stamp.end(), 0); cur = 1; }
    float hs = 1.0f;
    static const NavGraph* last = nullptr; static float lastH = 1;
    if (last != &G) {   // (the old graph's scale: over every link)
        for (size_t i = 0; i < nodes.size(); i++) for (auto& [j, c] : G.links[i]) {
            float dx = ((nodes[i].x0 + nodes[i].x1) - (nodes[j].x0 + nodes[j].x1)) * 0.5f, dz = ((nodes[i].z0 + nodes[i].z1) - (nodes[j].z0 + nodes[j].z1)) * 0.5f;
            float d = std::sqrt(dx * dx + dz * dz); if (d > 1e-3f) hs = std::min(hs, c / d); }
        last = &G; lastH = hs * 0.999f;
    }
    hs = lastH;
    auto cx = [&](int i) { return (nodes[i].x0 + nodes[i].x1) * 0.5f; };
    auto cz = [&](int i) { return (nodes[i].z0 + nodes[i].z1) * 0.5f; };
    const float bx = cx(b), bz = cz(b);
    auto h = [&](int i) { float dx = cx(i) - bx, dz = cz(i) - bz; return std::sqrt(dx * dx + dz * dz) * hs; };
    heap.clear();
    stamp[a] = cur; g[a] = 0; prev[a] = -1;
    heap.push_back({-h(a), a});
    auto cmp = [](const std::pair<float, int>& x, const std::pair<float, int>& y) { return x.first < y.first; };
    bool found = false;
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), cmp);
        auto [nf, u] = heap.back();
        heap.pop_back();
        if (-nf > g[u] + h(u) + 1e-4f) continue;
        if (u == b) { found = true; break; }
        for (auto& [v, c] : G.links[u]) {
            float ng = g[u] + c;
            if (stamp[v] != cur || ng < g[v]) { stamp[v] = cur; g[v] = ng; prev[v] = u; heap.push_back({-(ng + h(v)), v}); std::push_heap(heap.begin(), heap.end(), cmp); }
        }
    }
    if (!found) return false;
    for (int v = b; v != a; v = prev[v]) out.push_back(v);
    std::reverse(out.begin(), out.end());
    return true;
}

static void navBench(const char* only, bool surfaces) {
    for (auto& name : listMaps()) {
        if (only && name != only) continue;
        Map m;
        if (!m.load(name)) continue;
        NavGraph g;
        double best = 1e9;
        for (int k = 0; k < 3; k++) { double t0 = timeNow(); g.build(m, surfaces ? NavKind::Surfaces : NavKind::Bots); best = std::min(best, timeNow() - t0); }
        size_t nl = 0;
        for (auto& l : g.links) nl += l.size();
        // where spawns' routes go: random pieces of the regions spawns are in or reach (the playable part)
        std::vector<int> spots;
        std::vector<char> seen(g.nodes.size(), 0);
        for (auto& sp : m.spawns) { int a = g.nodeNear(sp.pos + Vec3(0, 0.1f, 0), NavGraph::kStandNear); if (a >= 0 && !seen[a]) { seen[a] = 1; spots.push_back(a); } }
        for (size_t i = 0; i < spots.size(); i++) for (auto& l : g.links[spots[i]]) if (!seen[l.first]) { seen[l.first] = 1; spots.push_back(l.first); }
        uint32_t r = 99;
        auto ri = [&](size_t n) { r = r * 1664525u + 1013904223u; return (int)((r >> 8) % n); };
        std::vector<double> t;
        std::vector<int> path;
        for (int k = 0; k < 4000 && !spots.empty(); k++) {
            int a = spots[ri(spots.size())], b = k % 8 == 7 ? ri(g.nodes.size()) : spots[ri(spots.size())];   // (1 in 8 to anywhere: tops nobody reaches)
            double t0 = timeNow();
            if (surfaces) oldAStar(g, a, b, path); else g.findPath(a, b, path);
            t.push_back((timeNow() - t0) * 1e6);
        }
        std::sort(t.begin(), t.end());
        double sum = 0;
        for (double v : t) sum += v;
        printf("  %-10s %6zu nodes %5zu regions %7zu links  build %6.1f ms  search avg %5.1f us  p99 %6.1f us  worst %6.1f us\n", name.c_str(), g.nodes.size(),
               g.regions.size(), nl, best * 1000, t.empty() ? 0 : sum / t.size(), t.empty() ? 0 : t[t.size() * 99 / 100], t.empty() ? 0 : t.back());
    }
}

static constexpr float kAimSample = 75.0f; // s of game time per tick rate (all four levels at once)
// Bot aim by level, at three server tick rates: damage per second with the rifle against a player strafing (and
// juking) 14 m away. The target is a scripted player that moves on game time, so it moves the same way at every
// tick rate however loaded the machine is. One bot of each level stands side by side (2 m apart, facing the
// target) and they all shoot at it, so every level gets the same movement; their places rotate between rates.
// Levels must be clearly ordered, play the same at every tick rate (their timing is all game time), and even
// the expert must stay beatable (well under perfect aim).
// X11: a bot hurt by someone it can't see or hear (a sniper far off) turns toward them once its reaction time is
// over, not before.
static void testBotHurtTurn(uint16_t port) {
    printf("bots turn toward whoever hurt them, after their reaction time\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port; cfg.balanceBots = false;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    int shooter = s.addBot(TEAM_A, BOT_BEGINNER), bot = s.addBot(TEAM_A, BOT_INTERMEDIATE);
    s.bots_[bot].onlyEnemy = shooter;
    const Vec3 at(7, 0, -27), from(-7, 0, -24);   // the shooter stands still, silent, behind the bot
    s.bots_[shooter].script = [&](Server& sv, int) {
        auto& bp = sv.players_[bot]; bp.move.pos = at; bp.move.vel = {}; bp.health = 100;
        auto& sp = sv.players_[shooter]; sp.move.pos = from; sp.move.vel = {};
        UserCmd c; c.weapon = kNoWeaponChange; return c;
    };
    auto run = [&](double sec) { double end = timeNow() + sec; while (timeNow() < end) { s.frame(); sleepMs(1); } };
    run(1.0);
    Vec3 d = from - at;
    const float toShooter = std::atan2(-d.x, -d.z);
    s.bots_[bot].aimYaw = toShooter + PI;   // (facing away)
    s.players_[bot].health = 100;
    s.damagePlayer(bot, shooter, 1, WPN_SNIPER, Vec3());
    const double hitAt = s.time();
    run(0.1);
    CHECK(s.bots_[bot].aware[shooter].hurtNoticed < hitAt);   // still reacting
    run(1.2);
    auto off = [&] { float a = std::fmod(s.bots_[bot].aimYaw - toShooter + 3 * PI, 2 * PI) - PI; return std::fabs(a); };
    printf("  hit at %.2f, noticed at %.2f; facing %.0f deg off the shooter 1.3 s later\n", hitAt, s.bots_[bot].aware[shooter].hurtNoticed, off() * 180 / PI);
    CHECK(s.bots_[bot].aware[shooter].hurtNoticed == hitAt && off() < 0.6f);
    s.stop();
}

static void testBotAim(uint16_t& port) {
    printf("bot aim: damage per second vs a strafing player, by level and server tick rate\n");
    int rates[] = {1000, 250, 125};
    if (getenv("AIM_RATE")) rates[0] = rates[1] = rates[2] = atoi(getenv("AIM_RATE")); // (noise check: three samples at one rate)
    float dps[3][BOT_SKILLS] = {}, acc[3][BOT_SKILLS] = {};
    SimClock clock;   // (bots only: every tick runs, none waited for)
    for (int r = 0; r < 3; r++) {
        qsrand(0x5EED0A00u + r);
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
        simNavReady(s);
        simRun(s, 1.0);
        float dealt0[BOT_SKILLS]; int fired0[BOT_SKILLS], hit0[BOT_SKILLS];
        for (int k = 0; k < BOT_SKILLS; k++) { auto& bp = s.players_[bot[k]]; dealt0[k] = bp.dmgDealt; fired0[k] = bp.shotsFired; hit0[k] = bp.shotsHit; }
        double t0 = s.time();
        simRun(s, kAimSample);
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
    CHECK(mean[BOT_SKILLS - 1] > perfect * 0.35f); // ...and strong: about 18 dps on a strafer (17 at 1.17, about 45%)
}

// Alt-fires: thrown knife hits at range, shotgun ricochets hit someone behind you (off the wall in front),
// scoped rifle fires at half rate.
// Every ladder on every map gets you to the top: stand at its foot, hold forward (into the wall) and you
// end up standing on whatever it leads to.
int outOfBoundsTests(int part = 0, int parts = 1);   // tests/mapbounds.cpp (returns the failures)
int brainTests(uint16_t port);     // tests/brain_tests.cpp (returns the failure count)
int brainTool(const std::string& cmd, const char* arg); // tests/brain_tests.cpp: exportbrain / checkbrain / brainast / printbrain
int brainBench(uint16_t port);   // tests/brain_tests.cpp: native vs interpreted bot think cost
int modeTests();                 // tests/mode_tests.cpp: the mode logic language (returns the failure count)
int modeTool(const std::string& cmd, int argc, char** argv);   // tests/mode_tests.cpp: exportmode / checkmode / modeast / printmode
int modeBench();                 // tests/mode_tests.cpp: interpreted mode slot cost
int modeLockstepTests(int part = 0, int parts = 1);   // tests/mode_tests.cpp: logic on servers, native vs interpreted lockstep (the failure count)
int exportAssets(const char* dir);   // tests/assets_tests.cpp: every default asset as files + manifest.json
int assetTests();                    // tests/assets_tests.cpp (returns the failures)

static void testLadders() {
    printf("ladders: every one climbable\n");
    for (auto& name : testedMaps()) {
        Map m;
        if (!m.load(name)) continue;
        HumanMadeGrace hg(name, m);
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
                    cmd.forward = t < 700 ? 127 : -127;            // a ladder that ends at a wall: step back off it...
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
        for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE; i++) { skipCountdowns(s); pump(s, cls, 0.05, idle); }
        pump(s, cls, 0.2, idle);
        for (int round = 0; round < 2; round++) {   // (each wait: until it happened, at most what it used to be)
            int was = b.clientId();
            b.disconnect();
            for (int i = 0; i < 50 && !s.away(was); i++) pump(s, cls, 0.01, idle);
            CHECK(s.humanCount() == 3 && s.away(was));   // the slot waits for a reconnect (the grace)
            b.connect("127.0.0.1:" + std::to_string(cfg.port), "bob");
            auto back = [&] { return b.state() == ConnState::Connected && s.humanCount() == 3 && b.clientId() == was && !s.away(was); };
            for (int i = 0; i < 150 && !back(); i++) pump(s, cls, 0.01, idle);
            pump(s, cls, 0.1, idle);   // (and stays)
            bool ok = back();
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

// STOP HOSTING: the hosted server's thread ends after its bots searched routes, and the game goes on. The route
// search's scratch is the graph's: as thread_locals with destructors (MinGW's emutls frees their storage before the
// destructors run) the vectors freed garbage pointers at thread exit and the heap corruption closed the game on Windows.
static void testHostedBotsThreadExit(uint16_t port) {
    printf("hosted server with bots: its thread ends cleanly (stop hosting)\n");
    for (int round = 0; round < 2; round++) {
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true; cfg.map = "clockwork"; cfg.mode = MODE_FFA; cfg.port = port;
        cfg.vsBots = true; cfg.botsEnemy = 3; cfg.botSkill = BOT_EXPERT;
        s.setLog([](const std::string&) {});
        { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
        // Hosted until the bots have searched routes on that thread (read after each join: no race), in short spells
        // of 100 ms, each a thread started and stopped as Game::startHost / stopHost do, 2.5 s at most
        for (int spell = 0; spell < 25 && s.nav_.scratch_.cur == 0; spell++) {
            std::atomic<bool> quit{false};
            std::thread host([&] { s.run(quit); });   // as Game::startHost
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            quit = true; host.join();                 // as Game::stopHost
        }
        s.stop();
        CHECK(s.nav_.scratch_.cur > 0);           // (the bots did search routes on that thread)
    }
}

// Constants written out in more than one language: the queue list (community.cpp, the master Worker, the
// Discord bot) and the UDP info query the bot speaks (quarkdata.py). Parsed from the sources; any mismatch fails.
// The bot lives on the discord-bot branch: its half runs only with QUARK_BOT_DIR pointing at a checkout of it.
static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static std::string readSource(const char* rel) {
    if (sourceDir().empty()) return "";
    return readFile(sourceDir() + "/" + rel);
}
static std::string readBotSource(const char* rel) {
    const char* dir = getenv("QUARK_BOT_DIR");
    return dir && *dir ? readFile(std::string(dir) + "/" + rel) : "";
}
static std::string blockAfter(const std::string& s, const std::string& start, const std::string& end) {
    size_t a = s.find(start); if (a == std::string::npos) return "";
    size_t b = s.find(end, a + start.size()); return s.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
static void testCrossLanguageConstants(uint16_t port) {
    printf("cross-language constants\n");
    std::string cpp = readSource("src/client/community.cpp"), js = readSource("tools/master/worker.js"),
                bot = readBotSource("bot.py"), qd = readBotSource("quarkdata.py");
    if (cpp.empty() || js.empty()) { printf("  (sources not found: skipped)\n"); return; }
    bool withBot = !bot.empty() && !qd.empty();
    if (!withBot) printf("  (the bot's half skipped: set QUARK_BOT_DIR to a checkout of the discord-bot branch)\n");
    // queues: key -> (title, size)
    std::map<std::string, std::pair<std::string, int>> qc, qb; std::map<std::string, int> qj;
    std::smatch m;
    std::string b = blockAfter(cpp, "Game::kQueues[] = {", "};");
    static const std::regex rx83946("\\{\"([\\w-]+)\", \"([^\"]*)\", (\\d+)\\}"); for (std::sregex_iterator it(b.begin(), b.end(), rx83946), e; it != e; ++it)
        qc[(*it)[1]] = {(*it)[2], std::stoi((*it)[3])};
    b = blockAfter(js, "const QUEUE_SIZES = {", "};");
    static const std::regex rx7847("\"?([\\w-]+)\"?: (\\d+)"); for (std::sregex_iterator it(b.begin(), b.end(), rx7847), e; it != e; ++it) qj[(*it)[1]] = std::stoi((*it)[2]);
    CHECK(qc.size() >= 10);
    CHECK(qj.size() == qc.size());
    for (auto& [k, v] : qc)
        if (!qj.count(k) || qj[k] != v.second) { printf("  queue %s differs between community.cpp and worker.js\n", k.c_str()); g_fail++; }
    if (!withBot) return;
    b = blockAfter(bot, "QUEUES: dict", "\n}");
    static const std::regex rx61002("\"([\\w-]+)\": \\(\"([^\"]*)\", (\\d+)\\)"); for (std::sregex_iterator it(b.begin(), b.end(), rx61002), e; it != e; ++it)
        qb[(*it)[1]] = {(*it)[2], std::stoi((*it)[3])};
    CHECK(qb.size() == qc.size());
    for (auto& [k, v] : qc)
        if (!qb.count(k) || qb[k] != v) { printf("  queue %s differs between community.cpp and bot.py\n", k.c_str()); g_fail++; }
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
    // (the freeze, the 5 s time limit and the match end's wait are skipped: each still ends on its own clock)
    for (int i = 0; i < 400 && s.config().map == "bastion"; i++) {
        if (s.ms_.phase != PHASE_WARMUP) s.phaseEnd_ = std::min(s.phaseEnd_, s.time());
        pump(s, cls, 0.05, idle); collect();
    }
    CHECK(s.config().map == "harbor");   // the pool rotated
    s.stop();
    setActiveRules(CustomRules());
    CHECK(!activeRules().active() && std::fabs(weaponDef(WPN_RIFLE).damage - baseWeaponDef(WPN_RIFLE).damage) < 1e-4f);
    testHostedRulesPerThread((uint16_t)(port + 100));
}

// Every staircase (recorded by tools/make_maps.py as "stairs x0 z0 x1 z1 base top axis rising solid", Map::stairs):
// walked from the bottom to the top by the real movement code, and checked against the paths around it:
// no walkable surface may pass through a solid flight, or run underneath an open one with less than a
// player's height of clearance.
// Wedges and corners (map format 5): read and written, collision, rays, walking up a gentle one, sliding off a steep
// one, and bots' floor on the gentle one.
static void testSlopes() {
    printf("slopes: wedges, corners and tetras\n");
    const std::string base = "box -20 -1 -20 20 0 20\nspawn any -10 0 -10\n";
    {   // version 4 converts (nothing to change); a version 5 file round-trips; an older build would skip the lines
        std::string o;
        CHECK(upgradeMapText("version 4\n" + base, &o) && mapTextVersion(o) == kMapFormatVersion);
        Map m;
        CHECK(m.parse("version 5\nwedge 3 1 0 0 0 4 2 2\ncorner 0 2 5 0 5 9 3 9\n" + base) && m.warnings.empty());
        CHECK(m.brushes.size() == 3 && m.brushes[0].shape == BRUSH_WEDGE && m.brushes[0].dir == 1 && m.brushes[0].material == 3 && m.brushes[1].shape == BRUSH_CORNER);
        const std::string t = m.serialize();
        CHECK(t.find("wedge 3 1 0 0 0 4 2 2\n") != std::string::npos && t.find("corner 0 2 5 0 5 9 3 9\n") != std::string::npos);
        Map b;
        CHECK(b.parse(t) && b.serialize() == t);
        Map bad;
        CHECK(!bad.parse("wedge 0 24 0 0 0 1 1 1\n" + base) && !bad.parse("corner " + std::to_string(kMapMaterials) + " 0 0 0 0 1 1 1\n" + base));
    }
    Map m;
    // gentle: 4 m run, 2 m rise toward +x (27 degrees); steep: 1 m run, 4 m rise toward -z (76); a corner, high at +x+z
    CHECK(m.parse("wedge 0 0 2 0 -3 6 2 3\nwedge 0 3 -12 0 -9 -6 4 -8\ncorner 0 2 8 0 8 12 2 12\n" + base));
    {   // the solid: under the slope only
        const Brush& w = m.brushes[0];
        CHECK(std::fabs(brushTopAt(w, 4, 0) - 1.0f) < 1e-5f && brushContains(w, {4, 0.9f, 0}) && !brushContains(w, {4, 1.1f, 0}));
        const Brush& c = m.brushes[2];
        CHECK(std::fabs(brushTopAt(c, 11, 9) - 0.5f) < 1e-5f && std::fabs(brushTopAt(c, 12, 12) - 2.0f) < 1e-5f);
        CHECK(!m.boxSolid(playerBox({2.5f, 0.6f, 0}, false)) && m.boxSolid(playerBox({5, 0.6f, 0}, false)));
        float t = m.raycast({4, 5, 0}, {0, -1, 0}, 10);   // straight down onto the wedge at x 4: its top, 1 m up
        CHECK(std::fabs(t - 4.0f) < 1e-3f);
        Vec3 n;
        t = m.raycast({0, 1, 0}, {1, 0, 0}, 10, &n);      // level at 1 m: into the slope where it reaches 1 m (x 4)
        CHECK(std::fabs(t - 4.0f) < 1e-3f && n.y > 0.8f && n.x < -0.4f);
        CHECK(!m.raycastAny({0, 2.5f, 0}, {1, 0, 0}, 10) && m.raycastAny({11, 5, 9}, {0, -1, 0}, 10));
    }
    auto run = [&](MoveState& ms, Vec3 to, int ticks) {
        for (int t = 0; t < ticks; t++) {
            Vec3 d = to - ms.pos;
            UserCmd cmd; cmd.yaw = std::atan2(-d.x, -d.z); cmd.forward = 127; cmd.msec = 8;
            pmove(ms, cmd, m);
        }
    };
    {   // a low cut box's upright side at an angle in plan is stepped up like a box's (owner, 2026-10-31: "sometimes you
        // cant stepup on cut boxes even though the height distance is still below 0.55"); a low steep slope still isn't
        auto stepOnto = [&](const std::string& piece, Vec3 from, Vec3 to) {
            Map t;
            CHECK(t.parse(piece + base));
            MoveState ms; ms.pos = from;
            for (int k = 0; k < 20; k++) { UserCmd c; c.msec = 8; pmove(ms, c, t); }   // (settle on the floor)
            for (int k = 0; k < 250; k++) {
                Vec3 d = to - ms.pos;
                UserCmd c; c.yaw = std::atan2(-d.x, -d.z); c.forward = 127; c.msec = 8;
                pmove(ms, c, t);
            }
            return ms.pos.y;
        };
        // x 2-6, z -2..2, 0.4 m high, less the corner past fx + fz = 1 (an upright face facing +x+z), walked into from +x+z
        const float up = stepOnto("cut 0 1 1 0 1 1 2 0 -2 6 0.4 2\n", {8, 0, 4}, {3, 0, -1});
        const float steep = stepOnto("wedge 0 0 6 0 -2 6 0.4 2\n", {0, 0, 0}, {5, 0, 0});   // (a 0.4 m rise in 0.2 m: 63 degrees)
        printf("  0.4 m cut box, upright side at 45 degrees in plan: at %.2f m; 0.4 m 63 degree slope: at %.2f m\n", up, steep);
        CHECK(std::fabs(up - 0.4f) < 0.02f && steep < 0.05f);
    }
    {   // tetra (owner, 2026-10-28: "1 0 0 0 / 1 1 1 0"): three base corners, the apex over one, one sloped face
        Map t;
        CHECK(t.parse("tetra 0 0 0 0 0 4 2 4\ntetra 0 2 10 0 10 12 6 12\n" + base) && t.warnings.empty());
        const Brush& a = t.brushes[0];
        CHECK(a.shape == BRUSH_TETRA && t.serialize().find("tetra 0 0 0 0 0 4 2 4\n") != std::string::npos);
        Map r;
        CHECK(r.parse(t.serialize()) && r.serialize() == t.serialize());
        CHECK(std::fabs(brushTopAt(a, 1, 1) - 1.0f) < 1e-5f && brushTopAt(a, 3, 3) == 0.0f && std::fabs(brushTopAt(a, 0, 0) - 2.0f) < 1e-5f);
        CHECK(brushContains(a, {1, 0.9f, 1}) && !brushContains(a, {1, 1.1f, 1}) && !brushContains(a, {3, 0.1f, 3}));
        CHECK(!t.boxSolid(playerBox({3.3f, 0.05f, 3.3f}, false)) && t.boxSolid(playerBox({1, 0.05f, 1}, false)));
        CHECK(std::fabs(t.raycast({1, 5, 1}, {0, -1, 0}, 10) - 4.0f) < 1e-3f);
        Vec3 n;
        const float h = t.raycast({5, 0.5f, 1}, {-1, 0, 0}, 10, &n);   // level, toward -x: the face reaches 0.5 m at x 2
        CHECK(std::fabs(h - 3.0f) < 1e-3f && n.x > 0.3f && n.y > 0.3f && n.z > 0.3f);
        int faces = 0;
        slopePolygons(a, [&](const Vec3*, int np, const Vec3&) { faces++; CHECK(np == 3); });
        CHECK(faces == 4);
        Brush m2 = a;
        CHECK(slopeTurned(a, m2, [](const Vec3& p) { return Vec3(4 - p.x, p.y, p.z); }) && m2.dir == 1);
        // walking into the steep one's open (diagonal) side: stopped by the face, not by its box (x 9.6 would be)
        MoveState ms; ms.pos = {7, 0.02f, 10.5f};
        float touch = -1;   // (where it first turns you aside: the diagonal face slides you along it)
        for (int k = 0; k < 250 && touch < 0; k++) {
            UserCmd cmd; cmd.yaw = -PI * 0.5f; cmd.forward = 127; cmd.msec = 8;
            pmove(ms, cmd, t);
            if (std::fabs(ms.pos.z - 10.5f) > 0.02f) touch = ms.pos.x;
        }
        if (getenv("SLOPE_TRACE")) printf("    tetra wall: touched at x %.2f (%.2f %.2f %.2f)\n", touch, ms.pos.x, ms.pos.y, ms.pos.z);
        CHECK(touch > 10.2f && touch < 11.1f && ms.pos.y < 0.6f);
        NavGraph g;
        g.build(t, NavKind::Bots);
        bool onIt = false, past = false;
        for (auto& nd : g.nodes) {
            if (nd.x1 <= 4 + 1e-3f && nd.z1 <= 4 + 1e-3f && nd.x0 >= -1e-3f && nd.z0 >= -1e-3f && nd.y > 0.5f) onIt = true;
            if (nd.x0 >= 2.5f && nd.z0 >= 2.5f && nd.x1 <= 4 + 1e-3f && nd.z1 <= 4 + 1e-3f && nd.y > 1e-3f) past = true;
        }
        CHECK(onIt && !past);
    }
    {   // walking up the gentle one, onto its top, on the floor all the way up
        MoveState ms; ms.pos = {-1, 0.02f, 0};
        int air = 0;
        for (int t = 0; t < 200; t++) {
            Vec3 d = Vec3(7, 2, 0) - ms.pos;
            UserCmd cmd; cmd.yaw = std::atan2(-d.x, -d.z); cmd.forward = 127; cmd.msec = 8;
            pmove(ms, cmd, m);
            if (ms.pos.x > 2.5f && ms.pos.x < 5.5f && !(ms.flags & MF_ONGROUND)) air++;
            if (ms.pos.x > 5.0f) break;
        }
        if (getenv("SLOPE_TRACE")) printf("    up: %.2f %.2f %.2f ground %d air %d\n", ms.pos.x, ms.pos.y, ms.pos.z, (int)(ms.flags & MF_ONGROUND), air);
        CHECK(ms.pos.x > 5.0f && ms.pos.y > 1.5f && (ms.flags & MF_ONGROUND) && air < 5);
        run(ms, {-3, 0, 0}, 200);   // and back down it
        if (getenv("SLOPE_TRACE")) printf("    down: %.2f %.2f %.2f ground %d\n", ms.pos.x, ms.pos.y, ms.pos.z, (int)(ms.flags & MF_ONGROUND));
        CHECK(ms.pos.x < 0 && std::fabs(ms.pos.y) < 0.05f && (ms.flags & MF_ONGROUND));
    }
    {   // the steep one is a wall (walking up its slope, from +z)
        MoveState ms; ms.pos = {-10, 0.02f, -4};
        run(ms, {-10, 4, -12}, 250);
        if (getenv("SLOPE_TRACE")) printf("    wall: %.2f %.2f %.2f\n", ms.pos.x, ms.pos.y, ms.pos.z);
        CHECK(ms.pos.y < 0.6f && ms.pos.z > -8.5f);
    }
    {   // dropped onto the steep one: it slides you off downhill (+z: it rises toward -z), onto the floor
        MoveState ms; ms.pos = {-10, 6, -8.6f};
        for (int t = 0; t < 400; t++) { UserCmd idle; idle.msec = 8; pmove(ms, idle, m); }
        if (getenv("SLOPE_TRACE")) printf("    slide: %.2f %.2f %.2f ground %d\n", ms.pos.x, ms.pos.y, ms.pos.z, (int)(ms.flags & MF_ONGROUND));
        CHECK(ms.pos.z > -8.0f && std::fabs(ms.pos.y) < 0.05f && (ms.flags & MF_ONGROUND));
    }
    {   // bots: the gentle one is floor to the top, the steep one isn't
        NavGraph g;
        g.build(m, NavKind::Bots);
        float hiGentle = -1, hiSteep = -1;
        for (auto& n : g.nodes) {
            if (n.x0 >= 2 - 1e-3f && n.x1 <= 6 + 1e-3f && n.z0 >= -3 - 1e-3f && n.z1 <= 3 + 1e-3f) hiGentle = std::max(hiGentle, n.y);
            if (n.z0 >= -9 - 1e-3f && n.z1 <= -8 + 1e-3f && n.x0 >= -12 - 1e-3f && n.x1 <= -6 + 1e-3f) hiSteep = std::max(hiSteep, n.y);
        }
        CHECK(hiGentle > 1.7f && hiSteep < 0);
    }
    {   // cut box (owner, 2026-10-29: "cut box (any angle)"): a box less what lies past up to 6 planes over its fractions
        Map t;
        // a ramp (the plane fy <= fx: up 2 m over 4 m along x) and, at x 10..14, a ramp with its +x +z corner cut off
        // upright along the diagonal (fx + fz <= 1.5)
        CHECK(t.parse("cut 0 1 -1 1 0 0 0 0 0 4 2 4\ncut 3 2 -1 1 0 0 1 0 1 1.5 10 0 0 14 2 4\n" + base) && t.warnings.empty());
        const Brush &a = t.brushes[0], &c2 = t.brushes[1];
        CHECK(a.shape == BRUSH_CUT && a.ncut == 1 && c2.ncut == 2 && c2.material == 3);
        CHECK(t.serialize().find("cut 0 1 -1 1 0 0 0 0 0 4 2 4\n") != std::string::npos);
        Map r;
        CHECK(r.parse(t.serialize()) && r.serialize() == t.serialize());
        CHECK(std::fabs(brushTopAt(a, 2, 1) - 1.0f) < 1e-5f && std::fabs(brushTopAt(a, 4, 1) - 2.0f) < 1e-5f && brushTopAt(a, 0, 1) == 0.0f);
        CHECK(brushContains(a, {2, 0.9f, 1}) && !brushContains(a, {2, 1.1f, 1}));
        CHECK(!brushContains(c2, {13.8f, 0.1f, 3.8f}) && brushContains(c2, {13.8f, 1.8f, 0.2f}));   // (the cut-off corner; under the ramp's top)
        CHECK(std::fabs(t.raycast({2, 5, 1}, {0, -1, 0}, 10) - 4.0f) < 1e-3f);
        Vec3 n;
        CHECK(std::fabs(t.raycast({2, 5, 1}, {0, -1, 0}, 10, &n) - 4.0f) < 1e-3f && n.y > 0.85f && n.x < -0.4f);
        CHECK(!t.boxSolid(playerBox({13.6f, 1.0f, 3.6f}, false)) && t.boxSolid(playerBox({13.6f, 1.0f, 0.5f}, false)));
        // the faces: corners on the box, normals out; the ramp has 5 (no side at its low edge, none on top), the other 6
        for (const Brush* b : {&a, &c2}) {
            int faces = 0;
            slopePolygons(*b, [&](const Vec3* p, int np, const Vec3& nn) {
                Vec3 c;
                for (int k = 0; k < np; k++) c += p[k] * (1.0f / np);
                CHECK(!brushContains(*b, c + nn * 0.01f) && brushContains(*b, c - nn * 0.01f));
                faces++;
            });
            CHECK(faces == (b == &a ? 5 : 6));
        }
        // standing on it: dropped onto the ramp at x 2, you land on the highest of it under your feet (x 2.4: 1.2 m)
        MoveState ms; ms.pos = {2, 3, 2};
        for (int k = 0; k < 200; k++) { UserCmd idle; idle.msec = 8; pmove(ms, idle, t); }
        CHECK(ms.pos.y > 1.1f && ms.pos.y < 1.3f);
        // walking into the cut-off corner's diagonal wall: stopped by the face, not by the box (z 4 would be)
        MoveState mw; mw.pos = {13.5f, 0.02f, 7};
        for (int k = 0; k < 30; k++) { UserCmd idle; idle.msec = 8; pmove(mw, idle, t); }
        for (int k = 0; k < 200; k++) { UserCmd cmd; cmd.yaw = 0; cmd.forward = 127; cmd.msec = 8; pmove(mw, cmd, t); }
        CHECK(mw.pos.z < 4.0f - 0.05f || mw.pos.x < 13.0f);   // (slid along it, or got past its edge into the corner's space)
        // turned and mirrored (the editor's keys): the same solid moved
        auto mirX = [](const Vec3& p) { return Vec3(24 - p.x, p.y, p.z); };
        auto rotY = [](const Vec3& p) { return Vec3(p.z, p.y, -p.x); };
        Brush m1 = c2; m1.box = {{10, 0, 0}, {14, 2, 4}}; m1.box = {vmin(mirX(c2.box.mn), mirX(c2.box.mx)), vmax(mirX(c2.box.mn), mirX(c2.box.mx))};
        cutTurned(c2, m1, mirX);
        Brush m2 = c2; m2.box = {vmin(rotY(c2.box.mn), rotY(c2.box.mx)), vmax(rotY(c2.box.mn), rotY(c2.box.mx))};
        cutTurned(c2, m2, rotY);
        int same = 0;
        for (int i = 0; i < 200; i++) {
            const Vec3 p = {10.03f + 0.0197f * i, 0.013f + 0.0099f * ((i * 13) % 200), 0.021f + 0.0198f * ((i * 7) % 200)};
            same += brushContains(c2, p) == brushContains(m1, mirX(p)) && brushContains(c2, p) == brushContains(m2, rotY(p));
        }
        CHECK(same == 200);
        // a group's part (fractions of the group's box; it scales with it) and the edit line
        Map g;
        CHECK(g.parse("group custom 0 0 20 0 0 24 2 4\npart 0 0 0 0 1 1 1 cut 1 -1 1 0 0\n" + base));
        bool found = false;
        for (const Brush& b : g.brushes) if (b.generated && b.shape == BRUSH_CUT) found = std::fabs(brushTopAt(b, 22, 1) - 1.0f) < 1e-4f;
        CHECK(found);
        Map g2;
        CHECK(g2.parse(g.serialize()) && g2.serialize() == g.serialize());
        CHECK(describeMapObject(t, 'b', "set", 1).find(" x 2 -1 1 0 0 1 0 1 1.5") != std::string::npos);
        Map e = t;
        CHECK(applyMapEdit(e, describeMapObject(t, 'b', "set", 1)) && e.serialize() == t.serialize());
        NavGraph ng;
        ng.build(t, NavKind::Bots);
        bool onRamp = false;
        for (auto& nd : ng.nodes) onRamp |= nd.x0 >= -1e-3f && nd.x1 <= 4 + 1e-3f && nd.y > 0.5f;
        CHECK(onRamp);
    }
    {   // upside down and on its side (up, map.h): the solid, the file, turning one (the editor's rotate / mirror)
        Map o;
        CHECK(o.parse("wedge 0 4 0 0 0 4 2 2\nwedge 0 8 10 0 0 14 2 2\ncorner 1 22 20 0 0 24 2 2\n" + base));
        const Brush &f = o.brushes[0], &sd = o.brushes[1];
        CHECK(f.up == 1 && f.dir == 0 && sd.up == 2 && sd.dir == 0 && o.brushes[2].up == 5 && o.brushes[2].dir == 2);
        CHECK(brushContains(f, {3.5f, 1.9f, 1}) && !brushContains(f, {0.5f, 0.5f, 1}) && brushContains(f, {0.5f, 1.95f, 1}));
        CHECK(brushContains(sd, {11, 1.5f, 1}) && !brushContains(sd, {13, 0.5f, 1}));
        Map r;
        CHECK(r.parse(o.serialize()) && r.serialize() == o.serialize() && o.serialize().find("corner 1 22 20 0 0 24 2 2\n") != std::string::npos);
        // a ray up into the hanging one meets its sloped underside: at x 2 it hangs down to y 1
        Vec3 n;
        float t = o.raycast({2, 0.01f, 1}, {0, 1, 0}, 5, &n);
        CHECK(std::fabs(t - 0.99f) < 1e-3f && n.y < -0.5f);
        // mirrored upside down, a floor wedge becomes the hanging one; turned about x and z, a solid of the same shape
        Brush w; w.shape = BRUSH_WEDGE; w.box = {{0, 0, 0}, {4, 2, 2}};
        Brush m2 = w;
        CHECK(slopeTurned(w, m2, [](const Vec3& p) { return Vec3(p.x, 2 - p.y, p.z); }) && m2.up == 1 && m2.dir == 0);
        auto rotX = [](const Vec3& p) { return Vec3(p.x, -p.z, p.y); };
        Brush m3 = w; m3.box = {{0, -2, 0}, {4, 0, 2}};
        CHECK(slopeTurned(w, m3, rotX));
        for (int i = 0; i < 50; i++) {
            const Vec3 p = {0.05f + 0.077f * i, 0.03f + 0.039f * (i % 50), 0.09f + 0.037f * ((i * 7) % 50)};
            CHECK(brushContains(w, p) == brushContains(m3, rotX(p)));
        }
        // the same solid by its planes and its polygons: every face's corners on the box, its normal pointing out
        int faces = 0;
        for (const Brush& b : o.brushes) {
            if (b.shape == BRUSH_BOX) continue;
            slopePolygons(b, [&](const Vec3* p, int np, const Vec3& nn) {
                Vec3 c;
                for (int k = 0; k < np; k++) c += p[k] * (1.0f / np);
                CHECK(!brushContains(b, c + nn * 0.01f) && brushContains(b, c - nn * 0.01f));
                faces++;
            });
        }
        CHECK(faces >= 15);
    }
    {   // a dash into the steep one goes up it (owner, 2026-10-28), where running at it doesn't
        MoveState ms; ms.pos = {-9, 0.02f, -6};
        for (int t = 0; t < 30; t++) { UserCmd idle; idle.msec = 8; pmove(ms, idle, m); }
        float top = 0;
        for (int t = 0; t < 60; t++) {
            UserCmd cmd; cmd.yaw = 0; cmd.forward = 127; cmd.msec = 8; cmd.buttons = t < 3 ? BTN_DASH : 0;   // (yaw 0: facing -z)
            pmove(ms, cmd, m);
            top = std::max(top, ms.pos.y);
        }
        if (getenv("SLOPE_TRACE")) printf("    dash: %.2f %.2f %.2f top %.2f\n", ms.pos.x, ms.pos.y, ms.pos.z, top);
        CHECK(top > 1.5f);
    }
    {   // a wall on its side (up 2, rising toward +x along z: a diagonal wall) slides you along it, not stops you
        Map d;
        CHECK(d.parse("wedge 0 10 0 0 -4 4 4 4\n" + base));   // (dir 2 + 4 * 2: x thickness 0 at z -4 up to 4 at z 4)
        MoveState ms; ms.pos = {3.5f, 0.02f, -6};
        for (int t = 0; t < 150; t++) { UserCmd cmd; cmd.yaw = PI; cmd.forward = 127; cmd.msec = 8; pmove(ms, cmd, d); }   // (yaw pi: +z)
        if (getenv("SLOPE_TRACE")) printf("    side wall: %.2f %.2f %.2f\n", ms.pos.x, ms.pos.y, ms.pos.z);
        CHECK(ms.pos.z > 0 && ms.pos.x > 4.0f && !d.boxSolid(playerBox(ms.pos, false)));
    }
}

// Surf (owner, 2026-10-28): Cascade's ramps are ridden, not walked (the map checks don't ask bots to): a surfer
// dropped onto a ramp's face going along it, strafing into it, rides it to its end without falling off, and the run-only
// movement leaves no dash and no grapple.
// Slopes in custom groups (map format 5): parts read and written with their shape, edited in, turned with the group, and
// seams: a wedge part's side triangle flush with a box part's face joins it (no outline across), the slope's own
// crease keeps its outline, and the same pieces outside a group keep every outline (a hand-made map).
static void testGroupSlopes() {
    printf("group slopes: parts with shapes, seams\n");
    const std::string base = "handmade\nbox -20 -1 -20 20 0 20\nspawn any -10 0 -10\n";
    const std::string grp = "group custom 0 0 0 0 0 4 2 2\npart 0 0 0 0 0.5 1 1\npart 3 0.5 0 0 1 1 1 wedge 1\n";
    Map m;
    std::string err;
    CHECK(m.parse(grp + base, &err) && m.groups.size() == 1 && m.groups[0].parts.size() == 2);
    CHECK(m.groups[0].parts[1].shape == BRUSH_WEDGE && m.groups[0].parts[1].dir == 1 && m.groups[0].parts[1].up == 0);
    CHECK(m.serialize().find("part 3 0.5 0 0 1 1 1 wedge 1\n") != std::string::npos);
    {   // the world brush: a wedge, in the group, collides (its slope 1 m up at x 3)
        int wedges = 0;
        for (auto& b : m.brushes) wedges += b.generated && b.group == 0 && b.shape == BRUSH_WEDGE && b.dir == 1;
        CHECK(wedges == 1);
        const float t = m.raycast({3, 5, 1}, {0, -1, 0}, 10);
        CHECK(std::fabs(t - 4) < 0.02f);
    }
    {   // bad shape words fail; an edit adds a corner part (upside down)
        Map bad;
        CHECK(!bad.parse("group custom 0 0 0 0 0 4 2 2\npart 0 0 0 0 1 1 1 ramp 1\n" + base) && !bad.parse("group custom 0 0 0 0 0 4 2 2\npart 0 0 0 0 1 1 1 wedge 24\n" + base));
        Map e;
        CHECK(e.parse(grp + base));
        CHECK(applyMapEdit(e, "g part 0 0 0 0 0 0.5 0.5 0.5 c 6") && e.groups[0].parts.size() == 3 && e.groups[0].parts[2].shape == BRUSH_CORNER && e.groups[0].parts[2].dir == 2 && e.groups[0].parts[2].up == 1);
        CHECK(!applyMapEdit(e, "g part 0 0 0 0 0 0.5 0.5 0.5 x 1"));
        const std::string t = e.serialize();
        CHECK(t.find("corner 6\n") != std::string::npos);
        Map back;
        CHECK(back.parse(t) && back.groups[0].parts.size() == 3 && back.groups[0].parts[2].shape == BRUSH_CORNER && back.groups[0].parts[2].up == 1);
    }
    // the outline's distance at p on the face facing n (the mesh's edge interpolated; 1000 where an edge is off)
    auto outlineAt = [](const BakedWorld& w, Vec3 p, Vec3 n) {
        for (size_t i = 0; i + 2 < w.verts.size(); i += 3) {
            const WorldVertex* v[3] = {&w.verts[i], &w.verts[i + 1], &w.verts[i + 2]};
            Vec3 a(v[0]->pos[0], v[0]->pos[1], v[0]->pos[2]), b(v[1]->pos[0], v[1]->pos[1], v[1]->pos[2]), c(v[2]->pos[0], v[2]->pos[1], v[2]->pos[2]);
            Vec3 nn = cross(b - a, c - a);
            if (length(nn) < 1e-8f) continue;
            nn = normalize(nn);
            if (dot(nn, n) < 0.99f || std::fabs(dot(p - a, nn)) > 1e-3f) continue;
            const float area = dot(cross(b - a, c - a), nn);
            const float la = dot(cross(c - b, p - b), nn) / area, lb = dot(cross(a - c, p - c), nn) / area, lc = 1 - la - lb;
            if (la < -1e-4f || lb < -1e-4f || lc < -1e-4f) continue;
            float e[4];
            for (int k = 0; k < 4; k++) e[k] = v[0]->edge[k] * la + v[1]->edge[k] * lb + v[2]->edge[k] * lc;
            return std::min(std::min(e[0], e[1]), std::min(e[2] - e[0], e[3] - e[1]));
        }
        return -1.0f;   // (no face there)
    };
    BakedWorld g;
    buildWorld(m, g);
    const Vec3 nz(0, 0, -1);
    const float boxSide = outlineAt(g, {1.97f, 1.0f, 0}, nz), triSide = outlineAt(g, {2.03f, 1.0f, 0}, nz), crease = outlineAt(g, {3.0f, 0.97f, 0}, nz);
    if (getenv("SLOPE_TRACE")) printf("    grouped: box side %.3f, triangle side %.3f, its slope edge %.3f\n", boxSide, triSide, crease);
    CHECK(boxSide > 0.5f && triSide > 0.5f && crease >= 0 && crease < 0.1f);
    Map loose;   // the same pieces ungrouped: outlines stay
    CHECK(loose.parse("box 0 0 0 2 2 2\nwedge 3 1 2 0 0 4 2 2\n" + base));
    BakedWorld l;
    buildWorld(loose, l);
    const float lb = outlineAt(l, {1.97f, 1.0f, 0}, nz), lt = outlineAt(l, {2.03f, 1.0f, 0}, nz);
    if (getenv("SLOPE_TRACE")) printf("    loose: box side %.3f, triangle side %.3f\n", lb, lt);
    CHECK(lb >= 0 && lb < 0.1f && lt >= 0 && lt < 0.1f);
}

// Materials 12-19 (owner, 2026-10-28): ice is slippery underfoot, rubber bounces you off it (floor and walls), the rest
// are solid like base; every keyword reads and writes.
static void testMaterials() {
    printf("materials: ice, rubber, the new keywords\n");
    {
        Map m;
        std::string t = "version " + std::to_string(kMapFormatVersion) + "\nbox -20 -1 -20 20 0 20\nspawn any 0 0 0\n";
        for (int k = 12; k < kMapMaterials; k++) { char b[96]; snprintf(b, sizeof b, "%s %d 0 30 %d 1 31\n", kMapMaterialWords[k], k * 2, k * 2 + 1); t += b; }
        CHECK(m.parse(t) && m.brushes.size() == 1 + (size_t)(kMapMaterials - 12));
        for (int k = 12; k < kMapMaterials; k++) CHECK(m.brushes[k - 11].material == k && (semiMaterial(k) || solidMaterial(k) == !(liquidMaterial(k) || fieldMaterial(k))));   // (a see-through one: when it stops anything, testSeeThrough)   // (the liquids: you wade in)
        const std::string out = m.serialize();
        CHECK(out.find("brick 40 0 30 41 1 31") != std::string::npos && out.find("diamondplate 38 0 30 39 1 31") != std::string::npos && out.find("ice 24 0 30 25 1 31") != std::string::npos);
    }
    auto slide = [](const char* floor) {   // running at 8 m/s, then nothing held for 0.5 s: how far the speed fell
        Map m;
        CHECK(m.parse(std::string(floor) + " -40 -1 -40 40 0 40\nspawn any 0 0 0\n"));
        MoveState s; s.pos = {0, 0, 0}; s.vel = {8, 0, 0}; s.flags = MF_ONGROUND;
        UserCmd c; c.msec = 8;
        for (int t = 0; t < 62; t++) pmove(s, c, m);
        return std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
    };
    {   // a dash off ice does a quarter of what it does off base (owner, 2026-10-28)
        auto dash = [](const char* floor) {
            Map m;
            CHECK(m.parse(std::string(floor) + " -40 -1 -40 40 0 40\nspawn any 0 0 0\n"));
            MoveState s; s.pos = {0, 0, 0}; s.flags = MF_ONGROUND;
            UserCmd c; c.msec = 8; c.buttons = BTN_DASH; c.forward = 127;
            pmove(s, c, m); c.buttons = 0;
            for (int t = 0; t < 20; t++) pmove(s, c, m);
            return -s.pos.z;
        };
        const float b = dash("box"), i = dash("ice");
        printf("    a dash from standing, 0.17 s later: base %.2f m, ice %.2f m\n", b, i);
        CHECK(i > 0.1f && i < b * 0.5f);
    }
    const float onBase = slide("box"), onIce = slide("ice");
    printf("    let go at 8 m/s, 0.5 s later: base %.2f, ice %.2f m/s\n", onBase, onIce);
    CHECK(onBase < 0.5f && onIce > 5.0f);
    {   // rubber grips: from standing, running reaches speed sooner than on base, and stops sooner
        auto run = [](const char* floor, int ticks, bool held) {
            Map m;
            CHECK(m.parse(std::string(floor) + " -40 -1 -40 40 0 40\nspawn any 0 0 0\n"));
            MoveState s; s.flags = MF_ONGROUND; s.vel = held ? Vec3() : Vec3(8, 0, 0);
            UserCmd c; c.msec = 8; c.forward = held ? 127 : 0; c.yaw = -PI * 0.5f;
            for (int t = 0; t < ticks; t++) pmove(s, c, m);
            return std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
        };
        const float gb = run("box", 6, true), gr = run("rubber", 6, true), sb = run("box", 6, false), sr = run("rubber", 6, false);
        printf("    48 ms running from standing: base %.2f, rubber %.2f m/s; 48 ms letting go of 8: base %.2f, rubber %.2f\n", gb, gr, sb, sr);
        CHECK(gr > gb * 1.2f && sr < sb * 0.9f);
    }
    {   // dropped 5 m onto rubber: back up at most of the landing speed; onto base: stays
        auto drop = [](const char* floor) {
            Map m;
            CHECK(m.parse(std::string(floor) + " -40 -1 -40 40 0 40\nspawn any 0 0 0\n"));
            MoveState s; s.pos = {0, 5, 0};
            UserCmd c; c.msec = 8;
            float top = 0;
            bool landed = false;
            for (int t = 0; t < 250; t++) { pmove(s, c, m); if (s.pos.y < 0.05f || s.vel.y > 0) landed = true; if (landed) top = std::max(top, s.pos.y); }
            return top;
        };
        const float rb = drop("rubber"), bb = drop("box");
        printf("    dropped 5 m: rubber back up to %.2f m, base %.2f m\n", rb, bb);
        CHECK(rb > 3.0f && rb < 5.0f && bb < 0.01f);
    }
    {   // running into a rubber wall at 10 m/s: back off it
        Map m;
        CHECK(m.parse("box -40 -1 -40 40 0 40\nrubber 3 0 -5 4 4 5\nspawn any 0 0 0\n"));
        MoveState s; s.pos = {0, 0, 0}; s.vel = {10, 0, 0}; s.flags = MF_ONGROUND;
        UserCmd c; c.msec = 8; c.forward = 127; c.yaw = -PI * 0.5f;   // (running at it, +x)
        float minVx = 0;
        for (int t = 0; t < 40; t++) { pmove(s, c, m); minVx = std::min(minVx, s.vel.x); }
        printf("    into a rubber wall at 10 m/s: back at %.2f m/s\n", -minVx);
        CHECK(minVx < -5.0f);
    }
}

// Rockets and stickies on rubber and ice (owner, 2026-10-28): a rocket bounces off rubber (no blast), a sticky bounces off
// it too, a sticky thrown along ice slides before it sticks.
// Glass and blown holes (owner, 2026-10-28): a pane breaks and lets shots, players and sight through; thin brick and
// cobblestone get a hole from a blast (2.5 m less the distance to the wall), thick ones don't; everything comes back.
static void testDestruction(uint16_t port) {
    printf("destruction: glass breaks, thin brick blows open, all of it comes back\n");
    {   // format 7: materials 139-471 by keyword (the first and the last), solid and plain; a format 6 file converts unchanged
        Map m;
        CHECK(m.parse("version 7\nbox -20 -1 -20 20 0 20\ndirt 0 0 5 2 3 7\n" + std::string(kMapMaterialWords[kMatLiquids - 1]) + " 4 0 5 6 3 7\nspawn any 0 0 0\n"));
        CHECK(m.brushes.size() == 3 && m.brushes[1].material == 139 && m.brushes[2].material == kMatLiquids - 1 && kMatLiquids == 472);
        CHECK(m.warnings.empty() && solidMaterial(139) && !holeMaterial(139) && m.glass.empty());
        CHECK(m.raycastAny({1, 1.5f, 0}, {0, 0, 1}, 6.0f));
        std::string o;
        CHECK(upgradeMapText("version 6\nbox 0 0 0 1 1 1\n", &o) && o == "version " + std::to_string(kMapFormatVersion) + "\nbox 0 0 0 1 1 1\n");
        // format 8 (liquids, large patterns) converts unchanged to 9 (the see-through looks)
        CHECK(upgradeMapText("version 9\nsmokewall 0 0 0 1 1 1\n", &o) && o == "version " + std::to_string(kMapFormatVersion) + "\nsmokewall 0 0 0 1 1 1\n");
        CHECK(upgradeMapText("version 8\nhoney 0 0 0 1 1 1\n", &o) && o == "version " + std::to_string(kMapFormatVersion) + "\nhoney 0 0 0 1 1 1\n");
    }
    {   // format 6 (owner, 2026-10-29): plain brick, cobblestone and glass never break; the breakable ones do
        Map m;
        CHECK(m.parse("version 6\nbox -20 -1 -20 20 0 20\nglass -2 0 5 2 3 5.05\nbrick -3 0 10 3 3 10.1\ncobblestone -3 0 20 3 3 20.12\n"
                      "breakableglass -2 0 -5 2 3 -4.95\nbreakablebrick -3 0 -10 3 3 -9.9\nspawn any 0 0 0\n"));
        CHECK(m.glass.size() == 1 && m.glass[0] == 4 && m.windows.size() == 1 && m.windows[0] == 1);
        CHECK(!m.breakGlass(1) && m.breakGlass(4));
        CHECK(!m.raycastAny({0, 1.5f, 0}, {0, 0, 1}, 4.99f));   // (sight passes plain glass too)
        CHECK(m.punchHole({0, 1.5f, 9.95f}) == 0 && m.punchHole({0, 1.5f, 19.95f}) == 0 && m.punchHole({0, 1.5f, -9.85f}) == 1);
        // a version 5 file: all glass and thin brick / cobblestone (boxes, box parts, a room with walls that thin) turn breakable
        std::string o;
        CHECK(upgradeMapText("version 5\nglass 0 0 0 1 1 0.05\nbrick 0 0 0 4 3 0.1 # thin\nbrick 0 0 0 4 3 1\ncobblestone 0 0 0 4 0.12 4\n"
                             "wedge 21 0 0 0 0 1 1 1\nwedge 20 0 0 0 0 1 1 0.1\ngroup room 20 0 0 0 0 0.4 3 3\ngroup room 24 0 10 0 0 14 3 4\n"
                             "group custom 0 0 0 0 0 4 2 1\npart 20 0 0 0 1 1 0.1\npart 20 0 0 0 1 1 0.5\npart 21 0 0 0 1 1 1 wedge 0\n", &o));
        CHECK(o.find("breakableglass 0 0 0 1 1 0.05\n") != std::string::npos && o.find("breakablebrick 0 0 0 4 3 0.1 # thin\n") != std::string::npos);
        CHECK(o.find("\nbrick 0 0 0 4 3 1\n") != std::string::npos && o.find("breakablecobblestone 0 0 0 4 0.12 4\n") != std::string::npos);
        CHECK(o.find("wedge 37 0 0 0 0 1 1 1\n") != std::string::npos && o.find("wedge 20 0 0 0 0 1 1 0.1\n") != std::string::npos);
        CHECK(o.find("group room 36 0 0 0 0 0.4 3 3\n") != std::string::npos && o.find("group room 24 0 10 0 0 14 3 4\n") != std::string::npos);
        CHECK(o.find("part 36 0 0 0 1 1 0.1\n") != std::string::npos && o.find("part 20 0 0 0 1 1 0.5\n") != std::string::npos && o.find("part 37 0 0 0 1 1 1 wedge 0\n") != std::string::npos);
        Map c;
        std::string perr;
        CHECK(c.parse(o + "spawn any 2 3 2\n", &perr) && c.warnings.empty());
        if (!perr.empty()) printf("    ERR %s\n", perr.c_str());
    }
    {
        Map m;
        CHECK(m.parse("box -20 -1 -20 20 0 20\nglass -2 0 5 2 3 5.05\nbrick -3 0 10 3 3 10.1\nbrick -3 0 15 3 3 15.5\n"
                      "cobblestone -3 0 20 3 3 20.12\nspawn any 0 0 0\n"));
        CHECK(m.glass.size() == 1 && m.glass[0] == 1);
        const Vec3 eye = {0, 1.5f, 0}, fwd = {0, 0, 1};
        CHECK(std::fabs(m.raycast(eye, fwd, 50) - 5.0f) < 0.01f);
        CHECK(!m.raycastAny(eye, fwd, 4.99f) && m.glassAt({0, 1.5f, 5.0f}) == 1);   // (sight passes glass)
        CHECK(m.breakGlass(1) && !m.breakGlass(1) && m.glassAt({0, 1.5f, 5.0f}) < 0);
        CHECK(std::fabs(m.raycast(eye, fwd, 50) - 10.0f) < 0.01f);                     // through the gap to the thin brick
        // a direct hit on the thin brick: a hole about 2.5 m across; the edge of the wall stays
        const size_t before = m.brushes.size();
        CHECK(m.punchHole({0, 1.5f, 9.95f}) == 1 && m.brushes[2].gone && m.brushes.size() > before && m.holes.size() == 1);
        printf("    hole: %zu pieces, r %.2f\n", m.brushes.size() - before, m.holes.empty() ? 0.0f : m.holes[0].r);
        CHECK(std::fabs(m.raycast(eye, fwd, 50) - 15.0f) < 0.01f);                     // through the hole to the thick one
        CHECK(std::fabs(m.raycast({2.8f, 1.5f, 0}, fwd, 50) - 10.0f) < 0.01f);         // the wall's end: still there
        CHECK(std::fabs(m.raycast({0, 2.97f, 0}, fwd, 50) - 10.0f) < 0.01f || std::fabs(m.raycast({0, 0.03f, 0}, fwd, 50) - 10.0f) > -1);
        CHECK(m.punchHole({0, 1.5f, 15.0f - 0.05f}) == 0);                                // too thick: nothing
        CHECK(m.punchHole({0, 1.5f, 20.0f - 1.6f}) == 1 && m.holes.size() == 2 && m.holes[1].r < 0.7f);   // 1.5 m off: a smaller one
        {   // blown away past 80%: the rest of the wall comes down too (owner, 2026-10-29)
            Map c;
            CHECK(c.parse("box -20 -1 -20 20 0 20\nbrick -3 0 10 3 3 10.1\nspawn any 0 0 0\n"));
            std::vector<AABB> fell;
            int blasts = 0;
            for (float x : {-2.0f, 0.0f, 2.0f, -2.0f, 0.0f, 2.0f})
                if (fell.empty()) { c.punchHole({x, blasts < 3 ? 0.9f : 2.1f, 9.95f}, &fell); blasts++; }
            printf("    crumble: after %d blasts\n", blasts);
            CHECK(fell.size() == 1 && blasts >= 2 && c.brushes[1].gone && c.holes.back().r < 0);
            for (const Brush& q : c.brushes) CHECK(q.origin != 1 || q.gone);
            CHECK(c.raycast({2.8f, 1.5f, 0}, {0, 0, 1}, 50) < 0);   // (its end too: gone)
            c.resetDestruction();
            CHECK(!c.brushes[1].gone && c.holes.empty());
        }
        m.resetDestruction();
        CHECK(m.holes.empty() && m.brushes.size() == before && !m.brushes[1].gone && !m.brushes[2].gone);
        CHECK(std::fabs(m.raycast(eye, fwd, 50) - 5.0f) < 0.01f);
        // dashing through a pane: the move isn't stopped, and the pane is named for the server to break
        MoveState st; st.pos = {0, 0, 3.2f}; st.flags = MF_ONGROUND;
        UserCmd c; c.msec = 8; c.buttons = BTN_DASH; c.forward = 127; c.yaw = (float)M_PI;   // (yaw pi: facing +z)
        int pane = -1;
        for (int t = 0; t < 30 && pane < 0; t++) { pmove(st, c, m); if (st.glassBrush >= 0) pane = st.glassBrush; c.buttons = 0; }
        printf("    dash: at z %.2f, pane %d\n", st.pos.z, pane);
        CHECK(pane == 1 && st.pos.z > 4.4f);
    }
    {   // the event round trip
        DestructEv d, r;
        d.kind = DESTRUCT_GLASS; d.how = GLASS_DASH; d.by = 3; d.brush = 77; d.at = {1, 2, 3}; d.dir = {0, 0, 1}; d.seed = 1234;
        CHECK(readDestructEvent(destructEvent(d), &r) && r.brush == 77 && r.how == GLASS_DASH && r.by == 3 && r.seed == 1234 && r.at.y == 2);
        std::vector<uint32_t> g, g2; std::vector<Vec3> h, h2;
        for (uint32_t k = 0; k < 130; k++) g.push_back(k);
        for (int k = 0; k < 25; k++) h.push_back({(float)k, 0, 0});
        auto evs = destructListEvents(g, h);
        for (auto& e : evs) CHECK(e.text.size() <= 255 && readDestructList(e, &g2, &h2));
        CHECK(evs.size() == 5 && g2 == g && h2.size() == 25 && h2[24].x == 24);
        std::vector<GlassShard> a, b;
        glassShards({{-1, 0, 0}, {1, 2, 0.05f}}, GLASS_SHOT, {0, 1, 0}, {0, 0, 1}, 99, a);
        glassShards({{-1, 0, 0}, {1, 2, 0.05f}}, GLASS_SHATTER, {0, 1, -1}, {}, 99, b);
        // (a 4 m2 pane: SHARD 2-5 a m2 at 10 damage, SHATTER 25-100 a m2 at 1; q, 2026-10-29)
        CHECK(a.size() >= 8 && a.size() <= 20 && b.size() >= 100 && b.size() <= 400);
        CHECK(a[0].damage == kGlassShardDamage && b[0].damage == kGlassShatterDamage);
        bool on = true; for (auto& x : a) on = on && x.vel.z > 0; for (auto& x : b) on = on && x.vel.z > 0;
        CHECK(on);   // (along the shot; away from the blast's side)
    }
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    std::vector<NetClient*> none;
    auto idle = [](int) { return UserCmd(); };
    pump(s, none, 0.1, idle);
    CHECK(s.map_.parse("box -40 -1 -40 40 0 40\nglass -2 0 5 2 3 5.05\nglass -2 0 -5 2 3 -4.95\nbrick -3 0 10 3 3 10.1\nspawn any 0 0 0\n"));
    s.map_.name = "bastion";
    int v = s.addBot(TEAM_A, BOT_BEGINNER);
    CHECK(v >= 0);
    if (v < 0) { s.stop(); return; }
    auto& p = s.players_[v];
    p.alive = true; p.health = 100; p.move.pos = {0, 0, 6.0f}; p.move.vel = {};
    // a shot's shards fly on through the pane into whoever stands behind it: 10 each
    CHECK(s.breakGlass(1, GLASS_SHOT, {0, 1.0f, 5.0f}, {0, 0, 1}, -1) && s.shardsFlyingForTest() >= 10);
    for (int k = 0; k < 20; k++) { p.move.pos = {0, 0, 6.0f}; p.move.vel = {}; s.tickShards(); s.shardStepAt_ = 0; s.timeMs_ += 10; }
    printf("    shot shards: health 100 -> %.0f\n", p.health);
    CHECK(p.health < 100 && std::fmod(100 - p.health, 10.0f) < 0.01f);
    {   // one pane's shards hurt a player kGlassPaneMaxDamage (50) at most (owner, 2026-10-29): 8 more of pane 1's 10s
        p.health = 1000;
        const float before = p.health;
        for (int k = 0; k < 8; k++) s.shards_.push_back({{0, 1.0f, 5.5f}, {0, 0, 30}, 0.05f, kGlassShardDamage, s.time() + 3, -1, -1, 1u});
        for (int k = 0; k < 10; k++) { p.move.pos = {0, 0, 6.0f}; p.move.vel = {}; s.tickShards(); s.shardStepAt_ = 0; s.timeMs_ += 10; }
        float total = 0; for (float t : s.paneTaken_[1u]) total += t;
        printf("    pane 1's shards on one player: %.0f in all (before these %.0f)\n", s.paneTaken_[1u][(size_t)v], s.paneTaken_[1u][(size_t)v] - (before - p.health));
        CHECK(s.paneTaken_[1u][(size_t)v] <= kGlassPaneMaxDamage + 1e-3f && s.paneTaken_[1u][(size_t)v] > kGlassPaneMaxDamage - 1e-3f);
        p.health = 100;
    }
    // a rocket next to the other pane and the brick: both break
    p.health = 100;
    s.explodeForTest({0, 1.0f, -5.5f}, -1, WPN_ROCKET);
    CHECK(s.map_.brushes[2].gone);
    s.explodeForTest({0, 1.5f, 9.95f}, -1, WPN_ROCKET);
    CHECK(s.map_.brushes[3].gone && s.map_.holes.size() == 1);
    s.resetDestruction();
    CHECK(!s.map_.brushes[1].gone && !s.map_.brushes[2].gone && !s.map_.brushes[3].gone && s.map_.holes.empty() && s.shardsFlyingForTest() == 0);
    {   // breakable blocks (562-585, owner 2026-10-29): hp from shots (x bullet) and blasts (x blast), or on contact
        CHECK(s.map_.parse("box -40 -1 -40 40 0 40\nwoodencrate -1 0 10 1 2 12\nthinconcretepanel -1 0 20 1 2 20.2\nsugarglass 5 0 -1 6 2 1\n"
                           "rottenplanks -10 0 -10 -6 0.5 -6\nplywoodboardup 10 0 -1 10.2 2 1\nspawn any 0 0 0\n"));
        s.blockDmg_.clear(); s.blockStood_.clear();
        CHECK(s.map_.blocks.size() == 5 && solidMaterial(kMatBreak) && !bakedMaterial(kMatBreak));
        s.blockFromRay({0, 1, 0}, {0, 0, 1}, 10.0f, 50.0f, -1);   // crate: 80 hp
        CHECK(!s.map_.brushes[1].gone);
        s.blockFromRay({0, 1, 0}, {0, 0, 1}, 10.0f, 50.0f, -1);
        CHECK(s.map_.brushes[1].gone);
        for (int k = 0; k < 5; k++) s.blockFromRay({0, 1, 15}, {0, 0, 1}, 5.0f, 100.0f, -1);   // concrete: bullets do nothing
        CHECK(!s.map_.brushes[2].gone);
        s.explodeForTest({0, 1.0f, 19.5f}, -1, WPN_ROCKET);   // a rocket at it does
        s.explodeForTest({0, 1.0f, 19.5f}, -1, WPN_ROCKET);
        CHECK(s.map_.brushes[2].gone);
        p.move.pos = {4.69f, 0, 0}; p.move.vel = {}; p.move.flags = MF_ONGROUND;   // touching sugar glass
        s.blocksTouched(v);
        CHECK(s.map_.brushes[3].gone);
        p.move.pos = {9.69f, 0, 0}; p.move.dashTimer = 0;   // plywood: walking into it doesn't, a dash does
        s.blocksTouched(v);
        CHECK(!s.map_.brushes[5].gone);
        p.move.dashTimer = 0.1f;
        s.blocksTouched(v);
        CHECK(s.map_.brushes[5].gone);
        p.move.dashTimer = 0; p.move.pos = {-8, 0.5f, -8};   // standing on rotten planks: gone after kWeightBreakS
        s.blocksTouched(v);
        CHECK(!s.map_.brushes[4].gone);
        s.timeMs_ += 400;
        s.blocksTouched(v);
        CHECK(s.map_.brushes[4].gone);
        s.resetDestruction();
        CHECK(!s.map_.brushes[1].gone && !s.map_.brushes[4].gone && s.blockDmg_.empty());
    }
    {   // the crumble platform: stood on, it falls kCrumbleS later, and is back kCrumbleBackS after that
        CHECK(s.map_.parse("box -40 -1 -40 40 0 40\ncrumbleplatform -10 0 10 -6 0.5 14\nspawn any 0 0 0\n"));
        CHECK(s.map_.crumbles.size() == 1 && solidMaterial(kMatCrumble) && !bakedMaterial(kMatCrumble));
        p.move.pos = {-8, 0.5f, 12}; p.move.vel = {}; p.move.flags = MF_ONGROUND;
        s.crumblesTouched(v);
        s.tickCrumbles();
        CHECK(!s.map_.brushes[1].gone);
        s.timeMs_ += 1100;
        s.tickCrumbles();
        CHECK(s.map_.brushes[1].gone && !s.map_.boxSolid({{-9, 0.1f, 11}, {-7, 0.4f, 13}}));
        s.timeMs_ += 3000;
        s.tickCrumbles();
        CHECK(s.map_.brushes[1].gone);
        p.move.pos = {0, 0, 0};
        s.timeMs_ += 2100;
        s.tickCrumbles();
        CHECK(!s.map_.brushes[1].gone && s.map_.boxSolid({{-9, 0.1f, 11}, {-7, 0.4f, 13}}));
        s.resetDestruction();
    }
    s.stop();
}

static void testProjectileMaterials(uint16_t port) {
    printf("projectiles: rubber bounces, ice slides\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    std::vector<NetClient*> none;
    auto idle = [](int) { return UserCmd(); };
    pump(s, none, 0.1, idle);
    CHECK(s.map_.parse("box -40 -1 -40 40 0 40\nrubber 10 0 -5 11 6 5\nice -40 0 -40 -10 0.5 40\nspawn any 0 0 0\n"));
    auto fire = [&](int type, Vec3 pos, Vec3 vel) -> Projectile& {
        Projectile& p = s.projectiles_[1];
        p = Projectile();
        p.active = true; p.type = (uint8_t)type; p.owner = 0; p.id = 1001; p.pos = pos; p.vel = vel; p.dieAt = s.time() + 20;
        return p;
    };
    {
        Projectile& r = fire(WPN_ROCKET, {0, 3, 0}, {20, 0, 0});
        pump(s, none, 0.8, idle);
        CHECK(r.active && r.vel.x < -10 && r.pos.x < 9);   // (off the wall at x 10, back toward -x)
        r.active = false;
    }
    {
        Projectile& st = fire(WPN_STICKY, {0, 3, 0}, {15, 0, 0});
        pump(s, none, 0.5, idle);
        CHECK(st.active && !st.stuck && st.pos.x < 9.5f);
        st.active = false;
    }
    {
        Projectile& st = fire(WPN_STICKY, {-20, 1.5f, 0}, {-8, -2, 0});   // (onto the ice, going -x)
        pump(s, none, 0.25, idle);
        const float x0 = st.pos.x;
        pump(s, none, 0.5, idle);
        printf("    sticky on ice: %.2f -> %.2f, stuck %d\n", x0, st.pos.x, (int)st.stuck);
        CHECK(st.active && st.pos.x < x0 - 1.0f && st.pos.y > 0.4f);
        pump(s, none, 6.0, idle);
        CHECK(st.stuck);
    }
    {   // feature blocks: a rocket into a teleporter comes out of its pair's top; in a slow-mo field it flies at half speed
        CHECK(s.map_.parse("box -40 -1 -40 40 0 40\nteleporter 10 0 -2 11 4 2 tp 3 0\nteleporter 20 0 20 22 1 22 tp 3 2\n"
                           "slowmofield -30 0 -30 -10 10 -10\nspawn any 0 0 0\n"));
        Projectile& r = fire(WPN_ROCKET, {0, 2, 0}, {20, 0, 0});
        pump(s, none, 0.6, idle);
        printf("    rocket through a teleporter: %.2f %.2f %.2f, vel %.2f %.2f %.2f\n", r.pos.x, r.pos.y, r.pos.z, r.vel.x, r.vel.y, r.vel.z);
        CHECK(r.active && r.pos.x > 20 && r.pos.x < 22 && r.pos.y > 1.5f && r.vel.y > 15);
        r.active = false;
        Projectile& q = fire(WPN_ROCKET, {-28, 5, -20}, {10, 0, 0});
        pump(s, none, 0.5, idle);
        printf("    rocket in slow-mo: %.2f m in 0.5 s at 10 m/s\n", q.pos.x + 28);
        CHECK(q.active && q.pos.x + 28 > 2.0f && q.pos.x + 28 < 3.2f);
        q.active = false;
    }
    s.stop();
}

static void testSurf() {
    printf("surf: ride a ramp end to end\n");
    Map m;
    CHECK(m.load("cascade") && m.preferredMode == MODE_SURF && m.supportsMode(MODE_SURF) && m.checkpoints.size() == 4 && !m.supportsMode(MODE_FFA));
    {   // q's hand-made maps ship (owner, 2026-10-30); surfy is surf's default map (Game::bestMapFor)
        Map hs, hc;
        CHECK(hs.load("surfy") && hs.preferredMode == MODE_SURF && hs.supportsMode(MODE_SURF));
        CHECK(hc.load("cobblestone") && !hc.brushes.empty());
    }
    // the first ridge: x -4.7..4.7 along z 44..88, faces from y 34 to 40; ride its east face toward -z
    MoveState ms; ms.pos = {2.2f, 39.5f, 86}; ms.vel = {0, 0, -18}; ms.mods = MOVE_RUN_ONLY;
    float lowest = 1e9f, minX = 1e9f, maxX = -1e9f;
    int t = 0;
    for (; t < 400 && ms.pos.z > 47; t++) {
        // (facing -z; strafing left, -x: into the face, only while sinking: with the ramp grip a steady push into it
        // climbs it, over the ridge, as a player easing off the key would not)
        UserCmd cmd; cmd.yaw = 0; cmd.side = ms.vel.y < 0 ? -127 : 0; cmd.msec = 8;
        if (t < 50) cmd.buttons = BTN_DASH | BTN_GRAPPLE;          // (neither does anything)
        pmove(ms, cmd, m);
        lowest = std::min(lowest, ms.pos.y); minX = std::min(minX, ms.pos.x); maxX = std::max(maxX, ms.pos.x);
    }
    if (getenv("SLOPE_TRACE")) printf("    ride: %d ticks, at %.2f %.2f %.2f, lowest %.2f, x %.2f..%.2f, speed %.1f\n", t, ms.pos.x, ms.pos.y, ms.pos.z, lowest, minX, maxX, length(ms.vel));
    CHECK(ms.pos.z <= 47 && lowest > 34.5f && maxX < 4.8f && ms.dashTimer == 0 && !(ms.flags & MF_HOOKED) && length(ms.vel) > 12);
}

static void testStairs() {
    printf("stairs: every flight walkable, none cutting a path\n");
    int flights = 0;
    for (auto& name : testedMaps()) {
        Map m;
        if (!m.load(name)) continue;
        HumanMadeGrace hg(name, m);
        NavGraph g;
        g.build(m, NavKind::Surfaces);   // (every walkable top whole: the bots' graph cuts out what stands on them)
        for (const Map::Stairs& st : m.stairs) {
            float x0 = st.x0, z0 = st.z0, x1 = st.x1, z1 = st.z1, base = st.base, top = st.top, rising = st.rising;
            bool solid = st.solid;
            flights++;
            bool ax = st.axis == 'x';
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

// Hide and seek / tag bots pick spots from the nav graph. After a map change they must not head for a spot on the
// previous map's graph (it's still there while the new map's builds in the background), nor keep one picked on it.
static void testBotGoalsAfterMapChange(uint16_t port) {
    printf("bot goals after a map change\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = false; cfg.map = "hamlet"; cfg.mode = MODE_HIDE; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    int bot = s.addBot(TEAM_A, BOT_ADVANCED);
    std::vector<NetClient*> none;
    for (int k = 0; k < 50 && !s.nav_.ready(s.map_); k++) pump(s, none, 0.1, idle);
    CHECK(bot >= 0 && s.nav_.ready(s.map_));
    if (bot < 0) { s.stop(); return; }
    auto onGraph = [](const Vec3& g, const std::vector<NavGraph::Node>& nodes) {
        for (auto& n : nodes)
            if (std::fabs((n.x0 + n.x1) * 0.5f - g.x) < 1e-3f && std::fabs((n.z0 + n.z1) * 0.5f - g.z) < 1e-3f && std::fabs(n.y - g.y) < 1e-3f) return true;
        return false;
    };
    auto& p = s.players_[bot];
    p.alive = true; p.it = false;
    s.bots_[bot].goalUntil = 0;
    botThink(s, bot);
    std::vector<NavGraph::Node> old = s.nav_.nodes;
    CHECK(onGraph(s.bots_[bot].goal, old));                  // a hiding spot on hamlet, kept for a while
    s.command("map hightown hide");
    p.alive = true; p.it = false; p.move.pos = s.map_.spawns[0].pos; p.move.vel = {};
    for (int k = 0; k < 5; k++) { botThink(s, bot); CHECK(!onGraph(s.bots_[bot].goal, old)); }   // (graph built or not)
    for (int k = 0; k < 50 && !s.nav_.ready(s.map_); k++) pump(s, none, 0.1, idle);
    p.alive = true; p.it = false;
    botThink(s, bot);
    CHECK(s.nav_.ready(s.map_) && onGraph(s.bots_[bot].goal, s.nav_.nodes)); // and once it's ready, a spot on the new one
    s.stop();
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
    { // an admin's addbot mid match: the team lock holds for bots too (it used to put them straight into play)
        int before[kMaxPlayers]; for (int i = 0; i < kMaxPlayers; i++) before[i] = s.players_[i].connected;
        s.command("addbot a");
        pump(s, cls, 0.3, idle);
        int bot = -1;
        for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && !before[i] && s.players_[i].isBot) bot = i;
        CHECK(bot >= 0 && s.players_[bot].team == TEAM_SPECTATOR && s.players_[bot].waitingToPlay && !s.players_[bot].alive);
        if (bot >= 0) s.removeBot(bot);
    }
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
            pump(s, cls, std::max(0.0, (double)(s.players_[it].nextFire - s.time())), idle);   // (the knife's refire after the first find)
            stab(it, h2);
            pump(s, cls, 0.2, idle);
            CHECK(s.ms_.phase == PHASE_ROUND_END && s.players_[it].score == 2);
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
    CHECK(orig.load("qk1"));
    Server s;   // (not started: just its map facts cache)
    CHECK(s.mapInfo("qk1").ok && s.mapInfo("qk1").ranked != 0);
    CHECK(saveMapFile(userMapsDir() + "/qk1.qmap", orig.source + "box 0 0 0 1 1 1\n")); // your edit shadows the shipped one
    Map m;
    CHECK(m.load("qk1") && !isOfficialMap(m));
    CHECK(s.mapInfo("qk1").ok && s.mapInfo("qk1").ranked == 0);   // votes / ranked see the file that's played
    std::string shipped = userDataPath("zz_install_maps");
    std::error_code ec;
    fs::remove_all(shipped, ec);
    fs::create_directories(shipped, ec);
    CHECK(saveMapFile(shipped + "/qk1.qmap", orig.source + "box 0 0 0 1 1 1\n"));   // a shipped map changed in place
    CHECK(saveMapFile(shipped + "/crucible.qmap", orig.source) && saveMapFile(shipped + "/crucible.qlm", "x"));   // retired
    CHECK(saveMapFile(shipped + "/temple.qmap", orig.source) && saveMapFile(shipped + "/temple.qlm", "x"));
    CHECK(resetOfficialMaps(shipped) >= 13);
    CHECK(!fs::exists(shipped + "/crucible.qmap", ec) && !fs::exists(shipped + "/crucible.qlm", ec));
    CHECK(!fs::exists(shipped + "/temple.qmap", ec) && !fs::exists(shipped + "/temple.qlm", ec));   // (1.19.6: retired to maps/legacy/)
    Map r;
    CHECK(r.load("qk1") && isOfficialMap(r));   // (your edited copy is gone)
    CHECK(s.mapInfo("qk1").ranked != 0);
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

// 1.19.6 legacy maps: the old pool ships in maps/legacy/, listed only with LEGACY MAPS on but loaded by name either
// way; and once per version the untouched generated copies in your maps folder move to its legacy/ (nothing else).
static void testLegacyMaps() {
    printf("legacy maps: listed with the setting, loaded by name, old copies moved aside once\n");
    namespace fs = std::filesystem;
    std::error_code ec;
    auto has = [](const std::vector<std::string>& v, const char* n) { return std::find(v.begin(), v.end(), n) != v.end(); };
    // the shipped legacy folder (make_maps.py LEGACY): off, not listed; on, listed; loads by name, never official
    setLegacyMapsListed(false);
    std::vector<std::string> off = listMaps();
    CHECK(!has(off, "temple") && !has(off, "bastion") && has(off, "qk1"));
    Map t;
    CHECK(t.load("temple") && !isOfficialMap(t) && !isRankedMap(t, 0) && isLegacyMap("temple") && !isLegacyMap("qk1"));
    for (const char* n : kLegacyMapNames) { Map l; CHECK(l.load(n) && !isOfficialMap(l)); if (isOfficialMapName(n)) printf("  %s is still official\n", n); CHECK(!isOfficialMapName(n)); }
    setLegacyMapsListed(true);
    std::vector<std::string> on = listMaps();
    CHECK(has(on, "temple") && has(on, "bastion") && has(on, "qk1"));
    // your legacy folder too; a name that is also a current map plays the current one
    CHECK(saveMapFile(userLegacyMapsDir() + "/zz_oldie.qmap", "title Oldie\nbox -20 -1 -20 20 0 20\nspawn any 0 0 0\n"));
    CHECK(saveMapFile(userLegacyMapsDir() + "/qk1.qmap", "title Not QK1\nbox -20 -1 -20 20 0 20\nspawn any 0 0 0\n"));
    on = listMaps();
    CHECK(has(on, "zz_oldie") && std::count(on.begin(), on.end(), std::string("qk1")) == 1);
    Map q;
    CHECK(q.load("qk1") && isOfficialMap(q));
    setLegacyMapsListed(false);
    CHECK(!has(listMaps(), "zz_oldie"));
    Map o;
    CHECK(o.load("zz_oldie") && o.title == "Oldie");   // (by name: whatever the setting says)
    fs::remove(userLegacyMapsDir() + "/zz_oldie.qmap", ec);
    fs::remove(userLegacyMapsDir() + "/qk1.qmap", ec);
    for (int v : {0, 1, 5}) {   // a ranked server still set to an old ranked map plays its ladder's designed-for map
        Server rs;
        ServerConfig cfg;
        cfg.map = v == 5 ? "gantry" : v == 1 ? "furnace" : "temple"; cfg.port = 0; cfg.lanOnly = true; cfg.ranked = true; cfg.arenaVariant = v;
        rs.setLog([](const std::string&) {});
        std::string err;
        bool ok = rs.start(cfg, &err);
        if (!ok) printf("  ranked on %s: %s\n", cfg.map.c_str(), err.c_str());
        CHECK(ok && rs.cfg_.map == rankedDefaultMap(v));
        rs.stop();
    }

    // the move: a made-up user folder with a generated copy (and its sidecars), one with the same name already in
    // legacy/, a hand-made one, a custom one and a download; and an install folder with a retired official map
    const std::string root = userDataPath("zz_legacy_move"), maps = root + "/maps", ship = root + "/install_maps", stamp = root + "/legacy_maps.stamp";
    fs::remove_all(root, ec);
    Map gen;
    CHECK(gen.load("temple"));
    const std::string g = gen.source;   // (# generated by tools/make_maps.py, no handmade line)
    CHECK(g.rfind("# generated by tools/make_maps.py", 0) == 0);
    CHECK(saveMapFile(maps + "/temple.qmap", g) && saveMapFile(maps + "/temple.mapedit.json", "{}") && saveMapFile(maps + "/temple.qlm", "x"));
    CHECK(saveMapFile(root + "/cache/temple.qlm", "x"));
    CHECK(saveMapFile(maps + "/bastion.qmap", g) && saveMapFile(maps + "/legacy/bastion.qmap", "title other\n"));   // a name taken in legacy/
    std::string hand = g;
    hand.insert(hand.find('\n') + 1, "handmade\n");
    CHECK(saveMapFile(maps + "/harbor.qmap", hand));                                   // hand-made (edited): stays
    CHECK(saveMapFile(maps + "/my_arena.qmap", "title Mine\nbox -20 -1 -20 20 0 20\nspawn any 0 0 0\n"));   // custom: stays
    CHECK(saveMapFile(maps + "/downloads/abc.qmap", g));                               // downloads: never touched
    CHECK(saveMapFile(maps + "/sub/canal.qmap", g));                                   // a subfolder: never touched
    CHECK(saveMapFile(maps + "/solarsystem12.qmap", g));                               // a generated map under your own name: stays
    CHECK(saveMapFile(ship + "/temple.qmap", g) && saveMapFile(ship + "/legacy/temple.qmap", g) && saveMapFile(ship + "/qk1.qmap", q.source));
    LegacyMoveReport r = moveOldOfficialMaps(maps, ship, stamp, "9.9.9");
    for (auto& l : r.log) printf("    %s\n", l.c_str());
    CHECK(r.ran && r.moved == 2 && r.shipped == 1);
    CHECK(!fs::exists(maps + "/temple.qmap", ec) && fs::exists(maps + "/legacy/temple.qmap", ec) && fs::exists(maps + "/legacy/temple.mapedit.json", ec));
    CHECK(!fs::exists(maps + "/temple.mapedit.json", ec) && !fs::exists(maps + "/temple.qlm", ec) && !fs::exists(root + "/cache/temple.qlm", ec));
    CHECK(!fs::exists(maps + "/bastion.qmap", ec) && fs::exists(maps + "/legacy/bastion_2.qmap", ec) && fs::exists(maps + "/legacy/bastion.qmap", ec));
    CHECK(fs::exists(maps + "/harbor.qmap", ec) && fs::exists(maps + "/my_arena.qmap", ec) && fs::exists(maps + "/downloads/abc.qmap", ec) && fs::exists(maps + "/sub/canal.qmap", ec));
    CHECK(fs::exists(maps + "/solarsystem12.qmap", ec) && !fs::exists(maps + "/legacy/solarsystem12.qmap", ec));
    CHECK(!fs::exists(ship + "/temple.qmap", ec) && fs::exists(ship + "/legacy/temple.qmap", ec) && fs::exists(ship + "/qk1.qmap", ec));
    // the stamp: the same version doesn't scan again (a new copy stays), the next version does
    CHECK(saveMapFile(maps + "/temple.qmap", g));
    LegacyMoveReport again = moveOldOfficialMaps(maps, ship, stamp, "9.9.9");
    CHECK(!again.ran && again.moved == 0 && fs::exists(maps + "/temple.qmap", ec));
    LegacyMoveReport next = moveOldOfficialMaps(maps, ship, stamp, "9.9.10");
    CHECK(next.ran && next.moved == 1 && !fs::exists(maps + "/temple.qmap", ec));   // (identical to legacy/temple: just removed)
    fs::remove_all(root, ec);
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
    pumpUntilSpawned(s, cls);
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

// The primary rocket speeds up kRocketAccel (16 m/s2) along its flight (owner, 2026-10-30); the lob doesn't. The server's
// 1 ms steps, the client's closed form (projectileExtrapolate) and its inverse (projectileRewind) agree.
static void testRocketThrust(uint16_t port) {
    printf("rocket thrust: the primary speeds up 16 m/s2, the lob unchanged\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = false; cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port; cfg.balanceBots = false;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    CHECK(s.map_.parse("box -100 -1 -100 100 0 100\nspawn any 0 0 0\n"));   // (open: nothing in the way for 1 s)
    CHECK(kRocketAccel == 16.0f && baseWeaponDef(WPN_ROCKET).projSpeed == 48.0f && std::fabs(baseWeaponDef(WPN_ROCKET).projSpeed * kRocketLobSpeed - 33.6f) < 1e-3f && weaponAlt().rocketAccel == kRocketAccel && projectileAccel(WPN_ROCKET, 1) == kRocketAccel);
    CHECK(projectileAccel(WPN_ROCKET, kRocketLobGravity) == 0 && projectileAccel(WPN_STICKY, 1) == 0 && projectileAccel(WPN_CROSSBOW, 1) == 0);
    const float sp0 = weaponDef(WPN_ROCKET).projSpeed;
    auto fly = [&](Vec3 pos, Vec3 vel, float gravMul, int ms) {
        for (auto& pr : s.projectiles_) pr.active = false;
        Projectile& pr = s.projectiles_[0];
        pr = Projectile();
        pr.active = true; pr.id = 1; pr.type = WPN_ROCKET; pr.owner = kMaxPlayers - 1; pr.pos = pos; pr.vel = vel; pr.gravMul = gravMul;
        pr.dieAt = s.time() + 10;
        const uint64_t end = s.timeMs_ + (uint64_t)ms;
        while (s.timeMs_ < end && pr.active) { s.tickProjectilesForTest(); s.timeMs_ += 1000 / s.tickRate(); }
        return pr;
    };
    const Vec3 from(-60, 10, -5), dir = normalize(Vec3(1, 0.15f, 0.2f));
    {   // the primary: 64 m/s after 1 s, along the same line, where the closed form puts it
        const Projectile pr = fly(from, dir * sp0, 1, 1000);
        Vec3 cp = from, cv = dir * sp0;
        projectileExtrapolate(cp, cv, projectileFlight(WPN_ROCKET, 1), 1.0f);
        printf("    primary after 1 s: %.4f m/s (closed form %.4f), %.4f m along, %.3f mm from the closed form\n", length(pr.vel), length(cv),
               dot(pr.pos - from, dir), length(pr.pos - cp) * 1000);
        CHECK(pr.active && std::fabs(length(pr.vel) - 64.0f) < 0.01f && std::fabs(length(cv) - 64.0f) < 0.01f);
        CHECK(length(normalize(pr.vel) - dir) < 1e-4f && length(pr.pos - cp) < 0.002f);
        CHECK(std::fabs(dot(pr.pos - from, dir) - (sp0 + 0.5f * kRocketAccel * 1.001f)) < 0.01f);   // (48 + 16 x 1000 x 1001 / 2 ms2)
        // run back: projectileRewind undoes it (whole steps and a part of one)
        for (float t : {1.0f, 0.2345f}) {
            Vec3 p = from, v = dir * sp0;
            projectileExtrapolate(p, v, projectileFlight(WPN_ROCKET, 1), t);
            projectileRewind(p, v, projectileFlight(WPN_ROCKET, 1), t);
            CHECK(length(p - from) < 0.001f && length(v - dir * sp0) < 0.001f);
        }
    }
    {   // the lob: no thrust, its usual drop
        const Vec3 v0 = dir * (sp0 * kRocketLobSpeed);
        const Projectile pr = fly(from, v0, kRocketLobGravity, 1000);
        Vec3 cp = from, cv = v0;
        projectileExtrapolate(cp, cv, projectileFlight(WPN_ROCKET, kRocketLobGravity), 1.0f);
        CHECK(pr.active && std::fabs(pr.vel.x - v0.x) < 1e-3f && std::fabs(pr.vel.z - v0.z) < 1e-3f && std::fabs(pr.vel.y - (v0.y - kRocketLobGravity)) < 0.01f);
        CHECK(length(pr.pos - cp) < 0.002f);
    }
    {   // a weapon set giving the primary a drop as well: the stepped path, and its rewind
        const ProjFlight f(9.0f, kRocketAccel);
        Vec3 sp = from, sv = dir * sp0, p = from, v = dir * sp0;
        const float t = 0.7f;
        const int n = (int)(t / kMoveStep + 1e-3f);   // (as projectileExtrapolate counts the whole steps; the rest in a line)
        for (int k = 0; k < n; k++) { projectileStepVel(sv, f, kMoveStep); sp += sv * kMoveStep; }
        sp += sv * std::max(0.0f, t - n * kMoveStep);
        projectileExtrapolate(p, v, f, t);
        CHECK(length(p - sp) < 1e-3f && length(v - sv) < 1e-3f);
        projectileRewind(p, v, f, t);
        CHECK(length(p - from) < 0.002f && length(v - dir * sp0) < 0.002f);
    }
    s.stop();
}

// C8: projectiles fly in fixed 1 ms steps, like movement, so an arc lands on the same spot at every server tick
// rate (125 / 250 / 500 / 1000 Hz). Also measures what the projectile tick costs at each rate.
static void testProjectileTickRates(uint16_t port) {
    printf("projectile arcs identical at every tick rate\n");
    Server s;
    ServerConfig cfg;
    cfg.autoStart = false; cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port; cfg.balanceBots = false;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    const Vec3 from = s.map_.spawns[0].pos + Vec3(0, kEyeHeight, 0);
    const uint64_t t0 = (s.timeMs_ / 8 + 1) * 8 + 8000;   // a time every rate ticks on
    auto spawn = [&](int k, int type, Vec3 vel, float gravMul, float life) {
        Projectile& pr = s.projectiles_[k];
        pr = Projectile();
        pr.active = true; pr.id = (uint16_t)(k + 1); pr.type = type; pr.owner = kMaxPlayers - 1;   // (nobody: no player is hit)
        pr.pos = from; pr.vel = vel; pr.gravMul = gravMul;
        pr.dieAt = s.time() + life; pr.armAt = s.time() + kStickyArmTime;
    };
    // Fly one projectile at each rate until it lands (or its life runs out): where it ends up.
    auto fly = [&](int level, int type, Vec3 vel, float gravMul, float life, bool* landed) {
        for (auto& pr : s.projectiles_) pr.active = false;
        s.rateLevel_ = level;
        s.timeMs_ = t0;
        spawn(0, type, vel, gravMul, life);
        const Projectile& pr = s.projectiles_[0];
        for (int t = 0; t < 40000 && pr.active && !pr.stuck; t++) { s.tickProjectilesForTest(); s.timeMs_ += 1000 / s.tickRate(); }
        *landed = pr.stuck || s.time() < pr.dieAt;
        return pr.pos;
    };
    struct Shot { const char* name; int type; float speed, pitch, yaw, gravMul, life; };
    const Shot shots[] = {
        {"rocket", WPN_ROCKET, weaponDef(WPN_ROCKET).projSpeed, 0.25f, 0.4f, 1, weaponDef(WPN_ROCKET).life},
        {"rocket lob", WPN_ROCKET, weaponDef(WPN_ROCKET).projSpeed * kRocketLobSpeed, 0.6f, 2.0f, kRocketLobGravity, weaponDef(WPN_ROCKET).life},
        {"sticky", WPN_STICKY, weaponDef(WPN_STICKY).projSpeed, 0.5f, -1.2f, 1, weaponDef(WPN_STICKY).life},
        {"knife", WPN_KNIFE, kKnifeThrowSpeed, 0.35f, 3.0f, 1, kKnifeThrowLife},
        {"crossbow", WPN_CROSSBOW, weaponDef(WPN_CROSSBOW).projSpeed, 0.2f, -2.5f, 1, weaponDef(WPN_CROSSBOW).life},
        {"syringe", WPN_SYRINGE, kSyringeThrowSpeed, 0.4f, 1.0f, 1, kSyringeThrowLife},
        {"bubble", WPN_BUBBLE, weaponDef(WPN_BUBBLE).projSpeed, 0.0f, 0.8f, 1, weaponDef(WPN_BUBBLE).life},
    };
    for (const Shot& sh : shots) {
        Vec3 vel = angleForward(sh.yaw, sh.pitch) * sh.speed;
        bool landed[kTickRateLevels];
        Vec3 at[kTickRateLevels];
        for (int level = 0; level < kTickRateLevels; level++) at[level] = fly(level, sh.type, vel, sh.gravMul, sh.life, &landed[level]);
        float worst = 0;
        for (int level = 1; level < kTickRateLevels; level++) worst = std::max(worst, length(at[level] - at[0]));
        printf("  %-10s %s (%.2f, %.2f, %.2f): at most %.4f mm apart at 125-1000 Hz\n", sh.name, landed[0] ? "landed at" : "expired at",
               at[0].x, at[0].y, at[0].z, worst * 1000);
        CHECK(worst < 0.001f);
        for (int level = 1; level < kTickRateLevels; level++) CHECK(landed[level] == landed[0]);
        // the client draws it on the same arc: its extrapolation (up to 0.2 s, in closed form) vs the server's steps
        Vec3 sp = from, sv = vel, cp = from, cv = vel;
        const ProjFlight g = projectileFlight(sh.type, sh.gravMul);   // (the primary rocket: its thrust)
        for (int k = 0; k < 200; k++) { projectileStepVel(sv, g, kMoveStep); sp += sv * kMoveStep; }
        projectileExtrapolate(cp, cv, g, 0.2f);
        CHECK(length(cp - sp) < 0.001f && std::fabs(cv.y - sv.y) < 0.01f);
    }
    // Cost: 100 bubbles drifting (the slowest, longest-lived projectile) for 1 s at each rate.
    for (int level = 0; level < kTickRateLevels; level++) {
        double best = 1e9;
        for (int rep = 0; rep < 5; rep++) {
            for (auto& pr : s.projectiles_) pr.active = false;
            s.rateLevel_ = level;
            s.timeMs_ = t0;
            for (int k = 0; k < 100; k++) spawn(k, WPN_BUBBLE, angleForward(k * 0.0628f, 0.3f * std::sin(k * 1.7f)) * weaponDef(WPN_BUBBLE).projSpeed * 0.5f, 1, 4);
            int ticks = 0;
            double a = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            while (s.timeMs_ < t0 + 1000) { s.tickProjectilesForTest(); s.timeMs_ += 1000 / s.tickRate(); ticks++; }
            best = std::min(best, (std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - a) / ticks);
        }
        printf("  100 projectiles at %4d Hz: %.1f us a tick, %.2f ms per second of play\n", kTickRates[level], best * 1e6, best * kTickRates[level] * 1e3);
    }
    s.stop();
}

// Stuck knives / bolts / syringes (owner, 1.17.0: "projectiles that miss a player should stay stuck in the wall until
// the game is over, and projectiles that hit a player should stay stuck in them until they die"). Cosmetic: one EV_PROP
// each, kept by the clients (NetClient::stuckProps), never in the snapshots. A thrown knife's prop outlives the
// projectile (and the knife still comes back on its timer); a bolt in a player rides on them until they die; a match
// restart clears everything; the newest kMaxStuckProps are kept; a late joiner gets the newest few.
static void testStuckProps(uint16_t port) {
    printf("stuck knives / bolts / syringes (props)\n");
    {   // the event round-trips (world: exact; in a player: the attach grid; the direction to ~1e-4)
        StuckProp w; w.type = WPN_KNIFE; w.owner = 3; w.pos = {12.5f, 0.05f, -7.25f}; w.dir = normalize(Vec3(0.3f, -0.8f, 0.5f));
        StuckProp g;
        CHECK(readPropEvent(propEvent(w), &g) && g.type == WPN_KNIFE && g.owner == 3 && g.player == PROP_WORLD &&
              length(g.pos - w.pos) < 1e-6f && length(g.dir - w.dir) < 1e-3f);
        StuckProp b = w; b.type = WPN_CROSSBOW; b.player = 7; b.pos = {0.21f, 1.33f, -0.1f};
        const GameEvent be = propEvent(b);
        CHECK(be.text.size() == 7 && readPropEvent(be, &g) && g.player == 7 && length(g.pos - quantizeAttachOffset(b.pos)) < 1e-5f);
        GameEvent bad = be; bad.a = WPN_ROCKET;
        CHECK(!readPropEvent(bad, &g));
        bad = be; bad.text.pop_back();
        CHECK(!readPropEvent(bad, &g));
        CHECK(!readPropEvent(propClearEvent(PROP_WORLD), &g));
    }
    Server s;
    ServerConfig cfg;
    cfg.autoStart = false;   // warmup (a match start clears them)
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "thrower");
    b.connect("127.0.0.1:" + std::to_string(port), "target");
    std::vector<NetClient*> cls = {&a, &b};
    pumpUntilSpawned(s, cls);
    const int ia = a.clientId(), ib = b.clientId();
    auto& sa = s.players_[ia];
    auto& sb = s.players_[ib];
    auto place = [&] { sa.move.pos = {10, 0, 19}; sb.move.pos = {4, 0, 19}; sa.move.vel = sb.move.vel = {}; sa.health = sb.health = 100; };
    place();
    // a knife thrown at the floor: the projectile goes, its prop stays (everyone's), the knife still comes back
    auto knife = [&](uint16_t buttons) { return [&, buttons](int i) { UserCmd c; if (i == 0) { c.weapon = WPN_KNIFE; c.pitch = -1.3f; c.yaw = 1.57f; c.buttons = buttons; } return c; }; };
    pump(s, cls, 0.4, knife(0));
    CHECK(sa.weapon == WPN_KNIFE);
    pump(s, cls, 0.02, knife(BTN_ALTFIRE));
    pump(s, cls, 0.6, knife(0));
    bool flying = false;
    for (auto& pr : s.projectiles_) flying |= pr.active && pr.type == WPN_KNIFE;
    CHECK(!flying && sa.knifeBackAt > s.time());
    auto worldProps = [](const NetClient& c, int type) { int n = 0; for (auto& p : c.stuckProps()) n += p.player == PROP_WORLD && p.type == type; return n; };
    CHECK(worldProps(a, WPN_KNIFE) == 1 && worldProps(b, WPN_KNIFE) == 1);
    if (!a.stuckProps().empty()) {
        const StuckProp& k = a.stuckProps().back();
        printf("  knife: stuck at %.2f %.2f %.2f, flying %.2f %.2f %.2f; projectile gone\n", k.pos.x, k.pos.y, k.pos.z, k.dir.x, k.dir.y, k.dir.z);
        CHECK(k.pos.y < 0.3f && length(Vec3(k.pos.x - 10, 0, k.pos.z - 19)) < 2.0f && k.dir.y < -0.5f && k.owner == ia);
    }
    pump(s, cls, 1.0, idle);
    CHECK(worldProps(a, WPN_KNIFE) == 1);   // (still there)
    // a bolt into the target: stuck in them, riding along as they walk, gone when they die (the knife stays)
    place();
    pump(s, cls, 0.2, idle);
    sb.health = 1000;   // (it mustn't kill: a kill takes no prop)
    {
        Projectile& pr = s.projectiles_[0];
        pr = Projectile();
        pr.active = true; pr.id = 4100; pr.type = WPN_CROSSBOW; pr.owner = ia;
        pr.pos = sb.move.pos + Vec3(1.5f, 1.2f, 0); pr.vel = {-weaponDef(WPN_CROSSBOW).projSpeed, 0, 0}; pr.dieAt = s.time() + 5;
    }
    pump(s, cls, 0.3, idle);
    CHECK(!s.projectiles_[0].active && sb.health < 1000 && sb.alive);
    auto inB = [&](const NetClient& c) -> const StuckProp* { for (auto& p : c.stuckProps()) if (p.player == ib && p.type == WPN_CROSSBOW) return &p; return nullptr; };
    CHECK(inB(a) && inB(b));
    if (const StuckProp* p = inB(a)) {
        printf("  bolt: in the target at %.2f %.2f %.2f from their feet\n", p->pos.x, p->pos.y, p->pos.z);
        CHECK(p->pos.x > 0.1f && p->pos.y > 0.9f && p->pos.y < 1.5f && p->dir.x < -0.9f);
    }
    pump(s, cls, 0.5, [&](int i) { UserCmd c; if (i == 1) c.forward = 127; return c; });   // the target walks off
    {
        const StuckProp* p = inB(a);
        ProjectileSnap on; on.flags = PROJ_ON_PLAYER; on.onPlayer = (uint8_t)ib;
        Vec3 at;
        if (p) on.attachOffset = p->pos;
        auto ip = a.interpolatedPlayers();
        CHECK(p && a.attachedPos(on, 0.5f, &at) && ip[ib].valid && length(at - stickyOnPlayerPos(ip[ib].snap.pos, p->pos, false, 1)) < 1e-4f &&
              length(Vec3(ip[ib].snap.pos.x - 4, 0, ip[ib].snap.pos.z - 19)) > 1.0f);
    }
    s.killPlayer(ib, ia, WPN_CROSSBOW);
    pump(s, cls, 0.3, idle);
    CHECK(!inB(a) && !inB(b) && worldProps(a, WPN_KNIFE) == 1 && worldProps(b, WPN_KNIFE) == 1);
    // the cap: the newest kMaxStuckProps (in bursts: a client is dropped past kMaxUnackedEvents unacknowledged)
    for (int k = 0; k < 300; k++) {
        Projectile pr; pr.type = WPN_SYRINGE; pr.owner = ia;
        s.leaveProp(pr, -1, Vec3(1000 + k, 0, 0) * 0.01f, Vec3(0, -1, 0), AABB());
        if (k % 50 == 49) pump(s, cls, 0.15, idle);
    }
    pump(s, cls, 0.3, idle);
    const auto& ap = a.stuckProps();
    printf("  cap: %d props kept of 301, oldest at x %.2f\n", (int)ap.size(), ap.empty() ? 0.0f : ap.front().pos.x);
    CHECK((int)ap.size() == kMaxStuckProps && (int)b.stuckProps().size() == kMaxStuckProps && worldProps(a, WPN_KNIFE) == 0 &&
          std::fabs(ap.front().pos.x - (1000 + 300 - kMaxStuckProps) * 0.01f) < 1e-4f && a.state() == ConnState::Connected && b.state() == ConnState::Connected);
    // a late joiner gets the newest few
    NetClient c;
    c.connect("127.0.0.1:" + std::to_string(port), "late");
    cls.push_back(&c);
    pump(s, cls, 1.5, idle);
    CHECK((int)c.stuckProps().size() == Server::kPropReplay && std::fabs(c.stuckProps().back().pos.x - 12.99f) < 1e-4f);
    // a match restart clears them all, for everyone
    s.command("restart");
    pump(s, cls, 0.3, idle);
    CHECK(a.stuckProps().empty() && b.stuckProps().empty() && c.stuckProps().empty());
    s.stop();
}

// A sticky that hits an enemy sticks to them (owner: "the sticky should stick to players if it hits them"): it rides
// along on them, the clients draw it on the player as they show them, alt-fire sets it off on them for the full blast.
// Never on its owner; a teammate isn't stuck to (it drops off them, as before); when the one it's on dies it stays
// where they were (falls, sticks to the floor). The wire's new projectile bit round-trips.
static void testStickyOnPlayers(uint16_t port) {
    printf("stickies stick to enemies\n");
    {   // the wire: bit 32 + player + offset, full and as a delta; one without it reads as before
        Snapshot s;
        s.ms.mode = MODE_FFA;
        ProjectileSnap p; p.id = 7; p.type = WPN_STICKY; p.owner = 1; p.pos = {1, 2, 3};
        p.flags = PROJ_STUCK | PROJ_ON_PLAYER; p.onPlayer = 5; p.attachOffset = quantizeAttachOffset({0.31f, 1.27f, -0.4f});
        ProjectileSnap plain; plain.id = 8; plain.type = WPN_STICKY; plain.owner = 1; plain.pos = {4, 5, 6}; plain.flags = PROJ_STUCK;
        s.projectiles = {p, plain};
        std::vector<uint8_t> b(4096);
        Writer w(b.data(), (int)b.size());
        Snapshot sent = s;
        writeSnapshotWorld(w, sent, nullptr);
        Reader r(b.data(), w.size());
        Snapshot got;
        CHECK(readSnapshotWorld(r, got, nullptr) && got.projectiles.size() == 2);
        if (got.projectiles.size() == 2) {
            const ProjectileSnap& g = got.projectiles[0];
            CHECK((g.flags & PROJ_ON_PLAYER) && (g.flags & PROJ_STUCK) && g.onPlayer == 5 && length(g.attachOffset - p.attachOffset) < 1e-5f);
            CHECK(!(got.projectiles[1].flags & PROJ_ON_PLAYER) && length(got.projectiles[1].pos - plain.pos) < 0.02f);
            Writer w2(b.data(), (int)b.size());   // a delta against what the client has: the attachment comes again
            Snapshot again = s;
            writeSnapshotWorld(w2, again, &got);
            Reader r2(b.data(), w2.size());
            Snapshot got2;
            CHECK(readSnapshotWorld(r2, got2, &got) && got2.projectiles.size() == 2 && (got2.projectiles[0].flags & PROJ_ON_PLAYER) &&
                  got2.projectiles[0].onPlayer == 5 && length(got2.projectiles[0].attachOffset - p.attachOffset) < 1e-5f);
        }
        Snapshot bad = s;   // a player id out of range is refused
        bad.projectiles[0].onPlayer = kMaxPlayers;
        Writer w3(b.data(), (int)b.size());
        writeSnapshotWorld(w3, bad, nullptr);
        Reader r3(b.data(), w3.size());
        Snapshot got3;
        CHECK(!readSnapshotWorld(r3, got3, nullptr));
        // the offset on the body: crouching squashes it with the hull, size scales it
        Vec3 o = quantizeAttachOffset({0.2f, 1.5f, 0});
        CHECK(std::fabs(stickyOnPlayerPos({0, 0, 0}, o, true, 1).y - o.y * kPlayerCrouchHeight / kPlayerHeight) < 1e-5f);
        CHECK(length(stickyOnPlayerPos({1, 0, 0}, o, false, 2) - (Vec3(1, 0, 0) + o * 2)) < 1e-5f);
    }
    Server s;
    ServerConfig cfg;
    cfg.autoStart = false;   // warmup: a match start would clear the projectiles and move everyone
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "shooter");
    b.connect("127.0.0.1:" + std::to_string(port), "target");
    std::vector<NetClient*> cls = {&a, &b};
    pumpUntilSpawned(s, cls);
    const int ia = a.clientId(), ib = b.clientId();
    auto& sa = s.players_[ia];
    auto& sb = s.players_[ib];
    auto place = [&] { sa.move.pos = {10, 0, 19}; sb.move.pos = {2, 0, 19}; sa.move.vel = sb.move.vel = {}; sa.health = sb.health = 100; };
    place();
    pump(s, cls, 0.3, idle);
    auto aim = [&](int i, uint16_t buttons) {
        UserCmd c;
        if (i != 0) return c;
        Vec3 eye = a.predicted().pos + Vec3(0, kEyeHeight, 0), tgt = sb.move.pos + Vec3(0, 1.0f, 0);
        const WeaponDef& wd = weaponDef(WPN_STICKY);
        float t = length(tgt - eye) / wd.projSpeed;
        tgt.y += 0.5f * wd.gravity * t * t;
        Vec3 d = normalize(tgt - eye);
        c.pitch = std::asin(d.y); c.yaw = std::atan2(-d.x, -d.z);
        c.weapon = WPN_STICKY;
        c.buttons = buttons;
        return c;
    };
    pump(s, cls, 0.3, [&](int i) { return aim(i, 0); });
    CHECK(sa.weapon == WPN_STICKY);
    auto findSticky = [&]() -> Projectile* { for (auto& pr : s.projectiles_) if (pr.active && pr.type == WPN_STICKY) return &pr; return nullptr; };
    pump(s, cls, 0.05, [&](int i) { return aim(i, BTN_FIRE); });
    for (int k = 0; k < 100 && !(findSticky() && findSticky()->stuck); k++) pump(s, cls, 0.01, idle);
    Projectile* st = findSticky();
    CHECK(st && st->stuck && st->onPlayer == ib);
    if (!st || st->onPlayer != ib) { s.stop(); return; }
    printf("  hit: on the target at %.2f %.2f %.2f from their feet, %.0f hp\n", st->playerOffset.x, st->playerOffset.y, st->playerOffset.z, sb.health);
    CHECK(sb.health == 100);   // no damage of its own
    CHECK(s.hitBox(sb.move.pos, (sb.move.flags & MF_CROUCHED) != 0).contains(st->pos));
    // it rides along: the target walks off
    Vec3 from = st->pos;
    pump(s, cls, 0.5, [&](int i) { UserCmd c; if (i == 1) c.forward = 127; else c.weapon = WPN_STICKY; return c; });
    Vec3 want = stickyOnPlayerPos(sb.move.pos, st->playerOffset, (sb.move.flags & MF_CROUCHED) != 0, s.bodyScale());
    printf("  followed %.2f m (target moved), off by %.4f\n", length(st->pos - from), length(st->pos - want));
    CHECK(length(st->pos - from) > 1.0f && length(st->pos - want) < 1e-4f && st->stuck);
    // the clients draw it on the player as they show them, still walking
    pump(s, cls, 0.2, [&](int i) {
        UserCmd c; if (i == 1) c.forward = 127; else c.weapon = WPN_STICKY;
        if (i == 1) {   // (each client tick: checked while it moves)
            for (const ProjectileSnap& p : a.interpolatedProjectiles()) {
                if (p.id != st->id) continue;
                auto ip = a.interpolatedPlayers();
                bool okA = (p.flags & PROJ_ON_PLAYER) && p.onPlayer == ib && length(p.attachOffset - st->playerOffset) < 1e-5f && ip[ib].valid &&
                           length(p.pos - stickyOnPlayerPos(ip[ib].snap.pos, p.attachOffset, false, 1)) < 1e-4f;
                if (!okA) { printf("  FAIL %s:%d: owner's view of the sticky\n", __FILE__, __LINE__); g_fail++; }
            }
            for (const ProjectileSnap& p : b.interpolatedProjectiles()) {
                if (p.id != st->id) continue;
                Vec3 at;
                bool okB = b.attachedPos(p, 0.5f, &at) && length(at - stickyOnPlayerPos(b.smoothedPos(0.5f), p.attachOffset, false, 1)) < 1e-4f;
                if (!okB) { printf("  FAIL %s:%d: the victim's view of the sticky\n", __FILE__, __LINE__); g_fail++; }
            }
        }
        return c;
    });
    int seenA = 0, seenB = 0;
    for (const ProjectileSnap& p : a.interpolatedProjectiles()) seenA += p.id == st->id;
    for (const ProjectileSnap& p : b.interpolatedProjectiles()) seenB += p.id == st->id && p.onPlayer == ib;
    CHECK(seenA == 1 && seenB == 1);
    // set off: the full blast, centred on them
    place();
    pump(s, cls, 0.1, idle);
    float hp = sb.health;
    int d0 = sb.deaths;
    pump(s, cls, 0.1, [&](int i) { UserCmd c; if (i == 0) { c.weapon = WPN_STICKY; c.buttons = BTN_ALTFIRE; } return c; });
    float dealt = sb.deaths > d0 ? hp : hp - sb.health;
    printf("  set off: %.1f damage (full: %.1f)\n", dealt, weaponDef(WPN_STICKY).damage);
    CHECK(!findSticky() && dealt >= weaponDef(WPN_STICKY).damage - 0.5f);
    CHECK(sa.health == 100);   // (8 m away: out of its 3.4 m blast)
    // never on its owner: one dropped onto the owner's head goes through and sticks to the floor
    place();
    pump(s, cls, 0.1, idle);
    auto drop = [&](int owner, Vec3 at, Vec3 vel) -> Projectile& {
        Projectile& pr = s.projectiles_[0];
        pr = Projectile();
        pr.active = true; pr.id = 4000; pr.type = WPN_STICKY; pr.owner = owner;
        pr.pos = at; pr.vel = vel; pr.dieAt = s.time() + 20; pr.armAt = s.time();
        return pr;
    };
    Projectile& own = drop(ia, sa.move.pos + Vec3(0, 2.2f, 0), {0, -5, 0});
    pump(s, cls, 0.8, idle);
    CHECK(own.active && own.stuck && own.onPlayer < 0 && own.pos.y < 0.2f);
    // a sticky on a player who dies stays where they were: it falls to the floor there and sticks
    Projectile& onB = drop(ia, sb.move.pos + Vec3(0, 2.2f, 0), {0, -5, 0});
    for (int k = 0; k < 20 && !onB.stuck; k++) pump(s, cls, 0.05, idle);   // (until it lands: a loaded machine runs fewer ticks)
    CHECK(onB.active && onB.stuck && onB.onPlayer == ib && onB.pos.y > 1.5f);
    Vec3 died = sb.move.pos;
    s.killPlayer(ib, -1, 255);
    CHECK(onB.active && onB.onPlayer < 0 && !onB.stuck);
    pump(s, cls, 0.8, idle);
    printf("  victim died: the sticky lies at %.2f %.2f %.2f (died at %.2f %.2f %.2f)\n", onB.pos.x, onB.pos.y, onB.pos.z, died.x, died.y, died.z);
    CHECK(onB.active && onB.stuck && onB.onPlayer < 0 && onB.pos.y < 0.2f && length(Vec3(onB.pos.x - died.x, 0, onB.pos.z - died.z)) < 0.5f);
    for (int k = 0; k < 60 && !sb.alive; k++) pump(s, cls, 0.1, idle);   // respawned elsewhere: it doesn't come along
    CHECK(sb.alive && onB.active && length(Vec3(onB.pos.x - died.x, 0, onB.pos.z - died.z)) < 0.5f);
    // chain reaction: a blast near where the player is now sets off the one on them (not where it was last tick)
    place();
    pump(s, cls, 0.1, idle);
    Projectile& chain = drop(ia, sb.move.pos + Vec3(0, 2.2f, 0), {0, -5, 0});
    pump(s, cls, 0.2, idle);
    CHECK(chain.onPlayer == ib);
    sb.move.pos = sb.move.pos + Vec3(0, 0, 3);   // (moved since the projectiles' tick)
    s.explodeForTest(sb.move.pos + Vec3(3.0f, 1.0f, 0), -1, WPN_ROCKET);
    CHECK(!chain.active);
    s.stop();

    // team modes: a teammate isn't stuck to (it drops off them, as it always did); an enemy is
    Server t;
    cfg.mode = MODE_KOTH; cfg.port = (uint16_t)(port + 1);
    { std::string e_; if (!t.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    t.setLog([](const std::string&) {});
    NetClient c1, c2, c3;
    c1.connect("127.0.0.1:" + std::to_string(cfg.port), "a");
    c2.connect("127.0.0.1:" + std::to_string(cfg.port), "mate");
    c3.connect("127.0.0.1:" + std::to_string(cfg.port), "enemy");
    std::vector<NetClient*> tc = {&c1, &c2, &c3};
    pump(t, tc, 1.0, idle);
    const int mine = t.players_[c1.clientId()].team, theirs = mine == TEAM_A ? TEAM_B : TEAM_A;
    c2.sendCommand(std::string("team ") + (mine == TEAM_A ? "a" : "b"));
    c3.sendCommand(std::string("team ") + (theirs == TEAM_A ? "a" : "b"));
    pump(t, tc, 2.5, idle);
    auto& p1 = t.players_[c1.clientId()];
    auto& p2 = t.players_[c2.clientId()];
    auto& p3 = t.players_[c3.clientId()];
    CHECK(p1.team == p2.team && p3.team != p1.team && p1.alive && p2.alive && p3.alive);
    p2.move.pos = {2, 0, 19}; p3.move.pos = {-4, 0, 19}; p2.move.vel = p3.move.vel = {};
    pump(t, tc, 0.1, idle);
    auto shoot = [&](int at) -> Projectile& {   // from 1 m to the side of them, chest high, straight in
        Projectile& pr = t.projectiles_[at];
        const Vec3 feet = t.players_[at == 0 ? c2.clientId() : c3.clientId()].move.pos;
        pr = Projectile();
        pr.active = true; pr.id = (uint16_t)(5000 + at); pr.type = WPN_STICKY; pr.owner = c1.clientId();
        pr.pos = feet + Vec3(1.2f, 1.0f, 0); pr.vel = {-20, 0, 0}; pr.dieAt = t.time() + 20; pr.armAt = t.time();
        return pr;
    };
    Projectile& onMate = shoot(0);
    Projectile& onEnemy = shoot(1);
    pump(t, tc, 0.8, idle);
    printf("  teammate: %s, %s %.2f m up; enemy: %s\n", onMate.onPlayer < 0 ? "not stuck to" : "STUCK TO", onMate.stuck ? "on the floor" : "falling", onMate.pos.y, onEnemy.onPlayer == c3.clientId() ? "stuck to" : "NOT STUCK TO");
    CHECK(onMate.active && onMate.onPlayer < 0 && onMate.stuck && onMate.pos.y < 0.2f);
    CHECK(onEnemy.active && onEnemy.onPlayer == c3.clientId());
    t.stop();
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
        pumpUntilSpawned(s, cls);
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
    b.sendCommand("callvote map summit koth");   // a legacy map (1.19.6): not votable while the old maps aren't listed (owner, 2026-10-31)
    pump(s, cls, 0.3, idle);
    CHECK(!legacyMapsListed() && isLegacyMap("summit") && s.vote_.kind == Server::VOTE_NONE);
    b.sendCommand("callvote map glacier koth");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_MAP);
    a.sendCommand("vote yes");
    pump(s, cls, 1.0, idle);
    CHECK(s.config().map == "glacier" && s.config().mode == MODE_KOTH);
    // every map / mode pair the vote menu offers must be accepted
    for (auto& name : listMaps()) {
        Map m;
        if (!m.load(name)) continue;
        for (int k = 0; k < MODE_COUNT; k++) {
            if (!m.supportsMode(k)) continue;
            b.sendCommand(std::string("callvote map ") + name + " " + modeName(k) + (k == MODE_ARENA || k == MODE_TOURNEY ? " classic" : ""));
            for (int i = 0; i < 20 && s.vote_.kind != Server::VOTE_MAP; i++) pump(s, cls, 0.01, idle);   // (up to 0.2 s)
            if (s.vote_.kind != Server::VOTE_MAP) { printf("  FAIL vote refused: %s %s\n", name.c_str(), modeName(k)); g_fail++; }
            a.sendCommand("vote no"); b.sendCommand("vote no");
            for (int i = 0; i < 20 && s.vote_.kind == Server::VOTE_MAP; i++) pump(s, cls, 0.01, idle);
            s.vote_.kind = Server::VOTE_NONE;
            s.players_[b.clientId()].voteAt = 0;   // (a failed vote's caller waits before the next: not what this checks)
        }
    }
    s.stop();
}

// The vote builder's modifiers (owner, 2026-11-01): "callvote map <map> <mode> <ruleset> mods k=v ..." passes as a
// custom mode ("voted") everyone gets; a mode vote without "mods" keeps them; "mods" alone goes back to the standard
// rules; unknown or out-of-range words, and tourney with modifiers, are refused.
static void testVoteModifiers(uint16_t port) {
    printf("vote modifiers: one vote for map, mode and rules\n");
    Server s;
    s.voteCooldown_ = 0;
    ServerConfig cfg;
    cfg.map = "glacier"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 1.0, idle);
    auto refused = [&](const std::string& c) {
        b.sendCommand(c);
        pump(s, cls, 0.3, idle);
        const bool r = s.vote_.kind == Server::VOTE_NONE;
        if (!r) { printf("  FAIL not refused: %s\n", c.c_str()); g_fail++; s.vote_ = Server::Vote(); }
        s.players_[b.clientId()].voteAt = 0;
    };
    auto pass = [&](const std::string& c) {
        b.sendCommand(c);
        pump(s, cls, 0.3, idle);
        CHECK(s.vote_.kind == Server::VOTE_MAP);
        a.sendCommand("vote yes");
        pump(s, cls, 1.0, idle);
        CHECK(s.vote_.kind == Server::VOTE_NONE);
        s.players_[b.clientId()].voteAt = 0;
    };
    refused("callvote map glacier ffa classic mods foo=1");
    refused("callvote map glacier ffa classic mods vampire=2");
    refused("callvote map glacier ffa classic mods damage=abc");
    refused("callvote map glacier ffa classic damage=2");   // (no "mods" word)
    refused("callvote map dojo tourney classic mods damage=2");
    b.sendCommand("callvote map glacier ffa classic mods damage=2 vampire=1 health=150 dash=0");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_MAP && s.vote_.text.find("DAMAGE X2") != std::string::npos && s.vote_.text.find("VAMPIRE") != std::string::npos &&
          s.vote_.text.find("NO DASH") != std::string::npos);
    a.sendCommand("vote yes");
    pump(s, cls, 1.0, idle);
    const CustomRules& r = s.config().custom;
    CHECK(r.active() && r.name == Server::kVotedRules && r.baseMode == MODE_FFA && r.vampire && !r.dash && r.health == 150 &&
          std::fabs(r.damage[WPN_ROCKET] - 2) < 1e-4f && std::fabs(r.damage[0] - 2) < 1e-4f);
    CHECK(activeRules().active() && activeRules().vampire);
    printf("  passed: %s\n", votedModsTitle("damage=2\nvampire=1\nhealth=150\ndash=0\n").c_str());
    pass("callvote map glacier koth classic");   // a mode vote without modifiers keeps them
    CHECK(s.config().mode == MODE_KOTH && s.config().custom.active() && s.config().custom.baseMode == MODE_KOTH && s.config().custom.vampire);
    pass("callvote map glacier ffa classic mods");   // back to the standard rules
    CHECK(s.config().mode == MODE_FFA && !s.config().custom.active() && !activeRules().active());
    s.stop();
    setActiveRules(CustomRules());
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

// The spectate vote (OWNER_DECISIONS_2 B10): "callvote spec <player>" moves a player to spectator, by the usual vote
// rules; it needs 3+ players, the player it's about has no vote, they can't rejoin play until the next map (not by
// leaving and coming back either), and each caller gets one vote per player per map. Kicking stays admin-only: a
// player's "kick" does nothing and there's no kick vote.
static void testSpecVote(uint16_t port) {
    printf("spectate vote: 3+ players, target has no vote, out until the next map; kick votes (1.19.4)\n");
    Server s;
    s.voteCooldown_ = 0;   // (a failed vote's cooldown isn't what this checks)
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port; cfg.balanceBots = false;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    std::string addr = "127.0.0.1:" + std::to_string(port);
    NetClient a, b, c;
    a.connect(addr, "anna"); b.connect(addr, "bert");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 0.8, idle);
    a.sendCommand("callvote spec bert");   // two players: not available (a 1v1 never has it)
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE);
    c.connect(addr, "carl");
    cls = {&a, &b, &c};
    pump(s, cls, 0.8, idle);
    int ia = a.clientId(), ib = b.clientId(), ic = c.clientId();
    int bot = s.addBot(TEAM_A, BOT_BEGINNER);
    CHECK(bot >= 0);
    a.sendCommand("callvote spec anna");                         // not yourself
    a.sendCommand("callvote spec " + std::to_string(bot));       // not a bot
    a.sendCommand("callvote spec nobody");
    a.sendCommand("kick carl");                                  // a player can't kick (callvote kick: tested at the end)
    pump(s, cls, 0.4, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE && s.players_[ic].connected);
    a.sendCommand("callvote spec carl");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_SPEC && s.vote_.arg == ic);
    c.sendCommand("vote no");                                    // carl has no say
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_SPEC && !(s.vote_.no >> ic & 1) && s.ms_.voteVoters == 2);
    b.sendCommand("vote yes");
    pump(s, cls, 0.4, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE);
    CHECK(s.players_[ic].team == TEAM_SPECTATOR && s.players_[ic].specLocked && !s.players_[ic].alive);
    c.sendCommand("team a");                                     // can't rejoin play this map
    pump(s, cls, 0.5, idle);
    CHECK(s.players_[ic].team == TEAM_SPECTATOR && !s.players_[ic].alive);
    // leaving and coming back as a new connection: still spectating
    c.disconnect();
    cls = {&a, &b};
    pump(s, cls, 0.2, idle);
    s.players_[ic].awayUntil = timeNow() - 1;   // (the reconnect grace is over)
    pump(s, cls, 0.2, idle);
    CHECK(!s.players_[ic].connected);
    NetClient c2;
    c2.connect(addr, "carl");
    cls = {&a, &b, &c2};
    pump(s, cls, 0.8, idle);
    int ic2 = c2.clientId();
    CHECK(s.players_[ic2].connected && s.players_[ic2].team == TEAM_SPECTATOR && s.players_[ic2].specLocked);
    // one spectate vote per caller per player per map: anna's on bert fails (carl says no), and she can't call it again
    a.sendCommand("callvote spec bert");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_SPEC && s.vote_.arg == ib);
    c2.sendCommand("vote no");
    pump(s, cls, 0.4, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE && s.players_[ib].team != TEAM_SPECTATOR);
    a.sendCommand("callvote spec bert");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE);
    // the next map: carl plays again, and anna may call the vote on bert again
    s.command("map bastion ffa");
    pump(s, cls, 0.5, idle);
    CHECK(!s.players_[ic2].specLocked);
    c2.sendCommand("team a");
    pump(s, cls, 0.5, idle);
    CHECK(s.players_[ic2].team == TEAM_A);
    a.sendCommand("callvote spec bert");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_SPEC && s.vote_.arg == ib);
    CHECK(s.players_[ia].connected && s.players_[ib].connected);
    c2.sendCommand("vote no");
    pump(s, cls, 0.4, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE);
    // 1.19.4: a vote to kick (not a bot, not yourself; the player it's about has no say), and 5 more minutes
    a.sendCommand("callvote kick " + std::to_string(bot));
    a.sendCommand("callvote kick anna");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE);
    a.sendCommand("callvote kick carl");
    pump(s, cls, 0.3, idle);
    CHECK(s.vote_.kind == Server::VOTE_KICK && s.vote_.arg == ic2 && s.ms_.voteVoters == 2);
    c2.sendCommand("vote no");
    b.sendCommand("vote yes");
    pump(s, cls, 0.5, idle);
    CHECK(s.vote_.kind == Server::VOTE_NONE && !s.players_[ic2].connected);
    cls = {&a, &b};
    const float before = s.ms_.timeLeft;
    const bool timed = s.ms_.phase == PHASE_LIVE && s.phaseEnd_ > 0;
    a.sendCommand("callvote extend");
    pump(s, cls, 0.3, idle);
    if (!timed) CHECK(s.vote_.kind == Server::VOTE_NONE);   // (only a timed live round)
    else {
        CHECK(s.vote_.kind == Server::VOTE_EXTEND);
        b.sendCommand("vote yes");
        pump(s, cls, 0.4, idle);
        CHECK(s.vote_.kind == Server::VOTE_NONE && s.ms_.timeLeft > before + 250);
    }
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
    CHECK(a.map().id() == s.map_.id() && a.map().brushes.size() == base.brushes.size() + 1);
    std::string dl = downloadedMapPath(s.map_.id());
    FILE* f = fopen(dl.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) fclose(f);
    // a second connection uses the saved download instead of fetching again
    NetClient b;
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    cls.push_back(&b);
    pump(s, cls, 0.2, idle);
    CHECK(b.mapReady() && b.downloadProgress() < 0 && b.map().id() == s.map_.id());
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
        CHECK(m.load("qk1"));
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
        cfg.map = "qk1"; cfg.mode = MODE_FFA; cfg.port = (uint16_t)(port + variant); cfg.ranked = true; cfg.arenaVariant = variant;
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
    cfg.map = "qk1"; cfg.mode = MODE_ARENA; cfg.port = port; cfg.ranked = true; cfg.arenaVariant = 0;
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
    CHECK(s.config().map == "qk1" && s.players_[ib].connected && s.config().ranked);
    // the leaver's connection drops: slot kept, and a reconnect within the grace gets it back
    int scoreB = s.players_[ib].score = 2;
    b.disconnect();
    pump(s, cls, 0.3, idle);
    CHECK(s.away(ib) && s.players_[ib].connected && s.rankedLive() && s.ms_.phase != PHASE_MATCH_END);
    b.connect(addr, "leaver");
    pump(s, cls, 1.0, idle);
    CHECK(b.state() == ConnState::Connected && b.clientId() == ib && !s.away(ib) && s.players_[ib].score == scoreB && s.rankedLive());
    // gone past the grace: the first leaver loses, the match ends there, and the stayer can report it. (Each grace here
    // is cut short once the drop is seen: testSession waits one out.)
    b.disconnect();
    std::vector<NetClient*> onlyA = {&a};
    for (int i = 0; i < 50 && !s.away(ib); i++) pump(s, onlyA, 0.01, idle);
    CHECK(s.away(ib));
    s.players_[ib].awayUntil = timeNow();
    std::string ranked;
    double end = timeNow() + 2.0;
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
    for (int i = 0; i < 50 && s.players_[ia].connected && !s.away(ia); i++) pump(s, onlyA, 0.01, idle);
    if (s.away(ia)) s.players_[ia].awayUntil = timeNow();
    pump(s, onlyA, 0.5, idle);
    CHECK(!s.players_[ia].connected && s.rankedLeft_.size() == 1);
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
        cfg.map = "qk1"; cfg.mode = MODE_ARENA; cfg.port = port;
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
            CHECK(s.ownAddrHint() == relayAddr);   // (C9: the host screen shows it, with --own-addr)
        } else CHECK(s.players_[id].rankId == kv.id && s.players_[id].dialled.toString() == relayAddr && s.ownAddrHint().empty());
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
    cfg.map = "spire"; cfg.mode = MODE_FFA; cfg.port = port; cfg.ranked = true; cfg.arenaVariant = 5;   // instagib FFA: goes on after a leaver
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
// Name colour (1.17): sent with the model command (RGB565), seen by everyone, by a late joiner and in a demo's player info
static void testNameColor(uint16_t port) {
    printf("name colour\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port; cfg.balanceBots = false;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    NetClient h, b;
    std::string addr = "127.0.0.1:" + std::to_string(port);
    h.connect(addr, "Host"); b.connect(addr, "Bob");
    std::vector<NetClient*> cls = {&h, &b};
    pump(s, cls, 0.8, idle);
    int ih = h.clientId(), ib = b.clientId();
    CHECK(ih >= 0 && ib >= 0 && b.playerNameColor(ih) == 0);
    h.sendCommand("model 0 0 63488");   // pure red
    b.sendCommand("model 0 0");         // a client before 1.17: no colour, the default
    pump(s, cls, 0.5, idle);
    CHECK(b.playerNameColor(ih) == 63488 && h.playerNameColor(ih) == 63488 && h.playerNameColor(ib) == 0);
    NetClient c;
    c.connect(addr, "Carol");
    cls = {&h, &b, &c};
    pump(s, cls, 0.8, idle);
    CHECK(c.playerNameColor(ih) == 63488);   // a late joiner is told too
    h.sendCommand("model 0 0 70000");        // out of range: back to the default
    pump(s, cls, 0.5, idle);
    CHECK(b.playerNameColor(ih) == 0 && c.playerNameColor(ih) == 0);
    for (auto* k : cls) k->disconnect();
    pump(s, cls, 0.2, idle);
    s.stop();
}

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
    // the retries 0.5, 1, 2 s apart, not 10, 20, 40 (real seconds; set before the service's thread starts)
    RankService::helloRetryScale = 0.05;
    {
        RankService rs;
        rs.hello(k);
        // (a loaded box can be slow to start curl: up to 90 s)
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (std::chrono::steady_clock::now() < end && !rs.helloDone()) sleepMs(20);
        if (!rs.helloDone() || requests != 2) printf("  hello: done %d after %d requests\n", (int)rs.helloDone(), requests.load());
        CHECK(rs.helloDone() && requests == 2);   // refused once, then taken
        sleepMs(2000);                             // (twice the next retry's delay)
        CHECK(requests == 2);                      // and not sent again
    }
    RankService::helloRetryScale = 1.0;
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

// The browser's OFFICIAL tag: only the built-in addresses of the official servers, matched exactly (by the address the
// client talks to, never the name a server reports), so a lookalike address or a server named "official" isn't.
static void testOfficialServers() {
    printf("official servers\n");
    CHECK(kOfficialServerCount == 2);
    CHECK(isOfficialServer("75.127.7.139:27500"));   // production
    CHECK(isOfficialServer("75.127.7.139:27501"));   // testing
    const uint32_t ip = 75u << 24 | 127u << 16 | 7u << 8 | 139u;
    CHECK(isOfficialServer(ip, 27500) && isOfficialServer(ip, 27501));
    CHECK(!isOfficialServer(ip, 27502) && !isOfficialServer(ip + 1, 27500) && !isOfficialServer(0, 0));
    NetAddr a;   // what the browser holds: NetAddr's parse and toString agree with it
    CHECK(NetAddr::parse("75.127.7.139:27501", kDefaultPort, a) && isOfficialServer(a.ip, a.port) && isOfficialServer(a.toString()));
    for (const char* fake : {"75.127.7.139:27502", "75.127.7.13:27500", "75.127.7.1390:27500", "175.127.7.139:27500",
                             "75.127.7.139:2750", "75.127.7.139:275000", "75.127.7.139", "75.127.7.139:", "075.127.7.139:27500",
                             "75.127.7.139:027500", "75.127.7.139.1:27500", "75.127.7:27500", " 75.127.7.139:27500",
                             "75.127.7.139:27500 ", "75.127.7.139:27500x", "official", "OFFICIAL 75.127.7.139:27500",
                             "127.0.0.1:27500", "129.151.8.15:27500", ""})
        CHECK(!isOfficialServer(std::string(fake)));
}

// REVIEW 2.3: a dedicated server (src/server/main.cpp: frame() and idle(), never run()) handles ranking server
// replies: a failed race ticket clears and the racer is told. And replies nobody takes are capped.
static void testMasterReplies(uint16_t port) {
    printf("ranking server replies on a dedicated server\n");
    if (rankedRaceMaps().empty()) { CHECK(!"no ranked race map"); return; }
    Server s;
    ServerConfig cfg;
    cfg.map = rankedRaceMaps()[0].name; cfg.mode = rankedRaceMaps()[0].mode; cfg.port = port;   // (a time trial that counts: its ticket goes out)
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
        // (no list configured: each fails at once. Sent when the queue is empty, answered a moment later)
        for (int k = 0; k < 1000 && m.queuedPosts(); k++) sleepMs(2);
        sleepMs(10);
    }
    auto r = m.takeReplies();
    m.stop();
    printf("  %zu replies kept of 80\n", r.size());
    CHECK(!r.empty() && r.size() <= MasterAnnouncer::kMaxReplies);
    if (!r.empty() && r.back().first == "k79") CHECK(r.size() == MasterAnnouncer::kMaxReplies && r.front().first == "k16");   // (all done: the newest 64)
}

// The live ranked lock (no votes, no map / mode changes, no team switches) holds for exactly the three rated ladders:
// 1v1 vanilla, 1v1 laser vampire and instagib free for all (JUDGMENT_CALLS C15). Not the time trial, nor any other mode
// or ruleset, even with a match id open.
static void testRankedLockLadders() {
    printf("ranked lock: exactly 1v1 vanilla, 1v1 laser vampire, instagib ffa\n");
    int locked = 0;
    for (int v = 0; v < kArenaVariants; v++)
        for (int mode = 0; mode < MODE_COUNT; mode++) {
            Server s;
            s.cfg_.ranked = true;
            s.cfg_.arenaVariant = v;
            s.cfg_.mode = mode;
            s.rankedMatch_ = "0123456789abcdef";
            s.ms_.phase = PHASE_LIVE;
            bool ladder = (v == 0 && mode == MODE_ARENA) || (v == 1 && mode == MODE_ARENA) || (v == 5 && mode == MODE_FFA);
            CHECK(arenaVariantRanked(v) == (v == 0 || v == 1 || v == 5));
            CHECK(s.rankedLive() == ladder);
            locked += s.rankedLive();
            s.ms_.phase = PHASE_WARMUP;
            CHECK(!s.rankedLive());
        }
    CHECK(locked == 3);
    Server t;   // a race time trial: best times, no rating, never locked
    t.cfg_.timeTrial = true; t.cfg_.mode = MODE_RACE; t.cfg_.map = "accelerator";
    t.rankedMatch_ = "0123456789abcdef"; t.ms_.phase = PHASE_LIVE;
    CHECK(!t.rankedLive());
    t.cfg_.ranked = true;
    CHECK(!t.rankedLive());
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
    CHECK(a.map().id() == s.map_.id() && s.map_.brushes.size() == disk.brushes.size() + 1);
    b.sendCommand("callvote edit off");
    pump(s, cls, 0.2, idle);
    a.sendCommand("vote yes");
    pump(s, cls, 0.5, idle);
    CHECK(!s.editing_ && s.mapEdited_ && s.map_.brushes.size() == disk.brushes.size() + 1 && !(a.mode().flags & MS_EDITING));
    Map after;
    CHECK(after.load(s.config().map) && after.id() == disk.id());   // the file was never touched
    s.command("map " + s.config().map);
    pump(s, cls, 0.5, idle);
    CHECK(s.map_.id() == disk.id() && a.map().id() == disk.id() && !s.mapEdited_); // back to the original
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
    CHECK(a.map().id() == s.map_.id() && b.map().id() == s.map_.id());
    CHECK(a.map().brushes.size() == nb + 1 && b.map().lights.size() == s.map_.lights.size());
    // a late joiner (its disk copy is the old file) gets the edited map
    NetClient c;
    c.connect("127.0.0.1:" + std::to_string(port), "c");
    cls.push_back(&c);
    a.sendCommand("ed t Edited Arena");
    pump(s, cls, 1.5, idle);
    CHECK(c.mapReady() && c.map().id() == s.map_.id() && c.map().title == "Edited Arena" && a.map().title == "Edited Arena");
    // a burst of edits while c is connected
    for (int k = 0; k < 20; k++) b.sendCommand("ed p ins " + std::to_string(k) + " " + std::to_string(k) + " 0 0");
    pump(s, cls, 1.0, idle);
    CHECK(a.map().id() == s.map_.id() && b.map().id() == s.map_.id() && c.map().id() == s.map_.id());
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
        // (waits until it's through rather than for a fixed wall-clock window: a loaded machine only makes it slower)
        double t0 = timeNow();
        uint32_t tick0 = s.timeMs_;
        bool done = false;
        while (!done && timeNow() - t0 < 60.0) {
            pump(s, cls, 0.02, idle);
            done = s.map_.groups.size() == groupsBefore + 1 && s.map_.groups.back().parts.size() == 200 && b.pendingCommands() == 0;
        }
        printf("  200-part group: %.0f ms (%llu ms game time)\n", (timeNow() - t0) * 1000, (unsigned long long)(s.timeMs_ - tick0));
        CHECK(done);
        pump(s, cls, 0.3, idle);
        CHECK(a.map().id() == s.map_.id() && b.map().id() == s.map_.id() && c.map().id() == s.map_.id());
        CHECK(c.map().brushes.size() == s.map_.brushes.size() && c.map().gridRefs == s.map_.gridRefs);
        b.sendCommand("ed g del " + std::to_string(groupsBefore));   // (back as it was for the checks below)
        pump(s, cls, 0.3, idle);
        CHECK(s.map_.groups.size() == groupsBefore && c.map().id() == s.map_.id());
    }
    // switch to playing it: autosaved first, everyone spawns, then back to editing
    s.command("editmode off");
    pump(s, cls, 7.0, idle);
    CHECK(!(a.mode().flags & MS_EDITING) && !s.editing_);
    Map saved;
    CHECK(saved.load(name) && saved.id() == s.map_.id() && saved.title == "Edited Arena" && saved.brushes.size() == nb + 1);
    CHECK(a.map().id() == s.map_.id());
    bool anyAlive = false;
    for (auto& p : s.players_) anyAlive |= p.connected && p.alive;
    CHECK(anyAlive);
    s.command("editmode on");
    pump(s, cls, 0.5, idle);
    CHECK((a.mode().flags & MS_EDITING) && s.editing_);
    for (auto& p : s.players_) CHECK(!p.alive);
    a.sendCommand("ed b del 0");
    pump(s, cls, 0.5, idle);
    CHECK(c.map().id() == s.map_.id() && c.map().brushes.size() == nb);
    // the host names the map in the session (the editor's SAVE AS): saved, and autosaved, under the typed name
    std::string name2 = name + "_renamed", path2 = userMapsDir() + "/" + name2 + ".qmap";
    s.command("editmode saveas ../evil");
    CHECK(s.cfg_.map == name);
    s.command("editmode saveas " + name2);
    Map renamed;
    CHECK(s.cfg_.map == name2 && renamed.load(name2) && renamed.id() == s.map_.contentId() && renamed.brushes.size() == nb);
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
    for (const MapId& id : {a.map().id(), c.map().id()}) std::remove(downloadedMapPath(id).c_str());
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
    for (int attempt = 1; attempt <= kStallTries; attempt++) {
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
        // a 150 ms stall (the network, or the server's machine) while the client keeps playing: its commands then
        // arrive all at once, more than the token burst covers
        double until = timeNow() + 0.15, next = timeNow();
        while (timeNow() < until) { a.poll(); while (timeNow() >= next) { a.tick(UserCmd()); next += a.tickPeriod(); } sleepMs(1); }
        s.frame();
        size_t after = s.players_[id].cmdQueue.size();
        // then normal play: over the last 3/4 of a second the queue empties again. (Before, it never went below 3: the
        // backlog stayed for good. Its average depth also depends on how bursty the client's sends are, i.e. machine
        // load, so the test looks at the lowest.)
        size_t lowest = 99;
        double sum = 0, stalled = 0, off = offCpu();
        int samples = 0;
        for (double stop = timeNow() + 1.0, first = timeNow() + 0.25; timeNow() < stop;) {
            s.frame(); a.poll();
            while (timeNow() >= next) { a.tick(UserCmd()); next += a.tickPeriod(); }
            if (timeNow() >= first) { size_t q = s.players_[id].cmdQueue.size(); lowest = std::min(lowest, q); sum += (double)q; samples++; }
            sleepMs(1);
            double o = offCpu(); stalled = std::max(stalled, o - off - 0.001); off = o;   // (as pump's)
        }
        printf("  queued after the stall %zu, later at least %zu (%.2f on average; machine stalled %.1f ms)\n", after, lowest,
               samples ? sum / samples : 0.0, stalled * 1e3);
        bool ok = after > 2 && lowest <= 1;
#ifdef QUARK_SANITIZED   // (the CI sanitizers job: several times slower, so how fast the queue drains isn't telling)
        if (!ok) printf("  (sanitized build: drain timing not checked)\n");
        ok = true;
#endif
        // The premise: the test's own stall only. One of the machine's past the server's skip (Server::frame skips
        // ahead 20 periods behind) drops the ticks whose tokens would have run the commands sent meanwhile: a second
        // backlog, which the allowance (spent on the first) drains at 10 a second. Then the whole test runs again.
        if (!ok && stalled > serverSkipStall(s) && attempt < kStallTries) { s.stop(); printf("  (the machine stalled the server past its skip: again)\n"); continue; }
        CHECK(ok);
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
        break;
    }
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
    a.sendCommand("callvote tmaps colosseum,colosseum,colosseum");   // (the one map offered for tourney)
    pump(s, cls, 0.3, idle);
    uint32_t loads = s.mapInfoLoads_;
    a.sendCommand("callvote tmaps colosseum,colosseum");   // (a vote is already running: refused, after the checks)
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
    void accept(const std::string& map, const MapId& mapId, int id = 0) {
        uint8_t out[512];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_ACCEPT); w.u32(nonce); w.u8((uint8_t)id); w.str(map); w.bytes(mapId.b, 32); w.u8(MODE_FFA);
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
    f.accept("bastion", m.id());
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
    f.accept("../../pwned", mapContentId("x"));
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

// C13: demos recorded by an older version are converted at the first start of a new one (originals kept under
// demos/old/<version>/); what can't be converted stays as it is, logged, and says which version recorded it.
// demo_proto27.qdemo is a protocol 27 (game 1.13-1.16) demo: if a protocol change makes it unreadable, demo.h's
// kRecordFormatSince has to move and convert() needs a step for the older records.
static void testDemoUpgrade() {
    printf("demos of older versions: converted, originals kept\n");
    namespace fs = std::filesystem;
    auto rd = [](const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); };
    std::string fixture = rd(testDataDir() + "/demo_proto27.qdemo");
    CHECK(fixture.size() > 100);
    fs::path d = fs::temp_directory_path() / ("quark_demos_" + std::to_string(getpid()));
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    auto put = [&](const std::string& name, std::string data, int proto) {
        if (proto >= 0) { uint16_t v = (uint16_t)proto; memcpy(&data[4], &v, 2); }
        std::ofstream(d / name, std::ios::binary) << data;
    };
    put("old.qdemo", fixture, -1);
    put("current.qdemo", fixture, kProtocolVersion);
    put("future.qdemo", fixture, kProtocolVersion + 1);
    put("ancient.qdemo", fixture, 20);   // (from before demos: no converter)
    { // a protocol 27 demo whose map record names no valid map: can't be converted
        std::string bad = fixture.substr(0, 6);
        std::vector<uint8_t> b(256);
        Writer w(b.data(), (int)b.size());
        w.str("../escape"); w.u32(3); w.u8('x'); w.u8('y'); w.u8('z'); w.u8(0);
        uint8_t type = 1; double t = 0; uint32_t n = (uint32_t)w.size();
        bad.append((const char*)&type, 1); bad.append((const char*)&t, 8); bad.append((const char*)&n, 4); bad.append((const char*)b.data(), n);
        put("broken.qdemo", bad, -1);
    }
    std::ofstream(d / "notes.txt") << "not a demo";
    uint16_t proto = 0;
    CHECK(demo::readProtocol((d / "old.qdemo").string(), &proto) && proto == 27);
    CHECK(demo::versionForProtocol(27) == "1.13-1.16" && demo::versionForProtocol(24) == "1.11.0");
    CHECK(demo::convertible(24) && demo::convertible(27) && !demo::convertible(23) && !demo::convertible(kProtocolVersion));
    { // before conversion this version can't play it, and says who recorded it
        NetClient nc;
        std::string err;
        CHECK(!nc.startPlayback((d / "old.qdemo").string(), err) && err.find("recorded with version 1.13-1.16") != std::string::npos);
    }
    demo::UpgradeReport r = demo::upgradeDir(d.string());
    for (auto& l : r.log) printf("  %s\n", l.c_str());
    CHECK(r.converted == 1 && r.newer == 1 && r.failed == 2);
    CHECK(demo::readProtocol((d / "old.qdemo").string(), &proto) && proto == kProtocolVersion);
    CHECK(rd(d / "old" / "1.13-1.16" / "old.qdemo") == fixture);                         // the original, kept
    CHECK(rd(d / "old.qdemo").size() > fixture.size());                                  // snapshot headers rewritten (+ syringeAway)
    CHECK(demo::readProtocol((d / "broken.qdemo").string(), &proto) && proto == 27);     // left as it was
    CHECK(demo::readProtocol((d / "future.qdemo").string(), &proto) && proto == kProtocolVersion + 1);
    CHECK(rd(d / "current.qdemo").substr(6) == fixture.substr(6) && !fs::exists(d / "old" / "1.13-1.16" / "current.qdemo"));
    std::string log = rd(d / "old" / "convert.log");
    CHECK(log.find("old.qdemo: converted") != std::string::npos && log.find("broken.qdemo") != std::string::npos);
    CHECK(rd(d / "notes.txt") == "not a demo");
    // the next start: nothing again (failures aren't retried, converted ones are current)
    demo::UpgradeReport again = demo::upgradeDir(d.string());
    CHECK(again.converted == 0 && again.failed == 0 && again.newer == 0 && again.log.empty());
    { // the converted demo plays: map, names, chat and movement as recorded
        NetClient p;
        std::string err;
        CHECK(p.startPlayback((d / "old.qdemo").string(), err) && p.mapReady() && p.map().name == "oldbox");
        int snaps = 0;
        bool chat = false;
        double end = timeNow() + 5;
        while (p.playback() && timeNow() < end) {
            p.poll();
            if (p.hasSnapshot()) snaps++;
            for (auto& e : p.takeEvents()) chat |= e.type == EV_CHAT && e.text == "hello from 1.16";
            sleepMs(5);
        }
        CHECK(snaps > 10 && chat && p.playerName(0) == "oldtimer" && p.error() == "demo finished");
    }
    { // a demo of protocol 37 (before stickies stuck to players: no projectile bit 32) converts and plays as it was
        std::string cur = rd(d / "old.qdemo");
        put("p37.qdemo", cur, 37);
        demo::UpgradeReport r37 = demo::upgradeDir(d.string());
        CHECK(r37.converted == 1 && r37.failed == 0);
        CHECK(rd(d / "p37.qdemo").substr(6) == cur.substr(6));   // (the same records: its snapshots read the same way)
        NetClient p;
        std::string err;
        CHECK(p.startPlayback((d / "p37.qdemo").string(), err) && p.mapReady());
    }
    { // protocol 42 (own-state opt bit 4: the u8 charges + the u16 cooldown, no recharges), 41 (the dash charges alone,
      // u8) and 40 (the old dash cooldown, f32) convert to 43's packed charges / recharging count + cooldown + recharges:
      // a record of each appended to a current demo, converted, read back
        std::string cur = rd(d / "old.qdemo");
        auto demoAt = [&](uint16_t proto) {
            Snapshot sn;
            sn.tick = 1;
            sn.self.dashCharges = 1;
            sn.self.dashCooldownMs = 300;   // (nothing recharging: 43's bytes here are 42's)
            std::vector<uint8_t> b(4096);
            Writer w(b.data(), (int)b.size());
            writeSnapshotClient(w, sn);
            std::string hdr((const char*)b.data(), (size_t)w.size());
            const size_t optAt = 19 + 24 + 2;   // (tick .. clientId, pos + vel, flags + mods)
            CHECK((uint8_t)hdr[optAt] == 4 && (uint8_t)hdr[optAt + 1] == 1);
            if (proto == 42) {}   // the u8 charges + the u16 cooldown
            else if (proto == 41) hdr.erase(optAt + 2, 2);   // the u8 charges, no cooldown
            else { float cd = 0.5f; hdr.replace(optAt + 1, 3, (const char*)&cd, 4); }   // the f32 cooldown in its place
            Writer w2(b.data(), (int)b.size());
            writeSnapshotWorld(w2, sn, nullptr); writeSnapshotEvents(w2, sn.events);
            std::string body = hdr + std::string((const char*)b.data(), (size_t)w2.size()), out = cur;
            memcpy(&out[4], &proto, 2);
            uint8_t type = 2; double t = 1e6; uint32_t n = (uint32_t)body.size();
            out.append((const char*)&type, 1); out.append((const char*)&t, 8); out.append((const char*)&n, 4); out += body;
            return out;
        };
        auto lastSelf = [](const std::string& dm, MoveState* m) {   // the last snapshot's own movement state
            size_t pos = 6, last = 0;
            uint32_t ln = 0;
            while (pos + 13 <= dm.size()) {
                uint32_t n; memcpy(&n, dm.data() + pos + 9, 4);
                if ((uint8_t)dm[pos] == 2) { last = pos; ln = n; }
                pos += 13 + (size_t)n;
            }
            Reader r((const uint8_t*)dm.data() + last + 13, (int)ln);
            Snapshot sn;
            bool ok = last && readSnapshotClient(r, sn);
            *m = sn.self;
            return ok && sn.tick == 1;
        };
        std::string out42, out41, out40, why;
        MoveState m42, m41, m40;
        CHECK(demo::convert(demoAt(42), &out42, &why) && lastSelf(out42, &m42));
        CHECK(m42.dashCharges == 1 && m42.dashCooldownMs == 300 && m42.dashRecharging == 0);
        CHECK(demo::convert(demoAt(41), &out41, &why) && lastSelf(out41, &m41));
        CHECK(demo::convert(demoAt(40), &out40, &why) && lastSelf(out40, &m40));
        printf("  protocol 41 -> %d: %d charge(s), %d ms; protocol 40: %d charge(s), %d ms\n", (int)kProtocolVersion,
               (int)m41.dashCharges, (int)m41.dashCooldownMs, (int)m40.dashCharges, (int)m40.dashCooldownMs);
        CHECK(m41.dashCharges == 1 && m41.dashCooldownMs == 0 && m40.dashCharges == 2 && m40.dashCooldownMs == 500);
        uint16_t pv = 0; memcpy(&pv, &out41[4], 2);
        CHECK(pv == kProtocolVersion && demo::convertible(42) && demo::convertible(41) && demo::convertible(40));
        // protocol 46 (before rocket riding): a grapple on a player reads as it was; a hookTarget past the players was
        // nothing then and has nothing after it (47 adds the rocket's id, velocity and gravity only for kHookRocket)
        auto demo46 = [&](uint8_t target) {
            Snapshot sn;
            sn.tick = 1; sn.selfWeapon = WPN_ROCKET;
            sn.self.flags = MF_HOOKED; sn.self.hookPoint = {1, 2, 3}; sn.self.hookTarget = 5;
            std::vector<uint8_t> b(4096);
            Writer w(b.data(), (int)b.size());
            writeSnapshotClient(w, sn);
            std::string hdr((const char*)b.data(), (size_t)w.size());
            const size_t optAt = 19 + 24 + 2;
            CHECK((uint8_t)hdr[optAt] == (1 | 16) && (uint8_t)hdr[optAt + 13] == 5);
            hdr[optAt + 13] = (char)target;   // (46's bytes: nothing follows the u8)
            Writer w2(b.data(), (int)b.size());
            writeSnapshotWorld(w2, sn, nullptr); writeSnapshotEvents(w2, sn.events);
            std::string body = hdr + std::string((const char*)b.data(), (size_t)w2.size()), out = cur;
            const uint16_t proto = 46;
            memcpy(&out[4], &proto, 2);
            uint8_t type = 2; double t = 1e6; uint32_t n = (uint32_t)body.size();
            out.append((const char*)&type, 1); out.append((const char*)&t, 8); out.append((const char*)&n, 4); out += body;
            return out;
        };
        std::string out46a, out46b;
        MoveState m46a, m46b;
        CHECK(demo::convertible(46) && demo::convert(demo46(5), &out46a, &why) && lastSelf(out46a, &m46a));
        CHECK(demo::convert(demo46(kHookRocket), &out46b, &why) && lastSelf(out46b, &m46b));
        printf("  protocol 46 -> %d: hook target %d (was 5), %d (was %d)\n", (int)kProtocolVersion, (int)m46a.hookTarget, (int)m46b.hookTarget, (int)kHookRocket);
        CHECK(m46a.hookTarget == 5 && (m46a.flags & MF_HOOKED) && m46a.hookPoint.y == 2 && m46b.hookTarget == 0 && (m46b.flags & MF_HOOKED));
        // and today's: riding a rocket round-trips the rocket's id, velocity and gravity
        Snapshot sr;
        sr.self.flags = MF_HOOKED; sr.self.hookTarget = kHookRocket; sr.self.hookProj = 777; sr.self.hookVel = {48, -1, 2}; sr.self.hookGrav = 13.5f; sr.self.hookAccel = kRocketAccel;
        uint8_t rb[256];
        Writer wr(rb, sizeof rb);
        writeSnapshotClient(wr, sr);
        Reader rr(rb, wr.size());
        Snapshot back;
        CHECK(readSnapshotClient(rr, back) && back.self.hookTarget == kHookRocket && back.self.hookProj == 777 && back.self.hookVel.x == 48 &&
              back.self.hookGrav == 13.5f && back.self.hookAccel == kRocketAccel && rr.remaining() == 0);
        // protocol 54 (no thrust after the gravity) reads as no thrust, and nothing is left over
        uint8_t ob[256];
        Writer wo(ob, sizeof ob);
        writeSnapshotClient(wo, sr);
        const int cut = wo.size() - 4 - 4 - 4;   // (the f32 thrust sits before the 4 header bytes and the u32 baseTick)
        std::vector<uint8_t> old(ob, ob + cut);
        old.insert(old.end(), ob + cut + 4, ob + wo.size());
        Reader ro(old.data(), (int)old.size());
        Snapshot back54;
        CHECK(readSnapshotClient(ro, back54, 54) && back54.self.hookAccel == 0 && back54.self.hookGrav == 13.5f && ro.remaining() == 0);
    }
    // a second conversion of the same name keeps both originals
    put("old.qdemo", fixture, -1);
    r = demo::upgradeDir(d.string());
    CHECK(r.converted == 1 && rd(d / "old" / "1.13-1.16" / "old-1.qdemo") == fixture);
    // versions this game ran with name later protocols (versions.txt)
    fs::path vf = d / "versions.txt";
    demo::noteVersion(vf.string(), "9.8.0", 200);
    demo::noteVersion(vf.string(), "9.8.1", 200);
    demo::noteVersion(vf.string(), "9.8.1", 200);
    CHECK(demo::versionForProtocol(200) == "9.8.0-9.8.1" && demo::versionForProtocol(201) == "protocol 201");
    CHECK(rd(vf) == "200 9.8.0\n200 9.8.1\n");
    fs::remove_all(d, ec);
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
// OWNER_DECISIONS_2 B3: an edit session on a built-in map never writes it, nor a copy in your maps folder that would
// hide it (that used to happen at the first autosave): its edits last until the map changes. "editmode saveas" keeps
// them under a new name (never a built-in map's), autosaved from then on.
static void testBuiltInMapEditSession(uint16_t port) {
    printf("edit session on a built-in map: never saved over it\n");
    std::string mine = userMapsDir() + "/castle.qmap";
    std::remove(mine.c_str());
    Map disk;
    CHECK(disk.load("castle") && isOfficialMap(disk));
    Server s;
    ServerConfig cfg;
    cfg.map = "castle"; cfg.mode = MODE_FFA; cfg.port = port; cfg.editMode = true;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 1.0, idle);
    CHECK(a.mapReady() && a.hasSnapshot() && (a.mode().flags & MS_EDITING) && (a.mode().flags & MS_TEMP_EDIT) && s.tempEdit_);
    size_t nb = disk.brushes.size();
    a.sendCommand("ed b ins " + std::to_string(nb) + " 1 0 10 0 2 11 2");
    pump(s, cls, 0.5, idle);
    CHECK(s.map_.brushes.size() == nb + 1 && a.map().id() == s.map_.id() && !isOfficialMap(s.map_));
    s.command("editmode off");   // play it: the edited copy, nothing saved
    pump(s, cls, 1.0, idle);
    std::error_code ec;
    CHECK(!s.editing_ && s.map_.brushes.size() == nb + 1 && !std::filesystem::exists(mine, ec));
    s.command("editmode on");
    pump(s, cls, 0.3, idle);
    CHECK(s.editing_ && s.tempEdit_);
    s.command("editmode saveas qk1");   // another built-in map's name: refused
    CHECK(s.cfg_.map == "castle" && s.tempEdit_);
    char tag[16];
    snprintf(tag, sizeof tag, "%u", (unsigned)qrand());
    std::string name = std::string("zz_mybastion") + tag, path = userMapsDir() + "/" + name + ".qmap";
    s.command("editmode saveas " + name);
    Map kept;
    CHECK(s.cfg_.map == name && !s.tempEdit_ && kept.load(name) && kept.brushes.size() == nb + 1 && kept.handmade);
    a.sendCommand("ed t Mine");
    pump(s, cls, 0.5, idle);
    s.stop();   // (autosaves: it's yours now)
    CHECK(kept.load(name) && kept.title == "Mine");
    Map after;
    CHECK(!std::filesystem::exists(mine, ec) && after.load("castle") && isOfficialMap(after) && after.brushes.size() == nb);
    std::remove(path.c_str());
    std::remove((path + ".bak").c_str());
}

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
    CHECK(s.map_.brushes.size() == nb + 1 && a.map().id() == s.map_.id() && b.map().id() == s.map_.id());
    MapId edited = s.map_.id();
    b.sendCommand("callvote edit off");   // (nobody else votes: "play it" passes when the 30 s are up, as nobody said no)
    pumpMsgs(1.0);
    CHECK(s.editing_);                    // (not yet: one yes of two)
    s.vote_.endsAt = s.time();            // (the 30 s are up)
    pumpMsgs(5.0);
    CHECK(!s.editing_ && !(a.mode().flags & MS_EDITING) && !(b.mode().flags & MS_EDITING));
    CHECK(s.map_.id() == edited && a.map().id() == edited && b.map().id() == edited && a.mapReady() && b.mapReady());
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
        for (int i = 0; i < 100 && s.ms_.phase == PHASE_WARMUP; i++) { skipCountdowns(s); pump(s, cls, 0.05, idle); }
        CHECK(s.ms_.phase == PHASE_FREEZE);   // (the round starts in its freeze)
        auto& pa = s.players_[a.clientId()];
        CHECK(s.ms_.weaponMask == ((1 << WPN_ROCKET) | (1 << WPN_LASER) | (1 << WPN_KNIFE)));
        CHECK(pa.weapon == WPN_ROCKET || pa.weapon == WPN_LASER || pa.weapon == WPN_KNIFE);
        pump(s, cls, 0.2, [](int) { UserCmd c; c.weapon = WPN_SNIPER; return c; });
        CHECK(pa.weapon != WPN_SNIPER); // not in the pool
        pump(s, cls, 0.2, [](int) { UserCmd c; c.weapon = WPN_KNIFE; return c; });
        CHECK(pa.weapon == WPN_KNIFE);
        s.cfg_.arenaVariant = s.ms_.arenaVariant = (uint8_t)arenaVariantFromName("instagib");
        for (int i = 0; i < 100 && s.ms_.phase != PHASE_LIVE; i++) { skipCountdowns(s); pump(s, cls, 0.05, idle); }
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
            skipCountdowns(s);   // (the bracket, its byes and its matches still go their own way)
            pump(s, cls, 0.05, idle);
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
    for (int attempt = 1; attempt <= kStallTries; attempt++) {
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
        double stalled = 0;
        pump(s, cls, 2.0, idle, &stalled);
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
        }, &stalled);
        std::sort(lags.begin(), lags.end());
        int median = lags.empty() ? -1 : lags[lags.size() / 2];
        printf("  %d of %d render times on a float's 1/32 s grid; view tick a median %d ms behind the server (interp 50; machine stalled %.1f ms)\n",
               onGrid, samples, median, stalled * 1e3);
        s.stop();
        // The premise: a continuous server clock. A machine stall past the server's skip (Server::frame: 20 periods
        // behind, it skips ahead) moves its clock back against the client's, which follows a later clock only slowly
        // (as it should: it's how one late snapshot doesn't jerk the view): the view tick then reads early for a while.
        bool ok = median >= 45 && median < 150;
        if (!ok && stalled > serverSkipStall(s) && attempt < kStallTries) { printf("  (the machine stalled the server past its skip: again)\n"); continue; }
        CHECK(samples > 20 && onGrid * 4 < samples);
        CHECK(ok);
        break;
    }
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
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    // limit: two snapshot intervals plus up to 15 ms of measured timing jitter (machine dependent: shared CI
    // runners and cloud VMs measure 4-10 ms on their own; snapshots at 125/s would already be 17+ ms)
    const float jitterMax = 0.015f;
    for (int attempt = 1; attempt <= kStallTries; attempt++) {
        Server s;
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
        double stalled = 0;
        pump(s, cls, 4.0, [](int i) { UserCmd c; c.forward = 127; c.yaw = i * 1.5f + (float)timeNow(); c.buttons = BTN_JUMP; return c; }, &stalled);
        double avg = s.snapCount_ ? (double)s.snapBytes_ / s.snapCount_ : 0;
        uint32_t misses = 0;
        for (auto* c : cls) misses += c->baseMisses();
        float limit = 2.0f / cls[0]->snapRate() + jitterMax;
        printf("  8 players moving: %.0f bytes per snapshot, %u undecodable deltas, interpolation delay %.1f ms (limit %.1f; machine stalled %.1f ms)\n",
               avg, misses, cls[0]->interpDelay() * 1000, limit * 1000, stalled * 1e3);
        // The premise: a machine that runs the test when it asks. Snapshots are sent from the pump's frames, 1 ms
        // (real) apart; a stall of the machine that with that 1 ms holds them back longer than the jitter allowed is
        // late arrivals the client is right to measure (a single one fills the estimate's top percent): then the test
        // runs again, at most kStallTries times, the last one as it is. A real regression stays over with no stall.
        if (cls[0]->interpDelay() >= limit && (stalled + 0.001) * timeScale() > jitterMax && attempt < kStallTries) {
            s.stop(); printf("  (the machine stalled past the jitter allowed: again)\n"); continue;
        }
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
        break;
    }

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

// Hidden information (NET-10, JUDGMENT_CALLS C12): an enemy out of sight reaches the client without health, weapon or
// firing; a projectile out of sight isn't sent (its owner always gets it), one that leaves sight goes on as an id (not
// an explosion) and one that goes off out of sight is shown at the spot for a moment (its explosion); a dead player
// gets what the player it watches sees; the hide and seek seeker gets nothing about the hiders during the freeze.
// Stickies inherit the shooter's forward / back and vertical velocity, never the sideways part (owner, 1.17).
static void testStickyInherit() {
    printf("sticky: inherits forward and vertical speed, not sideways\n");
    auto near = [](Vec3 a, Vec3 b) { return length(a - b) < 1e-4f; };
    Vec3 aim = normalize(Vec3(0, 0.2f, -1));                               // looking along -z, a little up
    CHECK(near(stickyInherit({8, 0, 0}, aim), {0, 0, 0}));                 // pure strafe: nothing
    CHECK(near(stickyInherit({0, 0, -10}, aim), {0, 0, -9}));              // running forward: 90%
    CHECK(near(stickyInherit({0, 0, 10}, aim), {0, 0, 9}));                // backpedalling too
    CHECK(near(stickyInherit({6, 5, -8}, aim), {0, 4.5f, -7.2f}));         // strafe-jumping: the sideways 6 dropped
    CHECK(near(stickyInherit({6, -3, -8}, {0, -1, 0}), {0, -2.7f, 0}));    // aiming straight down: vertical only
}

static void testHiddenInfo(uint16_t port) {
    printf("hidden info: no health / weapon / projectiles out of sight, dead see what they watch, seeker blind in the freeze\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    std::string addr = "127.0.0.1:" + std::to_string(port);
    NetClient a, b, c;
    a.connect(addr, "a"); b.connect(addr, "b"); c.connect(addr, "c");
    std::vector<NetClient*> cls = {&a, &b, &c};
    pump(s, cls, 1.5, idle);
    const int ia = a.clientId(), ib = b.clientId(), ic = c.clientId();
    auto& pa = s.players_[ia]; auto& pb = s.players_[ib]; auto& pc = s.players_[ic];
    const Vec3 behindA(0, 0, -28), behindB(0, 0, 28), openA(-9, 0, -26), openB(-9, 0, 26), far(9, 0, 0);
    auto snapOf = [](NetClient& n, int id) -> const PlayerSnap* { for (auto& p : n.latest().players) if (p.id == id) return &p; return nullptr; };
    auto projOf = [](NetClient& n, uint16_t id) -> const ProjectileSnap* { for (auto& p : n.latest().projectiles) if (p.id == id) return &p; return nullptr; };
    // b: 37 hp, holding the sniper, firing; a counts the snapshots b came in hidden, and any with b's health or weapon
    int hiddenSnaps = 0, leaks = 0;
    auto hold = [&](Vec3 at, Vec3 bt, Vec3 ct) {
        return [&, at, bt, ct](int i) {
            pa.move.pos = at; pa.move.vel = {}; pb.move.pos = bt; pb.move.vel = {}; pc.move.pos = ct; pc.move.vel = {};
            pa.respawnAt = pb.respawnAt = pc.respawnAt = s.time() + 100;
            pb.health = 37; pb.lastFire = s.time();
            if (const PlayerSnap* q = snapOf(a, ib); q && q->hidden) {
                hiddenSnaps++;
                if (q->health || q->weapon || (q->flags & (PS_FIRING | PS_CROUCHED))) leaks++;
            }
            UserCmd u;
            if (cls[i] == &b) u.weapon = WPN_SNIPER;
            return u;
        };
    };
    pump(s, cls, 0.8, hold(behindA, behindB, far));
    const PlayerSnap* q = snapOf(a, ib);
    printf("  b behind walls: %d snapshots hidden, %d with health / weapon / firing\n", hiddenSnaps, leaks);
    CHECK(q && q->hidden && hiddenSnaps > 20 && leaks == 0);
    pump(s, cls, 0.5, hold(openA, openB, far));   // in sight: everything (the data is really there to hide)
    q = snapOf(a, ib);
    CHECK(q && !q->hidden && q->health == 37 && q->weapon == WPN_SNIPER);

    // projectiles: b's rocket by b, behind the walls from a (stuck in the air: it stays put)
    auto rocket = [&](int owner, Vec3 at) {
        for (auto& pr : s.projectiles_) if (!pr.active) {
            pr = Projectile();
            pr.active = true; pr.id = s.nextProjId_++; pr.type = WPN_ROCKET; pr.owner = owner; pr.pos = at; pr.stuck = true; pr.dieAt = s.time() + 100;
            return &pr;
        }
        return (Projectile*)nullptr;
    };
    Projectile* rb = rocket(ib, behindB + Vec3(0, 2.5f, 0));
    Projectile* ra = rocket(ia, behindB + Vec3(1, 2.5f, 0));   // a's own, just as far out of a's sight
    CHECK(rb && ra);
    if (!rb || !ra) { s.stop(); return; }
    const uint16_t idb = rb->id, ida = ra->id;
    pump(s, cls, 0.5, hold(behindA, behindB, far));
    CHECK(!projOf(a, idb));                                  // never seen: not sent at all
    CHECK(projOf(b, idb) && !projOf(b, idb)->hidden);        // its owner has it
    CHECK(projOf(a, ida) && !projOf(a, ida)->hidden);        // a's own: always
    rb->pos = openB + Vec3(0, 2.5f, 0);                      // in sight
    pump(s, cls, 0.4, hold(openA, openB, far));
    CHECK(projOf(a, idb) && !projOf(a, idb)->hidden);
    rb->pos = behindB + Vec3(0, 2.5f, 0);                    // out of sight again: an id, still flying (no explosion)
    pump(s, cls, 1.0, hold(behindA, behindB, far));
    CHECK(projOf(a, idb) && projOf(a, idb)->hidden && a.projectileHidden(idb));
    bool listed = false;
    for (auto& p : a.interpolatedProjectiles()) listed |= p.id == idb;
    CHECK(!listed);
    // it goes off out of sight: shown at the spot for a moment (the client plays the explosion there), then gone
    Vec3 boom = rb->pos;
    rb->active = false;
    bool revealed = false;
    pump(s, cls, 0.3, [&](int i) {
        if (const ProjectileSnap* r = projOf(a, idb); r && !r->hidden && length(r->pos - boom) < 0.1f) revealed = true;
        return hold(behindA, behindB, far)(i);
    });
    CHECK(revealed && !projOf(a, idb));
    ra->active = false;

    // dead: a (killed by b) gets what b sees - c in the open from b is sent, c behind walls from b isn't
    s.killPlayer(ia, ib, WPN_RIFLE);
    CHECK(pa.specTarget == ib);
    pump(s, cls, 0.8, hold(far, openA, openB));
    q = snapOf(a, ic);
    CHECK(q && !q->hidden && q->health > 0);
    pump(s, cls, 0.8, hold(far, behindA, behindB));
    q = snapOf(a, ic);
    CHECK(q && q->hidden && q->health == 0);
    s.stop();

    // hide and seek: during the freeze the seeker gets nothing of the hiders, even right in front of it
    Server h;
    ServerConfig hc;
    hc.autoStart = false; hc.map = "hamlet"; hc.mode = MODE_HIDE; hc.port = (uint16_t)(port + 1);
    h.setLog([](const std::string&) {});
    if (!h.start(hc)) { printf("  FAIL %s:%d: server start\n", __FILE__, __LINE__); g_fail++; return; }
    NetClient hs[3];
    std::vector<NetClient*> hcl;
    for (int i = 0; i < 3; i++) { hs[i].connect("127.0.0.1:" + std::to_string(hc.port), "h" + std::to_string(i)); hcl.push_back(&hs[i]); }
    pump(h, hcl, 1.5, idle);
    h.command("forcestart");
    pump(h, hcl, 0.3, idle);
    int seeker = -1;
    for (int i = 0; i < 3; i++) if (h.players_[hs[i].clientId()].it) seeker = i;
    CHECK(seeker >= 0 && h.ms_.phase == PHASE_FREEZE);
    if (seeker < 0) { h.stop(); return; }
    int seen = 0, snaps = 0;
    auto near = [&](int i) {   // hiders 3 m in front of the seeker
        Vec3 o = h.players_[hs[seeker].clientId()].move.pos;
        int k = 0;
        for (int j = 0; j < 3; j++) if (j != seeker) { auto& hp = h.players_[hs[j].clientId()]; hp.move.pos = o + Vec3(k++ ? 1.5f : -1.5f, 0, -3); hp.move.vel = {}; }
        for (auto& p : hs[seeker].latest().players)
            if (p.id != hs[seeker].clientId() && (p.flags & PS_ALIVE)) { snaps++; seen += !p.hidden || p.health || p.weapon; }
        (void)i;
        return UserCmd();
    };
    pump(h, hcl, 0.8, near);
    printf("  hide and seek freeze: hiders sent to the seeker %d times of %d\n", seen, snaps);
    CHECK(h.ms_.phase == PHASE_FREEZE && snaps > 20 && seen == 0);
    int other = seeker == 0 ? 1 : 0, third = 3 - seeker - other;
    const PlayerSnap* hq = snapOf(hs[other], hs[third].clientId());
    CHECK(hq && !hq->hidden);                                // (a hider sees the other hider next to it)
    h.phaseEnd_ = h.time();                                  // the round goes live: the seeker sees them
    pump(h, hcl, 0.6, near);
    hq = snapOf(hs[seeker], hs[other].clientId());
    CHECK(h.ms_.phase == PHASE_LIVE && hq && !hq->hidden);
    h.stop();
}

// FFA on Clockwork: 3 expert bots and a player standing still, 40 s of game time. Seeded: the match is forced to
// start over once the bots' navigation is ready, with every bot's dice and the spawn picks seeded, and the "player"
// is in-process (a scripted bot), so no network timing gets in. It runs on the manual clock (simRun: every tick, the
// warmup before the restart too, which is seeded as well), so a seed plays out exactly the same match every time.
struct FfaRun { int kills = 0, playerDeaths = 0, hooks = 0, phase = 0; };
// hash (simhash, tools/isa_check.sh): every tick's player positions, velocities, health and projectiles, bit for bit,
// so two builds of the game (the SSE4.2 and AVX2 clients) can be checked to simulate exactly the same match
static FfaRun ffaVsExperts(uint16_t port, uint32_t seed, uint64_t* hash = nullptr) {
    FfaRun r;
    SimClock clock;
    qsrand(seed);   // (the warmup's spawn picks and the bots' first dice)
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true;
    cfg.map = "clockwork"; cfg.mode = MODE_FFA; cfg.port = port;   // (a compact map: bastion is a big team map now)
    cfg.vsBots = true; cfg.botsEnemy = 3; cfg.botSkill = BOT_EXPERT; cfg.timeLimit = 60;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return r; } }
    CHECK(s.playerCount() == 3);
    int me = s.addBot(TEAM_A, BOT_BEGINNER);
    auto stand = [](Server&, int) { UserCmd c; c.weapon = kNoWeaponChange; return c; };
    simNavReady(s);
    simRun(s, 1.0);
    CHECK(me >= 0 && s.nav_.ready(s.map_));
    if (me < 0) { s.stop(); return r; }
    qsrand(seed);   // (spawn picks)
    for (int i = 0; i < kMaxPlayers; i++)
        if (s.players_[i].connected && s.players_[i].isBot) {
            int skill = s.bots_[i].skill;
            s.bots_[i] = BotBrain();
            s.bots_[i].skill = skill;
            s.bots_[i].rng = (seed * 2654435761u) ^ (uint32_t)(i * 40503u + 1u);
        }
    s.bots_[me].script = stand;
    for (auto& pr : s.projectiles_) pr.active = false;   // (nothing from the warmup still flying)
    s.command("forcestart");
    bool was[kMaxPlayers] = {};
    simRun(s, 40.0, [&] {   // counting grapple grabs as we go (every tick: a grab let go of at once counts too)
        for (int k = 0; k < kMaxPlayers; k++) {
            bool h = s.players_[k].connected && s.players_[k].isBot && k != me && (s.players_[k].move.flags & MF_HOOKED);
            r.hooks += h && !was[k];
            was[k] = h;
        }
        if (!hash) return;
        auto mix = [&](const void* p, size_t n) {   // FNV-1a
            for (size_t b = 0; b < n; b++) *hash = (*hash ^ ((const unsigned char*)p)[b]) * 1099511628211ull;
        };
        for (auto& p : s.players_) {
            if (!p.connected) continue;
            mix(&p.move.pos, sizeof p.move.pos);
            mix(&p.move.vel, sizeof p.move.vel);
            mix(&p.health, sizeof p.health);
        }
        for (auto& pr : s.projectiles_) if (pr.active) mix(&pr.pos, sizeof pr.pos);
    });
    for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && i != me) r.kills += s.players_[i].kills;
    r.playerDeaths = s.players_[me].deaths;
    r.phase = s.ms_.phase;
    s.stop();
    return r;
}

static void testBots(uint16_t& port) {
    printf("bots: vs-bots fights, balance bots, votes, every mode\n");
    { // FFA vs 3 expert bots: they should find and kill each other / us (seeded, on the manual clock: the same match every run)
        // (0x5EED0003, the third of 20 seeds: with 0x5EED0001 the player stood untouched through 40 s once the tick order rotated, 2026-10-30; 2 of 20 seeds
        // do that, before and after. Over 20 seeds now: kills 3 to 9, the player's deaths 0 to 4, grapples 5 to 16.)
        FfaRun r = ffaVsExperts(port++, 0x5EED0003u);
        printf("  ffa vs 3 expert bots: %d kills (%d of the player standing still), %d grapples in 40 s (phase %d)\n", r.kills, r.playerDeaths, r.hooks, r.phase);
        CHECK(r.kills >= 4);         // (7 to 11 over seeds, 1.17; 3 to 8 over 20 seeds and 7 for this one, on the manual clock)
        CHECK(r.playerDeaths >= 1);  // nobody stands in the open for long
        CHECK(r.hooks >= 1);         // bots use the grapple (3 grabs here; 0 to 10 over 20 seeds, every tick counted)
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
        CHECK(std::fabs(s.liveLength_ - 240.0f) < 0.01f);   // the round clock: 2 min a player on the teams (owner, 2026-10-30)
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
    const char* maps[MODE_COUNT] = {"bastion", "bastion", "foundry", "bastion", "bastion", "bastion", "bastion", "clockwork", "clockwork", "clockwork", "hamlet", "hamlet", "lodestone", "cascade", "bastion", "clockwork"};
    static_assert(MODE_COUNT == 16, "add the new mode's map here");
    SimClock clock;   // (bots only: every tick runs, none waited for)
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        qsrand(0x5EED0100u + mode);
        Server s;
        ServerConfig cfg;
        cfg.autoStart = true;
        cfg.map = maps[mode]; cfg.mode = mode; cfg.port = port++;
        cfg.vsBots = true; cfg.botsEnemy = 3; cfg.botsAlly = 2; cfg.botSkill = BOT_ADVANCED;
        s.setLog([](const std::string&) {});
        if (!s.start(cfg)) { g_fail++; continue; }
        if (isOneVsOneMode(mode) || mode == MODE_DUEL) {
            // 1v1 and duel are the player and their team size of bots (F7: no ally in 1v1): the player joins, a NetClient,
            // so these two run on the real clock (pump) as before; the other modes are bots only
            setManualClock(false);
            NetClient me;
            me.connect("127.0.0.1:" + std::to_string(cfg.port), "me");
            std::vector<NetClient*> cls = {&me};
            pump(s, cls, 14.0, idle);
            setManualClock(true);
        } else {
            simNavReady(s);
            simRun(s, 14.0);
        }
        int kills = 0, cps = 0;
        for (auto& p : s.players_) { kills += p.kills; cps += p.checkpoint; }
        printf("  %-8s with 5 bots: phase %d, round %d, kills %d, score %d-%d, cart %.1f, bomb %d, race checkpoints %d\n",
               modeName(mode), s.ms_.phase, s.ms_.round, kills, s.ms_.scoreA, s.ms_.scoreB, s.ms_.payloadDist, s.ms_.bombState, cps);
        CHECK(isRaceMode(mode) ? s.ms_.phase == PHASE_WARMUP : s.ms_.phase != PHASE_WARMUP); // bots alone start a match (race: practice until someone starts it)
        s.stop();
    }
}

// Server CPU benchmark: a full server (bots fighting + human clients receiving snapshots) on a busy map;
// prints the time per tick (simulate + snapshots) against the 1 ms budget of a 1000 Hz server.
// serverbench: the bots' thinking per tick, timed through the think hook (summed per tick)
static uint64_t g_benchThinkTick = ~0ull;
static double g_benchThinkSum = 0;
static std::vector<float>* g_benchThinks = nullptr;
static UserCmd benchThink(Server& s, int id) {
    if (s.tick_ != g_benchThinkTick) {
        if (g_benchThinkTick != ~0ull && g_benchThinks) g_benchThinks->push_back((float)g_benchThinkSum);
        g_benchThinkTick = s.tick_; g_benchThinkSum = 0;
    }
    double t0 = timeNow();
    UserCmd c = botThinkDirect(s, id);
    g_benchThinkSum += timeNow() - t0;
    return c;
}
static void serverBench(uint16_t port, int bots, int humans, double seconds, const char* map, int mode = MODE_FFA, int rate = 0) {
    g_serverRateCheck = false; // measure at 1000 Hz, don't step down
    Server s;
    ServerConfig cfg;
    cfg.map = map; cfg.mode = mode; cfg.port = port; cfg.autoStart = true; cfg.maxPlayers = kMaxPlayers;
    if (rate) cfg.maxTickRate = rate;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { printf("  start failed\n"); return; }
    std::vector<std::unique_ptr<NetClient>> owned;
    std::vector<NetClient*> cls;
    for (int i = 0; i < humans; i++) {
        owned.push_back(std::make_unique<NetClient>());
        owned.back()->connect("127.0.0.1:" + std::to_string(port), "h" + std::to_string(i));
        cls.push_back(owned.back().get());
    }
    const bool teams = modeIsTeamBased(mode);
    for (int i = 0; i < bots; i++) s.addBot(teams && i % 2 ? TEAM_B : TEAM_A, BOT_ADVANCED);
    pump(s, cls, 2.0, idle);
    if (teams) { s.command("forcestart"); pump(s, cls, 0.5, idle); }   // (a team mode: everyone spawns at once, as a round starts)
    std::vector<float> t;
    t.reserve(200000);
    s.tickTimes_ = &t;
    std::vector<float> th;
    th.reserve(200000);
    g_benchThinks = &th; g_benchThinkTick = ~0ull; g_botThinkHook = benchThink;
    pump(s, cls, seconds, [](int i) { UserCmd c; c.forward = 127; c.yaw = i * 1.3f + (float)timeNow(); c.buttons = BTN_JUMP | BTN_FIRE; return c; });
    g_botThinkHook = nullptr; g_benchThinks = nullptr;
    s.tickTimes_ = nullptr;
    std::sort(th.begin(), th.end());
    auto pctTh = [&](double p) { return th.empty() ? 0.0 : th[std::min(th.size() - 1, (size_t)(th.size() * p))] * 1e6; };
    // the bots' perception phases: how many look on the same tick (the busiest tick of each 50 ms)
    int slots[1000] = {}, busiest = 0, period = std::max(1, (int)std::lround(0.05 * s.tickRate()));
    for (int i = 0; i < kMaxPlayers; i++)
        if (s.players_[i].connected && s.players_[i].isBot && s.players_[i].alive && period <= 1000)
            busiest = std::max(busiest, ++slots[(int)(std::llround(s.bots_[i].nextLook * s.tickRate()) % period)]);
    std::sort(t.begin(), t.end());
    double sum = 0;
    for (float v : t) sum += v;
    auto pct = [&](double p) { return t.empty() ? 0.0 : t[std::min(t.size() - 1, (size_t)(t.size() * p))] * 1e6; };
    printf("  SERVERBENCH %s %s: %d bots + %d clients, %zu ticks at %d Hz: avg %.1f us  p99 %.1f us  p99.9 %.1f us  worst %.1f us  (%.1f%% of the tick budget); at most %d bots look on one tick\n"
           "    bot thinks per tick: avg %.1f us  p99 %.1f us  p99.9 %.1f us  worst %.1f us\n",
           map, modeName(mode), bots, humans, t.size(), s.tickRate(), t.empty() ? 0 : sum / t.size() * 1e6, pct(0.99), pct(0.999), t.empty() ? 0 : t.back() * 1e6,
           t.empty() ? 0 : sum / t.size() * s.tickRate() * 100, busiest,
           th.empty() ? 0 : std::accumulate(th.begin(), th.end(), 0.0) / th.size() * 1e6, pctTh(0.99), pctTh(0.999), th.empty() ? 0 : th.back() * 1e6);
    qzoneDump();
    s.stop();
}

// A bots-only match on a map: what killed whom. Falls are deaths to the void or lava (pushed = hit in the 2 s
// before), self = own rockets / stickies, mode = the mode's own kills (bomb, drain). trace: prints the last
// second of every fall (positions, speed, buttons, grapple) and every self-kill's weapon. On the manual clock
// (simRun), seeded by the map, mode and level: the same match every run.
struct BotMatch { int kills = 0, deaths = 0, falls = 0, pushed = 0, self = 0, modeKills = 0; std::string state; };
static bool playBots(const std::string& name, int mode, int skill, uint16_t& port, double seconds, bool trace, BotMatch& r) {
    SimClock clock;
    uint32_t seed = 2166136261u;
    for (char c : name) seed = (seed ^ (uint8_t)c) * 16777619u;
    qsrand(seed ^ (uint32_t)(mode * 7919 + skill * 104729));
    Server s;
    ServerConfig cfg;
    std::string logPath = (std::filesystem::temp_directory_path() / ("quark_playtest_" + std::to_string(getpid()) + ".log")).string();
    cfg.map = name; cfg.mode = mode; cfg.port = port++; cfg.autoStart = true;
    cfg.logLevel = LOG_SIMPLE; cfg.logFile = logPath;   // (every death, with its cause)
    s.setLog([](const std::string&) {});
    bool started = false;
    for (int k = 0; k < 20 && !started; k++) { started = s.start(cfg); if (!started) cfg.port = port++; } // (port taken: the next)
    if (!started) return false;
    bool duo = mode == MODE_ARENA || mode == MODE_DUEL || mode == MODE_TOURNEY;
    int n = duo ? 1 : mode == MODE_RACE ? 3 : 4;
    for (int i = 0; i < n; i++) { s.addBot(TEAM_A, skill); if (mode != MODE_RACE) s.addBot(TEAM_B, skill); }
    simNavReady(s);
    simRun(s, 1.0);
    s.command("forcestart");
    // where each fall began: the last place the bot stood before it died over the void / in lava
    Vec3 ground[kMaxPlayers]; bool was[kMaxPlayers] = {};
    struct Sample { double t; Vec3 pos, vel; uint16_t buttons; uint8_t flags; bool dash; Vec3 hook; };
    std::vector<Sample> hist[kMaxPlayers];   // (trace: the last second of each bot, printed for a fall)
    double nextPos = 0, nextHist = 0;
    const bool printPos = getenv("PT_POS") != nullptr;
    simRun(s, seconds, [&] {   // (every tick)
        const bool histNow = trace && s.time() >= nextHist;   // (trace: a sample each 10 ms of game time)
        if (histNow) nextHist = s.time() + 0.01;
        if (printPos && s.time() >= nextPos) {   // (where everyone is, every second)
            nextPos = s.time() + 1.0;
            printf("    t %.0f:", s.time());
            for (auto& p : s.players_) if (p.connected) printf("  %.1f %.1f %.1f%s", p.move.pos.x, p.move.pos.y, p.move.pos.z, p.alive ? "" : " dead");
            printf("\n");
        }
        for (int i = 0; i < kMaxPlayers; i++) {
            auto& p = s.players_[i];
            if (!p.connected) continue;
            if (was[i] && !p.alive && (p.move.pos.y < s.map_.killY() || (!s.map_.lava.empty() && s.map_.inLava(playerBox(p.move))))) {
                bool hit = s.time() - p.lastDamagedAt < 2.0;
                r.pushed += hit;
                if (trace) {
                    printf("    %s fell from %.1f %.1f %.1f%s\n", p.name.c_str(), ground[i].x, ground[i].y, ground[i].z, hit ? " (pushed)" : "");
                    for (auto& h : hist[i])
                        printf("      %.2f pos %.1f %.1f %.1f vel %.1f %.1f %.1f%s%s%s%s%s\n", h.t - s.time(), h.pos.x, h.pos.y, h.pos.z, h.vel.x, h.vel.y, h.vel.z,
                               (h.flags & MF_ONGROUND) ? " ground" : "", (h.buttons & BTN_JUMP) ? " JUMP" : "", (h.buttons & BTN_DASH) ? " DASH" : "",
                               h.dash ? " dashing" : "", (h.flags & MF_HOOKED) ? " hooked" : (h.buttons & BTN_GRAPPLE) ? " GRAPPLE" : "");
                    for (int j = (int)hist[i].size() - 1; j >= 0; j--) if (hist[i][j].flags & MF_HOOKED) {   // the last grapple: from where to where
                        int k = j; while (k > 0 && (hist[i][k - 1].flags & MF_HOOKED)) k--;
                        printf("      last hook from %.1f %.1f %.1f to %.1f %.1f %.1f, let go at %.1f %.1f %.1f\n", hist[i][k].pos.x, hist[i][k].pos.y, hist[i][k].pos.z,
                               hist[i][j].hook.x, hist[i][j].hook.y, hist[i][j].hook.z, hist[i][j].pos.x, hist[i][j].pos.y, hist[i][j].pos.z);
                        break;
                    }
                }
            }
            was[i] = p.alive;
            if (p.alive && (p.move.flags & MF_ONGROUND)) ground[i] = p.move.pos;
            if (trace) {
                if (!p.alive) { hist[i].clear(); continue; }
                if (!histNow) continue;
                hist[i].push_back({s.time(), p.move.pos, p.move.vel, p.buttons, (uint8_t)p.move.flags, p.move.dashTimer > 0, p.move.hookPoint});
                if (hist[i].size() > 300) hist[i].erase(hist[i].begin());
            }
        }
    });
    for (auto& p : s.players_) if (p.connected) { r.kills += p.kills; r.deaths += p.deaths; }
    char st[160];
    int cp = 0; for (auto& p : s.players_) if (p.connected) cp = std::max(cp, (int)p.checkpoint);
    int out = 0; for (auto& p : s.players_) out += p.connected && p.out;
    snprintf(st, sizeof st, "score %d-%d  payload %.0f/%.0f  cp %d  round %d  out %d", s.ms_.scoreA, s.ms_.scoreB, s.ms_.payloadDist, s.ms_.payloadLen, cp, s.ms_.round, out);
    r.state = st;
    s.stop();
    std::ifstream lf(logPath);
    for (std::string line; std::getline(lf, line);) {
        size_t k = line.find(" kill killer=");
        if (k == std::string::npos) continue;
        int killer = atoi(line.c_str() + k + 13);
        size_t v = line.find(" victim=", k), w = line.rfind(" weapon=");
        if (v == std::string::npos || w == std::string::npos) continue;
        int victim = atoi(line.c_str() + v + 8), weapon = atoi(line.c_str() + w + 8);
        if (weapon == 252) r.falls++;
        else if (killer == victim) { r.self++; if (trace) printf("    self-kill: %s\n", weapon >= 0 && weapon < WPN_COUNT ? weaponDef(weapon).name : "?"); }
        else if (killer < 0) r.modeKills++;
    }
    lf.close();
    std::filesystem::remove(logPath);
    return true;
}

// Bots play every map in the mode it's built for, at advanced and at expert level (PT_SKILL=advanced|expert for
// one), and print what killed them (playBots), plus the objective state, to spot layouts where nothing happens or
// bots keep falling. PT_MAP / PT_MODE pick one map / mode; PT_TRACE prints how each fall happened.
static void playtestMaps(uint16_t port, double seconds) {
    std::vector<int> skills = {BOT_ADVANCED, BOT_EXPERT};
    if (const char* k = getenv("PT_SKILL")) skills = {std::string(k) == "expert" ? BOT_EXPERT : std::string(k) == "intermediate" ? BOT_INTERMEDIATE : BOT_ADVANCED};
    int totalFalls = 0, totalSelf = 0;
    for (auto& name : listMaps()) {
        if (getenv("PT_MAP") && name != getenv("PT_MAP")) continue;
        Map m;
        if (!m.load(name)) continue;
        int mode = m.preferredMode >= 0 ? m.preferredMode : MODE_FFA;
        if (getenv("PT_MODE")) mode = modeFromName(getenv("PT_MODE"));
        for (int skill : skills) {
            BotMatch r;
            if (!playBots(name, mode, skill, port, seconds, getenv("PT_TRACE") != nullptr, r)) { printf("  %s: start failed\n", name.c_str()); continue; }
            totalFalls += r.falls; totalSelf += r.self;
            printf("  %-10s %-8s %-8s kills %3d  deaths %3d  falls %2d (pushed %d)  self %2d  mode %d  %s\n", name.c_str(), modeName(mode),
                   skill == BOT_EXPERT ? "expert" : skill == BOT_ADVANCED ? "advanced" : "other", r.kills, r.deaths, r.falls, r.pushed, r.self, r.modeKills, r.state.c_str());
            fflush(stdout);
        }
    }
    printf("  total: falls %d, self-kills %d\n", totalFalls, totalSelf);
}

// Bots keep off the edges (they used to fall a lot here: grapple pulls over the moat, bunny hops and fight jumps
// off the island, knockback at the rim): a minute and a half of experts in 1v1 on a platform in a lava moat (the old
// Dojo's layout, built here: the shipped Dojo is a hall now, with no lava) and of expert tag runners on Foundry (lava
// trenches), with a couple of falls allowed for blasts and laser pushes.
static std::string lavaMoatMap() {
    // a 24 m platform, a 4.5 m lava moat round it (its bed 1.4 m down), a ring path at the platform's height outside
    // (a jump or a hook across), walls round it all; one spawn each side of the platform, facing in
    return "version " + std::to_string(kMapFormatVersion) + "\ntitle Lava moat\nfor 1v1\n"
           "box -12 -1.4 -12 12 0 12\n"                                                   // the platform
           "box -24 -3 -24 24 -1.4 24\n"                                                   // the moat's bed
           "lava -16.5 -1.4 -16.5 16.5 -1.2 16.5\n"
           "box -24 -1.4 -24 24 0 -16.5\nbox -24 -1.4 16.5 24 0 24\nbox -24 -1.4 -16.5 -16.5 0 16.5\nbox 16.5 -1.4 -16.5 24 0 16.5\n"   // the ring
           "box -25 -3 -25 25 8 -24\nbox -25 -3 24 25 8 25\nbox -25 -3 -24 -24 8 24\nbox 24 -3 -24 25 8 24\n"                       // walls
           "clip -25 8 -25 25 9 25\n"
           "spawn a -8 0 0 90\nspawn b 8 0 0 -90\nlight 0 6 0 30 1\n";
}
static void testBotFalls(uint16_t port) {
    printf("bots keep off the edges\n");
    const std::string moat = "zz_lavamoat", path = userMapsDir() + "/" + moat + ".qmap";
    CHECK(saveMapFile(path, lavaMoatMap()));
    {   // (the map is what it says: lava all round the platform, nothing else between it and the ring)
        Map m;
        std::string err;
        CHECK(m.parse(lavaMoatMap(), &err) && m.lava.size() == 1 && m.supportsMode(MODE_ARENA));
        if (!err.empty()) printf("  %s\n", err.c_str());
        for (const Vec3& p : {Vec3(0, -1.4f, 14), Vec3(14, -1.4f, 0), Vec3(-14, -1.4f, -14)}) CHECK(m.inLava(playerBox(p, false)));
        CHECK(!m.inLava(playerBox({0, 0, 0}, false)) && !m.inLava(playerBox({20, 0, 20}, false)));
    }
    const std::pair<std::string, int> cases[] = {{moat, MODE_ARENA}, {"foundry", MODE_TAG}};
    for (auto& [map, mode] : cases) {
        BotMatch r;
        bool ok = playBots(map, mode, BOT_EXPERT, port, 90, false, r);
        CHECK(ok);
        printf("  %s: %d falls (%d pushed), %d self-kills, %d deaths\n", map == moat ? "lava moat" : map.c_str(), r.falls, r.pushed, r.self, r.deaths);
        CHECK(r.falls <= 2 && r.self <= 1);
    }
    std::remove(path.c_str());
}

// Team order (a map worker, 2026-10-30: "the team listed second scores about 3% more"): one seeded match of expert
// bots, team A's n added before team B's or after them (so A has the low slot ids or the high ones); team A's score
// and kills, and both teams' totals.
struct OrderRun { int scoreA = 0, scoreB = 0, killsA = 0, killsB = 0; bool ok = false; };
static OrderRun orderRun(const std::string& map, int mode, uint32_t seed, bool bFirst, double seconds, int perTeam, uint16_t port) {
    OrderRun r;
    SimClock clock;
    qsrand(seed);
    Server s;
    ServerConfig cfg;
    cfg.map = map; cfg.mode = mode; cfg.port = port; cfg.autoStart = true;   // (one port for every run: each stops first)
    s.setLog([](const std::string&) {});
    bool started = false;
    for (int k = 0; k < 2 && !started; k++) { started = s.start(cfg); if (!started) cfg.port = (uint16_t)(port + 1); }
    if (!started) return r;
    for (int side = 0; side < 2; side++)
        for (int i = 0; i < perTeam; i++) s.addBot((side == 0) != bFirst ? TEAM_A : TEAM_B, BOT_EXPERT);
    simNavReady(s);
    simRun(s, 1.0);
    s.command("forcestart");
    simRun(s, seconds);
    r.scoreA = s.ms_.scoreA; r.scoreB = s.ms_.scoreB;
    for (auto& p : s.players_) if (p.connected) (p.team == TEAM_A ? r.killsA : r.killsB) += p.kills;
    r.ok = true;
    s.stop();
    return r;
}
// Over seeds: team A's share of the kills (and of the score) listed first and listed second, each with its standard
// error (raw: each run's line too, "R seed bFirst killsA killsB scoreA scoreB").
struct OrderBias { double first = 0, second = 0, seFirst = 0, seSecond = 0, scoreFirst = 0, scoreSecond = 0; int n = 0; };
static OrderBias orderBias(const std::string& map, int mode, int seeds, double seconds, int perTeam, uint16_t port, int firstSeed = 0, bool raw = false) {
    OrderBias b;
    std::vector<double> share[2], score[2];
    for (int k = firstSeed; k < firstSeed + seeds; k++)
        for (int bf = 0; bf < 2; bf++) {
            OrderRun r = orderRun(map, mode, 0x0D3E0000u + (uint32_t)k, bf != 0, seconds, perTeam, port);
            if (!r.ok) continue;
            if (raw) printf("R %d %d %d %d %d %d\n", k, bf, r.killsA, r.killsB, r.scoreA, r.scoreB);
            int kt = r.killsA + r.killsB, st = r.scoreA + r.scoreB;
            share[bf].push_back(kt ? (double)r.killsA / kt : 0.5);
            score[bf].push_back(st ? (double)r.scoreA / st : 0.5);
        }
    auto mean = [](const std::vector<double>& v) { double m = 0; for (double x : v) m += x; return v.empty() ? 0.5 : m / v.size(); };
    auto se = [&](const std::vector<double>& v) {
        if (v.size() < 2) return 1.0;
        double m = mean(v), q = 0;
        for (double x : v) q += (x - m) * (x - m);
        return std::sqrt(q / (v.size() - 1) / v.size());
    };
    b.first = mean(share[0]); b.second = mean(share[1]); b.seFirst = se(share[0]); b.seSecond = se(share[1]);
    b.scoreFirst = mean(score[0]); b.scoreSecond = mean(score[1]);
    b.n = (int)share[0].size();
    return b;
}

// The tick's order doesn't favour a slot: every bot thinks on the same world (as the tick found it: nobody has moved,
// shot or died yet this tick), each once, and who thinks and moves first turns tick by tick. Then the statistics: team
// A's kill share with its bots added first or second (low slots or high) is the same within the noise.
// Sloped glass (a map review, 2026-10-30: Museum's glass dome, four cut boxes, drew as four 40 x 6 m slabs over the
// roof): a pane on a wedge or a cut is its shape everywhere: what's drawn (slopeTriangles, Renderer::translucentBrush),
// what players stand on and shots hit, which pane a shot is in (Map::glassAt) and where its shards start.
static void testSlopedGlass() {
    printf("sloped glass: drawn, solid, hit and shattered as its shape, not its box\n");
    // two breakable wedges in one box (rising toward +x and toward -x: each box holds the other's shape), and plain glass
    // on a cut box (x/8 + y/4 <= 1 over its fractions: the same slope as the first wedge's, falling toward +x)
    Map m;
    std::string err;
    const bool ok = m.parse("version 11\nbox -20 -1 -20 20 0 20\nspawn any 0 0 10\n"
                            "wedge 37 0 0 0 -3 8 4 3\nwedge 37 1 0 0 -3 8 4 3\ncut 21 1 1 1 0 1 0 0 6 8 4 12\n", &err);
    CHECK(ok && m.glass.size() == 2 && m.windows.size() == 1);
    if (!ok || m.glass.size() != 2 || m.windows.size() != 1) { printf("  %s\n", err.c_str()); return; }
    const Brush &up = m.brushes[m.glass[0]], &down = m.brushes[m.glass[1]], &cut = m.brushes[m.windows[0]];
    CHECK(up.shape == BRUSH_WEDGE && down.shape == BRUSH_WEDGE && cut.shape == BRUSH_CUT);
    auto around = [](Vec3 c, float r) { return AABB{c - Vec3(r, r, r), c + Vec3(r, r, r)}; };
    // drawn: every corner on the shape, and the faces' area the wedge's (bottom 48, slope 6 sqrt 80, the high end 24, two
    // ends 16 each: 157.7), not its box's (208)
    for (const Brush* b : {&up, &cut}) {
        float area = 0;
        int tris = 0, off = 0;
        slopeTriangles(*b, [&](const Vec3& a, const Vec3& c, const Vec3& d) {
            area += 0.5f * length(cross(c - a, d - a)); tris++;
            for (const Vec3* q : {&a, &c, &d}) off += !brushNear(*b, *q, 1e-3f);
        });
        printf("  %s: %d triangles, %.1f m2 of surface (its box: 208.0), %d corners off it\n", b == &up ? "wedge" : "cut", tris, area, off);
        CHECK(tris >= 8 && off == 0 && std::fabs(area - 157.67f) < 0.5f);
    }
    // solid only under the slope: air over the low end of the box, glass under the high end
    CHECK(m.boxSolid(around({1, 3, 9}, 0.1f)) && !m.boxSolid(around({7, 3, 9}, 0.1f)) && m.boxSolid(around({7, 0.2f, 9}, 0.1f)));   // (the cut: high at x 0)
    // shots and sight hit its surface: straight down onto the cut at x 7 meets it at 0.5 m, not the box's top at 4
    float t = m.raycast({7, 10, 9}, {0, -1, 0}, 50);
    printf("  a shot down onto the cut at x 7 hits at y %.2f (the box's top: 4)\n", 10 - t);
    CHECK(std::fabs(10 - t - 0.5f) < 0.02f);
    // standing on it: a player box over x 7 comes down onto the slope, not the box's top
    {
        AABB pb = around({7, 3, 9}, 0.2f);
        const float dy = m.sweepAxis(pb, 1, -5.0f);
        CHECK(pb.mn.y + dy < 1.2f && pb.mn.y + dy > 0.45f);   // (the slope's 0.5 m to 0.75 under the box's 0.4 m width)
    }
    // which pane a point is in: inside the box of both wedges, but only in the down wedge's shape (high at x 0)
    CHECK(m.glassAt({1, 2, 0}) == (int)m.glass[1]);
    CHECK(m.glassAt({7, 2, 0}) == (int)m.glass[0]);
    CHECK(m.glassAt({4, 3.9f, 0}) == -1);   // (over both slopes' meeting, under the box's top)
    // shards start in the shape (the same draws for a box: none moved)
    std::vector<GlassShard> inShape, inBox;
    glassShards(up.box, GLASS_SHATTER, {4, 1, 0}, {}, 12345u, inShape, &up);
    glassShards(up.box, GLASS_SHATTER, {4, 1, 0}, {}, 12345u, inBox);
    int outS = 0, outB = 0;
    for (auto& g : inShape) outS += !brushNear(up, g.pos, 1e-3f);
    for (auto& g : inBox) outB += !brushNear(up, g.pos, 1e-3f);
    printf("  shattered: %zu shards, %d off the wedge (%d with only its box)\n", inShape.size(), outS, outB);
    CHECK(!inShape.empty() && inShape.size() == inBox.size() && outS == 0 && outB > 0);
    Brush plain = up; plain.shape = BRUSH_BOX;
    std::vector<GlassShard> b1, b2;
    glassShards(plain.box, GLASS_SHOT, {4, 1, 0}, {0, 0, 1}, 777u, b1, &plain);
    glassShards(plain.box, GLASS_SHOT, {4, 1, 0}, {0, 0, 1}, 777u, b2);
    bool same = b1.size() == b2.size();
    for (size_t k = 0; same && k < b1.size(); k++) same = length(b1[k].pos - b2[k].pos) == 0 && length(b1[k].vel - b2[k].vel) == 0;
    CHECK(same);
    // breaking one leaves the other: a shot through the down wedge breaks it, and the up one still stands
    CHECK(m.breakGlass(m.glass[1]) && m.glassAt({1, 2, 0}) == -1 && m.glassAt({7, 2, 0}) == (int)m.glass[0]);
    CHECK(!m.boxSolid(around({1, 3, 0}, 0.1f)) && m.boxSolid(around({7, 3, 0}, 0.1f)));   // (the up wedge alone now: its low end is air)
}

static void testTickOrder(uint16_t port) {
    printf("tick order: bots think on one world, first mover turns, no slot advantage\n");
    SimClock clock;
    qsrand(0x0D3E);
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_VS; cfg.port = port; cfg.autoStart = true;
    s.setLog([](const std::string&) {});
    CHECK(s.start(cfg));
    for (int i = 0; i < 4; i++) { s.addBot(TEAM_A, BOT_EXPERT); s.addBot(TEAM_B, BOT_EXPERT); }
    simNavReady(s);
    simRun(s, 1.0);
    s.command("forcestart");
    static Server* srv;
    static uint32_t tickSeen;
    static uint64_t worldSeen;
    static int thinks, mixed, firstOf[kMaxPlayers], sameTick;
    static bool seenThis[kMaxPlayers];
    srv = &s; tickSeen = ~0u; thinks = mixed = sameTick = 0;
    for (int& f : firstOf) f = 0;
    g_botThinkHook = [](Server& sv, int id) {
        if (&sv == srv) {
            uint64_t h = 1469598103934665603ull;   // (everyone's movement, health and life: what a command changes)
            auto mix = [&](const void* p, size_t n) { for (size_t k = 0; k < n; k++) h = (h ^ ((const uint8_t*)p)[k]) * 1099511628211ull; };
            for (auto& p : sv.players_) if (p.connected) { mix(&p.move.pos, sizeof p.move.pos); mix(&p.move.vel, sizeof p.move.vel); mix(&p.health, sizeof p.health); mix(&p.alive, 1); }
            if (sv.tick_ != tickSeen) { tickSeen = sv.tick_; worldSeen = h; firstOf[id]++; for (bool& b : seenThis) b = false; }
            else if (h != worldSeen) mixed++;
            if (seenThis[id]) sameTick++;
            seenThis[id] = true;
            thinks++;
        }
        return botThinkDirect(sv, id);
    };
    simRun(s, 20.0);
    g_botThinkHook = nullptr;
    int firsts = 0;
    for (int i = 0; i < kMaxPlayers; i++) firsts += firstOf[i] > 0;
    printf("  %d thinks, %d on a world another command had changed, %d twice in a tick, %d different first thinkers\n", thinks, mixed, sameTick, firsts);
    CHECK(thinks > 1000 && mixed == 0 && sameTick == 0 && firsts == 8);
    s.stop();
    // the statistics (quark_tests orderbias for more: 600 seeds, a few minutes)
    OrderBias b = orderBias("bastion", MODE_VS, 40, 40, 4, (uint16_t)(port + 2));
    double se = std::sqrt(b.seFirst * b.seFirst + b.seSecond * b.seSecond);
    printf("  bastion vs, %d seeds: team A's kill share added first %.3f, second %.3f (diff %+.3f, se %.3f)\n", b.n, b.first, b.second, b.second - b.first, se);
    CHECK(b.n == 40 && std::fabs(b.second - b.first) < 4 * se + 0.01);
}

// Bomb: the attacking bots go for every site in turn (site round + rank: they used to take the first one every round,
// and Datacenter's B, 8 m further, never saw them). Rounds of expert bots on Depot (4 v 4, two sites): which site each
// plant was in; both get some. The defenders guard one each (dealt out by rank), so a site isn't left open either.
static void testBombSites(uint16_t port) {
    printf("bomb: bots plant at every site, round by round\n");
    SimClock clock;
    qsrand(0xB0B5u);
    Server s;
    ServerConfig cfg;
    cfg.map = "depot"; cfg.mode = MODE_BOMB; cfg.port = port; cfg.autoStart = true;
    s.setLog([](const std::string&) {});
    bool started = false;
    for (int k = 0; k < 20 && !started; k++) { started = s.start(cfg); if (!started) cfg.port = ++port; }
    CHECK(started);
    if (!started) return;
    for (int i = 0; i < 4; i++) { s.addBot(TEAM_A, BOT_EXPERT); s.addBot(TEAM_B, BOT_EXPERT); }
    simNavReady(s);
    simRun(s, 1.0);
    s.command("forcestart");
    const size_t n = s.map_.bombSites.size();
    std::vector<int> plants(n, 0);
    int lastRound = -1, rounds = 0, defended[2] = {0, 0};
    bool was = false;
    simRun(s, 600, [&] {
        const ModeState& m = s.ms_;
        if (m.round != lastRound && m.phase == PHASE_LIVE) {   // (each round once it's live: where the defenders are headed)
            lastRound = m.round; rounds++;
            int rank = 0;   // (teamRank: the defenders in slot order)
            for (int i = 0; i < kMaxPlayers; i++)
                if (s.players_[i].connected && s.players_[i].isBot && s.players_[i].team != m.attackers) defended[rank++ % n % 2]++;
        }
        const bool now = m.bombState == BOMB_PLANTED;
        if (now && !was) {
            size_t best = 0;
            for (size_t k = 1; k < n; k++) if (length(s.map_.bombSites[k].center() - m.bombPos) < length(s.map_.bombSites[best].center() - m.bombPos)) best = k;
            plants[best]++;
        }
        was = now;
    });
    printf("  depot: %d rounds, plants at A %d, B %d; defenders sent to A %d, B %d\n", rounds, plants[0], n > 1 ? plants[1] : 0, defended[0], defended[1]);
    CHECK(n == 2);
    for (size_t k = 0; k < n; k++) CHECK(plants[k] >= 1);
    CHECK(defended[0] >= 1 && defended[1] >= 1);
    s.stop();
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
        CHECK(m.parse("title\n" + base) && m.title.empty());       // a bare "title": none (every parse starts from a fresh map)
        CHECK(m.parse("title  Hi there\t\r\n" + base) && m.title == "Hi there");   // (the spaces around it dropped)
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
    CHECK(downloadedMapPath(mapContentId("../../x")).find("..") == std::string::npos);
    // custom rules: NaN / inf keep the default, out of range is clamped
    CustomRules def, r;
    r.fromText("custommode=x\ngravity=nan\nspeed=inf\njump=-inf\nknockback=99\nweapon=0,nan,nan,nan\n");
    CHECK(r.gravity == def.gravity && r.moveSpeed == def.moveSpeed && r.jump == def.jump && r.knockback == 5);
    CHECK(r.fireRate[0] == def.fireRate[0] && r.damage[0] == def.damage[0] && r.projSpeed[0] == def.projSpeed[0]);
}

// Test ports: the numbers in main() are offsets into a per-run block (TP), so two test runs on one machine
// (two checkouts, CI + local) don't fight over 28530 and friends. Picked once before the groups fork.
// Blocks stay out of the OS's ephemeral range: every client socket (the tests' own, and any other program's) gets a
// port from that range, so a block inside it could find one of its server ports taken mid-run.
static int g_portShift = 0;
static uint16_t TP(int port) { return (uint16_t)(port + g_portShift); }
static std::vector<int> portBlockShifts(int ephLo, int ephHi) {
    // the tests use ports 28500 .. ~30100 (+ the shift): blocks of 2000 from 10500 up that are clear of the range
    std::vector<int> shifts;
    for (int shift = -18000; shift <= 36000; shift += 2000) {
        int lo = 28500 + shift, hi = 30500 + shift;
        if (hi <= 65535 && (hi < ephLo || lo > ephHi)) shifts.push_back(shift);
    }
    if (shifts.empty()) shifts.push_back(0);
    return shifts;
}
static void ephemeralPortRange(int& lo, int& hi) {
    lo = 49152, hi = 65535;   // the IANA range (Windows, macOS)
#ifdef __linux__
    std::ifstream f("/proc/sys/net/ipv4/ip_local_port_range");
    int a = 0, b = 0;
    if (f >> a >> b && a > 0 && b >= a && b <= 65535) { lo = a; hi = b; }
#endif
}
static void pickPortBlock() {
    // (Windows: the parent's block, handed to each group's process, which can't inherit it through fork as on Linux)
    if (const char* e = getenv("QUARK_TEST_PORTSHIFT"); e && *e) { g_portShift = atoi(e); return; }
#ifdef _WIN32
    unsigned seed = (unsigned)std::chrono::steady_clock::now().time_since_epoch().count();
#else
    unsigned seed = (unsigned)getpid();
#endif
    int ephLo, ephHi;
    ephemeralPortRange(ephLo, ephHi);
    std::vector<int> shifts = portBlockShifts(ephLo, ephHi);
    for (size_t k = 0; k < shifts.size(); k++) {   // from a per-run start, the first block whose sample ports are free
        int shift = shifts[(seed + k) % shifts.size()];
        bool ok = true;
        for (int p : {28520, 28700, 28900, 29300, 29600, 29997}) { UdpSocket s; if (!s.open((uint16_t)(p + shift))) { ok = false; break; } }
        if (ok) { g_portShift = shift; return; }
    }
    g_portShift = shifts[seed % shifts.size()];
}
// The blocks never overlap the ephemeral range (Linux's default 32768-60999, the IANA 49152-65535, this machine's).
static void testPortBlocks() {
    printf("test port blocks: clear of the ephemeral port range\n");
    int lo, hi;
    ephemeralPortRange(lo, hi);
    for (auto [a, b] : std::vector<std::pair<int, int>>{{32768, 60999}, {49152, 65535}, {lo, hi}}) {
        std::vector<int> s = portBlockShifts(a, b);
        CHECK(s.size() >= 2);
        for (int shift : s) CHECK(28500 + shift >= 1024 && (30500 + shift < a || 28500 + shift > b));
    }
    CHECK(30500 + g_portShift < lo || 28500 + g_portShift > hi);
    printf("  ephemeral %d-%d, block %d-%d\n", lo, hi, 28500 + g_portShift, 30500 + g_portShift);
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
    CHECK(t.load("qk1"));
    { std::ofstream(old / "maps" / "qk1.qmap", std::ios::binary) << t.source; }          // untouched shipped map
    { std::ofstream(old / "maps" / "sewer.qmap", std::ios::binary) << t.source << "\n"; }   // edited one
    fs::create_directories(dst, ec);
    { std::ofstream(dst / "quark.key") << "newer\n"; } // never overwritten
    CHECK(migrateUserData(old.string(), dst.string()) == 5);
    auto rd = [](const fs::path& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); };
    CHECK(rd(dst / "quark.cfg") == "sens=2\n" && rd(dst / "quark.key") == "newer\n" && rd(dst / "demos" / "a.qdemo") == "demo");
    CHECK(fs::exists(dst / "maps" / "mine.qmap") && fs::exists(dst / "maps" / "sewer.qmap") && fs::exists(dst / "maps" / "downloads" / "x-0000.qmap"));
    CHECK(!fs::exists(dst / "maps" / "qk1.qmap") && !fs::exists(dst / "maps" / "mine.qlm"));
    CHECK(fs::exists(old / "quark.cfg") && fs::exists(old / "maps" / "mine.qmap")); // originals kept
    CHECK(migrateUserData(old.string(), dst.string()) == 0);                         // idempotent
    fs::remove_all(old, ec);
    fs::remove_all(dst, ec);
    // shipped maps are read from the install; your maps dir is searched first and listed too
    CHECK(userMapsDir().find(userDataPath()) == 0 && downloadedMapPath(MapId()).find(userDataPath()) == 0);
    std::vector<std::string> ml = listMaps();
    CHECK(std::find(ml.begin(), ml.end(), "qk1") != ml.end());
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
#include "weapon_tests.h"  // weapon sets (.qweapons: the weapons editor's) + the exportweapons / checkweapons tools
#include "server_tests.h"  // server / netcode tests (FINAL_REVIEW 2.8-2.11, 6.4, 6.5, clock, PERF-8 / 9, ENG-2)
#include "layout_tests.h"  // the layout engine and the HUD theme (layout_cases.json, cache, allocations, layoutbench)
#include "governor_tests.h" // two servers on one machine: the load governor, ranked never rated on testing
#include "moveexpr_tests.h" // movement equations (custom modes' expr.<key>= lines): parser, resolver, determinism, pmove, network


// ---- BENCHMARK (src/game/benchmark.h): the frame-time statistics on arrays whose answers are known, and a demo like
// the shipped one (Hamlet, free for all, 31 bots, recorded by a spectator) made and played back to its end.
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
static void testBenchStats() {
    printf("benchmark: frame-time stats (avg, 1%% / 0.1%% lows, min, stdev) and the graph\n");
    std::vector<float> scratch;
    {   // 990 frames of 10 ms, 9 of 20 ms, one of 50 ms (in a shuffled order: the answer can't depend on it)
        std::vector<float> t(990, 0.010f);
        t.insert(t.end(), 9, 0.020f);
        t.push_back(0.050f);
        std::mt19937 rng(7);
        std::shuffle(t.begin(), t.end(), rng);
        const std::vector<float> before = t;
        bench::Stats st = bench::compute(t.data(), t.size(), scratch);
        CHECK(t == before);   // (the input isn't reordered: the run's frames stay in order for the graph)
        CHECK(st.frames == 1000 && near(st.seconds, 10.13, 1e-4));
        CHECK(near(st.avgFps, 1000 / 10.13, 1e-3) && near(st.avgMs, 10.13, 1e-4));
        CHECK(near(st.p99Ms, 20, 1e-4) && near(st.low1, 50, 1e-3));      // element 990 of the sorted times
        CHECK(near(st.p999Ms, 50, 1e-4) && near(st.low01, 20, 1e-3));    // element 999
        CHECK(near(st.worstMs, 50, 1e-4) && near(st.minFps, 20, 1e-3));
        // population stdev: sqrt((990 * 0.13^2 + 9 * 9.87^2 + 39.87^2) / 1000) ms
        CHECK(near(st.stdevMs, std::sqrt((990 * 0.13 * 0.13 + 9 * 9.87 * 9.87 + 39.87 * 39.87) / 1000), 1e-3));
        CHECK(bench::graphTopMs(st) == 80);   // (50 ms x 1.25 = 62.5: the next of 5, 10, 20, 40, 80)
    }
    {   // a steady 3 ms: no spread, every low the average
        std::vector<float> t(5000, 0.003f);
        bench::Stats st = bench::compute(t.data(), t.size(), scratch);
        CHECK(near(st.avgFps, 333.33, 1e-1) && near(st.low1, 333.33, 1e-1) && near(st.low01, 333.33, 1e-1) && near(st.minFps, 333.33, 1e-1));
        CHECK(st.stdevMs < 1e-4);
        CHECK(bench::graphTopMs(st) == 5);    // (never under 5: the 240 fps line stays on the graph)
    }
    {   // 1, 2 .. 100 ms: index floor(n * p) of the sorted times
        std::vector<float> t;
        for (int i = 100; i >= 1; i--) t.push_back(i * 0.001f);
        bench::Stats st = bench::compute(t.data(), t.size(), scratch);
        CHECK(near(st.p99Ms, 100, 1e-3) && near(st.p999Ms, 100, 1e-3) && near(st.worstMs, 100, 1e-3));   // (99 and 99.9: both the last)
        CHECK(near(st.avgMs, 50.5, 1e-3) && near(st.stdevMs, std::sqrt((100.0 * 100 - 1) / 12), 1e-3));
        CHECK(bench::pctIndex(100, 0.99) == 99 && bench::pctIndex(1000, 0.99) == 990 && bench::pctIndex(1000, 0.999) == 999 && bench::pctIndex(1, 0.999) == 0);
    }
    {   // one frame, and none
        float one = 0.008f;
        bench::Stats st = bench::compute(&one, 1, scratch);
        CHECK(st.frames == 1 && near(st.avgFps, 125, 1e-2) && near(st.low01, 125, 1e-2) && near(st.minFps, 125, 1e-2) && st.stdevMs < 1e-6);
        bench::Stats z = bench::compute(nullptr, 0, scratch);
        CHECK(z.frames == 0 && z.avgFps == 0 && z.minFps == 0);
    }
    {   // the graph: each column the slowest and the mean frame of its share, in order
        const float t[10] = {0.001f, 0.003f, 0.002f, 0.002f, 0.010f, 0.004f, 0.001f, 0.001f, 0.005f, 0.007f};
        std::vector<float> mx, mean;
        bench::graphColumns(t, 10, 5, mx, mean);
        CHECK(mx.size() == 5 && mean.size() == 5);
        CHECK(near(mx[0], 3, 1e-4) && near(mean[0], 2, 1e-4) && near(mx[2], 10, 1e-4) && near(mean[2], 7, 1e-4) && near(mx[4], 7, 1e-4) && near(mean[4], 6, 1e-4));
        bench::graphColumns(t, 3, 464, mx, mean);   // (fewer frames than columns: a column each)
        CHECK(mx.size() == 3 && near(mx[1], 3, 1e-4));
    }
}

// A benchmark demo as the shipped one is made: Hamlet, free for all, 31 bots (expert) and a spectator recording.
// `quark_tests benchdemo OUT [seconds]` writes one for `quark --benchmark OUT` (tools/benchmark_check.sh); the group
// makes a short one and checks it plays to its end with the bots in it.
static bool recordBenchDemo(const std::string& path, double seconds, uint16_t port) {
    Server s;
    ServerConfig cfg;
    cfg.map = "hamlet"; cfg.mode = MODE_FFA; cfg.port = port; cfg.maxPlayers = kMaxPlayers;
    cfg.vsBots = true; cfg.botsEnemy = kMaxPlayers - 1; cfg.botSkill = BOT_EXPERT; cfg.balanceBots = false; cfg.lanOnly = true;
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return false; } }
    NetClient a;
    a.connect("127.0.0.1:" + std::to_string(port), "camera");
    std::vector<NetClient*> cls = {&a};
    pump(s, cls, 1.0, idle);
    a.sendCommand("team spec");
    pump(s, cls, 0.5, idle);
    const bool rec = a.startRecording(path);
    pump(s, cls, seconds, idle);
    a.stopRecording();
    int bots = 0;
    for (auto& p : s.players_) if (p.connected && p.isBot) bots++;
    s.stop();
    CHECK(rec && bots == kMaxPlayers - 1);
    return rec;
}
static void testBenchDemo(uint16_t port) {
    printf("benchmark: a Hamlet demo with 31 bots plays back to its end (playbackDone)\n");
    const std::string path = userDataPath() + "/zz_bench.qdemo";
    if (!recordBenchDemo(path, 3.0, port)) return;
    NetClient p;
    std::string err;
    CHECK(p.startPlayback(path, err) && p.mapReady() && p.map().name == "hamlet");
    int most = 0;
    double end = timeNow() + 10;
    while (p.playback() && !p.playbackDone() && timeNow() < end) {
        p.poll();
        if (p.hasSnapshot()) { int n = 0; for (auto& q : p.latest().players) if ((q.flags & PS_CONNECTED) && q.team() != TEAM_SPECTATOR) n++; most = std::max(most, n); }
        hostSleepMs(2);
    }
    CHECK(p.playbackDone() && p.playbackLeft() < 0.05);
    CHECK(most == kMaxPlayers - 1);
}

int main(int argc, char** argv) {
    setExecutableDir(argv[0]);
    pinTestingBuildForTests(0);   // (production behaviour, whatever version CI built this as: governor.h)
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
    if (argc >= 2 && std::string(argv[1]) == "serverbench") {   // [secs] [run: 1..5, or all]
        double secs = argc > 2 ? atof(argv[2]) : 15;
        int only = argc > 3 ? atoi(argv[3]) : 0;
        if (!only || only == 1) serverBench(TP(29300), 12, 4, secs, "harbor");
        if (!only || only == 2) serverBench(TP(29301), 24, 8, secs, "hamlet");
        if (!only || only == 3) serverBench(TP(29302), 16, 4, secs, "midtown", MODE_POINTS);
        if (!only || only == 4) serverBench(TP(29303), 16, 0, secs * 2, "depot", MODE_BOMB);        // (rounds: everyone respawns at once)
        if (!only || only == 5) serverBench(TP(29304), 16, 0, secs * 2, "depot", MODE_BOMB, 125);   // (...at 125 Hz: a look period is 6 ticks)
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "signing") { testSigning(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "launcher") { g_fail += launcherTests(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "racefloors") { // the RACE_MAPS table for tools/master/worker.js
        if (rankedRaceMaps().empty()) return 1;
        printf("%s", raceMapsJs().c_str());
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "bounds") { g_fail += outOfBoundsTests(); return g_fail ? 1 : 0; }
    if (argc >= 2 && std::string(argv[1]) == "exprbench") return moveExprBench();
    if (argc >= 2 && std::string(argv[1]) == "navbench") { navBench(argc > 2 && std::string(argv[2]) != "all" ? argv[2] : nullptr, argc > 3 && std::string(argv[3]) == "surfaces"); return 0; }
    if (argc >= 2 && std::string(argv[1]) == "simhash") {   // [seeds]: the seeded FFA's every tick, hashed (tools/isa_check.sh)
        int n = argc > 2 ? atoi(argv[2]) : 3;
        for (int k = 0; k < n; k++) {
            uint64_t h = 1469598103934665603ull;
            FfaRun r = ffaVsExperts(TP(29070 + k % 10), 0x5EED0001u + k, &h);
            printf("seed %d: %016llx (%d kills)\n", k, (unsigned long long)h, r.kills);
        }
        return g_fail ? 1 : 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "ffaexperts") {   // the seeded FFA vs experts, over many seeds
        int n = argc > 2 ? atoi(argv[2]) : 10;
        for (int k = 0; k < n; k++) {
            FfaRun r = ffaVsExperts(TP(29070 + k % 10), 0x5EED0001u + k);
            printf("  seed %d: %d kills (%d of the player standing still), %d grapples\n", k, r.kills, r.playerDeaths, r.hooks);
        }
        return g_fail ? 1 : 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "orderbias") {   // [seeds] [map] [mode] [seconds] [per team]: team A listed first vs second
        int n = argc > 2 ? atoi(argv[2]) : 50;
        std::string map = argc > 3 ? argv[3] : "bastion";
        int mode = argc > 4 ? modeFromName(argv[4]) : MODE_VS;
        double secs = argc > 5 ? atof(argv[5]) : 120;
        int per = argc > 6 ? atoi(argv[6]) : 4;
        int from = argc > 7 ? atoi(argv[7]) : 0;
        g_legacyTickOrder = getenv("QUARK_LEGACY_ORDER") != nullptr;   // (the old order, to compare)
        OrderBias b = orderBias(map, mode < 0 ? MODE_VS : mode, n, secs, per, (uint16_t)(TP(29180) + from % 200 * 2), from, true);
        printf("%s %s, %d seeds, %.0f s, %d a side: team A's kill share listed first %.4f +- %.4f, second %.4f +- %.4f (diff %+.4f +- %.4f); score share %.4f / %.4f\n",
               map.c_str(), modeName(mode < 0 ? MODE_VS : mode), b.n, secs, per, b.first, b.seFirst, b.second, b.seSecond, b.second - b.first,
               std::sqrt(b.seFirst * b.seFirst + b.seSecond * b.seSecond), b.scoreFirst, b.scoreSecond);
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "exportassets") return exportAssets(argc > 2 ? argv[2] : "assets_export") ? 1 : 0;
    if (argc >= 2 && (std::string(argv[1]) == "exportbrain" || std::string(argv[1]) == "checkbrain" || std::string(argv[1]) == "brainast" || std::string(argv[1]) == "printbrain"))
        return brainTool(argv[1], argc > 2 ? argv[2] : nullptr);
    if (argc >= 2 && std::string(argv[1]) == "brainbench") return brainBench(TP(29170));
    if (argc >= 2 && (std::string(argv[1]) == "exportweapons" || std::string(argv[1]) == "checkweapons" || std::string(argv[1]) == "weaponsast" || std::string(argv[1]) == "printweapons"))
        return weaponTool(argv[1], argc > 2 ? argv[2] : nullptr);
    if (argc >= 2 && (std::string(argv[1]) == "exportmode" || std::string(argv[1]) == "checkmode" || std::string(argv[1]) == "modeast" || std::string(argv[1]) == "printmode"))
        return modeTool(argv[1], argc, argv);
    if (argc >= 2 && std::string(argv[1]) == "modebench") return modeBench();
    if (argc >= 2 && std::string(argv[1]) == "layoutbench") return layoutBench();
    if (argc >= 3 && std::string(argv[1]) == "benchdemo")   // a benchmark demo for `quark --benchmark FILE` (tools/benchmark_check.sh)
        return recordBenchDemo(argv[2], argc > 3 ? atof(argv[3]) : 20, TP(29175)) && !g_fail ? 0 : 1;
    if (argc >= 2 && std::string(argv[1]) == "snapbench") {   // PERF-8 / PERF-9: snapshot + visibility CPU per send
        double secs = argc > 2 ? atof(argv[2]) : 8;
        snapBench("midtown", 32, secs, TP(28820));
        snapBench("midtown", 8, secs, TP(28821));
        snapBench("harbor", 16, secs, TP(28822));
        snapBench("midtown", 32, secs, TP(28823), true, 48);   // (projectiles: each one's visibility to each client)
        snapBench("harbor", 16, secs, TP(28824), true, 24);
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "rules") { testGameRules(argv[2]); return g_fail ? 1 : 0; }   // (one rules test by name)
    // ---- the whole suite. Most tests wait on real-time network play rather than the CPU, so each runs in its own
    // process, several at once (output kept in order); a few timing-sensitive ones run alone afterwards.
    // "quark_tests serial" runs everything in this process, one after another, as before.
    struct T { const char* name; std::function<void()> run; };
    std::vector<T> par = {   // (longest first: they set the wall time. Real seconds on an idle 4-core box, 12 at a time:
                             //  rules / governor ~21, modelockstep / modelockstep2 ~25 (CPU: two servers stepped tick by tick),
                             //  rules2 ~18, weapons ~16, mapbounds / mapbounds2 / maps ~12-14, server / modes ~10, ...
                             //  duel ~3, tourney ~1: the countdowns skipped, botaim ~1 and brains ~4: the manual clock)
        {"modelockstep", [] { g_fail += modeLockstepTests(0, 2); }},   // (every other mode each)
        {"modelockstep2", [] { g_fail += modeLockstepTests(1, 2); }},
        {"rules", [] { testGameRules("", 1); }},
        // (its own governor dir and ports; the load is the players' reported loss, stalls left out: beside the others.
        //  testGovernorPolicy and testRankedNotRated run in "server")
        {"governor", [] { testGovernorTwoServers(TP(28714)); }},
        {"rules2", [] { testGameRules("", 2); }},
        {"weapons", [] { testFixedSpread(TP(28555)); testWeapons(TP(28530)); testRocketSelfDamage(TP(28540)); testAltFires(TP(28545)); testRocketLob(TP(28550)); testRocketThrust(TP(28501)); testProjectileTickRates(TP(28556)); testStickyOnPlayers(TP(28557)); testStuckProps(TP(28558)); testTimeToKill(); testWallbang(); testFeelFixes(); testTapBuffer(); testWeaponSets(TP(28559)); }},
        // (nothing reachable out of bounds, tests/mapbounds.cpp: every other map each; `quark_tests bounds` runs them all)
        {"mapbounds2", [] { g_fail += outOfBoundsTests(1, 2); }},
        {"maps", [] { testMaps(); testWalkGrid(); testAllMaps(); testCartSweep(); testSpawnPools(); testSpawnSanity(); testStairs(); testLadders(); }},
        {"slopes", [] { testSlopes(); }},
        {"surf", [] { testSurf(); }},
        {"groupslopes", [] { testGroupSlopes(); }},
        {"materials", [] { testMaterials(); testProjectileMaterials(TP(28931)); testDestruction(TP(28932)); testClientHits(); }},
        {"mapbounds", [] { g_fail += outOfBoundsTests(0, 2); }},
        {"server", [] { serverTests(); testGovernorPolicy(); testRankedNotRated(TP(28632)); }},
        {"modes", [] { testCustomMode(TP(28570)); testMoveExprNetwork(TP(28576)); testPoints(TP(28571)); testSyringe(TP(28572)); testCartChecks(TP(28573)); testItModes(TP(28574)); testTagLateJoin(TP(28579)); testBotGoalsAfterMapChange(TP(28578)); }},
        {"votes", [] { testRejoin(TP(28590)); testMapVote(TP(28591)); testChatAndVoteLimits(TP(28592)); testHandshake(TP(28594)); testReachProbe(TP(28595)); testKickByName(TP(28596)); testAddbotArgs(TP(28597)); testSpecVote(TP(28598)); testNameColor(TP(28599)); testVoteModifiers(TP(28593)); }},
        {"sessions", [] { testSession(MODE_VS, "bastion", TP(28560)); testSession(MODE_BOMB, "bastion", TP(28561)); testSession(MODE_KOTH, "bastion", TP(28562)); }},
        {"edit", [] { testSnapshotBounds(); testDemoLengthWrap(); testDemoUpgrade(); testReplayBound(TP(28604)); testMapNames(TP(28605)); testMapDownload(TP(28600)); testEditSession(TP(28601)); testBuiltInMapEditSession(TP(28606)); testVotedEdit(TP(28602)); testDemo(TP(28603)); }},
        {"bots", [] { uint16_t p = TP(28900); testBots(p); }},
        {"ranked", [] { testFilledParse(); testRanked(TP(28610)); testRankedVoteAndRestart(TP(28620)); testInternetList(TP(28625)); testMasterReplies(TP(28627)); testHelloRetry(); testMasterSignature(); testOfficialServers(); testPlacementConstant(); testRankedLockLadders(); }},
        {"ranked2", [] { testRankedLeaver(TP(28636)); testRankedLeaverPaths(TP(28639)); testRankedRelay(TP(28640)); testReconnectGrace(TP(28637)); testTourneySeeding(TP(28638)); }},
        {"race", [] { testRaceFloors(); testTimeTrial(TP(28580)); testPayloadPush(TP(28581)); testPacketLoss(TP(28582)); testCartDash(TP(28583)); testSemiAuto(TP(28584)); testRemoteMoveSounds(TP(28585)); testRefillPrediction(TP(28586)); testHookPrediction(TP(28588)); }},
        {"brains", [] { g_fail += brainTests(TP(29150)); }},
        {"sessions2", [] { testSession(MODE_PAYLOAD, "bastion", TP(28563)); testSession(MODE_FFA, "bastion", TP(28564)); testSession(MODE_RACE, "foundry", TP(28565)); }},
        {"duel", [] { testDuel(TP(28520)); testDuel2v2(TP(28521)); testWinnerStays(TP(28522)); testWinnerStaysMode(TP(28523)); testAccuracyPerWeapon(); }},
        {"movement", [] { testPortBlocks(); testProcPipes(); testSleepPrecision(); testCompletion(); testSigning(); g_fail += launcherTests(); testHostileInput(); testFeelSounds(); testJumpTapBuffer(); testHeldDash(); testStrafeFps(); testDashJumpWindows(); testMovement(); testGrappleAgainstWall(); testAcid(); testLiquids(); testFeatureBlocks(); testStrafeEase(); testSeeThrough(); testMovementTuning(); testDashCharges(); testDashTech(); testTickRateIndependence(); testMapSaveExact(); testMapIdentity(); testMapFormat(); testMapConverter(); testModeSpawnSets(); testMapFix(); testHumanMadeMaps(); testMapLimits(); testRulesValues(); testMoveExpr(); testMoveExprCallers(); testClipRays(); testAtomicWrite(); testShippedLightCache(); testGridQueries(); testEditorLadder(); testEditorGroups(); testTrimClipEdits(); testIncrementalEdits(); }},
        {"hidden", [] { testStickyInherit(); testHiddenInfo(TP(28587)); }},
        {"botfalls", [] { testBotFalls(TP(29060)); }},
        {"bombsites", [] { testBombSites(TP(29090)); testPlantKnockback(TP(29091)); }},
        {"tickorder", [] { testTickOrder(TP(29096)); }},
        {"slopedglass", [] { testSlopedGlass(); }},
        {"tourney", [] { uint16_t p = TP(29100); testArenaAndTourney(p); }},
        {"botaim", [] { uint16_t p = TP(29000); testNavGraph(); testBotNavBlockers(TP(29030)); testBotAim(p); testHostedBotsThreadExit(TP(29010)); testBotHurtTurn(TP(29011)); }},
        {"assets", [] { g_fail += assetTests(); }},
        {"modelogic", [] { g_fail += modeTests(); }},
        {"resetmaps", [] { testResetMaps(); testLegacyMaps(); }},   // (each forked group has its own user data dir: these write only there)
        {"userdata", [] { testUserData(); }},
        {"layout", [] { testLayoutCases(); testLayoutCache(); testHudTheme(); }},
        {"benchmark", [] { testBenchStats(); testBenchDemo(TP(29160)); }},
    };
    std::vector<T> alone = {   // timing-sensitive: after the others, on an otherwise idle machine
        {"tickrate", [] {   // (real-time: timingTest re-runs an attempt the host paused in)
            timingTest("tick rate fallback", [] { testTickRateFallback(TP(28710)); });
            timingTest("rate between rounds", [] { testRateBetweenRounds(TP(28711)); });
            timingTest("rate two windows", [] { testRateTwoWindows(TP(28713)); });
            timingTest("rate undo step up", [] { testRateUndoStepUp(TP(28712)); });
            timingTest("rate jitter", [] { testRateJitter(TP(28720)); });
            timingTest("upload cap", [] { testUploadCap(TP(28721)); });
            timingTest("shared congestion", [] { testSharedCongestion(TP(28722)); });
        }},
        {"netcode", [] { testInterpRewind(TP(28703)); testUptimeClock(TP(28702)); testClockWrap(TP(28804)); testNetcode(TP(28700)); testInputBacklog(TP(28701)); testCrossLanguageConstants(TP(28705)); }},
        // (the client's weapon prediction over a 40 ms network: in the rules group it missed its 0.8 s window in about 1
        //  full run of 3 with 12 groups on 4 cores, and in 10 of 20 runs beside 8 busy loops; ~1 in 30 even idle)
        {"weaponpredict", [] { timingTest("weapon prediction", [] { testGameRules("weaponpredict"); }); }},
    };
    // `groups` lists the groups (tools/release_check.sh runs each); `group NAME`, or just `NAME`, runs exactly that one
    // group in this process (`group NAME --child`: as one of the run's processes on Windows, below: no summary, and the
    // exit code is the failure count, at most 100, as a forked group's)
    if (argc >= 2 && std::string(argv[1]) == "groups") {
        for (auto* list : {&par, &alone})
            for (auto& g : *list) printf("%s\n", g.name);
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) != "serial") {
        const std::string name = argc >= 3 && std::string(argv[1]) == "group" ? argv[2] : argv[1];
        for (auto* list : {&par, &alone})
            for (auto& g : *list)
                if (name == g.name) {
                    double t0 = wallSeconds();
                    g.run();
                    if (argc >= 4 && std::string(argv[3]) == "--child") { printf("%s\n", kGroupEnd); fflush(stdout); return std::min(g_fail, 100); }
                    printf("group %s: %.0f s\n", g.name, wallSeconds() - t0);
                    printf(g_fail ? "%d FAILURE(S)\n" : "all tests passed\n", g_fail);
                    return g_fail ? 1 : 0;
                }
        printf("no test group '%s' (quark_tests groups lists them; the tools: playtest, serverbench, snapbench, ffaexperts, simhash,\n"
               "racefloors, bounds, exportassets, rules NAME, signing, launcher, bot, serial)\n", name.c_str());
        return 1;
    }
    // QUARK_TEST_ONLY=a,b,c runs just those groups, as the whole suite runs them (in parallel, then the alone ones)
    if (const char* e = getenv("QUARK_TEST_ONLY"); e && *e) {
        const std::string only = std::string(",") + e + ",";
        for (auto* list : {&par, &alone})
            list->erase(std::remove_if(list->begin(), list->end(), [&](const T& g) { return only.find(std::string(",") + g.name + ",") == std::string::npos; }), list->end());
    }
    const bool serial = argc >= 2 && std::string(argv[1]) == "serial";
    if (!serial) {
        // Groups that only compute (no real-time play: lockstep, map reach and walk checks, bots on the manual clock) run
        // at a lower priority, so the real-time groups beside them get the cores when they ask: with them at equal
        // priority the CPU-bound ~6 cores' worth starved real-time checks (the laser's damage in 0.5 s, payload's start)
        // in 2 of 3 full runs. Their own results don't depend on timing.
        static const char* const cpuOnly[] = {"modelockstep", "modelockstep2", "mapbounds", "mapbounds2", "maps", "brains", "modelogic"};
#ifdef _WIN32
        // the group processes use this process's port block (pickPortBlock), so the groups' TP() ports stay apart as
        // they do forked
        _putenv(("QUARK_TEST_PORTSHIFT=" + std::to_string(g_portShift)).c_str());
#endif
        // Runs a list of groups, `jobs` at a time, each in its own process (groupproc.h: forked on Linux / macOS, this
        // program again on Windows; output kept in the list's order).
        auto parallel = [](std::vector<T>& list, unsigned jobs) {
            struct Job { intptr_t proc; std::string out; size_t idx; double t0; };
            std::vector<Job> running;
            std::vector<std::string> outputs(list.size());
            std::vector<int> done(list.size(), -1);
            size_t next = 0, printed = 0;
            auto tmp = std::filesystem::temp_directory_path();
            // each group gets its own user data dir (maps, demos, keys) under this run's: the edit and ranked groups write
            // zz_* maps that the maps group would otherwise list and check. Removed when the group ends. (Windows: each
            // group's process makes its own, as every run of quark_tests does, and removes it as it exits.)
            auto groupDir = [&](size_t i) { return userDataPath(std::string("groups/") + list[i].name); };
            auto start = [&](size_t i) {
                std::string out = (tmp / ("quark_test_" + std::to_string(getpid()) + "_" + list[i].name + ".log")).string();
                std::string data = groupDir(i);
                bool low = false;
                for (const char* n : cpuOnly) low = low || std::strcmp(n, list[i].name) == 0;
                intptr_t proc = groupStart(list[i].name, out, low, [&list, i, data] {   // (the forked child)
                    std::error_code ec;
                    std::filesystem::remove_all(data, ec);
                    std::filesystem::create_directories(data, ec);   // (the parent made the root already: set it, then make it)
                    setUserDataDir(data);
                    qsrand(1234);
                    g_fail = 0;   // (this group's own: the parent's count of groups already done isn't this one's)
                    list[i].run();
                    fflush(stdout);
                    return std::min(g_fail, 100);
                });
                if (proc != -1) running.push_back({proc, out, i, wallSeconds()});
                else { outputs[i] = std::string("  FAIL ") + list[i].name + ": its process didn't start\n"; done[i] = 1; }
            };
            auto print = [&] {
                while (printed < list.size() && done[printed] >= 0) {
                    fputs(outputs[printed].c_str(), stdout);
                    if (done[printed]) printf("  (group %s: %d failure(s))\n", list[printed].name, done[printed]);
                    g_fail += done[printed];
                    printed++;
                }
            };
            while (next < list.size() && running.size() < jobs) start(next++);
            while (!running.empty()) {
                std::vector<intptr_t> procs;
                for (auto& j : running) procs.push_back(j.proc);
                int code = 0;
                std::string crash;
                int k = groupWait(procs, &code, &crash);
                if (k < 0) break;
                size_t i = running[k].idx;
                std::ifstream f(running[k].out);
                std::stringstream ss; ss << f.rdbuf();
                f.close();
                outputs[i] = ss.str();
#ifdef _WIN32
                size_t end = outputs[i].rfind(kGroupEnd);
                if (end != std::string::npos && outputs[i].find_first_not_of("\r\n", end + std::strlen(kGroupEnd)) == std::string::npos)
                    outputs[i].resize(end);
                else if (crash.empty())
                    crash = "ended without finishing (exit code " + std::to_string(code) + ")";
#endif
                std::remove(running[k].out.c_str());
                { std::error_code ec; std::filesystem::remove_all(groupDir(i), ec); }
                done[i] = crash.empty() ? code : 1;
                char took[96];
                snprintf(took, sizeof took, "  (group %s: %.0f s)\n", list[i].name, wallSeconds() - running[k].t0);
                outputs[i] += took;
                if (!crash.empty()) outputs[i] += std::string("  FAIL ") + list[i].name + ": " + crash + "\n";
                running.erase(running.begin() + k);
                print();
                while (next < list.size() && running.size() < jobs) start(next++);
            }
            print();
        };
        // 3 per core: the groups mostly wait on real-time play (the whole run uses ~65 CPU-seconds), so with one per
        // core the cores sat ~70% idle. 4 cores: 4 at a time 153 s, 8 at a time ~114, 12 ~102-117 (then the longest
        // group bounds it: tourney's 25 s then, rules' / governor's ~21 s now), each group taking as long as with 4.
        // Windows: 1 per core. At 1x (testTimeScale) a real-time group needs a quarter of the CPU per second it needs at
        // 4x, but under Wine on 4 vCPUs 3 per core missed 55-90 real-time checks a run (the compute-only groups niced
        // or not) and 1 per core none, in the same wall time (~490 s: the lockstep groups' CPU bounds it there).
        // QUARK_TEST_JOBS overrides it.
#ifdef _WIN32
        unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
#else
        unsigned jobs = 3 * std::max(1u, std::thread::hardware_concurrency());
#endif
        if (const char* e = getenv("QUARK_TEST_JOBS"); e && atoi(e) > 0) jobs = (unsigned)atoi(e);
        parallel(par, std::min(jobs, kGroupMaxJobs));
        // the timing-sensitive ones, one at a time on the idle machine (both at once saved ~5 s of ~150 and failed
        // netcode's clock checks 3 times in 10 full runs, against 2 in 10 one at a time)
        parallel(alone, 1);
    } else {
        for (auto& t : par) t.run();
        for (auto& t : alone) t.run();
    }
    netShutdown();
    printf(g_fail ? "%d FAILURE(S)\n" : "all tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
