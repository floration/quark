#pragma once
// COPIED from tests/mapreport.cpp at 07ef254 (the measurement half: everything before mapReport()), adapted for
// quark_maptool: in namespace mr, no camera files written (the map shots' .cams block is gone), the movement envelope
// cached on disk and the hot loops threaded (marked "maptool:"). The map grading (tests/mapreport.cpp,
// docs/MAP_REPORT.md, docs/MAP_METRICS.md, `quark_tests mapreport`) was removed from the tests (the owner: maps will be
// human-designed); this copy is now the only one, and the old metric definitions are in git at 07ef254
// (docs/MAP_METRICS.md). The tests keep only the out-of-bounds check, trimmed, in tests/mapbounds.cpp.
// Map analysis: measures a map from its geometry, the bot nav graph and line-of-sight rays.
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "game/bots.h"
#include "game/gamedefs.h"
#include "game/map.h"
#include "game/pmove.h"
#include "walkgrid.h"
#include "walkfast.h"   // maptool: parallelFor

namespace mr {

// maptool: the measurement's parts timed (mrLaps: name, ms since the lap before; mrLap(nullptr) starts)
inline std::vector<std::pair<std::string, double>>& mrLaps() { static std::vector<std::pair<std::string, double>> v; return v; }
inline void mrLap(const char* what) {
    static std::chrono::steady_clock::time_point t;
    auto now = std::chrono::steady_clock::now();
    if (what) mrLaps().push_back({what, std::chrono::duration<double, std::milli>(now - t).count()});
    else mrLaps().clear();
    t = now;
}

// Weapon range bands (from the weapon set): close = shotgun / knife, mid = rifle / rocket / sticky,
// long = laser / crossbow out to the laser's reach (the weapon table's: it follows any change to the laser),
// extreme = only the sniper (and a scoped rifle) reaches.
constexpr float kBandClose = 6.0f, kBandMid = 20.0f;
float bandLong() { return weaponDef(WPN_LASER).range; }
constexpr float kRunSpeed = 9.5f;   // pmove kGroundSpeed (1.17: 8 -> 9.5)
// Attack / defend timing (owner decision B2): at every bomb site and capture point, the attackers' arrival minus the
// defenders' (positive: the defenders are there first) must be within this band. Graded at the worst site / point.
constexpr float kLeadLo = -1.0f, kLeadHi = 1.0f;
constexpr float kLoopTile = 4.0f;
constexpr float kLoopsFull = 3.0f;  // loops per 1000 m2 of floor for full marks: rooms about 18 m across, each with four ways out (MAP_METRICS.md)
constexpr float kJumpReach = 1.2f;     // a standing jump clears ~1.2 m (pmove: 7.67 m/s up, 24 m/s^2 gravity)
constexpr float kEye = 1.6f;

struct Rng { uint32_t s = 12345; float f() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.0f; } };

struct Metrics {
    std::string name, title, preferred;
    bool human = false;        // a human-made map (isHumanMadeMap): graded and reported, but the bar doesn't fail the run
    std::vector<std::string> belowBar;   // what it misses the bar by (human-made maps: listed in the report)
    std::set<std::string> exempt;        // categories its own data lets it off ("# exempt ..." lines: mapExemptions)
    // decor / detail
    int brushes = 0, detail = 0, glow = 0, metal = 0, accent = 0, wallbang = 0, far = 0, lights = 0, materials = 0;
    float detailDensity = 0;   // detail brushes per 100 m^2 of walkable floor
    float bareWalls = 0;       // share of wall hits (6-40 m from players) with no detail within 1 m
    // layout
    float area = 0;            // walkable m^2
    int levels = 0;            // floor heights with >= 10 m^2 (clustered 1.5 m apart)
    float vertRange = 0;
    int nodes = 0, links = 0, deadEnds = 0;
    float loops = 0;           // ways round (walkLoops: the mean over four placements of its tile grid)
    float visible = 0;         // share of random point pairs (eye height) that see each other
    float band[4] = {};        // share of visible pairs in close / mid / long / extreme range
    float cover = 0;           // mean share of 16 directions blocked within 3 m at chest height
    float coverPts = 0;        // share of points with >= 4 of 16 directions blocked
    // movement
    float jumpLinks = 0;       // share of nav links that need a jump (rise > step height)
    float grapple = 0;         // share of points with a hookable surface >= 3 m up within grapple range
    float headroom = 0;        // share of points with > 6 m above (vertical dash, rocket jumps)
    float roofed = 0;          // share of points with a ceiling within 12 m (indoor spaces)
    float fill = 1;            // share of the footprint's bounding box that is floor (1 = a plain rectangle)
    bool liquids = false;      // water / lava you play round (40 m2 or more of it: a fountain doesn't count)
    float liquidArea = 0;      // m2 of water / lava surface
    float lanes = 0;           // 90th percentile of the longest straight run from a point (m): dash / bhop lanes
    float voidEdge = 0;        // share of points within 2 m of a lethal drop
    int ladders = 0;
    // movement sub-scores (0-10 each; the movement score is their mean) and what they measured
    float mGrapple = 0, mLanes = 0, mVertical = 0, mHeadroom = 0, mRisk = 0, mTech = 0;
    std::string techLine;
    float techExtra = 0, outOfBounds = 0; // m2 only movement tech reaches; the part of it outside the playable footprint
    float lLevels = 0, lLoops = 0, lCover = 0, lRange = 0, lDead = 0; // layout parts (points out of 2, 2, 3, 3, minus)      // race: the shortcut check per leg; other modes: how much movement tech shortens trips
    // modes
    std::vector<std::pair<std::string, std::string>> modes; // mode -> measured line
    float fairness = -1;       // 0-1: the worst objective's fairness / timing band (team maps), -1: no team modes
    std::string worstFair;     // which objective that is
    std::vector<std::pair<std::string, float>> leads;   // every attack / defend arrival: attackers' minus defenders' (s)
    struct ModeFit { std::string mode; float fit; std::string why; };
    std::vector<ModeFit> modeFits;   // a map built for every mode: its fit in each of the others
    float hideSpots = 0;       // share of points seen by < 5% of others
    // originality
    std::vector<float> occ;    // 24x24 top-down footprint (max floor height, normalised)
    float originality = 0;
    std::string closest;
    // scores 0-10
    float sDecor = 0, sLayout = 0, sMovement = 0, sCreativity = 0, sBalance = 0, sOriginality = 0, sMode = -1;
    std::string modeFit;       // how the mode-fit score was reached
    std::string setting;       // "setting <words>" in the map: what place it is (a setting no other map has scores)
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

float clamp10(float v) { return std::clamp(v, 0.0f, 10.0f); }

// Spots of the walk grid (tests/walkgrid.h) a player can get to from some spawn without movement tech: the
// reachability every metric uses (floor nobody can walk to gets no credit).
std::vector<char> walkReachable(const Map& m, const WalkGrid& w) {
    std::vector<char> comps(w.compLinks.size(), 0), out(w.nodes.size(), 0);
    for (auto& sp : m.spawns) {
        int n = w.nodeAt(sp.pos);
        if (n < 0 || comps[w.comp[n]]) continue;
        auto seen = w.reach(n);
        for (size_t c = 0; c < seen.size(); c++) comps[c] |= seen[c];
    }
    for (size_t a = 0; a < w.nodes.size(); a++) out[a] = comps[w.comp[a]];
    return out;
}

// Loops: independent ways round, on the reachable walk grid. Only two-way moves count (walk, step, jump up and back
// down, a gap jumped both ways, ladders): a drop you can't climb back up is no way round, and it would glue a roof to
// the street below it. The spots are gathered into kLoopTile tiles (per tile, each piece of it you can move about in
// without leaving the tile is one node: the floor and a crate you can jump onto are one, the two sides of a wall, or
// a floor and the roof over it, are two), linked where a two-way move crosses from a tile to the next. An open floor
// makes a grid of tiles, and every 2 x 2 block of linked tiles is filled in (a face), so what's left is the first Betti number
// (links - nodes + parts - faces): the holes you can go round, i.e. a route round each obstacle wider than a tile,
// each building, each pit; and up one way and down another (stairs and a ladder, a ramp and a drop) when the two
// differ. A flat platform has none, a crate or a pillar narrower than a tile makes none. Ladders link the tiles at
// their foot and at their top. Whether an obstacle 3-7 m across fills a whole tile depends on where the grid falls,
// so the report takes the mean over four placements of the grid (offset by half a tile each way): such an obstacle
// counts in proportion to its size, the same wherever it stands.
int walkLoops(const WalkGrid& w, const std::vector<char>& reach, float ox, float oz) {
    const int N = (int)w.nodes.size() - w.ladderHubs;
    std::vector<int> uf(N);
    for (int i = 0; i < N; i++) uf[i] = i;
    auto find = [&](int a) { while (uf[a] != a) a = uf[a] = uf[uf[a]]; return a; };
    auto tileOf = [&](int a) { const Vec3& p = w.nodes[a].p; return std::make_pair((int)std::floor((p.x - w.x0 + ox) / kLoopTile), (int)std::floor((p.z - w.z0 + oz) / kLoopTile)); };
    auto twoWay = [&](int a, int b) { return std::find(w.links[b].begin(), w.links[b].end(), a) != w.links[b].end(); };
    for (int a = 0; a < N; a++) {
        if (!reach[a]) continue;
        auto ta = tileOf(a);
        for (int b : w.links[a]) if (b < N && reach[b] && tileOf(b) == ta && twoWay(a, b)) uf[find(a)] = find(b);
    }
    std::map<int, int> node;                              // piece root -> coarse node
    std::map<std::pair<int, int>, std::vector<int>> tile; // tile -> its coarse nodes
    std::vector<int> nodeOf(N, -1);
    for (int a = 0; a < N; a++) {
        if (!reach[a]) continue;
        int r = find(a);
        auto it = node.find(r);
        if (it == node.end()) { it = node.emplace(r, (int)node.size()).first; tile[tileOf(a)].push_back(it->second); }
        nodeOf[a] = it->second;
    }
    const int V = (int)node.size();
    std::vector<std::set<int>> adj(V);
    auto link = [&](int u, int v) { if (u != v && u >= 0 && v >= 0) { adj[u].insert(v); adj[v].insert(u); } };
    for (int a = 0; a < N; a++) {
        if (!reach[a]) continue;
        auto ta = tileOf(a);
        for (int b : w.links[a]) {
            if (b >= N || !reach[b]) continue;
            auto tb = tileOf(b);
            if (std::abs(ta.first - tb.first) + std::abs(ta.second - tb.second) == 1 && twoWay(a, b)) link(nodeOf[a], nodeOf[b]);   // (side by side only)
        }
    }
    for (int h = N; h < (int)w.nodes.size(); h++) {   // a ladder: its floors from the foot to the top, one link per level
        std::vector<std::pair<float, int>> at;
        for (int a : w.links[h]) if (a < N && reach[a]) at.push_back({w.nodes[a].p.y, nodeOf[a]});
        std::sort(at.begin(), at.end());
        for (size_t k = 1; k < at.size(); k++) if (at[k].first - at[k - 1].first > 1.0f) link(at[k - 1].second, at[k].second);
    }
    long E = 0;
    for (auto& s : adj) E += (long)s.size();
    E /= 2;
    int C = 0;
    {
        std::vector<char> seen(V, 0);
        for (int s = 0; s < V; s++) {
            if (seen[s]) continue;
            C++;
            std::vector<int> st = {s};
            seen[s] = 1;
            while (!st.empty()) { int u = st.back(); st.pop_back(); for (int v : adj[u]) if (!seen[v]) { seen[v] = 1; st.push_back(v); } }
        }
    }
    long F = 0;   // 2 x 2 blocks of tiles whose four nodes link round
    for (auto& [t, ns] : tile) {
        auto right = tile.find({t.first + 1, t.second}), down = tile.find({t.first, t.second + 1}), diag = tile.find({t.first + 1, t.second + 1});
        if (right == tile.end() || down == tile.end() || diag == tile.end()) continue;
        for (int a : ns)
            for (int b : right->second) {
                if (!adj[a].count(b)) continue;
                for (int c : down->second) {
                    if (!adj[a].count(c)) continue;
                    for (int d : diag->second) F += adj[b].count(d) && adj[c].count(d);
                }
            }
    }
    return (int)std::max(0L, E - V + C - F);
}

// Ways onto an area (the hill, a capture point): the directions you can step, jump, drop or climb onto it from,
// on the walk grid. Every move from outside it to inside counts by where it comes from: which of 8 sectors round
// the area's middle, and which 3 m band of height. A whole open side is one way; a doorway, a stair, a drop from a
// balcony above, another. (Counting nav links instead gave Downtown's plaza 110 ways.)
int waysOnto(const WalkGrid& w, const std::vector<char>& reach, const AABB& zone) {
    std::set<int> in;
    for (int a : w.nodesIn(zone)) in.insert(a);
    const int N = (int)w.nodes.size() - w.ladderHubs;
    Vec3 c = zone.center();
    std::set<std::pair<int, int>> dirs;
    auto add = [&](int a) {
        const Vec3& p = w.nodes[a].p;
        int sector = ((int)std::floor((std::atan2(p.z - c.z, p.x - c.x) + 3.14159265f) / (6.2831853f / 8))) & 7;
        dirs.insert({sector, (int)std::floor((p.y - zone.mn.y) / 3.0f)});
    };
    w.forColumnsNear(zone, 4.0f, [&](int a) {
        if (in.count(a) || !reach[a]) return;
        for (int b : w.links[a]) {
            if (b >= N) { for (int x : w.links[b]) if (in.count(x)) { add(a); return; } continue; }   // (up a ladder onto it)
            if (in.count(b)) { add(a); return; }
        }
    });
    return (int)dirs.size();
}

// ---- the movement envelope, measured with the game's own movement code -----------------------------------------
// What a good player can do as the game plays (owner decision 5.4: auto-bhop, unbounded strafing and the dash's limit
// are intended): build speed by bunny-hopping, strafing and dashing; turn speed into distance and height with a jump
// and an upward dash; ride the grapple. Simulated with pmove once per report, so it follows any change to movement.
struct MoveEnvelope {
    static constexpr float kBin = 0.5f;          // horizontal distance bins
    static constexpr int kBins = 200;            // (100 m)
    static constexpr float kLowest = -40.0f;     // drops followed this far (there's no fall damage)
    std::vector<float> speed;                    // tier speeds, m/s (tier 0: running)
    std::vector<float> runUp;                    // metres of moving from a standstill it takes to reach each tier
    std::vector<std::vector<float>> top;         // [tier][bin]: the highest an arc from that speed (or slower) gets at that distance, relative to take-off
    std::vector<std::vector<float>> apexLevel;   // [tier][bin]: the lowest apex among those arcs that are still at take-off height or above there
    std::vector<std::vector<float>> pullExit;    // [tier][k]: speed after a grapple pull of 5k m toward an anchor ahead, from that tier's speed
    float buildTime = 0;                         // seconds of perfect play to the top tier
    int tiers() const { return (int)speed.size(); }
    int tierFor(float metres) const { int k = 0; while (k + 1 < tiers() && runUp[k + 1] <= metres) k++; return k; }
    int tierForSpeed(float v) const { int k = 0; while (k + 1 < tiers() && speed[k + 1] <= v + 0.01f) k++; return k; }
    // the slowest tier whose arcs reach (h across, dy up), or -1
    int need(float h, float dy) const {
        int b = (int)(h / kBin);
        if (b >= kBins || dy < kLowest) return -1;
        for (int k = 0; k < tiers(); k++) if (top[k][b] >= dy) return k;
        return -1;
    }
    float reach(int k, float dy) const { float r = 0; for (int b = 0; b < kBins; b++) if (top[k][b] >= dy) r = (b + 1) * kBin; return r; }
    float rise(int k) const { float r = -1e9f; for (float v : top[k]) r = std::max(r, v); return r; }
    float maxReach() const { return reach(tiers() - 1, kLowest); }
    int pullTier(int k, float len) const {
        size_t i = std::min(pullExit[k].size() - 1, (size_t)std::lround(len / 5.0f));
        return tierForSpeed(pullExit[k][i]);
    }
};

static float hspeed(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }

// maptool: computeMoveEnvelope() is the report's envelope; moveEnvelope() reads it from a cache file kept per build of
// the tool (measure.cpp cachedMoveEnvelope: the movement code can't have changed without a relink)
MoveEnvelope cachedMoveEnvelope();
MoveEnvelope computeMoveEnvelope() {
    return [] {
        auto t0 = std::chrono::steady_clock::now();
        auto lap = [&](const char* what) { if (std::getenv("MR_TIME")) fprintf(stderr, "envelope: %s by %.0f ms\n", what, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); };
        MoveEnvelope e;
        // (past 28 m/s an upward dash gets no higher: its exit speed is capped; 32 and 36 m/s: super dashes)
        e.speed = {kRunSpeed, 12, 16, 20, 24, 28, 32, 36};
        // speed from a standstill: jump held (auto-bhop), dashing whenever a charge is there and it adds speed, and each
        // millisecond the strafe / turn that gains the most (greedy: a good player, not a tool-assisted one). Two
        // ways, the better per tier: dashing as soon as it adds speed (a dash jump when it starts on the floor), and
        // super dashes (looking down, dashing on a bhop's landing tick whenever the dash is ready).
        Map flat;
        flat.parse("box -600 -1 -600 600 0 600\nspawn any 0 0 0\n");
        e.runUp.assign(e.speed.size(), 1e9f);
        e.runUp[0] = 0;
        std::vector<float> runUpBy[2] = {e.runUp, e.runUp};   // maptool: both ways at once, merged below (min: either order)
        float buildBy[2] = {0, 0};
        parallelFor(2, [&](int superDash) {
            std::vector<float>& runUp = runUpBy[superDash];
            float& buildTime = buildBy[superDash];
            MoveState s;
            s.flags = MF_ONGROUND;
            float dist = 0;
            bool dashed = false;
            size_t next = 1;
            for (int t = 0; t < 30000 && next < e.speed.size(); t++) {
                float h = hspeed(s.vel), vyaw = h > 0.1f ? std::atan2(-s.vel.x, -s.vel.z) : 0;
                // (charges: 2, the ground refills them, a bhop landing one, a dash jump all, a super dash leaves one; and
                // the 1.2 s cooldown from the end of the last dash;
                // above 31 m/s a dash slows you: it leaves at your speed + 1, at most 32)
                bool dash = !dashed && s.dashCharges > 0 && s.dashCooldownMs == 0 && s.dashTimer <= 0 && h < 31.0f;
                if (superDash) dash = !dashed && s.dashCharges > 0 && s.dashCooldownMs == 0 && s.dashTimer <= 0 && (s.flags & MF_ONGROUND) && h < 35.9f;
                MoveState best;
                float bestH = -1;
                bool bestDash = false;
                for (int side = 0; side < 2; side++)
                    for (int a = -90; a <= 90; a += 3) {
                        UserCmd c;
                        c.msec = 1;
                        c.buttons = BTN_JUMP | (dash ? BTN_DASH : 0);
                        c.pitch = superDash ? -1.2f : 0.0f;
                        if (side) c.side = 127; else c.forward = 127;
                        c.yaw = vyaw + a * 0.0174533f + (side ? 1.5707963f : 0.0f);
                        MoveState q = s;
                        pmove(q, c, flat);
                        if (hspeed(q.vel) > bestH) { bestH = hspeed(q.vel); best = q; bestDash = dash; }
                    }
                dashed = bestDash;
                dist += std::sqrt((best.pos.x - s.pos.x) * (best.pos.x - s.pos.x) + (best.pos.z - s.pos.z) * (best.pos.z - s.pos.z));
                s = best;
                if (s.dashTimer <= 0 && hspeed(s.vel) >= e.speed[next]) {
                    if (dist < runUp[next]) runUp[next] = dist;
                    if (++next == e.speed.size()) buildTime = buildTime > 0 ? std::min(buildTime, (t + 1) / 1000.0f) : (t + 1) / 1000.0f;
                }
            }   // (a tier neither way reaches keeps 1e9: never)
        }, 1);
        for (int w = 0; w < 2; w++) {
            for (size_t k = 0; k < e.runUp.size(); k++) e.runUp[k] = std::min(e.runUp[k], runUpBy[w][k]);
            if (buildBy[w] > 0) e.buildTime = e.buildTime > 0 ? std::min(e.buildTime, buildBy[w]) : buildBy[w];
        }
        lap("speed build-up");
        // take-offs: moving at the tier's speed, jump, then (or not) an upward dash after tau at pitch p, and with the
        // charges left (two; a dash jump off the take-off refills them) more dashes the same way: chained (each as the
        // last ends) or one 0.3 s later. Each arc is followed until it is kLowest below the take-off; the envelope is
        // the highest any of them is at each distance.
        Map pad;
        pad.parse("box -1 -1 -1 1 0 1\nspawn any 0 0 0\n");
        e.top.assign(e.speed.size(), std::vector<float>(MoveEnvelope::kBins, -1e9f));
        e.apexLevel.assign(e.speed.size(), std::vector<float>(MoveEnvelope::kBins, 1e9f));
        parallelFor((int)e.speed.size(), [&](int k) {   // maptool: every tier at once (the "slower reaches it too" pass after)
            for (int ti = 0; ti <= 5; ti++)
                for (int pi = -2; pi <= 9; pi++)   // (no dash, a down dash (a super dash off the take-off; aimed as far down as the
                                                   // mouse goes, 88.8 deg: its hop is highest there, a full jump), or one at
                                                   // 0-90 degrees up, 0-0.4 s after the jump)
                for (int more = 0; more < (pi == -1 ? 1 : 4); more++) {   // dashes after it: none, 1 or 2 chained, one 0.3 s later
                    float tau = ti * 0.08f, pitch = pi == -2 ? -1.55f : pi < 0 ? 0 : pi * 10 * 0.0174533f;
                    MoveState s;
                    s.pos = {0, 0, 0.9f};
                    s.vel = {0, 0, -e.speed[k]};
                    s.flags = MF_ONGROUND;
                    std::vector<float> y(MoveEnvelope::kBins, -1e9f);
                    float apex = 0;
                    for (int t = 0; t < 6000; t++) {
                        UserCmd c;
                        c.msec = 1;
                        c.forward = 127;
                        c.pitch = pitch;
                        int t0 = (int)(tau * 1000), n = more == 3 ? 1 : more, gapMs = more == 3 ? 300 : 125;
                        bool press = pi != -1 && t >= t0 && (t - t0) % gapMs < 5 && (t - t0) / gapMs <= n;
                        c.buttons = BTN_JUMP | (press ? BTN_DASH : 0);
                        pmove(s, c, pad);
                        apex = std::max(apex, s.pos.y);
                        int b = (int)((0.9f - s.pos.z) / MoveEnvelope::kBin);
                        if (b >= 0 && b < MoveEnvelope::kBins) y[b] = std::max(y[b], s.pos.y);
                        if (s.pos.y < MoveEnvelope::kLowest) break;
                    }
                    for (int b = 0; b < MoveEnvelope::kBins; b++) {
                        e.top[k][b] = std::max(e.top[k][b], y[b]);
                        if (y[b] >= 0) e.apexLevel[k][b] = std::min(e.apexLevel[k][b], apex);
                    }
                }
        }, 1);
        for (size_t k = 1; k < e.speed.size(); k++)   // anything a slower take-off reaches, a faster player reaches too (slowing down is free)
                for (int b = 0; b < MoveEnvelope::kBins; b++) {
                    e.top[k][b] = std::max(e.top[k][b], e.top[k - 1][b]);
                    e.apexLevel[k][b] = std::min(e.apexLevel[k][b], e.apexLevel[k - 1][b]);
                }
        lap("take-offs");
        // grapple pulls: an anchor 5k m ahead (on a wall, at eye height), from each tier's speed: the speed when it lets go
        e.pullExit.assign(e.speed.size(), std::vector<float>(10, 0));
        parallelFor((int)e.speed.size() * 10, [&](int ki) {   // maptool: every pull at once
                int k = ki / 10, i = ki % 10;
                float L = std::max(2.0f, i * 5.0f);
                char txt[160];
                snprintf(txt, sizeof txt, "box -20 -1 -60 20 0 20\nbox -10 0 %g 10 20 %g\nspawn any 0 0 0\n", -L - 1, -L);
                Map wall;
                wall.parse(txt);
                MoveState s;
                s.flags = MF_ONGROUND;
                s.vel = {0, 0, -e.speed[k]};
                float best = -1;   // the fastest it gets you going while it pulls (you let go before the wall)
                for (int t = 0; t < 4000; t++) {
                    UserCmd c;
                    c.msec = 1;
                    c.buttons = BTN_GRAPPLE;
                    pmove(s, c, wall);
                    if (s.flags & MF_HOOKED) best = std::max(best, hspeed(s.vel));
                    else if (best >= 0) break;
                }
                e.pullExit[k][i] = best >= 0 ? best : e.speed[k];
        }, 1);
        lap("grapple pulls");
        return e;
    }();
}
const MoveEnvelope& moveEnvelope() {
    static const MoveEnvelope env = cachedMoveEnvelope();
    return env;
}

// Movement-tech graph over the floor samples. Two graphs:
// - the run: what the bots' nav calls walkable (running, steps, ordinary jumps, ladders);
// - the tech path: the run plus leaps and grapple pulls, with the speed you carry (MoveEnvelope tiers) as part of the
//   state: moving builds speed (runUp: metres from a standstill, so circling to gain speed costs the metres it takes),
//   a ladder costs all of it, a pull leaves you at what the pull gives, and a leap needs the speed its arc takes.
// Shortest paths here are what a good player can do; the nav graph is the intended route.
struct TechGraph {
    enum { RUN, LEAP, PULL, LADDER };
    struct Edge { int to; float cost; uint8_t need, kind; };
    std::vector<Vec3> p;
    std::vector<std::vector<Edge>> adj;                   // the tech graph
    std::vector<std::vector<std::pair<int, float>>> walk; // running, ordinary jumps (2.6 m gaps, 1.15 m up) and ladders only
    std::vector<char> narrow;                              // sample stands on a surface under 2 m across
    std::vector<char> clipped;                             // sample is inside clip (a wall top under the invisible ceiling)
    std::set<std::pair<int, int>> jumps;                   // walk links that need a jump (a gap or more than a step up)
    const MoveEnvelope* env = nullptr;
    static constexpr float kHubLeap = 24.0f;              // leaps longer than this only between hub samples
    int leapCount = 0, fastLeaps = 0;                      // (leaps, and the ones that need more than running speed)
    // samples by 6 m column
    float cx0 = 0, cz0 = 0;
    int cw = 0, ch = 0;
    std::vector<std::vector<int>> cells;
    int cellOf(float x, float z) const {
        int i = (int)std::floor((x - cx0) / 6), j = (int)std::floor((z - cz0) / 6);
        return i < 0 || j < 0 || i >= cw || j >= ch ? -1 : j * cw + i;
    }
    int nearest(const Vec3& q, float maxD = 1e9f) const {
        int best = -1; float bd = maxD;
        int ci = (int)std::floor((q.x - cx0) / 6), cj = (int)std::floor((q.z - cz0) / 6);
        for (int dx = -2; dx <= 2; dx++)
            for (int dz = -2; dz <= 2; dz++) {
                int i = ci + dx, j = cj + dz;
                if (i < 0 || j < 0 || i >= cw || j >= ch) continue;
                for (int s : cells[j * cw + i]) { float d = length(p[s] - q); if (d < bd) { bd = d; best = s; } }
            }
        if (best < 0 && maxD >= 1e8f) for (size_t s = 0; s < p.size(); s++) { float d = length(p[s] - q); if (d < bd) { bd = d; best = (int)s; } }
        return best;
    }
    // m: the map as players collide with it (clip included: it blocks leaps). runReach: how far apart two samples on
    // one floor (or two linked floors) are still a run (6 m; more where the samples are sparse, on the biggest maps,
    // so the run isn't cut into pieces by the sampling: Lodestone's race legs read "-1 m", REVIEW MAP-4)
    void build(const Map& m, const NavGraph& g, const std::vector<Vec3>& pts, float runReach = 6.0f) {
        env = &moveEnvelope();
        const MoveEnvelope& E = *env;
        p = pts;
        std::vector<int> node(p.size());
        narrow.assign(p.size(), 0);
        clipped.assign(p.size(), 0);
        for (size_t i = 0; i < p.size(); i++) {
            clipped[i] = m.boxSolid(playerBox(p[i] + Vec3(0, 0.02f, 0), false));   // (inside clip: nobody stands there)
            node[i] = g.nodeAt(p[i] + Vec3(0, 0.05f, 0));
            if (node[i] >= 0) { auto& n = g.nodes[node[i]]; narrow[i] = std::min(n.x1 - n.x0, n.z1 - n.z0) < 2.0f; }
        }
        auto navLinked = [&](int a, int b) {
            if (a < 0 || b < 0) return false;
            if (a == b) return true;
            for (auto& l : g.links[a]) if (l.first == b) return true;
            return false;
        };
        adj.assign(p.size(), {});
        walk.assign(p.size(), {});
        float x1 = -1e9f, z1 = -1e9f;
        cx0 = cz0 = 1e9f;
        for (auto& q : p) { cx0 = std::min(cx0, q.x); cz0 = std::min(cz0, q.z); x1 = std::max(x1, q.x); z1 = std::max(z1, q.z); }
        cw = p.empty() ? 0 : (int)((x1 - cx0) / 6) + 1;
        ch = p.empty() ? 0 : (int)((z1 - cz0) / 6) + 1;
        cells.assign((size_t)cw * ch, {});
        for (size_t i = 0; i < p.size(); i++) cells[cellOf(p[i].x, p[i].z)].push_back((int)i);
        auto clear = [&](const Vec3& a, const Vec3& b) { Vec3 d = b - a; float l = length(d); return l < 0.01f || !m.raycastAny(a, d / l, l, true); };
        // the climb out of / down into a sample, to a chest-height line at y top (cached: every leap from here asks)
        // maptool: the per-sample loop below runs on every core: a memo, the jumps and the leap counts per worker
        const int W = maptoolThreads();
        std::vector<std::map<std::pair<int, int>, bool>> riseMemos(W);
        std::vector<std::set<std::pair<int, int>>> jumpsBy(W);
        std::vector<int> leapsBy(W, 0), fastBy(W, 0);
        auto rise = [&](std::map<std::pair<int, int>, bool>& riseMemo, int i, float top) {
            auto key = std::make_pair(i, (int)std::ceil(top * 4));
            auto it = riseMemo.find(key);
            if (it != riseMemo.end()) return it->second;
            bool ok = clear(p[i] + Vec3(0, 1.0f, 0), Vec3(p[i].x, key.second / 4.0f, p[i].z));
            riseMemo[key] = ok;
            return ok;
        };
        const float reachMax = E.maxReach();
        const int span = (int)std::ceil(reachMax / 6) + 1;
        parallelForWorker((int)p.size(), [&](int ii, int wk) {
            const size_t i = (size_t)ii;
            auto& riseMemo = riseMemos[wk];
            auto& jumps = jumpsBy[wk];
            int& leapCount = leapsBy[wk];
            int& fastLeaps = fastBy[wk];
            int ci = (int)std::floor((p[i].x - cx0) / 6), cj = (int)std::floor((p[i].z - cz0) / 6);
            for (int dz = -span; dz <= span; dz++)
                for (int dx = -span; dx <= span; dx++) {
                    int a = ci + dx, b = cj + dz;
                    if (a < 0 || b < 0 || a >= cw || b >= ch) continue;
                    if ((std::max(0, std::abs(dx) - 1) * 6.0f) * (std::max(0, std::abs(dx) - 1) * 6.0f) + (std::max(0, std::abs(dz) - 1) * 6.0f) * (std::max(0, std::abs(dz) - 1) * 6.0f) > reachMax * reachMax) continue;
                    for (int j : cells[b * cw + a]) {
                        if (j == (int)i || clipped[i] || clipped[j]) continue;   // (clip: nobody stands there, whatever the nav says)
                        Vec3 d = p[j] - p[i];
                        float h = std::sqrt(d.x * d.x + d.z * d.z);
                        // the run: what the bots' nav calls walkable (same surface, or a step / ordinary jump between two)
                        if (h <= runReach && navLinked(node[i], node[j])) {
                            walk[i].push_back({j, length(d)});
                            adj[i].push_back({j, length(d), 0, RUN});
                            if (node[i] != node[j]) {
                                auto &na = g.nodes[node[i]], &nb = g.nodes[node[j]];
                                float gx = std::max(0.0f, std::max(na.x0, nb.x0) - std::min(na.x1, nb.x1));
                                float gz = std::max(0.0f, std::max(na.z0, nb.z0) - std::min(na.z1, nb.z1));
                                if (gx * gx + gz * gz > 0.3f * 0.3f || nb.y - na.y > 0.55f) jumps.insert({(int)i, j});
                            }
                            continue;
                        }
                        // a leap: the slowest take-off whose arcs get there. Past standing dash-jump range only between
                        // every third sample (hubs a few metres apart: it's the long way across that matters, and it
                        // keeps the report quick)
                        if (h > kHubLeap && (i % 3 != 0 || j % 3 != 0)) continue;
                        if (clipped[i] || clipped[j]) continue;
                        int k = E.need(h, d.y);
                        if (k < 0) continue;
                        // the arc must be clear: up out of i, across at chest height over the higher end (or the arc's
                        // own apex, for a leap only a fast take-off makes), and down into j: three lines (the middle and
                        // both shoulders, so a player-wide body has to fit; a slit doesn't count as a way through).
                        // Within dash-jump range the shoulders' climbs and drops are checked too.
                        int bin = std::min(MoveEnvelope::kBins - 1, (int)(h / MoveEnvelope::kBin));
                        float apex = E.apexLevel[k][bin] < 1e8f ? E.apexLevel[k][bin] : 0;
                        float top = std::max(std::max(p[i].y, p[j].y) + 1.2f, p[i].y + apex + 1.0f);
                        bool fits = rise(riseMemo, (int)i, top) && rise(riseMemo, j, top);
                        if (fits) {
                            Vec3 side = h > 0.01f ? Vec3(-d.z / h, 0, d.x / h) * 0.35f : Vec3();
                            for (float o : {0.0f, -1.0f, 1.0f}) {
                                Vec3 a0 = p[i] + side * o, b0 = p[j] + side * o;
                                if (o != 0 && h <= 16.0f && (!clear(a0 + Vec3(0, 1.0f, 0), Vec3(a0.x, top, a0.z)) || !clear(Vec3(b0.x, top, b0.z), b0 + Vec3(0, 1.0f, 0)))) { fits = false; break; }
                                if (!clear(Vec3(a0.x, top, a0.z), Vec3(b0.x, top, b0.z))) { fits = false; break; }
                            }
                        }
                        if (!fits) continue;
                        adj[i].push_back({j, length(d), (uint8_t)k, LEAP});
                        leapCount++;
                        fastLeaps += k > 0;
                    }
                }
            // grapple pulls
            Vec3 eye = p[i] + Vec3(0, kEye, 0);
            for (int k = 0; k < 16; k++)
                for (float el : {0.3f, 0.7f, 1.1f}) {
                    float a = k * 6.2831853f / 16;
                    Vec3 d = normalize(Vec3(std::sin(a) * std::cos(el), std::sin(el), std::cos(a) * std::cos(el)));
                    float t = m.raycast(eye, d, kGrappleRange);
                    if (t < 2.0f) continue;
                    Vec3 hit = eye + d * t;
                    int j = nearest(hit, 4.0f);
                    if (j < 0 || j == (int)i || p[j].y > hit.y + 0.5f || clipped[j]) continue;
                    // the hook goes through clip, but you don't: the way to where you land must be open to a player
                    if (!clear(eye, hit - d * 0.5f) || !clear(hit - d * 0.5f, p[j] + Vec3(0, 1.0f, 0))) continue;
                    adj[i].push_back({j, t + length(p[j] - hit), 0, PULL});
                }
        }, 8);
        for (int wk = 0; wk < W; wk++) { jumps.insert(jumpsBy[wk].begin(), jumpsBy[wk].end()); leapCount += leapsBy[wk]; fastLeaps += fastBy[wk]; }
        // ladders: from the floor at the foot to the floor at the top, in both graphs
        for (const AABB& l : m.ladders) {
            Vec3 c = l.center();
            int lo = nearest({c.x, l.mn.y, c.z}, 2.5f);
            int hi = -1; float bd = 3.0f;
            for (size_t j = 0; j < p.size(); j++) {
                if (std::fabs(p[j].y - l.mx.y) > 0.8f) continue;
                float dd = std::hypot(p[j].x - c.x, p[j].z - c.z);
                if (dd < bd) { bd = dd; hi = (int)j; }
            }
            if (lo < 0 || hi < 0 || lo == hi) continue;
            float cost = (l.mx.y - l.mn.y) * 1.8f + bd; // climbing is slower than running
            adj[lo].push_back({hi, cost, 0, LADDER}); walk[lo].push_back({hi, cost});
            adj[hi].push_back({lo, cost, 0, LADDER}); walk[hi].push_back({lo, cost});
        }
    }
    // Shortest paths from sample s (standing still: tier 0). Per sample, the shortest over the speeds you can be
    // there with. prev / prevKind (per state, sample * tiers + tier) let walkLeg trace the path back.
    struct Paths { std::vector<float> d; std::vector<int> best, prev; std::vector<uint8_t> kind; };
    mutable std::map<std::pair<int, bool>, Paths> memo;
    const Paths& paths(int s, bool tech) const {
        auto key = std::make_pair(s, tech);
        auto it = memo.find(key);
        if (it != memo.end()) return it->second;
        Paths& P = memo[key];
        const int T = tech ? env->tiers() : 1, N = (int)p.size();
        std::vector<float> d((size_t)N * T, 1e30f);
        P.prev.assign((size_t)N * T, -1);
        P.kind.assign((size_t)N * T, RUN);
        P.d.assign(N, 1e30f);
        P.best.assign(N, -1);
        if (s >= 0) {
            using QE = std::pair<float, int>;
            std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
            d[(size_t)s * T] = 0; q.push({0, s * T});
            auto relax = [&](int u, int v, float c, uint8_t kind) { if (d[u] + c < d[v]) { d[v] = d[u] + c; P.prev[v] = u; P.kind[v] = kind; q.push({d[v], v}); } };
            while (!q.empty()) {
                auto [du, u] = q.top(); q.pop();
                if (du > d[u]) continue;
                int i = u / T, k = u % T;
                if (!tech) { for (auto& [j, c] : walk[i]) relax(u, j, c, RUN); continue; }
                for (const Edge& e : adj[i]) {
                    if (e.need > k) continue;
                    int k2 = e.kind == LADDER ? 0 : e.kind == PULL ? env->pullTier(k, e.cost) : std::max(k, env->tierFor(env->runUp[k] + e.cost));
                    relax(u, e.to * T + k2, e.cost, e.kind);
                }
            }
        }
        for (int i = 0; i < N; i++)
            for (int k = 0; k < T; k++) if (d[(size_t)i * T + k] < P.d[i]) { P.d[i] = d[(size_t)i * T + k]; P.best[i] = i * T + k; }
        return P;
    }
    std::vector<float> from(int s, bool tech = true) const { return paths(s, tech).d; }
    // shortest distance from a to anywhere inside box (tech: with the tech links)
    float toBox(const Vec3& a, const AABB& box, bool tech = true) const {
        const auto& d = paths(nearest(a), tech).d;
        float best = -1;
        for (size_t i = 0; i < p.size(); i++) {
            if (d[i] > 1e29f) continue;
            const Vec3& q = p[i];
            if (q.x < box.mn.x - 0.5f || q.x > box.mx.x + 0.5f || q.z < box.mn.z - 0.5f || q.z > box.mx.z + 0.5f || q.y < box.mn.y - 1.0f || q.y > box.mx.y) continue;
            if (best < 0 || d[i] < best) best = d[i];
        }
        return best;
    }
    // the route to a box, with what it asks of you
    // tech = false: the plain run; tech = true: the fastest way with leaps and the grapple, and what it still asks
    // of you (a grapple pull asks nothing; a leap counts as one jump; narrow footing only counts where you actually
    // run on it)
    struct Leg { float len = -1; int jumps = 0; float narrowLen = 0; float pulled = 0; int fastLeaps = 0; };
    Leg walkLeg(const Vec3& a, const AABB& box, bool tech = false) const {
        const Paths& P = paths(nearest(a), tech);
        int best = -1;
        for (size_t i = 0; i < p.size(); i++) {
            if (P.d[i] > 1e29f) continue;
            const Vec3& q = p[i];
            if (q.x < box.mn.x - 0.5f || q.x > box.mx.x + 0.5f || q.z < box.mn.z - 0.5f || q.z > box.mx.z + 0.5f || q.y < box.mn.y - 1.0f || q.y > box.mx.y) continue;
            if (best < 0 || P.d[i] < P.d[best]) best = (int)i;
        }
        Leg L;
        if (best < 0) return L;
        L.len = P.d[best];
        const int T = tech ? env->tiers() : 1;
        for (int vs = P.best[best]; P.prev[vs] >= 0; vs = P.prev[vs]) {
            int us = P.prev[vs], u = us / T, v = vs / T;
            uint8_t kind = P.kind[vs];
            bool walked = kind == RUN || kind == LADDER;
            L.jumps += (walked && jumps.count({u, v})) || kind == LEAP;
            L.fastLeaps += kind == LEAP && us % T > 0;
            if (kind == PULL) L.pulled += length(p[v] - p[u]);
            if (walked && narrow[u] && narrow[v]) L.narrowLen += length(p[v] - p[u]);
            if (std::getenv("MR_PATH") && tech) fprintf(stderr, "    %.1f %.1f %.1f -> %.1f %.1f %.1f %s%s n%d%d tier %d\n", p[u].x, p[u].y, p[u].z, p[v].x, p[v].y, p[v].z, kind == RUN ? "run" : kind == LEAP ? "leap" : kind == PULL ? "pull" : "ladder", jumps.count({u, v}) ? "+jump" : "", narrow[u], narrow[v], us % T);
        }
        return L;
    }
};

bool los(const Map& m, const Vec3& a, const Vec3& b) {
    Vec3 d = b - a;
    float len = length(d);
    return len < 0.01f || !m.raycastAny(a, d / len, len);
}

const char* modeLabel(int m) { return modeName(m); }

// solid: the map as players collide with it (m without clip is what everyone sees and plays on)
// w: the walk grid over `solid`; reach: its spots a player can get to from a spawn (walkReachable)
void measure(Metrics& r, const Map& m, const Map& solid, const NavGraph& g, const WalkGrid& w, const std::vector<char>& reach) {
    mrLap(nullptr);   // maptool: timings (start)
    Rng rng;
    r.name = m.name; r.title = m.title;
    r.preferred = m.preferredMode >= 0 ? modeLabel(m.preferredMode) : "any";
    // ---- decor / detail
    bool used[16] = {};
    std::vector<int> detailIdx;
    // a detail brush buried in solid on every side (a seam inside a wall, trim sunk in the floor) dresses nothing
    auto buried = [&](const AABB& b) {
        Vec3 c = b.center();
        const Vec3 probe[6] = {{b.mn.x - 0.02f, c.y, c.z}, {b.mx.x + 0.02f, c.y, c.z}, {c.x, b.mn.y - 0.02f, c.z},
                               {c.x, b.mx.y + 0.02f, c.z}, {c.x, c.y, b.mn.z - 0.02f}, {c.x, c.y, b.mx.z + 0.02f}};
        for (const Vec3& q : probe) if (!m.boxSolid({q - Vec3(0.005f, 0.005f, 0.005f), q + Vec3(0.005f, 0.005f, 0.005f)})) return false;
        return true;
    };
    for (size_t i = 0; i < m.brushes.size(); i++) {
        const Brush& b = m.brushes[i];
        Vec3 s = b.box.mx - b.box.mn;
        float mn = std::min({s.x, s.y, s.z});
        r.brushes++;
        used[std::clamp(b.material, 0, 15)] = true;   // (each material its own: trim is not lava)
        if (b.material == 1) r.accent++;
        if (b.material == 2) r.wallbang++;
        if (b.material == 3) r.metal++;
        if (b.material == 4) r.glow++;
        if (b.material == 5) { r.far++; continue; } // backdrop scenery outside the walls: counted apart
        if ((mn < 0.6f || s.x * s.y * s.z < 2.0f) && !buried(b.box)) { r.detail++; detailIdx.push_back((int)i); }
        if ((b.material == 6 || b.material == 7) && !buried(b.box)) r.liquidArea += s.x * s.z;
    }
    for (bool u : used) r.materials += u;
    r.lights = (int)m.lights.size();
    r.ladders = (int)m.ladders.size();
    // ---- walkable samples from the nav surfaces (2 m grid, standing room only)
    std::vector<Pt> pts;
    std::map<int, float> levelArea;
    float ymin = 1e9f, ymax = -1e9f;
    // floor you can walk to from a spawn (running, jumping, ladders) on the collision-checked walk grid: the playable
    // floor. Roofs and wall tops only a grapple reaches still get samples for the movement-tech graph, but don't count
    // as floor, and nothing nobody can walk to gets credit anywhere (C1).
    std::vector<char> playable(g.nodes.size(), 0);
    std::vector<float> standArea(g.nodes.size(), 0);   // m2 of each surface a player can stand on and get to
    for (size_t i = 0; i < g.nodes.size(); i++) {
        auto& n = g.nodes[i];
        if (n.y <= m.killY()) continue;   // (below the kill height you're dead)
        AABB box = {{n.x0 - 0.4f, n.y - 0.2f, n.z0 - 0.4f}, {n.x1 + 0.4f, n.y + 0.2f, n.z1 + 0.4f}};
        w.forColumnsNear(box, 0, [&](int a) {
            const Vec3& q = w.nodes[a].p;
            if (!reach[a] || std::fabs(q.y - n.y) >= 0.15f || q.x < box.mn.x || q.x > box.mx.x || q.z < box.mn.z || q.z > box.mx.z) return;
            playable[i] = 1;
            if (q.x >= n.x0 && q.x <= n.x1 && q.z >= n.z0 && q.z <= n.z1) standArea[i] += WalkGrid::kCol * WalkGrid::kCol;   // (a column's worth of it to stand on)
        });
    }
    for (size_t i = 0; i < g.nodes.size(); i++) {
        auto& n = g.nodes[i];
        float a = (n.x1 - n.x0) * (n.z1 - n.z0);
        if (playable[i]) {
            r.area += a;
            levelArea[(int)std::floor(n.y / 1.5f)] += a;
            ymin = std::min(ymin, n.y); ymax = std::max(ymax, n.y);
        }
        // every 2 m; a surface under 2 m across (a stair step, a beam) gets one row down its middle
        float sx = n.x1 - n.x0 < 2 ? (n.x0 + n.x1) / 2 : n.x0 + 1, sz = n.z1 - n.z0 < 2 ? (n.z0 + n.z1) / 2 : n.z0 + 1;
        for (float x = sx; x < n.x1; x += 2)
            for (float z = sz; z < n.z1; z += 2) {
                Vec3 p = {x, n.y, z};
                if (!m.boxSolid(playerBox(p + Vec3(0, 0.02f, 0), false))) pts.push_back({p, (int)i});
            }
    }
    for (auto& [k, a] : levelArea) r.levels += a >= 10.0f;
    if (std::getenv("MR_LEVELS") && r.name == std::getenv("MR_LEVELS")) for (auto& [k, a] : levelArea) fprintf(stderr, "  band %.1f m: %.0f m2\n", k * 1.5f, a);
    {   // the height range of the floor you play on: between the lowest and the highest real floors (10 m2 or more
        // in their 1.5 m band: a post top or a crate doesn't make a map vertical)
        float lo = 1e9f, hi = -1e9f;
        for (size_t i = 0; i < g.nodes.size(); i++) {
            auto& n = g.nodes[i];
            if (!playable[i] || levelArea[(int)std::floor(n.y / 1.5f)] < 10.0f) continue;
            lo = std::min(lo, n.y); hi = std::max(hi, n.y);
        }
        r.vertRange = hi > lo ? hi - lo : 0;
    }
    r.detailDensity = r.area > 0 ? r.detail * 100.0f / r.area : 0;
    mrLap("samples");   // maptool: timings
    TechGraph tech;
    float techCell = 4;   // m2 of floor per tech sample
    std::vector<char> techPlayable;
    {
        std::vector<Vec3> tp;
        size_t stride = pts.size() / 2500 + 1; // up to ~2500 points
        for (size_t i = 0; i < pts.size(); i += stride) { tp.push_back(pts[i].p); techPlayable.push_back(playable[pts[i].node]); }
        techCell = 4.0f * stride;
        // and a sample on every floor inside each objective (race checkpoints, bomb sites, the hill, capture points)
        // and under every spawn: a thin sampling must not make a leg or a trip read as unreachable
        std::vector<AABB> zones = m.checkpoints;
        zones.insert(zones.end(), m.bombSites.begin(), m.bombSites.end());
        zones.insert(zones.end(), m.points.begin(), m.points.end());
        if (m.hasHill) zones.push_back(m.hill);
        for (const AABB& z : zones)
            for (size_t i = 0; i < g.nodes.size(); i++) {
                auto& n = g.nodes[i];
                if (n.x1 < z.mn.x || n.x0 > z.mx.x || n.z1 < z.mn.z || n.z0 > z.mx.z || n.y < z.mn.y - 1.0f || n.y > z.mx.y) continue;
                Vec3 q = {std::clamp(z.center().x, n.x0, n.x1), n.y, std::clamp(z.center().z, n.z0, n.z1)};
                if (m.boxSolid(playerBox(q + Vec3(0, 0.02f, 0), false))) continue;
                tp.push_back(q); techPlayable.push_back(playable[i]);
            }
        for (auto& sp : m.spawns) {
            int n = g.nodeAt(sp.pos + Vec3(0, 0.05f, 0));
            if (n < 0) continue;
            tp.push_back({sp.pos.x, g.nodes[n].y, sp.pos.z}); techPlayable.push_back(playable[n]);
        }
        tech.build(solid, g, tp, std::max(6.0f, 3.0f * std::sqrt(techCell)));
    }
    {   std::vector<Pt> keep; for (auto& pt : pts) if (playable[pt.node]) keep.push_back(pt); pts.swap(keep); }
    if (pts.size() > 1500) { // keep it quick: an even subsample
        std::vector<Pt> sub;
        for (size_t i = 0; i < pts.size(); i += pts.size() / 1500 + 1) sub.push_back(pts[i]);
        pts.swap(sub);
    }
    const int P = (int)pts.size();
    if (P < 2) return;
    mrLap("techGraph");   // maptool: timings
    // ---- nav graph shape
    r.nodes = (int)g.nodes.size();
    int jumpLinks = 0;
    auto spawnReach = navDist(g, nearestNode(g, m.spawns.empty() ? Vec3() : m.spawns[0].pos));
    for (size_t i = 0; i < g.links.size(); i++) {
        int deg = 0;
        for (auto& [j, c] : g.links[i]) {
            if ((int)i < j) r.links++;
            deg++;
            if ((int)i < j && std::fabs(g.nodes[j].y - g.nodes[i].y) > 0.55f) jumpLinks++;
        }
        // a dead end is a floor you can only leave one way (a corridor end), not a crate top you can drop off
        // on every side: probe each side for a way out (room to stand, and a non-lethal floor within 6 m below)
        auto& n = g.nodes[i];
        // (its floor you can actually stand on and get to: a wall top under the wall above it, but for the arch cut
        // through it, is the arch's sill, not a 32 m2 dead end)
        if (standArea[i] <= 4 || deg > 1 || spawnReach[i] > 1e29f || !playable[i]) continue; // one link in, and somewhere you can get to (alive)
        int exits = 0;
        for (int side = 0; side < 4; side++) {
            bool out = false;
            for (float f : {0.25f, 0.5f, 0.75f}) {
                float x = side < 2 ? n.x0 + (n.x1 - n.x0) * f : (side == 2 ? n.x0 - 0.7f : n.x1 + 0.7f);
                float z = side < 2 ? (side == 0 ? n.z0 - 0.7f : n.z1 + 0.7f) : n.z0 + (n.z1 - n.z0) * f;
                Vec3 q = {x, n.y + 0.6f, z};
                if (m.boxSolid(playerBox(q, false))) continue;
                float down = m.raycast(q, {0, -1, 0}, 6.6f);
                if (down >= 0 && q.y - down > m.killY() + 0.5f) { out = true; break; }
            }
            exits += out;
        }
        if (exits <= 1) {
            r.deadEnds++;
            if (getenv("MR_DEAD") && r.name == getenv("MR_DEAD")) fprintf(stderr, "dead end: x %.1f..%.1f z %.1f..%.1f at y %.1f\n", n.x0, n.x1, n.z0, n.z1, n.y);
        }
    }
    // (ways round, on the reachable walk grid: not nav-surface fragments)
    r.loops = 0;
    {   // maptool: the four placements at once, summed in the same order
        int lp[4] = {};
        parallelFor(4, [&](int k) { lp[k] = walkLoops(w, reach, (k >> 1) * 0.5f * kLoopTile, (k & 1) * 0.5f * kLoopTile); }, 1);
        for (int k = 0; k < 4; k++) r.loops += lp[k] / 4.0f;
    }
    r.jumpLinks = r.links ? (float)jumpLinks / r.links : 0;
    mrLap("navShape");   // maptool: timings
    // ---- per-point rays: cover, lanes, headroom, grapple, void edges, bare walls
    int wallHits = 0, bareHits = 0, covered = 0, grapple = 0, head = 0, nearVoid = 0, roofedN = 0;
    std::vector<char> coveredAt; // per sample: >= 4 of 16 directions blocked within 3 m
    std::vector<float> lanes;
    float coverSum = 0;
    // maptool: every sample on every core (each its own record), then added up in sample order as before
    struct PtRays { int wallHits = 0, bareHits = 0, blocked = 0; float longest = 0; bool head = false, roofed = false, hook = false, edge = false; };
    std::vector<PtRays> rays(pts.size());
    parallelFor((int)pts.size(), [&](int pi) {
        const Pt& pt = pts[pi];
        PtRays& R = rays[pi];
        int& wallHits = R.wallHits; int& bareHits = R.bareHits;
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
        R.blocked = blocked;
        R.longest = longest;
        float up = m.raycast(eye, {0, 1, 0}, 30.0f);
        R.head = up < 0 || up > 6.0f;
        R.roofed = up >= 0 && up < 12.0f;
        bool hook = false;
        for (int k = 0; k < 8 && !hook; k++)
            for (float el : {0.5f, 1.0f}) {
                float a = k * 6.2831853f / 8;
                Vec3 d = normalize(Vec3(std::sin(a) * std::cos(el), std::sin(el), std::cos(a) * std::cos(el)));
                float t = m.raycast(eye, d, kGrappleRange);
                if (t >= 0 && (eye + d * t).y - pt.p.y >= 3.0f) { hook = true; break; }
            }
        R.hook = hook;
        bool edge = false;
        for (int k = 0; k < 8 && !edge; k++) {
            float a = k * 6.2831853f / 8;
            Vec3 probe = pt.p + Vec3(std::sin(a) * 2.0f, 0.5f, std::cos(a) * 2.0f);
            if (m.boxSolid(playerBox(probe, false))) continue;
            float down = m.raycast(probe, {0, -1, 0}, 200.0f);
            if (down < 0 || probe.y - down < m.killY()) edge = true;
        }
        R.edge = edge;
    }, 8);
    for (size_t pi = 0; pi < pts.size(); pi++) {
        const PtRays& R = rays[pi];
        wallHits += R.wallHits; bareHits += R.bareHits;
        coverSum += R.blocked / 16.0f;
        covered += R.blocked >= 4;
        coveredAt.push_back(R.blocked >= 4);
        if (std::getenv("MR_COVER") && r.name == std::getenv("MR_COVER")) printf("COVER %.1f %.2f %.1f %d %.0f\n", pts[pi].p.x, pts[pi].p.y, pts[pi].p.z, R.blocked, R.longest);
        lanes.push_back(R.longest);
        head += R.head; roofedN += R.roofed; grapple += R.hook; nearVoid += R.edge;
    }
    r.cover = coverSum / P;
    r.coverPts = (float)covered / P;
    r.grapple = (float)grapple / P;
    r.headroom = (float)head / P;
    r.roofed = (float)roofedN / P;
    r.liquids = r.liquidArea >= 40.0f;   // (a fountain's basin isn't water you play round)
    r.voidEdge = (float)nearVoid / P;
    r.bareWalls = wallHits ? (float)bareHits / wallHits : 0;
    std::sort(lanes.begin(), lanes.end());
    r.lanes = lanes[(size_t)(lanes.size() * 0.9)];
    mrLap("rays");   // maptool: timings
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
        bands[d < kBandClose ? 0 : d < kBandMid ? 1 : d < bandLong() ? 2 : 3]++;
    }
    r.visible = pairs ? (float)vis / pairs : 0;
    for (int b = 0; b < 4; b++) r.band[b] = vis ? (float)bands[b] / vis : 0;
    int hidden = 0;
    for (int i = 0; i < P; i++) hidden += tried[i] && (float)seen[i] / tried[i] < 0.05f;
    r.hideSpots = (float)hidden / P;
    mrLap("sightlines");   // maptool: timings
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
    {   int used = 0; for (float v : r.occ) used += v >= 0; r.fill = used / 576.0f; }
    mrLap("footprint");   // maptool: timings
    // ---- movement tech: race = the shortcut check per leg; other modes = how much tech shortens the main trips
    {
        char tb[512];
        if (!m.checkpoints.empty() && m.supportsMode(MODE_RACE)) {
            Vec3 at = m.spawns.empty() ? Vec3() : m.spawns[0].pos;
            for (auto& sp : m.spawns) if (sp.team == SPAWN_RACE) { at = sp.pos; break; }
            float worst = 1; int worstLeg = 0, totJumps = 0; float totLen = 0, totNarrow = 0, totPulled = 0;
            std::string legs;
            for (size_t k = 0; k < m.checkpoints.size(); k++) {
                const AABB& c = m.checkpoints[k];
                auto L = tech.walkLeg(at, c), T = tech.walkLeg(at, c, true);
                float route = L.len, skip = T.len;
                if (skip > 0) { totLen += skip; totJumps += T.jumps; totNarrow += T.narrowLen; totPulled += T.pulled; }
                // a leg the run can't do scores nothing (it used to read as a 1.00x leg: REVIEW MAP-4)
                float ratio = (route > 0 && skip > 0) ? route / skip : 99.0f;
                if (route > 0 && skip > 0)
                    snprintf(tb, sizeof tb, "%s%zu: %.0f m run / %.0f m with tech (%.2fx), %d jumps, %.0f m narrow, %.0f m grappled", legs.empty() ? "" : "; ", k + 1, route, skip, ratio, T.jumps, T.narrowLen, T.pulled);
                else snprintf(tb, sizeof tb, "%s%zu: UNREACHABLE (no run to it)", legs.empty() ? "" : "; ", k + 1);
                legs += tb;
                if (ratio > worst) { worst = ratio; worstLeg = (int)k + 1; }
                at = c.center(); at.y = c.mn.y;
            }
            // a race is only as good as its weakest part: no leg you can cut (shortcut), and a course that is still
            // hard for a player who dashes and grapples well. Measured along that player's path (the tech path):
            // jumps (8 per 100 m for full marks), narrow footing actually run on (30%), and how little of it the
            // grapple carries you (none of the 2 points at 30% or more)
            float shortcut = clamp10((2.5f - worst) / (2.5f - 1.25f) * 10);
            float jp = totLen > 0 ? totJumps * 100.0f / totLen : 0, ns = totLen > 0 ? totNarrow / totLen : 0, gs = totLen > 0 ? totPulled / totLen : 0;
            float challenge = 4 * std::min(jp / 8.0f, 1.0f) + 4 * std::min(ns / 0.3f, 1.0f) + 2 * (1 - std::min(gs / 0.3f, 1.0f));
            // (on a map built for another mode the race is a side mode: there only the shortcut check counts)
            r.mTech = m.preferredMode == MODE_RACE ? std::min(shortcut, challenge) : shortcut;
            char worstLine[64];
            if (worst >= 99.0f) snprintf(worstLine, sizeof worstLine, "leg %d unreachable", worstLeg);
            else snprintf(worstLine, sizeof worstLine, "worst leg %d at %.2fx", worstLeg, worst);
            snprintf(tb, sizeof tb, "race: shortcut %.1f (%s; 10 at <= 1.25x, 0 at >= 2.5x), challenge %.1f on the tech path (%.1f jumps per 100 m, %.0f%% narrow, %.0f%% grappled) - legs: ",
                     shortcut, worstLine, challenge, jp, ns * 100, gs * 100);
            r.techLine = tb + legs;
        } else {
            // trips from every spawn to the objectives / the other spawns: tech should help (>= 1.15x) without breaking (<= 1.8x)
            std::vector<AABB> targets;
            if (m.hasHill) targets.push_back(m.hill);
            for (auto& b : m.bombSites) targets.push_back(b);
            if (targets.empty()) for (auto& sp : m.spawns) targets.push_back({sp.pos - Vec3(1, 0, 1), sp.pos + Vec3(1, 2, 1)});
            float sum = 0; int n = 0;
            for (size_t i = 0; i < m.spawns.size() && i < 8; i++)
                for (size_t t = 0; t < targets.size() && t < 8; t++) {
                    float route = tech.toBox(m.spawns[i].pos, targets[t], false), skip = tech.toBox(m.spawns[i].pos, targets[t]);
                    if (route > 3 && skip > 0) { sum += route / skip; n++; }
                }
            float avg = n ? sum / n : 1;
            r.mTech = avg < 1.15f ? clamp10(4 + (avg - 1) / 0.15f * 6) : avg <= 1.8f ? 10 : clamp10(10 - (avg - 1.8f) / 0.7f * 10);
            snprintf(tb, sizeof tb, "movement tech shortens main trips by %.2fx on average (10 for 1.15-1.8x: it pays off without trivialising the map)", avg);
            r.techLine = tb;
        }
        // what the full envelope opens up: surfaces the run never reaches (roofs, wall tops: the cheese), and any of
        // them outside the playable floor's footprint (out of bounds)
        if (!m.spawns.empty()) {
            const auto& d = tech.from(tech.nearest(m.spawns[0].pos));
            float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
            // (the playable floor itself, every surface of it: not the sparse samples, whose edge moves with the sampling)
            for (size_t i = 0; i < g.nodes.size(); i++) if (playable[i]) { auto& n = g.nodes[i]; x0 = std::min(x0, n.x0); x1 = std::max(x1, n.x1); z0 = std::min(z0, n.z0); z1 = std::max(z1, n.z1); }
            int extra = 0, out = 0;
            for (size_t i = 0; i < tech.p.size(); i++) {
                if (d[i] > 1e29f || techPlayable[i]) continue;
                extra++;
                const Vec3& q = tech.p[i];
                bool o = q.x < x0 - 1 || q.x > x1 + 1 || q.z < z0 - 1 || q.z > z1 + 1;
                out += o;
                if (o && std::getenv("MR_OOB") && r.name == std::getenv("MR_OOB")) fprintf(stderr, "  out of bounds: %.1f %.1f %.1f (floor spans x %.1f..%.1f z %.1f..%.1f)\n", q.x, q.y, q.z, x0, x1, z0, z1);
            }
            r.techExtra = extra * techCell;
            r.outOfBounds = out * techCell;
            snprintf(tb, sizeof tb, "; with every move a good player also reaches %.0f m2 the run doesn't (roofs, wall tops), %.0f m2 of it outside the playable footprint; %d of %d leaps need a run-up",
                     r.techExtra, r.outOfBounds, tech.fastLeaps, tech.leapCount);
            r.techLine += tb;
        }
    }
    // cover on an area (the hill, a capture point): every 1 m of playable floor inside it, the share with >= 4 of 16
    // directions blocked within 3 m at chest height (the sightline samples are far too sparse for a 14 m point on
    // the biggest maps: one with no sample in it read as no cover at all)
    auto coverIn = [&](const AABB& z) {
        int hc = 0, hn = 0;
        for (size_t i = 0; i < g.nodes.size(); i++) {
            auto& n = g.nodes[i];
            if (!playable[i] || n.y < z.mn.y - 0.5f || n.y > z.mx.y || n.x1 < z.mn.x || n.x0 > z.mx.x || n.z1 < z.mn.z || n.z0 > z.mx.z) continue;
            for (float x = std::max(n.x0, z.mn.x) + 0.5f; x < std::min(n.x1, z.mx.x); x += 1)
                for (float zz = std::max(n.z0, z.mn.z) + 0.5f; zz < std::min(n.z1, z.mx.z); zz += 1) {
                    Vec3 p = {x, n.y, zz};
                    if (m.boxSolid(playerBox(p + Vec3(0, 0.02f, 0), false))) continue;
                    Vec3 chest = p + Vec3(0, 1.0f, 0);
                    int blocked = 0;
                    for (int k = 0; k < 16; k++) {
                        float a = k * 6.2831853f / 16, tt = m.raycast(chest, {std::sin(a), 0, std::cos(a)}, 3.0f);
                        blocked += tt >= 0 && tt < 3.0f;
                    }
                    hn++; hc += blocked >= 4;
                }
        }
        return hn ? (float)hc / hn : 0.0f;
    };
    mrLap("techTrips");   // maptool: timings
    // ---- modes: what each mode cares about, measured on this map
    std::vector<Vec3> spA, spB;
    for (auto& sp : m.spawns) { if (sp.team == SPAWN_A) spA.push_back(sp.pos); if (sp.team == SPAWN_B) spB.push_back(sp.pos); }
    bool teams = !spA.empty() && !spB.empty();
    auto teamDist = [&](const std::vector<Vec3>& sps, const Vec3& target) { // mean walking distance from the team's spawns
        float sum = 0; int n = 0;
        for (auto& p : sps) { float d = navBetween(g, p, target); if (d >= 0) { sum += d; n++; } else return -1.0f; }   // (one spawn that can't get there: unreachable)
        return n ? sum / n : -1.0f;
    };
    char buf[512];
    auto boxDist = [&](const std::vector<Vec3>& sps, const AABB& box) {
        float sum = 0; int n = 0;
        for (auto& p : sps) { float d = navToBox(g, p, box); if (d >= 0) { sum += d; n++; } else return -1.0f; }
        return n ? sum / n : -1.0f;
    };
    auto band = [](float v, float lo, float hi, float soft) { // 1 inside [lo, hi], falling to 0 over `soft` outside
        return v < lo ? std::max(0.0f, 1 - (lo - v) / soft) : v > hi ? std::max(0.0f, 1 - (v - hi) / soft) : 1.0f;
    };
    // Balance (C1): per team, for every objective of every team mode the map offers; the map's balance is the WORST of
    // them. Symmetric objectives: fairness = the shorter walk / the longer. Attack / defend objectives (bomb sites,
    // capture points): the B2 band, 1 when the attackers' arrival minus the defenders' is within -1..+1 s, falling to
    // 0 four seconds outside it.
    std::vector<std::pair<std::string, float>> fair;
    auto fairLine = [&](const char* what, float da, float db) {
        if (da < 0 || db < 0) { fair.push_back({what, 0.0f}); return std::string(what) + ": UNREACHABLE for a side"; }
        float f = std::min(da, db) / std::max(std::max(da, db), 0.01f);
        fair.push_back({what, f});
        snprintf(buf, sizeof buf, "%s: A %.0f m, B %.0f m (fairness %.2f)", what, da, db, f);
        return std::string(buf);
    };
    auto teamLine = [&](const char* what, const Vec3& target) { return fairLine(what, teamDist(spA, target), teamDist(spB, target)); };
    // every attack / defend arrival (the bar holds each to the B2 band): what, attackers' - defenders' arrival (s)
    std::vector<std::pair<std::string, float>> leads;
    auto lead = [&](const std::string& what, float att, float def) {   // walks in m -> the lead in s (NaN: unreachable)
        float l = att < 0 || def < 0 ? NAN : (att - def) / kRunSpeed;
        leads.push_back({what, l});
        fair.push_back({what, std::isnan(l) ? 0.0f : band(l, kLeadLo, kLeadHi, 4.0f)});
        return l;
    };
    auto leadText = [](float l) {
        char t[64];
        if (std::isnan(l)) return std::string("UNREACHABLE");
        snprintf(t, sizeof t, "%s first by %.1f s", l >= 0 ? "defenders" : "attackers", std::fabs(l));
        return std::string(t);
    };
    // multi point: mean walk to point i from the spawns that side uses while attacking / defending it (staged spawns)
    auto pointDist = [&](size_t i, bool defending) {
        int stage = -1;
        for (auto& sp : m.spawns) if (isStagedSpawn(sp.team) && spawnDefends(sp.team) == defending && spawnStage(sp.team) <= (int)i) stage = std::max(stage, spawnStage(sp.team));
        std::vector<Vec3> sps;
        for (auto& sp : m.spawns) if (sp.team == stagedSpawn(std::max(stage, 0), defending)) sps.push_back(sp.pos);
        if (getenv("MR_POINTS")) for (auto& p : sps) printf("    point %zu %s spawn %.0f %.0f %.0f: %.0f m (node %d)\n", i, defending ? "def" : "att", p.x, p.y, p.z, navToBox(g, p, m.points[i]), nearestNode(g, p));
        return boxDist(sps, m.points[i]);
    };
    auto across = [&](const std::vector<Vec3>& from, const std::vector<Vec3>& to) {   // each spawn to the nearest of the others
        float d = 0; int n = 0;
        for (auto& a : from) { float best = 1e9f; for (auto& b2 : to) { float x = navBetween(g, a, b2); if (x >= 0) best = std::min(best, x); } if (best < 1e8f) { d += best; n++; } else return -1.0f; }
        return n ? d / n : -1.0f;
    };
    if (std::getenv("MR_FROM") && r.name == std::getenv("MR_FROM")) {
        // debugging objective placement: each nav surface's mean walk from team A's and team B's spawns, as
        // "FROM <surface centre x y z> <x0 x1 z0 z1> <A m> <B m>" lines on stdout
        std::vector<std::vector<float>> da, db;
        for (auto& p : spA) { auto d = navDist(g, nearestNode(g, p)); auto& n = g.nodes[nearestNode(g, p)]; float o = length(Vec3((n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f) - p); for (auto& v : d) v += o; da.push_back(d); }
        for (auto& p : spB) { auto d = navDist(g, nearestNode(g, p)); auto& n = g.nodes[nearestNode(g, p)]; float o = length(Vec3((n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f) - p); for (auto& v : d) v += o; db.push_back(d); }
        for (size_t i = 0; i < g.nodes.size(); i++) {
            float a = 0, b = 0;
            for (auto& d : da) a += d[i] / da.size();
            for (auto& d : db) b += d[i] / db.size();
            auto& n = g.nodes[i];
            if (a < 1e8f && b < 1e8f) printf("FROM %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.1f %.1f\n", (n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f, n.x0, n.x1, n.z0, n.z1, a, b);
        }
    }
    if (std::getenv("MR_FIELD") && r.name == std::getenv("MR_FIELD")) {
        // debugging spawn placement: for every objective, each nav surface's walk to it (from the surface's centre),
        // as "FIELD <objective> <surface centre x y z> <x0 x1 z0 z1> <walk m>" lines on stdout
        std::vector<std::pair<std::string, AABB>> objs;
        for (size_t k = 0; k < m.points.size(); k++) objs.push_back({std::string("point") + char('A' + k), m.points[k]});
        for (size_t k = 0; k < m.bombSites.size(); k++) objs.push_back({"site" + std::to_string(k + 1), m.bombSites[k]});
        if (m.hasHill) objs.push_back({"hill", m.hill});
        auto centre = [&](size_t i) { auto& n = g.nodes[i]; return Vec3((n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f); };
        for (size_t i = 0; i < g.nodes.size(); i++) {
            auto d = navDist(g, (int)i);   // (navToBox's walk from a spot on surface i, less the spot's way to its centre)
            for (auto& [what, box] : objs) {
                float best = -1;
                for (size_t j = 0; j < g.nodes.size(); j++) {
                    auto& n = g.nodes[j];
                    if (d[j] > 1e29f || n.x1 < box.mn.x || n.x0 > box.mx.x || n.z1 < box.mn.z || n.z0 > box.mx.z || n.y < box.mn.y - 0.5f || n.y > box.mx.y) continue;
                    Vec3 in = {std::clamp((box.mn.x + box.mx.x) * 0.5f, n.x0, n.x1), n.y, std::clamp((box.mn.z + box.mx.z) * 0.5f, n.z0, n.z1)};
                    float tt = d[j] + length(in - centre(j));
                    if (best < 0 || tt < best) best = tt;
                }
                auto& n = g.nodes[i];
                Vec3 c = centre(i);
                printf("FIELD %s %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.1f\n", what.c_str(), c.x, c.y, c.z, n.x0, n.x1, n.z0, n.z1, best);
            }
        }
    }
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        if (!m.supportsMode(mode)) continue;
        std::string line;
        if (mode == MODE_BOMB && teams) {   // per site: A attacks (the first half; the sides swap spawns with roles, and the band is symmetric)
            for (size_t k = 0; k < m.bombSites.size(); k++) {
                float da = boxDist(spA, m.bombSites[k]), db = boxDist(spB, m.bombSites[k]);
                snprintf(buf, sizeof buf, "bomb site %zu", k + 1);
                float l = lead(buf, da, db);
                snprintf(buf, sizeof buf, "%ssite %zu: attackers (A) %.0f m, defenders (B) %.0f m, %s", k ? "; " : "", k + 1, da, db, leadText(l).c_str());
                line += buf;
            }
            line += " (-1..+1 s at every site)";
        } else if (mode == MODE_KOTH && m.hasHill && teams) line = fairLine("to the hill", boxDist(spA, m.hill), boxDist(spB, m.hill));
        else if (mode == MODE_NPAYLOAD && m.payloadPath.size() >= 2 && teams)
            line = teamLine("to the cart", m.payloadPoint(m.payloadLength() * 0.5f));
        else if (mode == MODE_PAYLOAD && m.payloadPath.size() >= 2 && teams) { // one-sided by design (sides swap): no fairness score
            float att = teamDist(spA, m.payloadPath.front()), def = teamDist(spB, m.payloadPoint(m.payloadLength() * 0.5f));
            snprintf(buf, sizeof buf, "attackers (A) %.0f m to the cart, defenders (B) %.0f m to the halfway point (track %.0f m)", att, def, m.payloadLength());
            line = buf;
        }
        else if ((mode == MODE_VS || mode == MODE_DUEL || mode == MODE_ARENA || mode == MODE_TOURNEY) && teams) {
            // each side's walk to the other side's spawns (direction-independent: a symmetric map scores 1.00); a
            // tourney match plays from the a / b spawns too
            float ab = across(spA, spB), ba = across(spB, spA);
            line = fairLine(modeLabel(mode), ab, ba);
            if (ab > 0 && ba > 0) { snprintf(buf, sizeof buf, ", %.1f s apart at 9.5 m/s", std::max(ab, ba) / kRunSpeed); line += buf; }
        }
        if (mode == MODE_FFA || mode == MODE_TOURNEY) {
            float minD = 1e9f; int seenPairs = 0, n = 0;
            for (size_t i = 0; i < m.spawns.size(); i++)
                for (size_t j = i + 1; j < m.spawns.size(); j++) {
                    minD = std::min(minD, length(m.spawns[i].pos - m.spawns[j].pos));
                    seenPairs += los(m, m.spawns[i].pos + Vec3(0, kEye, 0), m.spawns[j].pos + Vec3(0, kEye, 0));
                    n++;
                }
            snprintf(buf, sizeof buf, "%s%zu spawns, closest pair %.0f m, %d%% of spawn pairs see each other", line.empty() ? "" : "; ", m.spawns.size(), minD, n ? seenPairs * 100 / n : 0);
            line += buf;
        } else if (isRaceMode(mode) && !m.checkpoints.empty()) {
            Vec3 at = m.spawns.empty() ? Vec3() : m.spawns[0].pos;
            float total = 0;
            bool ok = true;
            for (auto& c : m.checkpoints) { float d = navBetween(g, at, c.center()); if (d < 0) ok = false; else total += d; at = c.center(); }
            snprintf(buf, sizeof buf, "%zu checkpoints, %s %.0f m walked (%.0f s at 9.5 m/s, %.0f s at 20 m/s)", m.checkpoints.size(), ok ? "route" : "PARTLY UNREACHABLE route", total, total / kRunSpeed, total / 20);
            line = buf;
        } else if (mode == MODE_POINTS && !m.points.empty()) {
            line = std::to_string(m.points.size()) + " points;";
            for (size_t i = 0; i < m.points.size(); i++) {
                float a = pointDist(i, false), d = pointDist(i, true);
                snprintf(buf, sizeof buf, "point %c", char('A' + i));
                float l = lead(buf, a, d);
                snprintf(buf, sizeof buf, " %c: attackers %.0f m, defenders %.0f m, %s;", char('A' + i), a, d, leadText(l).c_str());
                line += buf;
            }
            line += " (-1..+1 s at every point)";
        } else if (mode == MODE_HIDE || mode == MODE_TAG) {
            snprintf(buf, sizeof buf, "%.0f%% of floor is a hiding spot (seen from < 5%% of the map)", r.hideSpots * 100);
            line = buf;
        }
        if (!line.empty()) r.modes.push_back({modeLabel(mode), line});
    }
    r.leads = leads;
    mrLap("modes");   // maptool: timings
    // ---- mode fit: the mode the map is built for ("for <mode>"), judged on what that mode needs. A map built for
    // several modes (Lodestone) is graded in every mode it offers too (B1): each one's fit is listed,
    // and the bar holds its timing (B2) and balance in all of them; the mode-fit score is its built-for mode's
    float loopsPerK = r.loops / std::max(1.0f, r.area / 1000);
    auto fitFor = [&](int pm, std::string& why) -> float {
        float fit = -1;
        if ((pm == MODE_DUEL || pm == MODE_ARENA || pm == MODE_TOURNEY || pm == MODE_VS) && (teams || pm == MODE_TOURNEY)) {
            // duels want a compact map (meet in 2.5-7 s at a run, 300-2000 m2 of floor: with dashes and the grapple
            // that's 1-3 s); team fights a bigger one (4-9 s, 700-3000 m2); both want it fair, and loops so a fight
            // can be broken off and re-taken
            bool duel = pm != MODE_VS;
            float ab = teams ? across(spA, spB) : -1, ba = teams ? across(spB, spA) : -1;
            float t = (ab > 0 && ba > 0) ? std::max(ab, ba) / kRunSpeed : (duel ? 4.0f : 6.0f);
            float f = (ab > 0 && ba > 0) ? std::min(ab, ba) / std::max(ab, ba) : (teams ? 0.0f : 1.0f);
            float size = duel ? band(r.area, 300, 2000, 1500) : band(r.area, 700, 3000, 2000);
            float time = duel ? band(t, 2.5f, 7, 4) : band(t, 4, 9, 5);
            float loops = std::min(loopsPerK / kLoopsFull, 1.0f);
            fit = 3 * f + 2.5f * size + 2.5f * time + 2 * loops;
            snprintf(buf, sizeof buf, "%s: fairness %.2f, %.0f m2 floor (%s), spawns %.1f s apart (%s), %.1f loops per 1000 m2 (%.0f ideal)",
                     modeLabel(pm), f, r.area, duel ? "300-2000 ideal" : "700-3000 ideal", t, duel ? "2.5-7 s ideal" : "4-9 s ideal", loopsPerK, kLoopsFull);
        } else if (pm == MODE_BOMB && teams && !m.bombSites.empty()) {
            // the B2 band at the worst site (both sides arrive together: whoever takes a site first holds it); two
            // sites; the attackers have ways round (loops)
            float worst = 1, wl = 0; size_t ws = 0;
            for (size_t k = 0; k < m.bombSites.size(); k++) {
                float l = (boxDist(spA, m.bombSites[k]) - boxDist(spB, m.bombSites[k])) / kRunSpeed;
                bool unreachable = boxDist(spA, m.bombSites[k]) < 0 || boxDist(spB, m.bombSites[k]) < 0;
                float s = unreachable ? 0 : band(l, kLeadLo, kLeadHi, 4);
                if (s < worst || k == 0) { worst = s; wl = l; ws = k; }
            }
            fit = 4 * worst + 3 * std::min(m.bombSites.size() / 2.0f, 1.0f) + 3 * std::min(loopsPerK / kLoopsFull, 1.0f);
            snprintf(buf, sizeof buf, "bomb: worst site %zu, attackers' arrival minus defenders' %+.1f s (-1..+1 ideal), %zu sites, %.1f loops per 1000 m2", ws + 1, wl, m.bombSites.size(), loopsPerK);
        } else if (pm == MODE_KOTH && m.hasHill && teams) {
            // equal walks to the hill, several ways onto it, and cover on it to hold it with
            float da = boxDist(spA, m.hill), db = boxDist(spB, m.hill);
            float f = (da > 0 && db > 0) ? std::min(da, db) / std::max(da, db) : 0;
            int ways = waysOnto(w, reach, m.hill);
            float hcov = coverIn(m.hill);
            fit = 4 * f + 3 * std::min(ways / 4.0f, 1.0f) + 3 * std::min(hcov / 0.35f, 1.0f);
            snprintf(buf, sizeof buf, "koth: fairness %.2f, %d ways onto the hill (4+ ideal), %.0f%% of the hill has cover (35%% ideal)", f, ways, hcov * 100);
        } else if ((pm == MODE_PAYLOAD || pm == MODE_NPAYLOAD) && m.payloadPath.size() >= 2 && teams) {
            // a track long enough for a push (70-160 m), with cover beside it to push and hold from
            float len = m.payloadLength();
            int tc = 0, tn = 0;
            for (size_t i = 0; i < pts.size() && i < coveredAt.size(); i++) {
                const Vec3& q = pts[i].p;
                float best = 1e9f;
                for (size_t k = 0; k + 1 < m.payloadPath.size(); k++) {
                    Vec3 a = m.payloadPath[k], b2 = m.payloadPath[k + 1], ab = b2 - a; ab.y = 0;
                    float L = dot(ab, ab), tt = L > 0 ? std::clamp(dot(Vec3(q.x - a.x, 0, q.z - a.z), ab) / L, 0.0f, 1.0f) : 0;
                    best = std::min(best, length(Vec3(q.x - a.x - ab.x * tt, 0, q.z - a.z - ab.z * tt)));
                }
                if (best < 6) { tn++; tc += coveredAt[i]; }
            }
            float tcov = tn ? (float)tc / tn : 0;
            float f = 1;
            if (pm == MODE_NPAYLOAD) { float da = teamDist(spA, m.payloadPoint(len * 0.5f)), db = teamDist(spB, m.payloadPoint(len * 0.5f)); f = (da > 0 && db > 0) ? std::min(da, db) / std::max(da, db) : 0; }
            fit = 3 * f + 3.5f * band(len, 70, 160, 60) + 3.5f * std::min(tcov / 0.35f, 1.0f);
            snprintf(buf, sizeof buf, "%s: track %.0f m (70-160 ideal), %.0f%% of the floor beside it has cover (35%% ideal), fairness %.2f", modeLabel(pm), len, tcov * 100, f);
        } else if (pm == MODE_FFA) {
            // enough spawns spread out (12+, few in sight of each other), room for everyone, loops to keep moving
            std::vector<const SpawnPoint*> pool = spawnPool(m, SPAWN_ANY, 0);   // (the spawns a free for all uses)
            int seenPairs = 0, n = 0;
            for (size_t i = 0; i < pool.size(); i++) for (size_t j = i + 1; j < pool.size(); j++) { seenPairs += los(m, pool[i]->pos + Vec3(0, kEye, 0), pool[j]->pos + Vec3(0, kEye, 0)); n++; }
            float seen = n ? (float)seenPairs / n : 1;
            fit = 3 * std::min(pool.size() / 12.0f, 1.0f) + 4 * (1 - std::min(seen / 0.3f, 1.0f)) + 3 * std::min(loopsPerK / kLoopsFull, 1.0f);
            snprintf(buf, sizeof buf, "ffa: %zu spawns (12+ ideal), %.0f%% of spawn pairs in sight (0 ideal, 30%% scores nothing), %.1f loops per 1000 m2", pool.size(), seen * 100, loopsPerK);
        } else if (isRaceMode(pm)) {
            fit = r.mTech;
            snprintf(buf, sizeof buf, "race: the race movement check (shortcut%s on the tech path, see movement)", m.preferredMode == MODE_RACE ? " and challenge" : "");
        } else if (pm == MODE_POINTS && m.points.size() >= 2) {
            // an attack in stages: 3-6 points; 25-90 m of walking from one to the next (a push, not a hop); both
            // sides' stage spawns reach each point within a second of each other (B2: whoever is there first holds
            // it); 3+ ways onto every point (it can be flanked) and cover on it (35%) so a team can hold or take it.
            // Every part is graded at its WORST point.
            size_t n = m.points.size();
            float count = band((float)n, 3, 6, 2), spacing = 1, leadS = 1, cover = 1, ways = 1;
            int minWays = 1 << 30;
            std::string ls;
            for (size_t i = 0; i < n; i++) {
                const AABB& z = m.points[i];
                if (i > 0) spacing = std::min(spacing, band(navToBox(g, {m.points[i - 1].center().x, m.points[i - 1].mn.y, m.points[i - 1].center().z}, z), 25, 90, 30));
                float pa = pointDist(i, false), pd = pointDist(i, true);
                float la = (pa - pd) / kRunSpeed;
                leadS = std::min(leadS, pa < 0 || pd < 0 ? 0.0f : band(la, kLeadLo, kLeadHi, 4));
                snprintf(buf, sizeof buf, "%s%+.1f", i ? "/" : "", la); ls += buf;
                int wn = waysOnto(w, reach, z);
                minWays = std::min(minWays, wn);
                ways = std::min(ways, std::min(wn / 3.0f, 1.0f));
                cover = std::min(cover, std::min(coverIn(z) / 0.35f, 1.0f));
            }
            fit = 1.5f * count + 2 * spacing + 3 * leadS + 2 * cover + 1.5f * ways;
            snprintf(buf, sizeof buf, "points: %zu points (3-6 ideal), worst spacing %.2f (25-90 m), attackers' arrival minus defenders' %s s (-1..+1 ideal, worst %.2f), worst cover %.2f (35%% ideal), fewest ways onto a point %d (3+)",
                     n, spacing, ls.c_str(), leadS, cover, minWays);
        } else if (pm == MODE_HIDE || pm == MODE_TAG) {
            // hiding needs spots seen from almost nowhere; tag needs loops to run round (and hiding spots matter less)
            bool tag = pm == MODE_TAG;
            fit = (tag ? 4 : 6) * std::min(r.hideSpots / 0.15f, 1.0f) + (tag ? 6 : 4) * std::min(loopsPerK / kLoopsFull, 1.0f);
            snprintf(buf, sizeof buf, "%s: %.0f%% hiding spots (15%% ideal), %.1f loops per 1000 m2 (%.0f ideal)", modeLabel(pm), r.hideSpots * 100, loopsPerK, kLoopsFull);
        }
        if (fit >= 0) why = buf;
        return fit < 0 ? -1.0f : clamp10(fit);
    };
    if (m.preferredMode >= 0) {
        std::string why;
        float f = fitFor(m.preferredMode, why);
        if (f >= 0) { r.sMode = f; r.modeFit = why; }
    }
    int offered = 0;
    for (int mode = 0; mode < MODE_COUNT; mode++) offered += m.supportsMode(mode);
    if (offered > 2)   // a map offered for more than its own mode and free for all (Lodestone): its fit in each
        for (int mode = 0; mode < MODE_COUNT; mode++) {
            if (!m.supportsMode(mode) || mode == m.preferredMode) continue;
            std::string why;
            float f = fitFor(mode, why);
            if (f >= 0) r.modeFits.push_back({modeLabel(mode), f, why});
        }
    r.setting = m.setting;
    if (m.rankedLadders) {
        // the weapons each ranked ladder is about, and where they work
        float near = r.band[0] + r.band[1], longr = r.band[2] + r.band[3];
        snprintf(buf, sizeof buf, "ranked: %.0f%% of sightlines in rifle/rocket range (< %.0f m), %.0f%% in laser/crossbow/sniper range (> %.0f m)", near * 100, kBandMid, longr * 100, kBandMid);
        r.modes.push_back({"ranked", buf});
    }
    if (!fair.empty()) {   // the worst objective counts (C1), not the mean
        r.fairness = 1;
        for (auto& [what, f] : fair) if (f < r.fairness || r.worstFair.empty()) { r.fairness = std::min(r.fairness, f); r.worstFair = what; }
    }
    mrLap("modeFit");   // maptool: timings
    // ---- feature list (creativity)
    // distinct things the map does (creativity counts these; each one is a reason to play it differently)
    if (r.ladders) r.features.push_back("ladders");
    if (r.voidEdge >= 0.01f) r.features.push_back("lethal drops");   // (1% of the floor by a drop that kills, measured: a "void" line alone is no drop)
    if (r.liquids) r.features.push_back("water / lava");
    if (r.wallbang) r.features.push_back("wallbangs");
    if (m.payloadPath.size() >= 2) r.features.push_back("moving cart");
    if (r.levels >= 5) r.features.push_back("5+ floor levels");
    if (r.vertRange >= 12) r.features.push_back("12 m+ of height");
    if (r.roofed >= 0.15f && r.roofed <= 0.85f) r.features.push_back("indoor and outdoor");
    if (r.roofed > 0.85f) r.features.push_back("fully indoor");
    if (r.fill < 0.8f) r.features.push_back("irregular outline");
    if (r.lights >= 12) r.features.push_back("lit interiors");
    if (r.hideSpots > 0.15f) r.features.push_back("lots of hiding spots");
    mrLap("features");   // maptool: timings
}


// The parts of the bar a map is let off, from the map itself (no exemptions by name: C1, REVIEW MAP-4): a line
// "# exempt <category> ... : <why>" its generator writes (Dojo: the owner's bare eye-to-eye arena, simple on purpose,
// is let off layout, movement and creativity; decor, mode fit and balance still hold).
std::set<std::string> mapExemptions(const Map& m) {
    std::set<std::string> out;
    std::istringstream in(m.sourceText());
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("# exempt ", 0) != 0) continue;
        std::istringstream words(line.substr(9));
        std::string w;
        while (words >> w && w != ":") out.insert(w);
    }
    return out;
}

} // namespace mr
