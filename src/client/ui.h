// Tiny immediate-mode UI for menus (buttons, text fields, option cyclers, sliders).
#pragma once
#include <algorithm>
#include <cstdint>
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

// ---- the UI test's contract (docs/UI_TEST.md) -----------------------------------------------------------------
// Every interactive widget says what activating it is meant to do: a UiExp is a required argument of every widget
// call, so a new button can't be added without one. In normal play it's a few pointers on the stack and one null
// check per widget (Ui::probe); `quark --uitest` registers every widget each frame, clicks it, and checks that what it
// declares actually happened (src/client/uitest.cpp).
struct UiExp {
    enum Kind : uint8_t {
        NONE,   // no expectation: the UI test fails on it
        NAV,    // the screen becomes `what` (a screen path, see Game::uiScreenPath; "x*": any path starting with x)
        CFG,    // the config key `what` changes ("x*": any key starting with x), round-trips through quark.cfg and is
                // saved when the screen is left
        STATE,  // the observable `what` (Game::uiObserve) changes; to `arg` when given
        CMD,    // a server command starting with `what` is sent
        URL,    // a URL / folder is opened (intercepted in the test)
        QUIT,   // the game quits (intercepted in the test)
        EDIT,   // the editor's map changes, and undo puts it back
        LOCAL,  // only the widget's own value changes (a field the screen reads later, a list position...)
        INERT,  // nothing changes (a disabled control, a list with one entry)
        BIND,   // a keybind slot: it captures the next key, which is saved to the config at once
    };
    enum Flags : uint8_t { OPTIONAL = 1, CLIP_OK = 2 };   // OPTIONAL: a data row that may be gone before it's tested
    Kind kind = NONE;
    uint8_t flags = 0;
    const char* what = nullptr;
    const char* arg = nullptr;  // STATE: the value it must become; text fields: what the test types (default "7")
    const char* id = nullptr;   // the widget's stable id when its label shows data (a name, a number, a state)
    static constexpr UiExp make(Kind k, const char* w = nullptr, const char* a = nullptr) { UiExp e; e.kind = k; e.what = w; e.arg = a; return e; }
    static constexpr UiExp nav(const char* screen) { return make(NAV, screen); }
    static constexpr UiExp cfg(const char* key) { return make(CFG, key); }
    static constexpr UiExp state(const char* obs, const char* value = nullptr) { return make(STATE, obs, value); }
    static constexpr UiExp cmd(const char* prefix) { return make(CMD, prefix); }
    static constexpr UiExp url() { return make(URL); }
    static constexpr UiExp quit() { return make(QUIT); }
    static constexpr UiExp edit() { return make(EDIT); }
    static constexpr UiExp local() { return make(LOCAL); }
    static constexpr UiExp inert() { return make(INERT); }
    static constexpr UiExp bind() { return make(BIND); }
    constexpr UiExp withId(const char* i) const { UiExp e = *this; e.id = i; return e; }
    constexpr UiExp typing(const char* t) const { UiExp e = *this; e.arg = t; return e; }
    constexpr UiExp optional() const { UiExp e = *this; e.flags |= OPTIONAL; return e; }
    constexpr UiExp clipOk() const { UiExp e = *this; e.flags |= CLIP_OK; return e; }
};

// One widget as drawn this frame (only built while a probe is attached).
struct UiWidget {
    enum Kind : uint8_t { BUTTON, TOGGLE, CYCLE, SLIDER, TEXT, KEY, HANDLE, VIEW };
    Kind kind = BUTTON;
    float x = 0, y = 0, w = 0, h = 0;
    float nx = 0, nw = 0;       // slider: where its number is (click it to type a value)
    float dx = 0, dy = 0;       // handle: a screen direction to drag it in
    int count = 0;              // cycle: how many values; text field: its max length; KEY: 1 + the action to hold; slider: decimals shown
    int key = 0, mods = 0;      // KEY: the key that does it (GLFW code)
    bool enabled = true, clipped = false;
    std::string label, value;   // value: the widget's own state (toggle / cycle index / slider / text)
    const char* where = nullptr;   // the screen it belongs to, when it's drawn over another one (Ui::where)
    const char* scope = nullptr;   // what it acts on, when that changes what it does (Ui::scope: the editor's selection)
    UiExp exp;
};
class UiProbe {
public:
    virtual ~UiProbe() = default;
    virtual void widget(const UiWidget& w) = 0;
};

class Ui {
public:
    void begin(Renderer* r, const UiInput& in, float scale);
    float s() const { return scale_; }           // layout scale (1.0 at 720p)
    float textScale() const { return textScale_; }
    float caretWidth() const;
    void caret(float x, float y, float ts = 0); // blinking text caret after x (text top y), at text scale ts (0: the UI's)

    void panel(float x, float y, float w, float h, float alpha = 0.75f);
    void label(float x, float y, const std::string& t, const Color& c = {1, 1, 1, 1}, float sizeMul = 1.0f);
    void labelFit(float x, float y, float maxW, std::string t, const Color& c = {1, 1, 1, 1}); // shrunk / clipped to maxW
    void labelCentered(float cx, float y, const std::string& t, const Color& c = {1, 1, 1, 1}, float sizeMul = 1.0f);
    bool button(float x, float y, float w, float h, const std::string& label, const UiExp& e, bool enabled = true);
    bool textField(int id, float x, float y, float w, float h, std::string& value, size_t maxLen, const UiExp& e);
    bool cycle(float x, float y, float w, float h, const std::string& label, int& index, int count,
               const std::vector<std::string>& names, const UiExp& e);
    bool toggle(float x, float y, float w, float h, const std::string& label, bool& v, const UiExp& e);
    bool slider(float x, float y, float w, float h, const std::string& label, float& v, float lo, float hi, const char* fmt, const UiExp& e);
    // The last slider's change was a typed value (not a drag): callers that snap a dragged value to steps keep a typed
    // one as it is.
    bool typed() const { return typed_; }
    // Interactive things that aren't drawn as widgets, for the UI test: a key that does something on this screen, a
    // draggable handle (editor gizmo, HUD widget), a clickable 3D view. Only call while probing() (no cost otherwise).
    bool probing() const { return probe != nullptr; }
    // hold >= 0: an action the test holds down (isDown) instead of pressing the key
    void keyAction(const std::string& label, int key, int mods, const UiExp& e, int hold = -1);
    void handle(float x, float y, float w, float h, float dx, float dy, const std::string& label, const UiExp& e);
    void view(float x, float y, float w, float h, const std::string& label, const UiExp& e);
    void custom(float x, float y, float w, float h, const std::string& label, const UiExp& e);   // a button drawn by hand
    void clearFocus() { focus_ = -1; }
    void focus(int id) { focus_ = id; }  // (screenshot runs: --edfocus)
    bool steadyCaret = false;            // carets don't blink (screenshot runs)
    UiProbe* probe = nullptr;            // the UI test (null in normal play)
    // While probing, set around a group of widgets: `where` - the screen they belong to, when they're drawn on others
    // too (the editor's panel under its menus); `scope` - what they act on (the editor's selection kind). Cleared by begin().
    const char* where = nullptr;
    const char* scope = nullptr;
    bool muted = false;   // drawn under a modal question that takes every click: not reported (cleared by begin())
    // A list of n items in a box `w` wide with `availH` of height left: as many columns as it takes to fit (at least
    // `minCols`), so long lists grow sideways instead of running off the bottom. Returns the column count.
    static int fitColumns(int n, float availH, float rowH, int minCols = 1, int maxCols = 6) {
        int cols = std::max(1, minCols);
        while (cols < maxCols && (float)((n + cols - 1) / cols) * rowH > availH) cols++;
        return cols;
    }
    bool typing() const { return focus_ >= 0; }
    // Text the focused field wants on the clipboard (Ctrl+C); empty if none.
    std::string takeCopy() { std::string c; c.swap(copied_); return c; }

    Color accent = {0.25f, 0.85f, 1.0f, 1.0f};

private:
    bool hover(float x, float y, float w, float h) const;
    float fit(std::string& t, float maxW, bool* clipped = nullptr) const;   // scale for t to fit maxW; clips t if it can't
    bool buttonImpl(float x, float y, float w, float h, const std::string& label, bool enabled, bool* clipped);
    void report(UiWidget::Kind k, float x, float y, float w, float h, const std::string& label, const std::string& value,
                const UiExp& e, bool enabled, bool clipped);
    Renderer* r_ = nullptr;
    UiInput in_;
    float scale_ = 1, textScale_ = 2;
    int focus_ = -1;
    int activeSlider_ = -1, sliderCounter_ = 0;
    int editSlider_ = -1;       // slider whose number is being typed in (focus_ = kSliderFocus + id)
    bool typed_ = false;        // (typed())
    std::string editText_;
    static constexpr int kSliderFocus = 100000;
    double blink_ = 0;
    std::string copied_;
};
