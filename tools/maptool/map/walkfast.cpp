// WalkGrid::build (tests/walkgrid.h), COPIED and threaded: the same grid, spot for spot and link for link (the spots
// are found column row by column row and joined in order, every spot's links are its own), built on every core.
// Fills the tests' own WalkGrid, so nodeAt / reach / components / walkRoutesMissing are the header's.
// DEDUPE LATER: thread WalkGrid::build itself (tests/walkgrid.h is the tests thread's) and drop this copy.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include "analysis.h"
#include "maptool.h"
#include "walkfast.h"

int maptoolThreads() {
    static int n = [] { unsigned h = std::thread::hardware_concurrency(); return (int)std::clamp(h ? h : 4u, 1u, 16u); }();
    return n;
}

void buildWalkGrid(WalkGrid& w, const Map& m, Json* laps) {
    using W = WalkGrid;
    const float kCol = W::kCol, kStep = W::kStep, kJump = W::kJump, kGap = W::kGap, kRun = W::kRun, kArc = W::kArc;
    const float kHull = W::kHull, kCrouch = W::kCrouch, kHalf = W::kHalf;
    double t0 = maptoolNow();
    auto lap = [&](const char* what) { if (laps) laps->set(what, (maptoolNow() - t0) * 1000); t0 = maptoolNow(); };
    w.map = &m;
    const float kill = m.killY();
    w.x0 = std::floor(m.bounds.mn.x) - 1; w.z0 = std::floor(m.bounds.mn.z) - 1;
    w.nx = (int)std::ceil((m.bounds.mx.x + 1 - w.x0) / kCol); w.nz = (int)std::ceil((m.bounds.mx.z + 1 - w.z0) / kCol);
    const int nx = w.nx, nz = w.nz;
    const float x0 = w.x0, z0 = w.z0;
    const size_t C = (size_t)nx * nz;
    auto col = [&](int i, int k) { return k * nx + i; };
    // 1. raster (as the header)
    auto raster = [&](float half, std::vector<uint32_t>& outCol, std::vector<std::pair<float, float>>& out,
                      std::vector<uint32_t>* rawCol, std::vector<W::RawSpan>* raws, std::vector<float>* outSlack) {
        auto range = [&](float lo, float hi, float o, int n, int& a, int& b) {
            a = std::max(0, (int)std::floor((lo - half - o) / kCol - 0.5f));
            b = std::min(n - 1, (int)std::ceil((hi + half - o) / kCol - 0.5f));
            while (a <= b && o + (a + 0.5f) * kCol <= lo - half) a++;
            while (b >= a && o + (b + 0.5f) * kCol >= hi + half) b--;
        };
        std::vector<uint32_t> count(C + 1, 0), fill;
        std::vector<W::RawSpan> raw;
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) {
                for (size_t c = 0; c < C; c++) count[c + 1] += count[c];
                raw.assign(count[C], {0, 0, 0});
                fill.assign(count.begin(), count.end() - 1);
            }
            for (auto& b : m.brushes) {
                if (!Map::gridSolid(b)) continue;
                const bool ramp = W::walkableSlope(b);
                const float steep = ramp ? slopeSteepness(b) : 0;
                int i0, i1, k0, k1;
                range(b.box.mn.x, b.box.mx.x, x0, nx, i0, i1);
                range(b.box.mn.z, b.box.mx.z, z0, nz, k0, k1);
                for (int k = k0; k <= k1; k++)
                    for (int i = i0; i <= i1; i++) {
                        W::RawSpan r = {b.box.mn.y, b.box.mx.y, 0};
                        if (ramp) { Vec3 cc = w.colCentre(i, k); if (!W::columnSpan(b, steep, cc.x, cc.z, half, r.lo, r.hi, r.slack)) continue; }
                        size_t c = (size_t)col(i, k);
                        if (pass == 0) count[c + 1]++;
                        else raw[fill[c]++] = r;
                    }
            }
        }
        // (sorting and merging each column: per column, so on every core; the merged lists joined in order after)
        std::vector<std::vector<std::pair<float, float>>> rowOut(nz);
        std::vector<std::vector<float>> rowSlack(nz);
        std::vector<std::vector<uint32_t>> rowCount(nz);   // merged spans per column of the row
        parallelFor(nz, [&](int k) {
            auto& ro = rowOut[k];
            auto* rs = outSlack ? &rowSlack[k] : nullptr;
            auto& rc = rowCount[k];
            rc.assign(nx, 0);
            for (int i = 0; i < nx; i++) {
                size_t c = (size_t)col(i, k);
                size_t startSz = ro.size();
                std::sort(raw.begin() + count[c], raw.begin() + count[c + 1]);
                for (uint32_t t = count[c]; t < count[c + 1]; t++) W::mergeSpan(ro, rs, startSz, raw[t]);
                rc[i] = (uint32_t)(ro.size() - startSz);
            }
        }, 2);
        outCol.assign(C + 1, 0);
        out.clear();
        size_t total = 0;
        for (auto& r : rowOut) total += r.size();
        out.reserve(total);
        if (outSlack) { outSlack->clear(); outSlack->reserve(total); }
        for (int k = 0; k < nz; k++) {
            uint32_t base = (uint32_t)out.size();
            for (int i = 0; i < nx; i++) { outCol[col(i, k)] = base; base += rowCount[k][i]; }
            out.insert(out.end(), rowOut[k].begin(), rowOut[k].end());
            if (outSlack) outSlack->insert(outSlack->end(), rowSlack[k].begin(), rowSlack[k].end());
        }
        outCol[C] = (uint32_t)out.size();
        if (rawCol) *rawCol = std::move(count);
        if (raws) *raws = std::move(raw);
    };
    std::vector<uint32_t> count, coreCol;
    std::vector<W::RawSpan> tops;
    std::vector<float> slack;
    std::vector<std::pair<float, float>> core;
    raster(kHalf, w.colSpan, w.spans, &count, &tops, &slack);
    raster(kHalf - 0.12f, coreCol, core, nullptr, nullptr, nullptr);
    lap("raster");
    auto coreClear = [&](int c, float y0, float y1) {
        for (uint32_t s = coreCol[c]; s < coreCol[c + 1]; s++) {
            if (core[s].first >= y1) break;
            if (core[s].second > y0) return false;
        }
        return true;
    };
    // 2. standing spots, per row of columns (each row's own list, then joined in row order: the header's order)
    std::vector<std::vector<W::Node>> rowNodes(nz);
    std::vector<std::vector<uint32_t>> rowColStart(nz);
    parallelFor(nz, [&](int k) {
        auto& nodes = rowNodes[k];
        auto& cs = rowColStart[k];
        cs.assign(nx, 0);
        for (int i = 0; i < nx; i++) {
            int c = col(i, k);
            cs[i] = (uint32_t)nodes.size();
            size_t first = nodes.size();
            for (uint32_t s = w.colSpan[c]; s < w.colSpan[c + 1]; s++) {
                float y = w.spans[s].second, room = s + 1 < w.colSpan[c + 1] ? w.spans[s + 1].first - y : 1e9f;
                if (room < kCrouch || y < kill + 0.05f) continue;
                Vec3 p = w.colCentre(i, k);
                p.y = y;
                if (!W::supported(m, p, slack[s]) || m.inLava(playerBox(p, room < kHull))) continue;
                nodes.push_back({p, room});
            }
            for (uint32_t t = count[c]; t < count[c + 1]; t++) {
                float y = tops[t].hi;
                if (y < kill + 0.05f || (t > count[c] && tops[t - 1].hi == y)) continue;
                bool have = false;
                for (size_t a = first; a < nodes.size() && !have; a++) have = std::fabs(nodes[a].p.y - y) < 0.06f;
                if (have || !coreClear(c, y, y + kCrouch)) continue;
                static const float sh[] = {0.06f, -0.06f, 0.12f, -0.12f};
                Vec3 base = w.colCentre(i, k);
                bool found = false;
                for (int q = 0; q < 24 && !found; q++) {
                    float sx = 0, sz = 0;
                    if (q < 8) (q < 4 ? sx : sz) = sh[q % 4];
                    else { sx = sh[(q - 8) % 4]; sz = sh[(q - 8) / 4]; }
                    Vec3 p = {base.x + sx, y, base.z + sz};
                    if (m.boxSolid(playerBox(p, true))) continue;
                    if (!W::supported(m, p, tops[t].slack)) continue;
                    float room = m.boxSolid(playerBox(p, false)) ? kCrouch : kHull;
                    if (m.inLava(playerBox(p, room < kHull))) continue;
                    nodes.push_back({p, room, true});
                    found = true;
                }
            }
            std::sort(nodes.begin() + first, nodes.end(), [](const W::Node& a, const W::Node& b) { return a.p.y < b.p.y; });
        }
    }, 2);
    w.nodes.clear();
    w.colNodes.assign(C + 1, 0);
    {
        size_t total = 0;
        for (auto& r : rowNodes) total += r.size();
        w.nodes.reserve(total + m.ladders.size());
        for (int k = 0; k < nz; k++) {
            uint32_t base = (uint32_t)w.nodes.size();
            for (int i = 0; i < nx; i++) w.colNodes[col(i, k)] = base + rowColStart[k][i];
            w.nodes.insert(w.nodes.end(), rowNodes[k].begin(), rowNodes[k].end());
            std::vector<W::Node>().swap(rowNodes[k]);
        }
    }
    w.colNodes[C] = (uint32_t)w.nodes.size();
    const size_t N = w.nodes.size();
    w.links.assign(N, {});
    lap("spots");
    // 3. links (each spot's list is its own: rows in parallel)
    const int di[4] = {1, -1, 0, 0}, dk[4] = {0, 0, 1, -1};
    std::vector<uint8_t> edge(N, 0);
    parallelFor(nz, [&](int k) {
        for (int i = 0; i < nx; i++) {
            int c = col(i, k);
            for (uint32_t a = w.colNodes[c]; a < w.colNodes[c + 1]; a++) {
                const float ya = w.nodes[a].p.y;
                w.links[a].reserve(8);   // (one allocation for most spots: 4 walks, a few drops or jumps)
                for (int d = 0; d < 4; d++) {
                    int i2 = i + di[d], k2 = k + dk[d];
                    if (i2 < 0 || k2 < 0 || i2 >= nx || k2 >= nz) continue;
                    int c2 = col(i2, k2);
                    bool level = false;
                    for (uint32_t b = w.colNodes[c2]; b < w.colNodes[c2 + 1]; b++) {
                        float yb = w.nodes[b].p.y, rise = yb - ya;
                        if (rise > kJump) continue;
                        level |= rise > -kStep - 0.05f;
                        float top = std::max(ya, yb);
                        float y1 = rise <= kStep ? top + kCrouch : yb + kHull;
                        bool ok = w.nodes[a].shifted || w.nodes[b].shifted ? w.moveClear(w.nodes[a], w.nodes[b], y1) : w.clear(c, ya, y1) && w.clear(c2, yb, y1);
                        if (ok) w.links[a].push_back((int)b);
                    }
                    if (!level && w.clear(c2, ya + 0.05f, ya + kArc + kHull)) edge[a] |= (uint8_t)(1 << d);
                }
            }
        }
    }, 2);
    lap("links");
    const int kDirs = 32;
    parallelFor((int)N, [&](int ai) {
        size_t a = (size_t)ai;
        if (!edge[a]) return;
        const Vec3 pa = w.nodes[a].p;
        for (int d = 0; d < kDirs; d++) {
            float ang = d * 6.2831853f / kDirs, dx = std::cos(ang), dz = std::sin(ang);
            bool out = false;
            for (int e = 0; e < 4; e++) if (edge[a] >> e & 1) out |= dx * di[e] + dz * dk[e] > 0.34f;
            if (!out) continue;
            int last = -1;
            bool had[2] = {false, false};
            for (float sd = kCol; sd <= kGap + 1e-3f; sd += kCol * 0.5f) {
                float t = sd / kRun, arc = 7.67f * t - 12.0f * t * t;
                float x = pa.x + dx * sd, z = pa.z + dz * sd, lo = pa.y + arc;
                if (!w.clearAt(x, z, lo, lo + kHull)) break;
                int i = (int)std::floor((x - x0) / kCol), k = (int)std::floor((z - z0) / kCol);
                if (i < 0 || k < 0 || i >= nx || k >= nz) break;
                int c = col(i, k);
                if (c == last) continue;
                last = c;
                bool has[2] = {false, false};
                for (uint32_t b = w.colNodes[c]; b < w.colNodes[c + 1]; b++) {
                    float yb = w.nodes[b].p.y;
                    if (yb > lo + kStep || yb < pa.y - 30.0f || !w.nodeClear((int)b, std::max(yb, lo) + kHull)) continue;
                    int cls = yb < pa.y - 1.0f;
                    if (!had[cls] && sd >= 2 * kCol) w.links[a].push_back((int)b);
                    has[cls] = true;
                }
                had[0] = has[0]; had[1] = has[1];
            }
        }
    }, 256);
    lap("gaps");
    // 4. ladders (as the header)
    w.ladderHubs = (int)m.ladders.size();
    for (auto& l : m.ladders) {
        int hub = (int)w.nodes.size();
        w.nodes.push_back({l.center(), 0});
        w.links.emplace_back();
        AABB reach = {l.mn - Vec3(0.25f, 0.6f, 0.25f), l.mx + Vec3(0.25f, 0, 0.25f)};
        int i0 = std::max(0, (int)std::floor((reach.mn.x - kHalf - x0) / kCol)), i1 = std::min(nx - 1, (int)std::ceil((reach.mx.x + kHalf - x0) / kCol));
        int k0 = std::max(0, (int)std::floor((reach.mn.z - kHalf - z0) / kCol)), k1 = std::min(nz - 1, (int)std::ceil((reach.mx.z + kHalf - z0) / kCol));
        for (int k = k0; k <= k1; k++)
            for (int i = i0; i <= i1; i++) {
                int c = col(i, k);
                for (uint32_t a = w.colNodes[c]; a < w.colNodes[c + 1]; a++)
                    if (playerBox(w.nodes[a].p, w.nodes[a].room < kHull).overlaps(reach)) { w.links[a].push_back(hub); w.links[hub].push_back((int)a); }
            }
    }
    lap("ladders");
}

// walkverify {map}: the threaded build against the header's own, spot for spot and link for link (and both timed)
static int runWalkVerify(const Json& req, Json& out, CmdCtx&) {
    Map m;
    std::string err;
    if (!loadRequestMap(req, m, err)) return cmdError(out, err);
    Json laps = Json::object();
    double t0 = maptoolNow();
    WalkGrid a;
    buildWalkGrid(a, m, &laps);
    double t1 = maptoolNow();
    WalkGrid b;
    b.build(m);
    double t2 = maptoolNow();
    bool same = a.nodes.size() == b.nodes.size() && a.links == b.links && a.colNodes == b.colNodes && a.colSpan == b.colSpan && a.spans == b.spans && a.ladderHubs == b.ladderHubs;
    for (size_t i = 0; same && i < a.nodes.size(); i++)
        same = a.nodes[i].p.x == b.nodes[i].p.x && a.nodes[i].p.y == b.nodes[i].p.y && a.nodes[i].p.z == b.nodes[i].p.z && a.nodes[i].room == b.nodes[i].room && a.nodes[i].shifted == b.nodes[i].shifted;
    out = Json::object();
    out.set("same", same);
    out.set("spots", (double)a.nodes.size());
    out.set("threads", maptoolThreads());
    out.set("threadedMs", (t1 - t0) * 1000);
    out.set("headerMs", (t2 - t1) * 1000);
    out.set("laps", std::move(laps));
    return same ? 0 : cmdError(out, "the threaded walk grid differs from tests/walkgrid.h's");
}
MAPTOOL_COMMAND("walkverify", runWalkVerify, "{map} -> the threaded walk grid == tests/walkgrid.h's build (a self-check), with timings");
