// Authoritative game server. Used by the standalone quark_server executable and by
// the client when hosting a game (runs on its own thread, client connects via loopback).
#pragma once
#include <atomic>
#include <memory>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "../common/net.h"
#include "../common/upnp.h"
#include "map.h"
#include "protocol.h"
#include "bots.h"

struct ServerConfig {
    std::string name = "quark server";
    std::string map = "arena";
    int mode = MODE_FFA;
    uint16_t port = kDefaultPort;
    int maxPlayers = 8;
    bool upnp = false;          // try to open the port on the router automatically
    int maxTickRate = 1000;     // starting (highest) tick rate; lowered automatically if the host can't keep up
    // Bots
    bool balanceBots = true;    // team modes: an extra bot joins the side with fewer humans (vote can turn off)
    int balanceBotSkill = BOT_INTERMEDIATE;
    bool vsBots = false;        // "play vs bots": fixed bots, humans always on team A
    bool autoStart = false;     // start the match automatically once both sides have players (else: vote)
    int botsEnemy = 0, botsAlly = 0, botSkill = BOT_INTERMEDIATE;
    bool lanOnly = false;
    // Mode tuning (0 = mode default)
    int roundsToWin = 0;
    int timeLimit = 0;          // seconds (ffa)
    int scoreLimit = 0;         // koth points
};

struct ServerPlayer {
    bool connected = false;
    bool isBot = false;
    bool balanceBot = false;    // added automatically to even out teams
    NetAddr addr;
    uint32_t nonce = 0;
    std::string name;
    bool admin = false;
    int team = TEAM_A;
    bool alive = false;
    bool finished = false;      // race
    MoveState move;
    float yaw = 0, pitch = 0;
    uint16_t buttons = 0, prevButtons = 0;
    float health = 0;
    int weapon = WPN_RIFLE;
    float nextFire = 0;
    float lastFire = -10;
    int kills = 0, deaths = 0, score = 0;
    float respawnAt = -1;
    int checkpoint = 0;         // race: next checkpoint index
    uint32_t lastCmdSeq = 0;
    std::deque<UserCmd> cmdQueue;
    float cmdTokensMs = 16.0f;
    uint8_t reportedLoss = 0;   // % of snapshots the client says it missed
    uint32_t lastViewTick = 0;
    double lastRecv = 0;
    float ping = 0;
    uint16_t lastStringCmd = 0;
    uint32_t nextEventId = 1;
    uint32_t ackedEvent = 0;
    std::deque<GameEvent> events; // unacknowledged reliable events
    uint32_t lastDamagedTick = 0;
    // lag compensation history
    struct Hist { uint32_t tick; Vec3 pos; bool crouched; bool alive; };
    static constexpr int kHistory = 1024; // >= 1 s at any tick rate
    Hist history[kHistory];
};

struct Projectile {
    bool active = false;
    uint16_t id = 0;
    int type = WPN_ROCKET;
    int owner = 0;
    Vec3 pos, vel;
    float dieAt = 0;
    float armAt = 0;    // stickies can't be detonated before this
    bool stuck = false; // stickies: attached to a surface
};

class Server {
public:
    using LogFn = std::function<void(const std::string&)>;

    bool start(const ServerConfig& cfg, std::string* err = nullptr);
    void stop();
    // Blocking loop for a dedicated server or a hosting thread.
    void run(std::atomic<bool>& quit);
    // Process network + advance as many ticks as are due. Returns seconds until next tick.
    double frame();
    void idle(double secondsUntilNextTick);
    void command(const std::string& line, int fromPlayer = -1); // console / admin commands

    void setLog(LogFn fn) { log_ = std::move(fn); }
    uint16_t port() const { return sock_.localPort(); }
    int playerCount() const;
    std::string statusText();
    const ServerConfig& config() const { return cfg_; }
    std::string upnpStatus() { return upnp_ ? upnp_->status() : std::string(); }

    // ---- state used by the game mode rules (gamemodes.cpp) ----
    ServerConfig cfg_;
    Map map_;
    uint32_t tick_ = 0;
    uint32_t timeMs_ = 0;       // server clock (advances 1000/rate ms per tick)
    int rateLevel_ = 0;         // index into kTickRates
    int tickRate() const { return kTickRates[rateLevel_]; }
    float dt() const { return 1.0f / tickRate(); }
    float time() const { return timeMs_ * 0.001f; }
    ServerPlayer players_[kMaxPlayers];
    Projectile projectiles_[kMaxProjectiles];
    uint16_t nextProjId_ = 1;
    ModeState ms_;
    float phaseEnd_ = 0;
    float roundStart_ = 0;
    float lastProgress_ = 0;    // vs: last time damage was dealt
    float drainAccum_ = 0;
    // payload/race per-round bookkeeping
    float payloadIdleSince_ = 0;
    float bestProgress_[2] = {0, 0};
    float completeTime_[2] = {0, 0};
    float kothPoints_[2] = {0, 0};

    BotBrain bots_[kMaxPlayers];
    NavGraph nav_;
    int addBot(int team, int skill, bool balance = false);
    void removeBot(int id);
    int humanCount(int team = -1) const;
    void updateBalanceBots(bool matchStarting = false);
    // Votes (bots on/off, bot difficulty): majority of human players decides.
    enum VoteKind { VOTE_NONE = 0, VOTE_BOTS, VOTE_BOTSKILL, VOTE_START, VOTE_STARTBOTS };
    struct Vote { int kind = VOTE_NONE; int arg = 0; uint32_t yes = 0, no = 0; float endsAt = 0; std::string text; } vote_;
    void callVote(int caller, int kind, int arg);
    void castVote(int id, bool yes);
    void tickVote();
    float nextBalanceCheck_ = 0;

    void broadcastEvent(const GameEvent& e);
    void sendEvent(int player, const GameEvent& e);
    void message(const std::string& text);
    void spawnPlayer(int id);
    void killPlayer(int victim, int killer, int weapon);
    void damagePlayer(int victim, int attacker, float dmg, int weapon, const Vec3& knock);
    int aliveCount(int team) const;
    int teamCount(int team) const;
    void log(const std::string& s);

private:
    void handlePacket(const NetAddr& from, const uint8_t* data, int len);
    void handleConnect(const NetAddr& from, Reader& r);
    void handleInput(int id, Reader& r);
    void handleStringCmd(int id, const std::string& s);
    void dropPlayer(int id, const std::string& reason, bool notify);
    void simulate();
    void drainCmds(int id);
    void checkTickRate();
    void resetRateWindow();
    void runPlayerCmd(int id, const UserCmd& cmd);
    void fireWeapon(int id, const UserCmd& cmd);
    void tickProjectiles();
    void explode(const Vec3& at, int owner, int weapon);
    void hitscan(int id, const Vec3& eye, const Vec3& dir, float range, uint32_t viewMs, float* dmgPerPlayer, float dmg);
    Vec3 spreadDir(const Vec3& dir, float spread);
    void sendSnapshots();
    int findPlayer(const NetAddr& a) const;
    void autoTeam(int id);
    std::string uniqueName(std::string name, int self) const;

    UdpSocket sock_;
    uint32_t rng_ = 0x9E3779B9u;
    float randf() { rng_ = rng_ * 1664525u + 1013904223u; return (rng_ >> 8) / 16777216.0f; } // [0,1)
    std::vector<GameEvent> events_; // scratch for snapshot building
    bool running_ = false;
    double nextTick_ = 0;
    double rateWindowStart_ = 0;
    int windowTicks_ = 0, windowLate_ = 0, windowSendFail_ = 0, windowWorstLoss_ = 0;
    LogFn log_;
    std::unique_ptr<PortMapper> upnp_;
};
