#include "textfit.h"
#include <algorithm>
#include <cmath>

int textGlyph(char ch) { unsigned char uc = (unsigned char)ch; return (uc < 32 || uc > 126) ? '?' - 32 : uc - 32; }

void measureInk(const uint8_t* tex, const FontAtlasLayout& L, GlyphInk ink[95]) {
    // A texel inside the glyph (>= 0.5) at signed distance d (texels, inside positive) from the edge: the edge is no
    // nearer than d in any direction, so (its centre - d) bounds the ink from that side, exactly for the texel whose
    // nearest edge point is the extreme one.
    const float spread2 = 2.0f * L.spread / 255.0f;
    for (int g = 0; g < 95; g++) {
        GlyphInk k;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        const int cx = (g % 16) * L.cell, cy = (g / 16) * L.cell;
        if (g != 0)   // (the space has no ink)
            for (int ty = 0; ty < L.cell; ty++)
                for (int tx = 0; tx < L.cell; tx++) {
                    const int v = tex[(size_t)(cy + ty) * L.texW + cx + tx];
                    if (v < 128) continue;
                    const float d = (v - 127.5f) * spread2, px = tx + 0.5f, py = ty + 0.5f;
                    x0 = std::min(x0, px - d); x1 = std::max(x1, px + d);
                    y0 = std::min(y0, py - d); y1 = std::max(y1, py + d);
                }
        if (x0 <= x1) {   // texels -> units: the cell starts pad units left of the glyph box, whose pen origin is `origin` in
            k.x0 = x0 / L.tpu - L.pad - L.origin; k.x1 = x1 / L.tpu - L.pad - L.origin;
            k.y0 = y0 / L.tpu - L.pad; k.y1 = y1 / L.tpu - L.pad;
        }
        ink[g] = k;
    }
}

LineBox lineBoxOf(const GlyphInk ref[95]) {
    LineBox b;
    for (int g = 0; g < 95; g++) {
        if (ref[g].empty()) continue;
        b.top = std::min(b.top, ref[g].y0 - kLineOutline);
        b.bottom = std::max(b.bottom, ref[g].y1 + kLineOutline);
    }
    return b;
}

LineFit lineFit(const GlyphInk ink[95], const LineBox& box) {
    // the ink is scaled and moved, the outline round it isn't: the ink must fit in the box less an outline each side
    const float top = box.top + kLineOutline, bottom = box.bottom - kLineOutline;
    float y0 = 1e9f, y1 = -1e9f;
    for (int g = 0; g < 95; g++)
        if (!ink[g].empty()) { y0 = std::min(y0, ink[g].y0); y1 = std::max(y1, ink[g].y1); }
    LineFit f;
    if (y0 > y1 || (y0 >= top - 1e-3f && y1 <= bottom + 1e-3f)) return f;   // (it fits: as it is)
    f.scale = std::min(1.0f, (bottom - top) / (y1 - y0));
    // the shift that keeps the capitals' middle in place, within what keeps the ink in the box
    const float lo = top - y0 * f.scale, hi = bottom - y1 * f.scale;
    f.shift = std::min(std::max(kLineAnchor * (1 - f.scale), lo), hi);
    return f;
}

float textUnits(const TextMetrics& m, const std::string& s, float level, float scale) {
    const float o = outlineUnits(scale);
    float w = 0;
    int prev = -1;
    for (char ch : s) {
        int g = textGlyph(ch);
        if (prev >= 0) w += m.advance(prev, g) - m.closeUp(prev, g, level, o);
        prev = g;
    }
    if (prev >= 0) w += m.adv[prev];
    return w;
}

float maxLevel(const TextMetrics& m, const std::string& s, float scale) {
    const float o = outlineUnits(scale);
    float lv = 0;
    int prev = -1;
    for (char ch : s) { int g = textGlyph(ch); if (prev >= 0) lv = std::max(lv, m.room(prev, g, o)); prev = g; }
    return lv;
}

TextFit fitText(const TextMetrics& m, const std::string& s, float ts, float maxW) {
    TextFit f;
    f.scale = ts;
    const float k = m.fitScale;
    if (s.empty() || textUnits(m, s, 0, ts * k) * ts * k <= maxW) return f;
    // closed up at full size: the lowest level that fits (the width falls as the level rises: a binary search)
    float top = maxLevel(m, s, ts * k);
    if (top > 0 && textUnits(m, s, top, ts * k) * ts * k <= maxW) {
        float lo = 0, hi = top;
        for (int i = 0; i < 24; i++) {
            float mid = (lo + hi) * 0.5f;
            if (textUnits(m, s, mid, ts * k) * ts * k <= maxW) hi = mid; else lo = mid;
        }
        f.level = hi;
        return f;
    }
    // smaller, closed up all it may be at that size (its outline is wider in units, so a few rounds)
    float sc = ts;
    for (int i = 0; i < 6; i++) {
        float w = textUnits(m, s, maxLevel(m, s, sc * k), sc * k);
        float next = std::max(m.minScale, std::min(ts, maxW / (k * w)));
        if (std::fabs(next - sc) < 1e-4f) break;
        sc = next;
    }
    float lv = maxLevel(m, s, sc * k);
    for (int i = 0; i < 8 && sc > m.minScale && textUnits(m, s, lv, sc * k) * sc * k > maxW; i++) {   // (rounding: a hair smaller)
        sc = std::max(m.minScale, sc * 0.995f);
        lv = maxLevel(m, s, sc * k);
    }
    if (textUnits(m, s, lv, sc * k) * sc * k <= maxW + 0.01f) { f.scale = sc; f.level = lv; return f; }
    // cut short with "..." at the smallest size, as it is
    f.scale = m.minScale;
    f.level = 0;
    const float room = maxW / (m.minScale * k) - textUnits(m, "...", 0, m.minScale * k);
    float w = 0;
    int prev = -1, keep = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const int g = textGlyph(s[i]);
        const float nw = w + (prev >= 0 ? m.advance(prev, g) - m.adv[prev] : 0) + m.adv[g];
        if (nw > room) break;
        w = nw; prev = g; keep = (int)i + 1;
    }
    f.keep = keep;
    return f;
}
