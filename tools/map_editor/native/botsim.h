// BotSim: a headless match with the game's own server and bots, stepped tick by tick as fast as the CPU goes (or
// paced by the caller), read back as plain state. The `bots` command (bots_cmd.cpp) is its CLI; the bot editor's
// own commands (tools/bot_editor/native/sim_cmd.cpp: `sim`, `simbatch`) reuse it.
//
//   BotSim sim;
//   BotSimConfig cfg; cfg.mapText = text; cfg.mode = MODE_BOMB; cfg.bots = {{TEAM_A, BOT_EXPERT}, {TEAM_B, BOT_EXPERT}};
//   std::string err;
//   if (!sim.start(cfg, &err)) ...;
//   while (sim.time() < 60) { sim.step(sim.ticksFor(0.1)); sim.sample(states); auto ev = sim.takeEvents(); ... }
//   sim.stop();   // (the destructor stops it too)
//
// The match is the real Server (src/game/server.cpp): its simulate() runs once per step tick exactly as frame() runs it
// (then the tick count, the clock and the snapshot pass), with no sockets read and no real-time clock involved, so a
// run is the same every time for the same map, bots and seed. Placed bots are moved to their start (pos, yaw) the
// first time they are alive after the match starts.
//
// Several BotSims can run at once, one per thread (simbatch): give each the same `sharedDataDir`, prepared once with
// prepareSharedDir() before the threads start (the game's user data folder is one global). Run each match on a
// fresh thread: bots.cpp keeps its route budget in thread_locals that a previous match on the same thread would leave
// set (a match's result would then depend on what ran before it on that thread).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "json.h"
#include "common/qmath.h"

class Server;
struct UserCmd;

// An objective override: where a bot goes when it isn't fighting, instead of what its brain picks (sim_cmd.cpp's
// `objective`). Driven from outside the game code (botsim.cpp, BotSim::think): see SIM.md "Objective overrides".
struct BotOverride {
    enum Kind { NONE = 0, GOTO, HOLD, ROUTE, ATTACK, DEFEND };
    int kind = NONE;
    Vec3 pos;                      // goto / hold
    float radius = 0;              // hold: stays within this (m); goto: arrived within this (0: the defaults, SIM.md)
    std::vector<Vec3> points;      // route
    bool loop = false;             // route: start again after the last point (else hold the last one)
    int objectiveIndex = -1;       // attack / defend: which site / point / checkpoint / spawn (-1: the mode's own pick)
    bool chase = true;             // false: forgets enemies it can't see (no hunting them, no leads, no turning to sounds)
};

struct BotSpec {
    int team = 0;                  // TEAM_A / TEAM_B (free for all and the like: ignored)
    int skill = 2;                 // BOT_BEGINNER .. BOT_EXPERT (bots.h)
    bool placed = false;           // start at pos / yaw (else the map's spawns, as the game picks them)
    Vec3 pos;
    float yaw = 0;                 // radians, the map's convention (spawn yaw)
    std::string name;              // shown name (empty: the server's "BOT <NAME>")
    // ---- for the bot editor
    std::string brain;             // a brain's name or path (reported; "" / "default": the built-in brain)
    std::string brainText;         // its .qbrain text: compiled and given to the bot when cfg.applyBrains (and the game has brains)
    BotOverride override_;         // an objective override (kind NONE: none)
    Json extra;                    // anything else the request gave for this bot (echoed)
};

struct BotSimConfig {
    std::string mapText;           // the .qmap text (the map as edited) ...
    std::string mapName;           // ... or a map the game finds by name, when mapText is empty
    int mode = 5;                  // MODE_* (gamedefs.h); MODE_FFA
    int tickRate = 125;            // 1000 / 500 / 250 / 125 (bots play the same at every rate: game-time timings)
    uint32_t seed = 1;             // the server thread's qrand (spawn picks, bot rngs): same seed, same match
    std::vector<BotSpec> bots;
    bool forceStart = true;        // start the match after one second of warmup (else it waits as a server does)
    double warmup = 1.0;           // seconds of warmup before the forced start (bots join, spawn, the nav graph is there)
    bool viaFrame = false;         // (a cross-check) tick through Server::frame() on a sped-up clock instead of stepping
    int timeLimit = 0;             // ServerConfig::timeLimit (s, ffa's match length; 0: the mode's default)
    int scoreLimit = 0;            // ServerConfig::scoreLimit (koth points, ffa score; 0: the mode's default)
    bool applyBrains = false;      // compile each BotSpec::brainText and give it to the bot (the `bots` command doesn't)
    std::string sharedDataDir;     // several BotSims at once: the folder prepareSharedDir() made (empty: one of its own)
};

struct BotState {
    int id = -1;
    std::string name;
    int team = 0;
    int skill = 0;
    bool alive = false;
    Vec3 pos, vel;
    float yaw = 0, pitch = 0;      // radians (yaw: the map's convention)
    float health = 0;
    int weapon = 0;
    int kills = 0, deaths = 0, score = 0;
    bool onGround = false, hooked = false, crouched = false;
    const char* state = "dead";    // dead | fight (a target in sight) | hunt (one remembered) | hook | move
    int target = -1;               // the player it's fighting or hunting (-1: none)
    Vec3 goal;                     // BotBrain::goal: the roam / hide goal it keeps (objective modes: often stale)
    Vec3 dest;                     // where its route actually leads now (the objective, a lead, the override's target)
    Vec3 waypoint;                 // the next point it steers for
    std::vector<Vec3> path;        // the nav surfaces still to cross (their centres), in order
    std::string decision;          // what chose the current behaviour (SIM.md: fight, hunt, objective:hill, roam, override:goto...)
    float damage = 0, objTime = 0; // ServerPlayer::dmgDealt (to enemies), objTime (s on the objective)
    int shots = 0, hits = 0;       // shots fired, shots that damaged an enemy
    double aliveTime = 0;          // s alive since the match started
};

struct BotEvent {
    double t = 0;                  // seconds since the match started (negative: in the warmup)
    std::string kind;              // kill, spawn, round, match, point, checkpoint, finish, score, bomb, hill, tag, phase, overtime
    int who = -1, victim = -1;     // players (kill: killer and victim; -1: the world)
    Vec3 pos;
    std::string detail;
    Json json() const;
};

class BotSim {
public:
    BotSim();
    ~BotSim();
    bool start(const BotSimConfig& cfg, std::string* err);
    void stop();
    bool running() const { return server_ != nullptr; }
    void step(int ticks);                          // exactly that many server ticks
    int step(int ticks, bool stopAtMatchEnd);      // ... or fewer, stopping on the tick the match ends: the ticks run
    int ticksFor(double seconds) const;            // ticks in that much game time (at least 1)
    double time() const;                           // game seconds since the match started (forced start)
    int tickRate() const;
    void sample(std::vector<BotState>& out) const; // every bot, as it is now
    std::vector<BotEvent> takeEvents();            // what happened since the last call
    Json summary() const;                          // score, phase, round, objective state, each player's kills / deaths
    bool mapChanged() const;                       // the server moved on to another map (a custom mode's pool): stop
    // The match ended (PHASE_MATCH_END reached since the forced start). The server would start a new one after
    // kMatchEndTime (or a custom mode's pool would change the map): `sim` stops here.
    bool matchOver() const { return matchOverAt_ >= 0; }
    double matchOverAt() const { return matchOverAt_; }
    // the latest match end's line ("TEAM A wins the match", "BOT ANNA wins the match", ...) and how many have ended
    // (more than one only when the caller steps on past the first: a new match starts after the end screen)
    const std::string& matchText() const { return matchText_; }
    int matchesEnded() const { return matchesEnded_; }
    // the latest match's winner: a team (TEAM_A / TEAM_B; -1 none, a draw or a free for all) and a player (-1: none, or a team win)
    int winnerTeam() const { return winnerTeam_; }
    int winnerPlayer() const { return winnerPlayer_; }
    Server& server() { return *server_; }          // the real thing (players_, bots_, ms_, map_, nav_)
    const std::vector<int>& botIds() const { return ids_; }
    const std::vector<BotSpec>& specs() const { return specs_; }
    // per bot (index as in cfg.bots): the brain it plays ("default", "name#hash") and how it was given:
    // "default" | "applied" | "unsupported" (this build has no brains: the built-in one plays) | "error: ..."
    const std::string& brainLabel(size_t k) const { return brainLabel_[k]; }
    const std::string& brainStatus(size_t k) const { return brainStatus_[k]; }
    static bool brainsSupported();                 // this build links the game's bot brains (src/game/botbrain.h)
    // Check brain text without a match: "" if it compiles (or brains are unsupported: *supported = false).
    static std::string checkBrain(const std::string& text, bool* supported, std::string* label);
    // simbatch: make `dir` (maps/ + the map as zz_maptool.qmap when mapText isn't empty) and point the game's user
    // data folder at it. Call once, before the threads start; remove the folder after they're done.
    static bool prepareSharedDir(const std::string& dir, const std::string& mapText, std::string* err);

    UserCmd think(int id);                         // (the override wrapper around botThink: botsim.cpp)
private:
    std::unique_ptr<Server> server_;
    std::vector<int> ids_;
    std::vector<BotSpec> specs_;
    std::vector<char> placedDone_;
    bool viaFrame_ = false;
    bool ownDir_ = true;
    double t0_ = 0;
    std::string dataDir_, logPath_, mapName_, mapFile_;
    long logPos_ = 0;
    std::vector<BotEvent> events_;
    std::vector<std::string> brainLabel_, brainStatus_;
    std::vector<std::shared_ptr<const void>> brains_;   // the compiled brains (kept alive while the bots play them)
    // what the last tick looked like, for the events state changes make
    struct Prev { bool alive = false; int checkpoint = 0; bool finished = false; };
    std::vector<Prev> prev_;
    int prevScoreA_ = 0, prevScoreB_ = 0, prevPhase_ = -1, prevPoint_ = 0, prevBomb_ = 0, prevHill_ = 255;
    double matchOverAt_ = -1;
    std::string matchText_;
    int winnerTeam_ = -1, winnerPlayer_ = -1, matchesEnded_ = 0;
    std::vector<uint32_t> aliveTicks_;              // per bot index: ticks alive since the match started
    // the overrides' running state, per bot index
    struct Ov {
        int routeIdx = 0; bool arrived = false; Vec3 target; bool active = false; bool resolved = false;
        // no headway toward the target for a while (a route its steering can't walk): the bot roams on its own for a
        // few seconds (as a roaming bot picks another goal), then tries again
        float bestDist = 1e30f; double bestAt = -1, detourUntil = -1; Vec3 bestFor;
    };
    std::vector<Ov> ov_;
    std::vector<int> ovOf_;                         // player id -> bot index with an override (-1: none)
    bool anyOverride_ = false;
    bool started_ = false;                          // past the forced start (t0_ set)
    std::vector<AABB> sitesTmp_;                    // think(): the patched bomb sites / payload track (kept: no allocations)
    std::vector<Vec3> trackTmp_;
    void tickOnce();
    void watch();          // state-change events and placed starts, after each tick
    void readLog();        // the game log's kill / round / match lines since the last read
    void parseWinner(const std::string& text);
    bool overrideTarget(size_t k, int id, Vec3& at, float& arrive, bool& useBox, int& boxKind, int& boxIndex);
};
