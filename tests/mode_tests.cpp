// Game mode logic tests (src/game/modelogic.*, tools/modes): the .qlogic format (parse / print round trip, the JSON
// AST, converters), compile errors with line:col, every built-in default compiling with every slot native, and the
// interpreter run by hand against a test double of the game (FakeHost). The lockstep test against gamemodes.cpp comes
// with the engine binding (ModeHost on Server).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "game/gamedefs.h"
#include "game/map.h"
#include "game/modelogic.h"

std::string modeFmtNumberForTests(double v, bool f32);   // (modelogic.cpp)

namespace {

int g_modeFail = 0;
#define MCHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_modeFail++; } } while (0)

// ---- a test double of the game: plain arrays behind ModeHost, every action and text recorded
struct FakeHost : ModeHost {
    double scalar[MF_COUNT] = {};
    double team[MF_COUNT][2] = {};
    double player[MF_COUNT][kModePlayers] = {};
    uint32_t connected = 0;
    uint32_t inZone[8] = {};                     // zone kind -> player mask (index ignored)
    std::vector<std::string> acts, says;
    std::vector<int> sayWinner;
    uint32_t rng = 12345;
    int draws = 0;
    // what an action does to the fake world
    ModeLogicState* st = nullptr;                // (for re-entrant kill())
    const ModeLogic* L = nullptr;
    double phaseOnEnd = PHASE_ROUND_END;
    bool pointStepResult = false, tiebreak = false;
    bool record = true;                          // (the bench: no strings, so it times the interpreter, not the double)

    FakeHost() { scalar[MF_TEAMS] = 1; scalar[MF_TICK_RATE] = 60; scalar[MF_DT] = (double)(1.0f / 60.0f); scalar[MF_PHASE] = PHASE_LIVE; }
    void join(int id, int t, bool alive = true) {
        connected |= 1u << id;
        player[MF_TEAM][id] = t;
        player[MF_ALIVE][id] = alive;
    }
    double get(int f, int a, int b) override {
        if (f == MF_ALIVE_COUNT || f == MF_TEAM_COUNT) {
            int n = 0;
            for (int i = 0; i < kModePlayers; i++)
                if ((connected >> i & 1) && player[MF_TEAM][i] == a && (f == MF_TEAM_COUNT || player[MF_ALIVE][i] != 0)) n++;
            return n;
        }
        if (f == MF_IN_ZONE) return (unsigned)a < 32u && (inZone[modeZoneKind(b)] >> a & 1);
        if (f >= MF_ALIVE_COUNT && f <= MF_CART_NEAR) return (unsigned)a < 2u ? team[f][a] : 0;
        if (f >= MF_ALIVE && f < MF_IN_ZONE) return (unsigned)a < 32u ? player[f][a] : 0;
        return scalar[f];
    }
    void set(int f, int a, double v) override {
        if (f >= MF_ALIVE_COUNT && f <= MF_CART_NEAR) { if ((unsigned)a < 2u) team[f][a] = v; return; }
        if (f >= MF_ALIVE && f < MF_IN_ZONE) { if ((unsigned)a < 32u) player[f][a] = v; return; }
        scalar[f] = v;
    }
    uint32_t players(int set, int t) override {
        uint32_t m = 0;
        for (int i = 0; i < kModePlayers; i++) {
            if (!(connected >> i & 1)) continue;
            int pt = (int)player[MF_TEAM][i];
            if (set == MSET_EVERYONE || (set == MSET_PLAYING && pt != TEAM_SPECTATOR) || (set == MSET_TEAM && pt == t)) m |= 1u << i;
        }
        return m;
    }
    double act(int action, const double* args, int n) override {
        if (record) {
            std::string s = "act" + std::to_string(action);
            for (int i = 0; i < n; i++) { char b[32]; snprintf(b, sizeof b, " %g", args[i]); s += b; }
            acts.push_back(s);
        }
        switch (action) {
            case MA_KILL: {
                int v = (int)args[0];
                if ((unsigned)v < 32u && player[MF_ALIVE][v] != 0) {
                    player[MF_ALIVE][v] = 0;
                    player[MF_DEATHS][v] += 1;
                    if (L && st) { ModeArgs a; a.v[0] = v; a.v[1] = (int)args[1]; a.v[2] = (int)args[2]; runModeSlot(*L, *st, *this, MSLOT_KILL, a); }
                }
                return 0;
            }
            case MA_TAG_OUT: { int v = (int)args[0]; player[MF_OUT][v] = 1; player[MF_IT][v] = 0; player[MF_ALIVE][v] = 0; return 0; }
            case MA_PASS_IT: player[MF_IT][(int)args[0]] = 0; player[MF_IT][(int)args[1]] = 1; return 0;
            case MA_FREEZE: player[MF_FREEZE_UNTIL][(int)args[0]] = scalar[MF_NOW] + (float)args[1]; return 0;
            case MA_ADD_TIME: scalar[MF_PHASE_END] = std::max(scalar[MF_PHASE_END], scalar[MF_NOW]) + (float)args[0]; return 0;
            case MA_POINT_STEP: return pointStepResult;
            case MA_START_TIEBREAK: return tiebreak;
            case MA_HOLD_OVERTIME: return 0;
            default: return 0;
        }
    }
    void say(int action, int winner, const ModeTextPart* parts, int n) override {
        if (!record) { if (action == MA_END_ROUND || action == MA_END_ROUND_NO_SCORE || action == MA_END_ROUND_TEXT) scalar[MF_PHASE] = phaseOnEnd; return; }
        std::string s;
        for (int i = 0; i < n; i++) {
            const ModeTextPart& p = parts[i];
            switch (p.kind) {
                case ModeTextPart::STR: s.append(p.s, p.len); break;
                case ModeTextPart::PLAYER: s += p.v >= 0 ? "P" + std::to_string(p.v) : "nobody"; break;
                case ModeTextPart::TEAM: s += p.v == 0 ? "TEAM A" : p.v == 1 ? "TEAM B" : "nobody"; break;
                case ModeTextPart::INT: s += std::to_string(p.v); break;
                case ModeTextPart::LETTER: s += (char)('A' + p.v); break;
                case ModeTextPart::NAMES: {
                    std::string w;
                    for (int k = 0; k < 32; k++) if (p.v >> k & 1) w += (w.empty() ? "" : ", ") + std::string("P") + std::to_string(k);
                    s += w;
                    break;
                }
            }
        }
        says.push_back(std::to_string(action) + ":" + s);
        sayWinner.push_back(winner);
        if (action == MA_END_ROUND || action == MA_END_ROUND_NO_SCORE || action == MA_END_ROUND_TEXT) scalar[MF_PHASE] = phaseOnEnd;
        if (action == MA_END_ROUND && winner >= 0 && winner < 2) team[MF_TEAM_SCORE][winner] += 1;
        if (action == MA_END_MATCH) scalar[MF_PHASE] = PHASE_MATCH_END;
    }
    uint32_t qrand() override { draws++; rng = rng * 1103515245u + 12345u; return (rng >> 16) & 0x7fff; }
};

std::string firstDiff(const std::string& a, const std::string& b) {
    std::istringstream sa(a), sb(b);
    std::string la, lb;
    for (int n = 1;; n++) {
        bool ea = !std::getline(sa, la), eb = !std::getline(sb, lb);
        if (ea && eb) return a == b ? "" : "(line ends)";
        if (ea != eb || la != lb) return "line " + std::to_string(n) + ":\n    " + (ea ? "<end>" : la) + "\n    " + (eb ? "<end>" : lb);
    }
}

const char* const kModeFiles[] = {"vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "arena", "tourney", "hide", "tag", "points"};

void testFormat() {
    printf("  format: every default canonical, numbers, the JSON AST\n");
    for (int m = -1; m < MODE_COUNT; m++) {
        std::string text = defaultModeText(m);
        ModeDiag d;
        std::string printed = printModeText(text, &d);
        const char* nm = m < 0 ? "common" : kModeFiles[m];
        MCHECK(!text.empty() && d.errors.empty());
        if (!d.errors.empty()) printf("    %s.qlogic %d:%d: %s\n", nm, d.errors[0].line, d.errors[0].col, d.errors[0].msg.c_str());
        std::string diff = firstDiff(text, printed);
        if (!diff.empty()) printf("    %s.qlogic isn't canonical (print(parse(text)) != text) at %s\n", nm, diff.c_str());
        MCHECK(diff.empty());
        std::string ast = modeAstJson(text, &d);
        MCHECK(ast.find("\"format\":\"qmode-logic-ast\"") != std::string::npos);
    }
    // messy text reads the same and prints canonically; printing is idempotent; .qmode key lines are kept verbatim
    const char* messy =
        "custommode=Messy\nmode=1v1\n\nlogic 1\nbase arena\n\n\n# a\n\n\nparam  rounds   int = 3   # doc\n"
        "slot tick:\n    if (aliveCount(A) == 0):\n        endRound((B), \"x\")\n    else:\n        pass\n"
        "slot canHit:\n  return phase == live and (attacker == none or victim == attacker) or not (attacker in playing)\n";
    ModeDiag d2;
    std::string p1 = printModeText(messy, &d2), p2 = printModeText(p1, &d2);
    MCHECK(d2.errors.empty() && !p1.empty() && p1 == p2);
    if (!d2.errors.empty()) printf("    messy %d:%d: %s\n", d2.errors[0].line, d2.errors[0].col, d2.errors[0].msg.c_str());
    MCHECK(p1.compare(0, 26, "custommode=Messy\nmode=1v1\n") == 0);
    MCHECK(p1.find("param rounds int = 3 # doc\n") != std::string::npos);
    MCHECK(p1.find("  if aliveCount(A) == 0: endRound(B, \"x\")\n  else: pass\n") != std::string::npos);
    MCHECK(p1.find("return (phase == live and (attacker == none or victim == attacker)) or not (attacker in playing)") != std::string::npos);
    MCHECK(p1.find("\n\n# a\n\nparam") != std::string::npos);
    // numbers: the shortest text that reads back, laid out like JavaScript's toString
    MCHECK(modeFmtNumberForTests(0.05, false) == "0.05" && modeFmtNumberForTests(150, false) == "150");
    MCHECK(modeFmtNumberForTests(1e-7, false) == "1e-7" && modeFmtNumberForTests(1.5e21, false) == "1.5e+21");
    MCHECK(modeFmtNumberForTests((float)1.6, true) == "1.6" && modeFmtNumberForTests((float)0.1, true) == "0.1");
    // the export: the catalog and every default
    std::string j = exportModeJson();
    MCHECK(j.find("\"format\":\"qmode-export\"") != std::string::npos && j.find("\"cartCheckBonus\"") != std::string::npos && j.find("\"tourney\":{\"text\"") != std::string::npos);
}

// every built-in default compiles with no errors or warnings, every slot native; forced, every slot interpreted
void testDefaults() {
    printf("  every default compiles, native; forced: interpreted\n");
    for (int m = 0; m < MODE_COUNT; m++) {
        ModeDiag d;
        auto L = compileModeLogic(defaultModeText(m), m, &d);
        MCHECK(L != nullptr && d.warnings.empty());
        if (!L || !d.warnings.empty()) { printf("    %s: %s\n", kModeFiles[m], modeDiagJson(d, L.get()).c_str()); continue; }
        MCHECK(!L->anyInterp && L->base == m && L->hash.size() == 8);
        for (int s = 0; s < MSLOT_COUNT; s++) MCHECK(!L->interp[s]);
        auto D = defaultModeLogic(m);
        MCHECK(D && D->hash == L->hash && modeLabel(*D) == std::string(kModeFiles[m]) + "#" + D->hash);
        // a bare .qmode (keys only, no logic) plays the default natively
        auto K = compileModeLogic(std::string("custommode=Mine\nmode=") + (m == MODE_ARENA ? "1v1" : kModeFiles[m]) + "\n", -1, &d);
        MCHECK(K && !K->anyInterp && K->name == "Mine" && K->hash != L->hash);
        ModeDiag df;
        auto F = compileModeLogic(defaultModeText(m), m, &df, true);
        MCHECK(F && F->anyInterp && F->interp[MSLOT_FREEZE_LENGTH] && F->interp[MSLOT_CAN_DAMAGE]);
        for (int s = 0; s < MSLOT_TIMER; s++) {
            MCHECK(F && F->has[s] == L->has[s]);
            if (F && L->has[s]) MCHECK(F->interp[s] && F->cost[s] <= (uint32_t)kModeMaxCost);
        }
    }
    // the base must agree; common can't be played
    ModeDiag d;
    MCHECK(!compileModeLogic(defaultModeText(-1), -1, &d) && !d.errors.empty());
    ModeDiag d2;
    MCHECK(!compileModeLogic(defaultModeText(MODE_VS), MODE_BOMB, &d2) && !d2.errors.empty() && d2.errors[0].line == 2);
    MCHECK(modeBaseByName("1v1") == MODE_ARENA && modeBaseByName("arena") == MODE_ARENA && modeBaseByName("zork") == -1);
}

// format versions: logic 1 is the first (no converters yet); anything else is refused with a line:col
void testConverter() {
    printf("  format versions\n");
    std::string err;
    int from = 0;
    std::string t = defaultModeText(MODE_VS);
    MCHECK(upgradeModeLogicText(t, &err, &from) == t && from == kModeLogicVersion);
    std::string q = "custommode=x\nmode=vs\nlogic 2\n";
    MCHECK(upgradeModeLogicText(q, &err, &from).empty() && from == 2 && !err.empty());
    ModeDiag d;
    MCHECK(!compileModeLogic(q, -1, &d) && !d.errors.empty() && d.errors[0].line == 3);
    ModeDiag d0;
    MCHECK(!compileModeLogic("mode=vs\nlogic 0\n", -1, &d0) && !d0.errors.empty() && d0.errors[0].line == 2);
    std::string keys = "custommode=x\nmode=vs\n";
    MCHECK(upgradeModeLogicText(keys, &err, &from) == keys && from == kModeLogicVersion);
}

// logic that doesn't compile says where and why; logic that does runs native where it matches the default
void testCompileErrors() {
    printf("  compile errors and native slots\n");
    struct Case { const char* body; int line, col; const char* has; };
    const Case cases[] = {
        {"slot tick:\n  x = zork\n", 4, 7, "unknown name 'zork'"},
        {"slot tick:\n  x = 1 / 2\n", 4, 7, "int / int"},
        {"slot roundLength:\n  return now\n", 4, 10, "f32(...)"},
        {"slot tick:\n  if now > 1: x = 1\n  y = x\n", 5, 7, "may not be set"},
        {"slot tick:\n\tpass\n", 4, 1, "tab"},
        {"slot tick:\n  pass\nslot tick:\n  pass\n", 5, 1, "twice"},
        {"param rounds f32 = 2\n", 3, 1, "is a int"},
        {"slot tick:\n  bombStep()\n", 4, 3, "needs 'use bomb'"},
        {"slot tick:\n  swapTeams()\n", 4, 3, "isn't available"},
        {"slot tick:\n  for p in everyone:\n    for q in everyone:\n      pass\n", 5, 5, "inside another for"},
        {"slot tick:\n  x = count(p in everyone where any(q in everyone))\n", 4, 33, "inside another aggregate"},
        {"slot kill:\n  for victim in everyone:\n    pass\n", 4, 3, "already a name"},
        {"slot tick:\n  announce(\"t\", dt)\n", 4, 17, "int(...)"},
        {"slot tick:\n  x = 1 < 2 < 3\n", 4, 13, "chain"},
        {"slot tick:\n  x = 1.5 % 2\n", 4, 7, "'%' takes ints"},
        {"slot tick:\n  w = none\n", 4, 3, "none needs a type"},
        {"slot tick:\n  endRound(A, \"x\")\n  pass\n", 5, 3, "never runs"},   // (a warning: it still compiles)
        {"proc again:\n  again()\nslot tick:\n  again()\n", 4, 3, "calls itself"},
        {"slot tick:\n  now = 1\n", 4, 3, "read-only"},
        {"slot tick:\n  kills[0] = 1\n", 4, 3, "read-only"},
        {"slot tick:\n  return 1\n", 4, 3, "an event"},
        {"slot zork:\n  pass\n", 3, 6, "not a slot"},
        {"slot timer t:\n  pass\n", 3, 1, "no 'timer t'"},
        {"use zork\n", 3, 5, "not an object"},
        {"state match hud.x int = 0\n", 3, 1, "reserved"},
        {"slot tick:\n  x = " "((((((((((((((((((((((((((((((((((((((((((((((((((1))))))))))))))))))))))))))))))))))))))))))))))))))\n", 4, 0, "nested too deep"},
    };
    for (auto& c : cases) {
        std::string text = std::string("logic 1\nbase vs\n") + c.body;
        ModeDiag d;
        auto L = compileModeLogic(text, -1, &d);
        bool warn = strstr(c.has, "never runs") != nullptr;
        const std::vector<ModeDiag::Msg>& v = warn ? d.warnings : d.errors;
        bool ok = (warn ? L != nullptr : L == nullptr) && !v.empty() && v[0].line == c.line && (c.col == 0 || v[0].col == c.col) && v[0].msg.find(c.has) != std::string::npos;
        if (!ok) printf("    case %s: got %s\n", c.body, modeDiagJson(d, L.get()).c_str());
        MCHECK(ok);
    }
    {   // a line of 600 tokens
        std::string t = "logic 1\nbase vs\nslot tick:\n  x = 1";
        for (int k = 0; k < 300; k++) t += " + 1";
        ModeDiag d;
        MCHECK(!compileModeLogic(t + "\n", -1, &d) && !d.errors.empty() && d.errors[0].line == 4 && d.errors[0].msg.find("too long") != std::string::npos);
    }
    {   // too slow: a loop body over the worst-case cost
        std::string t = "logic 1\nbase vs\nslot tick:\n  for p in everyone:\n";
        for (int k = 0; k < 40; k++) t += "    score[p] = score[p] + kills[p] * 2 - deaths[p]\n";
        ModeDiag d;
        MCHECK(!compileModeLogic(t, -1, &d) && !d.errors.empty() && d.errors[0].line == 3 && d.errors[0].msg.find("instructions a call") != std::string::npos);
    }
    {   // an error in a default's declaration names the file it came from: the merged common's
        ModeDiag d;
        MCHECK(!compileModeLogic("logic 1\nbase vs\nstate match freezeTime int = 0\n", -1, &d) && !d.errors.empty() && d.errors[0].line == 3);
    }
    // .qmode line numbers: the key lines count
    {
        ModeDiag d;
        MCHECK(!compileModeLogic("custommode=x\nmode=vs\nmaps=bastion\nlogic 1\nslot tick:\n  x = zork\n", -1, &d) && !d.errors.empty() && d.errors[0].line == 6 && d.errors[0].col == 7);
    }
    // params only: still all native, the hash changes; a changed slot runs interpreted, the rest native
    ModeDiag d;
    auto p = compileModeLogic("custommode=Long\nmode=vs\nlogic 1\nparam rounds int = 9 # longer\n", -1, &d);
    MCHECK(p && !p->anyInterp && p->param("rounds") == 9 && p->hash != defaultModeLogic(MODE_VS)->hash);
    auto q = compileModeLogic("custommode=Q\nmode=vs\nlogic 1\nslot tick:\n  if aliveCount(A) == 0: endRound(B, \"wiped\")\n  if aliveCount(B) == 0: endRound(A, \"wiped\")\n", -1, &d);
    MCHECK(q && q->anyInterp && q->interp[MSLOT_TICK] && !q->interp[MSLOT_ROUND_OVER] && !q->interp[MSLOT_CAN_DAMAGE]);
    MCHECK(q && modeLabel(*q) == "Q#" + q->hash);
    // a comment changes nothing: same hash, native
    auto r = compileModeLogic("custommode=Long\nmode=vs\nlogic 1\n# hello\nparam rounds int = 9 # different words\n", -1, &d);
    MCHECK(r && p && r->hash == p->hash && !r->anyInterp);
    // changing a proc a slot runs makes that slot interpreted (its written-out text differs)
    auto s = compileModeLogic("logic 1\nbase vs\nproc firstToRounds:\n  endMatch(A, \"A always wins\")\n", -1, &d);
    MCHECK(s && s->interp[MSLOT_ROUND_OVER] && !s->interp[MSLOT_TICK]);
    // a param of a built-in mode keeps its type; an object's params come in at their defaults
    auto o = compileModeLogic("logic 1\nbase vs\nuse bomb\n", -1, &d);
    MCHECK(o && o->param("bombFuse") == 40 && o->uses_(MO_BOMB) && !d.warnings.empty());   // (vs doesn't use the bomb natively: warned)
}

// The interpreter, slot by slot against the test double
void testInterpreter() {
    printf("  interpreter\n");
    ModeLogicState st;
    {   // vs: the last side standing takes the round (the round end ends the slot: no drain after it)
        auto L = compileModeLogic(defaultModeText(MODE_VS), MODE_VS, nullptr, true);
        MCHECK(L != nullptr);
        if (!L) return;
        FakeHost h;
        initModeState(*L, st);
        h.join(0, TEAM_A); h.join(1, TEAM_B, false); h.join(2, TEAM_B, false);
        MCHECK(runModeSlot(*L, st, h, MSLOT_TICK));
        MCHECK(h.says.size() == 1 && h.says[0] == std::to_string(MA_END_ROUND) + ":" && h.sayWinner[0] == TEAM_A && h.acts.empty());
        h.says.clear();
        h.scalar[MF_PHASE] = PHASE_LIVE;
        h.player[MF_ALIVE][1] = 1;
        runModeSlot(*L, st, h, MSLOT_TICK);
        MCHECK(h.says.empty() && h.acts.size() == 1 && h.acts[0] == "act" + std::to_string(MA_STALEMATE_DRAIN));
        // roundOver: first to 5
        h.team[MF_TEAM_SCORE][1] = 5; h.scalar[MF_HUD_ROUNDS_TO_WIN] = 5;
        runModeSlot(*L, st, h, MSLOT_ROUND_OVER);
        MCHECK(h.says.size() == 1 && h.says[0] == std::to_string(MA_END_MATCH) + ":TEAM B wins the match" && h.sayWinner.back() == TEAM_B);
        // queries
        double r = -5;
        h.scalar[MF_PHASE] = PHASE_FREEZE;
        runModeSlot(*L, st, h, MSLOT_FREEZE_LENGTH, ModeArgs(), &r);
        MCHECK(r == 3);
        ModeArgs a; a.v[0] = 0; a.v[1] = 1; a.v[2] = WPN_RIFLE;
        runModeSlot(*L, st, h, MSLOT_CAN_DAMAGE, a, &r);
        MCHECK(r == 0);   // (freeze)
        h.scalar[MF_PHASE] = PHASE_LIVE;
        runModeSlot(*L, st, h, MSLOT_CAN_DAMAGE, a, &r);
        MCHECK(r == 1);
        h.player[MF_TEAM][1] = TEAM_A;
        runModeSlot(*L, st, h, MSLOT_CAN_DAMAGE, a, &r);
        MCHECK(r == 0);   // (friendly fire)
        a.v[0] = -1;
        runModeSlot(*L, st, h, MSLOT_CAN_DAMAGE, a, &r);
        MCHECK(r == 1);   // (the world)
        runModeSlot(*L, st, h, MSLOT_ROUNDS_TO_WIN, ModeArgs(), &r);
        MCHECK(r == 5);
        h.scalar[MF_CFG_ROUNDS_TO_WIN] = 2;
        runModeSlot(*L, st, h, MSLOT_ROUNDS_TO_WIN, ModeArgs(), &r);
        MCHECK(r == 2);
        ModeArgs one; one.v[0] = 0;
        h.scalar[MF_PHASE] = PHASE_WARMUP;
        runModeSlot(*L, st, h, MSLOT_RESPAWN_DELAY, one, &r);
        MCHECK(r == 1.5);
        // a teamkill costs 1, a kill scores 1
        h.scalar[MF_PHASE] = PHASE_LIVE;
        ModeArgs k; k.v[0] = 1; k.v[1] = 0; k.v[2] = WPN_RIFLE;
        runModeSlot(*L, st, h, MSLOT_KILL, k);
        MCHECK(h.player[MF_SCORE][0] == -1);
        h.player[MF_TEAM][1] = TEAM_B;
        runModeSlot(*L, st, h, MSLOT_KILL, k);
        MCHECK(h.player[MF_SCORE][0] == 0);
    }
    {   // koth: state per team, f32 sums, endMatch at the target
        auto L = compileModeLogic(defaultModeText(MODE_KOTH), MODE_KOTH, nullptr, true);
        MCHECK(L && L->stateValues == 2);
        if (!L) return;
        FakeHost h;
        initModeState(*L, st);
        h.join(0, TEAM_A); h.join(1, TEAM_B); h.join(2, TEAM_A);
        runModeSlot(*L, st, h, MSLOT_MATCH_START);
        MCHECK(h.scalar[MF_HUD_TARGET] == 100);
        h.inZone[MZ_HILL] = 1u << 0 | 1u << 2;
        float hold = 0, obj = 0;
        const float dt = (float)h.scalar[MF_DT];
        for (int t = 0; t < 600; t++) { runModeSlot(*L, st, h, MSLOT_TICK); hold += dt; obj += dt; }
        MCHECK(h.team[MF_TEAM_SCORE][0] == (int)hold && h.team[MF_TEAM_SCORE][1] == 0 && h.scalar[MF_HUD_OWNER] == TEAM_A && h.scalar[MF_HUD_CONTESTED] == 0);
        MCHECK((float)h.player[MF_OBJ_TIME][0] == obj);
        h.inZone[MZ_HILL] |= 1u << 1;
        runModeSlot(*L, st, h, MSLOT_TICK);
        MCHECK(h.scalar[MF_HUD_CONTESTED] == 1 && h.scalar[MF_HUD_OWNER] == -1);
        h.scalar[MF_HUD_TARGET] = 3;
        runModeSlot(*L, st, h, MSLOT_TICK);
        MCHECK(!h.says.empty() && h.says.back() == std::to_string(MA_END_MATCH) + ":TEAM A wins the match");
        // match state resets at match start
        runModeSlot(*L, st, h, MSLOT_MATCH_START);
        MCHECK(st.vars[0] == 0 && st.vars[1] == 0);
    }
    {   // payload: the tiebreak resets, then a draw on the second tie
        auto L = compileModeLogic(defaultModeText(MODE_PAYLOAD), MODE_PAYLOAD, nullptr, true);
        MCHECK(L != nullptr);
        if (!L) return;
        FakeHost h;
        h.scalar[MF_PHASE] = PHASE_ROUND_END;
        h.scalar[MF_ROUND_NO] = 2;
        h.team[MF_CART_BEST][0] = h.team[MF_CART_BEST][1] = 12.5;
        runModeSlot(*L, st, h, MSLOT_ROUND_OVER);
        MCHECK(h.says.size() == 1 && h.says[0] == std::to_string(MA_ANNOUNCE) + ":tied - tiebreaker rounds" && h.team[MF_CART_BEST][0] == 0);
        h.scalar[MF_ROUND_NO] = 4;
        runModeSlot(*L, st, h, MSLOT_ROUND_OVER);
        MCHECK(h.says.back() == std::to_string(MA_END_MATCH) + ":match draw" && h.sayWinner.back() == -1);
        // faster delivery wins
        h.scalar[MF_PHASE] = PHASE_ROUND_END;
        h.team[MF_TEAM_SCORE][0] = h.team[MF_TEAM_SCORE][1] = 1;
        h.team[MF_CART_COMPLETED][0] = 100; h.team[MF_CART_COMPLETED][1] = 90;
        runModeSlot(*L, st, h, MSLOT_ROUND_OVER);
        MCHECK(h.says.back() == std::to_string(MA_END_MATCH) + ":TEAM B wins the match");
        // roundSetup: sides alternate
        h.scalar[MF_ROUND_NO] = 3;
        runModeSlot(*L, st, h, MSLOT_ROUND_SETUP);
        MCHECK(h.scalar[MF_ATTACKERS] == TEAM_A);
        // the checkpoint's text
        h.scalar[MF_NOW] = 50; h.scalar[MF_PHASE_END] = 40;
        ModeArgs a; a.v[0] = 2;
        runModeSlot(*L, st, h, MSLOT_CART_CHECKPOINT, a);
        MCHECK(h.says.back() == std::to_string(MA_ANNOUNCE) + ":TEAM A reached checkpoint 2 - +60 s" && h.scalar[MF_PHASE_END] == 110);
    }
    {   // points: letters
        auto L = compileModeLogic(defaultModeText(MODE_POINTS), MODE_POINTS, nullptr, true);
        MCHECK(L != nullptr);
        if (!L) return;
        FakeHost h;
        h.scalar[MF_POINT_COUNT] = 5; h.scalar[MF_ATTACKERS] = TEAM_B;
        ModeArgs a; a.v[0] = 2;
        runModeSlot(*L, st, h, MSLOT_CAPTURE, a);
        MCHECK(h.says.size() == 2 && h.says[0] == std::to_string(MA_ANNOUNCE) + ":TEAM B took point B - +90 s, on to point C" && h.says[1] == std::to_string(MA_LOG) + ":point 2 taken by TEAM B");
        a.v[0] = 5;
        h.scalar[MF_NOW] = 77; h.scalar[MF_ROUND_START_TIME] = 7;
        runModeSlot(*L, st, h, MSLOT_CAPTURE, a);
        MCHECK(h.says.back() == std::to_string(MA_END_ROUND) + ":every point taken" && h.team[MF_CART_COMPLETED][1] == 70);
    }
    {   // ffa: a tie goes to overtime once; the next kill decides
        auto L = compileModeLogic(defaultModeText(MODE_FFA), MODE_FFA, nullptr, true);
        MCHECK(L != nullptr);
        if (!L) return;
        FakeHost h;
        h.scalar[MF_TEAMS] = 0;
        h.join(0, TEAM_A); h.join(1, TEAM_A); h.join(2, TEAM_A);
        h.player[MF_SCORE][0] = 3; h.player[MF_SCORE][1] = 5; h.player[MF_SCORE][2] = 5;
        h.scalar[MF_NOW] = 10; h.scalar[MF_PHASE_END] = 5;
        runModeSlot(*L, st, h, MSLOT_TICK);
        runModeSlot(*L, st, h, MSLOT_TICK);
        MCHECK(h.says.size() == 1 && h.says[0] == std::to_string(MA_ANNOUNCE) + ":tie - overtime! next kill wins" && h.scalar[MF_HUD_OVERTIME] == 1);
        ModeArgs k; k.v[0] = 0; k.v[1] = 2; k.v[2] = WPN_ROCKET;
        runModeSlot(*L, st, h, MSLOT_KILL, k);
        MCHECK(h.says.back() == std::to_string(MA_END_MATCH) + ":P2 wins the match" && h.sayWinner.back() == 2);
        // a suicide costs 1 while live
        h.scalar[MF_PHASE] = PHASE_LIVE; h.scalar[MF_HUD_OVERTIME] = 0;
        ModeArgs s; s.v[0] = 1; s.v[1] = -1; s.v[2] = 252;
        runModeSlot(*L, st, h, MSLOT_KILL, s);
        MCHECK(h.player[MF_SCORE][1] == 4);
        double r = 0;
        h.scalar[MF_RANKED] = 1;
        runModeSlot(*L, st, h, MSLOT_ROUND_LENGTH, ModeArgs(), &r);
        MCHECK(r == 300);
        h.scalar[MF_RANKED] = 0; h.scalar[MF_CFG_TIME_LIMIT] = 120;
        runModeSlot(*L, st, h, MSLOT_ROUND_LENGTH, ModeArgs(), &r);
        MCHECK(r == 120);
    }
    {   // hide: the stab finds (kill() runs slot kill re-entrantly: the seeker scores), the time-out names the hidden
        auto L = compileModeLogic(defaultModeText(MODE_HIDE), MODE_HIDE, nullptr, true);
        MCHECK(L != nullptr);
        if (!L) return;
        FakeHost h;
        h.scalar[MF_TEAMS] = 0;
        h.L = L.get(); h.st = &st;
        initModeState(*L, st);
        for (int i = 0; i < 4; i++) h.join(i, TEAM_A);
        h.player[MF_IT][1] = 1;
        ModeArgs a; a.v[0] = 1; a.v[1] = 3;
        runModeSlot(*L, st, h, MSLOT_STAB, a);
        MCHECK(h.player[MF_FOUND][3] == 1 && h.player[MF_ALIVE][3] == 0 && h.player[MF_SCORE][1] == 1);
        h.scalar[MF_NOW] = 400; h.scalar[MF_PHASE_END] = 300;
        runModeSlot(*L, st, h, MSLOT_TICK);
        MCHECK(h.says.size() == 1 && h.says[0] == std::to_string(MA_END_ROUND_TEXT) + ":the hiders win - P0, P2 stayed hidden");
        MCHECK(h.player[MF_SCORE][0] == 1 && h.player[MF_SCORE][2] == 1 && h.player[MF_SCORE][3] == 0);
        // the seeker faces the wall
        double r = 0;
        h.scalar[MF_PHASE] = PHASE_FREEZE;
        ModeArgs p; p.v[0] = 1;
        runModeSlot(*L, st, h, MSLOT_FROZEN, p, &r);
        MCHECK(r == 1);
        p.v[0] = 0;
        runModeSlot(*L, st, h, MSLOT_FROZEN, p, &r);
        MCHECK(r == 0);
        // roundOver: not everyone has sought: nothing
        h.scalar[MF_PHASE] = PHASE_ROUND_END;
        size_t n = h.says.size();
        runModeSlot(*L, st, h, MSLOT_ROUND_OVER);
        MCHECK(h.says.size() == n);
        for (int i = 0; i < 4; i++) h.player[MF_SOUGHT][i] = 1;
        h.player[MF_SCORE][2] = 7;
        runModeSlot(*L, st, h, MSLOT_ROUND_OVER);
        MCHECK(h.says.back() == std::to_string(MA_END_MATCH) + ":P2 wins the match");
    }
    {   // tag: a stab passes it on; it leaving ends the round
        auto L = compileModeLogic(defaultModeText(MODE_TAG), MODE_TAG, nullptr, true);
        MCHECK(L != nullptr);
        if (!L) return;
        FakeHost h;
        h.scalar[MF_TEAMS] = 0;
        for (int i = 0; i < 4; i++) h.join(i, TEAM_A);
        h.player[MF_IT][0] = 1;
        h.scalar[MF_NOW] = 20;
        ModeArgs a; a.v[0] = 0; a.v[1] = 2;
        runModeSlot(*L, st, h, MSLOT_STAB, a);
        MCHECK(h.player[MF_IT][2] == 1 && h.player[MF_IT][0] == 0 && h.player[MF_SCORE][0] == 1 && h.player[MF_FREEZE_UNTIL][2] == 22);
        MCHECK(h.says.size() == 2 && h.says[0] == std::to_string(MA_ANNOUNCE) + ":P2 is it");
        ModeArgs q; q.v[0] = 2;
        runModeSlot(*L, st, h, MSLOT_SPECTATE, q);
        MCHECK(h.player[MF_OUT][2] == 1 && h.player[MF_SCORE][0] == 2 && h.player[MF_SCORE][2] == 0);
        MCHECK(h.says.back() == std::to_string(MA_END_ROUND_TEXT) + ":P2 was it - out! 3 left");
        double r = 0;
        h.scalar[MF_PHASE] = PHASE_LIVE;
        runModeSlot(*L, st, h, MSLOT_TEAM_LOCKED, ModeArgs(), &r);
        MCHECK(r == 1);
    }
    {   // bomb: random(...) draws once (qrand() % n) and gives the bomb; nobody to give it to: no draw
        auto L = compileModeLogic(defaultModeText(MODE_BOMB), MODE_BOMB, nullptr, true);
        MCHECK(L != nullptr);
        if (!L) return;
        FakeHost h;
        h.join(0, TEAM_A); h.join(3, TEAM_A); h.join(5, TEAM_A); h.join(1, TEAM_B);
        runModeSlot(*L, st, h, MSLOT_ROUND_START);
        uint32_t x = 12345u * 1103515245u + 12345u;
        int want = (int)(((x >> 16) & 0x7fff) % 3);
        const int ids[3] = {0, 3, 5};
        MCHECK(h.draws == 1 && h.acts.size() == 1 && h.acts[0] == "act" + std::to_string(MA_GIVE_BOMB) + " " + std::to_string(ids[want]));
        h.player[MF_ALIVE][0] = h.player[MF_ALIVE][3] = h.player[MF_ALIVE][5] = 0;
        runModeSlot(*L, st, h, MSLOT_ROUND_START);
        MCHECK(h.draws == 1 && h.acts.size() == 1);
        // the second half
        h.scalar[MF_HUD_ROUNDS_TO_WIN] = 7; h.scalar[MF_ROUND_NO] = 7;
        runModeSlot(*L, st, h, MSLOT_ROUND_SETUP);
        MCHECK(h.scalar[MF_ATTACKERS] == TEAM_B);
        h.scalar[MF_ROUND_NO] = 6;
        runModeSlot(*L, st, h, MSLOT_ROUND_SETUP);
        MCHECK(h.scalar[MF_ATTACKERS] == TEAM_A);
    }
    {   // the language's corners: %, curves, timers, state resets, announcements at 4 a second, the nesting limit
        ModeDiag d;
        auto L = compileModeLogic("logic 1\nbase ffa\n"
                                  "curve ramp(x f32) = points 0: 0, 10: 1\n"
                                  "curve dbl(x f32) = x * 2\n"
                                  "state match total int = 0\nstate round seen[player] bool = no\n"
                                  "timer t\n"
                                  "slot matchStart:\n  startTimer(t, 2.0f)\n"
                                  "slot timer t:\n  total = total + 100\n"
                                  "slot tick:\n  total = total + 7 % 3 + -7 % 3\n  seen[first(p in everyone where team(p) == B)] = yes\n  hud.clock = ramp(5.0f) + dbl(1.0f)\n  hud.target = int(timerLeft(t))\n"
                                  "  announce(\"x\")\n", -1, &d);
        MCHECK(L != nullptr);
        if (!L) { printf("    %s\n", modeDiagJson(d, nullptr).c_str()); return; }
        MCHECK(L->interp[MSLOT_TICK] && L->interp[MSLOT_TIMER] && L->timers.size() == 1);
        FakeHost h;
        initModeState(*L, st);
        h.join(0, TEAM_A); h.join(2, TEAM_B);
        h.scalar[MF_NOW] = 100;
        runModeSlot(*L, st, h, MSLOT_MATCH_START);
        for (int k = 0; k < 6; k++) runModeSlot(*L, st, h, MSLOT_TICK);
        MCHECK(st.vars[0] == 6 * (1 + -1) && h.scalar[MF_HUD_CLOCK] == 2.5 && h.scalar[MF_HUD_TARGET] == 2);
        MCHECK(h.says.size() == 5 && h.says[4].find("dropped") != std::string::npos);   // (4 announcements, then one log line)
        h.scalar[MF_NOW] = 103;
        runModeTimers(*L, st, h);
        runModeTimers(*L, st, h);
        MCHECK(st.vars[0] == 100);
        runModeSlot(*L, st, h, MSLOT_ROUND_SETUP);
        MCHECK(st.vars[1 + 2] == 0 && st.vars[0] == 100);
        // kill() inside slot kill inside ... : the fifth level runs native (false), logged once
        auto K = compileModeLogic("logic 1\nbase ffa\nslot kill:\n  kill(victim, none, fell)\n", -1, &d);
        MCHECK(K != nullptr);
        if (!K) return;
        struct Loop : FakeHost {
            const ModeLogic* K = nullptr; ModeLogicState* s = nullptr; int falses = 0;
            double act(int action, const double* args, int n) override {
                ModeArgs a; a.v[0] = 0; a.v[1] = -1; a.v[2] = 252;
                if (action == MA_KILL && !runModeSlot(*K, *s, *this, MSLOT_KILL, a)) falses++;
                return 0;
            }
        } lp;
        lp.K = K.get(); lp.s = &st;
        initModeState(*K, st);
        ModeArgs a; a.v[0] = 0; a.v[1] = -1; a.v[2] = 252;
        runModeSlot(*K, st, lp, MSLOT_KILL, a);
        MCHECK(lp.falses == 1 && st.depth == 0 && lp.says.size() == 1 && lp.says[0].find("nested") != std::string::npos);
    }
}

// checkmode --map: the map features the logic reads
int checkNeeds(const ModeLogic& L, const std::string& mapName, ModeDiag& d) {
    Map m;
    std::string err;
    if (!m.load(mapName, &err)) { d.errors.push_back({0, 0, "can't load map " + mapName + ": " + err}); return 1; }
    uint32_t n = modeLogicNeeds(L);
    auto need = [&](uint32_t bit, bool has, const char* what) { if ((n & bit) && !has) d.errors.push_back({0, 0, mapName + " has no " + what + " (the logic needs one)"}); };
    need(MN_HILL, m.hasHill, "hill");
    need(MN_SITES, !m.bombSites.empty(), "bomb site");
    need(MN_CART, m.payloadPath.size() >= 2, "payload track");
    need(MN_POINTS, !m.points.empty(), "multi point zone");
    need(MN_CHECKPOINTS, !m.checkpoints.empty(), "race checkpoint");
    return d.errors.empty() ? 0 : 1;
}

bool readFile(const char* path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

} // namespace

int modeTests() {
    g_modeFail = 0;
    printf("mode logic\n");
    testFormat();
    testDefaults();
    testConverter();
    testCompileErrors();
    testInterpreter();
    {   // checkmode --map: koth reads the hill
        auto L = defaultModeLogic(MODE_KOTH);
        MCHECK(L && (modeLogicNeeds(*L) & MN_HILL));
        auto B = defaultModeLogic(MODE_BOMB);
        MCHECK(B && (modeLogicNeeds(*B) & MN_SITES));
        if (L) {
            ModeDiag d1, d2;
            MCHECK(checkNeeds(*L, "summit", d1) == 0);
            MCHECK(checkNeeds(*L, "viaduct", d2) == 1 && !d2.errors.empty());
        }
    }
    return g_modeFail;
}

// quark_tests modebench: what an interpreted slot run costs (every default, every slot interpreted, a test double)
int modeBench() {
    using clk = std::chrono::steady_clock;
    std::vector<double> all;
    printf("interpreted slot runs (every slot of each default forced into the interpreter; FakeHost, 16 players)\n");
    for (int m = 0; m < MODE_COUNT; m++) {
        auto L = compileModeLogic(defaultModeText(m), m, nullptr, true);
        if (!L) { printf("%s doesn't compile\n", kModeFiles[m]); return 1; }
        ModeLogicState st;
        initModeState(*L, st);
        FakeHost h;
        h.scalar[MF_TEAMS] = 1;
        for (int i = 0; i < 16; i++) h.join(i, i % 2 ? TEAM_B : TEAM_A);
        h.player[MF_IT][3] = 1;
        h.scalar[MF_PHASE_END] = 1e9; h.scalar[MF_NOW] = 10; h.scalar[MF_HUD_TARGET] = 1000000; h.scalar[MF_HUD_ROUNDS_TO_WIN] = 99;
        h.inZone[MZ_HILL] = 0x00FF;
        h.phaseOnEnd = PHASE_LIVE;   // (a round end doesn't stop the bench)
        h.record = false;
        std::vector<double> tick, other;
        const int kRuns = 20000;
        for (int k = 0; k < kRuns; k++) {
            h.scalar[MF_PHASE] = PHASE_LIVE;
            for (int i = 0; i < 16; i++) h.player[MF_ALIVE][i] = 1;
            auto t0 = clk::now();
            runModeSlot(*L, st, h, MSLOT_TICK);
            auto t1 = clk::now();
            tick.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
            int s = MSLOT_FREEZE_LENGTH + k % (MSLOT_TIMER - MSLOT_FREEZE_LENGTH);
            ModeArgs a; a.v[0] = k % 16; a.v[1] = (k + 1) % 16; a.v[2] = WPN_RIFLE;
            double r;
            auto t2 = clk::now();
            runModeSlot(*L, st, h, s, a, &r);
            auto t3 = clk::now();
            if (L->has[s]) other.push_back(std::chrono::duration<double, std::nano>(t3 - t2).count());
        }
        auto stat = [](std::vector<double> v, double& mean, double& p99, double& mx) {
            double sum = 0; for (double x : v) sum += x;
            mean = v.empty() ? 0 : sum / v.size();
            std::sort(v.begin(), v.end());
            p99 = v.empty() ? 0 : v[v.size() * 99 / 100];
            mx = v.empty() ? 0 : v.back();
        };
        double tm, t99, tmx, qm, q99, qmx;
        stat(tick, tm, t99, tmx); stat(other, qm, q99, qmx);
        printf("%-9s tick: mean %6.0f ns p99 %6.0f max %7.0f (worst case %4u insns) | queries: mean %5.0f ns p99 %5.0f max %6.0f\n", kModeFiles[m], tm, t99, tmx,
               L->cost[MSLOT_TICK], qm, q99, qmx);
        all.insert(all.end(), tick.begin(), tick.end());
        all.insert(all.end(), other.begin(), other.end());
    }
    double sum = 0;
    for (double x : all) sum += x;
    std::sort(all.begin(), all.end());
    printf("all: %zu slot runs, mean %.0f ns, p99 %.0f ns, p99.9 %.0f ns per slot run\n", all.size(), sum / all.size(), all[all.size() * 99 / 100], all[all.size() * 999 / 1000]);
    return 0;
}

// quark_tests exportmode / checkmode / modeast / printmode (the mode editor's tools)
int modeTool(const std::string& cmd, int argc, char** argv) {
    const char* arg = argc > 2 ? argv[2] : nullptr;
    if (cmd == "exportmode") {
        std::string j = exportModeJson();
        if (!arg) { fputs(j.c_str(), stdout); return 0; }
        std::ofstream o(arg, std::ios::binary);
        o << j;
        printf("wrote %s (%zu bytes)\n", arg, j.size());
        return o ? 0 : 1;
    }
    std::string text;
    if (!arg || !readFile(arg, text)) { printf("{\"ok\":false,\"errors\":[{\"line\":0,\"col\":0,\"msg\":\"can't read the file\"}],\"warnings\":[]}\n"); return 1; }
    ModeDiag d;
    if (cmd == "checkmode") {
        auto L = compileModeLogic(text, -1, &d);
        int rc = L ? 0 : 1;
        for (int i = 3; i + 1 < argc; i++)
            if (std::string(argv[i]) == "--map" && L) rc |= checkNeeds(*L, argv[i + 1], d);
        printf("%s\n", modeDiagJson(d, rc ? nullptr : L.get()).c_str());
        return rc;
    }
    std::string out = cmd == "modeast" ? modeAstJson(text, &d) : printModeText(text, &d);
    if (out.empty()) { printf("%s\n", modeDiagJson(d, nullptr).c_str()); return 1; }
    fputs(out.c_str(), stdout);
    if (cmd == "modeast") fputs("\n", stdout);
    return 0;
}
