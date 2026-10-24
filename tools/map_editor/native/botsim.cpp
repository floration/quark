// BotSim (botsim.h): the game's server and bots, stepped headless.
#include "botsim.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "common/net.h"
#include "common/u8file.h"
#include "common/rng.h"
#include "game/bots.h"
#include "game/customrules.h"
#include "game/gamedefs.h"
#include "game/gamemodes.h"
#include "game/map.h"
#include "game/server.h"
#include "maptool.h"

// ---- the brain seam ---------------------------------------------------------------------------------------------
// Data-driven bot brains (src/game/botbrain.h) are being added to the game on another branch. When the header is
// there, BotSpec::brainText is compiled and given to the bot (setBotBrain); without it every bot plays the built-in
// code and the brain is reported as "unsupported". QUARK_NO_BOTBRAIN forces the latter. Everything the brain API is
// used for is in this block and BotSim::start / checkBrain.
#if !defined(QUARK_NO_BOTBRAIN) && defined(__has_include)
#if __has_include("game/botbrain.h")
#include "game/botbrain.h"
#define QUARK_SIM_BRAINS 1
#endif
#endif
#ifndef QUARK_SIM_BRAINS
#define QUARK_SIM_BRAINS 0
#endif

namespace {
#if QUARK_SIM_BRAINS
// text -> a compiled brain, or null + err (the first errors, "line:col msg")
std::shared_ptr<const Brain> compileSimBrain(const std::string& text, std::string* err) {
    BrainDiag d;
    auto b = compileBrain(text, &d);
    if (!b && err) {
        std::string e;
        for (size_t k = 0; k < d.errors.size() && k < 3; k++)
            e += (k ? "; " : "") + std::to_string(d.errors[k].line) + ":" + std::to_string(d.errors[k].col) + " " + d.errors[k].msg;
        *err = e.empty() ? "the brain doesn't compile" : e;
    }
    return b;
}
#endif
}  // namespace

bool BotSim::brainsSupported() { return QUARK_SIM_BRAINS != 0; }

std::string BotSim::checkBrain(const std::string& text, bool* supported, std::string* label) {
    if (supported) *supported = QUARK_SIM_BRAINS != 0;
#if QUARK_SIM_BRAINS
    std::string err;
    auto b = compileSimBrain(text, &err);
    if (!b) return err;
    if (label) *label = ::brainLabel(*b);
    return "";
#else
    (void)text; (void)label;
    return "";
#endif
}

// Server::simulate and Server::sendSnapshots are private (frame() runs them on the real-time clock). An explicit
// instantiation may name a private member (C++17 [temp.explicit]/12), which hands their addresses over here: the tick
// loop below is frame()'s own, minus the sockets and the clock. DEDUPE LATER: a public Server::stepTick() in src/game.
namespace {
template <class Tag, typename Tag::type M> struct Rob { friend typename Tag::type get(Tag) { return M; } };
struct SimTag { using type = void (Server::*)(); friend type get(SimTag); };
struct SnapTag { using type = void (Server::*)(); friend type get(SnapTag); };
template struct Rob<SimTag, &Server::simulate>;
template struct Rob<SnapTag, &Server::sendSnapshots>;

// The BotSim stepping on this thread: the override bots' BotBrain::script calls into it (simThink).
thread_local BotSim* t_sim = nullptr;
UserCmd simThink(Server& s, int id) { return t_sim ? t_sim->think(id) : (s.bots_[id].script = nullptr, botThink(s, id)); }

std::atomic<int> g_simSerial{0};
const char* kPhaseNames[] = {"warmup", "freeze", "live", "round end", "match end"};
}  // namespace

Json BotEvent::json() const {
    Json o = Json::object();
    o.set("t", t); o.set("kind", kind);
    if (who >= 0) o.set("who", who);
    if (victim >= 0) o.set("victim", victim);
    o.set("pos", jvec(pos));
    if (!detail.empty()) o.set("detail", detail);
    return o;
}

BotSim::BotSim() = default;
BotSim::~BotSim() { stop(); }

bool BotSim::prepareSharedDir(const std::string& dir, const std::string& mapText, std::string* err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(utf8Path(dir) / "maps", ec);
    if (ec) { if (err) *err = "can't make " + dir + ": " + ec.message(); return false; }
    if (!mapText.empty() && !saveMapFile((utf8Path(dir) / "maps" / "zz_maptool.qmap").u8string(), mapText)) { if (err) *err = "can't write the map"; return false; }
    setUserDataDir(dir);
    if (g_serverRateCheck) g_serverRateCheck = false;
    return true;
}

bool BotSim::start(const BotSimConfig& cfg, std::string* err) {
    stop();
    namespace fs = std::filesystem;
    std::error_code ec;
    const int serial = g_simSerial++;
    mapName_ = cfg.mapName;
    mapFile_.clear();
    if (cfg.sharedDataDir.empty()) {
        // a data folder of its own (the map as edited, the game log): never the player's real one
        fs::path dir = fs::temp_directory_path(ec) / ("quark_maptool_" + std::to_string((long long)(maptoolNow() * 1000)) + "_" + std::to_string(serial));
        fs::create_directories(dir / "maps", ec);
        if (ec) { if (err) *err = "can't make a temp folder: " + ec.message(); return false; }
        ownDir_ = true;
        dataDir_ = dir.u8string();   // (UTF-8 like every path the game keeps: %TEMP% under a non-ASCII user name)
        setUserDataDir(dataDir_);
        if (!cfg.mapText.empty()) {
            mapName_ = "zz_maptool";
            if (!saveMapFile((dir / "maps" / "zz_maptool.qmap").u8string(), cfg.mapText)) { if (err) *err = "can't write the map"; return false; }
        }
        logPath_ = (dir / "game.log").u8string();
        if (g_serverRateCheck) g_serverRateCheck = false;   // (no real-time clock to keep up with: never step the rate down)
    } else {
        // simbatch: the folder (and the map in it) is shared, prepared once (prepareSharedDir); a log of our own
        ownDir_ = false;
        dataDir_ = cfg.sharedDataDir;
        if (!cfg.mapText.empty()) mapName_ = "zz_maptool";
        logPath_ = (utf8Path(dataDir_) / ("game_" + std::to_string(serial) + ".log")).u8string();
    }
    logPos_ = 0;
    qsrand(cfg.seed);            // this thread's generator: spawn picks, bots' rngs
    viaFrame_ = cfg.viaFrame;
    if (viaFrame_) setTimeScale(20.0);
    server_ = std::make_unique<Server>();
    Server& s = *server_;
    s.setLog([](const std::string&) {});
    ServerConfig sc;
    sc.map = mapName_;
    sc.mode = cfg.mode;
    sc.port = 0;                 // (an ephemeral port: nothing connects)
    sc.maxPlayers = std::clamp((int)cfg.bots.size(), 2, kMaxPlayers);
    sc.maxTickRate = cfg.tickRate;
    sc.lanOnly = true;
    sc.balanceBots = false;
    sc.autoStart = true;
    sc.logLevel = LOG_SIMPLE;
    sc.logFile = logPath_;
    sc.timeLimit = std::max(0, cfg.timeLimit);
    sc.scoreLimit = std::max(0, cfg.scoreLimit);
    std::string e;
    if (!s.start(sc, &e)) { server_.reset(); if (err) *err = "server: " + e; return false; }
    // the nav graph builds in the background: wait for it (bots without it stand still)
    for (int k = 0; k < 20000 && !s.nav_.ready(s.map_); k++) sleepMs(1);
    specs_ = cfg.bots;
    ids_.clear();
    brainLabel_.assign(specs_.size(), "default");
    brainStatus_.assign(specs_.size(), "default");
    brains_.clear();
    for (size_t k = 0; k < specs_.size(); k++) {
        auto& b = specs_[k];
        int id = s.addBot(b.team, std::clamp(b.skill, 0, BOT_SKILLS - 1));
        ids_.push_back(id);
        if (id >= 0 && !b.name.empty()) s.players_[id].name = b.name.substr(0, 24);
        if (!b.brain.empty() && b.brain != "default") brainLabel_[k] = b.brain;
        if (!cfg.applyBrains || id < 0 || b.brainText.empty()) continue;
#if QUARK_SIM_BRAINS
        std::string berr;
        auto br = compileSimBrain(b.brainText, &berr);
        if (!br) { brainStatus_[k] = "error: " + berr; continue; }
        if (!setBotBrain(s, id, br)) { brainStatus_[k] = "error: the server refused it"; continue; }
        brainLabel_[k] = ::brainLabel(*br);
        brainStatus_[k] = "applied";
        brains_.push_back(br);
#else
        brainStatus_[k] = "unsupported";
#endif
    }
    placedDone_.assign(ids_.size(), 0);
    prev_.assign(kMaxPlayers, Prev());
    aliveTicks_.assign(ids_.size(), 0);
    ov_.assign(ids_.size(), Ov());
    ovOf_.assign(kMaxPlayers, -1);
    anyOverride_ = false;
    for (size_t k = 0; k < specs_.size(); k++)
        if (specs_[k].override_.kind != BotOverride::NONE && ids_[k] >= 0) { ovOf_[ids_[k]] = (int)k; anyOverride_ = true; }
    events_.clear();
    matchOverAt_ = -1; matchText_.clear(); winnerTeam_ = winnerPlayer_ = -1;
    t0_ = 0;
    started_ = false;
    if (cfg.forceStart) {
        step(ticksFor(cfg.warmup));
        s.command("forcestart");
    }
    t0_ = s.time();
    for (auto& e : events_) e.t -= t0_;   // (the warmup's state changes, watched with t0_ = 0: negative times)
    readLog();   // (the warmup's log lines, before started_: not taken for the match's)
    started_ = true;
    prevPhase_ = s.ms_.phase;
    std::fill(aliveTicks_.begin(), aliveTicks_.end(), 0u);
    events_.erase(std::remove_if(events_.begin(), events_.end(), [](const BotEvent& e) { return e.kind == "match"; }), events_.end());
    matchOverAt_ = -1; matchText_.clear(); winnerTeam_ = winnerPlayer_ = -1; matchesEnded_ = 0;   // (nothing from the warmup)
    return true;
}

void BotSim::stop() {
    if (!server_) return;
    server_->stop();
    server_.reset();
    std::error_code ec;
    if (ownDir_ && !dataDir_.empty()) std::filesystem::remove_all(utf8Path(dataDir_), ec);
    else if (!logPath_.empty()) std::filesystem::remove(utf8Path(logPath_), ec);
    dataDir_.clear();
}

int BotSim::tickRate() const { return server_ ? server_->tickRate() : 125; }
int BotSim::ticksFor(double seconds) const { return std::max(1, (int)std::lround(seconds * tickRate())); }
double BotSim::time() const { return server_ ? server_->time() - t0_ : 0; }
bool BotSim::mapChanged() const { return server_ && server_->cfg_.map != mapName_; }

void BotSim::tickOnce() {
    Server& s = *server_;
    // the override bots think through BotSim::think this tick (it clears the hook again: botThink runs as usual)
    if (anyOverride_)
        for (size_t k = 0; k < ids_.size(); k++)
            if (ids_[k] >= 0 && specs_[k].override_.kind != BotOverride::NONE && s.players_[ids_[k]].connected) s.bots_[ids_[k]].script = &simThink;
    // frame()'s tick: simulate, the tick count and clock, the snapshot pass (no clients: bookkeeping only)
    (s.*get(SimTag()))();
    s.tick_++;
    s.timeMs_ += 1000 / s.tickRate();
    if (s.tick_ % s.snapEvery() == 0) (s.*get(SnapTag()))();
}

void BotSim::step(int ticks) { step(ticks, false); }

int BotSim::step(int ticks, bool stopAtMatchEnd) {
    if (!server_) return 0;
    BotSim* prevSim = t_sim;
    t_sim = this;
    // (frame() does this on whichever thread runs the server: the custom rules are per thread)
    if (activeRules().name != server_->cfg_.custom.name) setActiveRules(server_->cfg_.custom);
    int ran = 0;
    if (viaFrame_) {   // (the cross-check: the server's own frame() on a clock 20x real time)
        uint32_t target = server_->tick_ + (uint32_t)ticks;
        while (server_->tick_ < target) {
            uint32_t t = server_->tick_;
            server_->frame();
            if (server_->tick_ != t) { ran += (int)(server_->tick_ - t); watch(); if (stopAtMatchEnd && matchOver()) break; }
            else sleepMs(0);
        }
    } else {
        for (int k = 0; k < ticks; k++) {
            tickOnce(); watch(); ran++;
            if (stopAtMatchEnd && matchOver()) break;
        }
    }
    readLog();
    t_sim = prevSim;
    return ran;
}

void BotSim::watch() {
    Server& s = *server_;
    const double t = s.time() - t0_;
    for (int i = 0; i < kMaxPlayers; i++) {
        ServerPlayer& p = s.players_[i];
        Prev& pv = prev_[i];
        if (!p.connected) { pv = Prev(); continue; }
        const bool spawned = p.alive && !pv.alive;
        // a placed bot starts where it was put: the first time it's alive in the match (after the forced start: the
        // round start moves everyone to a spawn without a death, so "alive" may never change), and again if it
        // respawns before the round first goes live
        if (started_ && p.alive && s.ms_.phase != PHASE_WARMUP)
            for (size_t k = 0; k < ids_.size(); k++)
                if (ids_[k] == i && specs_[k].placed && placedDone_[k] < 2) {
                    if (placedDone_[k] == 0 || spawned) {
                        p.move.pos = specs_[k].pos;
                        p.move.vel = Vec3();
                        p.yaw = specs_[k].yaw;
                        s.bots_[i].aimYaw = specs_[k].yaw;
                        s.bots_[i].needPath = true;
                        s.bots_[i].lastPos = specs_[k].pos;
                    }
                    placedDone_[k] = s.ms_.phase == PHASE_LIVE ? 2 : 1;
                }
        if (spawned) {
            BotEvent e; e.t = t; e.kind = "spawn"; e.who = i; e.pos = p.move.pos;
            events_.push_back(e);
        }
        if (p.checkpoint != pv.checkpoint && p.checkpoint > pv.checkpoint) {
            BotEvent e; e.t = t; e.kind = "checkpoint"; e.who = i; e.pos = p.move.pos; e.detail = std::to_string(p.checkpoint);
            events_.push_back(e);
        }
        if (p.finished && !pv.finished) { BotEvent e; e.t = t; e.kind = "finish"; e.who = i; e.pos = p.move.pos; events_.push_back(e); }
        pv.alive = p.alive; pv.checkpoint = p.checkpoint; pv.finished = p.finished;
    }
    if (started_)
        for (size_t k = 0; k < ids_.size(); k++)
            if (ids_[k] >= 0 && s.players_[ids_[k]].alive && s.players_[ids_[k]].connected) aliveTicks_[k]++;
    const ModeState& ms = s.ms_;
    if (ms.scoreA != prevScoreA_ || ms.scoreB != prevScoreB_) {
        BotEvent e; e.t = t; e.kind = "score"; e.detail = std::to_string(ms.scoreA) + "-" + std::to_string(ms.scoreB);
        events_.push_back(e);
        prevScoreA_ = ms.scoreA; prevScoreB_ = ms.scoreB;
    }
    if (ms.phase != prevPhase_) {
        BotEvent e; e.t = t; e.kind = "phase"; e.detail = ms.phase < 5 ? kPhaseNames[ms.phase] : "?";
        events_.push_back(e);
        prevPhase_ = ms.phase;
        if (ms.phase == PHASE_MATCH_END && started_ && matchOverAt_ < 0) matchOverAt_ = t;
    }
    if (ms.bombState != prevBomb_) {
        static const char* bs[] = {"none", "carried", "dropped", "planted", "exploded", "defused"};
        BotEvent e; e.t = t; e.kind = "bomb"; e.pos = ms.bombPos; e.detail = ms.bombState < 6 ? bs[ms.bombState] : "?";
        if (ms.bombCarrier < kMaxPlayers) e.who = ms.bombCarrier;
        events_.push_back(e);
        prevBomb_ = ms.bombState;
    }
    if (ms.hillOwner != prevHill_) {
        BotEvent e; e.t = t; e.kind = "hill"; e.detail = ms.hillOwner == TEAM_A ? "a" : ms.hillOwner == TEAM_B ? "b" : "none";
        if (s.map_.hasHill) e.pos = s.map_.hill.center();
        events_.push_back(e);
        prevHill_ = ms.hillOwner;
    }
    if (ms.point != prevPoint_) {   // (the game log says who took it too: "point" events)
        prevPoint_ = ms.point;
    }
}

// "TEAM A wins the match", "TEAM B delivered the payload", "BOT ANNA wins the match", "match draw", ...
void BotSim::parseWinner(const std::string& text) {
    const Server& s = *server_;
    winnerTeam_ = winnerPlayer_ = -1;
    if (text.compare(0, 6, "TEAM A") == 0) { winnerTeam_ = TEAM_A; return; }
    if (text.compare(0, 6, "TEAM B") == 0) { winnerTeam_ = TEAM_B; return; }
    size_t best = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        const ServerPlayer& p = s.players_[i];
        if (!p.connected || p.name.empty() || p.name.size() <= best) continue;
        if (text.compare(0, p.name.size(), p.name) == 0 && text.size() > p.name.size() && text[p.name.size()] == ' ') { best = p.name.size(); winnerPlayer_ = i; }
    }
    if (winnerPlayer_ >= 0 && modeIsTeamBased(s.ms_.mode)) winnerTeam_ = s.players_[winnerPlayer_].team;
}

void BotSim::readLog() {
    fflush(nullptr);   // (the server's game log is a FILE*: what it wrote so far)
    FILE* f = u8fopen(logPath_, "rb");
    if (!f) return;
    if (fseek(f, logPos_, SEEK_SET) != 0) { fclose(f); return; }
    char line[1024];
    Server& s = *server_;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n == 0 || line[n - 1] != '\n') break;   // (a line still being written: next time)
        logPos_ += (long)n;
        line[n - 1] = 0;
        if (n >= 2 && line[n - 2] == '\r') line[n - 2] = 0;   // (Windows: the server writes its log in text mode)
        unsigned long long sec = 0; unsigned ms = 0; int off = 0;
        if (sscanf(line, "%llu.%u %n", &sec, &ms, &off) < 2) continue;
        const char* msg = line + off;
        double t = sec + ms * 0.001 - t0_;
        BotEvent e; e.t = t;
        int a = -1, b = -1, w = -1;
        if (!strncmp(msg, "kill ", 5)) {
            const char* k = strstr(msg, "killer="); const char* v = strstr(msg, "victim="); const char* wp = strstr(msg, "weapon=");
            if (!k || !v || !wp) continue;
            a = atoi(k + 7); b = atoi(v + 7); w = atoi(wp + 7);
            e.kind = "kill"; e.who = a; e.victim = b;
            if (b >= 0 && b < kMaxPlayers) e.pos = s.players_[b].move.pos;
            e.detail = w >= 0 && w < WPN_COUNT ? weaponDef(w).name : w == 252 ? "fell" : a == b ? "self" : "world";
        } else if (sscanf(msg, "point %d taken by team %d", &a, &b) == 2) {
            e.kind = "point"; e.detail = std::string("point ") + char('A' + std::max(0, a - 1)) + " taken by team " + (b == TEAM_B ? "b" : "a");
            if (a >= 1 && a <= (int)s.map_.points.size()) e.pos = s.map_.points[a - 1].center();
        } else if (sscanf(msg, "tag %d -> %d", &a, &b) == 2) { e.kind = "tag"; e.who = a; e.victim = b; if (b >= 0 && b < kMaxPlayers) e.pos = s.players_[b].move.pos; }
        else if (!strncmp(msg, "round ", 6) && strstr(msg, " end: ")) { e.kind = "round"; e.detail = msg; }
        else if (!strncmp(msg, "match end: ", 11)) {
            e.kind = "match"; e.detail = msg + 11;
            if (started_) { matchText_ = e.detail; parseWinner(matchText_); matchesEnded_++; if (matchOverAt_ < 0) matchOverAt_ = t; }   // (the latest match's)
        }
        else if (!strcmp(msg, "overtime")) e.kind = "overtime";
        else if (!strncmp(msg, "round ", 6) && strstr(msg, " modifier ")) { e.kind = "modifier"; e.detail = msg; }
        else continue;
        events_.push_back(e);
    }
    fclose(f);
}

std::vector<BotEvent> BotSim::takeEvents() {
    std::vector<BotEvent> out;
    out.swap(events_);
    std::stable_sort(out.begin(), out.end(), [](const BotEvent& x, const BotEvent& y) { return x.t < y.t; });
    return out;
}

// ---- objective overrides ---------------------------------------------------------------------------------------
// The game has no "go here" input for a bot, and this tool doesn't edit the game. So for one think of an override bot
// (BotBrain::script, set by tickOnce, calls think(), which calls the real botThink) the things bots.cpp's objective()
// reads are pointed at the override's target, then put back before the bot's command runs: BotBrain::goal (free for
// all, team vs, duel, hide and seek, tag: the modes that roam), the hill, the point under attack, the bot's next race
// checkpoint, the bomb sites / dropped or planted bomb, the payload track. The bot then walks there with all of its
// own movement (routes, jumps, ladders, grapple) and fights as usual. Once there it stands (its keys are let go).
// Limits: SIM.md "Objective overrides".
namespace {
constexpr float kArriveGoto = 2.0f;    // goto / route points: reached within this (xz; bots.cpp's own roam goals: 2 m)
constexpr float kHoldRadius = 4.0f;    // hold: walks back once pushed farther than this
constexpr float kDefendRadius = 6.0f;
constexpr float kArriveY = 2.5f;       // ... and this high above / below
constexpr double kStuckTime = 6.0;     // no metre of headway in this long (not fighting): stuck...
constexpr double kDetourTime = 4.0;    // ... so it roams on its own this long, then heads for the target again

int nearestSpawn(const Map& map, const Vec3& from, int team, bool wantTeam) {
    int best = -1; float bd = 1e30f;
    for (size_t k = 0; k < map.spawns.size(); k++) {
        int t = map.spawns[k].team;
        if (wantTeam) {
            int side = isStagedSpawn(t) ? -1 : t;
            if (side != team) continue;
        }
        float d = length(map.spawns[k].pos - from);
        if (d < bd) { bd = d; best = (int)k; }
    }
    return best;
}
}  // namespace

// The override's target for bot k now. False: none (play natively). arrive: stands within this (0: never: the
// patched objective already makes it stop there). useBox / boxKind / boxIndex: patch with a real objective's box
// (0 none, 1 hill, 2 point, 3 checkpoint, 4 bomb site) instead of a box around the target.
bool BotSim::overrideTarget(size_t k, int id, Vec3& at, float& arrive, bool& useBox, int& boxKind, int& boxIndex) {
    Server& s = *server_;
    const BotOverride& o = specs_[k].override_;
    Ov& st = ov_[k];
    const ServerPlayer& me = s.players_[id];
    const Map& map = s.map_;
    const ModeState& ms = s.ms_;
    useBox = false; boxKind = 0; boxIndex = -1;
    auto isNear = [&](const Vec3& p, float r) { float dx = me.move.pos.x - p.x, dz = me.move.pos.z - p.z; return dx * dx + dz * dz < r * r && std::fabs(me.move.pos.y - p.y) < kArriveY; };
    switch (o.kind) {
        case BotOverride::GOTO:
            at = o.pos; arrive = o.radius > 0 ? o.radius : kArriveGoto;
            return true;
        case BotOverride::HOLD: {
            at = o.pos;
            float r = o.radius > 0 ? o.radius : kHoldRadius;
            // there: stays while within the radius; pushed out of it: walks back to the middle
            if (st.arrived && !isNear(at, r)) st.arrived = false;
            arrive = st.arrived ? std::max(r, kArriveGoto) : kArriveGoto;
            return true;
        }
        case BotOverride::ROUTE: {
            if (o.points.empty()) return false;
            int n = (int)o.points.size();
            if (st.routeIdx >= n) st.routeIdx = o.loop ? 0 : n - 1;
            if (isNear(o.points[st.routeIdx], kArriveGoto) && (st.routeIdx < n - 1 || o.loop)) st.routeIdx = (st.routeIdx + 1) % n;
            at = o.points[st.routeIdx];
            arrive = !o.loop && st.routeIdx == n - 1 ? kArriveGoto : 0;
            return true;
        }
        case BotOverride::ATTACK:
        case BotOverride::DEFEND: {
            const bool att = o.kind == BotOverride::ATTACK;
            const int oi = o.objectiveIndex;
            switch (ms.mode) {
                case MODE_KOTH:
                    if (!map.hasHill) break;
                    at = {map.hill.center().x, map.hill.mn.y, map.hill.center().z}; arrive = 0; useBox = true; boxKind = 1;
                    return true;
                case MODE_POINTS: {
                    if (map.points.empty()) break;
                    int i = oi >= 0 ? std::min(oi, (int)map.points.size() - 1) : std::min((int)ms.point, (int)map.points.size() - 1);
                    const AABB& p = map.points[i];
                    at = {p.center().x, p.mn.y, p.center().z}; arrive = 0; useBox = true; boxKind = 2; boxIndex = i;
                    return true;
                }
                case MODE_RACE: {
                    if (map.checkpoints.empty()) break;
                    int i = oi >= 0 ? std::min(oi, (int)map.checkpoints.size() - 1) : std::min(me.checkpoint, (int)map.checkpoints.size() - 1);
                    at = map.checkpoints[i].center(); arrive = 0; useBox = true; boxKind = 3; boxIndex = i;
                    return true;
                }
                case MODE_BOMB: {
                    if (map.bombSites.empty()) break;
                    if (ms.bombState == BOMB_PLANTED || (ms.bombState == BOMB_DROPPED && att)) return false;   // (the bomb: as the bot would)
                    int i = oi >= 0 ? oi % (int)map.bombSites.size() : att ? 0 : id % (int)map.bombSites.size();
                    at = map.bombSites[i].center(); arrive = att ? 0 : kDefendRadius; useBox = true; boxKind = 4; boxIndex = i;
                    return true;
                }
                case MODE_PAYLOAD:
                case MODE_NPAYLOAD: {
                    if (map.payloadPath.size() < 2) break;
                    if (att) return false;   // (pushing the cart: as the bot would)
                    float d = std::min(ms.payloadDist + 10.0f, std::max(0.0f, ms.payloadLen));
                    at = map.cartPoint(ms.mode, ms.attackers, d); arrive = kDefendRadius;
                    return true;
                }
                case MODE_HIDE:
                case MODE_TAG:
                    return false;   // (nothing to attack or defend: as the bot would)
                default: break;
            }
            // no objective: the enemy's spawns (attack), its own (defend); picked once, from where the bot first was
            if (map.spawns.empty()) return false;
            if (!st.resolved) {
                bool teams = modeIsTeamBased(ms.mode);
                int team = att ? (me.team == TEAM_A ? TEAM_B : TEAM_A) : me.team;
                int i = -1;
                if (oi >= 0) {
                    std::vector<int> c;
                    for (size_t j = 0; j < map.spawns.size(); j++) if (!teams || map.spawns[j].team == team) c.push_back((int)j);
                    if (c.empty()) for (size_t j = 0; j < map.spawns.size(); j++) c.push_back((int)j);
                    i = c[oi % c.size()];
                } else {
                    i = teams ? nearestSpawn(map, me.move.pos, team, true) : -1;
                    if (i < 0) {   // free for all: attack the farthest spawn, defend the nearest
                        float bd = att ? -1 : 1e30f;
                        for (size_t j = 0; j < map.spawns.size(); j++) {
                            float d = length(map.spawns[j].pos - me.move.pos);
                            if (att ? d > bd : d < bd) { bd = d; i = (int)j; }
                        }
                    }
                }
                st.target = map.spawns[i].pos; st.resolved = true;
            }
            at = st.target; arrive = att ? kArriveGoto : kDefendRadius;
            return true;
        }
        default: return false;
    }
    return false;   // (an objective mode whose map lacks the objective)
}

UserCmd BotSim::think(int id) {
    Server& s = *server_;
    BotBrain& b = s.bots_[id];
    b.script = nullptr;   // (botThink below runs the bot's own code; tickOnce hooks it again next tick)
    const int k = id >= 0 && id < (int)ovOf_.size() ? ovOf_[id] : -1;
    ServerPlayer& me = s.players_[id];
    if (k < 0) return botThink(s, id);
    Ov& st = ov_[k];
    const BotOverride& o = specs_[k].override_;
    if (!me.alive) { st.arrived = false; st.active = false; if (o.kind == BotOverride::ROUTE) st.routeIdx = 0; st.resolved = false; st.bestAt = st.detourUntil = -1; return botThink(s, id); }
    const double now = s.time();
    if (!o.chase) {   // only what it sees: no hunting, no leads, no sounds
        for (int i = 0; i < kMaxPlayers; i++)
            if (!b.aware[i].visible) { b.aware[i].seenAt = std::min(b.aware[i].seenAt, now - 1e6); b.aware[i].heardAt = -1e9; }
        if (b.target >= 0 && !b.aware[b.target].visible) { b.target = -1; b.engagedAt = -1; }
    }
    Vec3 T; float arrive = 0; bool useBox = false; int boxKind = 0, boxIndex = -1;
    if (!overrideTarget((size_t)k, id, T, arrive, useBox, boxKind, boxIndex)) { st.active = false; return botThink(s, id); }
    st.active = true;
    st.target = T;
    // headway: closer by a metre within kStuckTime (fighting doesn't count), else a detour of kDetourTime
    {
        const bool fightingNow = b.target >= 0 && b.target < kMaxPlayers && b.aware[b.target].visible;
        float d = length(me.move.pos - T);
        if (length(st.bestFor - T) > 0.5f || st.bestAt < 0) { st.bestFor = T; st.bestDist = d; st.bestAt = now; }
        const bool canMove = s.ms_.phase == PHASE_LIVE || s.ms_.phase == PHASE_WARMUP;   // (frozen: no headway to make)
        if (d < st.bestDist - 1.0f || fightingNow || !canMove || st.arrived || (arrive > 0 && d < arrive + 1.0f)) { st.bestDist = std::min(st.bestDist, d); st.bestAt = now; }
        if (now < st.detourUntil) return botThink(s, id);   // (roaming on its own meanwhile)
        if (now - st.bestAt > kStuckTime) { st.detourUntil = now + kDetourTime; st.bestAt = st.detourUntil; st.bestDist = 1e30f; b.goalUntil = 0; return botThink(s, id); }
    }
    Map& map = s.map_;
    ModeState& ms = s.ms_;
    // the roaming modes' goal (and the map / nav graph it's on, or botThink drops it as stale)
    b.goal = T; b.goalUntil = now + 1e6;
    b.goalMapId = map.id();
    if (!s.nav_.nodes.empty() && s.nav_.mapId == map.id()) b.goalNavGen = s.nav_.generation;
    // the objective modes: what objective() reads, pointed at T for this think
    const float r = std::max(0.75f, arrive > 0 ? std::min(arrive, 3.0f) : 1.0f);
    const AABB floorBox{T - Vec3(r, 0, r), T + Vec3(r, 2.5f, r)};
    const AABB midBox{T - Vec3(r, 1.0f, r), T + Vec3(r, 1.0f, r)};
    AABB savedBox; int patched = 0;   // 1 hill, 2 point, 3 checkpoint
    std::vector<AABB>& sites = sitesTmp_;
    std::vector<Vec3>& track = trackTmp_;
    bool sitesSwapped = false, trackSwapped = false, bombPosSet = false, bombStateSet = false;
    Vec3 savedBombPos; uint8_t savedBombState = 0;
    switch (ms.mode) {
        case MODE_KOTH:
            if (map.hasHill) { savedBox = map.hill; map.hill = useBox && boxKind == 1 ? map.hill : floorBox; patched = 1; }
            break;
        case MODE_POINTS:
            if (ms.point < map.points.size()) {
                savedBox = map.points[ms.point];
                map.points[ms.point] = useBox && boxKind == 2 ? map.points[boxIndex] : floorBox;
                patched = 2;
            }
            break;
        case MODE_RACE:
            if (me.checkpoint >= 0 && me.checkpoint < (int)map.checkpoints.size()) {
                savedBox = map.checkpoints[me.checkpoint];
                map.checkpoints[me.checkpoint] = useBox && boxKind == 3 ? map.checkpoints[boxIndex] : midBox;
                patched = 3;
            }
            break;
        case MODE_BOMB:
            if (!map.bombSites.empty()) {
                sites.clear();
                sites.push_back(useBox && boxKind == 4 ? map.bombSites[boxIndex] : midBox);
                std::swap(sites, map.bombSites); sitesSwapped = true;
                bool att = me.team == ms.attackers;
                if (ms.bombState == BOMB_PLANTED || (ms.bombState == BOMB_DROPPED && att)) { savedBombPos = ms.bombPos; ms.bombPos = T; bombPosSet = true; }
                else if (ms.bombState == BOMB_CARRIED && att && ms.bombCarrier != id) { savedBombState = ms.bombState; ms.bombState = BOMB_NONE; bombStateSet = true; }
            }
            break;
        case MODE_PAYLOAD:
        case MODE_NPAYLOAD:
            if (map.payloadPath.size() >= 2) { track.assign({T, T}); std::swap(track, map.payloadPath); trackSwapped = true; }
            break;
        default: break;
    }
    UserCmd c = botThink(s, id);
    if (patched == 1) map.hill = savedBox;
    else if (patched == 2) map.points[ms.point] = savedBox;
    else if (patched == 3) map.checkpoints[me.checkpoint] = savedBox;
    if (sitesSwapped) std::swap(sites, map.bombSites);
    if (trackSwapped) std::swap(track, map.payloadPath);
    if (bombPosSet) ms.bombPos = savedBombPos;
    if (bombStateSet) ms.bombState = savedBombState;
    // there: stand (still aiming, shooting, using), unless fighting or on a grapple
    const bool fighting = b.target >= 0 && b.target < kMaxPlayers && b.aware[b.target].visible;
    float dx = me.move.pos.x - T.x, dz = me.move.pos.z - T.z;
    bool inside = arrive > 0 && dx * dx + dz * dz < arrive * arrive && std::fabs(me.move.pos.y - T.y) < kArriveY;
    if (o.kind == BotOverride::HOLD && inside) st.arrived = true;
    else if (o.kind != BotOverride::HOLD) st.arrived = inside;
    if (inside && !fighting && !(me.move.flags & MF_HOOKED)) {
        c.forward = 0; c.side = 0;
        c.buttons &= (uint16_t)~(BTN_JUMP | BTN_DASH | BTN_GRAPPLE);
    }
    return c;
}

// ---- reading it back ---------------------------------------------------------------------------------------------
namespace {
// What chose the bot's behaviour, from its state (bots.cpp's own branches, read from outside)
std::string decisionOf(const Server& s, int id, const BotBrain& br, const ServerPlayer& p) {
    if (!p.alive || !p.connected) return "dead";
    const double now = s.time();
    const ModeState& m = s.ms_;
    const Map& map = s.map_;
    bool urgent = m.mode == MODE_BOMB && ((m.bombState == BOMB_CARRIED && m.bombCarrier == id) || (m.bombState == BOMB_PLANTED && p.team != m.attackers));
    if (br.target >= 0 && br.target < kMaxPlayers) {
        const BotAwareness& a = br.aware[br.target];
        if (a.visible && now < a.reactedAt) return "react";
        if (a.visible) return urgent ? "fight+objective" : "fight";
        return urgent ? "hunt+objective" : "hunt";
    }
    switch (m.mode) {
        case MODE_RACE: if (p.checkpoint < (int)map.checkpoints.size()) return "objective:checkpoint"; break;
        case MODE_KOTH: if (map.hasHill) return "objective:hill"; break;
        case MODE_PAYLOAD: case MODE_NPAYLOAD: if (map.payloadPath.size() >= 2) return "objective:cart"; break;
        case MODE_POINTS: if (m.point < map.points.size()) return "objective:point"; break;
        case MODE_BOMB: {
            if (map.bombSites.empty()) break;
            bool att = p.team == m.attackers;
            if (m.bombState == BOMB_PLANTED) return att ? "objective:guard bomb" : "objective:defuse";
            if (m.bombState == BOMB_DROPPED && att) return "objective:pick up bomb";
            if (att && m.bombState == BOMB_CARRIED && m.bombCarrier == id) return "objective:plant";
            if (att && m.bombState == BOMB_CARRIED && m.bombCarrier < kMaxPlayers && s.players_[m.bombCarrier].alive) return "objective:escort";
            return att ? "objective:site" : "objective:guard site";
        }
        case MODE_HIDE: case MODE_TAG:
            if (p.it) return "seek";
            if (now < br.fleeUntil) return "flee";
            return m.mode == MODE_HIDE ? "hide" : "roam";
        default: break;
    }
    // no objective: a lead (someone recently seen or heard) when the route goes elsewhere than the roam goal
    if (length(br.pathGoal - br.goal) > 1.0f) return "lead";
    return "roam";
}
}  // namespace

void BotSim::sample(std::vector<BotState>& out) const {
    out.clear();
    if (!server_) return;
    const Server& s = *server_;
    const double dtTick = 1.0 / std::max(1, s.tickRate());
    for (size_t k = 0; k < ids_.size(); k++) {
        int i = ids_[k];
        if (i < 0) continue;
        const ServerPlayer& p = s.players_[i];
        const BotBrain& br = s.bots_[i];
        BotState b;
        b.id = i; b.name = p.name; b.team = p.team; b.skill = br.skill;
        b.alive = p.alive && p.connected;
        b.pos = p.move.pos; b.vel = p.move.vel; b.yaw = p.yaw; b.pitch = p.pitch;
        b.health = p.health; b.weapon = p.weapon;
        b.kills = p.kills; b.deaths = p.deaths; b.score = p.score;
        b.onGround = (p.move.flags & MF_ONGROUND) != 0;
        b.hooked = (p.move.flags & MF_HOOKED) != 0;
        b.crouched = (p.move.flags & MF_CROUCHED) != 0;
        b.target = br.target;
        b.goal = br.goal;
        b.dest = br.pathGoal;
        b.waypoint = br.waypoint;
        if (!b.alive) b.state = "dead";
        else if (br.target >= 0 && br.target < kMaxPlayers && br.aware[br.target].visible) b.state = "fight";
        else if (b.hooked) b.state = "hook";
        else if (br.target >= 0) b.state = "hunt";
        else b.state = "move";
        if (br.pathGen == s.nav_.generation)
            for (int n : br.path)
                if (n >= 0 && n < (int)s.nav_.nodes.size()) { auto& nd = s.nav_.nodes[n]; b.path.push_back({(nd.x0 + nd.x1) * 0.5f, nd.y, (nd.z0 + nd.z1) * 0.5f}); }
        b.decision = decisionOf(s, i, br, p);
        if (k < ov_.size() && ov_[k].active && b.alive && b.decision.compare(0, 5, "fight") != 0 && b.decision != "react") {
            static const char* kinds[] = {"", "goto", "hold", "route", "attack", "defend"};
            int kind = specs_[k].override_.kind;
            b.decision = std::string("override:") + (kind > 0 && kind < 6 ? kinds[kind] : "?") +
                         (s.time() < ov_[k].detourUntil ? " (stuck: detour)" : ov_[k].arrived ? " (there)" : "");
            b.dest = ov_[k].target;
        }
        b.damage = p.dmgDealt; b.objTime = p.objTime; b.shots = p.shotsFired; b.hits = p.shotsHit;
        b.aliveTime = k < aliveTicks_.size() ? aliveTicks_[k] * dtTick : 0;
        out.push_back(std::move(b));
    }
}

Json BotSim::summary() const {
    Json o = Json::object();
    if (!server_) return o;
    const Server& s = *server_;
    const ModeState& ms = s.ms_;
    Json sc = Json::array(); sc.push((int)ms.scoreA); sc.push((int)ms.scoreB);
    o.set("score", sc);
    o.set("phase", ms.phase < 5 ? kPhaseNames[ms.phase] : "?");
    o.set("round", (int)ms.round);
    o.set("timeLeft", ms.timeLeft);
    if (s.map_.payloadPath.size() >= 2) { o.set("payloadDist", ms.payloadDist); o.set("payloadLen", ms.payloadLen); }
    if (!s.map_.points.empty()) o.set("pointsTaken", (int)ms.point);
    if (s.map_.hasHill) o.set("hillOwner", ms.hillOwner == TEAM_A ? Json("a") : ms.hillOwner == TEAM_B ? Json("b") : Json());
    Json pl = Json::array();
    for (int i = 0; i < kMaxPlayers; i++) {
        const ServerPlayer& p = s.players_[i];
        if (!p.connected) continue;
        Json x = Json::object();
        x.set("id", i); x.set("name", p.name); x.set("team", p.team == TEAM_B ? "b" : p.team == TEAM_A ? "a" : "spectator");
        x.set("kills", p.kills); x.set("deaths", p.deaths); x.set("score", p.score);
        if (!s.map_.checkpoints.empty()) { x.set("checkpoint", p.checkpoint); x.set("finished", p.finished); }
        pl.push(std::move(x));
    }
    o.set("players", std::move(pl));
    o.set("tickRate", s.tickRate());
    o.set("map", s.cfg_.map);
    return o;
}
