// The UI fonts of Settings > FONT (the `ui_font` setting): the built-ins (kBuiltinFonts in uifonts.cpp, one row each:
// Rubik, the default; Work Sans; QUARK CLASSIC, the original 5x7 pixel font; then free look-alikes of game fonts), then
// every .ttf / .otf in the game folder's mods/fonts/. A game-styled row uses the player's own copy of the real font
// when it finds one (mods/fonts under a known name, the system's fonts, a Steam install: read only, never shipped),
// else its bundled look-alike; a row with neither isn't listed. docs/ASSETS.md.
// Nothing here runs per frame: scan() when the menu opens or REFRESH is pressed, load() once per pick (and at startup).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "fontatlas.h"

namespace uifonts {
constexpr const char* kDefault = "rubik";
struct Entry {
    std::string id;     // the ui_font value: a built-in's id, or a mods/fonts file name ("MyFont.ttf")
    std::string name;   // as the menu shows it
    std::string hint;   // the grey line beside it ("CLOSEST FREE MATCH: OVERWATCH", "USING YOUR VERDANAB.TTF", "")
    bool user = false;  // a mods/fonts file
};
// Every font that can be picked, built-ins first. Looks on the disk (a few file checks): the menu calls it on opening
// and on REFRESH, never per frame.
std::vector<Entry> scan();
std::string modsDir();      // mods/fonts in the game folder (may not exist yet)
// The atlas for a ui_font value ("" = the default). true with out null: the baked default (font_ui.h) as it is.
// name: what was loaded, for notices. false: err says why (the caller falls back to the default).
bool load(const std::string& id, const FontAtlasLayout& layout, std::unique_ptr<FontAtlas>& out, std::string* err, std::string* name = nullptr);
bool isBuiltin(const std::string& id);
}
