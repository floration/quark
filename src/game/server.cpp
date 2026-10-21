#include "server.h"

// EV_PLAYERINFO's b byte: model in the low 4 bits, sound pack in the high 4
static uint8_t looksByte(const ServerPlayer& p) { return (uint8_t)((p.model & 15) | ((p.soundPack & 15) << 4)); }
#include "ranked.h"
#include <random>
#include "../common/profile.h"
#include "gamemodes.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <cstdarg>
#include <ctime>
#include <filesystem>

namespace {
constexpr double kTimeout = 10.0;
constexpr int kMaxQueuedCmds = 12;
constexpr uint32_t kMaxRewindMs = 500; // lag compensation window
constexpr float kMaxCmdTokensMs = 40.0f; // burst allowance after network jitter
// Tick-rate test during warmup: each window must stay clean or the rate steps down.
constexpr double kRateWindow = 3.0;
constexpr double kMaxLateFraction = 0.01;  // ticks that ran more than 2 periods late
constexpr int kMaxClientLossPct = 2;       // snapshot loss any client may report
const float kKnifeRange = weaponDef(WPN_KNIFE).range; // the knife's reach (also what clients draw)
constexpr float kRocketLife = 5.0f;

bool isLoopback(const NetAddr& a) { return (a.ip >> 24) == 127; }
}

bool g_serverRateCheck = true;

void Server::log(const std::string& s) {
    if (log_) log_(s);
    else printf("[server] %s\n", s.c_str());
}

bool Server::start(const ServerConfig& cfg, std::string* err) {
    cfg_ = cfg;
    if (cfg_.maxPlayers < 1) cfg_.maxPlayers = 1;
    if (cfg_.maxPlayers > kMaxPlayers) cfg_.maxPlayers = kMaxPlayers;
    if (cfg_.ranked) cfg_.custom = CustomRules();   // ranked plays by the standard rules
    if (cfg_.custom.active()) { // a custom mode: its mode, limits and map pool
        const CustomRules& c = cfg_.custom;
        cfg_.mode = c.baseMode;
        if (c.timeLimit) cfg_.timeLimit = c.timeLimit;
        if (c.scoreLimit) cfg_.scoreLimit = c.scoreLimit;
        if (c.roundsToWin) cfg_.roundsToWin = c.roundsToWin;
        if (!c.maps.empty() && std::find(c.maps.begin(), c.maps.end(), cfg_.map) == c.maps.end()) cfg_.map = c.maps[0];
    }
    setActiveRules(cfg_.custom);   // (the weapon table and movement everything in this process uses)
    if (cfg_.ranked) { // ranked = 1v1 on one of the ladder rulesets
        cfg_.mode = MODE_ARENA;
        if (!arenaVariantRanked(cfg_.arenaVariant)) cfg_.arenaVariant = 0;
    }
    std::string e;
    if (!map_.load(cfg_.map, &e)) { if (err) *err = e; return false; }
    if (cfg_.editMode && !map_.supportsMode(cfg_.mode)) // an edit session runs whatever the map can't play yet
        cfg_.mode = map_.supportsMode(MODE_FFA) ? MODE_FFA : cfg_.mode;
    if (cfg_.ranked && !isRankedMap(map_, cfg_.arenaVariant)) { if (err) *err = "ranked only runs on the ranked maps for its ladder"; return false; }
    if (cfg_.ranked) cfg_.editMode = false;
    // any map runs any mode (one not built for it may be missing what the mode needs: the host was warned)
    if (!cfg_.editMode && !map_.supportsMode(cfg_.mode)) log("warning: " + cfg_.map + " isn't built for " + modeTitle(cfg_.mode) + " - it may not work");
    if (!sock_.open(cfg_.port, true)) {
        if (err) *err = "could not bind UDP port " + std::to_string(cfg_.port) + " (already in use?)";
        return false;
    }
    for (auto& p : players_) p = ServerPlayer();
    for (auto& p : projectiles_) p = Projectile();
    tick_ = 0;
    timeMs_ = 0;
    rateLevel_ = 0;
    // Bandwidth grows with players x snapshot rate: big servers send fewer snapshots (snapEvery), the
    // simulation itself keeps the full tick rate.
    int cap = cfg_.maxTickRate > 0 ? cfg_.maxTickRate : 1000;
    while (rateLevel_ < kTickRateLevels - 1 && kTickRates[rateLevel_] > cap) rateLevel_++;
    nextTick_ = timeNow();
    resetRateWindow();
    rulesReset(*this);
    running_ = true;
    editing_ = editDirty_ = false;
    balanceBotsUnranked_ = cfg_.balanceBots;
    restartNow_ = restartAnnounced_ = false;
    if (cfg_.ranked) { cfg_.balanceBots = false; cfg_.vsBots = false; cfg_.botsEnemy = cfg_.botsAlly = 0; }
    if (cfg_.editMode) setEditing(true);
    if (cfg_.vsBots) {
        cfg_.autoStart = true;
        bool teams = modeIsTeamBased(cfg_.mode);
        for (int k = 0; k < cfg_.botsEnemy; k++) addBot(teams ? TEAM_B : TEAM_A, cfg_.botSkill);
        for (int k = 0; k < cfg_.botsAlly && teams; k++) addBot(TEAM_A, cfg_.botSkill);
    }
    log("listening on UDP " + std::to_string(port()) + " - map " + cfg_.map + ", mode " + modeName(cfg_.mode));
    openGameLog();
    if (cfg_.publicList && !cfg_.lanOnly) {
        master_ = std::make_unique<MasterAnnouncer>();
        master_->start(port());
    }
    glog(LOG_SIMPLE, "map %s mode %s", cfg_.map.c_str(), modeName(cfg_.mode));
    if (cfg_.upnp && !cfg_.lanOnly) {
        upnp_ = std::make_unique<PortMapper>();
        upnp_->start(port());
    }
    return true;
}

void Server::stop() {
    if (!running_) return;
    if (editDirty_ && !tempEdit_) autosave();
    for (int i = 0; i < kMaxPlayers; i++)
        if (players_[i].connected) dropPlayer(i, "server shutting down", true);
    sock_.close();
    if (cfg_.custom.active()) setActiveRules(CustomRules());
    if (upnp_) { upnp_->stop(); upnp_.reset(); }
    if (master_) { master_->stop(); master_.reset(); }
    running_ = false;
    log("stopped");
    if (glog_) { glog(LOG_SIMPLE, "server stopped"); fclose(glog_); glog_ = nullptr; }
}

void Server::run(std::atomic<bool>& quit) {
    std::string lastUpnp, lastMaster;
    while (!quit && running_) {
        double wait = frame();
        if (upnp_ && upnp_->done()) {
            std::string st = upnp_->status();
            if (st != lastUpnp) { log(st); lastUpnp = st; }
        }
        if (master_) {
            std::string st = master_->status();
            if (!st.empty() && st != lastMaster) { log(st); lastMaster = st; }
        }
        idle(wait);
    }
}

void Server::idle(double wait) {
    // Sleep in the socket until the next tick (wakes early for packets); the last ~0.3 ms is spun
    // by the caller's loop so 1 ms ticks stay on time.
    bool anyHuman = false;
    for (auto& p : players_) anyHuman |= p.connected && !p.isBot;
    if (!anyHuman) { if (wait > 0) sock_.waitUs((int)std::min(wait * 1e6, 5000.0)); return; } // nobody to be on time for: no spinning
    if (wait > 0.0003) sock_.waitUs((int)std::min((wait - 0.0003) * 1e6, 5000.0));
}

void Server::resetRateWindow() {
    rateWindowStart_ = timeNow();
    windowTicks_ = windowLate_ = windowSendFail_ = windowWorstLoss_ = 0;
    windowBusy_ = 0;
}

// Warmup only: if the host can't sustain the current rate cleanly, step down a level.
void Server::checkTickRate() {
    double now = timeNow();
    if (now - rateWindowStart_ < kRateWindow) return;
    if (!g_serverRateCheck) { resetRateWindow(); return; }
    if (editing_ || ms_.phase != PHASE_WARMUP || playerCount() == 0 || rateLevel_ >= kTickRateLevels - 1) { resetRateWindow(); return; }
    int worstLoss = windowWorstLoss_;
    double lateFrac = windowTicks_ ? windowLate_ / (double)windowTicks_ : 0;
    double busy = windowBusy_ / std::max(1e-6, now - rateWindowStart_); // share of the time spent simulating + sending
    // Late ticks alone don't mean the server can't keep up: on shared / burstable VMs the OS wakes us late
    // while the CPU is nearly idle, and the catch-up loop still runs every tick. Step down only when the
    // work itself is heavy (or late ticks come with real load), or packets / clients are being lost.
    bool bad = busy > 0.5 || (lateFrac > kMaxLateFraction && busy > 0.2) || windowSendFail_ > 0 || worstLoss > kMaxClientLossPct;
    if (bad) {
        char why[200];
        snprintf(why, sizeof why, "busy %.0f%%, late %.1f%%, send failures %d, worst client loss %d%%", busy * 100, lateFrac * 100, windowSendFail_, worstLoss);
        rateLevel_++;
        message("server tick rate lowered to " + std::to_string(tickRate()) + " Hz");
        log("tick rate -> " + std::to_string(tickRate()) + " Hz (" + why + ")");
    }
    resetRateWindow();
}

double Server::frame() {
    uint8_t buf[2048];
    NetAddr from;
    int n;
    while ((n = sock_.recv(from, buf, sizeof buf)) > 0) handlePacket(from, buf, n);

    double now = timeNow();
    double period = 1.0 / tickRate();
    if (now - nextTick_ > period * 20) { // fell far behind (debugger, hitch): skip ahead instead of spiralling
        windowLate_ += 20;
        nextTick_ = now;
    }
    while (now >= nextTick_) {
        if (now - nextTick_ > period * 2) windowLate_++;
        windowTicks_++;
        double t0 = timeNow();
        QZONE("server tick");
        { QZONE("server simulate"); simulate(); }
        logTick();
        tick_++;
        timeMs_ += 1000 / tickRate();
        if (tick_ % snapEvery() == 0) { QZONE("server snapshots"); sendSnapshots(); } // input still runs every tick
        double busy = timeNow() - t0;
        windowBusy_ += busy;
        if (tickTimes_) tickTimes_->push_back((float)busy);
        nextTick_ += 1.0 / tickRate();
        now = timeNow();
    }
    for (int i = 0; i < kMaxPlayers; i++)
        if (players_[i].connected && !players_[i].isBot && now - players_[i].lastRecv > kTimeout) dropPlayer(i, "timed out", false);
    checkTickRate();
    checkRestartFlag();
    return nextTick_ - timeNow();
}

int Server::findPlayer(const NetAddr& a) const {
    for (int i = 0; i < kMaxPlayers; i++)
        if (players_[i].connected && players_[i].addr == a) return i;
    return -1;
}

int Server::playerCount() const {
    int c = 0;
    for (auto& p : players_) c += p.connected;
    return c;
}

int Server::aliveCount(int team) const {
    int c = 0;
    for (auto& p : players_) c += p.connected && p.alive && p.team == team;
    return c;
}

int Server::teamCount(int team) const {
    int c = 0;
    for (auto& p : players_) c += p.connected && p.team == team;
    return c;
}

void Server::handlePacket(const NetAddr& from, const uint8_t* data, int len) {
    Reader r(data, len);
    if (r.u32() != kProtocolMagic) return;
    uint8_t type = r.u8();
    if (type == PKT_INFO_REQUEST) {
        uint8_t out[512];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_INFO_REPLY);
        w.str(listName()); w.str(cfg_.map); w.u8((uint8_t)cfg_.mode);
        w.u8((uint8_t)playerCount()); w.u8((uint8_t)cfg_.maxPlayers); w.u16(port());
        sock_.send(from, out, w.size());
        return;
    }
    if (type == PKT_CONNECT) { handleConnect(from, r); return; }
    int id = findPlayer(from);
    if (id < 0) return;
    if (r.u32() != players_[id].nonce) return;
    players_[id].lastRecv = timeNow();
    if (type == PKT_INPUT) handleInput(id, r);
    else if (type == PKT_DISCONNECT) dropPlayer(id, "disconnected", false);
    else if (type == PKT_MAP_REQUEST) { // client lacks this map (or has a different copy): send it a piece
        uint32_t crc = r.u32(), off = r.u32();
        (void)crc; // always serve the current map: in an edit session it may have changed since the client asked
        if (r.bad() || off >= map_.source.size()) return;
        uint8_t out[kMaxPacket];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_MAP_DATA);
        uint16_t n = (uint16_t)std::min<size_t>(kMapChunk, map_.source.size() - off);
        w.u32(players_[id].nonce); w.u32(map_.crc); w.u32((uint32_t)map_.source.size()); w.u32(off); w.u16(n);
        w.bytes((const uint8_t*)map_.source.data() + off, n);
        sock_.send(from, out, w.size());
    }
}

static void sendAccept(UdpSocket& sock, const NetAddr& to, uint32_t nonce, int id, const Map& map, int mode) {
    uint8_t out[512];
    Writer w(out, sizeof out);
    writeHeader(w, PKT_ACCEPT);
    w.u32(nonce); w.u8((uint8_t)id); w.str(map.name); w.u32(map.crc); w.u8((uint8_t)mode);
    sock.send(to, out, w.size());
}

void Server::handleConnect(const NetAddr& from, Reader& r) {
    uint16_t version = r.u16();
    uint32_t nonce = r.u32();
    std::string name = r.str();
    if (r.bad()) return;
    auto reject = [&](const std::string& why) {
        uint8_t out[300];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_REJECT);
        w.str(why);
        sock_.send(from, out, w.size());
    };
    if (version != kProtocolVersion) { reject("version mismatch (server " + std::to_string(kProtocolVersion) + ")"); return; }
    int existing = findPlayer(from);
    if (existing >= 0) {
        if (players_[existing].nonce == nonce) { sendAccept(sock_, from, nonce, existing, map_, cfg_.mode); return; }
        dropPlayer(existing, "reconnecting", false);
    }
    if (playerCount() >= cfg_.maxPlayers) {
        for (int i = kMaxPlayers - 1; i >= 0; i--)
            if (players_[i].connected && players_[i].isBot) { removeBot(i); break; }
    }
    if (playerCount() >= cfg_.maxPlayers) { reject("server is full"); return; }
    int id = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (!players_[i].connected) { id = i; break; }
    if (id < 0) { reject("server is full"); return; }

    for (auto& c : name) if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    name = uniqueName(name, -1);

    ServerPlayer& p = players_[id];
    p = ServerPlayer();
    p.connected = true;
    p.joinId = nextJoinId_++;
    vote_.yes &= ~(1u << id); vote_.no &= ~(1u << id); // a newcomer in this slot hasn't voted
    p.addr = from;
    p.nonce = nonce;
    p.name = name;
    p.admin = isLoopback(from);
    p.lastRecv = timeNow();
    for (auto& h : p.history) h.tick = 0xFFFFFFFFu;
    autoTeam(id);
    sendAccept(sock_, from, nonce, id, map_, cfg_.mode);

    // Tell the newcomer about everyone, and everyone about the newcomer.
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!players_[i].connected || i == id) continue;
        GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)i; e.b = looksByte(players_[i]); e.text = players_[i].name;
        sendEvent(id, e);
        if (!players_[i].rankId.empty()) { GameEvent r; r.type = EV_RANKINFO; r.a = (uint8_t)i; r.text = players_[i].rankId; sendEvent(id, r); }
    }
    GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)id; e.b = looksByte(p); e.text = name;
    broadcastEvent(e);
    GameEvent noRank; noRank.type = EV_RANKINFO; noRank.a = (uint8_t)id; // the slot's previous owner's id is gone
    broadcastEvent(noRank);
    message(name + " joined");
    sendRules(id);
    log(name + " connected from " + from.toString() + " as #" + std::to_string(id));

    float d = rulesRespawnDelay(*this);
    if (d >= 0 && p.team != TEAM_SPECTATOR) p.respawnAt = time() + 0.5f;
}

void Server::sendRules(int id) {
    if (!cfg_.custom.active()) return;
    std::string t = cfg_.custom.toText();
    int n = (int)(t.size() + 239) / 240;
    for (int k = 0; k < n; k++) {
        GameEvent e; e.type = EV_RULES; e.a = (uint8_t)k; e.b = (uint8_t)n; e.text = t.substr(k * 240, 240);
        sendEvent(id, e);
    }
}

void Server::autoTeam(int id) {
    ServerPlayer& p = players_[id];
    if (cfg_.mode == MODE_TOURNEY) { p.team = tourney_.running ? TEAM_SPECTATOR : TEAM_A; return; } // joins the next tourney
    if (!modeIsTeamBased(cfg_.mode) || (cfg_.vsBots && !p.isBot)) { p.team = TEAM_A; return; }
    if (cfg_.mode == MODE_DUEL || cfg_.mode == MODE_ARENA) { // duelTeamSize (1v1: 1) humans per side; balance bots make way, anyone else spectates
        int takenA = 0, takenB = 0;
        for (int i = 0; i < kMaxPlayers; i++) {
            const auto& o = players_[i];
            if (i == id || !o.connected || (o.balanceBot && !p.isBot)) continue;
            if (o.team == TEAM_A) takenA++;
            else if (o.team == TEAM_B) takenB++;
        }
        int size = cfg_.mode == MODE_ARENA ? 1 : std::min(std::max(cfg_.duelTeamSize, 1), 2);
        p.team = takenA < size && takenA <= takenB ? TEAM_A : takenB < size ? TEAM_B : takenA < size ? TEAM_A : TEAM_SPECTATOR;
        return;
    }
    int a = 0, b = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (i == id || !players_[i].connected || players_[i].isBot != p.isBot) continue;
        if (players_[i].team == TEAM_A) a++;
        else if (players_[i].team == TEAM_B) b++;
    }
    p.team = a <= b ? TEAM_A : TEAM_B;
}

void Server::dropPlayer(int id, const std::string& reason, bool notify) {
    ServerPlayer& p = players_[id];
    if (!p.connected) return;
    if (notify) {
        uint8_t out[300];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_KICKED);
        w.u32(p.nonce);
        w.str(reason);
        sock_.send(p.addr, out, w.size());
    }
    rulesOnLeave(*this, id);
    vote_.yes &= ~(1u << id); vote_.no &= ~(1u << id);
    std::string name = p.name;
    p.connected = false;
    p.alive = false;
    message(name + " left (" + reason + ")");
    log(name + " dropped: " + reason);
}

void Server::handleInput(int id, Reader& r) {
    ServerPlayer& p = players_[id];
    uint32_t ackMs = r.u32();
    uint32_t ackEvent = r.u32();
    uint8_t loss = r.u8();
    int n = r.u8();
    if (n > 16) return;
    UserCmd cmds[16];
    for (int i = 0; i < n; i++) cmds[i] = readCmd(r);
    int nStr = r.u8(); // reliable string commands: the unacknowledged ones, oldest first
    if (nStr > 16) return;
    uint16_t strSeq = 0;
    std::string strs[16];
    if (nStr) { strSeq = r.u16(); for (int k = 0; k < nStr; k++) strs[k] = r.str(); }
    if (r.bad()) return;

    p.reportedLoss = loss;
    windowWorstLoss_ = std::max(windowWorstLoss_, (int)loss);
    if (ackMs <= timeMs_ && ackMs > p.ackSnapMs) p.ackSnapMs = ackMs; // newest snapshot it has: delta base
    if (ackMs <= timeMs_ && ackMs + 10000 > timeMs_) {
        float sample = (float)(timeMs_ - ackMs);
        p.ping = p.ping == 0 ? sample : p.ping * 0.98f + sample * 0.02f;
    }
    if (ackEvent > p.ackedEvent) p.ackedEvent = ackEvent;
    while (!p.events.empty() && p.events.front().id <= p.ackedEvent) p.events.pop_front();

    uint32_t newest = p.cmdQueue.empty() ? p.lastCmdSeq : p.cmdQueue.back().seq;
    for (int i = 0; i < n; i++) // cmds are sent oldest first
        if (cmds[i].seq > newest) { p.cmdQueue.push_back(cmds[i]); newest = cmds[i].seq; }
    while ((int)p.cmdQueue.size() > kMaxQueuedCmds) { p.lastCmdSeq = p.cmdQueue.front().seq; p.cmdQueue.pop_front(); }
    drainCmds(id); // run input the moment it arrives instead of waiting for the next tick

    for (int k = 0; k < nStr && players_[id].connected; k++) {
        uint16_t seq = (uint16_t)(strSeq + k);
        if (seq != (uint16_t)(p.lastStringCmd + 1)) continue; // already handled (resent until acked)
        p.lastStringCmd = seq;
        handleStringCmd(id, strs[k]);
    }
}

void Server::handleStringCmd(int id, const std::string& s) {
    ServerPlayer& p = players_[id];
    std::istringstream ss(s);
    std::string cmd;
    ss >> cmd;
    std::string rest;
    std::getline(ss, rest);
    while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    if (cmd == "say") {
        if (rest.empty()) return;
        if (rest.size() > 120) rest.resize(120);
        GameEvent e; e.type = EV_CHAT; e.a = (uint8_t)id; e.text = rest;
        broadcastEvent(e);
        log(p.name + ": " + rest);
        glog(LOG_SIMPLE, "chat %d \"%s\": %s", id, p.name.c_str(), rest.c_str());
    } else if (cmd == "rankid") { // the player's ranked id (hash of their key); shown to everyone for the reports
        bool ok = rest.size() == 32;
        for (char c : rest) ok &= (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok || rest == p.rankId) return;
        for (int k = 0; k < kMaxPlayers; k++) if (k != id && players_[k].connected && players_[k].rankId == rest) return; // one key, one slot
        p.rankId = rest;
        GameEvent e; e.type = EV_RANKINFO; e.a = (uint8_t)id; e.text = rest;
        broadcastEvent(e);
    } else if (cmd == "ed" && editing_) {
        if (!applyEdit(id, rest)) { GameEvent e; e.type = EV_MESSAGE; e.text = "edit refused"; sendEvent(id, e); }
    } else if (cmd == "edcam" && editing_) { // where this editor's camera is, so others see it
        Vec3 c;
        std::istringstream cs(rest);
        if (cs >> c.x >> c.y >> c.z && std::isfinite(c.x + c.y + c.z)) p.move.pos = vmax(vmin(c, Vec3(4096, 4096, 4096)), Vec3(-4096, -4096, -4096));
    } else if (cmd == "team") {
        int t = rest == "a" ? TEAM_A : rest == "b" ? TEAM_B : rest == "spec" ? TEAM_SPECTATOR : -1;
        if (t < 0 || t == p.team) return;
        if (tourney_.running && t != TEAM_SPECTATOR) { // the bracket decides who plays
            GameEvent e; e.type = EV_MESSAGE; e.text = "a tourney is running - you can join the next one"; sendEvent(id, e);
            return;
        }
        if (!modeIsTeamBased(cfg_.mode) && t == TEAM_B) t = TEAM_A;
        if ((cfg_.mode == MODE_DUEL || cfg_.mode == MODE_ARENA) && t != TEAM_SPECTATOR && !p.letIn) {
            int n[2] = {0, 0};
            for (int i = 0; i < kMaxPlayers; i++)
                if (i != id && players_[i].connected && !players_[i].balanceBot && (players_[i].team == TEAM_A || players_[i].team == TEAM_B)) n[players_[i].team == TEAM_B]++;
            int size = cfg_.mode == MODE_ARENA ? 1 : std::min(std::max(cfg_.duelTeamSize, 1), 2);
            int mine = n[t == TEAM_B], other = n[t != TEAM_B];
            if (mine >= size) {
                if (other < size) {            // uneven: no vote needed, take the side with room
                    t = t == TEAM_A ? TEAM_B : TEAM_A;
                } else if (n[0] == n[1] && !cfg_.ranked) {   // even and full: the players decide
                    callVote(id, VOTE_LETIN, id);
                    return;
                } else {
                    GameEvent e; e.type = EV_MESSAGE; e.text = cfg_.ranked ? "that side is full (ranked is 1v1)" : "that side is full"; sendEvent(id, e);
                    return;
                }
            }
        }
        if (p.alive) killPlayer(id, -1, 255);
        if (t == TEAM_SPECTATOR) p.letIn = false;   // (sitting out gives the slot back)
        p.team = t;
        message(p.name + (t == TEAM_SPECTATOR ? " is spectating" : !modeIsTeamBased(cfg_.mode) ? " joined the game" : t == TEAM_A ? " joined team A" : " joined team B"));
        if (t != TEAM_SPECTATOR && rulesRespawnDelay(*this) >= 0) p.respawnAt = time() + 1.0f;
    } else if (cmd == "name") {
        if (rest.empty()) return;
        rest = uniqueName(rest, id);
        if (rest == p.name) return;
        message(p.name + " is now " + rest);
        p.name = rest;
        GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)id; e.b = looksByte(p); e.text = rest;
        broadcastEvent(e);
    } else if (cmd == "model") { // cosmetic only: same hitbox for every model
        int mdl = 0, pack = 0;              // "model <model> <sound pack>": both cosmetic, both heard / seen by everyone
        sscanf(rest.c_str(), "%d %d", &mdl, &pack);
        if (cfg_.ranked) mdl = 0; // ranked: everyone is the default model (sound packs are allowed)
        if (mdl < 0 || mdl >= kPlayerModels) mdl = 0;
        if (pack < 0 || pack >= kSoundPacks) pack = 0;
        if (mdl == p.model && pack == p.soundPack) return;
        p.model = mdl;
        p.soundPack = pack;
        GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)id; e.b = looksByte(p); e.text = p.name;
        broadcastEvent(e);
    } else if (cmd == "callvote") {
        std::istringstream vs(rest);
        std::string what, arg;
        vs >> what >> arg;
        if (what == "edit") { // callvote edit on / off
            if (arg != "on" && arg != "off") return;
            if ((arg == "on") == editing_ || rankedBlocks(id, "map editing")) return;
            callVote(id, VOTE_EDIT, arg == "on" ? 1 : 0);
        }
        else if (what == "ranked") { // callvote ranked on <vanilla|vampire|instagib> / callvote ranked off
            if (editing_) { GameEvent e; e.type = EV_MESSAGE; e.text = "not while editing the map"; sendEvent(id, e); return; }
            if (arg == "off") { if (cfg_.ranked) callVote(id, VOTE_RANKED, -1); return; }
            std::string lname;
            vs >> lname;
            int v = lname.empty() ? (arenaVariantRanked(cfg_.arenaVariant) ? cfg_.arenaVariant : 0) : arenaVariantFromName(lname.c_str());
            if (v < 0 || !arenaVariantRanked(v)) { GameEvent e; e.type = EV_MESSAGE; e.text = "ranked ladders: vanilla, vampire, instagib"; sendEvent(id, e); return; }
            if (cfg_.ranked && ms_.arenaVariant == v) return;
            callVote(id, VOTE_RANKED, v);
        }
        else if (what == "bots" || what == "botskill") { if (!rankedBlocks(id, "bots")) {
            if (what == "bots") callVote(id, VOTE_BOTS, arg == "off" ? 0 : 1);
            else {
                int k = -1;
                for (auto& c : arg) c = (char)toupper((unsigned char)c);
                for (int i = 0; i < BOT_SKILLS; i++) if (arg == botSkillName(i) || arg == std::to_string(i)) k = i;
                if (k >= 0) callVote(id, VOTE_BOTSKILL, k);
            }
        } }
        else if (what == "start") {
            if (ms_.phase != PHASE_WARMUP) { GameEvent e; e.type = EV_MESSAGE; e.text = "the match has already started"; sendEvent(id, e); }
            else callVote(id, VOTE_START, 0);
        }
        else if ((what == "tmaps" || what == "tmodes") && rankedBlocks(id, "tourney")) {}
        else if (what == "tmaps" || what == "tmodes") { // tourney rotation: callvote tmaps a,b,c / tmodes classic,hitscan
            bool ok = !arg.empty();
            std::istringstream ls(arg);
            std::string it;
            while (std::getline(ls, it, ',')) {
                Map m;
                if (what == "tmaps" ? (it.find_first_of("/\\.") != std::string::npos || !m.load(it) || !m.supportsMode(MODE_TOURNEY))
                                    : arenaVariantFromName(it.c_str()) < 0) ok = false;
            }
            if (!ok) { GameEvent e; e.type = EV_MESSAGE; e.text = "can't vote for that list"; sendEvent(id, e); }
            else callVote(id, what == "tmaps" ? VOTE_TMAPS : VOTE_TMODES, 0, arg);
        }
        else if (what == "map" || what == "mode") { // callvote map <name> [mode] [ruleset] / callvote mode <mode> [ruleset]
            std::string m2, v2;
            vs >> m2 >> v2;
            if (what == "mode") v2 = m2;
            int variant = v2.empty() ? cfg_.arenaVariant : arenaVariantFromName(v2.c_str());
            if (variant < 0) variant = cfg_.arenaVariant;
            std::string mapName = what == "map" ? arg : cfg_.map;
            int mode = what == "mode" ? modeFromName(arg.c_str()) : m2.empty() ? cfg_.mode : modeFromName(m2.c_str());
            Map m;
            std::string why;   // say exactly what's wrong instead of a generic refusal
            if (mode < 0) why = "unknown mode '" + (what == "mode" ? arg : m2) + "'";
            else if (cfg_.custom.active() && !cfg_.custom.maps.empty() && std::find(cfg_.custom.maps.begin(), cfg_.custom.maps.end(), mapName) == cfg_.custom.maps.end())
                why = mapName + " isn't in this custom mode's map pool";
            else if (cfg_.custom.active() && mode != cfg_.custom.baseMode) why = "this server plays the custom mode " + cfg_.custom.name;
            else if (mapName.find_first_of("/\\.") != std::string::npos || !m.load(mapName)) why = "this server doesn't have the map '" + mapName + "' (it may be on an older version)";

            bool ok = why.empty();
            if (ok && cfg_.ranked && (mode != MODE_ARENA || !arenaVariantRanked(variant) || !isRankedMap(m, variant))) {
                GameEvent e; e.type = EV_MESSAGE; e.text = "ranked server: 1v1 on a ranked map for the ladder (vanilla / vampire / instagib), or vote ranked off"; sendEvent(id, e);
                return;
            }
            if (!ok) { GameEvent e; e.type = EV_MESSAGE; e.text = "can't vote: " + why; sendEvent(id, e); }
            else callVote(id, VOTE_MAP, mode, mapName, variant);
        }
    } else if (cmd == "vote") {
        castVote(id, rest == "yes" || rest == "y" || rest == "1");
    } else if (cmd == "kill") {
        if (p.alive) killPlayer(id, -1, 255);
    } else if (p.admin) {
        command(s, id);
    }
}

void Server::command(const std::string& line, int fromPlayer) {
    std::istringstream ss(line);
    std::string cmd, arg;
    ss >> cmd >> arg;
    auto reply = [&](const std::string& t) {
        if (fromPlayer >= 0) { GameEvent e; e.type = EV_MESSAGE; e.text = t; sendEvent(fromPlayer, e); }
        else log(t);
    };
    if (cmd == "editmode" && cfg_.ranked) { reply("no map editing on a ranked server (vote ranked off first)"); return; }
    if (cmd == "editmode") { // editmode on|off: switch between editing the map together and playing it
        if (arg == "on") { if (!editing_) setEditing(true); return; }
        if (!editing_) return;
        int mode = map_.supportsMode(cfg_.mode) ? cfg_.mode : -1;
        if (mode < 0 && map_.preferredMode >= 0 && map_.supportsMode(map_.preferredMode)) mode = map_.preferredMode;
        if (mode < 0 && map_.supportsMode(MODE_FFA)) mode = MODE_FFA;
        for (int m = 0; m < MODE_COUNT && mode < 0; m++) if (map_.supportsMode(m)) mode = m;
        if (mode < 0) { { std::string why = "this map can't be played in any mode yet (add spawns)"; reply(why); message(why); return; } }
        if (tempEdit_) { setEditing(false); playMap(mode); return; } // play the edited copy; it's gone at the next map change
        if (!autosave()) { { std::string why = "could not save the map"; reply(why); message(why); return; } }
        setEditing(false);
        command("map " + cfg_.map + " " + modeName(mode), fromPlayer);
        return;
    }
    if (cmd == "reload") { cmd = "map"; arg = cfg_.map; } // re-read the current map file (edited while running)
    if (cmd == "map" || cmd == "mode") {
        std::string newMap = cmd == "map" ? arg : cfg_.map;
        int newMode = cfg_.mode;
        if (cmd == "mode") newMode = modeFromName(arg.c_str());
        else { std::string m2; if (ss >> m2) newMode = modeFromName(m2.c_str()); }
        if (newMode < 0) { reply("unknown mode"); return; }
        std::string v2; // optional 1v1 ruleset: map <name> 1v1 classic
        if (ss >> v2) {
            int v = arenaVariantFromName(v2.c_str());
            if (v < 0) { reply("unknown 1v1 ruleset"); return; }
            if (cfg_.ranked && !arenaVariantRanked(v)) { reply("ranked: only vanilla, laser vampire or instagib"); return; }
            cfg_.arenaVariant = v;
        }
        Map m;
        std::string err;
        if (!m.load(newMap, &err)) { reply(err); return; }
        if (!m.supportsMode(newMode)) reply("warning: " + newMap + " isn't built for " + modeTitle(newMode) + " - it may not work");
        if (cfg_.ranked && (newMode != MODE_ARENA || !arenaVariantRanked(cfg_.arenaVariant) || !isRankedMap(m, cfg_.arenaVariant))) {
            reply("ranked: only 1v1 on vanilla, laser vampire or instagib, on that ladder's ranked maps");
            return;
        }
        if (editing_ && tempEdit_) setEditing(false); // a voted edit was temporary: the new map loads as it is on disk
        map_ = m;
        cfg_.map = newMap;
        mapEdited_ = false;
        playMap(newMode);
    } else if (cmd == "addbot" && cfg_.ranked) {
        reply("no bots on a ranked server");
    } else if (cmd == "ranked") { // ranked on [ladder] / ranked off
        std::string lname;
        ss >> lname;
        int v = lname.empty() ? 0 : arenaVariantFromName(lname.c_str());
        if (arg == "off") setRanked(false, 0);
        else if (arg == "on" && v >= 0 && arenaVariantRanked(v)) setRanked(true, v);
        else reply("ranked on [vanilla|vampire|instagib] / ranked off");
    } else if (cmd == "addbot") {
        std::string sk;
        ss >> sk;
        int team = arg == "b" ? TEAM_B : TEAM_A;
        int skill = sk.empty() ? cfg_.botSkill : atoi(sk.c_str());
        if (addBot(team, std::min(std::max(skill, 0), BOT_SKILLS - 1)) < 0) reply("server is full");
    } else if (cmd == "kickbots") {
        for (int i = 0; i < kMaxPlayers; i++) if (players_[i].connected && players_[i].isBot) removeBot(i);
    } else if (cmd == "forcestart") {
        rulesForceStart(*this);
    } else if (cmd == "restart") {
        for (auto& p : players_) p.alive = false;
        rulesReset(*this);
        message("match restarted");
    } else if (cmd == "kick") {
        int id = atoi(arg.c_str());
        if (id >= 0 && id < kMaxPlayers && players_[id].connected) dropPlayer(id, "kicked", true);
    } else if (cmd == "say") {
        std::string rest;
        std::getline(ss, rest);
        message("[server] " + arg + rest);
    } else if (cmd == "status") {
        reply(statusText());
    } else {
        reply("commands: map <name> [mode], mode <name>, reload, editmode on|off, ranked on [ladder]|off, forcestart, restart, kick <id>, say <text>, status, addbot [a|b] [0-3], kickbots");
    }
}

std::string Server::statusText() {
    std::string s = "map " + cfg_.map + " mode " + modeName(cfg_.mode) + " players " + std::to_string(playerCount()) + "/" +
                    std::to_string(cfg_.maxPlayers);
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected) continue;
        s += "\n  #" + std::to_string(i) + " " + p.name + " team " + std::to_string(p.team) + " ping " +
             std::to_string((int)p.ping) + " " + p.addr.toString();
    }
    return s;
}

void Server::broadcastEvent(const GameEvent& e) {
    for (int i = 0; i < kMaxPlayers; i++)
        if (players_[i].connected) sendEvent(i, e);
}

void Server::sendEvent(int id, const GameEvent& e) {
    ServerPlayer& p = players_[id];
    if (p.isBot) return;
    if (p.events.size() > 256) p.events.pop_front(); // client is hopelessly behind
    GameEvent c = e;
    c.id = p.nextEventId++;
    p.events.push_back(c);
}

void Server::message(const std::string& text) {
    GameEvent e;
    e.type = EV_MESSAGE;
    e.text = text;
    broadcastEvent(e);
}

void Server::spawnPlayer(int id) {
    ServerPlayer& p = players_[id];
    int want = rulesSpawnType(*this, id);
    std::vector<const SpawnPoint*> cands;
    if (isStagedSpawn(want)) { // multi point: the latest stage reached (points taken) that has spawns for this side
        bool defending = spawnDefends(want);
        int stage = -1;
        for (auto& sp : map_.spawns)
            if (isStagedSpawn(sp.team) && spawnDefends(sp.team) == defending && spawnStage(sp.team) <= ms_.point) stage = std::max(stage, spawnStage(sp.team));
        want = stagedSpawn(std::max(stage, 0), defending);
    }
    for (auto& sp : map_.spawns) if (sp.team == want) cands.push_back(&sp);
    if (cands.empty()) for (auto& sp : map_.spawns) if (sp.team == SPAWN_ANY) cands.push_back(&sp);
    if (cands.empty()) for (auto& sp : map_.spawns) cands.push_back(&sp);

    Vec3 pos;
    float yaw = 0;
    if (ms_.mode == MODE_RACE && p.checkpoint > 0 && p.checkpoint <= (int)map_.checkpoints.size() && (ms_.phase == PHASE_LIVE || ms_.phase == PHASE_WARMUP)) { // (practice too)
        const AABB& c = map_.checkpoints[p.checkpoint - 1];
        pos = {(c.mn.x + c.mx.x) * 0.5f, c.mn.y + 0.05f, (c.mn.z + c.mx.z) * 0.5f};
        yaw = p.yaw;
    } else {
        // Pick the free spawn farthest from other living players.
        const SpawnPoint* best = cands[rand() % cands.size()];
        float bestScore = -1;
        for (auto* sp : cands) {
            float nearest = 1e9f;
            for (int i = 0; i < kMaxPlayers; i++)
                if (i != id && players_[i].connected && players_[i].alive)
                    nearest = std::min(nearest, length(players_[i].move.pos - sp->pos));
            if (map_.boxSolid(playerBox(sp->pos, false))) continue;
            float score = nearest + (rand() % 100) * 0.01f;
            if (score > bestScore) { bestScore = score; best = sp; }
        }
        pos = best->pos;
        yaw = best->yaw;
    }
    p.move = MoveState();
    p.move.pos = pos;
    p.yaw = yaw;
    p.pitch = 0;
    p.alive = true;
    p.health = kMaxHealth;
    p.weapon = p.selectedWeapon;
    p.knifeBackAt = 0;
    if (!weaponAllowed(p.weapon)) // restricted pool: first allowed weapon, in slot order
        for (int k = 0; k < WPN_COUNT; k++) if (weaponAllowed(kWeaponSlots[k])) { p.weapon = kWeaponSlots[k]; break; }
    const DuelModDef& dm = duelModDef(duelMod()); // duel: this life's modifier (none outside duel)
    p.health = dm.health == kMaxHealth ? maxHealth() : dm.health;   // (a custom mode sets its own health)
    p.move.mods |= dm.moveMods;
    if (arenaRules() & AR_QUICK_DASH) p.move.mods |= MOVE_QUICK_DASH;
    if (arenaRules() & AR_LOW_GRAVITY) p.move.mods |= MOVE_LOW_GRAVITY;
    p.nextFire = time() + 0.25f;
    p.respawnAt = -1;
    GameEvent e;
    e.type = EV_SPAWN;
    e.a = (uint8_t)id;
    e.c = (int16_t)std::lround(std::remainder(yaw, 2 * PI) * 1000.0f);
    sendEvent(id, e);
}

void Server::killPlayer(int victim, int killer, int weapon) {
    ServerPlayer& v = players_[victim];
    if (!v.alive) return;
    v.alive = false;
    v.health = 0;
    v.deaths++;
    if (killer >= 0 && killer != victim) {
        ServerPlayer& k = players_[killer];
        bool teamkill = modeIsTeamBased(cfg_.mode) && k.team == v.team;
        if (teamkill) k.score--;
        else { k.kills++; k.score++; }
    } else if (cfg_.mode == MODE_FFA && ms_.phase == PHASE_LIVE) {
        v.score--; // suicide
    }
    GameEvent e;
    e.type = EV_KILL;
    e.a = (uint8_t)(killer < 0 ? 255 : killer);
    e.b = (uint8_t)victim;
    e.c = (int16_t)weapon;
    broadcastEvent(e);
    glog(LOG_SIMPLE, "kill killer=%d \"%s\" victim=%d \"%s\" weapon=%d", killer, killer >= 0 ? players_[killer].name.c_str() : "", victim,
         v.name.c_str(), weapon);
    rulesOnDeath(*this, victim, killer);
    float d = rulesRespawnDelay(*this);
    v.respawnAt = d >= 0 ? time() + d : -1;
}

void Server::damagePlayer(int victim, int attacker, float dmg, int weapon, const Vec3& knock) {
    ServerPlayer& v = players_[victim];
    if (!v.alive) return;
    bool canHurt = rulesCanDamage(*this, attacker, victim);
    bool friendly = attacker >= 0 && attacker != victim && modeIsTeamBased(cfg_.mode) && players_[attacker].team == v.team;
    // Race: you can only affect yourself (rocket jumps etc.), never other racers.
    if (ms_.mode == MODE_RACE && ms_.phase != PHASE_WARMUP && attacker >= 0 && attacker != victim) return;
    if (isItMode(ms_.mode) && attacker >= 0 && attacker != victim) {
        // hide and seek / tag: only "it" can touch anyone, with the knife, once the round is live.
        // Hide and seek: one stab finds (kills) a hider. Tag: the stab passes "it" on.
        ServerPlayer& a = players_[attacker];
        if (ms_.phase != PHASE_LIVE || !a.it || v.it || weapon != WPN_KNIFE) return;
        if (ms_.mode == MODE_TAG) {
            a.it = false;
            v.it = true;
            v.itFreezeUntil = time() + kTagFreeze;
            a.score++;
            message(v.name + " is it");
            glog(LOG_SIMPLE, "tag %d -> %d", attacker, victim);
            return;
        }
        v.found = true;
        killPlayer(victim, attacker, WPN_KNIFE); // found
        return;
    }
    Vec3 kb = cfg_.custom.active() ? knock * cfg_.custom.knockback : knock;   // (a custom mode's knockback)
    if (!friendly) {
        v.move.vel += kb;
        if (kb.y > 0.5f) v.move.flags &= ~MF_ONGROUND;
    }
    if (!canHurt || dmg <= 0) return;
    uint8_t rules = arenaRules();
    if (attacker == victim && (rules & AR_NO_SELF_DAMAGE)) return;
    if (attacker >= 0 && attacker != victim) dmg *= duelModDef(duelMod()).damageMul; // glass cannons / double damage
    if (attacker >= 0 && attacker != victim && (rules & AR_INSTAGIB)) dmg = std::max(dmg, v.health + 1);
    if (attacker >= 0 && attacker != victim && !friendly) players_[attacker].dmgDealt += std::min(dmg, std::max(0.0f, v.health)); // (the health it took)
    v.health -= dmg;
    v.lastDamagedTick = tick_;
    if (attacker >= 0 && attacker != victim) {
        lastProgress_ = time();
        // Hit feedback (hitsound, damage number): rapid small hits (laser) are batched every 50 ms
        // instead of sending a reliable event per tick. c = damage in hundredths.
        hitAccum_[attacker][victim] += dmg;
        hitPending_ = true;
        if (time() >= hitFlushAt_[attacker][victim]) flushHit(attacker, victim);
        // Vampire laser: damage dealt heals the attacker (up to full health).
        ServerPlayer& a = players_[attacker];
        if ((rules & AR_VAMPIRE) && a.alive) { // healing fades as the round goes on
            float f = clampf((time() - roundStart_) / kVampireFadeTime, 0, 1);
            a.health = std::min(maxHealth(), a.health + dmg * (1.0f - (1.0f - kVampireMinHeal) * f));
        }
    }
    glog(LOG_VERBOSE, "hit attacker=%d victim=%d dmg=%.2f weapon=%d health=%.2f", attacker, victim, dmg, weapon, v.health);
    if (v.health <= 0) killPlayer(victim, attacker, weapon);
}

void Server::flushHit(int attacker, int victim) {
    float& acc = hitAccum_[attacker][victim];
    if (acc <= 0) return;
    GameEvent e; e.type = EV_HIT; e.a = (uint8_t)attacker; e.b = (uint8_t)victim;
    e.c = (int16_t)std::lround(std::min(acc, 300.0f) * 100.0f);
    sendEvent(attacker, e);
    acc = 0;
    hitFlushAt_[attacker][victim] = time() + 0.05f;
}

void Server::simulate() {
    if (editing_) { // nobody plays: just keep acknowledging input (cameras) and autosave now and then
        for (auto& p : players_) {
            if (!p.connected) continue;
            if (!p.cmdQueue.empty()) { p.lastCmdSeq = p.cmdQueue.back().seq; p.yaw = p.cmdQueue.back().yaw; p.pitch = p.cmdQueue.back().pitch; }
            p.cmdQueue.clear();
        }
        if (editDirty_ && !tempEdit_ && time() >= nextAutosave_) autosave();
        return;
    }
    if (hitPending_) { // batched hit feedback waiting to go out (skip the 32x32 scan when there's none)
        hitPending_ = false;
        for (int a = 0; a < kMaxPlayers; a++)
            for (int v = 0; v < kMaxPlayers; v++) {
                if (hitAccum_[a][v] <= 0) continue;
                if (time() >= hitFlushAt_[a][v]) flushHit(a, v);
                if (hitAccum_[a][v] > 0) hitPending_ = true;
            }
    }
    for (int i = 0; i < kMaxPlayers; i++) {
        ServerPlayer& p = players_[i];
        if (!p.connected) continue;
        if (p.isBot) {
            UserCmd c;
            { QZONE("bot think"); c = botThink(*this, i); }
            c.seq = tick_; // distinct shot spread per shot
            c.msec = (uint8_t)(1000 / tickRate());
            c.viewTick = timeMs_;
            { QZONE("bot cmd"); runPlayerCmd(i, c); }
            continue;
        }
        p.cmdTokensMs = std::min(p.cmdTokensMs + 1000.0f / tickRate(), kMaxCmdTokensMs);
        { QZONE("human cmds"); drainCmds(i); }
    }
    // Record positions for lag compensation.
    for (auto& p : players_) {
        if (!p.connected) continue;
        auto& h = p.history[tick_ % ServerPlayer::kHistory];
        h.tick = timeMs_;
        h.pos = p.move.pos;
        h.crouched = p.move.flags & MF_CROUCHED;
        h.alive = p.alive;
    }
    { QZONE("projectiles"); tickProjectiles(); }
    tickVote();
    if (time() >= nextBalanceCheck_) { nextBalanceCheck_ = time() + 1.0f; updateBalanceBots(); }
    for (int i = 0; i < kMaxPlayers; i++) {
        ServerPlayer& p = players_[i];
        if (!p.connected) continue;
        if (p.alive && p.move.pos.y < map_.killY()) killPlayer(i, -1, 252);
        if (!p.alive && p.respawnAt >= 0 && time() >= p.respawnAt && p.team != TEAM_SPECTATOR) spawnPlayer(i);
        if (p.alive && (arenaRules() & AR_REGEN) && tick_ - p.lastDamagedTick > (uint32_t)(3 * tickRate()) && p.health < maxHealth())
            p.health = std::min(maxHealth(), p.health + 10.0f * dt()); // regeneration: 10 hp/s after 3 s untouched
    }
    rulesTick(*this);
}

// Commands cost one token each and tokens refill at the tick rate, so a client can't
// move faster than real time, but bursts after network jitter are absorbed immediately.
void Server::drainCmds(int id) {
    ServerPlayer& p = players_[id];
    while (!p.cmdQueue.empty()) {
        UserCmd& c = p.cmdQueue.front();
        if (c.msec != 1 && c.msec != 2 && c.msec != 4 && c.msec != 8) c.msec = 1;
        if (p.cmdTokensMs < c.msec) break;
        p.cmdTokensMs -= c.msec;
        UserCmd run = c;
        p.cmdQueue.pop_front();
        runPlayerCmd(id, run);
    }
}

void Server::runPlayerCmd(int id, const UserCmd& cmd) {
    ServerPlayer& p = players_[id];
    p.lastCmdSeq = cmd.seq;
    p.yaw = cmd.yaw;
    p.pitch = cmd.pitch;
    p.prevButtons = p.buttons;
    p.buttons = cmd.buttons;
    p.lastViewTick = cmd.viewTick;
    if (cmd.weapon < WPN_COUNT) p.selectedWeapon = cmd.weapon; // also while dead: you respawn with it
    if (!p.alive) return;
    if (rulesFrozen(*this, id)) p.move.flags |= MF_FROZEN;
    else p.move.flags &= ~MF_FROZEN;
    pmove(p.move, cmd, map_);
    if (p.move.flags & MF_ONGROUND) p.lastGround = p.move.pos;

    // the knife can't be drawn while it's away after a throw (unless it's the only weapon this mode allows)
    auto otherWeapon = [&]() { for (int k = 0; k < WPN_COUNT; k++) { int w = kWeaponSlots[k]; if (w != WPN_KNIFE && weaponAllowed(w)) return w; } return -1; };
    bool knifeAway = time() < p.knifeBackAt;
    int heldWeapon = p.weapon;
    if (cmd.weapon < WPN_COUNT && weaponAllowed(cmd.weapon) && !(knifeAway && cmd.weapon == WPN_KNIFE && otherWeapon() >= 0)) p.weapon = cmd.weapon;
    if (knifeAway && p.weapon == WPN_KNIFE && otherWeapon() >= 0) p.weapon = otherWeapon();
    // a weapon switch never fires the new weapon with a button held from before: let go and press again
    if (p.weapon != heldWeapon && (cmd.buttons & (BTN_FIRE | BTN_ALTFIRE))) p.fireLocked = true;
    // Sticky launcher: alt-fire detonates your armed stickies.
    if (p.weapon == WPN_STICKY && (cmd.buttons & BTN_ALTFIRE) && !p.fireLocked)
        for (auto& pr : projectiles_)
            if (pr.active && pr.owner == id && pr.type == WPN_STICKY && time() >= pr.armAt) {
                pr.active = false;
                explode(pr.pos, id, WPN_STICKY);
            }
    // Knife and shotgun: alt-fire is its own shot (throw / bouncing pellets); it takes priority over fire.
    bool itMode = isItMode(ms_.mode);
    bool altShot = altFires(p.weapon) && (cmd.buttons & BTN_ALTFIRE) && !(p.weapon == WPN_KNIFE && knifeAway) && !itMode; // no throwing in hide and seek / tag
    bool wantFire = ((cmd.buttons & BTN_FIRE) != 0 || altShot) && (!itMode || p.it); // only "it" has a knife
    if (p.fireLocked) { // the click that threw the knife doesn't also fire the weapon you switched to
        if (!(cmd.buttons & (BTN_FIRE | BTN_ALTFIRE))) p.fireLocked = false;
        else wantFire = altShot = false;
    }
    if (weaponDef(p.weapon).semiAuto) { // one shot per click
        if (wantFire && !(p.prevButtons & BTN_FIRE)) p.fireQueuedUntil = time() + kFireBuffer;
        wantFire = time() <= p.fireQueuedUntil;
    }
    // A knife swing that missed keeps cutting along your current aim for a moment (one hit at most).
    if (p.knifeSwingUntil > 0 && (p.weapon != WPN_KNIFE || time() > p.knifeSwingUntil)) p.knifeSwingUntil = -1;
    if (p.knifeSwingUntil > 0 && !(p.move.flags & MF_FROZEN)) {
        UserCmd sw = cmd; sw.buttons &= ~BTN_ALTFIRE;
        if (fireWeapon(id, sw)) p.knifeSwingUntil = -1;
    }
    // Fast weapons (laser: every 10 ms) can fire more than once in a command at low tick rates.
    for (int shot = 0; shot < 16 && wantFire && time() >= p.nextFire && !(p.move.flags & MF_FROZEN); shot++) {
        p.fireQueuedUntil = -1;
        // Holding fire keeps an exact cadence, catching up shots that fell between commands (the laser
        // fires every 1 ms, so late or batched commands must not drop shots). After a pause, or more
        // than 100 ms behind, the timer restarts from now.
        float refire = shotRefire(p.weapon, (cmd.buttons & BTN_ALTFIRE) != 0);
        bool held = (p.prevButtons & (altShot ? BTN_ALTFIRE : BTN_FIRE)) != 0;
        p.nextFire = (held && time() - p.nextFire < 0.1f) ? p.nextFire + refire : time() + refire;
        p.lastFire = time();
        bool threw = p.weapon == WPN_KNIFE && (cmd.buttons & BTN_ALTFIRE) && !itMode && !knifeAway; // same test as fireWeapon
        bool hit = fireWeapon(id, cmd);
        p.knifeSwingUntil = (p.weapon == WPN_KNIFE && !threw && !hit) ? time() + kKnifeSwingTime : -1;
        if (threw) { // the knife is away: switch to the next weapon, ready to fire right away
            p.knifeBackAt = time() + kKnifeAway;
            p.fireLocked = true;
            if (otherWeapon() >= 0) p.weapon = otherWeapon();
            p.nextFire = time();
            p.fireQueuedUntil = -1;
            break;
        }
    }
}

Vec3 Server::spreadDir(const Vec3& dir, float spread, uint32_t seq, int pellet) {
    if (spread <= 0) return dir;
    Vec3 right = normalize(cross(dir, {0, 1, 0})), up = cross(right, dir);
    float a, r;
    spreadOffset(seq, pellet, spread, a, r);
    return normalize(dir + right * (std::cos(a) * r) + up * (std::sin(a) * r));
}

// One lag-compensated ray: adds dmg to the first player it hits (tested against where players
// were at the shooter's view time).
void Server::hitscan(int id, const Vec3& eye, const Vec3& dir, float range, uint32_t vt, float* dmgPer, float dmg, float headDmg, float grow, bool pierce) {
    float bang;
    float wall = map_.raycastBullet(eye, dir, range, &bang); // goes through wallbang brushes
    float best = wall >= 0 ? wall : range;
    int hit = -1;
    AABB hitBb;
    Vec3 inv = safeInv(dir);
    for (int i = 0; i < kMaxPlayers; i++) {
        if (i == id || !players_[i].connected || !players_[i].alive) continue;
        Vec3 pos = players_[i].move.pos;
        bool crouched = players_[i].move.flags & MF_CROUCHED;
        // newest recorded state at or before the shooter's view time
        for (uint32_t k = 1; k <= (uint32_t)ServerPlayer::kHistory && k <= tick_; k++) {
            const auto& h = players_[i].history[(tick_ - k) % ServerPlayer::kHistory];
            if (h.tick == 0xFFFFFFFFu || h.tick > timeMs_) break;
            if (h.tick <= vt) { if (h.alive) { pos = h.pos; crouched = h.crouched; } break; }
        }
        AABB bb = hitBox(pos, crouched);
        bb.mn.x -= grow; bb.mn.z -= grow; bb.mx.x += grow; bb.mx.z += grow;
        if (pierce) { // sniper collateral: every player along the line up to the wall is hit
            float t = rayAABB(eye, inv, bb, best);
            if (t < 0) continue;
            float y = eye.y + dir.y * t, d = dmg;
            if (headDmg > 0 && y >= bb.mx.y - (bb.mx.y - bb.mn.y) * kHeadFraction) d = std::max(d, headDmg);
            dmgPer[i] += (bang >= 0 && t > bang) ? d * kWallbangDamage : d;
            continue;
        }
        float t = rayAABB(eye, inv, bb, best);
        if (t >= 0 && t < best) { best = t; hit = i; hitBb = bb; }
    }
    if (hit < 0) return;
    float y = eye.y + dir.y * best; // headshot: the ray enters the top of the box
    if (headDmg > 0 && y >= hitBb.mx.y - (hitBb.mx.y - hitBb.mn.y) * kHeadFraction) dmg = std::max(dmg, headDmg);
    dmgPer[hit] += (bang >= 0 && best > bang) ? dmg * kWallbangDamage : dmg;
}

// A pellet that ricochets: straight until it hits a player (done) or a wall, then reflects off it, up to
// kShotgunBounces times, with whatever range is left. Players are tested where the shooter saw them.
void Server::bounceShot(int id, Vec3 o, Vec3 dir, float range, uint32_t vt, float* dmgPer, float dmg) {
    for (int b = 0; b <= kShotgunBounces && range > 0.01f; b++) {
        Vec3 n;
        float wall = map_.raycast(o, dir, range, &n);
        float best = wall >= 0 ? wall : range;
        int hit = -1;
        Vec3 inv = safeInv(dir);
        for (int i = 0; i < kMaxPlayers; i++) {
            if (i == id || !players_[i].connected || !players_[i].alive) continue; // your own ricochets never hit you
            Vec3 pos = players_[i].move.pos;
            bool crouched = players_[i].move.flags & MF_CROUCHED;
            for (uint32_t k = 1; k <= (uint32_t)ServerPlayer::kHistory && k <= tick_; k++) {
                const auto& h = players_[i].history[(tick_ - k) % ServerPlayer::kHistory];
                if (h.tick == 0xFFFFFFFFu || h.tick > timeMs_) break;
                if (h.tick <= vt) { if (h.alive) { pos = h.pos; crouched = h.crouched; } break; }
            }
            float t = rayAABB(o, inv, hitBox(pos, crouched), best);
            if (t >= 0 && t < best) { best = t; hit = i; }
        }
        if (hit >= 0) { dmgPer[hit] += dmg; return; }
        if (wall < 0) return;
        o = o + dir * wall + n * 0.01f;
        dir = dir - n * (2.0f * dot(dir, n));
        range -= wall;
    }
}

bool Server::fireWeapon(int id, const UserCmd& cmd) {
    ServerPlayer& p = players_[id];
    const WeaponDef& wd = weaponDef(p.weapon);
    float eyeH = ((p.move.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * bodyScale(); // giants shoot from their own eyes
    Vec3 eye = p.move.pos + Vec3(0, eyeH, 0);
    Vec3 aim = angleForward(cmd.yaw, cmd.pitch);
    bool scoped = wd.scopedSpread >= 0 && (cmd.buttons & BTN_ALTFIRE);
    float spread = scoped ? wd.scopedSpread : wd.spread;

    bool alt = (cmd.buttons & BTN_ALTFIRE) != 0;
    // a throw needs the knife in hand and a mode that allows it (holding alt while clicking fire used to throw
    // past both checks: endless knives in hide and seek / tag)
    if (p.weapon == WPN_KNIFE) alt = alt && time() >= p.knifeBackAt && !isItMode(ms_.mode);
    if (p.weapon == WPN_KNIFE && alt) { // thrown knife: a fast arcing projectile, hits for knife damage
        for (auto& pr : projectiles_) {
            if (pr.active) continue;
            pr = Projectile();
            pr.active = true;
            pr.id = nextProjId_++;
            pr.type = WPN_KNIFE;
            pr.owner = id;
            pr.pos = eye + aim * 0.3f;
            pr.vel = aim * kKnifeThrowSpeed;
            pr.dieAt = time() + kKnifeThrowLife;
            return false;
        }
        return false;
    }
    if (p.weapon == WPN_SYRINGE) {
        if (!alt) { p.health = std::min(maxHealth(), p.health + wd.damage); return false; }   // a dose for yourself
        for (auto& pr : projectiles_) {   // thrown: an arcing dart that heals the teammate it hits
            if (pr.active) continue;
            pr = Projectile();
            pr.active = true;
            pr.id = nextProjId_++;
            pr.type = WPN_SYRINGE;
            pr.owner = id;
            pr.pos = eye + aim * 0.3f;
            pr.vel = aim * kSyringeThrowSpeed;
            pr.dieAt = time() + kSyringeThrowLife;
            return false;
        }
        return false;
    }
    if (wd.projectile) {
        if (wd.sticky) { // too many out: the oldest one fizzles
            int count = 0, oldest = -1;
            for (int k = 0; k < kMaxProjectiles; k++) {
                auto& pr = projectiles_[k];
                if (!pr.active || pr.owner != id || pr.type != WPN_STICKY) continue;
                count++;
                if (oldest < 0 || (uint16_t)(pr.id - projectiles_[oldest].id) > 0x8000) oldest = k;
            }
            if (count >= kMaxStickies && oldest >= 0) projectiles_[oldest].active = false;
        }
        if (p.weapon == WPN_BUBBLE && alt) { // the stream: every bubble at once, each at its own speed
            int n = 0;
            for (auto& pr : projectiles_) {
                if (pr.active) continue;
                pr = Projectile();
                pr.active = true;
                pr.id = nextProjId_++;
                pr.type = WPN_BUBBLE;
                pr.owner = id;
                pr.pos = eye + aim * 0.3f;
                pr.vel = spreadDir(aim, 0.09f, cmd.seq, n) * kBubbleStreamSpeeds[n] * (wd.projSpeed / baseWeaponDef(WPN_BUBBLE).projSpeed);
                pr.dieAt = time() + wd.life;
                if (++n == kBubbleStream) break;
            }
            return false;
        }
        for (auto& pr : projectiles_) {
            if (pr.active) continue;
            pr = Projectile();
            pr.active = true;
            pr.id = nextProjId_++;
            pr.type = p.weapon;
            pr.owner = id;
            pr.pos = eye + aim * 0.3f;
            pr.vel = spreadDir(aim, spread, cmd.seq, 0) * wd.projSpeed;
            if (p.weapon == WPN_ROCKET && alt) { pr.vel = pr.vel * kRocketLobSpeed; pr.gravMul = kRocketLobGravity; }
            pr.dieAt = time() + wd.life;
            pr.armAt = time() + kStickyArmTime;
            return false;
        }
        return false;
    }
    uint32_t vt = cmd.viewTick; // ms on the server clock
    if (vt > timeMs_) vt = timeMs_;
    if (timeMs_ - vt > kMaxRewindMs) vt = timeMs_ - kMaxRewindMs;
    float range = p.weapon == WPN_KNIFE ? kKnifeRange : wd.range;
    // Pellets add up per target so one shotgun blast is one hit (one damage event) per player.
    float dmgPer[kMaxPlayers] = {};
    // Laser alt-fire is a tractor beam: pulls the target toward you for a little less damage.
    bool pull = wd.beam && (cmd.buttons & BTN_ALTFIRE);
    float dmg = wd.damage * (pull ? kLaserPullDamage : 1.0f);
    bool bounce = p.weapon == WPN_SHOTGUN && alt; // ricochet pellets: half damage, off up to 2 walls
    for (int k = 0; k < wd.pellets; k++) {
        Vec3 d = spreadDir(aim, spread, cmd.seq, k);
        if (bounce) bounceShot(id, eye, d, range, vt, dmgPer, dmg * kShotgunBounceDamage);
        else hitscan(id, eye, d, range, vt, dmgPer, dmg, pull ? 0.0f : headDamage(p.weapon), p.weapon == WPN_KNIFE ? kKnifeGrow : 0.0f, p.weapon == WPN_SNIPER);
    }
    bool any = false;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (dmgPer[i] <= 0) continue;
        Vec3 knock = aim * (wd.pellets > 1 ? 0.15f * dmgPer[i] / wd.damage : 1.0f);
        if (wd.beam) knock = aim * (kLaserPush * wd.refire * (pull ? -kLaserPullScale : 1.0f)); // same push per second at any fire rate
        damagePlayer(i, id, dmgPer[i], p.weapon, knock);
        any = true;
    }
    return any;
}

void Server::tickProjectiles() {
    for (auto& pr : projectiles_) {
        if (!pr.active) continue;
        if (time() >= pr.dieAt) { pr.active = false; continue; }
        if (pr.stuck && pr.onCart) {
            if (map_.dynSolids.empty()) pr.active = false; // cart gone (round over)
            else pr.pos = map_.payloadPoint(ms_.payloadDist) + pr.cartOffset;
        }
        if (pr.stuck) continue;
        const WeaponDef& wd = weaponDef(pr.type);
        pr.vel.y -= (pr.type == WPN_KNIFE ? kKnifeThrowGravity : pr.type == WPN_SYRINGE ? kSyringeThrowGravity : wd.gravity * pr.gravMul) * dt();
        Vec3 step = pr.vel * dt();
        float len = length(step);
        if (len < 1e-6f) continue;
        Vec3 dir = step / len;
        Vec3 n;
        float wall = map_.raycast(pr.pos, dir, len, &n);
        float best = wall >= 0 ? wall : len;
        int hitPlayer = -1;
        AABB hitBb;
        Vec3 inv = safeInv(dir);
        for (int i = 0; i < kMaxPlayers; i++) {
            auto& p = players_[i];
            if (!p.connected || !p.alive || i == pr.owner) continue;
            AABB b = hitBox(p.move.pos, p.move.flags & MF_CROUCHED);
            float t = rayAABB(pr.pos, inv, b, best);
            if (t >= 0 && t <= best) { best = t; hitPlayer = i; hitBb = b; }
        }
        if (wall < 0 && hitPlayer < 0) { pr.pos += step; continue; }
        Vec3 at = pr.pos + dir * std::max(0.0f, best - 0.05f);
        if (pr.type == WPN_SYRINGE) {  // a teammate: 30 hp and every cooldown reset; anyone else: nothing
            auto& o = players_[pr.owner];
            if (hitPlayer >= 0 && modeIsTeamBased(ms_.mode) && players_[hitPlayer].team == o.team) {
                auto& t = players_[hitPlayer];
                t.health = std::min(maxHealth(), t.health + kSyringeHeal * tunedDamage(WPN_SYRINGE));
                t.nextFire = time();
                t.move.dashCooldown = 0;
                t.move.hookCooldown = 0;
                if (t.knifeBackAt > time()) t.knifeBackAt = time();
                GameEvent e; e.type = EV_HEALED; e.a = (uint8_t)hitPlayer; e.b = (uint8_t)pr.owner; broadcastEvent(e);
            }
            pr.active = false;
        } else if (wd.sticky) {               // stick where it landed (on a player: it drops there)
            pr.pos = hitPlayer < 0 ? at + n * 0.08f : at;
            pr.vel = {};
            pr.stuck = hitPlayer < 0;
            if (pr.stuck && !map_.dynSolids.empty()) {
                AABB cart = map_.dynSolids[0];
                cart.mn -= Vec3(0.2f, 0.2f, 0.2f); cart.mx += Vec3(0.2f, 0.2f, 0.2f);
                if (cart.contains(pr.pos)) {
                    pr.onCart = true;
                    pr.cartOffset = pr.pos - map_.payloadPoint(ms_.payloadDist);
                }
            }
        } else if (wd.splashRadius <= 0) { // crossbow bolt / thrown knife: direct hit only
            if (hitPlayer >= 0) {
                float dmg = (float)wd.damage, head = headDamage(pr.type); // bolts: headshots (the knife has none)
                float y = pr.pos.y + dir.y * best;
                if (head > 0 && y >= hitBb.mx.y - (hitBb.mx.y - hitBb.mn.y) * kHeadFraction) dmg = std::max(dmg, head);
                damagePlayer(hitPlayer, pr.owner, dmg, pr.type, dir * 3.0f);
            }
            pr.active = false;
        } else {
            pr.active = false;
            explode(at, pr.owner, pr.type);
        }
    }
}

void Server::explode(const Vec3& at, int owner, int weapon) {
    const WeaponDef& wd = weaponDef(weapon);
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected || !p.alive) continue;
        AABB b = hitBox(p.move.pos, p.move.flags & MF_CROUCHED);
        Vec3 closest = vmax(b.mn, vmin(at, b.mx));
        float d = length(closest - at);
        if (d > wd.splashRadius) continue;
        // Splash is blocked by walls between the blast and the target's center.
        Vec3 c = b.center();
        Vec3 to = c - at;
        float dist = length(to);
        if (dist > 0.01f) {
            float w = map_.raycast(at, to / dist, dist);
            if (w >= 0 && w < dist - 0.1f) continue;
        }
        float frac = 1.0f - d / wd.splashRadius;
        Vec3 push = dist > 0.01f ? to / dist : Vec3(0, 1, 0);
        bool self = i == owner;
        // Self damage: 10% when the blast is at or below your feet (rocket jumps), full otherwise.
        float selfScale = at.y <= p.move.pos.y + 0.3f ? 0.1f : 1.0f;
        float dmg = wd.damage * frac * (self ? selfScale : 1.0f);
        Vec3 knock = push * (11.0f * frac * (self ? 1.3f : 1.0f) * (weapon == WPN_BUBBLE ? kBubbleKnock : 1.0f));
        if (!self) knock.y *= 2.0f; // enemies get launched: twice the vertical knockback
        damagePlayer(i, owner, dmg, weapon, knock);
    }
    // Blasts set off any sticky in range, anyone's (yours and the enemy's): chain reactions. Each one is
    // switched off before it goes, so a chain always ends.
    for (auto& pr : projectiles_) {
        if (!pr.active || pr.type != WPN_STICKY || length(pr.pos - at) > wd.splashRadius) continue;
        pr.active = false;
        explode(pr.pos, pr.owner, WPN_STICKY);
    }
}

// Line of sight from each human to each enemy, 20 times a second. Players you can't see are sent
// without a position (wallhacks get nothing); anyone within earshot, teammates, and everyone while you
// are dead or spectating are always sent. A player stays visible for 0.6 s after line of sight breaks,
// so peeking around a corner never pops in late.
void Server::updateVisibility() {
    if (time() < nextVisCheck_) return;
    nextVisCheck_ = time() + 0.05f;
    for (int v = 0; v < kMaxPlayers; v++) {
        auto& pv = players_[v];
        if (!pv.connected || pv.isBot || !pv.alive) continue;
        Vec3 eye = pv.move.pos + Vec3(0, kEyeHeight * bodyScale(), 0);
        for (int t = 0; t < kMaxPlayers; t++) {
            auto& pt = players_[t];
            if (t == v || !pt.connected || !pt.alive) continue;
            bool mate = modeIsTeamBased(ms_.mode) && pt.team == pv.team;
            Vec3 d = pt.move.pos - pv.move.pos;
            if (mate || length(d) < 20.0f) { pv.lastSeen[t] = time(); continue; }
            // Generous on purpose (a player popping out of view while their shoulder shows is worse than a
            // wallhack seeing a little): rays from both sides of your head to the target's head, chest, knees
            // and both shoulders, where they are and where they'll be in 0.25 s (and where you'll be).
            Vec3 lead = pt.move.vel * 0.25f, mine = pv.move.vel * 0.15f;
            Vec3 side = normalize(Vec3(-d.z, 0, d.x)) * 0.45f;
            bool seen = false;
            for (const Vec3& from : {eye, eye + side * 0.7f, eye - side * 0.7f, eye + mine})
                if (!seen) for (const Vec3& off : {Vec3(0, 1.75f, 0), Vec3(0, 1.0f, 0), Vec3(0, 0.25f, 0), side + Vec3(0, 1.3f, 0), Vec3() - side + Vec3(0, 1.3f, 0),
                                        lead + Vec3(0, 1.0f, 0), lead + side + Vec3(0, 1.3f, 0), lead - side + Vec3(0, 1.3f, 0)}) {
                    Vec3 to = pt.move.pos + off - from;
                    float len = length(to);
                    if (len < 0.01f || !map_.raycastAny(from, to / len, len)) { seen = true; break; }
                }
            if (seen) pv.lastSeen[t] = time();
        }
    }
}

void Server::sendSnapshots() {
    updateVisibility();
    Snapshot& s = snapScratch_;
    s.tick = tick_;
    s.timeMs = timeMs_;
    s.tickRate = (uint16_t)tickRate();
    s.snapRate = (uint16_t)(tickRate() / snapEvery());
    s.projectiles.clear();
    s.ms = ms_;
    if (editing_) s.ms.flags |= MS_EDITING | (tempEdit_ ? MS_TEMP_EDIT : 0);
    if (cfg_.ranked) s.ms.flags |= MS_RANKED;
    // a time trial counts for the race board only on the shipped Viaduct, unchanged
    if (cfg_.timeTrial && cfg_.map == "viaduct" && isOfficialMap(map_)) s.ms.flags |= MS_TIMETRIAL;
    std::vector<PlayerSnap>& all = snapPlayers_;
    all.clear();
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected) continue;
        PlayerSnap ps;
        ps.id = (uint8_t)i;
        ps.flags = PS_CONNECTED;
        if (p.alive) ps.flags |= PS_ALIVE;
        if (p.move.flags & MF_CROUCHED) ps.flags |= PS_CROUCHED;
        if (ms_.bombState == BOMB_CARRIED && ms_.bombCarrier == i) ps.flags |= PS_HASBOMB;
        if (isItMode(ms_.mode) && p.it) ps.flags |= PS_HASBOMB; // hide and seek / tag: "it"
        if (p.team == TEAM_SPECTATOR) ps.flags |= PS_SPECTATOR;
        if (p.team == TEAM_B) ps.flags |= PS_TEAM_B;
        if (p.finished) ps.flags |= PS_FINISHED;
        if (p.alive && time() - p.lastFire < 0.1f) ps.flags |= PS_FIRING;
        ps.pos = p.move.pos;
        ps.yaw = p.yaw;
        ps.pitch = p.pitch;
        ps.health = (uint8_t)clampf(std::ceil(p.health), 0, 255);
        ps.weapon = (uint8_t)p.weapon;
        ps.kills = (int16_t)p.kills;
        ps.deaths = (int16_t)p.deaths;
        ps.score = (int16_t)p.score;
        ps.dmg = (int16_t)std::min(p.dmgDealt, 32000.0f);
        ps.obj = (int16_t)std::min(p.objTime, 32000.0f);
        ps.ping = (uint16_t)p.ping;
        ps.checkpoint = (uint8_t)p.checkpoint;
        ps.hooked = p.alive && (p.move.flags & MF_HOOKED);
        ps.hookPoint = p.move.hookPoint;
        all.push_back(ps);
    }
    for (auto& pr : projectiles_) {
        if (!pr.active) continue;
        if (s.projectiles.size() >= 48) break;
        s.projectiles.push_back({pr.id, (uint8_t)pr.type, (uint8_t)pr.owner, pr.pos, pr.vel});
    }
    snapProj_ = s.projectiles;
    const std::vector<ProjectileSnap>& allProj = snapProj_;
    uint8_t buf[kMaxPacket], world[kMaxPacket];
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected || p.isBot) continue;
        if ((int)p.sent.size() != ServerPlayer::kSentRing) p.sent.assign(ServerPlayer::kSentRing, Snapshot());
        s.clientId = (uint8_t)i;
        s.ackCmdSeq = p.lastCmdSeq;
        s.ackStringCmd = p.lastStringCmd;
        s.self = p.move;
        s.selfHealth = (uint8_t)clampf(std::ceil(p.health), 0, 255);
        s.selfWeapon = (uint8_t)p.weapon;
        s.knifeAway = (uint8_t)clampf(std::ceil((p.knifeBackAt - time()) * 10), 0, 255);
        // this client's view of the players: enemies out of sight are hidden
        bool seesAll = !p.alive || p.team == TEAM_SPECTATOR;
        s.players = all;
        s.projectiles = allProj;
        for (auto& ps : s.players)
            ps.hidden = !seesAll && ps.id != i && (ps.flags & PS_ALIVE) && time() - p.lastSeen[ps.id] > 0.6f;
        // delta base: the newest snapshot the client confirmed, if we still remember it
        const Snapshot* base = nullptr;
        for (auto& o : p.sent)
            if (o.tick && o.timeMs == p.ackSnapMs && tick_ - o.tick < (uint32_t)(ServerPlayer::kSentRing * 8)) { base = &o; break; }
        s.baseTick = base ? base->tick : 0;
        Writer ww(world, sizeof world);
        writeSnapshotWorld(ww, s, base);
        while (ww.overflowed() && !s.projectiles.empty()) { // too much: drop projectiles rather than the snapshot
            s.projectiles.resize(s.projectiles.size() / 2);
            ww = Writer(world, sizeof world);
            writeSnapshotWorld(ww, s, base);
        }
        Writer w(buf, kMaxPacket);
        writeHeader(w, PKT_SNAPSHOT);
        w.u32(p.nonce);
        writeSnapshotClient(w, s);
        w.bytes(world, ww.size());
        // Reliable events fill whatever room is left.
        int room = kMaxPacket - w.size() - 1;
        // An event goes out at once, then again only after a round trip without an ack (not every snapshot).
        events_.clear();
        float resend = std::max(0.03f, p.ping * 0.001f + 0.01f);
        for (auto& e : p.events) {
            if (e.sentAt >= 0 && time() - e.sentAt < resend) continue;
            int sz = 10 + (int)e.text.size();
            if (events_.size() >= 16) break;
            if (sz > room) continue; // a big one waits for a roomier snapshot; smaller ones behind it still go
            room -= sz;
            e.sentAt = time();
            events_.push_back(e);
        }
        writeSnapshotEvents(w, events_);
        snapBytes_ += w.size(); snapCount_++;
        if (!sock_.send(p.addr, buf, w.size())) windowSendFail_++;
        // remember exactly what we sent (world part) as a future delta base
        Snapshot& keep = p.sent[p.sentSeq++ % ServerPlayer::kSentRing]; // by send count: stays a ring when the snapshot interval changes
        keep.tick = tick_; keep.timeMs = timeMs_; keep.ms = s.ms;
        keep.players = s.players; keep.projectiles = s.projectiles;
    }
}

// ---------------------------------------------------------------------------
// bots

int Server::humanCount(int team) const {
    int c = 0;
    for (auto& p : players_) c += p.connected && !p.isBot && (team < 0 || p.team == team);
    return c;
}

int Server::addBot(int team, int skill, bool balance) {
    if (playerCount() >= cfg_.maxPlayers) return -1;
    int id = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (!players_[i].connected) { id = i; break; }
    if (id < 0) return -1;
    static const char* names[] = {"ANNA", "BRUNO", "CLEO", "DMITRI", "EZRA", "FAYE", "GUS", "HANA", "IVO", "JUNE", "KAI", "LENA", "MILO", "NOOR", "OTTO", "PIA"};
    ServerPlayer& p = players_[id];
    p = ServerPlayer();
    for (auto& h : p.history) h.tick = 0xFFFFFFFFu;
    p.connected = true;
    p.joinId = nextJoinId_++;
    p.isBot = true;
    p.balanceBot = balance;
    if (!modeIsTeamBased(cfg_.mode) && team != TEAM_SPECTATOR) team = TEAM_A;   // FFA and the like: no sides
    p.team = team;
    p.lastRecv = timeNow();
    p.name = uniqueName(std::string("BOT ") + names[(id * 7 + tick_) % 16], id);
    bots_[id] = BotBrain();
    bots_[id].skill = skill;
    bots_[id].rng = 0x9E3779B9u ^ (uint32_t)(id * 2654435761u) ^ tick_;
    { GameEvent r; r.type = EV_RANKINFO; r.a = (uint8_t)id; broadcastEvent(r); }
    p.model = cfg_.ranked ? 0 : (int)(bots_[id].rng % kPlayerModels); // bots: a mix of looks (ranked: default)
    p.soundPack = (int)((bots_[id].rng >> 8) % kSoundPacks);
    GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)id; e.b = looksByte(p); e.text = p.name;
    broadcastEvent(e);
    message(p.name + " (" + botSkillName(skill) + ") joined " + (team == TEAM_B ? "team B" : modeIsTeamBased(cfg_.mode) ? "team A" : "the game"));
    if (rulesRespawnDelay(*this) >= 0) p.respawnAt = time() + 0.5f;
    return id;
}

void Server::removeBot(int id) {
    if (!players_[id].connected || !players_[id].isBot) return;
    dropPlayer(id, "bot removed", false);
}

// Team modes: keep teams even by adding intermediate bots to the side with fewer humans.
void Server::updateBalanceBots(bool matchStarting) {
    // No balance bots during warmup: they're added when a match starts (if voted / enabled).
    if (tourney_.running) return; // tourney: entrants waiting their turn are spectators, and a bot removed here would forfeit (a BYE advancing)
    bool inMatch = ms_.phase != PHASE_WARMUP || matchStarting;
    bool wanted = inMatch && cfg_.balanceBots && !cfg_.vsBots && modeIsTeamBased(cfg_.mode) && humanCount() > 0;
    int hA = humanCount(TEAM_A), hB = humanCount(TEAM_B);
    int needA = wanted ? std::max(0, hB - hA) : 0, needB = wanted ? std::max(0, hA - hB) : 0;
    int haveA = 0, haveB = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected || !p.balanceBot) continue;
        int& have = p.team == TEAM_A ? haveA : haveB;
        int need = p.team == TEAM_A ? needA : needB;
        if (have >= need) removeBot(i);
        else have++;
    }
    while (haveA < needA && addBot(TEAM_A, cfg_.balanceBotSkill, true) >= 0) haveA++;
    while (haveB < needB && addBot(TEAM_B, cfg_.balanceBotSkill, true) >= 0) haveB++;
}

void Server::callVote(int caller, int kind, int arg, const std::string& map, int arg2) {
    if (vote_.kind != VOTE_NONE) {
        GameEvent e; e.type = EV_MESSAGE; e.text = "a vote is already in progress";
        sendEvent(caller, e);
        return;
    }
    vote_ = Vote();
    vote_.kind = kind;
    vote_.arg = arg;
    vote_.map = map;
    vote_.arg2 = arg2;
    vote_.endsAt = time() + 30.0f;
    vote_.text = kind == VOTE_BOTS        ? std::string("BALANCE BOTS ") + (arg ? "ON" : "OFF")
               : kind == VOTE_BOTSKILL    ? std::string("BOT DIFFICULTY: ") + botSkillName(arg)
               : kind == VOTE_START       ? std::string("START THE MATCH")
               : kind == VOTE_MAP         ? "PLAY " + map + " - " + modeTitle(arg) + (arg == MODE_ARENA || arg == MODE_TOURNEY ? std::string(" (") + arenaVariant(vote_.arg2).title + ")" : "")
               : kind == VOTE_TMAPS       ? "TOURNEY MAPS: " + map
               : kind == VOTE_TMODES      ? "TOURNEY RULESETS: " + map
               : kind == VOTE_EDIT        ? std::string(arg ? "EDIT THE MAP TOGETHER (CHANGES LAST UNTIL THE MAP CHANGES)" : "STOP EDITING AND PLAY THE MAP")
               : kind == VOTE_LETIN       ? "LET " + players_[arg].name + " IN (TEAMS GO UNEVEN)"
               : kind == VOTE_RANKED      ? (arg < 0 ? std::string("RANKED OFF") : std::string("RANKED ON - ") + arenaVariant(arg).title + " 1V1")
                                          : std::string("FILL THE UNEVEN TEAMS WITH BOTS?");
    GameEvent e; e.type = EV_VOTE; e.a = (uint8_t)(caller < 0 ? 255 : caller); e.text = vote_.text;
    broadcastEvent(e);
    message((caller < 0 ? std::string("vote") : players_[caller].name + " called a vote") + ": " + vote_.text + " (F1 yes / F2 no)");
    if (caller >= 0) castVote(caller, true);
}

void Server::castVote(int id, bool yes) {
    if (vote_.kind == VOTE_NONE || players_[id].isBot) return;
    uint32_t bit = 1u << id;
    vote_.yes &= ~bit;
    vote_.no &= ~bit;
    (yes ? vote_.yes : vote_.no) |= bit;
    tickVote();
}

void Server::tickVote() {
    ms_.voteActive = 0;
    if (vote_.kind == VOTE_NONE) return;
    int humans = humanCount();
    int y = 0, n = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!players_[i].connected || players_[i].isBot) continue;
        y += (vote_.yes >> i) & 1;
        n += (vote_.no >> i) & 1;
    }
    bool pass = y * 2 > humans, fail = n * 2 >= humans || humans == 0;
    if (time() >= vote_.endsAt) { pass = y > n; fail = !pass; }
    if (vote_.kind == VOTE_STARTBOTS && (pass || fail)) {
        // follow-up to a passed start vote: decides balance bots, then the match starts either way
        cfg_.balanceBots = pass;
        message(std::string("bots ") + (pass ? "will fill the uneven teams" : "off for this match"));
        vote_ = Vote();
        updateBalanceBots(true);
        rulesForceStart(*this);
        return;
    }
    if (pass && vote_.kind == VOTE_START) {
        message("vote passed: starting the match");
        vote_ = Vote();
        bool uneven = modeIsTeamBased(cfg_.mode) && !cfg_.vsBots && !cfg_.ranked && humanCount(TEAM_A) != humanCount(TEAM_B);
        if (uneven) callVote(-1, VOTE_STARTBOTS, 1);
        else { updateBalanceBots(true); rulesForceStart(*this); }
        return;
    }
    if (pass) {
        if (vote_.kind == VOTE_TMAPS || vote_.kind == VOTE_TMODES) {
            std::vector<std::string> items;
            std::istringstream ls(vote_.map);
            std::string it;
            while (std::getline(ls, it, ',')) if (!it.empty()) items.push_back(it);
            if (vote_.kind == VOTE_TMAPS) cfg_.tourneyMaps = items;
            else { cfg_.tourneyVariants.clear(); for (auto& v : items) cfg_.tourneyVariants.push_back(arenaVariantFromName(v.c_str())); }
            message("vote passed: " + vote_.text);
            vote_ = Vote();
            return;
        }
        if (vote_.kind == VOTE_EDIT) {
            int on = vote_.arg;
            message("vote passed: " + vote_.text);
            vote_ = Vote();
            command(on ? "editmode on" : "editmode off");
            return;
        }
        if (vote_.kind == VOTE_LETIN) {
            int who = vote_.arg;
            message("vote passed: " + vote_.text);
            vote_ = Vote();
            auto& q = players_[who];
            if (q.connected && q.team == TEAM_SPECTATOR) {
                int a = humanCount(TEAM_A), b = humanCount(TEAM_B);
                q.letIn = true;
                q.team = a <= b ? TEAM_A : TEAM_B;
                message(q.name + (q.team == TEAM_A ? " joined team A" : " joined team B"));
                if (rulesRespawnDelay(*this) >= 0) q.respawnAt = time() + 1.0f;
            }
            return;
        }
        if (vote_.kind == VOTE_RANKED) {
            int v = vote_.arg;
            message("vote passed: " + vote_.text);
            vote_ = Vote();
            setRanked(v >= 0, v);
            return;
        }
        if (vote_.kind == VOTE_MAP) {
            std::string cmd = "map " + vote_.map + " " + modeName(vote_.arg) + " " + arenaVariant(vote_.arg2).name;
            message("vote passed: " + vote_.text);
            vote_ = Vote();
            command(cmd);
            return;
        }
        if (vote_.kind == VOTE_BOTS) cfg_.balanceBots = vote_.arg != 0;
        else {
            cfg_.balanceBotSkill = cfg_.botSkill = vote_.arg;
            for (int i = 0; i < kMaxPlayers; i++) if (players_[i].isBot) bots_[i].skill = vote_.arg;
        }
        message("vote passed: " + vote_.text);
        vote_ = Vote();
        updateBalanceBots();
        return;
    }
    if (fail) { message("vote failed: " + vote_.text); vote_ = Vote(); return; }
    ms_.voteActive = 1;
    ms_.voteYes = (uint8_t)y;
    ms_.voteNo = (uint8_t)n;
    ms_.voteVoters = (uint8_t)humans;
    ms_.voteTimeLeft = vote_.endsAt - time();
}

// Printable, max 20 chars, and not already taken by someone else (then "name (2)", ...).
std::string Server::uniqueName(std::string name, int self) const {
    for (auto& c : name) if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) name = "player";
    if (name.size() > 20) name.resize(20);
    auto taken = [&](const std::string& n) {
        for (int i = 0; i < kMaxPlayers; i++) if (i != self && players_[i].connected && players_[i].name == n) return true;
        return false;
    };
    std::string out = name;
    for (int k = 2; taken(out); k++) out = name.substr(0, 16) + " (" + std::to_string(k) + ")";
    return out;
}

// ---------------------------------------------------------------------------
// game log (off / simple / verbose / extreme)

void Server::broadcastBracket() {
    std::string t;
    for (auto& r : tourney_.rounds) {
        for (size_t i = 0; i < r.size(); i++) t += (i ? "|" : "") + std::to_string(r[i]);
        t += "\n";
    }
    t += "@" + std::to_string(tourney_.round) + "," + std::to_string(tourney_.match);
    GameEvent e; e.type = EV_BRACKET; e.text = t;
    broadcastEvent(e);
}

bool Server::switchMapKeepRules(const std::string& name) {
    if (name == cfg_.map) return true;
    Map m;
    if (!m.load(name) || !m.supportsMode(cfg_.mode)) return false;
    if (editing_ && tempEdit_) setEditing(false);
    map_ = m;
    cfg_.map = name;
    mapEdited_ = false;
    for (auto& pr : projectiles_) pr.active = false;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected) continue;
        p.alive = false;
        p.cmdQueue.clear();
        if (!p.isBot) sendAccept(sock_, p.addr, p.nonce, i, map_, cfg_.mode);
    }
    glog(LOG_SIMPLE, "map %s mode %s", name.c_str(), modeName(cfg_.mode));
    return true;
}

uint16_t Server::weaponMask() const {
    if (isItMode(ms_.mode)) return (uint16_t)(1u << WPN_KNIFE); // only "it" can use it; everyone else is unarmed
    uint16_t m = ms_.mode == MODE_ARENA || ms_.mode == MODE_TOURNEY ? arenaVariant(ms_.arenaVariant).weapons : duelModDef(duelMod()).weapons;
    uint16_t c = cfg_.custom.active() ? cfg_.custom.weapons : 0;   // a custom mode's weapons (narrowed by a duel life's, if any)
    if (c && m && (m & c)) return m & c;
    return c ? c : m;
}

AABB Server::hitBox(const Vec3& feet, bool crouched) const {
    AABB b = playerBox(feet, crouched);
    float k = duelModDef(duelMod()).size;
    if (k == 1.0f) return b;
    Vec3 c = {feet.x, feet.y, feet.z};
    return {c + (b.mn - c) * k, c + (b.mx - c) * k};
}

void Server::openGameLog() {
    if (cfg_.logLevel <= LOG_OFF || glog_) return;
    std::string path = cfg_.logFile;
    if (path.empty()) {
        std::string dir = (executableDir().empty() ? std::string(".") : executableDir()) + "/logs";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        time_t t = ::time(nullptr);
        char buf[64];
        strftime(buf, sizeof buf, "/quark-%Y%m%d-%H%M%S.log", localtime(&t));
        path = dir + buf;
    }
    glog_ = fopen(path.c_str(), "w");
    if (!glog_) { log("could not open log file " + path); return; }
    // A big buffer keeps 1000 Hz logging to a handful of writes per second, off the tick's critical path.
    setvbuf(glog_, nullptr, _IOFBF, 1 << 20);
    static const char* names[] = {"off", "simple", "verbose", "extreme"};
    log(std::string("logging (") + names[cfg_.logLevel] + ") to " + path);
    glog(LOG_SIMPLE, "quark server log, level %s, version %s", names[cfg_.logLevel], QUARK_VERSION);
}

void Server::glog(int level, const char* fmt, ...) {
    if (!glog_ || cfg_.logLevel < level) return;
    fprintf(glog_, "%u.%03u ", timeMs_ / 1000, timeMs_ % 1000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(glog_, fmt, ap);
    va_end(ap);
    fputc('\n', glog_);
}

// verbose: a gameplay sample at 125 Hz; extreme: every tick, with inputs and projectiles.
void Server::logTick() {
    if (!glog_ || cfg_.logLevel < LOG_VERBOSE) return;
    bool extreme = cfg_.logLevel >= LOG_EXTREME;
    uint32_t next = timeMs_ + 1000 / tickRate();
    if (!extreme && next / 8 == timeMs_ / 8) return; // only when this tick crosses an 8 ms boundary
    glog(LOG_VERBOSE, "tick %u phase %d round %d score %d-%d", tick_, ms_.phase, ms_.round, ms_.scoreA, ms_.scoreB);
    for (int i = 0; i < kMaxPlayers; i++) {
        const auto& p = players_[i];
        if (!p.connected || !p.alive) continue;
        const Vec3& o = p.move.pos;
        const Vec3& v = p.move.vel;
        if (extreme)
            fprintf(glog_, "  p%d pos %.3f %.3f %.3f vel %.3f %.3f %.3f yaw %.4f pitch %.4f hp %.1f wpn %d btn %04x flags %02x dash %.3f hook %.3f\n",
                    i, o.x, o.y, o.z, v.x, v.y, v.z, p.yaw, p.pitch, p.health, p.weapon, p.buttons, p.move.flags,
                    p.move.dashCooldown, p.move.hookCooldown);
        else
            fprintf(glog_, "  p%d pos %.2f %.2f %.2f vel %.2f %.2f %.2f yaw %.3f pitch %.3f hp %.0f wpn %d\n",
                    i, o.x, o.y, o.z, v.x, v.y, v.z, p.yaw, p.pitch, p.health, p.weapon);
    }
    if (!extreme) return;
    for (const auto& pr : projectiles_)
        if (pr.active)
            fprintf(glog_, "  proj %u type %d owner %d pos %.3f %.3f %.3f vel %.3f %.3f %.3f%s\n", pr.id, pr.type, pr.owner,
                    pr.pos.x, pr.pos.y, pr.pos.z, pr.vel.x, pr.vel.y, pr.vel.z, pr.stuck ? " stuck" : "");
}

// Start playing map_ (already loaded or edited) in `mode`: everyone respawns, clients get the map again.
void Server::playMap(int mode) {
    cfg_.mode = mode;
    for (auto& pr : projectiles_) pr.active = false;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected) continue;
        p.alive = false;
        p.cmdQueue.clear();
        if (p.team != TEAM_SPECTATOR) autoTeam(i);
        sendAccept(sock_, p.addr, p.nonce, i, map_, cfg_.mode);
    }
    rulesReset(*this);
    message("now playing " + cfg_.map + (mapEdited_ ? " (edited)" : "") + " - " + modeTitle(mode));
    glog(LOG_SIMPLE, "map %s mode %s", cfg_.map.c_str(), modeName(mode));
}

void Server::setEditing(bool on) {
    editing_ = on;
    if (!on) { if (tempEdit_) editDirty_ = false; return; }
    tempEdit_ = !cfg_.editMode; // voted on a normal server: edits live until the map changes, never saved
    for (int i = kMaxPlayers - 1; i >= 0; i--) if (players_[i].connected && players_[i].isBot) removeBot(i);
    for (auto& pr : projectiles_) pr.active = false;
    for (auto& p : players_) { p.alive = false; p.respawnAt = -1; p.cmdQueue.clear(); }
    vote_ = Vote();
    tourney_ = Tourney();
    nextAutosave_ = time() + 5;
    message(tempEdit_ ? "map editing - everyone can build; changes last until the map changes" : "map editing - everyone can build; the host saves");
    glog(LOG_SIMPLE, "edit mode on %s", cfg_.map.c_str());
}

bool Server::applyEdit(int from, const std::string& op) {
    if (op.size() > 220) return false;
    uint32_t before = map_.crc;
    if (!applyMapEdit(map_, op)) return false;
    map_.source = map_.serialize();
    map_.crc = crc32(map_.source.data(), map_.source.size());
    mapEdited_ = true;
    if (!editDirty_) nextAutosave_ = time() + 5;
    editDirty_ = true;
    char head[24];
    snprintf(head, sizeof head, "%08x %08x ", before, map_.crc);
    GameEvent e; e.type = EV_EDIT; e.text = head + op;
    broadcastEvent(e);
    glog(LOG_VERBOSE, "edit %d %s", from, op.c_str());
    return true;
}

bool Server::autosave() {
    if (cfg_.map.find_first_of("/\\.") != std::string::npos) return false;
    std::string text = map_.serialize();
    if (!saveMapFile(findDataPath("maps") + "/" + cfg_.map + ".qmap", text)) { log("autosave failed: maps/" + cfg_.map + ".qmap"); return false; }
    editDirty_ = false;
    nextAutosave_ = time() + 5;
    return true;
}

// Who placed where in the match that just ended, for ranked: team modes rank the two sides (winner 0,
// loser 1, both 0 on a draw); free-for-all modes rank players by score (ties share a place). Every
// player in it must have a ranked id and be human, else the match isn't rated.
std::string Server::rankedResult(int winner, std::string* whyNot) {
    if (cfg_.mode != MODE_ARENA) { *whyNot = "only 1v1 is ranked"; return ""; }
    if (!arenaVariantRanked(ms_.arenaVariant)) { *whyNot = std::string(arenaVariant(ms_.arenaVariant).title) + " isn't a ranked ruleset"; return ""; }
    bool teams = modeIsTeamBased(cfg_.mode);
    struct Entry { int slot, team, score; };
    std::vector<Entry> es;
    for (int i = 0; i < kMaxPlayers; i++) {
        const auto& p = players_[i];
        if (!p.connected || p.team == TEAM_SPECTATOR) continue;
        if (p.isBot) { *whyNot = "bots played"; return ""; }
        if (p.rankId.empty()) { *whyNot = p.name + " has no ranked id"; return ""; }
        es.push_back({i, teams ? p.team : i, p.score * 1000 + p.kills});
    }
    if (es.size() > 16) { *whyNot = "ranked counts 16 players at most"; return ""; }
    std::vector<bool> sides(kMaxPlayers, false);
    int nSides = 0;
    for (auto& e : es) if (!sides[e.team]) { sides[e.team] = true; nSides++; }
    if (es.size() < 2 || nSides < 2) { *whyNot = "not enough players"; return ""; }
    std::random_device rd;
    char id[24];
    snprintf(id, sizeof id, "%08x%08x", (unsigned)rd(), (unsigned)rd());
    std::string out = std::string(id) + " " + arenaVariant(ms_.arenaVariant).name + " "; // the ladder
    for (size_t k = 0; k < es.size(); k++) {
        int place = 0;
        if (teams) place = winner < 0 ? 0 : es[k].team == winner ? 0 : 1;
        else for (auto& o : es) place += o.score > es[k].score;
        out += (k ? "," : "") + std::to_string(es[k].slot) + ":" + std::to_string(es[k].team) + ":" + std::to_string(place);
    }
    return out;
}

std::string Server::listName() const {
    if (cfg_.custom.active()) return "[" + cfg_.custom.name + "] " + cfg_.name;
    return cfg_.ranked ? std::string("[RANKED ") + arenaVariant(ms_.arenaVariant).title + "] " + cfg_.name : cfg_.name;
}

bool Server::rankedBlocks(int id, const std::string& what) {
    if (!cfg_.ranked) return false;
    GameEvent e; e.type = EV_MESSAGE; e.text = "ranked server: no " + what + " (vote ranked off first)";
    sendEvent(id, e);
    return true;
}

// Ranked on: no bots, 1v1 on the ladder's ruleset, on this map if it's an official 1v1 map, else the
// official map built for 1v1. Off: back to the bot setting from before.
void Server::setRanked(bool on, int ladder) {
    if (!on) {
        if (!cfg_.ranked) return;
        cfg_.ranked = false;
        cfg_.balanceBots = balanceBotsUnranked_;
        message("ranked off - matches aren't rated");
        updateBalanceBots();
        return;
    }
    if (editing_) setEditing(false);
    std::string mapName = cfg_.map;
    Map m;
    if (!(m.load(mapName) && isRankedMap(m, ladder) && m.supportsMode(MODE_ARENA))) {
        // the ladder's own map first (built for just that ruleset), else any ranked map that takes it
        mapName.clear();
        for (auto& n : listMaps()) {
            Map c;
            if (!c.load(n) || !isRankedMap(c, ladder) || !c.supportsMode(MODE_ARENA)) continue;
            bool own = c.rankedLadders == (1u << ladder);
            if (mapName.empty() || own) mapName = n;
            if (own) break;
        }
        if (mapName.empty()) { message("can't go ranked: no ranked map for that ladder installed"); return; }
    }
    if (!cfg_.ranked) balanceBotsUnranked_ = cfg_.balanceBots;
    cfg_.ranked = true;
    cfg_.balanceBots = false;
    for (int i = kMaxPlayers - 1; i >= 0; i--) if (players_[i].connected && players_[i].isBot) removeBot(i);
    cfg_.arenaVariant = ladder;
    command("map " + mapName + " 1v1 " + arenaVariant(ladder).name);
    message(std::string("ranked on - ") + arenaVariant(ladder).title + " ladder");
}

bool Server::matchInProgress() const {
    if (editing_ || humanCount() == 0) return false;
    return ms_.phase == PHASE_FREEZE || ms_.phase == PHASE_LIVE || ms_.phase == PHASE_ROUND_END;
}

// An installed update leaves restartFlag behind: say so, wait for the match in progress to end, then
// ask the process to exit (the service manager starts the new version).
void Server::checkRestartFlag() {
    double now = timeNow();
    if (restartAt_ > 0) { if (now >= restartAt_) restartNow_ = true; return; }
    if (cfg_.restartFlag.empty() || now < nextFlagCheck_) return;
    nextFlagCheck_ = now + 2;
    FILE* f = fopen(cfg_.restartFlag.c_str(), "r");
    if (!f) return;
    fclose(f);
    if (matchInProgress()) {
        if (!restartAnnounced_) { message("an update is ready - the server restarts after this match"); restartAnnounced_ = true; log("update ready: waiting for the match to end"); }
        return;
    }
    std::remove(cfg_.restartFlag.c_str());
    message("restarting for an update - rejoin in a few seconds");
    log("restarting for an update");
    restartAt_ = now + 2; // let the message reach everyone
}
