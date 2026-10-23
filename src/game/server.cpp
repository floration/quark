#include "server.h"
#include "../common/rng.h"
#include "ranked.h"
#include "../common/sha256.h"
#include <climits>
#include <cstring>
#include <random>
#include "../common/profile.h"
#include "gamemodes.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <cstdarg>
#include <ctime>
#include <filesystem>
#include "../common/version.h"

// EV_PLAYERINFO's b byte: model in the low 4 bits, sound pack in the high 4
static uint8_t looksByte(const ServerPlayer& p) { return (uint8_t)((p.model & 15) | ((p.soundPack & 15) << 4)); }

namespace {
constexpr double kTimeout = 10.0;
constexpr int kMaxQueuedCmds = 12;
constexpr uint32_t kMaxRewindMs = 500; // lag compensation window (hard ceiling)
constexpr uint32_t kRewindToleranceMs = 30; // over ping + interpolation (ping smoothing, jitter)
constexpr float kMaxCmdTokensMs = 40.0f; // burst allowance after network jitter
constexpr int kMaxStringCmdsPerTick = 8;   // per player (chat, votes, commands)
// Map edits (MAP-12): an op updates the map in place (a few us; a rebuild when the grid has to move or grow, ~0.3 ms
// on Midtown), so a client's whole packet of them (16) can go in one tick, within a shared time budget. And while
// any player still has half the reliable-event backlog unacknowledged, edits wait (the editor resends them): a host
// editing over loopback can't outrun a far-away player's acks until that player gets dropped.
constexpr int kMaxEditOpsPerTick = 16;     // per player (what one input packet carries)
constexpr double kEditBudgetMs = 0.5;      // per tick, all players: past it, edits wait for the next tick
constexpr float kChatBurst = 5, kChatPerSecond = 1;

// Backlog: tokens refill at exactly the command rate, so after a stall (the network's or this machine's) the commands
// that then arrive together, past the token burst, stayed queued for good (+12 ms of input latency at 1000 Hz, +96 ms at 125 Hz). A queue that stays deeper than
// kDeepQueue for kDeepTicks ticks runs one extra command a tick from a small catch-up allowance, which refills at 1%
// of the tick rate: enough for any number of hitches, too little for a speedhack to gain anything.
constexpr int kDeepQueue = 2;
constexpr int kDeepTicks = 32;
constexpr float kCatchUpMax = (float)kMaxQueuedCmds;
constexpr float kCatchUpRefill = 0.01f;   // commands per tick
// Tick-rate test during warmup: each window must stay clean or the rate steps down.
constexpr double kRateWindow = 3.0;
constexpr double kMaxLateFraction = 0.01;  // ticks that ran more than 2 periods late
constexpr int kMaxClientLossPct = 2;       // snapshot loss any client may report
const float kKnifeRange = weaponDef(WPN_KNIFE).range; // the knife's reach (also what clients draw)
constexpr float kRocketLife = 5.0f;

bool isLoopback(const NetAddr& a) { return (a.ip >> 24) == 127; }
constexpr double kProbeTimeout = 3.0;     // no PKT_PROBE_REPLY on the game port by then: not reachable
constexpr double kProbeRecheck = 300.0;   // re-check every 5 minutes (and on the menu button)
constexpr double kCookieBucket = 10.0;     // connect cookies are good for 10-20 s
constexpr float kMapReqPerSec = 400.0f;    // PKT_MAP_REQUEST chunks per second per player (a download asks ~128/s)
constexpr float kMapReqBurst = 64.0f;
constexpr size_t kMaxVoteListItems = 8;    // callvote tmaps / tmodes
constexpr size_t kMaxMapInfo = 1024;       // map facts cached (only maps whose file exists; past this it starts over)
constexpr size_t kMaxUnackedEvents = 256;
}

bool g_serverRateCheck = true;

void Server::log(const std::string& s) {
    if (log_) log_(s);
    else printf("[server] %s\n", s.c_str());
}

// Stamps the file Map::load reads (your maps folder first, then the shipped one), so an edited or new copy of a
// map is re-read, and switching between the two counts as a change. -1: no such map file.
int64_t Server::mapStamp(const std::string& name) {
    std::error_code ec;
    std::string path = mapFilePath(name);
    if (path.empty()) return -1;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) return -1;
    auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return -1;
    return ((int64_t)size * 1000003 ^ (int64_t)t.time_since_epoch().count() ^ (int64_t)std::hash<std::string>()(path)) & INT64_MAX;
}

Server::MapInfo Server::readMapInfo(const std::string& name, int64_t stamp) {
    MapInfo mi;
    mi.stamp = stamp;
    Map m;
    if (!validMapName(name) || !m.load(name)) return mi;
    mi.ok = true;
    for (int mode = 0; mode < MODE_COUNT; mode++) if (m.supportsMode(mode)) mi.supports |= 1u << mode;
    if (isOfficialMap(m)) mi.ranked = m.rankedLadders;
    return mi;
}

Server::MapInfo Server::mapInfo(const std::string& name) {
    if (!validMapName(name)) return MapInfo();
    int64_t stamp = mapStamp(name);
    if (stamp < 0) {   // no such file: nothing to cache (a vote for any made-up name must not grow the cache)
        std::lock_guard<std::mutex> lk(mapInfoMu_);
        mapInfo_.erase(name);
        return MapInfo();
    }
    {
        std::lock_guard<std::mutex> lk(mapInfoMu_);
        auto it = mapInfo_.find(name);
        if (it != mapInfo_.end() && it->second.stamp == stamp) return it->second;
    }
    MapInfo mi = readMapInfo(name, stamp);   // new or changed since: read it now (once)
    mapInfoLoads_++;
    std::lock_guard<std::mutex> lk(mapInfoMu_);
    if (mapInfo_.size() >= kMaxMapInfo && !mapInfo_.count(name)) mapInfo_.clear();   // (bounded: re-read as needed)
    mapInfo_[name] = mi;
    return mi;
}

void Server::stopMapInfo() {
    mapInfoQuit_ = true;
    if (mapInfoThread_.joinable()) mapInfoThread_.join();
    mapInfoQuit_ = false;
}

bool Server::start(const ServerConfig& cfg, std::string* err) {
    stopMapInfo();
    mapInfoThread_ = std::thread([this, names = listMaps()] {   // the map facts votes need, off the tick
        for (auto& n : names) {
            if (mapInfoQuit_) return;
            {
                std::lock_guard<std::mutex> lk(mapInfoMu_);
                if (mapInfo_.count(n)) continue;
            }
            int64_t stamp = mapStamp(n);
            if (stamp < 0) continue;   // (gone since it was listed)
            MapInfo mi = readMapInfo(n, stamp);
            std::lock_guard<std::mutex> lk(mapInfoMu_);
            if (mapInfo_.size() >= kMaxMapInfo) return;
            mapInfo_.emplace(n, mi);
        }
    });
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
    setActiveRules(cfg_.custom);   // (for the calling thread; frame() sets them on whichever thread runs the server)
    if (cfg_.ranked) { // ranked = 1v1 on one of the ladder rulesets
        if (!arenaVariantRanked(cfg_.arenaVariant)) cfg_.arenaVariant = 0;
        cfg_.mode = rankedMode(cfg_.arenaVariant);
    }
    {   // the connect-challenge secret
        std::random_device rd;
        for (int i = 0; i < 16; i += 4) { uint32_t v = rd(); memcpy(cookieKey_ + i, &v, 4); }
    }
    mapList_ = listMaps();
    std::string e;
    if (!map_.load(cfg_.map, &e)) { if (err) *err = e; return false; }
    nav_.buildAsync(map_);   // bot navigation, built off the tick thread
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
    rateTopLevel_ = rateLevel_;
    rateUpWait_ = 60; rateUpAt_ = 0;
    rateDownPending_ = false; rateRaisedAt_ = -1e9;
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
    ownAddrs_.clear();
    for (auto& s : cfg_.ownAddrs) { NetAddr a; if (NetAddr::parse(s, port(), a)) ownAddrs_.push_back(a); else log("own_addr " + s + ": not an address"); }
    localIps_ = localIpv4Addresses(); localIpsAt_ = timeNow();
    const char* pubOff = getenv("QUARK_PUBLIC_IP");
    if (!cfg_.lanOnly && !(pubOff && strcmp(pubOff, "off") == 0)) {   // our public IPv4 (behind a NAT), for ownAddress
        auto look = std::make_shared<PublicIpLookup>();
        publicIp_ = look;
        std::thread([look] {   // (detached: a slow lookup never holds up stop(); retried with backoff until it works)
            for (int wait = 5; !look->quit; wait = std::min(wait * 2, 600)) {
                NetAddr a;
                std::string ip = publicIpv4();
                if (!ip.empty() && NetAddr::parse(ip, 0, a)) { look->ip = a.ip; return; }
                for (int s = 0; s < wait * 10 && !look->quit; s++) sleepMs(100);
            }
        }).detach();
    }
    if (cfg_.publicList && !cfg_.lanOnly) {
        master_ = std::make_unique<MasterAnnouncer>();
        master_->start(port());
    }
    glog(LOG_SIMPLE, "map %s mode %s", cfg_.map.c_str(), modeName(cfg_.mode));
    if (cfg_.upnp && !cfg_.lanOnly) {
        upnp_ = std::make_unique<PortMapper>();
        upnp_->start(port());
    }
    if (cfg_.probe && !cfg_.lanOnly && probeSock_.open(0)) {
        reach_ = REACH_CHECKING;
        { std::lock_guard<std::mutex> l(reachMu_); reachDetail_ = "finding a server to test with"; }
        probeSentAt_ = 0; probeNextAt_ = timeNow() + 0.5; probeStartedAt_ = timeNow(); upnpWasDone_ = false;
        reachPicked_ = false;
        reachThread_ = std::thread([this] { pickProber(); });
    }
    return true;
}

void Server::stop() {
    stopMapInfo();
    if (!running_) return;
    if (editDirty_ && !tempEdit_) autosave();
    for (int i = 0; i < kMaxPlayers; i++)
        if (players_[i].connected) dropPlayer(i, "server shutting down", true, true);
    seedRatings_.reset();
    sock_.close();
    if (reachThread_.joinable()) reachThread_.join();
    probeSock_.close();
    reach_ = REACH_OFF;
    if (upnp_) { upnp_->stop(); upnp_.reset(); }
    if (master_) { master_->stop(); master_.reset(); }
    if (publicIp_) { publicIp_->quit = true; publicIp_.reset(); }
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

// The tick rate changes only in warmup or between rounds (gamedefs.h; REVIEW GAME-11): if the host can't sustain the
// current rate cleanly, it steps down a level then (mid-round only if it truly can't keep up: busy over 90%); now and
// then, at a break, it tries the next rate up. A step up that doesn't hold is undone at once, in any phase (REVIEW
// 6.1): else a raise made in a break could run late ticks for the whole next round.
void Server::stepRateDown(const char* why) {
    rateLevel_++;
    rateUpAt_ = timeNow() + rateUpWait_;
    rateUpWait_ = std::min(rateUpWait_ * 2, 900.0);
    rateDownPending_ = false;
    rateRaisedAt_ = -1e9;   // (undone: the next step down waits for a break again)
    message("server tick rate lowered to " + std::to_string(tickRate()) + " Hz");
    log("tick rate -> " + std::to_string(tickRate()) + " Hz (" + why + ")");
}

void Server::checkTickRate() {
    double now = timeNow();
    const bool between = ms_.phase == PHASE_WARMUP || ms_.phase == PHASE_ROUND_END || ms_.phase == PHASE_MATCH_END;
    if (rateDownPending_ && between && rateLevel_ < kTickRateLevels - 1) { stepRateDown("fell behind during the round"); resetRateWindow(); return; }
    if (now - rateWindowStart_ < kRateWindow) return;
    if (!g_serverRateCheck) { resetRateWindow(); return; }
    if (editing_ || playerCount() == 0) { resetRateWindow(); return; }
    int worstLoss = windowWorstLoss_;
    double lateFrac = windowTicks_ ? windowLate_ / (double)windowTicks_ : 0;
    double busy = windowBusy_ / std::max(1e-6, now - rateWindowStart_); // share of the time spent simulating + sending
    // Late ticks alone don't mean the server can't keep up: on shared / burstable VMs the OS wakes us late
    // while the CPU is nearly idle, and the catch-up loop still runs every tick. Step down only when the
    // work itself is heavy (or late ticks come with real load), or packets / clients are being lost.
    bool bad = busy > 0.5 || (lateFrac > kMaxLateFraction && busy > 0.2) || windowSendFail_ > 0 || worstLoss > kMaxClientLossPct;
    // Below the top rate, now and then try the next one up: a clean, lightly loaded window, and the wait since the
    // last step down is over. If it steps back down, the wait doubles (up to 15 min), so a host that can't hold it
    // isn't bounced up and down. (Between rounds only, like stepping down.)
    bool clean = !bad && busy < 0.2 && lateFrac < kMaxLateFraction * 0.5 && worstLoss == 0;
    if (rateLevel_ > rateTopLevel_ && clean && now >= rateUpAt_ && between) {
        rateLevel_--;
        rateRaisedAt_ = now;
        message("server tick rate raised to " + std::to_string(tickRate()) + " Hz");
        log("tick rate -> " + std::to_string(tickRate()) + " Hz (trying the next rate up: busy " + std::to_string((int)(busy * 100)) + "%)");
        resetRateWindow();
        return;
    }
    if (bad && rateLevel_ < kTickRateLevels - 1) {
        char why[200];
        snprintf(why, sizeof why, "busy %.0f%%, late %.1f%%, send failures %d, worst client loss %d%%", busy * 100, lateFrac * 100, windowSendFail_, worstLoss);
        bool undo = now - rateRaisedAt_ < 3 * kRateWindow;   // our own recent step up didn't hold: back down now
        if (between || busy > 0.9 || undo) stepRateDown(why);   // (else mid-round only if it can't keep up at all)
        else if (!rateDownPending_) { rateDownPending_ = true; log(std::string("tick rate: stepping down at the next break (") + why + ")"); }
    }
    resetRateWindow();
}

double Server::frame() {
    if (activeRules().name != cfg_.custom.name) setActiveRules(cfg_.custom);   // rules are per thread: this thread simulates by this server's
    uint8_t buf[2048];
    NetAddr from;
    int n;
    while ((n = sock_.recv(from, buf, sizeof buf)) > 0) handlePacket(from, buf, n);
    if (probeSock_.isOpen()) tickReachability();

    double now = timeNow();
    double period = 1.0 / tickRate();
    if (now - nextTick_ > period * 20) { // fell far behind (debugger, hitch): skip ahead instead of spiralling
        windowLate_ += 20;
        nextTick_ = now;
    }
    int caught = 0;
    while (now >= nextTick_) {
        if (++caught > 20) break; // 20 ticks per frame at most, then back to the network; the rest of the backlog
                                  // next frame (time stays continuous; a backlog past 20 periods is skipped above).
                                  // Catching up without a limit let a slow machine spin here forever.
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
    for (int i = 0; i < kMaxPlayers; i++) {
        ServerPlayer& p = players_[i];
        if (!p.connected || p.isBot) continue;
        if (p.dropPending) dropPlayer(i, "too many unacknowledged reliable messages (connection too lossy)", true);
        else if (p.awayUntil > 0) { if (now >= p.awayUntil) dropPlayer(i, p.awayReason, false); } // the grace is over: gone
        else if (now - p.lastRecv > kTimeout) holdPlayer(i, "timed out");
    }
    if (rankedLive() && !rankedTicketSent_ && time() >= rankedSigDeadline_) maybeSendRankedTicket(true);
    if (!rankedMatch_.empty() && ms_.phase == PHASE_WARMUP) rankedMatch_.clear(); // (a match that fell back to warmup: the next one gets its own ticket)
    // Ranking server replies (tickets, race runs, results): here, not in run(), which dedicated servers don't use
    // (src/server/main.cpp drives frame() / idle() itself: REVIEW 2.3).
    if (master_ && master_->hasReplies()) for (auto& r : master_->takeReplies()) handleMasterReply(r.first, r.second);
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
        if (len < kInfoRequestBytes) return; // unpadded: a reply bigger than the request would make us a reflector
        uint8_t out[kInfoRequestBytes];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_INFO_REPLY);
        w.str(listName().substr(0, kInfoNameMax)); w.str(cfg_.map.substr(0, kInfoMapMax)); w.u8((uint8_t)cfg_.mode);
        w.u8((uint8_t)playerCount()); w.u8((uint8_t)cfg_.maxPlayers); w.u16(port());
        sock_.send(from, out, w.size());
        return;
    }
    if (type == PKT_PROBE_REQUEST) { handleProbeRequest(from, r, len); return; }
    if (type == PKT_PROBE_REPLY) {   // (to our game socket: the port is reachable from wherever the prober is)
        uint32_t nonce = r.u32();
        if (!r.bad() && probeSentAt_ > 0 && nonce == probeNonce_) {
            probeSentAt_ = 0; probeNextAt_ = timeNow() + kProbeRecheck;
            setReach(REACH_OK, from.toString());
        }
        return;
    }
    if (type == PKT_CONNECT) { handleConnect(from, r); return; }
    int id = findPlayer(from);
    if (id < 0) return;
    if (r.u32() != players_[id].nonce) return;
    players_[id].lastRecv = timeNow();
    if (type == PKT_INPUT) handleInput(id, r);
    else if (type == PKT_DISCONNECT) holdPlayer(id, "disconnected");   // (a crash or a mis-click: 10 s to come back)
    else if (type == PKT_MAP_REQUEST) { // client lacks this map (or has a different copy): send it a piece
        uint32_t crc = r.u32(), off = r.u32();
        (void)crc; // always serve the current map: in an edit session it may have changed since the client asked
        const std::string& text = map_.sourceText();   // (after edits: serialized here, once per change)
        if (r.bad() || off >= text.size()) return;
        ServerPlayer& mp = players_[id];   // rate limit (token bucket)
        double now = timeNow();
        mp.mapTokens = std::min(kMapReqBurst, mp.mapTokens + (float)(now - mp.mapTokensAt) * kMapReqPerSec);
        mp.mapTokensAt = now;
        if (mp.mapTokens < 1.0f) return;
        mp.mapTokens -= 1.0f;
        uint8_t out[kMaxPacket];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_MAP_DATA);
        uint16_t n = (uint16_t)std::min<size_t>(kMapChunk, text.size() - off);
        w.u32(players_[id].nonce); w.u32(map_.crc); w.u32(map_.sourceTextCrc()); w.u32((uint32_t)text.size()); w.u32(off); w.u16(n);
        w.bytes((const uint8_t*)text.data() + off, n);
        sock_.send(from, out, w.size());
    }
}

// ---- reachability probe ----
// Prober side: one 9 byte reply per (padded, 32 byte) request, only ever to the request's own source IP, at the
// port it names. Nothing is allocated; a fixed table rate-limits each source IP and a global cap bounds the total.
void Server::handleProbeRequest(const NetAddr& from, Reader& r, int len) {
    if (len < kProbeRequestBytes) return;          // unpadded: the reply must never be bigger than the request
    uint32_t nonce = r.u32();
    uint16_t askPort = r.u16();
    if (r.bad() || askPort == 0 || from.ip == 0) return;
    double now = timeNow();
    if (now - probeWindowAt_ >= 1.0) { probeWindowAt_ = now; probeWindowCount_ = 0; }
    if (++probeWindowCount_ > 50) return;          // global: at most 50 probe replies a second
    ProbeSlot& sl = probeSlots_[(from.ip * 2654435761u) >> 26];
    if (sl.ip == from.ip && now - sl.at < 0.5) return; // per source IP: one every half second (allows the client's retry)
    sl.ip = from.ip; sl.at = now;
    uint8_t out[16];
    Writer w(out, sizeof out);
    writeHeader(w, PKT_PROBE_REPLY);
    w.u32(nonce);
    sock_.send(NetAddr{from.ip, askPort}, out, w.size()); // the source's own IP, never one named in the packet
}

void Server::setReach(Reach rv, const std::string& detail) {
    { std::lock_guard<std::mutex> l(reachMu_); reachDetail_ = detail; }
    if (reach_.exchange(rv) != rv) log("reachability: " + reachStatus());
}

std::string Server::reachStatus() {
    std::string d;
    { std::lock_guard<std::mutex> l(reachMu_); d = reachDetail_; }
    std::string p = std::to_string(cfg_.probeAskPort ? cfg_.probeAskPort : port());
    switch (reachability()) {
    case REACH_CHECKING: return "checking if UDP " + p + " is reachable from the internet... (" + d + ")";
    case REACH_OK: return "UDP " + p + " is reachable from the internet - friends can join";
    case REACH_BLOCKED: return "UDP " + p + " is not reachable from the internet - forward UDP " + p + " on your router to this PC";
    case REACH_UNKNOWN: return "couldn't check reachability: " + d;
    default: return "";
    }
}

// Background thread: choose the server that will probe us (blocking HTTP for the list and our public IP).
void Server::pickProber() {
    NetAddr a;
    std::string over = cfg_.prober;
    if (over.empty()) if (const char* e = getenv("QUARK_PROBER")) over = e;
    if (!over.empty()) {
        if (!NetAddr::parse(over, kDefaultPort, a)) a = NetAddr{};
    } else {
        std::vector<std::string> list;
        fetchServerList(list);
        NetAddr own;
        std::string ip = publicIpv4();
        if (!ip.empty()) NetAddr::parse(ip, 0, own);
        std::vector<NetAddr> others, same;
        for (auto& s : list) {
            NetAddr c;
            if (!NetAddr::parse(s, kDefaultPort, c)) continue;
            if (own.ip && c.ip == own.ip) { if (c.port != port()) same.push_back(c); } // (our own IP: hairpin NAT may lie)
            else others.push_back(c);
        }
        std::vector<NetAddr>& pool = others.empty() ? same : others;
        if (!pool.empty()) a = pool[std::random_device{}() % pool.size()];
    }
    { std::lock_guard<std::mutex> l(reachMu_); reachProber_ = a; }
    reachPicked_ = true;
}

void Server::tickReachability() {
    uint8_t buf[64]; NetAddr from;
    while (probeSock_.recv(from, buf, sizeof buf) > 0) {}   // (nothing is expected here)
    if (!reachPicked_) return;
    double now = timeNow();
    NetAddr prober;
    { std::lock_guard<std::mutex> l(reachMu_); prober = reachProber_; }
    bool again = reachAgain_.exchange(false);
    if (prober.ip == 0) {
        setReach(REACH_UNKNOWN, "no public quark server to test with (set QUARK_PROBER=host:port)");
        if (again) { // look again
            if (reachThread_.joinable()) reachThread_.join();
            reachPicked_ = false; reach_ = REACH_CHECKING;
            { std::lock_guard<std::mutex> l(reachMu_); reachDetail_ = "finding a server to test with"; }
            reachThread_ = std::thread([this] { pickProber(); });
        }
        return;
    }
    // UPnP finishing (or failing) changes the answer: check again then
    bool upDone = upnp_ && upnp_->done();
    if (upDone && !upnpWasDone_) { upnpWasDone_ = true; if (probeSentAt_ == 0) probeNextAt_ = std::min(probeNextAt_, now); }
    if (probeSentAt_ > 0) {
        if (probeTries_ < 2 && now - probeSentAt_ >= 1.0 * probeTries_) sendProbe(prober);  // one retry (packet loss)
        else if (now - probeSentAt_ >= kProbeTimeout) {
            probeSentAt_ = 0; probeNextAt_ = now + kProbeRecheck;
            setReach(REACH_BLOCKED, prober.toString());
        }
        return;
    }
    if (again) probeNextAt_ = now;
    if (now < probeNextAt_) return;
    if (upnp_ && !upDone && now - probeStartedAt_ < 10) return;   // give the router a moment to map the port first
    { std::lock_guard<std::mutex> l(reachMu_); reachDetail_ = "asking " + prober.toString(); }
    if (reach_ == REACH_UNKNOWN) reach_ = REACH_CHECKING;
    probeNonce_ = (uint32_t)std::random_device{}() | 1u;
    probeTries_ = 0;
    probeSentAt_ = now;
    sendProbe(prober);
}

void Server::sendProbe(const NetAddr& prober) {
    uint8_t out[kProbeRequestBytes] = {};
    Writer w(out, sizeof out);
    writeHeader(w, PKT_PROBE_REQUEST);
    w.u32(probeNonce_); w.u16(cfg_.probeAskPort ? cfg_.probeAskPort : port());
    probeSock_.send(prober, out, sizeof out);   // from the separate socket: the reply must come in unsolicited
    probeTries_++;
}

static void sendAccept(UdpSocket& sock, const ServerPlayer& p, int id, const Map& map, int mode) {
    uint8_t out[512];
    Writer w(out, sizeof out);
    writeHeader(w, PKT_ACCEPT);
    w.u32(p.nonce); w.u8((uint8_t)id); w.str(map.name); w.u32(map.crc); w.u8((uint8_t)mode);
    w.u32((uint32_t)p.reconnectToken); w.u32((uint32_t)(p.reconnectToken >> 32));
    sock.send(p.addr, out, w.size());
}

// Stateless connect challenge (like Quake 3's getchallenge): a keyed hash of the sender's address, its
// nonce and a time bucket. A spoofed CONNECT only ever gets this 13 byte reply, sent to the spoofed address;
// nothing is allocated and nothing bigger goes out until the sender echoes the cookie it received.
uint32_t Server::challengeCookie(const NetAddr& a, uint32_t nonce, int64_t bucket) const {
    char msg[40];
    memcpy(msg, cookieKey_, 16);
    memcpy(msg + 16, &a.ip, 4); memcpy(msg + 20, &a.port, 2); memcpy(msg + 22, &nonce, 4); memcpy(msg + 26, &bucket, 8);
    memcpy(msg + 34, cookieKey_, 6);
    uint32_t c = (uint32_t)strtoul(sha256Hex(std::string(msg, sizeof msg)).substr(0, 8).c_str(), nullptr, 16);
    return c ? c : 1;
}

// Admin is the in-process host only (its client's session, over loopback): no passwords, no IP rules (NET-8).
bool Server::isAdmin(int id) const {
    const ServerPlayer& p = players_[id];
    uint32_t host = hostNonce_.load(std::memory_order_relaxed);
    return host != 0 && p.nonce == host && p.awayUntil <= 0 && isLoopback(p.addr);
}

// Is the address a client dialled (and signed, REVIEW 2.2) this server? A relaying server could only make the player sign
// its own address, so this is what keeps a player's ranked id from being used through someone else's server. Ours: an
// --own-addr the host configured; else our port and loopback (only from this machine), one of this machine's interface
// addresses (LAN, or a public address on the interface), or our public IPv4 behind a NAT (looked up at start, or
// the router's, from UPnP).
bool Server::ownAddress(const NetAddr& d, const NetAddr& from) {
    if (d.ip == 0) return false;
    for (auto& a : ownAddrs_) if (a == d) return true;
    if (d.port != port()) return false;
    if (isLoopback(d)) return isLoopback(from);
    if (publicIp_ && publicIp_->ip.load(std::memory_order_relaxed) == d.ip) return true;
    auto local = [&] { for (uint32_t ip : localIps_) if (ip == d.ip) return true; return false; };
    if (local()) return true;
    double now = timeNow();
    if (now - localIpsAt_ >= 10) { localIps_ = localIpv4Addresses(); localIpsAt_ = now; if (local()) return true; }   // (a new interface / DHCP lease)
    if (upnp_ && upnp_->ok()) { NetAddr ext; if (NetAddr::parse(upnp_->externalIp(), 0, ext) && ext.ip == d.ip) return true; }
    return false;
}

void Server::handleConnect(const NetAddr& from, Reader& r) {
    uint16_t version = r.u16();
    uint32_t nonce = r.u32();
    if (r.bad()) return;
    if (version != kProtocolVersion) {   // (old clients: no cookie field; a short reject, no state)
        uint8_t out[64];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_REJECT);
        w.str("version mismatch (server " + std::to_string(kProtocolVersion) + ")");
        sock_.send(from, out, w.size());
        return;
    }
    uint32_t cookie = r.u32();
    std::string name = r.str();
    if (r.bad()) return;
    int64_t bucket = (int64_t)(timeNow() / kCookieBucket);
    if (cookie == 0 || (cookie != challengeCookie(from, nonce, bucket) && cookie != challengeCookie(from, nonce, bucket - 1))) {
        uint8_t out[16];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_CHALLENGE);
        w.u32(nonce); w.u32(challengeCookie(from, nonce, bucket));
        sock_.send(from, out, w.size());
        return;
    }
    // From here on the sender has proven it receives at `from`.
    auto reject = [&](const std::string& why) {
        uint8_t out[300];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_REJECT);
        w.str(why);
        sock_.send(from, out, w.size());
    };
    // Optional (every client from protocol 29 sends them): the reconnect token it was given here before, and its
    // ranked id's public key with a signature of this connect challenge (proof that it holds the key: REVIEW NET-12).
    uint64_t token = 0;
    std::string rpub, rsig;
    NetAddr dialled;   // the address the client dialled: what it signed (REVIEW 2.2)
    if (r.remaining() > 0) {
        uint32_t lo = r.u32(), hi = r.u32();
        rpub = r.str(); rsig = r.str();
        dialled.ip = r.u32(); dialled.port = r.u16();
        if (r.bad()) { rpub.clear(); dialled = NetAddr{}; }
        else token = (uint64_t)hi << 32 | lo;
    }
    int existing = findPlayer(from);
    if (existing >= 0 && players_[existing].nonce == nonce) { sendAccept(sock_, players_[existing], existing, map_, cfg_.mode); return; } // (a resend)
    std::string rankId;
    auto hexLen = [](const std::string& h, size_t n) { return h.size() == n && h.find_first_not_of("0123456789abcdef") == std::string::npos; };
    if (hexLen(rpub, 64) && hexLen(rsig, 128)) {   // (anything else: no ranked id, and no signature check spent on it)
        double now = timeNow();
        verifyTokens_ = std::min(50.0f, verifyTokens_ + (float)(now - verifyAt_) * 50.0f);
        verifyAt_ = now;
        if (verifyTokens_ < 1) return;   // a flood of signed connects: this one waits for the client's resend
        verifyTokens_ -= 1;
        if (!ownAddress(dialled, from))   // signed for another server: relayed through the one the player joined
            log("connect from " + from.toString() + ": ranked key proof is for " + dialled.toString() + ", not this server (joins without a ranked id; " +
                "if players reach it there, start the server with --own-addr " + dialled.toString() + ")");
        else if (rankVerify(rpub, rankConnectMessage(cookie, nonce, dialled.toString()), rsig)) rankId = rankIdFromPublic(rpub);
        else log("connect from " + from.toString() + ": ranked key proof failed (joins without a ranked id)");
    }
    // Back within the reconnect grace (or a new session of the same player taking over its old one): same slot,
    // team, score and state. Matched by the proven ranked id, else by the token.
    for (int i = 0; i < kMaxPlayers; i++) {
        const ServerPlayer& o = players_[i];
        if (!o.connected || o.isBot) continue;
        if ((!rankId.empty() && o.rankId == rankId) || (token != 0 && o.reconnectToken == token)) {
            if (existing >= 0 && existing != i) dropPlayer(existing, "reconnecting", false);
            restorePlayer(i, from, nonce, rankId.empty() ? std::string() : rpub, dialled);
            return;
        }
    }
    if (existing >= 0) dropPlayer(existing, "reconnecting", false); // (a new session that passed the cookie check: it owns the address)
    if (playerCount() >= cfg_.maxPlayers) {
        for (int i = kMaxPlayers - 1; i >= 0; i--)
            if (players_[i].connected && players_[i].isBot) { removeBot(i); break; }
    }
    if (playerCount() >= cfg_.maxPlayers) { reject("server is full"); return; }
    int id = -1;
    for (int i = 0; i < kMaxPlayers && id < 0; i++) {
        if (players_[i].connected) continue;
        bool reserved = false;   // a ranked leaver's slot stays theirs until the match's result is out
        if (rankedLive()) for (auto& l : rankedLeft_) reserved |= l.slot == i;
        if (!reserved) id = i;
    }
    if (id < 0) { reject("server is full"); return; }

    name = uniqueName(name, -1);

    ServerPlayer& p = players_[id];
    p = ServerPlayer();
    p.connected = true;
    p.joinId = nextJoinId_++;
    vote_.yes &= ~(1u << id); vote_.no &= ~(1u << id); // a newcomer in this slot hasn't voted
    p.addr = from;
    p.nonce = nonce;
    p.name = name;
    p.rankId = rankId;
    if (!rankId.empty()) { p.rankPub = rpub; p.dialled = dialled; }
    {
        std::random_device rd;
        do p.reconnectToken = (uint64_t)rd() << 32 ^ rd(); while (p.reconnectToken == 0);
    }
    p.lastRecv = timeNow();
    for (auto& h : p.history) h.tick = 0xFFFFFFFFu;
    autoTeam(id);
    // A live ranked match takes no one new: they watch it. (Else a leaver who comes back would be counted twice, and a
    // dodger's new session without a key would leave the match unrated: "has no ranked id".)
    if (rankedLive()) p.team = TEAM_SPECTATOR;
    sendAccept(sock_, p, id, map_, cfg_.mode);
    sendWelcome(id);
    GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)id; e.b = looksByte(p); e.text = name;
    broadcastEvent(e);
    GameEvent rk; rk.type = EV_RANKINFO; rk.a = (uint8_t)id; rk.text = rankId; // (empty: the slot's previous owner's id is gone)
    broadcastEvent(rk);
    message(name + " joined");
    sendRules(id);
    log(name + " connected from " + from.toString() + " as #" + std::to_string(id) + (rankId.empty() ? "" : " (ranked id " + rankId + ")"));
    if (cfg_.mode == MODE_TOURNEY && !rankId.empty() && !tourney_.running) prefetchSeedRatings();

    float d = rulesRespawnDelay(*this);
    if (d >= 0 && p.team != TEAM_SPECTATOR) p.respawnAt = time() + 0.5f;
}

// Everyone's name, looks and ranked id, to one (re)joining client.
void Server::sendWelcome(int id) {
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!players_[i].connected || i == id) continue;
        GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)i; e.b = looksByte(players_[i]); e.text = players_[i].name;
        sendEvent(id, e);
        if (!players_[i].rankId.empty()) { GameEvent r; r.type = EV_RANKINFO; r.a = (uint8_t)i; r.text = players_[i].rankId; sendEvent(id, r); }
    }
}

// The connection dropped (timed out, or the client said goodbye): the player stays in the game, standing where they
// were, for kReconnectGrace; a reconnect gets it all back (restorePlayer), else dropPlayer when the grace is over.
void Server::holdPlayer(int id, const std::string& reason) {
    ServerPlayer& p = players_[id];
    if (!p.connected || p.isBot || p.awayUntil > 0) return;
    p.awayUntil = timeNow() + kReconnectGrace;
    p.awayReason = reason;
    p.addr = NetAddr{};   // (no packets to or from it until a reconnect)
    p.cmdQueue.clear();
    p.events.clear();
    p.keys = 0;
    vote_.yes &= ~(1u << id); vote_.no &= ~(1u << id);
    message(p.name + " " + (reason == "disconnected" ? "disconnected" : "lost connection") + " - their place is kept for " +
            std::to_string((int)kReconnectGrace) + " s");
    log(p.name + " away: " + reason);
    glog(LOG_SIMPLE, "away %d \"%s\": %s", id, p.name.c_str(), reason.c_str());
}

bool Server::restorePlayer(int id, const NetAddr& from, uint32_t nonce, const std::string& rankPub, const NetAddr& dialled) {
    ServerPlayer& p = players_[id];
    static const ServerPlayer fresh{};
    bool wasAway = p.awayUntil > 0;
    p.addr = from;
    p.nonce = nonce;
    if (!rankPub.empty()) { p.rankPub = rankPub; p.dialled = dialled; }
    p.awayUntil = 0;
    p.awayReason.clear();
    p.dropPending = false;
    // a new session: its own command / event / snapshot sequences start over
    p.sentSeq = 0; p.sent.clear(); p.ackSnapMs = 0;
    p.lastCmdSeq = 0; p.cmdQueue.clear(); p.cmdTokensMs = fresh.cmdTokensMs; p.deepTicks = 0; p.catchUp = fresh.catchUp;
    p.lastStringCmd = 0; p.strTick = 0; p.strCount = p.edCount = 0;
    p.nextEventId = 1; p.ackedEvent = 0; p.events.clear();
    p.reportedLoss = 0; p.lastViewTick = 0; p.ping = 0; p.lastRecv = timeNow();
    p.mapTokens = fresh.mapTokens; p.mapTokensAt = 0;
    p.buttons = p.prevButtons = 0; p.keys = 0;
    sendAccept(sock_, p, id, map_, cfg_.mode);
    sendWelcome(id);
    { GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)id; e.b = looksByte(p); e.text = p.name; sendEvent(id, e); }
    if (!p.rankId.empty()) { GameEvent r; r.type = EV_RANKINFO; r.a = (uint8_t)id; r.text = p.rankId; sendEvent(id, r); }
    sendRules(id);
    if (tourney_.running) broadcastBracket();
    if (rankedLive() && !rankedTicketSent_ && isRankedParticipant(id) && p.ticketSig.empty()) {
        GameEvent e; e.type = EV_RANKSIGN; e.text = rankedMatch_; sendEvent(id, e);
    }
    if (!raceTicket_.empty() && !raceTicketSent_ && !p.rankId.empty() && p.rankId == raceTicketFor_) {   // (the race ticket, unsigned yet)
        GameEvent e; e.type = EV_RANKSIGN; e.text = raceTicket_; sendEvent(id, e);
    }
    message(p.name + (wasAway ? " reconnected" : " rejoined"));
    log(p.name + " reconnected from " + from.toString() + " as #" + std::to_string(id));
    return true;
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
    if (rulesTeamLocked(*this)) { p.team = TEAM_SPECTATOR; p.waitingToPlay = true; return; }   // tag: watches this match, plays the next
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

void Server::dropPlayer(int id, const std::string& reason, bool notify, bool shutdown) {
    ServerPlayer& p = players_[id];
    if (!p.connected) return;
    if (notify && p.awayUntil <= 0) {
        uint8_t out[300];
        Writer w(out, sizeof out);
        writeHeader(w, PKT_KICKED);
        w.u32(p.nonce);
        w.str(reason);
        sock_.send(p.addr, out, w.size());
    }
    // Leaving a live ranked match: the first to leave takes the loss. Any drop but the server stopping counts (the
    // grace running out, a new session from the same address without the token or key, the unacked-event cap): only
    // a reconnect within the grace (restorePlayer) keeps the player in it. Each ranked id is counted once.
    bool rankedLeaver = !shutdown && rankedLive() && matchInProgress() && isRankedParticipant(id);
    for (auto& l : rankedLeft_) if (rankedLeaver && l.rankId == p.rankId) rankedLeaver = false;
    bool firstLeaver = rankedLeaver && rankedLeft_.empty();
    if (rankedLeaver) rankedLeft_.push_back({id, p.rankId, p.team, p.score, p.kills});
    rulesOnLeave(*this, id);
    vote_.yes &= ~(1u << id); vote_.no &= ~(1u << id);
    std::string name = p.name;
    int team = p.team;
    p.connected = false;
    p.alive = false;
    p.awayUntil = 0;
    message(name + " left (" + reason + ")");
    log(name + " dropped: " + reason);
    if (rankedLeaver) {
        if (firstLeaver) message(name + " left the ranked match - it counts as a loss");
        glog(LOG_SIMPLE, "ranked leaver %d \"%s\"%s", id, name.c_str(), firstLeaver ? " (first: takes the loss)" : "");
        int still = 0;
        for (int i = 0; i < kMaxPlayers; i++) still += isRankedParticipant(i);
        // 1v1: the match is decided right there. Free for all: it goes on while two or more are left.
        if (modeIsTeamBased(cfg_.mode) && firstLeaver) {
            int winner = team == TEAM_A ? TEAM_B : TEAM_A;
            rulesEndMatch(*this, name + " left the match", winner);
        } else if (!modeIsTeamBased(cfg_.mode) && still < 2) {
            rulesEndMatch(*this, "match over - the other players left", -1);
        }
    }
}

bool Server::isRankedParticipant(int id) const {
    const ServerPlayer& p = players_[id];
    return p.connected && !p.isBot && p.team != TEAM_SPECTATOR && !p.rankId.empty();
}

// (The console completes /kick with names: atoi("Bob") was 0, so "/kick Bob" kicked slot 0, usually the host.)
int Server::playerFromArg(const std::string& who, std::string* err) const {
    auto lower = [](std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; };
    if (who.empty()) { *err = "kick whom? a name or an id (status lists them)"; return -1; }
    if (who.size() <= 2 && who.find_first_not_of("0123456789") == std::string::npos) {
        int id = atoi(who.c_str());
        if (id < kMaxPlayers && players_[id].connected) return id;
    }
    const std::string w = lower(who);
    int exactCase = -1, exact = -1, prefix = -1, exacts = 0, prefixes = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!players_[i].connected) continue;
        if (players_[i].name == who) exactCase = i;
        std::string n = lower(players_[i].name);
        if (n == w) { exact = i; exacts++; }
        else if (n.compare(0, w.size(), w) == 0) { prefix = i; prefixes++; }
    }
    if (exactCase >= 0) return exactCase;
    if (exacts == 1) return exact;
    if (exacts == 0 && prefixes == 1) return prefix;
    *err = exacts + prefixes ? "\"" + who + "\" matches more than one player: use the id (status lists them)" : "no player \"" + who + "\"";
    return -1;
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
        // a budget per tick (map edits cost the most): what's over it stays unacknowledged and the client resends it
        if (p.strTick != tick_) { p.strTick = tick_; p.strCount = p.edCount = 0; }
        bool ed = strs[k].compare(0, 3, "ed ") == 0;
        if (ed) {   // edits have their own budget (they come by the hundred: a custom group is one op per part)
            if (editTick_ != tick_) { editTick_ = tick_; editSpentMs_ = 0; }
            if (p.edCount >= kMaxEditOpsPerTick || editSpentMs_ >= kEditBudgetMs || editBacklogged()) break;
            double t0 = timeNow();
            p.edCount++;
            p.lastStringCmd = seq;
            handleStringCmd(id, strs[k]);
            editSpentMs_ += (timeNow() - t0) * 1000.0;
            continue;
        }
        if (p.strCount >= kMaxStringCmdsPerTick) break;
        p.strCount++;
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
    if (cmd == "locate") { // the tagger / seeker's locator: where a random runner / hider is, every 5 s
        if (!isItMode(ms_.mode) || ms_.phase != PHASE_LIVE || !p.it || !p.alive || time() < p.locateAt) return;
        std::vector<int> targets;
        for (int i = 0; i < kMaxPlayers; i++) {
            const auto& o = players_[i];
            if (i != id && o.connected && o.alive && !o.it && o.team != TEAM_SPECTATOR && !(ms_.mode == MODE_TAG && o.out)) targets.push_back(i);
        }
        if (targets.empty()) return;
        p.locateAt = time() + kLocatorCooldown;
        int t = targets[qrand() % targets.size()];
        char pos[64];
        snprintf(pos, sizeof pos, "%.2f %.2f %.2f", players_[t].move.pos.x, players_[t].move.pos.y, players_[t].move.pos.z);
        GameEvent e; e.type = EV_LOCATE; e.a = (uint8_t)t; e.text = pos;
        sendEvent(id, e);
        return;
    }
    if (cmd == "quip") { // a random voice line, heard from where they stand; one every 1.5 s at most
        if (time() < p.quipAt) return;
        p.quipAt = time() + 1.5f;
        GameEvent e; e.type = EV_QUIP; e.a = (uint8_t)id; e.b = (uint8_t)(qrand() & 255);
        broadcastEvent(e);
        return;
    }
    if (cmd == "typing") { // chat keystrokes: at most 4 a second each, to everyone else
        if (time() < p.typingAt) return;
        p.typingAt = time() + 0.25f;
        GameEvent e; e.type = EV_TYPING; e.a = (uint8_t)id;
        for (int i = 0; i < kMaxPlayers; i++) if (i != id && players_[i].connected && !players_[i].isBot) sendEvent(i, e);
    } else if (cmd == "say") {
        if (rest.empty()) return;
        if (rest.size() > 120) rest.resize(120);
        for (auto& c : rest) if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';   // (no control or escape codes in anyone's console or log)
        p.chatTokens = std::min(kChatBurst, p.chatTokens + (float)(time() - p.chatAt) * kChatPerSecond);
        p.chatAt = time();
        if (p.chatTokens < 1) { GameEvent e; e.type = EV_MESSAGE; e.text = "chat: slow down"; sendEvent(id, e); return; }
        p.chatTokens -= 1;
        GameEvent e; e.type = EV_CHAT; e.a = (uint8_t)id; e.text = rest;
        broadcastEvent(e);
        log(p.name + ": " + rest);
        glog(LOG_SIMPLE, "chat %d \"%s\": %s", id, p.name.c_str(), rest.c_str());
    } else if (cmd == "ticketsig") { // the player's signature of the live ranked match id (EV_RANKSIGN): goes with the ticket
        // (or of the time trial's race ticket id, from its racer)
        if (!raceTicket_.empty() && !raceTicketSent_ && !p.rankId.empty() && p.rankId == raceTicketFor_ &&
            rankVerify(p.rankPub, rankTicketMessage(raceTicket_, p.dialled.toString()), rest)) { sendRaceTicket(p.rankPub, rest, p.dialled.toString()); return; }
        if (!rankedLive() || rankedTicketSent_ || !isRankedParticipant(id) || !p.ticketSig.empty()) return;
        if (!rankVerify(p.rankPub, rankTicketMessage(rankedMatch_, p.dialled.toString()), rest)) return;   // (names the address it signed at connect)
        p.ticketSig = rest;
        maybeSendRankedTicket();
    } else if (cmd == "ed" && editing_) {
        if (!applyEdit(id, rest)) { GameEvent e; e.type = EV_MESSAGE; e.text = "edit refused"; sendEvent(id, e); }
    } else if (cmd == "edcam" && editing_) { // where this editor's camera is, so others see it
        Vec3 c;
        std::istringstream cs(rest);
        if (cs >> c.x >> c.y >> c.z && std::isfinite(c.x + c.y + c.z)) p.move.pos = vmax(vmin(c, Vec3(4096, 4096, 4096)), Vec3(-4096, -4096, -4096));
    } else if (cmd == "team") {
        int t = rest == "a" ? TEAM_A : rest == "b" ? TEAM_B : rest == "spec" ? TEAM_SPECTATOR : -1;
        if (t < 0 || t == p.team) return;
        if (rankedLive()) { GameEvent e; e.type = EV_MESSAGE; e.text = "a ranked match is being played - teams stay as they are until it's over"; sendEvent(id, e); return; }
        if (tourney_.running && t != TEAM_SPECTATOR) { // the bracket decides who plays
            GameEvent e; e.type = EV_MESSAGE; e.text = "a tourney is running - you can join the next one"; sendEvent(id, e);
            return;
        }
        if (rulesTeamLocked(*this) && t != TEAM_SPECTATOR) { // tag: no joining (or rejoining) mid-match
            GameEvent e; e.type = EV_MESSAGE; e.text = "a tag match is being played - you can join the next one"; sendEvent(id, e);
            return;
        }
        if (t == TEAM_SPECTATOR) rulesOnSpectate(*this, id);   // (tag: that's elimination)
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
        if (rankedLive()) { GameEvent e; e.type = EV_MESSAGE; e.text = "a ranked match is being played - no votes until it's over"; sendEvent(id, e); return; }
        if (time() < p.voteAt) { GameEvent e; e.type = EV_MESSAGE; e.text = "your last vote failed: wait a few seconds"; sendEvent(id, e); return; }
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
            int n = 0;
            while (std::getline(ls, it, ',') && ok) {
                if (++n > 32 || (what == "tmaps" ? !(mapInfo(it).supports >> MODE_TOURNEY & 1) : arenaVariantFromName(it.c_str()) < 0)) ok = false;
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
            MapInfo m = mapInfo(mapName);
            std::string why;   // say exactly what's wrong instead of a generic refusal
            if (mode < 0) why = "unknown mode '" + (what == "mode" ? arg : m2) + "'";
            else if (cfg_.custom.active() && !cfg_.custom.maps.empty() && std::find(cfg_.custom.maps.begin(), cfg_.custom.maps.end(), mapName) == cfg_.custom.maps.end())
                why = mapName + " isn't in this custom mode's map pool";
            else if (cfg_.custom.active() && mode != cfg_.custom.baseMode) why = "this server plays the custom mode " + cfg_.custom.name;
            else if (!m.ok) why = "this server doesn't have the map '" + mapName + "' (it may be on an older version)";

            bool ok = why.empty();
            if (ok && cfg_.ranked && (!arenaVariantRanked(variant) || mode != rankedMode(variant) || !(m.ranked >> variant & 1))) {
                GameEvent e; e.type = EV_MESSAGE; e.text = "ranked server: a ranked map for the ladder (vanilla / vampire 1v1, instagib free for all), or vote ranked off"; sendEvent(id, e);
                return;
            }
            if (!ok) { GameEvent e; e.type = EV_MESSAGE; e.text = "can't vote: " + why; sendEvent(id, e); }
            else callVote(id, VOTE_MAP, mode, mapName, variant);
        }
    } else if (cmd == "vote") {
        castVote(id, rest == "yes" || rest == "y" || rest == "1");
    } else if (cmd == "interp") {   // the client's fixed interpolation delay in ms (0 = adaptive): the rewind cap covers it
        p.interpMs = (uint16_t)std::min(std::max(std::atoi(rest.c_str()), 0), kMaxInterpMs);
    } else if (cmd == "kill") {
        if (p.alive) killPlayer(id, -1, 255);
    } else if (isAdmin(id)) {
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
    std::string who;   // kick: the rest of the line (names can have spaces), trimmed
    if (cmd == "kick") {
        std::istringstream t(line);
        std::string c;
        t >> c;
        std::getline(t, who);
        size_t a = who.find_first_not_of(" \t"), b = who.find_last_not_of(" \t\r\n");
        who = a == std::string::npos ? std::string() : who.substr(a, b - a + 1);
    }
    if (rankedLive()) {   // a live ranked match can't be changed or dodged, by anyone, until it's over (OWNER 3.4)
        bool blocked = cmd == "map" || cmd == "mode" || cmd == "reload" || cmd == "ranked" || cmd == "restart" || cmd == "editmode" ||
                       cmd == "forcestart" || cmd == "addbot";
        if (cmd == "kick") { std::string e; int k = playerFromArg(who, &e); blocked = k >= 0 && isRankedParticipant(k); }
        if (blocked) { reply("a ranked match is being played: '" + cmd + "' has to wait until it's over"); return; }
    }
    if (cmd == "editmode" && cfg_.ranked) { reply("no map editing on a ranked server (vote ranked off first)"); return; }
    if (cmd == "editmode") { // editmode on|off: switch between editing the map together and playing it
        if (arg == "on") { if (!editing_) setEditing(true); return; }
        if (!editing_) return;
        if (arg == "saveas") { // editmode saveas NAME: the session's map is saved (and autosaved from now on) under NAME
            std::string name;
            ss >> name;
            if (!validMapName(name)) { reply("map name: letters, digits, - and _ only (not a device name like con)"); return; }
            std::string old = cfg_.map;
            cfg_.map = name;
            map_.name = name;
            if (!autosave()) { cfg_.map = map_.name = old; reply("could not save maps/" + name + ".qmap"); return; }
            if (std::find(mapList_.begin(), mapList_.end(), name) == mapList_.end()) { mapList_.push_back(name); std::sort(mapList_.begin(), mapList_.end()); }
            message("map saved as " + name);
            return;
        }
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
        if (cfg_.ranked && (!arenaVariantRanked(cfg_.arenaVariant) || newMode != rankedMode(cfg_.arenaVariant) || !isRankedMap(m, cfg_.arenaVariant))) {
            reply("ranked: vanilla or laser vampire 1v1, or instagib free for all, on that ladder's ranked maps");
            return;
        }
        if (editing_ && tempEdit_) setEditing(false); // a voted edit was temporary: the new map loads as it is on disk
        map_ = m;
        nav_.buildAsync(std::move(m));
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
        // addbot [a|b] [skill]: the skill is 0-3 or its name in any case (what the console completes: REVIEW 4.2)
        std::string sk;
        ss >> sk;
        if (arg != "a" && arg != "b" && sk.empty()) sk = arg;   // (just a skill)
        int team = arg == "b" ? TEAM_B : TEAM_A;
        int skill = sk.empty() ? cfg_.botSkill : -1;
        auto lower = [](std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; };
        for (int i = 0; i < BOT_SKILLS && skill < 0; i++) if (sk == std::to_string(i) || lower(sk) == lower(botSkillName(i))) skill = i;
        if (skill < 0) { reply("addbot [a|b] [0-3 or beginner|intermediate|advanced|expert]"); return; }
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
        std::string err;
        int id = playerFromArg(who, &err);
        if (id >= 0) dropPlayer(id, "kicked", true);
        else reply(err);
    } else if (cmd == "say") {
        std::string rest;
        std::getline(ss, rest);
        message("[server] " + arg + rest);
    } else if (cmd == "status") {
        reply(statusText());
    } else {
        reply("commands: map <name> [mode], mode <name>, reload, editmode on|off, ranked on [ladder]|off, forcestart, restart, kick <name|id>, say <text>, status, addbot [a|b] [0-3|skill], kickbots");
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

bool Server::editBacklogged() const {
    for (int i = 0; i < kMaxPlayers; i++)
        if (players_[i].connected && !players_[i].isBot && players_[i].events.size() >= kMaxUnackedEvents / 2) return true;
    return false;
}

void Server::broadcastEvent(const GameEvent& e) {
    for (int i = 0; i < kMaxPlayers; i++)
        if (players_[i].connected) sendEvent(i, e);
}

void Server::sendEvent(int id, const GameEvent& e) {
    ServerPlayer& p = players_[id];
    if (p.isBot || p.awayUntil > 0 || p.dropPending) return;   // (away: a reconnect starts a new session, with what it needs)
    if (p.events.size() >= kMaxUnackedEvents) { // hopelessly behind: dropping one would desync it for good
        if (!p.connected) return;
        p.events.clear();
        p.dropPending = true;   // dropped at the end of this frame (dropPlayer): dropping here, in the middle of whatever
        return;                 // is sending, could end a ranked match from inside another broadcast or the rules
    }
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
    std::vector<const SpawnPoint*> cands = spawnPool(map_, rulesSpawnType(*this, id), ms_.point);

    Vec3 pos;
    float yaw = 0;
    if (ms_.mode == MODE_RACE && p.checkpoint > 0 && p.checkpoint <= (int)map_.checkpoints.size() && (ms_.phase == PHASE_LIVE || ms_.phase == PHASE_WARMUP)) { // (practice too)
        const AABB& c = map_.checkpoints[p.checkpoint - 1];
        pos = {(c.mn.x + c.mx.x) * 0.5f, c.mn.y + 0.05f, (c.mn.z + c.mx.z) * 0.5f};
        yaw = p.yaw;
    } else {
        // Pick the free spawn farthest from other living players. Free for all and instagib (one hit kills):
        // above all, one nobody can see (a clear line from anyone's eyes), so nobody is shot as they appear.
        const bool unseen = ms_.mode == MODE_FFA || (arenaRules() & AR_INSTAGIB);
        const SpawnPoint* best = cands[qrand() % cands.size()];
        float bestScore = -1e9f;
        for (auto* sp : cands) {
            float nearest = 1e9f;
            int seenBy = 0;
            Vec3 chest = sp->pos + Vec3(0, 1.2f, 0);
            for (int i = 0; i < kMaxPlayers; i++) {
                if (i == id || !players_[i].connected || !players_[i].alive) continue;
                nearest = std::min(nearest, length(players_[i].move.pos - sp->pos));
                if (!unseen) continue;
                Vec3 eye = players_[i].move.pos + Vec3(0, kEyeHeight * bodyScale(), 0), to = chest - eye;
                float d = length(to);
                if (d > 0.1f && map_.raycast(eye, to / d, d) < 0) seenBy++;   // nothing in between
            }
            if (map_.boxSolid(playerBox(sp->pos, false, bodyScale()))) continue;   // (a giant needs a giant's room)
            float score = nearest + (qrand() % 100) * 0.01f - seenBy * 1000.0f;
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
    p.health = lifeMaxHealth();   // (a duel life's own, or a custom mode's)
    p.move.mods |= dm.moveMods | moveScaleMods(dm.size);   // (size modifiers: the hull, as well as the hitbox and eyes)
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
        else { k.kills++; k.score += ms_.mode == MODE_HIDE ? 0 : 1; }   // (hide and seek scores by time: rulesOnDeath)
    } else if (cfg_.mode == MODE_FFA && ms_.phase == PHASE_LIVE) {
        v.score--; // suicide
    }
    GameEvent e;
    e.type = EV_KILL;
    e.a = (uint8_t)(killer < 0 ? 255 : killer);
    e.b = (uint8_t)victim;
    // c: the weapon, plus how it happened (the announcer's headshot / airborne / air rocket calls)
    bool airborne = !(v.move.flags & MF_ONGROUND) && !map_.onLadder(playerBox(v.move));
    e.c = (int16_t)(weapon < 0 ? weapon : weapon | (headHit_[victim] ? KILL_HEADSHOT : 0) | (airborne ? KILL_AIRBORNE : 0));
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
    if (attacker >= 0 && attacker != victim && !friendly) players_[attacker].hitSinceShot = true;
    if (attacker >= 0 && attacker != victim && !friendly) players_[attacker].dmgDealt += std::min(dmg, std::max(0.0f, v.health)); // (the health it took)
    v.health -= dmg;
    v.lastDamagedAt = time();
    if (attacker >= 0 && attacker != victim) {
        lastProgress_ = time();
        // Hit feedback (hitsound, damage number): rapid small hits (laser) are batched every 50 ms
        // instead of sending a reliable event per tick. c = damage in hundredths.
        if (headHit_[victim]) {   // a headshot goes out on its own, at once (the client shows it red)
            flushHit(attacker, victim);
            hitAccum_[attacker][victim] += dmg;
            hitHead_[attacker][victim] = true;
            flushHit(attacker, victim);
        } else {
            hitAccum_[attacker][victim] += dmg;
            hitPending_ = true;
            if (time() >= hitFlushAt_[attacker][victim]) flushHit(attacker, victim);
        }
        // Vampire laser: damage dealt heals the attacker (up to full health).
        ServerPlayer& a = players_[attacker];
        if ((rules & AR_VAMPIRE) && a.alive) { // healing fades as the round goes on
            float f = clampf((time() - roundStart_) / kVampireFadeTime, 0, 1);
            a.health = std::min(lifeMaxHealth(), a.health + dmg * (1.0f - (1.0f - kVampireMinHeal) * f));
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
    if (hitHead_[attacker][victim]) e.text = "h";   // a headshot
    hitHead_[attacker][victim] = false;
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
        tickVote();   // (votes still time out: "play it" passes unless someone said no)
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
            c.seq = tick_; // (a command number like a client's; spread doesn't depend on it, NET-9)
            c.msec = (uint8_t)(1000 / tickRate());
            c.viewTick = timeMs_;
            { QZONE("bot cmd"); runPlayerCmd(i, c); }
            continue;
        }
        p.cmdTokensMs = std::min(p.cmdTokensMs + 1000.0f / tickRate(), kMaxCmdTokensMs);
        { QZONE("human cmds"); drainCmds(i); }
        p.deepTicks = (int)p.cmdQueue.size() > kDeepQueue ? p.deepTicks + 1 : 0;
        p.catchUp = std::min(kCatchUpMax, p.catchUp + kCatchUpRefill);
        if (p.deepTicks >= kDeepTicks && p.catchUp >= 1) {   // a backlog that isn't going away: one extra command
            p.catchUp -= 1;
            uint8_t ms = p.cmdQueue.front().msec;
            p.cmdTokensMs += (ms == 1 || ms == 2 || ms == 4 || ms == 8) ? ms : 1;
            drainCmds(i);
        }
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
        if (p.alive && (p.move.pos.y < map_.killY() || (!map_.lava.empty() && map_.inLava(playerBox(p.move))))) killPlayer(i, -1, 252);
        if (!p.alive && p.respawnAt >= 0 && time() >= p.respawnAt && p.team != TEAM_SPECTATOR) spawnPlayer(i);
        if (p.alive && (arenaRules() & AR_REGEN) && time() - p.lastDamagedAt > 3.0 && p.health < lifeMaxHealth())
            p.health = std::min(lifeMaxHealth(), p.health + 10.0f * dt()); // regeneration: 10 hp/s after 3 s untouched
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
    p.cmdsRun++;
    p.keys = (cmd.forward > 0 ? KEY_FWD : 0) | (cmd.forward < 0 ? KEY_BACK : 0) | (cmd.side < 0 ? KEY_LEFT : 0) | (cmd.side > 0 ? KEY_RIGHT : 0) |
             (cmd.buttons & BTN_JUMP ? KEY_JUMP : 0) | (cmd.buttons & BTN_CROUCH ? KEY_CROUCH : 0) | (cmd.buttons & BTN_FIRE ? KEY_FIRE : 0) |
             (cmd.buttons & BTN_ALTFIRE ? KEY_ALT : 0) | (cmd.buttons & BTN_DASH ? KEY_DASH : 0) | (cmd.buttons & BTN_GRAPPLE ? KEY_HOOK : 0);
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
    bool throwReady = time() >= p.throwReadyAt;   // (knife / syringe throws share a cooldown)
    bool altShot = altFires(p.weapon) && (cmd.buttons & BTN_ALTFIRE) && !(p.weapon == WPN_KNIFE && knifeAway) && !itMode // no throwing in hide and seek / tag
                   && !(isThrow(p.weapon, true) && !throwReady);
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
        bool altEff = (cmd.buttons & BTN_ALTFIRE) && !(isThrow(p.weapon, true) && !throwReady);   // (a throw on cooldown isn't one)
        float refire = shotRefire(p.weapon, altEff);
        bool held = (p.prevButtons & (altShot ? BTN_ALTFIRE : BTN_FIRE)) != 0;
        bool burst = held && time() - p.nextFire < 0.1f && p.weapon == p.burstWeapon;
        p.burstShot = burst ? p.burstShot + 1 : 0;   // the shot's place in the fixed spread sequence (NET-9)
        p.burstWeapon = p.weapon;
        p.nextFire = (held && time() - p.nextFire < 0.1f) ? p.nextFire + refire : time() + refire;
        p.lastFire = time();
        bool threw = p.weapon == WPN_KNIFE && (cmd.buttons & BTN_ALTFIRE) && !itMode && !knifeAway && throwReady; // same test as fireWeapon
        int firedWeapon = p.weapon;
        bool hit = fireWeapon(id, cmd);
        if (isThrow(firedWeapon, altEff) && (firedWeapon != WPN_KNIFE || threw)) {
            p.throwReadyAt = time() + throwCooldown(firedWeapon);
            throwReady = false;
        }
        p.knifeSwingUntil = (p.weapon == WPN_KNIFE && !threw && !hit) ? time() + kKnifeSwingTime : -1;
        if (threw) { // the knife is away: switch to the next weapon
            p.knifeBackAt = time() + kKnifeAway;
            p.fireLocked = true;
            if (otherWeapon() >= 0) p.weapon = otherWeapon();   // (ready after a knife swing's refire: nextFire above)
            p.fireQueuedUntil = -1;
            break;
        }
    }
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
            float d = dmg;
            if (headDmg > 0 && rayHitsHead(eye, dir, bb)) { d = std::max(d, headDmg); headHit_[i] = true; }
            dmgPer[i] += (bang >= 0 && t > bang) ? d * kWallbangDamage : d;
            continue;
        }
        float t = rayAABB(eye, inv, bb, best);
        if (t >= 0 && t < best) { best = t; hit = i; hitBb = bb; }
    }
    if (hit < 0) return;
    if (headDmg > 0 && rayHitsHead(eye, dir, hitBb)) { dmg = std::max(dmg, headDmg); headHit_[hit] = true; }   // (the head, not the shoulders)
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
    if (map_.holdFire > 0 && ms_.phase == PHASE_LIVE && time() < liveSince_ + map_.holdFire) return false;   // the bow before the fight
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
    if (isThrow(p.weapon, alt) && time() < p.throwReadyAt) alt = false;   // the shared throw cooldown (syringe: a dose for yourself instead)
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
        if (!alt) { p.health = std::min(lifeMaxHealth(), p.health + wd.damage); return false; }   // a dose for yourself
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
            float nearest = 1e9f;
            for (int i = 0; i < kMaxPlayers; i++) {
                const auto& o = players_[i];
                if (i == id || !o.connected || !o.alive || (modeIsTeamBased(cfg_.mode) && o.team == p.team)) continue;
                nearest = std::min(nearest, length(o.move.pos - p.move.pos));
            }
            const int count = bubbleStreamCount(nearest);
            int n = 0;
            for (auto& pr : projectiles_) {
                if (pr.active) continue;
                pr = Projectile();
                pr.active = true;
                pr.id = nextProjId_++;
                pr.type = WPN_BUBBLE;
                pr.owner = id;
                pr.pos = eye + aim * 0.3f;
                float speed = kBubbleStreamSlow + (kBubbleStreamFast - kBubbleStreamSlow) * n / std::max(1, count - 1);
                pr.vel = spreadAim(aim, 0.09f, n, count, 0) * speed * (wd.projSpeed / baseWeaponDef(WPN_BUBBLE).projSpeed);
                pr.dieAt = time() + wd.life;
                if (++n == count) break;
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
            if (p.weapon != WPN_SYRINGE) p.shotsFired++;   // accuracy: a hit is counted when it lands
            pr.pos = eye + aim * 0.3f;
            pr.vel = spreadAim(aim, spread, 0, 1, p.burstShot) * wd.projSpeed;
            if (p.weapon == WPN_ROCKET && alt) { pr.vel = pr.vel * kRocketLobSpeed; pr.gravMul = kRocketLobGravity; }
            if (p.weapon == WPN_STICKY) pr.vel += p.move.vel * kProjInherit; // stickies only (1.14.2: rockets fly straight again): launched from a moving player, most of your velocity
            pr.dieAt = time() + wd.life;
            pr.armAt = time() + kStickyArmTime;
            return false;
        }
        return false;
    }
    p.shotsFired++;
    for (bool& h : headHit_) h = false;
    uint32_t vt = cmd.viewTick; // ms on the server clock
    if (vt > timeMs_) vt = timeMs_;
    {   // rewind at most the measured ping + the interpolation this client uses + a little jitter: the adaptive delay's
        // most (two snapshots + 50 ms), or the fixed delay it reported ("interp", up to kMaxInterpMs: FINAL_REVIEW 2.5,
        // a 200 ms setting was clamped short and its hitscan missed movers)
        uint32_t interp = std::max<uint32_t>(2000u / (uint32_t)std::max(1, tickRate() / snapEvery()) + 50u, 100u);
        interp = std::max<uint32_t>(interp, std::min<uint32_t>(p.interpMs, (uint32_t)kMaxInterpMs));
        uint32_t cap = std::min<uint32_t>(kMaxRewindMs, (uint32_t)p.ping + interp + kRewindToleranceMs);
        if (timeMs_ - vt > cap) vt = timeMs_ - cap;
    }
    float range = p.weapon == WPN_KNIFE ? kKnifeRange : wd.range;
    // Pellets add up per target so one shotgun blast is one hit (one damage event) per player.
    float dmgPer[kMaxPlayers] = {};
    // Laser alt-fire is a tractor beam: pulls the target toward you for a little less damage.
    bool pull = wd.beam && (cmd.buttons & BTN_ALTFIRE);
    float dmg = wd.damage * (pull ? kLaserPullDamage : 1.0f);
    bool bounce = p.weapon == WPN_SHOTGUN && alt; // ricochet pellets: half damage, off up to 2 walls
    for (int k = 0; k < wd.pellets; k++) {
        Vec3 d = spreadAim(aim, spread, k, wd.pellets, p.burstShot);   // the fixed pattern (NET-9)
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
    if (shotHit(id)) p.shotsHit++;
    for (bool& h : headHit_) h = false;
    return any;
}

void Server::tickProjectiles() {
    for (auto& pr : projectiles_) {
        if (!pr.active) continue;
        if (time() >= pr.dieAt) { pr.active = false; continue; }
        if (pr.stuck && pr.onCart) {
            if (map_.dynSolids.empty()) pr.active = false; // cart gone (round over)
            else pr.pos = map_.cartPoint(ms_.mode, ms_.attackers, ms_.payloadDist) + pr.cartOffset;
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
                t.health = std::min(lifeMaxHealth(), t.health + kSyringeHeal * tunedDamage(WPN_SYRINGE));
                t.nextFire = time();
                t.move.dashCooldown = 0;
                t.move.hookCooldown = 0;
                if (t.knifeBackAt > time()) t.knifeBackAt = time();
                t.throwReadyAt = time();   // (the throws' shared cooldown too: it used to survive the reset)
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
                    pr.cartOffset = pr.pos - map_.cartPoint(ms_.mode, ms_.attackers, ms_.payloadDist);
                }
            }
        } else if (wd.splashRadius <= 0) { // crossbow bolt / thrown knife: direct hit only
            if (hitPlayer >= 0) {
                float dmg = (float)wd.damage, head = headDamage(pr.type); // bolts: headshots (the knife has none)
                if (head > 0 && rayHitsHead(pr.pos, dir, hitBb)) { dmg = std::max(dmg, head); headHit_[hitPlayer] = true; }
                damagePlayer(hitPlayer, pr.owner, dmg, pr.type, dir * (pr.type == WPN_CROSSBOW ? kCrossbowKnock : 3.0f));
                headHit_[hitPlayer] = false;
                if (pr.owner >= 0 && shotHit(pr.owner)) players_[pr.owner].shotsHit++;
            }
            pr.active = false;
        } else {
            pr.active = false;
            explode(at, pr.owner, pr.type, pr.type == WPN_ROCKET ? hitPlayer : -1);
        }
    }
}

void Server::explode(const Vec3& at, int owner, int weapon, int direct) {
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
        if (i == direct && !self) dmg = std::max(dmg, kRocketDirectDamage * tunedDamage(weapon));   // a direct hit
        Vec3 knock = push * (11.0f * frac * (self ? 1.3f : 1.0f) * (weapon == WPN_BUBBLE ? kBubbleKnock : 1.0f));
        if (!self) knock.y *= 2.0f; // enemies get launched: twice the vertical knockback
        damagePlayer(i, owner, dmg, weapon, knock);
    }
    if (owner >= 0 && weapon != WPN_BUBBLE && shotHit(owner)) players_[owner].shotsHit++;
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
        ps.acc = (uint8_t)(p.shotsFired ? std::min(100, p.shotsHit * 100 / p.shotsFired) : 0);
        ps.ping = (uint16_t)p.ping;
        ps.checkpoint = (uint8_t)p.checkpoint;
        ps.keys = p.alive ? p.keys : 0;
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
        if (!p.connected || p.isBot || p.awayUntil > 0) continue;
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
    bots_[id].rng = (qrand() ^ (uint32_t)(id * 2654435761u)) | 1u;   // (qrand: seeded per thread at random, not from the tick count: REVIEW GAME-12)
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
    // A round mode's live round keeps a balance bot that's still alive (REVIEW GAME-10: removing a team's last player
    // alive lost them the round on the spot); it goes once it dies or the round ends.
    bool roundOn = (ms_.phase == PHASE_FREEZE || ms_.phase == PHASE_LIVE) && !modeRespawns(ms_.mode) && wanted;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected || !p.balanceBot) continue;
        int& have = p.team == TEAM_A ? haveA : haveB;
        int need = p.team == TEAM_A ? needA : needB;
        if (have >= need && !(roundOn && p.alive)) removeBot(i);
        else have++;
    }
    while (haveA < needA && addBot(TEAM_A, cfg_.balanceBotSkill, true) >= 0) haveA++;
    while (haveB < needB && addBot(TEAM_B, cfg_.balanceBotSkill, true) >= 0) haveB++;
}

void Server::callVote(int caller, int kind, int arg, const std::string& map, int arg2) {
    if (vote_.kind != VOTE_NONE) {
        // calling the same vote that's already running is a yes to it
        if (caller >= 0 && kind == vote_.kind && arg == vote_.arg && arg2 == vote_.arg2 && map == vote_.map) { castVote(caller, true); return; }
        GameEvent e; e.type = EV_MESSAGE; e.text = "a vote is already in progress";
        sendEvent(caller, e);
        return;
    }
    vote_ = Vote();
    vote_.caller = caller;
    vote_.kind = kind;
    vote_.caller = caller;
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
    int humans = 0, y = 0, n = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!players_[i].connected || players_[i].isBot || players_[i].awayUntil > 0) continue;   // (away: can't vote)
        humans++;
        y += (vote_.yes >> i) & 1;
        n += (vote_.no >> i) & 1;
    }
    bool pass = y * 2 > humans, fail = n * 2 >= humans || humans == 0;
    if (time() >= vote_.endsAt) {   // time's up: more yes than no passes ("play it" while editing: passes unless someone said no)
        pass = (vote_.kind == VOTE_EDIT && vote_.arg == 0) ? n == 0 : y > n;
        fail = !pass;
    }
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
    if (fail) {
        if (vote_.caller >= 0 && vote_.caller < kMaxPlayers) players_[vote_.caller].voteAt = time() + voteCooldown_;   // (no vote spam)
        message("vote failed: " + vote_.text);
        vote_ = Vote();
        return;
    }
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
    nav_.buildAsync(std::move(m));
    cfg_.map = name;
    mapEdited_ = false;
    for (auto& pr : projectiles_) pr.active = false;
    for (int i = 0; i < kMaxPlayers; i++) {
        auto& p = players_[i];
        if (!p.connected) continue;
        p.alive = false;
        p.cmdQueue.clear();
        if (!p.isBot) sendAccept(sock_, p, i, map_, cfg_.mode);
    }
    glog(LOG_SIMPLE, "map %s mode %s", name.c_str(), modeName(cfg_.mode));
    return true;
}

uint16_t Server::weaponMask() const {
    if (isItMode(ms_.mode)) return (uint16_t)(1u << WPN_KNIFE); // only "it" can use it; everyone else is unarmed
    uint16_t m = ms_.mode == MODE_ARENA || ms_.mode == MODE_TOURNEY || (cfg_.ranked && ms_.mode == MODE_FFA) ? arenaVariant(ms_.arenaVariant).weapons : duelModDef(duelMod()).weapons;
    uint16_t c = cfg_.custom.active() ? cfg_.custom.weapons : 0;   // a custom mode's weapons (narrowed by a duel life's, if any)
    uint16_t r = (c && m && (m & c)) ? m & c : c ? c : m;
    // The syringe only exists in modes where the dead respawn (owner 5.2): in a round mode it out-heals the stalemate
    // drain. A custom mode that lists it in its weapons keeps it anyway.
    const uint16_t syringe = (uint16_t)(1u << WPN_SYRINGE);
    if (!modeRespawns(ms_.mode) && !(c & syringe)) {
        if (!r) r = (uint16_t)((1u << WPN_COUNT) - 1);
        r &= (uint16_t)~syringe;
    }
    return r;
}

AABB Server::hitBox(const Vec3& feet, bool crouched) const {
    return playerBox(feet, crouched, bodyScale());   // (the same box as the hull: size modifiers scale both)
}

void Server::openGameLog() {
    if (cfg_.logLevel <= LOG_OFF || glog_) return;
    std::string path = cfg_.logFile;
    if (path.empty()) {
        std::string dir = userDataPath("logs");
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
        sendAccept(sock_, p, i, map_, cfg_.mode);
    }
    rulesReset(*this);
    message("now playing " + cfg_.map + (mapEdited_ ? " (edited)" : "") + " - " + modeTitle(mode));
    glog(LOG_SIMPLE, "map %s mode %s", cfg_.map.c_str(), modeName(mode));
}

void Server::setEditing(bool on) {
    editing_ = on;
    if (!on) { if (tempEdit_) editDirty_ = false; return; }
    tempEdit_ = !cfg_.editMode; // voted on a normal server: edits live until the map changes, never saved
    autosaveBackedUp_ = false;
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
    try {   // (applyMapEdit checks the result against the same limits a map file gets, and leaves map_ as it was if it fails)
        if (!applyMapEdit(map_, op)) return false;   // (in place: the text is only made again when it's sent or saved)
    } catch (const std::exception& e) {   // (out of memory: refuse the edit, keep the server)
        log(std::string("edit refused: ") + e.what());
        return false;
    }
    map_.crc = mapEditCrc(before, op);
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
    if (!validMapName(cfg_.map)) return false;
    const std::string& text = map_.sourceText();
    // Never write a file that wouldn't load again. (Checked here, not per edit: parsing Midtown takes ~100 ms, and
    // edit sessions autosave at most every 5 s.)
    Map check;
    std::string err;
    if (!check.parse(text, &err)) { log("autosave skipped, the edited map wouldn't load: " + err); nextAutosave_ = time() + 5; return false; }
    std::string path = userMapsDir() + "/" + cfg_.map + ".qmap";
    if (!autosaveBackedUp_) {   // the file as it was before this edit session: maps/<name>.qmap.bak
        std::error_code ec;
        std::filesystem::copy_file(path, path + ".bak", std::filesystem::copy_options::overwrite_existing, ec);
        autosaveBackedUp_ = true;
    }
    if (!saveMapFile(path, text)) { log("autosave failed: maps/" + cfg_.map + ".qmap"); return false; }
    editDirty_ = false;
    nextAutosave_ = time() + 5;
    return true;
}

// Who placed where in the match that just ended, for ranked: team modes rank the two sides (winner 0,
// loser 1, both 0 on a draw); free-for-all modes rank players by score (ties share a place). Every
// player in it must have a ranked id and be human, else the match isn't rated.
std::string Server::rankedResult(int winner, std::string* whyNot) {
    if (cfg_.mode != rankedMode(ms_.arenaVariant)) { *whyNot = "not the ladder's mode"; return ""; }
    if (!arenaVariantRanked(ms_.arenaVariant)) { *whyNot = std::string(arenaVariant(ms_.arenaVariant).title) + " isn't a ranked ruleset"; return ""; }
    bool teams = modeIsTeamBased(cfg_.mode);
    struct Entry { int slot, team; long long score; };
    std::vector<Entry> es;
    for (int i = 0; i < kMaxPlayers; i++) {
        const auto& p = players_[i];
        if (!p.connected || p.team == TEAM_SPECTATOR) continue;
        if (p.isBot) { *whyNot = "bots played"; return ""; }
        if (p.rankId.empty()) { *whyNot = p.name + " has no ranked id"; return ""; }
        es.push_back({i, teams ? p.team : i, (long long)p.score * 1000 + p.kills});
    }
    // players who left during the match count too: the first leaver last (their side lost), later ones as they stood
    for (size_t k = 0; k < rankedLeft_.size(); k++) {
        const RankedLeaver& l = rankedLeft_[k];
        es.push_back({l.slot, teams ? l.team : l.slot, k == 0 ? LLONG_MIN : (long long)l.score * 1000 + l.kills});
    }
    if (teams && !rankedLeft_.empty()) winner = rankedLeft_[0].team == TEAM_A ? TEAM_B : TEAM_A;
    if (es.size() > 16) { *whyNot = "ranked counts 16 players at most"; return ""; }
    std::vector<bool> sides(kMaxPlayers, false);
    int nSides = 0;
    for (auto& e : es) if (!sides[e.team]) { sides[e.team] = true; nSides++; }
    if (es.size() < 2 || nSides < 2) { *whyNot = "not enough players"; return ""; }
    // the ticket's match id (opened when the match went live); without one the Worker won't rate it
    std::string out = (rankedMatch_.empty() ? randomMatchId() : rankedMatch_) + " " + arenaVariant(ms_.arenaVariant).name + " "; // the ladder
    for (size_t k = 0; k < es.size(); k++) {
        int place = 0;
        if (teams) place = winner < 0 ? 0 : es[k].team == winner ? 0 : 1;
        else for (auto& o : es) place += o.score > es[k].score;
        out += (k ? "," : "") + std::to_string(es[k].slot) + ":" + std::to_string(es[k].team) + ":" + std::to_string(place);
    }
    for (size_t k = 0; k < rankedLeft_.size(); k++) out += (k ? "," : " left=") + std::to_string(rankedLeft_[k].slot);
    return out;
}

void Server::ensureMaster() {
    if (master_ || cfg_.lanOnly) return;
    master_ = std::make_unique<MasterAnnouncer>();
    master_->start(port(), cfg_.publicList); // not public: registers for a token, stays off the list
}

// The ranked match goes live: from here until it ends nothing may change it (rankedLive). Each player's game signs
// the match id (EV_RANKSIGN -> "ticketsig"); with every signature in, the ticket goes to the Worker, which only
// rates players who signed it, so a server can't put someone in a match they never played.
void Server::openRankedTicket() {
    if (!cfg_.ranked || !rankedMatch_.empty()) return;
    rankedMatch_ = randomMatchId();
    rankedLeft_.clear();
    rankedTicketSent_ = false;
    rankedSigDeadline_ = time() + 15.0;
    if (vote_.kind != VOTE_NONE) { message("vote cancelled: the ranked match is live"); vote_ = Vote(); }
    if (cfg_.lanOnly) { rankedTicketSent_ = true; message("ranked: a LAN-only server can't open match tickets - this match won't be rated"); return; }
    for (int i = 0; i < kMaxPlayers; i++) {
        players_[i].ticketSig.clear();
        if (isRankedParticipant(i)) { GameEvent e; e.type = EV_RANKSIGN; e.text = rankedMatch_; sendEvent(i, e); }
    }
}

void Server::maybeSendRankedTicket(bool giveUp) {
    if (!rankedLive() || rankedTicketSent_) return;
    std::string players, keys, sigs, addrs;
    bool all = true;
    for (int i = 0; i < kMaxPlayers; i++) {
        const ServerPlayer& p = players_[i];
        if (!p.connected || p.isBot || p.team == TEAM_SPECTATOR) continue;
        if (p.rankId.empty() || p.ticketSig.empty()) { all = false; continue; }
        players += (players.empty() ? "" : ",") + p.rankId;
        keys += (keys.empty() ? "" : ",") + p.rankPub;
        sigs += (sigs.empty() ? "" : ",") + p.ticketSig;
        addrs += (addrs.empty() ? "" : ",") + p.dialled.toString();   // (what each signature names: the Worker checks it's us)
    }
    if (!all && !giveUp) return;
    rankedTicketSent_ = true;
    if (!all) { message("ranked: not every player signed the match ticket - this match won't be rated"); return; }
    ensureMaster();
    if (master_) master_->post("ticket", "/ticket", "kind=match&match=" + rankedMatch_ + "&ladder=" + arenaVariant(ms_.arenaVariant).name +
                                                     "&map=" + cfg_.map + "&players=" + players + "&keys=" + keys + "&sigs=" + sigs + "&addrs=" + addrs);
    glog(LOG_SIMPLE, "ranked ticket %s players %s", rankedMatch_.c_str(), players.c_str());
}

bool Server::timeTrialCounts() const { return cfg_.timeTrial && cfg_.map == "viaduct" && isOfficialMap(map_); }

// A time trial's race ticket: the racer's game signs its id (EV_RANKSIGN -> "ticketsig", as for a ranked match),
// then sendRaceTicket opens it on the Worker with the racer's key and signature.
void Server::openRaceTicket() {
    if (!cfg_.timeTrial || cfg_.lanOnly) return;
    int racer = -1;
    for (int i = 0; i < kMaxPlayers && racer < 0; i++) if (players_[i].connected && !players_[i].isBot && !players_[i].rankId.empty()) racer = i;
    if (racer < 0 || (players_[racer].rankId == raceTicketFor_ && !raceTicket_.empty()) || !timeTrialCounts()) return;
    raceTicket_ = randomMatchId();
    raceTicketFor_ = players_[racer].rankId;
    raceTicketSent_ = false;
    raceFloors_ = raceSplitFloorsMs(map_);
    GameEvent e; e.type = EV_RANKSIGN; e.text = raceTicket_; sendEvent(racer, e);
}

void Server::sendRaceTicket(const std::string& pub, const std::string& sig, const std::string& addr) {
    raceTicketSent_ = true;
    ensureMaster();
    if (master_) master_->post("raceticket", "/ticket", "kind=race&match=" + raceTicket_ + "&map=" + cfg_.map + "&players=" + raceTicketFor_ +
                                                         "&keys=" + pub + "&sigs=" + sig + "&addrs=" + addr);
}

void Server::finishRaceRun(int id) {
    ServerPlayer& p = players_[id];
    if (raceTicket_.empty() || p.rankId != raceTicketFor_ || p.raceSplits.empty() || !master_) return;
    if (!raceTicketSent_) {
        GameEvent e; e.type = EV_MESSAGE; e.text = "run not sent to the leaderboard: your game didn't sign the time trial ticket";
        sendEvent(id, e);
        return;
    }
    if (!raceSplitsPlausible(p.raceSplits, raceFloors_)) {
        GameEvent e; e.type = EV_MESSAGE; e.text = "run not sent to the leaderboard: a checkpoint was reached faster than the course allows";
        sendEvent(id, e);
        glog(LOG_SIMPLE, "race run rejected (implausible splits)");
        return;
    }
    std::string form = buildRaceRunForm(raceTicket_, cfg_.map, p.rankId, p.raceSplits.back(), p.raceSplits, p.name);
    if (!form.empty()) master_->post("racerun:" + std::to_string(id), "/racerun", form);
}

void Server::postRankedResult(const std::string& eventText) {
    std::vector<std::string> ids;
    for (auto& p : players_) ids.push_back(p.rankId);
    std::string match, left, list = rankedResultIds(eventText, ids, &match, nullptr, nullptr, &left);
    if (master_ && !list.empty()) master_->post("result", "/result", "match=" + match + "&p=" + list + (left.empty() ? "" : "&left=" + left));
}

// ---- tourney seeding ----
void Server::prefetchSeedRatings() {
    if (cfg_.lanOnly) return;
    std::vector<std::string> ids;
    for (auto& p : players_) if (p.connected && !p.isBot && !p.rankId.empty()) ids.push_back(p.rankId);
    if (ids.empty()) return;
    if (!seedRatings_) seedRatings_ = std::make_unique<RankService>();   // (its own thread: no HTTP on the tick)
    for (int v = 0; v < kArenaVariants; v++) if (arenaVariantRanked(v)) seedRatings_->fetch(arenaVariant(v).name, ids);
}

int Server::seedRating(int id) {
    const ServerPlayer& p = players_[id];
    auto fixed = seedFixed_.find(p.rankId);
    if (fixed != seedFixed_.end()) return fixed->second;
    int sum = 0, n = 0;
    for (int v = 0; v < kArenaVariants; v++) {
        if (!arenaVariantRanked(v)) continue;
        Rating r;
        bool rated = !p.rankId.empty() && seedRatings_ && seedRatings_->rating(arenaVariant(v).name, p.rankId, r) && r.games > 0;
        sum += rated ? r.display() : kUnratedSeed;
        n++;
    }
    return n ? (int)std::lround((double)sum / n) : kUnratedSeed;
}

std::vector<int> Server::tourneySeeding(const std::vector<int>& entrants) {
    std::vector<int> e = entrants;
    for (size_t i = e.size(); i > 1; i--) std::swap(e[i - 1], e[qrand() % i]); // equal seeds: random order
    std::vector<int> rating(kMaxPlayers, kUnratedSeed);
    for (int i : e) rating[i] = seedRating(i);
    std::stable_sort(e.begin(), e.end(), [&](int a, int b) { return rating[a] > rating[b]; });
    for (size_t k = 0; k < e.size(); k++) glog(LOG_SIMPLE, "tourney seed %d: %s (%d)", (int)k + 1, players_[e[k]].name.c_str(), rating[e[k]]);
    prefetchSeedRatings();   // (fresh numbers for the next one)
    return seededBracket(e);
}

void Server::handleMasterReply(const std::string& key, const std::string& reply) {
    log("ranking server (" + key + "): " + reply);
    if (key == "ticket" && reply.rfind("ticket ", 0) != 0) message("ranked: no match ticket (" + reply + ") - this match won't be rated");
    else if (key == "raceticket" && reply.rfind("ticket ", 0) != 0) { message("time trial: no race ticket (" + reply + ") - runs won't reach the leaderboard"); raceTicket_.clear(); }
    else if (key.rfind("racerun:", 0) == 0) {
        int id = atoi(key.c_str() + 8);
        if (id >= 0 && id < kMaxPlayers && players_[id].connected) { GameEvent e; e.type = EV_MESSAGE; e.text = "leaderboard: " + reply; sendEvent(id, e); }
    }
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
    auto fits = [&](const MapInfo& mi) { return mi.ok && (mi.ranked >> ladder & 1) && (mi.supports >> rankedMode(ladder) & 1); };
    if (!fits(mapInfo(mapName))) {
        // the ladder's own map first (built for just that ruleset), else any ranked map that takes it
        mapName.clear();
        for (auto& n : mapList_) {   // (the list cached at start / saveas: no folder scan on the tick)
            MapInfo c = mapInfo(n);
            if (!fits(c)) continue;
            bool own = c.ranked == (1u << ladder);
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
    command("map " + mapName + " " + modeName(rankedMode(ladder)) + " " + arenaVariant(ladder).name);
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
