// Movement equations (owner 2026-10-24: "i should be able to set value equations, not just fixed values ... curves,
// ease in, out, whatever with bounds"): a custom mode's movement setting given as an expression, one `expr.<key>=`
// line per setting in the .qmode (CustomRules::exprs). The expression's value is the setting's multiplier (1 =
// standard), clamped to the setting's range. Syntax and semantics: tools/physics_editor/INTERFACE.md, "v2".
//
// - Static expressions (numbers, other settings by key, base.<key>) are worked out once, when the rules are set
//   (setActiveRules -> build), in dependency order, into the MoveTuning every step reads.
// - Dynamic ones read the player's state (p.speed, p.air, ...) and are compiled to a small stack bytecode that pmove
//   runs at the start of every 1 ms step (pmove.cpp, only when MoveTuning::expr is set: the standard rules pay one
//   predictable branch per command). Static parts of a dynamic expression are folded to constants first.
// - Determinism: the server and every client run this same code on the same rules text. Everything is IEEE double
//   arithmetic (+ - * / and sqrt, all exactly rounded; built with -ffp-contract=off, no fast-math); exp / log / pow
//   are implemented here from those operations (no libm calls whose last bit differs between glibc, MinGW and
//   macOS); floor / ceil / round / frexp / ldexp are exact by definition.
// - Errors (syntax, unknown names, cycles) leave that setting at its fixed value (the plain `<key>=` line); `build`'s
//   report lists them (the physics editor's helper shows them).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "gamedefs.h"

struct MoveTuning;

namespace mexpr {

// The settings an expression can be set on and refer to: 0 gravity, 1 speed, 2 jump, 3 + k = MV_k (moveParamKey).
constexpr int kSettings = 3 + MV_COUNT;
int settingIndex(const std::string& key);    // -1: not a movement setting
const char* settingKey(int s);
float settingMin(int s), settingMax(int s);  // the multiplier's range (CustomRules::fromText's clamps; a test keeps them equal)
float settingBase(int s);                    // the standard absolute value (base.<key>; moveParamBase / moveBase*)
inline float settingGet(const MoveTuning& t, int s) { return s >= 3 ? t.m[s - 3] : s == 0 ? t.gravity : s == 1 ? t.speed : t.jump; }
inline void settingSet(MoveTuning& t, int s, float v) { if (s >= 3) t.m[s - 3] = v; else if (s == 0) t.gravity = v; else if (s == 1) t.speed = v; else t.jump = v; }

// The player's state a dynamic expression reads (p.<name>), filled by pmove at the start of each 1 ms step.
enum Input : uint8_t { P_SPEED, P_HSPEED, P_VSPEED, P_AIR, P_GROUND, P_ONGROUND, P_CHARGES, P_DASHING, P_HOOKED, P_HEIGHT, P_COUNT };
struct InputInfo { const char* name; const char* unit; float lo, hi, rest; const char* desc; };
const InputInfo& inputInfo(int i);           // name without the "p." (lo..hi: the range the editor samples; rest: standing still)
constexpr uint32_t kPhaseInputs = 1u << P_AIR | 1u << P_GROUND;   // these need MoveState::phaseMs counted
constexpr int kMaxText = 512;                // longest expression (characters); longer lines are ignored by fromText

struct FuncInfo { const char* name; int minArgs, maxArgs; const char* sig; const char* desc; };
const std::vector<FuncInfo>& functions();
const std::vector<const char*>& easeModes();  // the ease / curve mode names, in their id order

// One compiled dynamic program: every dynamic setting's bytecode, in dependency order.
struct Op { uint8_t code, arg; uint16_t idx; };
struct Program {
    struct Item { uint8_t setting; uint16_t first, count; float lo, hi, fallback; };
    std::vector<Item> items;
    std::vector<Op> ops;
    std::vector<double> consts;
    uint32_t inputs = 0;                     // bit per Input any item reads (pmove fills only these where it costs)
    bool empty() const { return items.empty(); }
    // the dynamic settings' values for these inputs, written into t (which holds the static values; each item may
    // read settings set by the items before it). No allocation, no calls; a NaN result writes the item's fallback.
    void run(const double* in, MoveTuning& t) const;
};

// What build found for each expression (the physics editor's helper shows it).
struct ItemReport {
    std::string key, text;
    bool ok = false, dynamic = false;
    std::string error;                       // (ok false) where and why, e.g. "col 9: unknown name 'sped'"
    std::vector<std::string> deps;           // the settings it refers to, by key (as written)
    uint32_t inputs = 0, allInputs = 0;      // p.* read directly / directly or through the settings it refers to
    float value = 1;                         // its value: static: resolved; dynamic: at rest (standing still on the floor)
};
struct Report { std::vector<ItemReport> items; std::vector<int> order; /* ok items, dependency order (indices into items) */ };

// Resolves the static expressions into t (which holds the plain values), compiles the dynamic ones into prog (cleared
// first) and writes their value at rest into t. exprs: {key, text} (CustomRules::exprs); unknown keys are reported.
void build(const std::vector<std::pair<std::string, std::string>>& exprs, MoveTuning& t, Program& prog, Report* rep = nullptr);

// The rest state's inputs (p.* standing still on the floor with full charges).
void restInputs(double* in);

// Deterministic math (exposed for tests): the same bits on every platform.
double detExp(double x), detLog(double x), detPow(double a, double b), detExp2(double x);
double ease(int mode, double t);             // t clamped to 0..1 (mode: easeModes() index)

} // namespace mexpr
