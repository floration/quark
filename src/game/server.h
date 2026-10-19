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

struct ServerConfig {
    std::string name = "quark server";
    std::string map = "arena";
    int mode = MODE_FFA;
    uint16_t port = kDefaultPort;
    int maxPlayers = 12;
    bool upnp = false;          // try to open the port on the router automatically
    bool lanOnly = false;
    // Mode tuning (0 = mode default)
    int roundsToWin = 0;
    int timeLimit = 0;          // seconds (ffa)
    int scoreLimit = 0;         // koth points
};

struct ServerPlayer {
    bool connected = false;
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
    int kills = 0, deaths = 0, score = 0;
    float respawnAt = -1;
    int checkpoint = 0;         // race: next checkpoint index
    uint32_t lastCmdSeq = 0;
    std::deque<UserCmd> cmdQueue;
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
    Hist history[128];
};

struct Projectile {
    bool active = false;
    uint16_t id = 0;
    int type = WPN_ROCKET;
    int owner = 0;
    Vec3 pos, vel;
    float dieAt = 0;
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
    float time() const { return tick_ * kTickDt; }
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
    void runPlayerCmd(int id, const UserCmd& cmd);
    void fireWeapon(int id, const UserCmd& cmd);
    void tickProjectiles();
    void explode(const Vec3& at, int owner, int weapon);
    void sendSnapshots();
    int findPlayer(const NetAddr& a) const;
    void autoTeam(int id);

    UdpSocket sock_;
    bool running_ = false;
    double startTime_ = 0;
    LogFn log_;
    std::unique_ptr<PortMapper> upnp_;
};
