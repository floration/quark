// Tiny immediate-mode UI for menus (buttons, text fields, option cyclers, sliders).
#pragma once
#include <string>
#include <vector>
#include "renderer.h"

struct UiInput {
    float mx = 0, my = 0;
    bool down = false, pressed = false;
    std::string chars;
    bool backspace = false, enter = false, tab = false, escape = false;
    bool copy = false;          // Ctrl/Cmd+C this frame
};

class Ui {
public:
    void begin(Renderer* r, const UiInput& in, float scale);
    float s() const { return scale_; }           // layout scale (1.0 at 720p)
    float textScale() const { return textScale_; }

    void panel(float x, float y, float w, float h, float alpha = 0.75f);
    void label(float x, float y, const std::string& t, const Color& c = {1, 1, 1, 1}, float sizeMul = 1.0f);
    void labelCentered(float cx, float y, const std::string& t, const Color& c = {1, 1, 1, 1}, float sizeMul = 1.0f);
    bool button(float x, float y, float w, float h, const std::string& label, bool enabled = true);
    bool textField(int id, float x, float y, float w, float h, std::string& value, size_t maxLen = 32);
    bool cycle(float x, float y, float w, float h, const std::string& label, int& index, int count,
               const std::vector<std::string>& names);
    bool toggle(float x, float y, float w, float h, const std::string& label, bool& v);
    bool slider(float x, float y, float w, float h, const std::string& label, float& v, float lo, float hi, const char* fmt = "%.1f");
    void clearFocus() { focus_ = -1; }
    bool typing() const { return focus_ >= 0; }
    // Text the focused field wants on the clipboard (Ctrl+C); empty if none.
    std::string takeCopy() { std::string c; c.swap(copied_); return c; }

    Color accent = {0.25f, 0.85f, 1.0f, 1.0f};

private:
    bool hover(float x, float y, float w, float h) const;
    float fit(const std::string& t, float maxW) const;
    Renderer* r_ = nullptr;
    UiInput in_;
    float scale_ = 1, textScale_ = 2;
    int focus_ = -1;
    int activeSlider_ = -1, sliderCounter_ = 0;
    int editSlider_ = -1;       // slider whose number is being typed in (focus_ = kSliderFocus + id)
    std::string editText_;
    static constexpr int kSliderFocus = 100000;
    double blink_ = 0;
    std::string copied_;
};
