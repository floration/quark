// bots: a headless match with the game's own bots (BotSim), traced; --stream: NDJSON as it runs, with stdin controls.
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>
#include "botsim.h"
#include "maptool.h"
#include "common/net.h"
#include "game/bots.h"
#include "game/gamedefs.h"
#include "game/gamemodes.h"

static int skillFromJson(const Json& j, int def) {
    if (j.isNum()) return std::clamp(j.i(), 0, BOT_SKILLS - 1);
    if (j.isStr()) for (int k = 0; k < BOT_SKILLS; k++) { std::string a = botSkillName(k), b = j.str(); for (auto& c : a) c = (char)tolower((unsigned char)c); for (auto& c : b) c = (char)tolower((unsigned char)c); if (a == b) return k; }
    return def;
}
static const char* teamName(int t) { return t == TEAM_B ? "b" : t == TEAM_A ? "a" : "spectator"; }

static Json stateJson(const BotState& b, bool paths) {
    Json o = Json::object();
    o.set("id", b.id);
    o.set("pos", jvec(b.pos)); o.set("yaw", b.yaw); o.set("pitch", b.pitch); o.set("vel", jvec(b.vel));
    o.set("health", b.health); o.set("weapon", b.weapon); o.set("weaponName", b.weapon >= 0 && b.weapon < WPN_COUNT ? weaponDef(b.weapon).name : "");
    o.set("alive", b.alive); o.set("state", b.state); o.set("target", b.target);
    o.set("onGround", b.onGround); o.set("crouched", b.crouched);
    o.set("goal", jvec(b.goal)); o.set("waypoint", jvec(b.waypoint));
    if (paths) { Json p = Json::array(); for (auto& v : b.path) p.push(jvec(v)); o.set("path", std::move(p)); }
    o.set("kills", b.kills); o.set("deaths", b.deaths);
    return o;
}

static int runBots(const Json& req, Json& out, CmdCtx& ctx) {
    double w0 = maptoolNow();
    Map m;
    std::string err;
    if (!loadRequestMap(req, m, err)) return cmdError(out, err);
    BotSimConfig cfg;
    cfg.mapText = m.sourceText();
    cfg.mode = modeFromJson(req["mode"], m.preferredMode >= 0 ? m.preferredMode : MODE_FFA);
    cfg.tickRate = req["tickRate"].i(125);
    if (cfg.tickRate != 1000 && cfg.tickRate != 500 && cfg.tickRate != 250 && cfg.tickRate != 125) return cmdError(out, "tickRate is 1000, 500, 250 or 125");
    cfg.seed = (uint32_t)req["seed"].num(1);
    cfg.viaFrame = req["viaFrame"].boolean(false);   // (a cross-check of the stepping: the server's own frame() loop)
    int skill = skillFromJson(req["skill"], BOT_ADVANCED);
    bool teams = modeIsTeamBased(cfg.mode);
    // the bots: "placed" ones first (team, skill, pos + yaw, name, brain), then "bots": N more (or a list of specs)
    for (auto& p : req["placed"].items()) {
        BotSpec b;
        b.team = p["team"].str("a") == "b" ? TEAM_B : TEAM_A;
        b.skill = skillFromJson(p["skill"], skill);
        if (p["pos"].isArr()) { b.placed = true; b.pos = toVec(p["pos"]); b.yaw = p["yaw"].f(0); }
        b.name = p["name"].str();
        b.brain = p["brain"].str();
        b.extra = p;
        cfg.bots.push_back(b);
    }
    int more = req["bots"].isNum() ? req["bots"].i() : (req.has("placed") || req["bots"].isArr()) ? 0 : 4;   // (a list or placed bots: only those)
    for (auto& p : req["bots"].items()) {   // (a list: the same specs as "placed")
        BotSpec b;
        b.team = p["team"].str("a") == "b" ? TEAM_B : TEAM_A;
        b.skill = skillFromJson(p["skill"], skill);
        if (p["pos"].isArr()) { b.placed = true; b.pos = toVec(p["pos"]); b.yaw = p["yaw"].f(0); }
        b.name = p["name"].str(); b.brain = p["brain"].str(); b.extra = p;
        cfg.bots.push_back(b);
    }
    for (int k = 0; k < more; k++) { BotSpec b; b.team = teams && (cfg.bots.size() % 2) ? TEAM_B : TEAM_A; b.skill = skill; cfg.bots.push_back(b); }
    if (cfg.bots.empty() || (int)cfg.bots.size() > kMaxPlayers) return cmdError(out, "1 to 32 bots");
    double seconds = std::clamp(req["seconds"].num(30), 0.1, 3600.0);
    double rate = std::clamp(req["rate"].num(10), 0.5, 1000.0);
    double speed = std::max(0.0, req["speed"].num(ctx.stream ? 1.0 : 0.0));   // x real time; 0: as fast as it goes
    bool paths = req["paths"].boolean(ctx.stream);
    netInit();
    BotSim sim;
    if (!sim.start(cfg, &err)) return cmdError(out, err);
    const double dt = 1.0 / rate;
    const int ticksPer = std::max(1, (int)std::lround(sim.tickRate() * dt));
    std::vector<BotState> st;
    auto botsInfo = [&] {
        Json a = Json::array();
        sim.sample(st);
        for (size_t k = 0; k < st.size(); k++) {
            Json o = Json::object();
            o.set("id", st[k].id); o.set("name", st[k].name); o.set("team", teamName(st[k].team)); o.set("skill", botSkillName(st[k].skill));
            if (k < cfg.bots.size() && cfg.bots[k].placed) { o.set("start", jvec(cfg.bots[k].pos)); o.set("startYaw", cfg.bots[k].yaw); }
            if (k < cfg.bots.size() && !cfg.bots[k].brain.empty()) o.set("brain", cfg.bots[k].brain);
            a.push(std::move(o));
        }
        return a;
    };
    if (ctx.stream) {
        Json s = Json::object();
        s.set("type", "start"); s.set("map", m.name); s.set("mode", modeName(cfg.mode)); s.set("tickRate", sim.tickRate());
        s.set("rate", rate); s.set("seconds", seconds); s.set("speed", speed); s.set("seed", (double)cfg.seed);
        s.set("bots", botsInfo());
        ctx.emit(s);
        // controls from stdin, read on their own thread (pause, resume, speed <x>, step, stop)
        struct Controls { std::mutex mu; std::deque<std::string> q; std::atomic<bool> eof{false}; };
        auto ctl = std::make_shared<Controls>();
        if (ctx.control) {
            std::istream* in = ctx.control;
            std::thread([ctl, in] {
                std::string line;
                while (std::getline(*in, line)) { std::lock_guard<std::mutex> l(ctl->mu); ctl->q.push_back(line); }
                ctl->eof = true;
            }).detach();   // (blocked on stdin when the run ends: the process exits under it)
        }
        bool paused = false, stop = false;
        int steps = 0;
        double wallBase = maptoolNow(), simBase = sim.time();
        while (!stop && sim.time() < seconds - 1e-9 && !sim.mapChanged()) {
            {
                std::lock_guard<std::mutex> l(ctl->mu);
                while (!ctl->q.empty()) {
                    std::string c = ctl->q.front(); ctl->q.pop_front();
                    while (!c.empty() && (c.back() == '\r' || c.back() == ' ')) c.pop_back();
                    if (c == "pause") paused = true;
                    else if (c == "resume") { paused = false; wallBase = maptoolNow(); simBase = sim.time(); }
                    else if (c == "step") steps++;
                    else if (c == "stop") stop = true;
                    else if (c.compare(0, 6, "speed ") == 0) { speed = std::max(0.0, atof(c.c_str() + 6)); wallBase = maptoolNow(); simBase = sim.time(); }
                    else if (!c.empty()) { Json e = Json::object(); e.set("type", "warning"); e.set("warning", "unknown control '" + c + "'"); ctx.emit(e); }
                }
            }
            if (stop) break;
            if (paused && steps == 0) { sleepMs(10); continue; }
            if (paused) steps--;
            sim.step(ticksPer);
            for (auto& e : sim.takeEvents()) { Json j = e.json(); j.set("type", "event"); ctx.emit(j); }
            sim.sample(st);
            Json s2 = Json::object();
            s2.set("type", "sample"); s2.set("t", sim.time());
            Json bs = Json::array();
            for (auto& b : st) bs.push(stateJson(b, paths));
            s2.set("bots", std::move(bs));
            ctx.emit(s2);
            if (speed > 0 && !paused) {   // paced: sim seconds / speed of wall time
                double due = wallBase + (sim.time() - simBase) / speed;
                double now = maptoolNow();
                if (due > now) std::this_thread::sleep_for(std::chrono::duration<double>(std::min(due - now, 1.0)));
            }
        }
        Json d = Json::object();
        d.set("type", "done"); d.set("t", sim.time());
        Json res = sim.summary();
        res.set("stopped", stop);
        res.set("mapChanged", sim.mapChanged());
        res.set("wallSeconds", maptoolNow() - w0);
        d.set("result", std::move(res));
        ctx.emit(d);
        sim.stop();
        out = Json::object();
        return 0;
    }
    // a whole run, as fast as it goes: every bot's trace at `rate` Hz, the events, the end state
    std::vector<Json> traces(cfg.bots.size(), Json::array());
    Json events = Json::array();
    double s0 = maptoolNow();
    auto record = [&] {
        sim.sample(st);
        for (size_t k = 0; k < st.size() && k < traces.size(); k++) {
            const BotState& b = st[k];
            Json r = Json::array();
            r.push(sim.time()); r.push(b.pos.x); r.push(b.pos.y); r.push(b.pos.z); r.push(b.yaw); r.push(b.state);
            traces[k].push(std::move(r));
        }
    };
    record();
    while (sim.time() < seconds - 1e-9 && !sim.mapChanged()) {
        sim.step(ticksPer);
        for (auto& e : sim.takeEvents()) events.push(e.json());
        record();
    }
    double simWall = maptoolNow() - s0;
    out = Json::object();
    out.set("map", m.name); out.set("mode", modeName(cfg.mode)); out.set("rate", rate); out.set("tickRate", sim.tickRate()); out.set("seed", (double)cfg.seed);
    out.set("seconds", sim.time());
    Json info = botsInfo();
    Json bots = Json::array();
    for (size_t k = 0; k < info.size(); k++) { Json o = info[k]; o.set("trace", traces[k]); bots.push(std::move(o)); }
    out.set("bots", std::move(bots));
    out.set("traceFields", Json::array());
    { Json f = Json::array(); for (const char* s : {"t", "x", "y", "z", "yaw", "state"}) f.push(s); out.set("traceFields", f); }
    out.set("events", std::move(events));
    Json sum = sim.summary();
    sum.set("mapChanged", sim.mapChanged());
    out.set("result", std::move(sum));
    Json ms = Json::object();
    ms.set("sim", simWall * 1000); ms.set("total", (maptoolNow() - w0) * 1000);
    ms.set("speedup", simWall > 0 ? sim.time() / simWall : 0);   // game seconds per wall second
    out.set("ms", std::move(ms));
    sim.stop();
    return 0;
}
MAPTOOL_COMMAND("bots", runBots, "{map, mode?, bots?: N | [specs], placed?: [{team, skill, pos, yaw, name, brain}], seconds?, skill?, seed?, tickRate?, rate?} [--stream] -> traces and events");
