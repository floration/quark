// Bot brain tests: the .qbrain format (parse / print round trip, converters), and the lockstep test that runs two
// bot brains side by side on the same server state every tick and compares what they do, input and dice included.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include "common/net.h"
#include "game/botbrain.h"
#include "game/bots.h"
#include "game/server.h"

bool brainParamsEqual(const BrainParams& a, const BrainParams& b, std::string* which);   // (botbrain.cpp)
std::string brainFmtNumberForTests(double v, bool f32);

namespace {

int g_brainFail = 0;
#define BCHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_brainFail++; } } while (0)

void pumpFor(Server& s, double gameSecs) {
    double end = timeNow() + gameSecs;
    while (timeNow() < end) { s.frame(); sleepMs(1); }
}

// ---- lockstep: two think functions, the same state, every tick
using ThinkFn = UserCmd (*)(Server&, int);
struct Lockstep {
    ThinkFn a = nullptr, b = nullptr;
    long thinks = 0, mismatches = 0;
    std::string first;   // the first mismatch, described
    uint16_t buttons = 0; // every button pressed (coverage)
    long fights = 0;      // thinks with a target
    int kills = 0;
};
Lockstep* g_ls = nullptr;

template <class T> bool same(const T& x, const T& y) { return std::memcmp(&x, &y, sizeof(T)) == 0; }

std::string diffBrains(const BotBrain& x, const BotBrain& y) {
    std::string d;
#define BB(f) if (!same(x.f, y.f)) d += #f " ";
    BB(rng) BB(target) BB(engagedAt) BB(leadFactor) BB(dropFactor) BB(speedJudge) BB(rejudgeAt) BB(velEst)
    BB(aimYaw) BB(aimPitch) BB(yawVel) BB(pitchVel) BB(noiseYaw) BB(noisePitch) BB(flickYaw) BB(flickPitch)
    BB(glanceUntil) BB(nextGlance) BB(glanceYaw) BB(goal) BB(goalUntil) BB(lastPos) BB(stuckCheckAt) BB(strafeDir)
    BB(strafeSwitchAt) BB(unstickUntil) BB(fleeUntil) BB(hookUntil) BB(nextHookTry) BB(hookAnchor) BB(hookChecked)
    BB(fightHookUntil) BB(nextDecision) BB(nearEdge) BB(edgeFar) BB(edgeAway) BB(pathPrev) BB(pathGoalNode) BB(waypoint)
    BB(pathGoal) BB(repathAt) BB(nextSteer) BB(needPath) BB(nextLook)
#undef BB
    if (x.path != y.path) d += "path ";
    for (int i = 0; i < kMaxPlayers; i++) {
        const BotAwareness &p = x.aware[i], &q = y.aware[i];
        if (!same(p.visible, q.visible) || !same(p.reactedAt, q.reactedAt) || !same(p.seenAt, q.seenAt) || !same(p.seenPos, q.seenPos) ||
            !same(p.seenVel, q.seenVel) || !same(p.heardAt, q.heardAt) || !same(p.heardPos, q.heardPos)) { d += "aware[" + std::to_string(i) + "] "; break; }
    }
    return d;
}

UserCmd lockstepThink(Server& s, int id) {
    Lockstep& ls = *g_ls;
    BotBrain saved = s.bots_[id];
    saved.tc.dt = -1;   // (each side works out its own per-tick constants)
    s.bots_[id] = saved;
    const uint32_t pathTick = botPathTick();   // (the route-search budget is per tick: both sides start from the same)
    const int paths = botPathsThisTick();
    UserCmd ca = ls.a(s, id);
    BotBrain ba = s.bots_[id];
    s.bots_[id] = saved;
    botPathTick() = pathTick; botPathsThisTick() = paths;
    UserCmd cb = ls.b(s, id);
    ls.thinks++;
    ls.buttons |= cb.buttons;
    ls.fights += s.bots_[id].target >= 0;
    std::string d = diffBrains(ba, s.bots_[id]);
    if (ca.forward != cb.forward) d += "cmd.forward ";
    if (ca.side != cb.side) d += "cmd.side ";
    if (ca.buttons != cb.buttons) d += "cmd.buttons ";
    if (ca.weapon != cb.weapon) d += "cmd.weapon ";
    if (!same(ca.yaw, cb.yaw)) d += "cmd.yaw ";
    if (!same(ca.pitch, cb.pitch)) d += "cmd.pitch ";
    if (!d.empty()) {
        if (!ls.mismatches) {
            char buf[160];
            snprintf(buf, sizeof buf, "bot %d (%s) at %.3f s, tick %llu: ", id, botSkillName(s.bots_[id].skill), s.time(), (unsigned long long)s.tick_);
            ls.first = buf + d;
        }
        ls.mismatches++;
    }
    return cb;
}

struct LockMatch { const char* map; int mode; int bots; };

// One match: `bots` bots of every level (both teams), seeded, `secs` of game time with every bot's every tick run
// through both think functions.
void lockstepMatch(uint16_t port, const LockMatch& lm, int rate, double secs, uint32_t seed, Lockstep& ls) {
    Server s;
    ServerConfig cfg;
    cfg.map = lm.map; cfg.mode = lm.mode; cfg.port = port; cfg.maxTickRate = rate; cfg.maxPlayers = 16;
    cfg.autoStart = true; cfg.balanceBots = false;
    s.setLog([](const std::string&) {});
    std::string err;
    if (!s.start(cfg, &err)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, err.c_str()); g_brainFail++; return; }
    BCHECK(s.tickRate() == rate);
    for (int k = 0; k < lm.bots; k++) s.addBot(k % 2 ? TEAM_B : TEAM_A, (k / 2 + k) % BOT_SKILLS);
    for (int k = 0; k < 200 && !s.nav_.ready(s.map_); k++) pumpFor(s, 0.05);
    BCHECK(s.nav_.ready(s.map_));
    for (int i = 0; i < kMaxPlayers; i++)
        if (s.players_[i].connected && s.players_[i].isBot) s.bots_[i].rng = (seed * 2654435761u) ^ (uint32_t)(i * 40503u + 1u);
    s.command("forcestart");
    g_ls = &ls;
    g_botThinkHook = lockstepThink;
    pumpFor(s, secs);
    for (auto& p : s.players_) ls.kills += p.connected ? p.kills : 0;
    g_botThinkHook = nullptr;
    g_ls = nullptr;
    s.stop();
}

const LockMatch kLockMatches[] = {
    {"clockwork", MODE_FFA, 6}, {"summit", MODE_KOTH, 8}, {"depot", MODE_BOMB, 8}, {"railyard", MODE_PAYLOAD, 8},
    {"canal", MODE_NPAYLOAD, 8}, {"midtown", MODE_POINTS, 8}, {"foundry", MODE_TAG, 6}, {"hamlet", MODE_HIDE, 6},
    {"viaduct", MODE_RACE, 4}, {"bastion", MODE_VS, 8}, {"furnace", MODE_ARENA, 2},
};

void lockstepAll(uint16_t& port, ThinkFn a, ThinkFn b, const char* what, double secs) {
    const int rates[] = {1000, 250, 125};
    long thinks = 0, fights = 0;
    int kills = 0;
    uint16_t buttons = 0;
    for (const LockMatch& lm : kLockMatches)
        for (int r = 0; r < 3; r++) {
            Lockstep ls;
            ls.a = a; ls.b = b;
            lockstepMatch(port, lm, rates[r], secs, 0xB4A1u + 977u * r + (uint32_t)lm.mode, ls);
            thinks += ls.thinks; fights += ls.fights; kills += ls.kills; buttons |= ls.buttons;
            if (ls.mismatches) printf("  FAIL %s: %s %s at %d Hz: %ld of %ld thinks differ; first: %s\n", what, lm.map, modeName(lm.mode), rates[r], ls.mismatches, ls.thinks, ls.first.c_str());
            BCHECK(ls.mismatches == 0 && ls.thinks > 0);
        }
    printf("  %s: %ld bot thinks compared (%ld fighting, %d kills, buttons %#x)\n", what, thinks, fights, kills, buttons);
    BCHECK(fights > thinks / 20 && kills > 0);
}

// ---- native vs interpreter: the same default brain, compiled with every slot forced through the interpreter
std::shared_ptr<const Brain> g_interpBrain;
UserCmd interpThink(Server& s, int id) {
    auto keep = s.bots_[id].brain;
    s.bots_[id].brain = g_interpBrain;
    UserCmd c = botThinkDirect(s, id);
    s.bots_[id].brain = keep;
    return c;
}

// The harness can see a difference: a brain that strafes the other way must not match the default
void testLockstepSees(uint16_t port) {
    BrainDiag d;
    std::string t = defaultBrainText();
    const std::string from = "dir = roll() < strafe ? (roll() < 0.5f ? -1.0f : 1.0f) : 0.0f";
    size_t at = t.find(from);
    BCHECK(at != std::string::npos);
    if (at != std::string::npos) t.replace(at, from.size(), "dir = roll() < strafe ? (roll() < 0.5f ? 1.0f : -1.0f) : 0.0f");
    auto other = compileBrain(t, &d);
    BCHECK(other && !other->native[SLOT_STRAFE] && other->native[SLOT_REACT]);
    if (!other) { printf("    %s\n", brainDiagJson(d, nullptr).c_str()); return; }
    g_interpBrain = other;
    Lockstep ls;
    ls.a = botThinkDirect; ls.b = interpThink;
    lockstepMatch(port, {"clockwork", MODE_FFA, 6}, 250, 4.0, 7, ls);
    printf("  lockstep sees a changed slot: %ld of %ld thinks differ\n", ls.mismatches, ls.thinks);
    BCHECK(ls.mismatches > 0);
    g_interpBrain = nullptr;
}

// ---- cost: the same bot states thought by the native code and by the interpreter, timed one think at a time
struct Bench { std::vector<uint32_t> nat, itp; };
Bench* g_bench = nullptr;
UserCmd benchThink(Server& s, int id) {
    using clk = std::chrono::steady_clock;
    BotBrain saved = s.bots_[id];
    saved.tc.dt = -1;
    bool interpFirst = s.tick_ & 1;   // (alternate the order: neither side always gets the warm cache)
    uint32_t tn = 0, ti = 0;
    UserCmd c;
    for (int k = 0; k < 2; k++) {
        bool itp = (k == 0) == interpFirst;
        s.bots_[id] = saved;
        if (itp) s.bots_[id].brain = g_interpBrain;
        auto t0 = clk::now();
        c = botThinkDirect(s, id);
        auto t1 = clk::now();
        (itp ? ti : tn) = (uint32_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        if (itp) s.bots_[id].brain = saved.brain;
    }
    g_bench->nat.push_back(tn);
    g_bench->itp.push_back(ti);
    return c;
}

// ---- picking a brain: addbot brain=<name>, the server's botBrain, never on ranked
void testSelection(uint16_t port) {
    printf("  addbot brain=, botBrain, ranked\n");
    std::string name = "zz_brain" + std::to_string((unsigned)(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000)), path = userDataPath("bots") + "/" + name + ".qbrain";
    std::error_code ec;
    std::filesystem::create_directories(userDataPath("bots"), ec);
    { std::ofstream o(path, std::ios::binary); o << "version 1\nbrain \"zz\"\nparam reactMs f32 = 900\nslot strafe:\n  dir = 1.0f\n  next = now + 1.0\n"; }
    Server s;
    ServerConfig cfg;
    cfg.map = "clockwork"; cfg.mode = MODE_FFA; cfg.port = port; cfg.maxPlayers = 8; cfg.balanceBots = false;
    cfg.botBrain = name; cfg.botsEnemy = 1; cfg.vsBots = true;
    std::vector<std::string> logs;
    s.setLog([&](const std::string& l) { logs.push_back(l); });
    std::string err;
    BCHECK(s.start(cfg, &err));
    auto bots = [&] { std::vector<int> v; for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].isBot) v.push_back(i); return v; };
    auto v = bots();
    BCHECK(v.size() == 1 && s.bots_[v[0]].brain && s.bots_[v[0]].brain->name == "zz" && s.bots_[v[0]].brain->lvl[1].reactMs == 900.0f);
    BCHECK(v.size() == 1 && s.bots_[v[0]].brain && !s.bots_[v[0]].brain->native[SLOT_STRAFE] && s.bots_[v[0]].brain->native[SLOT_REACT]);
    s.command("addbot b 2 brain=default");
    s.command("addbot expert brain=zz_no_such_brain");
    s.command("addbot brain=../x");
    s.command("addbot 3 brain=" + name);
    v = bots();
    BCHECK(v.size() == 3);
    if (v.size() == 3) {
        BCHECK(!s.bots_[v[1]].brain && s.bots_[v[1]].skill == 2);
        BCHECK(s.bots_[v[2]].brain && s.bots_[v[2]].skill == 3 && s.bots_[v[2]].brain == s.bots_[v[0]].brain);   // (one compile, shared)
    }
    bool refused = false;
    for (auto& l : logs) refused |= l.find("zz_no_such_brain") != std::string::npos;
    BCHECK(refused);
    pumpFor(s, 0.5);   // (they think: the interpreter runs in a live server)
    s.cfg_.ranked = true;
    auto b = loadBrain(name, &err);
    BCHECK(b && !setBotBrain(s, v[0], b) && setBotBrain(s, v[0], nullptr) && !s.bots_[v[0]].brain);
    s.cfg_.ranked = false;
    BCHECK(b && setBotBrain(s, v[0], b) && s.bots_[v[0]].brain == b);
    BCHECK(b && brainLabel(*b) == "zz#" + b->hash);
    s.stop();
    std::filesystem::remove(path, ec);
}

// ---- the format
std::string firstDiff(const std::string& a, const std::string& b) {
    std::istringstream x(a), y(b);
    std::string la, lb;
    for (int n = 1;; n++) {
        bool ea = !std::getline(x, la), eb = !std::getline(y, lb);
        if (ea && eb) return "";
        if (ea != eb || la != lb) return "line " + std::to_string(n) + ":\n    " + (ea ? "<end>" : la) + "\n    " + (eb ? "<end>" : lb);
    }
}

void testFormat() {
    printf("  format: round trip, numbers, the default brain's numbers\n");
    BrainDiag d;
    std::string text = defaultBrainText();
    std::string printed = printBrainText(text, &d);
    BCHECK(d.errors.empty());
    if (!d.errors.empty()) printf("    default.qbrain %d:%d: %s\n", d.errors[0].line, d.errors[0].col, d.errors[0].msg.c_str());
    std::string diff = firstDiff(text, printed);
    if (!diff.empty()) printf("    default.qbrain isn't canonical (print(parse(text)) != text) at %s\n", diff.c_str());
    BCHECK(diff.empty());
    // non-canonical text reads the same and prints canonically; printing is idempotent
    const char* messy =
        "version 1\nbrain \"messy\"\n\n\n# a\n\n\nparam  x   f32 = 1.50 1.5 1.5 1.5   # doc\n"
        "slot react:\n    if (sinceSeen < 2.0):\n        r = ((reactMs) * 0.001f)\n    else:\n        r = 0.25f\n"
        "slot urgent:\n  return mode == bomb and (bombCarried and carrierIsMe or bombPlanted)\n";
    BrainDiag d2;
    std::string p1 = printBrainText(messy, &d2), p2 = printBrainText(p1, &d2);
    BCHECK(d2.errors.empty() && !p1.empty() && p1 == p2);
    BCHECK(p1.find("param x f32 = 1.5 # doc\n") != std::string::npos);
    BCHECK(p1.find("  if sinceSeen < 2.0: r = reactMs * 0.001f\n  else: r = 0.25f\n") != std::string::npos);
    BCHECK(p1.find("return mode == bomb and ((bombCarried and carrierIsMe) or bombPlanted)") != std::string::npos);
    BCHECK(p1.find("\n\n# a\n\nparam") != std::string::npos);
    // numbers: the shortest text that reads back, laid out like JavaScript's toString
    BCHECK(brainFmtNumberForTests(0.05, false) == "0.05" && brainFmtNumberForTests(150, false) == "150");
    BCHECK(brainFmtNumberForTests(1e-7, false) == "1e-7" && brainFmtNumberForTests(1.5e21, false) == "1.5e+21");
    BCHECK(brainFmtNumberForTests(0.000001, false) == "0.000001" && brainFmtNumberForTests(-3, false) == "-3");
    BCHECK(brainFmtNumberForTests((float)1.55, true) == "1.55" && brainFmtNumberForTests((float)0.1, true) == "0.1");
    BCHECK(brainFmtNumberForTests(0.1f, false) == "0.10000000149011612");
    // the JSON AST is there
    std::string ast = brainAstJson(text, &d);
    BCHECK(ast.find("\"format\":\"qbrain-ast\"") != std::string::npos && ast.find("\"op\":\"?:\"") != std::string::npos);
    // parse(default.qbrain) is the C++ table bit for bit; every slot native
    auto b = compileBrain(text, &d);
    BCHECK(b != nullptr);
    if (!b) { printf("    default.qbrain: %s\n", brainDiagJson(d, nullptr).c_str()); return; }
    for (int lv = 0; lv < 4; lv++) {
        std::string which;
        bool eq = brainParamsEqual(b->lvl[lv], defaultBrainParams(lv), &which);
        if (!eq) printf("    level %d: '%s' differs from the C++ table\n", lv, which.c_str());
        BCHECK(eq);
    }
    BCHECK(b->allNative);
    for (int sl = 0; sl < SLOT_COUNT; sl++) BCHECK(b->native[sl]);
    BCHECK(d.warnings.empty());
    for (auto& w : d.warnings) printf("    warning %d:%d: %s\n", w.line, w.col, w.msg.c_str());
    BCHECK(b->hash.size() == 8 && b->hash == defaultBrain().hash);
    auto f = compileBrain(text, &d, true);
    BCHECK(f && !f->allNative && !f->native[SLOT_REACT]);
}

// format versions: v1 is the first (no converters yet); anything else is refused with a line:col
void testConverter() {
    printf("  format versions\n");
    std::string err;
    int from = 0;
    std::string t = defaultBrainText();
    BCHECK(upgradeBrainText(t, &err, &from) == t && from == kBrainFormatVersion);
    std::string newer = "version 2\nbrain \"x\"\n";
    BCHECK(upgradeBrainText(newer, &err, &from).empty() && from == 2 && !err.empty());
    BrainDiag d;
    BCHECK(!compileBrain(newer, &d) && !d.errors.empty() && d.errors[0].line == 1);
    BrainDiag d0;
    BCHECK(!compileBrain("brain \"x\"\n", &d0) && !d0.errors.empty());
    BrainDiag d1;
    BCHECK(!compileBrain("version 0\nbrain \"x\"\n", &d1) && !d1.errors.empty());
}

// brains that don't compile say where and why; ones that do run native where they match the default
void testCompileErrors() {
    printf("  compile errors and native slots\n");
    struct Case { const char* body; int line; const char* has; };
    const Case cases[] = {
        {"slot react:\n  return reactMs * zork\n", 4, "unknown name 'zork'"},
        {"slot react:\n  return sinceSeen\n", 4, "f32(...)"},
        {"slot hearing:\n  return 1 / 2 > 0\n", 4, "int / int"},
        {"slot objective koth:\n  if hasHill: goto hill.floor\n", 3, "every path"},
        {"slot react:\n  if dist > 3: x = 1.0f\n  return x\n", 5, "may not be set"},
        {"slot react:\n  return wpDist\n", 4, "isn't available"},
        {"slot react:\n\treturn 1\n", 4, "tab"},
        {"slot react:\n  return 1.0f\nslot react:\n  return 2.0f\n", 5, "twice"},
        {"param fov f64 = 2\n", 3, "is a f32"},
        {"slot fire:\n  goto me.pos\n", 4, "objective slots"},
        {"slot strafe:\n  dir = 1 < 2 < 3\n", 4, "chain"},
        {"curve c(x f32) = x * dist\nslot react:\n  return c(1.0f)\n", 3, "curve sees only"},
        {"slot react:\n  return now > 1 ? 1 : 2\n", 4, ""},
        {"slot react:\n  return " "((((((((((((((((((((((((((((((((((((((((((((((((((1))))))))))))))))))))))))))))))))))))))))))))))))))\n", 4, "nested too deep"},
    };
    for (auto& c : cases) {
        std::string text = std::string("version 1\nbrain \"bad\"\n") + c.body;
        BrainDiag d;
        auto b = compileBrain(text, &d);
        bool ok = !b && !d.errors.empty() && d.errors[0].line == c.line && d.errors[0].msg.find(c.has) != std::string::npos;
        if (!*c.has) ok = b != nullptr;   // (an int ternary into an f32 output: widening is fine)
        if (!ok) printf("    case %s: got %s\n", c.body, brainDiagJson(d, b.get()).c_str());
        BCHECK(ok);
    }
    {   // a line of 600 tokens
        std::string t = "version 1\nbrain \"long\"\nslot react:\n  return 1.0f";
        for (int k = 0; k < 300; k++) t += " + 1.0f";
        BrainDiag d;
        BCHECK(!compileBrain(t + "\n", &d) && !d.errors.empty() && d.errors[0].line == 4 && d.errors[0].msg.find("too long") != std::string::npos);
    }
    // params only: still all native; a changed slot runs in the interpreter, the rest native; the hash changes
    BrainDiag d;
    auto p = compileBrain("version 1\nbrain \"tuned\"\nparam reactMs f32 = 500 400 300 200\nlist close = rifle shotgun\n", &d);
    BCHECK(p && p->allNative && p->lvl[3].reactMs == 200.0f && p->lvl[0].l_close.n == 2 && p->hash != defaultBrain().hash);
    auto q = compileBrain("version 1\nbrain \"mine\"\nparam lowHealth f32 = 40 # my own\nslot objective koth:\n  if health < lowHealth: fallback\n  if hasHill: goto hill.floor\n  fallback\n", &d);
    BCHECK(q && !q->allNative && !q->native[SLOT_OBJECTIVE + MODE_KOTH] && q->native[SLOT_OBJECTIVE + MODE_FFA] && q->native[SLOT_REACT]);
    BCHECK(q && brainLabel(*q) == "mine#" + q->hash);
    // `objective default` changes every mode the built-in brain doesn't write, not the ones it does
    auto r = compileBrain("version 1\nbrain \"r\"\nslot objective default:\n  goto me.pos\n", &d);
    BCHECK(r && !r->native[SLOT_OBJECTIVE + MODE_FFA] && r->native[SLOT_OBJECTIVE + MODE_KOTH] && r->objSlot[MODE_FFA] == SLOT_OBJECTIVE + MODE_COUNT);
}

// The interpreter's numbers: a few slots run by hand
void testInterpreter() {
    printf("  interpreter\n");
    BrainDiag d;
    auto b = compileBrain("version 1\nbrain \"t\"\n"
                          "curve tri(x f32) = points 0: 0, 10: 1, 20: 0\n"
                          "slot react:\n  a = clamp(dist, 1.0f, 5.0f) + tri(off)\n  return a * 2 - min(3, 2)\n"
                          "slot pickWeapon:\n  return firstAllowed(close, none)\n", &d, true);
    BCHECK(b != nullptr);
    if (!b) { printf("    %s\n", brainDiagJson(d, nullptr).c_str()); return; }
    uint32_t rng = 1;
    BrainCall c;
    c.rng = &rng;
    c.in[S_DIST] = 9; c.in[S_OFF] = 15; c.in[S_SINCESEEN] = 0;
    runBrainSlot(*b, SLOT_REACT, 1, 0.001f, c);
    BCHECK(c.out[0] == (5.0f + 0.5f) * 2 - 2);
    c.weaponMask = (uint16_t)(1u << WPN_LASER);
    runBrainSlot(*b, SLOT_PICK_WEAPON, 1, 0.001f, c);
    BCHECK(c.out[0] == WPN_LASER);
    c.weaponMask = (uint16_t)(1u << WPN_LOCATOR);
    runBrainSlot(*b, SLOT_PICK_WEAPON, 1, 0.001f, c);
    BCHECK(c.out[0] == -1);
}

} // namespace

int brainTests(uint16_t port) {
    g_brainFail = 0;
    printf("bot brains\n");
    testFormat();
    testConverter();
    testCompileErrors();
    testInterpreter();
    testSelection(port);
    testLockstepSees(port);
    BrainDiag d;
    g_interpBrain = compileBrain(defaultBrainText(), &d, true);
    BCHECK(g_interpBrain && !g_interpBrain->allNative);
    if (g_interpBrain) lockstepAll(port, botThinkDirect, interpThink, "lockstep: native vs interpreted default brain", 12.0);
    g_interpBrain = nullptr;
    return g_brainFail;
}

// quark_tests brainbench: what a bot think costs native vs through the interpreter (default brain, every slot interpreted)
int brainBench(uint16_t port) {
    BrainDiag d;
    g_interpBrain = compileBrain(defaultBrainText(), &d, true);
    if (!g_interpBrain) return 1;
    const LockMatch ms[] = {{"clockwork", MODE_FFA, 12}, {"summit", MODE_KOTH, 12}, {"depot", MODE_BOMB, 12}, {"hamlet", MODE_HIDE, 10}};
    Bench all;
    for (const LockMatch& lm : ms) {
        Bench b;
        g_bench = &b;
        // (the lockstep runner's setup, with this hook in place of the comparing one)
        Server s;
        ServerConfig cfg;
        cfg.map = lm.map; cfg.mode = lm.mode; cfg.port = port; cfg.maxTickRate = 250; cfg.maxPlayers = 16;
        cfg.autoStart = true; cfg.balanceBots = false;
        s.setLog([](const std::string&) {});
        std::string err;
        if (!s.start(cfg, &err)) { printf("server start: %s\n", err.c_str()); return 1; }
        for (int k = 0; k < lm.bots; k++) s.addBot(k % 2 ? TEAM_B : TEAM_A, k % BOT_SKILLS);
        for (int k = 0; k < 200 && !s.nav_.ready(s.map_); k++) pumpFor(s, 0.05);
        s.command("forcestart");
        pumpFor(s, 2.0);
        g_botThinkHook = benchThink;
        pumpFor(s, 20.0);
        g_botThinkHook = nullptr;
        s.stop();
        auto stat = [](std::vector<uint32_t> v, double& mean, uint32_t& p99, uint32_t& p999) {
            double sum = 0; for (auto x : v) sum += x;
            mean = v.empty() ? 0 : sum / v.size();
            std::sort(v.begin(), v.end());
            p99 = v.empty() ? 0 : v[v.size() * 99 / 100];
            p999 = v.empty() ? 0 : v[v.size() * 999 / 1000];
        };
        double mn, mi; uint32_t n99, n999, i99, i999;
        stat(b.nat, mn, n99, n999); stat(b.itp, mi, i99, i999);
        printf("%-9s %-6s %2d bots, %7zu thinks: native mean %6.0f ns p99 %6u p99.9 %6u | interpreter mean %6.0f ns p99 %6u p99.9 %6u | +%.0f%%\n",
               lm.map, modeName(lm.mode), lm.bots, b.nat.size(), mn, n99, n999, mi, i99, i999, mn > 0 ? (mi / mn - 1) * 100 : 0);
        all.nat.insert(all.nat.end(), b.nat.begin(), b.nat.end());
        all.itp.insert(all.itp.end(), b.itp.begin(), b.itp.end());
    }
    double sn = 0, si = 0;
    for (auto x : all.nat) sn += x;
    for (auto x : all.itp) si += x;
    printf("all: %zu thinks, native %.0f ns, interpreter %.0f ns per think (+%.0f ns, +%.0f%%)\n", all.nat.size(), sn / all.nat.size(), si / all.itp.size(),
           (si - sn) / all.nat.size(), (si / sn - 1) * 100);
    g_bench = nullptr;
    g_interpBrain = nullptr;
    return 0;
}

// quark_tests exportbrain / checkbrain / brainast / printbrain (the editor's tools)
int brainTool(const std::string& cmd, const char* arg) {
    auto readFile = [](const char* path, std::string& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::stringstream ss; ss << f.rdbuf(); out = ss.str();
        return true;
    };
    if (cmd == "exportbrain") {
        std::string j = exportBrainJson();
        if (!arg) { fputs(j.c_str(), stdout); return 0; }
        std::ofstream o(arg, std::ios::binary);
        o << j;
        printf("wrote %s (%zu bytes)\n", arg, j.size());
        return o ? 0 : 1;
    }
    std::string text;
    if (!arg || !readFile(arg, text)) { printf("{\"ok\":false,\"errors\":[{\"line\":0,\"col\":0,\"msg\":\"can't read the file\"}],\"warnings\":[]}\n"); return 1; }
    BrainDiag d;
    if (cmd == "checkbrain") {
        auto b = compileBrain(text, &d);
        printf("%s\n", brainDiagJson(d, b.get()).c_str());
        return b ? 0 : 1;
    }
    std::string out = cmd == "brainast" ? brainAstJson(text, &d) : printBrainText(text, &d);
    if (out.empty()) { printf("%s\n", brainDiagJson(d, nullptr).c_str()); return 1; }
    fputs(out.c_str(), stdout);
    if (cmd == "brainast") fputs("\n", stdout);
    return 0;
}
