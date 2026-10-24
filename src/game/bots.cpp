#include <algorithm>
#include <atomic>
#include <thread>
#include "bots.h"
#include "botbrain.h"
#include "../common/profile.h"
#include "gamemodes.h"
#include "server.h"

static_assert(kMaxPlayers == 32, "BotBrain::aware is sized for kMaxPlayers");

// =====================================================================================================
// Navigation graph
// =====================================================================================================

struct NavJob {
    Map map;
    MapId id;   // map.id(), taken before the thread starts
    NavGraph result;
    std::atomic<bool> done{false};
};

namespace {
constexpr float kNavCell = 4.0f;
constexpr float kLinkGap = 2.6f;   // farthest jump between surfaces
constexpr float kNodePad = 0.3f;   // nodeAt counts a point this close to a surface's edge as on it
constexpr float kNavInf = 1e30f;
// What stands on a surface and blocks a walk across it: a solid that reaches above a step (kStepHeight 0.55, pmove.cpp)
// and starts below a standing player's head (bots don't crouch). Its footprint, grown by the hull's half width less a
// centimetre (so a gap just wide enough for the hull still leaves a sliver), is cut out of the surface: what's left is
// where a player's centre can be, in rectangles.
constexpr float kNavStep = 0.56f;
constexpr float kNavHead = kPlayerHeight;
constexpr float kNavClear = kPlayerHalfWidth - 0.01f;
constexpr float kNavSnap = 0.1f;   // blockers' edges this close together are one edge (no slivers: a gap under 0.88 m closes)
constexpr size_t kNavSplitAt = 48;  // a surface with more blockers than this is halved first (keeps the cutting cheap)
constexpr float kNavTile = 12.0f;   // regions are cut at these tiles (by piece centre): a region's centre then says where it is

struct Rect { float x0, z0, x1, z1; };

// Calls f(brush index) for the solid brushes (the collision grid's: what boxSolid tests) in the cells the rectangle
// touches. A brush can come more than once.
template <class F> void brushesNear(const Map& m, float x0, float z0, float x1, float z1, F f) {
    if (m.gridW <= 0 || m.gridH <= 0 || m.cells.empty()) return;
    int cx0 = std::max(0, (int)std::floor((x0 - m.gridX0) / Map::kCell)), cx1 = std::min(m.gridW - 1, (int)std::floor((x1 - m.gridX0) / Map::kCell));
    int cz0 = std::max(0, (int)std::floor((z0 - m.gridZ0) / Map::kCell)), cz1 = std::min(m.gridH - 1, (int)std::floor((z1 - m.gridZ0) / Map::kCell));
    for (int z = cz0; z <= cz1; z++)
        for (int x = cx0; x <= cx1; x++) {
            const Map::GridCell& c = m.cells[(size_t)z * m.gridW + x];
            for (uint32_t k = c.start; k < c.end; k++) f((int)m.cellBrushes[k]);
        }
}
bool blocksAt(const AABB& b, float y) { return b.mn.y < y + kNavHead && b.mx.y > y + kNavStep; }

// The rectangle r with the blockers (each clipped to r) cut out, as rectangles: the blockers' edges grid r into cells
// (edges within kNavSnap merged: no slivers), and the free cells are covered greedily, each time with a run along x
// grown along z while the whole run stays free. A rectangle with a great many blockers (a city block under its
// buildings) is halved first, so the grid stays small.
void freeRects(const Rect& r, const std::vector<Rect>& blk, std::vector<Rect>& out, int depth = 0) {
    if (blk.empty()) { out.push_back(r); return; }
    if (blk.size() > kNavSplitAt && depth < 16) {
        bool alongX = r.x1 - r.x0 >= r.z1 - r.z0;
        float mid = alongX ? (r.x0 + r.x1) * 0.5f : (r.z0 + r.z1) * 0.5f;
        Rect h[2] = {r, r};
        if (alongX) { h[0].x1 = mid; h[1].x0 = mid; } else { h[0].z1 = mid; h[1].z0 = mid; }
        for (const Rect& hr : h) {
            std::vector<Rect> sub;
            for (const Rect& b : blk) {
                Rect c = {std::max(b.x0, hr.x0), std::max(b.z0, hr.z0), std::min(b.x1, hr.x1), std::min(b.z1, hr.z1)};
                if (c.x1 > c.x0 && c.z1 > c.z0) sub.push_back(c);
            }
            freeRects(hr, sub, out, sub.size() < blk.size() ? depth + 1 : 16);   // (no fewer: halving again gains nothing)
        }
        return;
    }
    auto edges = [](float lo, float hi, std::vector<float>& v) {
        std::sort(v.begin(), v.end());
        std::vector<float> e = {lo};
        for (float x : v) if (x - e.back() >= kNavSnap && hi - x >= kNavSnap) e.push_back(x);
        e.push_back(hi);
        v.swap(e);
    };
    std::vector<float> xs, zs;
    for (const Rect& b : blk) { xs.push_back(b.x0); xs.push_back(b.x1); zs.push_back(b.z0); zs.push_back(b.z1); }
    edges(r.x0, r.x1, xs); edges(r.z0, r.z1, zs);
    const int nx = (int)xs.size() - 1, nz = (int)zs.size() - 1;
    auto nearest = [](const std::vector<float>& v, float x) {   // the (merged) edge a blocker's edge became
        int i = (int)(std::lower_bound(v.begin(), v.end(), x) - v.begin());
        if (i >= (int)v.size()) return (int)v.size() - 1;
        if (i > 0 && x - v[i - 1] < v[i] - x) i--;
        return i;
    };
    std::vector<char> used((size_t)nx * nz, 0);
    for (const Rect& b : blk) {
        int i0 = nearest(xs, b.x0), i1 = nearest(xs, b.x1), k0 = nearest(zs, b.z0), k1 = nearest(zs, b.z1);
        for (int k = k0; k < k1; k++) for (int i = i0; i < i1; i++) used[(size_t)k * nx + i] = 1;
    }
    auto isFree = [&](int k, int i) { return !used[(size_t)k * nx + i]; };
    for (int k = 0; k < nz; k++)
        for (int i = 0; i < nx; i++) {
            if (!isFree(k, i)) continue;
            // the bigger of: a run along x grown along z, and a run along z grown along x (no long thin strips where
            // the other way round makes a square)
            int ia = i + 1;
            while (ia < nx && isFree(k, ia)) ia++;
            int ka = k + 1;
            for (bool ok = true; ka < nz && ok; ka += ok) for (int q = i; q < ia && ok; q++) ok = isFree(ka, q);
            int kb = k + 1;
            while (kb < nz && isFree(kb, i)) kb++;
            int ib = i + 1;
            for (bool ok = true; ib < nx && ok; ib += ok) for (int q = k; q < kb && ok; q++) ok = isFree(q, ib);
            const bool a = (xs[ia] - xs[i]) * (zs[ka] - zs[k]) >= (xs[ib] - xs[i]) * (zs[kb] - zs[k]);
            const int i1 = a ? ia : ib, k1 = a ? ka : kb;
            for (int kk = k; kk < k1; kk++) for (int q = i; q < i1; q++) used[(size_t)kk * nx + q] = 1;
            out.push_back({xs[i], zs[k], xs[i1], zs[k1]});
        }
}

float centreDist(float ax0, float az0, float ax1, float az1, float bx0, float bz0, float bx1, float bz1) {
    float cx = ((ax0 + ax1) - (bx0 + bx1)) * 0.5f, cz = ((az0 + az1) - (bz0 + bz1)) * 0.5f;
    return std::sqrt(cx * cx + cz * cz);
}

// A* over adjacency lists (the surfaces' links, or the regions'), with a heuristic h(v) that never overestimates and
// returns kNavInf for "can't get there from v". Reuses the scratch (stamped: nothing cleared per search).
template <class H>
bool astar(int a, int b, size_t n, const std::vector<std::vector<std::pair<int, float>>>& adj, NavGraph::PathScratch& s, H h, std::vector<int>& out) {
    out.clear();
    auto& [g, hc, prev, stamp, heap, cur] = s;
    if (g.size() < n) { g.resize(n); hc.resize(n); prev.resize(n); stamp.assign(n, 0); cur = 0; }
    if (++cur == 0) { std::fill(stamp.begin(), stamp.end(), 0); cur = 1; }
    heap.clear();
    stamp[a] = cur; g[a] = 0; prev[a] = -1; hc[a] = h(a);
    if (hc[a] >= kNavInf) return false;
    heap.push_back({-hc[a], a});
    auto cmp = [](const std::pair<float, int>& x, const std::pair<float, int>& y) { return x.first < y.first; }; // max-heap on -f
    bool found = false;
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), cmp);
        auto [nf, u] = heap.back();
        heap.pop_back();
        if (-nf > g[u] + hc[u] + 1e-4f) continue; // stale entry
        if (u == b) { found = true; break; }
        for (auto& [v, c] : adj[u]) {
            float ng = g[u] + c;
            if (stamp[v] != cur) { stamp[v] = cur; hc[v] = h(v); g[v] = kNavInf; }
            if (ng < g[v] && hc[v] < kNavInf) {
                g[v] = ng; prev[v] = u;
                heap.push_back({-(ng + hc[v]), v});
                std::push_heap(heap.begin(), heap.end(), cmp);
            }
        }
    }
    if (!found) return false;
    for (int v = b; v != a; v = prev[v]) out.push_back(v);
    std::reverse(out.begin(), out.end());
    return true;
}
}  // namespace

int NavGraph::cellOf(float x, float z) const {
    int cx = (int)std::floor((x - gx0) / kNavCell), cz = (int)std::floor((z - gz0) / kNavCell);
    if (cx < 0 || cz < 0 || cx >= gw || cz >= gh) return -1;
    return cz * gw + cx;
}

// The part of linkCost that's the same both ways: is the way between surfaces i and j open (for a player at the
// higher one's level), and not just a shortcut over a surface between them.
bool NavGraph::linkOpen(const Map& m, int i, int j) const {
    if (kind == NavKind::Surfaces) return true;   // (gap and height alone)
    const Node &a = nodes[i], &b = nodes[j];
    // the overlap of the two along each axis (negative: the gap between them)
    const float ox = std::min(a.x1, b.x1) - std::max(a.x0, b.x0), oz = std::min(a.z1, b.z1) - std::max(a.z0, b.z0);
    if (surf[i] == surf[j]) return ox >= 0 && oz >= 0 && std::max(ox, oz) >= 0.01f; // pieces of one top: where they meet along an edge
    if (ox > 1e-4f && oz > 1e-4f) return true;   // one over the other (a step on a floor, a ledge over one)
    // level, meeting along an edge: anything standing across it would have been cut out of both
    if (ox >= -1e-4f && oz >= -1e-4f && std::max(ox, oz) >= 0.05f && std::fabs(a.y - b.y) < 1e-4f) return true;
    // side by side: the stretch between them (a line where they meet)
    float cx0 = std::min(a.x1, b.x1), cx1 = std::max(a.x0, b.x0), cz0 = std::min(a.z1, b.z1), cz1 = std::max(a.z0, b.z0);
    if (ox > 0) { cx0 = std::max(a.x0, b.x0); cx1 = std::min(a.x1, b.x1); }
    if (oz > 0) { cz0 = std::max(a.z0, b.z0); cz1 = std::min(a.z1, b.z1); }
    const float base = std::max(a.y, b.y), dy = std::fabs(b.y - a.y);
    if (ox < -1e-4f || oz < -1e-4f) {
        // a gap: another surface in it, from about the lower one's level to a step over the higher one's, is walked onto
        // (or landed on) on the way across. The route goes by that one, so a link straight across would only be a
        // shortcut (and a floor cut into pieces would have a great many).
        const float px = (cx0 + cx1) * 0.5f, pz = (cz0 + cz1) * 0.5f;
        const float lo = std::min(a.y, b.y) - (dy <= kNavStep ? 0.6f : 0.05f), hi = base + kNavStep;
        int c = cellOf(px, pz);
        if (c >= 0)
            for (uint32_t k = cellStart[c]; k < cellStart[c + 1]; k++) {
                const Node& n = nodes[cellNodes[k]];
                if ((n.y >= lo) & (n.y <= hi) & (px >= n.x0) & (px <= n.x1) & (pz >= n.z0) & (pz <= n.z1)) return false;   // (&: no branch per test)
            }
    }
    const float yLo = base + kNavStep, yHi = base + kNavHead;
    if (ox < -1e-4f && oz < -1e-4f) {
        // corner to corner (a jump across to a pad off to one side): the line between the near corners, which is where
        // steer() aims, clear of anything standing in the way (grown by the hull, less what the cutting may leave)
        const float sx = a.x1 < b.x0 ? a.x1 : a.x0, sz = a.z1 < b.z0 ? a.z1 : a.z0;
        const float ex = a.x1 < b.x0 ? b.x0 : b.x1, ez = a.z1 < b.z0 ? b.z0 : b.z1;
        const float dx = ex - sx, dz = ez - sz, grow = kNavClear - kNavSnap;
        bool blocked = false;
        brushesNear(m, cx0 - kNavClear, cz0 - kNavClear, cx1 + kNavClear, cz1 + kNavClear, [&](int k) {
            const AABB& bb = m.brushes[k].box;
            if (blocked || bb.mn.y >= yHi || bb.mx.y <= yLo) return;
            // slab test of the segment against the grown box in xz (dx, dz are never 0 here: both gaps are open)
            float t0x = (bb.mn.x - grow - sx) / dx, t1x = (bb.mx.x + grow - sx) / dx;
            float t0z = (bb.mn.z - grow - sz) / dz, t1z = (bb.mx.z + grow - sz) / dz;
            float t0 = std::max(std::min(t0x, t1x), std::min(t0z, t1z)), t1 = std::min(std::max(t0x, t1x), std::max(t0z, t1z));
            blocked = t0 < std::min(t1, 1.0f) && t1 > 0.0f;
        });
        return !blocked;
    }
    // nothing standing in the stretch (a wall between two floors, a crate on the seam), grown by the hull like the
    // cuts. Not counting what reaches less than kNavSnap into it, the most the cutting's merged edges can leave of a
    // blocker in a piece (so on a line: only a blocker reaching that far past both sides of it).
    const bool lineX = cx1 - cx0 < 2e-4f, lineZ = cz1 - cz0 < 2e-4f;
    const float ax0 = lineX ? cx1 + kNavSnap : cx0 + kNavSnap, ax1 = lineX ? cx0 - kNavSnap : cx1 - kNavSnap;
    const float az0 = lineZ ? cz1 + kNavSnap : cz0 + kNavSnap, az1 = lineZ ? cz0 - kNavSnap : cz1 - kNavSnap;
    bool blocked = false;
    brushesNear(m, cx0 - kNavClear, cz0 - kNavClear, cx1 + kNavClear, cz1 + kNavClear, [&](int k) {
        const AABB& bb = m.brushes[k].box;
        blocked |= (bb.mn.y < yHi) & (bb.mx.y > yLo) & (bb.mn.x - kNavClear < ax1) & (bb.mx.x + kNavClear > ax0) &
                   (bb.mn.z - kNavClear < az1) & (bb.mx.z + kNavClear > az0);   // (&: no branch per test)
    });
    return !blocked;
}

namespace {
float linkGap(const NavGraph::Node& a, const NavGraph::Node& b) {
    float gx = std::max(0.0f, std::max(a.x0, b.x0) - std::min(a.x1, b.x1));
    float gz = std::max(0.0f, std::max(a.z0, b.z0) - std::min(a.z1, b.z1));
    return std::sqrt(gx * gx + gz * gz);
}
bool linkReach(float gap, float up) { return gap <= kLinkGap && up <= 1.15f && up >= -8.0f; }
float linkCostOf(const NavGraph::Node& a, const NavGraph::Node& b, float gap) {
    return centreDist(a.x0, a.z0, a.x1, a.z1, b.x0, b.z0, b.x1, b.z1) + gap * 2 + std::max(b.y - a.y, 0.0f) * 2;
}
}  // namespace

bool NavGraph::linkCost(const Map& m, int i, int j, float& cost) const {
    if (i == j) return false;
    const Node &a = nodes[i], &b = nodes[j];
    float gap = linkGap(a, b);
    if (!linkReach(gap, b.y - a.y) || !linkOpen(m, i, j)) return false;
    cost = linkCostOf(a, b, gap);
    return true;
}

void NavGraph::build(const Map& m, NavKind k) {
    kind = k;
    nodes.clear(); surf.clear(); links.clear(); region.clear(); regions.clear(); rlinks.clear(); rcomp.clear();
    mapId = m.id();
    generation++;
    // Surfaces: the top of every solid block, with whatever stands on it cut out, in rectangles (walls, crates and
    // pillars on a floor split it, so a route goes round them instead of into them).
    std::vector<uint32_t> seen(m.brushes.size(), 0);
    uint32_t mark = 0;
    std::vector<Rect> blk, pieces;
    for (size_t bi = 0; bi < m.brushes.size(); bi++) {
        const Brush& b = m.brushes[bi];
        const AABB& a = b.box;
        if (b.material >= 5) continue; // far scenery, water / lava
        if (a.mx.x - a.mn.x < 0.4f || a.mx.z - a.mn.z < 0.4f) continue; // (0.5 m stair steps are floor)
        const Rect r = {a.mn.x, a.mn.z, a.mx.x, a.mx.z};
        const float y = a.mx.y;
        blk.clear();
        ++mark;
        if (kind == NavKind::Bots) brushesNear(m, r.x0 - kNavClear, r.z0 - kNavClear, r.x1 + kNavClear, r.z1 + kNavClear, [&](int k) {
            if (seen[k] == mark) return;
            seen[k] = mark;
            const AABB& o = m.brushes[k].box;
            if (!blocksAt(o, y)) return;
            Rect c = {std::max(o.mn.x - kNavClear, r.x0), std::max(o.mn.z - kNavClear, r.z0), std::min(o.mx.x + kNavClear, r.x1), std::min(o.mx.z + kNavClear, r.z1)};
            if (c.x1 > c.x0 && c.z1 > c.z0) blk.push_back(c);
        });
        pieces.clear();
        if (kind == NavKind::Surfaces) {
            // the whole top, if a player fits on it somewhere (5x5 samples: other blocks may cover parts of it)
            bool free = false;
            for (int s = 0; s < 25 && !free; s++) {
                Vec3 top = {lerpf(a.mn.x, a.mx.x, 0.1f + 0.2f * (s % 5)), a.mx.y + 0.02f, lerpf(a.mn.z, a.mx.z, 0.1f + 0.2f * (s / 5))};
                AABB probe = {{top.x - 0.05f, top.y, top.z - 0.05f}, {top.x + 0.05f, top.y + kPlayerCrouchHeight, top.z + 0.05f}};
                free = !m.boxSolid(probe);
            }
            if (free) pieces.push_back(r);
        } else {
            freeRects(r, blk, pieces);
        }
        for (const Rect& p : pieces) {
            nodes.push_back({p.x0, p.z0, p.x1, p.z1, y});
            surf.push_back((int)bi);
        }
    }
    const int n = (int)nodes.size();
    // Grid of the surfaces (each in every cell its padded rectangle touches): nodeAt looks in one cell, and the
    // link pass only compares surfaces within jumping distance instead of every pair (REVIEW PERF-10).
    cellStart.clear(); cellNodes.clear(); gw = gh = 0;
    if (n) {
        float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
        for (auto& nd : nodes) { x0 = std::min(x0, nd.x0); z0 = std::min(z0, nd.z0); x1 = std::max(x1, nd.x1); z1 = std::max(z1, nd.z1); }
        gx0 = x0 - kNodePad - 0.01f; gz0 = z0 - kNodePad - 0.01f;
        gw = std::max(1, (int)std::ceil((x1 + kNodePad + 0.01f - gx0) / kNavCell));
        gh = std::max(1, (int)std::ceil((z1 + kNodePad + 0.01f - gz0) / kNavCell));
        if ((size_t)gw * gh > 4u * 1024 * 1024) gw = gh = 0; // absurd extents: no grid (checks every surface instead)
        auto range = [&](const Node& nd, float pad, int& cx0, int& cz0, int& cx1, int& cz1) {
            cx0 = std::clamp((int)std::floor((nd.x0 - pad - gx0) / kNavCell), 0, gw - 1);
            cx1 = std::clamp((int)std::floor((nd.x1 + pad - gx0) / kNavCell), 0, gw - 1);
            cz0 = std::clamp((int)std::floor((nd.z0 - pad - gz0) / kNavCell), 0, gh - 1);
            cz1 = std::clamp((int)std::floor((nd.z1 + pad - gz0) / kNavCell), 0, gh - 1);
        };
        if (gw > 0) {
            std::vector<uint32_t> count((size_t)gw * gh + 1, 0);
            for (auto& nd : nodes) {
                int a, b, c, d; range(nd, kNodePad, a, b, c, d);
                for (int z = b; z <= d; z++) for (int x = a; x <= c; x++) count[(size_t)z * gw + x]++;
            }
            cellStart.assign((size_t)gw * gh + 1, 0);
            for (size_t i = 0; i < (size_t)gw * gh; i++) cellStart[i + 1] = cellStart[i] + count[i];
            cellNodes.resize(cellStart.back());
            std::vector<uint32_t> fill(cellStart.begin(), cellStart.end() - 1);
            for (int i = 0; i < n; i++) { // (ascending: each cell's list is in node order)
                int a, b, c, d; range(nodes[i], kNodePad, a, b, c, d);
                for (int z = b; z <= d; z++) for (int x = a; x <= c; x++) cellNodes[fill[(size_t)z * gw + x]++] = i;
            }
        }
    }
    // Links: each pair once (j after i; the open check is the same both ways), into both lists in ascending order
    // (the order checking every pair gives).
    struct Pair { int from, to; float cost; };
    std::vector<Pair> pairs;   // (collected flat, then grouped by node: no list grows one link at a time)
    std::vector<uint32_t> stamp(n, 0);
    std::vector<int> cand;
    for (int i = 0; i < n; i++) {
        cand.clear();
        const Node& a = nodes[i];
        if (gw > 0) {
            int cx0 = std::clamp((int)std::floor((a.x0 - kLinkGap - gx0) / kNavCell), 0, gw - 1), cx1 = std::clamp((int)std::floor((a.x1 + kLinkGap - gx0) / kNavCell), 0, gw - 1);
            int cz0 = std::clamp((int)std::floor((a.z0 - kLinkGap - gz0) / kNavCell), 0, gh - 1), cz1 = std::clamp((int)std::floor((a.z1 + kLinkGap - gz0) / kNavCell), 0, gh - 1);
            for (int z = cz0; z <= cz1; z++)
                for (int x = cx0; x <= cx1; x++)
                    for (uint32_t k = cellStart[(size_t)z * gw + x]; k < cellStart[(size_t)z * gw + x + 1]; k++) {
                        int j = cellNodes[k];
                        if (j > i && stamp[j] != (uint32_t)i + 1) { stamp[j] = (uint32_t)i + 1; cand.push_back(j); }
                    }
            std::sort(cand.begin(), cand.end());
        } else {
            for (int j = i + 1; j < n; j++) cand.push_back(j);
        }
        for (int j : cand) {
            const Node& b = nodes[j];
            float gap = linkGap(a, b);
            bool ij = linkReach(gap, b.y - a.y), ji = linkReach(gap, a.y - b.y);
            if (!ij && !ji) continue;
            if (!linkOpen(m, i, j)) continue;
            if (ij) pairs.push_back({i, j, linkCostOf(a, b, gap)});
            if (ji) pairs.push_back({j, i, linkCostOf(b, a, gap)});
        }
    }
    {   // each node's links in ascending order (the order checking every pair gives): a stable counting sort by node
        std::vector<uint32_t> count(n + 1, 0);
        for (const Pair& p : pairs) count[p.from + 1]++;
        for (int i = 0; i < n; i++) count[i + 1] += count[i];
        std::vector<std::pair<int, float>> flat(pairs.size());
        std::vector<uint32_t> at(count.begin(), count.end() - 1);
        for (const Pair& p : pairs) flat[at[p.from]++] = {p.to, p.cost};
        links.assign(n, {});
        for (int i = 0; i < n; i++) {
            links[i].assign(flat.begin() + count[i], flat.begin() + count[i + 1]);
            std::sort(links[i].begin(), links[i].end(), [](const std::pair<int, float>& x, const std::pair<int, float>& y) { return x.first < y.first; });
        }
    }
    // ladders: from the surface at the foot of each ladder up to the surfaces at its top
    ladderLinks.clear();
    for (const AABB& l : m.ladders) {
        Vec3 c = l.center();
        // (a ladder against a wall stands in the strip cut off the floor along it: the floor a step out from it)
        int bottom = nodeNear({c.x, l.mn.y + 0.05f, c.z}, 1.0f);
        if (bottom < 0) continue;
        for (int j = 0; j < n; j++) {
            const Node& nd = nodes[j];
            if (nd.y > l.mx.y + 0.2f || nd.y < l.mx.y - 1.8f || j == bottom) continue; // the climb volume ends ~1 m over the top floor
            float gx = std::max(0.0f, std::max(l.mn.x, nd.x0) - std::min(l.mx.x, nd.x1));
            float gz = std::max(0.0f, std::max(l.mn.z, nd.z0) - std::min(l.mx.z, nd.z1));
            if (gx * gx + gz * gz > 1.5f * 1.5f) continue;
            links[bottom].push_back({j, (l.mx.y - l.mn.y) * 2 + 2});
            ladderLinks.push_back({{bottom, j}, Vec3(c.x, l.mn.y, c.z)});
        }
    }
    // Regions: the pieces of one top that join up (where they meet) within a 12 m tile, so not many more than there are
    // tops (a floor a wall cuts in two is two; a street through a city a few). Routes are planned over the regions,
    // then walked over the pieces of the regions planned (findPath). (The tiles keep a region's centre near all of
    // it, so the plan's costs are near the walk's: routes come out within a few % of the shortest, quark_tests nav.)
    region.assign(n, -1);
    std::vector<int> q;
    auto tile = [&](int i) { const Node& nd = nodes[i]; return std::make_pair((int)std::floor((nd.x0 + nd.x1) * 0.5f / kNavTile), (int)std::floor((nd.z0 + nd.z1) * 0.5f / kNavTile)); };
    for (int i = 0; i < n; i++) {
        if (region[i] >= 0) continue;
        const auto ti = tile(i);
        const int r = (int)regions.size();
        Region rg = {nodes[i].x0, nodes[i].z0, nodes[i].x1, nodes[i].z1, nodes[i].y};
        region[i] = r;
        q.assign(1, i);
        for (size_t k = 0; k < q.size(); k++)
            for (auto& l : links[q[k]])
                if (region[l.first] < 0 && surf[l.first] == surf[i] && tile(l.first) == ti) {
                    const Node& nd = nodes[l.first];
                    region[l.first] = r; q.push_back(l.first);
                    rg.x0 = std::min(rg.x0, nd.x0); rg.z0 = std::min(rg.z0, nd.z0); rg.x1 = std::max(rg.x1, nd.x1); rg.z1 = std::max(rg.z1, nd.z1);
                }
        regions.push_back(rg);
    }
    // ...linked where any of their pieces are, at the old cost (centre to centre, plus gaps and climbs; a ladder its climb)
    const int R = (int)regions.size();
    rlinks.assign(R, {});
    std::vector<int> at(R, -1);
    std::vector<std::vector<int>> members(R);
    for (int i = 0; i < n; i++) members[region[i]].push_back(i);
    for (int r = 0; r < R; r++) {
        const Region& ra = regions[r];
        for (int i : members[r])
            for (auto& [j, c] : links[i]) {
                const int s = region[j];
                if (s == r) continue;
                const Node &a = nodes[i], &b = nodes[j];
                const Region& rb = regions[s];
                float rc = std::max(0.01f, c - centreDist(a.x0, a.z0, a.x1, a.z1, b.x0, b.z0, b.x1, b.z1) + centreDist(ra.x0, ra.z0, ra.x1, ra.z1, rb.x0, rb.z0, rb.x1, rb.z1));
                if (at[s] < 0) { at[s] = (int)rlinks[r].size(); rlinks[r].push_back({s, rc}); }
                else rlinks[r][at[s]].second = std::min(rlinks[r][at[s]].second, rc);
            }
        for (auto& l : rlinks[r]) at[l.first] = -1;
    }
    // connected groups of regions (links taken both ways): a search between two groups fails at once
    rcomp.resize(R);
    for (int r = 0; r < R; r++) rcomp[r] = r;
    auto root = [&](int x) { while (rcomp[x] != x) x = rcomp[x] = rcomp[rcomp[x]]; return x; };
    for (int r = 0; r < R; r++)
        for (auto& l : rlinks[r]) { int ra = root(r), rb = root(l.first); if (ra != rb) rcomp[std::max(ra, rb)] = std::min(ra, rb); }
    for (int r = 0; r < R; r++) rcomp[r] = root(r);
    // A* heuristic scale: the smallest cost per metre of straight-line distance between region centres over all
    // links (ladders can be cheap for how far apart their regions' centres are), so it never overestimates.
    hScale = 1.0f;
    for (int r = 0; r < R; r++)
        for (auto& [s, c] : rlinks[r]) {
            const Region &a = regions[r], &b = regions[s];
            float d = centreDist(a.x0, a.z0, a.x1, a.z1, b.x0, b.z0, b.x1, b.z1);
            if (d > 1e-3f) hScale = std::min(hScale, c / d);
        }
    hScale = std::max(0.0f, hScale * 0.999f);
    hScaleNodes = 1.0f;   // (the same over the pieces' links, for the walk through the planned regions)
    for (int i = 0; i < n; i++)
        for (auto& [j, c] : links[i]) {
            const Node &a = nodes[i], &b = nodes[j];
            float d = centreDist(a.x0, a.z0, a.x1, a.z1, b.x0, b.z0, b.x1, b.z1);
            if (d > 1e-3f) hScaleNodes = std::min(hScaleNodes, c / d);
        }
    hScaleNodes = std::max(0.0f, hScaleNodes * 0.999f);
    buildLandmarks();
}

// A* landmarks (ALT) over the regions: a few far apart, with every region's route distance from and to each. The
// triangle rule bounds a route from below (from v to t: at least d(L,t) - d(L,v), and d(v,L) - d(t,L)), far closer than
// a straight line round a city's walls, so a search opens far fewer regions; and a goal it can't reach shows at once (L
// reaches v but not t, or t reaches L and v can't). Still never an overestimate, so routes stay the shortest.
void NavGraph::buildLandmarks() {
    landFwd.clear(); landBwd.clear(); nLand = 0;
    const int R = (int)regions.size();
    if (R < 32) return;   // (small maps: the straight line does fine)
    std::vector<std::vector<std::pair<int, float>>> rev(R);
    for (int r = 0; r < R; r++) for (auto& [s, c] : rlinks[r]) rev[s].push_back({r, c});
    auto dijkstra = [&](int src, const std::vector<std::vector<std::pair<int, float>>>& adj, std::vector<float>& d) {
        d.assign(R, kNavInf);
        std::vector<std::pair<float, int>> q;
        auto cmp = [](const std::pair<float, int>& x, const std::pair<float, int>& y) { return x.first > y.first; };
        d[src] = 0; q.push_back({0, src});
        while (!q.empty()) {
            std::pop_heap(q.begin(), q.end(), cmp);
            auto [du, u] = q.back(); q.pop_back();
            if (du > d[u]) continue;
            for (auto& [v, c] : adj[u]) if (du + c < d[v]) { d[v] = du + c; q.push_back({d[v], v}); std::push_heap(q.begin(), q.end(), cmp); }
        }
    };
    // in the biggest group: the first landmark farthest from its first region, each next farthest from those picked
    std::vector<int> size(R, 0);
    for (int r = 0; r < R; r++) size[rcomp[r]]++;
    const int big = (int)(std::max_element(size.begin(), size.end()) - size.begin());
    int start = 0;
    while (rcomp[start] != big) start++;
    std::vector<float> d, near(R, kNavInf), fwd[kNavLandmarks], bwd[kNavLandmarks];
    dijkstra(start, rlinks, d);
    auto farthest = [&](const std::vector<float>& w) {
        int best = -1; float bd = 0;
        for (int r = 0; r < R; r++) if (rcomp[r] == big && w[r] < kNavInf && w[r] > bd) { bd = w[r]; best = r; }
        return best;
    };
    for (int L = farthest(d); L >= 0 && nLand < kNavLandmarks; L = farthest(near)) {
        dijkstra(L, rlinks, fwd[nLand]);
        dijkstra(L, rev, bwd[nLand]);
        for (int r = 0; r < R; r++) near[r] = std::min(near[r], std::min(fwd[nLand][r], bwd[nLand][r]));
        nLand++;
    }
    landFwd.resize((size_t)R * nLand); landBwd.resize((size_t)R * nLand);
    for (int r = 0; r < R; r++)
        for (int l = 0; l < nLand; l++) { landFwd[(size_t)r * nLand + l] = fwd[l][r]; landBwd[(size_t)r * nLand + l] = bwd[l][r]; }
}

int NavGraph::nodeAt(const Vec3& p) const {
    // the highest surface under the point (within a step over it, 6 m under it); of equal ones, the one it's on (or
    // nearest to: pieces of a top meet, and their padding overlaps). (Surfaces: the first of equal ones, as the map
    // report has always had it.)
    int best = -1;
    float bestY = -1e9f, bestOut = 1e9f;
    const float tie = kind == NavKind::Bots ? 1e-4f : 0.0f;
    auto test = [&](int i) {
        const Node& n = nodes[i];
        if (p.x < n.x0 - kNodePad || p.x > n.x1 + kNodePad || p.z < n.z0 - kNodePad || p.z > n.z1 + kNodePad) return;
        if (n.y > p.y + 0.6f || n.y < p.y - 6.0f) return; // the surface below us, not one overhead
        float out = std::max({n.x0 - p.x, p.x - n.x1, 0.0f}) + std::max({n.z0 - p.z, p.z - n.z1, 0.0f});
        if (n.y > bestY + tie || (tie > 0 && n.y > bestY - tie && out < bestOut)) { bestY = n.y; bestOut = out; best = i; }
    };
    if (gw > 0) {
        int c = cellOf(p.x, p.z);
        if (c < 0) return -1; // (every surface's padded rectangle is inside the grid)
        for (uint32_t k = cellStart[c]; k < cellStart[c + 1]; k++) test(cellNodes[k]);
    } else {
        for (size_t i = 0; i < nodes.size(); i++) test((int)i);
    }
    return best;
}

int NavGraph::nodeNear(const Vec3& p, float radius) const {
    int best = nodeAt(p);
    if (best >= 0 || radius <= 0) return best;
    // not over a node (in the strip cut round a wall, a crate, a mast): the nearest one within radius (a level about
    // the same counts more than a few metres of distance)
    float bestD = 1e30f;
    auto test = [&](int i) {
        const Node& n = nodes[i];
        if (n.y > p.y + 0.6f || n.y < p.y - 6.0f) return;
        float dx = std::max({n.x0 - p.x, p.x - n.x1, 0.0f}), dz = std::max({n.z0 - p.z, p.z - n.z1, 0.0f});
        float d = dx * dx + dz * dz;
        if (d > radius * radius) return;
        d += (p.y - n.y) * (p.y - n.y) * 0.25f;
        if (d < bestD) { bestD = d; best = i; }
    };
    if (gw > 0) {
        int cx0 = std::clamp((int)std::floor((p.x - radius - gx0) / kNavCell), 0, gw - 1), cx1 = std::clamp((int)std::floor((p.x + radius - gx0) / kNavCell), 0, gw - 1);
        int cz0 = std::clamp((int)std::floor((p.z - radius - gz0) / kNavCell), 0, gh - 1), cz1 = std::clamp((int)std::floor((p.z + radius - gz0) / kNavCell), 0, gh - 1);
        for (int z = cz0; z <= cz1; z++)
            for (int x = cx0; x <= cx1; x++)
                for (uint32_t k = cellStart[(size_t)z * gw + x]; k < cellStart[(size_t)z * gw + x + 1]; k++) test(cellNodes[k]);
    } else {
        for (size_t i = 0; i < nodes.size(); i++) test((int)i);
    }
    return best;
}

bool NavGraph::findRegionPath(int ra, int rb, std::vector<int>& out) const {
    out.clear();
    const int R = (int)regions.size();
    if (ra < 0 || rb < 0 || ra >= R || rb >= R) return false;
    if (ra == rb) return true;
    if (rcomp[ra] != rcomp[rb]) return false;   // (no link between their groups either way)
    auto cx = [&](int r) { return (regions[r].x0 + regions[r].x1) * 0.5f; };
    auto cz = [&](int r) { return (regions[r].z0 + regions[r].z1) * 0.5f; };
    const float bx = cx(rb), bz = cz(rb);
    const int K = nLand;
    const float* tf = K ? &landFwd[(size_t)rb * K] : nullptr;
    const float* tb = K ? &landBwd[(size_t)rb * K] : nullptr;
    auto h = [&](int r) {
        float dx = cx(r) - bx, dz = cz(r) - bz, e = std::sqrt(dx * dx + dz * dz) * hScale;
        if (K) {
            const float *vf = &landFwd[(size_t)r * K], *vb = &landBwd[(size_t)r * K];
            for (int l = 0; l < K; l++) {
                if (tf[l] < kNavInf) { if (vf[l] < kNavInf) e = std::max(e, (tf[l] - vf[l]) * 0.9999f); }
                else if (vf[l] < kNavInf) return kNavInf;   // L reaches r but not the goal: r can't either
                if (tb[l] < kNavInf) { if (vb[l] >= kNavInf) return kNavInf; e = std::max(e, (vb[l] - tb[l]) * 0.9999f); } // the goal reaches L, r can't
            }
        }
        return e;
    };
    return astar(ra, rb, regions.size(), rlinks, regionScratch_, h, out);
}

bool NavGraph::findPath(int a, int b, std::vector<int>& out) const {
    out.clear();
    if (a < 0 || b < 0 || a >= (int)nodes.size() || b >= (int)nodes.size()) return false;
    if (a == b) return true;
    // the regions to cross (A* over the regions), then the shortest walk over the pieces of those regions only
    std::vector<int>& rp = regionPath_;
    if (!findRegionPath(region[a], region[b], rp)) return false;
    if (corridor_.size() < regions.size()) corridor_.resize(regions.size(), 0);
    if (++corridorMark_ == 0) { std::fill(corridor_.begin(), corridor_.end(), 0); corridorMark_ = 1; }
    corridor_[region[a]] = corridorMark_;
    for (int r : rp) corridor_[r] = corridorMark_;
    const float bx = (nodes[b].x0 + nodes[b].x1) * 0.5f, bz = (nodes[b].z0 + nodes[b].z1) * 0.5f;
    auto h = [&](int i) {
        if (corridor_[region[i]] != corridorMark_) return kNavInf;
        float dx = (nodes[i].x0 + nodes[i].x1) * 0.5f - bx, dz = (nodes[i].z0 + nodes[i].z1) * 0.5f - bz;
        return std::sqrt(dx * dx + dz * dz) * hScaleNodes;
    };
    return astar(a, b, nodes.size(), links, scratch_, h, out);
}

Vec3 NavGraph::steer(const Vec3& from, int prevNode, const std::vector<int>& path, const Vec3& to) const {
    // Walk through the "portal" between consecutive surfaces: the stretch of their shared edge (or the
    // next surface itself when they overlap, e.g. stepping down off a block). Clamping to the shared edge
    // keeps bots from cutting corners across a hole between two surfaces. If that point is right where we
    // stand, look further along the path.
    if (prevNode < 0) return to;
    for (int nodeIdx : path) {
        const Node& n = nodes[nodeIdx];
        const Node& pn = nodes[prevNode];
        bool ladder = false;
        for (auto& ll : ladderLinks)
            if (ll.first.first == prevNode && ll.first.second == nodeIdx) { // climb: walk into the ladder
                float dx = ll.second.x - from.x, dz = ll.second.z - from.z;
                if (dx * dx + dz * dz > 0.36f) return ll.second;
                ladder = true;
            }
        prevNode = nodeIdx;
        if (ladder) return {(n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f}; // on it: keep pushing toward the top
        auto span = [](float a0, float a1, float b0, float b1, float& lo, float& hi) {
            lo = std::max(a0, b0); hi = std::min(a1, b1);
            if (lo > hi) { lo = hi = (a1 < b0 ? b0 : b1); return; }        // no overlap: the near edge of the next surface
            float inset = std::min(0.5f, (hi - lo) * 0.5f);                  // keep away from the ends of the shared edge
            lo += inset; hi -= inset;
        };
        float x0, x1, z0, z1;
        span(pn.x0, pn.x1, n.x0, n.x1, x0, x1);
        span(pn.z0, pn.z1, n.z0, n.z1, z0, z1);
        Vec3 p = {clampf(from.x, x0, x1), n.y, clampf(from.z, z0, z1)};
        float dx = p.x - from.x, dz = p.z - from.z;
        if (dx * dx + dz * dz > 1.0f) return p;
    }
    return to;
}

void NavGraph::buildAsync(Map m) {
    auto j = std::make_shared<NavJob>();
    j->id = m.id();   // (hashed here, once: the thread and ready() only read it)
    j->map = std::move(m);
    job = j;
    std::thread([j] { j->result.build(j->map); j->done.store(true, std::memory_order_release); }).detach();
}

bool NavGraph::ready(const Map& m) {
    if (job && job->done.load(std::memory_order_acquire)) {
        auto j = std::move(job);
        if (j->result.mapId == m.id()) {
            uint32_t gen = generation;
            *this = std::move(j->result);   // (its job is empty: ours was just taken)
            generation = gen + 1;
        }
    }
    if (mapId == m.id()) return true;
    if (!job || job->id != m.id()) buildAsync(m); // changed under us (map edits): rebuild in the background
    return false;
}

// =====================================================================================================
// Bots
// =====================================================================================================

namespace {

float rnd(BotBrain& b) { return brainRnd(b.rng); }   // xorshift, 0..1 (botbrain.h)
float gauss(BotBrain& b) { return brainGauss(b.rng); } // Box-Muller
// An event that happens `rate` times a second on average: the chance over this tick (any tick length) is
// p = 1 - exp(-rate * dt) (from tickConsts)
bool chance(BotBrain& b, float rate, float p) { return rate > 0 && rnd(b) < p; }

// std::remainder(x, 2 pi), the angle wrapped into -pi..pi. When |x| <= pi it is x itself (the nearest multiple of
// 2 pi is 0, and a tie at exactly pi goes to the even one, 0, as well), which is most calls: the library call (~6-20 ns
// on MinGW) only beyond that. remainder is exact, so this is bit-identical to always calling it.
float wrapAngle(float x) { return std::fabs(x) <= PI ? x : std::remainder(x, 2 * PI); }
float angleDiff(float a, float b) { return wrapAngle(a - b); }
float yawOf(const Vec3& d) { return std::atan2(-d.x, -d.z); }

Vec3 eyeOf(const ServerPlayer& p, float scale = 1.0f) {
    return p.move.pos + Vec3(0, ((p.move.flags & MF_CROUCHED) ? kCrouchEyeHeight : kEyeHeight) * scale, 0);
}

bool isEnemy(const Server& s, int a, int b) {
    if (a == b) return false;
    const auto& pb = s.players_[b];
    if (!pb.connected || !pb.alive || pb.team == TEAM_SPECTATOR) return false;
    if (s.players_[a].isBot && s.bots_[a].onlyEnemy >= 0) return b == s.bots_[a].onlyEnemy;
    if (isItMode(s.ms_.mode)) return s.players_[a].it && !pb.it; // only "it" hunts; everyone else runs
    if (!modeIsTeamBased(s.ms_.mode)) return true;
    return s.players_[a].team != pb.team;
}
// someone this bot watches out for: an enemy, or (hide and seek / tag, as a runner) whoever is "it"
bool isThreat(const Server& s, int a, int b) {
    if (isEnemy(s, a, b)) return true;
    const auto& pb = s.players_[b];
    return a != b && isItMode(s.ms_.mode) && pb.connected && pb.alive && pb.it && !s.players_[a].it;
}

bool canSee(const Server& s, const Vec3& from, const Vec3& to) {
    Vec3 d = to - from;
    float len = length(d);
    if (len < 0.01f) return true;
    return !s.map_.raycastAny(from, d / len, len - 0.1f);
}

// Where a player was `ms` before the last recorded tick (lag-compensation history): what the bot's eyes are
// reporting now. (Counted from the last recorded tick, which is also what a shot fired now is tested against,
// so the bot's lag behind the target is the same at every tick rate.)
// `ticks` = seenAgoTicks(ms, the tick rate) (from tickConsts).
int seenAgoTicks(float ms, int rate) { return 1 + (int)std::lround(ms * rate * 0.001f); }
Vec3 seenAgo(const Server& s, const ServerPlayer& t, float ms, int ticks) {
    if (ticks >= ServerPlayer::kHistory || (int64_t)s.tick_ < ticks) return t.move.pos;
    const auto& h = t.history[(s.tick_ - ticks) % ServerPlayer::kHistory];
    int64_t want = (int64_t)s.timeMs_ - 1000 / s.tickRate() - (int64_t)ms;
    if (!h.alive || std::llabs((int64_t)h.tick - want) > 12) return t.move.pos; // (spawned since, or the rate changed)
    return h.pos;
}

// Ground all along a straight burst (dash): floor within a short drop every couple of metres, so a dash on a map
// with pits never ends over one.
bool groundAlong(const Server& s, const Vec3& from, const Vec3& dir, float len) {
    for (float t = 2.0f; t <= len + 0.01f; t += 2.0f)
        if (!s.map_.raycastAny(from + dir * t + Vec3(0, 0.6f, 0), {0, -1, 0}, 2.5f, true)) return false;
    return !s.map_.raycastAny(from + Vec3(0, 0.6f, 0), dir, len, true); // (and nothing in the way)
}

// Dropping straight down from p lands on something to stand on: not the void below the map, not lava.
bool safeDrop(const Server& s, const Vec3& p) {
    float depth = std::max(p.y - s.map_.killY(), 0.1f);
    if (s.map_.lava.empty()) return s.map_.raycastAny(p, {0, -1, 0}, depth);   // (no lava: any floor will do, not the nearest)
    float t = s.map_.raycast(p, {0, -1, 0}, depth);
    if (t < 0) return false;
    Vec3 h = p - Vec3(0, t, 0);
    return !s.map_.inLava({h + Vec3(-0.3f, 0.0f, -0.3f), h + Vec3(0.3f, 0.6f, 0.3f)});
}

// A grapple pull from where the bot is, with the speed it has, toward `anchor`: momentum is kept and the pull only
// bends it, so the path curves (a sideways run swings wide). Simulated roughly (the pull, its top speed, half
// gravity, sliding along walls; 50 ms steps) up to where the bot lets go, then on in the direction it's flying for
// the skid. Fine on maps without pits; elsewhere every point must be over ground that's safe to drop onto. Checked
// when a hook is planned and again when it catches (the bot has moved by then, and the catch may be off the aim).
bool safeHook(const Server& s, const ServerPlayer& me, const Vec3& anchor) {
    if (!s.map_.hasVoid && s.map_.lava.empty()) return true;
    MoveTuning scratch;   // (the values pmove plays for the bot's state now: movement equations; moveTuning() without)
    const auto& mt = stepTuning(me.move, &s.map_, scratch).m;
    const float accel = 38.0f * mt[MV_HOOK_ACCEL], top = 24.0f * mt[MV_HOOK_MAX], fall = 22.0f * 0.5f * mt[MV_HOOK_GRAVITY]; // (pmove.cpp)
    Vec3 p = me.move.pos, v = me.move.vel;
    const bool level = anchor.y < p.y + 1.0f;
    // a long pull strays metres off any line (one across a whole map: into whatever pit is near it)
    if ((anchor.x - p.x) * (anchor.x - p.x) + (anchor.z - p.z) * (anchor.z - p.z) > 22.0f * 22.0f) return false;
    {   // the anchor's near side must be ground too: a hook in the side of a pit drags you into it
        Vec3 back = {p.x - anchor.x, 0, p.z - anchor.z};
        float bl = length(back);
        if (bl > 0.1f && !safeDrop(s, anchor + back * (0.6f / bl) + Vec3(0, 0.4f, 0))) return false;
    }
    const float floorY = std::min(p.y, anchor.y) - 0.3f;   // (the ground under the pull holds it up)
    const float dt = 0.05f;
    for (int k = 0; k < 32; k++) {   // (1.6 s: as long as a bot holds on)
        Vec3 to = anchor - (p + Vec3(0, 1.0f, 0));
        float dist = length(to);
        float hsNow = std::sqrt(v.x * v.x + v.z * v.z);   // (where botThink lets go: a level pull sooner when fast)
        if (dist < 1.2f || length(anchor - p) < (level ? std::max(3.0f, hsNow * 0.3f) : 3.0f)) break;
        float before = length(v);
        v += to / dist * (accel * dt);
        float sp = length(v), cap = std::max(top, before);
        if (sp > cap) v = v * (cap / sp);
        v.y -= fall * dt;
        Vec3 hv = {v.x, 0, v.z};
        float hl = length(hv) * dt;
        Vec3 n;
        if (hl > 1e-3f && s.map_.raycast(p + Vec3(0, 0.6f, 0), hv / (hl / dt), hl + 0.4f, &n) >= 0 && n.y == 0) {
            float into = dot(v, n);   // a wall in the way: slide along it (that's where the pull takes you then)
            if (into < 0) v = v - n * into;
        }
        p += v * dt;
        p.y = std::max(p.y, floorY);
        // (and a metre either side: the real pull strays from this by about that, more on long pulls)
        Vec3 off = hl > 1e-3f ? Vec3(-hv.z, 0, hv.x) * (1.0f / (hl / dt)) : Vec3();
        if (!safeDrop(s, p + Vec3(0, 0.3f, 0)) || (k > 2 && (!safeDrop(s, p + off + Vec3(0, 0.3f, 0)) || !safeDrop(s, p - off + Vec3(0, 0.3f, 0))))) return false;
    }
    // let go here: it flies and skids on, give or take 17 degrees, unless a wall stops it first
    Vec3 d = {v.x, 0, v.z};
    float hs = length(d);
    if (hs < 1.0f) return true;
    d = d / hs;
    Vec3 side = {-d.z, 0, d.x};
    float reach = std::min(12.0f, 2.0f + hs * 0.45f);   // (in the air a moment, then a skid: ~0.45 s worth)
    float wall = s.map_.raycast(p + Vec3(0, 0.5f, 0), d, reach);
    float over = wall >= 0 ? wall : reach;
    for (float k = 1.0f; k <= over; k += 1.0f) {
        Vec3 q = p + d * k + Vec3(0, 0.3f, 0);
        if (!safeDrop(s, q) || !safeDrop(s, q + side * (0.3f * k)) || !safeDrop(s, q - side * (0.3f * k))) return false;
    }
    return true;
}

// ---- a loaded brain's interpreted slots (null when every slot is native: the built-in brain, or one that only
// retunes numbers). The common sensors are filled once per think; each call site fills its own and runs the slot.
struct Interp {
    const Brain* br = nullptr;
    int level = 1;
    float dt = 0;
    BrainCall c;
    bool native(int slot) const { return br->native[slot]; }
    const double* run(int slot) { runBrainSlot(*br, slot, level, dt, c); return c.out; }
};
#define NATIVE(slot) (!ip || ip->native(slot))

// How many of the bot's teammates have a lower slot id: defenders deal out the bomb sites by it (by slot id they
// could all draw the same one: in a vs bots game every defender bot had an odd id - F10)
int teamRank(const Server& s, int id) {
    int r = 0;
    for (int i = 0; i < id; i++) r += s.players_[i].connected && s.players_[i].team == s.players_[id].team;
    return r;
}

// The objective slots' sensors (and urgent's)
void objectiveSensors(const Server& s, int id, BrainCall& c) {
    const ModeState& m = s.ms_;
    const ServerPlayer& me = s.players_[id];
    const Map& map = s.map_;
    double* in = c.in;
    in[S_CHECKPOINTSLEFT] = me.checkpoint < (int)map.checkpoints.size();
    in[S_HASHILL] = map.hasHill;
    in[S_HASCART] = map.payloadPath.size() >= 2;
    in[S_POINTACTIVE] = m.point < map.points.size();
    in[S_BOMBPLANTED] = m.bombState == BOMB_PLANTED;
    in[S_BOMBDROPPED] = m.bombState == BOMB_DROPPED;
    in[S_BOMBCARRIED] = m.bombState == BOMB_CARRIED;
    in[S_ATTACKER] = me.team == m.attackers;
    in[S_CARRIERISME] = m.bombCarrier == id;
    in[S_CARRIERALIVE] = m.bombState == BOMB_CARRIED && m.bombCarrier >= 0 && m.bombCarrier < kMaxPlayers &&
                         s.players_[m.bombCarrier].connected && s.players_[m.bombCarrier].alive;
    in[S_NAVREADY] = !s.nav_.nodes.empty() && s.nav_.mapId == map.id();
    in[S_IT] = me.it;
    in[S_SITECOUNT] = (double)map.bombSites.size();
    in[S_TEAMRANK] = teamRank(s, id);
}

// ---- perception: look (view cone + line of sight) and listen, at 20 Hz of game time
void perceive(Server& s, int id, BotBrain& b, const BrainParams& sp, const Vec3& eye, double now, Interp* ip) {
    const float scale = s.bodyScale();
    for (int i = 0; i < kMaxPlayers; i++) {
        BotAwareness& a = b.aware[i];
        if (!isThreat(s, id, i)) { a.visible = false; continue; }
        const ServerPlayer& o = s.players_[i];
        Vec3 chest = o.move.pos + Vec3(0, sp.chestHeight * scale, 0), head = o.move.pos + Vec3(0, sp.headHeight * scale, 0);
        Vec3 d = chest - eye;
        float dist = length(d);
        bool vis = false;
        if (dist < sp.viewDist) {
            float off = std::fabs(angleDiff(yawOf(d), b.aimYaw));
            float pOff = std::fabs(std::asin(clampf(d.y / std::max(dist, 0.01f), -1, 1)) - b.aimPitch);
            bool inCone = (off <= sp.fov * 0.5f && pOff < sp.vertCone) || dist < sp.feelDist; // (bumping into someone: felt)
            if (inCone) vis = canSee(s, eye, head) || canSee(s, eye, chest);
            if (vis && !a.visible) { // just came into view: the reaction starts now (slower out of the corner of the eye)
                float react;
                if (NATIVE(SLOT_REACT)) {
                    react = sp.reactMs * 0.001f * (1.0f + 0.5f * off / (sp.fov * 0.5f)) * evalBrainCurve(sp.c_reactDist, dist);
                    if (now - a.seenAt < sp.reacquireWindow) react *= sp.reacquire; // lost behind cover a moment ago: expected back
                } else {
                    ip->c.in[S_OFF] = off; ip->c.in[S_DIST] = dist; ip->c.in[S_SINCESEEN] = now - a.seenAt;
                    react = (float)ip->run(SLOT_REACT)[0];
                }
                a.reactedAt = now + react;
            }
        }
        a.visible = vis;
        if (vis) {
            a.seenAt = now;
            a.seenPos = o.move.pos;
            a.seenVel = o.move.vel;
            continue;
        }
        // hearing: gunfire, running footsteps, dashes; the direction (and distance) only roughly
        float hs = std::sqrt(o.move.vel.x * o.move.vel.x + o.move.vel.z * o.move.vel.z);
        bool heard;
        if (NATIVE(SLOT_HEARING)) {
            bool gun = now - o.lastFire < sp.hearGunWindow && dist < sp.hearGun;
            bool steps = (o.move.flags & MF_ONGROUND) && hs > sp.hearStepSpeed && dist < sp.hearSteps;
            bool dash = o.move.dashTimer > 0 && dist < sp.hearSteps * sp.hearDashMult;
            heard = gun || steps || dash;
        } else {
            double* in = ip->c.in;
            in[S_DIST] = dist; in[S_GUNAGE] = now - o.lastFire; in[S_ONGROUND] = (o.move.flags & MF_ONGROUND) != 0;
            in[S_GROUNDSPEED] = hs; in[S_DASHING] = o.move.dashTimer > 0;
            heard = ip->run(SLOT_HEARING)[0] != 0;
        }
        // hurt by it (X11: a sniper beyond hearing went unanswered): known like a sound from where it is, once the
        // reaction time has passed since the hit
        const ServerPlayer& self = s.players_[id];
        if (self.lastHitBy == i && self.lastHitByAt > a.hurtNoticed && now >= self.lastHitByAt + sp.reactMs * 0.001f) {
            a.hurtNoticed = self.lastHitByAt;
            heard = true;
        }
        if (heard) {
            if (now - a.heardAt > sp.soundNewWindow) { // a new sound: a fresh guess at where it came from
                float err = dist * sp.soundErr;
                a.heardPos = o.move.pos + Vec3(gauss(b) * err, 0, gauss(b) * err);
            } else { // the same source again: the guess firms up a little
                a.heardPos = a.heardPos + (o.move.pos - a.heardPos) * sp.soundRefine;
            }
            a.heardAt = now;
        }
    }
}

// Where the bot wants to go when it isn't fighting: the mode's objective, else roam. The objective slots (botbrain.h)
// choose between going to a point and these behaviours.
Vec3 randomNavNode(Server& s, BotBrain& b) {
    const auto& n = s.nav_.nodes[(size_t)(rnd(b) * s.nav_.nodes.size()) % s.nav_.nodes.size()];
    return Vec3((n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f);
}
// no objective: go after what was last seen or heard, else roam between spawn points
Vec3 fallbackGoal(Server& s, int id, BotBrain& b, const BrainParams& sp) {
    const ServerPlayer& me = s.players_[id];
    const Map& map = s.map_;
    const double now = s.time();
    double bestT = -1e9; Vec3 lead;
    for (int i = 0; i < kMaxPlayers; i++) {
        const BotAwareness& a = b.aware[i];
        if (now - a.seenAt >= sp.memory && now - a.heardAt >= sp.huntHeardWindow) continue; // (nothing recent: skip the checks)
        if (!isEnemy(s, id, i)) continue;
        if (now - a.seenAt < sp.memory && a.seenAt > bestT) { bestT = a.seenAt; lead = a.seenPos; }
        else if (now - a.heardAt < sp.huntHeardWindow && a.heardAt > bestT) { bestT = a.heardAt; lead = a.heardPos; }
    }
    if (bestT > -1e9 && length(lead - me.move.pos) > sp.arriveDist) return lead;
    if (map.spawns.empty()) return me.move.pos;
    if (now > b.goalUntil || length(b.goal - me.move.pos) < sp.arriveDist) {
        const auto& spn = map.spawns[(size_t)(rnd(b) * map.spawns.size()) % map.spawns.size()];
        b.goal = spn.pos;
        b.goalUntil = now + sp.roamGoal + rnd(b) * sp.roamGoalRand;
    }
    return b.goal;
}
// (the graph is still the previous map's while this one's builds: roam between spawns until it's ready)
bool navReadyFor(const Server& s) { return !s.nav_.nodes.empty() && s.nav_.mapId == s.map_.id(); }
// seek ("it"): wander everywhere, toward anything heard or the locator's last find (targets are chased by the fight
// code once seen)
Vec3 seekGoal(Server& s, int id, BotBrain& b, const BrainParams& sp) {
    const ServerPlayer& me = s.players_[id];
    const double now = s.time();
    if (now - me.locatedAt < sp.seekLocatorWindow) { b.goal = me.locatedPos; b.goalUntil = now + sp.seekLocatorGoal; }
    for (int i = 0; i < kMaxPlayers; i++)
        if (isEnemy(s, id, i) && now - b.aware[i].heardAt < sp.seekHeardWindow && now - b.aware[i].seenAt > sp.seekUnseen) { b.goal = b.aware[i].heardPos; b.goalUntil = now + sp.seekHeardGoal; }
    if (now > b.goalUntil || length(b.goal - me.move.pos) < sp.arriveDist) { b.goal = randomNavNode(s, b); b.goalUntil = now + sp.seekWanderGoal; }
    return b.goal;
}
// evade (a runner): flee "it" when it's near, else hide (hide and seek) or wander
Vec3 evadeGoal(Server& s, int id, BotBrain& b, const BrainParams& sp) {
    const ModeState& m = s.ms_;
    const ServerPlayer& me = s.players_[id];
    const double now = s.time();
    // only what this runner has seen or heard of "it" (no knowing where it is through walls)
    int it = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (i != id && s.players_[i].connected && s.players_[i].alive && s.players_[i].it) it = i;
    Vec3 itPos;
    bool threat = false;
    if (it >= 0) {
        const BotAwareness& a = b.aware[it];
        float range = m.mode == MODE_TAG ? sp.evadeRangeTag : sp.evadeRangeHide;
        if (now - a.seenAt < sp.evadeFresh && length(a.seenPos - me.move.pos) < range) { threat = true; itPos = a.seenPos; }
        else if (now - a.heardAt < sp.evadeFresh && length(a.heardPos - me.move.pos) < range * sp.evadeHeardMult) { threat = true; itPos = a.heardPos; }
    }
    if (threat && now > b.fleeUntil) { // run: the reachable-looking spot farthest from it
        Vec3 best = b.goal;
        float bestScore = -1e9f;
        for (int k = 0; k < sp.fleeCandidates; k++) {
            Vec3 c = randomNavNode(s, b);
            float sc = length(c - itPos) - sp.fleeNearWeight * length(c - me.move.pos);
            if (sc > bestScore) { bestScore = sc; best = c; }
        }
        b.goal = best; b.goalUntil = now + sp.fleeGoal; b.fleeUntil = now + sp.fleeCommit + (1.0f - sp.fleePredict) * sp.fleeHesitate;
        return b.goal;
    }
    if (m.mode == MODE_HIDE && !threat) { // pick a hiding spot once and stay put
        if (now > b.goalUntil) { b.goal = randomNavNode(s, b); b.goalUntil = now + sp.hideHold; }
        if (length(b.goal - me.move.pos) < sp.hideArrive) return me.move.pos;
        return b.goal;
    }
    if (now > b.goalUntil || length(b.goal - me.move.pos) < sp.arriveDist) { b.goal = randomNavNode(s, b); b.goalUntil = now + sp.evadeWanderGoal; }
    return b.goal;
}
// A point an objective slot named (where the bot stands when the map has no such thing)
Vec3 brainPoint(const Server& s, int id, int pt, int arg) {
    const ModeState& m = s.ms_;
    const ServerPlayer& me = s.players_[id];
    const Map& map = s.map_;
    switch (pt) {
        case PT_CHECKPOINT: if (me.checkpoint >= 0 && me.checkpoint < (int)map.checkpoints.size()) return map.checkpoints[me.checkpoint].center(); break;
        case PT_HILL: if (map.hasHill) return {map.hill.center().x, map.hill.mn.y, map.hill.center().z}; break;
        case PT_CART: if (map.payloadPath.size() >= 2) return map.cartPoint(m.mode, m.attackers, m.payloadDist); break;
        case PT_POINT: if (m.point < map.points.size()) { const AABB& p = map.points[m.point]; return {p.center().x, p.mn.y, p.center().z}; } break;
        case PT_BOMB: if (m.bombState == BOMB_PLANTED || m.bombState == BOMB_DROPPED) return m.bombPos; break;
        case PT_CARRIER:
            if (m.bombState == BOMB_CARRIED && m.bombCarrier >= 0 && m.bombCarrier < kMaxPlayers && s.players_[m.bombCarrier].connected && s.players_[m.bombCarrier].alive)
                return s.players_[m.bombCarrier].move.pos;
            break;
        case PT_SITE:
            if (!map.bombSites.empty()) { int n = (int)map.bombSites.size(); return map.bombSites[((arg % n) + n) % n].center(); }
            break;
        default: break;
    }
    return me.move.pos;
}

Vec3 objective(Server& s, int id, BotBrain& b, const BrainParams& sp, Interp* ip) {
    const ModeState& m = s.ms_;
    const ServerPlayer& me = s.players_[id];
    const Map& map = s.map_;
    if (ip && m.mode >= 0 && m.mode < MODE_COUNT && !ip->native(SLOT_OBJECTIVE + m.mode)) {
        objectiveSensors(s, id, ip->c);
        const double* out = ip->run(ip->br->objSlot[m.mode]);
        switch ((int)out[0]) {
            case ACT_GOTO: return brainPoint(s, id, (int)out[1], (int)out[2]);
            case ACT_SEEK: if (navReadyFor(s)) return seekGoal(s, id, b, sp); break;
            case ACT_EVADE: if (navReadyFor(s)) return evadeGoal(s, id, b, sp); break;
            default: break;
        }
        return fallbackGoal(s, id, b, sp);
    }
    switch (m.mode) {
        case MODE_RACE:
            if (me.checkpoint < (int)map.checkpoints.size()) return map.checkpoints[me.checkpoint].center();
            break;
        case MODE_KOTH:
            if (map.hasHill) return {map.hill.center().x, map.hill.mn.y, map.hill.center().z}; // the hill's floor
            break;
        case MODE_PAYLOAD:
        case MODE_NPAYLOAD:
            if (map.payloadPath.size() >= 2) return map.cartPoint(m.mode, m.attackers, m.payloadDist);
            break;
        case MODE_POINTS:   // both sides go to the point under attack (its floor)
            if (m.point < map.points.size()) { const AABB& p = map.points[m.point]; return {p.center().x, p.mn.y, p.center().z}; }
            break;
        case MODE_BOMB: {
            bool att = me.team == m.attackers;
            if (m.bombState == BOMB_PLANTED) return m.bombPos;
            if (m.bombState == BOMB_DROPPED && att) return m.bombPos;
            if (!map.bombSites.empty()) {
                // carrier (and attackers) head for the nearest site; defenders guard a site each, dealt out by their
                // rank in the team (by slot id they could all draw the same site: every defender bot had odd ids - F10)
                const AABB& site = map.bombSites[(att ? 0 : teamRank(s, id)) % map.bombSites.size()];
                if (att && m.bombState == BOMB_CARRIED && m.bombCarrier != id) {
                    const auto& c = s.players_[m.bombCarrier];
                    if (c.connected && c.alive) return c.move.pos; // escort (a teammate: shown on the HUD)
                }
                return site.center();
            }
            break;
        }
        case MODE_HIDE:
        case MODE_TAG:
            if (!navReadyFor(s)) break;
            return me.it ? seekGoal(s, id, b, sp) : evadeGoal(s, id, b, sp);
        default: break;
    }
    return fallbackGoal(s, id, b, sp);
}

// Best weapon for the range that this mode allows (1v1 rulesets / duel lives restrict the pool).
// Beginners stick to the rifle when they can; levels that can't lead well prefer hitscan.
int pickWeapon(const Server& s, const BrainParams& sp, float dist) {
    const uint16_t mask = s.weaponMask();   // (once: weaponAllowed works it out on every call)
    auto allowed = [mask](int w) { return !mask || (mask >> w & 1); };
    if (!sp.weapons && allowed(WPN_RIFLE)) return WPN_RIFLE;
    const bool lead = sp.canLead;
    const BrainList& order = dist < sp.weaponClose ? sp.l_close : dist < sp.weaponMid ? (lead ? sp.l_mid : sp.l_midNoLead)
                           : dist < sp.weaponFar ? (lead ? sp.l_far : sp.l_farNoLead) : (lead ? sp.l_vfar : sp.l_farNoLead);
    for (int k = 0; k < order.n; k++) if (allowed(order.w[k])) return order.w[k];
    return WPN_RIFLE;
}

// Repath budget: route searches per server tick across all bots (the rest wait a tick: no spikes).
constexpr int kPathsPerTick = 2;
thread_local uint32_t g_pathTick = ~0u;   // (per thread: a process can host several servers)
thread_local int g_pathsThisTick = 0;

// botThink's per-tick constants (exp, sqrt, lround of the tick length and the skill's parameters), worked out once
// per tick rate and skill instead of on every tick. Exactly the expressions botThink used inline, on the same float
// values: the results are bit-identical (no -ffast-math, -ffp-contract=off: CMakeLists.txt).
const BotBrain::TickConsts& tickConsts(BotBrain& b, int skill, const BrainParams& sp, float dt, int rate) {
    BotBrain::TickConsts& c = b.tc;
    if (c.dt != dt || c.rate != rate || c.skill != skill || c.params != &sp) {
        c.dt = dt; c.rate = rate; c.skill = skill; c.params = &sp;
        c.velK = 1.0f - std::exp(-dt / sp.velTau);
        float a = std::exp(-dt / sp.noiseTau);
        c.noiseA = a;
        c.noiseKK = std::sqrt(std::max(0.0f, 1.0f - a * a));
        c.settle = std::exp(-dt / sp.settleTau);
        auto p = [dt](float r) { return 1.0f - std::exp(-r * dt); };
        c.pJump = p(sp.jumpRate); c.pDash = p(sp.dashRate); c.pRunDash = p(sp.runDash); c.pPulledDash = p(sp.pulledDashRate);
        c.seenTicks = seenAgoTicks(sp.visDelayMs, rate);
        c.olderTicks = seenAgoTicks(sp.visDelayMs + 16.0f, rate);
        c.turnSteps = std::max(1, (int)std::lround(dt * 1000.0f));
        c.turnH = dt / c.turnSteps;
        c.turnDv = sp.turnAccel * c.turnH;
    }
    return c;
}

} // namespace

bool setBotBrain(Server& s, int id, std::shared_ptr<const Brain> brain) {
    if (id < 0 || id >= kMaxPlayers || !s.players_[id].connected || !s.players_[id].isBot) return false;
    if (brain && (s.cfg_.ranked || s.cfg_.timeTrial)) return false;   // (bots there play the built-in brain: they aren't there at all today)
    s.bots_[id].brain = brain && !brain->builtIn ? std::move(brain) : nullptr;
    s.bots_[id].tc.dt = -1;
    return true;
}

const char* botSkillName(int skill) {
    static const char* n[BOT_SKILLS] = {"BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"};
    return n[skill < 0 || skill >= BOT_SKILLS ? 1 : skill];
}

UserCmd (*g_botThinkHook)(Server&, int) = nullptr;
uint32_t& botPathTick() { return g_pathTick; }
int& botPathsThisTick() { return g_pathsThisTick; }
UserCmd botThink(Server& s, int id) { return g_botThinkHook ? g_botThinkHook(s, id) : botThinkDirect(s, id); }

UserCmd botThinkDirect(Server& s, int id) {
    ServerPlayer& me = s.players_[id];
    BotBrain& b = s.bots_[id];
    const int skill = b.skill < 0 || b.skill >= BOT_SKILLS ? 1 : b.skill;
    const Brain* br = b.brain.get();   // (null: the built-in brain, all native)
    const BrainParams& sp = br ? br->lvl[skill] : defaultBrainParams(skill);
    const double now = s.time(); const float dt = s.dt();
    if (b.script) return b.script(s, id);
    UserCmd cmd;
    cmd.weapon = kNoWeaponChange;
    bool dashJump = false;   // this tick's dash is meant as a dash jump (experts on long straight routes)
    bool navOk = s.nav_.ready(s.map_);
    // a goal from the previous map, or picked before this map's graph was ready, is picked again
    if (b.goalMapId != s.map_.id() || (navOk && b.goalNavGen != s.nav_.generation)) {
        b.goalMapId = s.map_.id();
        if (navOk) b.goalNavGen = s.nav_.generation;
        b.goalUntil = 0; b.fleeUntil = 0;
        b.goal = me.move.pos;   // (not the old spot, nor a fresh brain's 0,0,0 - a spot on the last map's graph - while a lead is chased instead)
    }
    if (!me.alive) {
        b.target = -1;
        b.engagedAt = -1;
        for (auto& a : b.aware) { a.visible = false; a.seenAt = -1e9; a.heardAt = -1e9; }
        b.path.clear(); b.needPath = true;
        cmd.yaw = b.aimYaw;
        return cmd;
    }
    Vec3 eye = eyeOf(me, s.bodyScale());
    const BotBrain::TickConsts& tc = tickConsts(b, skill, sp, dt, s.tickRate());
    // a brain with interpreted slots: their common sensors, the dice, the chance() thresholds (once per tick length)
    Interp interp;
    Interp* ip = nullptr;
    if (br && !br->allNative) {
        ip = &interp;
        interp.br = br; interp.level = skill; interp.dt = dt;
        BrainCall& c = interp.c;
        c.in[S_NOW] = now; c.in[S_DT] = dt; c.in[S_MODE] = s.ms_.mode; c.in[S_LEVEL] = skill; c.in[S_MYID] = id;
        c.in[S_HEALTH] = me.health; c.in[S_ITMODE] = isItMode(s.ms_.mode);
        c.rng = &b.rng;
        c.weaponMask = s.weaponMask();
        BrainChanceCache& cc = b.chance;
        if (cc.brain != br || cc.level != skill || cc.dt != dt) {
            cc.brain = br; cc.level = skill; cc.dt = dt;
            for (size_t k = 0; k < br->chanceParams.size(); k++) cc.p[k] = 1.0f - std::exp(-(float)br->P[skill][br->chanceParams[k]] * dt);
        }
        c.chanceP = cc.p;
    }

    // --- perception (20 Hz of game time, each bot at its own phase)
    bool damageOn = (s.ms_.phase == PHASE_LIVE || s.ms_.phase == PHASE_WARMUP) && !(s.ms_.mode == MODE_RACE && s.ms_.phase != PHASE_WARMUP);
    if (b.nextLook <= 0) b.nextLook = now + (id % 10) * (sp.lookEvery / 10); // spread the bots' looks over the 50 ms
    if (now >= b.nextLook) {
        b.nextLook = std::max(b.nextLook + sp.lookEvery, now + sp.lookEvery * 0.5);
        { QZONE("bot perceive"); perceive(s, id, b, sp, eye, now, ip); }
        // Edges: the void or lava within a stumble, a knockback or a hop (maps that have them). Eight directions at
        // 1.5 and 3 m (near: no jumping about), and 5 m (a fight drifts away from it: a laser or a blast pushes you
        // that far); the nearer one weighs more in which way is away.
        b.nearEdge = b.edgeFar = false;
        if (s.map_.hasVoid || !s.map_.lava.empty()) {
            Vec3 away;
            for (int k = 0; k < 8; k++) {
                Vec3 dir = {std::cos(k * PI / 4), 0, std::sin(k * PI / 4)};
                if (!safeDrop(s, me.move.pos + dir * 1.5f + Vec3(0, 0.5f, 0))) { away = away - dir * 2.0f; b.nearEdge = true; }
                else if (!safeDrop(s, me.move.pos + dir * 3.0f + Vec3(0, 0.5f, 0))) { away = away - dir; b.nearEdge = true; }
                else if (!safeDrop(s, me.move.pos + dir * 5.0f + Vec3(0, 0.5f, 0))) { away = away - dir * 0.5f; b.edgeFar = true; }
            }
            float l = length(away);
            b.edgeAway = l > 1e-3f ? away / l : Vec3();
        }
        // target: someone in view (nearest to the crosshair and closest, the current one preferred), else the
        // last one seen while it's still remembered (hunted)
        int best = -1;
        float bestScore = 1e9f;
        for (int i = 0; i < kMaxPlayers && damageOn; i++) {
            if (!b.aware[i].visible || !isEnemy(s, id, i)) continue;
            Vec3 d = s.players_[i].move.pos + Vec3(0, sp.targetAimHeight, 0) - eye;
            float sc;
            if (NATIVE(SLOT_TARGET_SCORE)) sc = length(d) + std::fabs(angleDiff(yawOf(d), b.aimYaw)) * sp.targetAngleWeight - (i == b.target ? sp.targetKeepBonus : 0.0f);
            else {
                ip->c.in[S_DIST] = length(d); ip->c.in[S_ANGLEOFF] = std::fabs(angleDiff(yawOf(d), b.aimYaw)); ip->c.in[S_CURRENT] = i == b.target;
                sc = (float)ip->run(SLOT_TARGET_SCORE)[0];
            }
            if (sc < bestScore) { bestScore = sc; best = i; }
        }
        if (best < 0 && b.target >= 0 && damageOn && isEnemy(s, id, b.target) && now - b.aware[b.target].seenAt < sp.memory) best = b.target;
        if (best != b.target) {
            b.target = best;
            b.engagedAt = -1;
            b.rejudgeAt = 0;
            if (best >= 0) b.velEst = b.aware[best].seenVel * sp.initVelEst;
        }
    }
    if (b.target >= 0 && !isEnemy(s, id, b.target)) { b.target = -1; b.engagedAt = -1; }

    // Objective movement (nav route to the mode's goal), also used while fighting when the
    // objective can't wait: carrying the bomb, or defusing a planted one.
    auto objectiveMove = [&](Vec3& dirOut, uint16_t& buttons, const Vec3* override) {
        Vec3 goal = override ? *override : objective(s, id, b, sp, ip);
        if (navOk && b.pathGen != s.nav_.generation) { b.path.clear(); b.pathPrev = -1; b.needPath = true; b.pathGen = s.nav_.generation; }
        Vec3 toWp = b.waypoint - me.move.pos;
        bool steerNow = now >= b.nextSteer || b.needPath || toWp.x * toWp.x + toWp.z * toWp.z < 1.0f || length(goal - b.pathGoal) > 1.0f;
        if (navOk && steerNow) { // (route progress and the next waypoint: every 20 ms of game time, or when needed)
            b.nextSteer = now + sp.steerEvery;
            int cur = s.nav_.nodeNear(me.move.pos + Vec3(0, 0.1f, 0), NavGraph::kStandNear);
            // follow the kept route: drop the surfaces already reached; off it (fell, pushed): find a new one
            if (cur >= 0 && cur != b.pathPrev) {
                auto it = std::find(b.path.begin(), b.path.end(), cur);
                if (it != b.path.end()) { b.path.erase(b.path.begin(), it + 1); b.pathPrev = cur; }
                else if (!b.path.empty() && std::any_of(s.nav_.links[cur].begin(), s.nav_.links[cur].end(), [&](const std::pair<int, float>& l) { return l.first == b.path.front(); }))
                    b.pathPrev = cur;   // (cut across a neighbouring piece of the floor: on from here, no new search)
                else b.needPath = true;
            }
            if (length(goal - b.pathGoal) > 1.0f) {
                int gn = s.nav_.nodeNear(goal + Vec3(0, 0.5f, 0), NavGraph::kGoalNear);
                if (gn != b.pathGoalNode) b.needPath = true;
                b.pathGoal = goal;
            }
            if (now >= b.repathAt) b.needPath = true;
            if (b.needPath && cur >= 0) {
                if (g_pathTick != s.tick_) { g_pathTick = s.tick_; g_pathsThisTick = 0; }
                if (g_pathsThisTick < kPathsPerTick) {
                    g_pathsThisTick++;
                    QZONE("bot nav path");
                    b.pathGoalNode = s.nav_.nodeNear(goal + Vec3(0, 0.5f, 0), NavGraph::kGoalNear);
                    b.pathGoal = goal;
                    if (!s.nav_.findPath(cur, b.pathGoalNode, b.path)) b.path.clear();
                    b.pathPrev = cur;
                    b.needPath = false;
                    b.repathAt = now + sp.repathEvery + rnd(b) * sp.repathRand; // (a moving goal re-plans when its surface changes)
                }
            }
            b.waypoint = b.path.empty() || b.pathPrev < 0 ? goal : s.nav_.steer(me.move.pos, b.pathPrev, b.path, goal);
        } else if (!navOk) {
            b.waypoint = goal; // (the graph is still building: head straight for it)
            b.pathGoal = goal;
        }
        Vec3 to = b.waypoint - me.move.pos;
        to.y = 0;
        float d = length(to);
        dirOut = d > 0.6f ? to / d : Vec3();
        if (d > 0.6f && now < b.unstickUntil && b.target < 0) dirOut += Vec3(-dirOut.z, 0, dirOut.x) * b.strafeDir; // unsticking
        const ModeState& m = s.ms_;
        const AABB hull = playerBox(me.move);   // (the server's own zone test: size modifiers and crouching included)
        if (m.mode == MODE_BOMB) {
            bool inSite = false;
            for (auto& site : s.map_.bombSites) inSite |= hull.overlaps(site);
            if (m.bombState == BOMB_CARRIED && m.bombCarrier == id && inSite) { buttons |= BTN_USE; dirOut = {}; }
            if (m.bombState == BOMB_PLANTED && me.team != m.attackers && length(m.bombPos - me.move.pos) < sp.defuseDist) { buttons |= BTN_USE; dirOut = {}; }
        }
        if (m.mode == MODE_KOTH && s.map_.hasHill && hull.overlaps(s.map_.hill)) dirOut = dirOut * sp.objectiveSlow;
        if (m.mode == MODE_POINTS && m.point < s.map_.points.size() && hull.overlaps(s.map_.points[m.point])) dirOut = dirOut * sp.objectiveSlow;
    };
    const ModeState& ms = s.ms_;
    bool urgent;
    if (NATIVE(SLOT_URGENT)) urgent = ms.mode == MODE_BOMB && ((ms.bombState == BOMB_CARRIED && ms.bombCarrier == id) ||
                                                               (ms.bombState == BOMB_PLANTED && me.team != ms.attackers));
    else { objectiveSensors(s, id, ip->c); urgent = ip->run(SLOT_URGENT)[0] != 0; }

    float wantYaw = b.aimYaw, wantPitch = 0;
    float ffYaw = 0;             // feed-forward turn speed (tracking a target crossing the view)
    float gain = 20.0f;          // correction gain (aim: the level's; just looking around: relaxed)
    Vec3 moveDir;                // world-space desired movement (xz)
    // Hooked on a grapple (fighting or not: letting go mid-pull flings you off at full speed wherever you are).
    auto holdHook = [&] {
        const Vec3& at = me.move.hookPoint;
        cmd.buttons |= BTN_GRAPPLE;
        // caught: the pull checked again from here, as we move now (it was planned before the aim got there, and
        // the catch may not be quite where it was aimed); not safe: let go at once, while still slow
        if (!b.hookChecked) {
            b.hookChecked = true;
            b.hookUntil = now + 1.6;   // (the 1.6 s of pull the check simulates count from the catch, not from the plan)
            // (latched onto a player: both are pulled to the middle, so that's where the pull ends)
            Vec3 ends = me.move.hookTarget ? (me.move.pos + Vec3(0, 1.0f, 0) + at) * 0.5f : at;
            if (!safeHook(s, me, ends)) { b.hookUntil = 0; cmd.buttons &= ~BTN_GRAPPLE; return; }
        }
        // keys along the pull: air control would bend the path off it
        Vec3 along = {at.x - me.move.pos.x, 0, at.z - me.move.pos.z};
        if (length(along) > 0.5f) moveDir = normalize(along);
        // arrived: let go (a level pull at speed sooner, on maps with pits: the momentum carries on past it)
        float hsNow = std::sqrt(me.move.vel.x * me.move.vel.x + me.move.vel.z * me.move.vel.z);
        float letGo = (s.map_.hasVoid || !s.map_.lava.empty()) && at.y < me.move.pos.y + 1.0f ? std::max(3.0f, hsNow * 0.3f) : 3.0f;
        if (length(at - me.move.pos) < letGo) { b.hookUntil = 0; cmd.buttons &= ~BTN_GRAPPLE; }
    };
    bool fighting = false;
    const BotAwareness* ta = b.target >= 0 ? &b.aware[b.target] : nullptr;
    bool reacted = ta && ta->visible && now >= ta->reactedAt;
    if (b.target >= 0 && (reacted || !ta->visible)) {
        // ---- fight: aim at the target (seen), or where it went (remembered)
        fighting = true;
        const ServerPlayer& t = s.players_[b.target];
        if (now >= b.rejudgeAt) { // misjudgements, rolled per engagement and every second or so
            if (NATIVE(SLOT_REJUDGE)) {
                b.leadFactor = std::max(0.0f, sp.leadMean + gauss(b) * sp.leadSd);
                b.dropFactor = std::max(0.3f, 1.0f + gauss(b) * sp.leadSd * 0.7f);
                b.speedJudge = std::max(0.3f, 1.0f + gauss(b) * sp.speedJudge);
                b.rejudgeAt = now + 0.7 + rnd(b) * 0.8;
            } else {
                const double* o = ip->run(SLOT_REJUDGE);
                b.leadFactor = (float)o[0]; b.dropFactor = (float)o[1]; b.speedJudge = (float)o[2]; b.rejudgeAt = o[3];
            }
        }
        Vec3 perceived;
        if (ta->visible) {
            perceived = seenAgo(s, t, sp.visDelayMs, tc.seenTicks);
            Vec3 older = seenAgo(s, t, sp.visDelayMs + 16.0f, tc.olderTicks);
            Vec3 vObs = (perceived - older) / 0.016f;
            if (length(vObs) < 40.0f) { // (not a respawn / teleport)
                b.velEst += (vObs * b.speedJudge - b.velEst) * tc.velK;
            }
            perceived += b.velEst * (sp.visDelayMs * 0.001f * sp.predict);
        } else { // behind cover: where it should be by now if it kept going
            perceived = ta->seenPos + ta->seenVel * (float)std::min(now - ta->seenAt, sp.huntPredictCap) * sp.predict;
        }
        Vec3 aimAt = perceived + Vec3(0, sp.aimHeight * s.bodyScale(), 0);
        float dist = length(aimAt - eye);
        int w = b.forceWeapon;
        if (w < 0) {
            if (NATIVE(SLOT_PICK_WEAPON)) w = pickWeapon(s, sp, dist);
            else {
                ip->c.in[S_DIST] = dist;
                w = (int)ip->run(SLOT_PICK_WEAPON)[0];
                if (w < 0 || w >= WPN_COUNT) w = WPN_RIFLE;
            }
        }
        if (now < me.backAt(w) && me.weapon != w) w = me.weapon; // thrown it (knife / syringe): use what we switched to
        const WeaponDef& wd = weaponDef(w);
        bool throwKnife;   // out of reach: throw it
        if (NATIVE(SLOT_THROW_KNIFE)) throwKnife = w == WPN_KNIFE && dist > wd.range + 0.5f && dist < sp.knifeThrowMax && sp.canLead && !isItMode(s.ms_.mode);
        else { ip->c.in[S_WEAPON] = w; ip->c.in[S_DIST] = dist; ip->c.in[S_WEAPONRANGE] = wd.range; throwKnife = ip->run(SLOT_THROW_KNIFE)[0] != 0; }
        if (throwKnife) {
            float tf = dist / weaponAlt().knifeThrowSpeed;
            aimAt += b.velEst * (tf * b.leadFactor);
            aimAt.y += 0.5f * weaponAlt().knifeThrowGravity * tf * tf * b.dropFactor;
        }
        if (wd.projectile) {
            float tf = dist / wd.projSpeed;
            aimAt += b.velEst * (tf * b.leadFactor);
            if (w == WPN_ROCKET && sp.canLead) aimAt.y = perceived.y + sp.rocketFeetHeight; // splash at the feet
            if (wd.gravity > 0) aimAt.y += 0.5f * wd.gravity * tf * tf * b.dropFactor;
        }
        Vec3 d = normalize(aimAt - eye);
        float tgtYaw = yawOf(d), tgtPitch = std::asin(clampf(d.y, -1, 1));
        // how fast the target crosses the view (perceived): tracking error grows with it; the hand follows it
        Vec3 rel = b.velEst - me.move.vel;
        float h2 = d.x * d.x + d.z * d.z;   // (d/dt of yawOf: (dz vx - dx vz) / horizontal distance^2)
        float angSpeed = h2 > 1e-4f ? (d.z * rel.x - d.x * rel.z) / (h2 * std::max(dist, 1.0f)) : 0.0f; // rad/s of yaw
        // new engagement: the flick lands off by a fraction of its size (overshooting more often than not)
        if (b.engagedAt < 0 && ta->visible) {
            b.engagedAt = now;
            float fy = angleDiff(tgtYaw, b.aimYaw), fp = tgtPitch - b.aimPitch;
            float amp = std::sqrt(fy * fy + fp * fp);
            if (amp > sp.flickMin) {
                float along = sp.overshoot + gauss(b) * sp.flickErr, across = gauss(b) * sp.flickErr * sp.flickAcross;
                b.flickYaw = (fy * along - fp * across);
                b.flickPitch = (fp * along + fy * across) * sp.flickPitch;
            }
        }
        // tracking error: a random process in game time (exact for any tick length)
        float sigma = sp.trackBase + sp.trackPerRad * std::fabs(angSpeed);
        const float a = tc.noiseA, kk = tc.noiseKK;
        b.noiseYaw = b.noiseYaw * a + sigma * kk * gauss(b);
        b.noisePitch = b.noisePitch * a + sigma * sp.trackPitch * kk * gauss(b);
        const float settle = tc.settle;
        b.flickYaw *= settle; b.flickPitch *= settle;
        wantYaw = tgtYaw + b.noiseYaw + b.flickYaw;
        wantPitch = tgtPitch + b.noisePitch + b.flickPitch;
        ffYaw = angSpeed * sp.predict;
        gain = sp.trackGain;
        if (me.weapon != w) cmd.weapon = (uint8_t)w;
        if (ta->visible) {
            // click once the crosshair looks on the target (where the bot sees it, not where it really is)
            float err = std::fabs(angleDiff(b.aimYaw, tgtYaw)) + std::fabs(b.aimPitch - tgtPitch);
            bool ready = err < sp.fireCone + 0.35f / std::max(dist, 1.0f);
            bool knifeOut = w == WPN_KNIFE && !throwKnife && dist > wd.range + 0.3f; // closing in to stab: don't swing at air
            // a blast weapon with a wall (or the floor) right in front: the splash would be ours (a self-kill)
            const WeaponDef& held = weaponDef(me.weapon);
            auto ownBlast = [&] { return held.splashRadius > 0 && !held.sticky && !(s.arenaRules() & AR_NO_SELF_DAMAGE) &&
                                         s.map_.raycast(eye, angleForward(b.aimYaw, b.aimPitch), held.splashRadius * 0.8f) >= 0; };
            if (NATIVE(SLOT_FIRE)) {
                if (ready && !knifeOut && !ownBlast()) cmd.buttons |= throwKnife ? BTN_ALTFIRE : BTN_FIRE;
                if (w == WPN_SNIPER) cmd.buttons |= BTN_ALTFIRE;
                if (w == WPN_RIFLE && dist > sp.rifleScopeDist && sp.canLead) cmd.buttons |= BTN_ALTFIRE; // scope in for long shots
            } else {
                double* in = ip->c.in;
                in[S_ERR] = err; in[S_DIST] = dist; in[S_WEAPON] = w; in[S_THROWKNIFE] = throwKnife; in[S_WEAPONRANGE] = wd.range;
                ip->c.user = &ownBlast;
                ip->c.lazy = [](void* u, int) { return (*static_cast<decltype(ownBlast)*>(u))(); };
                const double* o = ip->run(SLOT_FIRE);
                ip->c.lazy = nullptr; ip->c.user = nullptr;
                if (o[0] != 0) cmd.buttons |= BTN_FIRE;
                if (o[1] != 0) cmd.buttons |= BTN_ALTFIRE;
            }
            if (me.weapon == WPN_STICKY) { // set off our stickies when the target (as seen) is in the blast, or one is stuck on an enemy
                for (const auto& pr : s.projectiles_)
                    if (pr.active && pr.owner == id && pr.type == WPN_STICKY &&
                        (length(pr.pos - (perceived + Vec3(0, 0.9f, 0))) < weaponDef(WPN_STICKY).splashRadius * 0.7f || (pr.onPlayer >= 0 && isEnemy(s, id, pr.onPlayer))) &&
                        length(pr.pos - (me.move.pos + Vec3(0, 0.9f, 0))) > weaponDef(WPN_STICKY).splashRadius) {   // (and we're clear of it)
                        cmd.buttons |= BTN_ALTFIRE;
                        break;
                    }
            }
            // semi-auto: let go between shots (a click is buffered until the weapon is ready)
            if (weaponDef(me.weapon).semiAuto && (me.buttons & BTN_FIRE)) cmd.buttons &= ~BTN_FIRE;
            // switching weapons: let go first (a button held through a switch is locked until it's released)
            if (me.weapon != w || me.fireLocked) cmd.buttons &= ~(BTN_FIRE | BTN_ALTFIRE);
            // advanced and up, a close-range weapon out: now and then hook the target to drag the fight in (the hook
            // latches onto an enemy it hits and pulls the two together). Held by holdHook below once it catches.
            const bool closeWeapon = (sp.l_hookWeapons.mask >> w & 1) != 0;
            const float hookRange = kGrappleRange * moveValue(me.move, MV_HOOK_RANGE, &s.map_);
            const bool onTarget = err < 0.35f / std::max(dist, 1.0f);   // (the hook needs the body under the crosshair)
            if (now < b.fightHookUntil) {
                if (me.move.flags & MF_HOOKED) {   // caught: them (held by holdHook below), or the map behind them (let go)
                    b.fightHookUntil = 0;
                    if (me.move.hookTarget) { b.hookUntil = now + 1.6; b.hookChecked = false; b.hookAnchor = me.move.hookPoint; }
                    else b.hookUntil = 0;
                } else if (onTarget && !(me.move.flags & MF_HOOKHELD)) {
                    cmd.buttons |= BTN_GRAPPLE;   // (fires once the aim is on them)
                }
            } else if (moveTuning().grapple && !(me.move.mods & MOVE_RUN_ONLY) && s.ms_.mode != MODE_RACE &&
                !(me.move.flags & (MF_HOOKED | MF_HOOKHELD)) && me.move.hookCooldown <= 0 && now >= b.nextHookTry) {
                bool tryIt, go = false;
                double retry = 0, hold = 0;
                if (NATIVE(SLOT_FIGHT_HOOK)) {
                    tryIt = sp.grapple && closeWeapon && dist > sp.fightHookMin && dist < hookRange * 0.8f;
                    if (tryIt) { retry = now + sp.fightHookRetry; go = rnd(b) < sp.fightHookChance; hold = now + sp.fightHookHold; }
                } else {
                    ip->c.in[S_WEAPON] = w; ip->c.in[S_DIST] = dist; ip->c.in[S_HOOKRANGE] = hookRange;
                    const double* o = ip->run(SLOT_FIGHT_HOOK);
                    tryIt = o[0] != 0; retry = o[1]; go = o[2] != 0; hold = o[3];
                }
                if (tryIt) {
                    b.nextHookTry = retry;
                    // (both are pulled to the middle: that's where the pull ends, and it must be safe ground)
                    if (go && safeHook(s, me, (me.move.pos + perceived) * 0.5f + Vec3(0, 1.0f, 0))) {
                        b.fightHookUntil = hold;
                        if (onTarget) cmd.buttons |= BTN_GRAPPLE;
                    }
                }
            }
        }
        // fight movement: keep a good range for the weapon, dodge
        Vec3 to = perceived - me.move.pos;
        to.y = 0;
        Vec3 toN = normalize(to);
        float ideal;
        if (NATIVE(SLOT_IDEAL_RANGE)) ideal = sp.t_idealRange[w < 0 || w >= 16 ? 0 : w];
        else { ip->c.in[S_WEAPON] = w; ideal = (float)ip->run(SLOT_IDEAL_RANGE)[0]; }
        if (ta->visible) {
            moveDir = toN * (dist > ideal + sp.rangeBand ? 1.0f : dist < ideal - sp.rangeBand ? -1.0f : 0.0f);
            // decisions at a fixed game-time rate (not per tick): strafe or plant your feet, change direction
            if (now >= b.strafeSwitchAt) {
                if (NATIVE(SLOT_STRAFE)) {
                    b.strafeDir = rnd(b) < sp.strafe ? (rnd(b) < 0.5f ? -1.0f : 1.0f) : 0.0f;
                    b.strafeSwitchAt = now + (0.15 + rnd(b) * 2.0 / sp.jukeRate);
                } else {
                    const double* o = ip->run(SLOT_STRAFE);
                    b.strafeDir = (float)o[0]; b.strafeSwitchAt = o[1];
                }
            }
            Vec3 side = Vec3(-toN.z, 0, toN.x) * b.strafeDir;
            if (b.nearEdge || b.edgeFar) { // fighting by a drop: dodge along it or away, never toward it, and step back from it
                if (dot(side, b.edgeAway) < sp.edgeFlipDot) { b.strafeDir = -b.strafeDir; side = side * -1.0f; }
                moveDir += b.edgeAway * (b.nearEdge ? sp.edgePushNear : sp.edgePushFar);
            }
            moveDir += side;
            // held by the target's grapple and wanting range (no close-range weapon): advanced and up dash out of it
            auto pulledDash = [&] {
                if (NATIVE(SLOT_PULLED_DASH)) return sp.grapple && ideal > sp.pulledDashMinRange && chance(b, sp.pulledDashRate, tc.pPulledDash);
                ip->c.in[S_IDEAL] = ideal;
                return ip->run(SLOT_PULLED_DASH)[0] != 0;
            };
            if ((me.move.flags & MF_PULLED) && dashReady(me.move) && !b.nearEdge && !b.edgeFar &&
                pulledDash()) {
                Vec3 out = Vec3(-toN.z, 0, toN.x) * (b.strafeDir != 0 ? b.strafeDir : 1.0f);
                if (groundAlong(s, me.move.pos, out, 10.0f)) { moveDir = out; cmd.buttons |= BTN_DASH; }
            }
            // (no jumps or dashes within 5 m of a drop: a hit in the air there, a blast or a laser push, is a fall)
            if ((me.move.flags & MF_ONGROUND) && !b.nearEdge && !b.edgeFar) {
                if (NATIVE(SLOT_FIGHT_JUMP) ? chance(b, sp.jumpRate, tc.pJump) : ip->run(SLOT_FIGHT_JUMP)[0] != 0) cmd.buttons |= BTN_JUMP;
                bool dash = false, closeIn = false;
                if (dashReady(me.move)) {
                    if (NATIVE(SLOT_FIGHT_DASH)) { dash = chance(b, sp.dashRate, tc.pDash); closeIn = dash && dist > sp.dashCloseDist; }
                    else { ip->c.in[S_DIST] = dist; const double* o = ip->run(SLOT_FIGHT_DASH); dash = o[0] != 0; closeIn = o[1] != 0; }
                }
                if (dash) {
                    if (closeIn) moveDir = toN;                                  // close in
                    else moveDir = Vec3(-toN.z, 0, toN.x) * (b.strafeDir != 0 ? b.strafeDir : 1.0f); // dodge sideways
                    if (groundAlong(s, me.move.pos, normalize(moveDir), 10.0f)) cmd.buttons |= BTN_DASH;
                }
            }
        } else if (!urgent) { // hunt: go to where it was last seen
            Vec3 lastSeen = ta->seenPos;
            objectiveMove(moveDir, cmd.buttons, &lastSeen);
        }
        if (urgent) { // keep going for the objective, shooting on the way (instead of hunting: a route to each, every
                      // think, would plan both again every time, the goal jumping between them)
            uint16_t extra = 0;
            objectiveMove(moveDir, extra, nullptr);
            if (extra & BTN_USE) cmd.buttons = (uint16_t)((cmd.buttons & ~BTN_FIRE) | BTN_USE);
        }
        if ((me.move.flags & MF_HOOKED) && now < b.hookUntil) holdHook();
    } else {
        if (b.target < 0 || !ta->visible) b.engagedAt = -1;
        b.flickYaw = b.flickPitch = 0;
        uint16_t extra = 0;
        objectiveMove(moveDir, extra, nullptr);
        cmd.buttons |= extra;
        if (isItMode(ms.mode) && me.it && ms.phase == PHASE_LIVE) {   // seeking / tagging, nobody in sight: the locator when it's ready
            if (now >= me.locateAt) {
                if (me.weapon != WPN_LOCATOR) cmd.weapon = WPN_LOCATOR;
                else if (!me.fireLocked && !(me.buttons & BTN_FIRE)) cmd.buttons |= BTN_FIRE;
            } else if (me.weapon != WPN_KNIFE) cmd.weapon = WPN_KNIFE;
        }
        gain = sp.lookGain;
        bool idle = length(moveDir) < 1e-3f;
        if (!idle) wantYaw = yawOf(normalize(moveDir));
        wantPitch = 0;
        // look toward a fresh sound, else glance around now and then (that's how flanks get noticed)
        double heardT = -1e9; Vec3 heard;
        for (int i = 0; i < kMaxPlayers; i++)
            if (b.aware[i].heardAt > heardT && now - b.aware[i].heardAt < sp.soundLookWindow && isThreat(s, id, i)) { heardT = b.aware[i].heardAt; heard = b.aware[i].heardPos; }
        bool spotted = b.target >= 0 && ta->visible; // seen, still reacting: eyes stay on it, not on sounds
        if (spotted) {
            wantYaw = b.aimYaw;
            wantPitch = b.aimPitch;
        } else if (heardT > -1e9) {
            Vec3 d = heard + Vec3(0, 1.1f, 0) - eye;
            wantYaw = yawOf(d);
            wantPitch = std::asin(clampf(d.y / std::max(length(d), 0.01f), -1, 1));
            gain = sp.soundGain;
        } else {
            if (now >= b.nextGlance) { // (moving: off the route's heading; standing: a new direction to face)
                float off;
                if (NATIVE(SLOT_GLANCE)) {
                    off = (rnd(b) * 2 - 1) * (idle ? sp.glanceIdle : sp.glanceMoving);
                    b.glanceUntil = now + (idle ? 1.0 + rnd(b) * 1.5 : 0.35 + rnd(b) * 0.4);
                    b.nextGlance = now + 1.5 + rnd(b) * 3.0;
                } else {
                    ip->c.in[S_IDLE] = idle;
                    const double* o = ip->run(SLOT_GLANCE);
                    off = (float)o[0]; b.glanceUntil = o[1]; b.nextGlance = o[2];
                }
                b.glanceYaw = idle ? wrapAngle(b.aimYaw + off) : off;
            }
            if (now < b.glanceUntil) wantYaw = idle ? b.glanceYaw : wantYaw + b.glanceYaw;
        }
        // Grapple (advanced and up, or when it's the only way to move): up to a ledge on the route, or across a
        // long open stretch. Only when the hook would catch near that point; hold until close or out of time.
        bool grappleOnly = (me.move.mods & MOVE_GRAPPLE_ONLY) != 0;
        if ((sp.grapple || grappleOnly) && !(me.move.mods & MOVE_RUN_ONLY)) {
            if (now < b.hookUntil) {
                Vec3 d = normalize(b.hookAnchor - eye);
                wantYaw = yawOf(d);
                wantPitch = std::asin(clampf(d.y, -1, 1));
                gain = sp.hookAimGain;
                // (the hook needs the aim on the anchor before it's fired)
                if (me.move.flags & MF_HOOKED) holdHook();
                else if (std::fabs(angleDiff(b.aimYaw, wantYaw)) + std::fabs(b.aimPitch - wantPitch) < sp.hookAimTol) cmd.buttons |= BTN_GRAPPLE;
            } else if (me.move.hookCooldown <= 0 && now >= b.nextHookTry) {
                if (NATIVE(SLOT_HOOK_RETRY)) b.nextHookTry = now + (grappleOnly ? sp.hookRetryGrappleOnly : sp.hookRetry); // (expert: hooks around all the time)
                else { ip->c.in[S_GRAPPLEONLY] = grappleOnly; b.nextHookTry = ip->run(SLOT_HOOK_RETRY)[0]; }
                // the next waypoint if it's a ledge up, else the end of the route if it's a long open stretch
                // (and, on maps with pits, a few surfaces further along the route: the line to the route's end often
                // crosses a pit where one to a point along the route doesn't)
                Vec3 cands[5] = {b.waypoint, b.pathGoal};
                int nc = 2;
                if (s.map_.hasVoid || !s.map_.lava.empty())
                    for (int k : {5, 3, 1})
                        if ((size_t)k < b.path.size() && b.path[k] < (int)s.nav_.nodes.size()) {
                            const auto& n = s.nav_.nodes[b.path[k]];
                            cands[nc++] = {(n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f};
                        }
                for (int ci = 0; ci < nc; ci++) {
                    const Vec3& cand = cands[ci];
                    Vec3 off = cand - me.move.pos;
                    float rise = off.y, hd = std::sqrt(off.x * off.x + off.z * off.z);
                    bool want;
                    if (NATIVE(SLOT_ROUTE_HOOK)) want = grappleOnly ? hd > 2.0f : (rise > sp.hookRise && hd < sp.hookRiseReach) || (hd > sp.hookFlat && rise > sp.hookFlatDrop);
                    else { ip->c.in[S_RISE] = rise; ip->c.in[S_FLAT] = hd; ip->c.in[S_GRAPPLEONLY] = grappleOnly; want = ip->run(SLOT_ROUTE_HOOK)[0] != 0; }
                    if (!want) continue;
                    Vec3 tgt = cand - Vec3(0, 0.1f, 0); // just into the walkable surface, so the hook catches on it
                    Vec3 d = normalize(tgt - eye);
                    float t = s.map_.raycast(eye, d, kGrappleRange * moveValue(me.move, MV_HOOK_RANGE, &s.map_));
                    if (t >= 0 && length(eye + d * t - tgt) < 3.0f && safeHook(s, me, eye + d * t)) {
                        b.hookAnchor = eye + d * t;
                        b.hookUntil = now + 1.6;
                        b.hookChecked = false;
                        break;
                    }
                }
            }
        }
    }

    // Turn like a hand on a mouse: speed capped, changing at a limited rate, so long flicks take time and
    // settle; while tracking, the hand also moves with the target. Stepped in 1 ms of game time whatever the
    // tick length, so the hand is the same at every server tick rate.
    auto turn = [&](float& ang, float& vel, float want, float ff, bool wrap) {
        const int steps = tc.turnSteps;
        const float h = tc.turnH, dv = tc.turnDv;
        for (int k = 0; k < steps; k++) {
            float diff = wrap ? angleDiff(want, ang) : want - ang;
            float brake = std::sqrt(2.0f * sp.turnAccel * std::fabs(diff)) * sp.handBrake;
            float corr = clampf(diff * gain, -brake, brake);
            float target = clampf(ff + corr, -sp.turnRate, sp.turnRate);
            vel += clampf(target - vel, -dv, dv);
            ang += vel * h;
            if (wrap) ang = wrapAngle(ang);
        }
    };
    // The command carries the view as it is at the start of this tick (the hand then moves on through the tick),
    // the same instant a shot fired now is tested at: so the tick length doesn't change how well it hits.
    cmd.yaw = b.aimYaw;
    cmd.pitch = clampf(b.aimPitch, -1.5f, 1.5f);
    turn(b.aimYaw, b.yawVel, wantYaw, ffYaw, true);
    turn(b.aimPitch, b.pitchVel, clampf(wantPitch, -1.5f, 1.5f), 0.0f, false);

    // movement relative to the view direction
    float ml = length(moveDir);
    Vec3 fwd = {-std::sin(cmd.yaw), 0, -std::cos(cmd.yaw)}, right = {std::cos(cmd.yaw), 0, -std::sin(cmd.yaw)};
    if (ml > 1e-3f) {
        moveDir = moveDir / ml;
        Vec3 wish = moveDir;
        bool onGround = (me.move.flags & MF_ONGROUND) != 0;
        bool hopping;
        if (NATIVE(SLOT_BHOP)) hopping = !fighting && sp.bhop && s.ms_.mode != MODE_RACE && length(b.waypoint - me.move.pos) > sp.bhopMinDist;
        else { ip->c.in[S_FIGHTING] = fighting; ip->c.in[S_WPDIST] = length(b.waypoint - me.move.pos); hopping = ip->run(SLOT_BHOP)[0] != 0; }
        hopping = hopping && !(me.move.flags & MF_HOOKED);   // (hooked: no air-strafing off the pull)
        // air-strafe: push at the angle to the current velocity that gains speed, curving toward the route
        Vec3 hv = {me.move.vel.x, 0, me.move.vel.z};
        float hs = length(hv);
        if (hopping && !onGround && hs > 4.0f && sp.airStrafe > 0) {
            Vec3 vn = hv / hs;
            float phi = std::atan2(vn.x * moveDir.z - vn.z * moveDir.x, dot(vn, moveDir)); // signed angle velocity -> route
            if (std::fabs(phi) < 1.2f) {
                float th = std::acos(clampf(2.3f / hs, 0.0f, 1.0f)) * sp.airStrafe;
                if (std::fabs(phi) > 0.05f) b.strafeDir = phi > 0 ? 1.0f : -1.0f;
                float ang = th * b.strafeDir, c = std::cos(ang), sn = std::sin(ang);
                wish = {vn.x * c - vn.z * sn, 0, vn.x * sn + vn.z * c};
            } else if (hs > 9.5f) {
                // a sharp turn at speed: push square to the momentum, toward the route (a strafe key's push). Above
                // 9.5 m/s W-only air control turns slower than that (pmove: its rate falls off as 1 / speed, and a
                // strafe key's push gets the strafe boost), so a bot steering with W alone would swing wide.
                float sg = phi > 0 ? 1.0f : -1.0f;
                wish = {-vn.z * sg, 0, vn.x * sg};
            }
        }
        cmd.forward = (int8_t)clampf(dot(wish, fwd) * 127, -127, 127);
        cmd.side = (int8_t)clampf(dot(wish, right) * 127, -127, 127);
        // jump over obstacles (something knee-high ahead) and across gaps (no floor ahead)
        Vec3 knee = me.move.pos + Vec3(0, 0.6f, 0);
        // (movement probes: clip blocks players, so it counts here)
        if (s.map_.raycastAny(knee, moveDir, 1.2f, true)) cmd.buttons |= BTN_JUMP;
        Vec3 ahead = me.move.pos + moveDir * 0.7f + Vec3(0, 0.2f, 0);
        const bool pits = s.map_.hasVoid || !s.map_.lava.empty();
        // (lava can lie on a floor: a step into it is a drop too)
        if (onGround && (!s.map_.raycastAny(ahead, {0, -1, 0}, 1.5f, true) || (!s.map_.lava.empty() && !safeDrop(s, ahead)))) {
            // a drop ahead: on maps with pits or lava, only go if there's safe ground below, or a safe
            // landing within jumping distance; otherwise stop at the edge (the stuck check re-plans)
            Vec3 far = me.move.pos + moveDir * 2.6f + Vec3(0, 0.6f, 0);
            bool safe = !pits || safeDrop(s, ahead);
            // (a landing within jumping distance: only looked for when there's no safe ground below)
            if (safe || (s.map_.raycastAny(far, {0, -1, 0}, 2.0f, true) && (!pits || safeDrop(s, far)))) cmd.buttons |= BTN_JUMP;
            else { cmd.forward = 0; cmd.side = 0; hopping = false; }   // (and no bunny hop off it)
        }
        if (hopping) cmd.buttons |= BTN_JUMP;
        // long straight stretch: dash along it (levels that would), when it lands on something
        auto runDash = [&] {
            if (NATIVE(SLOT_RUN_DASH)) return sp.runDash > 0 && length(b.waypoint - me.move.pos) > sp.runDashMinDist && dot(moveDir, fwd) > sp.runDashStraight && chance(b, sp.runDash, tc.pRunDash);
            ip->c.in[S_WPDIST] = length(b.waypoint - me.move.pos); ip->c.in[S_STRAIGHT] = dot(moveDir, fwd);
            return ip->run(SLOT_RUN_DASH)[0] != 0;
        };
        if (!fighting && onGround && dashReady(me.move) && !(me.move.mods & MOVE_RUN_ONLY) && runDash()) {
            if (groundAlong(s, me.move.pos, moveDir, 12.0f)) {
                cmd.buttons |= BTN_DASH;
                // experts with the room make it a dash jump: jump with it and keep the dash's 24 m/s through a
                // jump's ~16 m flight (ground all the way, and the waypoint further than that)
                bool want;
                if (NATIVE(SLOT_DASH_JUMP)) want = sp.dashJump && hopping && length(b.waypoint - me.move.pos) > sp.dashJumpMinDist;
                else { ip->c.in[S_HOPPING] = hopping; ip->c.in[S_WPDIST] = length(b.waypoint - me.move.pos); want = ip->run(SLOT_DASH_JUMP)[0] != 0; }
                dashJump = want && groundAlong(s, me.move.pos, moveDir, 20.0f);
            }
        }
    }
    // Maps with pits or lava: look ahead along where we're actually going (momentum included: on the ground, mid-hop,
    // or just let go of a grapple, steering or not). If there's no safe ground there and nothing safe to land on,
    // brake hard and stay on the ground (friction stops you, a hop at speed carries you on). Fast (a dash, a
    // grapple's pull) looks further ahead: about what it takes to stop.
    if (s.map_.hasVoid || !s.map_.lava.empty()) {
        Vec3 hv = {me.move.vel.x, 0, me.move.vel.z};
        float hs = length(hv);
        if (ml > 1e-3f || hs > 1.0f) {
            bool onGround = (me.move.flags & MF_ONGROUND) != 0;
            Vec3 ahead = me.move.pos + hv * (hs > 10.0f ? 0.6f : 0.35f);
            bool ground = safeDrop(s, ahead + moveDir * 0.6f + Vec3(0, 0.3f, 0));
            if (hs > 10.0f) // (fast: the whole way there, a probe every ~1.5 m, so a pit on the way isn't skipped over)
                for (int k = 1, n = std::min(6, (int)(hs * 0.6f / 1.5f)); k < n && ground; k++)
                    ground = safeDrop(s, me.move.pos + hv * (0.6f * k / n) + Vec3(0, 0.3f, 0));
            Vec3 far = me.move.pos + moveDir * 2.6f + Vec3(0, 0.6f, 0);   // (a gap a jump from here clears)
            // (a gap jump: at a run, heading where we're going; flung along by a grapple or a dash isn't one)
            // (only needed with no safe ground ahead)
            bool landing = !ground && onGround && ml > 1e-3f && hs < 10.0f && dot(hv, moveDir) >= 0.5f * hs &&
                           s.map_.raycastAny(far, {0, -1, 0}, 2.0f, true) && safeDrop(s, far);
            if (!onGround && !ground) { // mid-jump over a gap: fine if the arc comes down on safe ground (braking now drops us in)
                float vy = me.move.vel.y, g = 22.0f;   // (kGravity)
                float tl = (vy + std::sqrt(std::max(0.0f, vy * vy + 2 * g * 0.5f))) / g;   // back down to about this height
                Vec3 at = me.move.pos + hv * tl + Vec3(0, 0.6f, 0);
                landing = s.map_.raycastAny(at, {0, -1, 0}, 2.0f, true) && safeDrop(s, at) &&
                          (hs < 6.0f || safeDrop(s, at + hv * (std::min(hs * 0.2f, 5.0f) / hs)));   // (and the skid after it)
                cmd.buttons &= ~BTN_JUMP;   // (either way: no hop on landing, toward a drop)
                if (landing) { cmd.forward = 0; cmd.side = 0; }   // (and no air-strafing on toward it)
            }
            if (!ground && !landing) {
                Vec3 back = hs > 0.5f ? hv / hs * -1.0f : moveDir * -1.0f;
                cmd.forward = (int8_t)clampf(dot(back, fwd) * 127, -127, 127);
                cmd.side = (int8_t)clampf(dot(back, right) * 127, -127, 127);
                cmd.buttons &= ~(BTN_JUMP | BTN_DASH);
                b.goalUntil = 0; // re-plan
            }
        }
    }

    // unstick: if we've barely moved for a while, hop and sidestep; when just roaming, pick somewhere else
    if (now >= b.stuckCheckAt) {
        bool stuck;
        double until = 0, next;
        if (NATIVE(SLOT_UNSTICK)) {
            stuck = ml > 1e-3f && length(me.move.pos - b.lastPos) < sp.stuckDist && b.target < 0;
            if (stuck) { b.strafeDir = rnd(b) < 0.5f ? -1.0f : 1.0f; until = now + sp.unstickTime; }
            next = now + sp.stuckEvery;
        } else {
            ip->c.in[S_MOVING] = ml > 1e-3f; ip->c.in[S_MOVED] = length(me.move.pos - b.lastPos); ip->c.in[S_HASTARGET] = b.target >= 0;
            const double* o = ip->run(SLOT_UNSTICK);
            stuck = o[0] != 0;
            if (stuck) { b.strafeDir = (float)o[1]; until = o[2]; }
            next = o[3];
        }
        if (stuck) {
            if (!b.nearEdge) cmd.buttons |= BTN_JUMP;
            else if (dot(Vec3(-moveDir.z, 0, moveDir.x) * b.strafeDir, b.edgeAway) < 0) b.strafeDir = -b.strafeDir; // by a drop: no hop, sidestep away from it
            b.unstickUntil = until;
            b.goalUntil = 0;
            b.needPath = true;
        }
        b.lastPos = me.move.pos;
        b.stuckCheckAt = next;
    }
    // jump with our own dash on the floor (a hop's landing, a fight jump) turns it into a dash jump: a 16 m flight
    // at dash speed, past what the dash checked for ground. Only when meant.
    if (!dashJump && ((cmd.buttons & BTN_DASH) || me.move.dashTimer > 0)) cmd.buttons &= ~BTN_JUMP;
    return cmd;
}
