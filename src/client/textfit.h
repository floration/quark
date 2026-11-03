// Measuring and fitting UI text (no GL: the renderer and the tests share it). A UI font is its atlas (fontatlas.h's
// layout), its advances and kerning, and what's measured from the atlas once when the font is picked (never per
// frame): every glyph's ink box, and the scale that keeps the font's ink inside the line box.
//
// Font units: a line is 8 tall, the em 7.5, cap tops 0.75 below the line top, the baseline 6.0 (tools/make_font_atlas.py).
// Text is drawn at a scale (pixels per unit) times the font's fitScale. Tightening (the fit's first step) takes room
// out of the gaps between letters, each pair only as far as its own ink allows (TextMetrics::room).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "fontatlas.h"

// a glyph's ink, in font units: x from its pen origin, y from the line top (the distance field's 0.5 edge); none: x0 > x1
struct GlyphInk { float x0 = 1, x1 = 0, y0 = 1, y1 = 0; bool empty() const { return x0 > x1; } };

constexpr float kMaxTighten = 0.15f;   // a pair never tighter than 15% of the first letter's advance (readable)
inline float outlineUnits(float scale) { return scale > 0.01f ? (1.0f / scale > 0.15f ? 1.0f / scale : 0.15f) : 100.0f; }   // (Renderer's UI shader: max(1 px, 0.15 units))

// Glyphs past ASCII (the language's atlas: lang.h, Renderer::setExtGlyphs): glyph ids 95 and up, no kerning
struct ExtMetrics {
    std::vector<float> adv;          // units
    std::vector<GlyphInk> ink;
    std::vector<uint16_t> bmp;       // codepoint (U+0000..U+FFFF) -> 1 + its index; 0: not in the atlas
};

struct TextMetrics {
    const float* adv = nullptr;          // [95] units
    const signed char* kern = nullptr;   // [95 * 95] 1/16 units
    GlyphInk ink[95];
    const ExtMetrics* ext = nullptr;     // (null: ASCII only; anything else is a '?')
    float fitScale = 1;                  // < 1: the font is drawn smaller so its ink stays in the line box (lineFit)...
    float fitShift = 0;                  // ...and moved this far down (units; y' = y * fitScale + fitShift)
    float minScale = 1.3f;               // the smallest text scale (Renderer::minTextScale)
    float A(int g) const { return g < 95 ? adv[g] : ext->adv[g - 95]; }
    const GlyphInk& I(int g) const { return g < 95 ? ink[g] : ext->ink[g - 95]; }
    float advance(int a, int b) const { return A(a) + (a < 95 && b < 95 ? kern[a * 95 + b] * (1.0f / 16.0f) : 0.0f); }
    // The glyph at s[i] (UTF-8), i moved past it: ASCII 32..126 -> 0..94, a character of the ext atlas -> 95 + its index,
    // anything else (a control character, a broken sequence, a character no atlas has) '?'.
    int next(const char* s, size_t n, size_t& i) const {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { i++; return (c < 32 || c > 126) ? '?' - 32 : c - 32; }
        int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        uint32_t cp = len == 1 ? 0xFFFD : (c & (0x7F >> len));
        for (int k = 1; k < len; k++) {
            if (i + k >= n || ((unsigned char)s[i + k] & 0xC0) != 0x80) { len = k; cp = 0xFFFD; break; }
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        }
        i += len;
        if (ext && cp < ext->bmp.size() && ext->bmp[cp]) return 94 + ext->bmp[cp];
        return '?' - 32;
    }
    // How much the pair a, b may close up (units) at an outline of o units: until a's ink is one outline from b's (so
    // the fills never touch: the black outline stays between them), and never over kMaxTighten of a's advance.
    float room(int a, int b, float o) const {
        float r = kMaxTighten * A(a);
        // a word space keeps most of its width: closed up from both sides it all but vanished ("BOTSAUSGLEICHEN")
        if (b == 0) return 0;
        if (a == 0) return 0.1f * A(a);   // (a third of what both sides closed before)
        const GlyphInk &ia = I(a), &ib = I(b);
        if (!ia.empty() && !ib.empty()) {
            float gap = advance(a, b) + ib.x0 - ia.x1 - o;
            r = gap < r ? (gap > 0 ? gap : 0) : r;
        }
        return r;
    }
    // what a pair is closed up by at a tightening level (units): the level, or less where its ink allows less
    float closeUp(int a, int b, float level, float o) const { if (level <= 0) return 0; float r = room(a, b, o); return level < r ? level : r; }
};

int textGlyph(char ch);   // ASCII 32..126 -> 0..94 ('?' for anything else)
// every glyph's ink box from the atlas (a few hundred thousand texel reads: once per font)
void measureInk(const uint8_t* tex, const FontAtlasLayout& L, GlyphInk ink[95]);
// the same for n cells of an atlas cols cells wide (the language glyphs: lang.h), cell i at (i % cols, i / cols)
void measureInkCells(const uint8_t* tex, int texW, int cols, int n, const FontAtlasLayout& L, GlyphInk* ink);
// The line box (font units, from the line top) a font's ink must stay in: the 8-unit line, or the default font's own
// ink where that reaches further (Rubik's metrics define the box: it always fits), with the outline's width at the
// UI's base text scale (1 px at text scale 2: 0.5 units) on each side.
constexpr float kLineOutline = 0.5f;
struct LineBox { float top = 0, bottom = 8; };
LineBox lineBoxOf(const GlyphInk ref[95]);
// How a font's ink (plus the outline) is fitted into the line box: drawn just enough smaller (scale: 1 when it fits
// already) and moved as little as that needs (shift, units: y' = y * scale + shift), keeping the middle of its
// capitals where Rubik's are when it can (so it stays centred in buttons as Rubik's text is).
constexpr float kLineAnchor = 0.75f + 5.25f * 0.5f;
struct LineFit { float scale = 1, shift = 0; };
LineFit lineFit(const GlyphInk ink[95], const LineBox& box);
inline float fittedY(float y, const LineFit& f) { return y * f.scale + f.shift; }

// The advance of s (units at scale 1, before fitScale; kerning included), closed up by `level` (0: as it is) at text
// scale `scale` (the pairs' room depends on it: the outline is 1 px)
float textUnits(const TextMetrics& m, const std::string& s, float level = 0, float scale = 2);
// the level at which every pair of s is closed up as far as it may go (more changes nothing)
float maxLevel(const TextMetrics& m, const std::string& s, float scale);
// Fitting s into maxW pixels at text scale ts: first closed up (a level, the same for every pair that has the room),
// then smaller (down to minScale, closed up all it may be), and only then cut with "..." (keep: how many characters
// stay before it, in bytes, whole characters; -1: all of it). Width in pixels: textUnits(m, s, level, scale * fitScale) * scale * fitScale.
struct TextFit { float scale = 0, level = 0; int keep = -1; };
TextFit fitText(const TextMetrics& m, const std::string& s, float ts, float maxW);
