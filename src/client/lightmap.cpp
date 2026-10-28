#include "lightmap.h"
#include "../common/u8file.h"
#include "../common/net.h"
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>
#include <climits>
#include <cstdlib>
#include "stb/stb_image.h"   // (its zlib decoder, for shipped bakes: built in quark_assets)

namespace {
constexpr uint32_t kCacheMagic = 0x324D4C51; // "QLM2": named by the map's sha256 (QLM1: by its name, with its CRC)
constexpr uint32_t kBakeVersion = 12;   // 12: no payload cart shadow (bakes took its box from the join) // 11: sun and bounce rays pass clip // 10: point lights 10% brighter // 6: point lights
constexpr int kSkySamples = 48;
constexpr int kSunSamples = 6;
constexpr float kAlbedo = 0.65f;

struct Face {
    int axis, sign;
    float plane;
    float u0, u1, v0, v1;
    int wu, wv;   // texels
    int rx, ry;   // atlas rect origin (including 1px padding)
    int material;
};

bool anyHit(const Map& m, const Vec3& o, const Vec3& d, float maxT) { return m.raycastAny(o, d, maxT); }

// "Is this point inside any brush?" (every brush: trim, far scenery, water and clip too, which the map's own collision
// grid leaves out), asked for up to 81 points per face: tens of millions of times on a big map, and it used to test
// every brush each time (seconds of every load, a cached one too). An XZ grid over all of them: a point strictly
// inside a box lies in a cell of the box's span (same floor of the same expression), so the answer is the same.
struct PointGrid {
    float x0 = 0, z0 = 0, cs = 4;
    int w = 1, h = 1;
    std::vector<uint32_t> start, ids;   // cell c's brushes: ids[start[c], start[c + 1])
    std::vector<uint32_t> big;          // brushes over a quarter of the grid: tested for every point
    const Map* m = nullptr;
    int cx(float x) const { return (int)std::floor((x - x0) / cs); }
    int cz(float z) const { return (int)std::floor((z - z0) / cs); }
    explicit PointGrid(const Map& map) : m(&map) {
        if (map.brushes.empty()) { start.assign(2, 0); return; }
        float ax = 1e30f, az = 1e30f, bx = -1e30f, bz = -1e30f;
        for (const Brush& b : map.brushes) { ax = std::min(ax, b.box.mn.x); az = std::min(az, b.box.mn.z); bx = std::max(bx, b.box.mx.x); bz = std::max(bz, b.box.mx.z); }
        x0 = ax; z0 = az;
        cs = std::max(4.0f, std::max(bx - ax, bz - az) / 256.0f);   // (at most ~256 x 256 cells)
        w = std::max(1, cx(bx) + 1); h = std::max(1, cz(bz) + 1);
        auto span = [&](const Brush& b, int& a0, int& a1, int& b0, int& b1) {
            a0 = std::max(0, cx(b.box.mn.x)); a1 = std::min(w - 1, cx(b.box.mx.x));
            b0 = std::max(0, cz(b.box.mn.z)); b1 = std::min(h - 1, cz(b.box.mx.z));
        };
        const long long bigCells = (long long)w * h / 4;
        std::vector<uint32_t> count((size_t)w * h + 1, 0);
        for (size_t i = 0; i < map.brushes.size(); i++) {
            int a0, a1, b0, b1; span(map.brushes[i], a0, a1, b0, b1);
            if (a1 < a0 || b1 < b0) continue;
            if ((long long)(a1 - a0 + 1) * (b1 - b0 + 1) > std::max(64LL, bigCells)) { big.push_back((uint32_t)i); continue; }
            for (int z = b0; z <= b1; z++) for (int x = a0; x <= a1; x++) count[(size_t)z * w + x]++;
        }
        start.assign((size_t)w * h + 1, 0);
        for (size_t c = 0; c < (size_t)w * h; c++) start[c + 1] = start[c] + count[c];
        ids.resize(start.back());
        std::vector<uint32_t> fill(start.begin(), start.end() - 1);
        for (size_t i = 0; i < map.brushes.size(); i++) {
            int a0, a1, b0, b1; span(map.brushes[i], a0, a1, b0, b1);
            if (a1 < a0 || b1 < b0 || (long long)(a1 - a0 + 1) * (b1 - b0 + 1) > std::max(64LL, bigCells)) continue;
            for (int z = b0; z <= b1; z++) for (int x = a0; x <= a1; x++) ids[fill[(size_t)z * w + x]++] = (uint32_t)i;
        }
    }
    bool inside(const Brush& b, const Vec3& p) const {
        return p.x > b.box.mn.x && p.x < b.box.mx.x && p.y > b.box.mn.y && p.y < b.box.mx.y && p.z > b.box.mn.z && p.z < b.box.mx.z;
    }
    // A face's rectangle (on plane `plane` of `axis`, lifted `off` along its normal) completely inside the union of
    // occluding brushes, exactly: the brushes that hold that lifted plane, their rectangles clipped to the face, then
    // every cell between their edges tested once. (A 9 x 9 sample grid missed visible strips thinner than a ninth of
    // the face: stair sides and risers vanished.) Clip boxes are invisible, so they hide nothing.
    bool buried(int axis, float plane, float u0, float u1, float v0, float v1) const {
        const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
        Vec3 lo, hi;
        lo[axis] = hi[axis] = plane; lo[ua] = u0; hi[ua] = u1; lo[va] = v0; hi[va] = v1;
        std::vector<uint32_t> cand(big);
        int x0c = std::max(0, cx(lo.x)), x1c = std::min(w - 1, cx(hi.x)), z0c = std::max(0, cz(lo.z)), z1c = std::min(h - 1, cz(hi.z));
        for (int z = z0c; z <= z1c; z++) for (int x = x0c; x <= x1c; x++) {
            const size_t c = (size_t)z * w + x;
            for (uint32_t k = start[c]; k < start[c + 1]; k++) cand.push_back(ids[k]);
        }
        std::sort(cand.begin(), cand.end());
        cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
        struct R { float u0, u1, v0, v1; };
        std::vector<R> rs;
        std::vector<float> us = {u0, u1}, vs = {v0, v1};
        for (uint32_t i : cand) {
            const Brush& b = m->brushes[i];
            if (b.material == 9 || !(plane > b.box.mn[axis] && plane < b.box.mx[axis])) continue;
            R r{std::max(u0, b.box.mn[ua]), std::min(u1, b.box.mx[ua]), std::max(v0, b.box.mn[va]), std::min(v1, b.box.mx[va])};
            if (r.u1 <= r.u0 || r.v1 <= r.v0) continue;
            rs.push_back(r); us.push_back(r.u0); us.push_back(r.u1); vs.push_back(r.v0); vs.push_back(r.v1);
        }
        if (rs.empty()) return false;
        std::sort(us.begin(), us.end()); us.erase(std::unique(us.begin(), us.end()), us.end());
        std::sort(vs.begin(), vs.end()); vs.erase(std::unique(vs.begin(), vs.end()), vs.end());
        for (size_t i = 0; i + 1 < us.size(); i++) {
            if (us[i + 1] - us[i] < 1e-4f) continue;   // (slivers below the snap: not visible)
            const float u = 0.5f * (us[i] + us[i + 1]);
            for (size_t j = 0; j + 1 < vs.size(); j++) {
                if (vs[j + 1] - vs[j] < 1e-4f) continue;
                const float v = 0.5f * (vs[j] + vs[j + 1]);
                bool in = false;
                for (const R& r : rs) if (u > r.u0 && u < r.u1 && v > r.v0 && v < r.v1) { in = true; break; }
                if (!in) return false;
            }
        }
        return true;
    }
    bool solid(const Vec3& p) const {
        for (uint32_t i : big) if (inside(m->brushes[i], p)) return true;
        int x = cx(p.x), z = cz(p.z);
        if (x < 0 || z < 0 || x >= w || z >= h) return false;   // (outside every brush's box)
        const size_t c = (size_t)z * w + x;
        for (uint32_t k = start[c]; k < start[c + 1]; k++) if (inside(m->brushes[ids[k]], p)) return true;
        return false;
    }
};

Vec3 facePoint(const Face& f, float u, float v) {
    Vec3 p;
    p[f.axis] = f.plane;
    p[(f.axis + 1) % 3] = u;
    p[(f.axis + 2) % 3] = v;
    return p;
}

Vec3 faceNormal(const Face& f) {
    Vec3 n;
    n[f.axis] = (float)f.sign;
    return n;
}

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}

void basis(const Vec3& n, Vec3& t, Vec3& b) {
    t = std::fabs(n.y) < 0.9f ? normalize(cross(n, {0, 1, 0})) : normalize(cross(n, {1, 0, 0}));
    b = cross(n, t);
}

// Light caches are keyed by the map's content (cache/<sha256>.qlm): the same map under any name shares one, an
// edited map never picks up the bake of the version before, and there's nothing to invalidate.
std::string cachePath(const Map& m) { return userDataPath("cache") + "/" + lightCacheName(m); }
constexpr size_t kMaxCaches = 32;   // bakes kept (an editor's BAKE LIGHT after each change makes a new one): oldest go
uint32_t idWord(const Map& m) { const uint8_t* b = m.contentId().b; return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
std::string g_shippedDir;   // (setShippedLightCacheDir: tests)

bool headerOk(const uint32_t hdr[5], const Map& m, const BakedWorld& out) {
    return hdr[0] == kCacheMagic && hdr[1] == kBakeVersion && hdr[2] == idWord(m) &&
           (int)hdr[3] == out.atlasW && (int)hdr[4] == out.atlasH;
}

bool readCache(const std::string& path, const Map& m, BakedWorld& out) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) return false;
    uint32_t hdr[5];
    f.read((char*)hdr, sizeof hdr);
    if (!f || !headerOk(hdr, m, out)) return false;
    out.texels.resize((size_t)out.atlasW * out.atlasH * 2);
    f.read((char*)out.texels.data(), out.texels.size());
    return (bool)f;
}

// A shipped bake (.qlbz): a cache file's bytes as one zlib stream (tools/pack_lightcache.py; 84 MB of bakes ship as
// ~12), inflated by stb_image's decoder (Midtown: 2.7 -> 16.8 MB).
bool readShipped(const std::string& path, const Map& m, BakedWorld& out) {
    std::ifstream f(utf8Path(path), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff zn = f.tellg();
    const size_t want = sizeof(uint32_t) * 5 + (size_t)out.atlasW * out.atlasH * 2;
    if (zn <= 0 || zn > INT_MAX || want > INT_MAX) return false;
    std::vector<char> z((size_t)zn);
    f.seekg(0);
    if (!f.read(z.data(), zn)) return false;
    int n = 0;
    char* raw = stbi_zlib_decode_malloc_guesssize_headerflag(z.data(), (int)zn, (int)want, &n, 1);
    if (!raw) return false;
    uint32_t hdr[5];
    const bool ok = (size_t)n == want && (memcpy(hdr, raw, sizeof hdr), headerOk(hdr, m, out));
    if (ok) out.texels.assign(raw + sizeof hdr, raw + want);
    free(raw);   // (stb's allocator: malloc)
    return ok;
}

bool loadCache(const Map& m, BakedWorld& out) {
    if (readCache(cachePath(m), m, out)) {
        // (eviction goes by write time: a hit marks the bake used, so the maps being played keep theirs)
        std::error_code ec;
        std::filesystem::last_write_time(utf8Path(cachePath(m)), std::filesystem::file_time_type::clock::now(), ec);
        return true;
    }
    // the package's bake of an official map (read-only: the install may be, and an update replaces the folder)
    const std::string dir = g_shippedDir.empty() ? findDataPath("maps/lightcache") : g_shippedDir;
    return readShipped(dir + "/" + lightCacheName(m, true), m, out);
}

// written to a temporary file and renamed over the cache in one step (a crash or a full disk mid-write left a
// truncated cache that every later load turned down and baked again)
bool writeCache(const std::string& path, const Map& m, const BakedWorld& w) {
    std::error_code ec;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(utf8Path(tmp), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        uint32_t hdr[5] = {kCacheMagic, kBakeVersion, idWord(m), (uint32_t)w.atlasW, (uint32_t)w.atlasH};
        f.write((const char*)hdr, sizeof hdr);
        f.write((const char*)w.texels.data(), w.texels.size());
        f.flush();
        if (!f) { f.close(); std::filesystem::remove(utf8Path(tmp), ec); return false; }
    }
    std::filesystem::rename(utf8Path(tmp), utf8Path(path), ec);
    if (ec) { std::filesystem::remove(utf8Path(tmp), ec); return false; }
    return true;
}

void saveCache(const Map& m, const BakedWorld& w) {
    std::error_code ec;
    std::filesystem::create_directories(userDataPath("cache"), ec);
    if (!writeCache(cachePath(m), m, w)) return;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> all;
    for (auto& e : std::filesystem::directory_iterator(userDataPath("cache"), ec))
        if (e.path().extension() == ".qlm") all.push_back({e.last_write_time(ec), e.path()});
    if (all.size() <= kMaxCaches) return;
    std::sort(all.begin(), all.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (size_t i = kMaxCaches; i < all.size(); i++) std::filesystem::remove(all[i].second, ec);
}

// one core left for the game (the loading screen, a hosted server's thread, software GL's own threads); quark
// --bake-maps has nothing else to run and takes them all
std::atomic<bool> g_allCores{false};
unsigned bakeThreads() {
    unsigned hc = std::thread::hardware_concurrency();
    return g_allCores ? std::max(1u, hc) : std::max(1u, hc > 2 ? hc - 1 : hc);
}

// fn(i) for every i in [0, n), on the bake's thread count (the caller's thread is one of them), 256 at a time from a
// shared counter; false if stopped() turned true (checked once per chunk). Each fn(i) writes only its own slots, so
// the result is the same as a serial loop's.
template <class S, class F> bool parallelFor(size_t n, const S& stopped, const F& fn) {
    constexpr size_t kChunk = 256;
    unsigned nt = bakeThreads();
    nt = (unsigned)std::min<size_t>(nt, (n + kChunk - 1) / kChunk);
    std::atomic<size_t> next{0};
    std::atomic<bool> halted{false};
    auto run = [&] {
        for (;;) {
            const size_t b = next.fetch_add(kChunk, std::memory_order_relaxed);
            if (b >= n) return;
            if (stopped()) { halted = true; return; }
            for (size_t i = b, e = std::min(n, b + kChunk); i < e; i++) fn(i);
        }
    };
    std::vector<std::thread> threads;
    for (unsigned t = 1; t < nt; t++) threads.emplace_back(run);
    run();
    for (auto& th : threads) th.join();
    return !halted;
}
} // namespace

static std::atomic<int> g_maxTex{4096};
void setLightmapMaxTextureSize(int px) { g_maxTex = std::max(1024, px); }

float sunVisibility(const Map& map, const Vec3& p) {
    return anyHit(map, p, map.sunDir, 500.0f) ? 0.0f : 1.0f;
}

// The user's cache (then a shipped bake) is read first and a bake saved to it; with `file`, only that file is read and
// nothing is saved (quark --bake-maps: the caller writes it).
static void bake(const Map& map, BakedWorld& out, const std::function<void(float)>& progress, const std::atomic<bool>* cancel,
                 const std::string* file) {
    out = BakedWorld();
    // 1. Collect visible faces.
    std::vector<Face> faces;
    const PointGrid solidAt(map);
    auto stopped = [&] { return cancel && cancel->load(std::memory_order_relaxed); };   // (checked through the prepass too)
    // The prepass's three loops run on the bake's threads (110-180 ms of every load, a cached one too, on one core):
    // each brush's faces go to its own six slots and are gathered in brush order after, so the faces (and the atlas,
    // the mesh and the cache they key) are the same as a serial loop's.
    std::vector<Face> slots(map.brushes.size() * 6);
    std::vector<uint8_t> keep(slots.size(), 0);
    bool ran = parallelFor(map.brushes.size(), stopped, [&](size_t bi) {
        const Brush& br = map.brushes[bi];
        if (br.material == 9) return;   // clip: invisible
        for (int axis = 0; axis < 3; axis++)
            for (int sign = -1; sign <= 1; sign += 2) {
                Face f;
                f.axis = axis;
                f.sign = sign;
                f.material = br.material;
                f.plane = sign > 0 ? br.box.mx[axis] : br.box.mn[axis];
                int ua = (axis + 1) % 3, va = (axis + 2) % 3;
                f.u0 = br.box.mn[ua]; f.u1 = br.box.mx[ua];
                f.v0 = br.box.mn[va]; f.v1 = br.box.mx[va];
                if (f.u1 - f.u0 < 1e-4f || f.v1 - f.v0 < 1e-4f) continue;
                // Cull faces completely buried in other (visible) brushes.
                bool hidden = solidAt.buried(axis, f.plane + sign * 0.01f, f.u0, f.u1, f.v0, f.v1);
                if (!hidden) { const size_t s = bi * 6 + axis * 2 + (sign > 0); slots[s] = f; keep[s] = 1; }
            }
    });
    if (!ran) { out.cancelled = true; return; }
    for (size_t s = 0; s < slots.size(); s++) if (keep[s]) faces.push_back(slots[s]);
    // 1b. Outline edges: an edge shared flush with another face of the same material in the same plane is
    // not a real edge (a wall built from several boxes reads as one wall), so its outline is turned off.
    // (Piece by piece where a neighbour covers only part of an edge: see splits below.)
    // Both passes below only compare faces on the same side of the same plane, near each other: bucket faces by
    // (axis, sign, material, plane in mm, 8 m cell of the plane) so each lookup sees a handful of faces, not all
    // of them (this was O(faces^2): minutes on a big map). Planes match within 1e-4, so the plane slot either
    // side is checked too; huge faces (more than 32x32 cells) go in one list per plane instead of every cell.
    const float kBucket = 8.0f;
    auto cellOf = [&](float x) { return (int)std::floor(std::max(-8192.0f, std::min(8192.0f, x)) / kBucket); };
    auto planeOf = [](float p) { return (int64_t)std::floor((double)p * 1000.0); };
    auto key = [](int axis, int sign, int material, int64_t plane, int cu, int cv) {
        uint64_t k = (uint64_t)axis | (uint64_t)(sign > 0) << 2 | (uint64_t)(material & 15) << 3;
        k = k * 0x9E3779B97F4A7C15ull ^ (uint64_t)plane;
        k = k * 0x9E3779B97F4A7C15ull ^ (uint64_t)(uint32_t)cu;
        k = k * 0x9E3779B97F4A7C15ull ^ (uint64_t)(uint32_t)cv;
        return k;
    };
    const int kBig = 1 << 20; // cell coordinate of the per-plane "huge faces" list
    std::unordered_map<uint64_t, std::vector<uint32_t>> buckets;
    buckets.reserve(faces.size() * 2);
    for (size_t i = 0; i < faces.size(); i++) {
        const Face& f = faces[i];
        int cu0 = cellOf(f.u0), cu1 = cellOf(f.u1), cv0 = cellOf(f.v0), cv1 = cellOf(f.v1);
        int64_t pl = planeOf(f.plane);
        if ((int64_t)(cu1 - cu0 + 1) * (cv1 - cv0 + 1) > 32 * 32) { buckets[key(f.axis, f.sign, f.material, pl, kBig, kBig)].push_back((uint32_t)i); continue; }
        for (int cv = cv0; cv <= cv1; cv++)
            for (int cu = cu0; cu <= cu1; cu++) buckets[key(f.axis, f.sign, f.material, pl, cu, cv)].push_back((uint32_t)i);
    }
    // calls fn(k) for faces that might share f's plane with this material near cell (cu, cv); fn returns true to stop
    auto near = [&](const Face& f, int material, int cu, int cv, auto fn) {
        int64_t pl = planeOf(f.plane);
        for (int64_t p = pl - 1; p <= pl + 1; p++)
            for (int big = 0; big < 2; big++) {
                auto it = buckets.find(big ? key(f.axis, f.sign, material, p, kBig, kBig) : key(f.axis, f.sign, material, p, cu, cv));
                if (it == buckets.end()) continue;
                for (uint32_t k : it->second) if (fn(k)) return true;
            }
        return false;
    };
    // A neighbour can cover only part of an edge (a taller step beside a shorter one, a wall meeting a longer
    // wall): then the edge is split where the neighbours' edges cross it, and the face drawn as a grid of pieces, so
    // each piece of the border keeps or drops its outline on its own (the whole edge kept it before: a line ran
    // across the neighbour). Edges between a face's own pieces never have one.
    // Flush within 1 mm: parts of a scaled group (stored as fractions of its box) land a hair off each other's plane.
    const float kFlush = 1e-3f;
    struct Split { std::vector<float> bu, bv; std::vector<uint8_t> off; };   // off: per piece, bit 0 u0, 1 u1, 2 v0, 3 v1
    std::vector<Split> splits(faces.size());
    auto covered = [&](size_t self, float u, float v) {
        const Face& f = faces[self];
        return near(f, f.material, cellOf(u), cellOf(v), [&](uint32_t k) {
            const Face& o = faces[k];
            if (k == self || o.axis != f.axis || o.sign != f.sign || o.material != f.material || std::fabs(o.plane - f.plane) > kFlush) return false;
            return u > o.u0 && u < o.u1 && v > o.v0 && v < o.v1;
        });
    };
    ran = parallelFor(faces.size(), stopped, [&](size_t i) {
        const Face& f = faces[i];
        const float e = 0.02f, kMin = 0.01f;
        Split& sp = splits[i];
        sp.bu = {f.u0, f.u1}; sp.bv = {f.v0, f.v1};
        // where the neighbours along the border start and stop
        auto add = [&](uint32_t k) {
            const Face& o = faces[k];
            if (k == i || o.axis != f.axis || o.sign != f.sign || o.material != f.material || std::fabs(o.plane - f.plane) > kFlush) return false;
            if (!(o.u0 < f.u1 + e && o.u1 > f.u0 - e && o.v0 < f.v1 + e && o.v1 > f.v0 - e)) return false;
            for (float x : {o.u0, o.u1}) if (x > f.u0 + kMin && x < f.u1 - kMin) sp.bu.push_back(x);
            for (float x : {o.v0, o.v1}) if (x > f.v0 + kMin && x < f.v1 - kMin) sp.bv.push_back(x);
            return false;
        };
        const int cu0 = cellOf(f.u0 - e), cu1 = cellOf(f.u1 + e), cv0 = cellOf(f.v0 - e), cv1 = cellOf(f.v1 + e);
        auto row = [&](int cv) { for (int cu = cu0; cu <= cu1; cu++) near(f, f.material, cu, cv, add); };
        auto col = [&](int cu) { for (int cv = cv0; cv <= cv1; cv++) near(f, f.material, cu, cv, add); };
        if ((int64_t)(cu1 - cu0 + 1) * (cv1 - cv0 + 1) <= 64 * 64) {   // (the border's cells; a neighbour touching it is bucketed there)
            for (int cv : {cellOf(f.v0 - e), cellOf(f.v0 + e), cellOf(f.v1 - e), cellOf(f.v1 + e)}) row(cv);
            for (int cu : {cellOf(f.u0 - e), cellOf(f.u0 + e), cellOf(f.u1 - e), cellOf(f.u1 + e)}) col(cu);
        }
        auto tidy = [&](std::vector<float>& b) {
            std::sort(b.begin(), b.end());
            std::vector<float> o;
            for (float x : b) if (o.empty() || x - o.back() > kMin) o.push_back(x);
            o.back() = b.back();
            b.swap(o);
        };
        tidy(sp.bu); tidy(sp.bv);
        if ((sp.bu.size() - 1) * (sp.bv.size() - 1) > 1024) { sp.bu = {f.u0, f.u1}; sp.bv = {f.v0, f.v1}; }   // (a cap on the pieces: whole edges, as before)
        // each border piece: off when every sample just outside it is inside a neighbour
        auto seg = [&](int edge, float a, float b) {
            for (int k = 0; k < 5; k++) {
                float t = lerpf(a, b, (k + 0.5f) / 5.0f);
                float u = edge == 0 ? f.u0 - e : edge == 1 ? f.u1 + e : t;
                float v = edge == 2 ? f.v0 - e : edge == 3 ? f.v1 + e : t;
                if (!covered(i, u, v)) return false;
            }
            return true;
        };
        const int nu = (int)sp.bu.size() - 1, nv = (int)sp.bv.size() - 1;
        sp.off.assign((size_t)nu * nv, 0);
        for (int b = 0; b < nv; b++)
            for (int a = 0; a < nu; a++) {
                uint8_t o = 0;
                if (a > 0 || seg(0, sp.bv[b], sp.bv[b + 1])) o |= 1;
                if (a < nu - 1 || seg(1, sp.bv[b], sp.bv[b + 1])) o |= 2;
                if (b > 0 || seg(2, sp.bu[a], sp.bu[a + 1])) o |= 4;
                if (b < nv - 1 || seg(3, sp.bu[a], sp.bu[a + 1])) o |= 8;
                sp.off[(size_t)b * nu + a] = o;
            }
    });
    if (!ran) { out.cancelled = true; return; }
    // 1c. Coplanar faces of different materials overlapping would z-fight: the higher material (wallbang >
    // accent > base) gets lifted a hair off its plane, so decorations can sit flush on gameplay geometry.
    // Only those faces: lifting every accent/wallbang face opened gaps along all their edges.
    std::vector<uint8_t> needLift(faces.size(), 0);
    ran = parallelFor(faces.size(), stopped, [&](size_t i) {
        const Face& f = faces[i];
        if (f.material == 0) return;
        auto overlaps = [&](uint32_t k) {
            const Face& o = faces[k];
            if (o.material >= f.material || o.axis != f.axis || o.sign != f.sign || std::fabs(o.plane - f.plane) > 1e-4f) return false;
            return f.u0 < o.u1 && o.u0 < f.u1 && f.v0 < o.v1 && o.v0 < f.v1;
        };
        int cu0 = cellOf(f.u0), cu1 = cellOf(f.u1), cv0 = cellOf(f.v0), cv1 = cellOf(f.v1);
        bool hit = false;
        for (int mat = 0; mat < f.material && !hit; mat++) {
            if ((int64_t)(cu1 - cu0 + 1) * (cv1 - cv0 + 1) > 32 * 32) { // a huge face: just scan everything on its plane
                for (size_t k = 0; k < faces.size() && !hit; k++) hit = overlaps((uint32_t)k);
                break;
            }
            for (int cv = cv0; cv <= cv1 && !hit; cv++)
                for (int cu = cu0; cu <= cu1 && !hit; cu++) hit = near(f, mat, cu, cv, overlaps);
        }
        needLift[i] = hit;
    });
    if (!ran) { out.cancelled = true; return; }
    // 2. Choose texel density so the atlas fits in 2048x2048.
    float area = 0;
    for (auto& f : faces) if (f.material < 4 || f.material == kMatNoGrapple) area += (f.u1 - f.u0) * (f.v1 - f.v0); // glow / far faces ignore lighting
    float density = 4.0f;
    while (area * density * density > 2048.0f * 2048.0f * 0.55f && density > 0.25f) density *= 0.8f;
    auto sizeFaces = [&] {
        for (auto& f : faces) {
            f.wu = std::max(2, (int)std::ceil((f.u1 - f.u0) * density));
            f.wv = std::max(2, (int)std::ceil((f.v1 - f.v0) * density));
            f.wu = std::min(f.wu, 1000);
            f.wv = std::min(f.wv, 1000);
            if ((f.material >= 4 && f.material < 8) || f.material == kMatAcid) f.wu = f.wv = 2;   // (unlit: the shader skips the lightmap)
        }
    };
    sizeFaces();
    // 3. Shelf-pack, W wide; returns the (power of two) height.
    auto pack = [&](int W) {
        std::vector<int> order(faces.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return faces[a].wv > faces[b].wv; });
        int x = 0, y = 0, rowH = 0;
        for (int i : order) {
            Face& f = faces[i];
            int w = f.wu + 2, h = f.wv + 2;
            if (x + w > W) { x = 0; y += rowH; rowH = 0; }
            f.rx = x; f.ry = y;
            x += w;
            rowH = std::max(rowH, h);
        }
        int H = 1;
        while (H < y + rowH) H *= 2;
        return H;
    };
    // It must fit the GPU's largest texture (PERF-13: Midtown is 2048x4096 already, and some GPUs stop at 4096): wider
    // first (up to that size), then fewer texels per metre, rather than an atlas the driver refuses (a black world).
    const int maxTex = g_maxTex.load();
    int W = std::min(2048, maxTex), H = pack(W);
    while (H > maxTex && density > 0.02f) {
        if (W < maxTex) W *= 2;
        else { density *= 0.8f; sizeFaces(); W = std::min(2048, maxTex); }
        H = pack(W);
    }
    out.atlasW = W;
    out.atlasH = H;

    // 4. Geometry.
    out.verts.reserve(faces.size() * 6);
    for (size_t fi = 0; fi < faces.size(); fi++) {
        const Face& f = faces[fi];
        Vec3 n = faceNormal(f);
        Vec3 lift = needLift[fi] ? n * (0.002f * f.material) : Vec3();
        const float B = 1000.0f; // a turned-off edge is "infinitely far": no outline
        const Split& sp = splits[fi];
        const int nu = (int)sp.bu.size() - 1, nv = (int)sp.bv.size() - 1;
        for (int pb = 0; pb < nv; pb++)
            for (int pa = 0; pa < nu; pa++) {
                const float pu0 = sp.bu[pa], pu1 = sp.bu[pa + 1], pv0 = sp.bv[pb], pv1 = sp.bv[pb + 1];
                const uint8_t off = sp.off[(size_t)pb * nu + pa];
                auto vert = [&](float u, float v) {
                    WorldVertex wv;
                    Vec3 p = facePoint(f, u, v) + lift;
                    memcpy(wv.pos, &p.x, 12);
                    memcpy(wv.normal, &n.x, 12);
                    wv.uv[0] = (f.rx + 1 + (u - f.u0) / (f.u1 - f.u0) * f.wu) / (float)W;
                    wv.uv[1] = (f.ry + 1 + (v - f.v0) / (f.v1 - f.v0) * f.wv) / (float)H;
                    wv.material = (float)f.material;
                    wv.edge[0] = u - pu0; wv.edge[1] = v - pv0; wv.edge[2] = pu1 - pu0; wv.edge[3] = pv1 - pv0;
                    // outline distance = min(edge.xy, edge.zw - edge.xy): push an edge away to hide its outline
                    if (off & 1) { wv.edge[0] += B; wv.edge[2] += B; }
                    if (off & 2) wv.edge[2] += B;
                    if (off & 4) { wv.edge[1] += B; wv.edge[3] += B; }
                    if (off & 8) wv.edge[3] += B;
                    return wv;
                };
                WorldVertex a = vert(pu0, pv0), b = vert(pu1, pv0), c = vert(pu1, pv1), d = vert(pu0, pv1);
                if (f.sign > 0) { out.verts.insert(out.verts.end(), {a, b, c, a, c, d}); }
                else { out.verts.insert(out.verts.end(), {a, c, b, a, d, c}); }
            }
    }

    if (file ? readCache(*file, map, out) : loadCache(map, out)) { out.fromCache = true; if (progress) progress(1.0f); return; }

    // 5. Bake, multithreaded over faces.
    out.texels.assign((size_t)W * H * 2, 0);
    const Vec3 sun = map.sunDir;
    Vec3 st, sb;
    basis(sun, st, sb);
    std::atomic<int> next{0}, doneCount{0};
    // biggest faces first: handed out in index order, a big floor picked up last ran on one thread while the rest
    // waited (the result is the same: each texel's samples depend only on its face index and position)
    std::vector<int> order(faces.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return faces[a].wu * faces[a].wv > faces[b].wu * faces[b].wv; });
    auto worker = [&]() {
        for (;;) {
            int oi = next++;
            if (oi >= (int)faces.size()) break;
            const int fi = order[oi];
            if (cancel && cancel->load(std::memory_order_relaxed)) { doneCount++; continue; }   // (stopped: count the rest done)
            const Face& f = faces[fi];
            Vec3 n = faceNormal(f);
            Vec3 t, b;
            basis(n, t, b);
            for (int j = 0; j < f.wv; j++)
                for (int i = 0; i < f.wu; i++) {
                    float u = lerpf(f.u0, f.u1, (i + 0.5f) / f.wu);
                    float v = lerpf(f.v0, f.v1, (j + 0.5f) / f.wv);
                    Vec3 p = facePoint(f, u, v) + n * 0.03f;
                    uint32_t h = hash32((uint32_t)fi * 7919u + (uint32_t)(j * 1031 + i));
                    // direct sun with a small cone for soft shadow edges
                    float ndl = std::max(0.0f, dot(n, sun));
                    float direct = 0;
                    if (ndl > 0) {
                        int vis = 0;
                        for (int k = 0; k < kSunSamples; k++) {
                            float a = (k + (h & 255) / 256.0f) * (2 * PI / kSunSamples);
                            float r = 0.03f * ((k % 2) + 0.5f);
                            Vec3 d = normalize(sun + st * (std::cos(a) * r) + sb * (std::sin(a) * r));
                            vis += !anyHit(map, p, d, 500.0f);
                        }
                        direct = ndl * vis / (float)kSunSamples;
                    }
                    // sky + one bounce, cosine-weighted hemisphere
                    float sky = 0, bounce = 0;
                    float rot = (h >> 8) / 16777216.0f;
                    for (int k = 0; k < kSkySamples; k++) {
                        float r1 = (k + 0.5f) / kSkySamples;
                        float r2 = std::fmod(k * 0.618034f + rot, 1.0f);
                        float r = std::sqrt(r1), phi = 2 * PI * r2;
                        Vec3 d = normalize(t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1 - r1)));
                        Vec3 hn;
                        float hit = map.raycast(p, d, 200.0f, &hn);
                        if (hit < 0) { sky += 1.0f; continue; }
                        Vec3 hp = p + d * hit + hn * 0.03f;
                        float hndl = std::max(0.0f, dot(hn, sun));
                        if (hndl > 0 && !anyHit(map, hp, sun, 500.0f)) bounce += kAlbedo * hndl;
                        else bounce += kAlbedo * 0.15f; // weak sky light on the bounced surface
                    }
                    float g = (sky + bounce * 0.9f) / kSkySamples;
                    // baked point lights (map "light" entries): soft falloff, shadowed by the world
                    for (const Map::Light& L : map.lights) {
                        Vec3 to = L.pos - p;
                        float dist = length(to);
                        if (dist >= L.radius || dist < 1e-3f) continue;
                        Vec3 dir = to / dist;
                        float ndl = dot(n, dir);
                        if (ndl <= 0 || anyHit(map, p, dir, dist - 0.05f)) continue;
                        float fall = 1.0f - dist / L.radius;
                        g += L.intensity * 1.1f * ndl * fall * fall;   // (10% up: indoor rooms read a little dark)
                    }
                    size_t idx = ((size_t)(f.ry + 1 + j) * W + (f.rx + 1 + i)) * 2;
                    out.texels[idx] = (uint8_t)(clampf(direct, 0, 1) * 255.0f + 0.5f);
                    out.texels[idx + 1] = (uint8_t)(clampf(g, 0, 1) * 255.0f + 0.5f);
                }
            // pad the border by copying edge texels so bilinear filtering never bleeds
            auto at = [&](int xx, int yy) -> uint8_t* { return &out.texels[((size_t)yy * W + xx) * 2]; };
            for (int j = 0; j < f.wv + 2; j++)
                for (int i = 0; i < f.wu + 2; i++) {
                    if (i > 0 && j > 0 && i <= f.wu && j <= f.wv) continue;
                    int si = std::min(std::max(i, 1), f.wu), sj = std::min(std::max(j, 1), f.wv);
                    memcpy(at(f.rx + i, f.ry + j), at(f.rx + si, f.ry + sj), 2);
                }
            doneCount++;
        }
    };
    const unsigned nt = bakeThreads();
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < nt; i++) threads.emplace_back(worker);
    if (progress) {
        while (doneCount < (int)faces.size()) {
            progress(faces.empty() ? 1.0f : doneCount / (float)faces.size());
            // ~30 ms between updates, in 1 ms steps that stop once the work is done (one 30 ms sleep held a finished
            // bake up to 30 ms; MinGW's sleep_for ignores the 1 ms timer period, sleepMs uses Sleep)
            for (int ms = 0; ms < 30 && doneCount < (int)faces.size(); ms++) sleepMs(1);
        }
    }
    for (auto& th : threads) th.join();
    if (cancel && cancel->load()) { out.cancelled = true; return; }
    if (!file) saveCache(map, out);
    if (progress) progress(1.0f);
}

void buildWorld(const Map& map, BakedWorld& out, const std::function<void(float)>& progress, const std::atomic<bool>* cancel) {
    bake(map, out, progress, cancel, nullptr);
}

std::string lightCacheName(const Map& map, bool shipped) { return map.contentId().hex() + (shipped ? ".qlbz" : ".qlm"); }
void setShippedLightCacheDir(const std::string& dir) { g_shippedDir = dir; }

bool bakeLightCacheFile(const Map& map, const std::string& path, bool* kept) {
    BakedWorld w;
    bake(map, w, nullptr, nullptr, &path);
    if (kept) *kept = w.fromCache;
    return w.fromCache || writeCache(path, map, w);
}

int bakeOfficialMaps(const std::string& dir) {
    namespace fs = std::filesystem;
    if (dir.empty()) { fprintf(stderr, "quark --bake-maps <dir>\n"); return 2; }
    g_allCores = true;
    // the atlas is packed to fit the GPU's largest texture (PERF-13): baked for 16384, what current GPUs (and
    // software GL) offer; a map that only fits a 4096 GPU differently is baked there, as before
    setLightmapMaxTextureSize(16384);
    std::error_code ec;
    fs::create_directories(utf8Path(dir), ec);
    std::vector<std::string> made;
    int failed = 0;
    const double t0 = timeNow();
    for (auto& [name, text] : officialMapTexts()) {
        Map m;
        m.name = name;
        std::string err;
        if (!m.parse(text, &err)) { fprintf(stderr, "%s: %s\n", name.c_str(), err.c_str()); failed++; continue; }
        m.dynSolids.clear();   // (as the client bakes a joined map: no payload cart box)
        const std::string file = m.contentId().hex() + ".qlb", path = dir + "/" + file;   // (raw: pack_lightcache.py compresses)
        made.push_back(file);
        const double t = timeNow();
        bool kept = false;
        if (!bakeLightCacheFile(m, path, &kept)) { fprintf(stderr, "%s: can't write %s\n", name.c_str(), path.c_str()); failed++; continue; }
        printf("%-12s %s %.2f s\n", name.c_str(), kept ? "kept " : "baked", timeNow() - t);
        fflush(stdout);
    }
    // anything else there is an older bake: dropped, so the folder holds exactly this build's maps
    for (auto& e : fs::directory_iterator(utf8Path(dir), ec))
        if (e.path().extension() == ".qlb" && std::find(made.begin(), made.end(), e.path().filename().u8string()) == made.end())
            fs::remove(e.path(), ec);
    printf("%d maps, %.1f s, %u threads%s\n", (int)made.size(), timeNow() - t0, bakeThreads(), failed ? ", FAILED" : "");
    return failed ? 1 : 0;
}
