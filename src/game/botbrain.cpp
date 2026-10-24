// Bot brains: the .qbrain parser, canonical printer, JSON export, type checker, compiler and interpreter
// (tools/bots/GRAMMAR.md). No dependency on Server: the bot code (bots.cpp) fills the sensors and runs the slots.
#include "botbrain.h"
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include "../common/sha256.h"
#include "../common/u8file.h"
#include "default_brain.h"
#include "gamedefs.h"
#include "map.h"

static_assert(kBrainModes == MODE_COUNT, "kBrainModes is MODE_COUNT");
static_assert(WPN_COUNT < 16, "BrainList and the tables hold 16 entries (the last: a table's else value)");
static_assert(sizeof(BrainInsn) == 8, "an instruction is 8 bytes");

float evalBrainCurve(const BrainCurve& c, float x) {
    if (c.kind == BrainCurve::STEPS) {
        float v = c.base;
        for (int k = 0; k < c.n; k++) if (x > c.x[k]) v = c.y[k];
        return v;
    }
    if (c.n == 0) return 0.0f;
    if (!(x > c.x[0])) return c.y[0];
    for (int k = 0; k + 1 < c.n; k++)
        if (x <= c.x[k + 1]) return c.y[k] + (c.y[k + 1] - c.y[k]) * ((x - c.x[k]) / (c.x[k + 1] - c.x[k]));
    return c.y[c.n - 1];
}

const char* defaultBrainText() { return kDefaultBrainText; }

namespace {

// =====================================================================================================
// The built-in parameters (the C++ table: the native bot's numbers)
// =====================================================================================================

BrainList makeList(std::initializer_list<int> ws) {
    BrainList l;
    for (int w : ws) { l.w[l.n++] = (uint8_t)w; l.mask |= (uint16_t)(1u << w); }
    return l;
}

BrainParams makeDefault(int level) {
    BrainParams p{};
#define QB_SET(name, type, unit, lo, hi, doc, a, b, c, d) { const QB_CT_##type v[4] = {a, b, c, d}; p.name = v[level]; }
    QUARK_BRAIN_PARAMS(QB_SET)
#undef QB_SET
    p.l_close       = makeList({WPN_SHOTGUN, WPN_KNIFE, WPN_LASER, WPN_BUBBLE, WPN_ROCKET, WPN_RIFLE, WPN_STICKY, WPN_CROSSBOW, WPN_SNIPER, WPN_SYRINGE});
    p.l_mid         = makeList({WPN_ROCKET, WPN_LASER, WPN_RIFLE, WPN_STICKY, WPN_BUBBLE, WPN_CROSSBOW, WPN_SHOTGUN, WPN_SNIPER, WPN_KNIFE, WPN_SYRINGE});
    p.l_midNoLead   = makeList({WPN_RIFLE, WPN_LASER, WPN_SHOTGUN, WPN_ROCKET, WPN_STICKY, WPN_CROSSBOW, WPN_BUBBLE, WPN_SNIPER, WPN_KNIFE, WPN_SYRINGE});
    p.l_far         = makeList({WPN_CROSSBOW, WPN_RIFLE, WPN_SNIPER, WPN_LASER, WPN_ROCKET, WPN_STICKY, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE});
    p.l_farNoLead   = makeList({WPN_RIFLE, WPN_SNIPER, WPN_LASER, WPN_CROSSBOW, WPN_ROCKET, WPN_STICKY, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE});
    p.l_vfar        = makeList({WPN_SNIPER, WPN_CROSSBOW, WPN_RIFLE, WPN_ROCKET, WPN_STICKY, WPN_LASER, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE});
    p.l_hookWeapons = makeList({WPN_SHOTGUN, WPN_KNIFE, WPN_LASER, WPN_BUBBLE});
    for (float& v : p.t_idealRange) v = 14.0f;
    p.t_idealRange[WPN_SHOTGUN] = 4.0f; p.t_idealRange[WPN_SNIPER] = 40.0f; p.t_idealRange[WPN_KNIFE] = 1.5f;
    p.t_idealRange[WPN_LASER] = 12.0f; p.t_idealRange[WPN_STICKY] = 10.0f; p.t_idealRange[WPN_BUBBLE] = 7.0f;
    p.t_idealRange[WPN_CROSSBOW] = 22.0f; p.t_idealRange[WPN_RIFLE] = 18.0f;
    p.c_reactDist.kind = BrainCurve::STEPS; p.c_reactDist.base = 1.0f;
    p.c_reactDist.n = 1; p.c_reactDist.x[0] = 40.0f; p.c_reactDist.y[0] = 1.25f;
    return p;
}

const BrainParams kDefaultParams[4] = {makeDefault(0), makeDefault(1), makeDefault(2), makeDefault(3)};

// =====================================================================================================
// Catalog: types, params, sensors, slots, points, builtins
// =====================================================================================================

enum Type : uint8_t { T_BOOL, T_INT, T_F32, T_F64, T_WEAPON, T_MODE, T_LIST, T_POINT, T_NONE };
const char* typeName(int t) {
    static const char* n[] = {"bool", "int", "f32", "f64", "weapon", "mode", "list", "point", "nothing"};
    return t >= 0 && t <= T_NONE ? n[t] : "?";
}
bool isNumeric(int t) { return t == T_INT || t == T_F32 || t == T_F64; }

const char* const kWeaponNames[WPN_COUNT] = {"rifle", "rocket", "knife", "sniper", "laser", "shotgun", "sticky", "crossbow", "bubble", "syringe", "locator"};
const char* const kModeNames[MODE_COUNT] = {"vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "arena", "tourney", "hide", "tag", "points"};
int weaponByName(const std::string& s) { for (int i = 0; i < WPN_COUNT; i++) if (s == kWeaponNames[i]) return i; return -1; }
int modeByName(const std::string& s) { for (int i = 0; i < MODE_COUNT; i++) if (s == kModeNames[i]) return i; return -1; }

enum PType : uint8_t { PT_F32_ = T_F32, PT_F64_ = T_F64 };
struct ParamInfo { const char* name; uint8_t type; const char* unit; double lo, hi; const char* doc; size_t off; };
#define QB_TYPE_F32 T_F32
#define QB_TYPE_F64 T_F64
#define QB_TYPE_INT T_INT
#define QB_TYPE_BOOL T_BOOL
const ParamInfo kParams[] = {
#define QB_INFO(name, type, unit, lo, hi, doc, a, b, c, d) {#name, QB_TYPE_##type, unit, lo, hi, doc, offsetof(BrainParams, name)},
    QUARK_BRAIN_PARAMS(QB_INFO)
#undef QB_INFO
};
constexpr int kNumParams = (int)(sizeof kParams / sizeof kParams[0]);

double getParam(const BrainParams& p, int i) {
    const char* at = (const char*)&p + kParams[i].off;
    switch (kParams[i].type) {
        case T_F32: { float v; memcpy(&v, at, 4); return v; }
        case T_F64: { double v; memcpy(&v, at, 8); return v; }
        case T_INT: { int v; memcpy(&v, at, sizeof v); return v; }
        default: { bool v; memcpy(&v, at, sizeof v); return v ? 1 : 0; }
    }
}
void setParam(BrainParams& p, int i, double v) {
    char* at = (char*)&p + kParams[i].off;
    switch (kParams[i].type) {
        case T_F32: { float f = (float)v; memcpy(at, &f, 4); break; }
        case T_F64: memcpy(at, &v, 8); break;
        case T_INT: { int n = (int)v; memcpy(at, &n, sizeof n); break; }
        default: { bool b = v != 0; memcpy(at, &b, sizeof b); break; }
    }
}

const char* const kListNames[] = {
#define QB_NAME(name) #name,
    QUARK_BRAIN_LISTS(QB_NAME)
#undef QB_NAME
};
BrainList BrainParams::* const kListMembers[] = {
#define QB_MEM(name) &BrainParams::l_##name,
    QUARK_BRAIN_LISTS(QB_MEM)
#undef QB_MEM
};
const char* const kTableNames[] = {
#define QB_NAME(name) #name,
    QUARK_BRAIN_TABLES(QB_NAME)
#undef QB_NAME
};
float (BrainParams::* const kTableMembers[])[16] = {
#define QB_MEM(name) &BrainParams::t_##name,
    QUARK_BRAIN_TABLES(QB_MEM)
#undef QB_MEM
};
const char* const kCurveNames[] = {
#define QB_NAME(name) #name,
    QUARK_BRAIN_CURVES(QB_NAME)
#undef QB_NAME
};
BrainCurve BrainParams::* const kCurveMembers[] = {
#define QB_MEM(name) &BrainParams::c_##name,
    QUARK_BRAIN_CURVES(QB_MEM)
#undef QB_MEM
};
constexpr int kNumCatLists = (int)(sizeof kListNames / sizeof kListNames[0]);
constexpr int kNumCatTables = (int)(sizeof kTableNames / sizeof kTableNames[0]);
constexpr int kNumCatCurves = (int)(sizeof kCurveNames / sizeof kCurveNames[0]);

struct SensorInfo { const char* name; uint8_t type; const char* unit; const char* doc; bool lazy; };
const SensorInfo kSensors[S_COUNT] = {
    {"now", T_F64, "s", "game time", false},
    {"dt", T_F32, "s", "the server tick's length", false},
    {"mode", T_MODE, "", "the game mode", false},
    {"level", T_INT, "", "the bot's skill level, 0 (BEGINNER) to 3 (EXPERT)", false},
    {"myId", T_INT, "", "the bot's player id (0-31)", false},
    {"health", T_F32, "hp", "the bot's health", false},
    {"itMode", T_BOOL, "", "hide and seek or tag", false},
    {"off", T_F32, "rad", "how far off the aim (yaw) it came into view", false},
    {"dist", T_F32, "m", "distance to it", false},
    {"sinceSeen", T_F64, "s", "time since it was last seen", false},
    {"gunAge", T_F64, "s", "time since it last fired", false},
    {"onGround", T_BOOL, "", "it is on the ground", false},
    {"groundSpeed", T_F32, "m/s", "its horizontal speed", false},
    {"dashing", T_BOOL, "", "it is dashing", false},
    {"angleOff", T_F32, "rad", "how far it is off the aim (yaw)", false},
    {"current", T_BOOL, "", "it is the current target", false},
    {"checkpointsLeft", T_BOOL, "", "race: a checkpoint still to reach", false},
    {"hasHill", T_BOOL, "", "the map has a hill", false},
    {"hasCart", T_BOOL, "", "the map has a payload track", false},
    {"pointActive", T_BOOL, "", "multi point: a point is under attack", false},
    {"bombPlanted", T_BOOL, "", "bomb: planted", false},
    {"bombDropped", T_BOOL, "", "bomb: lying on the ground", false},
    {"bombCarried", T_BOOL, "", "bomb: carried by someone", false},
    {"attacker", T_BOOL, "", "the bot is on the attacking side", false},
    {"carrierIsMe", T_BOOL, "", "the bot carries the bomb", false},
    {"carrierAlive", T_BOOL, "", "the bomb carrier is alive", false},
    {"navReady", T_BOOL, "", "the map's navigation is ready", false},
    {"it", T_BOOL, "", "hide and seek / tag: the bot is \"it\"", false},
    {"siteCount", T_INT, "", "bomb sites on the map", false},
    {"teamRank", T_INT, "", "how many players on the bot's team have a lower id (0 = the first): deals out the bomb sites", false},
    {"weapon", T_WEAPON, "", "the weapon in question", false},
    {"weaponRange", T_F32, "m", "its melee range (knife)", false},
    {"err", T_F32, "rad", "aim error (yaw + pitch) to where the target is seen", false},
    {"throwKnife", T_BOOL, "", "the knife is to be thrown (throwKnife slot)", false},
    {"ownBlast", T_BOOL, "", "the held weapon's splash would hit the bot (a wall right in front)", true},
    {"hookRange", T_F32, "m", "the grapple's range", false},
    {"ideal", T_F32, "m", "the weapon's ideal range (idealRange slot)", false},
    {"idle", T_BOOL, "", "standing still", false},
    {"grappleOnly", T_BOOL, "", "grapple-only movement", false},
    {"rise", T_F32, "m", "the anchor's height above the bot", false},
    {"flat", T_F32, "m", "the anchor's horizontal distance", false},
    {"fighting", T_BOOL, "", "in a fight", false},
    {"wpDist", T_F32, "m", "distance to the next waypoint", false},
    {"straight", T_F32, "", "cos of the angle between the view and the way to go", false},
    {"hopping", T_BOOL, "", "bunny-hopping (bhop slot)", false},
    {"moving", T_BOOL, "", "trying to move", false},
    {"moved", T_F32, "m", "distance moved since the last check", false},
    {"hasTarget", T_BOOL, "", "fighting or hunting someone", false},
};
constexpr int kCommonSensors = S_ITMODE + 1;

struct OutInfo { const char* name; uint8_t type; const char* doc; };
struct SlotInfo { const char* name; const char* doc; const char* when; std::vector<uint8_t> sensors; std::vector<OutInfo> outs; };
const std::vector<uint8_t> kObjSensors = {S_CHECKPOINTSLEFT, S_HASHILL, S_HASCART, S_POINTACTIVE, S_BOMBPLANTED, S_BOMBDROPPED,
                                          S_BOMBCARRIED, S_ATTACKER, S_CARRIERISME, S_CARRIERALIVE, S_NAVREADY, S_IT, S_SITECOUNT, S_TEAMRANK};
const SlotInfo kSlotsBase[SLOT_OBJECTIVE] = {
    {"react", "reaction time to a threat that just came into view", "a threat comes into view", {S_OFF, S_DIST, S_SINCESEEN}, {{"r", T_F32, "seconds until it starts aiming"}}},
    {"hearing", "is a threat out of view heard", "each look (20 Hz), per threat not in view", {S_DIST, S_GUNAGE, S_ONGROUND, S_GROUNDSPEED, S_DASHING}, {{"heard", T_BOOL, "heard"}}},
    {"targetScore", "score of an enemy in view as a target (lowest wins)", "each look, per enemy in view", {S_DIST, S_ANGLEOFF, S_CURRENT}, {{"score", T_F32, "lower is better"}}},
    {"urgent", "keep going for the objective while fighting", "each tick", kObjSensors, {{"urgent", T_BOOL, "the objective can't wait"}}},
    {"pickWeapon", "weapon for a fight at this range", "each fight tick", {S_DIST}, {{"pick", T_WEAPON, "the weapon"}}},
    {"throwKnife", "throw the knife (out of reach)", "each fight tick", {S_WEAPON, S_DIST, S_WEAPONRANGE}, {{"throw", T_BOOL, "throw it"}}},
    {"idealRange", "the range a weapon fights best at", "each fight tick", {S_WEAPON}, {{"range", T_F32, "metres"}}},
    {"rejudge", "how the bot misjudges leads and speeds, rolled now and then", "each new target, and when `next` is due", {},
        {{"lead", T_F32, "fraction of the needed lead given"}, {"drop", T_F32, "fraction of the needed drop given"}, {"speed", T_F32, "perceived speed factor"}, {"next", T_F64, "judge again at (s)"}}},
    {"fire", "press fire / alt fire", "each fight tick with the target in view", {S_ERR, S_DIST, S_WEAPON, S_THROWKNIFE, S_WEAPONRANGE, S_OWNBLAST},
        {{"fire", T_BOOL, "press fire"}, {"alt", T_BOOL, "press alt fire"}}},
    {"fightHook", "hook the target to drag the fight in", "fight, target in view, grapple ready and the last try due", {S_WEAPON, S_DIST, S_HOOKRANGE},
        {{"try", T_BOOL, "this is a try (sets the retry time)"}, {"retry", T_F64, "next try at (s)"}, {"go", T_BOOL, "hook now (if the pull is safe)"}, {"hold", T_F64, "keep the aim on them for the hook until (s)"}}},
    {"strafe", "strafe or stand, and for how long", "fight, target in view, when `next` is due", {}, {{"dir", T_F32, "-1 left, 1 right, 0 stand"}, {"next", T_F64, "decide again at (s)"}}},
    {"pulledDash", "dash out of an enemy's grapple", "fight, pulled by an enemy grapple, dash ready, no drop near", {S_IDEAL}, {{"dash", T_BOOL, "dash"}}},
    {"fightJump", "jump in a fight", "fight, on the ground, no drop near", {}, {{"jump", T_BOOL, "jump"}}},
    {"fightDash", "dash in a fight", "fight, on the ground, no drop near, dash ready", {S_DIST}, {{"dash", T_BOOL, "dash"}, {"closeIn", T_BOOL, "toward the target (else sideways)"}}},
    {"glance", "look around while moving / standing", "not fighting, nothing heard, when `next` is due", {S_IDLE},
        {{"yaw", T_F32, "off the route (moving) or turned by (standing), rad"}, {"until", T_F64, "for this long (s)"}, {"next", T_F64, "next glance at (s)"}}},
    {"hookRetry", "when to look for a route grapple again", "route grapple, when the last try is due", {S_GRAPPLEONLY}, {{"next", T_F64, "next try at (s)"}}},
    {"routeHook", "grapple to this anchor on the route", "route grapple, per candidate anchor", {S_RISE, S_FLAT, S_GRAPPLEONLY}, {{"want", T_BOOL, "hook it (if it catches and the pull is safe)"}}},
    {"bhop", "bunny-hop", "moving", {S_FIGHTING, S_WPDIST}, {{"hop", T_BOOL, "hop"}}},
    {"runDash", "dash along a long straight route", "not fighting, on the ground, dash ready", {S_WPDIST, S_STRAIGHT}, {{"dash", T_BOOL, "dash (if there's ground all along)"}}},
    {"dashJump", "make a run dash a dash jump", "a run dash was chosen", {S_HOPPING, S_WPDIST}, {{"jump", T_BOOL, "jump with it (if there's ground all along)"}}},
    {"unstick", "barely moved: sidestep and hop", "when `next` is due", {S_MOVING, S_MOVED, S_HASTARGET},
        {{"stuck", T_BOOL, "unstick now"}, {"side", T_F32, "-1 left, 1 right"}, {"until", T_F64, "sidestep until (s)"}, {"next", T_F64, "check again at (s)"}}},
};
const SlotInfo kObjectiveInfo = {"objective", "where to go when not fighting", "each tick not fighting (and while urgent)", kObjSensors,
                                 {{"$action", T_INT, ""}, {"$point", T_INT, ""}, {"$arg", T_INT, ""}}};
const SlotInfo& slotInfo(int slot) { return slot < SLOT_OBJECTIVE ? kSlotsBase[slot] : kObjectiveInfo; }
std::string slotName(int slot) {
    if (slot < SLOT_OBJECTIVE) return kSlotsBase[slot].name;
    int m = slot - SLOT_OBJECTIVE;
    return std::string("objective ") + (m < MODE_COUNT ? kModeNames[m] : "default");
}
bool slotHasSensor(int slot, int s) {
    if (s < kCommonSensors) return true;
    for (uint8_t x : slotInfo(slot).sensors) if (x == s) return true;
    return false;
}

struct PointInfo { const char* name; const char* doc; };
const PointInfo kPoints[PT_COUNT] = {
    {"me.pos", "where the bot stands (stay)"}, {"checkpoint.next", "race: the next checkpoint"}, {"hill.floor", "the hill's floor"},
    {"cart.goal", "payload: where the bot's side wants to be along the track"}, {"point.floor", "multi point: the floor of the point under attack"},
    {"bomb.pos", "the bomb (planted or dropped)"}, {"carrier.pos", "the bomb carrier"}, {"site", "site(i): bomb site i (modulo the count)"},
};

struct BuiltinInfo { const char* name; const char* sig; const char* doc; int draws; };
const BuiltinInfo kBuiltins[] = {
    {"roll", "roll() -> f32", "uniform random 0..1: one draw", 1},
    {"gauss", "gauss() -> f32", "normal random (mean 0, sd 1): two draws", 2},
    {"chance", "chance(rate f32) -> bool", "an event `rate` times a second, this tick: rate > 0 and roll() < 1 - exp(-rate * dt)", 1},
    {"min", "min(a, b)", "b < a ? b : a (std::min)", 0},
    {"max", "max(a, b)", "a < b ? b : a (std::max)", 0},
    {"clamp", "clamp(x, lo, hi)", "x >= lo ? (x > hi ? hi : x) : lo", 0},
    {"abs", "abs(x)", "absolute value (f32 / f64)", 0},
    {"sqrt", "sqrt(x)", "square root (f32 / f64)", 0},
    {"exp", "exp(x)", "e to the x (f32 / f64)", 0},
    {"f32", "f32(x) -> f32", "conversion", 0},
    {"f64", "f64(x) -> f64", "conversion", 0},
    {"allowed", "allowed(w weapon) -> bool", "the server's weapon rules allow w", 0},
    {"firstAllowed", "firstAllowed(l list, w weapon) -> weapon", "the first allowed weapon of l, else w", 0},
    {"site", "site(i int) -> point", "bomb site i (modulo the number of sites); objective goto only", 0},
};
bool isBuiltin(const std::string& n) { for (auto& b : kBuiltins) if (n == b.name) return true; return false; }

const char* const kKeywords[] = {"version", "brain", "levels", "param", "curve", "list", "table", "slot", "if", "elif", "else", "return",
                                 "goto", "fallback", "seek", "evade", "pass", "and", "or", "not", "in", "yes", "no", "true", "false", "none"};
bool isKeyword(const std::string& n) { for (auto* k : kKeywords) if (n == k) return true; return false; }
const char* const kLevelNames[4] = {"BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"};

// =====================================================================================================
// Numbers: canonical text (the shortest decimal that reads back as the same value of its type, laid out like
// JavaScript's Number.prototype.toString, so the editor's printer writes the same text)
// =====================================================================================================

std::string fmtNumber(double v, int type) {
    if (type == T_INT) { char b[32]; snprintf(b, sizeof b, "%lld", (long long)v); return b; }
    if (v == 0) return "0";
    char buf[64];
    int maxp = type == T_F32 ? 9 : 17;
    int p = 1;
    for (; p < maxp; p++) {
        snprintf(buf, sizeof buf, "%.*e", p - 1, v);
        if (type == T_F32 ? std::strtof(buf, nullptr) == (float)v : std::strtod(buf, nullptr) == v) break;
    }
    snprintf(buf, sizeof buf, "%.*e", p - 1, v);
    // buf: [-]d.ddde[+-]xx
    std::string s = buf, digits;
    bool neg = s[0] == '-';
    size_t i = neg ? 1 : 0;
    for (; i < s.size() && s[i] != 'e'; i++) if (s[i] != '.') digits += s[i];
    int e = atoi(s.c_str() + i + 1);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    int k = (int)digits.size(), n = e + 1;
    std::string out;
    if (k <= n && n <= 21) out = digits + std::string(n - k, '0');
    else if (0 < n && n <= 21) out = digits.substr(0, n) + "." + digits.substr(n);
    else if (-6 < n && n <= 0) out = "0." + std::string(-n, '0') + digits;
    else {
        out = digits.substr(0, 1);
        if (k > 1) out += "." + digits.substr(1);
        out += (n - 1 >= 0 ? "e+" : "e-") + std::to_string(std::abs(n - 1));
    }
    return (neg ? "-" : "") + out;
}
// a literal in an expression: its type shows in the text
std::string fmtLiteral(double v, int type) {
    std::string s = fmtNumber(v, type);
    if (type == T_INT) return s;
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    if (type == T_F32) s += "f";
    return s;
}

// =====================================================================================================
// AST
// =====================================================================================================

enum NodeOp : uint8_t {
    N_NUM, N_BOOL, N_NAME, N_BIN, N_NEG, N_NOT, N_COND, N_IN, N_CALL, N_INDEX,   // expressions
    N_SET, N_IF, N_RETURN, N_GOTO, N_FALLBACK, N_SEEK, N_EVADE, N_PASS            // statements
};
enum BinOp : uint8_t { B_ADD, B_SUB, B_MUL, B_DIV, B_LT, B_LE, B_GT, B_GE, B_EQ, B_NE, B_AND, B_OR };
const char* const kBinText[] = {"+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!=", "and", "or"};
int binPrec(int b) { return b <= B_SUB ? 6 : b <= B_DIV ? 7 : b <= B_NE ? 5 : b == B_AND ? 3 : 2; }

struct Node {
    NodeOp op = N_PASS;
    uint8_t bin = 0;
    uint8_t ntype = 0;          // N_NUM: T_INT / T_F32 / T_F64
    double v = 0;               // N_NUM, N_BOOL
    std::string name;           // N_NAME, N_CALL (function), N_IN (list), N_INDEX (table), N_SET (target)
    std::vector<Node> k;        // children: operands, call args, set value, return values, goto point
    struct Arm {
        std::vector<std::string> comments;
        std::string trail;
        std::vector<Node> body;
        int line = 0;
    };
    std::vector<Arm> arms;      // N_IF: k[i] is arms[i]'s condition
    bool hasElse = false;
    Arm elseArm;
    std::vector<std::string> comments;
    std::string trail;
    int line = 0, col = 0;
};

struct Decl {
    enum Kind : uint8_t { PARAM, CURVE, LIST, TABLE, SLOT } kind = PARAM;
    std::string name;
    int slot = -1;                                   // SLOT: BrainSlot
    int ptype = T_F32;                               // PARAM type; CURVE argument type
    std::vector<double> values;                      // PARAM: 1 or 4
    std::string arg;                                 // CURVE argument name
    int form = 0;                                    // CURVE: 0 expression, 1 steps, 2 points
    double base = 0;
    std::vector<std::pair<double, double>> pts;
    std::vector<Node> body;                          // CURVE expr: body[0]; SLOT: statements
    std::vector<int> items;                          // LIST weapons
    std::vector<std::pair<int, double>> entries;     // TABLE
    bool hasElse = false;
    double elseV = 0;
    std::vector<std::string> comments;
    std::string trail;
    int line = 0, col = 0;
};

struct File {
    int version = kBrainFormatVersion;
    std::string name;
    std::vector<Decl> decls;
    std::vector<std::string> tail;
};

// =====================================================================================================
// Lexer + parser
// =====================================================================================================

struct Tok {
    enum Kind : uint8_t { NAME, NUM, STR, OP, END } t = END;
    std::string s;
    int col = 0;
};
struct Line {
    int no = 0, indent = 0;
    std::vector<Tok> toks;
    std::string trail;
    std::vector<std::string> comments;
};

struct ParseError { int line, col; std::string msg; };

std::string rtrim(std::string s) { while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back(); return s; }

bool lexLines(const std::string& text, std::vector<Line>& out, std::vector<std::string>& tail, ParseError& err) {
    std::vector<std::string> pending;
    auto addComment = [&](const std::string& c) {
        if (c.empty() && (pending.empty() ? false : pending.back().empty())) return;
        pending.push_back(c);
    };
    std::istringstream in(text);
    std::string raw;
    int no = 0;
    while (std::getline(in, raw)) {
        no++;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        size_t hash = std::string::npos;
        bool inStr = false;
        for (size_t i = 0; i < raw.size(); i++) {
            if (raw[i] == '\t') { err = {no, (int)i + 1, "tab: indent with spaces"}; return false; }
            if (raw[i] == '"') inStr = !inStr;
            else if (raw[i] == '#' && !inStr) { hash = i; break; }
        }
        std::string code = hash == std::string::npos ? raw : raw.substr(0, hash);
        std::string trail = hash == std::string::npos ? "" : rtrim(raw.substr(hash));
        size_t ind = 0;
        while (ind < code.size() && code[ind] == ' ') ind++;
        if (rtrim(code).size() <= ind) {   // nothing but a comment, or blank
            if (!trail.empty()) addComment(trail);
            else if (!pending.empty() || !out.empty()) addComment("");
            continue;
        }
        Line L;
        L.no = no; L.indent = (int)ind; L.trail = trail;
        L.comments = std::move(pending); pending.clear();
        size_t i = ind;
        while (i < code.size()) {
            char c = code[i];
            if (c == ' ') { i++; continue; }
            Tok t;
            t.col = (int)i + 1;
            if (isalpha((unsigned char)c) || c == '_') {
                size_t j = i;
                for (;;) {
                    while (j < code.size() && (isalnum((unsigned char)code[j]) || code[j] == '_')) j++;
                    if (j + 1 < code.size() && code[j] == '.' && (isalpha((unsigned char)code[j + 1]) || code[j + 1] == '_')) { j++; continue; }
                    break;
                }
                t.t = Tok::NAME; t.s = code.substr(i, j - i); i = j;
            } else if (isdigit((unsigned char)c)) {
                size_t j = i;
                while (j < code.size() && isdigit((unsigned char)code[j])) j++;
                if (j < code.size() && code[j] == '.') {
                    j++;
                    if (j >= code.size() || !isdigit((unsigned char)code[j])) { err = {no, (int)j + 1, "a digit after the '.'"}; return false; }
                    while (j < code.size() && isdigit((unsigned char)code[j])) j++;
                }
                if (j < code.size() && (code[j] == 'e' || code[j] == 'E')) {
                    size_t e = j + 1;
                    if (e < code.size() && (code[e] == '+' || code[e] == '-')) e++;
                    if (e < code.size() && isdigit((unsigned char)code[e])) { j = e; while (j < code.size() && isdigit((unsigned char)code[j])) j++; }
                }
                if (j < code.size() && code[j] == 'f') j++;
                if (j < code.size() && (isalnum((unsigned char)code[j]) || code[j] == '_' || code[j] == '.')) { err = {no, (int)j + 1, "bad number"}; return false; }
                t.t = Tok::NUM; t.s = code.substr(i, j - i); i = j;
            } else if (c == '"') {
                size_t j = code.find('"', i + 1);
                if (j == std::string::npos) { err = {no, (int)i + 1, "unterminated string"}; return false; }
                t.t = Tok::STR; t.s = code.substr(i + 1, j - i - 1); i = j + 1;
                if (t.s.find('\\') != std::string::npos || t.s.size() > 64) { err = {no, t.col, "the name: at most 64 bytes, no \\"}; return false; }
            } else {
                static const char* two[] = {"<=", ">=", "==", "!="};
                std::string op;
                for (auto* o : two) if (code.compare(i, 2, o) == 0) op = o;
                if (op.empty()) {
                    if (!strchr("()[],:=?+-*/<>", c)) { err = {no, (int)i + 1, std::string("unexpected '") + c + "'"}; return false; }
                    op = std::string(1, c);
                }
                t.t = Tok::OP; t.s = op; i += op.size();
            }
            L.toks.push_back(t);
        }
        out.push_back(std::move(L));
    }
    while (!pending.empty() && pending.back().empty()) pending.pop_back();
    tail = std::move(pending);
    return true;
}

struct Parser {
    std::vector<Line> lines;
    size_t li = 0;
    const Line* L = nullptr;
    size_t ti = 0;
    ParseError err{0, 0, ""};
    bool failed = false;

    bool fail(int col, const std::string& m) { if (!failed) { failed = true; err = {L ? L->no : 0, col, m}; } return false; }
    const Tok& tok() { static Tok end; if (ti < L->toks.size()) return L->toks[ti]; end.col = L->toks.empty() ? 1 : L->toks.back().col + (int)L->toks.back().s.size(); return end; }
    bool isOp(const char* o) { const Tok& t = tok(); return t.t == Tok::OP && t.s == o; }
    bool isName(const char* n) { const Tok& t = tok(); return t.t == Tok::NAME && t.s == n; }
    bool expectOp(const char* o) { if (!isOp(o)) return fail(tok().col, std::string("expected '") + o + "'"); ti++; return true; }
    bool atEnd() { return ti >= L->toks.size(); }
    bool expectEnd() { if (!atEnd()) return fail(tok().col, "unexpected '" + tok().s + "'"); return true; }
    bool name(std::string& out, const char* what) {
        if (tok().t != Tok::NAME) return fail(tok().col, std::string("expected ") + what);
        out = tok().s; ti++; return true;
    }

    // a data number: optional '-', no 'f' suffix
    bool dataNum(double& v, int type) {
        bool neg = false;
        int col = tok().col;
        if (isOp("-")) { neg = true; ti++; }
        const Tok& t = tok();
        if (t.t != Tok::NUM) return fail(col, "expected a number");
        if (t.s.back() == 'f') return fail(t.col, "no 'f' here: the type is declared");
        std::string s = (neg ? "-" : "") + t.s;
        errno = 0;
        if (type == T_INT) {
            if (t.s.find_first_of(".eE") != std::string::npos) return fail(t.col, "an int: no '.' or exponent");
            long long n = strtoll(s.c_str(), nullptr, 10);
            if (errno || n < -2147483647LL || n > 2147483647LL) return fail(t.col, "out of range");
            v = (double)n;
        } else if (type == T_F32) {
            float f = strtof(s.c_str(), nullptr);
            if (errno || std::isinf(f)) return fail(t.col, "out of range for f32");
            v = f;
        } else {
            v = strtod(s.c_str(), nullptr);
            if (errno || std::isinf(v)) return fail(t.col, "out of range");
        }
        ti++;
        return true;
    }

    // ---- expressions
    bool expr(Node& n) { return ternary(n); }
    void setPos(Node& n, int col) { n.line = L->no; n.col = col; }
    int depth = 0;
    struct Nest { int& d; ~Nest() { d--; } };
    bool ternary(Node& n) {
        int col = tok().col;
        Nest nest{++depth};
        if (depth > 48) return fail(col, "nested too deep");
        if (!orExpr(n)) return false;
        if (!isOp("?")) return true;
        ti++;
        Node c = std::move(n), a, b;
        if (!expr(a) || !expectOp(":") || !ternary(b)) return false;
        n = Node(); n.op = N_COND; setPos(n, col);
        n.k.push_back(std::move(c)); n.k.push_back(std::move(a)); n.k.push_back(std::move(b));
        return true;
    }
    bool binLevel(Node& n, bool (Parser::*sub)(Node&), const char* word, uint8_t bop) {
        int col = tok().col;
        if (!(this->*sub)(n)) return false;
        while (isName(word)) {
            ti++;
            Node r;
            if (!(this->*sub)(r)) return false;
            Node x; x.op = N_BIN; x.bin = bop; setPos(x, col);
            x.k.push_back(std::move(n)); x.k.push_back(std::move(r));
            n = std::move(x);
        }
        return true;
    }
    bool orExpr(Node& n) { return binLevel(n, &Parser::andExpr, "or", B_OR); }
    bool andExpr(Node& n) { return binLevel(n, &Parser::notExpr, "and", B_AND); }
    bool notExpr(Node& n) {
        if (isName("not")) {
            int col = tok().col; ti++;
            Nest nest{++depth};
            if (depth > 48) return fail(col, "nested too deep");
            Node a;
            if (!notExpr(a)) return false;
            n = Node(); n.op = N_NOT; setPos(n, col); n.k.push_back(std::move(a));
            return true;
        }
        return cmpExpr(n);
    }
    bool cmpExpr(Node& n) {
        int col = tok().col;
        if (!sumExpr(n)) return false;
        static const char* ops[] = {"<", "<=", ">", ">=", "==", "!="};
        for (int k = 0; k < 6; k++)
            if (isOp(ops[k])) {
                ti++;
                Node r;
                if (!sumExpr(r)) return false;
                Node x; x.op = N_BIN; x.bin = (uint8_t)(B_LT + k); setPos(x, col);
                x.k.push_back(std::move(n)); x.k.push_back(std::move(r));
                n = std::move(x);
                for (auto* o : ops) if (isOp(o)) return fail(tok().col, "comparisons don't chain: use and");
                if (isName("in")) return fail(tok().col, "comparisons don't chain");
                return true;
            }
        if (isName("in")) {
            ti++;
            std::string l;
            if (!name(l, "a list name")) return false;
            Node x; x.op = N_IN; x.name = l; setPos(x, col); x.k.push_back(std::move(n));
            n = std::move(x);
            for (auto* o : ops) if (isOp(o)) return fail(tok().col, "comparisons don't chain");
        }
        return true;
    }
    bool sumExpr(Node& n) {
        int col = tok().col;
        if (!prodExpr(n)) return false;
        while (isOp("+") || isOp("-")) {
            uint8_t b = isOp("+") ? B_ADD : B_SUB; ti++;
            Node r;
            if (!prodExpr(r)) return false;
            Node x; x.op = N_BIN; x.bin = b; setPos(x, col);
            x.k.push_back(std::move(n)); x.k.push_back(std::move(r));
            n = std::move(x);
        }
        return true;
    }
    bool prodExpr(Node& n) {
        int col = tok().col;
        if (!unary(n)) return false;
        while (isOp("*") || isOp("/")) {
            uint8_t b = isOp("*") ? B_MUL : B_DIV; ti++;
            Node r;
            if (!unary(r)) return false;
            Node x; x.op = N_BIN; x.bin = b; setPos(x, col);
            x.k.push_back(std::move(n)); x.k.push_back(std::move(r));
            n = std::move(x);
        }
        return true;
    }
    bool unary(Node& n) {
        if (isOp("-")) {
            int col = tok().col; ti++;
            Nest nest{++depth};
            if (depth > 48) return fail(col, "nested too deep");
            if (tok().t == Tok::NUM) { if (!number(n, true)) return false; n.col = col; return true; }
            Node a;
            if (!unary(a)) return false;
            n = Node(); n.op = N_NEG; setPos(n, col); n.k.push_back(std::move(a));
            return true;
        }
        return primary(n);
    }
    bool number(Node& n, bool neg) {
        const Tok& t = tok();
        n = Node(); n.op = N_NUM; setPos(n, t.col);
        std::string s = (neg ? "-" : "") + t.s;
        errno = 0;
        if (t.s.back() == 'f') {
            n.ntype = T_F32;
            s.pop_back();
            float f = strtof(s.c_str(), nullptr);
            if (errno || std::isinf(f)) return fail(t.col, "out of range for f32");
            n.v = f;
        } else if (t.s.find_first_of(".eE") != std::string::npos) {
            n.ntype = T_F64;
            n.v = strtod(s.c_str(), nullptr);
            if (errno || std::isinf(n.v)) return fail(t.col, "out of range");
        } else {
            n.ntype = T_INT;
            long long v = strtoll(s.c_str(), nullptr, 10);
            if (errno || v < -2147483647LL || v > 2147483647LL) return fail(t.col, "out of range for int");
            n.v = (double)v;
        }
        ti++;
        return true;
    }
    bool primary(Node& n) {
        const Tok& t = tok();
        if (t.t == Tok::NUM) return number(n, false);
        if (t.t == Tok::OP && t.s == "(") {
            ti++;
            if (!expr(n) || !expectOp(")")) return false;
            return true;
        }
        if (t.t != Tok::NAME) return fail(t.col, atEnd() ? "expected a value" : "unexpected '" + t.s + "'");
        int col = t.col;
        std::string nm = t.s;
        ti++;
        if (nm == "yes" || nm == "true" || nm == "no" || nm == "false") {
            n = Node(); n.op = N_BOOL; n.v = (nm == "yes" || nm == "true") ? 1 : 0; setPos(n, col); return true;
        }
        if (isKeyword(nm) && nm != "none") return fail(col, "'" + nm + "' can't be used here");
        n = Node(); setPos(n, col);
        if (isOp("(")) {
            ti++;
            n.op = N_CALL; n.name = nm;
            if (!isOp(")"))
                for (;;) {
                    Node a;
                    if (!expr(a)) return false;
                    n.k.push_back(std::move(a));
                    if (isOp(",")) { ti++; continue; }
                    break;
                }
            return expectOp(")");
        }
        if (isOp("[")) {
            ti++;
            n.op = N_INDEX; n.name = nm;
            Node a;
            if (!expr(a) || !expectOp("]")) return false;
            n.k.push_back(std::move(a));
            return true;
        }
        n.op = N_NAME; n.name = nm;
        return true;
    }

    // ---- statements
    bool simple(Node& n) {
        const Tok& t = tok();
        int col = t.col;
        n = Node(); n.line = L->no; n.col = col;
        if (t.t != Tok::NAME) return fail(col, "expected a statement");
        std::string w = t.s;
        if (w == "return") {
            ti++; n.op = N_RETURN;
            if (atEnd()) return true;
            for (;;) {
                Node e;
                if (!expr(e)) return false;
                n.k.push_back(std::move(e));
                if (isOp(",")) { ti++; continue; }
                break;
            }
            return expectEnd();
        }
        if (w == "goto") { ti++; n.op = N_GOTO; Node e; if (!expr(e)) return false; n.k.push_back(std::move(e)); return expectEnd(); }
        if (w == "fallback" || w == "seek" || w == "evade" || w == "pass") {
            ti++; n.op = w == "fallback" ? N_FALLBACK : w == "seek" ? N_SEEK : w == "evade" ? N_EVADE : N_PASS;
            return expectEnd();
        }
        if (w == "if" || w == "elif" || w == "else") return fail(col, "'" + w + "' here needs its own line");
        if (isKeyword(w)) return fail(col, "unexpected '" + w + "'");
        ti++;
        if (!isOp("=")) return fail(tok().col, "expected '=' (an assignment)");
        ti++;
        n.op = N_SET; n.name = w;
        Node e;
        if (!expr(e)) return false;
        n.k.push_back(std::move(e));
        return expectEnd();
    }
    // the rest of an if / elif / else line after the ':' (inline statement) or the block below
    bool armBody(Node::Arm& arm, int indent) {
        arm.trail = L->trail;
        if (!atEnd()) {
            Node s;
            if (!simple(s)) return false;
            arm.body.push_back(std::move(s));
            return true;
        }
        if (!arm.trail.empty()) { /* the trail of a block header stays the arm's */ }
        li++;
        if (li >= lines.size() || lines[li].indent <= indent) { L = li < lines.size() ? &lines[li] : L; return fail(1, "expected an indented block"); }
        int inner = lines[li].indent;
        if (!block(arm.body, inner)) return false;
        li--;   // (stmt() moves past its own line)
        return true;
    }
    bool stmt(std::vector<Node>& out, int indent) {
        L = &lines[li]; ti = 0;
        Node n;
        n.comments = L->comments;
        n.line = L->no; n.col = L->toks[0].col;
        if (isName("elif") || isName("else")) return fail(tok().col, "'" + tok().s + "' without an if");
        if (isName("if")) {
            ti++;
            n.op = N_IF;
            for (bool first = true;; first = false) {
                Node cond;
                if (!expr(cond) || !expectOp(":")) return false;
                Node::Arm arm;
                arm.line = L->no;
                if (!first) arm.comments = L->comments;
                if (!armBody(arm, indent)) return false;
                n.k.push_back(std::move(cond));
                n.arms.push_back(std::move(arm));
                li++;
                if (li < lines.size() && lines[li].indent == indent && !lines[li].toks.empty() && lines[li].toks[0].t == Tok::NAME) {
                    const std::string& w = lines[li].toks[0].s;
                    if (w == "elif") { L = &lines[li]; ti = 1; continue; }
                    if (w == "else") {
                        L = &lines[li]; ti = 1;
                        if (!expectOp(":")) return false;
                        n.hasElse = true;
                        n.elseArm.line = L->no;
                        n.elseArm.comments = L->comments;
                        if (!armBody(n.elseArm, indent)) return false;
                        li++;
                    }
                }
                break;
            }
            out.push_back(std::move(n));
            return true;
        }
        Node s;
        if (!simple(s)) return false;
        s.comments = std::move(n.comments);
        s.trail = L->trail;
        out.push_back(std::move(s));
        li++;
        return true;
    }
    bool block(std::vector<Node>& out, int indent) {
        Nest nest{++depth};
        if (depth > 48) { L = li < lines.size() ? &lines[li] : L; return fail(indent + 1, "nested too deep"); }
        while (li < lines.size()) {
            const Line& l = lines[li];
            if (l.indent < indent) break;
            if (l.indent > indent) { L = &l; return fail(l.indent + 1, "unexpected indent"); }
            if (!stmt(out, indent)) return false;
        }
        return true;
    }

    // ---- declarations
    bool decl(File& f) {
        L = &lines[li]; ti = 0;
        Decl d;
        d.comments = L->comments; d.trail = L->trail; d.line = L->no; d.col = L->toks[0].col;
        if (L->indent != 0) return fail(L->indent + 1, "unexpected indent");
        std::string w;
        if (!name(w, "a declaration (param, curve, list, table, slot)")) return false;
        if (w == "param") {
            d.kind = Decl::PARAM;
            std::string t;
            if (!name(d.name, "a parameter name") || !name(t, "a type (f32, f64, int, bool)")) return false;
            d.ptype = t == "f32" ? T_F32 : t == "f64" ? T_F64 : t == "int" ? T_INT : t == "bool" ? T_BOOL : -1;
            if (d.ptype < 0) return fail(L->toks[ti - 1].col, "a parameter type is f32, f64, int or bool");
            if (!expectOp("=")) return false;
            while (!atEnd()) {
                double v;
                if (d.ptype == T_BOOL) {
                    std::string b;
                    if (!name(b, "yes or no")) return false;
                    if (b != "yes" && b != "no" && b != "true" && b != "false") return fail(L->toks[ti - 1].col, "yes or no");
                    v = (b == "yes" || b == "true") ? 1 : 0;
                } else if (!dataNum(v, d.ptype)) return false;
                d.values.push_back(v);
            }
            if (d.values.size() != 1 && d.values.size() != 4) return fail(tok().col, "one value (every level) or four (BEGINNER INTERMEDIATE ADVANCED EXPERT)");
        } else if (w == "curve") {
            d.kind = Decl::CURVE;
            std::string t;
            if (!name(d.name, "a curve name") || !expectOp("(") || !name(d.arg, "the argument's name") || !name(t, "its type (f32 or f64)") || !expectOp(")") || !expectOp("="))
                return false;
            d.ptype = t == "f32" ? T_F32 : t == "f64" ? T_F64 : -1;
            if (d.ptype < 0) return fail(L->toks[ti - 3].col, "a curve's argument is f32 or f64");
            size_t save = ti;
            bool data = false;
            if ((isName("steps") || isName("points")) && ti + 1 < L->toks.size()) {
                const Tok& nx = L->toks[ti + 1];
                data = nx.t == Tok::NUM || (nx.t == Tok::OP && nx.s == "-");
            }
            if (data) {
                d.form = isName("steps") ? 1 : 2;
                if (d.ptype != T_F32) return fail(tok().col, "steps / points curves take an f32");
                ti++;
                if (d.form == 1) { if (!dataNum(d.base, T_F32)) return false; if (!atEnd() && !expectOp(",")) return false; }
                while (!atEnd()) {
                    double x, y;
                    if (!dataNum(x, T_F32) || !expectOp(":") || !dataNum(y, T_F32)) return false;
                    d.pts.push_back({x, y});
                    if (atEnd()) break;
                    if (!expectOp(",")) return false;
                }
                if (d.pts.size() > 16 || (d.form == 2 && d.pts.empty())) return fail(1, d.form == 2 ? "1 to 16 points" : "at most 16 steps");
                for (size_t k = 1; k < d.pts.size(); k++)
                    if (!(d.pts[k].first > d.pts[k - 1].first)) return fail(1, "x must go up");
            } else {
                ti = save;
                d.form = 0;
                Node e;
                if (!expr(e)) return false;
                d.body.push_back(std::move(e));
                if (!expectEnd()) return false;
            }
        } else if (w == "list") {
            d.kind = Decl::LIST;
            if (!name(d.name, "a list name") || !expectOp("=")) return false;
            while (!atEnd()) {
                std::string wn;
                if (!name(wn, "a weapon")) return false;
                int wi = weaponByName(wn);
                if (wi < 0) return fail(L->toks[ti - 1].col, "'" + wn + "' is not a weapon");
                for (int x : d.items) if (x == wi) return fail(L->toks[ti - 1].col, "'" + wn + "' twice");
                d.items.push_back(wi);
            }
            if (d.items.empty()) return fail(tok().col, "a list needs a weapon");
        } else if (w == "table") {
            d.kind = Decl::TABLE;
            if (!name(d.name, "a table name") || !expectOp("=")) return false;
            for (;;) {
                std::string wn;
                if (!name(wn, "a weapon (or else)")) return false;
                double v;
                if (wn == "else") {
                    if (!dataNum(v, T_F32)) return false;
                    d.hasElse = true; d.elseV = v;
                    if (!expectEnd()) return false;
                    break;
                }
                int wi = weaponByName(wn);
                if (wi < 0) return fail(L->toks[ti - 1].col, "'" + wn + "' is not a weapon");
                for (auto& e : d.entries) if (e.first == wi) return fail(L->toks[ti - 1].col, "'" + wn + "' twice");
                if (!dataNum(v, T_F32)) return false;
                d.entries.push_back({wi, v});
                if (atEnd()) break;
                if (!expectOp(",")) return false;
            }
        } else if (w == "slot") {
            d.kind = Decl::SLOT;
            int col = tok().col;
            if (!name(d.name, "a slot name")) return false;
            if (d.name == "objective") {
                std::string m;
                if (!name(m, "a mode (or default)")) return false;
                int mi = m == "default" ? MODE_COUNT : modeByName(m);
                if (mi < 0) return fail(L->toks[ti - 1].col, "'" + m + "' is not a mode");
                d.slot = SLOT_OBJECTIVE + mi;
            } else {
                for (int k = 0; k < SLOT_OBJECTIVE; k++) if (d.name == kSlotsBase[k].name) d.slot = k;
                if (d.slot < 0) return fail(col, "'" + d.name + "' is not a slot");
            }
            if (!expectOp(":") || !expectEnd()) return false;
            li++;
            if (li >= lines.size() || lines[li].indent == 0) return fail(1, "expected the slot's indented block");
            if (!block(d.body, lines[li].indent)) return false;
            f.decls.push_back(std::move(d));
            return true;
        } else {
            return fail(L->toks[0].col, "expected a declaration (param, curve, list, table, slot)");
        }
        if (!expectEnd()) return false;
        f.decls.push_back(std::move(d));
        li++;
        return true;
    }

    bool file(const std::string& text, File& f) {
        if (!lexLines(text, lines, f.tail, err)) { failed = true; return false; }
        if (lines.empty()) { err = {1, 1, "empty file: it starts with 'version N'"}; failed = true; return false; }
        // (a cap on a line's tokens and on nesting: the printer and compiler recurse over the tree, and a 1 MB stack
        // must hold any file that parses)
        for (const Line& l : lines)
            if (l.toks.size() > 512) { err = {l.no, l.toks[512].col, "line too long: over 512 tokens"}; failed = true; return false; }
        // header: version, brain, levels
        L = &lines[0]; ti = 0;
        if (!L->comments.empty() || !L->trail.empty()) return fail(1, "the file starts with 'version N' (no comments before or on it)");
        if (!isName("version")) return fail(tok().col, "the file starts with 'version N'");
        ti++;
        if (tok().t != Tok::NUM || tok().s.find_first_not_of("0123456789") != std::string::npos) return fail(tok().col, "a version number");
        f.version = atoi(tok().s.c_str()); ti++;
        if (!expectEnd()) return false;
        li = 1;
        if (li >= lines.size()) return fail(1, "expected 'brain \"name\"'");
        L = &lines[li]; ti = 0;
        if (!L->comments.empty() || !L->trail.empty()) return fail(1, "no comments in the header");
        if (!isName("brain")) return fail(tok().col, "expected 'brain \"name\"'");
        ti++;
        if (tok().t != Tok::STR) return fail(tok().col, "expected the name in quotes");
        f.name = tok().s; ti++;
        if (!expectEnd()) return false;
        li++;
        if (li < lines.size() && !lines[li].toks.empty() && lines[li].toks[0].s == "levels") {
            L = &lines[li]; ti = 1;
            if (!L->comments.empty() || !L->trail.empty()) return fail(1, "no comments in the header");
            for (int k = 0; k < 4; k++) {
                std::string n;
                if (!name(n, "a level name") ) return false;
                if (n != kLevelNames[k]) return fail(L->toks[ti - 1].col, std::string("levels are BEGINNER INTERMEDIATE ADVANCED EXPERT (expected ") + kLevelNames[k] + ")");
            }
            if (!expectEnd()) return false;
            li++;
        }
        while (li < lines.size()) if (!decl(f)) return false;
        return true;
    }
};

// =====================================================================================================
// Canonical printer
// =====================================================================================================

struct Printer {
    bool comments = true;
    std::string out;

    int prec(const Node& n) {
        switch (n.op) {
            case N_COND: return 1;
            case N_BIN: return binPrec(n.bin);
            case N_NOT: return 4;
            case N_IN: return 5;
            case N_NEG: return 8;
            case N_NUM: return n.v < 0 || (n.v == 0 && std::signbit(n.v)) ? 8 : 9;
            default: return 9;
        }
    }
    std::string sub(const Node& c, int minPrec, bool force = false) {
        std::string s = ex(c);
        return force || prec(c) < minPrec ? "(" + s + ")" : s;
    }
    std::string ex(const Node& n) {
        switch (n.op) {
            case N_NUM: return fmtLiteral(n.v, n.ntype);
            case N_BOOL: return n.v ? "yes" : "no";
            case N_NAME: return n.name;
            case N_NEG: return "-" + sub(n.k[0], 8);
            case N_NOT: {
                const Node& a = n.k[0];
                bool force = a.op == N_IN || (a.op == N_BIN && binPrec(a.bin) == 5);
                return "not " + sub(a, 4, force);
            }
            case N_IN: return sub(n.k[0], 6) + " in " + n.name;
            case N_COND: return sub(n.k[0], 2, n.k[0].op == N_COND) + " ? " + sub(n.k[1], 1, n.k[1].op == N_COND) + " : " + sub(n.k[2], 1);
            case N_CALL: {
                std::string s = n.name + "(";
                for (size_t i = 0; i < n.k.size(); i++) s += (i ? ", " : "") + ex(n.k[i]);
                return s + ")";
            }
            case N_INDEX: return n.name + "[" + ex(n.k[0]) + "]";
            case N_BIN: {
                int p = binPrec(n.bin);
                const Node &a = n.k[0], &b = n.k[1];
                if (p == 5) return sub(a, 6) + " " + kBinText[n.bin] + " " + sub(b, 6);
                bool fa = n.bin == B_OR && a.op == N_BIN && a.bin == B_AND, fb = n.bin == B_OR && b.op == N_BIN && b.bin == B_AND;
                return sub(a, p, fa) + " " + kBinText[n.bin] + " " + sub(b, p + 1, fb);
            }
            default: return "?";
        }
    }
    void commentLines(const std::vector<std::string>& c, const std::string& ind) {
        if (!comments) return;
        for (auto& l : c) out += l.empty() ? "\n" : ind + l + "\n";
    }
    std::string trailOf(const std::string& t) { return comments && !t.empty() ? " " + t : ""; }
    std::string simpleText(const Node& s) {
        switch (s.op) {
            case N_SET: return s.name + " = " + ex(s.k[0]);
            case N_RETURN: {
                std::string r = "return";
                for (size_t i = 0; i < s.k.size(); i++) r += (i ? ", " : " ") + ex(s.k[i]);
                return r;
            }
            case N_GOTO: return "goto " + ex(s.k[0]);
            case N_FALLBACK: return "fallback";
            case N_SEEK: return "seek";
            case N_EVADE: return "evade";
            default: return "pass";
        }
    }
    bool inlineArm(const Node::Arm& a) { return a.body.size() == 1 && a.body[0].op != N_IF && (!comments || (a.body[0].comments.empty() && a.body[0].trail.empty())); }
    void arm(const std::string& head, const Node::Arm& a, const std::string& ind) {
        if (inlineArm(a)) { out += ind + head + " " + simpleText(a.body[0]) + trailOf(a.trail) + "\n"; return; }
        out += ind + head + trailOf(a.trail) + "\n";
        block(a.body, ind + "  ");
    }
    void stmt(const Node& s, const std::string& ind) {
        commentLines(s.comments, ind);
        if (s.op != N_IF) { out += ind + simpleText(s) + trailOf(s.trail) + "\n"; return; }
        for (size_t i = 0; i < s.arms.size(); i++) {
            if (i) commentLines(s.arms[i].comments, ind);
            arm(std::string(i ? "elif " : "if ") + ex(s.k[i]) + ":", s.arms[i], ind);
        }
        if (s.hasElse) { commentLines(s.elseArm.comments, ind); arm("else:", s.elseArm, ind); }
    }
    void block(const std::vector<Node>& b, const std::string& ind) { for (auto& s : b) stmt(s, ind); }
    std::string value(double v, int t) { return t == T_BOOL ? (v ? "yes" : "no") : fmtNumber(v, t); }
    void decl(const Decl& d) {
        commentLines(d.comments, "");
        switch (d.kind) {
            case Decl::PARAM: {
                out += "param " + d.name + " " + typeName(d.ptype) + " =";
                bool same = d.values.size() == 4 && d.values[1] == d.values[0] && d.values[2] == d.values[0] && d.values[3] == d.values[0];
                for (size_t i = 0; i < (same ? 1 : d.values.size()); i++) out += " " + value(d.values[i], d.ptype);
                break;
            }
            case Decl::CURVE: {
                out += "curve " + d.name + "(" + d.arg + " " + typeName(d.ptype) + ") = ";
                if (d.form == 0) out += ex(d.body[0]);
                else {
                    out += d.form == 1 ? "steps " + fmtNumber(d.base, T_F32) : "points";
                    for (size_t i = 0; i < d.pts.size(); i++)
                        out += std::string(i || d.form == 1 ? ", " : " ") + fmtNumber(d.pts[i].first, T_F32) + ": " + fmtNumber(d.pts[i].second, T_F32);
                }
                break;
            }
            case Decl::LIST:
                out += "list " + d.name + " =";
                for (int w : d.items) out += std::string(" ") + kWeaponNames[w];
                break;
            case Decl::TABLE:
                out += "table " + d.name + " =";
                for (size_t i = 0; i < d.entries.size(); i++) out += std::string(i ? ", " : " ") + kWeaponNames[d.entries[i].first] + " " + fmtNumber(d.entries[i].second, T_F32);
                if (d.hasElse) out += std::string(d.entries.empty() ? " " : ", ") + "else " + fmtNumber(d.elseV, T_F32);
                break;
            case Decl::SLOT:
                out += "slot " + slotName(d.slot) + ":" + trailOf(d.trail) + "\n";
                block(d.body, "  ");
                return;
        }
        out += trailOf(d.trail) + "\n";
    }
    std::string file(const File& f) {
        out = "version " + std::to_string(f.version) + "\nbrain \"" + f.name + "\"\nlevels BEGINNER INTERMEDIATE ADVANCED EXPERT\n";
        for (auto& d : f.decls) decl(d);
        commentLines(f.tail, "");
        return out;
    }
};

// =====================================================================================================
// JSON
// =====================================================================================================

std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

struct JsonAst {
    int nextId = 0;
    std::string id() { return "\"id\":\"n" + std::to_string(++nextId) + "\""; }
    std::string commentsOf(const std::vector<std::string>& c, const std::string& trail) {
        std::string o;
        if (!c.empty()) { o += ",\"comments\":["; for (size_t i = 0; i < c.size(); i++) o += (i ? "," : "") + jstr(c[i]); o += "]"; }
        if (!trail.empty()) o += ",\"trail\":" + jstr(trail);
        return o;
    }
    std::function<std::string(const std::string&)> refOf;   // name -> "param" etc. (set by the exporter)
    std::string ex(const Node& n) {
        std::string o = "{" + id() + ",";
        switch (n.op) {
            case N_NUM: return o + "\"op\":\"num\",\"type\":\"" + typeName(n.ntype) + "\",\"v\":" + fmtNumber(n.v, n.ntype) + "}";
            case N_BOOL: return o + "\"op\":\"bool\",\"v\":" + (n.v ? "true" : "false") + "}";
            case N_NAME: {
                std::string r = refOf ? refOf(n.name) : "";
                return o + "\"op\":\"name\",\"name\":" + jstr(n.name) + (r.empty() ? "" : ",\"ref\":" + jstr(r)) + "}";
            }
            case N_NEG: return o + "\"op\":\"neg\",\"a\":" + ex(n.k[0]) + "}";
            case N_NOT: return o + "\"op\":\"not\",\"a\":" + ex(n.k[0]) + "}";
            case N_COND: return o + "\"op\":\"?:\",\"c\":" + ex(n.k[0]) + ",\"a\":" + ex(n.k[1]) + ",\"b\":" + ex(n.k[2]) + "}";
            case N_IN: return o + "\"op\":\"in\",\"a\":" + ex(n.k[0]) + ",\"list\":" + jstr(n.name) + "}";
            case N_CALL: {
                o += "\"op\":\"call\",\"fn\":" + jstr(n.name) + ",\"args\":[";
                for (size_t i = 0; i < n.k.size(); i++) o += (i ? "," : "") + ex(n.k[i]);
                return o + "]}";
            }
            case N_INDEX: return o + "\"op\":\"index\",\"table\":" + jstr(n.name) + ",\"index\":" + ex(n.k[0]) + "}";
            case N_BIN: return o + "\"op\":\"" + kBinText[n.bin] + "\",\"a\":" + ex(n.k[0]) + ",\"b\":" + ex(n.k[1]) + "}";
            default: return o + "\"op\":\"?\"}";
        }
    }
    std::string body(const std::vector<Node>& b) {
        std::string o = "[";
        for (size_t i = 0; i < b.size(); i++) o += (i ? "," : "") + st(b[i]);
        return o + "]";
    }
    std::string st(const Node& s) {
        std::string o = "{" + id() + ",";
        switch (s.op) {
            case N_SET: o += "\"op\":\"set\",\"name\":" + jstr(s.name) + ",\"value\":" + ex(s.k[0]); break;
            case N_RETURN: { o += "\"op\":\"return\",\"values\":["; for (size_t i = 0; i < s.k.size(); i++) o += (i ? "," : "") + ex(s.k[i]); o += "]"; break; }
            case N_GOTO: o += "\"op\":\"goto\",\"point\":" + ex(s.k[0]); break;
            case N_FALLBACK: o += "\"op\":\"fallback\""; break;
            case N_SEEK: o += "\"op\":\"seek\""; break;
            case N_EVADE: o += "\"op\":\"evade\""; break;
            case N_PASS: o += "\"op\":\"pass\""; break;
            case N_IF: {
                o += "\"op\":\"if\",\"arms\":[";
                for (size_t i = 0; i < s.arms.size(); i++)
                    o += std::string(i ? "," : "") + "{\"cond\":" + ex(s.k[i]) + ",\"body\":" + body(s.arms[i].body) + commentsOf(i ? s.arms[i].comments : std::vector<std::string>{}, s.arms[i].trail) + "}";
                o += "]";
                if (s.hasElse) o += ",\"else\":{\"body\":" + body(s.elseArm.body) + commentsOf(s.elseArm.comments, s.elseArm.trail) + "}";
                break;
            }
            default: break;
        }
        return o + commentsOf(s.comments, s.op == N_IF ? "" : s.trail) + "}";
    }
    std::string decl(const Decl& d) {
        std::string o = "{" + id() + ",";
        switch (d.kind) {
            case Decl::PARAM: {
                o += "\"kind\":\"param\",\"name\":" + jstr(d.name) + ",\"type\":\"" + typeName(d.ptype) + "\",\"values\":[";
                for (size_t i = 0; i < d.values.size(); i++) o += (i ? "," : "") + (d.ptype == T_BOOL ? std::string(d.values[i] ? "true" : "false") : fmtNumber(d.values[i], d.ptype));
                o += "]";
                break;
            }
            case Decl::CURVE:
                o += "\"kind\":\"curve\",\"name\":" + jstr(d.name) + ",\"arg\":" + jstr(d.arg) + ",\"argType\":\"" + typeName(d.ptype) + "\",\"form\":\"" + (d.form == 0 ? "expr" : d.form == 1 ? "steps" : "points") + "\"";
                if (d.form == 0) o += ",\"body\":" + ex(d.body[0]);
                else {
                    if (d.form == 1) o += ",\"base\":" + fmtNumber(d.base, T_F32);
                    o += ",\"points\":[";
                    for (size_t i = 0; i < d.pts.size(); i++) o += std::string(i ? "," : "") + "[" + fmtNumber(d.pts[i].first, T_F32) + "," + fmtNumber(d.pts[i].second, T_F32) + "]";
                    o += "]";
                }
                break;
            case Decl::LIST:
                o += "\"kind\":\"list\",\"name\":" + jstr(d.name) + ",\"items\":[";
                for (size_t i = 0; i < d.items.size(); i++) o += std::string(i ? "," : "") + "\"" + kWeaponNames[d.items[i]] + "\"";
                o += "]";
                break;
            case Decl::TABLE:
                o += "\"kind\":\"table\",\"name\":" + jstr(d.name) + ",\"entries\":[";
                for (size_t i = 0; i < d.entries.size(); i++) o += std::string(i ? "," : "") + "[\"" + kWeaponNames[d.entries[i].first] + "\"," + fmtNumber(d.entries[i].second, T_F32) + "]";
                o += "]";
                if (d.hasElse) o += ",\"else\":" + fmtNumber(d.elseV, T_F32);
                break;
            case Decl::SLOT: {
                int m = d.slot - SLOT_OBJECTIVE;
                o += "\"kind\":\"slot\",\"name\":" + jstr(d.slot < SLOT_OBJECTIVE ? kSlotsBase[d.slot].name : "objective");
                if (m >= 0) o += ",\"mode\":" + jstr(m < MODE_COUNT ? kModeNames[m] : "default");
                o += ",\"body\":" + body(d.body);
                break;
            }
        }
        return o + commentsOf(d.comments, d.trail) + "}";
    }
    std::string file(const File& f) {
        std::string o = "{\"format\":\"qbrain-ast\",\"version\":" + std::to_string(f.version) + ",\"name\":" + jstr(f.name) +
                        ",\"levels\":[\"BEGINNER\",\"INTERMEDIATE\",\"ADVANCED\",\"EXPERT\"],\"decls\":[";
        for (size_t i = 0; i < f.decls.size(); i++) o += (i ? ",\n" : "\n") + decl(f.decls[i]);
        o += "],\"tail\":[";
        for (size_t i = 0; i < f.tail.size(); i++) o += (i ? "," : "") + jstr(f.tail[i]);
        return o + "]}";
    }
};

} // namespace

// =====================================================================================================
// Compiler: type check, definite assignment, code for the interpreter
// =====================================================================================================

namespace {

enum Op : uint8_t {
    OP_LDK, OP_LDP, OP_LDS, OP_LAZY, OP_MOV,
    OP_ADDF, OP_SUBF, OP_MULF, OP_DIVF, OP_ADDD, OP_SUBD, OP_MULD, OP_DIVD, OP_ADDI, OP_SUBI, OP_MULI,
    OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE, OP_NOT, OP_NEG, OP_TOF,
    OP_ABS, OP_SQRTF, OP_SQRTD, OP_EXPF, OP_EXPD, OP_MIN, OP_MAX, OP_CLAMP,
    OP_ROLL, OP_GAUSS, OP_CHANCE, OP_ALLOWED, OP_FIRST, OP_IN, OP_TABLE, OP_CURVE,
    OP_JMP, OP_JZ, OP_JNZ, OP_RET
};
constexpr int kMaxRegs = 64;

struct Merged {
    File file;                                  // the default's declarations with the user's in their places, then the user's own
    std::map<std::string, size_t> byName;       // param / curve / list / table name -> decl index
    int slotDecl[SLOT_COUNT];                   // decl index of each slot (-1: none)
};

struct Compiler {
    Brain& B;
    BrainDiag& diag;
    const Merged& M;
    std::map<std::string, int> params;          // name -> P index
    std::vector<int> ptype;
    std::map<std::string, int> lists, tables, dataCurves;
    std::map<std::string, const Decl*> exprCurves;
    std::map<std::string, int> curveType;
    // per slot
    int slot = 0;
    struct Var { int reg, type; bool output; };
    std::map<std::string, Var> vars;
    std::vector<std::pair<std::string, Var>> args;   // curve arguments in scope
    int curveDepth = 0;
    std::vector<std::string> curveStack;
    std::set<std::string> assigned;
    int firstTemp = 0, nextReg = 0, maxReg = 0;
    int base = 0;

    Compiler(Brain& b, BrainDiag& d, const Merged& m) : B(b), diag(d), M(m) {}

    void error(int line, int col, const std::string& m) { diag.errors.push_back({line, col, m}); }
    void error(const Node& n, const std::string& m) { error(n.line, n.col, m); }
    int emit(uint8_t op, int d = 0, int a = 0, int b = 0, int k = 0) {
        B.code.push_back({op, (uint8_t)d, (uint8_t)a, (uint8_t)b, k});
        return (int)B.code.size() - 1 - base;
    }
    int here() const { return (int)B.code.size() - base; }
    void patch(int at, int target) { B.code[base + at].k = target; }
    int konst(double v) {
        for (size_t i = 0; i < B.konst.size(); i++) if (memcmp(&B.konst[i], &v, sizeof v) == 0) return (int)i;
        B.konst.push_back(v);
        return (int)B.konst.size() - 1;
    }
    int tmp(const Node& n) {
        if (nextReg >= kMaxRegs) { error(n, "too complex: the slot needs more than 64 registers"); return kMaxRegs - 1; }
        int r = nextReg++;
        maxReg = std::max(maxReg, nextReg);
        return r;
    }
    int ldk(const Node& n, double v) { int d = tmp(n); emit(OP_LDK, d, 0, 0, konst(v)); return d; }

    static int promote(int a, int b) {
        if (a == T_NONE || b == T_NONE) return T_NONE;
        if (isNumeric(a) && isNumeric(b)) return a == T_F64 || b == T_F64 ? T_F64 : a == T_F32 || b == T_F32 ? T_F32 : T_INT;
        return a == b ? a : -1;
    }
    // implicit conversion of a value of type `from` to `to` (widening only); -1: not allowed
    int convert(const Node& n, int reg, int from, int to, const char* what) {
        if (from == to || from == T_NONE || to == T_NONE) return reg;
        if (from == T_INT && to == T_F32) { int d = tmp(n); emit(OP_TOF, d, reg); return d; }
        if ((from == T_INT || from == T_F32) && to == T_F64) return reg;
        if (from == T_F64 && to == T_F32) error(n, std::string(what) + ": an f64 doesn't go into an f32 without f32(...)");
        else error(n, std::string(what) + ": a " + typeName(from) + " where a " + typeName(to) + " is wanted");
        return reg;
    }

    // ---- names
    enum Ref { R_NONE, R_ARG, R_VAR, R_SENSOR, R_PARAM, R_WEAPON, R_MODE, R_LIST, R_POINT };
    Ref resolve(const std::string& nm, int& idx, int& type) {
        for (auto it = args.rbegin(); it != args.rend(); ++it) if (it->first == nm) { idx = it->second.reg; type = it->second.type; return R_ARG; }
        if (!curveDepth) {
            auto v = vars.find(nm);
            if (v != vars.end()) { idx = v->second.reg; type = v->second.type; return R_VAR; }
            for (int s = 0; s < S_COUNT; s++) if (nm == kSensors[s].name) { idx = s; type = kSensors[s].type; return R_SENSOR; }
        }
        auto p = params.find(nm);
        if (p != params.end()) { idx = p->second; type = ptype[p->second]; return R_PARAM; }
        if (nm == "none") { idx = -1; type = T_WEAPON; return R_WEAPON; }
        int w = weaponByName(nm);
        if (w >= 0) { idx = w; type = T_WEAPON; return R_WEAPON; }
        int m = modeByName(nm);
        if (m >= 0) { idx = m; type = T_MODE; return R_MODE; }
        auto l = lists.find(nm);
        if (l != lists.end()) { idx = l->second; type = T_LIST; return R_LIST; }
        for (int k = 0; k < PT_SITE; k++) if (nm == kPoints[k].name) { idx = k; type = T_POINT; return R_POINT; }
        return R_NONE;
    }

    // ---- type of an expression (no code)
    int typeOf(const Node& n) {
        switch (n.op) {
            case N_NUM: return n.ntype;
            case N_BOOL: return T_BOOL;
            case N_NAME: { int idx, t; Ref r = resolve(n.name, idx, t); return r == R_NONE ? T_NONE : t; }
            case N_NEG: return typeOf(n.k[0]);
            case N_NOT: case N_IN: return T_BOOL;
            case N_INDEX: return T_F32;
            case N_COND: { int t = promote(typeOf(n.k[1]), typeOf(n.k[2])); return t < 0 ? T_NONE : t; }
            case N_BIN:
                if (n.bin >= B_LT) return T_BOOL;
                { int t = promote(typeOf(n.k[0]), typeOf(n.k[1])); return t < 0 ? T_NONE : t; }
            case N_CALL: {
                const std::string& f = n.name;
                if (f == "roll" || f == "gauss" || f == "f32") return T_F32;
                if (f == "f64") return T_F64;
                if (f == "chance" || f == "allowed") return T_BOOL;
                if (f == "firstAllowed") return T_WEAPON;
                if (f == "site") return T_POINT;
                if (f == "abs" || f == "sqrt" || f == "exp") return n.k.empty() ? T_NONE : typeOf(n.k[0]);
                if (f == "min" || f == "max" || f == "clamp") {
                    int t = n.k.empty() ? T_NONE : typeOf(n.k[0]);
                    for (size_t i = 1; i < n.k.size(); i++) { t = promote(t, typeOf(n.k[i])); if (t < 0) return T_NONE; }
                    return t;
                }
                if (dataCurves.count(f)) return T_F32;
                auto c = curveType.find(f);
                if (c != curveType.end()) return c->second;
                auto e = exprCurves.find(f);
                if (e != exprCurves.end()) {
                    for (auto& s : curveStack) if (s == f) return T_NONE;
                    curveStack.push_back(f);
                    args.push_back({e->second->arg, {0, e->second->ptype, false}});
                    curveDepth++;
                    int t = typeOf(e->second->body[0]);
                    curveDepth--;
                    args.pop_back();
                    curveStack.pop_back();
                    curveType[f] = t;
                    return t;
                }
                return T_NONE;
            }
            default: return T_NONE;
        }
    }

    // ---- code for an expression: the register holding it (a variable's own, or a new temporary)
    int ce(const Node& n, int& type) {
        type = T_NONE;
        switch (n.op) {
            case N_NUM: type = n.ntype; return ldk(n, n.v);
            case N_BOOL: type = T_BOOL; return ldk(n, n.v);
            case N_NAME: {
                int idx, t;
                Ref r = resolve(n.name, idx, t);
                type = t;
                switch (r) {
                    case R_NONE:
                        if (curveDepth) {
                            int i2, t2;
                            int save = curveDepth; curveDepth = 0;
                            Ref r2 = resolve(n.name, i2, t2);
                            curveDepth = save;
                            if (r2 == R_SENSOR || r2 == R_VAR) { error(n, "a curve sees only its argument, params and other curves: '" + n.name + "' isn't one"); type = T_NONE; return ldk(n, 0); }
                        }
                        error(n, "unknown name '" + n.name + "'");
                        type = T_NONE;
                        return ldk(n, 0);
                    case R_ARG: return idx;
                    case R_VAR:
                        if (!assigned.count(n.name)) error(n, "'" + n.name + "' may not be set here");
                        return idx;
                    case R_SENSOR: {
                        if (!slotHasSensor(slot, idx)) { error(n, "sensor '" + n.name + "' isn't available in slot " + slotName(slot)); return ldk(n, 0); }
                        int d = tmp(n);
                        emit(kSensors[idx].lazy ? OP_LAZY : OP_LDS, d, 0, 0, idx);
                        return d;
                    }
                    case R_PARAM: { int d = tmp(n); emit(OP_LDP, d, 0, 0, idx); return d; }
                    case R_WEAPON: case R_MODE: case R_LIST: return ldk(n, idx);
                    case R_POINT: error(n, "a point is only for goto"); type = T_NONE; return ldk(n, 0);
                }
                return ldk(n, 0);
            }
            case N_NEG: {
                int t, a = ce(n.k[0], t);
                if (!isNumeric(t) && t != T_NONE) error(n, std::string("'-' of a ") + typeName(t));
                int d = tmp(n);
                emit(OP_NEG, d, a);
                type = t;
                return d;
            }
            case N_NOT: {
                int t, a = ce(n.k[0], t);
                if (t != T_BOOL && t != T_NONE) error(n, std::string("'not' of a ") + typeName(t) + " (it takes a bool)");
                int d = tmp(n);
                emit(OP_NOT, d, a);
                type = T_BOOL;
                return d;
            }
            case N_IN: {
                int t, a = ce(n.k[0], t);
                if (t != T_WEAPON && t != T_NONE) error(n, "'in' tests a weapon");
                auto l = lists.find(n.name);
                if (l == lists.end()) { error(n, "unknown list '" + n.name + "'"); type = T_BOOL; return ldk(n, 0); }
                int lr = ldk(n, l->second), d = tmp(n);
                emit(OP_IN, d, a, lr);
                type = T_BOOL;
                return d;
            }
            case N_INDEX: {
                int t, a = ce(n.k[0], t);
                if (t != T_WEAPON && t != T_NONE) error(n, "a table is looked up by weapon");
                auto tb = tables.find(n.name);
                if (tb == tables.end()) { error(n, "unknown table '" + n.name + "'"); type = T_F32; return ldk(n, 0); }
                int d = tmp(n);
                emit(OP_TABLE, d, a, 0, tb->second);
                type = T_F32;
                return d;
            }
            case N_COND: {
                int tc, c = ce(n.k[0], tc);
                if (tc != T_BOOL && tc != T_NONE) error(n.k[0], "a condition is a bool");
                int ta = typeOf(n.k[1]), tb = typeOf(n.k[2]);
                int t = promote(ta, tb);
                if (t < 0) { error(n, std::string("'?:' branches are a ") + typeName(ta) + " and a " + typeName(tb)); t = T_NONE; }
                int d = tmp(n);
                int jz = emit(OP_JZ, 0, c);
                int t1, a = ce(n.k[1], t1);
                a = convert(n.k[1], a, t1, t, "'?:'");
                emit(OP_MOV, d, a);
                int jmp = emit(OP_JMP);
                patch(jz, here());
                int t2, b = ce(n.k[2], t2);
                b = convert(n.k[2], b, t2, t, "'?:'");
                emit(OP_MOV, d, b);
                patch(jmp, here());
                type = t;
                return d;
            }
            case N_BIN: return bin(n, type);
            case N_CALL: return call(n, type);
            default: error(n, "not a value"); return ldk(n, 0);
        }
    }

    int bin(const Node& n, int& type) {
        const Node &A = n.k[0], &Bn = n.k[1];
        if (n.bin == B_AND || n.bin == B_OR) {
            int ta, a = ce(A, ta);
            if (ta != T_BOOL && ta != T_NONE) error(A, std::string("'") + kBinText[n.bin] + "' takes bools");
            int d = tmp(n);
            emit(OP_MOV, d, a);
            int j = emit(n.bin == B_AND ? OP_JZ : OP_JNZ, 0, d);
            int tb, b = ce(Bn, tb);
            if (tb != T_BOOL && tb != T_NONE) error(Bn, std::string("'") + kBinText[n.bin] + "' takes bools");
            emit(OP_MOV, d, b);
            patch(j, here());
            type = T_BOOL;
            return d;
        }
        int ta0 = typeOf(A), tb0 = typeOf(Bn);
        int t = promote(ta0, tb0);
        bool cmp = n.bin >= B_LT;
        bool bad = t < 0;
        if (!bad && t != T_NONE && !isNumeric(t)) bad = !cmp || (n.bin != B_EQ && n.bin != B_NE) || t == T_LIST || t == T_POINT;
        if (bad) {
            error(n, std::string("'") + kBinText[n.bin] + "' of a " + typeName(ta0) + " and a " + typeName(tb0));
            t = T_NONE;
        }
        if (n.bin == B_DIV && t == T_INT) error(n, "int / int: write a decimal (1.0 / 2)");
        int ta, a = ce(A, ta);
        a = convert(A, a, ta, t, kBinText[n.bin]);
        int tb, b = ce(Bn, tb);
        b = convert(Bn, b, tb, t, kBinText[n.bin]);
        int d = tmp(n);
        if (cmp) { emit((uint8_t)(OP_LT + (n.bin - B_LT)), d, a, b); type = T_BOOL; return d; }
        int base3 = t == T_F64 ? OP_ADDD : t == T_INT ? OP_ADDI : OP_ADDF;
        int off = n.bin - B_ADD;
        if (t == T_INT && off == 3) off = 2;   // (int / int: reported above)
        emit((uint8_t)(base3 + off), d, a, b);
        type = t;
        return d;
    }

    int call(const Node& n, int& type) {
        const std::string& f = n.name;
        auto nargs = [&](size_t want) {
            if (n.k.size() != want) { error(n, f + "() takes " + std::to_string(want) + " argument" + (want == 1 ? "" : "s")); return false; }
            return true;
        };
        if (f == "roll" || f == "gauss") {
            nargs(0);
            int d = tmp(n);
            emit(f == "roll" ? OP_ROLL : OP_GAUSS, d);
            type = T_F32;
            return d;
        }
        if (f == "chance") {
            type = T_BOOL;
            if (!nargs(1)) return ldk(n, 0);
            int cache = -1;
            const Node& a0 = n.k[0];
            int idx, t0;
            if (a0.op == N_NAME && resolve(a0.name, idx, t0) == R_PARAM && t0 == T_F32) {
                for (size_t i = 0; i < B.chanceParams.size(); i++) if (B.chanceParams[i] == idx) cache = (int)i;
                if (cache < 0 && B.chanceParams.size() < 16) { B.chanceParams.push_back(idx); cache = (int)B.chanceParams.size() - 1; }
            }
            int t, a = ce(a0, t);
            a = convert(a0, a, t, T_F32, "chance()");
            int d = tmp(n);
            emit(OP_CHANCE, d, a, 0, cache);
            return d;
        }
        if (f == "min" || f == "max" || f == "clamp") {
            size_t want = f == "clamp" ? 3 : 2;
            if (!nargs(want)) { type = T_NONE; return ldk(n, 0); }
            int t = typeOf(n);
            if (t != T_NONE && !isNumeric(t)) { error(n, f + "() takes numbers"); t = T_NONE; }
            int r[3];
            for (size_t i = 0; i < want; i++) { int ti; r[i] = ce(n.k[i], ti); r[i] = convert(n.k[i], r[i], ti, t, (f + "()").c_str()); }
            int d = tmp(n);
            if (f == "clamp") emit(OP_CLAMP, d, r[0], r[1], r[2]);
            else emit(f == "min" ? OP_MIN : OP_MAX, d, r[0], r[1]);
            type = t;
            return d;
        }
        if (f == "abs" || f == "sqrt" || f == "exp") {
            if (!nargs(1)) { type = T_NONE; return ldk(n, 0); }
            int t, a = ce(n.k[0], t);
            if (t != T_F32 && t != T_F64 && t != T_NONE) error(n, f + "() takes an f32 or f64 (write 2.0f, not 2)");
            int d = tmp(n);
            if (f == "abs") emit(OP_ABS, d, a);
            else if (f == "sqrt") emit(t == T_F64 ? OP_SQRTD : OP_SQRTF, d, a);
            else emit(t == T_F64 ? OP_EXPD : OP_EXPF, d, a);
            type = t;
            return d;
        }
        if (f == "f32" || f == "f64") {
            if (!nargs(1)) { type = T_NONE; return ldk(n, 0); }
            int t, a = ce(n.k[0], t);
            if (!isNumeric(t) && t != T_NONE) error(n, f + "() converts a number");
            type = f == "f32" ? T_F32 : T_F64;
            if (f == "f64" || t == T_F32) return a;
            int d = tmp(n);
            emit(OP_TOF, d, a);
            return d;
        }
        if (f == "allowed") {
            type = T_BOOL;
            if (!nargs(1)) return ldk(n, 0);
            int t, a = ce(n.k[0], t);
            if (t != T_WEAPON && t != T_NONE) error(n, "allowed() takes a weapon");
            int d = tmp(n);
            emit(OP_ALLOWED, d, a);
            return d;
        }
        if (f == "firstAllowed") {
            type = T_WEAPON;
            if (!nargs(2)) return ldk(n, 0);
            int tl, l = ce(n.k[0], tl), tw, w = ce(n.k[1], tw);
            if (tl != T_LIST && tl != T_NONE) error(n.k[0], "firstAllowed() takes a list first");
            if (tw != T_WEAPON && tw != T_NONE) error(n.k[1], "firstAllowed()'s second argument is a weapon");
            int d = tmp(n);
            emit(OP_FIRST, d, l, w);
            return d;
        }
        if (f == "site") { error(n, "site() is a point: only for goto"); type = T_NONE; return ldk(n, 0); }
        auto dc = dataCurves.find(f);
        if (dc != dataCurves.end()) {
            type = T_F32;
            if (!nargs(1)) return ldk(n, 0);
            int t, a = ce(n.k[0], t);
            a = convert(n.k[0], a, t, T_F32, (f + "()").c_str());
            int d = tmp(n);
            emit(OP_CURVE, d, a, 0, dc->second);
            return d;
        }
        auto ec = exprCurves.find(f);
        if (ec != exprCurves.end()) {
            const Decl& cd = *ec->second;
            for (auto& s : curveStack) if (s == f) { error(n, "curve '" + f + "' uses itself"); type = T_NONE; return ldk(n, 0); }
            if (!nargs(1)) { type = T_NONE; return ldk(n, 0); }
            int t, a = ce(n.k[0], t);
            a = convert(n.k[0], a, t, cd.ptype, (f + "()").c_str());
            curveStack.push_back(f);
            args.push_back({cd.arg, {a, cd.ptype, false}});
            curveDepth++;
            int r = ce(cd.body[0], type);
            curveDepth--;
            args.pop_back();
            curveStack.pop_back();
            return r;
        }
        if (isBuiltin(f)) error(n, f + "() can't be used here");
        else error(n, "unknown function '" + f + "'");
        type = T_NONE;
        return ldk(n, 0);
    }

    // ---- statements; true if the block always ends the slot (return / an action)
    bool isObjective() const { return slot >= SLOT_OBJECTIVE; }
    bool stmt(const Node& s) {
        nextReg = firstTemp;
        switch (s.op) {
            case N_PASS: return false;
            case N_SET: {
                const SlotInfo& si = slotInfo(slot);
                int idx, t0;
                Ref r = resolve(s.name, idx, t0);
                auto v = vars.find(s.name);
                if (v == vars.end() && r != R_NONE) { error(s, "'" + s.name + "' is a " + (r == R_SENSOR ? "sensor" : r == R_PARAM ? "param" : "name") + ": it can't be assigned"); return false; }
                int t, a = ce(s.k[0], t);
                if (v == vars.end()) {   // a new local
                    if (t == T_POINT || t == T_LIST) error(s, std::string("a local can't hold a ") + typeName(t));
                    if (isKeyword(s.name) || isBuiltin(s.name)) { error(s, "'" + s.name + "' is reserved"); return false; }
                    (void)si;
                    int reg = nextLocal++;
                    if (reg >= firstTempFixed) { error(s, "too many locals"); return false; }
                    vars[s.name] = {reg, t, false};
                    emit(OP_MOV, reg, a);
                } else {
                    a = convert(s.k[0], a, t, v->second.type, ("'" + s.name + "'").c_str());
                    emit(OP_MOV, v->second.reg, a);
                }
                assigned.insert(s.name);
                return false;
            }
            case N_RETURN: {
                const SlotInfo& si = slotInfo(slot);
                if (isObjective() && !s.k.empty()) { error(s, "an objective slot ends with goto, fallback, seek or evade"); return true; }
                if (s.k.size() > si.outs.size()) { error(s, "slot " + slotName(slot) + " has " + std::to_string(si.outs.size()) + " output(s)"); return true; }
                for (size_t i = 0; i < s.k.size(); i++) {
                    int t, a = ce(s.k[i], t);
                    a = convert(s.k[i], a, t, si.outs[i].type, ("output '" + std::string(si.outs[i].name) + "'").c_str());
                    emit(OP_MOV, (int)i, a);
                }
                emit(OP_RET);
                return true;
            }
            case N_GOTO: case N_FALLBACK: case N_SEEK: case N_EVADE: {
                if (!isObjective()) { error(s, "goto, fallback, seek and evade are for objective slots"); return true; }
                if (s.op == N_GOTO) {
                    const Node& p = s.k[0];
                    if (p.op == N_CALL && p.name == "site") {
                        if (p.k.size() != 1) error(p, "site() takes 1 argument");
                        else {
                            int t, a = ce(p.k[0], t);
                            if (t != T_INT && t != T_NONE) error(p.k[0], "site() takes an int");
                            emit(OP_MOV, 2, a);
                        }
                        emit(OP_LDK, 1, 0, 0, konst(PT_SITE));
                    } else {
                        int idx = -1;
                        if (p.op == N_NAME) for (int k = 0; k < PT_SITE; k++) if (p.name == kPoints[k].name) idx = k;
                        if (idx < 0) error(p, "goto takes a point: me.pos, checkpoint.next, hill.floor, cart.goal, point.floor, bomb.pos, carrier.pos or site(i)");
                        emit(OP_LDK, 1, 0, 0, konst(idx < 0 ? 0 : idx));
                    }
                }
                emit(OP_LDK, 0, 0, 0, konst(s.op == N_GOTO ? ACT_GOTO : s.op == N_FALLBACK ? ACT_FALLBACK : s.op == N_SEEK ? ACT_SEEK : ACT_EVADE));
                emit(OP_RET);
                return true;
            }
            case N_IF: {
                std::set<std::string> in = assigned, outSet;
                bool any = false, allTerm = true;
                std::vector<int> ends;
                for (size_t i = 0; i < s.arms.size(); i++) {
                    assigned = in;
                    nextReg = firstTemp;
                    int t, c = ce(s.k[i], t);
                    if (t != T_BOOL && t != T_NONE) error(s.k[i], std::string("an if needs a bool, not a ") + typeName(t));
                    int jz = emit(OP_JZ, 0, c);
                    bool term = block(s.arms[i].body);
                    if (!term) {
                        if (!any) outSet = assigned;
                        else { std::set<std::string> x; for (auto& n : outSet) if (assigned.count(n)) x.insert(n); outSet = x; }
                        any = true; allTerm = false;
                    }
                    if (i + 1 < s.arms.size() || s.hasElse) ends.push_back(emit(OP_JMP));
                    patch(jz, here());
                }
                if (s.hasElse) {
                    assigned = in;
                    bool term = block(s.elseArm.body);
                    if (!term) {
                        if (!any) outSet = assigned;
                        else { std::set<std::string> x; for (auto& n : outSet) if (assigned.count(n)) x.insert(n); outSet = x; }
                        any = true; allTerm = false;
                    }
                } else {
                    allTerm = false;
                    if (!any) outSet = in;
                    else { std::set<std::string> x; for (auto& n : outSet) if (in.count(n)) x.insert(n); outSet = x; }
                }
                for (int j : ends) patch(j, here());
                assigned = allTerm ? in : outSet;
                return allTerm;
            }
            default: error(s, "not a statement"); return false;
        }
    }
    bool block(const std::vector<Node>& b) {
        bool term = false;
        for (auto& s : b) {
            if (term) { diag.warnings.push_back({s.line, s.col, "never runs (the slot has ended above)"}); }
            term = stmt(s) || term;
        }
        return term;
    }
    int nextLocal = 0, firstTempFixed = 0;
    void collectLocals(const std::vector<Node>& b, std::set<std::string>& names) {
        for (auto& s : b) {
            if (s.op == N_SET) names.insert(s.name);
            if (s.op == N_IF) { for (auto& a : s.arms) collectLocals(a.body, names); if (s.hasElse) collectLocals(s.elseArm.body, names); }
        }
    }
    int drawsE(const Node& n) {
        int d = 0;
        if (n.op == N_CALL) {
            if (n.name == "roll" || n.name == "chance") d = 1;
            if (n.name == "gauss") d = 2;
            auto ec = exprCurves.find(n.name);
            if (ec != exprCurves.end() && curveStack.size() < 8) { curveStack.push_back(n.name); d += drawsE(ec->second->body[0]); curveStack.pop_back(); }
        }
        if (n.op == N_COND) return drawsE(n.k[0]) + std::max(drawsE(n.k[1]), drawsE(n.k[2]));
        for (auto& c : n.k) d += drawsE(c);
        return d;
    }
    int drawsB(const std::vector<Node>& b) {
        int d = 0;
        for (auto& s : b) {
            if (s.op == N_IF) {
                int conds = 0, best = 0;
                for (size_t i = 0; i < s.arms.size(); i++) { conds += drawsE(s.k[i]); best = std::max(best, conds + drawsB(s.arms[i].body)); }
                if (s.hasElse) best = std::max(best, conds + drawsB(s.elseArm.body));
                d += std::max(best, conds);
            } else for (auto& c : s.k) d += drawsE(c);
        }
        return d;
    }

    void compileSlot(int sl, const Decl& d) {
        slot = sl;
        vars.clear(); args.clear(); assigned.clear(); curveStack.clear(); curveDepth = 0;
        base = (int)B.code.size();
        B.start[sl] = base;
        const SlotInfo& si = slotInfo(sl);
        int nOut = (int)si.outs.size();
        for (int i = 0; i < nOut; i++) { vars[si.outs[i].name] = {i, si.outs[i].type, true}; assigned.insert(si.outs[i].name); }
        std::set<std::string> locals;
        collectLocals(d.body, locals);
        int nLocals = 0;
        for (auto& l : locals) if (!vars.count(l)) nLocals++;
        nextLocal = nOut;
        firstTempFixed = firstTemp = nOut + nLocals;
        if (firstTemp > kMaxRegs - 8) { error(d.line, d.col, "too many locals"); return; }
        nextReg = maxReg = firstTemp;
        for (int i = 0; i < nOut; i++) emit(OP_LDK, i, 0, 0, konst(0));
        bool term = block(d.body);
        if (isObjective() && !term) error(d.line, d.col, "slot " + slotName(sl) + " must end in goto, fallback, seek or evade on every path");
        emit(OP_RET);
        B.regs[sl] = (uint8_t)std::max(maxReg, firstTemp);
        B.rolls[sl] = (uint8_t)std::min(255, drawsB(d.body));
        // (an output never written stays 0 / no)
        if (!isObjective()) {
            std::set<std::string> written;
            collectLocals(d.body, written);
            bool returnsValues = false;
            std::function<void(const std::vector<Node>&)> scan = [&](const std::vector<Node>& b) {
                for (auto& s : b) {
                    if (s.op == N_RETURN && !s.k.empty()) returnsValues = true;
                    if (s.op == N_IF) { for (auto& a : s.arms) scan(a.body); if (s.hasElse) scan(s.elseArm.body); }
                }
            };
            scan(d.body);
            for (auto& o : si.outs)
                if (!written.count(o.name) && !returnsValues) diag.warnings.push_back({d.line, d.col, "slot " + slotName(sl) + ": output '" + o.name + "' is never set (stays 0 / no)"});
        }
        // jumps are relative to the slot's start: make them absolute in the program
        for (size_t i = base; i < B.code.size(); i++) {
            BrainInsn& in = B.code[i];
            if (in.op == OP_JMP || in.op == OP_JZ || in.op == OP_JNZ) in.k += base;
        }
    }
};

// ---- expanded text of a slot (curves written out): equal to the default's -> the native code runs it
std::string expandedText(const Decl& d, const std::map<std::string, const Decl*>& curves) {
    // print without comments, then append every expression curve the slot uses (transitively), by name
    Printer p;
    p.comments = false;
    p.block(d.body, "  ");
    std::string s = p.out;
    std::set<std::string> seen;
    std::vector<const Node*> todo;
    std::function<void(const Node&)> walk = [&](const Node& n) {
        if (n.op == N_CALL) {
            auto c = curves.find(n.name);
            if (c != curves.end() && !seen.count(n.name)) {
                seen.insert(n.name);
                Printer q; q.comments = false;
                s += "curve " + n.name + "(" + c->second->arg + " " + typeName(c->second->ptype) + ") = " + q.ex(c->second->body[0]) + "\n";
                walk(c->second->body[0]);
            }
        }
        for (auto& k : n.k) walk(k);
        for (auto& a : n.arms) for (auto& b : a.body) walk(b);
        if (n.hasElse) for (auto& b : n.elseArm.body) walk(b);
    };
    for (auto& st : d.body) walk(st);
    return s;
}

// =====================================================================================================
// Format versions: one converter per older version (like maps' kMapUpgrades)
// =====================================================================================================

using BrainUpgrade = std::string (*)(const std::string& text);   // version N -> N + 1 (text in, text out)
// kBrainUpgrades[k] converts version k + 1 to k + 2. Version 1 is the first: none yet.
const BrainUpgrade kBrainUpgrades[] = {nullptr};
constexpr int kNumBrainUpgrades = 0;
static_assert(kNumBrainUpgrades == kBrainFormatVersion - 1, "every older brain format version needs a converter in kBrainUpgrades");

int textVersion(const std::string& text) {
    size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r')) i++;
    if (text.compare(i, 8, "version ") != 0) return -1;
    return atoi(text.c_str() + i + 8);
}

std::shared_ptr<File> parseText(const std::string& text, BrainDiag& diag) {
    auto f = std::make_shared<File>();
    Parser p;
    if (!p.file(text, *f)) { diag.errors.push_back({p.err.line, p.err.col, p.err.msg}); return nullptr; }
    if (f->version != kBrainFormatVersion) { diag.errors.push_back({1, 9, "version " + std::to_string(f->version) + ": this game reads version " + std::to_string(kBrainFormatVersion)}); return nullptr; }
    return f;
}

const File& defaultFile() {
    static const std::shared_ptr<File> f = [] {
        BrainDiag d;
        auto r = parseText(kDefaultBrainText, d);
        if (!r) { fprintf(stderr, "default.qbrain: %d:%d: %s\n", d.errors[0].line, d.errors[0].col, d.errors[0].msg.c_str()); return std::make_shared<File>(); }
        return r;
    }();
    return *f;
}

const char* declKindName(int k) { static const char* n[] = {"param", "curve", "list", "table", "slot"}; return n[k]; }

bool merge(const File& user, Merged& m, BrainDiag& diag) {
    m.file = defaultFile();
    m.file.name = user.name;
    m.file.version = user.version;
    m.file.tail.clear();
    for (int s = 0; s < SLOT_COUNT; s++) m.slotDecl[s] = -1;
    for (size_t i = 0; i < m.file.decls.size(); i++) {
        const Decl& d = m.file.decls[i];
        if (d.kind == Decl::SLOT) m.slotDecl[d.slot] = (int)i; else m.byName[d.name] = i;
    }
    std::set<std::string> userNames;
    std::set<int> userSlots;
    for (const Decl& d : user.decls) {
        if (d.kind == Decl::SLOT) {
            if (userSlots.count(d.slot)) { diag.errors.push_back({d.line, d.col, "slot " + slotName(d.slot) + " twice"}); continue; }
            userSlots.insert(d.slot);
            if (m.slotDecl[d.slot] >= 0) m.file.decls[m.slotDecl[d.slot]] = d;
            else { m.slotDecl[d.slot] = (int)m.file.decls.size(); m.file.decls.push_back(d); }
            continue;
        }
        if (userNames.count(d.name)) { diag.errors.push_back({d.line, d.col, "'" + d.name + "' declared twice"}); continue; }
        userNames.insert(d.name);
        auto it = m.byName.find(d.name);
        if (it != m.byName.end()) {
            Decl& old = m.file.decls[it->second];
            if (old.kind != d.kind) { diag.errors.push_back({d.line, d.col, "'" + d.name + "' is the built-in brain's " + declKindName(old.kind) + ": a " + declKindName(d.kind) + " can't take its name"}); continue; }
            if (d.kind == Decl::PARAM && d.ptype != old.ptype) { diag.errors.push_back({d.line, d.col, "param '" + d.name + "' is a " + typeName(old.ptype) + " (the game reads it as one)"}); continue; }
            old = d;
        } else {
            m.byName[d.name] = m.file.decls.size();
            m.file.decls.push_back(d);
        }
    }
    return diag.errors.empty();
}

bool reservedName(const std::string& n) {
    if (isKeyword(n) || isBuiltin(n) || weaponByName(n) >= 0 || modeByName(n) >= 0 || n == "default" || n == "steps" || n == "points") return true;
    for (int s = 0; s < S_COUNT; s++) if (n == kSensors[s].name) return true;
    for (int s = 0; s < SLOT_OBJECTIVE; s++) for (auto& o : kSlotsBase[s].outs) if (n == o.name) return true;
    for (int k = 0; k < PT_SITE; k++) if (n == kPoints[k].name) return true;
    for (auto* l : kLevelNames) if (n == l) return true;
    return n.find('.') != std::string::npos;
}

// Build a brain from a parsed user file (null: the default text itself).
std::shared_ptr<Brain> build(const File& user, BrainDiag& diag, bool forceInterpreter) {
    Merged m;
    if (!merge(user, m, diag)) return nullptr;
    auto b = std::make_shared<Brain>();
    b->name = user.name;
    for (auto& s : b->start) s = -1;
    Compiler c(*b, diag, m);
    // params: the catalog's first, in its order (P index = catalog index), then the file's own
    for (int i = 0; i < kNumParams; i++) { c.params[kParams[i].name] = i; c.ptype.push_back(kParams[i].type); }
    std::vector<const Decl*> pdecl(kNumParams, nullptr);
    std::vector<const Decl*> listDecl(kNumCatLists, nullptr), tableDecl(kNumCatTables, nullptr);
    std::vector<const Decl*> curveDecl;
    for (const Decl& d : m.file.decls) {
        if (d.kind == Decl::SLOT) continue;
        bool cat = false;
        if (d.kind == Decl::PARAM) for (int i = 0; i < kNumParams; i++) if (d.name == kParams[i].name) { pdecl[i] = &d; cat = true; }
        if (d.kind == Decl::LIST) for (int i = 0; i < kNumCatLists; i++) if (d.name == kListNames[i]) { listDecl[i] = &d; cat = true; }
        if (d.kind == Decl::TABLE) for (int i = 0; i < kNumCatTables; i++) if (d.name == kTableNames[i]) { tableDecl[i] = &d; cat = true; }
        if (!cat && reservedName(d.name)) { diag.errors.push_back({d.line, d.col, "'" + d.name + "' is a reserved name (a keyword, builtin, weapon, mode, sensor or output)"}); continue; }
        switch (d.kind) {
            case Decl::PARAM:
                if (!cat) { c.params[d.name] = (int)c.ptype.size(); c.ptype.push_back(d.ptype); pdecl.push_back(&d); }
                break;
            case Decl::LIST:
                if (!cat) { listDecl.push_back(&d); }
                break;
            case Decl::TABLE:
                if (!cat) { tableDecl.push_back(&d); }
                break;
            case Decl::CURVE:
                if (d.form == 0) c.exprCurves[d.name] = &d;
                else { c.dataCurves[d.name] = (int)curveDecl.size(); curveDecl.push_back(&d); }
                break;
            default: break;
        }
    }
    for (int i = 0; i < kNumParams; i++) if (!pdecl[i]) diag.errors.push_back({1, 1, std::string("internal: the built-in brain has no param ") + kParams[i].name});
    if (!diag.errors.empty()) return nullptr;
    // values per level
    for (int lv = 0; lv < 4; lv++) {
        b->lvl[lv] = kDefaultParams[lv];
        b->P[lv].resize(pdecl.size());
        for (size_t i = 0; i < pdecl.size(); i++) {
            const Decl& d = *pdecl[i];
            double v = d.values.size() == 4 ? d.values[lv] : d.values[0];
            b->P[lv][i] = v;
            if ((int)i < kNumParams) setParam(b->lvl[lv], (int)i, v);
        }
    }
    auto toList = [](const Decl& d) { BrainList l; for (int w : d.items) { l.w[l.n++] = (uint8_t)w; l.mask |= (uint16_t)(1u << w); } return l; };
    for (size_t i = 0; i < listDecl.size(); i++) {
        if (!listDecl[i]) { diag.errors.push_back({1, 1, std::string("internal: the built-in brain has no list ") + kListNames[i]}); continue; }
        c.lists[listDecl[i]->name] = (int)i;
        b->lists.push_back(toList(*listDecl[i]));
        if ((int)i < kNumCatLists) for (auto& L : b->lvl) L.*kListMembers[i] = b->lists.back();
    }
    for (size_t i = 0; i < tableDecl.size(); i++) {
        if (!tableDecl[i]) { diag.errors.push_back({1, 1, std::string("internal: the built-in brain has no table ") + kTableNames[i]}); continue; }
        const Decl& d = *tableDecl[i];
        c.tables[d.name] = (int)i;
        std::vector<float> t(16, d.hasElse ? (float)d.elseV : 0.0f);
        for (auto& e : d.entries) t[e.first] = (float)e.second;
        b->tables.push_back(t);
        if ((int)i < kNumCatTables) for (auto& L : b->lvl) for (int w = 0; w < 16; w++) (L.*kTableMembers[i])[w] = t[w];
    }
    for (const Decl* dp : curveDecl) {
        BrainCurve cv;
        cv.kind = dp->form == 1 ? BrainCurve::STEPS : BrainCurve::POINTS;
        cv.base = (float)dp->base;
        cv.n = (uint8_t)dp->pts.size();
        for (size_t k = 0; k < dp->pts.size(); k++) { cv.x[k] = (float)dp->pts[k].first; cv.y[k] = (float)dp->pts[k].second; }
        b->curves.push_back(cv);
        for (int i = 0; i < kNumCatCurves; i++) if (dp->name == kCurveNames[i]) for (auto& L : b->lvl) L.*kCurveMembers[i] = cv;
    }
    // slots
    const File& def = defaultFile();
    std::map<std::string, const Decl*> defCurves;
    int defSlot[SLOT_COUNT];
    for (auto& s : defSlot) s = -1;
    for (size_t i = 0; i < def.decls.size(); i++) {
        const Decl& d = def.decls[i];
        if (d.kind == Decl::CURVE && d.form == 0) defCurves[d.name] = &d;
        if (d.kind == Decl::SLOT) defSlot[d.slot] = (int)i;
    }
    for (int s = 0; s < SLOT_COUNT; s++) {
        if (m.slotDecl[s] < 0) {
            if (s < SLOT_OBJECTIVE || s == SLOT_OBJECTIVE + MODE_COUNT) diag.errors.push_back({1, 1, "internal: the built-in brain has no slot " + slotName(s)});
            continue;
        }
        c.compileSlot(s, m.file.decls[m.slotDecl[s]]);
    }
    if (!diag.errors.empty()) return nullptr;
    // native: the slot (as the bot code calls it, curves written out) is the default's
    auto resolved = [](const int* slots, int s) { if (s >= SLOT_OBJECTIVE && s < SLOT_OBJECTIVE + MODE_COUNT && slots[s] < 0) return slots[SLOT_OBJECTIVE + MODE_COUNT]; return slots[s]; };
    b->allNative = true;
    for (int s = 0; s < SLOT_COUNT; s++) {
        int mu = resolved(m.slotDecl, s), du = resolved(defSlot, s);
        bool nat = !forceInterpreter && mu >= 0 && du >= 0 && expandedText(m.file.decls[mu], c.exprCurves) == expandedText(def.decls[du], defCurves);
        b->native[s] = nat;
        if (s < SLOT_OBJECTIVE + MODE_COUNT && !nat) b->allNative = false;
    }
    for (int md = 0; md < MODE_COUNT; md++) b->objSlot[md] = (uint8_t)(m.slotDecl[SLOT_OBJECTIVE + md] >= 0 ? SLOT_OBJECTIVE + md : SLOT_OBJECTIVE + MODE_COUNT);
    // canonical text and the hash of what it does (the merged brain, no comments)
    Printer pr;
    b->text = pr.file(user);
    Printer ph;
    ph.comments = false;
    b->hash = sha256Hex(ph.file(m.file)).substr(0, 8);
    return b;
}

} // namespace

// =====================================================================================================
// Interpreter
// =====================================================================================================

void runBrainSlot(const Brain& b, int slot, int level, float dt, BrainCall& c) {
    double r[kMaxRegs];
    const BrainInsn* code = b.code.data();
    const double* P = b.P[level < 0 || level > 3 ? 1 : level].data();
    const double* K = b.konst.data();
    for (int pc = b.start[slot];;) {
        const BrainInsn& i = code[pc++];
        switch (i.op) {
            case OP_LDK: r[i.d] = K[i.k]; break;
            case OP_LDP: r[i.d] = P[i.k]; break;
            case OP_LDS: r[i.d] = c.in[i.k]; break;
            case OP_LAZY: r[i.d] = c.lazy && c.lazy(c.user, i.k) ? 1 : 0; break;
            case OP_MOV: r[i.d] = r[i.a]; break;
            case OP_ADDF: r[i.d] = (float)r[i.a] + (float)r[i.b]; break;
            case OP_SUBF: r[i.d] = (float)r[i.a] - (float)r[i.b]; break;
            case OP_MULF: r[i.d] = (float)r[i.a] * (float)r[i.b]; break;
            case OP_DIVF: r[i.d] = (float)r[i.a] / (float)r[i.b]; break;
            case OP_ADDD: r[i.d] = r[i.a] + r[i.b]; break;
            case OP_SUBD: r[i.d] = r[i.a] - r[i.b]; break;
            case OP_MULD: r[i.d] = r[i.a] * r[i.b]; break;
            case OP_DIVD: r[i.d] = r[i.a] / r[i.b]; break;
            case OP_ADDI: r[i.d] = (double)((int64_t)r[i.a] + (int64_t)r[i.b]); break;
            case OP_SUBI: r[i.d] = (double)((int64_t)r[i.a] - (int64_t)r[i.b]); break;
            case OP_MULI: r[i.d] = (double)((int64_t)r[i.a] * (int64_t)r[i.b]); break;
            case OP_LT: r[i.d] = r[i.a] < r[i.b]; break;
            case OP_LE: r[i.d] = r[i.a] <= r[i.b]; break;
            case OP_GT: r[i.d] = r[i.a] > r[i.b]; break;
            case OP_GE: r[i.d] = r[i.a] >= r[i.b]; break;
            case OP_EQ: r[i.d] = r[i.a] == r[i.b]; break;
            case OP_NE: r[i.d] = r[i.a] != r[i.b]; break;
            case OP_NOT: r[i.d] = r[i.a] == 0; break;
            case OP_NEG: r[i.d] = -r[i.a]; break;
            case OP_TOF: r[i.d] = (float)r[i.a]; break;
            case OP_ABS: r[i.d] = std::fabs(r[i.a]); break;
            case OP_SQRTF: r[i.d] = std::sqrt((float)r[i.a]); break;
            case OP_SQRTD: r[i.d] = std::sqrt(r[i.a]); break;
            case OP_EXPF: r[i.d] = std::exp((float)r[i.a]); break;
            case OP_EXPD: r[i.d] = std::exp(r[i.a]); break;
            case OP_MIN: r[i.d] = r[i.b] < r[i.a] ? r[i.b] : r[i.a]; break;
            case OP_MAX: r[i.d] = r[i.a] < r[i.b] ? r[i.b] : r[i.a]; break;
            case OP_CLAMP: { double v = r[i.a], lo = r[i.b], hi = r[i.k]; r[i.d] = v >= lo ? (v > hi ? hi : v) : lo; break; }
            case OP_ROLL: r[i.d] = brainRnd(*c.rng); break;
            case OP_GAUSS: r[i.d] = brainGauss(*c.rng); break;
            case OP_CHANCE: {
                float rate = (float)r[i.a];
                bool hit = false;
                if (rate > 0) {
                    float p = i.k >= 0 && c.chanceP ? c.chanceP[i.k] : 1.0f - std::exp(-rate * dt);
                    hit = brainRnd(*c.rng) < p;
                }
                r[i.d] = hit;
                break;
            }
            case OP_ALLOWED: { int w = (int)r[i.a]; r[i.d] = w >= 0 && w < 16 && (!c.weaponMask || (c.weaponMask >> w & 1)); break; }
            case OP_FIRST: {
                const BrainList& l = b.lists[(size_t)r[i.a]];
                double w = r[i.b];
                for (int k = 0; k < l.n; k++) if (!c.weaponMask || (c.weaponMask >> l.w[k] & 1)) { w = l.w[k]; break; }
                r[i.d] = w;
                break;
            }
            case OP_IN: { int w = (int)r[i.a]; r[i.d] = w >= 0 && w < 16 && (b.lists[(size_t)r[i.b]].mask >> w & 1); break; }
            case OP_TABLE: { int w = (int)r[i.a]; const auto& t = b.tables[i.k]; r[i.d] = w >= 0 && w < 16 ? t[w] : t[15]; break; }
            case OP_CURVE: r[i.d] = evalBrainCurve(b.curves[i.k], (float)r[i.a]); break;
            case OP_JMP: pc = i.k; break;
            case OP_JZ: if (r[i.a] == 0) pc = i.k; break;
            case OP_JNZ: if (r[i.a] != 0) pc = i.k; break;
            case OP_RET: {
                int n = (int)slotInfo(slot).outs.size();
                for (int k = 0; k < n; k++) c.out[k] = r[k];
                return;
            }
        }
    }
}

// =====================================================================================================
// API
// =====================================================================================================

const BrainParams& defaultBrainParams(int level) { return kDefaultParams[level < 0 || level > 3 ? 1 : level]; }

std::string upgradeBrainText(const std::string& text, std::string* err, int* fromVersion) {
    int v = textVersion(text);
    if (fromVersion) *fromVersion = v;
    if (v < 1 || v > kBrainFormatVersion) { if (err) *err = v < 1 ? "the file starts with 'version N'" : "version " + std::to_string(v) + " is newer than this game reads (" + std::to_string(kBrainFormatVersion) + ")"; return ""; }
    std::string t = text;
    for (int k = v; k < kBrainFormatVersion; k++) t = kBrainUpgrades[k - 1](t);
    return t;
}

std::shared_ptr<const Brain> compileBrain(const std::string& text, BrainDiag* diagOut, bool forceInterpreter) {
    BrainDiag local;
    BrainDiag& diag = diagOut ? *diagOut : local;
    std::string err;
    int from = 0;
    std::string t = upgradeBrainText(text, &err, &from);
    if (t.empty()) { diag.errors.push_back({1, 1, err}); return nullptr; }
    auto f = parseText(t, diag);
    if (!f) return nullptr;
    return build(*f, diag, forceInterpreter);
}

const Brain& defaultBrain() {
    static const std::shared_ptr<const Brain> b = [] {
        BrainDiag d;
        auto r = compileBrain(kDefaultBrainText, &d);
        std::shared_ptr<Brain> out = r ? std::make_shared<Brain>(*r) : std::make_shared<Brain>();
        if (!r) { for (auto& s : out->start) s = -1; fprintf(stderr, "default.qbrain doesn't compile: %s\n", d.errors.empty() ? "?" : d.errors[0].msg.c_str()); }
        for (int lv = 0; lv < 4; lv++) out->lvl[lv] = kDefaultParams[lv];   // (the C++ table: the native bot exactly)
        for (auto& n : out->native) n = true;
        out->allNative = true;
        out->builtIn = true;
        out->name = "default";
        return out;
    }();
    return *b;
}

std::string brainLabel(const Brain& b) { return b.name + "#" + b.hash; }

std::shared_ptr<const Brain> loadBrain(const std::string& name, std::string* err) {
    if (name.empty() || name == "default") return nullptr;
    for (char ch : name)
        if (!(isalnum((unsigned char)ch) || ch == '_' || ch == '-')) { if (err) *err = "a brain name is letters, digits, - and _"; return nullptr; }
    std::string path = userDataPath("bots") + "/" + name + ".qbrain";
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "no brain " + path; return nullptr; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    static std::mutex mu;
    static std::map<std::string, std::pair<std::string, std::shared_ptr<const Brain>>> cache;   // name -> (text, brain)
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache.find(name);
    if (it != cache.end() && it->second.first == text) return it->second.second;
    int from = 0;
    std::string uerr;
    std::string up = upgradeBrainText(text, &uerr, &from);
    if (!up.empty() && from >= 1 && from < kBrainFormatVersion) {   // an older format: converted on disk, the original kept
        std::error_code ec;
        std::string oldDir = userDataPath("bots") + "/old/v" + std::to_string(from);
        std::filesystem::create_directories(oldDir, ec);
        std::filesystem::copy_file(path, oldDir + "/" + name + ".qbrain", std::filesystem::copy_options::overwrite_existing, ec);
        std::ofstream o(path, std::ios::binary | std::ios::trunc);
        o << up;
    }
    BrainDiag d;
    auto b = compileBrain(text, &d);
    if (!b) {
        if (err) *err = path + ":" + std::to_string(d.errors[0].line) + ":" + std::to_string(d.errors[0].col) + ": " + d.errors[0].msg;
        return nullptr;
    }
    cache[name] = {text, b};
    return b;
}

std::vector<std::string> listBrains() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(utf8Path(userDataPath("bots")), ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".qbrain") continue;
        std::string n = e.path().stem().u8string();
        bool ok = !n.empty() && n != "default";   // ("default" is the built-in brain's name: loadBrain never reads that file)
        for (char ch : n) ok = ok && (isalnum((unsigned char)ch) || ch == '_' || ch == '-');
        if (ok) out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string brainDiagJson(const BrainDiag& d, const Brain* b) {
    auto msgs = [](const std::vector<BrainDiag::Msg>& v) {
        std::string o = "[";
        for (size_t i = 0; i < v.size(); i++)
            o += std::string(i ? "," : "") + "{\"line\":" + std::to_string(v[i].line) + ",\"col\":" + std::to_string(v[i].col) + ",\"msg\":" + jstr(v[i].msg) + "}";
        return o + "]";
    };
    std::string o = std::string("{\"ok\":") + (d.errors.empty() && b ? "true" : "false") + ",\"errors\":" + msgs(d.errors) + ",\"warnings\":" + msgs(d.warnings);
    if (b) {
        o += ",\"name\":" + jstr(b->name) + ",\"hash\":" + jstr(b->hash) + ",\"label\":" + jstr(brainLabel(*b)) + ",\"slots\":[";
        for (int s = 0; s < SLOT_COUNT; s++) {
            if (!b->usesSlot(s)) continue;
            int end = (int)b->code.size();
            for (int t = 0; t < SLOT_COUNT; t++) if (b->start[t] > b->start[s] && b->start[t] < end) end = b->start[t];
            o += std::string(o.back() == '[' ? "" : ",") + "{\"slot\":" + jstr(slotName(s)) + ",\"native\":" + (b->native[s] ? "true" : "false") +
                 ",\"instructions\":" + std::to_string(end - b->start[s]) +
                 ",\"registers\":" + std::to_string(b->regs[s]) + ",\"rolls\":" + std::to_string(b->rolls[s]) + "}";
        }
        o += "]";
    }
    return o + "}";
}

std::string brainAstJson(const std::string& text, BrainDiag* diagOut) {
    BrainDiag local;
    BrainDiag& diag = diagOut ? *diagOut : local;
    auto f = parseText(text, diag);
    if (!f) return "";
    JsonAst j;
    return j.file(*f);
}

std::string printBrainText(const std::string& text, BrainDiag* diagOut) {
    BrainDiag local;
    BrainDiag& diag = diagOut ? *diagOut : local;
    auto f = parseText(text, diag);
    if (!f) return "";
    Printer p;
    return p.file(*f);
}

std::string exportBrainJson() {
    const File& f = defaultFile();
    const Brain& b = defaultBrain();
    std::string o = "{\"format\":\"qbrain-export\",\"version\":" + std::to_string(kBrainFormatVersion) + ",\"hash\":" + jstr(b.hash) + ",\n\"text\":" + jstr(kDefaultBrainText) + ",\n\"ast\":";
    JsonAst j;
    std::set<std::string> paramNames, listNames, tableNames, curveNames;
    for (auto& d : f.decls) {
        if (d.kind == Decl::PARAM) paramNames.insert(d.name);
        if (d.kind == Decl::LIST) listNames.insert(d.name);
        if (d.kind == Decl::TABLE) tableNames.insert(d.name);
        if (d.kind == Decl::CURVE) curveNames.insert(d.name);
    }
    j.refOf = [&](const std::string& n) -> std::string {
        for (int s = 0; s < S_COUNT; s++) if (n == kSensors[s].name) return "sensor";
        if (paramNames.count(n)) return "param";
        if (n == "none" || weaponByName(n) >= 0) return "weapon";
        if (modeByName(n) >= 0) return "mode";
        if (listNames.count(n)) return "list";
        for (int k = 0; k < PT_SITE; k++) if (n == kPoints[k].name) return "point";
        for (int s = 0; s < SLOT_OBJECTIVE; s++) for (auto& out : kSlotsBase[s].outs) if (n == out.name) return "output";
        return "local";
    };
    o += j.file(f);
    o += ",\n\"catalog\":{\"levels\":[\"BEGINNER\",\"INTERMEDIATE\",\"ADVANCED\",\"EXPERT\"],\"types\":[\"bool\",\"int\",\"f32\",\"f64\",\"weapon\",\"mode\",\"list\",\"point\"],\n\"params\":[";
    for (int i = 0; i < kNumParams; i++) {
        const ParamInfo& p = kParams[i];
        o += std::string(i ? ",\n" : "\n") + "{\"name\":" + jstr(p.name) + ",\"type\":\"" + typeName(p.type) + "\",\"unit\":" + jstr(p.unit) + ",\"min\":" + fmtNumber(p.lo, T_F64) +
             ",\"max\":" + fmtNumber(p.hi, T_F64) + ",\"doc\":" + jstr(p.doc) + ",\"default\":[";
        for (int lv = 0; lv < 4; lv++) {
            double v = getParam(kDefaultParams[lv], i);
            o += std::string(lv ? "," : "") + (p.type == T_BOOL ? std::string(v ? "true" : "false") : fmtNumber(v, p.type));
        }
        o += "]}";
    }
    o += "],\n\"lists\":[";
    for (int i = 0; i < kNumCatLists; i++) o += std::string(i ? "," : "") + jstr(kListNames[i]);
    o += "],\"tables\":[";
    for (int i = 0; i < kNumCatTables; i++) o += std::string(i ? "," : "") + jstr(kTableNames[i]);
    o += "],\"dataCurves\":[";
    for (int i = 0; i < kNumCatCurves; i++) o += std::string(i ? "," : "") + jstr(kCurveNames[i]);
    o += "],\n\"sensors\":[";
    for (int s = 0; s < S_COUNT; s++)
        o += std::string(s ? ",\n" : "\n") + "{\"name\":" + jstr(kSensors[s].name) + ",\"type\":\"" + typeName(kSensors[s].type) + "\",\"unit\":" + jstr(kSensors[s].unit) +
             ",\"doc\":" + jstr(kSensors[s].doc) + ",\"common\":" + (s < kCommonSensors ? "true" : "false") + (kSensors[s].lazy ? ",\"lazy\":true" : "") + "}";
    o += "],\n\"slots\":[";
    for (int s = 0; s <= SLOT_OBJECTIVE; s++) {
        const SlotInfo& si = slotInfo(s);
        o += std::string(s ? ",\n" : "\n") + "{\"name\":" + jstr(si.name) + ",\"doc\":" + jstr(si.doc) + ",\"when\":" + jstr(si.when) + ",\"sensors\":[";
        for (size_t k = 0; k < si.sensors.size(); k++) o += std::string(k ? "," : "") + jstr(kSensors[si.sensors[k]].name);
        o += "],\"outputs\":[";
        if (s < SLOT_OBJECTIVE)
            for (size_t k = 0; k < si.outs.size(); k++)
                o += std::string(k ? "," : "") + "{\"name\":" + jstr(si.outs[k].name) + ",\"type\":\"" + typeName(si.outs[k].type) + "\",\"doc\":" + jstr(si.outs[k].doc) + "}";
        o += "]";
        if (s == SLOT_OBJECTIVE) {
            o += ",\"modes\":[";
            for (int m = 0; m < MODE_COUNT; m++) o += std::string(m ? "," : "") + jstr(kModeNames[m]);
            o += ",\"default\"],\"actions\":[\"goto\",\"fallback\",\"seek\",\"evade\"]";
        }
        o += ",\"rolls\":" + std::to_string(b.rolls[s < SLOT_OBJECTIVE ? s : SLOT_OBJECTIVE + MODE_COUNT]) + "}";
    }
    o += "],\n\"points\":[";
    for (int k = 0; k < PT_COUNT; k++) o += std::string(k ? "," : "") + "{\"name\":" + jstr(k == PT_SITE ? "site(i)" : kPoints[k].name) + ",\"doc\":" + jstr(kPoints[k].doc) + "}";
    o += "],\n\"builtins\":[";
    for (size_t k = 0; k < sizeof kBuiltins / sizeof kBuiltins[0]; k++)
        o += std::string(k ? ",\n" : "\n") + "{\"name\":" + jstr(kBuiltins[k].name) + ",\"sig\":" + jstr(kBuiltins[k].sig) + ",\"doc\":" + jstr(kBuiltins[k].doc) + ",\"draws\":" + std::to_string(kBuiltins[k].draws) + "}";
    o += "],\n\"weapons\":[";
    for (int w = 0; w < WPN_COUNT; w++) o += std::string(w ? "," : "") + jstr(kWeaponNames[w]);
    o += ",\"none\"],\"modes\":[";
    for (int m = 0; m < MODE_COUNT; m++) o += std::string(m ? "," : "") + jstr(kModeNames[m]);
    o += "],\"keywords\":[";
    for (size_t k = 0; k < sizeof kKeywords / sizeof kKeywords[0]; k++) o += std::string(k ? "," : "") + jstr(kKeywords[k]);
    o += "]}}\n";
    return o;
}

// ---- for tests (tests/brain_tests.cpp)
bool brainParamsEqual(const BrainParams& a, const BrainParams& b, std::string* which) {
    for (int i = 0; i < kNumParams; i++) {
        double x = getParam(a, i), y = getParam(b, i);
        if (memcmp(&x, &y, sizeof x) != 0) { if (which) *which = kParams[i].name; return false; }
    }
    for (int i = 0; i < kNumCatLists; i++) {
        const BrainList &x = a.*kListMembers[i], &y = b.*kListMembers[i];
        if (x.n != y.n || x.mask != y.mask || memcmp(x.w, y.w, x.n) != 0) { if (which) *which = kListNames[i]; return false; }
    }
    for (int i = 0; i < kNumCatTables; i++) if (memcmp(a.*kTableMembers[i], b.*kTableMembers[i], sizeof(float) * 16) != 0) { if (which) *which = kTableNames[i]; return false; }
    for (int i = 0; i < kNumCatCurves; i++) {
        const BrainCurve &x = a.*kCurveMembers[i], &y = b.*kCurveMembers[i];
        if (x.kind != y.kind || x.n != y.n || memcmp(&x.base, &y.base, 4) || memcmp(x.x, y.x, 4 * x.n) || memcmp(x.y, y.y, 4 * x.n)) { if (which) *which = kCurveNames[i]; return false; }
    }
    return true;
}
std::string brainFmtNumberForTests(double v, bool f32) { return fmtNumber(v, f32 ? T_F32 : T_F64); }
