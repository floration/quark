// Client side of the protocol: connection, input sending, prediction, interpolation.
// Has no rendering dependencies so tests and bots can use it headless.
#pragma once
#include <mutex>
#include <thread>
#include <array>
#include <deque>
#include <string>
#include <vector>
#include "../common/net.h"
#include "map.h"
#include "protocol.h"
#include "ranked.h"

enum class ConnState { Disconnected, Connecting, Connected };

struct InterpPlayer {
    bool valid = false;
    PlayerSnap snap; // interpolated pose
};

class NetClient {
public:
    bool connect(const std::string& address, const std::string& name);
    // The ranked key: its id is proven to the server at connect (signing the connect challenge), and the client signs
    // ranked match ids the server asks for (EV_RANKSIGN). No key = no ranked id.
    void setRankKey(const RankKey& k) { rankKey_ = k; }
    void disconnect();
    // Receive packets, handle timeouts/retries. Call every frame.
    void poll();
    // Run one simulation tick with this input (call at tickRate() Hz).
    void tick(UserCmd cmd);
    void sendCommand(const std::string& s); // reliable string command ("say hi", "team b", ...)
    size_t pendingCommands() const { return stringCmds_.size(); }

    ConnState state() const { return state_; }
    const std::string& error() const { return error_; }
    const std::string& address() const { return addrStr_; }
    bool mapReady() const { return mapReady_; }
    // 0..1 while fetching the server's map file, -1 when not downloading
    float downloadProgress() const { return dl_.active ? (dl_.total ? (float)dl_.got / dl_.total : 0.0f) : -1.0f; }
    const std::string& downloadName() const { return dl_.name; }
    uint32_t mapGeneration() const { return mapGen_; }
    const Map& map() const { return map_; }
    uint32_t mapRevision() const { return mapGen_ + editRev_; } // changes on every map load and every applied edit
    int clientId() const { return clientId_; }
    uint32_t sessionNonce() const { return nonce_; } // (the in-process host makes this session admin)
    bool hasSnapshot() const { return !snaps_.empty(); }
    const Snapshot& latest() const { return snaps_.back(); }
    const ModeState& mode() const { return snaps_.back().ms; }

    // Prediction
    const MoveState& predicted() const { return pred_; }
    uint32_t nextSeq() const { return nextSeq_; } // seq the next tick()'s command will get
    static constexpr uint32_t kCmdRing = 1024;     // commands (and predicted states) kept for replay: over 1 s at 1000 Hz
    uint32_t lastReplayCount() const { return lastReplay_; } // commands the last reconcile re-ran (0 = prediction matched)
    Vec3 smoothedPos(float alpha) const; // predicted position interpolated between ticks + error smoothing
    bool selfAlive() const;
    int selfHealth() const { return hasSnapshot() ? latest().selfHealth : 0; }
    int selfWeapon() const;
    // Interpolated view of every player at render time.
    using PlayerArray = std::array<InterpPlayer, kMaxPlayers>;
    PlayerArray interpolatedPlayers() const;
    std::vector<ProjectileSnap> interpolatedProjectiles() const;   // the ones in sight (the server sends others as an id: NET-10)
    bool projectileHidden(uint16_t id) const;                      // still flying, out of sight
    const PlayerSnap* findPlayer(int id) const;
    std::string playerName(int id) const;
    int playerModel(int id) const { return id >= 0 && id < kMaxPlayers ? models_[id] : 0; }
    int playerSoundPack(int id) const { return id >= 0 && id < kMaxPlayers ? packs_[id] : 0; }
    const std::string& rankId(int id) const { static const std::string none; return id >= 0 && id < kMaxPlayers ? rankIds_[id] : none; }

    // Reliable events received since last call.
    std::vector<GameEvent> takeEvents() { std::vector<GameEvent> e; e.swap(events_); return e; }
    double renderTime() const { return renderTime_; }
    uint32_t serverTick() const { return hasSnapshot() ? latest().tick : 0; }
    int tickRate() const { return hasSnapshot() ? latest().tickRate : 1000; }
    double tickPeriod() const { return 1.0 / tickRate(); }
    // Render other players this far behind the newest snapshot: 3 snapshots + 10 ms jitter margin.
    // Render other players this far behind the newest snapshot: 2 snapshots plus the measured arrival
    // jitter (99th percentile), between 1 and 50 ms. On a clean connection that's a few ms instead of 13.
    // How far behind the newest snapshot other players are drawn: adaptive (two snapshot intervals + measured
    // jitter) unless a fixed delay is set (seconds, < 0 = adaptive). Ranked forces a fixed 10 ms.
    float interpDelay() const { return interpFixed_ >= 0 ? interpFixed_ : 2.0f / snapRate() + std::min(0.05f, std::max(0.001f, jitter_ + 0.001f)); }
    void setInterpFixed(float seconds) { interpFixed_ = seconds; }
    int snapRate() const { return hasSnapshot() && latest().snapRate ? latest().snapRate : 1000; }
    uint32_t baseMisses() const { return baseMisses_; } // delta snapshots we couldn't decode (should stay ~0)
    int snapshotLoss() const { return lossPct_; }
    void overrideReportedLoss(int pct) { lossOverride_ = pct; } // tests: pretend to lose snapshots

    // Demos: recording writes what this client sees (snapshots at up to 60 Hz, delta-encoded, with events, plus
    // the map whenever it changes) to a file; playback feeds a file back through the same snapshot path, in real time.
    bool startRecording(const std::string& path, bool fullRate = false); // full rate: every snapshot, else 62.5 Hz
    void stopRecording();
    bool recording() const { return rec_ != nullptr; }
    double recordingTime() const;
    bool startPlayback(const std::string& path, std::string& err);
    bool playback() const { return play_.active; }
    float playbackProgress() const { return play_.active && play_.length > 0 ? (float)(play_.t / play_.length) : 0; }
    void setPlaybackSpeed(float s) { play_.speed = s; }
    void holdPlaybackClock() { play_.wall = timeNow(); }   // don't count time spent elsewhere (a map bake) as watched
    float playbackSpeed() const { return play_.speed; }

private:
    void handlePacket(const uint8_t* data, int len);
    void recordSnapshot(const Snapshot& s, const std::vector<GameEvent>& events);
    void pollPlayback();
    bool startPlaybackImpl(const std::string& path, std::string& err);
    FILE* rec_ = nullptr;
    double recStart_ = 0, recLast_ = -1, recEvery_ = 0.016;
    uint32_t recMapRev_ = 0xFFFFFFFFu, recEventId_ = 0;
    Snapshot recPrev_;
    bool recHavePrev_ = false;
    std::vector<GameEvent> recEvents_;
    struct Playback {
        bool active = false;
        std::string data;
        size_t pos = 0;
        double t = 0, length = 0, wall = 0;
        float speed = 1;
        Snapshot prev;
        bool havePrev = false;
    } play_;
    void onSnapshot(Snapshot& s);
    void sendInput();
    bool useMap(const std::string& name, uint32_t crc); // load name (own copy, else a downloaded one) if its CRC matches
    void pumpDownload(double now);

    UdpSocket sock_;
    NetAddr server_;
    std::string addrStr_, name_, error_;
    ConnState state_ = ConnState::Disconnected;
    uint32_t nonce_ = 0;
    uint32_t cookie_ = 0;         // connect challenge cookie from PKT_CHALLENGE (0 = none yet)
    RankKey rankKey_;
    std::string connectSig_;      // rankKey_'s signature of the current challenge
    uint64_t token_ = 0;          // reconnect token the server at tokenAddr_ gave us
    std::string tokenAddr_;
    int clientId_ = -1;
    double lastSend_ = 0, lastRecv_ = 0, connectStart_ = 0;
    Map map_;
    bool mapReady_ = false;
    uint32_t mapGen_ = 0, editRev_ = 0;
    void applyEditEvent(const std::string& text);
    void startDownload(const std::string& name, uint32_t crc);
    struct Download {
        bool active = false;
        std::string name, data;
        uint32_t crc = 0, textCrc = 0, total = 0, got = 0;   // crc: the map's id; textCrc: of the text being sent
        std::vector<double> asked;   // per chunk: when last requested (0 = never)
        std::vector<bool> have;
        std::vector<std::string> edits; // edit events that arrived meanwhile: replayed on top if they're newer
    } dl_;

    std::deque<Snapshot> snaps_;
    static constexpr int kLateSamples = 512;
    float lateness_[kLateSamples] = {};
    uint32_t latenessN_ = 0;
    float jitterTarget_ = 0.009f, jitter_ = 0.009f; // start like the old fixed margin until measured
    double lastJitterUpdate_ = 0;
    uint32_t baseMisses_ = 0;
    double snapRecvTime_ = 0;
    double clockOffset_ = 0;      // estimated serverTime - localTime
    bool clockInit_ = false;
    // server time (s) used for interpolating others. Double: a float of server uptime is 8 ms off after 18 h and 31 ms
    // after 3 days, which jerked interpolation and the lag-compensation view tick (FINAL_REVIEW 2.6).
    double renderTime_ = 0;

    UserCmd cmds_[kCmdRing];
    uint32_t nextSeq_ = 1;
    MoveState pred_, predPrev_;
    MoveState predicted_[kCmdRing];        // our predicted state after each command
    uint32_t predictedSeq_[kCmdRing] = {};
    uint32_t lastReplay_ = 0;
    void resetPrediction();             // new connection or demo: none of the old commands apply
    uint32_t lossExpected_ = 0, lossReceived_ = 0;
    uint8_t lossPct_ = 0;
    int lossOverride_ = -1;
    Vec3 errorOffset_;
    uint32_t lastEventId_ = 0;
    std::vector<GameEvent> events_;
    std::vector<std::string> names_ = std::vector<std::string>(kMaxPlayers);
    uint8_t models_[kMaxPlayers] = {}, packs_[kMaxPlayers] = {};
    float interpFixed_ = -1;
    int interpSent_ = 0;          // the fixed delay (ms) last reported to the server ("interp"; 0 = adaptive)
    std::vector<std::string> rankIds_ = std::vector<std::string>(kMaxPlayers);

    std::deque<std::string> stringCmds_;
    uint16_t stringSeq_ = 0;      // seq of front of queue = stringAcked_+1
    uint16_t stringAcked_ = 0;
};

struct ServerInfo {
    NetAddr addr;
    std::string name, map;
    int mode = 0, players = 0, maxPlayers = 0;
    float ping = 0;
    bool noReply = false; // on the internet list but didn't answer (port not open?); still clickable
};

// LAN discovery and direct server queries.
class ServerBrowser {
public:
    ~ServerBrowser();
    void refresh();                         // broadcast on the LAN + fetch the internet list
    void query(const std::string& address); // ask a specific server
    void poll();
    const std::vector<ServerInfo>& servers() const { return servers_; }
    std::string internetStatus();
private:
    UdpSocket sock_;
    std::thread fetch_;
    std::mutex mu_;
    std::vector<std::string> fetched_; // addresses from the internet list, waiting to be queried
    std::string netStatus_, myIp_;
    struct Listed { NetAddr addr; double since; int tries; bool mine; };
    std::vector<Listed> listed_;       // internet entries not heard from yet
    std::vector<ServerInfo> servers_;
    double sentAt_ = 0;
};
