// Languages (Settings > INTERFACE > LANGUAGE, the `language` setting): the game's own text drawn in the player's
// language. The code keeps writing English; the renderer looks every string it draws up in the language's table
// (assets/lang/<code>.tsv: English key, TAB, translation; tools/lang/extract.py lists the keys) just before drawing
// or measuring it, so no menu, HUD or notice needed changing. A string that isn't a key is tried with its digit runs
// as '#' ("KILLS: 12" -> "KILLS: #", the number put back into the translation), without the decorations round it
// ("> TEAM <", "SAY: "), and split at ": " / " - ". Anything else (player names, chat, map names) is drawn as it is.
// English costs nothing: no lookup at all. Another language: one hash lookup per drawn string (a cache of what was
// drawn, cleared between frames once it's big), never an allocation for a string drawn before.
//
// Characters past ASCII (accents, Cyrillic, CJK) come from the language's glyph atlas (assets/lang/atlas/, baked
// from Rubik Bold and Noto Sans CJK by tools/lang/make_atlas.py), drawn by the same distance-field shader as the UI
// font from a second texture: Renderer::setExtGlyphs.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lang {

struct Language { const char* code; const char* name; const char* atlas; };   // name: in its own language, as the menu shows it
const std::vector<Language>& languages();       // English first, then the game's languages in menu order
int indexOf(const std::string& code);           // -1: not one of them
// The language the player's system is set to (Windows: the UI language; elsewhere LANG / LC_ALL), one of
// languages(), or "en" when none matches.
std::string systemLanguage();
// The `language` setting as a language: "auto" (and anything unknown) is the system's.
std::string resolve(const std::string& setting);

// Makes code the language drawn (its table parsed: a few ms, once per pick). false: no such language (English stays).
bool setLanguage(const std::string& code);
const std::string& current();                   // "en" until set
inline bool active();                           // a language other than English

// The text to draw for s: its translation, or s itself. The reference stays good until the next frame().
const std::string& tr(const std::string& s);
// once a frame (Game::frame): drops the lookup cache when it has grown big (strings with changing numbers)
void frame();
int generation();                               // changes with the language (cached widths: Ui's fit cache)

// The language's glyphs past ASCII: a distance-field atlas in UI-font cells (cols x rows of `cell` texels), each
// glyph's codepoint and advance (font units), in atlas order.
struct ExtGlyphs {
    int cols = 0, rows = 0, cell = 0, texW = 0, texH = 0;
    std::vector<uint32_t> cp;
    std::vector<float> adv;
    std::vector<uint8_t> tex;   // texW * texH, R8
};
// the atlas for a language (decoded from the embedded PNG: tens of ms; once per pick). false: none (English).
bool loadGlyphs(const std::string& code, ExtGlyphs& out);

// what the key tables hold (tests)
size_t keyCount();
bool hasKey(const std::string& key);
// every language's table checked against the keys list: the same keys, '#' counts kept; problems appended
bool checkTables(std::vector<std::string>& problems);

namespace detail { extern bool g_active; }
inline bool active() { return detail::g_active; }

}  // namespace lang
