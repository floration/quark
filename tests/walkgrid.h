// Collision-checked reachability for the map tests and the map report (REVIEW MAP-3: the bots' nav graph links brush
// tops with no collision check, so a spawn sealed in a box read as reachable).
//
// A walk grid over the map: columns every 0.25 m, and in each column every height where the real player hull
// (playerBox, crouched at least) fits against the real collision (the boxSolid brushes: clip blocks, trim doesn't),
// at the column's centre or up to 0.12 m off it (a 0.9 m slot), with at least 10 cm of floor under it, above the kill
// height and out of lava. Links are what a player can do without movement tech:
//   - walk, step up (0.55 m, pmove's step) or drop any distance to the next column, the hull clear all the way;
//   - jump up to 1.15 m onto the next column (a standing jump clears 1.22 m), standing headroom for it;
//   - jump a gap of up to 2.5 m (in 32 directions) onto floor under the jump's arc, the hull clear along the arc;
//   - climb a ladder: any two spots touching the same ladder volume.
// No dash, grapple, rocket jumps or strafe jumps: what this finds reachable, anyone can reach.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "game/gamedefs.h"
#include "game/gamemodes.h"
#include "game/map.h"
#include "game/pmove.h"

struct WalkGrid {
    static constexpr float kCol = 0.25f;        // column spacing (with the off-centre spots: any slot the hull fits, 6 cm to spare)
    static constexpr float kStep = 0.55f;       // pmove kStepHeight
    static constexpr float kJump = 1.15f;       // a standing jump clears ~1.22 m (7.34 m/s up, 22 m/s^2)
    static constexpr float kGap = 2.5f;         // a running jump covers ~5 m flat; 2.5 m leaves room for the rise
    static constexpr float kArc = 1.22f;        // the jump's peak above the take-off
    static constexpr float kHull = kPlayerHeight, kCrouch = kPlayerCrouchHeight, kHalf = kPlayerHalfWidth;

    // feet; room: free height above the feet (up to the next solid); shifted: off its column's centre (a narrow
    // place: the hull fits only nearer one side of the column), so its moves are checked with boxSolid itself
    struct Node { Vec3 p; float room; bool shifted = false; };
    std::vector<Node> nodes;                    // (ladders add one hub node each at the end: p = ladder centre, room 0)
    std::vector<std::vector<int>> links;        // directed: from -> to
    float x0 = 0, z0 = 0;
    int nx = 0, nz = 0, ladderHubs = 0;
    std::vector<uint32_t> colNodes;             // nodes of column c: [colNodes[c], colNodes[c + 1]), ascending y
    std::vector<uint32_t> colSpan;              // merged solid spans of column c (over its hull footprint): ...
    std::vector<std::pair<float, float>> spans; // ... spans[colSpan[c] .. colSpan[c + 1]), ascending, disjoint
    const Map* map = nullptr;

    int col(int i, int k) const { return k * nx + i; }
    Vec3 colCentre(int i, int k) const { return {x0 + (i + 0.5f) * kCol, 0, z0 + (k + 0.5f) * kCol}; }
    // the hull footprint of column c is clear of solids from y0 to y1
    bool clear(int c, float y0, float y1) const {
        for (uint32_t s = colSpan[c]; s < colSpan[c + 1]; s++) {
            if (spans[s].first >= y1) break;
            if (spans[s].second > y0) return false;
        }
        return true;
    }
    // a hull centred anywhere (x, z) is clear from y0 to y1: the footprints of the columns around it cover it (one,
    // two or four of them), so this errs toward blocked by up to a column's width
    bool clearAt(float x, float z, float y0, float y1) const {
        float fx = (x - x0) / kCol - 0.5f, fz = (z - z0) / kCol - 0.5f;
        int i = (int)std::floor(fx), k = (int)std::floor(fz);
        fx -= i; fz -= k;
        int i1 = fx < 1e-3f ? i : fx > 1 - 1e-3f ? i + 1 : -1, k1 = fz < 1e-3f ? k : fz > 1 - 1e-3f ? k + 1 : -1;
        for (int kk = (k1 >= 0 ? k1 : k); kk <= (k1 >= 0 ? k1 : k + 1); kk++)
            for (int ii = (i1 >= 0 ? i1 : i); ii <= (i1 >= 0 ? i1 : i + 1); ii++) {
                if (ii < 0 || kk < 0 || ii >= nx || kk >= nz) return false;
                if (!clear(col(ii, kk), y0, y1)) return false;
            }
        return true;
    }

    // the hull at a spot is clear from its feet up to y1 (shifted spots: against the map itself)
    bool nodeClear(int n, float y1) const {
        const Node& a = nodes[n];
        if (!a.shifted) return clear(colOf(a.p), a.p.y, y1);
        return !map->boxSolid({{a.p.x - kHalf, a.p.y, a.p.z - kHalf}, {a.p.x + kHalf, y1, a.p.z + kHalf}});
    }
    // floor under the feet reaching to within kSupport of the centre: at least 10 cm of the hull on it (pmove would
    // stand on a 4 cm glow lip at the hull's very edge, but nobody runs a course along one)
    static constexpr float kSupport = 0.3f;
    static bool supported(const Map& m, const Vec3& p) {
        return m.boxSolid({{p.x - kSupport, p.y - 0.02f, p.z - kSupport}, {p.x + kSupport, p.y - 0.001f, p.z + kSupport}});
    }
    int colOf(const Vec3& p) const { return col((int)std::floor((p.x - x0) / kCol), (int)std::floor((p.z - z0) / kCol)); }
    // a step / walk / jump-up move between neighbouring spots where one is shifted: the two columns and the crossing
    // (two straight legs, along one axis then the other: sliding sideways into a slot on one floor, then across at
    // the higher floor's height, or across first and then sideways on the other floor; each leg's sweep is exact)
    bool moveClear(const Node& a, const Node& b, float y1) const {
        float top = std::max(a.p.y, b.p.y);
        auto sweep = [&](float xa, float za, float xb, float zb, float lo) {
            return !map->boxSolid({{std::min(xa, xb) - kHalf, lo, std::min(za, zb) - kHalf}, {std::max(xa, xb) + kHalf, y1, std::max(za, zb) + kHalf}});
        };
        if (!sweep(a.p.x, a.p.z, a.p.x, a.p.z, a.p.y) || !sweep(b.p.x, b.p.z, b.p.x, b.p.z, b.p.y)) return false;
        bool alongX = std::fabs(b.p.x - a.p.x) > std::fabs(b.p.z - a.p.z);   // the move's main axis
        if (alongX) return (sweep(a.p.x, a.p.z, a.p.x, b.p.z, a.p.y) && sweep(a.p.x, b.p.z, b.p.x, b.p.z, top)) ||
                           (sweep(a.p.x, a.p.z, b.p.x, a.p.z, top) && sweep(b.p.x, a.p.z, b.p.x, b.p.z, b.p.y));
        return (sweep(a.p.x, a.p.z, b.p.x, a.p.z, a.p.y) && sweep(b.p.x, a.p.z, b.p.x, b.p.z, top)) ||
               (sweep(a.p.x, a.p.z, a.p.x, b.p.z, top) && sweep(a.p.x, b.p.z, b.p.x, b.p.z, b.p.y));
    }

    void build(const Map& m) {
        map = &m;
        const float kill = m.killY();
        x0 = std::floor(m.bounds.mn.x) - 1; z0 = std::floor(m.bounds.mn.z) - 1;
        nx = (int)std::ceil((m.bounds.mx.x + 1 - x0) / kCol); nz = (int)std::ceil((m.bounds.mx.z + 1 - z0) / kCol);
        const size_t C = (size_t)nx * nz;
        // 1. every solid brush into the columns whose footprint (half width `half`) it overlaps (strictly, as
        //    AABB::overlaps), as y spans, merged per column; raw: the unmerged spans' tops too
        auto raster = [&](float half, std::vector<uint32_t>& outCol, std::vector<std::pair<float, float>>& out,
                          std::vector<uint32_t>* rawCol, std::vector<float>* rawTops) {
            auto range = [&](float lo, float hi, float o, int n, int& a, int& b) {   // columns whose centre is in (lo - half, hi + half)
                a = std::max(0, (int)std::floor((lo - half - o) / kCol - 0.5f));
                b = std::min(n - 1, (int)std::ceil((hi + half - o) / kCol - 0.5f));
                while (a <= b && o + (a + 0.5f) * kCol <= lo - half) a++;
                while (b >= a && o + (b + 0.5f) * kCol >= hi + half) b--;
            };
            std::vector<uint32_t> count(C + 1, 0), fill;
            std::vector<std::pair<float, float>> raw;
            for (int pass = 0; pass < 2; pass++) {
                if (pass == 1) {
                    for (size_t c = 0; c < C; c++) count[c + 1] += count[c];
                    raw.assign(count[C], {0, 0});
                    fill.assign(count.begin(), count.end() - 1);
                }
                for (auto& b : m.brushes) {
                    if (!Map::gridSolid(b)) continue;
                    int i0, i1, k0, k1;
                    range(b.box.mn.x, b.box.mx.x, x0, nx, i0, i1);
                    range(b.box.mn.z, b.box.mx.z, z0, nz, k0, k1);
                    for (int k = k0; k <= k1; k++)
                        for (int i = i0; i <= i1; i++) {
                            size_t c = (size_t)col(i, k);
                            if (pass == 0) count[c + 1]++;
                            else raw[fill[c]++] = {b.box.mn.y, b.box.mx.y};
                        }
                }
            }
            outCol.assign(C + 1, 0);
            out.clear();
            out.reserve(raw.size());
            if (rawTops) rawTops->resize(raw.size());
            for (size_t c = 0; c < C; c++) {
                outCol[c] = (uint32_t)out.size();
                std::sort(raw.begin() + count[c], raw.begin() + count[c + 1]);
                for (uint32_t t = count[c]; t < count[c + 1]; t++) {
                    if (rawTops) (*rawTops)[t] = raw[t].second;
                    if (out.size() > outCol[c] && raw[t].first <= out.back().second) out.back().second = std::max(out.back().second, raw[t].second);
                    else out.push_back(raw[t]);
                }
            }
            outCol[C] = (uint32_t)out.size();
            if (rawCol) *rawCol = std::move(count);
        };
        std::vector<uint32_t> count, coreCol;
        std::vector<float> tops;
        std::vector<std::pair<float, float>> core;
        raster(kHalf, colSpan, spans, &count, &tops);
        // the core: what every hull up to 0.12 m off a column's centre covers (a solid in it rules out every shift)
        raster(kHalf - 0.12f, coreCol, core, nullptr, nullptr);
        auto coreClear = [&](int c, float y0, float y1) {
            for (uint32_t s = coreCol[c]; s < coreCol[c + 1]; s++) {
                if (core[s].first >= y1) break;
                if (core[s].second > y0) return false;
            }
            return true;
        };
        // 2. standing spots: the top of each span with at least a crouched hull of room above it
        nodes.clear();
        colNodes.assign(C + 1, 0);
        for (int k = 0; k < nz; k++)
            for (int i = 0; i < nx; i++) {
                int c = col(i, k);
                colNodes[c] = (uint32_t)nodes.size();
                for (uint32_t s = colSpan[c]; s < colSpan[c + 1]; s++) {
                    float y = spans[s].second, room = s + 1 < colSpan[c + 1] ? spans[s + 1].first - y : 1e9f;
                    if (room < kCrouch || y < kill + 0.05f) continue;
                    Vec3 p = colCentre(i, k);
                    p.y = y;
                    if (!supported(m, p) || m.inLava(playerBox(p, room < kHull))) continue;
                    nodes.push_back({p, room});
                }
                // a floor the hull doesn't fit on at the column's centre may fit a little to one side (a 0.9 m slot
                // fits it only within 5 cm): try the hull up to 0.12 m off centre, 6 cm apart, against the map itself
                for (uint32_t t = count[c]; t < count[c + 1]; t++) {
                    float y = tops[t];
                    if (y < kill + 0.05f || (t > count[c] && tops[t - 1] == y)) continue;
                    bool have = false;
                    for (uint32_t a = colNodes[c]; a < nodes.size() && !have; a++) have = std::fabs(nodes[a].p.y - y) < 0.06f;
                    if (have || !coreClear(c, y, y + kCrouch)) continue;
                    static const float sh[] = {0.06f, -0.06f, 0.12f, -0.12f};
                    Vec3 base = colCentre(i, k);
                    bool found = false;
                    for (int q = 0; q < 24 && !found; q++) {   // 8 along an axis first, then 16 diagonal
                        float sx = 0, sz = 0;
                        if (q < 8) (q < 4 ? sx : sz) = sh[q % 4];
                        else { sx = sh[(q - 8) % 4]; sz = sh[(q - 8) / 4]; }
                        Vec3 p = {base.x + sx, y, base.z + sz};
                        if (m.boxSolid(playerBox(p, true))) continue;
                        if (!supported(m, p)) continue;
                        float room = m.boxSolid(playerBox(p, false)) ? kCrouch : kHull;
                        if (m.inLava(playerBox(p, room < kHull))) continue;
                        nodes.push_back({p, room, true});
                        found = true;
                    }
                }
                std::sort(nodes.begin() + colNodes[c], nodes.end(), [](const Node& a, const Node& b) { return a.p.y < b.p.y; });
            }
        colNodes[C] = (uint32_t)nodes.size();
        const size_t N = nodes.size();
        links.assign(N, {});
        // 3. links: walk / step / drop / jump to the 4 neighbouring columns, and gap jumps along x and z
        const int di[4] = {1, -1, 0, 0}, dk[4] = {0, 0, 1, -1};
        std::vector<uint8_t> edge(N, 0);   // per spot, the sides (bits: +x -x +z -z) with no floor to walk onto: where gaps are jumped from
        for (int k = 0; k < nz; k++)
            for (int i = 0; i < nx; i++) {
                int c = col(i, k);
                for (uint32_t a = colNodes[c]; a < colNodes[c + 1]; a++) {
                    const float ya = nodes[a].p.y;
                    for (int d = 0; d < 4; d++) {
                        int i2 = i + di[d], k2 = k + dk[d];
                        if (i2 < 0 || k2 < 0 || i2 >= nx || k2 >= nz) continue;
                        int c2 = col(i2, k2);
                        bool level = false;   // a floor next door within a step down or a jump up
                        for (uint32_t b = colNodes[c2]; b < colNodes[c2 + 1]; b++) {
                            float yb = nodes[b].p.y, rise = yb - ya;
                            if (rise > kJump) continue;
                            level |= rise > -kStep - 0.05f;
                            float top = std::max(ya, yb);
                            // walk, step or drop: a crouched hull at the higher floor's height, over both columns
                            // (jump up: standing room)
                            float y1 = rise <= kStep ? top + kCrouch : yb + kHull;
                            bool ok = nodes[a].shifted || nodes[b].shifted ? moveClear(nodes[a], nodes[b], y1) : clear(c, ya, y1) && clear(c2, yb, y1);
                            if (ok) links[a].push_back((int)b);
                        }
                        // an edge: open air next door at this height (not a wall), with no floor in it to walk onto
                        if (!level && clear(c2, ya + 0.05f, ya + kArc + kHull)) edge[a] |= (uint8_t)(1 << d);
                    }
                }
            }
        // gaps: from an edge, a jump in any of 32 directions over up to 2.5 m (centre to centre), the standing hull
        // clear along the jump's arc (7.34 m/s up at 8 m/s across); it lands on any floor it passes over that is
        // no higher than the arc (plus a step), or falls to one lower down
        const int kDirs = 32;
        for (size_t a = 0; a < N; a++) {
            if (!edge[a]) continue;
            const Vec3 pa = nodes[a].p;
            for (int d = 0; d < kDirs; d++) {
                float ang = d * 6.2831853f / kDirs, dx = std::cos(ang), dz = std::sin(ang);
                bool out = false;   // only out over an open side (within 70 degrees of it)
                for (int e = 0; e < 4; e++) if (edge[a] >> e & 1) out |= dx * di[e] + dz * dk[e] > 0.34f;
                if (!out) continue;
                int last = -1;
                bool had[2] = {false, false};   // the column before had a landing: [0] about level or up, [1] lower down
                for (float sd = kCol; sd <= kGap + 1e-3f; sd += kCol * 0.5f) {
                    float t = sd / 8.0f, arc = 7.34f * t - 11.0f * t * t;
                    float x = pa.x + dx * sd, z = pa.z + dz * sd, lo = pa.y + arc;
                    if (!clearAt(x, z, lo, lo + kHull)) break;   // (the arc runs into something: no further this way)
                    int i = (int)std::floor((x - x0) / kCol), k = (int)std::floor((z - z0) / kCol);
                    if (i < 0 || k < 0 || i >= nx || k >= nz) break;
                    int c = col(i, k);
                    if (c == last) continue;
                    last = c;
                    // link the first column of each stretch of floor the jump passes over (walking covers the rest)
                    bool has[2] = {false, false};
                    for (uint32_t b = colNodes[c]; b < colNodes[c + 1]; b++) {
                        float yb = nodes[b].p.y;
                        if (yb > lo + kStep || yb < pa.y - 30.0f || !nodeClear((int)b, std::max(yb, lo) + kHull)) continue;
                        int cls = yb < pa.y - 1.0f;
                        if (!had[cls] && sd >= 2 * kCol) links[a].push_back((int)b);
                        has[cls] = true;
                    }
                    had[0] = has[0]; had[1] = has[1];
                }
            }
        }
        // 4. ladders: a hub per ladder, linked both ways with every spot whose hull touches its climb volume
        ladderHubs = (int)m.ladders.size();
        for (auto& l : m.ladders) {
            int hub = (int)nodes.size();
            nodes.push_back({l.center(), 0});
            links.emplace_back();
            AABB reach = {l.mn - Vec3(0.25f, 0.6f, 0.25f), l.mx + Vec3(0.25f, 0, 0.25f)};
            int i0 = std::max(0, (int)std::floor((reach.mn.x - kHalf - x0) / kCol)), i1 = std::min(nx - 1, (int)std::ceil((reach.mx.x + kHalf - x0) / kCol));
            int k0 = std::max(0, (int)std::floor((reach.mn.z - kHalf - z0) / kCol)), k1 = std::min(nz - 1, (int)std::ceil((reach.mx.z + kHalf - z0) / kCol));
            for (int k = k0; k <= k1; k++)
                for (int i = i0; i <= i1; i++) {
                    int c = col(i, k);
                    for (uint32_t a = colNodes[c]; a < colNodes[c + 1]; a++)
                        if (playerBox(nodes[a].p, nodes[a].room < kHull).overlaps(reach)) { links[a].push_back(hub); links[hub].push_back((int)a); }
                }
        }
    }

    // the standing spot for a spawn (or any point on a floor): in its column or one near it, the floor nearest under it
    int nodeAt(const Vec3& p, float maxRadius = 0.6f) const {
        int best = -1;
        float bestD = 1e30f;
        int r = (int)std::ceil(maxRadius / kCol);
        int ci = (int)std::floor((p.x - x0) / kCol), ck = (int)std::floor((p.z - z0) / kCol);
        for (int k = ck - r; k <= ck + r; k++)
            for (int i = ci - r; i <= ci + r; i++) {
                if (i < 0 || k < 0 || i >= nx || k >= nz) continue;
                int c = col(i, k);
                for (uint32_t a = colNodes[c]; a < colNodes[c + 1]; a++) {
                    const Vec3& q = nodes[a].p;
                    float dy = p.y - q.y;
                    if (dy < -0.3f || dy > 1.0f) continue;
                    float dx = q.x - p.x, dz = q.z - p.z, d = dx * dx + dz * dz + dy * dy;
                    if (dx * dx + dz * dz <= maxRadius * maxRadius && d < bestD) { bestD = d; best = (int)a; }
                }
            }
        return best;
    }
    // the standing spots in the columns within r of a box's XZ extent (then filtered by the caller's own test)
    template <class F> void forColumnsNear(const AABB& z, float r, F f) const {
        int i0 = std::max(0, (int)std::floor((z.mn.x - r - x0) / kCol)), i1 = std::min(nx - 1, (int)std::ceil((z.mx.x + r - x0) / kCol));
        int k0 = std::max(0, (int)std::floor((z.mn.z - r - z0) / kCol)), k1 = std::min(nz - 1, (int)std::ceil((z.mx.z + r - z0) / kCol));
        for (int k = k0; k <= k1; k++)
            for (int i = i0; i <= i1; i++) {
                int c = col(i, k);
                for (uint32_t a = colNodes[c]; a < colNodes[c + 1]; a++) f((int)a);
            }
    }
    // every standing spot whose hull overlaps a zone (hill, bomb site, capture point, race checkpoint: feetIn)
    std::vector<int> nodesIn(const AABB& zone) const {
        std::vector<int> out;
        forColumnsNear(zone, kHalf + kCol, [&](int a) { if (playerBox(nodes[a].p, nodes[a].room < kHull).overlaps(zone)) out.push_back(a); });
        return out;
    }
    // every standing spot within r of a point (the payload cart: pushers stand within 4 m of it)
    std::vector<int> nodesNear(const Vec3& p, float r) const {
        std::vector<int> out;
        forColumnsNear({p, p}, r, [&](int a) { if (length(nodes[a].p - p) < r) out.push_back(a); });
        return out;
    }
    // Strongly connected components (iterative Tarjan) and the graph between them: most of a map is one component,
    // so reachability from each spawn is a walk over a few hundred components instead of a million spots.
    std::vector<int> comp;
    std::vector<std::vector<int>> compLinks;
    void components() {
        const int N = (int)nodes.size();
        comp.assign(N, -1);
        std::vector<int> index(N, -1), low(N, 0), stack, callNode, callEdge;
        std::vector<char> onStack(N, 0);
        int next = 0, comps = 0;
        for (int root = 0; root < N; root++) {
            if (index[root] >= 0) continue;
            callNode.push_back(root); callEdge.push_back(0);
            index[root] = low[root] = next++; stack.push_back(root); onStack[root] = 1;
            while (!callNode.empty()) {
                int v = callNode.back();
                int& e = callEdge.back();
                if (e < (int)links[v].size()) {
                    int w = links[v][e++];
                    if (index[w] < 0) {
                        index[w] = low[w] = next++; stack.push_back(w); onStack[w] = 1;
                        callNode.push_back(w); callEdge.push_back(0);
                    } else if (onStack[w]) low[v] = std::min(low[v], index[w]);
                    continue;
                }
                if (low[v] == index[v]) {
                    int w;
                    do { w = stack.back(); stack.pop_back(); onStack[w] = 0; comp[w] = comps; } while (w != v);
                    comps++;
                }
                callNode.pop_back(); callEdge.pop_back();
                if (!callNode.empty()) low[callNode.back()] = std::min(low[callNode.back()], low[v]);
            }
        }
        compLinks.assign(comps, {});
        for (int v = 0; v < N; v++)
            for (int w : links[v]) if (comp[w] != comp[v]) compLinks[comp[v]].push_back(comp[w]);
        for (auto& l : compLinks) { std::sort(l.begin(), l.end()); l.erase(std::unique(l.begin(), l.end()), l.end()); }
    }
    // the components reachable from spot `from` (components() first)
    std::vector<char> reach(int from) const {
        std::vector<char> seen(compLinks.size(), 0);
        if (from < 0) return seen;
        std::vector<int> q = {comp[from]};
        seen[comp[from]] = 1;
        for (size_t h = 0; h < q.size(); h++)
            for (int b : compLinks[q[h]]) if (!seen[b]) { seen[b] = 1; q.push_back(b); }
        return seen;
    }
};

// The spawn groups a mode plays from on this map: the spawns that must reach each other and the objectives.
struct WalkSpawnSets {
    std::vector<int> a, b, any, race, ffa;          // ffa: the free-for-all pool (spawnPool: any spawns, else every spawn)
    std::vector<std::vector<int>> stage[2];         // staged spawns per stage: [0] attackers, [1] defenders
    explicit WalkSpawnSets(const Map& m) {
        for (int i = 0; i < (int)m.spawns.size(); i++) {
            int t = m.spawns[i].team;
            if (t == SPAWN_A) a.push_back(i); else if (t == SPAWN_B) b.push_back(i); else if (t == SPAWN_ANY) any.push_back(i);
            else if (t == SPAWN_RACE) race.push_back(i);
            else if (isStagedSpawn(t)) {
                auto& v = stage[spawnDefends(t) ? 1 : 0];
                if ((int)v.size() <= spawnStage(t)) v.resize(spawnStage(t) + 1);
                v[spawnStage(t)].push_back(i);
            }
        }
        for (const SpawnPoint* sp : spawnPool(m, SPAWN_ANY, 0)) ffa.push_back((int)(sp - m.spawns.data()));   // (spawnPlayer's own pool)
    }
};

// Every route each mode the map offers needs, walked on the grid; returns one line per missing route (empty: all
// there). vs / duel / 1v1: each team's spawns reach every enemy spawn. Free for all, hide, tag and the tourney's
// warmup: every spawn of the pool reaches every other (and a tourney's match spawns each other). Bomb: every team
// spawn reaches every site; koth: the hill; payload: the whole track (within the 4 m push range, every 4 m); multi
// point: each stage's spawns every point from theirs on; race: the race spawns every checkpoint.
inline std::vector<std::string> walkRoutesMissing(const Map& m, WalkGrid& w, int* routes = nullptr) {
    if (w.comp.size() != w.nodes.size()) w.components();
    struct Goal { std::string what; std::vector<int> nodes; };
    std::vector<Goal> goals;
    std::vector<std::vector<int>> need(m.spawns.size());   // spawn -> goals it must reach
    auto goal = [&](const std::string& what, std::vector<int> nodes) { goals.push_back({what, std::move(nodes)}); return (int)goals.size() - 1; };
    std::vector<int> spawnGoal(m.spawns.size(), -1), spawnNode(m.spawns.size(), -1);
    char buf[160];
    for (size_t i = 0; i < m.spawns.size(); i++) {
        spawnNode[i] = w.nodeAt(m.spawns[i].pos);
        const Vec3& p = m.spawns[i].pos;
        snprintf(buf, sizeof buf, "spawn %.1f %.1f %.1f", p.x, p.y, p.z);
        spawnGoal[i] = goal(buf, spawnNode[i] >= 0 ? std::vector<int>{spawnNode[i]} : std::vector<int>{});
    }
    auto require = [&](const std::vector<int>& from, int g) { for (int s : from) if (std::find(need[s].begin(), need[s].end(), g) == need[s].end()) need[s].push_back(g); };
    auto eachOther = [&](const std::vector<int>& x, const std::vector<int>& y) { for (int s : y) require(x, spawnGoal[s]); for (int s : x) require(y, spawnGoal[s]); };
    std::vector<std::pair<std::string, int>> zones;   // (each zone's spots found once)
    auto zone = [&](const char* kind, size_t k, const AABB& z) {
        snprintf(buf, sizeof buf, "%s %zu", kind, k + 1);
        for (auto& [what, g] : zones) if (what == buf) return g;
        int g = goal(buf, w.nodesIn(z));
        zones.push_back({buf, g});
        return g;
    };
    WalkSpawnSets S(m);
    std::vector<int> teams = S.a;
    teams.insert(teams.end(), S.b.begin(), S.b.end());
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        if (!m.supportsMode(mode)) continue;
        switch (mode) {
            case MODE_VS: case MODE_DUEL: case MODE_ARENA: eachOther(S.a, S.b); break;
            case MODE_TOURNEY: eachOther(S.ffa, S.ffa); if (m.canSupportMode(MODE_VS)) eachOther(S.a, S.b); break;
            case MODE_FFA: case MODE_HIDE: case MODE_TAG: eachOther(S.ffa, S.ffa); break;
            case MODE_BOMB: for (size_t k = 0; k < m.bombSites.size(); k++) require(teams, zone("bomb site", k, m.bombSites[k])); break;
            case MODE_KOTH: require(teams, zone("hill", 0, m.hill)); break;
            case MODE_PAYLOAD: case MODE_NPAYLOAD: {
                std::vector<int> from = teams;
                for (auto& side : S.stage) for (auto& v : side) from.insert(from.end(), v.begin(), v.end());
                float len = m.payloadLength();
                for (float d = 0; ; d = std::min(len, d + 4.0f)) {
                    Vec3 c = m.payloadPoint(d);
                    snprintf(buf, sizeof buf, "the cart at %.0f m of the track", d);
                    require(from, goal(buf, w.nodesNear(c, 3.5f)));
                    if (d >= len) break;
                }
                break;
            }
            case MODE_POINTS:
                for (int side = 0; side < 2; side++)
                    for (size_t st = 0; st < S.stage[side].size(); st++)
                        for (size_t k = st; k < m.points.size(); k++) require(S.stage[side][st], zone("point", k, m.points[k]));
                break;
            case MODE_RACE: for (size_t k = 0; k < m.checkpoints.size(); k++) require(S.race, zone("checkpoint", k, m.checkpoints[k])); break;
        }
    }
    std::vector<std::string> missing;
    int total = 0;
    for (size_t s = 0; s < m.spawns.size(); s++) {
        if (need[s].empty()) continue;
        const Vec3& p = m.spawns[s].pos;
        if (spawnNode[s] < 0) {
            snprintf(buf, sizeof buf, "spawn %.1f %.1f %.1f: no room to stand", p.x, p.y, p.z);
            missing.push_back(buf);
            total += (int)need[s].size();
            continue;
        }
        std::vector<char> seen = w.reach(spawnNode[s]);
        for (int g : need[s]) {
            total++;
            bool ok = false;
            for (int n : goals[g].nodes) if (seen[w.comp[n]]) { ok = true; break; }
            if (!ok) { snprintf(buf, sizeof buf, "spawn %.1f %.1f %.1f can't reach %s", p.x, p.y, p.z, goals[g].what.c_str()); missing.push_back(buf); }
        }
    }
    if (routes) *routes = total;
    return missing;
}
