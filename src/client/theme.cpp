#include "theme.h"
#include "config.h"
#include "filewatch.h"
#include "../common/u8file.h"
#include "../game/map.h"   // (writeFileAtomic)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

static_assert(HudTheme::kElems == Settings::HUD_COUNT, "a theme style per HUD element");

namespace {
const char* const kElemNames[HudTheme::kElems] = {"health", "weapon", "bars", "speed", "slots", "weapon-model", "keys", "grapple-model",
                                                  "score", "killfeed", "chat", "objective", "notices", "scoreboard"};
static_assert(Settings::HUD_SCORE == 8 && Settings::HUD_KILLFEED == 9 && Settings::HUD_CHAT == 10 && Settings::HUD_OBJECTIVE == 11 &&
              Settings::HUD_NOTICES == 12 && Settings::HUD_SCOREBOARD == 13, "kElemNames follows Settings::HUD_*");
const char* const kSelNames[HudTheme::SEL_ELEM0] = {"hud", "hud-panel", "killfeed", "chat"};

std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
std::vector<std::string> words(const std::string& s) {   // split at spaces, but not inside rgb( ... )
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(') depth++;
        if (c == ')') depth = std::max(0, depth - 1);
        if ((c == ' ' || c == '\t') && depth == 0) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
bool number(const std::string& w, float& v, bool* percent = nullptr) {
    if (w.empty()) return false;
    char* e = nullptr;
    v = strtof(w.c_str(), &e);
    if (e == w.c_str() || !std::isfinite(v)) return false;
    std::string rest(e);
    if (percent) *percent = rest == "%";
    return rest.empty() || rest == "px" || (percent && rest == "%");
}
std::string upperCopy(std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; }
std::string fmt(float v) { char b[32]; snprintf(b, sizeof b, "%g", std::round(v * 100) / 100); return b; }
double pollClock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

struct Named { const char* name; Color c; };
const Named kNames[] = {{"white", {1, 1, 1, 1}}, {"black", {0, 0, 0, 1}}, {"grey", {0.5f, 0.52f, 0.55f, 1}}, {"gray", {0.5f, 0.52f, 0.55f, 1}},
                        {"red", {1, 0.25f, 0.2f, 1}}, {"green", {0.3f, 0.85f, 0.4f, 1}}, {"blue", {0.25f, 0.5f, 1, 1}},
                        {"yellow", {1, 0.85f, 0.25f, 1}}, {"orange", {1, 0.55f, 0.2f, 1}}, {"cyan", {0.25f, 0.85f, 1, 1}},
                        {"magenta", {1, 0.3f, 0.9f, 1}}};
}

const char* HudTheme::elementName(int el) { return el >= 0 && el < kElems ? kElemNames[el] : "?"; }
const char* HudTheme::selectorName(int sel) {
    static std::string names[kSelectors];
    if (sel < 0 || sel >= kSelectors) return "?";
    if (sel < SEL_ELEM0) return kSelNames[sel];
    if (names[sel].empty()) names[sel] = std::string("hud.") + kElemNames[sel - SEL_ELEM0];
    return names[sel].c_str();
}
bool HudTheme::styled(int el) { return el >= 0 && el < kElems && !Settings::isModelElem(el); }

bool parseThemeColor(const std::string& text, ThemeColor& out) {
    std::vector<std::string> w = words(lower(trim(text)));
    if (w.empty() || w.size() > 2) return false;
    ThemeColor c;
    const std::string& v = w[0];
    if (v == "none" || v == "transparent") c.c = {0, 0, 0, 0};
    else if (v == "team" || v == "accent" || v == "friendly") c.kind = ThemeColor::TEAM;
    else if (v == "enemy") c.kind = ThemeColor::ENEMY;
    else if (v[0] == '#') {
        std::string h = v.substr(1);
        if (h.size() == 3) h = {h[0], h[0], h[1], h[1], h[2], h[2]};
        if ((h.size() != 6 && h.size() != 8) || h.find_first_not_of("0123456789abcdef") != std::string::npos) return false;
        unsigned long x = strtoul(h.c_str(), nullptr, 16);
        if (h.size() == 6) x = (x << 8) | 255;
        c.c = {((x >> 24) & 255) / 255.0f, ((x >> 16) & 255) / 255.0f, ((x >> 8) & 255) / 255.0f, (x & 255) / 255.0f};
    } else if (v.rfind("rgb(", 0) == 0 || v.rfind("rgba(", 0) == 0) {
        float k[4] = {0, 0, 0, 1};
        size_t open = v.find('('), close = v.rfind(')');
        if (close == std::string::npos || close < open) return false;
        std::string in = v.substr(open + 1, close - open - 1);
        for (char& ch : in) if (ch == ',') ch = ' ';
        std::vector<std::string> p = words(in);
        if (p.size() < 3 || p.size() > 4) return false;
        for (size_t i = 0; i < p.size(); i++) if (!number(p[i], k[i])) return false;
        c.c = {clampf(k[0] / 255, 0, 1), clampf(k[1] / 255, 0, 1), clampf(k[2] / 255, 0, 1), clampf(k[3], 0, 1)};
    } else {
        bool found = false;
        for (const Named& n : kNames) if (v == n.name) { c.c = n.c; found = true; }
        if (!found) return false;
    }
    if (w.size() == 2) {   // "<colour> 40%": its opacity
        float a; bool pct = false;
        if (!number(w[1], a, &pct) || !pct) return false;
        c.c.a = clampf(a / 100, 0, 1);
    }
    out = c;
    return true;
}

std::string themeColorText(const ThemeColor& c) {
    auto alpha = [&](std::string s) { return c.c.a < 0.999f ? s + " " + fmt(c.c.a * 100) + "%" : s; };
    if (c.kind == ThemeColor::TEAM) return alpha("team");
    if (c.kind == ThemeColor::ENEMY) return alpha("enemy");
    if (c.c.a <= 0) return "none";
    auto by = [](float v) { return (int)std::lround(clampf(v, 0, 1) * 255); };
    char b[16];
    snprintf(b, sizeof b, "#%02x%02x%02x", by(c.c.r), by(c.c.g), by(c.c.b));
    return alpha(b);
}

void HudStyle::apply(const HudStyle& o) {
    if (o.has(BACKGROUND)) background = o.background;
    if (o.has(COLOR)) color = o.color;
    if (o.has(OPACITY)) opacity = o.opacity;
    if (o.has(BORDER)) { border = o.border; borderW = o.borderW; }
    if (o.has(RADIUS)) radius = o.radius;
    if (o.has(PADDING)) std::copy(o.pad, o.pad + 4, pad);
    if (o.has(MARGIN)) std::copy(o.margin, o.margin + 4, margin);
    if (o.has(GAP)) gap = o.gap;
    if (o.has(FONT)) fontScale = o.fontScale;
    if (o.has(ALIGN)) align = o.align;
    if (o.has(STRIPE)) { stripe = o.stripe; stripeW = o.stripeW; }
    set |= o.set;
}

bool HudStyle::operator==(const HudStyle& o) const {
    if (set != o.set) return false;
    return (!has(BACKGROUND) || background == o.background) && (!has(COLOR) || color == o.color) && (!has(OPACITY) || opacity == o.opacity) &&
           (!has(BORDER) || (border == o.border && borderW == o.borderW)) && (!has(RADIUS) || radius == o.radius) &&
           (!has(PADDING) || std::equal(pad, pad + 4, o.pad)) && (!has(MARGIN) || std::equal(margin, margin + 4, o.margin)) &&
           (!has(GAP) || gap == o.gap) && (!has(FONT) || fontScale == o.fontScale) && (!has(ALIGN) || align == o.align) &&
           (!has(STRIPE) || (stripe == o.stripe && stripeW == o.stripeW));
}

namespace {
// one declaration into a rule; false: an unknown property or a value it can't read
bool declare(HudStyle& st, const std::string& prop, const std::string& value) {
    std::vector<std::string> w = words(value);
    auto box = [&](float* out, float lo, float hi) {   // CSS order: top right bottom left; stored left top right bottom
        float v[4];
        if (w.empty() || w.size() > 4) return false;
        for (size_t i = 0; i < w.size(); i++) if (!number(w[i], v[i])) return false;
        const float t = v[0], r = w.size() > 1 ? v[1] : t, b = w.size() > 2 ? v[2] : t, l = w.size() > 3 ? v[3] : r;
        out[0] = clampf(l, lo, hi); out[1] = clampf(t, lo, hi); out[2] = clampf(r, lo, hi); out[3] = clampf(b, lo, hi);
        return true;
    };
    auto one = [&](float& out, float lo, float hi) {
        float v;
        if (w.size() != 1 || !number(w[0], v)) return false;
        out = clampf(v, lo, hi);
        return true;
    };
    if (prop == "background" || prop == "background-color") {
        if (!parseThemeColor(value, st.background)) return false;
        st.set |= HudStyle::BACKGROUND;
    } else if (prop == "color") {
        if (!parseThemeColor(value, st.color)) return false;
        st.set |= HudStyle::COLOR;
    } else if (prop == "opacity") {
        float v; bool pct = false;
        if (w.size() != 1 || !number(w[0], v, &pct)) return false;
        st.opacity = clampf(pct || v > 1 ? v / 100 : v, 0, 1);
        st.set |= HudStyle::OPACITY;
    } else if (prop == "border") {
        if (w.size() == 1 && w[0] == "none") st.borderW = 0;
        else {
            float bw;
            if (w.empty() || !number(w[0], bw)) return false;
            st.borderW = clampf(bw, 0, 8);
            if (w.size() > 1) {
                std::string rest;
                for (size_t i = 1; i < w.size(); i++) rest += (i > 1 ? " " : "") + w[i];
                if (!parseThemeColor(rest, st.border)) return false;
            } else if (!st.has(HudStyle::BORDER)) st.border = ThemeColor();
        }
        st.set |= HudStyle::BORDER;
    } else if (prop == "border-width") {
        if (!one(st.borderW, 0, 8)) return false;
        st.set |= HudStyle::BORDER;
    } else if (prop == "border-color") {
        if (!parseThemeColor(value, st.border)) return false;
        st.set |= HudStyle::BORDER;
    } else if (prop == "radius" || prop == "border-radius") {
        if (!one(st.radius, 0, 64)) return false;
        st.set |= HudStyle::RADIUS;
    } else if (prop == "padding") {
        if (!box(st.pad, 0, 24)) return false;
        st.set |= HudStyle::PADDING;
    } else if (prop == "margin") {
        if (!box(st.margin, -200, 200)) return false;
        st.set |= HudStyle::MARGIN;
    } else if (prop == "gap") {
        if (!one(st.gap, 0, 48)) return false;
        st.set |= HudStyle::GAP;
    } else if (prop == "font-size") {
        float v; bool pct = false;
        if (w.size() != 1 || !number(w[0], v, &pct)) return false;
        st.fontScale = clampf(pct ? v / 100 : v, 0.5f, 3);
        st.set |= HudStyle::FONT;
    } else if (prop == "text-align") {
        if (w.size() != 1) return false;
        if (w[0] == "left" || w[0] == "start") st.align = 0;
        else if (w[0] == "center" || w[0] == "centre") st.align = 1;
        else if (w[0] == "right" || w[0] == "end") st.align = 2;
        else return false;
        st.set |= HudStyle::ALIGN;
    } else if (prop == "stripe") {
        if (w.size() == 1 && w[0] == "none") st.stripeW = 0;
        else {
            float h;
            if (w.empty() || !number(w[0], h)) return false;
            st.stripeW = clampf(h, 0, 8);
            st.stripe = ThemeColor();
            st.stripe.kind = ThemeColor::TEAM;
            if (w.size() > 1) {
                std::string rest;
                for (size_t i = 1; i < w.size(); i++) rest += (i > 1 ? " " : "") + w[i];
                if (!parseThemeColor(rest, st.stripe)) return false;
            }
        }
        st.set |= HudStyle::STRIPE;
    } else return false;
    return true;
}
}

int HudTheme::fromText(const std::string& textIn) {
    HudStyle rules[kSelectors];
    std::string err;
    int n = 0, line = 1;
    auto fail = [&](int ln, const std::string& what) { if (err.empty()) err = "THEME.CFG LINE " + std::to_string(ln) + ": " + what; };
    // comments out (newlines kept, for the line numbers)
    std::string t;
    t.reserve(textIn.size());
    for (size_t i = 0; i < textIn.size(); i++) {
        if (textIn.compare(i, 2, "/*") == 0) {
            size_t e = textIn.find("*/", i + 2);
            const size_t end = e == std::string::npos ? textIn.size() : e + 2;
            for (size_t k = i; k < end; k++) if (textIn[k] == '\n') t += '\n';
            i = end - 1;
            continue;
        }
        t += textIn[i];
    }
    bool broken = false;
    size_t i = 0;
    while (i < t.size()) {
        const size_t open = t.find('{', i);
        if (open == std::string::npos) {
            if (!trim(t.substr(i)).empty()) { fail(line, "TEXT AFTER THE LAST RULE"); broken = true; }
            break;
        }
        const int selLine = line + (int)std::count(t.begin() + i, t.begin() + open, '\n');
        const size_t close = t.find('}', open);
        const size_t nested = t.find('{', open + 1);
        if (close == std::string::npos || (nested != std::string::npos && nested < close)) { fail(selLine, "A { WITHOUT ITS }"); broken = true; break; }
        // the selectors
        bool sels[kSelectors] = {};
        bool any = false;
        std::string selText = t.substr(i, open - i);
        std::stringstream ss(selText);
        std::string one;
        while (std::getline(ss, one, ',')) {
            std::string s = lower(trim(one));
            int found = -1;
            for (int k = 0; k < kSelectors; k++) if (s == selectorName(k)) found = k;
            for (int el = 0; el < kElems && found < 0; el++)   // (a bare element name, where it isn't a class)
                if (s == kElemNames[el] && s != "killfeed" && s != "chat") found = SEL_ELEM0 + el;
            if (found < 0) fail(selLine, "UNKNOWN SELECTOR '" + upperCopy(s) + "'");
            else { sels[found] = true; any = true; }
        }
        // the declarations
        HudStyle st;
        int dl = line + (int)std::count(t.begin() + i, t.begin() + open, '\n');
        std::string body = t.substr(open + 1, close - open - 1);
        size_t p = 0;
        while (p <= body.size()) {
            size_t semi = body.find(';', p);
            if (semi == std::string::npos) semi = body.size();
            std::string decl = body.substr(p, semi - p);
            const size_t first = decl.find_first_not_of(" \t\r\n");
            const int at = dl + (first == std::string::npos ? 0 : (int)std::count(decl.begin(), decl.begin() + first, '\n'));
            if (!trim(decl).empty()) {
                size_t colon = decl.find(':');
                if (colon == std::string::npos) fail(at, "EXPECTED NAME: VALUE");
                else {
                    std::string prop = lower(trim(decl.substr(0, colon))), value = lower(trim(decl.substr(colon + 1)));
                    if (!declare(st, prop, value)) fail(at, "CAN'T READ '" + upperCopy(prop) + ": " + upperCopy(value) + "'");
                }
            }
            dl += (int)std::count(body.begin() + p, body.begin() + std::min(body.size(), semi + 1), '\n');
            p = semi + 1;
        }
        if (any) {
            for (int k = 0; k < kSelectors; k++) if (sels[k]) rules[k].apply(st);
            n++;
        }
        line += (int)std::count(t.begin() + i, t.begin() + close + 1, '\n');
        i = close + 1;
    }
    error_ = err;
    for (int k = 0; k < kSelectors; k++) rules_[k] = broken ? HudStyle() : rules[k];
    resolve();
    return broken ? -1 : n;
}

std::string HudTheme::toText() const {
    std::string out = "/* quark HUD theme (theme.cfg): selectors hud, hud-panel, killfeed, chat, hud.<element>;\n"
                      "   properties background, color, opacity, border, radius, padding, margin, gap, font-size,\n"
                      "   text-align, stripe. Settings > HUD > EDIT HUD writes this file; the game reloads it within a second. */\n";
    for (int k = 0; k < kSelectors; k++) {
        const HudStyle& s = rules_[k];
        if (!s.set) continue;
        out += std::string("\n") + selectorName(k) + " {\n";
        auto line = [&](const char* p, const std::string& v) { out += std::string("    ") + p + ": " + v + ";\n"; };
        auto box = [&](const float* b) { return fmt(b[1]) + " " + fmt(b[2]) + " " + fmt(b[3]) + " " + fmt(b[0]); };
        if (s.has(HudStyle::BACKGROUND)) line("background", themeColorText(s.background));
        if (s.has(HudStyle::COLOR)) line("color", themeColorText(s.color));
        if (s.has(HudStyle::OPACITY)) line("opacity", fmt(s.opacity * 100) + "%");
        if (s.has(HudStyle::BORDER)) line("border", s.borderW <= 0 ? "none" : fmt(s.borderW) + " " + themeColorText(s.border));
        if (s.has(HudStyle::RADIUS)) line("radius", fmt(s.radius));
        if (s.has(HudStyle::PADDING)) line("padding", box(s.pad));
        if (s.has(HudStyle::MARGIN)) line("margin", box(s.margin));
        if (s.has(HudStyle::GAP)) line("gap", fmt(s.gap));
        if (s.has(HudStyle::FONT)) line("font-size", fmt(s.fontScale * 100) + "%");
        if (s.has(HudStyle::ALIGN)) line("text-align", s.align == 0 ? "left" : s.align == 1 ? "center" : "right");
        if (s.has(HudStyle::STRIPE)) line("stripe", s.stripeW <= 0 ? "none" : fmt(s.stripeW) + " " + themeColorText(s.stripe));
        out += "}\n";
    }
    return out;
}

void HudTheme::resolve() {
    for (int el = 0; el < kElems; el++) {
        HudStyle st;
        st.apply(rules_[SEL_HUD]);
        if (el == Settings::HUD_SCORE) st.apply(rules_[SEL_PANEL]);
        if (el == Settings::HUD_KILLFEED) st.apply(rules_[SEL_KILLFEED]);
        if (el == Settings::HUD_CHAT) st.apply(rules_[SEL_CHAT]);
        st.apply(rules_[SEL_ELEM0 + el]);
        resolved_[el] = st;
    }
    version_++;
}

bool HudTheme::empty() const {
    for (const HudStyle& s : rules_) if (s.set) return false;
    return true;
}

void HudTheme::clearRules() {
    for (HudStyle& s : rules_) s = HudStyle();
    resolve();
}

bool HudTheme::pollDue() const { return pollClock() - lastPoll_ >= 1.0; }

void HudTheme::poll(const std::string& path) {
    const double now = pollClock();
    if (now - lastPoll_ < 1.0) return;
    lastPoll_ = now;
    const long long mt = watchedFileKey(path);
    if (mt == mtime_) return;
    mtime_ = mt;
    if (mt == -2) { clearRules(); error_.clear(); return; }   // (no file: the default theme)
    std::ifstream f(utf8Path(path));
    std::stringstream ss;
    ss << f.rdbuf();
    fromText(ss.str());
}

bool HudTheme::save(const std::string& path) {
    if (empty()) {   // (nothing styled: no file, the default look)
        std::error_code ec;
        std::filesystem::remove(utf8Path(path), ec);
        mtime_ = fileWritten(path);
        return true;
    }
    if (!writeFileAtomic(path, toText())) return false;
    mtime_ = fileWritten(path);
    return true;
}
