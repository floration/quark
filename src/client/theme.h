// HUD theme: theme.cfg (next to hud.cfg) styles the HUD's elements with a small CSS-like style sheet (HUD plan, dima's
// Discord ask: corner rounding, colours and opacity, margins and padding, "maybe CSS"). A fixed list of properties on a
// fixed list of selectors, every value clamped, read only when the file changes (live, within a second, like hud.cfg):
// nothing is parsed or looked up while you play, and a theme can't hide or break what it doesn't style.
//
//   /* comments */
//   hud            { radius: 4; }                               every element
//   hud-panel      { background: #0d0f14 55%; stripe: 2 team; }  the score box (the class of the top panels)
//   killfeed, chat { ... }                                       those boxes' classes
//   hud.health     { background: #000 40%; padding: 4 8; border: 1 #ffffff 30%; }   one element (hud.<name>)
//
// Elements: health, weapon, bars, speed, slots, keys, score, killfeed, chat (the two first-person models, weapon-model and
// grapple-model, have no 2D look). A later rule wins over an earlier one for the same selector; hud.<name> wins over
// its class, a class over hud. Lengths are 720p units (scaled with the window and the element's SIZE).
//
// Properties (unset: the element's built-in look, so an empty theme changes nothing):
//   background: <colour> [<opacity>%] | none     the element's box (the kill feed: each line)
//   color: <colour>                              the element's colour (its HUD COLOR, when that's off)
//   opacity: <0-100>%                            the whole element (ranked: at least 30%)
//   border: <width> [<colour> [<opacity>%]] | none, border-width, border-color
//   radius (border-radius): <n>                  rounded corners (at most half the box)
//   padding: <all> | <y> <x> | <top> <right> <bottom> <left>   (0-24)
//   margin: the same, moves the element (-200 to 200)
//   gap: <n>                                     between lines / weapons / bars / keys (0-48)
//   font-size: <percent>% | <scale>              text size (50% - 300%)
//   text-align: left | center | right            the kill feed's and chat's lines
//   stripe: <height> [<colour>] | none           the line along a panel's top (the score box's accent)
// Colours: #rgb, #rrggbb, #rrggbbaa, rgb(r, g, b), rgba(r, g, b, a), a few names (white, black, grey, red, green,
// blue, yellow, orange, cyan, magenta), team / accent (your team colour), enemy, none / transparent.
#pragma once
#include <cstdint>
#include <string>
#include "renderer.h"

// A colour in a theme: fixed, or your team / the enemy colour (looked up when drawn, so it follows a change)
struct ThemeColor {
    enum Kind : uint8_t { FIXED, TEAM, ENEMY } kind = FIXED;
    Color c = {1, 1, 1, 1};   // (TEAM / ENEMY: only its alpha)
    Color get(const Color& team, const Color& enemy) const {
        if (kind == FIXED) return c;
        const Color& k = kind == TEAM ? team : enemy;
        return {k.r, k.g, k.b, c.a};
    }
    bool operator==(const ThemeColor& o) const { return kind == o.kind && c.r == o.c.r && c.g == o.c.g && c.b == o.c.b && c.a == o.c.a; }
};

// One rule's (or one element's resolved) properties: each either set or left to the element's own look.
struct HudStyle {
    enum Prop : uint16_t {
        BACKGROUND = 1, COLOR = 2, OPACITY = 4, BORDER = 8, RADIUS = 16, PADDING = 32, MARGIN = 64, GAP = 128, FONT = 256,
        ALIGN = 512, STRIPE = 1024, ALL = 2047
    };
    uint16_t set = 0;
    ThemeColor background, color, border, stripe;
    float opacity = 1, borderW = 0, radius = 0, gap = 0, fontScale = 1, stripeW = 0;
    float pad[4] = {0, 0, 0, 0}, margin[4] = {0, 0, 0, 0};   // left, top, right, bottom
    uint8_t align = 0;                                        // 0 left, 1 centre, 2 right
    bool has(Prop p) const { return (set & p) != 0; }
    void apply(const HudStyle& o);                            // o's set properties over these
    void clear(Prop p) { set &= (uint16_t)~p; }
    bool operator==(const HudStyle& o) const;
    bool operator!=(const HudStyle& o) const { return !(*this == o); }
};

class HudTheme {
public:
    static constexpr int kElems = 14;   // Settings::HUD_COUNT (theme.cpp checks)
    // what a rule can select: every element, a class, or one element (kSelElem + Settings::HUD_*)
    enum { SEL_HUD, SEL_PANEL, SEL_KILLFEED, SEL_CHAT, SEL_ELEM0, kSelectors = SEL_ELEM0 + kElems };
    static const char* elementName(int el);         // "health", ...: hud.<name> in the file
    static const char* selectorName(int sel);       // "hud", "hud-panel", "killfeed", "chat", "hud.health", ...
    static bool styled(int el);                     // the element has a 2D look (not a first-person model)

    // (Re)loads when the file changed: the shared watcher thread's key (hudwidgets.h), once a second (pollDue).
    void poll(const std::string& path);
    bool pollDue() const;
    // Replaces the rules with the text's; returns the number of rules read. A file that can't be read as a whole (a
    // brace left open) gives the default theme; an unknown property or value skips just that declaration. Either way
    // error() says where (the first problem).
    int fromText(const std::string& text);
    std::string toText() const;                     // the rules as theme.cfg text (comments aren't kept)
    bool save(const std::string& path);
    const std::string& error() const { return error_; }

    const HudStyle& style(int el) const { return resolved_[el]; }   // the element with every rule applied
    HudStyle& rule(int sel) { return rules_[sel]; }                // (the editor: change a rule, then resolve())
    const HudStyle& rule(int sel) const { return rules_[sel]; }
    void resolve();
    bool empty() const;                             // no rule sets anything
    void clearRules();                              // the default theme (nothing styled)
    uint32_t version() const { return version_; }   // counts changes (what's drawn from the theme can be cached on it)

private:
    HudStyle rules_[kSelectors];
    HudStyle resolved_[kElems];
    std::string error_;
    long long mtime_ = -1;
    double lastPoll_ = -10;
    uint32_t version_ = 0;
};

// Parsing pieces, shared with the editor's text fields and the tests.
bool parseThemeColor(const std::string& v, ThemeColor& out);
std::string themeColorText(const ThemeColor& c);   // as the file writes it
