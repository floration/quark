// Client side of the protocol: connection, input sending, prediction, interpolation.
// Has no rendering dependencies so tests and bots can use it headless.
#pragma once
#include <deque>
#include <string>
#include <vector>
#include "../common/net.h"
#include "map.h"
#include "protocol.h"

enum class ConnState { Disconnected, Connecting, Connected };

struct InterpPlayer {
    bool valid = false;
    PlayerSnap snap; // interpolated pose
};

class NetClient {
public:
    bool connect(const std::string& address, const std::string& name);
    void disconnect();
    // Receive packets, handle timeouts/retries. Call every frame.
    void poll();
    // Run one fixed simulation tick with this input (call at kTickRate).
    void tick(UserCmd cmd);
    void sendCommand(const std::string& s); // reliable string command ("say hi", "team b", ...)

    ConnState state() const { return state_; }
    const std::string& error() const { return error_; }
    const std::string& address() const { return addrStr_; }
    bool mapReady() const { return mapReady_; }
    uint32_t mapGeneration() const { return mapGen_; }
    const Map& map() const { return map_; }
    int clientId() const { return clientId_; }
    bool hasSnapshot() const { return !snaps_.empty(); }
    const Snapshot& latest() const { return snaps_.back(); }
    const ModeState& mode() const { return snaps_.back().ms; }

    // Prediction
    const MoveState& predicted() const { return pred_; }
    Vec3 smoothedPos(float alpha) const; // predicted position interpolated between ticks + error smoothing
    bool selfAlive() const;
    int selfHealth() const { return hasSnapshot() ? latest().selfHealth : 0; }
    int selfWeapon() const;
    // Interpolated view of every player at render time.
    std::vector<InterpPlayer> interpolatedPlayers() const;
    std::vector<ProjectileSnap> interpolatedProjectiles() const;
    const PlayerSnap* findPlayer(int id) const;
    std::string playerName(int id) const;

    // Reliable events received since last call.
    std::vector<GameEvent> takeEvents() { std::vector<GameEvent> e; e.swap(events_); return e; }
    float renderTime() const { return renderTime_; }
    uint32_t serverTick() const { return hasSnapshot() ? latest().tick : 0; }

private:
    void handlePacket(const uint8_t* data, int len);
    void onSnapshot(Snapshot& s);
    void sendInput();

    UdpSocket sock_;
    NetAddr server_;
    std::string addrStr_, name_, error_;
    ConnState state_ = ConnState::Disconnected;
    uint32_t nonce_ = 0;
    int clientId_ = -1;
    double lastSend_ = 0, lastRecv_ = 0, connectStart_ = 0;
    Map map_;
    bool mapReady_ = false;
    uint32_t mapGen_ = 0;

    std::deque<Snapshot> snaps_;
    double snapRecvTime_ = 0;
    double clockOffset_ = 0;      // estimated serverTime - localTime
    bool clockInit_ = false;
    float renderTime_ = 0;        // server time (s) used for interpolating others

    UserCmd cmds_[128];
    uint32_t nextSeq_ = 1;
    MoveState pred_, predPrev_;
    Vec3 errorOffset_;
    uint32_t lastEventId_ = 0;
    std::vector<GameEvent> events_;
    std::vector<std::string> names_ = std::vector<std::string>(kMaxPlayers);

    std::deque<std::string> stringCmds_;
    uint16_t stringSeq_ = 0;      // seq of front of queue = stringAcked_+1
    uint16_t stringAcked_ = 0;
};

struct ServerInfo {
    NetAddr addr;
    std::string name, map;
    int mode = 0, players = 0, maxPlayers = 0;
    float ping = 0;
};

// LAN discovery and direct server queries.
class ServerBrowser {
public:
    void refresh();                         // broadcast on the LAN
    void query(const std::string& address); // ask a specific server
    void poll();
    const std::vector<ServerInfo>& servers() const { return servers_; }
private:
    UdpSocket sock_;
    std::vector<ServerInfo> servers_;
    double sentAt_ = 0;
};
