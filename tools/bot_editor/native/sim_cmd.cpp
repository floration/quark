// sim / simbatch: the bot editor's simulator (SIM.md). A headless match with the game's own server and bots (BotSim,
// tools/map_editor/native/botsim.h), with per-bot brains and objective overrides; `sim` streams it as NDJSON for the
// page to watch (paced or as fast as it goes, stdin controls), `simbatch` runs N seeded matches on every core and
// reports per bot, per brain and per match.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include "botsim.h"
#include "maptool.h"
#include "walkfast.h"
#include "common/net.h"
#include "game/bots.h"
#include "game/gamedefs.h"
#include "game/gamemodes.h"
#include "game/map.h"
#include "game/server.h"

namespace {
namespace fs = std::filesystem;

std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
int skillFromJson(const Json& j, int def) {
    if (j.isNum()) return std::clamp(j.i(), 0, BOT_SKILLS - 1);
    if (j.isStr()) for (int k = 0; k < BOT_SKILLS; k++) if (lower(botSkillName(k)) == lower(j.str())) return k;
    return def;
}
const char* teamStr(int t) { return t == TEAM_B ? "b" : t == TEAM_A ? "a" : "spectator"; }
const char* kOverrideNames[] = {"default", "goto", "hold", "route", "attack", "defend"};
bool readText(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
bool isFile(const std::string& p) { std::error_code ec; return !p.empty() && fs::is_regular_file(p, ec); }

// ---- the request ------------------------------------------------------------------------------------------------
struct Setup {
    BotSimConfig cfg;
    std::string mapLabel;
    double seconds = 120;
    bool untilMatchEnd = true;     // until: "matchEnd" (default) | "seconds"
    double rate = 30;              // frames per game second (sim)
    bool realtime = true;          // sim: paced at speed x real time
    double speed = 1;
    std::vector<Json> botEcho;     // per bot: what start lines report about it
    std::vector<std::string> warnings;
};

// "map": a name or a path, the page's {name, source, text}, or the map JSON; or text / file / name at the top level
bool loadMap(const Json& req, Map& m, std::string& err) {
    const Json& mj = req["map"];
    Json r = Json::object();
    if (mj.isStr()) { if (isFile(mj.str())) r.set("file", mj.str()); else r.set("name", mj.str()); }
    else if (mj.isObj() && mj["text"].isStr()) { r.set("text", mj["text"]); if (mj["name"].isStr()) r.set("mapName", mj["name"]); }
    else if (mj.isObj() && mj["file"].isStr()) r.set("file", mj["file"]);
    else if (mj.isObj() && mj["name"].isStr() && !mj.has("brushes")) r.set("name", mj["name"]);
    else return loadRequestMap(req, m, err);
    if (req["mapName"].isStr()) r.set("mapName", req["mapName"]);
    return loadRequestMap(r, m, err);
}

// A brain's text: the request's own "brains" {name: text}, a file, the player's bots folder, the repo's tools/bots.
// (Before BotSim points the game's user data folder at its temp folder: userDataPath here is the player's.)
bool resolveBrain(const Json& req, const std::string& brain, std::string& text, std::string& err) {
    text.clear();
    if (brain.empty() || brain == "default") return true;
    const Json& inl = req["brains"][brain];
    if (inl.isStr()) { text = inl.str(); return true; }
    if (inl.isObj() && inl["text"].isStr()) { text = inl["text"].str(); return true; }
    std::vector<std::string> tries = {brain};
    if (brain.find('/') == std::string::npos && brain.find('\\') == std::string::npos) {
        std::string f = brain.size() > 7 && brain.compare(brain.size() - 7, 7, ".qbrain") == 0 ? brain : brain + ".qbrain";
        tries.push_back(userDataPath("bots") + "/" + f);
        tries.push_back("tools/bots/" + f);
        tries.push_back(findDataPath("maps") + "/../tools/bots/" + f);
    }
    for (auto& p : tries) if (isFile(p) && readText(p, text)) return true;
    err = "brain '" + brain + "' not found (give its text in \"brains\", a path, or a name in the bots folder)";
    return false;
}

bool parseOverride(const Json& j, BotOverride& o, std::string& err) {
    o = BotOverride();
    if (j.isNull()) return true;
    if (j.isStr()) { if (j.str() == "default") return true; err = "objective: an object {type, ...}"; return false; }
    std::string t = lower(j["type"].str(j["kind"].str("default")));
    if (t == "objective") t = lower(j["role"].str("attack"));
    if (t == "follow") t = "route";
    o.radius = std::max(0.0f, j["radius"].f(0));
    o.loop = j["loop"].boolean(false);
    o.chase = j["chase"].boolean(true);
    o.objectiveIndex = j["objectiveIndex"].isNum() ? j["objectiveIndex"].i() : j["index"].isNum() ? j["index"].i() : j["target"]["index"].isNum() ? j["target"]["index"].i() : -1;
    if (t == "default" || t.empty()) return true;
    if (t == "goto" || t == "hold") {
        if (!j["pos"].isArr()) { err = "objective " + t + " needs pos [x,y,z]"; return false; }
        o.kind = t == "goto" ? BotOverride::GOTO : BotOverride::HOLD;
        o.pos = toVec(j["pos"]);
        return true;
    }
    if (t == "route") {
        for (auto& p : j["points"].items()) if (p.isArr()) o.points.push_back(toVec(p));
        if (o.points.empty()) { err = "objective route needs points [[x,y,z], ...]"; return false; }
        o.kind = BotOverride::ROUTE;
        return true;
    }
    if (t == "attack") { o.kind = BotOverride::ATTACK; return true; }
    if (t == "defend") { o.kind = BotOverride::DEFEND; return true; }
    err = "objective type '" + t + "': default, goto, hold, route, attack or defend";
    return false;
}

Json overrideJson(const BotOverride& o) {
    Json j = Json::object();
    j.set("type", kOverrideNames[std::clamp(o.kind, 0, 5)]);
    if (o.kind == BotOverride::GOTO || o.kind == BotOverride::HOLD) j.set("pos", jvec(o.pos));
    if (o.radius > 0) j.set("radius", o.radius);
    if (o.kind == BotOverride::ROUTE) { Json p = Json::array(); for (auto& v : o.points) p.push(jvec(v)); j.set("points", p); j.set("loop", o.loop); }
    if (o.objectiveIndex >= 0) j.set("objectiveIndex", o.objectiveIndex);
    if (!o.chase) j.set("chase", false);
    return j;
}

bool parseSetup(const Json& req, Setup& S, bool batch, std::string& err) {
    Map m;
    if (!loadMap(req, m, err)) return false;
    BotSimConfig& cfg = S.cfg;
    S.mapLabel = m.name;
    cfg.mapText = m.sourceText();
    cfg.mode = modeFromJson(req["mode"], m.preferredMode >= 0 ? m.preferredMode : MODE_FFA);
    cfg.tickRate = req["tickRate"].i(125);
    if (cfg.tickRate != 1000 && cfg.tickRate != 500 && cfg.tickRate != 250 && cfg.tickRate != 125) { err = "tickRate is 1000, 500, 250 or 125"; return false; }
    cfg.seed = (uint32_t)req["seed"].num(1);
    cfg.timeLimit = std::max(0, req["timeLimit"].i(0));
    cfg.scoreLimit = std::max(0, req["scoreLimit"].i(0));
    cfg.applyBrains = true;
    S.seconds = std::clamp(req["seconds"].num(req["secs"].num(batch ? 900 : 120)), 0.1, 7200.0);
    std::string until = lower(req["until"].str("matchend"));
    if (until != "matchend" && until != "match" && until != "seconds" && until != "time") { err = "until is \"matchEnd\" or \"seconds\""; return false; }
    S.untilMatchEnd = until == "matchend" || until == "match";
    S.rate = std::clamp(req["rate"].num(30), 0.5, 1000.0);
    S.realtime = req["realtime"].boolean(true);
    S.speed = std::max(0.0, req["speed"].num(1));
    const int skill = skillFromJson(req["skill"], BOT_ADVANCED);
    const bool teams = modeIsTeamBased(cfg.mode);
    int n = 0;
    auto addSpec = [&](const Json& p) -> bool {
        BotSpec b;
        std::string team = lower(p["team"].str(""));
        b.team = team == "b" ? TEAM_B : team == "a" ? TEAM_A : (teams && (n % 2)) ? TEAM_B : TEAM_A;
        b.skill = skillFromJson(p["skill"], skill);
        if (p["pos"].isArr()) {
            b.placed = true; b.pos = toVec(p["pos"]);
            b.yaw = p["yawDeg"].isNum() ? (float)(p["yawDeg"].num() * PI / 180.0) : p["yaw"].f(0);
        }
        b.name = p["name"].str();
        b.brain = p["brain"].str();
        if (!resolveBrain(req, b.brain, b.brainText, err)) return false;
        if (!parseOverride(p["objective"].isNull() ? p["override"] : p["objective"], b.override_, err)) { err = "bot " + std::to_string(n) + ": " + err; return false; }
        b.extra = p;
        cfg.bots.push_back(b);
        n++;
        return true;
    };
    if (req["bots"].isNum()) {
        for (int k = 0; k < req["bots"].i(); k++) if (!addSpec(Json::object())) return false;
    } else if (req["bots"].isArr()) {
        for (auto& p : req["bots"].items()) if (!addSpec(p)) return false;
    } else for (int k = 0; k < 4; k++) if (!addSpec(Json::object())) return false;
    if (cfg.bots.empty() || (int)cfg.bots.size() > kMaxPlayers) { err = "1 to 32 bots"; return false; }
    if (!BotSim::brainsSupported()) {
        for (auto& b : cfg.bots) if (!b.brainText.empty()) { S.warnings.push_back("this build has no bot brains (src/game/botbrain.h): every bot plays the built-in brain"); break; }
        return true;
    }
    // every brain compiles, or the request fails here (not a match per brain error)
    std::map<std::string, std::string> checked;
    for (size_t k = 0; k < cfg.bots.size(); k++) {
        const BotSpec& b = cfg.bots[k];
        if (b.brainText.empty()) continue;
        auto it = checked.find(b.brainText);
        if (it == checked.end()) it = checked.emplace(b.brainText, BotSim::checkBrain(b.brainText, nullptr, nullptr)).first;
        if (!it->second.empty()) { err = "bot " + std::to_string(k) + ": brain '" + b.brain + "': " + it->second; return false; }
    }
    return true;
}

Json botsStartJson(const Setup& S, BotSim& sim) {
    Json a = Json::array();
    std::vector<BotState> st;
    sim.sample(st);
    for (size_t k = 0; k < st.size() && k < S.cfg.bots.size(); k++) {
        const BotSpec& b = S.cfg.bots[k];
        Json o = Json::object();
        o.set("id", st[k].id); o.set("name", st[k].name); o.set("team", teamStr(st[k].team)); o.set("skill", botSkillName(st[k].skill));
        o.set("brain", b.brain.empty() ? "default" : b.brain);
        o.set("brainLabel", sim.brainLabel(k));
        o.set("brainStatus", sim.brainStatus(k));
        if (b.placed) { o.set("start", jvec(b.pos)); o.set("startYaw", b.yaw); }
        o.set("objective", overrideJson(b.override_));
        a.push(std::move(o));
    }
    return a;
}

// one bot's numbers for the match so far (index k in the setup)
Json botStats(BotSim& sim, size_t k, const BotState& b, const Setup& S) {
    Json o = Json::object();
    o.set("id", b.id); o.set("name", b.name); o.set("team", teamStr(b.team)); o.set("skill", botSkillName(b.skill));
    const std::string& br = S.cfg.bots[k].brain;
    o.set("brain", br.empty() ? "default" : br);
    o.set("brainLabel", sim.brainLabel(k));
    o.set("kills", b.kills); o.set("deaths", b.deaths); o.set("score", b.score);
    o.set("damage", b.damage); o.set("objTime", b.objTime); o.set("shots", b.shots); o.set("hits", b.hits);
    o.set("aliveTime", b.aliveTime);
    bool won = sim.matchOver() && ((sim.winnerPlayer() >= 0 && sim.winnerPlayer() == b.id) ||
                                   (sim.winnerTeam() >= 0 && modeIsTeamBased(sim.server().ms_.mode) && sim.winnerTeam() == b.team));
    o.set("won", won);
    return o;
}

Json winnerJson(BotSim& sim) {
    Json w = Json::object();
    if (!sim.matchOver()) return Json();
    w.set("team", sim.winnerTeam() >= 0 ? Json(teamStr(sim.winnerTeam())) : Json());
    w.set("player", sim.winnerPlayer() >= 0 ? Json(sim.winnerPlayer()) : Json());
    w.set("text", sim.matchText());
    w.set("matchesEnded", sim.matchesEnded());
    return w;
}

// who's ahead when the time ran out first: {team} (team modes, by score) or {player} (by score); null: level
Json leaderJson(BotSim& sim) {
    const Server& s = sim.server();
    Json l = Json::object();
    if (modeIsTeamBased(s.ms_.mode)) {
        if (s.ms_.scoreA == s.ms_.scoreB) return Json();
        l.set("team", s.ms_.scoreA > s.ms_.scoreB ? "a" : "b");
        return l;
    }
    std::vector<BotState> st; sim.sample(st);
    int best = -1, bs = -(1 << 30), ties = 0;
    for (auto& b : st) { if (b.score > bs) { bs = b.score; best = b.id; ties = 1; } else if (b.score == bs) ties++; }
    if (best < 0 || ties > 1) return Json();
    l.set("player", best);
    return l;
}

// stdin controls on their own thread (pause, resume, speed <x>, step, stop)
struct Controls { std::mutex mu; std::deque<std::string> q; std::atomic<bool> eof{false}; };
std::shared_ptr<Controls> readControls(CmdCtx& ctx) {
    auto ctl = std::make_shared<Controls>();
    if (ctx.control) {
        std::istream* in = ctx.control;
        std::thread([ctl, in] {
            std::string line;
            while (std::getline(*in, line)) { std::lock_guard<std::mutex> l(ctl->mu); ctl->q.push_back(line); }
            ctl->eof = true;
        }).detach();   // (blocked on stdin when the run ends: the process exits under it)
    }
    return ctl;
}
std::vector<std::string> takeControls(Controls& c) {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> l(c.mu);
    while (!c.q.empty()) {
        std::string s = c.q.front(); c.q.pop_front();
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\n')) s.pop_back();
        if (!s.empty()) out.push_back(s);
    }
    return out;
}

void finishLine(CmdCtx& ctx, Json& out, Json line) {
    if (ctx.stream) { ctx.emit(line); out = Json::object(); }
    else out = std::move(line);   // (main writes it: the last NDJSON line)
}

// ---- sim ------------------------------------------------------------------------------------------------------------
// frames carry millimetres and 0.1 mrad (a third of the NDJSON a full float takes; the page draws nothing finer)
Json qv(const Vec3& v) { Json a = Json::array(); for (float x : {v.x, v.y, v.z}) a.push(std::round(x * 1000.0) / 1000.0); return a; }
double qa(float a) { return std::round(a * 10000.0) / 10000.0; }

Json frameBot(const BotState& b, std::vector<Vec3>* lastPath) {
    Json o = Json::object();
    o.set("id", b.id); o.set("team", teamStr(b.team)); o.set("alive", b.alive);
    o.set("pos", qv(b.pos)); o.set("yaw", qa(b.yaw)); o.set("pitch", qa(b.pitch)); o.set("vel", qv(b.vel));
    o.set("health", std::round(b.health * 10.0) / 10.0);
    o.set("weapon", b.weapon >= 0 && b.weapon < WPN_COUNT ? weaponDef(b.weapon).name : ""); o.set("weaponId", b.weapon);
    o.set("state", b.state); o.set("decision", b.decision); o.set("target", b.target);
    o.set("goal", b.alive ? qv(b.dest) : Json()); o.set("waypoint", b.alive ? qv(b.waypoint) : Json());
    auto same = [](const std::vector<Vec3>& x, const std::vector<Vec3>& y) {
        if (x.size() != y.size()) return false;
        for (size_t k = 0; k < x.size(); k++) if (x[k].x != y[k].x || x[k].y != y[k].y || x[k].z != y[k].z) return false;
        return true;
    };
    if (lastPath && !same(*lastPath, b.path)) {
        Json p = Json::array(); for (auto& v : b.path) p.push(qv(v));
        o.set("path", std::move(p));
        *lastPath = b.path;
    }
    o.set("onGround", b.onGround); o.set("crouched", b.crouched); o.set("hooked", b.hooked);
    o.set("kills", b.kills); o.set("deaths", b.deaths); o.set("score", b.score); o.set("damage", std::round(b.damage * 10.0) / 10.0);
    return o;
}

int runSim(const Json& req, Json& out, CmdCtx& ctx) {
    const double w0 = maptoolNow();
    Setup S;
    std::string err;
    if (!parseSetup(req, S, false, err)) return cmdError(out, err);
    netInit();
    BotSim sim;
    if (!sim.start(S.cfg, &err)) return cmdError(out, err);
    const double startSeconds = maptoolNow() - w0;   // (the map, the server, the nav graph, the warmup)
    const bool paths = req["paths"].boolean(true);
    const int ticksPer = std::max(1, (int)std::lround(sim.tickRate() / S.rate));
    {
        Json s = Json::object();
        s.set("type", "start"); s.set("map", S.mapLabel); s.set("mode", modeName(S.cfg.mode)); s.set("tickRate", sim.tickRate());
        s.set("rate", (double)sim.tickRate() / ticksPer); s.set("seconds", S.seconds); s.set("until", S.untilMatchEnd ? "matchEnd" : "seconds");
        s.set("realtime", S.realtime); s.set("speed", S.speed); s.set("seed", (double)S.cfg.seed);
        s.set("teams", modeIsTeamBased(S.cfg.mode));
        s.set("brainSupport", BotSim::brainsSupported());
        s.set("bots", botsStartJson(S, sim));
        ctx.emit(s);
        for (auto& w : S.warnings) { Json j = Json::object(); j.set("type", "warning"); j.set("warning", w); ctx.emit(j); }
    }
    auto ctl = readControls(ctx);
    bool paused = false, stopped = false;
    int steps = 0;
    double speed = S.speed;
    double wallBase = maptoolNow(), simBase = sim.time(), simWall = 0;
    long long ticks = 0;
    std::vector<BotState> st;
    std::vector<std::vector<Vec3>> lastPaths(S.cfg.bots.size(), std::vector<Vec3>{Vec3(1e9f, 0, 0)});
    std::string reason = "time";
    for (;;) {
        for (auto& c : takeControls(*ctl)) {
            if (c == "pause") paused = true;
            else if (c == "resume") { paused = false; wallBase = maptoolNow(); simBase = sim.time(); }
            else if (c == "step") steps++;
            else if (c == "stop") stopped = true;
            else if (c.compare(0, 6, "speed ") == 0) { speed = std::max(0.0, atof(c.c_str() + 6)); wallBase = maptoolNow(); simBase = sim.time(); }
            else if (c == "realtime on" || c == "realtime off") { S.realtime = c == "realtime on"; wallBase = maptoolNow(); simBase = sim.time(); }
            else { Json e = Json::object(); e.set("type", "warning"); e.set("warning", "unknown control '" + c + "'"); ctx.emit(e); }
        }
        if (stopped) { reason = "stopped"; break; }
        if (S.untilMatchEnd && sim.matchOver()) { reason = "matchEnd"; break; }
        if (sim.mapChanged()) { reason = "mapChanged"; break; }
        if (sim.time() >= S.seconds - 1e-9) { reason = "time"; break; }
        if (paused && steps == 0) { sleepMs(10); continue; }
        if (paused) steps--;
        double a = maptoolNow();
        ticks += sim.step(ticksPer, S.untilMatchEnd);
        simWall += maptoolNow() - a;
        Json f = Json::object();
        f.set("type", "frame"); f.set("t", sim.time());
        sim.sample(st);
        Json bs = Json::array();
        for (size_t k = 0; k < st.size(); k++) bs.push(frameBot(st[k], paths && k < lastPaths.size() ? &lastPaths[k] : nullptr));
        f.set("bots", std::move(bs));
        Json ev = Json::array();
        for (auto& e : sim.takeEvents()) ev.push(e.json());
        f.set("events", std::move(ev));
        ctx.emit(f);
        if (S.realtime && speed > 0 && !paused) {   // paced: sim seconds / speed of wall time
            double due = wallBase + (sim.time() - simBase) / speed;
            double now = maptoolNow();
            if (due > now) std::this_thread::sleep_for(std::chrono::duration<double>(std::min(due - now, 1.0)));
        }
    }
    Json d = Json::object();
    d.set("type", "end"); d.set("t", sim.time()); d.set("reason", reason);
    d.set("winner", winnerJson(sim));
    if (!sim.matchOver()) d.set("leader", leaderJson(sim));
    sim.sample(st);
    Json stats = Json::array();
    for (size_t k = 0; k < st.size(); k++) stats.push(botStats(sim, k, st[k], S));
    d.set("bots", std::move(stats));
    d.set("result", sim.summary());
    Json perf = Json::object();
    perf.set("wallSeconds", maptoolNow() - w0); perf.set("startSeconds", startSeconds); perf.set("simSeconds", simWall); perf.set("ticks", (double)ticks);
    perf.set("ticksPerSecond", simWall > 0 ? ticks / simWall : 0);
    perf.set("speedup", simWall > 0 ? sim.time() / simWall : 0);
    d.set("perf", std::move(perf));
    sim.stop();
    finishLine(ctx, out, std::move(d));
    return 0;
}

// ---- simbatch -------------------------------------------------------------------------------------------------------
struct MatchResult {
    int index = 0;
    uint32_t seed = 0;
    std::string reason, error;
    double t = 0, wall = 0;
    long long ticks = 0;
    Json winner, leader, score, bots;   // bots: botStats per bot
    std::vector<Json> botRows;
};

int runSimBatch(const Json& req, Json& out, CmdCtx& ctx) {
    const double w0 = maptoolNow();
    Setup S;
    std::string err;
    if (!parseSetup(req, S, true, err)) return cmdError(out, err);
    std::vector<uint32_t> seeds;
    for (auto& s : req["seeds"].items()) if (s.isNum()) seeds.push_back((uint32_t)s.num());
    if (seeds.empty()) {
        int n = std::clamp(req["matches"].i(8), 1, 10000);
        for (int k = 0; k < n; k++) seeds.push_back(S.cfg.seed + (uint32_t)k);
    }
    const int N = (int)seeds.size();
    const int T = std::clamp(req["threads"].i(maptoolThreads()), 1, std::max(1, std::min(N, 64)));
    const double progressEvery = std::max(0.0, req["progressEvery"].num(0.5));   // s of wall time between progress lines
    netInit();
    // one data folder for all the matches (the game's user data folder is global): the map written once
    fs::path dir = fs::temp_directory_path() / ("quark_simbatch_" + std::to_string((long long)(maptoolNow() * 1000)));
    if (!BotSim::prepareSharedDir(dir.string(), S.cfg.mapText, &err)) return cmdError(out, err);
    S.cfg.sharedDataDir = dir.string();
    {
        Json s = Json::object();
        s.set("type", "start"); s.set("map", S.mapLabel); s.set("mode", modeName(S.cfg.mode)); s.set("tickRate", S.cfg.tickRate);
        s.set("matches", N); s.set("threads", T); s.set("seconds", S.seconds); s.set("until", S.untilMatchEnd ? "matchEnd" : "seconds");
        Json sj = Json::array(); for (auto x : seeds) sj.push((double)x); s.set("seeds", sj);
        s.set("teams", modeIsTeamBased(S.cfg.mode));
        s.set("brainSupport", BotSim::brainsSupported());
        Json bots = Json::array();
        for (size_t k = 0; k < S.cfg.bots.size(); k++) {
            const BotSpec& b = S.cfg.bots[k];
            Json o = Json::object();
            o.set("index", (int)k); o.set("name", b.name); o.set("team", modeIsTeamBased(S.cfg.mode) ? teamStr(b.team) : "a");
            o.set("skill", botSkillName(b.skill)); o.set("brain", b.brain.empty() ? "default" : b.brain);
            o.set("objective", overrideJson(b.override_));
            bots.push(std::move(o));
        }
        s.set("bots", bots);
        ctx.emit(s);
        for (auto& w : S.warnings) { Json j = Json::object(); j.set("type", "warning"); j.set("warning", w); ctx.emit(j); }
    }
    auto ctl = readControls(ctx);
    std::atomic<bool> cancel{false};
    std::atomic<int> next{0};
    std::vector<MatchResult> results(N);
    std::vector<std::string> labels(S.cfg.bots.size(), "default"), statuses(S.cfg.bots.size(), "default");
    std::mutex mu;
    std::condition_variable cv;
    std::deque<int> finished;
    // one match, on a thread of its own (bots.cpp's thread_local route budget starts fresh: the same seed gives the
    // same match as `sim`, whichever thread ran what before)
    auto playOne = [&](int i) {
        MatchResult& R = results[i];
        R.index = i; R.seed = seeds[i];
        double a = maptoolNow();
        BotSimConfig cfg = S.cfg;
        cfg.seed = seeds[i];
        BotSim sim;
        std::string e;
        if (!sim.start(cfg, &e)) { R.error = e; R.reason = "error"; return; }
        const int chunk = sim.ticksFor(1.0);
        std::string reason = "time";
        while (true) {
            if (cancel) { reason = "stopped"; break; }
            if (S.untilMatchEnd && sim.matchOver()) { reason = "matchEnd"; break; }
            if (sim.mapChanged()) { reason = "mapChanged"; break; }
            if (sim.time() >= S.seconds - 1e-9) break;
            int want = std::min(chunk, std::max(1, sim.ticksFor(S.seconds - sim.time())));
            R.ticks += sim.step(want, S.untilMatchEnd);
            sim.takeEvents();
        }
        R.reason = reason; R.t = sim.time();
        R.winner = winnerJson(sim);
        R.leader = sim.matchOver() ? Json() : leaderJson(sim);
        R.score = Json::array(); R.score.push((int)sim.server().ms_.scoreA); R.score.push((int)sim.server().ms_.scoreB);
        std::vector<BotState> st; sim.sample(st);
        for (size_t k = 0; k < st.size(); k++) R.botRows.push_back(botStats(sim, k, st[k], S));
        if (i == 0) {
            std::lock_guard<std::mutex> l(mu);
            for (size_t k = 0; k < labels.size(); k++) { labels[k] = sim.brainLabel(k); statuses[k] = sim.brainStatus(k); }
        }
        sim.stop();
        R.wall = maptoolNow() - a;
    };
    std::vector<std::thread> drivers;
    std::atomic<int> driversDone{0};
    for (int w = 0; w < T; w++)
        drivers.emplace_back([&] {
            for (int i; !cancel && (i = next++) < N;) {
                std::thread th(playOne, i);
                th.join();
                { std::lock_guard<std::mutex> l(mu); finished.push_back(i); }
                cv.notify_one();
            }
            driversDone++;
            cv.notify_one();
        });
    int done = 0;
    double lastProgress = maptoolNow();
    for (bool last = false; !last;) {
        last = driversDone == T;   // (every driver is out: this pass drains what's left)
        std::vector<int> got;
        {
            std::unique_lock<std::mutex> l(mu);
            if (!last) cv.wait_for(l, std::chrono::milliseconds(100), [&] { return !finished.empty() || driversDone == T; });
            while (!finished.empty()) { got.push_back(finished.front()); finished.pop_front(); }
        }
        for (auto& c : takeControls(*ctl)) {
            if (c == "stop") cancel = true;
            else { Json e = Json::object(); e.set("type", "warning"); e.set("warning", "simbatch takes only 'stop' (not '" + c + "')"); ctx.emit(e); }
        }
        for (int i : got) {
            done++;
            const MatchResult& R = results[i];
            Json p = Json::object();
            p.set("type", "match"); p.set("done", done); p.set("total", N); p.set("index", i); p.set("seed", (double)R.seed);
            if (!R.error.empty()) p.set("error", R.error);
            p.set("reason", R.reason); p.set("t", R.t); p.set("winner", R.winner);
            if (!R.leader.isNull()) p.set("leader", R.leader);
            p.set("score", R.score); p.set("wallSeconds", R.wall); p.set("ticks", (double)R.ticks);
            Json rows = Json::array(); for (auto& r : R.botRows) rows.push(r);
            p.set("bots", rows);
            ctx.emit(p);
        }
        double now = maptoolNow();
        if (!last && progressEvery > 0 && now - lastProgress >= progressEvery) {
            lastProgress = now;
            Json p = Json::object();
            p.set("type", "progress"); p.set("done", done); p.set("total", N);
            p.set("running", std::max(0, std::min(N, next.load()) - done)); p.set("wallSeconds", now - w0);
            ctx.emit(p);
        }
    }
    for (auto& d : drivers) d.join();
    std::error_code ec;
    fs::remove_all(dir, ec);
    // ---- the report
    const bool teams = modeIsTeamBased(S.cfg.mode);
    struct Agg { int matches = 0, wins = 0, kills = 0, deaths = 0, score = 0, shots = 0, hits = 0; double damage = 0, objTime = 0, alive = 0, t = 0; };
    auto aggJson = [](const Agg& a, bool brain) {
        Json o = Json::object();
        o.set(brain ? "botMatches" : "matches", a.matches); o.set("wins", a.wins); o.set("winRate", a.matches ? (double)a.wins / a.matches : 0.0);
        o.set("kills", a.kills); o.set("deaths", a.deaths); o.set("kd", a.deaths ? (double)a.kills / a.deaths : (double)a.kills);
        o.set("damage", a.damage); o.set("score", a.score); o.set("objTime", a.objTime);
        o.set("shots", a.shots); o.set("hits", a.hits); o.set("accuracy", a.shots ? (double)a.hits / a.shots : 0.0);
        o.set("aliveTime", a.alive); o.set("aliveShare", a.t > 0 ? a.alive / a.t : 0.0);
        double mins = a.t / 60.0;
        o.set("killsPerMin", mins > 0 ? a.kills / mins : 0.0); o.set("damagePerMin", mins > 0 ? a.damage / mins : 0.0);
        return o;
    };
    std::vector<Agg> perBot(S.cfg.bots.size());
    std::map<std::string, Agg> perBrain;
    std::vector<std::string> brainOrder;
    int winsA = 0, winsB = 0, draws = 0, timeouts = 0, errors = 0, played = 0;
    std::map<int, int> playerWins;
    double matchWall = 0; long long ticks = 0;
    for (auto& R : results) {
        if (R.reason.empty()) continue;   // (never started: stopped)
        if (!R.error.empty()) { errors++; continue; }
        played++;
        matchWall += R.wall; ticks += R.ticks;
        if (R.reason != "matchEnd") timeouts++;
        else if (teams) {
            const Json& wt = R.winner["team"];
            if (wt.str() == "a") winsA++; else if (wt.str() == "b") winsB++; else draws++;
        } else if (R.winner["player"].isNum()) playerWins[R.winner["player"].i()]++;
        else draws++;
        for (size_t k = 0; k < R.botRows.size() && k < perBot.size(); k++) {
            const Json& r = R.botRows[k];
            for (Agg* a : {&perBot[k], &perBrain[labels[k]]}) {
                a->matches++; a->wins += r["won"].boolean() ? 1 : 0;
                a->kills += r["kills"].i(); a->deaths += r["deaths"].i(); a->score += r["score"].i();
                a->damage += r["damage"].num(); a->objTime += r["objTime"].num(); a->shots += r["shots"].i(); a->hits += r["hits"].i();
                a->alive += r["aliveTime"].num(); a->t += R.t;
            }
            if (std::find(brainOrder.begin(), brainOrder.end(), labels[k]) == brainOrder.end()) brainOrder.push_back(labels[k]);
        }
    }
    Json d = Json::object();
    d.set("type", "end");
    d.set("reason", cancel ? "stopped" : "done");
    d.set("matchesPlayed", played); d.set("errors", errors); d.set("timeouts", timeouts);
    Json bj = Json::array();
    for (size_t k = 0; k < perBot.size(); k++) {
        const BotSpec& b = S.cfg.bots[k];
        Json o = aggJson(perBot[k], false);
        o.set("index", (int)k); o.set("name", results.empty() || results[0].botRows.size() <= k ? b.name : results[0].botRows[k]["name"].str());
        o.set("team", teams ? teamStr(b.team) : "a"); o.set("skill", botSkillName(b.skill));
        o.set("brain", b.brain.empty() ? "default" : b.brain); o.set("brainLabel", labels[k]); o.set("brainStatus", statuses[k]);
        o.set("objective", overrideJson(b.override_));
        bj.push(std::move(o));
    }
    d.set("bots", std::move(bj));
    Json brj = Json::array();
    for (auto& name : brainOrder) { Json o = aggJson(perBrain[name], true); o.set("brain", name); brj.push(std::move(o)); }
    d.set("brains", std::move(brj));
    if (teams) { Json t = Json::object(); t.set("a", winsA); t.set("b", winsB); t.set("draws", draws); d.set("teamWins", t); }
    else {
        Json pw = Json::array();
        for (auto& kv : playerWins) { Json o = Json::object(); o.set("player", kv.first); o.set("wins", kv.second); pw.push(o); }
        d.set("playerWins", pw); d.set("draws", draws);
    }
    Json ms = Json::array();
    for (auto& R : results) {
        if (R.reason.empty()) continue;
        Json o = Json::object();
        o.set("index", R.index); o.set("seed", (double)R.seed); o.set("reason", R.reason); o.set("t", R.t);
        if (!R.error.empty()) o.set("error", R.error);
        o.set("winner", R.winner); if (!R.leader.isNull()) o.set("leader", R.leader);
        o.set("score", R.score); o.set("wallSeconds", R.wall);
        ms.push(std::move(o));
    }
    d.set("matches", std::move(ms));
    Json perf = Json::object();
    double wall = maptoolNow() - w0;
    perf.set("wallSeconds", wall); perf.set("threads", T);
    perf.set("matchesPerSecond", wall > 0 ? played / wall : 0);
    perf.set("ticks", (double)ticks);
    perf.set("ticksPerSecondPerMatch", matchWall > 0 ? ticks / matchWall : 0);   // (a match's wall time includes its start: map load, nav graph)
    perf.set("ticksPerSecondTotal", wall > 0 ? ticks / wall : 0);
    d.set("perf", std::move(perf));
    finishLine(ctx, out, std::move(d));
    return 0;
}

// ---- simcheck: a brain's text compiled the way sim would (the page's "check" before a run)
int runSimCheck(const Json& req, Json& out, CmdCtx&) {
    std::string text = req["text"].str(), err;
    if (text.empty() && req["brain"].isStr() && !resolveBrain(req, req["brain"].str(), text, err)) return cmdError(out, err);
    bool supported = false; std::string label;
    std::string e = BotSim::checkBrain(text, &supported, &label);
    out = Json::object();
    out.set("brainSupport", supported);
    out.set("ok", supported && e.empty());
    if (!e.empty()) out.set("error", e);
    if (!label.empty()) out.set("label", label);
    return 0;
}
}  // namespace

MAPTOOL_COMMAND("sim", runSim, "{map, mode?, bots: [{team, skill, name, brain, pos, yaw, objective}], seconds?, until?, seed?, tickRate?, rate?, realtime?, speed?, brains?} [--stream] -> NDJSON start / frame / end (SIM.md)");
MAPTOOL_COMMAND("simbatch", runSimBatch, "{...sim's setup, matches? | seeds?, threads?} [--stream] -> NDJSON start / match / progress / end, per bot / brain / match (SIM.md)");
MAPTOOL_COMMAND("simcheck", runSimCheck, "{text | brain, brains?} -> {brainSupport, ok, label?, error?}: compiles a brain the way sim does");
