#include "hudwidgets.h"
#include <sys/stat.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

static const char* kValueNames[HV_COUNT] = {"health", "speed", "dash", "hook", "fps", "fpsmin", "ping", "loss", "time",
                                            "roundtime", "enemies", "teammates", "kills", "deaths", "score", "tickrate"};
const char* hudValueName(int v) { return v >= 0 && v < HV_COUNT ? kValueNames[v] : "?"; }

// 7x7 icons, one row per string ('#' = filled)
static const char* kIcons[][7] = {
    {".##.##.", "#######", "#######", ".#####.", "..###..", "...#...", "......."}, // heart
    {"...##..", "..##...", ".#####.", "...##..", "..##...", ".##....", "#......"}, // bolt
    {"..###..", ".#...#.", ".....#.", "...##..", "...#...", "...#...", "..###.."}, // hook
    {".#####.", "#.###.#", "#######", ".##.##.", "..###..", "..#.#..", "......."}, // skull
    {"...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", "#.....#"}, // star
    {"#######", "#######", "#######", ".#####.", ".#####.", "..###..", "...#..."}, // shield
    {"..###..", ".#..#..", "#...#.#", "#.###.#", "#.....#", ".#...#.", "..###.."}, // clock
    {".......", "..###..", ".#...#.", "#..#..#", ".#...#.", "..###..", "......."}, // eye
};
static const char* kIconNames[] = {"heart", "bolt", "hook", "skull", "star", "shield", "clock", "eye"};
static const int kIconCount = 8;
static const char* kStyleNames[] = {"number", "bar", "gradbar", "icon", "iconbar"};
static const char* kAnchorNames[] = {"tl", "tr", "bl", "br", "c"};

static Color parseRgba(const std::string& s, Color def) {
    float c[4] = {def.r, def.g, def.b, def.a};
    if (s.size() >= 6 && s.find(',') == std::string::npos) { // hex rrggbb[aa]
        unsigned v = (unsigned)strtoul(s.c_str(), nullptr, 16);
        if (s.size() == 6) v = (v << 8) | 255;
        for (int i = 0; i < 4; i++) c[i] = ((v >> (24 - 8 * i)) & 255) / 255.0f;
    } else {
        int n = sscanf(s.c_str(), "%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3]);
        bool bytes = false;
        for (int i = 0; i < n; i++) bytes |= c[i] > 1.0f; // 0-255 or 0-1, either works
        if (bytes) for (int i = 0; i < n; i++) c[i] /= 255.0f;
    }
    return {c[0], c[1], c[2], c[3]};
}

void HudWidgets::poll(const std::string& path) {
    double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - lastPoll_ < 1.0) return;
    lastPoll_ = now;
    struct stat st;
    long long mt = stat(path.c_str(), &st) == 0 ? (long long)st.st_mtime : -2;
    if (mt == mtime_) return;
    mtime_ = mt;
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    fromText(ss.str());
}

int HudWidgets::fromText(const std::string& text) {
    widgets_.clear();
    error_.clear();
    std::istringstream f(text);
    std::string line;
    int ln = 0;
    while (std::getline(f, line)) {
        ln++;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream ls(line);
        std::string word;
        if (!(ls >> word) || word != "widget") continue;
        HudWidget w;
        while (ls >> word) {
            size_t eq = word.find('=');
            if (eq == std::string::npos) continue;
            std::string k = word.substr(0, eq), v = word.substr(eq + 1);
            float fv = (float)atof(v.c_str());
            if (k == "value") {
                w.value = -1;
                for (int i = 0; i < HV_COUNT; i++) if (v == kValueNames[i]) w.value = i;
            } else if (k == "style") {
                for (int i = 0; i < 5; i++) if (v == kStyleNames[i]) w.style = (HudWidget::Style)i;
            } else if (k == "anchor") {
                for (int i = 0; i < 5; i++) if (v == kAnchorNames[i]) w.anchor = i;
            } else if (k == "x") w.x = fv;
            else if (k == "y") w.y = fv;
            else if (k == "w") w.w = fv;
            else if (k == "h") w.h = fv;
            else if (k == "size") w.size = std::max(0.25f, fv);
            else if (k == "max") w.max = fv;
            else if (k == "color") w.color = parseRgba(v, w.color);
            else if (k == "low") { w.low = parseRgba(v, w.low); w.grade = true; }
            else if (k == "label") { w.label = v; for (char& c : w.label) c = c == '_' ? ' ' : (char)toupper((unsigned char)c); }
            else if (k == "icon") for (int i = 0; i < kIconCount; i++) if (v == kIconNames[i]) w.icon = i;
        }
        if (w.value < 0) { if (error_.empty()) error_ = "HUD.CFG LINE " + std::to_string(ln) + ": UNKNOWN VALUE"; continue; }
        widgets_.push_back(w);
    }
    return (int)widgets_.size();
}

static std::string hexColor(const Color& c) {
    char b[12];
    auto by = [](float v) { return (int)std::lround(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255); };
    snprintf(b, sizeof b, "%02x%02x%02x%02x", by(c.r), by(c.g), by(c.b), by(c.a));
    return b;
}

std::string HudWidgets::toText() const {
    std::string out = "# quark HUD widgets - widget value=<name> style=number|bar|gradbar|icon|iconbar anchor=tl|tr|bl|br|c\n"
                      "#   x= y= w= h= size= max= color=rrggbbaa low=rrggbbaa label=TEXT icon=heart|bolt|hook|skull|star|shield|clock|eye\n";
    for (const HudWidget& w : widgets_) {
        char b[256];
        snprintf(b, sizeof b, "widget value=%s style=%s anchor=%s x=%g y=%g w=%g h=%g size=%g max=%g color=%s icon=%s",
                 kValueNames[w.value], kStyleNames[w.style], kAnchorNames[w.anchor], w.x, w.y, w.w, w.h, w.size, w.max,
                 hexColor(w.color).c_str(), kIconNames[w.icon]);
        out += b;
        if (w.grade) out += " low=" + hexColor(w.low);
        if (!w.label.empty()) {
            std::string l = w.label;
            for (char& c : l) if (c == ' ') c = '_';
            out += " label=" + l;
        }
        out += "\n";
    }
    return out;
}

bool HudWidgets::save(const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << toText();
    f.close();
    struct stat st;
    if (stat(path.c_str(), &st) == 0) mtime_ = (long long)st.st_mtime; // don't reload our own write
    return true;
}

static void fmtNum(char* b, size_t n, float v) { snprintf(b, n, (v != std::floor(v) && std::fabs(v) < 100) ? "%.1f" : "%.0f", v); }

void HudWidgets::bounds(const HudWidget& w, const HudVals& vals, const Renderer& r, float W, float H, float s,
                        float& x, float& y, float& fw, float& fh) const {
    float k = s * w.size, ts = std::fmax(1.0f, std::round(s * 2 * w.size));
    char num[24];
    fmtNum(num, sizeof num, vals.v[w.value]);
    float iw = 7 * 2 * k, bw = w.w * k;
    fw = w.style == HudWidget::NUMBER ? r.textWidth(num, ts) : w.style == HudWidget::ICON ? iw : w.style == HudWidget::ICONBAR ? iw + 4 * k + bw : bw;
    fh = w.style == HudWidget::NUMBER ? r.textHeight(ts) : w.style == HudWidget::BAR || w.style == HudWidget::GRADBAR ? w.h * k : iw;
    x = w.x * s; y = w.y * s;
    switch (w.anchor) { // offsets measured from the anchor edge, so right / bottom widgets stay on screen at any size
        case 1: x = W - x - fw; break;
        case 2: y = H - y - fh; break;
        case 3: x = W - x - fw; y = H - y - fh; break;
        case 4: x = W * 0.5f + x - fw * 0.5f; y = H * 0.5f + y - fh * 0.5f; break;
    }
}

void HudWidgets::setFromScreen(HudWidget& w, float x, float y, float fw, float fh, float W, float H, float s) {
    float mx = x + fw * 0.5f, my = y + fh * 0.5f;
    bool centre = std::fabs(mx - W * 0.5f) < W * 0.15f && std::fabs(my - H * 0.5f) < H * 0.15f;
    if (centre) { w.anchor = 4; w.x = (mx - W * 0.5f) / s; w.y = (my - H * 0.5f) / s; }
    else {
        bool right = mx > W * 0.5f, bottom = my > H * 0.5f;
        w.anchor = (bottom ? 2 : 0) + (right ? 1 : 0);
        w.x = (right ? W - x - fw : x) / s;
        w.y = (bottom ? H - y - fh : y) / s;
    }
    w.x = std::round(w.x); w.y = std::round(w.y);
}

void HudWidgets::draw(Renderer& r, const HudVals& vals, float W, float H, float s) const {
    for (const HudWidget& w : widgets_) {
        float v = vals.v[w.value], mx = w.max > 0 ? w.max : vals.max[w.value];
        float f = mx > 0 ? std::fmin(std::fmax(v / mx, 0.0f), 1.0f) : 0.0f;
        auto mix = [&](float t) { return Color{w.low.r + (w.color.r - w.low.r) * t, w.low.g + (w.color.g - w.low.g) * t,
                                               w.low.b + (w.color.b - w.low.b) * t, w.low.a + (w.color.a - w.low.a) * t}; };
        float k = s * w.size;
        float ts = std::fmax(1.0f, std::round(s * 2 * w.size));
        char num[24];
        fmtNum(num, sizeof num, v);
        float iw = 7 * 2 * k, bw = w.w * k;
        float x, y, fw, fh;
        bounds(w, vals, r, W, H, s, x, y, fw, fh);
        if (!w.label.empty()) {
            float lts = std::fmax(1.0f, std::round(s * w.size));
            float lx = (w.anchor == 1 || w.anchor == 3) ? x + fw - r.textWidth(w.label, lts) : w.anchor == 4 ? x + (fw - r.textWidth(w.label, lts)) * 0.5f : x;
            r.text(lx, y - r.textHeight(lts) - 3 * k, lts, w.label, {w.color.r, w.color.g, w.color.b, 0.8f});
        }
        auto icon = [&](float ix, float iy, Color c) {
            float p = 2 * k;
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 7; col++)
                    if (kIcons[w.icon][row][col] == '#') r.rect(ix + col * p, iy + row * p, p, p, c);
        };
        auto bar = [&](float bx, float by, float bwid, float bh, Color c) {
            r.rect(bx - 1, by - 1, bwid + 2, bh + 2, {0, 0, 0, 0.7f});
            r.rect(bx, by, bwid * f, bh, c);
        };
        Color graded = w.grade ? mix(f) : w.color;
        switch (w.style) {
            case HudWidget::NUMBER: r.text(x, y, ts, num, graded); break;
            case HudWidget::BAR: bar(x, y, bw, fh, w.color); break;
            case HudWidget::GRADBAR: bar(x, y, bw, fh, mix(f)); break;
            case HudWidget::ICON: icon(x, y, {w.color.r, w.color.g, w.color.b, w.color.a * (0.15f + 0.85f * f)}); break;
            case HudWidget::ICONBAR:
                icon(x, y, graded);
                bar(x + iw + 4 * k, y + iw * 0.5f - w.h * k * 0.5f, bw, w.h * k, graded);
                break;
        }
    }
}
