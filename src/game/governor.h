// Load governor: a production and a testing dedicated server sharing one machine (tools/server/install.sh runs one
// of each on every server box). Each server writes a small status file into a shared directory about once a second
// (players, live ranked match, tick rate, whether it is overloaded) and reads the other's. When the machine can't
// carry both, the owner's order applies (governorStep below):
//   1. throttle the server nobody is on, one tick-rate step at a time, down to 125 Hz;
//   2. still overloaded (or nobody's empty): throttle the one NOT playing a ranked match (a ranked match with 0-1
//      players doesn't count); both alike: testing first;
//   3. still overloaded: throttle the other too;
//   4. both at 125 Hz and production still overloaded for closeAfter: CLOSE the testing server (its players are told
//      why, it leaves the list and idles at ~0 CPU). Production is never closed: a machine that can't carry
//      production alone still keeps it open (at 125 Hz, as a single server would).
// Recovery has hysteresis: caps rise one step at a time, each after `calm` seconds without any overload, in reverse
// order (the most important server first); testing reopens (at 125 Hz) once production has been calm for
// reopenAfter. A raise that brings the overload back doubles the next calm wait; a close soon after a reopen doubles
// the next reopen wait.
//
// Production is the arbiter: it runs governorStep on both statuses and publishes the decision with its own status;
// testing applies production's decision for itself. (Production is never closed, so the decision lives with the
// server that is always up, and the two can never disagree.) A missing or stale peer (the other server is down or
// restarting) puts no pressure on anyone: each server then manages its tick rate alone, exactly as a single server
// always has (except that a closed testing server stays closed for a minute while production restarts, e.g. for an
// update: production picks its last decision up again from its own status file). No governor directory (in-game
// hosting, plain quark_server): no governor at all.
//
// Mid-round (REVIEW GAME-11): a cap applies at once on a server nobody is on; on one with players it waits for the
// next break (warmup, round end, match end) like the server's own rate changes, unless the overload has lasted
// forceAfter or is severe (a server over 90% busy): then at once. While a lowered cap waits for its break, nothing
// else is lowered (unless forced). Raises always wait for a break.
//
// Cost: the file I/O and the decision run on the governor's own thread, once a second. The server thread hands over
// a POD status (a mutex-guarded copy, a few times a second) and reads the decision from atomics: nothing per tick
// allocates or touches the disk.
#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include "gamedefs.h"

enum GovChannel { GOV_PROD = 0, GOV_TESTING = 1 };
// What players of a closed testing server are told (dropped, or refused at connect).
constexpr const char* kGovernorClosedText = "testing server closed: this machine is too busy to run it next to the production server - "
                                            "it reopens by itself later (play on the production server meanwhile)";
const char* govChannelName(int ch);   // "prod" / "testing"
int govChannelFromName(const std::string& s);   // -1 if neither
// A version that isn't a production release (x.y.z): CI's testing builds, "1.19.0.testing.5" (from 1.19.0 on: the
// production VERSION plus .testing.<run>, newer than that release) or "1.18.9-test.42" (before: a semver prerelease).
bool isTestingVersion(const char* version);
// This build is a testing one (isTestingVersion(QUARK_VERSION)): it never rates ranked and its servers default to the
// testing channel. quark_tests pins it (0: production): CI builds the tests as a testing version on the testing branch,
// and the tests check the production behaviour, plus the testing one where they pin it to 1.
bool testingBuild();
void pinTestingBuildForTests(int testing);   // -1: by the version again

struct GovParams {           // seconds (wall clock: the decision survives a restart of production)
    double stepGap = 6;      // between two throttle steps while overloaded: two rate windows, so the last step's effect is measured
    double forceAfter = 20;  // overload this long (or a severe one: busy > 90%): caps apply mid-round, not at the next break
    double closeAfter = 30;  // both at 125 Hz and production still overloaded this long: testing closes
    double calm = 60;        // no overload this long before a cap rises one step (and between two raises)
    double calmMax = 900;    // (the calm wait doubles after a raise that brought the overload back, up to this)
    double reopenAfter = 300;// production calm this long after testing closed: testing reopens, at 125 Hz
    double reopenMax = 3600; // (doubles when testing has to close again within `relapse` of reopening)
    double relapse = 1800;
    double stale = 5;        // a status older than this: that server is down (no pressure from it)
};

struct GovSide {             // one server, as its status file says
    bool up = false;         // a fresh status (else: down, no pressure from it, nothing decided for it)
    bool closed = false;     // testing: closed right now
    int players = 0;         // humans connected
    int rankedPlayers = 0;   // players in its live ranked match (0: none live)
    int level = 0;           // its tick rate now (index into kTickRates: 0 = 1000 Hz, 3 = 125 Hz)
    int top = 0;             // its own highest rate (--tickrate)
    bool overloaded = false; // its last rate window was bad (the server's own fell-behind / loss / send-failure test; late
                             // ticks alone: from the second window in a row, Server kLateWindows)
    bool severe = false;     // it can't keep up at all (busy > 90%)
};

struct GovState {            // the decision, and what it needs to remember
    bool active = false;     // both up and testing open: caps apply (else each server runs alone, as before)
    int cap[2] = {0, 0};     // the highest rate each may run, as a level (index into kTickRates; >= its own top)
    bool now[2] = {false, false};   // apply the cap mid-round (sustained or severe overload)
    bool testingClosed = false;
    double overSince = -1;   // the current overload began then (-1: calm)
    double calmSince = 0;    // calm since then
    double lastChange = -1e9, lastRaise = -1e9;
    double calmWait = 60;
    double floorSince = -1;  // both at 125 Hz with production overloaded since then
    double closedAt = -1e9, reopenedAt = -1e9;
    double reopenWait = 300;
    double prodCalmSince = 0;// testing closed: production not overloaded since then
};

// The policy: pure and deterministic (same inputs, same answer). s[GOV_PROD], s[GOV_TESTING]; now: wall seconds.
GovState governorStep(const GovState& prev, const GovSide s[2], double now, const GovParams& p);
GovState governorInitial(const GovParams& p);

// Status file text (key value lines) and back. The decision is included when `st` is given (production).
std::string govStatusText(int channel, double now, const GovSide& me, const GovState* st);
bool govParseStatus(const std::string& text, int* channel, double* t, GovSide* side, GovState* st, bool* hasState);

class Governor {
public:
    ~Governor() { stop(); }
    // dir: the shared directory; interval: seconds between rounds (0: no thread, tests call poll() themselves)
    void start(const std::string& dir, int channel, const GovParams& p, double interval = 1.0);
    void stop();
    // Server thread: its latest numbers (a POD copy under a mutex; called a few times a second). What the governor
    // wants logged since the last call is swapped into `log` (nothing allocates unless there is something).
    void report(const GovSide& me, std::string& log) { std::lock_guard<std::mutex> l(mu_); me_ = me; if (!log_.empty()) log.swap(log_); }
    // One round: write our status, read the peer's, decide (production) or take production's decision (testing).
    void poll(double wallNow);
    // The decision for this server, read by its tick thread (relaxed atomics: the next frame sees it).
    std::atomic<bool> active{false}, closed{false}, applyNow{false};
    std::atomic<int> cap{0};
    int channel() const { return channel_; }
    GovState state() { std::lock_guard<std::mutex> l(mu_); return st_; }   // (tests)
    static double wallNow();
private:
    void run();
    std::string dir_;
    int channel_ = GOV_PROD;
    GovParams p_;
    double interval_ = 1;
    std::mutex mu_;
    std::condition_variable cv_;
    bool quit_ = false;
    std::thread thread_;
    GovSide me_;
    GovState st_;
    std::string log_;
};
