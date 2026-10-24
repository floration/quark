// wpnstats: the Values table's numbers from a weapon set's values, as rows the page's stats.js makes (same ids, same
// columns, same arithmetic), read from the game's compiled set: the tuned values every machine plays with, the falloff
// fields or the set's `<weapon>.falloff` curve evaluated by the game (evalWeaponCurve), the shotgun's pellets in the
// game's fixed pattern (pelletOffset) against the game's hit box and head box. Plus `gameRows`: the game's own
// computeWeaponStats (weaponset.cpp), a simpler table (every pellet lands, no distance).
#include <algorithm>
#include <cmath>
#include <functional>
#include "maptool.h"
#include "wpn.h"
#include "game/pmove.h"
#include "game/weaponset.h"

namespace wpn {
namespace {

constexpr double kInf = 1e300;
// numbers out: 7 significant digits (the set's f32 values read as written: 9.9, not 9.899999618530273); inf: null
double sig(double x) { if (!std::isfinite(x) || x == 0) return x; char b[40]; snprintf(b, sizeof b, "%.7g", x); return atof(b); }
Json num(double x) { return std::isfinite(x) && x < 1e299 ? Json(sig(x)) : Json(); }
std::string fmt(double x) {   // stats.js fmt: 3 significant-ish digits, no trailing zeros
    double a = std::fabs(x);
    char b[48];
    snprintf(b, sizeof b, a >= 100 ? "%.0f" : a >= 10 ? "%.1f" : a >= 1 ? "%.2f" : "%.3f", x);
    std::string s = b;
    if (s.find('.') != std::string::npos) { while (!s.empty() && s.back() == '0') s.pop_back(); if (!s.empty() && s.back() == '.') s.pop_back(); }
    return s.empty() || s == "-0" ? "0" : s;
}

// damage x at d metres: the set's curve, or the falloff fields (the game's weaponFalloff, without its "has falloff" flag)
double falloffAt(const WeaponExtra& e, double d) {
    if (e.curveKind) { float y = evalWeaponCurve(e, (float)d); return std::isfinite(y) ? std::max(0.0f, y) : 1.0; }
    if (e.falloffMin == 1.0f) return 1;
    if (d <= e.falloffStart) return 1;
    if (e.falloffEnd <= e.falloffStart || d >= e.falloffEnd) return e.falloffMin;
    return 1.0 + (e.falloffMin - 1.0) * ((d - e.falloffStart) / (e.falloffEnd - e.falloffStart));
}

// pellets of one shot landing on the body / the head of a player d m away, aimed at the body's centre or the head's
struct PH { int body = 0, head = 0; };
PH pelletHits(int pellets, float spread, double d, bool aimHead) {
    const double H = kPlayerHeight, hw = kPlayerHalfWidth, headH = H * kHeadFraction, headHW = hw * kHeadWidthFraction;
    const double aimY = aimHead ? H - headH / 2 : H / 2;
    PH r;
    for (int k = 0; k < pellets; k++) {
        float a, rr;
        pelletOffset(k, pellets, spread, a, rr);
        double off = std::tan(rr) * std::max(0.0, d);
        double x = std::cos(a) * off, y = aimY + std::sin(a) * off;
        bool inHead = std::fabs(x) <= headHW && y >= H - headH && y <= H;
        bool inBody = std::fabs(x) <= hw && y >= 0 && y <= H;
        if (aimHead && inHead) r.head++;
        else if (inBody) r.body++;
    }
    return r;
}

int bubbleCount(const WeaponAlt& a, double dist) {
    double f = a.streamFar > a.streamNear ? (dist - a.streamNear) / (a.streamFar - a.streamNear) : dist >= a.streamFar ? 1 : 0;
    f = std::clamp(f, 0.0, 1.0);
    return a.streamMin + (int)std::floor(f * (a.streamMax - a.streamMin) + 0.5);
}

struct RowDef { int w; const char* mode; const char* variant; const char* suffix; };
const RowDef kRows[] = {
    {WPN_RIFLE, "primary", "", ""}, {WPN_RIFLE, "alt", "", "scoped"},
    {WPN_ROCKET, "primary", "direct", "direct hit"}, {WPN_ROCKET, "primary", "splash", "splash only"},
    {WPN_ROCKET, "alt", "direct", "lob · direct hit"}, {WPN_ROCKET, "alt", "splash", "lob · splash only"},
    {WPN_KNIFE, "primary", "", "swing"}, {WPN_KNIFE, "alt", "", "throw"},
    {WPN_SNIPER, "primary", "", ""},
    {WPN_LASER, "primary", "", "beam"}, {WPN_LASER, "alt", "", "pull beam"},
    {WPN_SHOTGUN, "primary", "", ""}, {WPN_SHOTGUN, "alt", "", "bounce (after a bounce)"},
    {WPN_STICKY, "primary", "", "blast centre"},
    {WPN_CROSSBOW, "primary", "", ""},
    {WPN_BUBBLE, "primary", "", ""}, {WPN_BUBBLE, "alt", "", "stream"},
    {WPN_SYRINGE, "primary", "", "self heal"}, {WPN_SYRINGE, "alt", "", "throw (heal a teammate)"},
    {WPN_LOCATOR, "primary", "", ""},
};
const char* kindOf(int w) {
    switch (w) {
        case WPN_RIFLE: case WPN_SNIPER: case WPN_SHOTGUN: return "hitscan";
        case WPN_LASER: return "beam";
        case WPN_KNIFE: return "melee";
        case WPN_SYRINGE: case WPN_LOCATOR: return "support";
        default: return "projectile";
    }
}
const double kPerAt[] = {0, 1, 2, 3, 5, 7.5, 10, 12.5, 15, 20, 25, 30, 35, 40, 50, 60, 70, 80, 100};

Json row(const RowDef& R, const WeaponValues& V, const WeaponSet* set, double H, double D) {
    const int w = R.w;
    const bool alt = std::string(R.mode) == "alt";
    const std::string variant = R.variant;
    const WeaponDef& def = V.def[w];
    const WeaponExtra& ex = V.ex[w];
    const WeaponAlt& A = V.alt;
    std::string kind = kindOf(w);
    Json notes = Json::array();
    auto fall = [&](double d) { return falloffAt(ex, d); };
    double dmg = def.damage, head = ex.headDamage, refire = std::max(0.001f, def.refire);
    int pellets = std::max(1, def.pellets);
    double range = def.range, speed = def.projSpeed, life = def.life, gravity = def.gravity, spread = def.spread, splash = def.splashRadius, direct = ex.directDamage;
    bool headshots = head > 0;
    if (!headshots) head = dmg;
    std::function<double(double, bool)> per = [](double, bool) { return 0.0; };
    double extraTtk = 0, reach = 0, heal = -1;
    bool support = false, beam = false;
    if (w == WPN_LOCATOR) { support = true; notes.push("no damage: shows a hider / runner for a moment"); }
    else if (w == WPN_SYRINGE) {
        support = true;
        if (!alt) { heal = dmg; notes.push("heals you " + fmt(dmg) + " hp a use (" + fmt(dmg / refire) + " hp/s)"); }
        else {
            heal = A.syringeHeal; refire = std::max<double>(refire, A.syringeAway); speed = A.syringeThrowSpeed; gravity = A.syringeThrowGravity; life = A.syringeThrowLife;
            notes.push("a teammate it hits gets " + fmt(heal) + " hp and every cooldown reset; away " + fmt(A.syringeAway) + " s");
            kind = "projectile";
        }
    } else if (w == WPN_LASER) {
        if (alt) { dmg *= A.pullDamage; head *= A.pullDamage; notes.push("pulls x" + fmt(A.pullScale) + " harder for x" + fmt(A.pullDamage) + " damage"); }
        per = [=](double d, bool h) { return d > range ? 0.0 : (h ? head : dmg) * fall(d); };
        beam = true;
        notes.push("a damage tick every " + fmt(refire * 1000) + " ms");
    } else if (kind == "hitscan") {
        if (alt && w == WPN_RIFLE) { refire *= A.scopedRefire; spread = std::max(0.0f, def.scopedSpread); notes.push("scoped: x" + fmt(A.scopedRefire) + " refire"); }
        double mult = 1;
        if (alt && w == WPN_SHOTGUN) { mult = A.bounceDamage; notes.push("pellets bounce " + std::to_string(A.bounces) + "x; a bounced pellet does x" + fmt(mult) + " (direct pellets: full)"); }
        if (pellets > 1) per = [=](double d, bool h) { if (d > range) return 0.0; PH p = pelletHits(pellets, (float)spread, d, h); return (p.body * dmg + p.head * head) * mult * fall(d); };
        else per = [=](double d, bool h) { return d > range ? 0.0 : (h ? head : dmg) * mult * fall(d); };
    } else if (w == WPN_KNIFE && !alt) {
        per = [=](double d, bool h) { return d > range ? 0.0 : (h ? head : dmg) * fall(d); };
        notes.push("reach " + fmt(range) + " m; hit boxes +" + fmt(A.knifeGrow) + " m");
    } else {
        if (w == WPN_ROCKET && alt) { speed *= A.lobSpeed; gravity = A.lobGravity; }
        if (w == WPN_KNIFE) { speed = A.knifeThrowSpeed; gravity = A.knifeThrowGravity; life = A.knifeThrowLife; refire = std::max<double>(refire, A.knifeAway); kind = "projectile";
            notes.push("one throw, then the knife is away " + fmt(A.knifeAway) + " s"); }
        if (w == WPN_STICKY) { extraTtk = A.stickyArmTime; notes.push("arms in " + fmt(extraTtk) + " s; " + std::to_string(A.stickyMax) + " out at once"); }
        reach = speed * life;
        double base = dmg, baseHead = head;
        if (splash > 0) {
            if (variant == "splash") { base = dmg; baseHead = dmg; headshots = false; notes.push("the blast's centre; 0 at " + fmt(splash) + " m"); }
            else if (direct > 0) { base = std::max(direct, dmg); baseHead = headshots ? std::max(direct, head) : base; notes.push("direct hit: at least " + fmt(direct)); }
        }
        if (w == WPN_BUBBLE && alt) {
            refire = A.streamRefire;
            double fastest = std::max(A.streamSlow, A.streamFast), lf = life;
            per = [=, &A](double d, bool) { return d > fastest * lf ? 0.0 : bubbleCount(A, d) * base * fall(d); };
            notes.push(std::to_string(bubbleCount(A, 0)) + " bubbles within " + fmt(A.streamNear) + " m, " + std::to_string(A.streamMax) + " from " + fmt(A.streamFar) + " m; " +
                       fmt(A.streamSlow) + "-" + fmt(fastest) + " m/s");
            speed = fastest;
        } else {
            double rc = reach;
            per = [=](double d, bool h) { return rc > 0 && d > rc ? 0.0 : (h ? baseHead : base) * fall(d); };
        }
        if (gravity) notes.push("drops " + fmt(gravity) + " m/s²");
    }
    Json r = Json::object();
    std::string id = std::string(weaponKey(w)) + ":" + R.mode + (variant.empty() ? "" : ":" + variant);
    r.set("id", id); r.set("weapon", weaponKey(w)); r.set("mode", R.mode); r.set("variant", variant); r.set("kind", kind);
    std::string title = baseWeaponDef(w).name;
    if (set && !weaponSetLabel(*set, w).empty()) title += " (" + weaponSetLabel(*set, w) + ")";
    r.set("label", title + (*R.suffix ? std::string(" · ") + R.suffix : alt ? std::string(" · alt") : std::string()));
    r.set("headshots", headshots);
    if (support) r.set("support", true);
    if (beam) r.set("beam", true);
    if (heal >= 0) r.set("heal", num(heal));
    const bool reachKind = kind == "hitscan" || kind == "beam" || kind == "melee";
    const double rangeOut = reachKind ? range : reach;
    const double speedOut = kind == "projectile" ? speed : 0;
    const double splashOut = kind == "projectile" && w != WPN_KNIFE && w != WPN_SYRINGE ? splash : 0;
    r.set("refire", num(refire)); r.set("pellets", pellets); r.set("range", num(rangeOut)); r.set("speed", num(speedOut));
    r.set("reach", num(reach));
    r.set("splashRadius", num(splashOut)); r.set("splashMax", num(splashOut > 0 ? dmg : 0.0));
    double body = support ? 0 : per(D, false), hd = support ? 0 : per(D, true);
    auto stk = [&](double x) { return x > 1e-9 ? std::ceil(H / x - 1e-9) : kInf; };
    double sb = support ? kInf : stk(body), sh = support ? kInf : stk(hd);
    auto ttk = [&](double n) { return n < kInf ? (n - 1) * refire + extraTtk : kInf; };
    r.set("body", num(body)); r.set("head", num(hd)); r.set("burst", num(body));
    r.set("stkBody", num(sb)); r.set("stkHead", num(sh)); r.set("ttkBody", num(ttk(sb))); r.set("ttkHead", num(ttk(sh)));
    r.set("dpsBody", num(body / refire)); r.set("dpsHead", num(hd / refire));
    for (double d : {10.0, 20.0, 40.0}) {
        std::string k = std::to_string((int)d);
        Json t = kind == "projectile" && speed > 0 ? (reach > 0 && d > reach ? Json() : num(d / speed)) : kind == "support" ? Json() : Json(0.0);
        r.set("t" + k, t);
        r.set("fall" + k, num(fall(d)));
        r.set("at" + k, num(support ? 0.0 : per(d, false)));
    }
    Json pa = Json::array();
    if (!support)
        for (double d : kPerAt) { Json o = Json::object(); o.set("d", num(d)); o.set("body", num(per(d, false))); o.set("head", num(per(d, true))); pa.push(std::move(o)); }
    r.set("per", std::move(pa));
    r.set("notes", std::move(notes));
    return r;
}

}  // namespace

Json statsRows(const WeaponValues& v, const WeaponSet* set, float health, float dist) {
    Json rows = Json::array();
    const double H = health > 0 ? health : kMaxHealth, D = std::max(0.0f, dist);
    for (const RowDef& R : kRows) rows.push(row(R, v, set, H, D));
    return rows;
}

namespace {

int runStats(const Json& req, Json& out, CmdCtx&) {
    const double t0 = maptoolNow();
    const float health = (float)std::clamp(req["health"].num(kMaxHealth), 1.0, 100000.0);
    const float dist = (float)std::clamp(req["dist"].num(0), 0.0, 10000.0);
    auto one = [&](const Json& tj, Json& o, std::string& err) -> bool {
        std::string text = tj.isStr() ? tj.str() : tj.isObj() && tj["text"].isStr() ? tj["text"].str() : std::string();
        if (text.empty()) text = standardWeaponSetText();
        WsDiag d;
        auto set = compileWeaponSet(text, &d);
        if (!set) {
            const WsDiag::Msg m = d.errors.empty() ? WsDiag::Msg{} : d.errors[0];
            err = "weapons line " + std::to_string(m.line) + ":" + std::to_string(m.col) + ": " + m.msg;
            return false;
        }
        o = Json::object();
        o.set("name", weaponSetName(*set));
        o.set("rows", statsRows(weaponSetValues(*set), set.get(), health, dist));
        Json g;
        std::string e;
        if (Json::parse(weaponStatsJson(weaponSetValues(*set), health), g, &e)) o.set("gameRows", std::move(g));
        return true;
    };
    std::string err;
    Json main;
    if (!one(req["weapons"], main, err)) return cmdError(out, err);
    out = std::move(main);
    Json cmp = Json::array();
    for (auto& c : req["compare"].items()) {
        Json o;
        if (!one(c, o, err)) return cmdError(out, "compare: " + err);
        cmp.push(std::move(o));
    }
    out.set("compare", std::move(cmp));
    out.set("health", health);
    out.set("dist", dist);
    out.set("ms", (maptoolNow() - t0) * 1000.0);
    return 0;
}

}  // namespace
}  // namespace wpn

MAPTOOL_COMMAND("wpnstats", wpn::runStats, "{weapons?, compare?: [text], health?, dist?} -> {rows: [row], compare: [{rows}], gameRows}: per weapon and fire mode damage / shots / time to kill / DPS / range / falloff (the page's stats.js rows)");
