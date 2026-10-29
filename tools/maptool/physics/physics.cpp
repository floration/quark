// Physics sets (a custom mode's movement settings) and the physparams / physexport commands.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "maptool.h"
#include "physics.h"
#include "common/version.h"
#include "game/gamedefs.h"

namespace phys {
namespace {

std::string num(double v) { char b[40]; snprintf(b, sizeof b, "%.9g", v); return b; }
std::string mulStr(float v) { char b[40]; snprintf(b, sizeof b, "%.6g", v); return b; }

// The ranges CustomRules::fromText clamps to, found by asking it (so they follow the game's code, never a copy):
// a huge and a tiny value for the key, read back.
struct Range { float min, max; };
Range probeRange(const std::string& key, const std::string& field) {
    auto read = [&](const char* v) {
        CustomRules r;
        r.fromText("custommode=probe\n" + key + "=" + v + "\n");
        if (field == "gravity") return r.gravity;
        if (field == "speed") return r.moveSpeed;
        if (field == "jump") return r.jump;
        for (int k = 0; k < MV_COUNT; k++) if (field == moveParamKey(k)) return r.move[k];
        return 1.0f;
    };
    return {read("-1e30"), read("1e30")};
}

const char* groupOf(int k) {
    switch (k) {
    case MV_CROUCH_SPEED: case MV_GROUND_ACCEL: case MV_FRICTION: case MV_STOP_SPEED: case MV_STEP: return "ground";
    case MV_AIR_ACCEL: case MV_AIR_CAP: case MV_OVERSTRAFE: case MV_AIR_CONTROL: case MV_AIR_DRIFT: case MV_STRAFE_BOOST: return "air";
    case MV_DASH_SPEED: case MV_DASH_TIME: case MV_DASH_EXIT_MIN: case MV_DASH_KEEP: case MV_DASH_EXIT_MAX: case MV_DASH_VERTICAL:
    case MV_DASH_CHARGES: case MV_DASH_COOLDOWN: case MV_DASH_BOUNCE_ANGLE: return "dash";
    case MV_DASH_JUMP_WINDOW: case MV_SUPER_DASH_BOOST: case MV_SUPER_DASH_JUMP: case MV_SUPER_DASH_MAX: return "super dash";
    case MV_HOOK_RANGE: case MV_HOOK_ACCEL: case MV_HOOK_MAX: case MV_HOOK_GRAVITY: case MV_HOOK_RELEASE: case MV_HOOK_COOLDOWN: return "grapple";
    case MV_LADDER_SPEED: case MV_LADDER_SIDE: return "ladder";
    default: return "general";   // bhop bounce
    }
}

// The C++ constants each multiplier scales (physexport's list). Hardcoded on purpose: names are never read from the
// sources at run time. Where they live: pmove.cpp's anonymous namespace (top of the file) unless the file says
// pmove.h. A multiplier that scales more than one constant lists each one. test_phys.py checks every name and base
// value here against the sources.
struct ConstRef { const char* file; const char* name; float base; bool integer; };
struct ParamConsts { int param; ConstRef c[3]; };
// (-1 / -2 / -3: gravity, speed, jump)
const ParamConsts kConsts[] = {
    {-1, {{"src/game/pmove.cpp", "kGravity", 24.0f, false}}},
    {-2, {{"src/game/pmove.cpp", "kGroundSpeed", 9.5f, false}, {"src/game/pmove.cpp", "kAirControlFrom", 9.5f, false},
         }},   // (pmove: maxSpeed and the W-only air control knee x speed; the crouch speed too: listed under crouch_speed)
    {-3, {{"src/game/pmove.cpp", "kJumpSpeed", 7.67f, false}}},
    {MV_CROUCH_SPEED, {{"src/game/pmove.cpp", "kCrouchSpeed", 4.0f, false}}},
    {MV_GROUND_ACCEL, {{"src/game/pmove.cpp", "kGroundAccel", 16.0f, false}}},
    {MV_FRICTION, {{"src/game/pmove.cpp", "kFriction", 7.0f, false}}},
    {MV_STOP_SPEED, {{"src/game/pmove.cpp", "kStopSpeed", 2.0f, false}}},
    {MV_AIR_ACCEL, {{"src/game/pmove.cpp", "kAirAccel", 2.2f, false}}},
    {MV_AIR_CAP, {{"src/game/pmove.cpp", "kAirSpeedCap", 2.3f, false}}},
    {MV_OVERSTRAFE, {{"src/game/pmove.cpp", "kOverstrafeBrake", 0.35f, false}}},
    {MV_AIR_CONTROL, {{"src/game/pmove.cpp", "kAirControl", 4.0f, false}}},
    {MV_STEP, {{"src/game/pmove.cpp", "kStepHeight", 0.55f, false}}},
    {MV_DASH_SPEED, {{"src/game/pmove.cpp", "kDashSpeed", 24.0f, false}, {"src/game/pmove.cpp", "kDashBounceSpeed", 16.0f, false}}},
    {MV_DASH_TIME, {{"src/game/pmove.cpp", "kDashTime", 0.12f, false}}},
    {MV_DASH_EXIT_MIN, {{"src/game/pmove.cpp", "kDashExitMin", 11.0f, false}}},
    {MV_DASH_KEEP, {{"src/game/pmove.cpp", "kDashKeep", 1.0f, false}}},
    {MV_DASH_EXIT_MAX, {{"src/game/pmove.cpp", "kDashExitMax", 32.0f, false}}},
    {MV_DASH_VERTICAL, {{"src/game/pmove.cpp", "kDashVerticalDown", 0.9f, false}, {"src/game/pmove.cpp", "kDashVerticalUp", 0.4f, false}}},
    {MV_DASH_CHARGES, {{"src/game/pmove.h", "kDashCharges", 2.0f, true}}},
    {MV_DASH_COOLDOWN, {{"src/game/pmove.h", "kDashCooldown", 0.6f, false}, {"src/game/pmove.h", "kDashRecharge", 1.2f, false}}},
    {MV_BOUNCE_GAIN, {{"src/game/pmove.cpp", "kBounceGain", 0.5f, false}}},
    {MV_BOUNCE_MAX, {{"src/game/pmove.cpp", "kBounceMax", 8.0f, false}}},
    {MV_HOOK_RANGE, {{"src/game/pmove.h", "kGrappleRange", 45.0f, false}}},
    {MV_HOOK_ACCEL, {{"src/game/pmove.cpp", "kHookAccel", 38.0f, false}}},
    {MV_HOOK_MAX, {{"src/game/pmove.cpp", "kHookMaxSpeed", 24.0f, false}}},
    {MV_HOOK_GRAVITY, {{"src/game/pmove.cpp", "kHookGravityScale", 0.5f, false}}},
    {MV_HOOK_RELEASE, {{"src/game/pmove.cpp", "kHookRelease", 1.2f, false}}},
    {MV_HOOK_COOLDOWN, {{"src/game/pmove.h", "kHookCooldown", 1.0f, false}}},
    {MV_LADDER_SPEED, {{"src/game/pmove.cpp", "kLadderSpeed", 6.75f, false}}},
    {MV_LADDER_SIDE, {{"src/game/pmove.cpp", "kLadderSide", 4.5f, false}}},
    {MV_DASH_JUMP_WINDOW, {{"src/game/pmove.cpp", "kDashJumpWindow", 0.1f, false}}},
    {MV_SUPER_DASH_BOOST, {{"src/game/pmove.cpp", "kSuperDashBoost", 5.0f, false}}},
    {MV_SUPER_DASH_JUMP, {{"src/game/pmove.cpp", "kSuperDashJump", 0.9f, false}}},
    {MV_SUPER_DASH_MAX, {{"src/game/pmove.cpp", "kSuperDashMax", 36.0f, false}}},
    {MV_DASH_BOUNCE_ANGLE, {{"src/game/pmove.cpp", "kDashBounceAngle", 45.0f, false}}},
    {MV_AIR_DRIFT, {{"src/game/pmove.cpp", "kAirDriftAccel", 8.0f, false}}},
    {MV_STRAFE_BOOST, {{"src/game/pmove.cpp", "kStrafeBoost", 0.10f, false}}},
};
const ParamConsts* constsFor(int param) {
    for (auto& p : kConsts) if (p.param == param) return &p;
    return nullptr;
}
Json constsJson(int param) {
    Json a = Json::array();
    if (const ParamConsts* p = constsFor(param))
        for (auto& c : p->c) if (c.name) { Json o = Json::object(); o.set("file", c.file); o.set("name", c.name); o.set("base", c.base); a.push(std::move(o)); }
    return a;
}

// Notes on parameters the page should show (the game's behaviour, read from pmove.cpp).
const char* noteFor(int k) {
    switch (k) {
    case MV_CROUCH_SPEED: return "crouching was removed from the game (pmove ignores BTN_CROUCH): this has no effect";
    case MV_AIR_ACCEL: return "pmove uses it x10 (22 m/s2 per m/s of wish speed)";
    case MV_DASH_SPEED: return "also scales the wall bounce's speed (16 m/s)";
    case MV_DASH_COOLDOWN: return "scales the cooldown (0.6 s) and each charge's recharge (1.2 s)";
    case MV_DASH_CHARGES: return "rounded to whole charges, 1 to 10";
    case MV_SUPER_DASH_JUMP: return "scales the share of its speed that goes up (the dash's down share, at most 0.9)";
    default: return nullptr;
    }
}

// The equation language (moveexpr.h), from the game's own tables: the page's help and its autocomplete.
Json exprSyntaxJson() {
    Json e = Json::object();
    Json in = Json::array();
    for (int i = 0; i < mexpr::P_COUNT; i++) {
        const mexpr::InputInfo& q = mexpr::inputInfo(i);
        Json o = Json::object();
        o.set("name", std::string("p.") + q.name); o.set("unit", q.unit); o.set("min", q.lo); o.set("max", q.hi); o.set("rest", q.rest);
        o.set("desc", q.desc);
        if (q.hi - q.lo == 1 && q.lo == 0 && i != mexpr::P_AIR && i != mexpr::P_GROUND) o.set("flag", true);   // (0 / 1)
        if (i == mexpr::P_CHARGES) o.set("integer", true);
        in.push(std::move(o));
    }
    e.set("inputs", std::move(in));
    Json fn = Json::array();
    for (auto& f : mexpr::functions()) {
        Json o = Json::object();
        o.set("name", f.name); o.set("sig", f.sig); o.set("desc", f.desc); o.set("minArgs", f.minArgs); o.set("maxArgs", f.maxArgs);
        fn.push(std::move(o));
    }
    e.set("functions", std::move(fn));
    Json ease = Json::array();
    for (const char* m : mexpr::easeModes()) ease.push(m);
    e.set("ease", std::move(ease));
    Json ops = Json::array();
    for (const char* o : {"+", "-", "*", "/", "^", "<", "<=", ">", ">=", "==", "!=", "&&", "||", "!", "?:", "( )"}) ops.push(o);
    e.set("operators", std::move(ops));
    Json names = Json::array();
    names.push("<key>: another setting's multiplier (gravity, speed, jump, or a params key)");
    names.push("base.<key>: that setting's standard absolute value (base.speed = 9.5)");
    names.push("pi");
    e.set("names", std::move(names));
    e.set("maxLength", mexpr::kMaxText);
    e.set("value", "the setting's multiplier (1 = standard), clamped to its min..max; NaN keeps its fixed value");
    e.set("line", "expr.<key>=<expression> in the .qmode, next to the plain <key>= / move_<key>= line (the fallback)");
    return e;
}

int runParams(const Json&, Json& out, CmdCtx&) {
    double t0 = maptoolNow();
    out = Json::object();
    Json params = Json::array();
    for (int k = 0; k < MV_COUNT; k++) {
        Json p = Json::object();
        Range r = probeRange(std::string("move_") + moveParamKey(k), moveParamKey(k));
        p.set("key", moveParamKey(k));
        p.set("label", moveParamLabel(k));
        p.set("unit", moveParamUnit(k));
        p.set("base", moveParamBase(k));
        p.set("group", groupOf(k));
        p.set("min", r.min);
        p.set("max", r.max);
        p.set("step", 0.01f);
        p.set("dragStep", 0.05f);   // (the game's slider: dragged in 0.05 steps, typed as is)
        p.set("default", 1);
        p.set("constants", constsJson(k));
        p.set("cpp", constsFor(k)->c[0].name);   // (the main one; `constants` lists every constant it scales)
        if (const char* n = noteFor(k)) { p.set("note", n); p.set("doc", n); }
        if (k == MV_CROUCH_SPEED) p.set("inactive", true);
        params.push(std::move(p));
    }
    out.set("params", std::move(params));
    Json top = Json::array();
    struct T { const char* key; const char* label; const char* unit; float base; int id; } tops[] = {
        {"gravity", "GRAVITY", "m/s2", moveBaseGravity(), -1}, {"speed", "RUN SPEED", "m/s", moveBaseSpeed(), -2},
        {"jump", "JUMP", "m/s", moveBaseJump(), -3}};
    for (auto& t : tops) {
        Json p = Json::object();
        Range r = probeRange(t.key, t.key);
        p.set("key", t.key); p.set("label", t.label); p.set("unit", t.unit); p.set("base", t.base);
        p.set("group", "top"); p.set("min", r.min); p.set("max", r.max); p.set("step", 0.01f); p.set("default", 1);
        p.set("constants", constsJson(t.id));
        p.set("cpp", constsFor(t.id)->c[0].name);
        if (t.id == -2) { const char* n = "also scales the W-only air control knee (9.5 m/s), the air drift's limit and the crouch speed"; p.set("note", n); p.set("doc", n); }
        top.push(std::move(p));
    }
    out.set("top", std::move(top));
    Json toggles = Json::array();
    for (const char* t : {"dash", "grapple"}) { Json o = Json::object(); o.set("key", t); o.set("default", true); toggles.push(std::move(o)); }
    out.set("toggles", std::move(toggles));
    Json pl = Json::object();
    pl.set("halfWidth", kPlayerHalfWidth); pl.set("height", kPlayerHeight); pl.set("crouchHeight", kPlayerCrouchHeight);
    pl.set("eye", kEyeHeight); pl.set("crouchEye", kCrouchEyeHeight); pl.set("stepUp", moveParamBase(MV_STEP));
    out.set("player", std::move(pl));
    Json c = Json::object();   // (from the game's public headers: pmove.h, gamedefs.h)
    c.set("gravity", moveBaseGravity()); c.set("runSpeed", moveBaseSpeed()); c.set("jumpSpeed", moveBaseJump());
    c.set("standingJumpHeight", moveBaseJump() * moveBaseJump() / (2 * moveBaseGravity()));
    c.set("dashCharges", kDashCharges); c.set("maxDashCharges", kMaxDashCharges);
    c.set("dashCooldown", kDashCooldown); c.set("dashRecharge", kDashRecharge);
    c.set("grappleRange", kGrappleRange); c.set("hookCooldown", kHookCooldown);
    c.set("moveStep", kMoveStep); c.set("killPlaneY", -100.0f);
    c.set("buttons", [] { Json b = Json::object(); b.set("jump", (int)BTN_JUMP); b.set("crouch", (int)BTN_CROUCH);
                          b.set("dash", (int)BTN_DASH); b.set("hook", (int)BTN_GRAPPLE); return b; }());
    out.set("constants", std::move(c));
    out.set("expr", exprSyntaxJson());
    out.set("moveStep", kMoveStep);
    out.set("angles", "degrees; yaw 0 faces -z, -90 faces +x; pitch > 0 looks up");
    out.set("version", kQuarkVersion);
    out.set("ms", (maptoolNow() - t0) * 1000);
    return 0;
}

std::string cppFloat(float v) {
    char b[40];
    snprintf(b, sizeof b, "%.6g", v);
    std::string s = b;
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    return s + "f";
}

int runExport(const Json& req, Json& out, CmdCtx&) {
    CustomRules r;
    std::vector<std::string> clamped;
    std::string err;
    if (!requestPhysics(req, r, clamped, err)) return cmdError(out, err);
    if (req["name"].isStr()) { CustomRules t; t.fromText("custommode=" + req["name"].str() + "\n"); if (!t.name.empty()) r.name = t.name; }
    if (req["mode"].isStr() || req["mode"].isNum()) r.baseMode = modeFromJson(req["mode"], r.baseMode);
    // equations: the plain line of each one that works gets its value (static: resolved; dynamic: at rest), so a
    // version without equations plays a sensible fixed value; the game itself plays the expr.<key>= line
    MoveTuning et;
    mexpr::Program eprog;
    mexpr::Report erep;
    buildExprs(r, et, eprog, erep);
    std::string exprCpp;
    for (auto& it : erep.items) {
        const int s = mexpr::settingIndex(it.key);
        if (it.ok && s >= 0) {
            const float v = it.value;
            if (s == 0) r.gravity = v; else if (s == 1) r.moveSpeed = v; else if (s == 2) r.jump = v; else r.move[s - 3] = v;
        }
        if (!it.ok) exprCpp += "// broken (not played; the fixed value stays): " + it.key + " = " + it.text + "   // " + it.error + "\n";
        else if (it.dynamic) exprCpp += "// dynamic: " + it.key + " = " + it.text + "   // reads " + inputList(it.allInputs) + "; x" + mulStr(it.value) + " at rest\n";
        else exprCpp += "// static: " + it.key + " = " + it.text + "   // = x" + mulStr(it.value) + " (listed below as a constant)\n";
    }
    out = Json::object();
    out.set("qmode", r.toText());
    out.set("file", "custom_modes/" + r.name + ".qmode");
    std::string cpp = "// Physics set '" + r.name + "' as the game's standard movement (quark " + kQuarkVersion + ").\n"
                      "// Each line: file: constant = standard -> this set (standard x the multiplier).\n";
    if (!exprCpp.empty())
        cpp += "// Movement equations (expr.<key>= lines; src/game/moveexpr.h). A dynamic one has no single constant: as the game's\n"
               "// standard it would be code in pmove.cpp reading the player's state; the constant below is its value at rest.\n" + exprCpp;
    Json changes = Json::array();
    auto add = [&](int param, float mult, const char* what) {
        const ParamConsts* p = constsFor(param);
        if (!p || mult == 1.0f) return;
        for (auto& c : p->c) {
            if (!c.name) continue;
            std::string to;
            if (c.integer) {
                int v = std::min(kMaxDashCharges, std::max(1, (int)std::lround(c.base * mult)));
                to = std::to_string(v);
                cpp += std::string(c.file) + ": " + c.name + " = " + std::to_string((int)c.base) + " -> " + to + "   // " + what + " x" + mulStr(mult) + "\n";
            } else {
                to = cppFloat(c.base * mult);
                cpp += std::string(c.file) + ": " + c.name + " = " + cppFloat(c.base) + " -> " + to + "   // " + what + " x" + mulStr(mult) + "\n";
            }
            Json o = Json::object();
            const std::string wk = what;
            const size_t eq = wk.find(" (equation");
            o.set("key", wk.substr(0, eq)); o.set("multiplier", mult);
            if (eq != std::string::npos) o.set("equation", wk.find("at rest") != std::string::npos ? "dynamic" : "static");
            o.set("file", c.file); o.set("name", c.name);
            o.set("base", c.base); o.set("value", c.integer ? (float)std::atof(to.c_str()) : c.base * mult);
            changes.push(std::move(o));
        }
    };
    std::vector<std::string> labels;   // (an equation's constant says so: "friction (equation, at rest)")
    auto label = [&](const char* key) -> const char* {
        for (auto& it : erep.items)
            if (it.ok && it.key == key) { labels.push_back(std::string(key) + (it.dynamic ? " (equation, at rest)" : " (equation)")); return labels.back().c_str(); }
        return key;
    };
    labels.reserve(mexpr::kSettings + 1);
    add(-1, r.gravity, label("gravity"));
    add(-2, r.moveSpeed, label("speed"));
    add(-3, r.jump, label("jump"));
    // (the crouch speed is x speed and x crouch_speed in pmove: one line for both)
    add(MV_CROUCH_SPEED, r.move[MV_CROUCH_SPEED] * r.moveSpeed, r.moveSpeed != 1 ? "crouch_speed x speed" : "crouch_speed");
    for (int k = 0; k < MV_COUNT; k++) if (k != MV_CROUCH_SPEED) add(k, r.move[k], label(moveParamKey(k)));
    if (!r.dash) cpp += "// dash off: a custom mode rule, not a constant (pmove: mt.dash; ruleset modifier MOVE_RUN_ONLY does the same)\n";
    if (!r.grapple) cpp += "// grapple off: a custom mode rule, not a constant (pmove: mt.grapple)\n";
    if (changes.size() == 0 && r.dash && r.grapple && r.exprs.empty()) cpp += "// (no change: this is the standard movement)\n";
    else
        cpp += "// Also check the copies of these numbers outside pmove: src/game/bots.cpp (air strafe cap 2.3, gravity 22, run\n"
               "// speed 9.5 in its air strafe and gap jumps), tools/maptool/map/common.cpp playerJson (step 0.55, jump\n"
               "// 1.22 m, run 9.5), and the maps: tools/maps/*.py are built for a 1.22 m jump and a 0.55 m step.\n";
    out.set("cpp", cpp);
    out.set("changes", std::move(changes));
    if (!erep.items.empty()) { Json a = Json::array(); for (auto& it : erep.items) a.push(exprItemJson(it)); out.set("exprs", std::move(a)); }
    out.set("physics", physicsToJson(r));
    if (!clamped.empty()) { Json c = Json::array(); for (auto& k : clamped) c.push(k); out.set("clamped", std::move(c)); }
    return 0;
}

} // namespace

void buildExprs(const CustomRules& r, MoveTuning& t, mexpr::Program& prog, mexpr::Report& rep) {
    t = MoveTuning();   // (as customrules.cpp's rebuild: the plain values, then the equations over them)
    t.gravity = r.gravity; t.speed = r.moveSpeed; t.jump = r.jump; t.dash = r.dash; t.grapple = r.grapple;
    for (int k = 0; k < MV_COUNT; k++) t.m[k] = r.move[k];
    mexpr::build(r.exprs, t, prog, &rep);
}

std::string inputList(uint32_t bits) {
    std::string s;
    for (int i = 0; i < mexpr::P_COUNT; i++) if (bits & 1u << i) s += std::string(s.empty() ? "" : ", ") + "p." + mexpr::inputInfo(i).name;
    return s;
}

Json exprItemJson(const mexpr::ItemReport& it) {
    Json o = Json::object();
    o.set("key", it.key);
    o.set("text", it.text);
    o.set("ok", it.ok);
    if (!it.ok) o.set("error", it.error);
    o.set("dynamic", it.dynamic);
    Json deps = Json::array(), all = Json::array(), direct = Json::array();
    for (auto& d : it.deps) deps.push(d);
    for (int i = 0; i < mexpr::P_COUNT; i++) {
        if (it.allInputs & 1u << i) all.push(std::string("p.") + mexpr::inputInfo(i).name);
        if (it.inputs & 1u << i) direct.push(std::string("p.") + mexpr::inputInfo(i).name);
    }
    o.set("deps", std::move(deps));
    o.set("inputs", std::move(all));
    o.set("direct", std::move(direct));
    const int s = mexpr::settingIndex(it.key);
    o.set("value", it.value);   // (static: resolved; dynamic: at rest; an error: the fixed value)
    if (s >= 0) { o.set("absolute", it.value * mexpr::settingBase(s)); o.set("min", mexpr::settingMin(s)); o.set("max", mexpr::settingMax(s)); }
    return o;
}

bool isStandardMovement(const CustomRules& r) {
    if (!r.exprs.empty()) return false;
    if (r.gravity != 1 || r.moveSpeed != 1 || r.jump != 1 || !r.dash || !r.grapple) return false;
    for (int k = 0; k < MV_COUNT; k++) if (r.move[k] != 1) return false;
    return true;
}

bool physicsFromJson(const Json& j, CustomRules& out, std::vector<std::string>& clamped, std::string& err) {
    std::string text;
    if (j.isNull() || (j.isStr() && (j.str() == "standard" || j.str().empty()))) text = "";
    else if (j.isStr()) {
        if (j.str().find('=') == std::string::npos) { err = "physics '" + j.str() + "' isn't 'standard' or .qmode text"; return false; }
        text = j.str();
    } else if (j.isObj()) {
        if (j["qmode"].isStr()) text = j["qmode"].str();
    } else { err = "physics must be an object, 'standard' or .qmode text"; return false; }
    CustomRules base;
    base.fromText(text);   // (no "custommode=" line is fine: the values are still read)
    std::string name = base.name.empty() ? std::string("physics") : base.name;
    if (j.isObj() && j["name"].isStr() && !j["name"].str().empty()) name = j["name"].str();
    // the overrides as .qmode lines after the base's own text: read by fromText, so they are clamped as the game does
    std::string lines = base.toText() + "custommode=" + name + "\n";
    struct Want { std::string key; double v; };
    std::vector<Want> want;
    auto numField = [&](const Json& v, const std::string& key, const std::string& line) -> bool {
        if (v.isNull()) return true;
        if (!v.isNum()) { err = "physics." + key + " must be a number"; return false; }
        lines += line + "=" + num(v.num()) + "\n";
        want.push_back({key, v.num()});
        return true;
    };
    if (j.isObj()) {
        if (!numField(j["gravity"], "gravity", "gravity") || !numField(j["speed"], "speed", "speed") || !numField(j["jump"], "jump", "jump"))
            return false;
        for (const char* t : {"dash", "grapple"})
            if (!j[t].isNull()) {
                if (!j[t].isBool() && !j[t].isNum()) { err = std::string("physics.") + t + " must be true or false"; return false; }
                lines += std::string(t) + "=" + (j[t].boolean() ? "1" : "0") + "\n";
            }
        const Json& mv = j["move"];
        if (!mv.isNull() && !mv.isObj()) { err = "physics.move must be an object {key: multiplier}"; return false; }
        for (auto& kv : mv.members()) {
            bool known = false;
            for (int k = 0; k < MV_COUNT; k++) if (kv.first == moveParamKey(k)) known = true;
            if (!known) { err = "physics.move." + kv.first + " isn't a movement setting (physparams lists them)"; return false; }
            if (!numField(kv.second, "move." + kv.first, "move_" + kv.first)) return false;
        }
    }
    // "expr": {key: text}: movement equations over the base's (an empty text removes that one), as expr.<key>= lines
    std::vector<std::string> exprWant, exprDrop;
    if (j.isObj() && !j["expr"].isNull()) {
        const Json& ex = j["expr"];
        if (!ex.isObj()) { err = "physics.expr must be an object {key: \"expression\"}"; return false; }
        for (auto& kv : ex.members()) {
            if (mexpr::settingIndex(kv.first) < 0) { err = "physics.expr." + kv.first + " isn't a movement setting (physparams lists them)"; return false; }
            if (!kv.second.isStr()) { err = "physics.expr." + kv.first + " must be a string"; return false; }
            const std::string& t = kv.second.str();
            if (t.find_first_of("\r\n") != std::string::npos) { err = "physics.expr." + kv.first + " must be one line"; return false; }
            if (t.size() > (size_t)mexpr::kMaxText) { err = "physics.expr." + kv.first + " is longer than " + std::to_string(mexpr::kMaxText) + " characters"; return false; }
            if (t.find_first_not_of(" \t") == std::string::npos) { exprDrop.push_back(kv.first); continue; }
            lines += "expr." + kv.first + "=" + t + "\n";
            exprWant.push_back(kv.first);
        }
    }
    out = CustomRules();
    out.fromText(lines);
    if (out.name.empty()) out.name = "physics";
    for (auto& k : exprDrop)
        out.exprs.erase(std::remove_if(out.exprs.begin(), out.exprs.end(), [&](const std::pair<std::string, std::string>& e) { return e.first == k; }), out.exprs.end());
    auto hasExpr = [&](const std::string& k) { for (auto& e : out.exprs) if (e.first == k) return true; return false; };
    for (auto& k : exprWant) if (!hasExpr(k)) { err = "physics.expr." + k + " has a control character"; return false; }
    for (auto& w : want) {
        if (hasExpr(w.key.rfind("move.", 0) == 0 ? w.key.substr(5) : w.key)) continue;   // (an equation sets it: not a clamp)
        float got = w.key == "gravity" ? out.gravity : w.key == "speed" ? out.moveSpeed : w.key == "jump" ? out.jump : 1.0f;
        if (w.key.rfind("move.", 0) == 0)
            for (int k = 0; k < MV_COUNT; k++) if (w.key.compare(5, std::string::npos, moveParamKey(k)) == 0) got = out.move[k];
        if (got != (float)w.v) clamped.push_back(w.key);
    }
    return true;
}

bool requestPhysics(const Json& req, CustomRules& out, std::vector<std::string>& clamped, std::string& err) {
    if (!req["physics"].isNull()) return physicsFromJson(req["physics"], out, clamped, err);
    if (req["qmode"].isStr()) return physicsFromJson(req["qmode"], out, clamped, err);
    return physicsFromJson(Json(), out, clamped, err);
}

Json physicsToJson(const CustomRules& r) {
    Json o = Json::object();
    o.set("name", r.name);
    o.set("gravity", r.gravity); o.set("speed", r.moveSpeed); o.set("jump", r.jump);
    o.set("dash", r.dash); o.set("grapple", r.grapple);
    Json mv = Json::object(), changed = Json::array();
    for (int k = 0; k < MV_COUNT; k++) { mv.set(moveParamKey(k), r.move[k]); if (r.move[k] != 1) changed.push(moveParamKey(k)); }
    if (r.gravity != 1) changed.push("gravity");
    if (r.moveSpeed != 1) changed.push("speed");
    if (r.jump != 1) changed.push("jump");
    if (!r.dash) changed.push("dash");
    if (!r.grapple) changed.push("grapple");
    Json ex = Json::object(), items = Json::array();
    if (!r.exprs.empty()) {
        MoveTuning t;
        mexpr::Program prog;
        mexpr::Report rep;
        buildExprs(r, t, prog, rep);
        for (auto& it : rep.items) {
            ex.set(it.key, it.text);
            items.push(exprItemJson(it));
            bool listed = false;
            for (size_t k = 0; k < changed.size(); k++) listed |= changed[k].str() == it.key;
            if (!listed) changed.push(it.key);
        }
    }
    o.set("expr", std::move(ex));
    o.set("exprs", std::move(items));
    o.set("move", std::move(mv));
    o.set("changed", std::move(changed));
    o.set("standard", isStandardMovement(r));
    return o;
}

MAPTOOL_COMMAND("physparams", runParams, "physics editor: every movement setting {key,label,unit,base,group,min,max,...}, the player, constants");
MAPTOOL_COMMAND("physexport", runExport, "physics editor: {physics|qmode, name?, mode?} -> {qmode, cpp, changes}");

} // namespace phys
