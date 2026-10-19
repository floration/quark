#include "netclient.h"
#include <random>

namespace {
constexpr double kConnectRetry = 0.5;
constexpr double kConnectTimeout = 10.0;
constexpr double kServerTimeout = 10.0;
constexpr int kCmdsPerPacket = 4;      // redundancy against packet loss

uint32_t randomNonce() {
    std::random_device rd;
    uint32_t n = ((uint32_t)rd() << 16) ^ (uint32_t)rd();
    return n ? n : 1;
}
}

bool NetClient::connect(const std::string& address, const std::string& name) {
    disconnect();
    error_.clear();
    if (!NetAddr::parse(address, kDefaultPort, server_)) { error_ = "bad address: " + address; return false; }
    if (!sock_.open(0)) { error_ = "could not open socket"; return false; }
    addrStr_ = address;
    name_ = name;
    nonce_ = randomNonce();
    state_ = ConnState::Connecting;
    connectStart_ = timeNow();
    lastSend_ = -1e9;
    lastRecv_ = timeNow();
    snaps_.clear();
    clockInit_ = false;
    nextSeq_ = 1;
    lossExpected_ = lossReceived_ = 0;
    lossPct_ = 0;
    for (auto& q : predictedSeq_) q = 0;
    lastEventId_ = 0;
    events_.clear();
    stringCmds_.clear();
    stringSeq_ = stringAcked_ = 0;
    mapReady_ = false;
    pred_ = predPrev_ = MoveState();
    for (auto& n : names_) n.clear();
    poll();
    return true;
}

void NetClient::disconnect() {
    if (state_ == ConnState::Connected) {
        uint8_t buf[16];
        Writer w(buf, sizeof buf);
        writeHeader(w, PKT_DISCONNECT);
        w.u32(nonce_);
        for (int i = 0; i < 3; i++) sock_.send(server_, buf, w.size());
    }
    sock_.close();
    state_ = ConnState::Disconnected;
    mapReady_ = false;
    snaps_.clear();
}

void NetClient::poll() {
    if (state_ == ConnState::Disconnected) return;
    uint8_t buf[2048];
    NetAddr from;
    int n;
    while ((n = sock_.recv(from, buf, sizeof buf)) > 0)
        if (from == server_) handlePacket(buf, n);
    if (state_ == ConnState::Disconnected) return;

    double now = timeNow();
    if (state_ == ConnState::Connecting) {
        if (now - connectStart_ > kConnectTimeout) {
            error_ = "no response from " + addrStr_ + " (is the port forwarded / server running?)";
            disconnect();
            return;
        }
        if (now - lastSend_ > kConnectRetry) {
            uint8_t out[128];
            Writer w(out, sizeof out);
            writeHeader(w, PKT_CONNECT);
            w.u16(kProtocolVersion);
            w.u32(nonce_);
            w.str(name_);
            sock_.send(server_, out, w.size());
            lastSend_ = now;
        }
    } else if (now - lastRecv_ > kServerTimeout) {
        error_ = "connection to server lost";
        disconnect();
        return;
    }
    if (state_ == ConnState::Connected && clockInit_) {
        float target = (float)(now + clockOffset_) - interpDelay();
        renderTime_ = target;
    }
}

void NetClient::handlePacket(const uint8_t* data, int len) {
    Reader r(data, len);
    if (r.u32() != kProtocolMagic) return;
    uint8_t type = r.u8();
    lastRecv_ = timeNow();
    switch (type) {
        case PKT_ACCEPT: {
            uint32_t nonce = r.u32();
            if (nonce != nonce_) return;
            int id = r.u8();
            std::string mapName = r.str();
            uint32_t crc = r.u32();
            r.u8(); // mode
            if (r.bad()) return;
            clientId_ = id;
            if (!mapReady_ || map_.name != mapName || map_.crc != crc) {
                std::string err;
                if (!map_.load(mapName, &err)) { error_ = "missing map '" + mapName + "': " + err; disconnect(); return; }
                if (map_.crc != crc) { error_ = "your copy of map '" + mapName + "' differs from the server's"; disconnect(); return; }
                mapReady_ = true;
                mapGen_++;
            }
            state_ = ConnState::Connected;
            break;
        }
        case PKT_REJECT:
            if (state_ == ConnState::Connecting) {
                error_ = "server refused: " + r.str();
                disconnect();
            }
            break;
        case PKT_KICKED:
            if (r.u32() != nonce_) return;
            error_ = "disconnected: " + r.str();
            sock_.close();
            state_ = ConnState::Disconnected;
            mapReady_ = false;
            break;
        case PKT_SNAPSHOT: {
            if (state_ != ConnState::Connected) return;
            if (r.u32() != nonce_) return;
            Snapshot s;
            if (!readSnapshot(r, s)) return;
            onSnapshot(s);
            break;
        }
    }
}

void NetClient::onSnapshot(Snapshot& s) {
    if (!snaps_.empty() && s.tick <= snaps_.back().tick) return; // out of order / duplicate
    double now = timeNow();
    // Snapshot loss for the server's tick-rate test (every tick sends one snapshot).
    if (!snaps_.empty()) {
        lossExpected_ += s.tick - snaps_.back().tick;
        lossReceived_++;
        if (lossExpected_ >= 500) {
            lossPct_ = (uint8_t)std::min(100u, (lossExpected_ - lossReceived_) * 100 / lossExpected_);
            lossExpected_ = lossReceived_ = 0;
        }
    }
    double serverTime = s.timeMs * 0.001;
    double offset = serverTime - now;
    if (!clockInit_ || std::fabs(offset - clockOffset_) > 0.5) { clockOffset_ = offset; clockInit_ = true; }
    // Track the most recent arrival tightly but let jitter average out.
    else clockOffset_ = offset > clockOffset_ ? clockOffset_ * 0.9 + offset * 0.1 : clockOffset_ * 0.99 + offset * 0.01;

    // Reliable events (in order, skip already seen).
    for (auto& e : s.events) {
        if (e.id <= lastEventId_) continue;
        lastEventId_ = e.id;
        if (e.type == EV_PLAYERINFO && e.a < kMaxPlayers) names_[e.a] = e.text;
        events_.push_back(e);
    }
    s.events.clear();
    // String command acks
    while (!stringCmds_.empty() && (int16_t)(s.ackStringCmd - (uint16_t)(stringAcked_ + 1)) >= 0) {
        stringCmds_.pop_front();
        stringAcked_++;
    }

    // Prediction reconcile. If the server's state for the acknowledged command matches what we
    // predicted for it, our prediction is already right: skip the replay (the common case).
    bool alive = false;
    for (auto& p : s.players) if (p.id == s.clientId) alive = p.flags & PS_ALIVE;
    const MoveState& mine = predicted_[s.ackCmdSeq % 256];
    bool match = predictedSeq_[s.ackCmdSeq % 256] == s.ackCmdSeq && length(mine.pos - s.self.pos) < 1e-4f &&
                 length(mine.vel - s.self.vel) < 1e-3f && mine.flags == s.self.flags;
    if (!alive || !mapReady_) {
        pred_ = predPrev_ = s.self;
        errorOffset_ = {};
    } else if (!match) {
        Vec3 oldVisual = pred_.pos + errorOffset_;
        pred_ = s.self;
        for (uint32_t seq = s.ackCmdSeq + 1; seq < nextSeq_; seq++) {
            const UserCmd& c = cmds_[seq % 256];
            if (c.seq != seq) continue;
            predPrev_ = pred_;
            pmove(pred_, c, map_);
            predicted_[seq % 256] = pred_;
            predictedSeq_[seq % 256] = seq;
        }
        Vec3 err = oldVisual - pred_.pos;
        errorOffset_ = length(err) < 2.0f ? err : Vec3();
    }

    snaps_.push_back(std::move(s));
    while (snaps_.size() > 64) snaps_.pop_front();
}

void NetClient::tick(UserCmd cmd) {
    if (state_ != ConnState::Connected || !mapReady_) return;
    cmd.seq = nextSeq_++;
    cmd.msec = (uint8_t)(1000 / tickRate());
    cmd.viewTick = (uint32_t)std::max(0.0, renderTime_ * 1000.0); // server ms we're looking at
    cmds_[cmd.seq % 256] = cmd;
    predPrev_ = pred_;
    if (selfAlive()) pmove(pred_, cmd, map_);
    predicted_[cmd.seq % 256] = pred_;
    predictedSeq_[cmd.seq % 256] = cmd.seq;
    errorOffset_ *= std::exp(-cmd.msec / 60.0f); // fade corrections out over ~60 ms, at any tick rate
    sendInput();
}

void NetClient::sendInput() {
    uint8_t buf[512];
    Writer w(buf, sizeof buf);
    writeHeader(w, PKT_INPUT);
    w.u32(nonce_);
    w.u32(hasSnapshot() ? latest().timeMs : 0);
    w.u32(lastEventId_);
    w.u8(lossOverride_ >= 0 ? (uint8_t)lossOverride_ : lossPct_);
    uint32_t first = nextSeq_ > (uint32_t)kCmdsPerPacket ? nextSeq_ - kCmdsPerPacket : 1;
    w.u8((uint8_t)(nextSeq_ - first));
    for (uint32_t s = first; s < nextSeq_; s++) writeCmd(w, cmds_[s % 256]);
    if (!stringCmds_.empty()) {
        w.u8(1);
        w.u16((uint16_t)(stringAcked_ + 1));
        w.str(stringCmds_.front());
    } else {
        w.u8(0);
    }
    sock_.send(server_, buf, w.size());
    lastSend_ = timeNow();
}

void NetClient::sendCommand(const std::string& s) {
    if (stringCmds_.size() < 64) stringCmds_.push_back(s);
}

Vec3 NetClient::smoothedPos(float alpha) const {
    return vlerp(predPrev_.pos, pred_.pos, alpha) + errorOffset_;
}

bool NetClient::selfAlive() const {
    const PlayerSnap* p = findPlayer(clientId_);
    return p && (p->flags & PS_ALIVE);
}

int NetClient::selfWeapon() const { return hasSnapshot() ? latest().selfWeapon : 0; }

const PlayerSnap* NetClient::findPlayer(int id) const {
    if (snaps_.empty()) return nullptr;
    for (auto& p : snaps_.back().players) if (p.id == id) return &p;
    return nullptr;
}

std::string NetClient::playerName(int id) const {
    if (id < 0 || id >= kMaxPlayers) return "world";
    return names_[id].empty() ? "player" + std::to_string(id) : names_[id];
}

static float lerpAngle(float a, float b, float t) {
    float d = std::remainder(b - a, 2 * PI);
    return a + d * t;
}

NetClient::PlayerArray NetClient::interpolatedPlayers() const {
    PlayerArray out{};
    if (snaps_.empty()) return out;
    // Find snapshots bracketing renderTime.
    const Snapshot* a = &snaps_.front();
    const Snapshot* b = &snaps_.back();
    for (size_t i = 0; i + 1 < snaps_.size(); i++) {
        if (snaps_[i].timeMs * 0.001f <= renderTime_ && snaps_[i + 1].timeMs * 0.001f >= renderTime_) {
            a = &snaps_[i];
            b = &snaps_[i + 1];
            break;
        }
    }
    if (renderTime_ >= snaps_.back().timeMs * 0.001f) a = b = &snaps_.back();
    float ta = a->timeMs * 0.001f, tb = b->timeMs * 0.001f;
    float t = tb > ta ? clampf((renderTime_ - ta) / (tb - ta), 0, 1) : 1.0f;
    for (auto& pb : b->players) {
        InterpPlayer& ip = out[pb.id];
        ip.valid = true;
        ip.snap = pb;
        for (auto& pa : a->players) {
            if (pa.id != pb.id) continue;
            // Don't interpolate across respawns/teleports.
            if ((pa.flags & PS_ALIVE) && (pb.flags & PS_ALIVE) && length(pa.pos - pb.pos) < 5.0f) {
                ip.snap.pos = vlerp(pa.pos, pb.pos, t);
                ip.snap.yaw = lerpAngle(pa.yaw, pb.yaw, t);
                ip.snap.pitch = lerpf(pa.pitch, pb.pitch, t);
            }
            break;
        }
    }
    return out;
}

std::vector<ProjectileSnap> NetClient::interpolatedProjectiles() const {
    std::vector<ProjectileSnap> out;
    if (snaps_.empty()) return out;
    // Projectiles fly straight: extrapolate the newest snapshot to "now" (they're visual only).
    const Snapshot& s = snaps_.back();
    float dt = clampf(renderTime_ + interpDelay() - s.timeMs * 0.001f, 0, 0.2f);
    for (auto p : s.projectiles) {
        p.pos += p.vel * dt;
        out.push_back(p);
    }
    return out;
}

// ---------------------------------------------------------------------------

void ServerBrowser::refresh() {
    if (!sock_.isOpen()) sock_.open(0, true);
    servers_.clear();
    uint8_t buf[16];
    Writer w(buf, sizeof buf);
    writeHeader(w, PKT_INFO_REQUEST);
    for (uint16_t p = kDefaultPort; p < kDefaultPort + 10; p++) {
        sock_.send(NetAddr::broadcast(p), buf, w.size());
        sock_.send(NetAddr{0x7F000001u, p}, buf, w.size());
    }
    sentAt_ = timeNow();
}

void ServerBrowser::query(const std::string& address) {
    if (!sock_.isOpen()) sock_.open(0, true);
    NetAddr a;
    if (!NetAddr::parse(address, kDefaultPort, a)) return;
    uint8_t buf[16];
    Writer w(buf, sizeof buf);
    writeHeader(w, PKT_INFO_REQUEST);
    sock_.send(a, buf, w.size());
    sentAt_ = timeNow();
}

void ServerBrowser::poll() {
    if (!sock_.isOpen()) return;
    uint8_t buf[1024];
    NetAddr from;
    int n;
    while ((n = sock_.recv(from, buf, sizeof buf)) > 0) {
        Reader r(buf, n);
        if (r.u32() != kProtocolMagic || r.u8() != PKT_INFO_REPLY) continue;
        ServerInfo si;
        si.addr = from;
        si.name = r.str(); si.map = r.str(); si.mode = r.u8(); si.players = r.u8(); si.maxPlayers = r.u8();
        r.u16();
        if (r.bad()) continue;
        si.ping = (float)((timeNow() - sentAt_) * 1000.0);
        bool dup = false;
        for (auto& s : servers_) {
            // Same server seen via loopback and LAN address: keep one.
            if (s.addr == from || (s.addr.port == from.port && s.name == si.name && s.map == si.map)) { dup = true; break; }
        }
        if (!dup) servers_.push_back(si);
    }
}
