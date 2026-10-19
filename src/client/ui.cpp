#include "ui.h"
#include <cstdio>
#include "../common/net.h"

void Ui::begin(Renderer* r, const UiInput& in, float scale) {
    r_ = r;
    in_ = in;
    scale_ = scale;
    textScale_ = std::max(1.0f, std::round(2.0f * scale));
    sliderCounter_ = 0;
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

void Ui::labelCentered(float cx, float y, const std::string& t, const Color& c, float sizeMul) {
    float ts = std::max(1.0f, std::round(textScale_ * sizeMul));
    r_->text(cx - r_->textWidth(t, ts) * 0.5f, y, ts, t, c);
}

bool Ui::button(float x, float y, float w, float h, const std::string& label, bool enabled) {
    bool hv = enabled && hover(x, y, w, h);
    Color bg = !enabled ? Color{0.12f, 0.12f, 0.14f, 0.8f} : hv ? Color{accent.r * 0.5f, accent.g * 0.5f, accent.b * 0.5f, 0.95f}
                                                             : Color{0.14f, 0.16f, 0.2f, 0.9f};
    r_->rect(x, y, w, h, bg);
    if (hv) r_->rect(x, y + h - 2 * scale_, w, 2 * scale_, accent);
    Color tc = enabled ? Color{1, 1, 1, 1} : Color{0.5f, 0.5f, 0.5f, 1};
    r_->text(x + (w - r_->textWidth(label, textScale_)) * 0.5f, y + (h - r_->textHeight(textScale_)) * 0.5f, textScale_, label, tc);
    return hv && in_.pressed;
}

bool Ui::textField(int id, float x, float y, float w, float h, std::string& value, size_t maxLen) {
    if (in_.pressed && hover(x, y, w, h)) focus_ = id;
    bool focused = focus_ == id;
    bool changed = false;
    if (focused) {
        for (char c : in_.chars)
            if (c >= 32 && c < 127 && value.size() < maxLen) { value += c; changed = true; }
        if (in_.backspace && !value.empty()) { value.pop_back(); changed = true; }
        if (in_.enter || in_.tab || in_.escape) focus_ = -1;
    }
    r_->rect(x, y, w, h, focused ? Color{0.1f, 0.12f, 0.16f, 1} : Color{0.08f, 0.09f, 0.11f, 0.95f});
    r_->rect(x, y + h - 2 * scale_, w, 2 * scale_, focused ? accent : Color{0.3f, 0.3f, 0.35f, 1});
    std::string shown = value;
    float maxW = w - 12 * scale_;
    while (!shown.empty() && r_->textWidth(shown + "_", textScale_) > maxW) shown.erase(0, 1);
    if (focused && std::fmod(timeNow(), 1.0) < 0.5) shown += "_";
    r_->text(x + 6 * scale_, y + (h - r_->textHeight(textScale_)) * 0.5f, textScale_, shown, {1, 1, 1, 1});
    return changed;
}

bool Ui::cycle(float x, float y, float w, float h, const std::string& label, int& index, int count,
               const std::vector<std::string>& names) {
    if (count <= 0) return false;
    float bw = h;
    bool changed = false;
    if (button(x, y, bw, h, "<")) { index = (index + count - 1) % count; changed = true; }
    if (button(x + w - bw, y, bw, h, ">")) { index = (index + 1) % count; changed = true; }
    r_->rect(x + bw, y, w - 2 * bw, h, {0.08f, 0.09f, 0.11f, 0.95f});
    std::string t = label + ": " + (index >= 0 && index < (int)names.size() ? names[index] : "?");
    r_->text(x + (w - r_->textWidth(t, textScale_)) * 0.5f, y + (h - r_->textHeight(textScale_)) * 0.5f, textScale_, t, {1, 1, 1, 1});
    return changed;
}

bool Ui::toggle(float x, float y, float w, float h, const std::string& label, bool& v) {
    if (button(x, y, w, h, label + ": " + (v ? "ON" : "OFF"))) { v = !v; return true; }
    return false;
}

bool Ui::slider(float x, float y, float w, float h, const std::string& label, float& v, float lo, float hi, const char* fmt) {
    int id = sliderCounter_++;
    if (in_.pressed && hover(x, y, w, h)) activeSlider_ = id;
    bool changed = false;
    if (activeSlider_ == id && in_.down) {
        float t = clampf((in_.mx - x) / w, 0, 1);
        float nv = lo + (hi - lo) * t;
        if (nv != v) { v = nv; changed = true; }
    }
    float t = clampf((v - lo) / (hi - lo), 0, 1);
    r_->rect(x, y, w, h, {0.08f, 0.09f, 0.11f, 0.95f});
    r_->rect(x, y, w * t, h, {accent.r * 0.45f, accent.g * 0.45f, accent.b * 0.45f, 0.95f});
    char buf[64];
    snprintf(buf, sizeof buf, fmt, v);
    std::string s = label + ": " + buf;
    r_->text(x + (w - r_->textWidth(s, textScale_)) * 0.5f, y + (h - r_->textHeight(textScale_)) * 0.5f, textScale_, s, {1, 1, 1, 1});
    return changed;
}
