#include <cmath>
#include <cstdlib>
#include "ui.h"
#include <cstdio>
#include <cstring>
#include "../common/net.h"

void Ui::begin(Renderer* r, const UiInput& in, float scale) {
    r_ = r;
    in_ = in;
    scale_ = scale;
    textScale_ = std::max(1.0f, std::round(2.0f * scale));
    sliderCounter_ = 0;
    boxL_ = boxR_ = 0;
    where = scope = nullptr;
    muted = false;
    if (!in.down) activeSlider_ = -1;
    // an open picker / dropdown whose widget wasn't drawn last frame (the page changed): gone
    if (popup_.kind && !popup_.seen) closePopup();
    popup_.seen = false;
    blocked_ = popup_.kind != POP_NONE;
    unblock_ = false;
    if (blocked_ && (in.pressed || in.rpressed)) {   // a click outside it and its widget: closes it, and does nothing else
        auto in4 = [&](float x, float y, float w, float h) { return in.mx >= x && in.mx < x + w && in.my >= y && in.my < y + h; };
        if (!in4(popup_.box[0], popup_.box[1], popup_.box[2], popup_.box[3]) && !in4(popup_.ax, popup_.ay, popup_.aw, popup_.ah)) {
            closePopup();
            in_.pressed = in_.rpressed = false;
        }
    }
    if (in_.pressed) focus_ = -1; // clicking anywhere drops focus unless a field reclaims it (the picker's hex field too)
}

bool Ui::hover(float x, float y, float w, float h) const {
    if (muted || (blocked_ && !unblock_)) return false;   // (under a modal / a popup is open: only it takes input)
    return in_.mx >= x && in_.mx < x + w && in_.my >= y && in_.my < y + h;
}

float Ui::hintScale() const { return std::max(r_->minTextScale(), textScale_ * 0.75f); }

void Ui::hint(float x, float y, const std::string& t, const Color& c, float maxW) { drawFitted(x, y, t, c, hintScale(), maxW > 0 ? maxW : boxWidth(x)); }

void Ui::panel(float x, float y, float w, float h, float alpha) {
    // (PLAYER > MORE: PANEL DARKNESS scales its opacity; HIGH CONTRAST: black)
    const float a = std::min(1.0f, alpha * panelDark);
    r_->rect(x, y, w, h, contrast ? Color{0, 0, 0, std::max(a, 0.9f)} : Color{0.05f, 0.06f, 0.08f, a});
    r_->rect(x, y, w, 2 * scale_, accent);
    box(x, x + w);   // (labels after it fit inside it, until a narrower box is set)
}

// A label's text in at most maxW (0: as wide as it is): tighter, then smaller, then cut (fitted()), vertically centred
// on where the full-size line would be.
void Ui::drawFitted(float x, float y, const std::string& t, const Color& c0, float ts, float maxW) {
    const Color c = textMul == 1.0f ? c0 : Color{std::min(1.0f, c0.r * textMul), std::min(1.0f, c0.g * textMul), std::min(1.0f, c0.b * textMul), c0.a};
    if (maxW <= 0 || t.empty() || r_->textWidth(t, ts) <= maxW) { r_->text(x, y, ts, t, c); return; }
    TextFit f = fitted(t, ts, maxW);
    float dy = (r_->textHeight(ts) - r_->textHeight(f.scale)) * 0.5f;
    if (f.keep >= 0) r_->text(x, y + dy, f.scale, t.substr(0, f.keep) + "...", c);
    else r_->text(x, y + dy, f.scale, t, c, f.level);
}

void Ui::label(float x, float y, const std::string& t, const Color& c, float sizeMul, float maxW) {
    float ts = std::max(1.0f, std::round(textScale_ * sizeMul));
    drawFitted(x, y, t, c, ts, maxW > 0 ? maxW : boxWidth(x));
}

void Ui::labelFit(float x, float y, float maxW, std::string t, const Color& c) {
    float tk = 0, ts = fit(t, maxW, nullptr, &tk);
    r_->text(x, y + (r_->textHeight(textScale_) - r_->textHeight(ts)) * 0.5f, ts, t, c, tk);
}

void Ui::labelCentered(float cx, float y, const std::string& t, const Color& c, float sizeMul, float maxW) {
    drawCentered(cx, y, t, c, std::max(1.0f, std::round(textScale_ * sizeMul)), maxW);
}

void Ui::hintCentered(float cx, float y, const std::string& t, const Color& c, float maxW) { drawCentered(cx, y, t, c, hintScale(), maxW); }

void Ui::drawCentered(float cx, float y, const std::string& t, const Color& c, float ts, float maxW) {
    if (maxW <= 0 && boxR_ > boxL_) maxW = 2 * std::max(0.0f, std::min(cx - boxL_, boxR_ - cx));   // (centred in its box)
    float w = r_->textWidth(t, ts);
    if (maxW <= 0 || t.empty() || w <= maxW) { r_->text(cx - w * 0.5f, y, ts, t, c); return; }
    TextFit f = fitted(t, ts, maxW);
    std::string shown = f.keep >= 0 ? t.substr(0, f.keep) + "..." : t;
    float tk = f.keep >= 0 ? 0 : f.level;
    r_->text(cx - r_->textWidth(shown, f.scale, tk) * 0.5f, y + (r_->textHeight(ts) - r_->textHeight(f.scale)) * 0.5f, f.scale, shown, c, tk);
}

// The fit of t at ts in maxW (Renderer::fitText: tighter letters, then smaller, then cut), from a small cache: a
// label that doesn't fit is worked out once, not every frame (keyed by the text, the width, the size and the font).
TextFit Ui::fitted(const std::string& t, float ts, float maxW) const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (unsigned char ch : t) mix(ch);
    uint32_t wb, sb;
    std::memcpy(&wb, &maxW, 4); std::memcpy(&sb, &ts, 4);
    mix(wb); mix(sb); mix((uint64_t)(uint32_t)r_->fontGeneration());
    h |= 1;   // (0: an empty slot)
    FitEntry& e = fitCache_[h % kFitCache];
    if (e.key != h) { e.key = h; e.fit = r_->fitText(t, ts, maxW); }
    return e.fit;
}

// Greedy word wrap, cached like fitted() (the text is kept too: a hash clash re-wraps instead of showing another text).
const std::vector<std::string>& Ui::wrap(const std::string& t, float ts, float maxW, int maxChars) const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (unsigned char ch : t) mix(ch);
    uint32_t wb, sb;
    std::memcpy(&wb, &maxW, 4); std::memcpy(&sb, &ts, 4);
    mix(wb); mix(sb); mix((uint64_t)(uint32_t)maxChars); mix((uint64_t)(uint32_t)r_->fontGeneration());
    h |= 1;
    WrapEntry& e = wrapCache_[h % kWrapCache];
    if (e.key == h && e.text == t) return e.lines;
    e.key = h;
    e.text = t;
    e.lines.clear();
    auto fits = [&](size_t a, size_t n) {
        return (maxChars <= 0 || (int)n <= maxChars) && r_->textWidth(t.substr(a, n), ts) <= maxW;
    };
    size_t a = 0;
    while (a < t.size()) {
        while (a < t.size() && t[a] == ' ') a++;
        if (a >= t.size()) break;
        size_t end = a, sp = t.find(' ', a);
        // as many whole words as fit
        while (true) {
            size_t next = sp == std::string::npos ? t.size() : sp;
            if (end > a && !fits(a, next - a)) break;
            end = next;
            if (sp == std::string::npos || !fits(a, end - a)) break;
            sp = t.find(' ', sp + 1);
        }
        if (!fits(a, end - a)) {   // one word wider than the line: cut it where it fits (at least one character)
            size_t n = 1;
            while (a + n < end && fits(a, n + 1)) n++;
            end = a + n;
        }
        e.lines.push_back(t.substr(a, end - a));
        a = end;
    }
    return e.lines;
}

int Ui::hintWrapped(float x, float y, const std::string& t, const Color& c, float maxW, float lineH) {
    const std::vector<std::string>& lines = wrap(t, hintScale(), maxW);
    for (size_t i = 0; i < lines.size(); i++) hint(x, y + i * lineH, lines[i], c, maxW);
    return (int)lines.size();
}

// Text scale that fits maxW: the normal size, else tighter letters, else shrunk only as much as needed. The renderer
// never draws below Renderer::minTextScale() (the font's), so text that still doesn't fit at that size is clipped with
// "..." (t is replaced). track: how far to close its letters up when it's drawn (Renderer::text's level).
float Ui::fit(std::string& t, float maxW, bool* clipped, float* track) const {
    if (track) *track = 0;
    if (t.empty() || r_->textWidth(t, textScale_) <= maxW) return textScale_;
    TextFit f = fitted(t, textScale_, maxW);
    if (f.keep >= 0) {
        t = t.substr(0, f.keep) + "...";
        if (clipped) *clipped = true;
    } else if (track) *track = f.level;
    return f.scale;
}

// fit() for drawing: no copy of t (buf holds it only when it has to be cut), with its scale, closing-up level and
// width at them (from the fit cache), so the caller doesn't measure it again. Returns the text to draw: t or buf.
const std::string& Ui::fitText(const std::string& t, float maxW, std::string& buf, float& ts, float& tk, float& tw, bool* clipped) const {
    ts = textScale_; tk = 0;
    tw = t.empty() ? 0.0f : r_->textWidth(t, textScale_);
    if (t.empty() || tw <= maxW) return t;
    const TextFit f = fitted(t, textScale_, maxW);
    ts = f.scale;
    if (f.keep < 0) { tk = f.level; tw = r_->textWidth(t, ts, tk); return t; }
    buf.assign(t, 0, (size_t)f.keep);
    buf += "...";
    if (clipped) *clipped = true;
    tw = r_->textWidth(buf, ts);
    return buf;
}

// ---- the UI test's record of each widget (only with a probe attached: never in normal play) ----
void Ui::report(UiWidget::Kind k, float x, float y, float w, float h, const std::string& label, const std::string& value,
                const UiExp& e, bool enabled, bool clipped) {
    UiWidget wd;
    wd.where = where; wd.scope = scope;
    wd.kind = k; wd.x = x; wd.y = y; wd.w = w; wd.h = h;
    wd.label = label; wd.value = value; wd.exp = e; wd.enabled = enabled; wd.clipped = clipped;
    probe->widget(wd);
}

void Ui::keyAction(const std::string& label, int key, int mods, const UiExp& e, int hold) {
    if (!reporting()) return;
    UiWidget wd;
    wd.where = where; wd.scope = scope;
    wd.kind = UiWidget::KEY; wd.key = key; wd.mods = mods; wd.label = label; wd.exp = e; wd.count = hold + 1;
    probe->widget(wd);
}

void Ui::handle(float x, float y, float w, float h, float dx, float dy, const std::string& label, const UiExp& e) {
    if (!reporting()) return;
    UiWidget wd;
    wd.where = where; wd.scope = scope;
    wd.kind = UiWidget::HANDLE; wd.x = x; wd.y = y; wd.w = w; wd.h = h; wd.dx = dx; wd.dy = dy; wd.label = label; wd.exp = e;
    probe->widget(wd);
}

void Ui::view(float x, float y, float w, float h, const std::string& label, const UiExp& e) {
    if (reporting()) report(UiWidget::VIEW, x, y, w, h, label, "", e, true, false);
}

void Ui::custom(float x, float y, float w, float h, const std::string& label, const UiExp& e) {
    if (reporting()) report(UiWidget::BUTTON, x, y, w, h, label, "", e, true, false);
}

bool Ui::button(float x, float y, float w, float h, const std::string& label, const UiExp& e, bool enabled) {
    bool clipped = false;
    bool hit = buttonImpl(x, y, w, h, label, enabled, &clipped);
    if (reporting()) report(UiWidget::BUTTON, x, y, w, h, label, "", e, enabled, clipped);
    return hit;
}

bool Ui::tab(float x, float y, float w, float h, const std::string& label, bool current, const UiExp& e, bool sub) {
    if (!current) return button(x, y, w, h, label, e);
    const bool hv = hover(x, y, w, h);
    const float k = sub ? 0.35f : 0.6f;
    r_->rect(x, y, w, h, {accent.r * k, accent.g * k, accent.b * k, sub ? 0.95f : 1.0f});
    if (sub) r_->rect(x, y + h - 2 * scale_, w, 2 * scale_, accent);
    std::string shown = label;
    bool clipped = false;
    float tk = 0, ts = fit(shown, w - 8 * scale_, &clipped, &tk);
    r_->text(x + (w - r_->textWidth(shown, ts, tk)) * 0.5f, y + (h - r_->textHeight(ts)) * 0.5f, ts, shown, textC(), tk);
    if (reporting()) report(UiWidget::BUTTON, x, y, w, h, label, "", e, true, clipped);
    return hv && in_.pressed;
}

bool Ui::buttonImpl(float x, float y, float w, float h, const std::string& label, bool enabled, bool* clipped) {
    bool hv = enabled && hover(x, y, w, h);
    Color bg = !enabled ? Color{0.12f, 0.12f, 0.14f, 0.8f} : hv ? Color{accent.r * 0.5f, accent.g * 0.5f, accent.b * 0.5f, 0.95f}
                                                             : btnC();
    r_->rect(x, y, w, h, bg);
    if (contrast && enabled) {   // (HIGH CONTRAST: a light outline round every button)
        const float o = std::max(1.0f, std::round(scale_));
        r_->rect(x, y, w, o, {0.9f, 0.9f, 0.9f, 1}); r_->rect(x, y + h - o, w, o, {0.9f, 0.9f, 0.9f, 1});
        r_->rect(x, y, o, h, {0.9f, 0.9f, 0.9f, 1}); r_->rect(x + w - o, y, o, h, {0.9f, 0.9f, 0.9f, 1});
    }
    if (hv) r_->rect(x, y + h - 2 * scale_, w, 2 * scale_, accent);
    Color tc = enabled ? textC() : Color{0.5f, 0.5f, 0.5f, 1};
    std::string buf;
    float ts, tk, tw;
    const std::string& shown = fitText(label, w - 8 * scale_, buf, ts, tk, tw, clipped);
    r_->text(x + (w - tw) * 0.5f, y + (h - r_->textHeight(ts)) * 0.5f, ts, shown, tc, tk);
    return hv && in_.pressed;
}

bool Ui::textField(int id, float x, float y, float w, float h, std::string& value, size_t maxLen, const UiExp& e) {
    if (in_.pressed && hover(x, y, w, h)) focus_ = id;
    bool focused = focus_ == id;
    bool changed = false;
    if (focused) {
        for (char c : in_.chars)
            if (c >= 32 && c < 127 && value.size() < maxLen) { value += c; changed = true; }
        if (in_.backspace && !value.empty()) { value.pop_back(); changed = true; }
        if (in_.copy) copied_ = value;
        if (in_.enter || in_.tab || in_.escape) focus_ = -1;
    }
    r_->rect(x, y, w, h, focused ? Color{0.1f, 0.12f, 0.16f, 1} : Color{0.08f, 0.09f, 0.11f, 0.95f});
    r_->rect(x, y + h - 2 * scale_, w, 2 * scale_, focused ? accent : Color{0.3f, 0.3f, 0.35f, 1});
    std::string shown = maskText ? std::string(value.size(), '*') : value;   // (STREAMER MODE: addresses and codes)
    float cw = caretWidth(), maxW = w - 12 * scale_ - cw - 2 * scale_;
    while (!shown.empty() && r_->textWidth(shown, textScale_) > maxW) shown.erase(0, 1); // long: show the end
    float tx = x + 6 * scale_, ty = y + (h - r_->textHeight(textScale_)) * 0.5f;
    r_->text(tx, ty, textScale_, shown, textC());
    if (focused) caret(tx + r_->textWidth(shown, textScale_), ty);
    if (reporting()) {
        UiWidget wd;
        wd.where = where; wd.scope = scope;
        wd.kind = UiWidget::TEXT; wd.x = x; wd.y = y; wd.w = w; wd.h = h; wd.count = (int)maxLen;
        wd.label = "FIELD " + std::to_string(id); wd.value = value; wd.exp = e;
        probe->widget(wd);
    }
    return changed;
}

float Ui::caretWidth() const { return std::max(1.0f, std::round(textScale_ * 0.6f)); }

// The one text caret (menus' fields, sliders, chat and the console): a blinking white bar the height of the text,
// just right of x, with a dark 1 px outline so it reads over the world too. ts <= 0: the UI's text scale.
void Ui::caret(float x, float y, float ts) {
    if (std::fmod(timeNow(), 1.0) >= 0.5 && !steadyCaret) return;
    if (ts <= 0) ts = textScale_;
    float h = r_->textHeight(ts), w = std::max(1.0f, std::round(ts * 0.6f)), bx = x + scale_;
    r_->rect(bx - 1, y - h * 0.05f - 1, w + 2, h * 1.05f + 2, {0, 0, 0, 0.8f});
    r_->rect(bx, y - h * 0.05f, w, h * 1.05f, {1, 1, 1, 1});
}

bool Ui::cycle(float x, float y, float w, float h, const std::string& label, int& index, int count,
               const std::vector<std::string>& names, const UiExp& e) {
    if (count <= 0) return false;
    float bw = h;
    bool changed = false;
    if (buttonImpl(x, y, bw, h, "<", true, nullptr)) { index = (index + count - 1) % count; changed = true; }
    if (buttonImpl(x + w - bw, y, bw, h, ">", true, nullptr)) { index = (index + 1) % count; changed = true; }
    r_->rect(x + bw, y, w - 2 * bw, h, {0.08f, 0.09f, 0.11f, 0.95f});
    // (the value names say what they are: no "LABEL: " prefix, it overflowed; the label is the widget's id)
    static const std::string kUnknown = "?";
    const std::string& name = index >= 0 && index < (int)names.size() ? names[index] : kUnknown;
    bool clipped = false;
    std::string buf;
    float ts, tk, tw;
    const std::string& t = fitText(name, w - 2 * bw - 8 * scale_, buf, ts, tk, tw, &clipped);
    r_->text(x + (w - tw) * 0.5f, y + (h - r_->textHeight(ts)) * 0.5f, ts, t, textC(), tk);
    if (reporting()) {
        UiWidget wd;
        wd.where = where; wd.scope = scope;
        wd.kind = UiWidget::CYCLE; wd.x = x; wd.y = y; wd.w = w; wd.h = h; wd.count = count;
        wd.label = label; wd.value = std::to_string(index); wd.exp = e; wd.clipped = clipped;
        probe->widget(wd);
    }
    return changed;
}

bool Ui::toggle(float x, float y, float w, float h, const std::string& label, bool& v, const UiExp& e) {
    bool clipped = false, hit = buttonImpl(x, y, w, h, label + ": " + (v ? "ON" : "OFF"), true, &clipped);
    if (hit) v = !v;
    if (reporting()) report(UiWidget::TOGGLE, x, y, w, h, label, v ? "1" : "0", e, true, clipped);
    return hit;
}

bool Ui::slider(float x, float y, float w, float h, const std::string& label, float& v, float lo, float hi, const char* fmt, const UiExp& e, float def) {
    int id = sliderCounter_++;
    typed_ = false;
    char buf[64];
    snprintf(buf, sizeof buf, fmt, v);
    // "LABEL: number", fitted to the slider: shrunk first, then the LABEL cut short - the number always shows whole
    // (a long label used to push the value off the end). Click the number to type a value (any value, not just the
    // slider's range).
    auto layout = [&](const std::string& num, std::string& lab, float& ts) {
        const float maxW = w - 8 * scale_;
        lab = label;
        ts = textScale_;
        const std::string full = lab + ": " + num;   // (built once: it was three times)
        if (r_->textWidth(full, ts) <= maxW) return;
        ts = std::max(r_->minTextScale(), maxW / (r_->textWidth(full, 10.0f) * 0.1f));
        if (r_->textWidth(full, ts) > maxW + 0.01f) lab = r_->textClip(lab, ts, std::max(0.0f, maxW - r_->textWidth(": " + num, ts)));
    };
    std::string lab;
    float ls;
    layout(buf, lab, ls);
    std::string prefix = lab + ": ", shown = prefix + buf;
    float tw = r_->textWidth(shown, ls), tx = x + (w - tw) * 0.5f;
    float pw = r_->textWidth(prefix, ls), bw = r_->textWidth(buf, ls);   // (kept for drawing below, unless the value changes)
    float nx = tx + pw, nw = std::max(bw, 24 * scale_);
    bool overNumber = hover(nx - 6 * scale_, y, nw + 12 * scale_, h);
    bool changed = false;
    bool editing = editSlider_ == id && focus_ == kSliderFocus + id;
    auto commit = [&] {
        char* end = nullptr;
        float nv = strtof(editText_.c_str(), &end);
        if (end != editText_.c_str() && std::isfinite(nv)) {
            if (lo >= 0) nv = std::max(nv, 0.0f);
            if (nv != v) { v = nv; changed = true; typed_ = true; }
        }
        editSlider_ = -1;
        if (focus_ == kSliderFocus + id) focus_ = -1;
    };
    if (in_.rpressed && hover(x, y, w, h)) {   // right-click: back to the default (and no typing, no drag)
        if (editing) { editSlider_ = -1; if (focus_ == kSliderFocus + id) focus_ = -1; editing = false; }
        if (activeSlider_ == id) activeSlider_ = -1;
        if (v != def) { v = def; changed = true; typed_ = true; }   // (typed_: exactly that, not snapped to a drag step)
    } else if (editing) {
        for (char c : in_.chars) if (((c >= '0' && c <= '9') || c == '.' || c == '-') && editText_.size() < 12) editText_ += c;
        if (in_.backspace && !editText_.empty()) editText_.pop_back();
        if (in_.escape) { editSlider_ = -1; focus_ = -1; editing = false; }
        else if (in_.enter || in_.tab || (in_.pressed && !hover(x, y, w, h))) { commit(); editing = false; }
    } else if (in_.pressed && overNumber) {
        editSlider_ = id;
        editText_ = buf;
        while (!editText_.empty() && editText_[0] == ' ') editText_.erase(0, 1);
        focus_ = kSliderFocus + id;
        editing = true;
    } else if (in_.pressed && hover(x, y, w, h)) activeSlider_ = id;
    if (!editing && activeSlider_ == id && in_.down) {
        float t = clampf((in_.mx - x) / w, 0, 1);
        float nv = lo + (hi - lo) * t;
        if (nv != v) { v = nv; changed = true; }
    }
    float t = clampf((v - lo) / (hi - lo), 0, 1);
    r_->rect(x, y, w, h, {0.08f, 0.09f, 0.11f, 0.95f});
    r_->rect(x, y, w * t, h, {accent.r * 0.45f, accent.g * 0.45f, accent.b * 0.45f, 0.95f});
    bool clipped = false;   // (only when the number itself can't be shown whole)
    if (editing) {   // the typed text in a dark box, with the caret
        std::string elab;
        float es;
        layout(editText_ + "  ", elab, es);
        std::string s = elab + ": " + editText_;
        float cw = caretWidth() + 2 * scale_;
        float ex = x + (w - r_->textWidth(s, es) - cw) * 0.5f, ew = r_->textWidth(s, es), ey = y + (h - r_->textHeight(es)) * 0.5f;
        r_->rect(ex + r_->textWidth(elab + ": ", es) - 3 * scale_, y + 3 * scale_, r_->textWidth(editText_, es) + cw + 6 * scale_, h - 6 * scale_, {0, 0, 0, 0.6f});
        r_->text(ex, ey, es, s, textC());
        caret(ex + ew, ey, es);
        r_->rect(x, y + h - 2 * scale_, w, 2 * scale_, accent);
    } else {
        if (changed) {   // (v was set this frame: its text and layout again; else they're the ones above)
            snprintf(buf, sizeof buf, fmt, v);
            layout(buf, lab, ls);
            prefix = lab + ": ";
            shown = prefix + buf;
            tw = r_->textWidth(shown, ls); pw = r_->textWidth(prefix, ls); bw = r_->textWidth(buf, ls);
        }
        clipped = bw > w - 8 * scale_;
        float sx = x + (w - tw) * 0.5f, sy = y + (h - r_->textHeight(ls)) * 0.5f;
        r_->text(sx, sy, ls, shown, textC());
        // the number looks clickable: a faint box always, brighter with an underline under the mouse
        float bx = sx + pw;
        r_->rect(bx - 3 * scale_, y + 3 * scale_, bw + 6 * scale_, h - 6 * scale_, {1, 1, 1, overNumber ? 0.14f : 0.05f});
        if (overNumber) r_->rect(bx, sy + r_->textHeight(ls) + 1 * scale_, bw, 1 * scale_, {1, 1, 1, 0.7f});
        // under the mouse, away from its default: a small hint above it (a dark tag, the smallest text)
        if (hover(x, y, w, h) && v != def && !in_.down) {
            const std::string hint = "RIGHT-CLICK: RESET";
            float hs = r_->minTextScale(), hw2 = r_->textWidth(hint, hs) + 8 * scale_, hh = r_->textHeight(hs) + 4 * scale_;
            float hx = std::min(x + w - hw2, std::max(x, in_.mx - hw2 * 0.5f)), hy = y - hh - 2 * scale_;
            r_->rect(hx, hy, hw2, hh, {0, 0, 0, 0.75f});
            r_->text(hx + 4 * scale_, hy + 2 * scale_, hs, hint, {0.85f, 0.87f, 0.9f, 1});
        }
    }
    if (reporting()) {
        UiWidget wd;
        wd.where = where; wd.scope = scope;
        wd.kind = UiWidget::SLIDER; wd.x = x; wd.y = y; wd.w = w; wd.h = h; wd.nx = nx; wd.nw = nw;
        wd.dx = lo; wd.dy = hi;   // (a slider's range rides in dx / dy, its shown decimals in count)
        wd.def = def;
        const char* dot = strchr(fmt, '.');
        wd.count = dot ? atoi(dot + 1) : 0;
        wd.label = label; wd.exp = e; wd.clipped = clipped;
        char vb[32];
        snprintf(vb, sizeof vb, "%.6g", v);
        wd.value = vb;
        probe->widget(wd);
    }
    return changed;
}

// ---- colour picker and dropdown: a widget, and a popup over the page (modal while open) ---------------------------

static void rgbToHsv(const Vec3& c, float& h, float& s, float& v) {
    float mx = std::max(c.x, std::max(c.y, c.z)), mn = std::min(c.x, std::min(c.y, c.z)), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 0) return;   // (grey: the hue stays what it was)
    float hh = mx == c.x ? (c.y - c.z) / d : mx == c.y ? 2 + (c.z - c.x) / d : 4 + (c.x - c.y) / d;
    h = std::fmod(hh / 6 + 1, 1.0f);
}
static Vec3 hsvToRgb(float h, float s, float v) {
    const float k[3] = {5, 3, 1};
    Vec3 o;
    for (int i = 0; i < 3; i++) {
        float t = std::fmod(k[i] + h * 6, 6.0f);
        o[i] = v - v * s * std::max(0.0f, std::min(std::min(t, 4 - t), 1.0f));
    }
    return o;
}
static int packRgb(const Vec3& c) {   // 0xRRGGBB (8 bits a channel, as quark.cfg keeps a colour)
    auto ch = [](float v) { return (int)std::lround(clampf(v, 0, 1) * 255); };
    return ch(c.x) << 16 | ch(c.y) << 8 | ch(c.z);
}
static Vec3 unpackRgb(int v) { return {((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f}; }
static std::string hexOf(const Vec3& c) { char b[8]; snprintf(b, sizeof b, "#%06X", packRgb(c)); return b; }

int Ui::colorPicker(float x, float y, float w, float h, const std::string& label, Vec3& c, const UiExp& e, const Vec3& def) {
    const float s = scale_;
    bool open = popup_.kind == POP_COLOR && popup_.id == label;
    int result = UNCHANGED;
    unblock_ = open;   // (its own swatch and popup take input while it's open)
    const bool hv = hover(x, y, w, h);
    if (in_.rpressed && hv) {   // right-click: the default (and the picker closes)
        if (packRgb(c) != packRgb(def)) { c = unpackRgb(packRgb(def)); result = RESET; }
        if (open) { closePopup(); open = false; }
    } else if (in_.pressed && hv) {
        if (open) { closePopup(); open = false; }
        else {
            popup_ = Popup();
            popup_.kind = POP_COLOR; popup_.id = label;
            rgbToHsv(c, popup_.hh, popup_.ss, popup_.vv);
            popup_.last = c;
            open = true;
        }
    }
    // the widget: a button with its label on the left and the colour on the right
    Color bg = hv ? Color{accent.r * 0.5f, accent.g * 0.5f, accent.b * 0.5f, 0.95f} : btnC();
    r_->rect(x, y, w, h, bg);
    if (hv || open) r_->rect(x, y + h - 2 * s, w, 2 * s, accent);
    const float sw = 36 * s, sh = std::min(h - 8 * s, 22 * s), sx = x + w - sw - 8 * s, sy = y + (h - sh) * 0.5f;
    r_->rect(sx - 1, sy - 1, sw + 2, sh + 2, {0, 0, 0, 1});
    r_->rect(sx, sy, sw, sh, rgba(c));
    std::string shown = label;
    bool clipped = false;
    float tk = 0, ts = fit(shown, sx - x - 16 * s, &clipped, &tk);
    r_->text(x + 8 * s, y + (h - r_->textHeight(ts)) * 0.5f, ts, shown, textC(), tk);
    if (open) {
        popup_.seen = true;
        popup_.ax = x; popup_.ay = y; popup_.aw = w; popup_.ah = h;
        // under the widget (above it when there's no room), its right edge on the widget's
        const float pad = 12 * s, svW = 168 * s, svH = 120 * s, hueW = 20 * s, gap = 8 * s, rowH = 28 * s;
        const float pw = pad + svW + gap + hueW + pad, ph = pad + svH + gap + rowH + pad;
        const float px = std::max(4 * s, std::min(x + w - pw, (float)r_->width() - pw - 4 * s));
        float py = y + h + 4 * s;
        if (py + ph > r_->height() - 4 * s) py = y - ph - 4 * s;
        float* b = popup_.box;
        b[0] = px; b[1] = py; b[2] = pw; b[3] = ph;
        const float svx = px + pad, svy = py + pad, hx = svx + svW + gap, rowY = svy + svH + gap;
        rects_ = PopupRects();
        std::copy(b, b + 4, rects_.box);
        rects_.sv[0] = svx; rects_.sv[1] = svy; rects_.sv[2] = svW; rects_.sv[3] = svH;
        rects_.hue[0] = hx; rects_.hue[1] = svy; rects_.hue[2] = hueW; rects_.hue[3] = svH;
        rects_.hex[0] = svx; rects_.hex[1] = rowY; rects_.hex[2] = 96 * s; rects_.hex[3] = rowH;
        if (popup_.init || packRgb(c) != packRgb(popup_.last)) rgbToHsv(c, popup_.hh, popup_.ss, popup_.vv);   // (changed elsewhere: a preset)
        popup_.init = false;
        auto in4 = [&](const float* r) { return in_.mx >= r[0] && in_.mx < r[0] + r[2] && in_.my >= r[1] && in_.my < r[1] + r[3]; };
        if (in_.pressed && in4(rects_.sv)) popup_.drag = 1;
        else if (in_.pressed && in4(rects_.hue)) popup_.drag = 2;
        if (!in_.down) popup_.drag = 0;
        if (popup_.drag == 1) {
            popup_.ss = clampf((in_.mx - svx) / svW, 0, 1);
            popup_.vv = 1 - clampf((in_.my - svy) / svH, 0, 1);
        } else if (popup_.drag == 2) popup_.hh = clampf((in_.my - svy) / svH, 0, 0.9999f);
        if (popup_.drag) {
            int n = packRgb(hsvToRgb(popup_.hh, popup_.ss, popup_.vv));
            if (n != packRgb(c)) { c = unpackRgb(n); result = CHANGED; }
        }
        // the hex field: click, type RRGGBB, Enter (Esc / a click elsewhere: as it was)
        if (in_.pressed && in4(rects_.hex)) { focus_ = kPickerFocus; hexText_ = hexOf(c); }
        if (focus_ == kPickerFocus) {
            for (char ch : in_.chars) {
                char u = (char)toupper((unsigned char)ch);
                if (((u >= '0' && u <= '9') || (u >= 'A' && u <= 'F') || (u == '#' && hexText_.empty())) && hexText_.size() < 7) hexText_ += u;
            }
            if (in_.backspace && !hexText_.empty()) hexText_.pop_back();
            if (in_.escape) focus_ = -1;
            else if (in_.enter || in_.tab) {
                std::string t = !hexText_.empty() && hexText_[0] == '#' ? hexText_.substr(1) : hexText_;
                if (t.size() == 6) {
                    int v = (int)strtol(t.c_str(), nullptr, 16);
                    if (v != packRgb(c)) { c = unpackRgb(v); result = CHANGED; }
                    rgbToHsv(c, popup_.hh, popup_.ss, popup_.vv);
                }
                focus_ = -1;
            }
        }
        popup_.last = c;
    }
    if (reporting()) {   // (reported while its popup is open too: it's that state's way out)
        UiWidget wd;
        wd.where = where; wd.scope = scope;
        wd.kind = UiWidget::COLOR; wd.x = x; wd.y = y; wd.w = w; wd.h = h;
        wd.label = label; wd.value = hexOf(c); wd.def = (float)packRgb(def); wd.exp = e; wd.clipped = clipped;
        probe->widget(wd);
    }
    unblock_ = false;
    return result;
}

bool Ui::dropdown(float x, float y, float w, float h, const std::string& label, int& index, const std::vector<std::string>& names,
                  const UiExp& opened, const UiExp& closed, const UiExp& pick) {
    const float s = scale_;
    const int n = (int)names.size();
    bool open = popup_.kind == POP_DROP && popup_.id == label, changed = false;
    unblock_ = open;
    const bool hv = hover(x, y, w, h);
    // the list: under the field, an entry a row
    const float rowH = h, gap = 4 * s, pad = 8 * s;
    const float lx = x, ly = y + h + 4 * s, lw = w, lh = pad * 2 + n * rowH + (n - 1) * gap;
    if (in_.pressed && hv) {
        if (open) { closePopup(); open = false; }
        else { popup_ = Popup(); popup_.kind = POP_DROP; popup_.id = label; open = true; }
    } else if (open && in_.pressed) {
        for (int i = 0; i < n; i++) {
            float ey = ly + pad + i * (rowH + gap);
            if (in_.mx >= lx + pad && in_.mx < lx + lw - pad && in_.my >= ey && in_.my < ey + rowH) {
                if (i != index) { index = i; changed = true; }
                closePopup(); open = false;
                break;
            }
        }
    }
    // the field: "LABEL: VALUE  V" (^ while open)
    Color bg = hv ? Color{accent.r * 0.5f, accent.g * 0.5f, accent.b * 0.5f, 0.95f} : btnC();
    r_->rect(x, y, w, h, bg);
    if (hv || open) r_->rect(x, y + h - 2 * s, w, 2 * s, accent);
    std::string shown = label + ": " + (index >= 0 && index < n ? names[index] : "?") + (open ? "  ^" : "  V");
    bool clipped = false;
    float tk = 0, ts = fit(shown, w - 8 * s, &clipped, &tk);
    r_->text(x + (w - r_->textWidth(shown, ts, tk)) * 0.5f, y + (h - r_->textHeight(ts)) * 0.5f, ts, shown, textC(), tk);
    if (open) {
        popup_.seen = true;
        popup_.ax = x; popup_.ay = y; popup_.aw = w; popup_.ah = h;
        popup_.box[0] = lx; popup_.box[1] = ly; popup_.box[2] = lw; popup_.box[3] = lh;
        popup_.names = names;
        popup_.index = index;
        if (reporting())
            for (int i = 0; i < n; i++) {
                UiWidget wd;
                wd.where = where; wd.scope = scope;
                wd.kind = UiWidget::BUTTON; wd.x = lx + pad; wd.y = ly + pad + i * (rowH + gap); wd.w = lw - 2 * pad; wd.h = rowH;
                wd.label = names[i]; wd.exp = (i == index ? closed : pick).withId(names[i].c_str());
                probe->widget(wd);
            }
    }
    if (reporting()) {
        UiWidget wd;
        wd.where = where; wd.scope = scope;
        wd.kind = UiWidget::DROPDOWN; wd.x = x; wd.y = y; wd.w = w; wd.h = h; wd.count = n;
        wd.label = label; wd.value = std::to_string(index); wd.exp = open ? closed.withId(opened.id) : opened; wd.clipped = clipped;
        probe->widget(wd);
    }
    unblock_ = false;
    return changed;
}

void Ui::popups() {
    const float s = scale_;
    const float* b = popup_.box;
    if (popup_.kind == POP_DROP) {
        r_->rect(b[0], b[1], b[2], b[3], {0.05f, 0.06f, 0.08f, 0.98f});
        const float pad = 8 * s, rowH = popup_.ah, gap = 4 * s;
        for (int i = 0; i < (int)popup_.names.size(); i++) {
            float ex = b[0] + pad, ey = b[1] + pad + i * (rowH + gap), ew = b[2] - 2 * pad;
            bool hv = in_.mx >= ex && in_.mx < ex + ew && in_.my >= ey && in_.my < ey + rowH;
            Color bg = i == popup_.index ? accent : hv ? Color{accent.r * 0.5f, accent.g * 0.5f, accent.b * 0.5f, 0.95f} : Color{0.14f, 0.16f, 0.2f, 0.9f};
            r_->rect(ex, ey, ew, rowH, bg);
            std::string t = popup_.names[i];
            float tk = 0, ts = fit(t, ew - 8 * s, nullptr, &tk);
            r_->text(ex + (ew - r_->textWidth(t, ts, tk)) * 0.5f, ey + (rowH - r_->textHeight(ts)) * 0.5f, ts, t, textC(), tk);
        }
    } else if (popup_.kind == POP_COLOR) {
        r_->rect(b[0], b[1], b[2], b[3], {0.05f, 0.06f, 0.08f, 0.98f});
        r_->rect(b[0], b[1], b[2], 2 * s, accent);
        const float* sv = rects_.sv;
        const float* hb = rects_.hue;
        const float* hx = rects_.hex;
        const Color white = {1, 1, 1, 1}, hc = rgba(hsvToRgb(popup_.hh, 1, 1)), clear = {0, 0, 0, 0}, black = {0, 0, 0, 1};
        r_->rectGradient(sv[0], sv[1], sv[2], sv[3], white, hc, hc, white);       // saturation across
        r_->rectGradient(sv[0], sv[1], sv[2], sv[3], clear, clear, black, black); // value down
        for (int i = 0; i < 6; i++) {   // the hue bar: red, yellow, green, cyan, blue, magenta, red
            float y0 = hb[1] + hb[3] * i / 6, y1 = hb[1] + hb[3] * (i + 1) / 6;
            Color c0 = rgba(hsvToRgb(i / 6.0f, 1, 1)), c1 = rgba(hsvToRgb((i + 1) % 6 / 6.0f, 1, 1));
            r_->rectGradient(hb[0], y0, hb[2], y1 - y0, c0, c0, c1, c1);
        }
        const float k = std::max(1.0f, std::round(s));
        {   // where the colour is in the square: a small ring (black and white: seen on any colour)
            float cx = sv[0] + popup_.ss * sv[2], cy = sv[1] + (1 - popup_.vv) * sv[3], rr = 5 * s;
            r_->rect(cx - rr - k, cy - rr - k, 2 * rr + 2 * k, 2 * rr + 2 * k, black);
            r_->rect(cx - rr, cy - rr, 2 * rr, 2 * rr, white);
            r_->rect(cx - rr + k, cy - rr + k, 2 * rr - 2 * k, 2 * rr - 2 * k, rgba(popup_.last));
        }
        {   // the hue: a bar across the hue strip
            float yy = hb[1] + popup_.hh * hb[3];
            r_->rect(hb[0] - 2 * k, yy - 2 * k, hb[2] + 4 * k, 4 * k, black);
            r_->rect(hb[0] - k, yy - k, hb[2] + 2 * k, 2 * k, white);
        }
        // the hex field (typed into), the colour, and its R G B
        const bool focused = focus_ == kPickerFocus;
        r_->rect(hx[0], hx[1], hx[2], hx[3], focused ? Color{0.1f, 0.12f, 0.16f, 1} : Color{0.08f, 0.09f, 0.11f, 0.95f});
        r_->rect(hx[0], hx[1] + hx[3] - 2 * s, hx[2], 2 * s, focused ? accent : Color{0.3f, 0.3f, 0.35f, 1});
        std::string t = focused ? hexText_ : hexOf(popup_.last);
        float tx = hx[0] + 6 * s, ty = hx[1] + (hx[3] - r_->textHeight(textScale_)) * 0.5f;
        r_->text(tx, ty, textScale_, t, textC());
        if (focused) caret(tx + r_->textWidth(t, textScale_), ty);
        float swx = hx[0] + hx[2] + 8 * s, sww = 28 * s;
        r_->rect(swx, hx[1], sww, hx[3], rgba(popup_.last));
        int v = packRgb(popup_.last);
        char rgb[32];
        snprintf(rgb, sizeof rgb, "%d %d %d", (v >> 16) & 255, (v >> 8) & 255, v & 255);
        float hs = hintScale();
        r_->text(swx + sww + 8 * s, hx[1] + (hx[3] - r_->textHeight(hs)) * 0.5f, hs, rgb, {0.7f, 0.72f, 0.75f, 1});
    }
}
