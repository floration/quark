// Map analysis: measures every shipped map from its geometry, the bot nav graph and line-of-sight rays, and
// writes docs/MAP_REPORT.md. Metric definitions and score formulas: docs/MAP_METRICS.md.
// Run: build/quark_tests mapreport
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <queue>
#include <sstream>
#include <string>
#include <vector>
#include "game/bots.h"
#include "game/gamedefs.h"
#include "game/map.h"
#include "game/pmove.h"

namespace {

// Weapon range bands (from the weapon set): close = shotgun / knife, mid = rifle / rocket / sticky,
// long = laser (32 m max) / crossbow, extreme = only the sniper (and a scoped rifle) reaches.
constexpr float kBandClose = 6.0f, kBandMid = 20.0f, kBandLong = 32.0f;
constexpr float kJumpReach = 1.2f;     // a standing jump clears ~1.2 m (pmove: 7 m/s up, 20 m/s^2 gravity)
constexpr float kEye = 1.6f;

struct Rng { uint32_t s = 12345; float f() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.0f; } };

struct Metrics {
    std::string name, title, preferred;
    // decor / detail
    int brushes = 0, detail = 0, glow = 0, metal = 0, accent = 0, wallbang = 0, far = 0, lights = 0, materials = 0;
    float detailDensity = 0;   // detail brushes per 100 m^2 of walkable floor
    float bareWalls = 0;       // share of wall hits (6-40 m from players) with no detail within 1 m
    // layout
    float area = 0;            // walkable m^2
    int levels = 0;            // floor heights with >= 10 m^2 (clustered 1.5 m apart)
    float vertRange = 0;
    int nodes = 0, links = 0, loops = 0, deadEnds = 0;
    float visible = 0;         // share of random point pairs (eye height) that see each other
    float band[4] = {};        // share of visible pairs in close / mid / long / extreme range
    float cover = 0;           // mean share of 16 directions blocked within 3 m at chest height
    float coverPts = 0;        // share of points with >= 4 of 16 directions blocked
    // movement
    float jumpLinks = 0;       // share of nav links that need a jump (rise > step height)
    float grapple = 0;         // share of points with a hookable surface >= 3 m up within grapple range
    float headroom = 0;        // share of points with > 6 m above (vertical dash, rocket jumps)
    float lanes = 0;           // 90th percentile of the longest straight run from a point (m): dash / bhop lanes
    float voidEdge = 0;        // share of points within 2 m of a lethal drop
    int ladders = 0;
    // modes
    std::vector<std::pair<std::string, std::string>> modes; // mode -> measured line
    float fairness = -1;       // 0-1, min/max of team distances (team maps)
    float hideSpots = 0;       // share of points seen by < 5% of others
    // originality
    std::vector<float> occ;    // 24x24 top-down footprint (max floor height, normalised)
    float originality = 0;
    std::string closest;
    // scores 0-10
    float sDecor = 0, sLayout = 0, sMovement = 0, sCreativity = 0, sBalance = 0, sOriginality = 0;
    std::vector<std::string> features;
};

struct Pt { Vec3 p; int node; };

// Dijkstra over the nav graph from one node: distances to every node (m, link costs).
std::vector<float> navDist(const NavGraph& g, int from) {
    std::vector<float> d(g.nodes.size(), 1e30f);
    if (from < 0) return d;
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    d[from] = 0; q.push({0, from});
    while (!q.empty()) {
        auto [du, u] = q.top(); q.pop();
        if (du > d[u]) continue;
        for (auto& [v, c] : g.links[u]) if (du + c < d[v]) { d[v] = du + c; q.push({d[v], v}); }
    }
    return d;
}

int nearestNode(const NavGraph& g, const Vec3& p) {
    int n = g.nodeAt(p);
    if (n >= 0) return n;
    float best = 1e30f;
    for (size_t i = 0; i < g.nodes.size(); i++) {
        auto& nd = g.nodes[i];
        float cx = std::clamp(p.x, nd.x0, nd.x1), cz = std::clamp(p.z, nd.z0, nd.z1);
        float d = (cx - p.x) * (cx - p.x) + (cz - p.z) * (cz - p.z) + (nd.y - p.y) * (nd.y - p.y) * 4;
        if (d < best) { best = d; n = (int)i; }
    }
    return n;
}

// Walking distance a -> b along the bots' nav graph (its link costs: centre to centre, plus gaps and climbs),
// with straight lines from a to its floor's centre and from the last floor's centre to b. -1 if unreachable.
float navBetween(const NavGraph& g, const Vec3& a, const Vec3& b) {
    int na = nearestNode(g, a), nb = nearestNode(g, b + Vec3(0, 0.5f, 0));
    if (na < 0 || nb < 0) return -1;
    auto centre = [&](int i) { auto& n = g.nodes[i]; return Vec3((n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f); };
    if (na == nb) return length(b - a);
    auto d = navDist(g, na);
    if (d[nb] > 1e29f) return -1;
    return length(centre(na) - a) + d[nb] + length(b - centre(nb));
}

// Walking distance from a into an area objective (hill, bomb site): to the nearest standable floor inside it.
float navToBox(const NavGraph& g, const Vec3& a, const AABB& box) {
    int na = nearestNode(g, a);
    if (na < 0) return -1;
    auto d = navDist(g, na);
    auto& n0 = g.nodes[na];
    Vec3 c0 = {(n0.x0 + n0.x1) * 0.5f, n0.y, (n0.z0 + n0.z1) * 0.5f};
    float best = -1;
    for (size_t i = 0; i < g.nodes.size(); i++) {
        auto& n = g.nodes[i];
        if (d[i] > 1e29f || n.x1 < box.mn.x || n.x0 > box.mx.x || n.z1 < box.mn.z || n.z0 > box.mx.z || n.y < box.mn.y - 0.5f || n.y > box.mx.y) continue;
        Vec3 in = {std::clamp((box.mn.x + box.mx.x) * 0.5f, n.x0, n.x1), n.y, std::clamp((box.mn.z + box.mx.z) * 0.5f, n.z0, n.z1)};
        float t = (int)i == na ? length(in - a) : length(c0 - a) + d[i] + length(in - Vec3((n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f));
        if (best < 0 || t < best) best = t;
    }
    return best;
}

bool los(const Map& m, const Vec3& a, const Vec3& b) {
    Vec3 d = b - a;
    float len = length(d);
    return len < 0.01f || !m.raycastAny(a, d / len, len);
}

const char* modeLabel(int m) { return modeName(m); }

void measure(Metrics& r, const Map& m, const NavGraph& g) {
    Rng rng;
    r.name = m.name; r.title = m.title;
    r.preferred = m.preferredMode >= 0 ? modeLabel(m.preferredMode) : "any";
    // ---- decor / detail
    bool used[8] = {};
    std::vector<int> detailIdx;
    for (size_t i = 0; i < m.brushes.size(); i++) {
        const Brush& b = m.brushes[i];
        Vec3 s = b.box.mx - b.box.mn;
        float mn = std::min({s.x, s.y, s.z});
        r.brushes++;
        used[std::clamp(b.material, 0, 7)] = true;
        if (b.material == 1) r.accent++;
        if (b.material == 2) r.wallbang++;
        if (b.material == 3) r.metal++;
        if (b.material == 4) r.glow++;
        if (b.material == 5) { r.far++; continue; } // backdrop scenery outside the walls: counted apart
        if (mn < 0.6f || s.x * s.y * s.z < 2.0f) { r.detail++; detailIdx.push_back((int)i); }
    }
    for (bool u : used) r.materials += u;
    r.lights = (int)m.lights.size();
    r.ladders = (int)m.ladders.size();
    // ---- walkable samples from the nav surfaces (2 m grid, standing room only)
    std::vector<Pt> pts;
    std::map<int, float> levelArea;
    float ymin = 1e9f, ymax = -1e9f;
    for (size_t i = 0; i < g.nodes.size(); i++) {
        auto& n = g.nodes[i];
        float a = (n.x1 - n.x0) * (n.z1 - n.z0);
        r.area += a;
        levelArea[(int)std::floor(n.y / 1.5f)] += a;
        ymin = std::min(ymin, n.y); ymax = std::max(ymax, n.y);
        for (float x = n.x0 + 1; x < n.x1; x += 2)
            for (float z = n.z0 + 1; z < n.z1; z += 2) {
                Vec3 p = {x, n.y, z};
                if (!m.boxSolid(playerBox(p + Vec3(0, 0.02f, 0), false))) pts.push_back({p, (int)i});
            }
    }
    for (auto& [k, a] : levelArea) r.levels += a >= 10.0f;
    r.vertRange = ymax > ymin ? ymax - ymin : 0;
    r.detailDensity = r.area > 0 ? r.detail * 100.0f / r.area : 0;
    {   // 8 eye-level camera spots for the map shots (tools/map_shots.sh): spread out (farthest-point picks), each
        // looking toward the middle of the map, slightly down
        std::vector<Vec3> picks;
        auto reach = navDist(g, nearestNode(g, m.spawns.empty() ? Vec3() : m.spawns[0].pos));
        std::vector<Pt> ok; // only floor you can get to (not the tops of the outer walls)
        for (auto& pt : pts) if (reach[pt.node] < 1e29f) ok.push_back(pt);
        Vec3 ctr;
        for (auto& pt : ok) ctr += pt.p / (float)ok.size();
        if (!ok.empty()) picks.push_back(ok[0].p);
        while (picks.size() < 8 && picks.size() < ok.size()) {
            float best = -1; Vec3 bp;
            for (auto& pt : ok) {
                float dmin = 1e30f;
                for (auto& q : picks) dmin = std::min(dmin, length(pt.p - q));
                if (dmin > best) { best = dmin; bp = pt.p; }
            }
            picks.push_back(bp);
        }
        std::ofstream cams("docs/map_shots/" + m.name + ".cams");
        for (auto& q : picks) { // face the longest open view from there (ties: toward the middle)
            Vec3 eye = q + Vec3(0, kEye, 0), dc = ctr - q;
            float bestYaw = std::atan2(-dc.x, -dc.z), bestLen = -1;
            for (int k = 0; k < 24; k++) {
                float yaw = k * 6.2831853f / 24;
                Vec3 d = {-std::sin(yaw), 0, -std::cos(yaw)};
                float t = m.raycast(eye, d, 80.0f);
                float len = (t < 0 ? 80.0f : t) + 0.05f * dot(d, normalize(Vec3(dc.x, 0, dc.z)));
                if (len > bestLen) { bestLen = len; bestYaw = yaw; }
            }
            cams << q.x << " " << q.y + kEye << " " << q.z << " " << bestYaw * 57.29578f << " " << -8 << "\n";
        }
    }
    if (pts.size() > 1500) { // keep it quick: an even subsample
        std::vector<Pt> sub;
        for (size_t i = 0; i < pts.size(); i += pts.size() / 1500 + 1) sub.push_back(pts[i]);
        pts.swap(sub);
    }
    const int P = (int)pts.size();
    if (P < 2) return;
    // ---- nav graph shape
    r.nodes = (int)g.nodes.size();
    int jumpLinks = 0;
    for (size_t i = 0; i < g.links.size(); i++) {
        int deg = 0;
        for (auto& [j, c] : g.links[i]) {
            if ((int)i < j) r.links++;
            deg++;
            if ((int)i < j && std::fabs(g.nodes[j].y - g.nodes[i].y) > 0.55f) jumpLinks++;
        }
        auto& n = g.nodes[i];
        if (deg == 1 && (n.x1 - n.x0) * (n.z1 - n.z0) > 4) r.deadEnds++;
    }
    {   // components (for loops = E - V + C)
        std::vector<int> comp(g.nodes.size(), -1);
        int c = 0;
        for (size_t s = 0; s < g.nodes.size(); s++) {
            if (comp[s] >= 0) continue;
            std::vector<int> st = {(int)s};
            comp[s] = c;
            while (!st.empty()) { int u = st.back(); st.pop_back(); for (auto& [v, w] : g.links[u]) if (comp[v] < 0) { comp[v] = c; st.push_back(v); } }
            c++;
        }
        r.loops = std::max(0, r.links - r.nodes + c);
    }
    r.jumpLinks = r.links ? (float)jumpLinks / r.links : 0;
    // ---- per-point rays: cover, lanes, headroom, grapple, void edges, bare walls
    int wallHits = 0, bareHits = 0, covered = 0, grapple = 0, head = 0, nearVoid = 0;
    std::vector<float> lanes;
    float coverSum = 0;
    for (auto& pt : pts) {
        Vec3 chest = pt.p + Vec3(0, 1.0f, 0), eye = pt.p + Vec3(0, kEye, 0);
        int blocked = 0;
        float longest = 0;
        for (int k = 0; k < 16; k++) {
            float a = k * 6.2831853f / 16;
            Vec3 d = {std::sin(a), 0, std::cos(a)};
            float t = m.raycast(chest, d, 60.0f);
            if (t >= 0 && t < 3.0f) blocked++;
            // speed lane: how far you can run this way with floor under you (1 m steps, small drops allowed)
            float run = 0, lim = t < 0 ? 60.0f : t;
            for (float x = 1; x <= lim; x += 1) {
                Vec3 q = chest + d * x;
                float down = m.raycast(q, {0, -1, 0}, 2.5f);
                if (down < 0) break;
                run = x;
            }
            longest = std::max(longest, run);
            float tw = m.raycast(eye, d, 40.0f);
            if (tw >= 6.0f) { // a wall a player looks at: is anything dressing it?
                Vec3 h = eye + d * tw;
                wallHits++;
                bool dressed = false;
                for (int bi : detailIdx) {
                    const AABB& b = m.brushes[bi].box;
                    Vec3 c = vmax(b.mn, vmin(h, b.mx));
                    if (length(c - h) < 1.0f) { dressed = true; break; }
                }
                bareHits += !dressed;
            }
        }
        coverSum += blocked / 16.0f;
        covered += blocked >= 4;
        lanes.push_back(longest);
        float up = m.raycast(eye, {0, 1, 0}, 30.0f);
        head += up < 0 || up > 6.0f;
        bool hook = false;
        for (int k = 0; k < 8 && !hook; k++)
            for (float el : {0.5f, 1.0f}) {
                float a = k * 6.2831853f / 8;
                Vec3 d = normalize(Vec3(std::sin(a) * std::cos(el), std::sin(el), std::cos(a) * std::cos(el)));
                float t = m.raycast(eye, d, kGrappleRange);
                if (t >= 0 && (eye + d * t).y - pt.p.y >= 3.0f) { hook = true; break; }
            }
        grapple += hook;
        bool edge = false;
        for (int k = 0; k < 8 && !edge; k++) {
            float a = k * 6.2831853f / 8;
            Vec3 probe = pt.p + Vec3(std::sin(a) * 2.0f, 0.5f, std::cos(a) * 2.0f);
            if (m.boxSolid(playerBox(probe, false))) continue;
            float down = m.raycast(probe, {0, -1, 0}, 200.0f);
            if (down < 0 || probe.y - down < m.killY()) edge = true;
        }
        nearVoid += edge;
    }
    r.cover = coverSum / P;
    r.coverPts = (float)covered / P;
    r.grapple = (float)grapple / P;
    r.headroom = (float)head / P;
    r.voidEdge = (float)nearVoid / P;
    r.bareWalls = wallHits ? (float)bareHits / wallHits : 0;
    std::sort(lanes.begin(), lanes.end());
    r.lanes = lanes[(size_t)(lanes.size() * 0.9)];
    // ---- sightlines: random pairs at eye height, by weapon band; how often each point is seen (hiding)
    std::vector<int> seen(P, 0), tried(P, 0);
    int vis = 0, pairs = 0, bands[4] = {};
    for (int k = 0; k < 30000; k++) {
        int i = (int)(rng.f() * P) % P, j = (int)(rng.f() * P) % P;
        if (i == j) continue;
        Vec3 a = pts[i].p + Vec3(0, kEye, 0), b = pts[j].p + Vec3(0, kEye, 0);
        pairs++; tried[i]++; tried[j]++;
        if (!los(m, a, b)) continue;
        vis++; seen[i]++; seen[j]++;
        float d = length(b - a);
        bands[d < kBandClose ? 0 : d < kBandMid ? 1 : d < kBandLong ? 2 : 3]++;
    }
    r.visible = pairs ? (float)vis / pairs : 0;
    for (int b = 0; b < 4; b++) r.band[b] = vis ? (float)bands[b] / vis : 0;
    int hidden = 0;
    for (int i = 0; i < P; i++) hidden += tried[i] && (float)seen[i] / tried[i] < 0.05f;
    r.hideSpots = (float)hidden / P;
    // ---- top-down footprint for originality (24x24, highest floor per cell, relative to the map's range)
    r.occ.assign(24 * 24, -1);
    float bx0 = 1e9f, bx1 = -1e9f, bz0 = 1e9f, bz1 = -1e9f;
    for (auto& n : g.nodes) { bx0 = std::min(bx0, n.x0); bx1 = std::max(bx1, n.x1); bz0 = std::min(bz0, n.z0); bz1 = std::max(bz1, n.z1); }
    for (auto& pt : pts) {
        int cx = std::clamp((int)((pt.p.x - bx0) / std::max(1.0f, bx1 - bx0) * 24), 0, 23);
        int cz = std::clamp((int)((pt.p.z - bz0) / std::max(1.0f, bz1 - bz0) * 24), 0, 23);
        float h = r.vertRange > 0.1f ? (pt.p.y - ymin) / r.vertRange : 0;
        r.occ[cz * 24 + cx] = std::max(r.occ[cz * 24 + cx], h);
    }
    // ---- modes: what each mode cares about, measured on this map
    std::vector<Vec3> spA, spB;
    for (auto& sp : m.spawns) { if (sp.team == SPAWN_A) spA.push_back(sp.pos); if (sp.team == SPAWN_B) spB.push_back(sp.pos); }
    bool teams = !spA.empty() && !spB.empty();
    auto teamDist = [&](const std::vector<Vec3>& sps, const Vec3& target) { // mean walking distance from the team's spawns
        float sum = 0; int n = 0;
        for (auto& p : sps) { float d = navBetween(g, p, target); if (d >= 0) { sum += d; n++; } }
        return n ? sum / n : -1.0f;
    };
    Vec3 mid;
    {   // the floor nearest the middle of the play area
        Vec3 c = {0, 0, 0}; float bx0 = 1e9f, bx1 = -1e9f, bz0 = 1e9f, bz1 = -1e9f;
        for (auto& n : g.nodes) { bx0 = std::min(bx0, n.x0); bx1 = std::max(bx1, n.x1); bz0 = std::min(bz0, n.z0); bz1 = std::max(bz1, n.z1); }
        c = {(bx0 + bx1) * 0.5f, ymin, (bz0 + bz1) * 0.5f};
        auto reach = navDist(g, nearestNode(g, m.spawns.empty() ? c : m.spawns[0].pos));
        float bd = 1e30f;
        for (size_t i = 0; i < g.nodes.size(); i++) {
            if (reach[i] > 1e29f) continue; // only floor you can actually get to
            auto& n = g.nodes[i];
            Vec3 q = {std::clamp(c.x, n.x0, n.x1), n.y, std::clamp(c.z, n.z0, n.z1)};
            float d = (q.x - c.x) * (q.x - c.x) + (q.z - c.z) * (q.z - c.z) + (q.y - ymin) * 0.5f;
            if (d < bd) { bd = d; mid = q; }
        }
    }
    char buf[256];
    std::vector<float> fair;
    auto boxDist = [&](const std::vector<Vec3>& sps, const AABB& box) {
        float sum = 0; int n = 0;
        for (auto& p : sps) { float d = navToBox(g, p, box); if (d >= 0) { sum += d; n++; } }
        return n ? sum / n : -1.0f;
    };
    auto fairLine = [&](const char* what, float da, float db) {
        if (da < 0 || db < 0) return std::string("unreachable");
        float f = std::min(da, db) / std::max(std::max(da, db), 0.01f);
        fair.push_back(f);
        snprintf(buf, sizeof buf, "%s: A %.0f m, B %.0f m (fairness %.2f)", what, da, db, f);
        return std::string(buf);
    };
    auto teamLine = [&](const char* what, const Vec3& target) { return fairLine(what, teamDist(spA, target), teamDist(spB, target)); };
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        if (!m.supportsMode(mode)) continue;
        std::string line;
        if (mode == MODE_BOMB && teams) {
            float att = 1e9f, def = 1e9f;
            for (auto& site : m.bombSites) {
                float da = boxDist(spA, site), db = boxDist(spB, site);
                if (da > 0) att = std::min(att, da);
                if (db >= 0) def = std::min(def, db);
            }
            snprintf(buf, sizeof buf, "nearest site: attackers (A) %.0f m, defenders (B) %.0f m - defenders should arrive first", att, def);
            line = buf;
        } else if (mode == MODE_KOTH && m.hasHill && teams) line = fairLine("to the hill", boxDist(spA, m.hill), boxDist(spB, m.hill));
        else if (mode == MODE_NPAYLOAD && m.payloadPath.size() >= 2 && teams)
            line = teamLine("to the cart", m.payloadPoint(m.payloadLength() * 0.5f));
        else if (mode == MODE_PAYLOAD && m.payloadPath.size() >= 2 && teams) { // one-sided by design (sides swap): no fairness score
            float att = teamDist(spA, m.payloadPath.front()), def = teamDist(spB, m.payloadPoint(m.payloadLength() * 0.5f));
            snprintf(buf, sizeof buf, "attackers (A) %.0f m to the cart, defenders (B) %.0f m to the halfway point (track %.0f m)", att, def, m.payloadLength());
            line = buf;
        }
        else if ((mode == MODE_VS || mode == MODE_DUEL || mode == MODE_ARENA) && teams) {
            // each side's walk to the other side's spawns (direction-independent: a symmetric map scores 1.00)
            auto across = [&](const std::vector<Vec3>& from, const std::vector<Vec3>& to) {
                float d = 0; int n = 0;
                for (auto& a : from) { float best = 1e9f; for (auto& b2 : to) { float x = navBetween(g, a, b2); if (x >= 0) best = std::min(best, x); } if (best < 1e8f) { d += best; n++; } }
                return n ? d / n : -1.0f;
            };
            float ab = across(spA, spB), ba = across(spB, spA);
            if (ab <= 0 || ba <= 0) line = "spawns can't reach each other";
            else {
                float f = std::min(ab, ba) / std::max(ab, ba);
                fair.push_back(f);
                snprintf(buf, sizeof buf, "A to B spawns %.0f m, B to A %.0f m (fairness %.2f, %.1f s apart at 8 m/s)", ab, ba, f, std::max(ab, ba) / 8.0f);
                line = buf;
            }
        } else if (mode == MODE_FFA || mode == MODE_TOURNEY) {
            float minD = 1e9f; int seenPairs = 0, n = 0;
            for (size_t i = 0; i < m.spawns.size(); i++)
                for (size_t j = i + 1; j < m.spawns.size(); j++) {
                    minD = std::min(minD, length(m.spawns[i].pos - m.spawns[j].pos));
                    seenPairs += los(m, m.spawns[i].pos + Vec3(0, kEye, 0), m.spawns[j].pos + Vec3(0, kEye, 0));
                    n++;
                }
            snprintf(buf, sizeof buf, "%zu spawns, closest pair %.0f m, %d%% of spawn pairs see each other", m.spawns.size(), minD, n ? seenPairs * 100 / n : 0);
            line = buf;
        } else if (mode == MODE_RACE && !m.checkpoints.empty()) {
            Vec3 at = m.spawns.empty() ? Vec3() : m.spawns[0].pos;
            float total = 0;
            bool ok = true;
            for (auto& c : m.checkpoints) { float d = navBetween(g, at, c.center()); if (d < 0) ok = false; else total += d; at = c.center(); }
            snprintf(buf, sizeof buf, "%zu checkpoints, %s %.0f m walked (%.0f s at 8 m/s, %.0f s at 20 m/s)", m.checkpoints.size(), ok ? "route" : "PARTLY UNREACHABLE route", total, total / 8, total / 20);
            line = buf;
        } else if (mode == MODE_HIDE || mode == MODE_TAG) {
            snprintf(buf, sizeof buf, "%.0f%% of floor is a hiding spot (seen from < 5%% of the map)", r.hideSpots * 100);
            line = buf;
        }
        if (!line.empty()) r.modes.push_back({modeLabel(mode), line});
    }
    if (m.rankedLadders) {
        // the weapons each ranked ladder is about, and where they work
        float near = r.band[0] + r.band[1], longr = r.band[2] + r.band[3];
        snprintf(buf, sizeof buf, "ranked: %.0f%% of sightlines in rifle/rocket range (< 20 m), %.0f%% in laser/crossbow/sniper range (> 20 m)", near * 100, longr * 100);
        r.modes.push_back({"ranked", buf});
    }
    if (!fair.empty()) { float s = 0; for (float f : fair) s += f; r.fairness = s / fair.size(); }
    // ---- feature list (creativity)
    if (r.ladders) r.features.push_back("ladders");
    if (m.hasVoid || r.voidEdge > 0.05f) r.features.push_back("lethal drops");
    if (r.wallbang) r.features.push_back("wallbangs");
    if (m.payloadPath.size() >= 2) r.features.push_back("moving cart");
    if (r.levels >= 4) r.features.push_back(std::to_string(r.levels) + " floor levels");
    if (r.grapple > 0.6f) r.features.push_back("grapple-friendly");
    if (r.lanes > 30) r.features.push_back("long speed lanes");
    if (r.lights >= 6) r.features.push_back("lit interiors");
    if (r.far > 0) r.features.push_back("skyline");
    if (r.hideSpots > 0.15f) r.features.push_back("lots of hiding spots");
}

float clamp10(float v) { return std::clamp(v, 0.0f, 10.0f); }

} // namespace

void mapReport(const char* outPath) {
    std::vector<Metrics> all;
    for (const std::string& name : listMaps()) {
        Map m;
        if (!m.load(name)) continue;
        NavGraph g;
        g.build(m);
        Metrics r;
        measure(r, m, g);
        printf("  %-10s %4d brushes, %4.0f m2, %d levels, visible %.0f%%, grapple %.0f%%\n", r.name.c_str(), r.brushes, r.area, r.levels, r.visible * 100, r.grapple * 100);
        all.push_back(std::move(r));
    }
    // originality: distance to the nearest other map in normalised feature space, and footprint overlap
    const int F = 9;
    auto feat = [](const Metrics& r) {
        return std::vector<float>{(float)r.levels, r.vertRange, r.loops / std::max(1.0f, r.area / 100), r.visible, r.cover,
                                  r.grapple, r.lanes, r.band[0] + r.band[1], r.detailDensity};
    };
    std::vector<float> mean(F, 0), sd(F, 0);
    for (auto& r : all) { auto f = feat(r); for (int i = 0; i < F; i++) mean[i] += f[i] / all.size(); }
    for (auto& r : all) { auto f = feat(r); for (int i = 0; i < F; i++) sd[i] += (f[i] - mean[i]) * (f[i] - mean[i]) / all.size(); }
    for (auto& s : sd) s = std::sqrt(s) + 1e-6f;
    for (auto& r : all) {
        auto fr = feat(r);
        float best = 1e9f;
        for (auto& o : all) {
            if (&o == &r) continue;
            auto fo = feat(o);
            float d = 0;
            for (int i = 0; i < F; i++) d += ((fr[i] - fo[i]) / sd[i]) * ((fr[i] - fo[i]) / sd[i]);
            d = std::sqrt(d / F);
            // footprint similarity (best of 4 rotations): blends in so same-shaped maps count as close
            float simBest = 0;
            for (int rot = 0; rot < 4; rot++) {
                int same = 0, any = 0;
                for (int z = 0; z < 24; z++)
                    for (int x = 0; x < 24; x++) {
                        int xr = x, zr = z;
                        for (int k = 0; k < rot; k++) { int t = xr; xr = 23 - zr; zr = t; }
                        float a = r.occ[z * 24 + x], b = o.occ[zr * 24 + xr];
                        if (a < 0 && b < 0) continue;
                        any++;
                        same += a >= 0 && b >= 0 && std::fabs(a - b) < 0.2f;
                    }
                simBest = std::max(simBest, any ? (float)same / any : 0);
            }
            d *= 1.0f - 0.5f * simBest;
            if (d < best) { best = d; r.closest = o.name; }
        }
        r.originality = best;
    }
    // scores (formulas in docs/MAP_METRICS.md)
    std::map<std::string, int> featureCount;
    for (auto& r : all) for (auto& f : r.features) featureCount[f.substr(f.find(' ') == std::string::npos || !isdigit((unsigned char)f[0]) ? 0 : f.find(' ') + 1)]++;
    for (auto& r : all) {
        r.sDecor = clamp10(4 * std::min(r.detailDensity / 12, 1.0f) + 4 * (1 - r.bareWalls) + 2 * std::min(r.lights / 30.0f, 1.0f));
        float bandMix = 0; // evenness of close / mid / long sightlines (entropy of the first three bands, 0-1)
        for (int b = 0; b < 3; b++) if (r.band[b] > 0) bandMix -= r.band[b] * std::log(r.band[b]) / std::log(3.0f);
        float loopsPerK = r.loops / std::max(1.0f, r.area / 1000);
        r.sLayout = clamp10(2 * std::min(r.levels / 8.0f, 1.0f) + 2 * std::min(loopsPerK / 20, 1.0f) + 3 * std::min(r.coverPts / 0.45f, 1.0f) + 3 * bandMix - 0.15f * r.deadEnds);
        r.sMovement = clamp10(3 * r.grapple + 1.5f * r.headroom + 2.5f * std::min(r.lanes / 40, 1.0f) + 2 * std::min(r.jumpLinks / 0.6f, 1.0f) + (r.ladders ? 1 : 0));
        float uniq = 0;
        for (auto& f : r.features) {
            std::string k = f.substr(isdigit((unsigned char)f[0]) ? f.find(' ') + 1 : 0);
            uniq += featureCount[k] <= (int)all.size() / 3 ? 1.0f : 0.4f;
        }
        r.sCreativity = clamp10(uniq * 1.5f + std::min(r.vertRange / 4, 2.0f));
        r.sBalance = r.fairness >= 0 ? clamp10(r.fairness * 10) : -1;
        r.sOriginality = clamp10(r.originality * 8);
    }
    // ---- write the report
    std::ofstream o(outPath);
    o << "# Map report\n\nGenerated by `build/quark_tests mapreport` from the map files, the bot nav graph and line-of-sight rays. "
         "Metric definitions and score formulas: [MAP_METRICS.md](MAP_METRICS.md). Scores are 0-10; a dash means not applicable.\n\n";
    o << "## Scores\n\n| map | for | decor | layout | movement | creativity | originality | balance |\n|---|---|---|---|---|---|---|---|\n";
    char b[512];
    for (auto& r : all) {
        snprintf(b, sizeof b, "| %s | %s | %.1f | %.1f | %.1f | %.1f | %.1f | %s |\n", r.name.c_str(), r.preferred.c_str(), r.sDecor, r.sLayout, r.sMovement, r.sCreativity, r.sOriginality,
                 r.sBalance >= 0 ? (std::to_string((int)std::lround(r.sBalance * 10) / 10) + "." + std::to_string((int)std::lround(r.sBalance * 10) % 10)).c_str() : "-");
        o << b;
    }
    o << "\n## Decor / detail\n\n| map | brushes | detail | per 100 m² | bare walls | materials | glow | metal | lights | skyline |\n|---|---|---|---|---|---|---|---|---|---|\n";
    for (auto& r : all) {
        snprintf(b, sizeof b, "| %s | %d | %d | %.1f | %.0f%% | %d | %d | %d | %d | %d |\n", r.name.c_str(), r.brushes, r.detail, r.detailDensity, r.bareWalls * 100, r.materials, r.glow, r.metal, r.lights, r.far);
        o << b;
    }
    o << "\n## Layout and sightlines\n\n| map | floor m² | levels | height range | loops | dead ends | visible pairs | <6 m | 6-20 m | 20-32 m | >32 m | cover |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (auto& r : all) {
        snprintf(b, sizeof b, "| %s | %.0f | %d | %.1f m | %d | %d | %.0f%% | %.0f%% | %.0f%% | %.0f%% | %.0f%% | %.0f%% |\n", r.name.c_str(), r.area, r.levels, r.vertRange, r.loops, r.deadEnds,
                 r.visible * 100, r.band[0] * 100, r.band[1] * 100, r.band[2] * 100, r.band[3] * 100, r.coverPts * 100);
        o << b;
    }
    o << "\n## Movement\n\n| map | grapple anchors | headroom > 6 m | speed lanes (p90) | jump links | ladders | near a lethal drop |\n|---|---|---|---|---|---|---|\n";
    for (auto& r : all) {
        snprintf(b, sizeof b, "| %s | %.0f%% | %.0f%% | %.0f m | %.0f%% | %d | %.0f%% |\n", r.name.c_str(), r.grapple * 100, r.headroom * 100, r.lanes, r.jumpLinks * 100, r.ladders, r.voidEdge * 100);
        o << b;
    }
    o << "\n## Per map: shots and modes\n\n";
    for (auto& r : all) {
        o << "### " << r.name << "\n\n![" << r.name << ": 8 orbits and 8 eye-level views](map_shots/" << r.name << ".jpg)\n\n";
        if (!r.features.empty()) { o << "Features: "; for (size_t i = 0; i < r.features.size(); i++) o << (i ? ", " : "") << r.features[i]; o << "\n\n"; }
        for (auto& [mode, line] : r.modes) o << "- " << mode << ": " << line << "\n";
        snprintf(b, sizeof b, "- most similar map: %s (distance %.2f)\n\n", r.closest.c_str(), r.originality);
        o << b;
    }
    printf("map report written to %s\n", outPath);
}
