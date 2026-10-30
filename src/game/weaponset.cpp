// Weapon sets: fields, the .qweapons parser / printer / JSON AST, compiler and interpreter (weaponset.h,
// tools/maptool/weapon/DESIGN.md). The grammar is the bot brain grammar (tools/bots/GRAMMAR.md) with a `weapons`
// header, one value per param, `label` declarations and `hit` / `kill` slots; the lexer, parser and printer follow
// botbrain.cpp's so the editors' code works on both.
#include "weaponset.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include "../common/u8file.h"
#include "customrules.h"
#include "map.h"

static_assert(WPN_COUNT == 11, "the weapon names below");

namespace {
const char* const kWeaponKeys[WPN_COUNT] = {"rifle", "rocket", "knife", "sniper", "laser", "shotgun", "sticky", "crossbow", "bubble", "syringe", "locator"};

float evalCurve(int kind, int n, float base, const float* xs, const float* ys, float x) {
    if (kind == 1) {   // steps
        float v = base;
        for (int k = 0; k < n; k++) if (x > xs[k]) v = ys[k];
        return v;
    }
    if (n == 0) return 1.0f;
    if (!(x > xs[0])) return ys[0];
    for (int k = 0; k + 1 < n; k++)
        if (x <= xs[k + 1]) return ys[k] + (ys[k + 1] - ys[k]) * ((x - xs[k]) / (xs[k + 1] - xs[k]));
    return ys[n - 1];
}

// =====================================================================================================
// The standard values (today's game)
// =====================================================================================================

void finishValues(WeaponValues& v) {
    for (auto& e : v.ex) e.falloff = e.curveKind != 0 || e.falloffMin != 1.0f;
}

WeaponValues makeStandard() {
    WeaponValues v;
    memset((void*)&v, 0, sizeof v);
    for (int w = 0; w < WPN_COUNT; w++) {
        v.def[w] = baseWeaponDef(w);
        WeaponExtra& e = v.ex[w];
        e.headDamage = 0; e.knock = 1;
        e.falloffStart = 0; e.falloffEnd = 0; e.falloffMin = 1;
        e.directDamage = 0; e.selfDamage = 1; e.wallbang = kWallbangDamage;
    }
    v.ex[WPN_SNIPER].headDamage = kSniperHeadDamage;
    v.ex[WPN_CROSSBOW].headDamage = kCrossbowHeadDamage;
    v.ex[WPN_RIFLE].headDamage = kRifleHeadDamage;
    v.ex[WPN_SHOTGUN].headDamage = kPelletHeadDamage;
    v.ex[WPN_SHOTGUN].knock = kShotgunKnock;
    v.ex[WPN_LASER].knock = kLaserPush;
    v.ex[WPN_CROSSBOW].knock = kCrossbowKnock;
    v.ex[WPN_SNIPER].knock = kSniperKnock;
    v.ex[WPN_ROCKET].knock = v.ex[WPN_STICKY].knock = v.ex[WPN_BUBBLE].knock = 11.0f;   // a blast's centre (Server::explode)
    v.ex[WPN_ROCKET].directDamage = kRocketDirectDamage;
    WeaponAlt& a = v.alt;
    a.scopedRefire = kRifleScopedRefire;
    a.lobSpeed = kRocketLobSpeed; a.lobGravity = kRocketLobGravity; a.rocketAccel = kRocketAccel;
    a.knifeThrowSpeed = kKnifeThrowSpeed; a.knifeThrowGravity = kKnifeThrowGravity; a.knifeThrowLife = kKnifeThrowLife;
    a.knifeAway = kKnifeAway; a.knifeGrow = kKnifeGrow;
    a.bounces = kShotgunBounces; a.bounceDamage = kShotgunBounceDamage;
    a.pullScale = kLaserPullScale; a.pullDamage = kLaserPullDamage;
    a.streamMin = kBubbleStreamMin; a.streamMax = kBubbleStreamMax; a.streamNear = kBubbleStreamNear; a.streamFar = kBubbleStreamFar;
    a.streamRefire = kBubbleStreamRefire; a.streamSlow = kBubbleStreamSlow; a.streamFast = kBubbleStreamFast;
    a.syringeThrowSpeed = kSyringeThrowSpeed; a.syringeThrowGravity = kSyringeThrowGravity; a.syringeThrowLife = kSyringeThrowLife;
    a.syringeHeal = kSyringeHeal; a.syringeAway = kSyringeAway;
    a.stickyMax = kMaxStickies; a.stickyArmTime = kStickyArmTime; a.stickyInherit = kProjInherit;
    finishValues(v);
    return v;
}

// =====================================================================================================
// Fields
// =====================================================================================================

enum Store : uint8_t { ST_DEF, ST_EX, ST_ALT };
struct FieldSpec { const char* field; uint8_t type; const char* unit; double lo, hi; const char* doc; uint8_t store; size_t off; bool readOnly; };
#define DEF_F(f, ty, unit, lo, hi, doc) {#f, ty, unit, lo, hi, doc, ST_DEF, offsetof(WeaponDef, f), false}
#define DEF_RO(f, doc) {#f, WF_BOOL, "", 0, 1, doc, ST_DEF, offsetof(WeaponDef, f), true}
#define EX_F(f, unit, lo, hi, doc) {#f, WF_F32, unit, lo, hi, doc, ST_EX, offsetof(WeaponExtra, f), false}
const FieldSpec kCommon[] = {
    DEF_F(refire, WF_F32, "s", 0.001, 10, "seconds between shots (the laser: between damage ticks)"),
    DEF_F(damage, WF_F32, "hp", 0, 1000, "damage per hit / per pellet / at the centre of a blast (the syringe: hp it heals you)"),
    DEF_F(range, WF_F32, "m", 0, 1000, "hitscan range (the knife: its reach)"),
    DEF_F(projSpeed, WF_F32, "m/s", 0, 500, "projectile speed"),
    DEF_F(splashRadius, WF_F32, "m", 0, 20, "blast radius (0 = direct hits only)"),
    DEF_F(spread, WF_F32, "rad", 0, 0.5, "hip-fire spread (a fixed pattern)"),
    DEF_F(scopedSpread, WF_F32, "rad", -1, 0.5, "spread while scoped (alt-fire held); below 0 = no scope"),
    DEF_F(zoomFov, WF_F32, "deg", 0, 120, "horizontal field of view while scoped (0: the weapon has no scope)"),
    DEF_F(pellets, WF_INT, "", 1, 64, "rays per shot (the shotgun's pellets)"),
    DEF_F(gravity, WF_F32, "m/s2", -50, 100, "projectile drop (negative: floats up)"),
    DEF_F(life, WF_F32, "s", 0, 60, "projectile lifetime"),
    DEF_F(semiAuto, WF_BOOL, "", 0, 1, "one shot per click"),
    DEF_RO(projectile, "fires projectiles (the weapon's kind: not editable)"),
    DEF_RO(beam, "a continuous beam (the weapon's kind: not editable)"),
    DEF_RO(sticky, "projectiles stick and are set off with alt-fire (the weapon's kind: not editable)"),
    EX_F(headDamage, "hp", 0, 1000, "damage of a hit on the head (0 = no headshots)"),
    EX_F(knock, "", 0, 100, "knockback: hitscan x1 (pellets: per pellet's worth of damage, m/s), beam m/s per second, bolt m/s, the knife throw 3x, a blast's centre m/s (the bubble: 0.35x)"),
    EX_F(falloffStart, "m", 0, 1000, "damage falloff: full damage up to here"),
    EX_F(falloffEnd, "m", 0, 1000, "damage falloff: down to falloffMin from here on"),
    EX_F(falloffMin, "x", 0, 10, "damage falloff: x damage at falloffEnd and beyond (1 = no falloff)"),
    EX_F(directDamage, "hp", 0, 1000, "a projectile that hits the player itself does at least this (splash weapons)"),
    EX_F(selfDamage, "x", 0, 10, "x your own splash damage (blasts at or below your feet do 10% of it, as always)"),
    EX_F(wallbang, "x", 0, 1, "x damage through a wallbang wall (hitscan)"),
};
#define ALT_F(w, f, mem, ty, unit, lo, hi, doc) {w, {f, ty, unit, lo, hi, doc, ST_ALT, offsetof(WeaponAlt, mem), false}}
struct AltSpec { int weapon; FieldSpec f; };
const AltSpec kAlts[] = {
    ALT_F(WPN_RIFLE, "scopedRefire", scopedRefire, WF_F32, "x", 0.1, 10, "x refire while scoped"),
    ALT_F(WPN_ROCKET, "lobSpeed", lobSpeed, WF_F32, "x", 0.05, 5, "the lob (alt-fire): x the rocket's speed"),
    ALT_F(WPN_ROCKET, "lobGravity", lobGravity, WF_F32, "m/s2", 0, 100, "the lob's drop"),
    ALT_F(WPN_ROCKET, "accel", rocketAccel, WF_F32, "m/s2", 0, 100, "the primary rocket speeds up this much a second along its flight (no cap; the lob doesn't)"),
    ALT_F(WPN_KNIFE, "throwSpeed", knifeThrowSpeed, WF_F32, "m/s", 1, 200, "the thrown knife's speed"),
    ALT_F(WPN_KNIFE, "throwGravity", knifeThrowGravity, WF_F32, "m/s2", -50, 100, "the thrown knife's drop"),
    ALT_F(WPN_KNIFE, "throwLife", knifeThrowLife, WF_F32, "s", 0.05, 30, "the thrown knife's lifetime"),
    ALT_F(WPN_KNIFE, "away", knifeAway, WF_F32, "s", 0, 60, "a thrown knife is away (can't be drawn) this long"),
    ALT_F(WPN_KNIFE, "grow", knifeGrow, WF_F32, "m", 0, 1, "a swing hits boxes this much wider on each side"),
    ALT_F(WPN_SHOTGUN, "bounces", bounces, WF_INT, "", 0, 8, "ricochet pellets (alt-fire): walls they bounce off"),
    ALT_F(WPN_SHOTGUN, "bounceDamage", bounceDamage, WF_F32, "x", 0, 10, "ricochet pellets: x damage (no headshots)"),
    ALT_F(WPN_LASER, "pullScale", pullScale, WF_F32, "x", 0, 10, "the pull beam (alt-fire) pulls this much harder than the beam pushes"),
    ALT_F(WPN_LASER, "pullDamage", pullDamage, WF_F32, "x", 0, 10, "the pull beam: x damage (no headshots)"),
    ALT_F(WPN_BUBBLE, "streamMin", streamMin, WF_INT, "", 1, 64, "the stream (alt-fire): bubbles with the nearest enemy within streamNear"),
    ALT_F(WPN_BUBBLE, "streamMax", streamMax, WF_INT, "", 1, 64, "the stream: bubbles with the nearest enemy streamFar away or more"),
    ALT_F(WPN_BUBBLE, "streamNear", streamNear, WF_F32, "m", 0, 200, "the stream: streamMin bubbles within this"),
    ALT_F(WPN_BUBBLE, "streamFar", streamFar, WF_F32, "m", 0, 200, "the stream: streamMax bubbles from this far"),
    ALT_F(WPN_BUBBLE, "streamRefire", streamRefire, WF_F32, "s", 0.05, 10, "the stream's reload"),
    ALT_F(WPN_BUBBLE, "streamSlow", streamSlow, WF_F32, "m/s", 0.1, 200, "the stream's slowest bubble (at the standard projSpeed 15.6; scales with projSpeed)"),
    ALT_F(WPN_BUBBLE, "streamFast", streamFast, WF_F32, "m/s", 0.1, 200, "the stream's fastest bubble (scales with projSpeed likewise)"),
    ALT_F(WPN_SYRINGE, "throwSpeed", syringeThrowSpeed, WF_F32, "m/s", 1, 200, "the thrown syringe's speed"),
    ALT_F(WPN_SYRINGE, "throwGravity", syringeThrowGravity, WF_F32, "m/s2", -50, 100, "the thrown syringe's drop"),
    ALT_F(WPN_SYRINGE, "throwLife", syringeThrowLife, WF_F32, "s", 0.05, 30, "the thrown syringe's lifetime"),
    ALT_F(WPN_SYRINGE, "throwHeal", syringeHeal, WF_F32, "hp", 0, 1000, "a teammate the thrown syringe hits gets this (and every cooldown reset)"),
    ALT_F(WPN_SYRINGE, "away", syringeAway, WF_F32, "s", 0, 60, "a thrown syringe is away this long"),
    ALT_F(WPN_STICKY, "max", stickyMax, WF_INT, "", 1, 64, "stickies out at once per player (one more: the oldest fizzles)"),
    ALT_F(WPN_STICKY, "armTime", stickyArmTime, WF_F32, "s", 0, 10, "a sticky can't be set off before this"),
    ALT_F(WPN_STICKY, "inherit", stickyInherit, WF_F32, "x", 0, 2, "x the shooter's forward / back and vertical velocity a sticky keeps"),
};

const std::vector<WeaponFieldInfo>& fields() {
    static const std::vector<WeaponFieldInfo> f = [] {
        std::vector<WeaponFieldInfo> out;
        for (int w = 0; w < WPN_COUNT; w++) {
            auto add = [&](const FieldSpec& s, bool alt) {
                WeaponFieldInfo i;
                i.weapon = w; i.field = s.field; i.key = std::string(kWeaponKeys[w]) + "." + s.field;
                i.type = s.type; i.unit = s.unit; i.lo = s.lo; i.hi = s.hi; i.doc = s.doc; i.alt = alt; i.readOnly = s.readOnly;
                i.store = s.store; i.off = s.off;
                out.push_back(i);
            };
            for (const FieldSpec& s : kCommon) add(s, false);
            for (const AltSpec& a : kAlts) if (a.weapon == w) add(a.f, true);
        }
        return out;
    }();
    return f;
}

void* fieldPtr(WeaponValues& v, const WeaponFieldInfo& f) {
    char* base = f.store == ST_DEF ? (char*)&v.def[f.weapon] : f.store == ST_EX ? (char*)&v.ex[f.weapon] : (char*)&v.alt;
    return base + f.off;
}

// =====================================================================================================
// Numbers: canonical text (botbrain.cpp's: the shortest decimal that reads back as the same value of its type,
// laid out like JavaScript's Number.prototype.toString)
// =====================================================================================================

enum Type : uint8_t { T_BOOL, T_INT, T_F32, T_F64, T_WEAPON, T_NONE };
const char* typeName(int t) {
    static const char* n[] = {"bool", "int", "f32", "f64", "weapon", "nothing"};
    return t >= 0 && t <= T_NONE ? n[t] : "?";
}
bool isNumeric(int t) { return t == T_INT || t == T_F32 || t == T_F64; }
int fieldType(uint8_t wf) { return wf == WF_F32 ? T_F32 : wf == WF_INT ? T_INT : T_BOOL; }

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
std::string fmtLiteral(double v, int type) {
    std::string s = fmtNumber(v, type);
    if (type == T_INT) return s;
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    if (type == T_F32) s += "f";
    return s;
}
std::string fmtValue(double v, int type) { return type == T_BOOL ? (v != 0 ? "yes" : "no") : fmtNumber(v, type); }

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
std::string jnum(double v) { return std::isfinite(v) ? fmtNumber(v, T_F64) : "null"; }

// =====================================================================================================
// Catalog: sensors, outputs, builtins, keywords
// =====================================================================================================

struct SensorInfo { const char* name; uint8_t type; const char* unit; const char* doc; bool hit, kill; };
const SensorInfo kSensors[WSS_COUNT] = {
    {"weapon", T_WEAPON, "", "the weapon that hit", true, true},
    {"alt", T_BOOL, "", "alt-fire (scope, lob, throw, ricochet, pull, stream)", true, true},
    {"dist", T_F32, "m", "the shooter's eye to the hit", true, true},
    {"head", T_BOOL, "", "a headshot", true, true},
    {"direct", T_BOOL, "", "a projectile that hit the player itself (not its blast)", true, false},
    {"splash", T_F32, "", "the blast's fraction (1 at the centre; 1 for a hit that isn't a blast)", true, false},
    {"self", T_BOOL, "", "the shooter hit themselves (their own blast)", true, false},
    {"airborne", T_BOOL, "", "the victim isn't on the ground", true, true},
    {"victimHealth", T_F32, "hp", "the victim's health before this hit", true, false},
    {"shooterHealth", T_F32, "hp", "the shooter's health", true, true},
    {"victimSpeed", T_F32, "m/s", "the victim's speed", true, true},
    {"shooterSpeed", T_F32, "m/s", "the shooter's speed", true, true},
    {"time", T_F64, "s", "seconds since the round started", true, true},
    {"streak", T_INT, "", "this shooter's hits on this victim less than 1 s apart, this one included", true, false},
    {"kills", T_INT, "", "the shooter's kills this life, this one included", false, true},
};
struct OutInfo { const char* name; uint8_t type; const char* doc; const char* def; };
const OutInfo kOutputs[WSO_COUNT] = {
    {"damage", T_F32, "the damage this hit does (starts as what the game worked out: after falloff, head, wallbang, splash)", "the game's"},
    {"knock", T_F32, "x the knockback", "1"},
    {"heal", T_F32, "hp the shooter gets back (up to their max health; only when the hit hurt)", "0"},
    {"refund", T_BOOL, "the shooter's weapon cooldown resets", "no"},
};
bool slotHasSensor(int slot, int s) { return slot == WS_HIT ? kSensors[s].hit : kSensors[s].kill; }
bool slotHasOutput(int slot, int o) { return slot == WS_HIT ? o <= WSO_HEAL : (o == WSO_HEAL || o == WSO_REFUND); }
const char* const kSlotNames[WS_SLOTS] = {"hit", "kill"};
const char* const kSlotDocs[WS_SLOTS] = {"each time a weapon hits a player (once per shot per player: pellets add up; each blast)",
                                         "each time a weapon's hit kills a player"};

enum Builtin : uint8_t { BI_MIN, BI_MAX, BI_ABS, BI_CLAMP, BI_SQRT, BI_FLOOR, BI_CEIL, BI_POW, BI_LERP, BI_F32, BI_F64, BI_INT, BI_COUNT };
struct BuiltinInfo { const char* name; int args; const char* sig; const char* doc; };
const BuiltinInfo kBuiltins[BI_COUNT] = {
    {"min", 2, "min(a, b)", "the smaller (common type)"},
    {"max", 2, "max(a, b)", "the larger (common type)"},
    {"abs", 1, "abs(x)", "absolute value (type of x)"},
    {"clamp", 3, "clamp(x, lo, hi)", "x >= lo ? (x > hi ? hi : x) : lo (common type)"},
    {"sqrt", 1, "sqrt(x)", "square root (f32 or f64; an int gives f64)"},
    {"floor", 1, "floor(x)", "round down (f32 or f64; an int gives f64)"},
    {"ceil", 1, "ceil(x)", "round up (f32 or f64; an int gives f64)"},
    {"pow", 2, "pow(a, b)", "a to the power b (common type; ints give f64)"},
    {"lerp", 3, "lerp(a, b, t)", "a + (b - a) * t (common type; ints give f64)"},
    {"f32", 1, "f32(x)", "to f32"},
    {"f64", 1, "f64(x)", "to f64"},
    {"int", 1, "int(x)", "to int (toward zero)"},
};
int builtinByName(const std::string& n) { for (int i = 0; i < BI_COUNT; i++) if (n == kBuiltins[i].name) return i; return -1; }

const char* const kKeywords[] = {"version", "weapons", "label", "param", "curve", "slot", "if", "elif", "else", "return", "pass",
                                 "and", "or", "not", "yes", "no", "true", "false"};
bool isKeyword(const std::string& n) { for (auto* k : kKeywords) if (n == k) return true; return false; }
int sensorByName(const std::string& n) { for (int i = 0; i < WSS_COUNT; i++) if (n == kSensors[i].name) return i; return -1; }
int outputByName(const std::string& n) { for (int i = 0; i < WSO_COUNT; i++) if (n == kOutputs[i].name) return i; return -1; }

// =====================================================================================================
// AST
// =====================================================================================================

enum NodeOp : uint8_t { N_NUM, N_BOOL, N_NAME, N_BIN, N_NEG, N_NOT, N_COND, N_CALL, N_SET, N_IF, N_RETURN, N_PASS };
enum BinOp : uint8_t { B_ADD, B_SUB, B_MUL, B_DIV, B_LT, B_LE, B_GT, B_GE, B_EQ, B_NE, B_AND, B_OR };
const char* const kBinText[] = {"+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!=", "and", "or"};
int binPrec(int b) { return b <= B_SUB ? 6 : b <= B_DIV ? 7 : b <= B_NE ? 5 : b == B_AND ? 3 : 2; }

struct Node {
    NodeOp op = N_PASS;
    uint8_t bin = 0;
    uint8_t ntype = 0;
    double v = 0;
    std::string name;
    std::vector<Node> k;
    struct Arm {
        std::vector<std::string> comments;
        std::string trail;
        std::vector<Node> body;
        int line = 0;
    };
    std::vector<Arm> arms;
    bool hasElse = false;
    Arm elseArm;
    std::vector<std::string> comments;
    std::string trail;
    int line = 0, col = 0;
};

struct Decl {
    enum Kind : uint8_t { PARAM, CURVE, LABEL, SLOT } kind = PARAM;
    std::string name;            // PARAM / CURVE name; LABEL text
    int slot = -1, weapon = -1;  // SLOT: WsSlot, weapon (-1 = every weapon); LABEL: weapon
    int ptype = T_F32;           // PARAM type; CURVE argument type
    double value = 0;            // PARAM
    std::string arg;             // CURVE
    int form = 0;                // CURVE: 0 expression, 1 steps, 2 points
    double base = 0;
    std::vector<std::pair<double, double>> pts;
    std::vector<Node> body;      // CURVE expr: body[0]; SLOT: statements
    std::vector<std::string> comments;
    std::string trail;
    int line = 0, col = 0, valueCol = 0;
};

struct File {
    int version = kWeaponSetFormatVersion;
    std::string name;
    std::vector<Decl> decls;
    std::vector<std::string> tail;
};

// =====================================================================================================
// Lexer + parser (botbrain.cpp's, for this grammar)
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
        if (rtrim(code).size() <= ind) {
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
                if (t.s.find('\\') != std::string::npos || t.s.size() > 64) { err = {no, t.col, "a string: at most 64 bytes, no \\"}; return false; }
            } else {
                static const char* two[] = {"<=", ">=", "==", "!="};
                std::string op;
                for (auto* o : two) if (code.compare(i, 2, o) == 0) op = o;
                if (op.empty()) {
                    if (!strchr("(),:=?+-*/<>", c)) { err = {no, (int)i + 1, std::string("unexpected '") + c + "'"}; return false; }
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
    Tok end_;   // (the token past a line's end: a member, so two threads parse at once safely)
    const Tok& tok() { if (ti < L->toks.size()) return L->toks[ti]; end_.col = L->toks.empty() ? 1 : L->toks.back().col + (int)L->toks.back().s.size(); return end_; }
    bool isOp(const char* o) { const Tok& t = tok(); return t.t == Tok::OP && t.s == o; }
    bool isName(const char* n) { const Tok& t = tok(); return t.t == Tok::NAME && t.s == n; }
    bool expectOp(const char* o) { if (!isOp(o)) return fail(tok().col, std::string("expected '") + o + "'"); ti++; return true; }
    bool atEnd() { return ti >= L->toks.size(); }
    bool expectEnd() { if (!atEnd()) return fail(tok().col, "unexpected '" + tok().s + "'"); return true; }
    bool name(std::string& out, const char* what) {
        if (tok().t != Tok::NAME) return fail(tok().col, std::string("expected ") + what);
        out = tok().s; ti++; return true;
    }
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
                return true;
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
        if (isKeyword(nm)) return fail(col, "'" + nm + "' can't be used here");
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
        if (w == "pass") { ti++; n.op = N_PASS; return expectEnd(); }
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
    bool armBody(Node::Arm& arm, int indent) {
        arm.trail = L->trail;
        if (!atEnd()) {
            Node s;
            if (!simple(s)) return false;
            arm.body.push_back(std::move(s));
            return true;
        }
        li++;
        if (li >= lines.size() || lines[li].indent <= indent) { L = li < lines.size() ? &lines[li] : L; return fail(1, "expected an indented block"); }
        int inner = lines[li].indent;
        if (!block(arm.body, inner)) return false;
        li--;
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
        if (!name(w, "a declaration (label, param, curve, slot)")) return false;
        if (w == "param") {
            d.kind = Decl::PARAM;
            std::string t;
            if (!name(d.name, "a parameter name") || !name(t, "a type (f32, f64, int, bool)")) return false;
            d.ptype = t == "f32" ? T_F32 : t == "f64" ? T_F64 : t == "int" ? T_INT : t == "bool" ? T_BOOL : -1;
            if (d.ptype < 0) return fail(L->toks[ti - 1].col, "a parameter type is f32, f64, int or bool");
            if (!expectOp("=")) return false;
            d.valueCol = tok().col;
            if (d.ptype == T_BOOL) {
                std::string b;
                if (!name(b, "yes or no")) return false;
                if (b != "yes" && b != "no" && b != "true" && b != "false") return fail(L->toks[ti - 1].col, "yes or no");
                d.value = (b == "yes" || b == "true") ? 1 : 0;
            } else if (!dataNum(d.value, d.ptype)) return false;
            if (!atEnd()) return fail(tok().col, "one value (a weapon set has no skill levels)");
        } else if (w == "label") {
            d.kind = Decl::LABEL;
            std::string wn;
            if (!name(wn, "a weapon")) return false;
            d.weapon = weaponFromKey(wn);
            if (d.weapon < 0) return fail(L->toks[ti - 1].col, "'" + wn + "' is not a weapon");
            if (tok().t != Tok::STR) return fail(tok().col, "expected the name in quotes");
            d.name = tok().s;
            if (d.name.empty() || d.name.size() > 24) return fail(tok().col, "a label: 1 to 24 characters");
            for (char c : d.name) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ')) return fail(tok().col, "a label: A-Z, 0-9 and spaces only");
            ti++;
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
        } else if (w == "slot") {
            d.kind = Decl::SLOT;
            int col = tok().col;
            if (!name(d.name, "a slot name (hit or kill)")) return false;
            d.slot = d.name == "hit" ? WS_HIT : d.name == "kill" ? WS_KILL : -1;
            if (d.slot < 0) return fail(col, "'" + d.name + "' is not a slot (hit, kill)");
            if (tok().t == Tok::NAME) {
                std::string wn = tok().s;
                d.weapon = weaponFromKey(wn);
                if (d.weapon < 0) return fail(tok().col, "'" + wn + "' is not a weapon");
                ti++;
            }
            if (!expectOp(":") || !expectEnd()) return false;
            li++;
            if (li >= lines.size() || lines[li].indent == 0) return fail(1, "expected the slot's indented block");
            if (!block(d.body, lines[li].indent)) return false;
            f.decls.push_back(std::move(d));
            return true;
        } else {
            return fail(L->toks[0].col, "expected a declaration (label, param, curve, slot)");
        }
        if (!expectEnd()) return false;
        f.decls.push_back(std::move(d));
        li++;
        return true;
    }

    bool file(const std::string& text, File& f) {
        if (!lexLines(text, lines, f.tail, err)) { failed = true; return false; }
        if (lines.empty()) { err = {1, 1, "empty file: it starts with 'version N'"}; failed = true; return false; }
        for (const Line& l : lines)
            if (l.toks.size() > 512) { err = {l.no, l.toks[512].col, "line too long: over 512 tokens"}; failed = true; return false; }
        L = &lines[0]; ti = 0;
        if (!L->comments.empty() || !L->trail.empty()) return fail(1, "the file starts with 'version N' (no comments before or on it)");
        if (!isName("version")) return fail(tok().col, "the file starts with 'version N'");
        ti++;
        if (tok().t != Tok::NUM || tok().s.find_first_not_of("0123456789") != std::string::npos) return fail(tok().col, "a version number");
        f.version = atoi(tok().s.c_str());
        if (f.version < 1 || f.version > kWeaponSetFormatVersion) return fail(tok().col, "version " + tok().s + ": this game reads version 1 to " + std::to_string(kWeaponSetFormatVersion));
        ti++;
        if (!expectEnd()) return false;
        li = 1;
        if (li >= lines.size()) return fail(1, "expected 'weapons \"name\"'");
        L = &lines[li]; ti = 0;
        if (!L->comments.empty() || !L->trail.empty()) return fail(1, "no comments in the header");
        if (!isName("weapons")) return fail(tok().col, "expected 'weapons \"name\"'");
        ti++;
        if (tok().t != Tok::STR) return fail(tok().col, "expected the name in quotes");
        f.name = tok().s; ti++;
        if (!expectEnd()) return false;
        li++;
        while (li < lines.size()) if (!decl(f)) return false;
        return true;
    }
};

bool parseText(const std::string& text, File& f, WsDiag* diag) {
    Parser p;
    if (p.file(text, f)) return true;
    if (diag) diag->errors.push_back({p.err.line, p.err.col, p.err.msg});
    return false;
}

// =====================================================================================================
// Canonical printer
// =====================================================================================================

struct Printer {
    std::string out;
    int prec(const Node& n) {
        switch (n.op) {
            case N_COND: return 1;
            case N_BIN: return binPrec(n.bin);
            case N_NOT: return 4;
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
            case N_NEG: return "-" + sub(n.k[0], 8, n.k[0].op == N_NUM);   // (-(2): "-2" would read back as the literal -2)
            case N_NOT: {
                const Node& a = n.k[0];
                bool force = a.op == N_BIN && binPrec(a.bin) == 5;
                return "not " + sub(a, 4, force);
            }
            case N_COND: return sub(n.k[0], 2, n.k[0].op == N_COND) + " ? " + sub(n.k[1], 1, n.k[1].op == N_COND) + " : " + sub(n.k[2], 1);
            case N_CALL: {
                std::string s = n.name + "(";
                for (size_t i = 0; i < n.k.size(); i++) s += (i ? ", " : "") + ex(n.k[i]);
                return s + ")";
            }
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
    void commentLines(const std::vector<std::string>& c, const std::string& ind) { for (auto& l : c) out += l.empty() ? "\n" : ind + l + "\n"; }
    std::string trailOf(const std::string& t) { return !t.empty() ? " " + t : ""; }
    std::string simpleText(const Node& s) {
        switch (s.op) {
            case N_SET: return s.name + " = " + ex(s.k[0]);
            case N_RETURN: {
                std::string r = "return";
                for (size_t i = 0; i < s.k.size(); i++) r += (i ? ", " : " ") + ex(s.k[i]);
                return r;
            }
            default: return "pass";
        }
    }
    bool inlineArm(const Node::Arm& a) { return a.body.size() == 1 && a.body[0].op != N_IF && a.body[0].comments.empty() && a.body[0].trail.empty(); }
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
    void decl(const Decl& d) {
        commentLines(d.comments, "");
        switch (d.kind) {
            case Decl::PARAM: out += "param " + d.name + " " + typeName(d.ptype) + " = " + fmtValue(d.value, d.ptype); break;
            case Decl::LABEL: out += std::string("label ") + kWeaponKeys[d.weapon] + " \"" + d.name + "\""; break;
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
            case Decl::SLOT:
                out += std::string("slot ") + kSlotNames[d.slot] + (d.weapon >= 0 ? std::string(" ") + kWeaponKeys[d.weapon] : "") + ":" + trailOf(d.trail) + "\n";
                block(d.body, "  ");
                return;
        }
        out += trailOf(d.trail) + "\n";
    }
    std::string file(const File& f) {
        out = "version " + std::to_string(f.version) + "\nweapons \"" + f.name + "\"\n";
        for (auto& d : f.decls) decl(d);
        commentLines(f.tail, "");
        return out;
    }
};

// =====================================================================================================
// JSON AST
// =====================================================================================================

struct JsonAst {
    int nextId = 0;
    std::function<std::string(const std::string&)> refOf;
    std::string id() { return "\"id\":\"n" + std::to_string(++nextId) + "\""; }
    std::string commentsOf(const std::vector<std::string>& c, const std::string& trail) {
        std::string o;
        if (!c.empty()) { o += ",\"comments\":["; for (size_t i = 0; i < c.size(); i++) o += (i ? "," : "") + jstr(c[i]); o += "]"; }
        if (!trail.empty()) o += ",\"trail\":" + jstr(trail);
        return o;
    }
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
            case N_CALL: {
                o += "\"op\":\"call\",\"fn\":" + jstr(n.name) + ",\"args\":[";
                for (size_t i = 0; i < n.k.size(); i++) o += (i ? "," : "") + ex(n.k[i]);
                return o + "]}";
            }
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
            case Decl::PARAM:
                o += "\"kind\":\"param\",\"name\":" + jstr(d.name) + ",\"type\":\"" + typeName(d.ptype) + "\",\"values\":[" +
                     (d.ptype == T_BOOL ? std::string(d.value ? "true" : "false") : fmtNumber(d.value, d.ptype)) + "]";
                break;
            case Decl::LABEL: o += std::string("\"kind\":\"label\",\"weapon\":\"") + kWeaponKeys[d.weapon] + "\",\"text\":" + jstr(d.name); break;
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
            case Decl::SLOT:
                o += std::string("\"kind\":\"slot\",\"name\":\"") + kSlotNames[d.slot] + "\"";
                if (d.weapon >= 0) o += std::string(",\"weapon\":\"") + kWeaponKeys[d.weapon] + "\"";
                o += ",\"body\":" + body(d.body);
                break;
        }
        return o + commentsOf(d.comments, d.trail) + "}";
    }
    std::string file(const File& f) {
        std::string o = "{\"format\":\"qweapons-ast\",\"version\":" + std::to_string(f.version) + ",\"name\":" + jstr(f.name) + ",\"decls\":[";
        for (size_t i = 0; i < f.decls.size(); i++) o += (i ? ",\n" : "\n") + decl(f.decls[i]);
        o += "],\"tail\":[";
        for (size_t i = 0; i < f.tail.size(); i++) o += (i ? "," : "") + jstr(f.tail[i]);
        return o + "]}";
    }
};

} // namespace

// =====================================================================================================
// The compiled set
// =====================================================================================================

namespace {
enum EOp : uint8_t { E_K, E_SENSOR, E_VAR, E_BIN, E_NEG, E_NOT, E_COND, E_CALL, E_CONV, E_CURVED, E_CURVEX };
struct CExpr { uint8_t op = E_K, type = T_F32, aux = 0; int32_t a = -1, b = -1, c = -1; double k = 0; };
enum SOp : uint8_t { S_SET, S_IF, S_RETURN, S_PASS };
struct CStmt { uint8_t op = S_PASS; int32_t var = -1, expr = -1, first = 0, count = 0; };
struct CArm { int32_t cond = -1, first = 0, count = 0; };   // cond -1: else; body: seq[first, first + count)
struct CCurve { uint8_t kind = 2, n = 0; float base = 0, x[16] = {}, y[16] = {}; };
constexpr int kMaxVars = 128;
} // namespace

struct WeaponSet {
    std::string name;
    WeaponValues values;
    std::vector<bool> writes;
    std::string labels[WPN_COUNT];
    std::vector<CExpr> ex;
    std::vector<CStmt> st;
    std::vector<CArm> arms;
    std::vector<int32_t> seq;    // statement lists (slot bodies, arm bodies)
    std::vector<int32_t> rets;   // return: (var, expr) pairs
    std::vector<CCurve> curves;
    int32_t slotFirst[WS_SLOTS][WPN_COUNT], slotCount[WS_SLOTS][WPN_COUNT];
    bool logic = false;
};
static_assert(sizeof(double) * 128 <= 1024, "runWeaponSlot's frame");

namespace {

struct Run {
    const WeaponSet& s;
    const double* in;
    double* var;
    static double toInt(double x) {
        if (!(x == x)) return 0;
        if (x >= 2147483647.0) return 2147483647.0;
        if (x <= -2147483648.0) return -2147483648.0;
        return (double)(int32_t)x;
    }
    static double wrap(int64_t v) { return (double)(int32_t)(uint32_t)(uint64_t)v; }
    double ev(int i) {
        const CExpr& e = s.ex[i];
        switch (e.op) {
            case E_K: return e.k;
            case E_SENSOR: return in[e.a];
            case E_VAR: return var[e.a];
            case E_CONV: {
                double x = ev(e.a);
                return e.aux == T_F32 ? (double)(float)x : e.aux == T_INT ? toInt(x) : x;
            }
            case E_NEG: {
                double x = ev(e.a);
                return e.type == T_F32 ? (double)(-(float)x) : e.type == T_INT ? wrap(-(int64_t)x) : -x;
            }
            case E_NOT: return ev(e.a) != 0 ? 0.0 : 1.0;
            case E_COND: return ev(e.a) != 0 ? ev(e.b) : ev(e.c);
            case E_BIN: {
                if (e.aux == B_AND) return (ev(e.a) != 0 && ev(e.b) != 0) ? 1.0 : 0.0;
                if (e.aux == B_OR) return (ev(e.a) != 0 || ev(e.b) != 0) ? 1.0 : 0.0;
                double x = ev(e.a), y = ev(e.b);
                switch (e.aux) {   // (operands: both of one type already, exact in a double)
                    case B_LT: return x < y; case B_LE: return x <= y; case B_GT: return x > y; case B_GE: return x >= y;
                    case B_EQ: return x == y; case B_NE: return x != y;
                    default: break;
                }
                if (e.type == T_F32) {
                    float a = (float)x, b = (float)y, r;
                    switch (e.aux) { case B_ADD: r = a + b; break; case B_SUB: r = a - b; break; case B_MUL: r = a * b; break; default: r = a / b; break; }
                    return r;
                }
                if (e.type == T_INT) {
                    int64_t a = (int64_t)x, b = (int64_t)y;
                    switch (e.aux) { case B_ADD: return wrap(a + b); case B_SUB: return wrap(a - b); default: return wrap(a * b); }
                }
                switch (e.aux) { case B_ADD: return x + y; case B_SUB: return x - y; case B_MUL: return x * y; default: return x / y; }
            }
            case E_CALL: {
                double a = ev(e.a);
                const bool f = e.type == T_F32;
                switch (e.aux) {
                    case BI_MIN: { double b = ev(e.b); return b < a ? b : a; }
                    case BI_MAX: { double b = ev(e.b); return a < b ? b : a; }
                    case BI_CLAMP: { double lo = ev(e.b), hi = ev(e.c); return a >= lo ? (a > hi ? hi : a) : lo; }
                    case BI_ABS: return e.type == T_INT ? toInt(std::fabs(a)) : std::fabs(a);
                    case BI_SQRT: return f ? (double)std::sqrt((float)a) : std::sqrt(a);
                    case BI_FLOOR: return f ? (double)std::floor((float)a) : std::floor(a);
                    case BI_CEIL: return f ? (double)std::ceil((float)a) : std::ceil(a);
                    case BI_POW: { double b = ev(e.b); return f ? (double)std::pow((float)a, (float)b) : std::pow(a, b); }
                    case BI_LERP: {
                        double b = ev(e.b), t = ev(e.c);
                        if (f) { float fa = (float)a, fb = (float)b, ft = (float)t; return fa + (fb - fa) * ft; }
                        return a + (b - a) * t;
                    }
                    default: return a;
                }
            }
            case E_CURVED: {
                const CCurve& c = s.curves[e.b];
                return evalCurve(c.kind, c.n, c.base, c.x, c.y, (float)ev(e.a));
            }
            case E_CURVEX: { var[e.c] = ev(e.a); return ev(e.b); }
        }
        return 0;
    }
    bool exec(int first, int count) {   // true: a return
        for (int i = 0; i < count; i++) {
            const CStmt& t = s.st[s.seq[first + i]];
            switch (t.op) {
                case S_SET: var[t.var] = ev(t.expr); break;
                case S_IF:
                    for (int k = 0; k < t.count; k++) {
                        const CArm& a = s.arms[t.first + k];
                        if (a.cond < 0 || ev(a.cond) != 0) { if (exec(a.first, a.count)) return true; break; }
                    }
                    break;
                case S_RETURN:
                    for (int k = 0; k < t.count; k++) var[s.rets[t.first + 2 * k]] = ev(s.rets[t.first + 2 * k + 1]);
                    return true;
                default: break;
            }
        }
        return false;
    }
};

// ---- the compiler: names, types, definite assignment
struct Compiler {
    WeaponSet& S;
    WsDiag& diag;
    struct ParamRef { int type; double value; };
    struct CurveRef { bool data; int idx; int argType; int resultType; int body; int argVar; int line; };
    std::map<std::string, ParamRef> params;
    std::map<std::string, CurveRef> curves;
    // the slot being compiled (slot < 0: a curve's expression)
    int slot = -1;
    std::string curveArg; int curveArgVar = -1, curveArgType = T_F32;
    struct Local { int var, type; };
    std::map<std::string, Local> locals;
    int nextVar = WSO_COUNT;
    int maxVar = WSO_COUNT;
    bool ok = true;

    Compiler(WeaponSet& s, WsDiag& d) : S(s), diag(d) {}
    int fail(int line, int col, const std::string& m) { diag.errors.push_back({line, col, m}); ok = false; return -1; }
    void warn(int line, int col, const std::string& m) { diag.warnings.push_back({line, col, m}); }
    int add(const CExpr& e) { S.ex.push_back(e); return (int)S.ex.size() - 1; }
    int konst(double v, int type) { CExpr e; e.op = E_K; e.type = (uint8_t)type; e.k = v; return add(e); }
    int conv(int e, int from, int to) {
        if (from == to) return e;
        if (S.ex[e].op == E_K) {   // (fold a constant)
            double v = S.ex[e].k;
            return konst(to == T_F32 ? (double)(float)v : to == T_INT ? Run::toInt(v) : v, to);
        }
        CExpr c; c.op = E_CONV; c.type = (uint8_t)to; c.aux = (uint8_t)to; c.a = e; return add(c);
    }
    static int promote(int a, int b) { return (a == T_F64 || b == T_F64) ? T_F64 : (a == T_F32 || b == T_F32) ? T_F32 : T_INT; }
    static bool widens(int from, int to) { return from == to || (from == T_INT && (to == T_F32 || to == T_F64)) || (from == T_F32 && to == T_F64); }
    bool reserved(const std::string& n) {
        return isKeyword(n) || builtinByName(n) >= 0 || sensorByName(n) >= 0 || outputByName(n) >= 0 || weaponFromKey(n) >= 0 ||
               n == "steps" || n == "points";
    }
    bool weaponPrefixed(const std::string& n, int& w, std::string& rest) {
        size_t dot = n.find('.');
        if (dot == std::string::npos) return false;
        w = weaponFromKey(n.substr(0, dot));
        rest = n.substr(dot + 1);
        return w >= 0;
    }

    // ---- expressions. assigned: the locals assigned on every path so far (nullptr in a curve)
    int expr(const Node& n, int& type, const std::vector<bool>* assigned) {
        switch (n.op) {
            case N_NUM: type = n.ntype; return konst(n.v, n.ntype);
            case N_BOOL: type = T_BOOL; return konst(n.v, T_BOOL);
            case N_NAME: return name(n, type, assigned);
            case N_NEG: {
                int t, a = expr(n.k[0], t, assigned);
                if (a < 0) return -1;
                if (!isNumeric(t)) return fail(n.line, n.col, std::string("'-' needs a number, not ") + typeName(t));
                type = t;
                CExpr e; e.op = E_NEG; e.type = (uint8_t)t; e.a = a; return add(e);
            }
            case N_NOT: {
                int t, a = expr(n.k[0], t, assigned);
                if (a < 0) return -1;
                if (t != T_BOOL) return fail(n.line, n.col, std::string("'not' needs a bool, not ") + typeName(t));
                type = T_BOOL;
                CExpr e; e.op = E_NOT; e.type = T_BOOL; e.a = a; return add(e);
            }
            case N_COND: {
                int tc, ta, tb;
                int c = expr(n.k[0], tc, assigned), a = expr(n.k[1], ta, assigned), b = expr(n.k[2], tb, assigned);
                if (c < 0 || a < 0 || b < 0) return -1;
                if (tc != T_BOOL) return fail(n.line, n.col, std::string("the condition of '?' is a bool, not ") + typeName(tc));
                if (ta == tb) type = ta;
                else if (isNumeric(ta) && isNumeric(tb)) { type = promote(ta, tb); a = conv(a, ta, type); b = conv(b, tb, type); }
                else return fail(n.line, n.col, std::string("the two sides of '?' differ: ") + typeName(ta) + " and " + typeName(tb));
                CExpr e; e.op = E_COND; e.type = (uint8_t)type; e.a = c; e.b = a; e.c = b; return add(e);
            }
            case N_BIN: {
                int ta, tb;
                int a = expr(n.k[0], ta, assigned), b = expr(n.k[1], tb, assigned);
                if (a < 0 || b < 0) return -1;
                CExpr e; e.op = E_BIN; e.aux = n.bin;
                const char* op = kBinText[n.bin];
                if (n.bin == B_AND || n.bin == B_OR) {
                    if (ta != T_BOOL || tb != T_BOOL) return fail(n.line, n.col, std::string("'") + op + "' needs two bools");
                    type = T_BOOL;
                } else if (n.bin == B_EQ || n.bin == B_NE) {
                    if (isNumeric(ta) && isNumeric(tb)) { int p = promote(ta, tb); a = conv(a, ta, p); b = conv(b, tb, p); }
                    else if (ta != tb) return fail(n.line, n.col, std::string("can't compare ") + typeName(ta) + " with " + typeName(tb));
                    type = T_BOOL;
                } else {
                    if (!isNumeric(ta) || !isNumeric(tb)) return fail(n.line, n.col, std::string("'") + op + "' needs numbers, not " + typeName(isNumeric(ta) ? tb : ta));
                    int p = promote(ta, tb);
                    if (n.bin == B_DIV && p == T_INT) return fail(n.line, n.col, "int / int: write 1.0 / 2 (no integer division)");
                    a = conv(a, ta, p); b = conv(b, tb, p);
                    type = n.bin >= B_LT ? T_BOOL : p;
                }
                e.type = (uint8_t)type; e.a = a; e.b = b;
                return add(e);
            }
            case N_CALL: return call(n, type, assigned);
            default: return fail(n.line, n.col, "not an expression");
        }
    }
    int name(const Node& n, int& type, const std::vector<bool>* assigned) {
        const std::string& nm = n.name;
        if (slot < 0) {   // a curve's expression: its argument, params, weapons
            if (nm == curveArg) { type = curveArgType; CExpr e; e.op = E_VAR; e.type = (uint8_t)type; e.a = curveArgVar; return add(e); }
            if (sensorByName(nm) >= 0 || outputByName(nm) >= 0) return fail(n.line, n.col, "a curve can't read '" + nm + "' (sensors and outputs are the slots')");
        } else {
            auto l = locals.find(nm);
            if (l != locals.end()) {
                if (assigned && !(*assigned)[l->second.var]) return fail(n.line, n.col, "'" + nm + "' may not be assigned on every path before this");
                type = l->second.type; CExpr e; e.op = E_VAR; e.type = (uint8_t)type; e.a = l->second.var; return add(e);
            }
            int o = outputByName(nm);
            if (o >= 0) {
                if (!slotHasOutput(slot, o)) return fail(n.line, n.col, "'" + nm + "' is not an output of slot " + kSlotNames[slot]);
                type = kOutputs[o].type; CExpr e; e.op = E_VAR; e.type = (uint8_t)type; e.a = o; return add(e);
            }
            int s = sensorByName(nm);
            if (s >= 0) {
                if (!slotHasSensor(slot, s)) return fail(n.line, n.col, "'" + nm + "' is not a sensor of slot " + kSlotNames[slot]);
                type = kSensors[s].type; CExpr e; e.op = E_SENSOR; e.type = (uint8_t)type; e.a = s; return add(e);
            }
        }
        auto p = params.find(nm);
        if (p != params.end()) { type = p->second.type; return konst(p->second.value, type); }
        int fi = weaponFieldIndex(nm);
        if (fi >= 0) { type = fieldType(weaponField(fi).type); return konst(getWeaponField(S.values, fi), type); }
        int w = weaponFromKey(nm);
        if (w >= 0) { type = T_WEAPON; return konst(w, T_WEAPON); }
        if (builtinByName(nm) >= 0 || curves.count(nm)) return fail(n.line, n.col, "'" + nm + "' is a function: call it, " + nm + "(...)");
        return fail(n.line, n.col, "unknown name '" + nm + "'");
    }
    int call(const Node& n, int& type, const std::vector<bool>* assigned) {
        int b = builtinByName(n.name);
        std::vector<int> args, types;
        for (const Node& a : n.k) { int t, e = expr(a, t, assigned); if (e < 0) return -1; args.push_back(e); types.push_back(t); }
        if (b >= 0) {
            const BuiltinInfo& bi = kBuiltins[b];
            if ((int)args.size() != bi.args) return fail(n.line, n.col, std::string(bi.sig) + ": " + std::to_string(bi.args) + " argument" + (bi.args > 1 ? "s" : ""));
            for (size_t i = 0; i < args.size(); i++)
                if (!isNumeric(types[i])) return fail(n.k[i].line, n.k[i].col, std::string(bi.name) + "() needs numbers, not " + typeName(types[i]));
            CExpr e; e.op = E_CALL; e.aux = (uint8_t)b;
            if (b == BI_F32 || b == BI_F64 || b == BI_INT) {
                type = b == BI_F32 ? T_F32 : b == BI_F64 ? T_F64 : T_INT;
                if (types[0] == type) return args[0];
                CExpr c; c.op = E_CONV; c.type = (uint8_t)type; c.aux = (uint8_t)type; c.a = args[0]; return add(c);
            }
            int t = types[0];
            for (int x : types) t = promote(t, x);
            bool floating = b == BI_SQRT || b == BI_FLOOR || b == BI_CEIL || b == BI_POW || b == BI_LERP;
            if (floating && t == T_INT) t = T_F64;
            for (size_t i = 0; i < args.size(); i++) args[i] = conv(args[i], types[i], t);
            type = t; e.type = (uint8_t)t;
            e.a = args[0]; if (args.size() > 1) e.b = args[1]; if (args.size() > 2) e.c = args[2];
            return add(e);
        }
        auto c = curves.find(n.name);
        if (c == curves.end()) return fail(n.line, n.col, "unknown function '" + n.name + "'");
        if (args.size() != 1) return fail(n.line, n.col, "a curve takes one argument");
        if (!isNumeric(types[0]) || !widens(types[0], c->second.argType))
            return fail(n.k[0].line, n.k[0].col, std::string("the curve takes an ") + typeName(c->second.argType) + ", not " + typeName(types[0]) +
                        (types[0] == T_F64 ? " (write f32(...))" : ""));
        int a = conv(args[0], types[0], c->second.argType);
        type = c->second.resultType;
        CExpr e; e.type = (uint8_t)type; e.a = a;
        if (c->second.data) { e.op = E_CURVED; e.b = c->second.idx; }
        else { e.op = E_CURVEX; e.b = c->second.body; e.c = c->second.argVar; }
        return add(e);
    }

    // ---- statements. Returns false on an error; *terminated: every path through it returns.
    bool block(const std::vector<Node>& body, std::vector<bool>& assigned, int& first, int& count, bool& terminated) {
        std::vector<int32_t> mine;
        terminated = false;
        for (const Node& s : body) {
            if (terminated) { warn(s.line, s.col, "never runs (after a return)"); }
            int idx = stmt(s, assigned, terminated);
            if (idx < 0) return false;
            mine.push_back(idx);
        }
        first = (int)S.seq.size();
        count = (int)mine.size();
        S.seq.insert(S.seq.end(), mine.begin(), mine.end());
        return true;
    }
    int addStmt(const CStmt& t) { S.st.push_back(t); return (int)S.st.size() - 1; }
    int assignTarget(const Node& at, const std::string& nm, int vt, int& e, std::vector<bool>& assigned) {
        int o = outputByName(nm);
        if (o >= 0) {
            if (!slotHasOutput(slot, o)) return fail(at.line, at.col, "'" + nm + "' is not an output of slot " + kSlotNames[slot]);
            int ot = kOutputs[o].type;
            if (!widens(vt, ot)) return fail(at.line, at.col, std::string("'") + nm + "' is " + typeName(ot) + ", not " + typeName(vt) + (vt == T_F64 && ot == T_F32 ? " (write f32(...))" : ""));
            e = conv(e, vt, ot);
            return o;
        }
        if (sensorByName(nm) >= 0) return fail(at.line, at.col, "'" + nm + "' is a sensor (read-only)");
        if (params.count(nm) || weaponFieldIndex(nm) >= 0) return fail(at.line, at.col, "'" + nm + "' is a param (a constant)");
        if (curves.count(nm) || builtinByName(nm) >= 0) return fail(at.line, at.col, "'" + nm + "' is a function");
        if (weaponFromKey(nm) >= 0) return fail(at.line, at.col, "'" + nm + "' is a weapon");
        if (nm.find('.') != std::string::npos) return fail(at.line, at.col, "a local's name has no '.'");
        auto l = locals.find(nm);
        if (l == locals.end()) {
            if (vt == T_NONE) return fail(at.line, at.col, "no value");
            if (nextVar >= kMaxVars) return fail(at.line, at.col, "too many locals");
            l = locals.insert({nm, {nextVar++, vt}}).first;
            maxVar = std::max(maxVar, nextVar);
            assigned.resize(kMaxVars, false);
        }
        if (!widens(vt, l->second.type)) return fail(at.line, at.col, std::string("'") + nm + "' is " + typeName(l->second.type) + " (its first assignment), not " + typeName(vt));
        e = conv(e, vt, l->second.type);
        assigned[l->second.var] = true;
        return l->second.var;
    }
    int stmt(const Node& s, std::vector<bool>& assigned, bool& terminated) {
        CStmt t;
        switch (s.op) {
            case N_PASS: t.op = S_PASS; return addStmt(t);
            case N_SET: {
                int vt, e = expr(s.k[0], vt, &assigned);
                if (e < 0) return -1;
                int v = assignTarget(s, s.name, vt, e, assigned);
                if (v < 0) return -1;
                t.op = S_SET; t.var = v; t.expr = e;
                return addStmt(t);
            }
            case N_RETURN: {
                int outs[WSO_COUNT], n = 0;
                for (int o = 0; o < WSO_COUNT; o++) if (slotHasOutput(slot, o)) outs[n++] = o;
                if ((int)s.k.size() > n) return fail(s.line, s.col, std::string("slot ") + kSlotNames[slot] + " has " + std::to_string(n) + " outputs");
                std::vector<int32_t> pairs;
                for (size_t i = 0; i < s.k.size(); i++) {
                    int vt, e = expr(s.k[i], vt, &assigned);
                    if (e < 0) return -1;
                    int o = outs[i], ot = kOutputs[o].type;
                    if (!widens(vt, ot)) return fail(s.k[i].line, s.k[i].col, std::string("'") + kOutputs[o].name + "' is " + typeName(ot) + ", not " + typeName(vt));
                    pairs.push_back(o); pairs.push_back(conv(e, vt, ot));
                }
                t.op = S_RETURN; t.first = (int)S.rets.size(); t.count = (int)s.k.size();
                S.rets.insert(S.rets.end(), pairs.begin(), pairs.end());
                terminated = true;
                return addStmt(t);
            }
            case N_IF: {
                std::vector<CArm> arms;
                std::vector<bool> joined;
                bool any = false, allTerm = true;
                auto join = [&](const std::vector<bool>& a, bool term) {
                    if (term) return;
                    allTerm = false;
                    if (!any) { joined = a; any = true; return; }
                    for (size_t i = 0; i < joined.size(); i++) joined[i] = joined[i] && i < a.size() && a[i];
                };
                for (size_t i = 0; i < s.arms.size(); i++) {
                    int ct, c = expr(s.k[i], ct, &assigned);
                    if (c < 0) return -1;
                    if (ct != T_BOOL) return fail(s.k[i].line, s.k[i].col, std::string("an if condition is a bool, not ") + typeName(ct));
                    std::vector<bool> a = assigned;
                    CArm arm; arm.cond = c;
                    bool term;
                    if (!block(s.arms[i].body, a, arm.first, arm.count, term)) return -1;
                    arms.push_back(arm);
                    join(a, term);
                }
                if (s.hasElse) {
                    std::vector<bool> a = assigned;
                    CArm arm;
                    bool term;
                    if (!block(s.elseArm.body, a, arm.first, arm.count, term)) return -1;
                    arms.push_back(arm);
                    join(a, term);
                } else join(assigned, false);
                if (allTerm) terminated = true;
                else assigned = joined;
                t.op = S_IF; t.first = (int)S.arms.size(); t.count = (int)arms.size();
                S.arms.insert(S.arms.end(), arms.begin(), arms.end());
                return addStmt(t);
            }
            default: return fail(s.line, s.col, "not a statement");
        }
    }

    // ---- declarations
    bool compile(const File& f) {
        S.name = f.name;
        S.values = standardWeaponValues();
        S.writes.assign(fields().size(), false);
        for (auto& r : S.slotFirst) for (auto& v : r) v = -1;
        for (auto& r : S.slotCount) for (auto& v : r) v = 0;
        // pass 0: params, labels
        std::map<std::string, int> declLine;
        for (const Decl& d : f.decls) {
            if (d.kind == Decl::LABEL) {
                if (!S.labels[d.weapon].empty()) fail(d.line, d.col, std::string("a second label for ") + kWeaponKeys[d.weapon]);
                S.labels[d.weapon] = d.name;
                continue;
            }
            if (d.kind != Decl::PARAM && d.kind != Decl::CURVE) continue;
            if (declLine.count(d.name)) { fail(d.line, d.col, "'" + d.name + "' is declared twice (line " + std::to_string(declLine[d.name]) + ")"); continue; }
            declLine[d.name] = d.line;
            if (d.kind != Decl::PARAM) continue;
            int w; std::string rest;
            int fi = weaponFieldIndex(d.name);
            if (fi >= 0) {
                const WeaponFieldInfo& fd = weaponField(fi);
                if (fd.readOnly) { fail(d.line, d.col, "'" + d.name + "' is the weapon's kind: it can't be changed"); continue; }
                int ft = fieldType(fd.type);
                if (d.ptype != ft) { fail(d.line, d.col, "'" + d.name + "' is " + typeName(ft) + ", not " + typeName(d.ptype)); continue; }
                double v = d.value;
                if (v < fd.lo || v > fd.hi) {
                    double c = v < fd.lo ? fd.lo : fd.hi;
                    warn(d.line, d.valueCol, d.name + " = " + fmtValue(v, ft) + " is out of range (" + fmtNumber(fd.lo, T_F64) + " to " + fmtNumber(fd.hi, T_F64) + "): " + fmtValue(c, ft) + " is used");
                }
                setWeaponField(S.values, fi, v);
                S.writes[fi] = true;
                params[d.name] = {ft, getWeaponField(S.values, fi)};
                continue;
            }
            if (weaponPrefixed(d.name, w, rest)) { fail(d.line, d.col, "'" + rest + "' is not a field of " + kWeaponKeys[w] + " (the fields: quark_tests exportweapons)"); continue; }
            if (reserved(d.name)) { fail(d.line, d.col, "'" + d.name + "' is reserved (a keyword, builtin, sensor, output or weapon)"); continue; }
            double v = d.ptype == T_F32 ? (double)(float)d.value : d.value;
            params[d.name] = {d.ptype, v};
        }
        finishValues(S.values);
        // pass 1: curves (an expression curve uses params and the curves declared before it)
        for (const Decl& d : f.decls) {
            if (d.kind != Decl::CURVE) continue;
            int w; std::string rest;
            bool falloff = weaponPrefixed(d.name, w, rest);
            if (falloff && rest != "falloff") { fail(d.line, d.col, "'" + d.name + "': a weapon's curve is <weapon>.falloff(dist f32)"); continue; }
            if (!falloff && d.name.find('.') == std::string::npos && reserved(d.name)) { fail(d.line, d.col, "'" + d.name + "' is reserved"); continue; }
            // (the argument may be named like a sensor, `dist`: a curve reads no sensors, so it only shadows)
            bool argOk = d.arg.find('.') == std::string::npos && !isKeyword(d.arg) && builtinByName(d.arg) < 0 && weaponFromKey(d.arg) < 0 &&
                         outputByName(d.arg) < 0 && !params.count(d.arg) && d.arg != "steps" && d.arg != "points";
            if (!argOk) { fail(d.line, d.col, "'" + d.arg + "' can't name the argument"); continue; }
            CurveRef r{};
            r.argType = d.ptype; r.line = d.line;
            if (d.form != 0) {
                CCurve c;
                c.kind = (uint8_t)d.form; c.n = (uint8_t)d.pts.size(); c.base = (float)d.base;
                for (size_t k = 0; k < d.pts.size(); k++) { c.x[k] = (float)d.pts[k].first; c.y[k] = (float)d.pts[k].second; }
                r.data = true; r.idx = (int)S.curves.size(); r.resultType = T_F32;
                S.curves.push_back(c);
                if (falloff) {
                    WeaponExtra& e = S.values.ex[w];
                    e.curveKind = c.kind; e.curveN = c.n; e.curveBase = c.base;
                    memcpy(e.curveX, c.x, sizeof c.x); memcpy(e.curveY, c.y, sizeof c.y);
                    e.falloff = true;
                }
            } else {
                if (falloff) { fail(d.line, d.col, "a weapon's falloff curve is data: points x: y, ... or steps base, x: y, ..."); continue; }
                if (nextVar >= kMaxVars) { fail(d.line, d.col, "too many curves"); continue; }
                slot = -1; curveArg = d.arg; curveArgType = d.ptype; curveArgVar = nextVar++;
                maxVar = std::max(maxVar, nextVar);
                int t, body = expr(d.body[0], t, nullptr);
                if (body < 0) continue;
                if (!isNumeric(t)) { fail(d.line, d.col, std::string("a curve gives a number, not ") + typeName(t)); continue; }
                r.data = false; r.body = body; r.argVar = curveArgVar; r.resultType = t;
            }
            curves[d.name] = r;
        }
        curveArg.clear();
        const int localBase = nextVar;
        // pass 2: slots
        bool seen[WS_SLOTS][WPN_COUNT + 1] = {};
        int general[WS_SLOTS][2], specific[WS_SLOTS][WPN_COUNT][2];
        for (auto& g : general) g[0] = -1;
        for (auto& sw : specific) for (auto& g : sw) g[0] = -1;
        for (const Decl& d : f.decls) {
            if (d.kind != Decl::SLOT) continue;
            int wi = d.weapon < 0 ? WPN_COUNT : d.weapon;
            if (seen[d.slot][wi]) { fail(d.line, d.col, std::string("a second slot ") + kSlotNames[d.slot] + (d.weapon >= 0 ? std::string(" ") + kWeaponKeys[d.weapon] : "")); continue; }
            seen[d.slot][wi] = true;
            if (d.weapon == WPN_SYRINGE || d.weapon == WPN_LOCATOR) warn(d.line, d.col, std::string("the ") + kWeaponKeys[d.weapon] + " never hits anyone: this slot never runs");
            slot = d.slot; locals.clear(); nextVar = localBase;
            std::vector<bool> assigned(kMaxVars, false);
            int first, count; bool term;
            if (!block(d.body, assigned, first, count, term)) continue;
            int* g = d.weapon < 0 ? general[d.slot] : specific[d.slot][d.weapon];
            g[0] = first; g[1] = count;
        }
        slot = -1;
        if (maxVar > kMaxVars) fail(1, 1, "too many locals and curves");
        for (int s = 0; s < WS_SLOTS; s++)
            for (int w = 0; w < WPN_COUNT; w++) {
                const int* g = specific[s][w][0] >= 0 ? specific[s][w] : general[s];
                if (g[0] < 0) continue;
                S.slotFirst[s][w] = g[0]; S.slotCount[s][w] = g[1];
                S.logic = true;
            }
        return ok;
    }
};

std::string defaultTextBuild() {
    const WeaponValues& v = standardWeaponValues();
    std::string o = "version " + std::to_string(kWeaponSetFormatVersion) + "\nweapons \"standard\"\n";
    static const char* altWhat[WPN_COUNT] = {"alt-fire: scope", "alt-fire: lob", "alt-fire: throw", "alt-fire: scope", "alt-fire: pull beam",
                                             "alt-fire: ricochet pellets", "alt-fire: set off your stickies", "alt-fire: scope", "alt-fire: stream",
                                             "fire heals you, alt-fire throws it to a teammate", "fire shows a hider (hide and seek, tag)"};
    int lastW = -1;
    for (int i = 0; i < weaponFieldCount(); i++) {
        const WeaponFieldInfo& f = weaponField(i);
        if (f.readOnly) continue;
        if (f.weapon != lastW) { o += std::string("\n# ") + kWeaponKeys[f.weapon] + " (" + altWhat[f.weapon] + ")\n"; lastW = f.weapon; }
        int t = fieldType(f.type);
        o += "param " + f.key + " " + typeName(t) + " = " + fmtValue(getWeaponField(v, i), t) + "\n";
    }
    return o;
}

} // namespace

// =====================================================================================================
// Public API
// =====================================================================================================

float evalWeaponCurve(const WeaponExtra& e, float x) { return evalCurve(e.curveKind, e.curveN, e.curveBase, e.curveX, e.curveY, x); }
const WeaponValues& standardWeaponValues() { static const WeaponValues v = makeStandard(); return v; }
const char* weaponKey(int w) { return w >= 0 && w < WPN_COUNT ? kWeaponKeys[w] : "?"; }
int weaponFromKey(const std::string& s) { for (int i = 0; i < WPN_COUNT; i++) if (s == kWeaponKeys[i]) return i; return -1; }
int weaponFieldCount() { return (int)fields().size(); }
const WeaponFieldInfo& weaponField(int i) { return fields()[i < 0 || i >= (int)fields().size() ? 0 : i]; }
int weaponFieldIndex(const std::string& key) {
    static const std::map<std::string, int> idx = [] { std::map<std::string, int> m; for (int i = 0; i < weaponFieldCount(); i++) m[weaponField(i).key] = i; return m; }();
    auto it = idx.find(key);
    return it == idx.end() ? -1 : it->second;
}
double getWeaponField(const WeaponValues& v, int i) {
    const WeaponFieldInfo& f = weaponField(i);
    const void* p = fieldPtr(const_cast<WeaponValues&>(v), f);
    if (f.type == WF_F32) { float x; memcpy(&x, p, 4); return x; }
    if (f.type == WF_INT) { int x; memcpy(&x, p, sizeof x); return x; }
    bool b; memcpy(&b, p, sizeof b); return b ? 1 : 0;
}
void setWeaponField(WeaponValues& v, int i, double x) {
    if (!(x == x)) return;
    const WeaponFieldInfo& f = weaponField(i);
    x = x < f.lo ? f.lo : x > f.hi ? f.hi : x;
    void* p = fieldPtr(v, f);
    if (f.type == WF_F32) { float y = (float)x; memcpy(p, &y, 4); }
    else if (f.type == WF_INT) { int y = (int)std::lround(x); memcpy(p, &y, sizeof y); }
    else { bool b = x != 0; memcpy(p, &b, sizeof b); }
    finishValues(v);
}

const char* standardWeaponSetText() { static const std::string t = defaultTextBuild(); return t.c_str(); }

std::shared_ptr<const WeaponSet> compileWeaponSet(const std::string& text, WsDiag* diagOut) {
    WsDiag local;
    WsDiag& diag = diagOut ? *diagOut : local;
    if (text.size() > kMaxWeaponSetBytes) { diag.errors.push_back({1, 1, "too big: a weapon set is at most " + std::to_string(kMaxWeaponSetBytes) + " bytes"}); return nullptr; }
    File f;
    if (!parseText(text, f, &diag)) return nullptr;
    auto s = std::make_shared<WeaponSet>();
    Compiler c(*s, diag);
    if (!c.compile(f)) return nullptr;
    return s;
}
const std::string& weaponSetName(const WeaponSet& s) { return s.name; }
const WeaponValues& weaponSetValues(const WeaponSet& s) { return s.values; }
bool weaponSetWrites(const WeaponSet& s, int field) { return field >= 0 && field < (int)s.writes.size() && s.writes[field]; }
const std::string& weaponSetLabel(const WeaponSet& s, int w) { static const std::string none; return w >= 0 && w < WPN_COUNT ? s.labels[w] : none; }
bool weaponSetHasLogic(const WeaponSet& s) { return s.logic; }
bool weaponSetHasSlot(const WeaponSet& s, int slot, int w) { return slot >= 0 && slot < WS_SLOTS && w >= 0 && w < WPN_COUNT && s.slotFirst[slot][w] >= 0; }
void runWeaponSlot(const WeaponSet& s, int slot, int w, WsCall& c) {
    if (!weaponSetHasSlot(s, slot, w)) return;
    double var[kMaxVars];   // (outputs, then curve arguments and locals: each written before it's read, the compiler checked)
    for (int o = 0; o < WSO_COUNT; o++) var[o] = c.out[o];
    Run r{s, c.in, var};
    r.exec(s.slotFirst[slot][w], s.slotCount[slot][w]);
    for (int o = 0; o < WSO_COUNT; o++) c.out[o] = var[o];
}

// ---- the editor's tools

std::string weaponSetDiagJson(const WsDiag& d) {
    auto msgs = [](const std::vector<WsDiag::Msg>& v) {
        std::string o = "[";
        for (size_t i = 0; i < v.size(); i++) o += (i ? "," : "") + std::string("{\"line\":") + std::to_string(v[i].line) + ",\"col\":" + std::to_string(v[i].col) + ",\"msg\":" + jstr(v[i].msg) + "}";
        return o + "]";
    };
    return std::string("{\"ok\":") + (d.errors.empty() ? "true" : "false") + ",\"errors\":" + msgs(d.errors) + ",\"warnings\":" + msgs(d.warnings) + "}";
}

std::string weaponSetAstJson(const std::string& text, WsDiag* d) {
    File f;
    if (!parseText(text, f, d)) return "";
    std::map<std::string, std::string> declared;
    for (const Decl& x : f.decls) if (x.kind == Decl::PARAM) declared[x.name] = "param"; else if (x.kind == Decl::CURVE) declared[x.name] = "curve";
    JsonAst j;
    j.refOf = [&](const std::string& n) -> std::string {
        if (sensorByName(n) >= 0) return "sensor";
        if (outputByName(n) >= 0) return "output";
        if (weaponFromKey(n) >= 0) return "weapon";
        auto it = declared.find(n);
        if (it != declared.end()) return it->second;
        if (weaponFieldIndex(n) >= 0) return "param";
        return "local";
    };
    return j.file(f);
}

std::string printWeaponSetText(const std::string& text, WsDiag* d) {
    File f;
    if (!parseText(text, f, d)) return "";
    Printer p;
    return p.file(f);
}

std::string weaponValuesJson(const WeaponValues& v) {
    std::string o = "{";
    for (int i = 0; i < weaponFieldCount(); i++) {
        const WeaponFieldInfo& f = weaponField(i);
        double x = getWeaponField(v, i);
        o += (i ? "," : "") + jstr(f.key) + ":" + (f.type == WF_BOOL ? std::string(x ? "true" : "false") : fmtNumber(x, fieldType(f.type)));
    }
    return o + "}";
}

std::string exportWeaponsJson() {
    const WeaponValues& std_ = standardWeaponValues();
    std::string o = "{\"format\":\"qweapons-catalog\",\"version\":" + std::to_string(kWeaponSetFormatVersion) + ",\"fields\":[";
    for (int i = 0; i < weaponFieldCount(); i++) {
        const WeaponFieldInfo& f = weaponField(i);
        int t = fieldType(f.type);
        double def = getWeaponField(std_, i);
        o += std::string(i ? ",\n" : "\n") + "{\"weapon\":\"" + kWeaponKeys[f.weapon] + "\",\"field\":" + jstr(f.field) + ",\"key\":" + jstr(f.key) +
             ",\"type\":\"" + typeName(t) + "\",\"unit\":" + jstr(f.unit) + ",\"min\":" + jnum(f.lo) + ",\"max\":" + jnum(f.hi) +
             ",\"default\":" + (t == T_BOOL ? std::string(def ? "true" : "false") : fmtNumber(def, t)) + ",\"doc\":" + jstr(f.doc) +
             (f.alt ? ",\"alt\":true" : "") + (f.readOnly ? ",\"readOnly\":true" : "") + "}";
    }
    static const char* kinds[WPN_COUNT] = {"hitscan", "projectile", "melee", "hitscan", "beam", "hitscan", "projectile", "projectile", "projectile", "support", "support"};
    static const char* alts[WPN_COUNT] = {"scope (zoom, slower fire)", "lob (slower, drops)", "throw (a projectile; the knife is away for a while)", "scope (zoom)",
                                          "pull beam (pulls, less damage)", "ricochet pellets (bounce off walls, less damage)", "set off your stickies",
                                          "scope (zoom)", "stream (5 to 15 bubbles at once, then a long reload)", "throw (heals a teammate, resets their cooldowns)", ""};
    o += "],\"weapons\":[";
    for (int w = 0; w < WPN_COUNT; w++)
        o += std::string(w ? ",\n" : "\n") + "{\"id\":" + std::to_string(w) + ",\"name\":\"" + kWeaponKeys[w] + "\",\"title\":" + jstr(baseWeaponDef(w).name) +
             ",\"kind\":\"" + kinds[w] + "\",\"altFire\":" + jstr(alts[w]) + ",\"slot\":" + std::to_string(weaponSlot(w)) + "}";
    o += "],\"slots\":[";
    for (int s = 0; s < WS_SLOTS; s++) {
        o += std::string(s ? "," : "") + "{\"name\":\"" + kSlotNames[s] + "\",\"doc\":" + jstr(kSlotDocs[s]) + ",\"perWeapon\":true,\"sensors\":[";
        bool first = true;
        for (int i = 0; i < WSS_COUNT; i++) {
            if (!slotHasSensor(s, i)) continue;
            o += std::string(first ? "" : ",") + "{\"name\":\"" + kSensors[i].name + "\",\"type\":\"" + typeName(kSensors[i].type) + "\",\"unit\":" + jstr(kSensors[i].unit) + ",\"doc\":" + jstr(kSensors[i].doc) + "}";
            first = false;
        }
        o += "],\"outputs\":[";
        first = true;
        for (int i = 0; i < WSO_COUNT; i++) {
            if (!slotHasOutput(s, i)) continue;
            o += std::string(first ? "" : ",") + "{\"name\":\"" + kOutputs[i].name + "\",\"type\":\"" + typeName(kOutputs[i].type) + "\",\"default\":" + jstr(kOutputs[i].def) + ",\"doc\":" + jstr(kOutputs[i].doc) + "}";
            first = false;
        }
        o += "]}";
    }
    o += "],\"builtins\":[";
    for (int b = 0; b < BI_COUNT; b++) o += std::string(b ? "," : "") + "{\"name\":\"" + kBuiltins[b].name + "\",\"args\":" + std::to_string(kBuiltins[b].args) + ",\"sig\":" + jstr(kBuiltins[b].sig) + ",\"doc\":" + jstr(kBuiltins[b].doc) + "}";
    o += "],\"keywords\":[";
    for (size_t k = 0; k < sizeof kKeywords / sizeof kKeywords[0]; k++) o += std::string(k ? "," : "") + jstr(kKeywords[k]);
    o += "],\"types\":[\"bool\",\"int\",\"f32\",\"f64\",\"weapon\"],\"defaultText\":" + jstr(standardWeaponSetText()) + "}";
    return o;
}

// ---- stats

std::vector<WeaponStats> computeWeaponStats(const WeaponValues& v, float health) {
    std::vector<WeaponStats> out;
    if (!(health > 0)) health = kMaxHealth;
    const WeaponAlt& a = v.alt;
    for (int w = 0; w < WPN_COUNT; w++) {
        if (w == WPN_LOCATOR) continue;
        const WeaponDef& d = v.def[w];
        const WeaponExtra& e = v.ex[w];
        auto make = [&](bool alt, const char* mode, float body, float head, float refire, float speed, float accel = 0) {
            WeaponStats s;
            s.weapon = w; s.alt = alt; s.mode = mode;
            s.dmgBody = body; s.dmgHead = head; s.refire = refire;
            s.shotsBody = body > 0 ? (int)std::ceil(health / body - 1e-4f) : 0;
            s.shotsHead = head > 0 ? (int)std::ceil(health / head - 1e-4f) : 0;
            s.ttkBody = s.shotsBody > 0 ? (s.shotsBody - 1) * refire : 0;
            s.ttkHead = s.shotsHead > 0 ? (s.shotsHead - 1) * refire : 0;
            s.dpsBody = refire > 0 ? body / refire : 0;
            s.dpsHead = refire > 0 ? head / refire : 0;
            s.range = d.projectile ? 0 : (w == WPN_KNIFE ? d.range : d.range);
            s.projSpeed = speed; s.splash = d.splashRadius;
            static const float at[3] = {10, 20, 40};
            for (int k = 0; k < 3; k++) {
                s.travel[k] = speed > 0 ? projectileFlightTime(at[k], speed, accel) : 0;   // (the primary rocket's thrust: accel)
                s.falloff[k] = weaponFalloff(e, at[k]);
            }
            if (w == WPN_SYRINGE) { s.shotsBody = s.shotsHead = 0; s.ttkBody = s.ttkHead = 0; s.dpsBody = s.dpsHead = 0; s.range = 0; }
            out.push_back(s);
        };
        float pel = (float)d.pellets;
        float body = d.damage * pel, head = e.headDamage > 0 ? std::max(d.damage, e.headDamage) * pel : body;
        if (d.projectile && e.directDamage > 0) body = head = std::max(body, e.directDamage);
        if (d.projectile && d.splashRadius > 0) head = body;   // (blasts: no headshots)
        float speed = d.projectile ? d.projSpeed : 0;
        if (w == WPN_SYRINGE) { make(false, "heal", d.damage, d.damage, d.refire, 0); make(true, "throw", a.syringeHeal, a.syringeHeal, d.refire, a.syringeThrowSpeed); continue; }
        make(false, "primary", body, head, d.refire, speed, w == WPN_ROCKET ? a.rocketAccel : 0.0f);
        switch (w) {
            case WPN_RIFLE: make(true, "scoped", body, head, d.refire * a.scopedRefire, 0); break;
            case WPN_ROCKET: make(true, "lob", body, head, d.refire, d.projSpeed * a.lobSpeed); break;
            case WPN_KNIFE: make(true, "throw", d.damage, d.damage, d.refire, a.knifeThrowSpeed); break;
            case WPN_LASER: make(true, "pull", d.damage * a.pullDamage, d.damage * a.pullDamage, d.refire, 0); break;
            case WPN_SHOTGUN: make(true, "bounce", d.damage * a.bounceDamage * pel, d.damage * a.bounceDamage * pel, d.refire, 0); break;
            case WPN_BUBBLE: {
                float n = (float)a.streamMax;   // (every bubble of the longest stream landing)
                make(true, "stream", d.damage * n, d.damage * n, a.streamRefire, a.streamSlow * (d.projSpeed / baseWeaponDef(WPN_BUBBLE).projSpeed));
                break;
            }
            default: break;
        }
    }
    return out;
}

std::string weaponStatsJson(const WeaponValues& v, float health) {
    std::string o = "[";
    auto st = computeWeaponStats(v, health);
    for (size_t i = 0; i < st.size(); i++) {
        const WeaponStats& s = st[i];
        auto n = [](float x) { return jnum(x); };
        o += std::string(i ? ",\n" : "\n") + "{\"weapon\":\"" + kWeaponKeys[s.weapon] + "\",\"alt\":" + (s.alt ? "true" : "false") + ",\"mode\":\"" + s.mode + "\"" +
             ",\"dmgBody\":" + n(s.dmgBody) + ",\"dmgHead\":" + n(s.dmgHead) + ",\"refire\":" + n(s.refire) +
             ",\"shotsBody\":" + std::to_string(s.shotsBody) + ",\"shotsHead\":" + std::to_string(s.shotsHead) +
             ",\"ttkBody\":" + n(s.ttkBody) + ",\"ttkHead\":" + n(s.ttkHead) + ",\"dpsBody\":" + n(s.dpsBody) + ",\"dpsHead\":" + n(s.dpsHead) +
             ",\"range\":" + n(s.range) + ",\"projSpeed\":" + n(s.projSpeed) + ",\"splash\":" + n(s.splash) +
             ",\"travel\":[" + n(s.travel[0]) + "," + n(s.travel[1]) + "," + n(s.travel[2]) + "]" +
             ",\"falloff\":[" + n(s.falloff[0]) + "," + n(s.falloff[1]) + "," + n(s.falloff[2]) + "]}";
    }
    return o + "]";
}

// ---- files and the rules

std::string safeWeaponSetName(const std::string& n) {   // (customrules.cpp's safeName rule)
    std::string s;
    for (char c : n) s += (isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') ? c : '_';
    if (s.size() > 40) s.resize(40);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
std::string customWeaponsDir() { return userDataPath("custom_weapons"); }
std::vector<std::string> listWeaponSets() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(utf8Path(customWeaponsDir()), ec))
        if (e.path().extension() == ".qweapons") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}
bool loadWeaponSetFile(const std::string& name, std::string& text) {
    std::string n = safeWeaponSetName(name);
    if (n.empty()) return false;
    std::ifstream f(utf8Path(customWeaponsDir() + "/" + n + ".qweapons"), std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    text = ss.str();
    return true;
}
bool saveWeaponSetFile(const std::string& name, const std::string& text) {
    std::string n = safeWeaponSetName(name);
    if (n.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(utf8Path(customWeaponsDir()), ec);
    return writeFileAtomic(customWeaponsDir() + "/" + n + ".qweapons", text);
}

std::string weaponSetRulesLines(const CustomRules& r) {
    std::string o;
    if (!r.weaponSetName.empty()) o += "\nweaponset=" + r.weaponSetName;
    const std::string& t = r.weaponSetText;
    for (size_t i = 0; i < t.size();) {
        size_t e = t.find('\n', i);
        if (e == std::string::npos) e = t.size();
        o += "\nwsline=" + t.substr(i, e - i);
        i = e + 1;
    }
    return o;
}

bool readWeaponSetRulesLine(CustomRules& r, const std::string& k, const std::string& v) {
    if (k == "weaponset") { r.weaponSetName = safeWeaponSetName(v); return true; }
    if (k != "wsline") return false;
    std::string line = v;
    while (!line.empty() && line.back() == '\r') line.pop_back();   // (fromText strips one: a round trip is the same text)
    if (r.weaponSetText.size() + line.size() + 1 <= kMaxWeaponSetBytes) r.weaponSetText += line + "\n";
    return true;
}

bool resolveWeaponSet(CustomRules& r, std::string* err) {
    auto drop = [&](const std::string& why) { r.weaponSetText.clear(); if (err) *err = why; return false; };
    if (r.weaponSetName.empty() && r.weaponSetText.empty()) return false;
    if (r.weaponSetText.empty()) {
        std::string text;
        if (!loadWeaponSetFile(r.weaponSetName, text)) return drop("weapon set '" + r.weaponSetName + "' not found (custom_weapons/" + r.weaponSetName + ".qweapons): the standard weapons");
        std::string norm;   // (one form everywhere: \n line ends, the last line ended)
        for (char c : text) if (c != '\r') norm += c;
        if (!norm.empty() && norm.back() != '\n') norm += '\n';
        if (norm.size() > kMaxWeaponSetBytes) return drop("weapon set '" + r.weaponSetName + "' is too big (" + std::to_string(norm.size()) + " bytes, at most " + std::to_string(kMaxWeaponSetBytes) + "): the standard weapons");
        r.weaponSetText = norm;
    }
    WsDiag d;
    if (!compileWeaponSet(r.weaponSetText, &d)) {
        const WsDiag::Msg& m = d.errors.empty() ? WsDiag::Msg{} : d.errors[0];
        return drop("weapon set '" + r.weaponSetName + "' line " + std::to_string(m.line) + ":" + std::to_string(m.col) + ": " + m.msg + " (the standard weapons)");
    }
    if (r.toText().size() > kMaxRulesTextBytes)
        return drop("weapon set '" + r.weaponSetName + "' makes the custom mode's rules too big to send to players (over " + std::to_string(kMaxRulesTextBytes) + " bytes): the standard weapons");
    return true;
}
