// BotSim (botsim.h): the game's server and bots, stepped headless.
#include "botsim.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "common/net.h"
#include "common/u8file.h"
#include "common/rng.h"
#include "game/bots.h"
#include "game/gamedefs.h"
#include "game/gamemodes.h"
#include "game/map.h"
#include "game/server.h"
#include "maptool.h"

// Server::simulate and Server::sendSnapshots are private (frame() runs them on the real-time clock). An explicit
// instantiation may name a private member (C++17 [temp.explicit]/12), which hands their addresses over here: the tick
// loop below is frame()'s own, minus the sockets and the clock. DEDUPE LATER: a public Server::stepTick() in src/game.
namespace {
template <class Tag, typename Tag::type M> struct Rob { friend typename Tag::type get(Tag) { return M; } };
struct SimTag { using type = void (Server::*)(); friend type get(SimTag); };
struct SnapTag { using type = void (Server::*)(); friend type get(SnapTag); };
template struct Rob<SimTag, &Server::simulate>;
template struct Rob<SnapTag, &Server::sendSnapshots>;
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

bool BotSim::start(const BotSimConfig& cfg, std::string* err) {
    stop();
    namespace fs = std::filesystem;
    std::error_code ec;
    // a data folder of its own (the map as edited, the game log): never the player's real one
    static int serial = 0;
    fs::path dir = fs::temp_directory_path(ec) / ("quark_maptool_" + std::to_string((long long)(maptoolNow() * 1000)) + "_" + std::to_string(serial++));
    fs::create_directories(dir / "maps", ec);
    if (ec) { if (err) *err = "can't make a temp folder: " + ec.message(); return false; }
    dataDir_ = dir.u8string();   // (UTF-8 like every path the game keeps: %TEMP% under a non-ASCII user name)
    setUserDataDir(dataDir_);
    mapName_ = cfg.mapName;
    if (!cfg.mapText.empty()) {
        mapName_ = "zz_maptool";
        if (!saveMapFile((dir / "maps" / "zz_maptool.qmap").u8string(), cfg.mapText)) { if (err) *err = "can't write the map"; return false; }
    }
    logPath_ = (dir / "game.log").u8string();
    logPos_ = 0;
    g_serverRateCheck = false;   // (no real-time clock to keep up with: never step the rate down)
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
    std::string e;
    if (!s.start(sc, &e)) { server_.reset(); if (err) *err = "server: " + e; return false; }
    // the nav graph builds in the background: wait for it (bots without it stand still)
    for (int k = 0; k < 20000 && !s.nav_.ready(s.map_); k++) sleepMs(1);
    specs_ = cfg.bots;
    ids_.clear();
    for (auto& b : specs_) {
        int id = s.addBot(b.team, std::clamp(b.skill, 0, BOT_SKILLS - 1));
        ids_.push_back(id);
        if (id >= 0 && !b.name.empty()) s.players_[id].name = b.name.substr(0, 24);
    }
    placedDone_.assign(ids_.size(), 0);
    prev_.assign(kMaxPlayers, Prev());
    events_.clear();
    t0_ = 0;
    if (cfg.forceStart) {
        step(ticksFor(cfg.warmup));
        s.command("forcestart");
    }
    t0_ = s.time();
    prevPhase_ = s.ms_.phase;
    return true;
}

void BotSim::stop() {
    if (!server_) return;
    server_->stop();
    server_.reset();
    std::error_code ec;
    if (!dataDir_.empty()) std::filesystem::remove_all(utf8Path(dataDir_), ec);
    dataDir_.clear();
}

int BotSim::tickRate() const { return server_ ? server_->tickRate() : 125; }
int BotSim::ticksFor(double seconds) const { return std::max(1, (int)std::lround(seconds * tickRate())); }
double BotSim::time() const { return server_ ? server_->time() - t0_ : 0; }
bool BotSim::mapChanged() const { return server_ && server_->cfg_.map != mapName_; }

void BotSim::tickOnce() {
    Server& s = *server_;
    // frame()'s tick: simulate, the tick count and clock, the snapshot pass (no clients: bookkeeping only)
    (s.*get(SimTag()))();
    s.tick_++;
    s.timeMs_ += 1000 / s.tickRate();
    if (s.tick_ % s.snapEvery() == 0) (s.*get(SnapTag()))();
}

void BotSim::step(int ticks) {
    if (!server_) return;
    if (viaFrame_) {   // (the cross-check: the server's own frame() on a clock 20x real time)
        uint32_t target = server_->tick_ + (uint32_t)ticks;
        while (server_->tick_ < target) { uint32_t t = server_->tick_; server_->frame(); if (server_->tick_ != t) watch(); else sleepMs(0); }
        readLog();
        return;
    }
    for (int k = 0; k < ticks; k++) { tickOnce(); watch(); }
    readLog();
}

void BotSim::watch() {
    Server& s = *server_;
    const double t = s.time() - t0_;
    for (int i = 0; i < kMaxPlayers; i++) {
        ServerPlayer& p = s.players_[i];
        Prev& pv = prev_[i];
        if (!p.connected) { pv = Prev(); continue; }
        if (p.alive && !pv.alive) {
            // a placed bot starts where it was put, the first time it's alive in the match
            for (size_t k = 0; k < ids_.size(); k++)
                if (ids_[k] == i && specs_[k].placed && !placedDone_[k] && s.ms_.phase != PHASE_WARMUP) {
                    p.move.pos = specs_[k].pos;
                    p.move.vel = Vec3();
                    p.yaw = specs_[k].yaw;
                    s.bots_[i].aimYaw = specs_[k].yaw;
                    s.bots_[i].needPath = true;
                    s.bots_[i].lastPos = specs_[k].pos;
                    placedDone_[k] = 1;
                }
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
    const ModeState& ms = s.ms_;
    if (ms.scoreA != prevScoreA_ || ms.scoreB != prevScoreB_) {
        BotEvent e; e.t = t; e.kind = "score"; e.detail = std::to_string(ms.scoreA) + "-" + std::to_string(ms.scoreB);
        events_.push_back(e);
        prevScoreA_ = ms.scoreA; prevScoreB_ = ms.scoreB;
    }
    if (ms.phase != prevPhase_) {
        static const char* ph[] = {"warmup", "freeze", "live", "round end", "match end"};
        BotEvent e; e.t = t; e.kind = "phase"; e.detail = ms.phase < 5 ? ph[ms.phase] : "?";
        events_.push_back(e);
        prevPhase_ = ms.phase;
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
        else if (!strncmp(msg, "match end: ", 11)) { e.kind = "match"; e.detail = msg + 11; }
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

void BotSim::sample(std::vector<BotState>& out) const {
    out.clear();
    if (!server_) return;
    const Server& s = *server_;
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
        b.waypoint = br.waypoint;
        if (!b.alive) b.state = "dead";
        else if (br.target >= 0 && br.target < kMaxPlayers && br.aware[br.target].visible) b.state = "fight";
        else if (b.hooked) b.state = "hook";
        else if (br.target >= 0) b.state = "hunt";
        else b.state = "move";
        if (br.pathGen == s.nav_.generation)
            for (int n : br.path)
                if (n >= 0 && n < (int)s.nav_.nodes.size()) { auto& nd = s.nav_.nodes[n]; b.path.push_back({(nd.x0 + nd.x1) * 0.5f, nd.y, (nd.z0 + nd.z1) * 0.5f}); }
        out.push_back(std::move(b));
    }
}

Json BotSim::summary() const {
    Json o = Json::object();
    if (!server_) return o;
    const Server& s = *server_;
    const ModeState& ms = s.ms_;
    static const char* ph[] = {"warmup", "freeze", "live", "round end", "match end"};
    Json sc = Json::array(); sc.push((int)ms.scoreA); sc.push((int)ms.scoreB);
    o.set("score", sc);
    o.set("phase", ms.phase < 5 ? ph[ms.phase] : "?");
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
