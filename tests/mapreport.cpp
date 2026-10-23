// Map analysis: measures every shipped map from its geometry, the bot nav graph and line-of-sight rays, and
// writes docs/MAP_REPORT.md. Metric definitions and score formulas: docs/MAP_METRICS.md.
// Run: build/quark_tests mapreport
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

namespace {

// Weapon range bands (from the weapon set): close = shotgun / knife, mid = rifle / rocket / sticky,
// long = laser (32 m max) / crossbow, extreme = only the sniper (and a scoped rifle) reaches.
constexpr float kBandClose = 6.0f, kBandMid = 20.0f, kBandLong = 32.0f;
constexpr float kRunSpeed = 8.0f;   // pmove kGroundSpeed
constexpr float kJumpReach = 1.2f;     // a standing jump clears ~1.2 m (pmove: 7.34 m/s up, 22 m/s^2 gravity)
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
    float roofed = 0;          // share of points with a ceiling within 12 m (indoor spaces)
    float fill = 1;            // share of the footprint's bounding box that is floor (1 = a plain rectangle)
    bool liquids = false;      // water / lava surfaces
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
    float fairness = -1;       // 0-1, min/max of team distances (team maps)
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

const MoveEnvelope& moveEnvelope() {
    static const MoveEnvelope env = [] {
        auto t0 = std::chrono::steady_clock::now();
        auto lap = [&](const char* what) { if (std::getenv("MR_TIME")) fprintf(stderr, "envelope: %s by %.0f ms\n", what, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); };
        MoveEnvelope e;
        e.speed = {kRunSpeed, 12, 16, 20, 24, 28};   // (past 28 m/s an upward dash gets no higher: its exit speed is capped)
        // speed from a standstill: jump held (auto-bhop), the dash on cooldown while it adds speed, and each
        // millisecond the strafe / turn that gains the most (greedy: a good player, not a tool-assisted one)
        Map flat;
        flat.parse("box -600 -1 -600 600 0 600\nspawn any 0 0 0\n");
        e.runUp.assign(e.speed.size(), 0);
        {
            MoveState s;
            s.flags = MF_ONGROUND;
            float dist = 0;
            bool dashed = false;
            size_t next = 1;
            for (int t = 0; t < 30000 && next < e.speed.size(); t++) {
                float h = hspeed(s.vel), vyaw = h > 0.1f ? std::atan2(-s.vel.x, -s.vel.z) : 0;
                bool dash = !dashed && s.dashCooldown <= 0 && s.dashTimer <= 0 && h < 29.0f;   // (above 29 m/s a dash slows you)
                MoveState best;
                float bestH = -1;
                bool bestDash = false;
                for (int side = 0; side < 2; side++)
                    for (int a = -90; a <= 90; a += 3) {
                        UserCmd c;
                        c.msec = 1;
                        c.buttons = BTN_JUMP | (dash ? BTN_DASH : 0);
                        if (side) c.side = 127; else c.forward = 127;
                        c.yaw = vyaw + a * 0.0174533f + (side ? 1.5707963f : 0.0f);
                        MoveState q = s;
                        pmove(q, c, flat);
                        if (hspeed(q.vel) > bestH) { bestH = hspeed(q.vel); best = q; bestDash = dash; }
                    }
                dashed = bestDash;
                dist += std::sqrt((best.pos.x - s.pos.x) * (best.pos.x - s.pos.x) + (best.pos.z - s.pos.z) * (best.pos.z - s.pos.z));
                s = best;
                if (s.dashTimer <= 0 && hspeed(s.vel) >= e.speed[next]) { e.runUp[next++] = dist; e.buildTime = (t + 1) / 1000.0f; }
            }
            for (; next < e.speed.size(); next++) e.runUp[next] = 1e9f;   // (never: the tier can't be reached)
        }
        lap("speed build-up");
        // take-offs: moving at the tier's speed, jump, then (or not) an upward dash after tau at pitch p. Each arc is
        // followed until it is kLowest below the take-off; the envelope is the highest any of them is at each distance.
        Map pad;
        pad.parse("box -1 -1 -1 1 0 1\nspawn any 0 0 0\n");
        e.top.assign(e.speed.size(), std::vector<float>(MoveEnvelope::kBins, -1e9f));
        e.apexLevel.assign(e.speed.size(), std::vector<float>(MoveEnvelope::kBins, 1e9f));
        for (size_t k = 0; k < e.speed.size(); k++) {
            for (int ti = 0; ti <= 5; ti++)
                for (int pi = -1; pi <= 9; pi++) {   // (no dash, or one at 0-90 degrees up, 0-0.4 s after the jump)
                    float tau = ti * 0.08f, pitch = pi < 0 ? 0 : pi * 10 * 0.0174533f;
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
                        c.buttons = BTN_JUMP | (pi >= 0 && t >= (int)(tau * 1000) && t < (int)(tau * 1000) + 5 ? BTN_DASH : 0);
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
            if (k > 0)   // anything a slower take-off reaches, a faster player reaches too (slowing down is free)
                for (int b = 0; b < MoveEnvelope::kBins; b++) {
                    e.top[k][b] = std::max(e.top[k][b], e.top[k - 1][b]);
                    e.apexLevel[k][b] = std::min(e.apexLevel[k][b], e.apexLevel[k - 1][b]);
                }
        }
        lap("take-offs");
        // grapple pulls: an anchor 5k m ahead (on a wall, at eye height), from each tier's speed: the speed when it lets go
        e.pullExit.assign(e.speed.size(), std::vector<float>(10, 0));
        for (size_t k = 0; k < e.speed.size(); k++)
            for (int i = 0; i < 10; i++) {
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
            }
        lap("grapple pulls");
        return e;
    }();
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
    // m: the map as players collide with it (clip included: it blocks leaps)
    void build(const Map& m, const NavGraph& g, const std::vector<Vec3>& pts) {
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
        std::map<std::pair<int, int>, bool> riseMemo;
        auto rise = [&](int i, float top) {
            auto key = std::make_pair(i, (int)std::ceil(top * 4));
            auto it = riseMemo.find(key);
            if (it != riseMemo.end()) return it->second;
            bool ok = clear(p[i] + Vec3(0, 1.0f, 0), Vec3(p[i].x, key.second / 4.0f, p[i].z));
            riseMemo[key] = ok;
            return ok;
        };
        const float reachMax = E.maxReach();
        const int span = (int)std::ceil(reachMax / 6) + 1;
        for (size_t i = 0; i < p.size(); i++) {
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
                        if (h <= 6.0f && navLinked(node[i], node[j])) {
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
                        bool fits = rise((int)i, top) && rise(j, top);
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
        }
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
void measure(Metrics& r, const Map& m, const Map& solid, const NavGraph& g) {
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
    // floor you can walk to from a spawn (running, jumping, ladders): the playable floor. Roofs and wall tops
    // only a grapple reaches still get samples for the movement-tech graph, but don't count as floor.
    std::vector<char> playable(g.nodes.size(), 0);
    for (auto& sp : m.spawns) {
        auto d = navDist(g, nearestNode(g, sp.pos));
        for (size_t i = 0; i < d.size(); i++) if (d[i] < 1e29f && g.nodes[i].y > m.killY()) playable[i] = 1;   // (below the kill height you're dead)
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
    TechGraph tech;
    float techCell = 4;   // m2 of floor per tech sample
    std::vector<char> techPlayable;
    {
        std::vector<Vec3> tp;
        size_t stride = pts.size() / 2500 + 1; // up to ~2500 points
        for (size_t i = 0; i < pts.size(); i += stride) { tp.push_back(pts[i].p); techPlayable.push_back(playable[pts[i].node]); }
        techCell = 4.0f * stride;
        tech.build(solid, g, tp);
    }
    {   std::vector<Pt> keep; for (auto& pt : pts) if (playable[pt.node]) keep.push_back(pt); pts.swap(keep); }
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
        if ((n.x1 - n.x0) * (n.z1 - n.z0) <= 4 || deg > 1 || spawnReach[i] > 1e29f || !playable[i]) continue; // one link in, and somewhere you can get to (alive)
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
    int wallHits = 0, bareHits = 0, covered = 0, grapple = 0, head = 0, nearVoid = 0, roofedN = 0;
    std::vector<char> coveredAt; // per sample: >= 4 of 16 directions blocked within 3 m
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
        coveredAt.push_back(blocked >= 4);
        lanes.push_back(longest);
        float up = m.raycast(eye, {0, 1, 0}, 30.0f);
        head += up < 0 || up > 6.0f;
        roofedN += up >= 0 && up < 12.0f;
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
    r.roofed = (float)roofedN / P;
    for (auto& b : m.brushes) r.liquids |= b.material == 6 || b.material == 7;
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
    {   int used = 0; for (float v : r.occ) used += v >= 0; r.fill = used / 576.0f; }
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
                float ratio = (route > 0 && skip > 0) ? route / skip : 1;
                snprintf(tb, sizeof tb, "%s%zu: %.0f m run / %.0f m with tech (%.2fx), %d jumps, %.0f m narrow, %.0f m grappled", legs.empty() ? "" : "; ", k + 1, route, skip, ratio, T.jumps, T.narrowLen, T.pulled);
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
            snprintf(tb, sizeof tb, "race: shortcut %.1f (worst leg %d at %.2fx; 10 at <= 1.25x, 0 at >= 2.5x), challenge %.1f on the tech path (%.1f jumps per 100 m, %.0f%% narrow, %.0f%% grappled) - legs: ",
                     shortcut, worstLeg, worst, challenge, jp, ns * 100, gs * 100);
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
            for (size_t i = 0; i < tech.p.size(); i++) if (techPlayable[i]) { x0 = std::min(x0, tech.p[i].x); x1 = std::max(x1, tech.p[i].x); z0 = std::min(z0, tech.p[i].z); z1 = std::max(z1, tech.p[i].z); }
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
    // multi point: mean walk to point i from the spawns that side uses while attacking / defending it (staged spawns)
    auto pointDist = [&](size_t i, bool defending) {
        int stage = -1;
        for (auto& sp : m.spawns) if (isStagedSpawn(sp.team) && spawnDefends(sp.team) == defending && spawnStage(sp.team) <= (int)i) stage = std::max(stage, spawnStage(sp.team));
        std::vector<Vec3> sps;
        for (auto& sp : m.spawns) if (sp.team == stagedSpawn(std::max(stage, 0), defending)) sps.push_back(sp.pos);
        if (getenv("MR_POINTS")) for (auto& p : sps) printf("    point %zu %s spawn %.0f %.0f %.0f: %.0f m (node %d)\n", i, defending ? "def" : "att", p.x, p.y, p.z, navToBox(g, p, m.points[i]), nearestNode(g, p));
        return boxDist(sps, m.points[i]);
    };
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
                snprintf(buf, sizeof buf, "A to B spawns %.0f m, B to A %.0f m (fairness %.2f, %.1f s apart at 8 m/s)", ab, ba, f, std::max(ab, ba) / kRunSpeed);
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
            snprintf(buf, sizeof buf, "%zu checkpoints, %s %.0f m walked (%.0f s at 8 m/s, %.0f s at 20 m/s)", m.checkpoints.size(), ok ? "route" : "PARTLY UNREACHABLE route", total, total / kRunSpeed, total / 20);
            line = buf;
        } else if (mode == MODE_POINTS && !m.points.empty()) {
            std::string t = std::to_string(m.points.size()) + " points;";
            for (size_t i = 0; i < m.points.size(); i++) {
                float a = pointDist(i, false), d = pointDist(i, true);
                snprintf(buf, sizeof buf, " %c: attackers %.0f m, defenders %.0f m;", char('A' + i), a, d);
                t += buf;
            }
            line = t;
        } else if (mode == MODE_HIDE || mode == MODE_TAG) {
            snprintf(buf, sizeof buf, "%.0f%% of floor is a hiding spot (seen from < 5%% of the map)", r.hideSpots * 100);
            line = buf;
        }
        if (!line.empty()) r.modes.push_back({modeLabel(mode), line});
    }
    // ---- mode fit: the one mode the map is built for ("for <mode>"), judged on what that mode needs
    {
        auto band = [](float v, float lo, float hi, float soft) { // 1 inside [lo, hi], falling to 0 over `soft` outside
            return v < lo ? std::max(0.0f, 1 - (lo - v) / soft) : v > hi ? std::max(0.0f, 1 - (v - hi) / soft) : 1.0f;
        };
        float loopsPerK = r.loops / std::max(1.0f, r.area / 1000);
        int pm = m.preferredMode;
        float fit = -1;
        auto across = [&](const std::vector<Vec3>& from, const std::vector<Vec3>& to) {
            float d = 0; int n = 0;
            for (auto& a : from) { float best = 1e9f; for (auto& b2 : to) { float x = navBetween(g, a, b2); if (x >= 0) best = std::min(best, x); } if (best < 1e8f) { d += best; n++; } }
            return n ? d / n : -1.0f;
        };
        if ((pm == MODE_DUEL || pm == MODE_ARENA || pm == MODE_TOURNEY || pm == MODE_VS) && (teams || pm == MODE_TOURNEY)) {
            // duels want a compact map (meet in 2.5-7 s at a run, 300-2000 m2 of floor: with dashes and the grapple
            // that's 1-3 s); team fights a bigger one (4-9 s,
            // 700-3000 m2); both want it fair, and loops so a fight can be broken off and re-taken
            bool duel = pm != MODE_VS;
            float ab = teams ? across(spA, spB) : -1, ba = teams ? across(spB, spA) : -1;
            float t = (ab > 0 && ba > 0) ? std::max(ab, ba) / kRunSpeed : (duel ? 4.0f : 6.0f);
            float f = (ab > 0 && ba > 0) ? std::min(ab, ba) / std::max(ab, ba) : 1;
            float size = duel ? band(r.area, 300, 2000, 1500) : band(r.area, 700, 3000, 2000);
            float time = duel ? band(t, 2.5f, 7, 4) : band(t, 4, 9, 5);
            float loops = std::min(loopsPerK / 20, 1.0f);
            fit = 3 * f + 2.5f * size + 2.5f * time + 2 * loops;
            snprintf(buf, sizeof buf, "%s: fairness %.2f, %.0f m2 floor (%s), spawns %.1f s apart (%s), %.0f loops per 1000 m2",
                     modeLabel(pm), f, r.area, duel ? "300-2000 ideal" : "700-3000 ideal", t, duel ? "2.5-7 s ideal" : "4-9 s ideal", loopsPerK);
        } else if (pm == MODE_BOMB && teams && !m.bombSites.empty()) {
            // defenders reach a site 3-10 s before the attackers; two sites; the attackers have ways round (loops)
            float att = 1e9f, def = 1e9f;
            for (auto& site : m.bombSites) { float da = boxDist(spA, site), db = boxDist(spB, site); if (da > 0) att = std::min(att, da); if (db >= 0) def = std::min(def, db); }
            float lead = (att - def) / 8;
            fit = 4 * band(lead, 3, 10, 4) + 3 * std::min(m.bombSites.size() / 2.0f, 1.0f) + 3 * std::min(loopsPerK / 20, 1.0f);
            snprintf(buf, sizeof buf, "bomb: defenders arrive %.1f s first (3-10 s ideal), %zu sites, %.0f loops per 1000 m2", lead, m.bombSites.size(), loopsPerK);
        } else if (pm == MODE_KOTH && m.hasHill && teams) {
            // equal walks to the hill, several ways onto it, and cover on it to hold it with
            float da = boxDist(spA, m.hill), db = boxDist(spB, m.hill);
            float f = (da > 0 && db > 0) ? std::min(da, db) / std::max(da, db) : 0;
            int ways = 0;
            auto in = [&](const NavGraph::Node& n) { float cx = (n.x0 + n.x1) / 2, cz = (n.z0 + n.z1) / 2; return cx > m.hill.mn.x && cx < m.hill.mx.x && cz > m.hill.mn.z && cz < m.hill.mx.z && n.y >= m.hill.mn.y - 0.5f; };
            for (size_t i = 0; i < g.nodes.size(); i++) if (!in(g.nodes[i])) for (auto& l : g.links[i]) ways += in(g.nodes[l.first]);
            int hc = 0, hn = 0;
            for (size_t i = 0; i < pts.size() && i < coveredAt.size(); i++) {
                const Vec3& q = pts[i].p;
                if (q.x > m.hill.mn.x && q.x < m.hill.mx.x && q.z > m.hill.mn.z && q.z < m.hill.mx.z && q.y >= m.hill.mn.y - 0.5f) { hn++; hc += coveredAt[i]; }
            }
            float hcov = hn ? (float)hc / hn : 0;
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
            int seenPairs = 0, n = 0;
            for (size_t i = 0; i < m.spawns.size(); i++) for (size_t j = i + 1; j < m.spawns.size(); j++) { seenPairs += los(m, m.spawns[i].pos + Vec3(0, kEye, 0), m.spawns[j].pos + Vec3(0, kEye, 0)); n++; }
            float seen = n ? (float)seenPairs / n : 1;
            fit = 3 * std::min(m.spawns.size() / 12.0f, 1.0f) + 4 * (1 - std::min(seen / 0.3f, 1.0f)) + 3 * std::min(loopsPerK / 20, 1.0f);
            snprintf(buf, sizeof buf, "ffa: %zu spawns (12+ ideal), %.0f%% of spawn pairs in sight (0 ideal, 30%% scores nothing), %.0f loops per 1000 m2", m.spawns.size(), seen * 100, loopsPerK);
        } else if (pm == MODE_RACE) {
            fit = r.mTech;
            snprintf(buf, sizeof buf, "race: the race movement check (shortcut and challenge on the tech path, see movement)");
        } else if (pm == MODE_POINTS && m.points.size() >= 2) {
            // an attack in stages: 3-6 points; 25-90 m of walking from one to the next (a push, not a hop); the
            // defenders' spawns reach each point 2-10 s before the attackers' (set up, then hold); 3+ ways onto every
            // point (it can be flanked) and cover on it (35%) so a team can hold or take it
            size_t n = m.points.size();
            float count = band((float)n, 3, 6, 2), spacing = 0, lead = 0, cover = 0, ways = 0;
            int minWays = 1 << 30;
            std::string leads;
            for (size_t i = 0; i < n; i++) {
                const AABB& z = m.points[i];
                if (i > 0) spacing += band(navToBox(g, {m.points[i - 1].center().x, m.points[i - 1].mn.y, m.points[i - 1].center().z}, z), 25, 90, 30);
                float la = (pointDist(i, false) - pointDist(i, true)) / 8;
                lead += band(la, 2, 10, 4);
                snprintf(buf, sizeof buf, "%s%.1f", i ? "/" : "", la); leads += buf;
                auto in = [&](const NavGraph::Node& nd) { float cx = (nd.x0 + nd.x1) / 2, cz = (nd.z0 + nd.z1) / 2; return cx > z.mn.x && cx < z.mx.x && cz > z.mn.z && cz < z.mx.z && nd.y >= z.mn.y - 0.5f && nd.y <= z.mx.y; };
                int w = 0;
                for (size_t k = 0; k < g.nodes.size(); k++) if (!in(g.nodes[k])) for (auto& l : g.links[k]) w += in(g.nodes[l.first]);
                minWays = std::min(minWays, w);
                ways += std::min(w / 3.0f, 1.0f);
                int hc = 0, hn = 0;
                for (size_t k = 0; k < pts.size() && k < coveredAt.size(); k++) {
                    const Vec3& q = pts[k].p;
                    if (q.x > z.mn.x && q.x < z.mx.x && q.z > z.mn.z && q.z < z.mx.z && q.y >= z.mn.y - 0.5f && q.y <= z.mx.y) { hn++; hc += coveredAt[k]; }
                }
                cover += std::min((hn ? (float)hc / hn : 0) / 0.35f, 1.0f);
            }
            spacing /= (n - 1); lead /= n; cover /= n; ways /= n;
            fit = 1.5f * count + 2 * spacing + 3 * lead + 2 * cover + 1.5f * ways;
            snprintf(buf, sizeof buf, "points: %zu points (3-6 ideal), spacing %.2f (25-90 m), defenders first by %s s (2-10 ideal), cover %.2f (35%% ideal), fewest ways onto a point %d (3+)",
                     n, spacing, leads.c_str(), cover, minWays);
        } else if (pm == MODE_HIDE || pm == MODE_TAG) {
            // hiding needs spots seen from almost nowhere; tag needs loops to run round (and hiding spots matter less)
            bool tag = pm == MODE_TAG;
            fit = (tag ? 4 : 6) * std::min(r.hideSpots / 0.15f, 1.0f) + (tag ? 6 : 4) * std::min(loopsPerK / 20, 1.0f);
            snprintf(buf, sizeof buf, "%s: %.0f%% hiding spots (15%% ideal), %.0f loops per 1000 m2 (20 ideal)", modeLabel(pm), r.hideSpots * 100, loopsPerK);
        }
        if (fit >= 0) { r.sMode = clamp10(fit); r.modeFit = buf; }
    }
    r.setting = m.setting;
    if (m.rankedLadders) {
        // the weapons each ranked ladder is about, and where they work
        float near = r.band[0] + r.band[1], longr = r.band[2] + r.band[3];
        snprintf(buf, sizeof buf, "ranked: %.0f%% of sightlines in rifle/rocket range (< 20 m), %.0f%% in laser/crossbow/sniper range (> 20 m)", near * 100, longr * 100);
        r.modes.push_back({"ranked", buf});
    }
    if (!fair.empty()) { float s = 0; for (float f : fair) s += f; r.fairness = s / fair.size(); }
    // ---- feature list (creativity)
    // distinct things the map does (creativity counts these; each one is a reason to play it differently)
    if (r.ladders) r.features.push_back("ladders");
    if (m.hasVoid || r.voidEdge > 0.05f) r.features.push_back("lethal drops");
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
}


} // namespace

int mapReport(const char* outPath) {
    std::vector<Metrics> all;
    for (const std::string& name : listMaps()) {
        if (name.rfind("zz_", 0) == 0) continue;   // (test maps other tests write: never graded)
        if (std::getenv("MR_ONLY") && name != std::getenv("MR_ONLY")) continue;   // (debugging one map: its scores against itself only)
        Map m;
        if (!m.load(name)) continue;
        const Map solid = m;   // (with clip: leaps can't go through it)
        // clip (invisible, out of reach above the walls) is no part of what anyone sees or plays on
        m.brushes.erase(std::remove_if(m.brushes.begin(), m.brushes.end(), [](const Brush& b) { return b.material == 9; }), m.brushes.end());
        m.rebuild();
        NavGraph g;
        g.build(m);
        Metrics r;
        measure(r, m, solid, g);
        printf("  %-10s %4d brushes, %4zu surfaces, %4.0f m2, %d levels, visible %.0f%%, grapple %.0f%%\n", r.name.c_str(), r.brushes, g.nodes.size(), r.area, r.levels, r.visible * 100, r.grapple * 100);
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
    for (auto& r : all) for (auto& f : r.features) featureCount[f]++;
    for (auto& r : all) {
        r.sDecor = clamp10(4 * std::min(r.detailDensity / 16, 1.0f) + 4 * (1 - r.bareWalls) + 2 * std::min(r.lights / 40.0f, 1.0f));
        float bandMix = 0; // evenness of close / mid / long sightlines (entropy of the first three bands, 0-1)
        for (int b = 0; b < 3; b++) if (r.band[b] > 0) bandMix -= r.band[b] * std::log(r.band[b]) / std::log(3.0f);
        float loopsPerK = r.loops / std::max(1.0f, r.area / 1000);
        r.lLevels = 2 * std::min(r.levels / 8.0f, 1.0f); r.lLoops = 2 * std::min(loopsPerK / 25, 1.0f);
        r.lCover = 3 * std::min(r.coverPts / 0.55f, 1.0f); r.lRange = 3 * bandMix; r.lDead = 0.3f * r.deadEnds;
        r.sLayout = clamp10(r.lLevels + r.lLoops + r.lCover + r.lRange - r.lDead);
        r.mGrapple = clamp10(r.grapple / 0.8f * 10);                       // anchors over most of the floor
        r.mLanes = clamp10(r.lanes / 40 * 10);                              // room to build speed
        r.mVertical = clamp10(r.jumpLinks / 0.5f * 7 + std::min(r.levels, 6) * 0.5f + (r.ladders ? 1 : 0)); // jumps / levels / ladders
        r.mHeadroom = clamp10(r.headroom / 0.9f * 10);                      // vertical dashes, rocket jumps
        r.mRisk = r.preferred == "race" ? 10 : r.voidEdge < 0.08f ? 6 + r.voidEdge / 0.08f * 4 : r.voidEdge <= 0.35f ? 10 : clamp10(10 - (r.voidEdge - 0.35f) / 0.4f * 8); // some edges to play around, not everywhere
        r.sMovement = (r.mGrapple + r.mLanes + r.mVertical + r.mHeadroom + r.mRisk + r.mTech) / 6;
        float rare = 0; // a small bonus for features at most a quarter of the maps have
        for (auto& f : r.features) rare += featureCount[f] <= (int)all.size() / 4 ? 0.5f : 0.0f;
        // a setting no other map has (a city, a space station, the inside of a machine...) is worth 1.5; features
        // count 0.75 each now, so a map can't get there on a checklist alone
        int sameSetting = 0;
        for (auto& o : all) sameSetting += &o != &r && !r.setting.empty() && o.setting == r.setting;
        r.sCreativity = clamp10(0.75f * r.features.size() + std::min(rare, 2.0f) + (!r.setting.empty() && !sameSetting ? 1.5f : 0.0f));
        r.sBalance = r.fairness >= 0 ? clamp10(r.fairness * 10) : -1;
        r.sOriginality = clamp10(r.originality * 8);
    }
    // ---- the bar every map has to clear: 7 in every category but originality (a map can't help resembling
    // the others), and balance where the map has team modes
    int below = 0;
    for (auto& r : all) {
        std::pair<const char*, float> cats[] = {{"decor", r.sDecor}, {"layout", r.sLayout}, {"movement", r.sMovement}, {"creativity", r.sCreativity}, {"balance", r.sBalance}, {"mode fit", r.sMode}};
        // (the owner's call: Dojo is a bare eye-to-eye laser arena, kept simple on purpose - its layout, movement
        // and creativity aren't held to the bar; decor, mode fit and balance still are)
        bool simpleArena = r.name == "dojo";
        for (auto& [k, v] : cats)
            if (simpleArena && (!strcmp(k, "layout") || !strcmp(k, "movement") || !strcmp(k, "creativity"))) continue;
            else if (v >= 0 && v < 7.0f - 1e-3f) { fprintf(stderr, "  FAIL %s: %s %.1f is below 7\n", r.name.c_str(), k, v); below++; }
        if (r.sMode < 0) { fprintf(stderr, "  FAIL %s: not built for one mode (\"for <mode>\")\n", r.name.c_str()); below++; }
        if (r.setting.empty()) { fprintf(stderr, "  FAIL %s: no \"setting\" (what place it is)\n", r.name.c_str()); below++; }
        // nothing outside the playable floor's footprint may be reachable with the full movement envelope (clip, or
        // walls too high to dash up, keep everyone in)
        if (r.outOfBounds > 0) { fprintf(stderr, "  FAIL %s: %.0f m2 outside the playable floor is reachable (MR_OOB=%s lists it)\n", r.name.c_str(), r.outOfBounds, r.name.c_str()); below++; }
    }
    // ---- write the report
    std::ofstream o(outPath);
    o << "# Map report\n\nGenerated by `build/quark_tests mapreport` from the map files, the bot nav graph and line-of-sight rays. "
         "Metric definitions and score formulas: [MAP_METRICS.md](MAP_METRICS.md). Scores are 0-10; a dash means not applicable.\n\n";
    {   // the movement envelope the tech checks use (measured with pmove; MAP_METRICS.md)
        const MoveEnvelope& E = moveEnvelope();
        o << "## Movement envelope\n\nMeasured with the game's movement code: the speed a good player builds with auto-bhop, strafing and dashes "
             "(after so many metres of moving from a standstill), and what a jump plus an upward dash reaches from that speed. "
             "The tech checks below use these.\n\n| speed | run-up | rise | across, level | across, 3.5 m up | across, 8 m up | after a 20 m grapple pull |\n|---|---|---|---|---|---|---|\n";
        for (int k = 0; k < E.tiers(); k++) {
            char line[256];
            snprintf(line, sizeof line, "| %.0f m/s | %.0f m | %.1f m | %.1f m | %.1f m | %.1f m | %.0f m/s |\n", E.speed[k], E.runUp[k], E.rise(k), E.reach(k, 0), E.reach(k, 3.5f), E.reach(k, 8), E.pullExit[k][4]);
            o << line;
        }
        o << "\n";
    }
    o << "## Scores\n\n| map | for | setting | decor | layout | movement | creativity | mode fit | balance | originality |\n|---|---|---|---|---|---|---|---|---|---|\n";
    char b[512];
    for (auto& r : all) {
        char bal[16] = "-", mf[16] = "-";
        if (r.sBalance >= 0) snprintf(bal, sizeof bal, "%.1f", r.sBalance);
        if (r.sMode >= 0) snprintf(mf, sizeof mf, "%.1f", r.sMode);
        snprintf(b, sizeof b, "| %s | %s | %s | %.1f | %.1f | %.1f | %.1f | %s | %s | %.1f |\n", r.name.c_str(), r.preferred.c_str(), r.setting.empty() ? "-" : r.setting.c_str(),
                 r.sDecor, r.sLayout, r.sMovement, r.sCreativity, mf, bal, r.sOriginality);
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
    o << "\n### Layout points\n\n| map | levels (/2) | loops (/2) | cover (/3) | range mix (/3) | dead ends (-) |\n|---|---|---|---|---|---|\n";
    for (auto& r : all) {
        snprintf(b, sizeof b, "| %s | %.1f | %.1f | %.1f | %.1f | -%.1f |\n", r.name.c_str(), r.lLevels, r.lLoops, r.lCover, r.lRange, r.lDead);
        o << b;
    }
    o << "\n## Movement\n\nSub-scores 0-10 (the movement score is their mean); the measurement behind each in brackets.\n\n"
         "| map | grapple (anchors) | speed lanes (p90) | vertical (jump links, levels) | headroom (>6 m) | risk (near a lethal drop) | tech payoff |\n|---|---|---|---|---|---|---|\n";
    for (auto& r : all) {
        snprintf(b, sizeof b, "| %s | %.1f (%.0f%%) | %.1f (%.0f m) | %.1f (%.0f%%, %d) | %.1f (%.0f%%) | %.1f (%.0f%%) | %.1f |\n", r.name.c_str(), r.mGrapple, r.grapple * 100,
                 r.mLanes, r.lanes, r.mVertical, r.jumpLinks * 100, r.levels, r.mHeadroom, r.headroom * 100, r.mRisk, r.voidEdge * 100, r.mTech);
        o << b;
    }
    o << "\nTech payoff:\n\n";
    for (auto& r : all) o << "- **" << r.name << "**: " << r.techLine << "\n";
    o << "\n### Mode fit\n\nEach map is built for one mode; this is how well it does what that mode needs.\n\n";
    for (auto& r : all) o << "- **" << r.name << "**: " << (r.modeFit.empty() ? std::string("no mode it is built for") : r.modeFit) << "\n";
    o << "\n## Per map: shots and modes\n\n";
    for (auto& r : all) {
        o << "### " << r.name << "\n\n![" << r.name << ": 8 orbits and 8 eye-level views](map_shots/" << r.name << ".jpg)\n\n";
        if (!r.features.empty()) { o << "Features: "; for (size_t i = 0; i < r.features.size(); i++) o << (i ? ", " : "") << r.features[i]; o << "\n\n"; }
        for (auto& [mode, line] : r.modes) o << "- " << mode << ": " << line << "\n";
        snprintf(b, sizeof b, "- most similar map: %s (distance %.2f)\n\n", r.closest.c_str(), r.originality);
        o << b;
    }
    printf("map report written to %s\n", outPath);
    if (below) printf("FAIL: %d map scores below 7 (see above)\n", below);
    return below;
}
