#include "fontatlas.h"
#include <algorithm>
#include <cmath>
#include "stb/stb_truetype.h"

// tools/make_font_atlas.py's metrics: the em is 7.5 units, cap tops sit 0.75 units below the line top
static constexpr float kEm = 7.5f, kCapTop = 0.75f;

bool buildFontAtlas(const uint8_t* data, size_t size, const FontAtlasLayout& L, FontAtlas& out, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (size < 12 || size > (64u << 20)) return fail("not a font file");
    int off = stbtt_GetFontOffsetForIndex(data, 0);
    stbtt_fontinfo f;
    if (off < 0 || !stbtt_InitFont(&f, data, off)) return fail("not a TrueType / OpenType font");
    if (!stbtt_FindGlyphIndex(&f, 'H') || !stbtt_FindGlyphIndex(&f, 'a')) return fail("the font has no Latin letters");
    const float px = L.tpu;                                       // atlas texels per font unit
    const float scale = stbtt_ScaleForMappingEmToPixels(&f, kEm * px);
    int hx0, hy0, hx1, hy1;
    stbtt_GetCodepointBox(&f, 'H', &hx0, &hy0, &hx1, &hy1);
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
    for (int y = 6 * L.cell; y < 7 * L.cell && y < L.texH; y++)   // the solid block (Renderer::rect)
        for (int x = 0; x < L.cell; x++) out.tex[(size_t)y * L.texW + x] = 255;
    for (int a = 0; a < 95; a++)
        for (int b = 0; b < 95; b++) {
            float k = stbtt_GetCodepointKernAdvance(&f, 32 + a, 32 + b) * scale / px;
            out.kern[a * 95 + b] = (signed char)std::max(-127.0f, std::min(127.0f, std::round(k * 16)));
        }
    return true;
}
