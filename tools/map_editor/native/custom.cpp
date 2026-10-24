// custom: a list of the owner's own tests on one map (kinds below; shapes in README.md and in `help`'s customKinds).
#include <algorithm>
#include <cmath>
#include <cstring>
#include "analysis.h"
#include "maptool.h"
#include "game/gamedefs.h"
#include "game/gamemodes.h"
#include "game/pmove.h"

namespace {
constexpr float kRun = 9.5f;   // pmove kGroundSpeed: metres walked -> seconds

// A place a test names: a point [x,y,z], {"spawn": i}, {"team": "a"} (every spawn of that team: the test holds for
// each), {"objective": "hill" | "site 1" | "point A" | "checkpoint 2" | "cart" | "cart start" | "cart end"} or
// {"box": {"min","max"}}. Objectives and boxes are areas: a route ends on any floor in them.
struct Loc {
    std::vector<Vec3> pts;    // one or more points (a team: each of its spawns)
    std::vector<int> spawn;   // the spawn index of each point (-1: not a spawn)
    bool isBox = false;
    AABB box;
    std::string label;
    std::string err;
};

std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

Loc resolve(const Map& m, const Json& j) {
    Loc L;
    char b[96];
    if (j.isArr()) {
        L.pts.push_back(toVec(j)); L.spawn.push_back(-1);
        snprintf(b, sizeof b, "%.1f %.1f %.1f", L.pts[0].x, L.pts[0].y, L.pts[0].z); L.label = b;
    } else if (j["spawn"].isNum()) {
        int i = j["spawn"].i();
        if (i < 0 || i >= (int)m.spawns.size()) { L.err = "no spawn " + std::to_string(i); return L; }
        L.pts.push_back(m.spawns[i].pos); L.spawn.push_back(i); L.label = "spawn " + std::to_string(i);
    } else if (j["team"].isStr()) {
        int t = spawnTeamFromName(j["team"].str());
        if (t < 0) { L.err = "no spawn team '" + j["team"].str() + "'"; return L; }
        for (int i = 0; i < (int)m.spawns.size(); i++) if (m.spawns[i].team == t) { L.pts.push_back(m.spawns[i].pos); L.spawn.push_back(i); }
        if (L.pts.empty()) { L.err = "no " + j["team"].str() + " spawns"; return L; }
        L.label = "team " + j["team"].str() + " spawns";
    } else if (j["objective"].isStr()) {
        std::string o = lower(j["objective"].str());
        L.label = o;
        auto num = [&](const char* prefix, size_t count) -> int {   // "site 2" / "point B" / "point 2" -> index
            size_t n = strlen(prefix);
            if (o.compare(0, n, prefix) != 0) return -2;
            std::string rest = o.substr(n);
            while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            int k = -1;
            if (rest.size() == 1 && rest[0] >= 'a' && rest[0] <= 'z') k = rest[0] - 'a';
            else if (!rest.empty()) k = atoi(rest.c_str()) - 1;
            return k >= 0 && k < (int)count ? k : -1;
        };
        int k;
        if (o == "hill") { if (!m.hasHill) { L.err = "the map has no hill"; return L; } L.isBox = true; L.box = m.hill; }
        else if ((k = num("bomb site", m.bombSites.size())) != -2 || (k = num("site", m.bombSites.size())) != -2) { if (k < 0) { L.err = "no such bomb site"; return L; } L.isBox = true; L.box = m.bombSites[k]; }
        else if ((k = num("point", m.points.size())) != -2) { if (k < 0) { L.err = "no such capture point"; return L; } L.isBox = true; L.box = m.points[k]; }
        else if ((k = num("checkpoint", m.checkpoints.size())) != -2) { if (k < 0) { L.err = "no such checkpoint"; return L; } L.isBox = true; L.box = m.checkpoints[k]; }
        else if (o.compare(0, 4, "cart") == 0) {
            if (m.payloadPath.size() < 2) { L.err = "the map has no payload track"; return L; }
            float len = m.payloadLength();
            L.pts.push_back(m.payloadPoint(o == "cart start" ? 0 : o == "cart end" ? len : len * 0.5f)); L.spawn.push_back(-1);
        } else { L.err = "objective '" + o + "' isn't hill, site N, point X, checkpoint N or cart [start|end]"; return L; }
    } else if (j["box"].isObj()) { L.isBox = true; L.box = toBox(j["box"]); L.label = "box"; }
    else { L.err = "a place is [x,y,z], {spawn}, {team}, {objective} or {box}"; return L; }
    return L;
}

struct Result {
    std::string name, kind;
    bool pass = true;
    Json value;
    std::string detail;
    std::vector<Vec3> where;
    Json path;
    Json json() const {
        Json o = Json::object();
        o.set("name", name); o.set("kind", kind); o.set("pass", pass); o.set("value", value); o.set("detail", detail);
        Json w = Json::array();
        for (auto& p : where) w.push(jvec(p));
        o.set("where", std::move(w));
        if (!path.isNull()) o.set("paths", path);
        return o;
    }
};

Json jpath(const std::vector<Vec3>& p) { Json a = Json::array(); for (auto& v : p) a.push(jvec(v)); return a; }

// the walk grid spots a route to `to` may end on: the standing spots in the box, or the one at the point
std::vector<int> targetsOf(WalkGrid& w, const Loc& to, size_t k = 0) {
    if (to.isBox) return w.nodesIn(to.box);
    int n = w.nodeAt(to.pts[k]);
    return n >= 0 ? std::vector<int>{n} : std::vector<int>{};
}

// route_time / reachable: every `from` point to `to` (each from point on its own; the slowest decides)
void routeTest(Analysis& a, const Json& t, Result& r, bool timed) {
    const Map& m = a.m;
    Loc from = resolve(m, t["from"]), to = resolve(m, t["to"]);
    if (!from.err.empty() || !to.err.empty()) { r.pass = false; r.detail = !from.err.empty() ? "from: " + from.err : "to: " + to.err; return; }
    if (from.isBox) { r.pass = false; r.detail = "from must be a point, a spawn or a team"; return; }
    std::string via = t["via"].str("walk");   // walk: the walk grid (the player's hull); nav: the bots' nav graph (the map report's distances)
    bool expect = t["expect"].boolean(true);  // reachable: false = the test passes when it CAN'T be reached
    float maxS = t["max"].f(-1), minS = t["min"].f(-1);
    float worst = -1;
    bool allReach = true, anyReach = false;
    Json paths = Json::array();
    int worstK = -1;
    for (size_t k = 0; k < from.pts.size(); k++) {
        std::vector<Vec3> path;
        float d;
        if (via == "nav") {
            const NavGraph& g = a.navOpen();
            d = to.isBox ? navRouteToBox(g, from.pts[k], to.box, &path) : navRoute(g, from.pts[k], to.pts[0], &path);
        } else {
            WalkGrid& w = a.walk();
            d = walkRouteNodes(w, w.nodeAt(from.pts[k]), targetsOf(w, to), &path);
        }
        if (d < 0) { allReach = false; r.where.push_back(from.pts[k]); }
        else { anyReach = true; if (d > worst) { worst = d; worstK = (int)k; } }
        Json p = Json::object();
        p.set("from", jvec(from.pts[k]));
        if (from.spawn[k] >= 0) p.set("spawn", from.spawn[k]);
        p.set("seconds", d >= 0 ? Json(d / kRun) : Json());
        p.set("path", jpath(path));
        paths.push(std::move(p));
    }
    r.path = std::move(paths);
    char b[256];
    if (!timed) {
        r.value = allReach;
        r.pass = expect ? allReach : !anyReach;
        snprintf(b, sizeof b, "%s %s %s (%s)", from.label.c_str(), allReach ? "reaches" : anyReach ? "partly reaches" : "can't reach", to.label.c_str(), via.c_str());
        r.detail = b;
        if (!expect && anyReach) { r.where.clear(); for (size_t k = 0; k < from.pts.size(); k++) if (r.path[k]["seconds"].isNum()) r.where.push_back(from.pts[k]); }
        return;
    }
    float s = worst / kRun;
    r.value = allReach ? Json(s) : Json();
    r.pass = allReach && (maxS < 0 || s <= maxS) && (minS < 0 || s >= minS);
    if (!allReach) snprintf(b, sizeof b, "%s can't reach %s (%s)", from.label.c_str(), to.label.c_str(), via.c_str());
    else snprintf(b, sizeof b, "%s to %s: %.1f s at %.1f m/s (%s; slowest of %zu)%s%s", from.label.c_str(), to.label.c_str(), s, kRun, via.c_str(), from.pts.size(),
                  maxS >= 0 ? (" max " + std::to_string(maxS).substr(0, 5)).c_str() : "", minS >= 0 ? (" min " + std::to_string(minS).substr(0, 5)).c_str() : "");
    r.detail = b;
    if (allReach && !r.pass && worstK >= 0) r.where.push_back(from.pts[worstK]);
}

void arrivalTest(Analysis& a, const Json& t, Result& r) {
    const Map& m = a.m;
    int mode = modeFromJson(t["mode"], m.preferredMode >= 0 ? m.preferredMode : MODE_FFA);
    float within = t["within"].f(1.0f);
    std::string which = lower(t["objective"].str(""));
    Json routes = Json::array();
    Json objs = a.objectives(mode, routes, false);
    float worst = 0; bool any = false, unreach = false;
    std::string lines;
    for (auto& o : objs.items()) {
        std::string name = lower(o["name"].str());
        if (!which.empty() && name != which && name != "bomb " + which) continue;
        if (o["kind"].str() == "checkpoint") continue;
        any = true;
        char b[160];
        if (!o["diff"].isNum()) { unreach = true; r.where.push_back(toVec(o["pos"])); snprintf(b, sizeof b, "%s: unreachable for a side", o["name"].str().c_str()); }
        else {
            float d = o["diff"].f();
            if (std::fabs(d) > std::fabs(worst)) worst = d;
            if (std::fabs(d) > within) r.where.push_back(toVec(o["pos"]));
            snprintf(b, sizeof b, "%s: %+.1f s (%s first)", o["name"].str().c_str(), d, d >= 0 ? "defenders" : "attackers");
        }
        lines += (lines.empty() ? "" : "; ") + std::string(b);
    }
    if (!any) { r.pass = false; r.detail = std::string("no objective") + (which.empty() ? "" : " '" + which + "'") + " with two sides in " + modeName(mode); return; }
    r.value = unreach ? Json() : Json(worst);
    r.pass = !unreach && std::fabs(worst) <= within;
    char b[64];
    snprintf(b, sizeof b, " (attackers minus defenders, within +/-%.1f s, %s)", within, modeName(mode));
    r.detail = lines + b;
}

void ladderTest(Analysis& a, const Json& t, Result& r) {
    Json req = Json::object();
    Json only = Json::array(); only.push("ladders");
    req.set("only", only);
    Json c = checksJson(a, req)["checks"][0];
    int which = t["ladder"].isNum() ? t["ladder"].i() : -1;
    if (which >= 0) {   // one ladder: is it among the failures?
        if (which >= (int)a.m.ladders.size()) { r.pass = false; r.detail = "no ladder " + std::to_string(which); return; }
        std::string tag = "ladder " + std::to_string(which) + " ";
        bool bad = false;
        for (auto& s : c["items"].items()) bad |= s.str().compare(0, tag.size(), tag) == 0;
        r.pass = !bad; r.value = !bad;
        r.detail = tag + (bad ? "can't be climbed" : "climbs");
        if (bad) r.where.push_back(a.m.ladders[which].center());
        return;
    }
    r.pass = c["pass"].boolean(); r.value = r.pass; r.detail = c["detail"].str();
    for (auto& p : c["where"].items()) r.where.push_back(toVec(p));
}

// max_fall: the biggest drop between two standing spots a walk grid move makes (walking off, a gap jumped down):
// along the route from -> to when `to` is given, else anywhere a player can get to from a spawn
void fallTest(Analysis& a, const Json& t, Result& r) {
    WalkGrid& w = a.walk();
    float maxM = t["max"].f(-1);
    float worst = 0;
    Vec3 at, land;
    const int hub0 = (int)w.nodes.size() - w.ladderHubs;
    if (t.has("to")) {
        Loc from = resolve(a.m, t["from"]), to = resolve(a.m, t["to"]);
        if (!from.err.empty() || !to.err.empty()) { r.pass = false; r.detail = !from.err.empty() ? "from: " + from.err : "to: " + to.err; return; }
        std::vector<Vec3> path;
        int reached = -1;
        float d = walkRouteNodes(w, w.nodeAt(from.pts[0]), targetsOf(w, to), &path, &reached);
        if (d < 0) { r.pass = false; r.detail = from.label + " can't reach " + to.label; return; }
        // (the corners hold every drop: a straight stretch between two corners is level or a slope)
        for (size_t k = 0; k + 1 < path.size(); k++) { float dy = path[k].y - path[k + 1].y; if (dy > worst) { worst = dy; at = path[k]; land = path[k + 1]; } }
        r.path = jpath(path);
    } else {
        const std::vector<char>& reach = a.reach();
        for (int u = 0; u < hub0; u++) {
            if (!reach[u]) continue;
            for (int v : w.links[u]) {
                if (v >= hub0) continue;
                float dy = w.nodes[u].p.y - w.nodes[v].p.y;
                if (dy > worst) { worst = dy; at = w.nodes[u].p; land = w.nodes[v].p; }
            }
        }
    }
    r.value = worst;
    r.pass = maxM < 0 || worst <= maxM + 1e-4f;
    char b[200];
    if (worst > 0) snprintf(b, sizeof b, "biggest drop %.1f m, from %.1f %.1f %.1f to %.1f %.1f %.1f%s", worst, at.x, at.y, at.z, land.x, land.y, land.z, t.has("to") ? " (on the route)" : " (anywhere reachable)");
    else snprintf(b, sizeof b, "no drop%s", t.has("to") ? " on the route" : " anywhere reachable");
    r.detail = b;
    if (worst > 0) { r.where.push_back(at); r.where.push_back(land); }
}

void sightTest(Analysis& a, const Json& t, Result& r) {
    Loc A = resolve(a.m, t["a"]), B = resolve(a.m, t["b"]);
    if (!A.err.empty() || !B.err.empty()) { r.pass = false; r.detail = !A.err.empty() ? "a: " + A.err : "b: " + B.err; return; }
    if (A.isBox || B.isBox) { r.pass = false; r.detail = "line_of_sight takes points, spawns or teams"; return; }
    bool expect = t["expect"].boolean(false);   // default: they must NOT see each other
    float eye = t["eye"].f(kEyeHeight);          // added to each point's y (feet -> eyes); 0 for raw points
    int seen = 0, pairs = 0;
    for (auto& p : A.pts)
        for (auto& q : B.pts) {
            Vec3 e1 = p + Vec3(0, eye, 0), e2 = q + Vec3(0, eye, 0), d = e2 - e1;
            float L = length(d);
            pairs++;
            bool vis = L < 0.01f || !a.m.raycastAny(e1, d / L, L);
            seen += vis;
            if (vis != expect) { r.where.push_back(p); r.where.push_back(q); }
        }
    r.value = seen;
    r.pass = expect ? seen == pairs : seen == 0;
    char b[200];
    snprintf(b, sizeof b, "%d of %d pairs (%s - %s) see each other at %.2f m above the feet; wanted %s", seen, pairs, A.label.c_str(), B.label.c_str(), eye, expect ? "all" : "none");
    r.detail = b;
}

// spawn_clearance: open room round each spawn (of a team, or all): the nearest wall within 16 directions at chest
// height, and the headroom above
void clearanceTest(Analysis& a, const Json& t, Result& r) {
    const Map& m = a.m;
    float minM = t["min"].f(1.0f), headMin = t["headroom"].f(-1);
    int team = t["team"].isStr() ? spawnTeamFromName(t["team"].str()) : -2;
    float worst = 1e9f, worstHead = 1e9f;
    int n = 0;
    for (auto& sp : m.spawns) {
        if (team != -2 && sp.team != team) continue;
        n++;
        Vec3 chest = sp.pos + Vec3(0, 1.0f, 0);
        float near = 1e9f;
        for (int k = 0; k < 16; k++) {
            float ang = k * 6.2831853f / 16;
            float d = m.raycast(chest, {std::sin(ang), 0, std::cos(ang)}, 20.0f);
            if (d >= 0) near = std::min(near, d);
        }
        near = std::max(0.0f, near - kPlayerHalfWidth);   // (from the hull's side, not its centre)
        float up = m.raycast(sp.pos + Vec3(0, 0.05f, 0), {0, 1, 0}, 50.0f);
        float head = up < 0 ? 50.0f : up + 0.05f;
        if (near < minM || (headMin >= 0 && head < headMin)) r.where.push_back(sp.pos);
        worst = std::min(worst, near); worstHead = std::min(worstHead, head);
    }
    if (!n) { r.pass = false; r.detail = "no spawns"; return; }
    r.value = worst > 1e8f ? Json(20.0f) : Json(worst);
    r.pass = r.where.empty();
    char b[200];
    snprintf(b, sizeof b, "%d spawns: the closest wall %.2f m from a hull's side (min %.2f), the lowest headroom %.2f m%s", n, worst > 1e8f ? 20.0f : worst, minM, worstHead,
             headMin >= 0 ? (" (min " + std::to_string(headMin).substr(0, 4) + ")").c_str() : "");
    r.detail = b;
}

void checkTest(Analysis& a, const Json& t, Result& r) {
    std::string id = t["check"].str();
    bool known = false;
    for (int k = 0; kCheckIds[k]; k++) known |= id == kCheckIds[k];
    if (!known) { r.pass = false; r.detail = "no check '" + id + "'"; return; }
    Json req = Json::object();
    Json only = Json::array(); only.push(id);
    req.set("only", only);
    Json c = checksJson(a, req)["checks"][0];
    r.pass = c["pass"].boolean(); r.value = r.pass; r.detail = c["detail"].str();
    for (auto& p : c["where"].items()) r.where.push_back(toVec(p));
}
}  // namespace

Json customKindsJson() {
    auto field = [](const char* name, const char* type, Json def, const char* desc) {
        Json f = Json::object(); f.set("name", name); f.set("type", type); f.set("default", def); f.set("desc", desc); return f;
    };
    Json kinds = Json::array();
    auto kind = [&](const char* k, const char* title, const char* desc, std::vector<Json> fields) {
        Json o = Json::object(); o.set("kind", k); o.set("title", title); o.set("desc", desc);
        Json fs = Json::array(); for (auto& f : fields) fs.push(f);
        o.set("fields", fs);
        kinds.push(std::move(o));
    };
    const char* placeDesc = "a place: [x,y,z], {spawn:i}, {team:'a'} (each of its spawns), {objective:'hill'|'site 1'|'point A'|'checkpoint 2'|'cart'|'cart start'|'cart end'} or {box:{min,max}}";
    kind("route_time", "Time to a place", "the walk from each `from` point to `to`, at run speed (9.5 m/s); the slowest decides", {
        field("from", "place", Json(), placeDesc), field("to", "place", Json(), placeDesc),
        field("max", "seconds", Json(), "fail when slower (s)"), field("min", "seconds", Json(), "fail when faster (s)"),
        field("via", "string", "walk", "walk: the walk grid (the player's hull, no movement tech); nav: the bots' nav graph (the map report's distances)")});
    kind("reachable", "Reachable", "every `from` point gets to `to` (expect false: none of them does)", {
        field("from", "place", Json(), placeDesc), field("to", "place", Json(), placeDesc),
        field("expect", "bool", true, "true: must reach; false: must not"), field("via", "string", "walk", "walk | nav")});
    kind("objective_arrival_diff", "Attack / defend arrival", "attackers' arrival minus defenders' at each site / point / hill / cart in a mode (positive: defenders first)", {
        field("mode", "mode", Json(), "the mode (default: the map's own)"), field("objective", "objective", Json(), "one objective by name ('site 1', 'point A', 'hill'); default every one"),
        field("within", "seconds", 1.0f, "pass when |diff| <= this (owner B2: 1 s)")});
    kind("ladder_climbable", "Ladder climbable", "the real movement code climbs the ladder from a step away", {
        field("ladder", "number", Json(), "ladder index; default every ladder")});
    kind("max_fall", "Biggest drop", "the biggest drop a walk makes: along the route from -> to, or anywhere reachable from a spawn (no `to`)", {
        field("from", "place", Json(), placeDesc), field("to", "place", Json(), "optional: the biggest drop along the route to here"),
        field("max", "number", Json(), "fail when a drop is higher (m)")});
    kind("line_of_sight", "Line of sight", "whether a and b see each other (eye to eye); by default they must not", {
        field("a", "place", Json(), "a point, a spawn or a team"), field("b", "place", Json(), "a point, a spawn or a team"),
        field("expect", "bool", false, "true: every pair must see each other; false: none may"), field("eye", "number", kEyeHeight, "height added to each point (m; 0 for raw points)")});
    kind("spawn_clearance", "Spawn clearance", "room round each spawn: the nearest wall at chest height (from the hull's side) and the headroom", {
        field("team", "team", Json(), "only this team's spawns (a, b, any, race, att0, def1 ...)"), field("min", "number", 1.0f, "fail when a wall is closer (m)"),
        field("headroom", "number", Json(), "fail when the ceiling is lower (m)")});
    Json ids = Json::array();
    for (int k = 0; kCheckIds[k]; k++) ids.push(kCheckIds[k]);
    Json ck = field("check", "string", "walk_routes", "the check's id");
    ck.set("choices", ids);
    kind("check", "A built-in check", "one of `check`'s map checks, as a custom test", {ck});
    return kinds;
}

static int runCustom(const Json& req, Json& out, CmdCtx&) {
    double t0 = maptoolNow();
    Map m;
    std::string err;
    if (!loadRequestMap(req, m, err)) return cmdError(out, err);
    if (!req["tests"].isArr()) return cmdError(out, "custom needs \"tests\": [{kind, name, ...}]");
    Analysis a(std::move(m));
    Json results = Json::array();
    int failed = 0;
    for (auto& t : req["tests"].items()) {
        Result r;
        r.kind = t["kind"].str();
        r.name = t["name"].str(r.kind);
        double s = maptoolNow();
        if (r.kind == "route_time") routeTest(a, t, r, true);
        else if (r.kind == "reachable") routeTest(a, t, r, false);
        else if (r.kind == "objective_arrival_diff") arrivalTest(a, t, r);
        else if (r.kind == "ladder_climbable") ladderTest(a, t, r);
        else if (r.kind == "max_fall") fallTest(a, t, r);
        else if (r.kind == "line_of_sight") sightTest(a, t, r);
        else if (r.kind == "spawn_clearance") clearanceTest(a, t, r);
        else if (r.kind == "check") checkTest(a, t, r);
        else { r.pass = false; r.detail = "unknown kind '" + r.kind + "' (quark_maptool help lists them)"; }
        Json o = r.json();
        o.set("ms", (maptoolNow() - s) * 1000);
        failed += !r.pass;
        results.push(std::move(o));
    }
    out = Json::object();
    out.set("results", std::move(results));
    out.set("pass", failed == 0);
    out.set("failed", failed);
    Json tm = a.timings();
    tm.set("total", (maptoolNow() - t0) * 1000);
    out.set("ms", std::move(tm));
    return 0;
}
MAPTOOL_COMMAND("custom", runCustom, "{map, tests:[{kind, name, ...}]} -> {results:[{name, kind, pass, value, detail, where, paths?}]}");
