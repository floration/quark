// The map previews (mappreview.h)
#include "mappreview.h"
#include <algorithm>
#include <cmath>
#include "../common/qmath.h"
#include "../game/map.h"

// A map's preview for the picker: seen from above, each column of the map the colour of its highest surface (the roof
// of an indoor map and anything covering most of it left out, so the rooms show), lit by its height and a light from
// the top left, edges where the height steps. Rows bottom-up (the renderer's atlas). About a millisecond a map, once
// per map file (cached with its info).
std::vector<uint8_t> makeMapPreview(const Map& m, const int W, const int H) {
    std::vector<uint8_t> img((size_t)W * H * 4);
    std::vector<float> top((size_t)W * H, -1e9f);
    std::vector<Vec3> col((size_t)W * H, Vec3(0.07f, 0.08f, 0.1f));
    const AABB& b = m.bounds;
    const float sx = b.mx.x - b.mn.x, sz = b.mx.z - b.mn.z;
    if (sx <= 0 || sz <= 0) return img;
    const float scale = std::min((W - 8) / sx, (H - 8) / sz), ox = (W - sx * scale) * 0.5f, oz = (H - sz * scale) * 0.5f;
    const Vec3 base = m.hasPalette ? m.palBase : Vec3(0.62f, 0.62f, 0.64f), accent = m.hasPalette ? m.palAccent : Vec3(0.40f, 0.40f, 0.43f),
               metal = m.hasPalette ? m.palMetal : Vec3(0.30f, 0.33f, 0.37f), glow = m.hasPalette ? m.palGlow : Vec3(1.0f, 0.55f, 0.15f);
    auto matColor = [&](int mat) -> Vec3 {
        switch (mat) {
            case 1: case 8: return accent;
            case 2: return Vec3(0.25f, 0.25f, 0.27f);
            case 3: case kMatSteel: case kMatDiamond: case kMatCorrugated: return metal;
            case 4: case kMatCrystal: return glow;
            case 6: return Vec3(0.15f, 0.35f, 0.6f);
            case 7: return Vec3(1.0f, 0.35f, 0.05f);
            case kMatAcid: return Vec3(0.35f, 0.9f, 0.2f);
            case kMatIce: case kMatSnow: return Vec3(0.8f, 0.88f, 0.95f);
            case kMatSand: return Vec3(0.78f, 0.68f, 0.45f);
            case kMatWood: return Vec3(0.55f, 0.38f, 0.22f);
            case kMatGrass: return Vec3(0.3f, 0.55f, 0.22f);
            case kMatBrick: case kMatBrickBreak: case kMatTerracotta: return Vec3(0.6f, 0.28f, 0.2f);
            case kMatGlass: case kMatGlassBreak: return Vec3(0.6f, 0.75f, 0.8f);
            case kMatAsphalt: return Vec3(0.2f, 0.2f, 0.22f);
            case kMatMud: return Vec3(0.35f, 0.27f, 0.18f);
            case kMatRust: return Vec3(0.5f, 0.27f, 0.15f);
            case kMatHazard: return Vec3(0.85f, 0.7f, 0.1f);
            default: return base;
        }
    };
    const float area = sx * sz;
    for (const Brush& br : m.brushes) {
        if (hiddenMaterial(br.material) || br.material == 5) continue;   // (clip, ball wall: invisible; far: scenery outside the walls)
        const AABB& a = br.box;
        if (m.indoor && a.mn.y >= m.ceilingY - 0.5f) continue;
        if ((a.mx.x - a.mn.x) * (a.mx.z - a.mn.z) > area * 0.5f && a.mn.y > b.mn.y + 2.0f) continue;   // (a roof over most of it)
        const int x0 = std::max(0, (int)std::floor(ox + (a.mn.x - b.mn.x) * scale)), x1 = std::min(W - 1, (int)std::ceil(ox + (a.mx.x - b.mn.x) * scale) - 1);
        const int z0 = std::max(0, (int)std::floor(oz + (a.mn.z - b.mn.z) * scale)), z1 = std::min(H - 1, (int)std::ceil(oz + (a.mx.z - b.mn.z) * scale) - 1);
        const Vec3 c = matColor(br.material);
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) {
                const size_t k = (size_t)z * W + x;
                if (a.mx.y > top[k]) { top[k] = a.mx.y; col[k] = c; }
            }
    }
    const float lo = b.mn.y, span = std::max(1.0f, b.mx.y - b.mn.y);
    for (int z = 0; z < H; z++)
        for (int x = 0; x < W; x++) {
            const size_t k = (size_t)z * W + x;
            Vec3 c = col[k];
            if (top[k] > -1e8f) {
                const float h = top[k];
                const float hl = x > 0 && top[k - 1] > -1e8f ? top[k - 1] : h, hu = z > 0 && top[k - W] > -1e8f ? top[k - W] : h;
                float lit = 0.55f + 0.45f * clampf((h - lo) / span, 0, 1);
                lit += clampf((h - hl + h - hu) * 0.08f, -0.25f, 0.25f);   // (the light from the top left)
                if (std::fabs(h - hl) > 0.6f || std::fabs(h - hu) > 0.6f) lit *= 0.6f;   // (a step: an edge)
                c = c * lit;
            }
            uint8_t* p = &img[((size_t)(H - 1 - z) * W + x) * 4];
            p[0] = (uint8_t)(clampf(c.x, 0, 1) * 255); p[1] = (uint8_t)(clampf(c.y, 0, 1) * 255); p[2] = (uint8_t)(clampf(c.z, 0, 1) * 255); p[3] = 255;
        }
    return img;
}

namespace {
uint32_t crc32Png(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c;
}
void put32(std::string& o, uint32_t v) { o += (char)(v >> 24); o += (char)(v >> 16); o += (char)(v >> 8); o += (char)v; }
void pngChunk(std::string& out, const char* type, const std::string& data) {
    put32(out, (uint32_t)data.size());
    std::string td(type, 4);
    td += data;
    out += td;
    put32(out, crc32Png((const uint8_t*)td.data(), td.size()) ^ 0xFFFFFFFFu);
}
}   // namespace

std::string encodePreviewPng(const std::vector<uint8_t>& img, int W, int H) {
    if (W <= 0 || H <= 0 || img.size() != (size_t)W * H * 4) return std::string();
    std::string raw;   // (each row: filter 0, then its RGB)
    raw.reserve((size_t)H * (W * 3 + 1));
    for (int r = 0; r < H; r++) {
        raw += '\0';
        const uint8_t* row = &img[(size_t)(H - 1 - r) * W * 4];   // (bottom-up in, top row first out)
        for (int x = 0; x < W; x++) { raw += (char)row[x * 4]; raw += (char)row[x * 4 + 1]; raw += (char)row[x * 4 + 2]; }
    }
    std::string z = "\x78\x01";   // zlib, stored blocks of at most 65535 bytes, then the Adler-32
    for (size_t at = 0; at < raw.size() || at == 0; ) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        z += (char)(at + n >= raw.size() ? 1 : 0);
        z += (char)(n & 0xFF); z += (char)(n >> 8); z += (char)(~n & 0xFF); z += (char)((~n >> 8) & 0xFF);
        z.append(raw, at, n);
        at += n;
        if (n == 0) break;
    }
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32(z, b << 16 | a);
    std::string out = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    put32(ihdr, (uint32_t)W); put32(ihdr, (uint32_t)H);
    ihdr += (char)8; ihdr += (char)2; ihdr += (char)0; ihdr += (char)0; ihdr += (char)0;   // (8-bit RGB, no interlace)
    pngChunk(out, "IHDR", ihdr);
    pngChunk(out, "IDAT", z);
    pngChunk(out, "IEND", std::string());
    return out;
}
