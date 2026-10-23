#include "lightmap.h"
#include "../common/u8file.h"
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>

namespace {
constexpr uint32_t kCacheMagic = 0x324D4C51; // "QLM2": named by the map's sha256 (QLM1: by its name, with its CRC)
constexpr uint32_t kBakeVersion = 11;   // 11: sun and bounce rays pass clip // 10: point lights 10% brighter // 6: point lights
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

bool pointSolid(const Map& m, const Vec3& p) {
    for (const Brush& b : m.brushes)
        if (p.x > b.box.mn.x && p.x < b.box.mx.x && p.y > b.box.mn.y && p.y < b.box.mx.y && p.z > b.box.mn.z && p.z < b.box.mx.z)
            return true;
    return false;
}

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
std::string cachePath(const Map& m) { return userDataPath("cache") + "/" + m.contentId().hex() + ".qlm"; }
constexpr size_t kMaxCaches = 32;   // bakes kept (an editor's BAKE LIGHT after each change makes a new one): oldest go
uint32_t idWord(const Map& m) { const uint8_t* b = m.contentId().b; return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

bool loadCache(const Map& m, BakedWorld& out) {
    std::ifstream f(utf8Path(cachePath(m)), std::ios::binary);
    if (!f) return false;
    uint32_t hdr[5];
    f.read((char*)hdr, sizeof hdr);
    if (!f || hdr[0] != kCacheMagic || hdr[1] != kBakeVersion || hdr[2] != idWord(m) ||
        (int)hdr[3] != out.atlasW || (int)hdr[4] != out.atlasH) return false;
    out.texels.resize((size_t)out.atlasW * out.atlasH * 2);
    f.read((char*)out.texels.data(), out.texels.size());
    return (bool)f;
}

void saveCache(const Map& m, const BakedWorld& w) {
    std::error_code ec;
    std::filesystem::create_directories(userDataPath("cache"), ec);
    {
        std::ofstream f(utf8Path(cachePath(m)), std::ios::binary);
        if (!f) return;
        uint32_t hdr[5] = {kCacheMagic, kBakeVersion, idWord(m), (uint32_t)w.atlasW, (uint32_t)w.atlasH};
        f.write((const char*)hdr, sizeof hdr);
        f.write((const char*)w.texels.data(), w.texels.size());
    }
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> all;
    for (auto& e : std::filesystem::directory_iterator(userDataPath("cache"), ec))
        if (e.path().extension() == ".qlm") all.push_back({e.last_write_time(ec), e.path()});
    if (all.size() <= kMaxCaches) return;
    std::sort(all.begin(), all.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (size_t i = kMaxCaches; i < all.size(); i++) std::filesystem::remove(all[i].second, ec);
}
} // namespace

static std::atomic<int> g_maxTex{4096};
void setLightmapMaxTextureSize(int px) { g_maxTex = std::max(1024, px); }

float sunVisibility(const Map& map, const Vec3& p) {
    return anyHit(map, p, map.sunDir, 500.0f) ? 0.0f : 1.0f;
}

void buildWorld(const Map& map, BakedWorld& out, const std::function<void(float)>& progress, const std::atomic<bool>* cancel) {
    out = BakedWorld();
    // 1. Collect visible faces.
    std::vector<Face> faces;
    for (const Brush& br : map.brushes) {
        if (br.material == 9) continue;   // clip: invisible
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
                // Cull faces completely buried in other solids.
                Vec3 n = faceNormal(f);
                // Buried only if a 9x9 grid over the face is all inside solids (5 samples used to be fooled:
                // corner columns + something in the middle hid entire floors).
                bool hidden = true;
                for (int k = 0; k < 81 && hidden; k++) {
                    float fu = (k % 9 + 0.5f) / 9.0f, fv = (k / 9 + 0.5f) / 9.0f;
                    Vec3 p = facePoint(f, lerpf(f.u0, f.u1, fu), lerpf(f.v0, f.v1, fv)) + n * 0.01f;
                    hidden = pointSolid(map, p);
                }
                if (!hidden) faces.push_back(f);
            }
    }
    // 1b. Outline edges: an edge shared flush with another face of the same material in the same plane is
    // not a real edge (a wall built from several boxes reads as one wall), so its outline is turned off.
    // edgeOff[f] bit 0: u0 edge, 1: u1, 2: v0, 3: v1.
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
    std::vector<uint8_t> edgeOff(faces.size(), 0);
    auto covered = [&](size_t self, float u, float v) {
        const Face& f = faces[self];
        return near(f, f.material, cellOf(u), cellOf(v), [&](uint32_t k) {
            const Face& o = faces[k];
            if (k == self || o.axis != f.axis || o.sign != f.sign || o.material != f.material || std::fabs(o.plane - f.plane) > 1e-4f) return false;
            return u > o.u0 && u < o.u1 && v > o.v0 && v < o.v1;
        });
    };
    for (size_t i = 0; i < faces.size(); i++) {
        const Face& f = faces[i];
        const float e = 0.02f;
        for (int edge = 0; edge < 4; edge++) {
            bool all = true;
            for (int k = 0; k < 7 && all; k++) {
                float t = (k + 0.5f) / 7.0f;
                float u = edge == 0 ? f.u0 - e : edge == 1 ? f.u1 + e : lerpf(f.u0, f.u1, t);
                float v = edge == 2 ? f.v0 - e : edge == 3 ? f.v1 + e : lerpf(f.v0, f.v1, t);
                all = covered(i, u, v);
            }
            if (all) edgeOff[i] |= 1 << edge;
        }
    }
    // 1c. Coplanar faces of different materials overlapping would z-fight: the higher material (wallbang >
    // accent > base) gets lifted a hair off its plane, so decorations can sit flush on gameplay geometry.
    // Only those faces: lifting every accent/wallbang face opened gaps along all their edges.
    std::vector<uint8_t> needLift(faces.size(), 0);
    for (size_t i = 0; i < faces.size(); i++) {
        const Face& f = faces[i];
        if (f.material == 0) continue;
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
    }
    // 2. Choose texel density so the atlas fits in 2048x2048.
    float area = 0;
    for (auto& f : faces) if (f.material < 4) area += (f.u1 - f.u0) * (f.v1 - f.v0); // glow / far faces ignore lighting
    float density = 4.0f;
    while (area * density * density > 2048.0f * 2048.0f * 0.55f && density > 0.25f) density *= 0.8f;
    auto sizeFaces = [&] {
        for (auto& f : faces) {
            f.wu = std::max(2, (int)std::ceil((f.u1 - f.u0) * density));
            f.wv = std::max(2, (int)std::ceil((f.v1 - f.v0) * density));
            f.wu = std::min(f.wu, 1000);
            f.wv = std::min(f.wv, 1000);
            if (f.material >= 4 && f.material < 8) f.wu = f.wv = 2;
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
        uint8_t off = edgeOff[fi];
        auto vert = [&](float u, float v) {
            WorldVertex wv;
            Vec3 p = facePoint(f, u, v) + lift;
            memcpy(wv.pos, &p.x, 12);
            memcpy(wv.normal, &n.x, 12);
            wv.uv[0] = (f.rx + 1 + (u - f.u0) / (f.u1 - f.u0) * f.wu) / (float)W;
            wv.uv[1] = (f.ry + 1 + (v - f.v0) / (f.v1 - f.v0) * f.wv) / (float)H;
            wv.material = (float)f.material;
            wv.edge[0] = u - f.u0; wv.edge[1] = v - f.v0; wv.edge[2] = f.u1 - f.u0; wv.edge[3] = f.v1 - f.v0;
            // outline distance = min(edge.xy, edge.zw - edge.xy): push an edge away to hide its outline
            if (off & 1) { wv.edge[0] += B; wv.edge[2] += B; }
            if (off & 2) wv.edge[2] += B;
            if (off & 4) { wv.edge[1] += B; wv.edge[3] += B; }
            if (off & 8) wv.edge[3] += B;
            return wv;
        };
        WorldVertex a = vert(f.u0, f.v0), b = vert(f.u1, f.v0), c = vert(f.u1, f.v1), d = vert(f.u0, f.v1);
        if (f.sign > 0) { out.verts.insert(out.verts.end(), {a, b, c, a, c, d}); }
        else { out.verts.insert(out.verts.end(), {a, c, b, a, d, c}); }
    }

    if (loadCache(map, out)) { out.fromCache = true; if (progress) progress(1.0f); return; }

    // 5. Bake, multithreaded over faces.
    out.texels.assign((size_t)W * H * 2, 0);
    const Vec3 sun = map.sunDir;
    Vec3 st, sb;
    basis(sun, st, sb);
    std::atomic<int> next{0}, doneCount{0};
    auto worker = [&]() {
        for (;;) {
            int fi = next++;
            if (fi >= (int)faces.size()) break;
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
    unsigned nt = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < nt; i++) threads.emplace_back(worker);
    if (progress) {
        while (doneCount < (int)faces.size()) {
            progress(faces.empty() ? 1.0f : doneCount / (float)faces.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }
    for (auto& th : threads) th.join();
    if (cancel && cancel->load()) { out.cancelled = true; return; }
    saveCache(map, out);
    if (progress) progress(1.0f);
}
