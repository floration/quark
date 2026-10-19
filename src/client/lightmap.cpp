#include "lightmap.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>

namespace {
constexpr uint32_t kCacheMagic = 0x314D4C51; // "QLM1"
constexpr uint32_t kBakeVersion = 4;
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

bool anyHit(const Map& m, const Vec3& o, const Vec3& d, float maxT) {
    Vec3 inv = safeInv(d);
    for (const Brush& b : m.brushes)
        if (rayAABB(o, inv, b.box, maxT) >= 0) return true;
    return false;
}

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

std::string cachePath(const Map& m) { return findDataPath("maps") + "/" + m.name + ".qlm"; }

bool loadCache(const Map& m, BakedWorld& out) {
    std::ifstream f(cachePath(m), std::ios::binary);
    if (!f) return false;
    uint32_t hdr[5];
    f.read((char*)hdr, sizeof hdr);
    if (!f || hdr[0] != kCacheMagic || hdr[1] != kBakeVersion || hdr[2] != m.crc ||
        (int)hdr[3] != out.atlasW || (int)hdr[4] != out.atlasH) return false;
    out.texels.resize((size_t)out.atlasW * out.atlasH * 2);
    f.read((char*)out.texels.data(), out.texels.size());
    return (bool)f;
}

void saveCache(const Map& m, const BakedWorld& w) {
    std::ofstream f(cachePath(m), std::ios::binary);
    if (!f) return;
    uint32_t hdr[5] = {kCacheMagic, kBakeVersion, m.crc, (uint32_t)w.atlasW, (uint32_t)w.atlasH};
    f.write((const char*)hdr, sizeof hdr);
    f.write((const char*)w.texels.data(), w.texels.size());
}
} // namespace

float sunVisibility(const Map& map, const Vec3& p) {
    return anyHit(map, p, map.sunDir, 500.0f) ? 0.0f : 1.0f;
}

void buildWorld(const Map& map, BakedWorld& out, const std::function<void(float)>& progress) {
    out = BakedWorld();
    // 1. Collect visible faces.
    std::vector<Face> faces;
    for (const Brush& br : map.brushes) {
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
                bool hidden = true;
                const float fu[5] = {0.5f, 0.05f, 0.95f, 0.05f, 0.95f}, fv[5] = {0.5f, 0.05f, 0.05f, 0.95f, 0.95f};
                for (int k = 0; k < 5 && hidden; k++) {
                    Vec3 p = facePoint(f, lerpf(f.u0, f.u1, fu[k]), lerpf(f.v0, f.v1, fv[k])) + n * 0.01f;
                    hidden = pointSolid(map, p);
                }
                if (!hidden) faces.push_back(f);
            }
    }
    // 2. Choose texel density so the atlas fits in 2048x2048.
    float area = 0;
    for (auto& f : faces) area += (f.u1 - f.u0) * (f.v1 - f.v0);
    float density = 4.0f;
    while (area * density * density > 2048.0f * 2048.0f * 0.55f && density > 0.25f) density *= 0.8f;
    for (auto& f : faces) {
        f.wu = std::max(2, (int)std::ceil((f.u1 - f.u0) * density));
        f.wv = std::max(2, (int)std::ceil((f.v1 - f.v0) * density));
        f.wu = std::min(f.wu, 1000);
        f.wv = std::min(f.wv, 1000);
    }
    // 3. Shelf-pack.
    std::vector<int> order(faces.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return faces[a].wv > faces[b].wv; });
    const int W = 2048;
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
    out.atlasW = W;
    out.atlasH = H;

    // 4. Geometry.
    out.verts.reserve(faces.size() * 6);
    for (auto& f : faces) {
        Vec3 n = faceNormal(f);
        auto vert = [&](float u, float v) {
            WorldVertex wv;
            Vec3 p = facePoint(f, u, v);
            memcpy(wv.pos, &p.x, 12);
            memcpy(wv.normal, &n.x, 12);
            wv.uv[0] = (f.rx + 1 + (u - f.u0) / (f.u1 - f.u0) * f.wu) / (float)W;
            wv.uv[1] = (f.ry + 1 + (v - f.v0) / (f.v1 - f.v0) * f.wv) / (float)H;
            wv.material = (float)f.material;
            wv.edge[0] = u - f.u0; wv.edge[1] = v - f.v0; wv.edge[2] = f.u1 - f.u0; wv.edge[3] = f.v1 - f.v0;
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
    saveCache(map, out);
    if (progress) progress(1.0f);
}
