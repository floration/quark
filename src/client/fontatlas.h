// The UI font atlas from a TrueType / OpenType font file at run time: what tools/make_font_atlas.py bakes into
// font_ui.h, for an owner's font override (fonts/WorkSans.ttf, docs/ASSETS.md). Same layout: 16 x 6 cells for
// ASCII 32..126 plus the solid block at the start of row 7, a signed distance field (0.5 = the glyph edge),
// advances in font units and kerning in 1/16 units. Only built when an override exists (the Renderer, at startup).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct FontAtlasLayout { int texW, texH, cell; float tpu, pad, origin, spread; };   // font_ui.h's constants
struct FontAtlas {
    std::vector<uint8_t> tex;        // texW * texH, R8
    float adv[95] = {};
    signed char kern[95 * 95] = {};
};
bool buildFontAtlas(const uint8_t* font, size_t size, const FontAtlasLayout& layout, FontAtlas& out, std::string* err = nullptr);
