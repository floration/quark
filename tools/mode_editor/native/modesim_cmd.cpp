// modesim / modebatch / modecheck: the gamemode editor's headless runs (MODESIM.md, design 5.4).
//
//   modesim    one match of a mode (a .qmode's rules, or a built-in mode) on a map with bots, the game's own Server
//              stepped tick by tick: deterministic for a seed, streamed as NDJSON (start, frame, end).
//   modebatch  N seeded matches of the same setup on every core but one: per team / per bot results, match lengths,
//              rounds, score spread, timeouts, and a CSV of the matches.
//   modecheck  compile a .qmode the way modesim does (and check a map has what the logic reads): JSON errors.
//
// It builds its own Server (as tests/brain_tests.cpp's lockstep does) with ServerConfig::custom from the request,
// rather than using BotSim (tools/map_editor/native/botsim.h has no custom rules and belongs to the map / bot editors).
//
// Mode LOGIC: the language (src/game/modelogic.h) is compiled here for the label, the interpreted slots and the
// errors; the server runs it through the game's hook (mode editor phase 3: Server::presetModeLogic, ModeHooks). Each
// answer says whether it did (`logicApplied`), and the interpreted slots' cost is measured by wrapping the server's
// ModeHooks (TimedHooks below: no game code involved). Everything that touches the hook is in the "logic hook" block.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>
#include <utility>
#include "maptool.h"
#include "modesim_logic.h"   // (the logic language: not in this file, see its header)
#include "common/net.h"
#include "common/u8file.h"
#include "common/rng.h"
#include "game/botbrain.h"
#include "game/bots.h"
#include "game/customrules.h"
#include "game/gamedefs.h"
#include "game/gamemodes.h"
#include "game/map.h"
#include "game/server.h"

namespace {
namespace fs = std::filesystem;

// ---- the logic hook (G1) -------------------------------------------------------------------------------------------
// Everything that touches the server's running of a custom mode's logic is here (and only here):
//   1. applyLogicHook(s, sc, S): before Server::start, Server::presetModeLogic with the rules text (the same text and
//      base the label was compiled from: the same hash); on failure the run fails with its error.
//   2. serverRunsLogicT(s) / fileLogicRunning: after start, whether the server runs logic (Server::modeHooks_ non-null)
//      and loaded the file's (its hash). logicApplied in every answer is that: never claimed from the request alone.
//   3. TimedHooks: per-slot timings for `trace` and perf.logicNs. After start, the server's ModeHooks is wrapped in one
//      that forwards every call and times it (rulesReset takes Server::modeHooks_ from modeLogicHooks_, so the wrapper
//      goes there too and every later reset keeps it).
template <class S, class = void> struct HasModeHooks : std::false_type {};
template <class S> struct HasModeHooks<S, std::void_t<decltype(std::declval<const S&>().modeHooks_ != nullptr)>> : std::true_type {};
template <class S> bool serverRunsLogicT(const S& s) {
    if constexpr (HasModeHooks<S>::value) return s.modeHooks_ != nullptr;
    else { (void)s; return false; }
}
constexpr bool kServerHasLogicHook = HasModeHooks<Server>::value;
template <class T, class = void> struct HasLogicText : std::false_type {};
template <class T> struct HasLogicText<T, std::void_t<decltype(std::declval<T&>().logic = std::string())>> : std::true_type {};
template <class C> void setLogicText(C& c, const std::string& text) {
    if constexpr (HasLogicText<C>::value) c.logic = text; else (void)c, (void)text;
}
using Clock = std::chrono::steady_clock;
// The server's ModeHooks with a stopwatch: each event / query it forwards is timed. A slot can raise another (an
// event's action ends the round, which runs roundOver...): the nested call's time is in its caller's too, so the
// per-tick logic time counts only the outermost calls, while each slot's own totals are inclusive.
class TimedHooks final : public ModeHooks {
public:
    explicit TimedHooks(std::shared_ptr<ModeHooks> in) : in_(std::move(in)) { interp = in_->interp; params = in_->params; }
    bool event(Server& s, ModeSlot slot, int a, int b, int c) override { Scope sc(*this, slot); return in_->event(s, slot, a, b, c); }
    bool query(const Server& s, ModeSlot slot, int a, int b, int c, double& out) override { Scope sc(*this, slot); return in_->query(s, slot, a, b, c, out); }
    void nativeLifetime(ModeSlot slot) override { in_->nativeLifetime(slot); }
    struct Agg { long long calls = 0; double ns = 0, max = 0; };
    using Aggs = std::array<Agg, 64>;   // by ModeSlot (gamemodes.h: MSLOT_COUNT <= 64, MSLOT_TIMER among them)
    // the outermost calls' time since the last take (one per tick)
    double takeNs() { double v = pending_; pending_ = 0; return v; }
    // per slot since the last take (a frame's), and for the whole match
    Aggs takeFrame() { Aggs o = frame_; frame_ = Aggs(); return o; }
    const Aggs& totals() const { return total_; }
    // every call's time (any slot, nested ones too), for the per-call p99: at most kMaxCalls kept (then only counted)
    static constexpr size_t kMaxCalls = 4u << 20;
    const std::vector<float>& callNs() const { return calls_; }
    long long callCount() const { return callCount_; }
    void clear() { pending_ = 0; frame_ = total_ = Aggs(); calls_.clear(); callCount_ = 0; }
private:
    struct Scope {
        TimedHooks& h; int slot; Clock::time_point t0;
        Scope(TimedHooks& h_, int slot_) : h(h_), slot(slot_ & 63), t0(Clock::now()) { h.depth_++; }
        ~Scope() {
            const double ns = std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
            if (--h.depth_ == 0) h.pending_ += ns;
            for (Agg* a : {&h.frame_[slot], &h.total_[slot]}) { a->calls++; a->ns += ns; a->max = std::max(a->max, ns); }
            h.callCount_++;
            if (h.calls_.size() < kMaxCalls) h.calls_.push_back((float)ns);
        }
    };
    std::shared_ptr<ModeHooks> in_;
    int depth_ = 0;
    double pending_ = 0;
    Aggs frame_{}, total_{};
    std::vector<float> calls_;
    long long callCount_ = 0;
};
const char* hookSlotName(int slot) { return slot == MSLOT_TIMER ? "timers" : modeSlotNameOf(slot); }   // (MSLOT_TIMER: every timer)
// --------------------------------------------------------------------------------------------------------------------

// Server::simulate is private (frame() runs it on the real-time clock). An explicit instantiation may name a private
// member (C++17 [temp.explicit]/12): the tick below is frame()'s own minus the sockets and the clock (as botsim.cpp
// does; a public Server::stepTick() in src/game would replace both).
template <class Tag, typename Tag::type M> struct Rob { friend typename Tag::type get(Tag) { return M; } };
struct ModeSimTag { using type = void (Server::*)(); friend type get(ModeSimTag); };
template struct Rob<ModeSimTag, &Server::simulate>;

std::atomic<int> g_serial{0};
const char* kPhase[] = {"warmup", "freeze", "live", "roundEnd", "matchEnd"};
const char* phaseName(int p) { return p >= 0 && p < 5 ? kPhase[p] : "?"; }
const char* kBombState[] = {"none", "carried", "dropped", "planted", "exploded", "defused"};
const char* teamStr(int t) { return t == TEAM_B ? "b" : t == TEAM_A ? "a" : "spectator"; }
std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
double r2(double v) { return std::round(v * 100.0) / 100.0; }
double r1(double v) { return std::round(v * 10.0) / 10.0; }
Json qv(const Vec3& v) { Json a = Json::array(); a.push(r2(v.x)); a.push(r2(v.y)); a.push(r2(v.z)); return a; }
bool isFile(const std::string& p) { std::error_code ec; return !p.empty() && fs::is_regular_file(utf8Path(p), ec); }
std::string safeFileName(const std::string& n) {
    std::string o;
    for (char c : n) if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') o += c;
    while (!o.empty() && o[0] == '.') o.erase(0, 1);
    return o.substr(0, 48);
}
int skillFromJson(const Json& j, int def) {
    if (j.isNum()) return std::clamp(j.i(), 0, BOT_SKILLS - 1);
    if (j.isStr()) for (int k = 0; k < BOT_SKILLS; k++) if (lower(botSkillName(k)) == lower(j.str())) return k;
    return def;
}
int modeByAnyName(const std::string& n) {
    int m = modeFromName(n.c_str());
    if (m < 0) m = modeBaseFromName(n);   // ("arena" = 1v1)
    return m;
}

// A .qmode split where the game splits it (customrules.cpp isModeLogicLine): the key lines, the logic block (from that
// line to the end, "" = none).
void splitQmode(const std::string& text, std::string& keys, std::string& logic) {
    keys.clear(); logic.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        size_t end = e == std::string::npos ? text.size() : e;
        if (isModeLogicLine(text.substr(pos, end - pos))) { logic = text.substr(pos); return; }   // (the game's own rule)
        keys += text.substr(pos, (e == std::string::npos ? text.size() : e + 1) - pos);
        pos = e == std::string::npos ? text.size() : e + 1;
    }
}

// ---- the request ------------------------------------------------------------------------------------------------
struct BotReq { int team = TEAM_A; int skill = BOT_ADVANCED; std::string name, brain; };   // brain: "" = the built-in one

struct Setup {
    std::string mapName, mapText;      // the map as written into the run's data folder
    int mode = MODE_FFA;               // the mode the match plays (the rules' base when they have one)
    std::string rulesText;             // the request's .qmode text ("" = the built-in mode)
    CustomRules custom;                // its key=value rules (name empty: none)
    std::string logicBlock;            // its logic block ("" = none)
    ModeCompile compiled;              // that, compiled (also for a file without logic: the label); compiled.ok false: none
    std::string label;                 // name#hash, or the built-in mode's name
    Json check;                        // the compile's answer (modeDiagJson)
    std::vector<BotReq> bots;
    uint32_t seed = 1;
    int tickRate = 125;
    double seconds = 900;
    bool untilMatchEnd = true;
    double rate = 4;                   // frames per game second
    bool realtime = false;
    double speed = 1;
    bool trace = false;
    bool positions = true;
    bool mapView = true;               // the start line's top-down map (modesim)
    std::map<std::string, std::string> brains, brainLabels;   // the brains the bots play: name -> text, name -> name#hash
    std::vector<std::string> warnings;
};

// A brain named by the request: its text from request.brains (serve.py puts the user's bots/<name>.qbrain there), else
// the player's bots folder (before prepareDir points the game's data folder at the run's). It must compile.
bool resolveBrain(const Json& req, const std::string& name, Setup& S, std::string& err) {
    if (name.empty() || name == "default" || S.brains.count(name)) return true;
    for (char c : name) if (!isalnum((unsigned char)c) && c != '_' && c != '-') { err = "brain '" + name + "': letters, digits, _ and -"; return false; }
    std::string text;
    const Json& inl = req["brains"][name];
    if (inl.isStr()) text = inl.str();
    else if (inl.isObj() && inl["text"].isStr()) text = inl["text"].str();
    else {
        std::ifstream f(utf8Path(userDataPath("bots") + "/" + name + ".qbrain"), std::ios::binary);
        if (!f) { err = "brain '" + name + "' not found (give its text in \"brains\", or put it in the bots folder)"; return false; }
        std::ostringstream ss; ss << f.rdbuf(); text = ss.str();
    }
    BrainDiag d;
    auto b = compileBrain(text, &d);
    if (!b) {
        err = "brain '" + name + "' doesn't compile" + (d.errors.empty() ? std::string() : ": " + std::to_string(d.errors[0].line) + ":" + std::to_string(d.errors[0].col) + " " + d.errors[0].msg);
        return false;
    }
    S.brains[name] = text;
    S.brainLabels[name] = brainLabel(*b);
    return true;
}

// the logic hook, part 1 (see the TODO at the top): before Server::start
// G1's API (server.h): Server::presetModeLogic(text, base, err) compiles the .qmode exactly as compileModeText did (the
// same text and base: the same hash), and start() plays it (never on ranked / time trial; only on its base mode).
bool applyLogicHook(Server& s, ServerConfig& sc, const Setup& S, std::string* err) {
    if (sc.custom.active()) setLogicText(sc.custom, S.logicBlock);
    if (S.logicBlock.empty() || !S.compiled.ok) return true;
    return s.presetModeLogic(S.rulesText, S.compiled.base, err);
}

bool loadMap(const Json& req, Map& m, std::string& err) {
    const Json& mj = req["map"];
    Json r = Json::object();
    if (mj.isStr()) { if (isFile(mj.str())) r.set("file", mj.str()); else r.set("name", mj.str()); }
    else if (mj.isObj() && mj["text"].isStr()) { r.set("text", mj["text"]); if (mj["name"].isStr()) r.set("mapName", mj["name"]); }
    else if (mj.isObj() && mj["file"].isStr()) r.set("file", mj["file"]);
    else if (mj.isObj() && mj["name"].isStr() && !mj.has("brushes")) r.set("name", mj["name"]);
    else return loadRequestMap(req, m, err);
    return loadRequestMap(r, m, err);
}

// The rules: compile the logic (errors fail the request), read the key lines as the game does.
bool parseRules(const Json& req, Setup& S, int fallbackMode, std::string& err, Json* errors) {
    S.rulesText = req["rules"].str(req["text"].str());
    if (S.rulesText.find_first_not_of(" \t\r\n") == std::string::npos) S.rulesText.clear();
    std::string keys;
    splitQmode(S.rulesText, keys, S.logicBlock);
    S.custom = CustomRules();
    const bool hasCustom = !keys.empty() && S.custom.fromText(keys);
    // the base: the rules' mode= (a custom mode always plays its own), else the request's / the map's
    int base = fallbackMode;
    if (hasCustom) base = S.custom.baseMode;
    if (!S.rulesText.empty()) {
        ModeCompile c = compileModeText(S.rulesText, hasCustom ? base : -1);
        Json::parse(c.diagJson, S.check);
        if (!c.ok && !hasCustom) {   // a bare logic text with no `base` line: the request's / the map's mode
            ModeCompile c2 = compileModeText(S.rulesText, base);
            Json j2;
            Json::parse(c2.diagJson, j2);
            if (c2.ok || j2["errors"].size() < S.check["errors"].size()) { c = c2; S.check = j2; }
        }
        if (!c.ok) {
            if (!S.logicBlock.empty()) {
                if (errors) *errors = S.check["errors"];
                const Json& e0 = S.check["errors"][0];
                err = "the logic doesn't compile" + (e0.isObj() ? ": " + std::to_string(e0["line"].i()) + ":" + std::to_string(e0["col"].i()) + " " + e0["msg"].str() : std::string());
                return false;
            }
        } else {
            S.compiled = c;
            if (!hasCustom && c.base >= 0) base = c.base;   // (a bare logic text: its `base` line)
        }
    }
    S.mode = base;
    if (S.logicBlock.empty() && !hasCustom) { S.label = modeName(base); return true; }
    if (!hasCustom) {   // logic without a custommode= line: the server needs a custom mode to carry it
        S.custom = CustomRules();
        S.custom.name = S.compiled.ok && !S.compiled.name.empty() ? S.compiled.name : "modesim";
        S.custom.baseMode = base;
        S.warnings.push_back("the rules have no custommode= line: played as the custom mode '" + S.custom.name + "'");
    }
    if (!S.custom.maps.empty()) {
        S.warnings.push_back("the rules' map pool (maps=) is ignored: the match plays on the requested map and stops at its end");
        S.custom.maps.clear();
    }
    S.label = S.compiled.ok ? S.compiled.label : S.custom.name;
    return true;
}

bool parseSetup(const Json& req, Setup& S, bool batch, std::string& err, Json* errors) {
    Map m;
    if (!loadMap(req, m, err)) return false;
    S.mapName = safeFileName(m.name);
    if (S.mapName.empty() || S.mapName == "untitled") S.mapName = "zz_modesim";
    S.mapText = m.sourceText();
    int reqMode = req.has("mode") ? (req["mode"].isStr() ? modeByAnyName(req["mode"].str()) : req["mode"].isNum() ? req["mode"].i() : -1) : -1;
    if (req.has("mode") && (reqMode < 0 || reqMode >= MODE_COUNT)) { err = "mode: one of vs bomb race koth payload ffa npayload duel 1v1 tourney hide tag points"; return false; }
    const int fallback = reqMode >= 0 ? reqMode : m.preferredMode >= 0 ? m.preferredMode : MODE_FFA;
    if (!parseRules(req, S, fallback, err, errors)) return false;
    if (reqMode >= 0 && reqMode != S.mode && !S.rulesText.empty())
        S.warnings.push_back(std::string("mode ") + modeName(reqMode) + " ignored: the rules play " + modeName(S.mode));
    if (!m.supportsMode(S.mode)) S.warnings.push_back(S.mapName + " isn't built for " + modeName(S.mode) + ": it may not work");
    S.tickRate = req["tickRate"].i(125);
    if (S.tickRate != 1000 && S.tickRate != 500 && S.tickRate != 250 && S.tickRate != 125) { err = "tickRate is 1000, 500, 250 or 125"; return false; }
    S.seed = (uint32_t)req["seed"].num(1);
    S.seconds = std::clamp(req["seconds"].num(req["secs"].num(900)), 0.1, 7200.0);
    std::string until = lower(req["until"].str("matchend"));
    if (until != "matchend" && until != "match" && until != "end" && until != "seconds" && until != "time") { err = "until is \"matchEnd\" or \"seconds\""; return false; }
    S.untilMatchEnd = until != "seconds" && until != "time";
    S.rate = std::clamp(req["rate"].num(4), 0.1, 1000.0);
    S.realtime = req["realtime"].boolean(false);
    S.speed = std::max(0.0, req["speed"].num(1));
    S.trace = req["trace"].boolean(false);
    S.positions = req["positions"].boolean(true);
    S.mapView = req["mapView"].boolean(true);
    const int skill = skillFromJson(req["skill"], BOT_ADVANCED);
    const std::string brain = req["brain"].str("");
    const bool teams = modeIsTeamBased(S.mode);
    auto add = [&](const Json& p) {
        BotReq b;
        std::string t = lower(p["team"].str(""));
        int n = (int)S.bots.size();
        b.team = t == "b" ? TEAM_B : t == "a" ? TEAM_A : (teams && n % 2) ? TEAM_B : TEAM_A;
        b.skill = skillFromJson(p["skill"], skill);
        b.name = p["name"].str().substr(0, 24);
        b.brain = p["brain"].str(brain);
        if (b.brain == "default") b.brain.clear();
        S.bots.push_back(b);
    };
    if (req["bots"].isNum()) for (int k = 0; k < req["bots"].i(); k++) add(Json::object());
    else if (req["bots"].isArr()) for (auto& p : req["bots"].items()) add(p);
    else for (int k = 0; k < (S.mode == MODE_DUEL || S.mode == MODE_ARENA ? 2 : 8); k++) add(Json::object());
    if (S.bots.empty() || (int)S.bots.size() > kMaxPlayers) { err = "1 to 32 bots"; return false; }
    for (const BotReq& b : S.bots) if (!resolveBrain(req, b.brain, S, err)) return false;
    if (S.trace && S.logicBlock.empty()) S.warnings.push_back("trace: the rules have no logic, so every slot is native: there is nothing to trace");
    (void)batch;
    return true;
}

// ---- one match --------------------------------------------------------------------------------------------------
struct Event { double t; Json j; };

class ModeMatch {
public:
    ~ModeMatch() { stop(); }
    // dataDir: a folder with maps/<mapName>.qmap, already the game's user data folder (setUserDataDir)
    bool start(const Setup& S, uint32_t seed, const std::string& dataDir, std::string* err);
    void stop() {
        if (!server_) return;
        server_->stop();
        server_.reset();
        std::error_code ec;
        if (!logPath_.empty()) fs::remove(utf8Path(logPath_), ec);
    }
    int step(int ticks, bool stopAtMatchEnd);
    double time() const { return server_ ? server_->time() - t0_ : 0; }
    int tickRate() const { return server_ ? server_->tickRate() : 125; }
    bool matchOver() const { return matchOverAt_ >= 0; }
    Server& server() { return *server_; }
    std::vector<Event> takeEvents() { std::vector<Event> o; o.swap(events_); std::stable_sort(o.begin(), o.end(), [](const Event& a, const Event& b) { return a.t < b.t; }); return o; }
    Json frame(bool positions) const;
    Json startBots() const;
    Json result() const;
    Json winner() const;
    Json leader() const;
    Json perf() const;
    Json slotFrame();                  // the interpreted slots' calls since the last frame (trace)
    Json slotTotals() const;           // ... over the whole match
    Json mapView() const;              // the map seen from above: boxes, spawns, objectives (the Sim plot draws it)
    bool runsLogic() const { return server_ && serverRunsLogicT(*server_); }
    std::string logicHash() const { return server_ && server_->modeLogic() ? server_->modeLogic()->hash : std::string(); }   // (what it loaded)
    const std::vector<std::string>& serverLog() const { return serverLog_; }
    long long ticks() const { return ticks_; }
    double simWall() const { return simWall_; }
    int roundsPlayed() const { return (int)rounds_.size(); }
    const Json& rounds() const { return rounds_; }

private:
    std::unique_ptr<Server> server_;
    std::vector<int> ids_;
    std::vector<std::string> brains_;   // per bot (ids_'s order): its brain's name ("" = the built-in one)
    double t0_ = 0, matchOverAt_ = -1;
    bool started_ = false;
    std::string logPath_;
    long logPos_ = 0;
    std::vector<Event> events_;
    std::vector<std::string> serverLog_;
    Json rounds_ = Json::array();
    std::string matchText_;
    int winTeam_ = -1, winPlayer_ = -1;
    // what the last tick looked like
    int pPhase_ = -1, pScoreA_ = 0, pScoreB_ = 0, pBomb_ = 0, pHill_ = 255;
    float pTimeLeft_ = 0;
    struct Prev { bool alive = false; int checkpoint = 0; bool finished = false; bool it = false; };
    Prev prev_[kMaxPlayers];
    std::vector<float> tickNs_;
    std::shared_ptr<TimedHooks> timed_;   // the server's ModeHooks behind a stopwatch (null: no logic runs)
    std::vector<float> logicNs_;          // the logic's time per tick (its outermost slot calls), while it runs
    std::vector<uint32_t> logicTick_;     // ... the tick (since the match start) each of those was
    long long ticks_ = 0;
    double simWall_ = 0;
    void tickOnce();
    void watch();
    void readLog();
    void push(double t, Json j) { j.set("t", r2(t)); events_.push_back({t, std::move(j)}); }
    void parseWinner(const std::string& text, int& team, int& player) const;
};

bool ModeMatch::start(const Setup& S, uint32_t seed, const std::string& dataDir, std::string* err) {
    const int serial = g_serial++;
    logPath_ = (utf8Path(dataDir) / ("modesim_" + std::to_string(serial) + ".log")).u8string();
    qsrand(seed);   // this thread's generator: spawn picks, the bots' rngs, random(...) in logic
    server_ = std::make_unique<Server>();
    Server& s = *server_;
    s.setLog([this](const std::string& l) { if (serverLog_.size() < 200) serverLog_.push_back(l); });
    ServerConfig sc;
    sc.map = S.mapName;
    sc.mode = S.mode;
    sc.port = 0;                 // (an ephemeral port: nothing connects)
    sc.maxPlayers = std::clamp((int)S.bots.size(), 2, kMaxPlayers);
    sc.maxTickRate = S.tickRate;
    sc.lanOnly = true;
    sc.balanceBots = false;
    sc.autoStart = true;
    sc.logLevel = LOG_SIMPLE;
    sc.logFile = logPath_;
    if (S.custom.active()) sc.custom = S.custom;
    std::string e;
    if (!applyLogicHook(s, sc, S, &e)) { server_.reset(); if (err) *err = "logic: " + e; return false; }   // (the logic hook)
    if (!s.start(sc, &e)) { server_.reset(); if (err) *err = "server: " + e; return false; }
    if (s.modeLogicHooks_) {   // (the logic hook, part 3: time every slot the server runs through the logic)
        std::shared_ptr<ModeHooks> in = s.modeLogicHooks_;
        timed_ = std::make_shared<TimedHooks>(in);
        s.modeLogicHooks_ = timed_;
        if (s.modeHooks_ == in.get()) s.modeHooks_ = timed_.get();
    }
    for (int k = 0; k < 20000 && !s.nav_.ready(s.map_); k++) sleepMs(1);   // (bots stand still without the nav graph)
    for (const BotReq& b : S.bots) {
        int id = s.addBot(b.team, b.skill, false, b.brain.empty() ? nullptr : &b.brain);
        ids_.push_back(id);
        brains_.push_back(b.brain);
        if (id >= 0 && !b.name.empty()) s.players_[id].name = b.name;
    }
    tickNs_.reserve((size_t)(S.seconds * S.tickRate) + 1024);
    if (timed_) { logicNs_.reserve(tickNs_.capacity()); logicTick_.reserve(tickNs_.capacity()); }
    // a second of warmup (the bots join and spawn), then the match starts as a vote would start it
    step(std::max(1, s.tickRate()), false);
    s.command("forcestart");
    t0_ = s.time();
    readLog();   // (the warmup's lines: not the match's)
    events_.clear();
    rounds_ = Json::array();
    matchOverAt_ = -1; matchText_.clear(); winTeam_ = winPlayer_ = -1;
    tickNs_.clear(); ticks_ = 0; simWall_ = 0;
    logicNs_.clear(); logicTick_.clear();
    if (timed_) timed_->clear();
    pPhase_ = s.ms_.phase;
    started_ = true;
    return true;
}

void ModeMatch::tickOnce() {
    Server& s = *server_;
    auto a = std::chrono::steady_clock::now();
    (s.*get(ModeSimTag()))();
    auto b = std::chrono::steady_clock::now();
    tickNs_.push_back((float)std::chrono::duration<double, std::nano>(b - a).count());
    s.tick_++;
    s.timeMs_ += 1000 / s.tickRate();
    if (s.tick_ % s.snapEvery() == 0) s.sendSnapshotsForTest();   // (no clients: bookkeeping, as frame() does it)
    if (timed_) {   // (while the logic plays)
        const double ns = timed_->takeNs();
        if (s.modeHooks_) { logicNs_.push_back((float)ns); logicTick_.push_back((uint32_t)(tickNs_.size() - 1)); }
    }
}

int ModeMatch::step(int ticks, bool stopAtMatchEnd) {
    if (!server_) return 0;
    Server& s = *server_;
    if (activeRules().name != s.cfg_.custom.name) setActiveRules(s.cfg_.custom);   // (per thread, as frame() does)
    const double w = maptoolNow();
    int ran = 0;
    for (int k = 0; k < ticks; k++) {
        tickOnce(); ran++;
        if (started_) watch();
        if (stopAtMatchEnd && s.ms_.phase == PHASE_MATCH_END && started_) break;
    }
    if (started_) { ticks_ += ran; simWall_ += maptoolNow() - w; readLog(); }
    return ran;
}

void ModeMatch::watch() {
    Server& s = *server_;
    const ModeState& ms = s.ms_;
    const double t = s.time() - t0_;
    if (ms.phase != pPhase_) {
        Json j = Json::object(); j.set("type", "phase"); j.set("phase", phaseName(ms.phase)); j.set("from", phaseName(pPhase_)); j.set("round", (int)ms.round);
        push(t, std::move(j));
        if (ms.phase == PHASE_MATCH_END && matchOverAt_ < 0) matchOverAt_ = t;
        pPhase_ = ms.phase;
    }
    if (ms.scoreA != pScoreA_ || ms.scoreB != pScoreB_) {
        Json j = Json::object(); j.set("type", "score"); j.set("scoreA", (int)ms.scoreA); j.set("scoreB", (int)ms.scoreB);
        push(t, std::move(j));
        pScoreA_ = ms.scoreA; pScoreB_ = ms.scoreB;
    }
    if (ms.bombState != pBomb_) {
        static const char* kinds[] = {"bombReset", "bombCarried", "bombDropped", "plant", "explode", "defuse"};
        Json j = Json::object(); j.set("type", ms.bombState < 6 ? kinds[ms.bombState] : "bomb");
        if (ms.bombCarrier < kMaxPlayers) j.set("who", (int)ms.bombCarrier);
        else if ((ms.bombState == BOMB_PLANTED || ms.bombState == BOMB_DEFUSED) && ms.actionPlayer < kMaxPlayers) j.set("who", (int)ms.actionPlayer);
        if (ms.bombState != BOMB_NONE) j.set("pos", qv(ms.bombPos));
        push(t, std::move(j));
        pBomb_ = ms.bombState;
    }
    if (ms.hillOwner != pHill_) {
        Json j = Json::object(); j.set("type", "hill"); j.set("owner", ms.hillOwner == TEAM_A ? Json("a") : ms.hillOwner == TEAM_B ? Json("b") : Json());
        push(t, std::move(j));
        pHill_ = ms.hillOwner;
    }
    // payload: the clock jumps up by the checkpoint bonus when the cart passes a checkpoint (the game says it only in a
    // message, which no bot receives)
    if ((ms.mode == MODE_PAYLOAD) && ms.phase == PHASE_LIVE && pPhase_ == PHASE_LIVE && ms.timeLeft > pTimeLeft_ + 1.0f) {
        Json j = Json::object(); j.set("type", "cartCheckpoint"); j.set("bonus", r1(ms.timeLeft - pTimeLeft_)); j.set("dist", r1(ms.payloadDist));
        push(t, std::move(j));
    }
    pTimeLeft_ = ms.timeLeft;
    for (int i = 0; i < kMaxPlayers; i++) {
        const ServerPlayer& p = s.players_[i];
        Prev& pv = prev_[i];
        if (!p.connected) { pv = Prev(); continue; }
        if (p.checkpoint > pv.checkpoint) { Json j = Json::object(); j.set("type", "checkpoint"); j.set("who", i); j.set("index", p.checkpoint); push(t, std::move(j)); }
        if (p.finished && !pv.finished) { Json j = Json::object(); j.set("type", "finish"); j.set("who", i); push(t, std::move(j)); }
        if (p.it && !pv.it && isItMode(ms.mode)) { Json j = Json::object(); j.set("type", "it"); j.set("who", i); push(t, std::move(j)); }
        pv.alive = p.alive; pv.checkpoint = p.checkpoint; pv.finished = p.finished; pv.it = p.it;
    }
}

// "TEAM A wins the round", "BOT ANNA wins the match", "match draw", ... -> a team and / or a player (-1: none)
void ModeMatch::parseWinner(const std::string& text, int& team, int& player) const {
    const Server& s = *server_;
    team = player = -1;
    if (text.compare(0, 6, "TEAM A") == 0) { team = TEAM_A; return; }
    if (text.compare(0, 6, "TEAM B") == 0) { team = TEAM_B; return; }
    size_t best = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        const ServerPlayer& p = s.players_[i];
        if (!p.connected || p.name.empty() || p.name.size() <= best) continue;
        if (text.compare(0, p.name.size(), p.name) == 0 && text.size() > p.name.size() && text[p.name.size()] == ' ') { best = p.name.size(); player = i; }
    }
    if (player >= 0 && modeIsTeamBased(s.ms_.mode)) team = s.players_[player].team;
}

void ModeMatch::readLog() {
    fflush(nullptr);   // (the server's game log is a FILE*: what it wrote so far)
    FILE* f = u8fopen(logPath_, "rb");
    if (!f) return;
    if (fseek(f, logPos_, SEEK_SET) != 0) { fclose(f); return; }
    char line[2048];
    Server& s = *server_;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n == 0 || line[n - 1] != '\n') break;   // (a line still being written: next time)
        logPos_ += (long)n;
        line[n - 1] = 0;
        if (n >= 2 && line[n - 2] == '\r') line[n - 2] = 0;
        if (!started_) continue;
        unsigned long long sec = 0; unsigned ms = 0; int off = 0;
        if (sscanf(line, "%llu.%u %n", &sec, &ms, &off) < 2) continue;
        const char* msg = line + off;
        const double t = sec + ms * 0.001 - t0_;
        Json j = Json::object();
        int a = -1, b = -1;
        if (!strncmp(msg, "kill ", 5)) {
            const char* k = strstr(msg, "killer="); const char* v = strstr(msg, "victim="); const char* wp = strstr(msg, "weapon=");
            if (!k || !v || !wp) continue;
            a = atoi(k + 7); b = atoi(v + 7); int w = atoi(wp + 7);
            j.set("type", "kill"); j.set("killer", a); j.set("victim", b);
            j.set("weapon", w >= 0 && w < WPN_COUNT ? weaponDef(w).name : w == 252 ? "fell" : w == 253 ? "bombed" : w == 254 ? "drained" : w == 251 ? "grapple" : a == b ? "self" : "world");
            if (b >= 0 && b < kMaxPlayers) j.set("pos", qv(s.players_[b].move.pos));
        } else if (sscanf(msg, "point %d taken by team %d", &a, &b) == 2) {
            j.set("type", "capture"); j.set("index", a - 1); j.set("point", std::string(1, char('A' + std::max(0, a - 1)))); j.set("team", teamStr(b));
        } else if (sscanf(msg, "tag %d -> %d", &a, &b) == 2) {
            j.set("type", "tag"); j.set("who", a); j.set("victim", b);
        } else if (!strncmp(msg, "round ", 6) && strstr(msg, " end: ")) {
            int round = atoi(msg + 6);
            std::string text = strstr(msg, " end: ") + 6;
            size_t sc = text.rfind(" (score ");
            if (sc != std::string::npos) text.resize(sc);
            std::string why;
            if (!text.empty() && text.back() == ')') { size_t o = text.rfind(" ("); if (o != std::string::npos) { why = text.substr(o + 2, text.size() - o - 3); } }
            int wt, wp; parseWinner(text, wt, wp);
            j.set("type", "roundEnd"); j.set("round", round); j.set("text", text);
            if (!why.empty()) j.set("reason", why);
            j.set("winner", wt >= 0 ? Json(teamStr(wt)) : Json());
            if (wp >= 0) j.set("player", wp);
            j.set("scoreA", (int)s.ms_.scoreA); j.set("scoreB", (int)s.ms_.scoreB);
            Json r = j; r.set("t", r2(t));
            rounds_.push(std::move(r));
        } else if (!strncmp(msg, "match end: ", 11)) {
            matchText_ = msg + 11;
            parseWinner(matchText_, winTeam_, winPlayer_);
            if (matchOverAt_ < 0) matchOverAt_ = t;
            j.set("type", "matchEnd"); j.set("text", matchText_);
            j.set("winner", winTeam_ >= 0 ? Json(teamStr(winTeam_)) : Json());
            if (winPlayer_ >= 0) j.set("player", winPlayer_);
        } else if (!strcmp(msg, "overtime")) j.set("type", "overtime");
        else if (!strncmp(msg, "round ", 6) && strstr(msg, " modifier ")) { j.set("type", "modifier"); j.set("text", strstr(msg, " modifier ") + 10); }
        else if (!strncmp(msg, "rps tiebreak: ", 14)) { j.set("type", "tiebreak"); j.set("text", msg + 14); }
        else if (!strncmp(msg, "tourney ", 8)) { j.set("type", "tourney"); j.set("text", msg + 8); }
        else if (!strncmp(msg, "map ", 4) || !strncmp(msg, "quark server log", 16) || !strncmp(msg, "server stopped", 14)) continue;
        else { j.set("type", "log"); j.set("text", std::string(msg).substr(0, 300)); }   // (anything else the game logs: a mode's log() / announce lines)
        push(t, std::move(j));
    }
    fclose(f);
}

Json ModeMatch::startBots() const {
    const Server& s = *server_;
    Json a = Json::array();
    for (size_t k = 0; k < ids_.size(); k++) {
        const int id = ids_[k];
        if (id < 0) continue;
        const ServerPlayer& p = s.players_[id];
        Json o = Json::object();
        o.set("id", id); o.set("name", p.name); o.set("team", teamStr(p.team)); o.set("skill", botSkillName(s.bots_[id].skill));
        o.set("brain", brains_[k].empty() ? "default" : brains_[k]);
        a.push(std::move(o));
    }
    return a;
}

Json ModeMatch::frame(bool positions) const {
    const Server& s = *server_;
    const ModeState& ms = s.ms_;
    const Map& map = s.map_;
    Json f = Json::object();
    f.set("type", "frame"); f.set("t", r2(time())); f.set("tick", (double)s.tick_);
    f.set("phase", phaseName(ms.phase)); f.set("round", (int)ms.round); f.set("clock", r1(ms.timeLeft));
    f.set("scoreA", (int)ms.scoreA); f.set("scoreB", (int)ms.scoreB);
    Json fl = Json::array();
    if (ms.flags & MS_OVERTIME) fl.push("overtime");
    if (ms.flags & MS_CONTESTED) fl.push("contested");
    if (ms.flags & MS_DRAIN) fl.push("drain");
    if (ms.flags & MS_RPS) fl.push("rps");
    f.set("flags", std::move(fl));
    if (modeIsTeamBased(ms.mode)) f.set("attackers", teamStr(ms.attackers));
    Json obj = Json::object();
    if (ms.mode == MODE_BOMB) {
        Json b = Json::object();
        b.set("state", ms.bombState < 6 ? kBombState[ms.bombState] : "?");
        b.set("carrier", ms.bombCarrier < kMaxPlayers ? Json((int)ms.bombCarrier) : Json());
        if (ms.bombState != BOMB_NONE) b.set("pos", qv(ms.bombPos));
        if (ms.bombState == BOMB_PLANTED) b.set("timer", r1(ms.bombTimer));
        if (ms.actionPlayer < kMaxPlayers) { b.set("acting", (int)ms.actionPlayer); b.set("progress", r2(ms.actionProgress)); }
        obj.set("bomb", std::move(b));
    }
    if ((ms.mode == MODE_PAYLOAD || ms.mode == MODE_NPAYLOAD) && map.payloadPath.size() >= 2) {
        Json c = Json::object(); c.set("dist", r1(ms.payloadDist)); c.set("len", r1(ms.payloadLen));
        obj.set("cart", std::move(c));
    }
    if (ms.mode == MODE_KOTH && map.hasHill) {
        Json h = Json::object(); h.set("owner", ms.hillOwner == TEAM_A ? Json("a") : ms.hillOwner == TEAM_B ? Json("b") : Json()); h.set("target", (int)ms.target);
        obj.set("hill", std::move(h));
    }
    if (ms.mode == MODE_POINTS && !map.points.empty()) {
        Json p = Json::object(); p.set("index", (int)ms.point); p.set("count", (int)ms.pointCount); p.set("capture", r2(ms.capture));
        obj.set("point", std::move(p));
    }
    if (obj.size()) f.set("obj", std::move(obj));
    Json pl = Json::array();
    const bool it = isItMode(ms.mode);
    for (int i = 0; i < kMaxPlayers; i++) {
        const ServerPlayer& p = s.players_[i];
        if (!p.connected) continue;
        Json o = Json::object();
        o.set("id", i); o.set("team", teamStr(p.team)); o.set("alive", p.alive);
        if (positions) o.set("pos", qv(p.move.pos));
        o.set("score", p.score); o.set("kills", p.kills); o.set("deaths", p.deaths); o.set("objTime", r1(p.objTime));
        if (it) { o.set("it", p.it); o.set("out", p.out); if (ms.mode == MODE_HIDE) o.set("found", p.found); }
        if (ms.mode == MODE_RACE) { o.set("checkpoint", p.checkpoint); o.set("finished", p.finished); }
        pl.push(std::move(o));
    }
    f.set("players", std::move(pl));
    return f;
}

Json ModeMatch::winner() const {
    if (!matchOver()) return Json();
    const Server& s = *server_;
    Json w = Json::object();
    w.set("team", winTeam_ >= 0 ? Json(teamStr(winTeam_)) : Json());
    w.set("player", winPlayer_ >= 0 ? Json(winPlayer_) : Json());
    w.set("name", winPlayer_ >= 0 ? Json(s.players_[winPlayer_].name) : winTeam_ >= 0 ? Json(winTeam_ == TEAM_A ? "TEAM A" : "TEAM B") : Json());
    w.set("text", matchText_);
    return w;
}

// who's ahead when the time ran out first (team modes by score, else by player score); null: level
Json ModeMatch::leader() const {
    const Server& s = *server_;
    Json l = Json::object();
    if (modeIsTeamBased(s.ms_.mode)) {
        if (s.ms_.scoreA == s.ms_.scoreB) return Json();
        l.set("team", s.ms_.scoreA > s.ms_.scoreB ? "a" : "b");
        return l;
    }
    int best = -1, bs = -(1 << 30), ties = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        const ServerPlayer& p = s.players_[i];
        if (!p.connected || p.team == TEAM_SPECTATOR) continue;
        if (p.score > bs) { bs = p.score; best = i; ties = 1; } else if (p.score == bs) ties++;
    }
    if (best < 0 || ties > 1) return Json();
    l.set("player", best);
    return l;
}

Json ModeMatch::result() const {
    const Server& s = *server_;
    Json r = Json::object();
    r.set("scoreA", (int)s.ms_.scoreA); r.set("scoreB", (int)s.ms_.scoreB); r.set("round", (int)s.ms_.round); r.set("phase", phaseName(s.ms_.phase));
    Json pl = Json::array();
    for (int i = 0; i < kMaxPlayers; i++) {
        const ServerPlayer& p = s.players_[i];
        if (!p.connected) continue;
        Json o = Json::object();
        o.set("id", i); o.set("name", p.name); o.set("team", teamStr(p.team));
        o.set("score", p.score); o.set("kills", p.kills); o.set("deaths", p.deaths); o.set("objTime", r1(p.objTime)); o.set("damage", r1(p.dmgDealt));
        bool won = matchOver() && ((winPlayer_ >= 0 && winPlayer_ == i) || (winTeam_ >= 0 && modeIsTeamBased(s.ms_.mode) && winTeam_ == p.team));
        o.set("won", won);
        pl.push(std::move(o));
    }
    r.set("players", std::move(pl));
    return r;
}

// avg / p99 / p99.9 / max of nanoseconds per tick (or per call), and when the max was: maxAt (game seconds since the
// match start) and maxTick, when tickOf(i) gives the tick of src[i] (ticks are 1 / tickRate s apart). null: none.
Json nsStats(const std::vector<float>& src, const std::function<long long(size_t)>& tickOf = nullptr, int tickRate = 0) {
    if (src.empty()) return Json();
    const size_t at = (size_t)(std::max_element(src.begin(), src.end()) - src.begin());
    std::vector<float> v = src;
    double sum = 0; for (float x : v) sum += x;
    auto pct = [&](size_t per1000) { size_t k = std::min(v.size() - 1, v.size() * per1000 / 1000); std::nth_element(v.begin(), v.begin() + k, v.end()); return v[k]; };
    const float p999 = pct(999), p99 = pct(990);
    Json o = Json::object();
    o.set("avg", std::round(sum / v.size())); o.set("p99", std::round(p99)); o.set("p999", std::round(p999)); o.set("max", std::round(src[at]));
    o.set("count", (double)v.size());
    if (tickOf && tickRate > 0) {
        const long long tk = tickOf(at);
        o.set("maxTick", (double)tk); o.set("maxAt", r2((tk + 1) * (double)(1000 / tickRate) / 1000.0));   // (the server's clock steps 1000 / rate ms a tick)
    }
    return o;
}

Json aggJson(int slot, const TimedHooks::Agg& a) {
    Json o = Json::object();
    o.set("slot", hookSlotName(slot)); o.set("calls", (double)a.calls); o.set("ns", std::round(a.ns));
    o.set("avg", a.calls ? std::round(a.ns / a.calls) : 0.0); o.set("max", std::round(a.max));
    return o;
}

Json ModeMatch::slotFrame() {
    Json a = Json::array();
    if (!timed_) return a;
    const TimedHooks::Aggs f = timed_->takeFrame();
    for (int k = 0; k < (int)f.size(); k++) if (f[k].calls) a.push(aggJson(k, f[k]));
    return a;
}

Json ModeMatch::slotTotals() const {
    Json a = Json::array();
    if (!timed_) return a;
    const TimedHooks::Aggs& t = timed_->totals();
    std::vector<int> order;
    for (int k = 0; k < (int)t.size(); k++) if (t[k].calls) order.push_back(k);
    std::sort(order.begin(), order.end(), [&](int x, int y) { return t[x].ns > t[y].ns; });   // (the costliest first)
    for (int k : order) a.push(aggJson(k, t[k]));
    return a;
}

Json ModeMatch::mapView() const {
    const Map& m = server_->map_;
    auto box = [](float x0, float z0, float x1, float z1) { Json a = Json::array(); a.push(r1(x0)); a.push(r1(z0)); a.push(r1(x1)); a.push(r1(z1)); return a; };
    auto aabb = [&](const AABB& b) { return box(b.mn.x, b.mn.z, b.mx.x, b.mx.z); };
    auto xz = [](const Vec3& v) { Json a = Json::array(); a.push(r1(v.x)); a.push(r1(v.z)); return a; };
    Json v = Json::object();
    // the solid boxes (not distant scenery, trim, water, lava or clip), each [x0, z0, x1, z1, top y], lowest first so
    // the plot paints higher floors over lower ones
    std::vector<const Brush*> bs;
    for (const Brush& b : m.brushes) if (b.material != 5 && b.material < 6) bs.push_back(&b);
    std::stable_sort(bs.begin(), bs.end(), [](const Brush* a, const Brush* b) { return a->box.mx.y < b->box.mx.y; });
    Json boxes = Json::array();
    float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (const Brush* b : bs) {
        Json a = aabb(b->box); a.push(r1(b->box.mx.y));
        boxes.push(std::move(a));
        x0 = std::min(x0, b->box.mn.x); z0 = std::min(z0, b->box.mn.z); x1 = std::max(x1, b->box.mx.x); z1 = std::max(z1, b->box.mx.z);
        y0 = std::min(y0, b->box.mx.y); y1 = std::max(y1, b->box.mx.y);
    }
    if (boxes.size()) { v.set("bounds", box(x0, z0, x1, z1)); Json y = Json::array(); y.push(r1(y0)); y.push(r1(y1)); v.set("heights", std::move(y)); }
    v.set("boxes", std::move(boxes));
    Json sp = Json::array();
    for (const SpawnPoint& p : m.spawns) { Json a = xz(p.pos); a.push(p.team); sp.push(std::move(a)); }
    v.set("spawns", std::move(sp));   // [x, z, SPAWN_* team]
    Json sites = Json::array(); for (const AABB& b : m.bombSites) sites.push(aabb(b)); v.set("sites", std::move(sites));
    if (m.hasHill) v.set("hill", aabb(m.hill));
    Json pts = Json::array(); for (const AABB& b : m.points) pts.push(aabb(b)); v.set("points", std::move(pts));
    Json cps = Json::array(); for (const AABB& b : m.checkpoints) cps.push(aabb(b)); v.set("checkpoints", std::move(cps));
    Json path = Json::array(); for (const Vec3& p : m.payloadPath) path.push(xz(p)); v.set("cart", std::move(path));
    return v;
}

Json ModeMatch::perf() const {
    Json p = Json::object();
    p.set("ticks", (double)ticks_);
    p.set("simSeconds", simWall_);
    p.set("ticksPerSecond", simWall_ > 0 ? ticks_ / simWall_ : 0.0);
    p.set("speedup", simWall_ > 0 ? time() / simWall_ : 0.0);   // game seconds per wall second (x real time)
    const int rate = tickRate();
    Json tn = nsStats(tickNs_, [](size_t i) { return (long long)i; }, rate);
    if (!tn.isNull()) {   // the match's first second (forcestart's round setup: spawns, the logic's first slots) apart, so
                          // a one-off start-up spike reads as that, not as what a tick usually costs at its worst
        tn.set("first", std::round(tickNs_[0]));
        const size_t startTicks = (size_t)std::max(1, rate);
        if (tickNs_.size() > startTicks) tn.set("maxAfterStart", std::round(*std::max_element(tickNs_.begin() + startTicks, tickNs_.end())));
        tn.set("ticks", (double)tickNs_.size());
    }
    p.set("tickNs", tn.isNull() ? Json::object() : tn);   // Server::simulate per tick (everything: movement, bots, rules, logic)
    Json ln = nsStats(logicNs_, [this](size_t i) { return (long long)logicTick_[i]; }, rate);
    if (!ln.isNull()) ln.set("ticks", (double)logicNs_.size());
    p.set("logicNs", std::move(ln));   // the interpreted slots' share of each tick (null: no logic ran)
    if (timed_ && timed_->callCount()) {   // one interpreted slot call (any slot): the cost of the logic when it does run
        Json c = nsStats(timed_->callNs());
        c.set("calls", (double)timed_->callCount());
        if ((long long)timed_->callNs().size() < timed_->callCount()) c.set("sampled", (double)timed_->callNs().size());   // (the first this many)
        p.set("callNs", std::move(c));
    } else p.set("callNs", Json());
    return p;
}

// the logic hook, part 2 (see the TODO at the top): the server runs logic, and the file has some
bool fileLogicRunning(const ModeMatch& M, const Setup& S) { return M.runsLogic() && !S.logicBlock.empty() && S.compiled.ok && M.logicHash() == S.compiled.hash; }

// ---- shared pieces of the commands ---------------------------------------------------------------------------------
// a data folder for the run(s): maps/<name>.qmap, made the game's user data folder (never the player's real one)
bool prepareDir(const Setup& S, const char* tag, std::string& dir, std::string& err) {
    std::error_code ec;
    fs::path d = fs::temp_directory_path(ec) / (std::string("quark_") + tag + "_" + std::to_string((long long)(maptoolNow() * 1000)) + "_" + std::to_string(g_serial++));
    fs::create_directories(d / "maps", ec);
    if (ec) { err = "can't make a temp folder: " + ec.message(); return false; }
    dir = d.u8string();
    if (!saveMapFile((d / "maps" / (S.mapName + ".qmap")).u8string(), S.mapText)) { err = "can't write the map"; return false; }
    if (!S.brains.empty()) fs::create_directories(d / "bots", ec);
    for (auto& [name, text] : S.brains) {   // (Server::addBot loads bots/<name>.qbrain from the game's data folder)
        std::ofstream f(d / "bots" / (name + ".qbrain"), std::ios::binary);
        if (!(f << text)) { err = "can't write the brain " + name; return false; }
    }
    setUserDataDir(dir);
    g_serverRateCheck = false;   // (no real-time clock to keep up with: never step the rate down)
    return true;
}

// known: false in modebatch's start line (no match has run yet: its end line says what the server did)
Json logicJson(const Setup& S, bool running, bool known = true) {
    Json l = Json::object();
    l.set("present", !S.logicBlock.empty());
    l.set("applied", running);
    l.set("hook", kServerHasLogicHook ? "game" : "pending");
    if (S.compiled.ok) {
        l.set("label", S.compiled.label); l.set("hash", S.compiled.hash);
        l.set("interpreted", S.check["interpreted"]); l.set("uses", S.check["uses"]); l.set("needs", S.check["needs"]);
        Json c = Json::object();
        for (auto& sl : S.check["slots"].items()) if (sl["interpreted"].boolean()) c.set(sl["slot"].str(), sl["worstCase"]);
        l.set("worstCase", std::move(c));
    }
    if (!S.logicBlock.empty() && !running)
        l.set("why", kServerHasLogicHook ? (known ? "the server didn't load the logic" : "known once a match has run (the end line)") :"this build's server doesn't run mode logic yet (mode editor phase 3): the match plays the base mode with the file's key=value rules");
    return l;
}

void startCommon(Json& s, const Setup& S, bool running, bool known = true) {
    s.set("map", S.mapName); s.set("base", modeName(S.mode)); s.set("mode", modeName(S.mode));
    s.set("label", S.label);
    s.set("custom", S.custom.active() ? Json(S.custom.name) : Json());
    s.set("logicApplied", running);
    s.set("interpreted", running ? S.check["interpreted"] : Json::array());   // slots the server interprets (logic.interpreted: what the file would)
    s.set("logic", logicJson(S, running, known));
    s.set("teams", modeIsTeamBased(S.mode));
    s.set("tickRate", S.tickRate); s.set("seconds", S.seconds); s.set("until", S.untilMatchEnd ? "matchEnd" : "seconds");
    if (!S.brainLabels.empty()) { Json b = Json::object(); for (auto& [n, l] : S.brainLabels) b.set(n, l); s.set("brains", std::move(b)); }
    Json w = Json::array();
    for (auto& x : S.warnings) w.push(x);
    for (auto& x : S.check["warnings"].items()) w.push(std::to_string(x["line"].i()) + ":" + std::to_string(x["col"].i()) + " " + x["msg"].str());
    s.set("warnings", std::move(w));
}

struct Controls { std::mutex mu; std::deque<std::string> q; };
std::shared_ptr<Controls> readControls(CmdCtx& ctx) {
    auto ctl = std::make_shared<Controls>();
    if (ctx.control) {
        std::istream* in = ctx.control;
        std::thread([ctl, in] {
            std::string line;
            while (std::getline(*in, line)) { std::lock_guard<std::mutex> l(ctl->mu); ctl->q.push_back(line); }
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
int failWith(Json& out, const std::string& err, const Json& errors) {
    cmdError(out, err);
    if (errors.isArr()) out.set("errors", errors);
    return 1;
}

// ---- modesim --------------------------------------------------------------------------------------------------------
int runModeSim(const Json& req, Json& out, CmdCtx& ctx) {
    const double w0 = maptoolNow();
    Setup S;
    std::string err;
    Json errors;
    if (!parseSetup(req, S, false, err, &errors)) return failWith(out, err, errors);
    netInit();
    std::string dir;
    if (!prepareDir(S, "modesim", dir, err)) return cmdError(out, err);
    struct Cleanup { std::string d; ~Cleanup() { std::error_code ec; fs::remove_all(utf8Path(d), ec); } } cleanup{dir};
    ModeMatch M;
    if (!M.start(S, S.seed, dir, &err)) return cmdError(out, err);
    const double startSeconds = maptoolNow() - w0;
    const int ticksPer = std::max(1, (int)std::lround(M.tickRate() / S.rate));
    const bool running = fileLogicRunning(M, S);
    {
        Json s = Json::object();
        s.set("type", "start");
        startCommon(s, S, running);
        s.set("seed", (double)S.seed); s.set("rate", (double)M.tickRate() / ticksPer);
        s.set("realtime", S.realtime); s.set("speed", S.speed); s.set("trace", S.trace && running);   // (frames carry `slots`)
        s.set("bots", M.startBots());
        if (S.mapView) s.set("mapView", M.mapView());
        Json log = Json::array();
        for (auto& l : M.serverLog())   // (not the log file or the ephemeral port: the same request gives the same lines)
            if (l.compare(0, 8, "logging ") != 0 && l.compare(0, 13, "listening on ") != 0) log.push(l);
        if (log.size()) s.set("serverLog", std::move(log));
        ctx.emit(s);
        ctx.emit(M.frame(S.positions));   // (the match start: t = 0)
    }
    auto ctl = readControls(ctx);
    bool paused = false, stopped = false;
    int steps = 0;
    double speed = S.speed;
    double wallBase = maptoolNow(), simBase = M.time();
    std::string reason = "time";
    for (;;) {
        for (auto& c : takeControls(*ctl)) {
            if (c == "pause") paused = true;
            else if (c == "resume") { paused = false; wallBase = maptoolNow(); simBase = M.time(); }
            else if (c == "step") steps++;
            else if (c == "stop") stopped = true;
            else if (c.compare(0, 6, "speed ") == 0) {
                std::string v = c.substr(6);
                if (v == "max") { S.realtime = false; } else { speed = std::max(0.0, atof(v.c_str())); S.realtime = speed > 0; }
                wallBase = maptoolNow(); simBase = M.time();
            }
            else if (c == "realtime on" || c == "realtime off") { S.realtime = c == "realtime on"; wallBase = maptoolNow(); simBase = M.time(); }
            else { Json e = Json::object(); e.set("type", "warning"); e.set("warning", "unknown control '" + c + "'"); ctx.emit(e); }
        }
        if (stopped) { reason = "stopped"; break; }
        if (S.untilMatchEnd && M.matchOver()) { reason = "matchEnd"; break; }
        if (M.time() >= S.seconds - 1e-9) { reason = "time"; break; }
        if (paused && steps == 0) { sleepMs(10); continue; }
        if (paused) steps--;
        int want = std::min(ticksPer, std::max(1, (int)std::lround((S.seconds - M.time()) * M.tickRate())));
        M.step(want, S.untilMatchEnd);
        Json f = M.frame(S.positions);
        Json ev = Json::array();
        for (auto& e : M.takeEvents()) ev.push(std::move(e.j));
        f.set("events", std::move(ev));
        if (S.trace && running) f.set("slots", M.slotFrame());
        ctx.emit(f);
        if (S.realtime && speed > 0 && !paused) {
            double due = wallBase + (M.time() - simBase) / speed, now = maptoolNow();
            if (due > now) std::this_thread::sleep_for(std::chrono::duration<double>(std::min(due - now, 1.0)));
        }
    }
    Json d = Json::object();
    d.set("type", "end"); d.set("t", r2(M.time())); d.set("reason", reason);
    d.set("label", S.label); d.set("logicApplied", running);
    d.set("winner", M.winner());
    if (!M.matchOver()) d.set("leader", M.leader());
    d.set("rounds", M.roundsPlayed());
    d.set("roundResults", M.rounds());
    d.set("result", M.result());
    Json perf = M.perf();
    perf.set("wallSeconds", maptoolNow() - w0); perf.set("startSeconds", startSeconds);
    if (running) perf.set("slots", M.slotTotals());   // every interpreted slot's calls and cost over the match
    d.set("perf", std::move(perf));
    M.stop();
    finishLine(ctx, out, std::move(d));
    return 0;
}

// ---- modebatch ------------------------------------------------------------------------------------------------------
struct MatchResult {
    int index = 0;
    uint32_t seed = 0;
    std::string reason, error;   // reason "" = never started (stopped)
    double t = 0, wall = 0, simWall = 0;
    long long ticks = 0;
    int rounds = 0, scoreA = 0, scoreB = 0;
    Json winner, leader, players, logicNs;
    bool applied = false;
};

std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += '"'; o += c; }
    return o + "\"";
}

int runModeBatch(const Json& req, Json& out, CmdCtx& ctx) {
    const double w0 = maptoolNow();
    Setup S;
    std::string err;
    Json errors;
    if (!parseSetup(req, S, true, err, &errors)) return failWith(out, err, errors);
    std::vector<uint32_t> seeds;
    const uint32_t first = (uint32_t)req["firstSeed"].num(req["seed"].num(1));
    if (req["seeds"].isArr()) { for (auto& s : req["seeds"].items()) if (s.isNum()) seeds.push_back((uint32_t)s.num()); }
    else {
        int n = std::clamp(req["seeds"].isNum() ? req["seeds"].i() : req["matches"].i(8), 1, 10000);
        for (int k = 0; k < n; k++) seeds.push_back(first + (uint32_t)k);
    }
    if (seeds.empty()) return cmdError(out, "no seeds");
    const int N = (int)seeds.size();
    unsigned hw = std::thread::hardware_concurrency();
    const int defThreads = std::max(1, (int)(hw ? hw : 2) - 1);   // every core but one
    const int T = std::clamp(req["threads"].i(defThreads), 1, std::max(1, std::min(N, 64)));
    const double progressEvery = std::max(0.0, req["progressEvery"].num(1.0));
    const double matchWallLimit = std::max(0.0, req["matchTimeout"].num(0));   // wall seconds per match (0: none)
    netInit();
    std::string dir;
    if (!prepareDir(S, "modebatch", dir, err)) return cmdError(out, err);
    struct Cleanup { std::string d; ~Cleanup() { std::error_code ec; fs::remove_all(utf8Path(d), ec); } } cleanup{dir};
    {
        Json s = Json::object();
        s.set("type", "start");
        startCommon(s, S, false, false);   // (the end line says what the server did: logicApplied there)
        s.set("matches", N); s.set("threads", T);
        Json sj = Json::array(); for (auto x : seeds) sj.push((double)x); s.set("seeds", sj);
        Json bots = Json::array();
        for (size_t k = 0; k < S.bots.size(); k++) {
            Json o = Json::object(); o.set("index", (int)k); o.set("team", teamStr(S.bots[k].team)); o.set("skill", botSkillName(S.bots[k].skill));
            if (!S.bots[k].name.empty()) o.set("name", S.bots[k].name);
            bots.push(std::move(o));
        }
        s.set("bots", std::move(bots));
        ctx.emit(s);
    }
    auto ctl = readControls(ctx);
    std::atomic<bool> cancel{false};
    std::atomic<int> next{0};
    std::vector<MatchResult> results(N);
    std::mutex mu;
    std::condition_variable cv;
    std::deque<int> finished;
    auto playOne = [&](int i) {
        MatchResult& R = results[i];
        R.index = i; R.seed = seeds[i];
        double a = maptoolNow();
        ModeMatch M;
        std::string e;
        if (!M.start(S, seeds[i], dir, &e)) { R.error = e; R.reason = "error"; return; }
        R.applied = fileLogicRunning(M, S);
        const int chunk = std::max(1, M.tickRate());
        std::string reason = "time";
        while (true) {
            if (cancel) { reason = "stopped"; break; }
            if (S.untilMatchEnd && M.matchOver()) { reason = "matchEnd"; break; }
            if (M.time() >= S.seconds - 1e-9) break;
            if (matchWallLimit > 0 && maptoolNow() - a > matchWallLimit) { reason = "wallTimeout"; break; }
            M.step(std::min(chunk, std::max(1, (int)std::lround((S.seconds - M.time()) * M.tickRate()))), S.untilMatchEnd);
            M.takeEvents();
        }
        R.reason = reason; R.t = M.time(); R.ticks = M.ticks(); R.simWall = M.simWall();
        R.winner = M.winner();
        R.leader = M.matchOver() ? Json() : M.leader();
        R.rounds = M.roundsPlayed();
        R.scoreA = M.server().ms_.scoreA; R.scoreB = M.server().ms_.scoreB;
        R.players = M.result()["players"];
        R.logicNs = M.perf()["logicNs"];
        M.stop();
        R.wall = maptoolNow() - a;
    };
    std::vector<std::thread> drivers;
    std::atomic<int> driversDone{0};
    for (int w = 0; w < T; w++)
        drivers.emplace_back([&] {
            // one match per fresh thread: bots.cpp keeps per-thread state (its route budget), so the same seed gives
            // the same match as modesim whichever thread ran what before
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
        last = driversDone == T;
        std::vector<int> got;
        {
            std::unique_lock<std::mutex> l(mu);
            if (!last) cv.wait_for(l, std::chrono::milliseconds(100), [&] { return !finished.empty() || driversDone == T; });
            while (!finished.empty()) { got.push_back(finished.front()); finished.pop_front(); }
        }
        for (auto& c : takeControls(*ctl)) {
            if (c == "stop") cancel = true;
            else { Json e = Json::object(); e.set("type", "warning"); e.set("warning", "modebatch takes only 'stop' (not '" + c + "')"); ctx.emit(e); }
        }
        for (int i : got) {
            done++;
            const MatchResult& R = results[i];
            Json p = Json::object();
            p.set("type", "match"); p.set("done", done); p.set("total", N); p.set("index", i); p.set("seed", (double)R.seed);
            if (!R.error.empty()) p.set("error", R.error);
            p.set("reason", R.reason); p.set("t", r2(R.t)); p.set("winner", R.winner);
            if (!R.leader.isNull()) p.set("leader", R.leader);
            p.set("rounds", R.rounds); p.set("scoreA", R.scoreA); p.set("scoreB", R.scoreB);
            p.set("wallSeconds", R.wall); p.set("players", R.players);
            ctx.emit(p);
        }
        double now = maptoolNow();
        if (!last && progressEvery > 0 && now - lastProgress >= progressEvery) {
            lastProgress = now;
            Json p = Json::object();
            p.set("type", "progress"); p.set("done", done); p.set("total", N); p.set("wallSeconds", now - w0);
            ctx.emit(p);
        }
    }
    for (auto& d : drivers) d.join();
    // ---- the summary
    const bool teams = modeIsTeamBased(S.mode);
    int winsA = 0, winsB = 0, draws = 0, timeouts = 0, errorsN = 0, played = 0, applied = 0;
    std::vector<double> lengths;
    double rounds = 0, spread = 0, gameSecs = 0, simWall = 0;
    long long ticks = 0;
    struct Agg { int matches = 0, wins = 0, kills = 0, deaths = 0, score = 0; double objTime = 0, damage = 0; std::string name, team; };
    std::vector<Agg> perBot(S.bots.size());
    std::string csv = "index,seed,reason,seconds,rounds,winnerTeam,winnerPlayer,winnerName,scoreA,scoreB,wallSeconds\n";
    for (auto& R : results) {
        if (R.reason.empty()) continue;
        if (!R.error.empty()) { errorsN++; csv += std::to_string(R.index) + "," + std::to_string(R.seed) + ",error,,,,,,,," + "\n"; continue; }
        played++;
        applied += R.applied;
        ticks += R.ticks; simWall += R.simWall; gameSecs += R.t;
        lengths.push_back(R.t);
        rounds += R.rounds;
        if (R.reason != "matchEnd") timeouts++;
        else {
            const Json& wt = R.winner["team"];
            if (teams) { if (wt.str() == "a") winsA++; else if (wt.str() == "b") winsB++; else draws++; }
            else if (!R.winner["player"].isNum()) draws++;
        }
        if (teams) spread += std::abs(R.scoreA - R.scoreB);
        else {   // free for all and the like: the winner's score minus the runner-up's
            std::vector<int> sc; for (auto& p : R.players.items()) sc.push_back(p["score"].i());
            std::sort(sc.rbegin(), sc.rend());
            spread += sc.size() >= 2 ? sc[0] - sc[1] : 0;
        }
        for (size_t k = 0; k < perBot.size() && k < R.players.size(); k++) {
            const Json& p = R.players[k];
            Agg& a = perBot[k];
            a.matches++; a.wins += p["won"].boolean() ? 1 : 0;
            a.kills += p["kills"].i(); a.deaths += p["deaths"].i(); a.score += p["score"].i();
            a.objTime += p["objTime"].num(); a.damage += p["damage"].num();
            if (a.name.empty()) { a.name = p["name"].str(); a.team = p["team"].str(); }
        }
        char buf[64];
        snprintf(buf, sizeof buf, "%.2f", R.t);
        std::string wt = R.winner["team"].str(), wp = R.winner["player"].isNum() ? std::to_string(R.winner["player"].i()) : "";
        csv += std::to_string(R.index) + "," + std::to_string(R.seed) + "," + R.reason + "," + buf + "," + std::to_string(R.rounds) + "," + wt + "," + wp + "," +
               csvField(R.winner["name"].str()) + "," + std::to_string(R.scoreA) + "," + std::to_string(R.scoreB) + ",";
        snprintf(buf, sizeof buf, "%.3f", R.wall);
        csv += std::string(buf) + "\n";
    }
    Json d = Json::object();
    d.set("type", "end");
    d.set("reason", cancel ? "stopped" : "done");
    d.set("label", S.label); d.set("base", modeName(S.mode)); d.set("map", S.mapName);
    d.set("logicApplied", played > 0 && applied == played);
    d.set("logic", logicJson(S, played > 0 && applied == played));
    d.set("matchesPlayed", played); d.set("errors", errorsN); d.set("timeouts", timeouts);
    if (teams) {
        Json t = Json::object();
        auto side = [&](int w) { Json o = Json::object(); o.set("wins", w); o.set("winRate", played ? (double)w / played : 0.0); return o; };
        t.set("a", side(winsA)); t.set("b", side(winsB)); t.set("draws", draws);
        d.set("teams", std::move(t));
    } else d.set("draws", draws);
    Json pb = Json::array();
    for (size_t k = 0; k < perBot.size(); k++) {
        const Agg& a = perBot[k];
        Json o = Json::object();
        o.set("index", (int)k); o.set("name", a.name); o.set("team", a.team.empty() ? teamStr(S.bots[k].team) : a.team); o.set("skill", botSkillName(S.bots[k].skill));
        o.set("matches", a.matches); o.set("wins", a.wins); o.set("winRate", a.matches ? (double)a.wins / a.matches : 0.0);
        double m = std::max(1, a.matches);
        o.set("kills", r2(a.kills / m)); o.set("deaths", r2(a.deaths / m)); o.set("score", r2(a.score / m)); o.set("objTime", r1(a.objTime / m)); o.set("damage", r1(a.damage / m));
        pb.push(std::move(o));
    }
    d.set("players", std::move(pb));   // per bot slot, per match averages (kills, deaths, score, objTime, damage)
    Json len = Json::object();
    if (!lengths.empty()) {
        std::vector<double> v = lengths; std::sort(v.begin(), v.end());
        double sum = 0; for (double x : v) sum += x;
        len.set("avg", r1(sum / v.size())); len.set("min", r1(v.front())); len.set("p50", r1(v[v.size() / 2])); len.set("max", r1(v.back()));
    }
    d.set("length", std::move(len));
    d.set("rounds", played ? r2(rounds / played) : 0.0);
    d.set("scoreSpread", played ? r2(spread / played) : 0.0);
    Json ms = Json::array();
    for (auto& R : results) {
        if (R.reason.empty()) continue;
        Json o = Json::object();
        o.set("index", R.index); o.set("seed", (double)R.seed); o.set("reason", R.reason); o.set("t", r2(R.t));
        if (!R.error.empty()) o.set("error", R.error);
        o.set("winner", R.winner); if (!R.leader.isNull()) o.set("leader", R.leader);
        o.set("rounds", R.rounds); o.set("scoreA", R.scoreA); o.set("scoreB", R.scoreB); o.set("wallSeconds", R.wall);
        ms.push(std::move(o));
    }
    d.set("matches", std::move(ms));
    d.set("csv", csv);
    Json perf = Json::object();
    double wall = maptoolNow() - w0;
    perf.set("wallSeconds", wall); perf.set("threads", T);
    perf.set("matchesPerSecond", wall > 0 ? played / wall : 0.0);
    perf.set("ticks", (double)ticks);
    perf.set("ticksPerSecondPerMatch", simWall > 0 ? ticks / simWall : 0.0);
    perf.set("speedupPerMatch", simWall > 0 ? gameSecs / simWall : 0.0);   // game seconds per wall second, one core
    perf.set("speedupTotal", wall > 0 ? gameSecs / wall : 0.0);            // ... all threads together
    {   // the logic's time per tick over every match: the mean of the matches' averages, the worst p99, p99.9 and max
        double avg = 0, p99 = 0, p999 = 0, mx = 0; int n = 0;
        for (auto& R : results) if (R.logicNs.isObj()) {
            n++; avg += R.logicNs["avg"].num(); p99 = std::max(p99, R.logicNs["p99"].num()); p999 = std::max(p999, R.logicNs["p999"].num()); mx = std::max(mx, R.logicNs["max"].num());
        }
        Json l;
        if (n) { l = Json::object(); l.set("avg", std::round(avg / n)); l.set("p99", p99); l.set("p999", p999); l.set("max", mx); l.set("matches", n); }
        perf.set("logicNs", std::move(l));
    }
    d.set("perf", std::move(perf));
    finishLine(ctx, out, std::move(d));
    return 0;
}

// ---- modecheck ------------------------------------------------------------------------------------------------------
int runModeCheck(const Json& req, Json& out, CmdCtx&) {
    std::string text = req["text"].str(req["rules"].str());
    if (text.empty() && req["file"].isStr()) {
        std::ifstream f(utf8Path(req["file"].str()), std::ios::binary);
        if (!f) return cmdError(out, "can't read " + req["file"].str());
        std::ostringstream ss; ss << f.rdbuf(); text = ss.str();
    }
    int base = -1;
    if (req["base"].isStr() || req["mode"].isStr()) {
        base = modeByAnyName(req["base"].str(req["mode"].str()));
        if (base < 0) return cmdError(out, "base: a mode name");
    }
    ModeCompile c = compileModeText(text, base);
    std::vector<std::string> mapErrs;
    std::string mapName;
    if (c.ok && (req["map"].isStr() || req["map"].isObj())) {
        Map m; std::string err;
        if (!loadMap(req, m, err)) mapErrs.push_back("can't load the map: " + err);
        else {
            mapName = m.name;
            auto need = [&](const char* key, bool has, const char* what) {
                if (!has && std::find(c.needs.begin(), c.needs.end(), key) != c.needs.end()) mapErrs.push_back(m.name + " has no " + what + " (the logic needs one)");
            };
            need("hill", m.hasHill, "hill");
            need("sites", !m.bombSites.empty(), "bomb site");
            need("cart", m.payloadPath.size() >= 2, "payload track");
            need("points", !m.points.empty(), "multi point zone");
            need("checkpoints", !m.checkpoints.empty(), "race checkpoint");
        }
    }
    if (!mapErrs.empty()) c = compileModeText(text, base, mapErrs);
    const bool ok = c.ok;
    Json::parse(c.diagJson, out);
    if (!out.isObj()) out = Json::object();
    if (!mapName.empty()) out.set("map", mapName);
    out.set("serverRunsLogic", kServerHasLogicHook);   // (modesim's logicApplied: also needs the match to load it)
    out.set("via", "quark_maptool modecheck");
    if (!ok) {   // (exit 1; the answer keeps every field: main only adds "error" when there is none)
        const Json& e0 = out["errors"][0];
        out.set("error", e0.isObj() ? std::to_string(e0["line"].i()) + ":" + std::to_string(e0["col"].i()) + " " + e0["msg"].str() : std::string("doesn't compile"));
    }
    return ok ? 0 : 1;
}

}  // namespace

MAPTOOL_COMMAND("modesim", runModeSim, "{map, rules?: \".qmode text\", mode?, bots?, skill?, seed?, seconds?, until?, tickRate?, rate?, realtime?, speed?, trace?, positions?} [--stream] -> NDJSON start / frame / end: one headless match of a mode (tools/mode_editor/native/MODESIM.md)");
MAPTOOL_COMMAND("modebatch", runModeBatch, "{...modesim's setup, seeds: N | [..], firstSeed?, threads?, matchTimeout?} [--stream] -> NDJSON start / match / progress / end: per team / bot results, lengths, rounds, timeouts, csv (MODESIM.md)");
MAPTOOL_COMMAND("modecheck", runModeCheck, "{text | file, base?, map?} -> checkmode's JSON (+ map needs, serverRunsLogic): compiles a .qmode the way modesim does");
