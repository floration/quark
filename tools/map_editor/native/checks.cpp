// check: the map safety checks quark_tests runs on every map, on one map, as data.
// COPIED (and adapted to report instead of CHECK) from tests/tests.cpp at 07ef254 - DEDUPE LATER, each marked below:
//   testAllMaps (preferred mode, spawns in walls, walk routes, bot nav routes), testLadders, testStairs,
//   testSpawnPools, testSpawnSanity, testCartSweep, the ranked symmetry part of testMaps; and the map report's
//   out of bounds (measure.cpp outOfBounds).
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <mutex>
#include "analysis.h"
#include "maptool.h"
#include "walkfast.h"
#include "game/gamedefs.h"
#include "game/gamemodes.h"
#include "game/pmove.h"
#include "game/server.h"

namespace {
struct Check {
    std::string id, name;
    bool pass = true, skipped = false;
    std::vector<std::string> items;   // what failed (one line each), or what was measured
    std::vector<Vec3> where;
    std::string summary;
    double ms = 0;
    void fail(const std::string& what, const Vec3* at = nullptr) { pass = false; items.push_back(what); if (at) where.push_back(*at); }
    Json json() const {
        Json o = Json::object();
        o.set("id", id); o.set("name", name); o.set("pass", pass);
        if (skipped) o.set("skipped", true);
        std::string d = summary;
        for (size_t k = 0; k < items.size() && k < 12; k++) d += (d.empty() ? "" : "; ") + items[k];
        if (items.size() > 12) d += "; (" + std::to_string(items.size() - 12) + " more)";
        o.set("detail", d);
        Json w = Json::array();
        for (auto& p : where) w.push(jvec(p));
        o.set("where", std::move(w));
        Json it = Json::array();
        for (auto& s : items) it.push(s);
        o.set("items", std::move(it));
        o.set("ms", ms);
        return o;
    }
};
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char b[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}
}  // namespace

// testAllMaps: the preferred mode is supported
static void checkPreferred(Analysis& a, Check& c) {
    const Map& m = a.m;
    if (m.preferredMode < 0) { c.summary = "no \"for <mode>\" line: no preferred mode"; return; }
    c.summary = std::string("built for ") + modeName(m.preferredMode);
    if (!m.supportsMode(m.preferredMode)) c.fail(std::string(modeName(m.preferredMode)) + " isn't supported (allowed modes, or the pieces it needs: spawns per team, sites, hill, track, points, checkpoints)");
}
// testAllMaps: no spawn inside a wall (the standing hull 2 cm up)
static void checkSpawnsInWalls(Analysis& a, Check& c) {
    const Map& m = a.m;
    for (size_t i = 0; i < m.spawns.size(); i++) {
        const Vec3& p = m.spawns[i].pos;
        if (m.boxSolid(playerBox(p + Vec3(0, 0.02f, 0), false))) c.fail(fmt("spawn %zu at %.1f %.1f %.1f is inside a wall", i, p.x, p.y, p.z), &p);
    }
    c.summary = fmt("%zu spawns", m.spawns.size());
}
// testAllMaps: every route each offered mode needs, walked with the player's hull (tests/walkgrid.h walkRoutesMissing)
static void checkWalkRoutes(Analysis& a, Check& c) {
    const Map& m = a.m;
    WalkGrid& w = a.walk();
    int routes = 0;
    std::vector<std::string> missing = walkRoutesMissing(m, w, &routes);
    for (auto& s : missing) {
        float x, y, z;
        Vec3 at;
        bool have = sscanf(s.c_str(), "spawn %f %f %f", &x, &y, &z) == 3;
        if (have) {   // (the exact spawn the line names, to 0.1 m)
            at = {x, y, z};
            float bd = 1e9f;
            for (auto& sp : m.spawns) if (length(sp.pos - at) < bd) { bd = length(sp.pos - at); if (bd < 0.1f) at = sp.pos; }
        }
        c.fail(s, have ? &at : nullptr);
    }
    c.summary = fmt("%zu standing spots, %d routes walked, %zu missing", w.nodes.size(), routes, missing.size());
}
// testAllMaps: the bots' nav graph links every spawn to every objective
static void checkNavRoutes(Analysis& a, Check& c) {
    const Map& m = a.m;
    const NavGraph& g = a.nav();
    std::vector<Vec3> goals;
    if (m.hasHill) {
        Vec3 hp = {m.hill.center().x, m.hill.mn.y, m.hill.center().z};
        for (float fx = 0.1f; fx < 1; fx += 0.2f)
            for (float fz = 0.1f; fz < 1; fz += 0.2f) {
                Vec3 q = {m.hill.mn.x + (m.hill.mx.x - m.hill.mn.x) * fx, m.hill.mn.y, m.hill.mn.z + (m.hill.mx.z - m.hill.mn.z) * fz};
                if (g.nodeAt(q + Vec3(0, 0.5f, 0)) >= 0 && g.nodeAt(hp + Vec3(0, 0.5f, 0)) < 0) hp = q;
            }
        goals.push_back(hp);
    }
    for (auto& s : m.bombSites) goals.push_back(s.center());
    if (m.payloadPath.size() >= 2) { goals.push_back(m.payloadPath.front()); goals.push_back(m.payloadPath.back()); }
    for (auto& ck : m.checkpoints) goals.push_back(ck.center());
    for (auto& p : m.points) goals.push_back({p.center().x, p.mn.y, p.center().z});
    if (m.preferredMode == MODE_HIDE || m.preferredMode == MODE_TAG) for (auto& sp2 : m.spawns) goals.push_back(sp2.pos);
    std::vector<int> goalNode;
    for (auto& gl : goals) goalNode.push_back(g.nodeNear(gl + Vec3(0, 0.5f, 0), NavGraph::kGoalNear));   // (as bots take them)
    int pairs = 0;
    for (auto& sp : m.spawns) {
        int s = g.nodeNear(sp.pos + Vec3(0, 0.1f, 0), NavGraph::kStandNear);
        std::vector<char> seen(g.nodes.size(), 0);   // (one search per spawn: the same answers as one per pair)
        if (s >= 0) {
            std::vector<int> q = {s};
            seen[s] = 1;
            for (size_t i = 0; i < q.size(); i++)
                for (auto& l : g.links[q[i]]) if (!seen[l.first]) { seen[l.first] = 1; q.push_back(l.first); }
        }
        for (size_t k = 0; k < goals.size(); k++) {
            pairs++;
            const Vec3& gl = goals[k];
            if (s < 0 || goalNode[k] < 0) {
                const Vec3& at = s < 0 ? sp.pos : gl;
                c.fail(fmt("%s at %.1f %.1f %.1f is off the walkable surfaces", s < 0 ? "spawn" : "goal", at.x, at.y, at.z), &at);
                continue;
            }
            if (!seen[goalNode[k]]) c.fail(fmt("spawn %.1f %.1f %.1f can't reach %.1f %.1f %.1f", sp.pos.x, sp.pos.y, sp.pos.z, gl.x, gl.y, gl.z), &sp.pos);
        }
    }
    c.summary = fmt("%zu nav surfaces, %d spawn->objective routes", g.nodes.size(), pairs);
}
// testLadders: climbed with the real movement code from a step away, four ways round
static void checkLadders(Analysis& a, Check& c) {
    const Map& m = a.m;
    std::vector<char> ok(m.ladders.size(), 0);
    parallelFor((int)m.ladders.size(), [&](int li) {
        const AABB& l = m.ladders[li];
        bool up = false;
        for (float yaw : {0.0f, PI, PI * 0.5f, -PI * 0.5f}) {
            MoveState ms;
            Vec3 cc = l.center();
            Vec3 back = -angleForward(yaw, 0) * 0.3f;
            ms.pos = {cc.x + back.x, l.mn.y + 0.05f, cc.z + back.z};
            if (m.boxSolid(playerBox(ms.pos, false))) continue;
            UserCmd cmd; cmd.yaw = yaw; cmd.pitch = 0.2f; cmd.forward = 127; cmd.msec = 8;
            for (int t = 0; t < 1000 && !up; t++) {
                cmd.forward = t < 700 ? 127 : -127;
                pmove(ms, cmd, m);
                up = ms.pos.y > l.mx.y - 1.3f && (ms.flags & MF_ONGROUND) && !m.onLadder(playerBox(ms.pos, false));
            }
            if (up) break;
        }
        ok[li] = up;
    }, 1);
    for (size_t k = 0; k < m.ladders.size(); k++) {
        const AABB& l = m.ladders[k];
        Vec3 at = {l.center().x, l.mn.y, l.center().z};
        if (!ok[k]) c.fail(fmt("ladder %zu at %.1f %.1f %.1f can't be climbed", k, at.x, at.y, at.z), &at);
    }
    c.summary = fmt("%zu ladders", m.ladders.size());
}
// testStairs: every flight walked up and jumped up with the real movement code, and none cutting a path
static void checkStairs(Analysis& a, Check& walkC, Check& jumpC, Check& cutC) {
    const Map& m = a.m;
    const NavGraph& g = a.navSurfaces();   // (every walkable top whole, as quark_tests' check)
    struct Res { bool up = false; Vec3 got; int bonks = 0; std::vector<std::pair<std::string, Vec3>> cuts; };
    std::vector<Res> res(m.stairs.size());
    parallelFor((int)m.stairs.size(), [&](int si) {
        const Map::Stairs& st = m.stairs[si];
        Res& R = res[si];
        float x0 = st.x0, z0 = st.z0, x1 = st.x1, z1 = st.z1, base = st.base, top = st.top, rising = st.rising;
        bool solid = st.solid;
        bool ax = st.axis == 'x';
        float lo = ax ? (rising > 0 ? x0 : x1) : (rising > 0 ? z0 : z1), hi = ax ? (rising > 0 ? x1 : x0) : (rising > 0 ? z1 : z0);
        float dir = rising > 0 ? 1.0f : -1.0f, mid = ax ? (z0 + z1) / 2 : (x0 + x1) / 2;
        float half = (ax ? (z1 - z0) : (x1 - x0)) / 2, step0 = lo + dir * 0.4f;
        auto P = [&](float along, float acr, float y) { return ax ? Vec3(along, y, acr) : Vec3(acr, y, along); };
        bool up = false;
        MoveState ms;
        for (Vec3 start : {P(lo - dir * 0.7f, mid, base + 0.05f), P(lo - dir * 0.7f, mid - half * 0.6f, base + 0.05f), P(lo - dir * 0.7f, mid + half * 0.6f, base + 0.05f),
                           P(step0, mid - half - 0.6f, base + 0.05f), P(step0, mid + half + 0.6f, base + 0.05f)}) {
            float across = ax ? start.z : start.x, a0 = mid - half + 0.45f, a1 = mid + half - 0.45f;
            Vec3 goal = P(hi - dir * 0.3f, a0 < a1 ? std::clamp(across, a0, a1) : mid, top);
            ms = MoveState();
            ms.pos = start;
            if (m.boxSolid(playerBox(ms.pos, false))) continue;
            for (int t = 0; t < 40; t++) { UserCmd idle; idle.msec = 8; pmove(ms, idle, m); }
            if (!(ms.flags & MF_ONGROUND) || std::fabs(ms.pos.y - base) > 0.3f) continue;
            bool side = std::fabs(across - mid) > half;
            for (int t = 0; t < 700 && !up; t++) {
                Vec3 to = (side && t < 50) ? P(step0, mid, base) - ms.pos : goal - ms.pos;
                UserCmd cmd; cmd.yaw = std::atan2(-to.x, -to.z); cmd.pitch = 0; cmd.forward = 127; cmd.msec = 8;
                if (dot(angleForward(cmd.yaw, 0), Vec3(to.x, 0, to.z)) < 0) cmd.yaw += PI;
                pmove(ms, cmd, m);
                up = ms.pos.y > top - 0.1f && (ms.flags & MF_ONGROUND);
                if (ms.pos.y < base - 1.0f) break;
            }
            if (up) break;
        }
        R.up = up;
        R.got = ms.pos;
        {
            MoveState js;
            js.pos = P(lo - dir * 0.7f, mid, base + 0.05f);
            if (!m.boxSolid(playerBox(js.pos, false))) {
                for (int t = 0; t < 40; t++) { UserCmd idle; idle.msec = 8; pmove(js, idle, m); }
                Vec3 goal = P(hi - dir * 0.3f, mid, top);
                for (int t = 0; t < 500; t++) {
                    Vec3 to = goal - js.pos;
                    if (length(Vec3(to.x, 0, to.z)) < 0.4f) break;
                    UserCmd cmd; cmd.yaw = std::atan2(-to.x, -to.z); cmd.forward = 127; cmd.msec = 8;
                    cmd.buttons = (t / 30) % 2 ? 0 : BTN_JUMP;
                    float h0 = std::sqrt(js.vel.x * js.vel.x + js.vel.z * js.vel.z);
                    pmove(js, cmd, m);
                    float h1 = std::sqrt(js.vel.x * js.vel.x + js.vel.z * js.vel.z);
                    if (!(js.flags & MF_ONGROUND) && h0 > 3 && h1 < h0 * 0.5f) R.bonks++;
                    if (js.pos.y < base - 1.0f) break;
                }
            }
        }
        int n = std::max(1, (int)std::ceil((top - base) / 0.5f - 1e-4f));
        for (size_t k = 0; k < g.nodes.size(); k++) {
            auto& nd = g.nodes[k];
            float ox = std::min(nd.x1, x1) - std::max(nd.x0, x0), oz = std::min(nd.z1, z1) - std::max(nd.z0, z0);
            if (ox < 0.3f || oz < 0.3f) continue;
            bool inside = nd.x0 >= x0 - 0.2f && nd.x1 <= x1 + 0.2f && nd.z0 >= z0 - 0.2f && nd.z1 <= z1 + 0.2f;
            if (inside && nd.y >= base - 0.01f && nd.y <= top + 0.01f) continue;
            if (std::fabs(nd.y - base) < 0.3f || nd.y >= top - 0.01f) continue;
            bool bad = false;
            if (solid) bad = nd.y > base && nd.y < top;
            else {
                for (int i = 0; i < n && !bad; i++) {
                    float fa = (float)i / n, fb = (float)(i + 1) / n;
                    float h = base + (top - base) * (rising > 0 ? (i + 1) : (n - i)) / n;
                    float s0 = (ax ? x0 : z0) + ((ax ? x1 : z1) - (ax ? x0 : z0)) * fa, s1 = (ax ? x0 : z0) + ((ax ? x1 : z1) - (ax ? x0 : z0)) * fb;
                    float n0 = ax ? nd.x0 : nd.z0, n1 = ax ? nd.x1 : nd.z1;
                    if (std::min(n1, s1) - std::max(n0, s0) < 0.3f) continue;
                    float bottom = h - 1.0f;
                    bad = nd.y < bottom && bottom - nd.y < kPlayerHeight;
                }
            }
            if (bad) R.cuts.push_back({fmt("stairs %g %g - %g %g (%g to %g) cut the path at %.1f..%.1f %.1f..%.1f, y %.1f", x0, z0, x1, z1, base, top, nd.x0, nd.x1, nd.z0, nd.z1, nd.y),
                                       Vec3((nd.x0 + nd.x1) * 0.5f, nd.y, (nd.z0 + nd.z1) * 0.5f)});
        }
    }, 1);
    for (size_t k = 0; k < m.stairs.size(); k++) {
        const Map::Stairs& st = m.stairs[k];
        Vec3 foot = {(st.x0 + st.x1) * 0.5f, st.base, (st.z0 + st.z1) * 0.5f};
        if (!res[k].up) walkC.fail(fmt("stairs %g %g - %g %g (%g to %g) can't be walked up (got to %.1f %.1f %.1f)", st.x0, st.z0, st.x1, st.z1, st.base, st.top, res[k].got.x, res[k].got.y, res[k].got.z), &foot);
        if (res[k].bonks) jumpC.fail(fmt("stairs %g %g - %g %g: jumping up them stops you dead %d times", st.x0, st.z0, st.x1, st.z1, res[k].bonks), &foot);
        for (auto& [s, at] : res[k].cuts) cutC.fail(s, &at);
    }
    walkC.summary = jumpC.summary = cutC.summary = fmt("%zu flights (recorded \"stairs\" lines)", m.stairs.size());
}
// testSpawnPools: a spawn per player in every offered mode, 1.5 m apart
static void checkSpawnPools(Analysis& a, Check& c) {
    const Map& m = a.m;
    const int full = ServerConfig().maxPlayers;
    int maxStage = 0;
    for (auto& sp : m.spawns) if (isStagedSpawn(sp.team)) maxStage = std::max(maxStage, spawnStage(sp.team));
    int worst = 1 << 30, pools = 0;
    std::string worstWhat;
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        if (!m.supportsMode(mode)) continue;
        struct Use { const char* who; int team; bool match; int need; };
        std::vector<Use> uses;
        if (mode == MODE_ARENA) uses = {{"a", TEAM_A, false, 1}, {"b", TEAM_B, false, 1}};
        else if (mode == MODE_DUEL) uses = {{"a", TEAM_A, false, 2}, {"b", TEAM_B, false, 2}};
        else if (mode == MODE_TOURNEY) uses = {{"warm-up", TEAM_A, false, full}, {"match a", TEAM_A, true, 1}, {"match b", TEAM_B, true, 1}};
        else if (modeIsTeamBased(mode)) uses = {{"a", TEAM_A, false, full / 2}, {"b", TEAM_B, false, full / 2}};
        else uses = {{"all", TEAM_A, false, full}};
        for (auto& u : uses)
            for (int stage = 0; stage <= maxStage; stage++) {
                int want = modeSpawnType(m, mode, u.team, TEAM_A, u.match);
                if (!isStagedSpawn(want) && stage > 0) break;
                std::vector<const SpawnPoint*> pool = spawnPool(m, want, stage);
                float closest = 1e9f;
                const SpawnPoint *ca = nullptr, *cb = nullptr;
                for (size_t i = 0; i < pool.size(); i++)
                    for (size_t j = i + 1; j < pool.size(); j++) { float d = length(pool[i]->pos - pool[j]->pos); if (d < closest) { closest = d; ca = pool[i]; cb = pool[j]; } }
                pools++;
                std::string what = std::string(modeName(mode)) + " " + u.who + (isStagedSpawn(want) ? " stage " + std::to_string(stage) : "");
                if ((int)pool.size() - u.need < worst) { worst = (int)pool.size() - u.need; worstWhat = what; }
                if ((int)pool.size() < u.need || closest < 1.5f) {
                    c.fail(fmt("%s draws from %zu spawns (needs %d), the closest two %.2f m apart", what.c_str(), pool.size(), u.need, closest));
                    if (closest < 1.5f && ca) { c.where.push_back(ca->pos); c.where.push_back(cb->pos); }
                }
            }
    }
    c.summary = fmt("%zu spawns, %d pools, fewest to spare: %d (%s)", m.spawns.size(), pools, worst, worstWhat.c_str());
}
// testSpawnSanity: facing into the map, apart, out of the enemy's sight
static void checkSpawnSanity(Analysis& a, Check& facing, Check& apart, Check& sight) {
    const Map& m = a.m;
    for (auto& sp : m.spawns) {
        Vec3 eye = sp.pos + Vec3(0, kEyeHeight, 0);
        float t = m.raycast(eye, angleForward(sp.yaw, 0), 2.0f);
        if (t >= 0) facing.fail(fmt("spawn %.1f %.1f %.1f faces a wall %.1f m away", sp.pos.x, sp.pos.y, sp.pos.z, t), &sp.pos);
    }
    auto group = [](int t) { return t == SPAWN_A || t == SPAWN_B ? 0 : t == SPAWN_ANY ? 1 : t == SPAWN_RACE ? 2 : 3; };
    for (size_t i = 0; i < m.spawns.size(); i++)
        for (size_t j = i + 1; j < m.spawns.size(); j++) {
            auto &p = m.spawns[i], &q = m.spawns[j];
            bool stagesApart = isStagedSpawn(p.team) && isStagedSpawn(q.team) && spawnDefends(p.team) == spawnDefends(q.team) && spawnStage(p.team) != spawnStage(q.team);
            float d = length(p.pos - q.pos);
            if (d < 1.5f && !stagesApart && group(p.team) == group(q.team)) {
                apart.fail(fmt("spawns %.1f %.1f %.1f and %.1f %.1f %.1f are %.2f m apart", p.pos.x, p.pos.y, p.pos.z, q.pos.x, q.pos.y, q.pos.z, d), &p.pos);
                apart.where.push_back(q.pos);
            }
        }
    std::vector<std::pair<int, int>> sides;
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        if (!m.supportsMode(mode) || !modeIsTeamBased(mode)) continue;
        int sa = modeSpawnType(m, mode, TEAM_A, TEAM_A, false), sb = modeSpawnType(m, mode, TEAM_B, TEAM_A, false);
        if (isStagedSpawn(sa)) for (int st = 0; st <= kMaxSpawnStage; st++) sides.push_back({stagedSpawn(st, false), stagedSpawn(st, true)});
        else sides.push_back({sa, sb});
    }
    std::sort(sides.begin(), sides.end());
    sides.erase(std::unique(sides.begin(), sides.end()), sides.end());
    if (m.holdFire > 0) { sides.clear(); sight.summary = "holdfire map: both sides start face to face on purpose"; }
    int pairs = 0;
    for (auto [ta, tb] : sides)
        for (auto& p : m.spawns)
            for (auto& q : m.spawns) {
                if (p.team != ta || q.team != tb) continue;
                Vec3 e1 = p.pos + Vec3(0, kEyeHeight, 0), e2 = q.pos + Vec3(0, kEyeHeight, 0), d = e2 - e1;
                float len = length(d);
                pairs++;
                if (len > 0.1f && !m.raycastAny(e1, d / len, len)) {
                    sight.fail(fmt("enemy spawns %.1f %.1f %.1f and %.1f %.1f %.1f see each other (%.0f m)", p.pos.x, p.pos.y, p.pos.z, q.pos.x, q.pos.y, q.pos.z, len), &p.pos);
                    sight.where.push_back(q.pos);
                }
            }
    if (sight.summary.empty()) sight.summary = fmt("%d enemy spawn pairs", pairs);
    facing.summary = apart.summary = fmt("%zu spawns", m.spawns.size());
}
// testCartSweep: the cart's box every 10 cm of the track clear of every brush you see or bump into
static void checkCartSweep(Analysis& a, Check& c) {
    const Map& m = a.m;
    if (!(m.supportsMode(MODE_PAYLOAD) || m.supportsMode(MODE_NPAYLOAD))) { c.skipped = true; c.summary = "no payload mode offered"; return; }
    float len = m.payloadLength();
    int hits = 0;
    std::vector<char> reported(m.brushes.size(), 0);
    for (float d = 0; d <= len + 1e-3f; d += 0.1f) {
        AABB cart = m.payloadCartBox(std::min(d, len));
        cart.mn = cart.mn + Vec3(0.02f, 0.02f, 0.02f);
        cart.mx = cart.mx - Vec3(0.02f, 0.02f, 0.02f);
        for (size_t i = 0; i < m.brushes.size(); i++) {   // (every brush: trim isn't in the collision grid)
            const Brush& b = m.brushes[i];
            if (b.material == 5 || b.material == 6 || b.material == 7 || !cart.overlaps(b.box)) continue;
            hits++;
            if (reported[i]) continue;
            reported[i] = 1;
            Vec3 at = cart.center();
            c.fail(fmt("the cart at %.1f m of %.1f goes through brush %zu (material %d) %.2f %.2f %.2f .. %.2f %.2f %.2f", d, len, i, b.material,
                       b.box.mn.x, b.box.mn.y, b.box.mn.z, b.box.mx.x, b.box.mx.y, b.box.mx.z), &at);
        }
    }
    c.summary = fmt("%.1f m of track", len);
}
// testMaps: a ranked map is point-symmetric (solids, spawns, ladders, lights, objectives) with the sun overhead
static void checkRankedMirror(Analysis& a, Check& c) {
    const Map& m = a.m;
    if (!m.rankedLadders) { c.skipped = true; c.summary = "not a ranked map"; return; }
    auto mirrored = [](const AABB& b) { return AABB{{-b.mx.x, b.mn.y, -b.mx.z}, {-b.mn.x, b.mx.y, -b.mn.z}}; };
    int lonely = 0;
    for (auto& br : m.brushes) {
        AABB mb = mirrored(br.box);
        bool found = false;
        for (auto& o : m.brushes)
            if (o.material == br.material && length(o.box.mn - mb.mn) < 0.01f && length(o.box.mx - mb.mx) < 0.01f) { found = true; break; }
        if (!found) { lonely++; if (lonely <= 50) { Vec3 at = br.box.center(); c.where.push_back(at); } }
    }
    if (lonely) c.fail(fmt("%d solids without a mirrored twin", lonely));
    int na = 0, nb = 0;
    for (auto& sp : m.spawns) { na += sp.team == SPAWN_A; nb += sp.team == SPAWN_B; }
    bool duel = (m.rankedLadders & 3) != 0;
    if (!(na == nb && (na >= 1 || !duel))) c.fail(fmt("%d team A spawns, %d team B", na, nb));
    auto same = [](const Vec3& p, const Vec3& q) { return length(p - q) < 0.01f; };
    auto flip = [](const Vec3& p) { return Vec3(-p.x, p.y, -p.z); };
    auto boxTwin = [&](const std::vector<AABB>& list, const AABB& x) {
        AABB mx = mirrored(x);
        for (auto& o : list) if (same(o.mn, mx.mn) && same(o.mx, mx.mx)) return true;
        return false;
    };
    for (auto& sp : m.spawns) {
        int twinTeam = sp.team == SPAWN_A ? SPAWN_B : sp.team == SPAWN_B ? SPAWN_A : sp.team;
        bool found = false;
        for (auto& o : m.spawns)
            if (o.team == twinTeam && same(o.pos, flip(sp.pos)) && std::fabs(std::remainder(o.yaw - sp.yaw - PI, 2 * PI)) < 0.01f) found = true;
        if (!found) c.fail(fmt("spawn %.1f %.1f %.1f (yaw %.0f) has no mirrored twin", sp.pos.x, sp.pos.y, sp.pos.z, sp.yaw * 180 / PI), &sp.pos);
    }
    for (auto& l : m.ladders) if (!boxTwin(m.ladders, l)) { Vec3 at = l.center(); c.fail(fmt("a ladder at %.1f %.1f has no twin", at.x, at.z), &at); }
    for (auto& l : m.lights) {
        bool found = false;
        for (auto& o : m.lights) found |= same(o.pos, flip(l.pos)) && o.radius == l.radius && o.intensity == l.intensity;
        if (!found) c.fail(fmt("a light at %.1f %.1f %.1f has no twin", l.pos.x, l.pos.y, l.pos.z), &l.pos);
    }
    for (auto* list : {&m.bombSites, &m.points, &m.checkpoints}) for (auto& z : *list) if (!boxTwin(*list, z)) { Vec3 at = z.center(); c.fail("an objective has no twin", &at); }
    if (m.hasHill && !boxTwin({m.hill}, m.hill)) c.fail("the hill isn't centred");
    if (std::fabs(m.sunDir.x) > 1e-6f || std::fabs(m.sunDir.z) > 1e-6f) c.fail(fmt("the sun (%.2f %.2f %.2f) isn't overhead", m.sunDir.x, m.sunDir.y, m.sunDir.z));
    c.summary = "ranked: point-symmetric through the centre";
}
// the map report: nothing outside the playable floor reachable with every move a good player has
static void checkOutOfBounds(Analysis& a, Check& c) {
    OutOfBounds o = outOfBounds(a);
    if (!o.measured) { c.skipped = true; c.summary = "no spawns"; return; }
    c.summary = fmt("%.0f m2 reached only with movement tech (roofs, wall tops), %.0f m2 of it outside the playable footprint", o.techExtra, o.outside);
    for (size_t k = 0; k < o.spots.size(); k++) {
        const Vec3& q = o.spots[k];
        c.fail(fmt("out of bounds: %.1f %.1f %.1f", q.x, q.y, q.z), &q);
    }
}

Json checksJson(Analysis& a, const Json& req) {
    // which checks: "only": ["walk_routes", ...] (default all); "outOfBounds": false skips the slowest one
    std::vector<std::string> only;
    for (auto& v : req["only"].items()) only.push_back(v.str());
    auto want = [&](const char* id) { return only.empty() ? (std::string(id) != "out_of_bounds" || req["outOfBounds"].boolean(true)) : std::find(only.begin(), only.end(), id) != only.end(); };
    std::vector<Check> checks;
    auto run = [&](const char* id, const char* name, auto fn) {
        if (!want(id)) return;
        Check c; c.id = id; c.name = name;
        double s = maptoolNow();
        fn(c);
        c.ms = (maptoolNow() - s) * 1000;
        checks.push_back(std::move(c));
    };
    run("preferred_mode", "the mode it's built for is supported", [&](Check& c) { checkPreferred(a, c); });
    run("spawns_in_walls", "no spawn inside a wall", [&](Check& c) { checkSpawnsInWalls(a, c); });
    run("walk_routes", "every spawn reaches every objective (the player's hull, walk grid)", [&](Check& c) { checkWalkRoutes(a, c); });
    run("nav_routes", "the bots' nav graph links every spawn to every objective", [&](Check& c) { checkNavRoutes(a, c); });
    run("ladders", "every ladder climbable", [&](Check& c) { checkLadders(a, c); });
    if (want("stairs_walkable") || want("stairs_jump") || want("stairs_cut")) {
        Check w, j, k;
        w.id = "stairs_walkable"; w.name = "every staircase walkable";
        j.id = "stairs_jump"; j.name = "jumping up a staircase never stops you dead";
        k.id = "stairs_cut"; k.name = "no staircase cuts a path (through or under it)";
        double s = maptoolNow();
        checkStairs(a, w, j, k);
        w.ms = (maptoolNow() - s) * 1000;
        for (Check* c : {&w, &j, &k}) if (want(c->id.c_str())) checks.push_back(*c);
    }
    run("spawn_pools", "a spawn per player in every offered mode, 1.5 m apart", [&](Check& c) { checkSpawnPools(a, c); });
    if (want("spawn_facing") || want("spawn_apart") || want("spawn_sight")) {
        Check f, p, s;
        f.id = "spawn_facing"; f.name = "every spawn faces into the map (2 m clear at eye height)";
        p.id = "spawn_apart"; p.name = "spawns used together 1.5 m apart";
        s.id = "spawn_sight"; s.name = "no spawn sees an enemy spawn (team modes)";
        double t = maptoolNow();
        checkSpawnSanity(a, f, p, s);
        f.ms = (maptoolNow() - t) * 1000;
        for (Check* c : {&f, &p, &s}) if (want(c->id.c_str())) checks.push_back(*c);
    }
    run("cart_sweep", "the payload cart's whole track clear of the map", [&](Check& c) { checkCartSweep(a, c); });
    run("ranked_mirror", "ranked maps are point-symmetric", [&](Check& c) { checkRankedMirror(a, c); });
    run("out_of_bounds", "nothing out of bounds is reachable with movement tech", [&](Check& c) { checkOutOfBounds(a, c); });
    Json out = Json::object();
    out.set("map", a.m.name);
    out.set("humanMade", a.m.humanMade());   // (quark_tests reports a human-made map's misses but doesn't fail on them)
    Json arr = Json::array();
    int failed = 0;
    for (auto& c : checks) { arr.push(c.json()); failed += !c.pass; }
    out.set("checks", std::move(arr));
    out.set("pass", failed == 0);
    out.set("failed", failed);
    return out;
}

static int runCheck(const Json& req, Json& out, CmdCtx&) {
    double t0 = maptoolNow();
    Map m;
    std::string err;
    if (!loadRequestMap(req, m, err)) return cmdError(out, err);
    Analysis a(std::move(m));
    out = checksJson(a, req);
    Json t = a.timings();
    t.set("total", (maptoolNow() - t0) * 1000);
    out.set("ms", std::move(t));
    return 0;
}
MAPTOOL_COMMAND("check", runCheck, "{map, only?: [ids], outOfBounds?} -> the map checks quark_tests runs, {checks:[{id,name,pass,detail,where,items}]}");

const char* const kCheckIds[] = {"preferred_mode", "spawns_in_walls", "walk_routes", "nav_routes", "ladders", "stairs_walkable", "stairs_jump",
                                 "stairs_cut", "spawn_pools", "spawn_facing", "spawn_apart", "spawn_sight", "cart_sweep", "ranked_mirror",
                                 "out_of_bounds", nullptr};
