// Layout engine (src/client/layout.h) and HUD theme (src/client/theme.h) tests: the shared cases in
// tests/data/layout_cases.json, the solve cache, zero heap allocations per frame, and `quark_tests layoutbench`.
// (Included by tests.cpp after server_tests.h: it uses g_countAllocs / g_allocs.)
#pragma once
#include "../src/client/layout.h"
#include "../src/client/theme.h"
#include "../src/client/config.h"
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace layout_tests {

// A small JSON reader for the cases file (numbers, strings, bools, arrays, objects; no escapes beyond \" and \\).
struct J {
    enum K { NUL, NUM, STR, BOOL, ARR, OBJ } k = NUL;
    double n = 0; std::string s; bool b = false;
    std::vector<J> a; std::vector<std::pair<std::string, J>> o;
    const J* get(const char* key) const { for (auto& kv : o) if (kv.first == key) return &kv.second; return nullptr; }
};
struct JParse {
    const std::string& t; size_t i = 0; bool bad = false;
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) i++; }
    std::string str() {
        std::string r; i++;
        while (i < t.size() && t[i] != '"') { if (t[i] == '\\' && i + 1 < t.size()) i++; r += t[i++]; }
        i++; return r;
    }
    J val() {
        ws(); J v;
        if (i >= t.size()) { bad = true; return v; }
        char c = t[i];
        if (c == '{') {
            v.k = J::OBJ; i++; ws();
            while (i < t.size() && t[i] != '}') {
                ws(); std::string key = str(); ws(); if (t[i] == ':') i++;
                v.o.emplace_back(key, val()); ws(); if (t[i] == ',') i++; ws();
            }
            i++;
        } else if (c == '[') {
            v.k = J::ARR; i++; ws();
            while (i < t.size() && t[i] != ']') { v.a.push_back(val()); ws(); if (t[i] == ',') i++; ws(); }
            i++;
        } else if (c == '"') { v.k = J::STR; v.s = str(); }
        else if (t.compare(i, 4, "true") == 0) { v.k = J::BOOL; v.b = true; i += 4; }
        else if (t.compare(i, 5, "false") == 0) { v.k = J::BOOL; i += 5; }
        else if (t.compare(i, 4, "null") == 0) { i += 4; }
        else { v.k = J::NUM; char* e = nullptr; v.n = strtod(t.c_str() + i, &e); if (e == t.c_str() + i) bad = true; i = e - t.c_str(); }
        return v;
    }
};

inline lay::Align alignOf(const std::string& s) {
    return s == "center" ? lay::CENTER : s == "end" ? lay::END : s == "between" ? lay::BETWEEN : s == "stretch" ? lay::STRETCH : lay::START;
}
inline lay::Size sizeOf(const J* v, const J& node, const char* mn, const char* mx) {
    lay::Size s;
    if (v && v->k == J::ARR && !v->a.empty()) {
        const std::string& m = v->a[0].s;
        float x = v->a.size() > 1 ? (float)v->a[1].n : 0;
        s = m == "fixed" ? lay::Size::fixed(x) : m == "grow" ? lay::Size::grow() : m == "pct" ? lay::Size::pct(x) : lay::Size::fit();
    }
    if (const J* q = node.get(mn)) s.min = (float)q->n;
    if (const J* q = node.get(mx)) s.max = (float)q->n;
    return s;
}
// declares a case's tree; names: each box's index by its id
inline void declare(lay::Layout& L, const J& node, std::map<std::string, int>& names) {
    lay::Decl d;
    d.w = sizeOf(node.get("w"), node, "wmin", "wmax");
    d.h = sizeOf(node.get("h"), node, "hmin", "hmax");
    if (const J* v = node.get("dir")) d.dir = v->s == "col" ? lay::COL : lay::ROW;
    if (const J* v = node.get("main")) d.main = alignOf(v->s);
    if (const J* v = node.get("cross")) d.cross = alignOf(v->s);
    if (const J* v = node.get("self")) d.self = (int8_t)alignOf(v->s);
    if (const J* v = node.get("gap")) d.gap = (float)v->n;
    if (const J* v = node.get("pad")) for (int k = 0; k < 4 && k < (int)v->a.size(); k++) d.pad[k] = (float)v->a[k].n;
    if (const J* v = node.get("float"); v && v->a.size() == 6)
        d.floatAt((float)v->a[0].n, (float)v->a[1].n, (float)v->a[2].n, (float)v->a[3].n, (float)v->a[4].n, (float)v->a[5].n);
    if (const J* v = node.get("clip")) d.clip = v->b;
    if (const J* v = node.get("scroll"); v && v->a.size() == 2) { d.scrollX = (float)v->a[0].n; d.scrollY = (float)v->a[1].n; }
    const std::string& name = node.get("id")->s;
    names[name] = L.open(lay::id(name.c_str()), d, nullptr);
    if (const J* ch = node.get("children")) for (auto& c : ch->a) declare(L, c, names);
    L.close();
}

}   // namespace layout_tests

// The shared cases: the C++ solver gives every expected rect (and culling) exactly.
void testLayoutCases() {
    using namespace layout_tests;
    std::string text = readFile(testDataDir() + "/layout_cases.json");
    JParse p{text};
    J root = p.val();
    const J* cases = root.get("cases");
    CHECK(!p.bad && cases && cases->a.size() >= 10);
    if (!cases) return;
    auto L = std::make_unique<lay::Layout>();
    int checked = 0;
    for (const J& c : cases->a) {
        const std::string& name = c.get("name")->s;
        const J* scr = c.get("screen");
        L->begin((float)scr->a[0].n, (float)scr->a[1].n);
        std::map<std::string, int> names;
        if (const J* t = c.get("tree")) declare(*L, *t, names);
        if (const J* ts = c.get("trees")) for (auto& t : ts->a) declare(*L, t, names);
        L->solve();
        for (auto& kv : c.get("expect")->o) {
            auto it = names.find(kv.first);
            if (it == names.end() || it->second < 0) { printf("FAIL layout case '%s': no box %s\n", name.c_str(), kv.first.c_str()); g_fail++; continue; }
            const lay::Rect& r = L->rect(it->second);
            const float want[4] = {(float)kv.second.a[0].n, (float)kv.second.a[1].n, (float)kv.second.a[2].n, (float)kv.second.a[3].n};
            const float got[4] = {r.x, r.y, r.w, r.h};
            bool ok = true;
            for (int k = 0; k < 4; k++) ok &= std::fabs(want[k] - got[k]) < 0.01f;
            if (!ok) {
                printf("FAIL layout case '%s': %s is [%g, %g, %g, %g], want [%g, %g, %g, %g]\n", name.c_str(), kv.first.c_str(),
                       got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
                g_fail++;
            }
            checked++;
        }
        if (const J* vis = c.get("visible"))
            for (auto& kv : vis->o) {
                auto it = names.find(kv.first);
                if (it == names.end() || L->visible(it->second) != kv.second.b) {
                    printf("FAIL layout case '%s': %s visible should be %d\n", name.c_str(), kv.first.c_str(), (int)kv.second.b);
                    g_fail++;
                }
            }
    }
    printf("layout: %d boxes checked in %zu cases\n", checked, cases->a.size());
}

// A HUD-sized declaration (what Game::hudLayout declares: about 40 boxes), for the cache and the benchmark.
static void declareHudLike(lay::Layout& L, float W, float H, float wobble) {
    using namespace lay;
    const float s = H / 720.0f;
    L.begin(W, H);
    // the score box: score, title column, score
    L.open(id("score"), Decl().row().size(Size::fit(440 * s, W - 16 * s), Size::fit(64 * s)).padding(20 * s, 8 * s, 20 * s, 8 * s).spacing(12 * s)
                          .align(BETWEEN, START).floatAt(0.5f, 0, 0.5f, 0, 0, 8 * s));
    L.leaf(id("scoreA"), Decl().size(Size::fixed(40 * s), Size::fixed(36 * s)).alignSelf(CENTER));
    L.open(id("mid"), Decl().col().align(START, CENTER));
    L.leaf(id("title"), Decl().size(Size::fixed(160 * s + wobble), Size::fit(22 * s)));
    L.leaf(id("phase"), Decl().size(Size::fixed(220 * s), Size::fixed(16 * s)));
    L.close();
    L.leaf(id("scoreB"), Decl().size(Size::fixed(40 * s), Size::fixed(36 * s)).alignSelf(CENTER));
    L.close();
    // the corner elements
    L.leaf(id("health"), Decl().size(Size::fixed(200 * s), Size::fixed(50 * s)).floatAt(0, 1, 0, 1, 24 * s, -20 * s));
    L.leaf(id("weapon"), Decl().size(Size::fixed(90 * s), Size::fixed(24 * s)).floatAt(1, 1, 1, 1, -24 * s, -26 * s));
    L.open(id("slots"), Decl().row().spacing(14 * s).floatAt(1, 1, 1, 1, -38 * s, -62 * s));
    for (uint32_t k = 0; k < 10; k++) L.leaf(id(id("slot"), k), Decl().size(Size::fixed(60 * s), Size::fixed(12 * s)));
    L.close();
    L.open(id("bars"), Decl().col().spacing(11 * s).floatAt(0.5f, 0.5f, 0.5f, 0, 0, 35 * s));
    for (uint32_t k = 0; k < 3; k++) L.leaf(id(id("bar"), k), Decl().size(Size::fixed(140 * s), Size::fixed(7 * s)));
    L.close();
    L.open(id("feed"), Decl().col().spacing(2 * s).align(START, END).floatAt(1, 0, 1, 0, -8 * s, 9 * s));
    for (uint32_t k = 0; k < 6; k++) L.leaf(id(id("line"), k), Decl().size(Size::fixed(200 * s), Size::fixed(22 * s)));
    L.close();
    L.open(id("chat"), Decl().col().spacing(4 * s).floatAt(0, 1, 0, 1, 24 * s, -110 * s));
    for (uint32_t k = 0; k < 8; k++) L.leaf(id(id("chatline"), k), Decl().size(Size::fixed(300 * s), Size::fixed(16 * s)));
    L.close();
}

// The solve is cached (the same declaration twice: solved once), a change re-solves, and declaring + solving a
// HUD-sized layout makes no heap allocation.
void testLayoutCache() {
    auto L = std::make_unique<lay::Layout>();
    declareHudLike(*L, 1920, 1080, 0);
    CHECK(L->solve());
    const long n = L->solves();
    declareHudLike(*L, 1920, 1080, 0);
    CHECK(!L->solve() && L->solves() == n);          // unchanged: the last result stands
    declareHudLike(*L, 1920, 1080, 3);
    CHECK(L->solve() && L->solves() == n + 1);       // one width changed
    declareHudLike(*L, 1280, 720, 3);
    CHECK(L->solve());                                // the screen changed
    const int title = L->find(lay::id("title"));
    CHECK(title >= 0 && L->rect(title).w > 160);
    CHECK(!L->overflowed());
    // overflow: past capacity() the extra boxes (and their children) are dropped, and open / close stay paired
    L->begin(100, 100);
    L->open(1, lay::Decl().col());
    for (int k = 0; k < L->capacity() + 10; k++) { L->open(lay::id(7, k), lay::Decl()); L->leaf(lay::id(9, k), lay::Decl()); L->close(); }
    L->close();
    L->solve();
    CHECK(L->overflowed() && L->count() == L->capacity());
    // same(): a page skips declaring while its key is unchanged (and only once something was solved)
    {
        lay::Layout C(4);
        const float k1[] = {1, 2, 3}, k2[] = {1, 2, 4};
        CHECK(!C.same(k1, sizeof k1));                 // nothing solved yet
        C.begin(10, 10); C.leaf(1, lay::Decl().size(lay::Size::fixed(3), lay::Size::fixed(3))); C.solve();
        CHECK(C.same(k1, sizeof k1) && C.rect(0).w == 3);
        CHECK(!C.same(k2, sizeof k2));                 // changed: declare again
        C.begin(10, 10); C.leaf(1, lay::Decl().size(lay::Size::fixed(4), lay::Size::fixed(3))); C.solve();
        CHECK(C.same(k2, sizeof k2) && C.rect(0).w == 4);
        float big[lay::Layout::kMaxKey / sizeof(float) + 1] = {};
        CHECK(!C.same(big, sizeof big) && !C.same(big, sizeof big));   // too long: never the same
    }
    // one declaration at a time per thread (the shared scratch): a Layout begun in the middle of another's declaration
    // leaves that one's last result standing, and it works it out again next time
    {
        lay::Layout A(8), B(8);
        auto declA = [&](float w) { A.begin(100, 100); A.open(1, lay::Decl()); A.leaf(2, lay::Decl().size(lay::Size::fixed(w), lay::Size::fixed(5))); A.close(); };
        declA(10);
        CHECK(A.solve() && A.rect(1).w == 10);
        declA(20);
        B.begin(50, 50); B.leaf(3, lay::Decl().size(lay::Size::fixed(7), lay::Size::fixed(7)));
        CHECK(B.solve() && B.rect(0).w == 7);
        CHECK(!A.solve() && A.rect(1).w == 10 && A.count() == 2);   // interrupted: the last result
        declA(20);
        CHECK(A.solve() && A.rect(1).w == 20);
    }
    // zero heap allocations: declare + solve, changed or not
    g_allocs = 0;
    g_countAllocs = true;
    for (int k = 0; k < 200; k++) { declareHudLike(*L, 1920, 1080, (float)(k % 3)); L->solve(); }
    g_countAllocs = false;
    CHECK(g_allocs.load() == 0);
    if (g_allocs.load()) printf("FAIL layout: %lld heap allocations in 200 frames\n", (long long)g_allocs.load());
}

// `quark_tests layoutbench`: the HUD-sized layout's cost per frame, declared and solved (a changed value every frame:
// the worst case) and declared with the cached solve (the usual case), plus heap allocations.
int layoutBench() {
    auto L = std::make_unique<lay::Layout>();
    const int frames = 200000;
    auto run = [&](bool change) {
        g_allocs = 0;
        g_countAllocs = true;
        auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < frames; k++) { declareHudLike(*L, 1920, 1080, change ? (float)(k & 7) : 0); L->solve(); }
        double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / frames;
        g_countAllocs = false;
        printf("layoutbench %-28s %6.3f us / frame  (%d boxes, %.2f heap allocations / frame)\n", change ? "declare + solve (changed):" : "declare + cached solve:",
               us, L->count(), (double)g_allocs.load() / frames);
        return us;
    };
    run(true);
    double solved = run(true), cached = run(false);
    printf("layoutbench: target under 5 us for the HUD: %s\n", solved < 5 ? "OK" : "OVER");
    return solved < 5 && cached < 5 ? 0 : 1;
}

// theme.cfg: selectors and the cascade, every property, clamps, colours, errors (a skipped declaration, a broken file
// giving the default theme), and toText() reading back the same.
void testHudTheme() {
    HudTheme t;
    CHECK(t.empty());
    const char* text =
        "/* a comment { with braces } */\n"
        "hud { radius: 4; opacity: 80%; }\n"
        "hud-panel { background: #0d0f14 55%; stripe: 3 enemy; }\n"
        "hud.health, hud.weapon {\n"
        "  background: rgba(0, 0, 0, 0.4); border: 1 #fff 30%;\n"
        "  padding: 2 4; margin: 0 0 10 0; color: team;\n"
        "}\n"
        "killfeed { text-align: left; gap: 99; font-size: 120%; padding: 30px; }\n"
        "health { radius: 6px }\n";
    CHECK(t.fromText(text) == 5);
    CHECK(t.error().empty());
    if (!t.error().empty()) printf("  theme error: %s\n", t.error().c_str());
    const HudStyle& h = t.style(Settings::HUD_HEALTH);
    CHECK(h.has(HudStyle::RADIUS) && h.radius == 6);                 // (hud.health over hud, a later rule over an earlier)
    CHECK(h.has(HudStyle::OPACITY) && std::fabs(h.opacity - 0.8f) < 1e-6f);
    CHECK(h.has(HudStyle::BACKGROUND) && std::fabs(h.background.c.a - 0.4f) < 1e-6f && h.background.c.r == 0);
    CHECK(h.borderW == 1 && std::fabs(h.border.c.a - 0.3f) < 1e-6f && h.border.c.g == 1);
    CHECK(h.pad[0] == 4 && h.pad[1] == 2 && h.pad[2] == 4 && h.pad[3] == 2);   // (CSS order: top right bottom left)
    CHECK(h.margin[3] == 10 && h.margin[0] == 0);
    CHECK(h.color.kind == ThemeColor::TEAM);
    CHECK(!h.has(HudStyle::STRIPE));
    const HudStyle& w = t.style(Settings::HUD_WEAPON);
    CHECK(w.radius == 4 && w.has(HudStyle::PADDING));
    const HudStyle& sc = t.style(Settings::HUD_SCORE);
    CHECK(sc.has(HudStyle::STRIPE) && sc.stripeW == 3 && sc.stripe.kind == ThemeColor::ENEMY && !sc.has(HudStyle::PADDING));
    const HudStyle& kf = t.style(Settings::HUD_KILLFEED);
    CHECK(kf.align == 0 && kf.gap == 48 && std::fabs(kf.fontScale - 1.2f) < 1e-6f && kf.pad[0] == 24);   // (clamped: gap 48, padding 24)
    CHECK(!t.style(Settings::HUD_CHAT).has(HudStyle::GAP) && t.style(Settings::HUD_CHAT).radius == 4);
    // round trip
    HudTheme u;
    CHECK(u.fromText(t.toText()) > 0 && u.error().empty());
    for (int el = 0; el < HudTheme::kElems; el++) CHECK(u.style(el) == t.style(el));
    for (int k = 0; k < HudTheme::kSelectors; k++) CHECK(u.rule(k) == t.rule(k));
    // a bad declaration is skipped (the rest of the rule stands), with the line in the error
    HudTheme b;
    CHECK(b.fromText("hud.speed {\n  radius: 3;\n  wobble: 2;\n  color: #12345;\n}\nnosuch { radius: 1; }\n") == 1);
    CHECK(b.style(Settings::HUD_SPEED).radius == 3 && !b.style(Settings::HUD_SPEED).has(HudStyle::COLOR));
    CHECK(b.error().find("LINE 3") != std::string::npos);
    // a broken file: the default theme, and an error
    HudTheme x;
    x.fromText("hud { radius: 2; }\nhud.health { radius: 5;\n");
    CHECK(x.empty() && !x.error().empty());
    // colours
    ThemeColor c;
    CHECK(parseThemeColor("#abc", c) && std::fabs(c.c.r - 0xaa / 255.0f) < 1e-6f && c.c.a == 1);
    CHECK(parseThemeColor("#11223380", c) && std::fabs(c.c.a - 0x80 / 255.0f) < 1e-6f);
    CHECK(parseThemeColor("rgb(255, 0, 0) 50%", c) && c.c.r == 1 && std::fabs(c.c.a - 0.5f) < 1e-6f);
    CHECK(parseThemeColor("none", c) && c.c.a == 0);
    CHECK(parseThemeColor("WHITE", c) && c.c.g == 1);
    CHECK(!parseThemeColor("#12", c) && !parseThemeColor("rgb(1,2)", c) && !parseThemeColor("blurple", c) && !parseThemeColor("#fff 40", c));
    CHECK(themeColorText(ThemeColor()) == "#ffffff");
    // clearRules: nothing left; the version moves on every change (what caches compare)
    const uint32_t v = t.version();
    t.clearRules();
    CHECK(t.empty() && t.version() != v);
}
