// Layout: a small box layout engine of our own (HUD plan, q 2026-10-25: self-made, C++17, no dependency). It only works
// out rectangles: no drawing, no input. A page (the HUD) declares nested boxes each frame, solve() places them, and the
// page draws its own things into L.rect(i).
//
//   declare  open(id, decl) / close() / leaf(): rows and columns, sized fit / fixed / grow / percent with min and max,
//            padding, gap, alignment (start, centre, end, space between) and floating boxes (anchored to the parent or
//            the screen, like the HUD's elements on their corners)
//   solve    two passes over the flat array per axis: sizes bottom-up (fit), then grow / percent and positions top-down
//   draw     rect(i); visible(i) is false for a box clipped out by a scrolled or clipping ancestor (culling)
//
// Cost rules (latency first): fixed-capacity arrays (kMaxBoxes) and no heap allocation, ever (quark_tests layoutbench
// counts them); and a cached solve: what's declared is hashed as it's declared, and solve() keeps the last result when
// the hash and the screen are unchanged, so a frame whose layout didn't change only pays for the declaration (a few
// stores and hash mixes per box). Lengths are pixels; nothing is rounded here (the renderer rounds).
#pragma once
#include <cstdint>

namespace lay {

enum Dir : uint8_t { ROW, COL };
// main axis: where the children sit; BETWEEN: the first at the start, the last at the end, the rest evenly between.
// cross axis: START / CENTER / END, or STRETCH (as wide as the parent's inside).
enum Align : uint8_t { START, CENTER, END, BETWEEN, STRETCH };
enum SizeMode : uint8_t { FIT, FIXED, GROW, PERCENT };

struct Size {
    SizeMode mode = FIT;
    float v = 0;                 // FIXED: pixels; PERCENT: a fraction of the parent's inside (0..1)
    float min = 0, max = 1e9f;   // clamps, whatever the mode
    static Size fit(float mn = 0, float mx = 1e9f) { Size s; s.min = mn; s.max = mx; return s; }
    static Size fixed(float v) { Size s; s.mode = FIXED; s.v = v; return s; }
    static Size grow(float mn = 0, float mx = 1e9f) { Size s; s.mode = GROW; s.min = mn; s.max = mx; return s; }
    static Size pct(float f) { Size s; s.mode = PERCENT; s.v = f; return s; }
};

// What a box asks for. Default: a row that fits its children, no padding, at the start of its parent.
struct Decl {
    Size w, h;
    Dir dir = ROW;
    Align main = START, cross = START;
    int8_t self = -1;            // this box's own cross alignment in its parent (an Align), -1: the parent's
    float pad[4] = {0, 0, 0, 0}; // left, top, right, bottom
    float gap = 0;               // between children on the main axis
    // floating: out of the parent's flow, its point (px, py) (fractions of its own size) put on the parent's point
    // (ax, ay) (fractions of the parent's rect; a root's parent is the screen), then moved by (ox, oy) pixels
    bool floating = false;
    float ax = 0, ay = 0, px = 0, py = 0, ox = 0, oy = 0;
    bool clip = false;           // children outside this box are culled (visible() false)
    float scrollX = 0, scrollY = 0;   // (a clipping box: its children moved up / left by this much)
    uint16_t tag = 0;            // the page's own data (the HUD: which element)

    Decl& row() { dir = ROW; return *this; }
    Decl& col() { dir = COL; return *this; }
    Decl& size(Size a, Size b) { w = a; h = b; return *this; }
    Decl& padding(float l, float t, float r, float b) { pad[0] = l; pad[1] = t; pad[2] = r; pad[3] = b; return *this; }
    Decl& padding(float x, float y) { return padding(x, y, x, y); }
    Decl& spacing(float g) { gap = g; return *this; }
    Decl& align(Align m, Align c) { main = m; cross = c; return *this; }
    Decl& alignSelf(Align a) { self = (int8_t)a; return *this; }
    Decl& floatAt(float anchorX, float anchorY, float pivotX, float pivotY, float offX = 0, float offY = 0) {
        floating = true; ax = anchorX; ay = anchorY; px = pivotX; py = pivotY; ox = offX; oy = offY; return *this;
    }
    Decl& tagged(uint16_t t) { tag = t; return *this; }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// A stable id from a label, at compile time where it can be (FNV-1a).
constexpr uint32_t id(const char* s, uint32_t h = 2166136261u) { return *s ? id(s + 1, (h ^ (uint8_t)*s) * 16777619u) : h; }
constexpr uint32_t id(uint32_t base, uint32_t index) { return (base ^ (index + 0x9e3779b9u)) * 16777619u; }

class Layout {
public:
    static constexpr int kMaxBoxes = 256;
    static constexpr int kMaxDepth = 32;

    // Starts a declaration for a screen W x H (the roots' parent).
    void begin(float W, float H);
    // A box under the open one (a root when none is open); returns its index, -1 when full (it and its children are
    // dropped: close() still pairs with it). label: a static string, for the debug overlay and the UI test.
    int open(uint32_t boxId, const Decl& d, const char* label = nullptr);
    void close();
    int leaf(uint32_t boxId, const Decl& d, const char* label = nullptr) { int i = open(boxId, d, label); close(); return i; }
    // Places everything declared since begin(). Returns true if it worked them out again, false when the cached result
    // still held (the same declaration on the same screen).
    bool solve();

    int count() const { return n_; }
    const Rect& rect(int i) const { return rects_[i]; }
    bool visible(int i) const { return visible_[i]; }
    int find(uint32_t boxId) const;           // the index of a box by its id (-1: none); a linear scan, for editors
    uint32_t boxId(int i) const { return ids_[i]; }
    const char* label(int i) const { return labels_[i]; }
    int parent(int i) const { return parent_[i]; }
    const Decl& decl(int i) const { return decl_[i]; }
    int depth(int i) const { int d = 0; for (int p = parent_[i]; p >= 0; p = parent_[p]) d++; return d; }
    bool overflowed() const { return dropped_ > 0; }   // something didn't fit in kMaxBoxes this declaration
    long solves() const { return solves_; }           // how many real solves (tests: the cache works)
    void invalidate() { lastHash_ = 0; }              // the next solve() works it all out again

private:
    void mix(uint64_t v) { hash_ = (hash_ ^ v) * 0x100000001b3ull; }
    void mixf(float f);
    void sizeAxis(int axis);   // fit sizes bottom-up, then grow / percent / stretch top-down, on one axis
    void place();              // positions, top-down; floating boxes; scroll and culling

    Decl decl_[kMaxBoxes];
    uint32_t ids_[kMaxBoxes];
    const char* labels_[kMaxBoxes];
    int16_t parent_[kMaxBoxes], first_[kMaxBoxes], next_[kMaxBoxes], last_[kMaxBoxes];
    float fit_[kMaxBoxes][2];          // (solve: each box's fitted size per axis)
    Rect rects_[kMaxBoxes];
    Rect clip_[kMaxBoxes];             // (place: what each box's children are culled against)
    bool visible_[kMaxBoxes];
    int16_t stack_[kMaxDepth];
    int depth_ = 0, n_ = 0, dropped_ = 0, dropDepth_ = 0;
    float W_ = 0, H_ = 0;
    uint64_t hash_ = 0, lastHash_ = 0;
    int lastN_ = -1;
    long solves_ = 0;
};

}   // namespace lay
