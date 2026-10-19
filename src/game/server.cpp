#include "server.h"
#include "gamemodes.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {
constexpr double kTimeout = 10.0;
constexpr int kMaxQueuedCmds = 12;
constexpr uint32_t kMaxRewindMs = 500; // lag compensation window
constexpr float kMaxCmdTokensMs = 40.0f; // burst allowance after network jitter
// Tick-rate test during warmup: each window must stay clean or the rate steps down.
constexpr double kRateWindow = 3.0;
constexpr double kMaxLateFraction = 0.01;  // ticks that ran more than 2 periods late
constexpr int kMaxClientLossPct = 2;       // snapshot loss any client may report
constexpr float kKnifeRange = 2.2f;
constexpr float kRocketLife = 5.0f;

bool isLoopback(const NetAddr& a) { return (a.ip >> 24) == 127; }
}

void Server::log(const std::string& s) {
    if (log_) log_(s);
    else printf("[server] %s\n", s.c_str());
}

bool Server::start(const ServerConfig& cfg, std::string* err) {
    cfg_ = cfg;
    if (cfg_.maxPlayers < 1) cfg_.maxPlayers = 1;
    if (cfg_.maxPlayers > kMaxPlayers) cfg_.maxPlayers = kMaxPlayers;
    std::string e;
    if (!map_.load(cfg_.map, &e)) { if (err) *err = e; return false; }
    if (!map_.supportsMode(cfg_.mode)) {
        if (err) *err = "map '" + cfg_.map + "' does not support mode " + modeName(cfg_.mode);
        return false;
    }
    if (!sock_.open(cfg_.port, true)) {
        if (err) *err = "could not bind UDP port " + std::to_string(cfg_.port) + " (already in use?)";
        return false;
    }
    for (auto& p : players_) p = ServerPlayer();
    for (auto& p : projectiles_) p = Projectile();
    tick_ = 0;
    timeMs_ = 0;
    rateLevel_ = 0;
    while (rateLevel_ < kTickRateLevels - 1 && cfg_.maxTickRate > 0 && kTickRates[rateLevel_] > cfg_.maxTickRate) rateLevel_++;
    nextTick_ = timeNow();
    resetRateWindow();
    rulesReset(*this);
    running_ = true;
    log("listening on UDP " + std::to_string(port()) + " - map " + cfg_.map + ", mode " + modeName(cfg_.mode));
    if (cfg_.upnp && !cfg_.lanOnly) {
        upnp_ = std::make_unique<PortMapper>();
        upnp_->start(port());
    }
    return true;
}

void Server::stop() {
    if (!running_) return;
    for (int i = 0; i < kMaxPlayers; i++)
        if (players_[i].connected) dropPlayer(i, "server shutting down", true);
    sock_.close();
    if (upnp_) { upnp_->stop(); upnp_.reset(); }
    running_ = false;
    log("stopped");
}

void Server::run(std::atomic<bool>& quit) {
    std::string lastUpnp;
    while (!quit && running_) {
        double wait = frame();
        if (upnp_ && upnp_->done()) {
            std::string st = upnp_->status();
            if (st != lastUpnp) { log(st); lastUpnp = st; }
        }
        idle(wait);
    }
}

void Server::idle(double wait) {
    // Sleep in the socket until the next tick (wakes early for packets); the last ~0.3 ms is spun
    // by the caller's loop so 1 ms ticks stay on time.
    if (wait > 0.0003) sock_.waitUs((int)std::min((wait - 0.0003) * 1e6, 5000.0));
}

void Server::resetRateWindow() {
    rateWindowStart_ = timeNow();
    windowTicks_ = windowLate_ = windowSendFail_ = windowWorstLoss_ = 0;
}

// Warmup only: if the host can't sustain the current rate cleanly, step down a level.
void Server::checkTickRate() {
    double now = timeNow();
    if (now - rateWindowStart_ < kRateWindow) return;
    if (ms_.phase != PHASE_WARMUP || playerCount() == 0 || rateLevel_ >= kTickRateLevels - 1) { resetRateWindow(); return; }
    int worstLoss = windowWorstLoss_;
    double lateFrac = windowTicks_ ? windowLate_ / (double)windowTicks_ : 0;
    bool bad = lateFrac > kMaxLateFraction || windowSendFail_ > 0 || worstLoss > kMaxClientLossPct;
    if (bad) {
        char why[160];
        snprintf(why, sizeof why, "late %.1f%%, send failures %d, worst client loss %d%%", lateFrac * 100, windowSendFail_, worstLoss);
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
        simulate();
        tick_++;
        timeMs_ += 1000 / tickRate();
        sendSnapshots();
        nextTick_ += 1.0 / tickRate();
        now = timeNow();
    }
    for (int i = 0; i < kMaxPlayers; i++)
        if (players_[i].connected && now - players_[i].lastRecv > kTimeout) dropPlayer(i, "timed out", false);
    checkTickRate();
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
        w.str(cfg_.name); w.str(cfg_.map); w.u8((uint8_t)cfg_.mode);
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
    if (playerCount() >= cfg_.maxPlayers) { reject("server is full"); return; }
    int id = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (!players_[i].connected) { id = i; break; }
    if (id < 0) { reject("server is full"); return; }

    for (auto& c : name) if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    if (name.empty()) name = "player";
    if (name.size() > 20) name.resize(20);

    ServerPlayer& p = players_[id];
    p = ServerPlayer();
    p.connected = true;
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
        GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)i; e.text = players_[i].name;
        sendEvent(id, e);
    }
    GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)id; e.text = name;
    broadcastEvent(e);
    message(name + " joined");
    log(name + " connected from " + from.toString() + " as #" + std::to_string(id));

    float d = rulesRespawnDelay(*this);
    if (d >= 0 && p.team != TEAM_SPECTATOR) p.respawnAt = time() + 0.5f;
}

void Server::autoTeam(int id) {
    ServerPlayer& p = players_[id];
    if (!modeIsTeamBased(cfg_.mode)) { p.team = TEAM_A; return; }
    int a = 0, b = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (i == id || !players_[i].connected) continue;
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
    bool hasStr = r.u8() != 0;
    uint16_t strSeq = 0;
    std::string str;
    if (hasStr) { strSeq = r.u16(); str = r.str(); }
    if (r.bad()) return;

    p.reportedLoss = loss;
    windowWorstLoss_ = std::max(windowWorstLoss_, (int)loss);
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

    if (hasStr && strSeq == (uint16_t)(p.lastStringCmd + 1)) {
        p.lastStringCmd = strSeq;
        handleStringCmd(id, str);
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
    } else if (cmd == "team") {
        int t = rest == "a" ? TEAM_A : rest == "b" ? TEAM_B : rest == "spec" ? TEAM_SPECTATOR : -1;
        if (t < 0 || t == p.team) return;
        if (!modeIsTeamBased(cfg_.mode) && t == TEAM_B) t = TEAM_A;
        if (p.alive) killPlayer(id, -1, 255);
        p.team = t;
        message(p.name + (t == TEAM_SPECTATOR ? " is spectating" : t == TEAM_A ? " joined team A" : " joined team B"));
        if (t != TEAM_SPECTATOR && rulesRespawnDelay(*this) >= 0) p.respawnAt = time() + 1.0f;
    } else if (cmd == "name") {
        if (rest.empty()) return;
        if (rest.size() > 20) rest.resize(20);
        message(p.name + " is now " + rest);
        p.name = rest;
        GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)id; e.text = rest;
        broadcastEvent(e);
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
    if (cmd == "map" || cmd == "mode") {
        std::string newMap = cmd == "map" ? arg : cfg_.map;
        int newMode = cfg_.mode;
        if (cmd == "mode") newMode = modeFromName(arg.c_str());
        else { std::string m2; if (ss >> m2) newMode = modeFromName(m2.c_str()); }
        if (newMode < 0) { reply("unknown mode"); return; }
        Map m;
        std::string err;
        if (!m.load(newMap, &err)) { reply(err); return; }
        if (!m.supportsMode(newMode)) { reply("map does not support that mode"); return; }
        map_ = m;
        cfg_.map = newMap;
        cfg_.mode = newMode;
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
        message("now playing " + newMap + " - " + modeTitle(newMode));
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
        reply("commands: map <name> [mode], mode <name>, forcestart, restart, kick <id>, say <text>, status");
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
    for (auto& sp : map_.spawns) if (sp.team == want) cands.push_back(&sp);
    if (cands.empty()) for (auto& sp : map_.spawns) if (sp.team == SPAWN_ANY) cands.push_back(&sp);
    if (cands.empty()) for (auto& sp : map_.spawns) cands.push_back(&sp);

    Vec3 pos;
    float yaw = 0;
    if (ms_.mode == MODE_RACE && p.checkpoint > 0 && p.checkpoint <= (int)map_.checkpoints.size() && ms_.phase == PHASE_LIVE) {
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
    p.weapon = WPN_RIFLE;
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
    rulesOnDeath(*this, victim, killer);
    float d = rulesRespawnDelay(*this);
    v.respawnAt = d >= 0 ? time() + d : -1;
}

void Server::damagePlayer(int victim, int attacker, float dmg, int weapon, const Vec3& knock) {
    ServerPlayer& v = players_[victim];
    if (!v.alive) return;
    bool canHurt = rulesCanDamage(*this, attacker, victim);
    bool friendly = attacker >= 0 && attacker != victim && modeIsTeamBased(cfg_.mode) && players_[attacker].team == v.team;
    if (!friendly) {
        v.move.vel += knock;
        if (knock.y > 0.5f) v.move.flags &= ~MF_ONGROUND;
    }
    if (!canHurt || dmg <= 0) return;
    v.health -= dmg;
    v.lastDamagedTick = tick_;
    if (attacker >= 0 && attacker != victim) {
        lastProgress_ = time();
        GameEvent e; e.type = EV_HIT; e.a = (uint8_t)attacker; e.b = (uint8_t)victim; e.c = (int16_t)dmg;
        sendEvent(attacker, e);
    }
    if (v.health <= 0) killPlayer(victim, attacker, weapon);
}

void Server::simulate() {
    for (int i = 0; i < kMaxPlayers; i++) {
        ServerPlayer& p = players_[i];
        if (!p.connected) continue;
        p.cmdTokensMs = std::min(p.cmdTokensMs + 1000.0f / tickRate(), kMaxCmdTokensMs);
        drainCmds(i);
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
    tickProjectiles();
    for (int i = 0; i < kMaxPlayers; i++) {
        ServerPlayer& p = players_[i];
        if (!p.connected) continue;
        if (p.alive && p.move.pos.y < map_.bounds.mn.y - 20.0f) killPlayer(i, -1, 252);
        if (!p.alive && p.respawnAt >= 0 && time() >= p.respawnAt && p.team != TEAM_SPECTATOR) spawnPlayer(i);
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
    if (!p.alive) return;
    if (rulesFrozen(*this, id)) p.move.flags |= MF_FROZEN;
    else p.move.flags &= ~MF_FROZEN;
    pmove(p.move, cmd, map_);

    if (cmd.buttons & BTN_WEAPON1) p.weapon = WPN_RIFLE;
    if (cmd.buttons & BTN_WEAPON2) p.weapon = WPN_ROCKET;
    if (cmd.buttons & BTN_WEAPON3) p.weapon = WPN_KNIFE;
    if (cmd.buttons & BTN_WEAPON4) p.weapon = WPN_SNIPER;
    if (cmd.buttons & BTN_WEAPON5) p.weapon = WPN_LASER;
    if ((cmd.buttons & BTN_FIRE) && time() >= p.nextFire && !(p.move.flags & MF_FROZEN)) {
        // Holding fire keeps an exact cadence; after a pause the timer restarts from now.
        float refire = weaponDef(p.weapon).refire;
        p.nextFire = (time() - p.nextFire < dt() + 0.0005f) ? p.nextFire + refire : time() + refire;
        p.lastFire = time();
        fireWeapon(id, cmd);
    }
}

void Server::fireWeapon(int id, const UserCmd& cmd) {
    ServerPlayer& p = players_[id];
    const WeaponDef& wd = weaponDef(p.weapon);
    float eyeH = (p.move.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight;
    Vec3 eye = p.move.pos + Vec3(0, eyeH, 0);
    Vec3 dir = angleForward(cmd.yaw, cmd.pitch);
    bool scoped = wd.scopedSpread >= 0 && (cmd.buttons & BTN_ALTFIRE);
    float spread = scoped ? wd.scopedSpread : wd.spread;
    if (spread > 0) {
        Vec3 right = normalize(cross(dir, {0, 1, 0})), up = cross(right, dir);
        float a = randf() * 2 * PI, r = std::sqrt(randf()) * spread;
        dir = normalize(dir + right * (std::cos(a) * r) + up * (std::sin(a) * r));
    }

    if (wd.projectile) {
        for (auto& pr : projectiles_) {
            if (pr.active) continue;
            pr.active = true;
            pr.id = nextProjId_++;
            pr.type = p.weapon;
            pr.owner = id;
            pr.pos = eye + dir * 0.3f;
            pr.vel = dir * wd.projSpeed;
            pr.dieAt = time() + kRocketLife;
            return;
        }
        return;
    }
    // Hitscan with lag compensation: test against where targets were on the shooter's screen.
    uint32_t vt = cmd.viewTick; // ms on the server clock
    if (vt > timeMs_) vt = timeMs_;
    if (timeMs_ - vt > kMaxRewindMs) vt = timeMs_ - kMaxRewindMs;
    float range = p.weapon == WPN_KNIFE ? kKnifeRange : wd.range;
    float wall = map_.raycast(eye, dir, range);
    float best = wall >= 0 ? wall : range;
    int hit = -1;
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
        float t = rayAABB(eye, inv, playerBox(pos, crouched), best);
        if (t >= 0 && t < best) { best = t; hit = i; }
    }
    if (hit >= 0) damagePlayer(hit, id, (float)wd.damage, p.weapon, dir * 1.0f);
}

void Server::tickProjectiles() {
    for (auto& pr : projectiles_) {
        if (!pr.active) continue;
        if (time() >= pr.dieAt) { pr.active = false; continue; }
        Vec3 step = pr.vel * dt();
        float len = length(step);
        Vec3 dir = step / len;
        float wall = map_.raycast(pr.pos, dir, len);
        float best = wall >= 0 ? wall : len;
        bool hitSomething = wall >= 0;
        Vec3 inv = safeInv(dir);
        for (int i = 0; i < kMaxPlayers; i++) {
            auto& p = players_[i];
            if (!p.connected || !p.alive || i == pr.owner) continue;
            AABB b = playerBox(p.move.pos, p.move.flags & MF_CROUCHED);
            float t = rayAABB(pr.pos, inv, b, best);
            if (t >= 0 && t <= best) { best = t; hitSomething = true; }
        }
        if (hitSomething) {
            Vec3 at = pr.pos + dir * std::max(0.0f, best - 0.05f);
            pr.active = false;
            explode(at, pr.owner, pr.type);
        } else {
            pr.pos += step;
        }
    }
}

void Server::explode(const Vec3& at, int owner, int weapon) {
    const WeaponDef& wd = weaponDef(weapon);
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected || !p.alive) continue;
        AABB b = playerBox(p.move.pos, p.move.flags & MF_CROUCHED);
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
        float dmg = wd.damage * frac * (self ? 0.5f : 1.0f);
        Vec3 knock = push * (11.0f * frac * (self ? 1.3f : 1.0f));
        damagePlayer(i, owner, dmg, weapon, knock);
    }
}

void Server::sendSnapshots() {
    Snapshot s;
    s.tick = tick_;
    s.timeMs = timeMs_;
    s.tickRate = (uint16_t)tickRate();
    s.ms = ms_;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected) continue;
        PlayerSnap ps;
        ps.id = (uint8_t)i;
        ps.flags = PS_CONNECTED;
        if (p.alive) ps.flags |= PS_ALIVE;
        if (p.move.flags & MF_CROUCHED) ps.flags |= PS_CROUCHED;
        if (ms_.bombState == BOMB_CARRIED && ms_.bombCarrier == i) ps.flags |= PS_HASBOMB;
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
        ps.ping = (uint16_t)p.ping;
        ps.checkpoint = (uint8_t)p.checkpoint;
        ps.hooked = p.alive && (p.move.flags & MF_HOOKED);
        ps.hookPoint = p.move.hookPoint;
        s.players.push_back(ps);
    }
    for (auto& pr : projectiles_) {
        if (!pr.active) continue;
        if (s.projectiles.size() >= 40) break;
        s.projectiles.push_back({pr.id, (uint8_t)pr.type, (uint8_t)pr.owner, pr.pos, pr.vel});
    }
    // Shared part (mode, players, projectiles) is identical for everyone: encode it once.
    uint8_t shared[kMaxPacket];
    Writer ws(shared, sizeof shared);
    writeSnapshotShared(ws, s);
    if (ws.overflowed()) { // too many projectiles: drop them rather than the snapshot
        s.projectiles.clear();
        ws = Writer(shared, sizeof shared);
        writeSnapshotShared(ws, s);
    }
    uint8_t buf[kMaxPacket];
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected) continue;
        s.clientId = (uint8_t)i;
        s.ackCmdSeq = p.lastCmdSeq;
        s.ackStringCmd = p.lastStringCmd;
        s.self = p.move;
        s.selfHealth = (uint8_t)clampf(std::ceil(p.health), 0, 255);
        s.selfWeapon = (uint8_t)p.weapon;
        Writer w(buf, kMaxPacket);
        writeHeader(w, PKT_SNAPSHOT);
        w.u32(p.nonce);
        writeSnapshotClient(w, s);
        w.bytes(shared, ws.size());
        // Reliable events fill whatever room is left.
        int room = kMaxPacket - w.size() - 1;
        events_.clear();
        for (auto& e : p.events) {
            int sz = 10 + (int)e.text.size();
            if (sz > room || events_.size() >= 16) break;
            room -= sz;
            events_.push_back(e);
        }
        writeSnapshotEvents(w, events_);
        if (!sock_.send(p.addr, buf, w.size())) windowSendFail_++;
    }
}
