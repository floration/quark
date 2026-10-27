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
// Cost rules (latency first): fixed-capacity arrays (sized once, when the Layout is made) and no heap allocation after
// that (quark_tests layoutbench counts them); and a cached solve: what's declared is hashed as it's declared, and
// solve() keeps the last result when the hash and the screen are unchanged, so a frame whose layout didn't change only
// pays for the declaration (a few stores and hash mixes per box). Lengths are pixels; nothing is rounded here (the
// renderer rounds).
//
// Memory: a declaration goes into one scratch shared by every Layout of the thread (what solving needs: each box's
// Decl and tree links), and a Layout keeps only its results (32 bytes a box), written when it really solves. A page of
// many small layouts (the HUD: one per element) then touches the same few hot cache lines for every declaration, not
// a dozen cold arrays per layout: in game, with the world pass between frames, that was most of the cost. The rule
// that comes with it: declare one Layout at a time on a thread (begin() .. solve(), no other Layout's begin() between;
// if one does, the interrupted solve() keeps its last result and works it out again next time).
#pragma once
#include <cstdint>
#include <vector>

namespace lay {

enum Dir : uint8_t { ROW, COL };
// main axis: where the children sit; BETWEEN: the first at the start, the last at the end, the rest evenly between.
// cross axis: START / CENTER / END, or STRETCH (as wide as the parent's inside).
enum Align : uint8_t { START, CENTER, END, BETWEEN, STRETCH };
enum SizeMode : uint32_t { FIT, FIXED, GROW, PERCENT };   // (32-bit: Size and Decl have no padding, see Decl)

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
// (No padding bytes, all 4-byte words: the cached solve hashes a Decl as its 23 words, which every field is part of.)
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
    bool clip = false;           // children outside this box are culled (visible() false)
    uint16_t tag = 0;            // the page's own data (the HUD: which element)
    float ax = 0, ay = 0, px = 0, py = 0, ox = 0, oy = 0;
    float scrollX = 0, scrollY = 0;   // (a clipping box: its children moved up / left by this much)

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

static_assert(sizeof(Size) == 16 && sizeof(Decl) == 92, "Decl: no padding (its words are hashed)");

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// A stable id from a label, at compile time where it can be (FNV-1a).
constexpr uint32_t id(const char* s, uint32_t h = 2166136261u) { return *s ? id(s + 1, (h ^ (uint8_t)*s) * 16777619u) : h; }
constexpr uint32_t id(uint32_t base, uint32_t index) { return (base ^ (index + 0x9e3779b9u)) * 16777619u; }

class alignas(64) Layout {   // (aligned: what a declaration touches is in its first cache line)
public:
    static constexpr int kMaxDepth = 32;
    explicit Layout(int capacity = 256);   // (the only allocation: every array, once)
    int capacity() const { return cap_; }

    // Starts a declaration for a screen W x H (the roots' parent).
    void begin(float W, float H);
    // A box under the open one (a root when none is open); returns its index, -1 when full (it and its children are
    // dropped: close() still pairs with it; overflowed() says so). label: a static string, for the debug overlay and the UI test.
    int open(uint32_t boxId, const Decl& d, const char* label = nullptr);
    void close();
    int leaf(uint32_t boxId, const Decl& d, const char* label = nullptr) { int i = open(boxId, d, label); close(); return i; }
    // Places everything declared since begin(). Returns true if it worked them out again, false when the cached result
    // still held (the same declaration on the same screen).
    bool solve();

    // The last solve's boxes (after solve(); a declaration in progress doesn't change them)
    int count() const { return outN_; }
    const Rect& rect(int i) const { return out_[i].r; }
    bool visible(int i) const { return out_[i].visible != 0; }
    int find(uint32_t boxId) const;           // the index of a box by its id (-1: none); a linear scan, for editors
    uint32_t boxId(int i) const { return out_[i].id; }
    const char* label(int i) const { return out_[i].label; }
    int parent(int i) const { return out_[i].parent; }
    int depth(int i) const { int d = 0; for (int p = out_[i].parent; p >= 0; p = out_[p].parent) d++; return d; }
    bool overflowed() const { return dropped_ > 0; }   // something didn't fit in capacity() this declaration
    long solves() const { return solves_; }           // how many real solves (tests: the cache works)
    void invalidate() { lastHash_ = 0; }              // the next solve() works it all out again
    // Asks the CPU to fetch what the next declaration and solve read (this, its first results, the thread's scratch),
    // so a page of several layouts drawn after the world pass (cold caches) waits on all its misses at once, not on
    // each in turn. Costs a few instructions; changes nothing.
    void prefetch() const;
    // A page's shortcut past declaring at all: key holds everything its declaration is made from (sizes, offsets,
    // measured text widths, a theme's version). True when it's byte for byte the key of the last call and that
    // declaration was solved: the last result stands, so skip begin() .. solve(). False: declare and solve as usual
    // (the key is kept for next time). Keys up to kMaxKey bytes; a longer one is never the same.
    static constexpr int kMaxKey = 96;
    bool same(const void* key, int bytes);

private:
    struct Scratch;            // (layout.cpp: the thread's declaration arrays)
    static Scratch& threadScratch();
    struct Out {               // a solved box (32 bytes)
        Rect r;
        uint32_t id = 0;
        int16_t parent = -1;
        uint8_t visible = 0;
        const char* label = nullptr;
    };
    void mix(uint64_t v) { hash_ = (hash_ ^ v) * 0x100000001b3ull; }
    void mixf(float f);
    void sizeAxis(int axis);   // fit sizes bottom-up, then grow / percent / stretch top-down, on one axis
    void place();              // positions, top-down; floating boxes; scroll and culling

    // (the first cache line: everything a declaration and a cached solve use, the open boxes' stack's first entries)
    uint64_t hash_ = 0, lastHash_ = 0;
    Scratch* s_ = nullptr;     // (begin(): the thread's scratch, while this Layout is declared)
    int cap_;
    int n_ = 0, depth_ = 0, dropDepth_ = 0, outN_ = 0, lastN_ = -1;
    int16_t stack_[kMaxDepth];
    std::vector<Out> out_;
    int keyN_ = -1;
    unsigned char key_[kMaxKey];
    int dropped_ = 0;
    float W_ = 0, H_ = 0;
    long solves_ = 0;
};

}   // namespace lay
