// The on-screen HUD editor: Settings > HUD > EDIT ON SCREEN (screen path settings/hud/edit). Every 2D element of the
// HUD drawn where it goes (sample values: the score box, kill feed and chat too), to click, drag (snapping to the
// screen's edges and centre and to the other elements, with guides; Alt: no snapping), size (a corner handle or the
// wheel) and nudge (arrow keys, Shift: 10); and a compact inspector for the element's settings (quark.cfg hud_N) and
// its style (theme.cfg: the element's own rule, hud.<name>). Every change is an undo step (Ctrl+Z / Ctrl+Y), a drag or a
// slider's whole gesture one step. Nothing here runs while the editor is closed (Game::hudEdit_ is the only check).
#include "game.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
// the editor's elements, in the ELEMENT list's order (the 3D models are moved on Settings > HUD itself)
const int kEdEls[] = {Settings::HUD_SCORE, Settings::HUD_KILLFEED, Settings::HUD_CHAT, Settings::HUD_HEALTH, Settings::HUD_WEAPON,
                      Settings::HUD_SLOTS, Settings::HUD_BARS, Settings::HUD_SPEED, Settings::HUD_KEYS};
constexpr int kEdN = sizeof(kEdEls) / sizeof(kEdEls[0]);
const std::vector<std::string>& edNames() {
    static const std::vector<std::string> n = {"ELEMENT: SCORE BOX", "ELEMENT: KILL FEED", "ELEMENT: CHAT", "ELEMENT: HEALTH", "ELEMENT: WEAPON NAME",
                                               "ELEMENT: WEAPON LIST", "ELEMENT: COOLDOWN BARS", "ELEMENT: SPEED METER", "ELEMENT: KEY OVERLAY"};
    return n;
}
int edIndex(int el) { for (int i = 0; i < kEdN; i++) if (kEdEls[i] == el) return i; return -1; }

// each element's built-in look, what its theme sliders show while unset (and go back to on a right-click)
struct Builtin { float padX = 0, padY = 0, gap = -1; Color bg = {0, 0, 0, 0}; int align = -1; };
Builtin builtin(int el) {
    Builtin b;
    switch (el) {
        case Settings::HUD_BARS: b.gap = 11; break;
        case Settings::HUD_SLOTS: b.gap = 14; break;
        case Settings::HUD_KEYS: b.gap = 4; break;
        case Settings::HUD_SCORE: b.padX = 20; b.padY = 8; b.gap = 12; b.bg = {0.05f, 0.06f, 0.08f, 0.55f}; break;
        case Settings::HUD_KILLFEED: b.padX = 6; b.padY = 3; b.gap = 2; b.bg = {0, 0, 0, 0.4f}; b.align = 2; break;
        case Settings::HUD_CHAT: b.padX = 6; b.padY = 4; b.gap = 4; b.align = 0; break;
    }
    return b;
}
bool sameElem(const Settings::HudElem& a, const Settings::HudElem& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && a.custom == b.custom && a.hidden == b.hidden && a.z == b.z && a.pitch == b.pitch &&
           a.yaw == b.yaw && a.roll == b.roll && a.color.x == b.color.x && a.color.y == b.color.y && a.color.z == b.color.z;
}
void outlineRect(Renderer& r, float x, float y, float w, float h, float t, Color c) {
    r.rect(x, y, w, t, c); r.rect(x, y + h - t, w, t, c);
    r.rect(x, y + t, t, h - 2 * t, c); r.rect(x + w - t, y + t, t, h - 2 * t, c);
}
}   // namespace

void Game::hudEditOpen() {
    hudEdit_ = true;
    if (edIndex(hudEdSel_) < 0) hudEdSel_ = Settings::HUD_HEALTH;
    hudEdDrag_ = 0;
    hudUndo_.clear(); hudRedo_.clear();
    if (themePath_.empty()) themePath_ = userDataPath("theme.cfg");
    theme_.poll(themePath_);
    for (int i = 0; i < Settings::HUD_COUNT; i++) hudEdLast_.hud[i] = cfg_.hud[i];
    hudEdLast_.theme = theme_.toText();
    hudEdLastVer_ = theme_.version();
    ui_.closePopup();
}

void Game::hudEditClose() {
    if (!hudEdit_) return;
    hudEdCommit(true);
    hudEdit_ = false;
    hudEdDrag_ = 0;
    hudUndo_.clear(); hudRedo_.clear();
    cfg_.save();
}

// A change made since the last step (an element's settings or the theme) becomes an undo step once it's finished: not
// while a drag, a slider or a colour picker is still going (the whole gesture is one step). The theme is saved with it.
void Game::hudEdCommit(bool force) {
    if (!force && (hudEdDrag_ || ui_.sliderHeld() || ui_.popupOpen())) return;
    bool hud = false;
    for (int i = 0; i < Settings::HUD_COUNT; i++) hud |= !sameElem(cfg_.hud[i], hudEdLast_.hud[i]);
    if (!hud && theme_.version() == hudEdLastVer_) return;
    std::string t = theme_.toText();
    const bool themeChanged = t != hudEdLast_.theme;
    hudEdLastVer_ = theme_.version();
    if (!hud && !themeChanged) return;   // (the theme file read again, the same)
    hudUndo_.push_back(hudEdLast_);
    if (hudUndo_.size() > 64) hudUndo_.erase(hudUndo_.begin());
    hudRedo_.clear();
    for (int i = 0; i < Settings::HUD_COUNT; i++) hudEdLast_.hud[i] = cfg_.hud[i];
    hudEdLast_.theme.swap(t);
    if (themeChanged) theme_.save(themePath_);
}

void Game::hudEdApply(const HudSnap& sn) {
    for (int i = 0; i < Settings::HUD_COUNT; i++) cfg_.hud[i] = sn.hud[i];
    if (sn.theme != hudEdLast_.theme) { theme_.fromText(sn.theme); theme_.save(themePath_); }
    hudEdLast_ = sn;
    hudEdLastVer_ = theme_.version();
}

bool Game::hudEdUndo(bool redo) {
    hudEdCommit(false);
    auto& from = redo ? hudRedo_ : hudUndo_;
    auto& to = redo ? hudUndo_ : hudRedo_;
    if (from.empty() || hudEdDrag_) return false;
    to.push_back(hudEdLast_);
    HudSnap sn = std::move(from.back());
    from.pop_back();
    hudEdApply(sn);
    return true;
}

// The editor's keys (Game::onKey, while it's open and no field is being typed in): arrows nudge the selection (Shift:
// 10 units; held: repeats), + / - the size, Tab / Shift+Tab the next / previous element, Delete hides or shows it, Ctrl+Z / Ctrl+Y
// (Ctrl+Shift+Z) undo and redo.
bool Game::hudEditKey(int key, int action, int mods) {
    const bool ctrl = (mods & GLFW_MOD_CONTROL) || (mods & GLFW_MOD_SUPER);
    const int el = hudEdSel_;
    if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT || key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) {
        if (el < 0 || hudEdDrag_) return true;
        const float d = (mods & GLFW_MOD_SHIFT) ? 10.0f : 1.0f;
        auto& e = cfg_.hud[el];
        if (key == GLFW_KEY_LEFT) e.x -= d; else if (key == GLFW_KEY_RIGHT) e.x += d;
        else if (key == GLFW_KEY_UP) e.y -= d; else e.y += d;
        e.x = clampf(std::round(e.x), -1280, 1280); e.y = clampf(std::round(e.y), -720, 720);
        return true;
    }
    if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_ADD || key == GLFW_KEY_KP_SUBTRACT) {
        if (el < 0 || hudEdDrag_ || ctrl) return el >= 0;   // (+ / -: the size, 0.05 a press; held: repeats)
        auto& e = cfg_.hud[el];
        const bool up = key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD;
        e.scale = clampf(std::round((e.scale + (up ? 0.05f : -0.05f)) * 100) / 100, 0.5f, 3);
        return true;
    }
    if (action != GLFW_PRESS) return false;
    if (key == GLFW_KEY_TAB) {
        int i = std::max(0, edIndex(el));
        hudEdSel_ = kEdEls[(i + ((mods & GLFW_MOD_SHIFT) ? kEdN - 1 : 1)) % kEdN];
        return true;
    }
    if (key == GLFW_KEY_DELETE && el >= 0) {
        // (the score box can't be hidden in ranked: it stays drawn there anyway)
        cfg_.hud[el].hidden = !cfg_.hud[el].hidden;
        return true;
    }
    if (ctrl && key == GLFW_KEY_Z) { hudEdUndo(mods & GLFW_MOD_SHIFT); return true; }
    if (ctrl && key == GLFW_KEY_Y) { hudEdUndo(true); return true; }
    return false;
}

void Game::drawHudEditor() {
    const float W = (float)r_.width(), H = (float)r_.height(), s = H / 720.0f, us = ui_.s();
    if (themePath_.empty()) themePath_ = userDataPath("theme.cfg");
    if (theme_.pollDue()) theme_.poll(themePath_);
    if (edIndex(hudEdSel_) < 0) hudEdSel_ = Settings::HUD_HEALTH;
    // ---- the inspector's place: on the right, or on the left when the selection is under it (not mid-drag)
    // (its height: the rows the selected element has - a GAP / TEXT ALIGN row only for elements of several parts or lines)
    const float pw = 320 * us, rowH = 22 * us, gap = 4 * us;
    const Builtin b0 = builtin(hudEdSel_);
    const int rows = 5 + 4 + (b0.gap >= 0 || b0.align >= 0 ? 1 : 0) + 1 + 3;
    const float ph = rows * (rowH + gap) + (20 + 6 + 44) * us;   // (+ the header, a space, the hint: up to 3 lines)
    const float py = std::max(108 * us, std::min(H - ph - 96 * us, (H - ph) * 0.45f));
    if (!hudEdDrag_) {
        const lay::Rect& sr = hudRect_[hudEdSel_];
        const float rx = W - pw - 16 * us;
        const bool under = sr.x + sr.w > rx && sr.x < W && sr.y + sr.h > py && sr.y < py + ph;
        const bool underL = sr.x < 16 * us + pw && sr.x + sr.w > 0 && sr.y + sr.h > py && sr.y < py + ph;
        if (under && !underL) hudEdLeft_ = true;
        else if (underL && !under) hudEdLeft_ = false;
    }
    const float px = hudEdLeft_ ? 16 * us : W - pw - 16 * us;
    const bool overPanel = uiIn_.mx >= px - 8 * us && uiIn_.mx <= px + pw + 8 * us && uiIn_.my >= py - 8 * us && uiIn_.my <= py + ph + 8 * us;

    // ---- every element, its box; the hidden ones only as a dashed outline
    r_.rect(W * 0.5f - 0.5f, 0, 1, H, {1, 1, 1, 0.06f});
    r_.rect(0, H * 0.5f - 0.5f, W, 1, {1, 1, 1, 0.06f});
    drawCrosshair(W * 0.5f, H * 0.5f, s, false);
    lay::Rect box[kEdN];
    for (int i = 0; i < kEdN; i++) {
        const int el = kEdEls[i];
        const bool shown = !cfg_.hud[el].hidden;
        const HudRect rc = drawHudElement(el, W, H, s, true, shown);
        box[i] = {rc.x, rc.y, rc.w, rc.h};
        if (!shown) box[i] = hudRect_[el];
    }

    // ---- mouse: select, move, size (a corner of the selection), the wheel
    const float hs = 7 * us;   // (corner handles)
    const int si = edIndex(hudEdSel_);
    auto corner = [&](int c) { const lay::Rect& b = box[si]; return lay::Rect{(c & 1) ? b.x + b.w - hs * 0.5f : b.x - hs * 0.5f, (c & 2) ? b.y + b.h - hs * 0.5f : b.y - hs * 0.5f, hs, hs}; };
    auto hitEl = [&](float mx, float my) {
        for (int i = kEdN - 1; i >= 0; i--) {
            const lay::Rect& b = box[i];
            if (mx >= b.x - 3 * us && mx <= b.x + b.w + 3 * us && my >= b.y - 3 * us && my <= b.y + b.h + 3 * us) return i;
        }
        return -1;
    };
    if (uiIn_.pressed && !overPanel && !ui_.popupOpen()) {
        int hitCorner = -1;
        for (int c = 0; c < 4 && si >= 0; c++) {
            const lay::Rect q = corner(c);
            if (uiIn_.mx >= q.x - 3 * us && uiIn_.mx <= q.x + q.w + 3 * us && uiIn_.my >= q.y - 3 * us && uiIn_.my <= q.y + q.h + 3 * us) hitCorner = c;
        }
        const int h = hitCorner >= 0 ? si : hitEl(uiIn_.mx, uiIn_.my);
        if (h >= 0) {
            hudEdSel_ = kEdEls[h];
            hudEdDrag_ = hitCorner >= 0 ? 2 : 1;
            hudEdX0_ = uiIn_.mx; hudEdY0_ = uiIn_.my;
            hudEdOrig_ = cfg_.hud[hudEdSel_];
            hudEdRect0_ = box[h];
        }
    }
    if (!uiIn_.down) hudEdDrag_ = 0;
    float guideX = -1, guideY = -1;
    if (hudEdDrag_ == 1) {
        float dx = uiIn_.mx - hudEdX0_, dy = uiIn_.my - hudEdY0_;
        const bool snap = glfwGetKey(win_, GLFW_KEY_LEFT_ALT) != GLFW_PRESS && glfwGetKey(win_, GLFW_KEY_RIGHT_ALT) != GLFW_PRESS;
        if (snap) {   // the box's edges and centre to the screen's and to the other elements' (the nearest within 6 px)
            const lay::Rect& b0 = hudEdRect0_;
            float bestX = 6 * us, bestY = 6 * us, sx = 0, sy = 0;
            auto tryAxis = [&](float a0, float len, float d, float target, float& best, float& snapD, float& guide) {
                for (float f : {0.0f, 0.5f, 1.0f}) {
                    const float diff = target - (a0 + len * f + d);
                    if (std::fabs(diff) < best) { best = std::fabs(diff); snapD = diff; guide = target; }
                }
            };
            for (float t : {0.0f, W * 0.5f, W}) tryAxis(b0.x, b0.w, dx, t, bestX, sx, guideX);
            for (float t : {0.0f, H * 0.5f, H}) tryAxis(b0.y, b0.h, dy, t, bestY, sy, guideY);
            for (int i = 0; i < kEdN; i++) {
                if (kEdEls[i] == hudEdSel_ || cfg_.hud[kEdEls[i]].hidden) continue;
                const lay::Rect& o = box[i];
                for (float t : {o.x, o.x + o.w * 0.5f, o.x + o.w}) tryAxis(b0.x, b0.w, dx, t, bestX, sx, guideX);
                for (float t : {o.y, o.y + o.h * 0.5f, o.y + o.h}) tryAxis(b0.y, b0.h, dy, t, bestY, sy, guideY);
            }
            dx += sx; dy += sy;
        }
        auto& e = cfg_.hud[hudEdSel_];
        e.x = clampf(std::round(hudEdOrig_.x + dx / s), -1280, 1280);
        e.y = clampf(std::round(hudEdOrig_.y + dy / s), -720, 720);
    } else if (hudEdDrag_ == 2) {   // a corner: the size follows the distance from the box's centre
        const lay::Rect& b0 = hudEdRect0_;
        const float cx = b0.x + b0.w * 0.5f, cy = b0.y + b0.h * 0.5f;
        const float d0 = std::max(4.0f, std::hypot(hudEdX0_ - cx, hudEdY0_ - cy)), d1 = std::hypot(uiIn_.mx - cx, uiIn_.my - cy);
        cfg_.hud[hudEdSel_].scale = clampf(std::round(hudEdOrig_.scale * d1 / d0 * 100) / 100, 0.5f, 3);
    }
    if (hudEdWheel_ != 0) {
        const int h = overPanel ? -1 : hitEl(uiIn_.mx, uiIn_.my);
        const int el = h >= 0 ? kEdEls[h] : hudEdSel_;
        if (!hudEdDrag_ && !overPanel) {
            hudEdSel_ = el;
            auto& e = cfg_.hud[el];
            e.scale = clampf(std::round(e.scale * (float)std::pow(1.05, hudEdWheel_) * 100) / 100, 0.5f, 3);
        }
        hudEdWheel_ = 0;
    }

    // ---- outlines, the selection's corners, the snapping guides
    const float t = std::max(1.0f, std::round(us));
    const int hov = overPanel || hudEdDrag_ ? -1 : hitEl(uiIn_.mx, uiIn_.my);
    for (int i = 0; i < kEdN; i++) {
        const lay::Rect& b = box[i];
        const bool sel = kEdEls[i] == hudEdSel_, hidden = cfg_.hud[kEdEls[i]].hidden;
        const Color c = sel ? ui_.accent : Color{1, 1, 1, i == hov ? 0.6f : hidden ? 0.2f : 0.3f};
        if (hidden) {   // dashed
            const float d = 6 * us;
            for (float u = 0; u < b.w; u += 2 * d) { float l = std::min(d, b.w - u); r_.rect(b.x + u, b.y, l, t, c); r_.rect(b.x + u, b.y + b.h - t, l, t, c); }
            for (float u = 0; u < b.h; u += 2 * d) { float l = std::min(d, b.h - u); r_.rect(b.x, b.y + u, t, l, c); r_.rect(b.x + b.w - t, b.y + u, t, l, c); }
        } else outlineRect(r_, b.x - t, b.y - t, b.w + 2 * t, b.h + 2 * t, t, c);
    }
    if (si >= 0) for (int c = 0; c < 4; c++) { const lay::Rect q = corner(c); r_.rect(q.x, q.y, q.w, q.h, ui_.accent); }
    const Color guide = {1.0f, 0.35f, 0.75f, 0.9f};
    if (guideX >= 0) r_.rect(guideX - t * 0.5f, 0, t, H, guide);
    if (guideY >= 0) r_.rect(0, guideY - t * 0.5f, W, t, guide);

    // the UI test: each element is a handle (dragged: it moves), the selection's corner a size handle; the keys
    if (ui_.probing()) {
        for (int i = 0; i < kEdN; i++) {
            const lay::Rect& b = box[i];
            const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
            const bool covered = cx >= px - 8 * us && cx <= px + pw + 8 * us && cy >= py - 8 * us && cy <= py + ph + 8 * us;
            if (covered || hitEl(cx, cy) != i || cx < 0 || cx > W || cy < 0 || cy > H) continue;   // (only what a click there grabs)
            static std::string labels[kEdN];
            if (labels[i].empty()) labels[i] = "MOVE " + edNames()[i].substr(9);
            ui_.handle(b.x, b.y, b.w, b.h, cx > W * 0.5f ? -1.0f : 1.0f, 0, labels[i], UiExp::cfg("hud_*"));
        }
        if (si >= 0) {
            const lay::Rect q = corner(3);
            const float cx = q.x + q.w * 0.5f, cy = q.y + q.h * 0.5f;
            if (!(cx >= px - 8 * us && cx <= px + pw + 8 * us && cy >= py - 8 * us && cy <= py + ph + 8 * us) && cx < W - 4 && cy < H - 4)
                ui_.handle(q.x, q.y, q.w, q.h, 0.7f, 0.7f, "SIZE (CORNER)", UiExp::cfg("hud_*"));
        }
        if (!ui_.typing()) {
            ui_.keyAction("ARROW (NUDGE)", GLFW_KEY_RIGHT, 0, UiExp::cfg("hud_*"));
            ui_.keyAction("SHIFT+ARROW (NUDGE 10)", GLFW_KEY_DOWN, GLFW_MOD_SHIFT, UiExp::cfg("hud_*"));
            ui_.keyAction("+ (BIGGER)", GLFW_KEY_EQUAL, 0, UiExp::cfg("hud_*"));
            ui_.keyAction("TAB (NEXT ELEMENT)", GLFW_KEY_TAB, 0, UiExp::state("hudedsel"));
            ui_.keyAction("DELETE (HIDE / SHOW)", GLFW_KEY_DELETE, 0, UiExp::cfg("hud_*"));
            ui_.keyAction("CTRL+Z (UNDO)", GLFW_KEY_Z, GLFW_MOD_CONTROL, hudUndo_.empty() ? UiExp::inert() : UiExp::state("hudedit"));
            ui_.keyAction("CTRL+Y (REDO)", GLFW_KEY_Y, GLFW_MOD_CONTROL, hudRedo_.empty() ? UiExp::inert() : UiExp::state("hudedit"));
        }
    }

    // ---- the inspector
    const int el = hudEdSel_;
    auto& e = cfg_.hud[el];
    const auto& de = defaultSettings().hud[el];
    const HudStyle& st = theme_.style(el);
    HudStyle& rule = theme_.rule(HudTheme::SEL_ELEM0 + el);
    const Builtin bi = builtin(el);
    const UiExp he = UiExp::cfg("hud_*"), th = UiExp::state("theme");
    ui_.panel(px - 8 * us, py - 8 * us, pw + 16 * us, ph + 16 * us, 0.92f);
    const float hw = (pw - gap) * 0.5f, x2 = px + hw + gap;
    float y = py;
    auto next = [&] { y += rowH + gap; };
    auto setRule = [&](HudStyle::Prop p, bool on) { if (on) rule.set |= p; else rule.clear(p); theme_.resolve(); };
    int idx = std::max(0, edIndex(el));
    if (ui_.cycle(px, y, pw, rowH, "ELEMENT", idx, kEdN, edNames(), UiExp::state("hudedsel").withId("HUD EDIT ELEMENT"))) hudEdSel_ = kEdEls[idx];
    next();
    bool shown = !e.hidden;
    ui_.toggle(px, y, hw, rowH, "SHOW", shown, he);
    e.hidden = !shown;
    if (ui_.button(x2, y, hw, rowH, "RESET ELEMENT", he, !sameElem(e, de))) e = de;
    next();
    ui_.slider(px, y, hw, rowH, "X", e.x, -640, 640, "%.0f", he, de.x); e.x = clampf(std::round(e.x), -1280, 1280);
    ui_.slider(x2, y, hw, rowH, "Y", e.y, -360, 360, "%.0f", he, de.y); e.y = clampf(std::round(e.y), -720, 720);
    next();
    ui_.slider(px, y, hw, rowH, "SIZE", e.scale, 0.5f, 3, "%.2f", he, de.scale); e.scale = clampf(e.scale, 0.5f, 3);
    {   // the element's opacity (theme; ranked: at least 30%)
        float v = (st.has(HudStyle::OPACITY) ? st.opacity : 1.0f) * 100;
        if (ui_.slider(x2, y, hw, rowH, "OPACITY %", v, 0, 100, "%.0f", th, 100)) {
            rule.opacity = clampf(v, 0, 100) / 100;
            setRule(HudStyle::OPACITY, v != 100);
        }
    }
    next();
    ui_.toggle(px, y, hw, rowH, "OWN COLOR", e.custom, he);
    ui_.colorPicker(x2, y, hw, rowH, "COLOR", e.color, he, de.color);
    next();
    ui_.label(px, y + 2 * us, "STYLE (THEME.CFG)", ui_.accent, 1.0f, pw);
    y += 20 * us;
    const Color fc = rgba(cfg_.colorFriendly), ec = rgba(cfg_.colorEnemy);
    {   // background: its colour and opacity
        const Color bg = st.has(HudStyle::BACKGROUND) ? st.background.get(fc, ec) : bi.bg;
        Vec3 c = {bg.r, bg.g, bg.b};
        const int r = ui_.colorPicker(px, y, hw, rowH, "BACKGROUND", c, th, Vec3{bi.bg.r, bi.bg.g, bi.bg.b});
        if (r == Ui::RESET) setRule(HudStyle::BACKGROUND, false);
        else if (r == Ui::CHANGED) {
            rule.background = ThemeColor{ThemeColor::FIXED, {c.x, c.y, c.z, bg.a > 0 ? bg.a : 0.4f}};
            setRule(HudStyle::BACKGROUND, true);
        }
        float a = bg.a * 100;
        if (ui_.slider(x2, y, hw, rowH, "BG OPACITY %", a, 0, 100, "%.0f", th, bi.bg.a * 100)) {
            rule.background = ThemeColor{ThemeColor::FIXED, {bg.r, bg.g, bg.b, clampf(a, 0, 100) / 100}};
            setRule(HudStyle::BACKGROUND, !(a == bi.bg.a * 100 && bg.r == bi.bg.r && bg.g == bi.bg.g && bg.b == bi.bg.b));
        }
    }
    next();
    {   // border: width and colour
        float bw = st.has(HudStyle::BORDER) ? st.borderW : 0;
        const Color bc = st.has(HudStyle::BORDER) ? st.border.get(fc, ec) : Color{1, 1, 1, 0.5f};
        if (ui_.slider(px, y, hw, rowH, "BORDER", bw, 0, 8, "%.0f", th, 0)) {
            rule.borderW = clampf(bw, 0, 8);
            if (!st.has(HudStyle::BORDER)) rule.border = ThemeColor{ThemeColor::FIXED, bc};
            setRule(HudStyle::BORDER, bw > 0);
        }
        Vec3 c = {bc.r, bc.g, bc.b};
        if (ui_.colorPicker(x2, y, hw, rowH, "BORDER COLOR", c, th, Vec3{1, 1, 1}) != Ui::UNCHANGED) {
            rule.border = ThemeColor{ThemeColor::FIXED, {c.x, c.y, c.z, bc.a}};
            if (!st.has(HudStyle::BORDER) || st.borderW <= 0) rule.borderW = 1;
            setRule(HudStyle::BORDER, true);
        }
    }
    next();
    {   // corners, text size
        float rad = st.has(HudStyle::RADIUS) ? st.radius : 0;
        if (ui_.slider(px, y, hw, rowH, "CORNERS", rad, 0, 32, "%.0f", th, 0)) { rule.radius = clampf(rad, 0, 64); setRule(HudStyle::RADIUS, rad != 0); }
        float fs = (st.has(HudStyle::FONT) ? st.fontScale : 1.0f) * 100;
        if (ui_.slider(x2, y, hw, rowH, "TEXT SIZE %", fs, 50, 300, "%.0f", th, 100)) { rule.fontScale = clampf(fs, 50, 300) / 100; setRule(HudStyle::FONT, fs != 100); }
    }
    next();
    {   // padding: across and down (inside the box)
        float pxv = st.has(HudStyle::PADDING) ? st.pad[0] : bi.padX, pyv = st.has(HudStyle::PADDING) ? st.pad[1] : bi.padY;
        const bool cx = ui_.slider(px, y, hw, rowH, "PAD ACROSS", pxv, 0, 24, "%.0f", th, bi.padX);
        const bool cy = ui_.slider(x2, y, hw, rowH, "PAD DOWN", pyv, 0, 24, "%.0f", th, bi.padY);
        if (cx || cy) {
            pxv = clampf(pxv, 0, 24); pyv = clampf(pyv, 0, 24);
            rule.pad[0] = rule.pad[2] = pxv; rule.pad[1] = rule.pad[3] = pyv;
            setRule(HudStyle::PADDING, !(pxv == bi.padX && pyv == bi.padY));
        }
    }
    next();
    if (bi.gap >= 0) {   // (elements of several parts: the space between them)
        float g = st.has(HudStyle::GAP) ? st.gap : bi.gap;
        if (ui_.slider(px, y, hw, rowH, "GAP", g, 0, 48, "%.0f", th, bi.gap)) { rule.gap = clampf(g, 0, 48); setRule(HudStyle::GAP, g != bi.gap); }
    }
    if (bi.align >= 0) {   // (lines of text: the kill feed, chat)
        static const std::vector<std::string> al = {"TEXT: LEFT", "TEXT: CENTRE", "TEXT: RIGHT"};
        int a = st.has(HudStyle::ALIGN) ? st.align : bi.align;
        if (ui_.cycle(x2, y, hw, rowH, "TEXT ALIGN", a, 3, al, th)) { rule.align = (uint8_t)a; setRule(HudStyle::ALIGN, a != bi.align); }
    }
    if (bi.gap >= 0 || bi.align >= 0) next();
    if (ui_.button(px, y, pw, rowH, "RESET STYLE", th, rule.set != 0)) { rule = HudStyle(); theme_.resolve(); }
    next();
    y += 6 * us;
    // ---- the whole HUD
    if (ui_.button(px, y, hw, rowH, "UNDO", UiExp::state("hudedit"), !hudUndo_.empty())) hudEdUndo(false);
    if (ui_.button(x2, y, hw, rowH, "REDO", UiExp::state("hudedit"), !hudRedo_.empty())) hudEdUndo(true);
    next();
    bool allDefault = true;
    for (int i : kEdEls) allDefault &= sameElem(cfg_.hud[i], defaultSettings().hud[i]);
    if (ui_.button(px, y, hw, rowH, "RESET ALL", he, !allDefault))
        for (int i : kEdEls) cfg_.hud[i] = defaultSettings().hud[i];
    if (ui_.button(x2, y, hw, rowH, "CLEAR THEME", th, !theme_.empty())) { theme_.clearRules(); }
    next();
    if (ui_.button(px, y, hw, rowH, "WIDGETS", UiExp::nav("~settings/hud/widgets"))) {
        hudEditClose();
        widgetEdit_ = true; widgetSel_ = -1;
        return;
    }
    if (ui_.button(x2, y, hw, rowH, "DONE", UiExp::nav("~settings/hud"))) { hudEditClose(); return; }
    next();
    ui_.hintWrapped(px, y + 2 * us, el == Settings::HUD_KEYS ? "SHOWN WITH VERBOSE INFO ON. DRAG TO MOVE, CORNER / WHEEL TO SIZE, ARROWS NUDGE, ALT: NO SNAP"
                                                               : "DRAG TO MOVE, CORNER / WHEEL / + - TO SIZE, ARROWS NUDGE (SHIFT 10), TAB NEXT, ALT: NO SNAP, CTRL+Z UNDO",
                    {0.7f, 0.72f, 0.75f, 1}, pw, 14 * us);
    ui_.popups();
    hudEdCommit(false);
}
