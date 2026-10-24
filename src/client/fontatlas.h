// The UI font atlas built at run time: what tools/make_font_atlas.py bakes into font_ui.h / font_builtin.h, for fonts
// the game doesn't bake (a player's mods/fonts file, an owner's override, a game font found on the player's own
// computer: uifonts.h). Same layout: 16 x 6 cells for ASCII 32..126 plus the solid block at the start of row 7, a
// signed distance field (0.5 = the glyph edge), advances in font units and kerning in 1/16 units. Built once when
// such a font is picked (Renderer::setUiFont), never per frame.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// font_ui.h's constants; cap: the capital height (units) every font is sized to (0: the em is 7.5 units, unscaled)
struct FontAtlasLayout { int texW, texH, cell; float tpu, pad, origin, spread; float cap = 0; };
struct FontAtlas {
    std::vector<uint8_t> tex;        // texW * texH, R8
    float adv[95] = {};
    signed char kern[95 * 95] = {};
    float minScale = 0;              // the smallest text scale (0: Renderer::kMinTextScale; a pixel font: 1)
};
// A TrueType / OpenType font (stb_truetype: a variable font gives its default instance).
bool buildFontAtlas(const uint8_t* font, size_t size, const FontAtlasLayout& layout, FontAtlas& out, std::string* err = nullptr);
// A pixel font sheet: 16 x 16 cells (a Quake-style console font: character n in cell n, read left to right, top to
// bottom), ink = non-zero; sheetW / sheetH divisible by 16. 1 unit a pixel from the line top (an 8-pixel cell = the
// 8-unit line, like QUARK CLASSIC), square edges (a max-norm distance field), each glyph as wide as its ink (the blank
// column 0 of the next one is the space; tools/make_font_atlas.py's "conchars"), drawn down to scale 1.
bool buildSheetAtlas(const uint8_t* ink, int sheetW, int sheetH, const FontAtlasLayout& layout, FontAtlas& out, std::string* err = nullptr);
// Quake's console font (CONCHARS, 128 x 128, one byte a pixel) from any of: a PAK (id1/pak0.pak: its gfx.wad), a
// WAD2 (gfx.wad: the CONCHARS lump), the raw 16384-byte lump (conchars.lmp), or a PNG / JPEG sheet. ink: 1 = ink.
bool quakeConchars(const uint8_t* data, size_t size, std::vector<uint8_t>& ink, int& w, int& h, std::string* err = nullptr);
// The atlas from a PNG made by tools/make_font_atlas.py (its R channel; must be texW x texH).
bool atlasFromPng(const uint8_t* png, size_t size, const FontAtlasLayout& layout, FontAtlas& out, std::string* err = nullptr);
