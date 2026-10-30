#pragma once
// Scriptable HUD widgets: hud.cfg (next to quark.cfg) places any game value on screen as a number, a bar,
// a colour-graded bar, an icon whose opacity follows the value, or an icon + bar. Reloaded when the file
// changes, so it can be edited while playing. Ranked servers ignore it (default HUD only).
#include <string>
#include <vector>
#include "renderer.h"

enum HudValue {
    HV_HEALTH, HV_SPEED, HV_DASH, HV_HOOK, HV_FPS, HV_FPSMIN, HV_PING, HV_LOSS, HV_TIME,
    HV_ENEMIES, HV_TEAMMATES, HV_KILLS, HV_DEATHS, HV_SCORE, HV_TICKRATE,
    // 1.19.4 (batch 3): appended, so a hud.cfg names them by word as always
    HV_SNAPRATE, HV_ACCURACY, HV_DAMAGE, HV_STREAK, HV_ROUND, HV_VSPEED, HV_HEIGHT, HV_WEAPON, HV_COUNT
};
const char* hudValueName(int v);

struct HudVals { float v[HV_COUNT] = {}; float max[HV_COUNT] = {}; };

struct HudWidget {
    int value = HV_HEALTH;
    enum Style { NUMBER, BAR, GRADBAR, ICON, ICONBAR } style = NUMBER;
    int anchor = 0;               // 0 tl, 1 tr, 2 bl, 3 br, 4 centre
    float x = 0, y = 0, w = 120, h = 10, size = 1;  // 720p units, scaled with the window
    Color color = {1, 1, 1, 1}, low = {1, 0.2f, 0.2f, 1}; // gradbar / number: low colour at 0, color at max
    float max = 0;                // 0 = the value's natural maximum
    int icon = 0;                 // heart, bolt, hook, skull, star, shield, clock, eye
    bool grade = false;           // number tinted low -> color
    std::string label;
};

class HudWidgets {
public:
    void poll(const std::string& path);      // (re)load when the file's mtime changes; cheap stat once a second
    bool pollDue() const;                    // a second since the last poll: poll() would look at the file
    bool empty() const { return widgets_.empty(); }
    void draw(Renderer& r, const HudVals& vals, float W, float H, float s) const;
    const std::string& error() const { return error_; }
    // editor
    std::vector<HudWidget>& list() { return widgets_; }
    void bounds(const HudWidget& w, const HudVals& vals, const Renderer& r, float W, float H, float s,
                float& x, float& y, float& fw, float& fh) const; // on-screen box (without the label)
    std::string toText() const;                  // hud.cfg text (also the export format)
    int fromText(const std::string& text);       // replaces the widgets; returns how many were read
    bool save(const std::string& path);
    static void setFromScreen(HudWidget& w, float x, float y, float fw, float fh, float W, float H, float s); // re-anchor to nearest corner
private:
    std::vector<HudWidget> widgets_;
    long long mtime_ = -1;
    double lastPoll_ = -10;
    std::string error_;
};
