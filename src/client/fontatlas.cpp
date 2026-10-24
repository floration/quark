#include "fontatlas.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include "overrides.h"   // decodeImage
#include "stb/stb_truetype.h"

// tools/make_font_atlas.py's metrics: the em is 7.5 units, cap tops sit 0.75 units below the line top
static constexpr float kEm = 7.5f, kCapTop = 0.75f;

static void solidBlock(const FontAtlasLayout& L, FontAtlas& out) {   // (Renderer::rect)
    for (int y = 6 * L.cell; y < 7 * L.cell && y < L.texH; y++)
        for (int x = 0; x < L.cell; x++) out.tex[(size_t)y * L.texW + x] = 255;
}

bool buildFontAtlas(const uint8_t* data, size_t size, const FontAtlasLayout& L, FontAtlas& out, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (size < 12 || size > (64u << 20)) return fail("not a font file");
    int off = stbtt_GetFontOffsetForIndex(data, 0);
    stbtt_fontinfo f;
    if (off < 0 || !stbtt_InitFont(&f, data, off)) return fail("not a TrueType / OpenType font");
    if (!stbtt_FindGlyphIndex(&f, 'H') || !stbtt_FindGlyphIndex(&f, 'A')) return fail("the font has no Latin letters");
    const float px = L.tpu;                                       // atlas texels per font unit
    float scale = stbtt_ScaleForMappingEmToPixels(&f, kEm * px);
    int hx0, hy0, hx1, hy1;
    stbtt_GetCodepointBox(&f, 'H', &hx0, &hy0, &hx1, &hy1);
    if (L.cap > 0 && hy1 > hy0)   // sized so its capitals are as tall as the default font's (within reason)
        scale *= std::min(2.0f, std::max(0.5f, L.cap * px / ((hy1 - hy0) * scale)));
    const float baseline = std::round((L.pad + kCapTop) * px + hy1 * scale);   // texels below the cell's top
    const float penX = (L.pad + L.origin) * px;
    const int padding = (int)std::ceil(L.spread) + 1;
    const float distScale = 127.5f / L.spread;                   // 0.5 + d / (2 * spread), in 0..255
    out.tex.assign((size_t)L.texW * L.texH, 0);
    for (int g = 0; g < 95; g++) {
        int cp = 32 + g, adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f, cp, &adv, &lsb);
        out.adv[g] = adv * scale / px;
        if (cp == ' ') continue;
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* sdf = stbtt_GetCodepointSDF(&f, scale, cp, padding, 128, distScale, &w, &h, &xo, &yo);
        if (!sdf) continue;
        const int cx = (g % 16) * L.cell, cy = (g / 16) * L.cell;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int tx = (int)std::lround(penX) + xo + x, ty = (int)baseline + yo + y;
                if (tx < 0 || ty < 0 || tx >= L.cell || ty >= L.cell) continue;
                uint8_t& t = out.tex[(size_t)(cy + ty) * L.texW + cx + tx];
                t = std::max(t, sdf[y * w + x]);
            }
        stbtt_FreeSDF(sdf, nullptr);
    }
    solidBlock(L, out);
    for (int a = 0; a < 95; a++)
        for (int b = 0; b < 95; b++) {
            float k = stbtt_GetCodepointKernAdvance(&f, 32 + a, 32 + b) * scale / px;
            out.kern[a * 95 + b] = (signed char)std::max(-127.0f, std::min(127.0f, std::round(k * 16)));
        }
    return true;
}

bool buildSheetAtlas(const uint8_t* ink, int sw, int sh, const FontAtlasLayout& L, FontAtlas& out, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (sw < 16 || sh < 16 || sw % 16 || sh % 16 || sw > 4096 || sh > 4096) return fail("not a 16 x 16 character sheet");
    const int cw = sw / 16, ch = sh / 16;
    auto at = [&](int c, int x, int y) { return ink[(size_t)((c / 16) * ch + y) * sw + (c % 16) * cw + x] != 0; };
    bool hasH = false;
    for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) hasH |= at('H', x, y);
    if (!hasH) return fail("the sheet has no letter H at character 72");
    const float unit = 8.0f / ch, top = 0.0f;                            // a cell is the 8-unit line (8 px: 1 unit a pixel)
    const float px = L.tpu, s = unit * px;                                // texels per sheet pixel
    const float gx0 = (L.pad + L.origin) * px, gy0 = (L.pad + top) * px;  // the glyph's pixel grid in its cell (texels)
    const float k = 0.5f / L.spread;                                     // texels -> field units
    out.tex.assign((size_t)L.texW * L.texH, 0);
    std::vector<float> inkR, offR;   // squares (x0, y0, x1, y1) in cell texels
    for (int g = 0; g < 95; g++) {
        const int c = 32 + g;
        inkR.clear(); offR.clear();
        int c0 = cw, c1 = -1;   // the glyph's ink columns
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                if (at(c, x, y)) { c0 = std::min(c0, x); c1 = std::max(c1, x); }
                std::vector<float>& v = at(c, x, y) ? inkR : offR;
                v.insert(v.end(), {gx0 + x * s, gy0 + y * s, gx0 + (x + 1) * s, gy0 + (y + 1) * s});
            }
        // up to its rightmost ink: the next glyph's blank column 0 is the space (half a cell for a blank one)
        out.adv[g] = (c1 < 0 ? cw * 0.5f : (float)(c1 + 1 + (c0 == 0))) * unit;
        if (inkR.empty()) continue;
        const float gx1 = gx0 + cw * s, gy1 = gy0 + ch * s;
        const int cx = (g % 16) * L.cell, cy = (g / 16) * L.cell;
        for (int ty = 0; ty < L.cell; ty++)
            for (int tx = 0; tx < L.cell; tx++) {
                const float px0 = tx + 0.5f, py0 = ty + 0.5f;
                // the max-norm distance from the texel to the nearest square of a set (0 inside one)
                auto dist = [&](const std::vector<float>& r) {
                    float d = 1e9f;
                    for (size_t i = 0; i < r.size(); i += 4) {
                        float dx = std::max({r[i] - px0, px0 - r[i + 2], 0.0f}), dy = std::max({r[i + 1] - py0, py0 - r[i + 3], 0.0f});
                        d = std::min(d, std::max(dx, dy));
                    }
                    return d;
                };
                float dIn = dist(inkR), v;
                if (dIn > 0) v = 0.5f - dIn * k;                                 // outside the ink
                else {                                                           // inside: to the nearest paper
                    float dOut = std::min({px0 - gx0, gx1 - px0, py0 - gy0, gy1 - py0});
                    if (!offR.empty()) dOut = std::min(dOut, dist(offR));
                    v = 0.5f + dOut * k;
                }
                out.tex[(size_t)(cy + ty) * L.texW + cx + tx] = (uint8_t)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255);
            }
    }
    solidBlock(L, out);
    std::memset(out.kern, 0, sizeof out.kern);
    out.minScale = 1.0f;   // (1 screen pixel a font pixel at the smallest: the font is wide)
    return true;
}

static int32_t le32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static bool nameIs(const uint8_t* p, size_t n, const char* want) {   // a fixed-size, NUL-padded name, any case
    size_t i = 0;
    for (; i < n && p[i]; i++) if (!want[i] || std::tolower(p[i]) != std::tolower((unsigned char)want[i])) return false;
    return !want[i];
}

bool quakeConchars(const uint8_t* d, size_t n, std::vector<uint8_t>& ink, int& w, int& h, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    auto raw = [&](const uint8_t* p, size_t size) {   // the lump: 128 x 128 bytes, or a qpic (width, height, bytes)
        if (size == 128 * 128) { w = h = 128; }
        else if (size >= 8 && le32(p) > 0 && le32(p + 4) > 0 && (size_t)le32(p) * le32(p + 4) + 8 == size) { w = le32(p); h = le32(p + 4); p += 8; }
        else return fail("CONCHARS isn't a 128 x 128 picture");
        ink.resize((size_t)w * h);
        for (size_t i = 0; i < ink.size(); i++) ink[i] = p[i] != 0;   // (palette index 0 is transparent)
        return true;
    };
    if (n >= 12 && !std::memcmp(d, "PACK", 4)) {   // a PAK: its gfx.wad
        int32_t dir = le32(d + 4), len = le32(d + 8);
        if (dir < 0 || len < 0 || (size_t)dir + len > n) return fail("a damaged PAK");
        for (int32_t e = dir; e + 64 <= dir + len; e += 64)
            if (nameIs(d + e, 56, "gfx.wad")) {
                int32_t pos = le32(d + e + 56), sz = le32(d + e + 60);
                if (pos < 0 || sz < 0 || (size_t)pos + sz > n) return fail("a damaged PAK");
                return quakeConchars(d + pos, (size_t)sz, ink, w, h, err);
            }
        return fail("the PAK has no gfx.wad");
    }
    if (n >= 12 && !std::memcmp(d, "WAD2", 4)) {   // gfx.wad: the CONCHARS lump
        int32_t num = le32(d + 4), info = le32(d + 8);
        if (num < 0 || num > 100000 || info < 0 || (size_t)info + (size_t)num * 32 > n) return fail("a damaged WAD");
        for (int32_t i = 0; i < num; i++) {
            const uint8_t* e = d + info + i * 32;
            if (!nameIs(e + 16, 16, "conchars")) continue;
            int32_t pos = le32(e), sz = le32(e + 8);
            if (e[13] != 0) return fail("CONCHARS is compressed");
            if (pos < 0 || sz < 0 || (size_t)pos + sz > n) return fail("a damaged WAD");
            return raw(d + pos, (size_t)sz);
        }
        return fail("the WAD has no CONCHARS");
    }
    if (n >= 8 && (!std::memcmp(d, "\x89PNG", 4) || (d[0] == 0xFF && d[1] == 0xD8))) {   // a picture of the sheet
        std::vector<uint8_t> rgba;
        if (!decodeImage(d, n, w, h, rgba) || w <= 0 || h <= 0) return fail("not a readable picture");
        ink.resize((size_t)w * h);
        for (size_t i = 0; i < ink.size(); i++) {
            const uint8_t* p = &rgba[i * 4];
            ink[i] = p[3] > 127 && std::max({p[0], p[1], p[2]}) > 63;   // (transparent, black and a dark drop shadow are paper)
        }
        return true;
    }
    return raw(d, n);
}

bool atlasFromPng(const uint8_t* png, size_t size, const FontAtlasLayout& L, FontAtlas& out, std::string* err) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!decodeImage(png, size, w, h, rgba) || w != L.texW || h != L.texH) { if (err) *err = "a damaged built-in atlas"; return false; }
    out.tex.resize((size_t)w * h);
    for (size_t i = 0; i < out.tex.size(); i++) out.tex[i] = rgba[i * 4];
    return true;
}
