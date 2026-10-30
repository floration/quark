#include "netclient.h"
#include "../common/u8file.h"
#include <algorithm>
#include <cctype>
#include "demo.h"
#include "glass.h"
#include "gamemodes.h"
#include "customrules.h"
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
    moveEv_ = 0; moveLand_ = 0;
    hookCatch_ = HookCatch();
    remoteEvHead_ = remoteEvTail_;   // (another server's or demo's clock)
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
    evHold_.clear();
    events_.clear();
    props_.clear(); propPlayers_ = 0;
    stringCmds_.clear();
    stringSeq_ = stringAcked_ = 0;
    interpSent_ = 0;
    hitsSent_ = false;
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
                else if (!allowDownloads_) {   // (MAP DOWNLOADS off: not fetched)
                    error_ = "the server's map " + mapName + " isn't on this PC (map downloads are off in settings)";
                    disconnect();
                    return;
                }
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
                replayDestruction();
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
    replayDestruction();
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
    // Reliable events, handled in id order and acked only up to the last one with none missing before it. The server
    // resends unacked events paced, and skips one that doesn't fit, so a later event can come first: taking it and
    // acking its id told the server the missing one had arrived, and it was gone for good (kill feed, chat, names,
    // ranked tickets, map edits). A demo has gaps in its ids: there, every newer one as it comes.
    evReady_.clear();
    if (play_.active) {
        for (auto& e : s.events) if (e.id > lastEventId_) { lastEventId_ = e.id; evReady_.push_back(std::move(e)); }
    } else {
        for (auto& e : s.events)
            if (e.id > lastEventId_ && e.id - lastEventId_ <= 512) evHold_.emplace(e.id, std::move(e));   // (512: the server drops a client 256 behind)
        while (!evHold_.empty() && evHold_.begin()->first == lastEventId_ + 1) {
            evReady_.push_back(std::move(evHold_.begin()->second));
            evHold_.erase(evHold_.begin());
            lastEventId_++;
        }
    }
    for (auto& e : evReady_) {
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
        if (e.type == EV_DESTRUCT) {
            if (e.a == DESTRUCT_RESET) destructLog_.clear();
            else if (destructLog_.size() < 8192) destructLog_.push_back(e);
            applyDestructEvent(e, true);
            continue;
        }
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

    if (!pendingGlass_.empty()) expireGlassPredictions(timeNow());
    reconcilePending_ = true;   // (once a poll, against its newest snapshot: reconcile)

    // everyone else's movement events, played as each is drawn there (an old server or demo sends none)
    for (const PlayerSnap& p : s.players) {
        const uint16_t ev = p.keys & (uint16_t)~KEY_HELD_MASK;
        if (!ev || p.hidden || (p.id == s.clientId && !play_.active) || !(p.flags & PS_ALIVE)) continue;   // (a demo: the recorded player's too)
        if (remoteEvTail_ - remoteEvHead_ >= kRemoteEvRing) remoteEvHead_++;   // (nobody's reading: the oldest goes)
        remoteEv_[remoteEvTail_++ % kRemoteEvRing] = {s.timeMs * 0.001, p.id, ev};
    }
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

// The dash charges, their timers and the hook cooldown agree (F19): a teammate's syringe refills them on the server
// between commands, which moves nothing, so a position-only compare kept the empty pips and the next dash or grapple
// came a round trip late as a correction. Each is exact on the wire and pmove's integer / float steps match.
static bool sameCharges(const MoveState& a, const MoveState& b) {
    if (a.dashCharges != b.dashCharges || a.dashRecharging != b.dashRecharging || a.dashCooldownMs != b.dashCooldownMs ||
        std::fabs(a.hookCooldown - b.hookCooldown) > 1e-4f) return false;
    for (int i = 0; i < a.dashRecharging && i < kMaxDashCharges; i++) if (a.dashRechargeMs[i] != b.dashRechargeMs[i]) return false;
    return true;
}

// The players a grapple fired in `cmd` could catch, as Server::grappleTargets picks them, where you see them (render
// time: the server rewinds to it). Only for a command that can fire the hook (rare), so the interpolation costs nothing
// the rest of the time. What the client can't know (a custom rule) stays the server's word: a correction, as before.
bool NetClient::hookProbe(const UserCmd& cmd, HookProbe& probe) {
    probe.n = 0;
    const MoveState& m = pred_;
    if (!(cmd.buttons & BTN_GRAPPLE) || (m.flags & (MF_HOOKHELD | MF_HOOKED | MF_FROZEN)) || (m.mods & MOVE_RUN_ONLY)) return false;
    if (!moveTuning().grapple || m.hookCooldown > cmd.msec * kMoveStep + 1e-4f || !hasSnapshot()) return false;
    const ModeState& ms = mode();
    if (isRaceMode(ms.mode) || ms.phase == PHASE_ROUND_END || ms.phase == PHASE_MATCH_END || ms.phase == PHASE_FREEZE) return false;
    const PlayerSnap* me = findPlayer(clientId_);
    if (!me) return false;
    interpolatedPlayers(probePlayers_);
    uint32_t held = 0;   // players someone else's grapple holds already
    for (const InterpPlayer& ip : probePlayers_)
        if (ip.valid && ip.snap.id != clientId_ && ip.snap.hooked >= 2 && ip.snap.hooked - 2 < kMaxPlayers) held |= 1u << (ip.snap.hooked - 2);
    const float scale = bodyScale();
    for (int i = 0; i < kMaxPlayers && probe.n < HookProbe::kMax; i++) {
        const InterpPlayer& ip = probePlayers_[i];
        const PlayerSnap& q = ip.snap;
        if (!ip.valid || i == clientId_ || !(q.flags & PS_ALIVE) || (q.flags & PS_SPECTATOR) || (held >> i & 1)) continue;
        if (modeIsTeamBased(ms.mode) && q.team() == me->team()) continue;
        if (isItMode(ms.mode) && ((q.flags & PS_HASBOMB) != 0) == ((me->flags & PS_HASBOMB) != 0)) continue;
        probe.box[probe.n] = playerBox(q.pos, (q.flags & PS_CROUCHED) != 0, scale);
        probe.id[probe.n++] = (uint8_t)i;
    }
    // Rocket riding, as Server::grappleTargets picks them: the rockets in reach as the server will have them when it runs
    // this command (the newest snapshot's, flown on by the commands before it: one a tick), your own tested there, anyone
    // else's where the server tests them (your view time + rocketSeenMs: about where they're drawn). Then your copies.
    if (moveTuning().rocketRide) {
        const Snapshot& s = snaps_.back();
        const Vec3 eye = m.pos + Vec3(0, kEyeHeight * moveScale(m.mods), 0);
        const float reach = kGrappleRange * moveValue(m, MV_HOOK_RANGE, &map_) + 2 * kRocketCatchHalf;
        const int64_t nowMs = (int64_t)s.timeMs + (int64_t)std::max<int64_t>(0, (int64_t)cmd.seq - s.ackCmdSeq - 1) * cmd.msec;   // (the server's clock then)
        const int64_t seenMs = std::min<int64_t>(nowMs, (int64_t)cmd.viewTick + rocketSeenMs((uint32_t)interpSent_, snapRate()));
        for (const ProjectileSnap& pr : s.projectiles) {
            if (probe.nr >= HookProbe::kRockets) break;
            if (pr.hidden || pr.type != WPN_ROCKET || (pr.flags & (PROJ_STUCK | PROJ_ON_PLAYER))) continue;
            const float g = projectileGravity(pr.type, (pr.flags & PROJ_LOB) ? kRocketLobGravity : 1.0f);
            Vec3 pos = pr.pos, vel = pr.vel;
            map_.flyProjectile(pos, vel, g, (float)(nowMs - (int64_t)s.timeMs) * 0.001f);
            const Vec3 d = pos - eye;
            if (dot(d, d) > reach * reach) continue;
            Vec3 at = pr.pos;
            if (pr.owner == clientId_) at = pos;
            else {
                Vec3 v = pr.vel;
                const int64_t dt = seenMs - (int64_t)s.timeMs;
                if (dt >= 0) map_.flyProjectile(at, v, g, (float)dt * 0.001f);
                else projectileRewind(at, v, g, (float)-dt * 0.001f);
            }
            const int k = probe.nr++;
            probe.at[k] = at; probe.pos[k] = pos; probe.vel[k] = vel; probe.grav[k] = g; probe.rid[k] = pr.id;
        }
        for (int c = 0; c < nCopies_ && probe.nr < HookProbe::kRockets; c++) {
            const Vec3 d = copies_[c].pos - eye;
            if (dot(d, d) > reach * reach) continue;
            const int k = probe.nr++;
            probe.at[k] = probe.pos[k] = copies_[c].pos; probe.vel[k] = copies_[c].vel; probe.grav[k] = copies_[c].grav; probe.rid[k] = 0;
        }
    }
    return probe.n > 0 || probe.nr > 0;
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
                 length(mine.vel - s.self.vel) < 1e-3f && mine.flags == s.self.flags && mine.hookTarget == s.self.hookTarget &&
                 sameCharges(mine, s.self) && mine.acidOutMs == s.self.acidOutMs;
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
            if (hookCatch_.seq == seq) {   // (the command whose grapple caught a player or a rocket: that one again)
                HookProbe probe;
                if (hookCatch_.rocket) {
                    probe.nr = 1; probe.at[0] = hookCatch_.at; probe.pos[0] = hookCatch_.pos; probe.vel[0] = hookCatch_.vel;
                    probe.grav[0] = hookCatch_.grav; probe.rid[0] = hookCatch_.rid;
                } else { probe.n = 1; probe.box[0] = hookCatch_.box; probe.id[0] = hookCatch_.id; }
                pmove(pred_, c, map_, &probe);
            } else pmove(pred_, c, map_);
            predicted_[seq % kCmdRing] = pred_;
            predictedSeq_[seq % kCmdRing] = seq;
        }
        Vec3 err = oldVisual - pred_.pos;
        errorOffset_ = length(err) < 2.0f ? err : Vec3();
    }
    // A grapple latched onto a player (or another's holding us): the anchor moves with them, and only the server knows
    // where they are now. Prediction pulls toward its latest value (the replay above already started from it).
    if (alive && mapReady_) {
        if ((s.self.flags & MF_HOOKED) && s.self.hookTarget && s.self.hookTarget != kHookRocket && (pred_.flags & MF_HOOKED) && pred_.hookTarget == s.self.hookTarget)
            pred_.hookPoint = s.self.hookPoint;
        if ((s.self.flags & MF_PULLED) && (pred_.flags & MF_PULLED)) pred_.pullPoint = s.self.pullPoint;
        // riding your own rocket caught as a predicted copy: the server's name for it (prediction already flies the anchor
        // with it; a replay started from the server's)
        if ((s.self.flags & MF_HOOKED) && s.self.hookTarget == kHookRocket && (pred_.flags & MF_HOOKED) && pred_.hookTarget == kHookRocket && !pred_.hookProj)
            pred_.hookProj = s.self.hookProj;
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
    if (reportHits_ && !hitsSent_) { hitsSent_ = true; sendCommand("clienthits"); }
    cmds_[cmd.seq % kCmdRing] = cmd;
    predPrev_ = pred_;
    if (selfAlive()) {
        HookProbe probe;   // a grapple this command may fire: the enemies you see can catch it, as on the server (F20)
        if (hookProbe(cmd, probe)) {
            Vec3 at0[HookProbe::kRockets], pos0[HookProbe::kRockets], vel0[HookProbe::kRockets];   // (pmove flies the probe's rockets on)
            for (int k = 0; k < probe.nr; k++) { at0[k] = probe.at[k]; pos0[k] = probe.pos[k]; vel0[k] = probe.vel[k]; }
            pmove(pred_, cmd, map_, &probe);
            if (probe.hit >= 0) { hookCatch_ = HookCatch(); hookCatch_.seq = cmd.seq; hookCatch_.box = probe.box[probe.hit]; hookCatch_.id = probe.id[probe.hit]; }
            if (probe.hitRocket >= 0) {
                const int k = probe.hitRocket;   // (as the probe had it as this command started: pmove flew it on since)
                hookCatch_ = HookCatch(); hookCatch_.seq = cmd.seq; hookCatch_.rocket = true;
                hookCatch_.at = at0[k]; hookCatch_.pos = pos0[k]; hookCatch_.vel = vel0[k];
                hookCatch_.grav = probe.grav[k]; hookCatch_.rid = probe.rid[k];
            }
        } else pmove(pred_, cmd, map_);
        moveEv_ |= pred_.events;
        moveLand_ = std::max(moveLand_, pred_.landSpeed);
    }
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
    const uint32_t per = (uint32_t)(cmdsPerPacket_ > 0 ? cmdsPerPacket_ : kCmdsPerPacket);   // (PACKET LOSS PROTECTION: 2 / 4 / 8)
    uint32_t first = nextSeq_ > per ? nextSeq_ - per : 1;
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

int NetClient::selfHealth() const {
    if (!hasSnapshot()) return 0;
    const int hp = latest().selfHealth;
    const float acid = hp > 0 ? pendingAcid() : 0.0f;
    return acid > 0 ? std::max(1, (int)std::ceil(hp - acid)) : hp;
}

float NetClient::pendingAcid() const {
    if (play_.active || !mapReady_ || map_.acid.empty() || !hasSnapshot() || !selfAlive()) return 0;
    const Snapshot& s = latest();
    if (!acidHurts(s.ms.phase)) return 0;
    // pred_.acidTaken keeps adding as prediction runs; at the acked command it held what the server has taken by now
    // (a replay starts from the server's state, whose acidTaken is 0)
    const uint32_t k = s.ackCmdSeq % kCmdRing;
    const float acked = predictedSeq_[k] == s.ackCmdSeq ? predicted_[k].acidTaken : 0.0f;
    return std::max(0.0f, pred_.acidTaken - acked) * lifeMaxHealth();
}

float NetClient::lifeMaxHealth() const {
    const ModeState* m = hasSnapshot() ? &latest().ms : nullptr;
    const float h = duelModDef(m && m->mode == MODE_DUEL ? m->duelMod : 255).health;
    if (h != kMaxHealth) return h;
    const CustomRules& r = activeRules();
    return r.active() ? (float)r.health : (float)kMaxHealth;
}

float NetClient::bodyScale() const {
    if (!hasSnapshot()) return 1.0f;
    const ModeState& m = latest().ms;
    return m.mode == MODE_DUEL && m.phase != PHASE_WARMUP ? duelModDef(m.duelMod).size : 1.0f;
}

constexpr double kGlassPredictWait = 1.0;   // s: a predicted break the server hasn't confirmed by then comes back

void NetClient::applyDestructEvent(const GameEvent& e, bool fx) {
    if (!mapReady_) return;   // (logged: applied when the map is in)
    if (e.a == DESTRUCT_RESET) { map_.resetDestruction(); pendingGlass_.clear(); return; }
    if (e.a == DESTRUCT_GLASS_LIST || e.a == DESTRUCT_HOLE_LIST) {
        std::vector<uint32_t> glass;
        std::vector<Vec3> holes;
        if (!readDestructList(e, &glass, &holes)) return;
        for (uint32_t b : glass) map_.breakGlass(b);
        for (const Vec3& h : holes) map_.punchHole(h);
        return;
    }
    DestructEv d;
    if (!readDestructEvent(e, &d)) return;
    if (d.kind == DESTRUCT_HOLE) {
        crumbled_.clear();
        if (map_.punchHole(d.at, &crumbled_) > 0 && fx) {
            glassFx_.push_back({AABB(), 0, d.at, {}, 0, true});
            for (const AABB& w : crumbled_) glassFx_.push_back({w, 1, d.at, {}, 0, true});   // (a whole wall coming down)
        }
        return;
    }
    if (d.kind == DESTRUCT_RESTORE) { map_.restoreBrush(d.brush); return; }   // (a crumble platform back)
    if (d.kind == DESTRUCT_SHAKE) {   // (a crumble platform about to fall: dust off it)
        if (fx && d.brush < map_.brushes.size() && glassFx_.size() < 256) glassFx_.push_back({map_.brushes[d.brush].box, kGlassFxShake, d.at, {}, 0, false, map_.brushes[d.brush].material});
        return;
    }
    for (size_t k = 0; k < pendingGlass_.size(); k++)   // our own, predicted: already broken and shown
        if (pendingGlass_[k].brush == d.brush) { pendingGlass_.erase(pendingGlass_.begin() + (long)k); return; }
    if (map_.breakGlass(d.brush) && fx && glassFx_.size() < 256) glassFx_.push_back({map_.brushes[d.brush].box, d.how, d.at, d.dir, d.seed, false, map_.brushes[d.brush].material});
}

void NetClient::replayDestruction() {
    pendingGlass_.clear();
    for (const GameEvent& e : destructLog_) applyDestructEvent(e, false);
}

bool NetClient::predictGlass(int brush, int how, const Vec3& at, const Vec3& dir) {
    if (play_.active || brush < 0 || !map_.breakGlass((size_t)brush)) return false;
    pendingGlass_.push_back({(uint32_t)brush, timeNow()});
    if (glassFx_.size() < 256) glassFx_.push_back({map_.brushes[(size_t)brush].box, (uint8_t)how, at, dir, glassRand(glassSeed_), false});
    return true;
}

void NetClient::expireGlassPredictions(double now) {
    for (size_t k = 0; k < pendingGlass_.size();) {
        if (now - pendingGlass_[k].at < kGlassPredictWait) { k++; continue; }
        const uint32_t b = pendingGlass_[k].brush;
        pendingGlass_.erase(pendingGlass_.begin() + (long)k);
        if (b < map_.brushes.size() && map_.brushes[b].gone) {   // the server didn't break it: back as it was
            Brush br = map_.brushes[b];
            br.gone = false;
            map_.setBrush(b, br);
            map_.destructSerial++;
        }
    }
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
        if (!(p.flags & PROJ_STUCK)) map_.flyProjectile(p.pos, p.vel, projectileGravity(p.type, (p.flags & PROJ_LOB) ? kRocketLobGravity : 1.0f), dt);
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
    recStart_ = timeNow(); recLast_ = -1; recEvery_ = fullRate ? 0.0 : 0.016; recMapRev_ = 0xFFFFFFFFu; recEventId_ = 0; recHavePrev_ = false; std::memset(recMoveEv_, 0, sizeof recMoveEv_);
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
    for (const PlayerSnap& p : s.players)   // (movement events of the snapshots skipped go in the next one recorded)
        if (p.id < kMaxPlayers) recMoveEv_[p.id] |= p.keys & (uint16_t)~KEY_HELD_MASK;
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
    for (PlayerSnap& p : c.players)
        if (p.id < kMaxPlayers) p.keys = (uint16_t)((p.keys & KEY_HELD_MASK) | recMoveEv_[p.id]);
    std::memset(recMoveEv_, 0, sizeof recMoveEv_);
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
    snaps_.clear(); events_.clear(); lastEventId_ = 0; evHold_.clear(); clockInit_ = false; mapReady_ = false;
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
            destructLog_.clear();   // (a demo's map: its own breaks follow)
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
