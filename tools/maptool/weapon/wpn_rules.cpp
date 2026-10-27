// The weapons editor's request rules (weapons / qmode / weaponsAllowed), the per weapon tally sim / simbatch report,
// and the wpnparams / wpncheck commands (README.md here).
#include <algorithm>
#include <cctype>
#include <cmath>
#include "maptool.h"
#include "wpn.h"
#include "game/server.h"
#include "game/weaponset.h"

namespace wpn {
namespace {

std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string textOf(const Json& j) { return j.isStr() ? j.str() : j.isObj() && j["text"].isStr() ? j["text"].str() : std::string(); }
std::string normText(const std::string& t) {   // (one form everywhere, as resolveWeaponSet: \n line ends, the last ended)
    std::string n;
    n.reserve(t.size() + 1);
    for (char c : t) if (c != '\r') n += c;
    if (!n.empty() && n.back() != '\n') n += '\n';
    return n;
}
std::string firstError(const WsDiag& d) {
    if (d.errors.empty()) return "the weapon set doesn't compile";
    const WsDiag::Msg& m = d.errors[0];
    return "weapons line " + std::to_string(m.line) + ":" + std::to_string(m.col) + ": " + m.msg;
}
Json diagMsgs(const std::vector<WsDiag::Msg>& v) {
    Json a = Json::array();
    for (auto& m : v) { Json o = Json::object(); o.set("line", m.line); o.set("col", m.col); o.set("msg", m.msg); a.push(std::move(o)); }
    return a;
}

}  // namespace

int weaponFromName(const std::string& s0) {
    std::string s = lower(s0);
    if (s.empty()) return -1;
    int k = weaponFromKey(s);
    if (k >= 0) return k;
    for (int w = 0; w < WPN_COUNT; w++)
        if (lower(weaponDef(w).name) == s || lower(baseWeaponDef(w).name) == s) return w;
    return -1;
}

uint16_t maskFromJson(const Json& j, std::string& err) {
    if (j.isNull()) return 0;
    if (j.isNum()) return (uint16_t)(j.i() & ((1 << WPN_COUNT) - 1));
    if (j.isStr()) {
        if (lower(j.str()) == "all" || j.str().empty()) return 0;
        int w = weaponFromName(j.str());
        if (w < 0) { err = "weaponsAllowed: no weapon '" + j.str() + "'"; return 0; }
        return (uint16_t)(1u << w);
    }
    uint16_t m = 0;
    for (auto& x : j.items()) {
        int w = x.isNum() ? x.i() : weaponFromName(x.str());
        if (w < 0 || w >= WPN_COUNT) { err = "weaponsAllowed: no weapon '" + (x.isStr() ? x.str() : x.dump()) + "'"; return 0; }
        m |= (uint16_t)(1u << w);
    }
    return m;
}

Json maskJson(uint16_t mask) {
    if (!mask) return Json();
    Json a = Json::array();
    for (int w = 0; w < WPN_COUNT; w++) if (mask >> w & 1) a.push(weaponKey(w));
    return a;
}

bool requestRules(const Json& req, Rules& out, std::string& err) {
    out = Rules();
    const std::string qmode = textOf(req["qmode"]);
    const std::string weapons = textOf(req["weapons"]);
    const bool hasMask = !req["weaponsAllowed"].isNull();
    if (qmode.empty() && weapons.empty() && !hasMask) return true;
    out.any = true;
    CustomRules& r = out.rules;
    if (!qmode.empty()) {
        if (!r.fromText(qmode)) {   // (a mode text without its custommode= line: named here)
            if (!r.fromText("custommode=wpn-editor\n" + qmode)) { err = "qmode isn't a custom mode text (key=value lines)"; return false; }
        }
        out.qmodeName = r.name;
        r.maps.clear();   // (a pool would move the match to another map: the request's map plays)
        if (weapons.empty() && !r.weaponSetName.empty()) {
            std::string why;
            if (!resolveWeaponSet(r, &why) && !why.empty()) out.warnings.push_back(why);
        }
    } else r.name = "wpn-editor";
    if (!weapons.empty()) {
        std::string text = normText(weapons);
        if (text.size() > kMaxWeaponSetBytes) { err = "weapons: the set is " + std::to_string(text.size()) + " bytes (at most " + std::to_string(kMaxWeaponSetBytes) + ")"; return false; }
        WsDiag d;
        auto set = compileWeaponSet(text, &d);
        if (!set) { err = firstError(d); return false; }
        for (auto& m : d.warnings) out.warnings.push_back("weapons line " + std::to_string(m.line) + ":" + std::to_string(m.col) + ": " + m.msg);
        r.weaponSetText = text;
        r.weaponSetName = safeWeaponSetName(weaponSetName(*set));
        if (r.weaponSetName.empty()) r.weaponSetName = "edited";
    }
    if (!r.weaponSetText.empty()) {
        WsDiag d;
        auto set = compileWeaponSet(r.weaponSetText, &d);
        if (set) { out.setName = weaponSetName(*set); if (out.setName.empty()) out.setName = r.weaponSetName; out.hasLogic = weaponSetHasLogic(*set); }
        else r.weaponSetText.clear();
    }
    if (hasMask) {
        uint16_t m = maskFromJson(req["weaponsAllowed"], err);
        if (!err.empty()) return false;
        r.weapons = m;
    }
    out.allowed = r.weapons;
    return true;
}

void applyToServer(Server& s, const Rules& r) {
    if (!r.any) return;
    s.cfg_.custom = r.rules;
    if (r.rules.roundsToWin) s.cfg_.roundsToWin = r.rules.roundsToWin;
    setActiveRules(s.cfg_.custom);   // (this thread: the next step would; the fix-ups below read the rules)
    for (int i = 0; i < kMaxPlayers; i++) {
        ServerPlayer& p = s.players_[i];
        if (!p.connected) continue;
        if (!s.weaponAllowed(p.selectedWeapon))
            for (int k = 0; k < WPN_COUNT; k++) if (s.weaponAllowed(kWeaponSlots[k])) { p.selectedWeapon = kWeaponSlots[k]; break; }
        if (!p.alive) continue;
        if (!s.weaponAllowed(p.weapon)) p.weapon = p.selectedWeapon;
        p.health = s.lifeMaxHealth();   // (spawned before the rules: a custom health starts now)
    }
}

void startJson(Json& line, const Rules& r) {
    line.set("weaponSet", r.setName.empty() ? Json() : Json(r.setName));
    line.set("weaponLogic", r.hasLogic);
    line.set("weaponsAllowed", maskJson(r.allowed));
    line.set("qmode", r.qmodeName.empty() ? Json() : Json(r.qmodeName));
    if (!r.warnings.empty()) { Json w = Json::array(); for (auto& x : r.warnings) w.push(x); line.set("weaponWarnings", w); }
}

// ---- the tally --------------------------------------------------------------------------------------------------
constexpr double kEngagementGap = 3.0;   // s without damage: the next damage starts a new fight (time to kill counts from it)
void Tally::kill(int killer, int victim, const std::string& weaponName, double t) {
    int w = weaponFromName(weaponName);
    if (w < 0 || killer < 0 || killer == victim) return;
    W& x = this->w[w];
    x.kills++;
    if (victim >= 0 && victim < kMaxPlayers) {
        const P& v = p[victim];
        x.ttkSum += v.firstDmg >= 0 ? std::max(0.0, t - v.firstDmg) : 0.0;   // (first damage and the kill in one sample: 0)
        x.ttkN++;
    }
}

void Tally::observe(const Server& s, double t) {
    const double dt = std::max(0.0, t - lastT);
    lastT = t;
    for (int i = 0; i < kMaxPlayers; i++) {
        const ServerPlayer& q = s.players_[i];
        P& m = p[i];
        if (!q.connected) { m = P(); continue; }
        if (!m.seen) { m.seen = true; m.weapon = q.weapon; m.shots = q.shotsFired; m.hits = q.shotsHit; m.dmg = q.dmgDealt; m.alive = q.alive; m.health = q.health; continue; }
        const int w = m.weapon;
        if (w >= 0 && w < WPN_COUNT) {
            W& x = this->w[w];
            // (counters only grow; a match restart that zeroes them is taken as a new start)
            if (q.shotsFired >= m.shots) x.shots += q.shotsFired - m.shots;
            if (q.shotsHit >= m.hits) x.hits += q.shotsHit - m.hits;
            if (q.dmgDealt >= m.dmg) x.damage += q.dmgDealt - m.dmg;
            if (m.alive) x.held += dt;
        }
        if (m.alive && !q.alive && w >= 0 && w < WPN_COUNT) this->w[w].deaths++;   // (died holding it)
        // the fight's start: the first damage after kEngagementGap s without any (or since the spawn)
        if (!q.alive) m.firstDmg = -1;
        else if (!m.alive) { m.firstDmg = q.health < s.lifeMaxHealth() - 1e-3f ? t : -1; m.lastDmg = m.firstDmg; }   // (respawned)
        else if (q.health < m.health - 1e-4f) { if (m.firstDmg < 0 || t - m.lastDmg > kEngagementGap) m.firstDmg = t; m.lastDmg = t; }
        m.weapon = q.weapon; m.shots = q.shotsFired; m.hits = q.shotsHit; m.dmg = q.dmgDealt; m.alive = q.alive; m.health = q.health;
    }
}

void Tally::merge(const Tally& o) {
    for (int k = 0; k < WPN_COUNT; k++) {
        W& a = w[k]; const W& b = o.w[k];
        a.kills += b.kills; a.shots += b.shots; a.hits += b.hits; a.deaths += b.deaths; a.ttkN += b.ttkN;
        a.damage += b.damage; a.ttkSum += b.ttkSum; a.held += b.held;
    }
}

Json Tally::json() const {
    Json a = Json::array();
    for (int k = 0; k < WPN_COUNT; k++) {
        const W& x = w[k];
        if (!x.kills && !x.shots && !x.hits && x.damage <= 0 && x.held <= 0) continue;
        Json o = Json::object();
        o.set("weapon", weaponKey(k));
        o.set("kills", x.kills); o.set("damage", std::round(x.damage * 10.0) / 10.0);
        o.set("shots", x.shots); o.set("hits", x.hits);
        o.set("accuracy", x.shots ? (double)x.hits / x.shots : Json());
        o.set("ttk", x.ttkN ? Json(x.ttkSum / x.ttkN) : Json()); o.set("ttkSamples", x.ttkN);
        o.set("deaths", x.deaths); o.set("heldSeconds", std::round(x.held * 100.0) / 100.0);
        o.set("dps", x.held > 0 ? Json(x.damage / x.held) : Json());
        a.push(std::move(o));
    }
    return a;
}

// ---- wpnparams / wpncheck ---------------------------------------------------------------------------------------
namespace {

// The set of a request (`weapons`, or the standard set): compiled, or an error answer.
struct ReqSet { std::shared_ptr<const WeaponSet> set; WsDiag diag; std::string text; bool given = false; };
bool requestSet(const Json& req, ReqSet& rs, std::string& err, const char* key = "weapons") {
    rs.text = textOf(req[key]);
    rs.given = !rs.text.empty();
    if (!rs.given) rs.text = standardWeaponSetText();
    rs.text = normText(rs.text);
    rs.set = compileWeaponSet(rs.text, &rs.diag);
    if (!rs.set) { err = firstError(rs.diag); return false; }
    return true;
}

Json valuesJson(const WeaponValues& v) {
    Json o;
    std::string e;
    Json::parse(weaponValuesJson(v), o, &e);
    return o;
}

int runParams(const Json& req, Json& out, CmdCtx&) {
    const double t0 = maptoolNow();
    std::string err;
    Json cat;
    if (!Json::parse(exportWeaponsJson(), cat, &err)) return cmdError(out, "the catalog isn't JSON: " + err);
    out = std::move(cat);
    ReqSet rs;
    bool ok = requestSet(req, rs, err);
    const WeaponValues& std_ = standardWeaponValues();
    const WeaponValues& v = ok ? weaponSetValues(*rs.set) : std_;
    out.set("values", valuesJson(v));
    Json changed = Json::array(), writes = Json::array();
    for (int i = 0; i < weaponFieldCount(); i++) {
        const WeaponFieldInfo& f = weaponField(i);
        if (getWeaponField(v, i) != getWeaponField(std_, i)) changed.push(f.key);
        if (ok && rs.given && weaponSetWrites(*rs.set, i)) writes.push(f.key);
    }
    out.set("changed", std::move(changed));
    out.set("writes", std::move(writes));
    Json labels = Json::object();
    if (ok) for (int w = 0; w < WPN_COUNT; w++) if (!weaponSetLabel(*rs.set, w).empty()) labels.set(weaponKey(w), weaponSetLabel(*rs.set, w));
    out.set("labels", std::move(labels));
    out.set("name", ok && rs.given ? Json(weaponSetName(*rs.set)) : Json());
    out.set("hasLogic", ok && weaponSetHasLogic(*rs.set));
    out.set("ok", ok);
    out.set("errors", diagMsgs(rs.diag.errors));
    out.set("warnings", diagMsgs(rs.diag.warnings));
    Json feats = Json::array();
    for (const char* f : {"params", "check", "stats", "range", "fight"}) feats.push(f);
    out.set("features", std::move(feats));
    out.set("ms", (maptoolNow() - t0) * 1000.0);
    return 0;
}

int runCheck(const Json& req, Json& out, CmdCtx&) {
    const double t0 = maptoolNow();
    std::string text = textOf(req["weapons"]);
    if (text.empty()) text = textOf(req["text"]);
    text = normText(text);
    WsDiag d;
    auto set = compileWeaponSet(text, &d);
    out = Json::object();
    out.set("ok", set != nullptr && d.errors.empty());
    out.set("errors", diagMsgs(d.errors));
    out.set("warnings", diagMsgs(d.warnings));
    if (set) {
        out.set("name", weaponSetName(*set));
        out.set("hasLogic", weaponSetHasLogic(*set));
        Json w = Json::array();
        for (int i = 0; i < weaponFieldCount(); i++) if (weaponSetWrites(*set, i)) w.push(weaponField(i).key);
        out.set("writes", std::move(w));
    }
    if (!req["ast"].isBool() || req["ast"].boolean()) {
        WsDiag pd;
        std::string ast = weaponSetAstJson(text, &pd);
        Json a;
        std::string e;
        if (!ast.empty() && Json::parse(ast, a, &e)) out.set("ast", std::move(a));
    }
    if (req["canonical"].boolean(false)) {
        WsDiag pd;
        std::string c = printWeaponSetText(text, &pd);
        if (!c.empty()) out.set("canonical", c);
    }
    out.set("ms", (maptoolNow() - t0) * 1000.0);
    return 0;
}

}  // namespace
}  // namespace wpn

MAPTOOL_COMMAND("wpnparams", wpn::runParams, "{weapons?} -> the weapon catalog (quark_tests exportweapons) + values {key: value}, changed [keys], writes, labels, ok, errors, warnings (weapon_editor/native/README.md)");
MAPTOOL_COMMAND("wpncheck", wpn::runCheck, "{weapons, ast?, canonical?} -> {ok, errors: [{line, col, msg}], warnings, name, hasLogic, writes, ast}: a .qweapons text checked by the game's compiler");
