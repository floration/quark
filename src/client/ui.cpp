#include <cmath>
#include <cstdlib>
#include "ui.h"
#include <cstdio>
#include "../common/net.h"

void Ui::begin(Renderer* r, const UiInput& in, float scale) {
    r_ = r;
    in_ = in;
    scale_ = scale;
    textScale_ = std::max(1.0f, std::round(2.0f * scale));
    sliderCounter_ = 0;
    where = scope = nullptr;
    muted = false;
    if (!in.down) activeSlider_ = -1;
    if (in.pressed) focus_ = -1; // clicking anywhere drops focus unless a field reclaims it
}

bool Ui::hover(float x, float y, float w, float h) const {
    return in_.mx >= x && in_.mx < x + w && in_.my >= y && in_.my < y + h;
}

void Ui::panel(float x, float y, float w, float h, float alpha) {
    r_->rect(x, y, w, h, {0.05f, 0.06f, 0.08f, alpha});
    r_->rect(x, y, w, 2 * scale_, accent);
}

void Ui::label(float x, float y, const std::string& t, const Color& c, float sizeMul) {
    float ts = std::max(1.0f, std::round(textScale_ * sizeMul));
    r_->text(x, y, ts, t, c);
}

void Ui::labelFit(float x, float y, float maxW, std::string t, const Color& c) {
    float ts = fit(t, maxW);
    r_->text(x, y + (r_->textHeight(textScale_) - r_->textHeight(ts)) * 0.5f, ts, t, c);
}

void Ui::labelCentered(float cx, float y, const std::string& t, const Color& c, float sizeMul) {
    float ts = std::max(1.0f, std::round(textScale_ * sizeMul));
    r_->text(cx - r_->textWidth(t, ts) * 0.5f, y, ts, t, c);
}

// Text scale that fits maxW: the normal size, or shrunk only as much as needed. The renderer never draws below
// Renderer::kMinTextScale, so text that still doesn't fit at that size is clipped with "..." (t is replaced).
float Ui::fit(std::string& t, float maxW, bool* clipped) const {
    if (t.empty() || r_->textWidth(t, textScale_) <= maxW) return textScale_;
    float ts = std::max(Renderer::kMinTextScale, maxW / (r_->textWidth(t, 10.0f) * 0.1f)); // (width at 10: above the minimum)
    if (r_->textWidth(t, ts) > maxW) {
        t = r_->textClip(t, ts, maxW);
        if (clipped) *clipped = true;
    }
    return ts;
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
    if (!probe || muted) return;
    UiWidget wd;
    wd.where = where; wd.scope = scope;
    wd.kind = UiWidget::KEY; wd.key = key; wd.mods = mods; wd.label = label; wd.exp = e; wd.count = hold + 1;
    probe->widget(wd);
}

void Ui::handle(float x, float y, float w, float h, float dx, float dy, const std::string& label, const UiExp& e) {
    if (!probe || muted) return;
    UiWidget wd;
    wd.where = where; wd.scope = scope;
    wd.kind = UiWidget::HANDLE; wd.x = x; wd.y = y; wd.w = w; wd.h = h; wd.dx = dx; wd.dy = dy; wd.label = label; wd.exp = e;
    probe->widget(wd);
}

void Ui::view(float x, float y, float w, float h, const std::string& label, const UiExp& e) {
    if (probe && !muted) report(UiWidget::VIEW, x, y, w, h, label, "", e, true, false);
}

void Ui::custom(float x, float y, float w, float h, const std::string& label, const UiExp& e) {
    if (probe && !muted) report(UiWidget::BUTTON, x, y, w, h, label, "", e, true, false);
}

bool Ui::button(float x, float y, float w, float h, const std::string& label, const UiExp& e, bool enabled) {
    bool clipped = false;
    bool hit = buttonImpl(x, y, w, h, label, enabled, &clipped);
    if (probe && !muted) report(UiWidget::BUTTON, x, y, w, h, label, "", e, enabled, clipped);
    return hit;
}

bool Ui::buttonImpl(float x, float y, float w, float h, const std::string& label, bool enabled, bool* clipped) {
    bool hv = enabled && hover(x, y, w, h);
    Color bg = !enabled ? Color{0.12f, 0.12f, 0.14f, 0.8f} : hv ? Color{accent.r * 0.5f, accent.g * 0.5f, accent.b * 0.5f, 0.95f}
                                                             : Color{0.14f, 0.16f, 0.2f, 0.9f};
    r_->rect(x, y, w, h, bg);
    if (hv) r_->rect(x, y + h - 2 * scale_, w, 2 * scale_, accent);
    Color tc = enabled ? Color{1, 1, 1, 1} : Color{0.5f, 0.5f, 0.5f, 1};
    std::string shown = label;
    float ts = fit(shown, w - 8 * scale_, clipped);
    r_->text(x + (w - r_->textWidth(shown, ts)) * 0.5f, y + (h - r_->textHeight(ts)) * 0.5f, ts, shown, tc);
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
    std::string shown = value;
    float cw = caretWidth(), maxW = w - 12 * scale_ - cw - 2 * scale_;
    while (!shown.empty() && r_->textWidth(shown, textScale_) > maxW) shown.erase(0, 1); // long: show the end
    float tx = x + 6 * scale_, ty = y + (h - r_->textHeight(textScale_)) * 0.5f;
    r_->text(tx, ty, textScale_, shown, {1, 1, 1, 1});
    if (focused) caret(tx + r_->textWidth(shown, textScale_), ty);
    if (probe && !muted) {
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
    std::string t = index >= 0 && index < (int)names.size() ? names[index] : "?";
    bool clipped = false;
    float ts = fit(t, w - 2 * bw - 8 * scale_, &clipped);
    r_->text(x + (w - r_->textWidth(t, ts)) * 0.5f, y + (h - r_->textHeight(ts)) * 0.5f, ts, t, {1, 1, 1, 1});
    if (probe && !muted) {
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
    if (probe && !muted) report(UiWidget::TOGGLE, x, y, w, h, label, v ? "1" : "0", e, true, clipped);
    return hit;
}

bool Ui::slider(float x, float y, float w, float h, const std::string& label, float& v, float lo, float hi, const char* fmt, const UiExp& e) {
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
        if (r_->textWidth(lab + ": " + num, ts) <= maxW) return;
        ts = std::max(Renderer::kMinTextScale, maxW / (r_->textWidth(lab + ": " + num, 10.0f) * 0.1f));
        if (r_->textWidth(lab + ": " + num, ts) > maxW) lab = r_->textClip(lab, ts, std::max(0.0f, maxW - r_->textWidth(": " + num, ts)));
    };
    std::string lab;
    float ls;
    layout(buf, lab, ls);
    std::string prefix = lab + ": ";
    float tw = r_->textWidth(prefix + buf, ls), tx = x + (w - tw) * 0.5f;
    float nx = tx + r_->textWidth(prefix, ls), nw = std::max(r_->textWidth(buf, ls), 24 * scale_);
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
    if (editing) {
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
        r_->text(ex, ey, es, s, {1, 1, 1, 1});
        caret(ex + ew, ey, es);
        r_->rect(x, y + h - 2 * scale_, w, 2 * scale_, accent);
    } else {
        snprintf(buf, sizeof buf, fmt, v);
        layout(buf, lab, ls);
        std::string s = lab + ": " + buf;
        clipped = r_->textWidth(buf, ls) > w - 8 * scale_;
        float sx = x + (w - r_->textWidth(s, ls)) * 0.5f, sy = y + (h - r_->textHeight(ls)) * 0.5f;
        r_->text(sx, sy, ls, s, {1, 1, 1, 1});
        // the number looks clickable: a faint box always, brighter with an underline under the mouse
        float bx = sx + r_->textWidth(lab + ": ", ls), bw = r_->textWidth(buf, ls);
        r_->rect(bx - 3 * scale_, y + 3 * scale_, bw + 6 * scale_, h - 6 * scale_, {1, 1, 1, overNumber ? 0.14f : 0.05f});
        if (overNumber) r_->rect(bx, sy + r_->textHeight(ls) + 1 * scale_, bw, 1 * scale_, {1, 1, 1, 0.7f});
    }
    if (probe && !muted) {
        UiWidget wd;
        wd.where = where; wd.scope = scope;
        wd.kind = UiWidget::SLIDER; wd.x = x; wd.y = y; wd.w = w; wd.h = h; wd.nx = nx; wd.nw = nw;
        wd.dx = lo; wd.dy = hi;   // (a slider's range rides in dx / dy, its shown decimals in count)
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
