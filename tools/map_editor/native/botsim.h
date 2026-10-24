// BotSim: a headless match with the game's own server and bots, stepped tick by tick as fast as the CPU goes (or
// paced by the caller), read back as plain state. The `bots` command (bots_cmd.cpp) is its CLI; the bot editor's
// own commands (tools/bot_editor/native/) are meant to reuse it.
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
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "json.h"
#include "common/qmath.h"

class Server;

struct BotSpec {
    int team = 0;                  // TEAM_A / TEAM_B (free for all and the like: ignored)
    int skill = 2;                 // BOT_BEGINNER .. BOT_EXPERT (bots.h)
    bool placed = false;           // start at pos / yaw (else the map's spawns, as the game picks them)
    Vec3 pos;
    float yaw = 0;                 // radians, the map's convention (spawn yaw)
    std::string name;              // shown name (empty: the server's "BOT <NAME>")
    // ---- for the bot editor (stored, not used yet): a brain file, objective overrides, whatever it adds
    std::string brain;
    Json extra;
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
    Vec3 goal;                     // where it's heading (its current goal)
    Vec3 waypoint;                 // the next point it steers for
    std::vector<Vec3> path;        // the nav surfaces still to cross (their centres), in order
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
    int ticksFor(double seconds) const;            // ticks in that much game time (at least 1)
    double time() const;                           // game seconds since the match started (forced start)
    int tickRate() const;
    void sample(std::vector<BotState>& out) const; // every bot, as it is now
    std::vector<BotEvent> takeEvents();            // what happened since the last call
    Json summary() const;                          // score, phase, round, objective state, each player's kills / deaths
    bool mapChanged() const;                       // the server moved on to another map (a match end's vote): stop
    Server& server() { return *server_; }          // the real thing (players_, bots_, ms_, map_, nav_)
    const std::vector<int>& botIds() const { return ids_; }
private:
    std::unique_ptr<Server> server_;
    std::vector<int> ids_;
    std::vector<BotSpec> specs_;
    std::vector<char> placedDone_;
    bool viaFrame_ = false;
    double t0_ = 0;
    std::string dataDir_, logPath_, mapName_;
    long logPos_ = 0;
    std::vector<BotEvent> events_;
    // what the last tick looked like, for the events state changes make
    struct Prev { bool alive = false; int checkpoint = 0; bool finished = false; };
    std::vector<Prev> prev_;
    int prevScoreA_ = 0, prevScoreB_ = 0, prevPhase_ = -1, prevPoint_ = 0, prevBomb_ = 0, prevHill_ = 255;
    void tickOnce();
    void watch();          // state-change events and placed starts, after each tick
    void readLog();        // the game log's kill / round / match lines since the last read
};
