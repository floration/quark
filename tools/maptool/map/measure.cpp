// The analysis behind check / metrics / route / custom, and the map report's measurement as JSON.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>
#include "common/u8file.h"
#include "analysis.h"
#include "maptool.h"
#include "measure_impl.h"
#include "walkfast.h"

// ---- the movement envelope's cache -----------------------------------------------------------------------------
// The envelope (~0.7 s of pmove on one core, ~0.25 s threaded) depends only on the movement code, so it is kept in a
// file per build of this tool: <temp>/quark_maptool_envelope_<size>_<mtime>.bin (a relink is a new file).
std::string g_maptoolExe;   // main(): this executable's path
namespace mr {
static bool envRead(FILE* f, std::vector<float>& v) {
    uint32_t n = 0;
    if (fread(&n, 4, 1, f) != 1 || n > 100000) return false;
    v.resize(n);
    return n == 0 || fread(v.data(), 4, n, f) == n;
}
static void envWrite(FILE* f, const std::vector<float>& v) { uint32_t n = (uint32_t)v.size(); fwrite(&n, 4, 1, f); if (n) fwrite(v.data(), 4, n, f); }
MoveEnvelope cachedMoveEnvelope() {
    std::string path;
    std::error_code ec;
    if (!g_maptoolExe.empty() && !std::getenv("QUARK_MAPTOOL_NO_CACHE")) {
        auto size = std::filesystem::file_size(utf8Path(g_maptoolExe), ec);
        auto mt = std::filesystem::last_write_time(utf8Path(g_maptoolExe), ec);
        if (!ec) {
            auto dir = std::filesystem::temp_directory_path(ec);
            if (!ec) path = (dir / ("quark_maptool_envelope_" + std::to_string(size) + "_" + std::to_string((long long)mt.time_since_epoch().count()) + ".bin")).u8string();
        }
    }
    const uint32_t kMagic = 0x51454E56u;   // "QENV"
    if (!path.empty()) {
        if (FILE* f = u8fopen(path, "rb")) {
            MoveEnvelope e;
            uint32_t magic = 0, tiers = 0;
            bool ok = fread(&magic, 4, 1, f) == 1 && magic == kMagic && fread(&tiers, 4, 1, f) == 1 && tiers > 0 && tiers < 64 &&
                      fread(&e.buildTime, 4, 1, f) == 1 && envRead(f, e.speed) && envRead(f, e.runUp) && e.speed.size() == tiers && e.runUp.size() == tiers;
            e.top.resize(tiers); e.apexLevel.resize(tiers); e.pullExit.resize(tiers);
            for (uint32_t k = 0; ok && k < tiers; k++) ok = envRead(f, e.top[k]) && envRead(f, e.apexLevel[k]) && envRead(f, e.pullExit[k]) &&
                                                              e.top[k].size() == (size_t)MoveEnvelope::kBins && e.apexLevel[k].size() == (size_t)MoveEnvelope::kBins && !e.pullExit[k].empty();
            fclose(f);
            if (ok) return e;
        }
    }
    MoveEnvelope e = computeMoveEnvelope();
    if (!path.empty()) {
        std::string tmp = path + ".tmp" + std::to_string((long long)maptoolNow());
        if (FILE* f = u8fopen(tmp, "wb")) {
            uint32_t tiers = (uint32_t)e.speed.size();
            fwrite(&kMagic, 4, 1, f); fwrite(&tiers, 4, 1, f); fwrite(&e.buildTime, 4, 1, f);
            envWrite(f, e.speed); envWrite(f, e.runUp);
            for (uint32_t k = 0; k < tiers; k++) { envWrite(f, e.top[k]); envWrite(f, e.apexLevel[k]); envWrite(f, e.pullExit[k]); }
            bool ok = fclose(f) == 0;
            if (ok) std::filesystem::rename(utf8Path(tmp), utf8Path(path), ec); else ec = std::make_error_code(std::errc::io_error);
            if (ec) std::filesystem::remove(utf8Path(tmp), ec);
        }
    }
    return e;
}
}  // namespace mr

// ---- Analysis ----------------------------------------------------------------------------------------------------
void Analysis::lap(const char* what, double t0) { ms_.push_back({what, (maptoolNow() - t0) * 1000}); }
const Map& Analysis::open() {
    if (!open_) {
        double t0 = maptoolNow();
        open_ = std::make_unique<Map>(m);
        // (as the map report: clip, invisible and out of reach above the walls, is no part of what anyone sees or plays on)
        open_->brushes.erase(std::remove_if(open_->brushes.begin(), open_->brushes.end(), [](const Brush& b) { return b.material == 9; }), open_->brushes.end());
        open_->rebuild();
        lap("noClip", t0);
    }
    return *open_;
}
const NavGraph& Analysis::nav() {
    if (!nav_) { double t0 = maptoolNow(); nav_ = std::make_unique<NavGraph>(); nav_->build(m); lap("nav", t0); }
    return *nav_;
}
const NavGraph& Analysis::navOpen() {
    if (!navOpen_) { const Map& o = open(); double t0 = maptoolNow(); navOpen_ = std::make_unique<NavGraph>(); navOpen_->build(o, NavKind::Surfaces); lap("navNoClip", t0); }
    return *navOpen_;
}
const NavGraph& Analysis::navSurfaces() {
    if (!navSurf_) { double t0 = maptoolNow(); navSurf_ = std::make_unique<NavGraph>(); navSurf_->build(m, NavKind::Surfaces); lap("navSurfaces", t0); }
    return *navSurf_;
}
WalkGrid& Analysis::walk() {
    if (!walk_) {
        double t0 = maptoolNow();
        walk_ = std::make_unique<WalkGrid>();
        buildWalkGrid(*walk_, m);
        lap("walkGrid", t0);
        t0 = maptoolNow();
        walk_->components();
        lap("walkComponents", t0);
    }
    return *walk_;
}
const std::vector<char>& Analysis::reach() {
    if (!reachDone_) { WalkGrid& w = walk(); double t0 = maptoolNow(); reach_ = mr::walkReachable(m, w); reachDone_ = true; lap("walkReach", t0); }
    return reach_;
}
Json Analysis::timings() const {
    Json t = Json::object();
    for (auto& [k, v] : ms_) t.set(k, v);
    return t;
}

// ---- routes --------------------------------------------------------------------------------------------------
// Douglas-Peucker: the corners of a dense route (walk grid spots are 0.25 m apart)
static void simplify(std::vector<Vec3>& p, float eps) {
    if (p.size() < 3) return;
    std::vector<char> keep(p.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<size_t, size_t>> st = {{0, p.size() - 1}};
    while (!st.empty()) {
        auto [a, b] = st.back(); st.pop_back();
        if (b <= a + 1) continue;
        Vec3 ab = p[b] - p[a];
        float L2 = dot(ab, ab);
        float best = -1; size_t bi = a;
        for (size_t i = a + 1; i < b; i++) {
            float t = L2 > 0 ? std::clamp(dot(p[i] - p[a], ab) / L2, 0.0f, 1.0f) : 0;
            float d = length(p[i] - (p[a] + ab * t));
            if (d > best) { best = d; bi = i; }
        }
        if (best > eps) { keep[bi] = 1; st.push_back({a, bi}); st.push_back({bi, b}); }
    }
    std::vector<Vec3> out;
    for (size_t i = 0; i < p.size(); i++) if (keep[i]) out.push_back(p[i]);
    p.swap(out);
}

// Dijkstra over the nav graph from `from`'s surface, with the predecessors
static void navDijkstra(const NavGraph& g, int from, std::vector<float>& d, std::vector<int>& prev) {
    d.assign(g.nodes.size(), 1e30f);
    prev.assign(g.nodes.size(), -1);
    if (from < 0) return;
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    d[from] = 0; q.push({0, from});
    while (!q.empty()) {
        auto [du, u] = q.top(); q.pop();
        if (du > d[u]) continue;
        for (auto& [v, c] : g.links[u]) if (du + c < d[v]) { d[v] = du + c; prev[v] = u; q.push({d[v], v}); }
    }
}
static Vec3 navCentre(const NavGraph& g, int i) { auto& n = g.nodes[i]; return Vec3((n.x0 + n.x1) * 0.5f, n.y, (n.z0 + n.z1) * 0.5f); }
static void navPath(const NavGraph& g, const std::vector<int>& prev, int to, const Vec3& a, const Vec3& b, std::vector<Vec3>* path) {
    if (!path) return;
    std::vector<int> nodes;
    for (int v = to; v >= 0; v = prev[v]) nodes.push_back(v);
    std::reverse(nodes.begin(), nodes.end());
    path->clear();
    path->push_back(a);
    for (int v : nodes) path->push_back(navCentre(g, v));
    path->push_back(b);
}
float navRoute(const NavGraph& g, const Vec3& a, const Vec3& b, std::vector<Vec3>* path) {
    // (mr::navBetween's distance: straight to the first surface's centre, the graph's costs, straight on to b)
    int na = mr::nearestNode(g, a), nb = mr::nearestNode(g, b + Vec3(0, 0.5f, 0));
    if (na < 0 || nb < 0) return -1;
    if (na == nb) { if (path) *path = {a, b}; return length(b - a); }
    std::vector<float> d; std::vector<int> prev;
    navDijkstra(g, na, d, prev);
    if (d[nb] > 1e29f) return -1;
    navPath(g, prev, nb, a, b, path);
    return length(navCentre(g, na) - a) + d[nb] + length(b - navCentre(g, nb));
}
float navRouteToBox(const NavGraph& g, const Vec3& a, const AABB& box, std::vector<Vec3>* path) {
    // (mr::navToBox's distance, the same loop, keeping where it ends)
    int na = mr::nearestNode(g, a);
    if (na < 0) return -1;
    std::vector<float> d; std::vector<int> prev;
    navDijkstra(g, na, d, prev);
    Vec3 c0 = navCentre(g, na), bestIn;
    float best = -1; int bi = -1;
    for (size_t i = 0; i < g.nodes.size(); i++) {
        auto& n = g.nodes[i];
        if (d[i] > 1e29f || n.x1 < box.mn.x || n.x0 > box.mx.x || n.z1 < box.mn.z || n.z0 > box.mx.z || n.y < box.mn.y - 0.5f || n.y > box.mx.y) continue;
        Vec3 in = {std::clamp((box.mn.x + box.mx.x) * 0.5f, n.x0, n.x1), n.y, std::clamp((box.mn.z + box.mx.z) * 0.5f, n.z0, n.z1)};
        float t = (int)i == na ? length(in - a) : length(c0 - a) + d[i] + length(in - navCentre(g, (int)i));
        if (best < 0 || t < best) { best = t; bi = (int)i; bestIn = in; }
    }
    if (bi >= 0 && path) { if (bi == na) *path = {a, bestIn}; else navPath(g, prev, bi, a, bestIn, path); }
    return best;
}

float walkRouteNodes(WalkGrid& w, int na, const std::vector<int>& targets, std::vector<Vec3>* path, int* reached) {
    if (reached) *reached = -1;
    if (na < 0 || targets.empty()) return -1;
    if (w.comp.size() != w.nodes.size()) w.components();
    {   // (no route to any of them: skip the search)
        std::vector<char> seen = w.reach(na);
        bool any = false;
        for (int t : targets) if (t >= 0 && seen[w.comp[t]]) { any = true; break; }
        if (!any) return -1;
    }
    const size_t N = w.nodes.size();
    std::vector<char> goal(N, 0);
    for (int t : targets) if (t >= 0) goal[t] = 1;
    std::vector<float> d(N, 1e30f);
    std::vector<int> prev(N, -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    d[na] = 0; q.push({0, na});
    const int hub0 = (int)N - w.ladderHubs;
    auto cost = [&](int u, int v) {   // a ladder: climbing (the report's 1.8 per vertical metre) to and from its hub
        if (u >= hub0 || v >= hub0) { Vec3 dd = w.nodes[v].p - w.nodes[u].p; return std::hypot(dd.x, dd.z) + std::fabs(dd.y) * 1.8f * 0.5f; }
        return length(w.nodes[v].p - w.nodes[u].p);
    };
    int end = -1;
    while (!q.empty()) {
        auto [du, u] = q.top(); q.pop();
        if (du > d[u]) continue;
        if (goal[u]) { end = u; break; }
        for (int v : w.links[u]) {
            float nd = d[u] + cost(u, v);
            if (nd < d[v]) { d[v] = nd; prev[v] = u; q.push({nd, v}); }
        }
    }
    if (end < 0) return -1;
    if (reached) *reached = end;
    if (path) {
        path->clear();
        for (int v = end; v >= 0; v = prev[v]) path->push_back(w.nodes[v].p);
        std::reverse(path->begin(), path->end());
        simplify(*path, 0.12f);
    }
    return d[end];
}

float walkRoute(WalkGrid& w, const Vec3& a, const Vec3& b, std::vector<Vec3>* path, float maxRadius) {
    int na = w.nodeAt(a, maxRadius), nb = w.nodeAt(b, maxRadius);
    if (na < 0 || nb < 0) return -1;
    return walkRouteNodes(w, na, {nb}, path);
}

// ---- the movement envelope -----------------------------------------------------------------------------------
Json envelopeJson() {
    const mr::MoveEnvelope& e = mr::moveEnvelope();
    Json tiers = Json::array();
    for (int k = 0; k < e.tiers(); k++) {
        Json t = Json::object();
        t.set("speed", e.speed[k]);
        t.set("runUp", e.runUp[k] < 1e8f ? Json(e.runUp[k]) : Json());   // m of moving from a standstill to get to it (null: never)
        t.set("flatReach", e.reach(k, 0));     // m across a jump from it reaches at take-off height
        t.set("dropReach", e.reach(k, -6));    // ...6 m lower down
        t.set("rise", e.rise(k));              // the highest above take-off its arcs get (m)
        Json pulls = Json::array();
        for (float v : e.pullExit[k]) pulls.push(v);
        t.set("pullExit", std::move(pulls));   // speed after a grapple pull of 0, 5, 10 .. 45 m
        tiers.push(std::move(t));
    }
    Json o = Json::object();
    o.set("tiers", std::move(tiers));
    o.set("buildTime", e.buildTime);           // s of perfect play to the top speed
    o.set("maxReach", e.maxReach());
    Json walk = Json::object();               // what the walk grid (no movement tech) links
    walk.set("step", WalkGrid::kStep); walk.set("jumpUp", WalkGrid::kJump); walk.set("gap", WalkGrid::kGap);
    walk.set("run", WalkGrid::kRun); walk.set("arc", WalkGrid::kArc);
    o.set("walkGrid", std::move(walk));
    return o;
}

// ---- out of bounds ---------------------------------------------------------------------------------------------
// COPIED from measure() (measure_impl.h, itself mapreport.cpp's): the samples, the playable floor, the tech graph and
// its out-of-bounds count, without the rest of the measurement. DEDUPE LATER (with measure()).
OutOfBounds outOfBounds(Analysis& a) {
    OutOfBounds R;
    const Map& m = a.open();
    const Map& solid = a.m;
    const NavGraph& g = a.navOpen();
    WalkGrid& w = a.walk();
    const std::vector<char>& reach = a.reach();
    if (m.spawns.empty()) return R;
    std::vector<char> playable(g.nodes.size(), 0);
    for (size_t i = 0; i < g.nodes.size(); i++) {
        auto& n = g.nodes[i];
        if (n.y <= m.killY()) continue;
        AABB box = {{n.x0 - 0.4f, n.y - 0.2f, n.z0 - 0.4f}, {n.x1 + 0.4f, n.y + 0.2f, n.z1 + 0.4f}};
        w.forColumnsNear(box, 0, [&](int s) {
            const Vec3& q = w.nodes[s].p;
            if (!reach[s] || std::fabs(q.y - n.y) >= 0.15f || q.x < box.mn.x || q.x > box.mx.x || q.z < box.mn.z || q.z > box.mx.z) return;
            playable[i] = 1;
        });
    }
    std::vector<mr::Pt> pts;
    for (size_t i = 0; i < g.nodes.size(); i++) {
        auto& n = g.nodes[i];
        float sx = n.x1 - n.x0 < 2 ? (n.x0 + n.x1) / 2 : n.x0 + 1, sz = n.z1 - n.z0 < 2 ? (n.z0 + n.z1) / 2 : n.z0 + 1;
        for (float x = sx; x < n.x1; x += 2)
            for (float z = sz; z < n.z1; z += 2) {
                Vec3 p = {x, n.y, z};
                if (!m.boxSolid(playerBox(p + Vec3(0, 0.02f, 0), false))) pts.push_back({p, (int)i});
            }
    }
    mr::TechGraph tech;
    float techCell = 4;
    std::vector<char> techPlayable;
    {
        std::vector<Vec3> tp;
        size_t stride = pts.size() / 2500 + 1;
        for (size_t i = 0; i < pts.size(); i += stride) { tp.push_back(pts[i].p); techPlayable.push_back(playable[pts[i].node]); }
        techCell = 4.0f * stride;
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
    const auto& d = tech.from(tech.nearest(m.spawns[0].pos));
    float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
    for (size_t i = 0; i < g.nodes.size(); i++) if (playable[i]) { auto& n = g.nodes[i]; x0 = std::min(x0, n.x0); x1 = std::max(x1, n.x1); z0 = std::min(z0, n.z0); z1 = std::max(z1, n.z1); }
    int extra = 0, out = 0;
    for (size_t i = 0; i < tech.p.size(); i++) {
        if (d[i] > 1e29f || techPlayable[i]) continue;
        extra++;
        const Vec3& q = tech.p[i];
        bool o = q.x < x0 - 1 || q.x > x1 + 1 || q.z < z0 - 1 || q.z > z1 + 1;
        out += o;
        if (o) R.spots.push_back(q);
    }
    R.techExtra = extra * techCell;
    R.outside = out * techCell;
    R.measured = true;
    return R;
}

// ---- the measurement as JSON -------------------------------------------------------------------------------------
Json measureJson(Analysis& a, Json* timings) {
    double t0 = maptoolNow();
    mr::Metrics r;
    a.navOpen(); a.reach();
    mr::measure(r, a.open(), a.m, a.navOpen(), a.walk(), a.reach());
    r.exempt = mr::mapExemptions(a.m);
    double t1 = maptoolNow();
    // the per-map scores (mapReport()'s formulas, copied: DEDUPE LATER); creativity and originality need every map
    using mr::clamp10;
    r.sDecor = clamp10(4 * std::min(r.detailDensity / 16, 1.0f) + 4 * (1 - r.bareWalls) + 2 * std::min(r.lights / 40.0f, 1.0f));
    float bandMix = 0;
    for (int b = 0; b < 3; b++) if (r.band[b] > 0) bandMix -= r.band[b] * std::log(r.band[b]) / std::log(3.0f);
    float loopsPerK = r.loops / std::max(1.0f, r.area / 1000);
    r.lLevels = 2 * std::min(r.levels / 8.0f, 1.0f); r.lLoops = 2 * std::min(loopsPerK / mr::kLoopsFull, 1.0f);
    r.lCover = 3 * std::min(r.coverPts / 0.55f, 1.0f); r.lRange = 3 * bandMix; r.lDead = 0.3f * r.deadEnds;
    r.sLayout = clamp10(r.lLevels + r.lLoops + r.lCover + r.lRange - r.lDead);
    r.mGrapple = clamp10(r.grapple / 0.8f * 10);
    r.mLanes = clamp10(r.lanes / 40 * 10);
    r.mVertical = clamp10(5 * std::min(r.vertRange / 12.0f, 1.0f) + 0.5f * std::min(r.levels, 6) + std::min(r.jumpLinks / 0.25f, 1.0f) + (r.ladders ? 1 : 0));
    r.mHeadroom = clamp10(r.headroom / 0.9f * 10);
    r.mRisk = r.preferred == "race" ? 10 : r.voidEdge < 0.08f ? 6 + r.voidEdge / 0.08f * 4 : r.voidEdge <= 0.35f ? 10 : clamp10(10 - (r.voidEdge - 0.35f) / 0.4f * 8);
    r.sMovement = (r.mGrapple + r.mLanes + r.mVertical + r.mHeadroom + r.mRisk + r.mTech) / 6;
    r.sBalance = r.fairness >= 0 ? clamp10(r.fairness * 10) : -1;

    Json o = Json::object();
    Json dec = Json::object();
    dec.set("brushes", r.brushes); dec.set("detail", r.detail); dec.set("glow", r.glow); dec.set("metal", r.metal);
    dec.set("accent", r.accent); dec.set("wallbang", r.wallbang); dec.set("far", r.far); dec.set("lights", r.lights);
    dec.set("materials", r.materials); dec.set("detailDensity", r.detailDensity); dec.set("bareWalls", r.bareWalls);
    o.set("decor", std::move(dec));
    Json lay = Json::object();
    lay.set("area", r.area); lay.set("levels", r.levels); lay.set("vertRange", r.vertRange); lay.set("navSurfaces", r.nodes);
    lay.set("navLinks", r.links); lay.set("deadEnds", r.deadEnds); lay.set("loops", r.loops); lay.set("loopsPer1000m2", loopsPerK);
    lay.set("visible", r.visible);
    Json bands = Json::array(); for (float b : r.band) bands.push(b);
    lay.set("sightBands", std::move(bands));   // close (< 6 m) / mid (< 20) / long (< laser range) / extreme
    lay.set("cover", r.cover); lay.set("coverPoints", r.coverPts); lay.set("fill", r.fill); lay.set("hideSpots", r.hideSpots);
    o.set("layout", std::move(lay));
    Json mv = Json::object();
    mv.set("jumpLinks", r.jumpLinks); mv.set("grapple", r.grapple); mv.set("headroom", r.headroom); mv.set("roofed", r.roofed);
    mv.set("liquids", r.liquids); mv.set("liquidArea", r.liquidArea); mv.set("lanes", r.lanes); mv.set("voidEdge", r.voidEdge);
    mv.set("ladders", r.ladders); mv.set("techExtra", r.techExtra); mv.set("outOfBounds", r.outOfBounds); mv.set("tech", r.techLine);
    o.set("movement", std::move(mv));
    Json modes = Json::array();
    for (auto& [k, v] : r.modes) { Json x = Json::object(); x.set("mode", k); x.set("line", v); modes.push(std::move(x)); }
    o.set("modes", std::move(modes));
    Json leads = Json::array();
    for (auto& [k, v] : r.leads) { Json x = Json::object(); x.set("what", k); x.set("lead", std::isnan(v) ? Json() : Json(v)); leads.push(std::move(x)); }
    o.set("leads", std::move(leads));   // attackers' arrival minus defenders' (s; positive: defenders first; null: unreachable)
    o.set("fairness", r.fairness); o.set("worstFair", r.worstFair);
    Json fits = Json::array();
    for (auto& f : r.modeFits) { Json x = Json::object(); x.set("mode", f.mode); x.set("fit", f.fit); x.set("why", f.why); fits.push(std::move(x)); }
    o.set("modeFits", std::move(fits));
    Json feats = Json::array(); for (auto& f : r.features) feats.push(f);
    o.set("features", std::move(feats));
    o.set("setting", r.setting);
    Json ex = Json::array(); for (auto& e : r.exempt) ex.push(e);
    o.set("exempt", std::move(ex));
    Json sc = Json::object();
    sc.set("decor", r.sDecor); sc.set("layout", r.sLayout); sc.set("movement", r.sMovement);
    sc.set("modeFit", r.sMode >= 0 ? Json(r.sMode) : Json()); sc.set("modeFitWhy", r.modeFit);
    sc.set("balance", r.sBalance >= 0 ? Json(r.sBalance) : Json());
    Json parts = Json::object();
    parts.set("layoutLevels", r.lLevels); parts.set("layoutLoops", r.lLoops); parts.set("layoutCover", r.lCover); parts.set("layoutRange", r.lRange); parts.set("layoutDeadEnds", -r.lDead);
    parts.set("moveGrapple", r.mGrapple); parts.set("moveLanes", r.mLanes); parts.set("moveVertical", r.mVertical); parts.set("moveHeadroom", r.mHeadroom);
    parts.set("moveRisk", r.mRisk); parts.set("moveTech", r.mTech);
    sc.set("parts", std::move(parts));
    o.set("scores", std::move(sc));
    if (timings) {
        timings->set("measure", (t1 - t0) * 1000);
        for (auto& [k, v] : mr::mrLaps()) timings->set("measure." + k, v);
    }
    return o;
}
