#include "netclient.h"
#include "../common/u8file.h"
#include <algorithm>
#include <cctype>
#include "demo.h"
#include "master.h"
#include "../common/hex.h"
#include <random>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>

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

void NetClient::resetPrediction() {
    nextSeq_ = 1;
    for (auto& c : cmds_) c = UserCmd();
    for (auto& q : predictedSeq_) q = 0;
    pred_ = predPrev_ = MoveState();
    errorOffset_ = {};
    lastReplay_ = 0;
    havePendingOffset_ = reconcilePending_ = false;
}

void NetClient::resetJitter() {
    for (auto& l : lateness_) l = 0;
    latenessN_ = 0;
    jitterTarget_ = jitter_ = kJitterStart;
    lastJitterUpdate_ = 0;
}

bool NetClient::connect(const std::string& address, const std::string& name) {
    disconnect();
    error_.clear();
    if (!NetAddr::parse(address, kDefaultPort, server_)) { error_ = "bad address: " + address; return false; }
    if (!sock_.open(0)) { error_ = "could not open socket"; return false; }
    if (address != tokenAddr_) token_ = 0;   // (a token is only good on the server that gave it)
    addrStr_ = address;
    name_ = name;
    nonce_ = randomNonce();
    cookie_ = 0;
    connectSig_.clear();
    state_ = ConnState::Connecting;
    connectStart_ = timeNow();
    lastSend_ = -1e9;
    lastRecv_ = timeNow();
    snaps_.clear();
    clockInit_ = false;
    resetJitter();
    resetPrediction();
    lossExpected_ = lossReceived_ = 0;
    lossPct_ = 0;
    lastEventId_ = 0;
    events_.clear();
    props_.clear(); propPlayers_ = 0;
    stringCmds_.clear();
    stringSeq_ = stringAcked_ = 0;
    interpSent_ = 0;
    mapReady_ = false;
    dl_ = Download();
    for (auto& n : names_) n.clear();
    for (auto& c : nameColors_) c = 0;
    for (auto& n : rankIds_) n.clear();
    poll();
    return true;
}

void NetClient::disconnect() {
    stopRecording();
    resetPrediction();
    if (play_.active) { play_ = Playback(); state_ = ConnState::Disconnected; return; }
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
    if (play_.active) {   // (ENG-2: a demo that makes anything throw ends, with the reason; the game goes on)
        try { pollPlayback(); }
        catch (const std::exception& e) { error_ = std::string("demo stopped: ") + e.what(); play_ = Playback(); state_ = ConnState::Disconnected; }
        return;
    }
    if (state_ == ConnState::Disconnected) return;
    uint8_t buf[2048];
    NetAddr from;
    int n;
    while ((n = sock_.recv(from, buf, sizeof buf)) > 0)
        if (from == server_) {
            try { handlePacket(buf, n); }   // (ENG-2: a packet that makes anything throw is dropped)
            catch (const std::exception&) {}
        }
    afterSnapshots();
    if (state_ == ConnState::Disconnected) return;

    double now = timeNow();
    if (state_ == ConnState::Connecting) {
        if (now - connectStart_ > kConnectTimeout) {
            error_ = "no response from " + addrStr_ + " (is the port forwarded / server running?)";
            disconnect();
            return;
        }
        if (now - lastSend_ > kConnectRetry) {
            uint8_t out[512];
            Writer w(out, sizeof out);
            writeHeader(w, PKT_CONNECT);
            w.u16(kProtocolVersion);
            w.u32(nonce_);
            w.u32(cookie_);
            w.str(name_);
            if (cookie_) {   // (the first CONNECT only asks for the challenge)
                w.u32((uint32_t)token_); w.u32((uint32_t)(token_ >> 32));
                w.str(connectSig_.empty() ? "" : rankKey_.pub);
                w.str(connectSig_);
                w.u32(server_.ip); w.u16(server_.port);   // (the address we signed: the one we dialled)
            }
            sock_.send(server_, out, w.size());
            lastSend_ = now;
        }
    } else if (dl_.active) {
        pumpDownload(now);
    }
    if (state_ == ConnState::Connected && now - lastRecv_ > kServerTimeout) {
        error_ = "connection to server lost";
        disconnect();
        return;
    }
    if (state_ == ConnState::Connected && clockInit_) {
        // ease toward the measured jitter so the render time never jumps
        jitter_ += (jitterTarget_ - jitter_) * (float)std::min(1.0, (now - lastJitterUpdate_) * 2.0);
        lastJitterUpdate_ = now;
        renderTime_ = now + clockOffset_ - interpDelay();
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
            MapId mapId;
            for (auto& c : mapId.b) c = r.u8();
            r.u8(); // mode
            if (r.bad() || id >= kMaxPlayers) return;
            uint32_t lo = r.u32(), hi = r.u32();
            if (!r.bad()) { token_ = (uint64_t)hi << 32 | lo; tokenAddr_ = addrStr_; }
            if (!validMapName(mapName)) {   // it becomes a file name (map, download, light cache, demo): never a path
                error_ = "server sent a bad map name";
                disconnect();
                return;
            }
            clientId_ = id;
            if (!mapReady_ || map_.name != mapName || map_.id() != mapId) {
                if (useMap(mapName, mapId)) dl_ = Download();
                else if (!dl_.active || !dl_.idKnown || dl_.id != mapId) startDownload(mapName, &mapId); // not on disk (or ours differs): fetch the server's
            }
            state_ = ConnState::Connected;
            break;
        }
        case PKT_MAP_DATA: {
            if (r.u32() != nonce_ || !dl_.active) return;
            MapId id, textId;   // (id: the map's; textId: this text's sha256)
            for (auto& c : id.b) c = r.u8();
            for (auto& c : textId.b) c = r.u8();
            uint32_t total = r.u32(), off = r.u32();
            int n = r.u16();
            if (!r.bad() && !dl_.idKnown) { dl_.id = id; dl_.idKnown = true; }   // (asked for "whatever it is now")
            if (!r.bad() && id != dl_.id) startDownload(dl_.name, &id); // the map changed meanwhile (edit session): restart on the new one
            if (r.bad() || id != dl_.id || total == 0 || total > kMaxMapBytes || n <= 0 || n > r.remaining()) return;
            if (!dl_.total) {
                dl_.total = total;
                dl_.textId = textId;
                dl_.data.assign(total, '\0');
                size_t chunks = (total + kMapChunk - 1) / kMapChunk;
                dl_.asked.assign(chunks, 0.0); dl_.have.assign(chunks, false);
            }
            size_t k = off / kMapChunk;
            if (total != dl_.total || textId != dl_.textId || off % kMapChunk || k >= dl_.have.size() || off + n > total || dl_.have[k]) return;
            for (int i = 0; i < n; i++) dl_.data[off + i] = (char)r.u8();
            dl_.have[k] = true;
            dl_.got += n;
            if (dl_.got >= dl_.total) {
                Map m;
                std::string err;
                if (mapContentId(dl_.data) != dl_.textId || !m.parse(dl_.data, &err)) {
                    error_ = "downloaded map '" + dl_.name + "' is broken" + (err.empty() ? "" : ": " + err);
                    disconnect();
                    return;
                }
                m.name = dl_.name;
                m.contentId_ = dl_.textId; m.contentIdKnown_ = true;   // (just checked)
                if (dl_.id != dl_.textId) m.setEditId(dl_.id);   // an edited map's id comes from the edit chain, not its text
                else saveMapFile(downloadedMapPath(dl_.id), dl_.data); // reused next time; failing just means re-downloading
                map_ = std::move(m);
                mapReady_ = true;
                mapGen_++;
                std::vector<std::string> later;
                later.swap(dl_.edits);
                dl_ = Download();
                // edits made while downloading: older ones are already in the file; apply the ones that build on it
                std::string last;
                for (auto& t : later) {
                    size_t sp = t.find(' ');
                    if (sp == std::string::npos) continue;
                    last = t.substr(0, sp);
                    std::string op = t.substr(sp + 1);
                    if (map_.id().startsWith(last)) continue;   // already in the file
                    MapId next = mapEditId(map_.id(), op);
                    if (next.startsWith(last) && applyMapEdit(map_, op)) {
                        map_.setEditId(next);
                        editRev_++;
                    }
                }
                if (!last.empty() && !map_.id().startsWith(last)) startDownload(map_.name, nullptr);
            }
            break;
        }
        case PKT_CHALLENGE: { // echo the server's cookie: proves we receive at our address
            if (r.u32() != nonce_) return;
            uint32_t c = r.u32();
            if (r.bad() || state_ != ConnState::Connecting || c == cookie_) return;
            cookie_ = c;
            connectSig_.clear();
            if (!rankKey_.secret.empty()) rankSign(rankKey_, rankConnectMessage(cookie_, nonce_, server_.toString()), &connectSig_);
            lastSend_ = -1e9; // send the CONNECT with the cookie right away (next poll)
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
            if (!readSnapshotClient(r, s)) return;
            const Snapshot* base = nullptr;
            if (s.baseTick) { // delta: needs the snapshot the server based it on (we acked it, so we have it)
                for (auto it = snaps_.rbegin(); it != snaps_.rend(); ++it)
                    if (it->tick == s.baseTick) { base = &*it; break; }
                if (!base) { baseMisses_++; return; } // can't decode; our next ack makes the server send against a newer base
            }
            if (!readSnapshotWorld(r, s, base) || !readSnapshotEvents(r, s)) return;
            onSnapshot(s);
            break;
        }
    }
}

void NetClient::startDownload(const std::string& name, const MapId* id) {
    dl_ = Download();
    dl_.active = true; dl_.name = name;
    if (id) { dl_.id = *id; dl_.idKnown = true; }
}

// Edit session: apply the server's edit if we hold the map it was made on; if we already have the result
// (it came with a download) skip it; anything else means we drifted, so fetch the server's current map.
// The event names the map the edit leaves (the first bytes of its id): we hold the one it was made on when our id, chained
// over the op, gives that.
void NetClient::applyEditEvent(const std::string& text) {
    size_t sp = text.find(' ');
    if (sp != 2 * MapId::kShortBytes) return;
    if (dl_.active) { if (dl_.edits.size() < 4096) dl_.edits.push_back(text); return; } // replayed when the download is done
    if (!mapReady_) return;
    std::string after = text.substr(0, sp), op = text.substr(sp + 1);
    if (map_.id().startsWith(after)) return;
    MapId next = mapEditId(map_.id(), op);
    if (next.startsWith(after) && applyMapEdit(map_, op)) {   // (in place, no re-serializing: MAP-12)
        map_.setEditId(next);
        editRev_++;
        return;
    }
    startDownload(map_.name, nullptr);   // (we only know the start of its id: whatever the server's map is now)
}

bool NetClient::useMap(const std::string& name, const MapId& id) {
    if (!validMapName(name)) return false; // just a name, never a path
    Map m;
    if (!(m.load(name) && m.id() == id)) {
        std::ifstream f(utf8Path(downloadedMapPath(id)), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        if (!f || !m.parse(ss.str()) || m.id() != id) return false;
        m.name = name;
    }
    map_ = std::move(m);
    mapReady_ = true;
    mapGen_++;
    return true;
}

// Map download: keep up to 32 chunk requests in flight, re-asking any that went unanswered for 250 ms.
// Before the first reply we don't know the size, so just ask for the start.
void NetClient::pumpDownload(double now) {
    auto ask = [&](uint32_t off) {
        uint8_t out[64];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_MAP_REQUEST);
        w.u32(nonce_); w.bytes(dl_.id.b, 32); w.u32(off);
        sock_.send(server_, out, w.size());
    };
    if (!dl_.total) {
        if (now - lastSend_ > 0.25) { ask(0); lastSend_ = now; }
        return;
    }
    int inFlight = 0;
    for (size_t k = 0; k < dl_.have.size(); k++)
        if (!dl_.have[k] && dl_.asked[k] > 0 && now - dl_.asked[k] < 0.25) inFlight++;
    for (size_t k = 0; k < dl_.have.size() && inFlight < 32; k++) {
        if (dl_.have[k] || (dl_.asked[k] > 0 && now - dl_.asked[k] < 0.25)) continue;
        ask((uint32_t)(k * kMapChunk));
        dl_.asked[k] = now;
        inFlight++;
    }
}

void NetClient::onSnapshot(Snapshot& s) {
    // out of order / duplicate (serial arithmetic: the tick counter wraps after 2^32 ticks)
    if (!snaps_.empty() && (int32_t)(s.tick - snaps_.back().tick) <= 0) return;
    // the server clock is 32 bits on the wire: unwrap it next to the last one (never jumps back at 2^32 ms)
    if (!snaps_.empty()) s.timeMs = snaps_.back().timeMs + (int64_t)(int32_t)((uint32_t)s.timeMs - (uint32_t)snaps_.back().timeMs);
    double now = timeNow();
    // Snapshot loss for the server's tick-rate test. Snapshots go out every `every` ticks (snapshot rate is
    // separate from the tick rate), so count the gap in snapshots, not ticks: counting ticks read as exactly
    // 50% loss at 1000 Hz / 500 snapshots and made the server lower its tick rate.
    if (!snaps_.empty()) {
        uint32_t every = s.snapRate > 0 && s.tickRate > s.snapRate ? (uint32_t)((s.tickRate + s.snapRate / 2) / s.snapRate) : 1;
        lossExpected_ += std::max<uint32_t>(1, (s.tick - snaps_.back().tick + every / 2) / every);
        lossReceived_++;
        if (lossExpected_ >= 500) {
            lossPct_ = (uint8_t)std::min(100u, (lossExpected_ - lossReceived_) * 100 / lossExpected_);
            lossExpected_ = lossReceived_ = 0;
        }
    }
    // The clock and the jitter estimate follow the newest snapshot of each poll (updateClock): every snapshot read in
    // one poll is stamped with the same "now", so the older ones of a batch looked up to a frame late, and the
    // interpolation delay (sized from the 99th percentile of lateness) grew by about a frame time - remote players
    // drawn that much further in the past (~7 ms at 144 fps, ~16 ms at 60). Real network jitter is still measured.
    pendingOffset_ = s.timeMs * 0.001 - now;
    havePendingOffset_ = true;

    // Reliable events (in order, skip already seen).
    std::vector<GameEvent> fresh;
    for (auto& e : s.events) {
        if (e.id <= lastEventId_) continue;
        lastEventId_ = e.id;
        if (rec_) fresh.push_back(e);
        if (e.type == EV_PLAYERINFO && e.a < kMaxPlayers) { names_[e.a] = e.text; int m = e.b & 15, sp = e.b >> 4;
            models_[e.a] = m < kPlayerModels ? m : 0; packs_[e.a] = sp < kSoundPacks ? sp : 0; nameColors_[e.a] = (uint16_t)e.c; }
        if (e.type == EV_RANKINFO && e.a < kMaxPlayers) rankIds_[e.a] = e.text;
        if (e.type == EV_RANKSIGN) {   // a ranked match went live (or a time trial started): vouch that we're in it (the server forwards it with the ticket)
            std::string sig;
            bool hex16 = isLowerHex(e.text, 16);
            if (!play_.active && hex16 && rankSign(rankKey_, rankTicketMessage(e.text, server_.toString()), &sig)) sendCommand("ticketsig " + sig);
            continue;
        }
        if (e.type == EV_EDIT) { applyEditEvent(e.text); continue; }
        if (e.type == EV_PROP) { applyPropEvent(e); continue; }
        events_.push_back(std::move(e));   // (s.events is cleared next)
    }
    s.events.clear();
    if (propPlayers_) {   // (the server says when one goes; this catches a death or a leave it said before we had it)
        uint32_t alive = 0;
        for (auto& p : s.players) if ((p.flags & PS_ALIVE) && p.id < kMaxPlayers) alive |= 1u << p.id;
        if (propPlayers_ & ~alive) dropProps(propPlayers_ & ~alive);
    }
    // String command acks
    while (!stringCmds_.empty() && (int16_t)(s.ackStringCmd - (uint16_t)(stringAcked_ + 1)) >= 0) {
        stringCmds_.pop_front();
        stringAcked_++;
    }

    reconcilePending_ = true;   // (once a poll, against its newest snapshot: reconcile)

    if (rec_) recordSnapshot(s, fresh);
    snaps_.push_back(std::move(s));
    indexLatest();
    // keep enough history to decode deltas based on any snapshot we acked within the last round trip
    while (snaps_.size() > 512) snaps_.pop_front();
}

void NetClient::updateClock(double offset) {
    // Arrival jitter: how much later than the best-case arrival this snapshot came. The interpolation
    // delay is sized from the 99th percentile of that instead of a fixed worst-case margin.
    if (clockInit_) {
        lateness_[latenessN_++ % kLateSamples] = (float)std::max(0.0, clockOffset_ - offset);
        if (latenessN_ % 64 == 0) {
            int n = (int)std::min<uint32_t>(latenessN_, kLateSamples);
            float* v = latenessScratch_;
            std::copy(lateness_, lateness_ + n, v);
            std::nth_element(v, v + n * 99 / 100, v + n);
            jitterTarget_ = v[n * 99 / 100];
        }
    }
    if (!clockInit_ || std::fabs(offset - clockOffset_) > 0.5) { clockOffset_ = offset; clockInit_ = true; }
    // Track the most recent arrival tightly but let jitter average out.
    else clockOffset_ = offset > clockOffset_ ? clockOffset_ * 0.9 + offset * 0.1 : clockOffset_ * 0.99 + offset * 0.01;
}

// After a poll's snapshots: the clock from the newest, and one prediction reconcile against it (a frame that reads
// several used to replay the unacknowledged commands for each of them: after a hitch, dozens of replays in a frame).
void NetClient::afterSnapshots() {
    if (havePendingOffset_) { havePendingOffset_ = false; updateClock(pendingOffset_); }
    if (reconcilePending_) { reconcilePending_ = false; if (!snaps_.empty()) reconcile(snaps_.back()); }
}

void NetClient::reconcile(const Snapshot& s) {
    // The payload cart is solid: mirror the server's collision box so prediction agrees (at the cart's place on
    // the track, which runs from the other end when team B attacks an unstaged map: Map::cartTrack, as the server).
    map_.dynSolids.clear();
    if (mapReady_ && isPayloadMode(s.ms.mode) && map_.payloadPath.size() >= 2)
        map_.dynSolids.push_back(map_.payloadCartBox(map_.cartTrack(s.ms.mode, s.ms.attackers, s.ms.payloadDist)));

    // Prediction reconcile. If the server's state for the acknowledged command matches what we
    // predicted for it, our prediction is already right: skip the replay (the common case).
    bool alive = false;
    for (auto& p : s.players) if (p.id == s.clientId) alive = p.flags & PS_ALIVE;
    const MoveState& mine = predicted_[s.ackCmdSeq % kCmdRing];
    bool match = predictedSeq_[s.ackCmdSeq % kCmdRing] == s.ackCmdSeq && length(mine.pos - s.self.pos) < 1e-4f &&
                 length(mine.vel - s.self.vel) < 1e-3f && mine.flags == s.self.flags && mine.hookTarget == s.self.hookTarget;
    lastReplay_ = 0;
    if (!alive || !mapReady_) {
        pred_ = predPrev_ = s.self;
        errorOffset_ = {};
    } else if (!match) {
        Vec3 oldVisual = pred_.pos + errorOffset_;
        pred_ = s.self;
        // what we now hold for the acked command: the next snapshot with the same ack (no new commands reached the
        // server) matches instead of replaying everything again. Before the replay, which overwrites the slot if a
        // newer command shares it. (Not in a demo: nothing is predicted there, pred_ follows every snapshot.)
        if (!play_.active) { predicted_[s.ackCmdSeq % kCmdRing] = s.self; predictedSeq_[s.ackCmdSeq % kCmdRing] = s.ackCmdSeq; }
        // only commands still in the ring can be replayed (an ack far behind, from a server or a demo, would
        // otherwise loop over millions of sequence numbers)
        uint32_t first = s.ackCmdSeq + 1;
        if (nextSeq_ > kCmdRing && first < nextSeq_ - kCmdRing) first = nextSeq_ - kCmdRing;
        for (uint32_t seq = first; seq < nextSeq_; seq++) {
            const UserCmd& c = cmds_[seq % kCmdRing];
            lastReplay_++;
            if (c.seq != seq) continue;
            predPrev_ = pred_;
            pmove(pred_, c, map_);
            predicted_[seq % kCmdRing] = pred_;
            predictedSeq_[seq % kCmdRing] = seq;
        }
        Vec3 err = oldVisual - pred_.pos;
        errorOffset_ = length(err) < 2.0f ? err : Vec3();
    }
    // A grapple latched onto a player (or another's holding us): the anchor moves with them, and only the server knows
    // where they are now. Prediction pulls toward its latest value (the replay above already started from it).
    if (alive && mapReady_) {
        if ((s.self.flags & MF_HOOKED) && s.self.hookTarget && (pred_.flags & MF_HOOKED) && pred_.hookTarget == s.self.hookTarget)
            pred_.hookPoint = s.self.hookPoint;
        if ((s.self.flags & MF_PULLED) && (pred_.flags & MF_PULLED)) pred_.pullPoint = s.self.pullPoint;
    }

}

void NetClient::indexLatest() {
    std::memset(latestIdx_, 0xFF, sizeof latestIdx_);
    const std::vector<PlayerSnap>& ps = snaps_.back().players;   // (at most 255: a u8 count)
    for (size_t k = 0; k < ps.size(); k++)
        if (ps[k].id < kMaxPlayers && latestIdx_[ps[k].id] == 0xFF) latestIdx_[ps[k].id] = (uint8_t)k;   // (the first, as the scan found)
}

void NetClient::tick(UserCmd cmd) {
    if (play_.active || state_ != ConnState::Connected || !mapReady_) return;
    cmd.seq = nextSeq_++;
    cmd.msec = (uint8_t)(1000 / tickRate());
    cmd.viewTick = (uint32_t)(uint64_t)std::max(0.0, renderTime_ * 1000.0); // server ms we're looking at (its low 32 bits)
    // a fixed interpolation delay goes to the server, whose lag compensation then rewinds that far (FINAL_REVIEW 2.5)
    int interpMs = interpFixed_ >= 0 ? std::min((int)std::lround(interpFixed_ * 1000.0f), kMaxInterpMs) : 0;
    if (interpMs != interpSent_) { interpSent_ = interpMs; sendCommand("interp " + std::to_string(interpMs)); }
    cmds_[cmd.seq % kCmdRing] = cmd;
    predPrev_ = pred_;
    if (selfAlive()) pmove(pred_, cmd, map_);
    predicted_[cmd.seq % kCmdRing] = pred_;
    predictedSeq_[cmd.seq % kCmdRing] = cmd.seq;
    errorOffset_ *= std::exp(-cmd.msec / 60.0f); // fade corrections out over ~60 ms, at any tick rate
    if (++unsentCmds_ >= inputBatch_) sendInput();
}

void NetClient::sendInput() {
    uint8_t buf[kMaxPacket];
    Writer w(buf, sizeof buf);
    writeHeader(w, PKT_INPUT);
    w.u32(nonce_);
    w.u32(hasSnapshot() ? (uint32_t)latest().timeMs : 0);
    w.u32(lastEventId_);
    w.u8(lossOverride_ >= 0 ? (uint8_t)lossOverride_ : lossPct_);
    uint32_t first = nextSeq_ > (uint32_t)kCmdsPerPacket ? nextSeq_ - kCmdsPerPacket : 1;
    w.u8((uint8_t)(nextSeq_ - first));
    for (uint32_t s = first; s < nextSeq_; s++) writeCmd(w, cmds_[s % kCmdRing]);
    // every unacknowledged string command that fits (resent until the server acks it), so a burst of
    // editor ops or chat doesn't trickle out one per round trip
    int nStr = 0, room = (int)sizeof buf - w.size() - 3;
    while (nStr < (int)stringCmds_.size() && nStr < 16 && room >= (int)std::min<size_t>(stringCmds_[nStr].size(), 255) + 1)
        room -= (int)std::min<size_t>(stringCmds_[nStr++].size(), 255) + 1;
    w.u8((uint8_t)nStr);
    if (nStr) {
        w.u16((uint16_t)(stringAcked_ + 1));
        for (int k = 0; k < nStr; k++) w.str(stringCmds_[k]);
    }
    sock_.send(server_, buf, w.size());
    lastSend_ = timeNow();
    unsentCmds_ = 0;
}

void NetClient::sendCommand(const std::string& s) {
    if (commandHook && !commandHook(commandHookCtx, s)) return;
    if (stringCmds_.size() < 256) stringCmds_.push_back(s);
}

Vec3 NetClient::smoothedPos(float alpha) const {
    if (visualOk_) return visualPos_;
    return vlerp(predPrev_.pos, pred_.pos, alpha) + errorOffset_;
}

// Your own position as of now, for drawing only: the last tick's prediction carried over the part of a tick since, in
// pmove's own 1 ms steps, with the last command (its aim brought up to date). It's what the next tick will work out
// unless the keys change, so nothing jumps. It used to be drawn between the last two ticks: as it was a whole tick
// ago (1 ms at 1000 Hz, 8 ms at 125 Hz). The command sent and the server are unchanged.
void NetClient::updateVisualPos(double tickAcc, float yaw, float pitch) {
    visualOk_ = false;
    if (play_.active || !mapReady_ || state_ != ConnState::Connected || nextSeq_ <= 1 || !selfAlive()) return;
    UserCmd c = cmds_[(nextSeq_ - 1) % kCmdRing];
    if (c.seq != nextSeq_ - 1) return;
    const double ms = std::max(0.0, tickAcc * 1000.0);
    const int whole = std::min(7, (int)ms);
    const float part = (float)std::min(1.0, ms - whole);
    c.yaw = yaw; c.pitch = pitch;
    MoveState s = pred_;
    if (whole > 0) { c.msec = (uint8_t)whole; pmove(s, c, map_); }
    MoveState next = s;
    c.msec = 1;
    pmove(next, c, map_);
    visualPos_ = vlerp(s.pos, next.pos, part) + errorOffset_;
    visualOk_ = true;
}

bool NetClient::selfAlive() const {
    const PlayerSnap* p = findPlayer(clientId_);
    return p && (p->flags & PS_ALIVE);
}

int NetClient::selfWeapon() const { return hasSnapshot() ? latest().selfWeapon : 0; }

const PlayerSnap* NetClient::findPlayer(int id) const {
    if (snaps_.empty() || id < 0 || id >= kMaxPlayers || latestIdx_[id] == 0xFF) return nullptr;   // (decoded ids are all < kMaxPlayers)
    return &snaps_.back().players[latestIdx_[id]];
}

std::string NetClient::playerName(int id) const { return nameRef(id); }

const std::string& NetClient::nameRef(int id) const {
    static const std::string world = "world";
    static const std::vector<std::string> unnamed = [] {   // (built once: "player<id>" for one who has no name yet)
        std::vector<std::string> v;
        for (int i = 0; i < kMaxPlayers; i++) v.push_back("player" + std::to_string(i));
        return v;
    }();
    if (id < 0 || id >= kMaxPlayers) return world;
    return names_[id].empty() ? unnamed[id] : names_[id];
}

static float lerpAngle(float a, float b, float t) {
    float d = std::remainder(b - a, 2 * PI);
    return a + d * t;
}

// The snapshots bracketing renderTime (searching from the newest: it's a few snapshots back), and how far between.
// At or past the newest (interpolation off, or a late snapshot) and before the oldest there's nothing to search:
// those used to walk the whole 512 snapshot history on every call.
static float renderBracket(const std::deque<Snapshot>& snaps, double renderTime, const Snapshot*& a, const Snapshot*& b) {
    b = &snaps.back();
    if (renderTime >= b->timeMs * 0.001) { a = b; return 1.0f; }
    a = &snaps.front();
    if (renderTime >= a->timeMs * 0.001)   // (before the oldest: front to back at 0, as the full search ended)
        for (size_t i = snaps.size() - 1; i > 0; i--) {
            if (snaps[i - 1].timeMs * 0.001 <= renderTime && snaps[i].timeMs * 0.001 >= renderTime) {
                a = &snaps[i - 1];
                b = &snaps[i];
                break;
            }
        }
    double ta = a->timeMs * 0.001, tb = b->timeMs * 0.001;   // (doubles: server uptime in seconds)
    return tb > ta ? clampf((float)((renderTime - ta) / (tb - ta)), 0, 1) : 1.0f;
}

// A player in snapshot `a` by id, in sight there (null: not there, or out of sight).
static const PlayerSnap* inSight(const Snapshot* a, int id) {
    for (auto& pa : a->players) if (pa.id == id && !pa.hidden) return &pa;
    return nullptr;
}

// A player (in sight in the newer snapshot `pb`) at render time: between where the older snapshot has them (`pa`:
// in sight there, else null) and `pb`. pa == &pb (one snapshot: nothing to interpolate) is a copy.
static void lerpPlayer(const PlayerSnap* pa, const PlayerSnap& pb, float t, PlayerSnap* out) {
    *out = pb;
    if (!pa || pa == &pb) return;
    // Don't interpolate across respawns/teleports.
    if ((pa->flags & PS_ALIVE) && (pb.flags & PS_ALIVE) && length(pa->pos - pb.pos) < 5.0f) {
        out->pos = vlerp(pa->pos, pb.pos, t);
        out->yaw = lerpAngle(pa->yaw, pb.yaw, t);
        out->pitch = lerpf(pa->pitch, pb.pitch, t);
    }
}

NetClient::PlayerArray NetClient::interpolatedPlayers() const {
    PlayerArray out;
    interpolatedPlayers(out);
    return out;
}

void NetClient::interpolatedPlayers(PlayerArray& out) const {
    static_assert(kMaxPlayers <= 32, "one bit per player id");
    uint32_t set = 0;
    if (!snaps_.empty()) {
        const Snapshot *a, *b;
        float t = renderBracket(snaps_, renderTime_, a, b);
        // a's players by id (in sight there), once: not a scan of a for every player of b
        const PlayerSnap* from[kMaxPlayers] = {};
        if (a != b)
            for (auto& pa : a->players) if (pa.id < kMaxPlayers && !pa.hidden && !from[pa.id]) from[pa.id] = &pa;
        for (auto& pb : b->players) {
            if (pb.hidden || pb.id >= kMaxPlayers) continue; // out of sight: the server didn't send where they are (ids: checked at decode too)
            InterpPlayer& ip = out[pb.id];
            ip.valid = true;
            lerpPlayer(a == b ? &pb : from[pb.id], pb, t, &ip.snap);
            set |= 1u << pb.id;
        }
    }
    for (int i = 0; i < kMaxPlayers; i++) if (!(set >> i & 1)) out[i] = InterpPlayer();   // (the rest as a fresh array has them)
}

float NetClient::bodyScale() const {
    if (!hasSnapshot()) return 1.0f;
    const ModeState& m = latest().ms;
    return m.mode == MODE_DUEL && m.phase != PHASE_WARMUP ? duelModDef(m.duelMod).size : 1.0f;
}

void NetClient::applyPropEvent(const GameEvent& e) {
    if (e.a == PROP_CLEAR) {
        if (e.b == PROP_WORLD) { props_.clear(); propPlayers_ = 0; }
        else if (e.b < kMaxPlayers) dropProps(1u << e.b);
        return;
    }
    StuckProp p;
    if (!readPropEvent(e, &p)) return;
    if (props_.capacity() < (size_t)kMaxStuckProps) props_.reserve(kMaxStuckProps);   // (once: no allocations after)
    if ((int)props_.size() >= kMaxStuckProps) props_.erase(props_.begin());   // the oldest goes
    props_.push_back(p);
    if (p.player != PROP_WORLD) propPlayers_ |= 1u << p.player;
}

void NetClient::dropProps(uint32_t players) {
    uint32_t left = 0;
    size_t keep = 0;
    for (auto& p : props_) {
        if (p.player != PROP_WORLD && (players >> p.player & 1)) continue;
        if (p.player != PROP_WORLD) left |= 1u << p.player;
        props_[keep++] = p;
    }
    props_.resize(keep);
    propPlayers_ = left;
}

bool NetClient::attachedPos(const ProjectileSnap& p, float alpha, Vec3* out) const {
    if (snaps_.empty() || !(p.flags & PROJ_ON_PLAYER)) return false;
    if (p.onPlayer == clientId_ && !play_.active && selfAlive()) {   // on you: where you see yourself
        *out = stickyOnPlayerPos(smoothedPos(alpha), p.attachOffset, (pred_.flags & MF_CROUCHED) != 0, bodyScale());
        return true;
    }
    const Snapshot *a, *b;
    float t = renderBracket(snaps_, renderTime_, a, b);
    for (auto& pb : b->players) {
        if (pb.id != p.onPlayer) continue;
        if (pb.hidden || !(pb.flags & PS_ALIVE)) return false;
        PlayerSnap ps;
        lerpPlayer(a == b ? &pb : inSight(a, pb.id), pb, t, &ps);
        *out = stickyOnPlayerPos(ps.pos, p.attachOffset, (ps.flags & PS_CROUCHED) != 0, bodyScale());
        return true;
    }
    return false;
}

void NetClient::interpolatedProjectiles(std::vector<ProjectileSnap>& out) const {
    out.clear();
    if (snaps_.empty()) return;
    // Extrapolate the newest snapshot to "now" (they're visual only) along the server's arc: its fixed 1 ms steps.
    const Snapshot& s = snaps_.back();
    float dt = clampf((float)(renderTime_ + interpDelay() - s.timeMs * 0.001), 0, 0.2f);
    for (auto p : s.projectiles) {
        if (p.hidden) continue;
        // on a player: glued to them as they're drawn (players are interpolated, projectiles extrapolated: the
        // newest snapshot's spot would run ahead of their model); not in sight: where the server last put it
        if (p.flags & PROJ_ON_PLAYER) { attachedPos(p, 1.0f, &p.pos); out.push_back(p); continue; }
        if (!(p.flags & PROJ_STUCK)) projectileExtrapolate(p.pos, p.vel, projectileGravity(p.type, (p.flags & PROJ_LOB) ? kRocketLobGravity : 1.0f), dt);
        out.push_back(p);
    }
}

bool NetClient::projectileHidden(uint16_t id) const {
    if (snaps_.empty()) return false;
    for (auto& p : snaps_.back().projectiles) if (p.id == id) return p.hidden;
    return false;
}

// ---------------------------------------------------------------------------

ServerBrowser::~ServerBrowser() = default;   // (a fetch still running keeps its own Fetch alive and drops its result)

std::string ServerBrowser::internetStatus() { std::lock_guard<std::mutex> l(fetch_->mu); return fetch_->netStatus; }

void ServerBrowser::refresh() {
    refreshes_++;
    if (!sock_.isOpen()) sock_.open(0, true);
    servers_.clear();
    listed_.clear();
    uint64_t gen;
    { std::lock_guard<std::mutex> l(fetch_->mu); gen = ++fetch_->gen; fetch_->netStatus = "SEARCHING THE INTERNET..."; fetch_->fetched.clear(); }
    std::thread([f = fetch_, gen] {
        std::vector<std::string> list;
        std::string err;
        bool ok = fetchServerList(list, &err);
        std::string me = ok && !list.empty() ? publicIpv4() : "";
        std::lock_guard<std::mutex> l(f->mu);
        if (f->gen != gen) return;   // a newer refresh() is under way: its answer counts
        f->fetched = list;
        f->myIp = me;
        f->netStatus = ok ? std::to_string(list.size()) + " INTERNET SERVER(S) LISTED" : "INTERNET LIST: " + err;
        for (auto& c : f->netStatus) c = (char)toupper((unsigned char)c);
    }).detach();
    uint8_t buf[kInfoRequestBytes] = {}; // padded: servers don't answer a request smaller than the reply
    Writer w(buf, sizeof buf);
    writeHeader(w, PKT_INFO_REQUEST);
    for (uint16_t p = kDefaultPort; p < kDefaultPort + 10; p++) {
        sock_.send(NetAddr::broadcast(p), buf, sizeof buf);
        sock_.send(NetAddr{0x7F000001u, p}, buf, sizeof buf);
    }
    sentAt_ = timeNow();
}

void ServerBrowser::query(const std::string& address) {
    if (!sock_.isOpen()) sock_.open(0, true);
    NetAddr a;
    if (!NetAddr::parse(address, kDefaultPort, a)) return;
    uint8_t buf[kInfoRequestBytes] = {};
    Writer w(buf, sizeof buf);
    writeHeader(w, PKT_INFO_REQUEST);
    sock_.send(a, buf, sizeof buf);
    sentAt_ = timeNow();
}

void ServerBrowser::poll() {
    if (!sock_.isOpen()) return;
    std::vector<std::string> addrs;
    std::string me;
    { std::lock_guard<std::mutex> l(fetch_->mu); addrs.swap(fetch_->fetched); me = fetch_->myIp; }
    for (auto& a : addrs) {
        NetAddr na;
        if (!NetAddr::parse(a, kDefaultPort, na)) continue;
        bool mine = !me.empty() && a.rfind(me + ":", 0) == 0;
        listed_.push_back({na, timeNow(), 1, mine});
        query(a);
        // Your own server: routers usually can't loop back to their own public IP, so ask locally too.
        if (mine) query("127.0.0.1:" + std::to_string(na.port));
    }
    // Re-ask (UDP can drop), and after 2.5 s show silent entries anyway so they can still be tried.
    double now = timeNow();
    for (size_t i = 0; i < listed_.size();) {
        Listed& l = listed_[i];
        bool answered = false;
        for (auto& sv : servers_)
            answered |= !sv.noReply && (sv.addr == l.addr || (l.mine && sv.addr.port == l.addr.port && (sv.addr.ip >> 24) == 127));
        if (answered) { listed_.erase(listed_.begin() + i); continue; }
        if (l.tries < 3 && now - l.since > 0.7 * l.tries) { l.tries++; query(l.addr.toString()); }
        if (now - l.since > 2.5) {
            bool shown = false;
            for (auto& sv : servers_) shown |= sv.addr == l.addr;
            if (!shown) { ServerInfo si; si.addr = l.addr; si.name = l.addr.toString(); si.noReply = true; servers_.push_back(si); }
        }
        i++;
    }
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
        for (size_t k = 0; k < servers_.size(); k++) // a late answer replaces its "no response" placeholder
            if (servers_[k].noReply && servers_[k].addr == from) { servers_.erase(servers_.begin() + k); break; }
        bool dup = false;
        for (auto& s : servers_) {
            // Same server seen via loopback and LAN address: keep one.
            if (s.addr == from || (s.addr.port == from.port && s.name == si.name && s.map == si.map)) { dup = true; break; }
        }
        if (!dup) servers_.push_back(si);
    }
}

// ---- demos ----
// The file format, and converting demos of older versions: demo.h.
static constexpr uint32_t kDemoMagic = demo::kMagic;
static constexpr uint64_t kMaxDemoBytes = demo::kMaxBytes;

bool NetClient::startRecording(const std::string& path, bool fullRate) {
    stopRecording();
    if (!hasSnapshot() || !mapReady_) return false;
    rec_ = std::make_unique<AsyncFileWriter>();
    if (!rec_->open(path)) { rec_.reset(); return false; }
    uint32_t magic = kDemoMagic; uint16_t ver = kProtocolVersion;
    rec_->write(&magic, 4); rec_->write(&ver, 2);
    recStart_ = timeNow(); recLast_ = -1; recEvery_ = fullRate ? 0.0 : 0.016; recMapRev_ = 0xFFFFFFFFu; recEventId_ = 0; recHavePrev_ = false;
    // who's who so far (names, models, ranked ids arrived as events before the recording started)
    recEvents_.clear();
    for (int i = 0; i < kMaxPlayers; i++) {
        if (names_[i].empty()) continue;
        GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)i; e.b = (uint8_t)(models_[i] | packs_[i] << 4); e.c = (int16_t)nameColors_[i]; e.text = names_[i];
        recEvents_.push_back(e);
        if (!rankIds_[i].empty()) { GameEvent r; r.type = EV_RANKINFO; r.a = (uint8_t)i; r.text = rankIds_[i]; recEvents_.push_back(r); }
    }
    for (auto& p : props_) recEvents_.push_back(propEvent(p));   // (and what's stuck where so far)
    return true;
}

void NetClient::stopRecording() {
    rec_.reset();   // (closes it: what was recorded reaches the file first)
}

double NetClient::recordingTime() const { return rec_ ? timeNow() - recStart_ : 0; }

void NetClient::recordSnapshot(const Snapshot& s, const std::vector<GameEvent>& events) {
    for (const auto& e : events) recEvents_.push_back(e);
    double t = timeNow() - recStart_;
    if (recLast_ >= 0 && t - recLast_ < recEvery_ && recMapRev_ == mapRevision()) return; // 62.5 Hz, or every snapshot (full rate)
    recLast_ = t;
    auto record = [&](uint8_t type, const uint8_t* p, uint32_t n) {
        rec_->write(&type, 1); rec_->write(&t, 8); rec_->write(&n, 4); rec_->write(p, n);
    };
    if (recMapRev_ != mapRevision()) {   // the map (new, or edited): its whole text, so the demo plays anywhere
        recMapRev_ = mapRevision();
        const std::string& src = map_.sourceText();
        std::vector<uint8_t> buf(src.size() + map_.name.size() + 64);
        Writer w(buf.data(), buf.size());
        w.str(map_.name.substr(0, 255)); w.u32((uint32_t)src.size());
        w.bytes((const uint8_t*)src.data(), (int)src.size());
        w.u8((uint8_t)clientId_);
        record(1, buf.data(), (uint32_t)w.size());
        recHavePrev_ = false;   // a full snapshot after a map change
    }
    // (recCur_ and recBuf_ keep their memory: no allocations per recorded snapshot)
    Snapshot& c = recCur_;
    c = s;
    c.baseTick = recHavePrev_ ? recPrev_.tick : 0;
    // numbered from 1 in the demo's own order; at most 255 a record (the count is a u8: the rest go in the next one)
    const size_t nEv = std::min<size_t>(recEvents_.size(), 255);
    const GameEvent* ev[255];
    for (size_t k = 0; k < nEv; k++) { recEvents_[k].id = recEventId_ + 1 + (uint32_t)k; ev[k] = &recEvents_[k]; }
    if (recBuf_.size() < 64 * 1024) recBuf_.resize(64 * 1024);
    std::vector<uint8_t>& buf = recBuf_;
    Writer w(buf.data(), (int)buf.size());
    writeSnapshotClient(w, c);
    writeSnapshotWorld(w, c, recHavePrev_ ? &recPrev_ : nullptr);
    writeSnapshotEvents(w, ev, (int)nEv);
    if (w.overflowed()) return;   // (too big: skip this one, the next will be against the same base, its events the same ids)
    record(2, buf.data(), (uint32_t)w.size());
    recEventId_ += (uint32_t)nEv;   // (taken only once written: no gaps)
    recEvents_.erase(recEvents_.begin(), recEvents_.begin() + (std::ptrdiff_t)nEv);
    std::swap(recPrev_, recCur_);
    recHavePrev_ = true;
    rec_->flush();   // (to the writer thread)
}

bool NetClient::startPlayback(const std::string& path, std::string& err) {
    try { return startPlaybackImpl(path, err); }
    catch (const std::exception& e) {   // (ENG-2: a hostile or broken demo file is an error, never a crash)
        err = std::string("can't play ") + path + ": " + e.what();
        play_ = Playback();
        state_ = ConnState::Disconnected;
        return false;
    }
}

bool NetClient::startPlaybackImpl(const std::string& path, std::string& err) {
    disconnect();
    std::ifstream f(utf8Path(path), std::ios::binary | std::ios::ate);
    if (f && (uint64_t)f.tellg() > kMaxDemoBytes) { err = "demo too big"; return false; }
    f.seekg(0);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string d = ss.str();
    if (!f || d.size() < 6) { err = "can't read " + path; return false; }
    uint32_t magic; uint16_t ver;
    memcpy(&magic, d.data(), 4); memcpy(&ver, d.data() + 4, 2);
    if (magic != kDemoMagic) { err = "not a quark demo"; return false; }
    if (ver < kOldestDemoProtocol || ver > kProtocolVersion) {   // (older ones are converted at startup, demo::startUpgrade; this one couldn't be)
        err = "recorded with " + demo::recordedWith(ver) + (ver > kProtocolVersion ? " (newer than this one)" : ", which this version can't play");
        return false;
    }
    play_ = Playback();
    play_.data = std::move(d);
    play_.proto = ver;
    play_.pos = 6;
    for (size_t p = 6; p + 13 <= play_.data.size();) { // its length: the last record's time
        uint32_t n; memcpy(&play_.length, play_.data.data() + p + 1, 8); memcpy(&n, play_.data.data() + p + 9, 4);
        if (n > play_.data.size() - p - 13) break;   // (truncated; and 13 + n in 32 bits could wrap to 0)
        p += 13 + (size_t)n;
    }
    play_.active = true;
    play_.wall = timeNow();
    snaps_.clear(); events_.clear(); lastEventId_ = 0; clockInit_ = false; mapReady_ = false;
    resetJitter();
    props_.clear(); propPlayers_ = 0;
    for (auto& n : names_) n.clear();
    for (auto& c : nameColors_) c = 0;
    state_ = ConnState::Connected;
    addrStr_ = "demo";
    error_.clear();
    pollPlayback();
    if (!mapReady_) { err = "demo has no map"; play_ = Playback(); state_ = ConnState::Disconnected; return false; }
    return true;
}

void NetClient::pollPlayback() {
    double now = timeNow();
    play_.t += (now - play_.wall) * play_.speed;
    play_.wall = now;
    const std::string& d = play_.data;
    while (play_.pos + 13 <= d.size()) {
        uint8_t type = (uint8_t)d[play_.pos];
        double t; uint32_t n;
        memcpy(&t, d.data() + play_.pos + 1, 8); memcpy(&n, d.data() + play_.pos + 9, 4);
        if (t > play_.t && mapReady_) break;
        if (n > d.size() - play_.pos - 13) break;
        Reader r((const uint8_t*)d.data() + play_.pos + 13, (int)n);
        play_.pos += 13 + (size_t)n;
        if (type == 1) {
            std::string name = r.str();
            uint32_t len = r.u32();
            std::string src;
            for (uint32_t i = 0; i < len && !r.bad(); i++) src += (char)r.u8();
            int id = r.u8();
            Map m;
            if (r.bad() || id >= kMaxPlayers || !validMapName(name) || !m.parse(src)) continue;
            m.name = name;
            map_ = std::move(m);
            mapReady_ = true;
            mapGen_++;
            clientId_ = id;
            play_.havePrev = false;
            snaps_.clear();
            clockInit_ = false;   // (the next snapshot sets the clock: map changes can jump the server clock)
            havePendingOffset_ = reconcilePending_ = false;   // (nor a snapshot from before it)
        } else if (type == 2) {
            Snapshot s;
            if (!readSnapshotClient(r, s, play_.proto)) continue;
            if (!readSnapshotWorld(r, s, s.baseTick && play_.havePrev ? &play_.prev : nullptr)) continue;
            if (!readSnapshotEvents(r, s)) continue;
            play_.prev = s;
            play_.havePrev = true;
            // the snapshot's server time, shifted so it arrives "now" at the demo's pace
            onSnapshot(s);
        }
    }
    afterSnapshots();
    if (clockInit_) {   // (the jitter eases as in poll(): it used to stay where it started)
        jitter_ += (jitterTarget_ - jitter_) * (float)std::min(1.0, (now - lastJitterUpdate_) * 2.0);
        lastJitterUpdate_ = now;
        renderTime_ = timeNow() + clockOffset_ - interpDelay();
    }
    if (play_.pos + 13 > d.size() && play_.t > play_.length + 0.5) {
        error_ = "demo finished";
        play_ = Playback();
        state_ = ConnState::Disconnected;
    }
}
