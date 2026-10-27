#include "layout.h"
#include <algorithm>
#include <cstring>

namespace lay {

// What a declaration needs until it's solved: one set per thread, shared by every Layout (layout.h: Memory). It grows
// to the largest capacity a Layout of the thread has (an allocation the first time that Layout begins, never after).
struct Layout::Scratch {
    std::vector<Decl> decl;
    std::vector<uint32_t> id;
    std::vector<const char*> label;
    std::vector<int16_t> parent, first, next, last;
    std::vector<float> fit;    // (solve: each box's fitted size, 2 per box)
    std::vector<Rect> clip;    // (place: what each box's children are culled against)
    const Layout* owner = nullptr;
    int cap = 0;
    void reserve(int n) {
        if (n <= cap) return;
        decl.resize(n); id.resize(n); label.resize(n); parent.resize(n); first.resize(n); next.resize(n); last.resize(n); fit.resize(2 * n); clip.resize(n);
        cap = n;
    }
};

Layout::Scratch& Layout::threadScratch() {
    static thread_local Layout::Scratch s;
    return s;
}

Layout::Layout(int capacity) : cap_(capacity < 1 ? 1 : capacity > 32000 ? 32000 : capacity), out_((size_t)cap_) {
    threadScratch().reserve(cap_);   // (made with the Layout, on the thread that makes it: usually the one that uses it)
}

void Layout::begin(float W, float H) {
    s_ = &threadScratch();
    s_->reserve(cap_);   // (only grows: once for a thread that didn't make this Layout)
    s_->owner = this;
    n_ = depth_ = dropped_ = dropDepth_ = 0;
    W_ = W; H_ = H;
    hash_ = 0xcbf29ce484222325ull;
    mixf(W); mixf(H);
}

void Layout::prefetch() const {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(this);
    __builtin_prefetch(key_);
    __builtin_prefetch(out_.data());
    __builtin_prefetch(out_.data() + 2);
#endif
}

void Layout::prefetchScratch() {
#if defined(__GNUC__) || defined(__clang__)
    const Scratch& S = threadScratch();
    if (S.cap > 0) { __builtin_prefetch(S.decl.data()); __builtin_prefetch((const char*)S.decl.data() + 64); __builtin_prefetch((const char*)S.decl.data() + 128); }
#endif
}

bool Layout::same(const void* key, int bytes) {
    if (bytes > kMaxKey) { keyN_ = -1; return false; }
    if (bytes == keyN_ && lastN_ >= 0 && std::memcmp(key_, key, (size_t)bytes) == 0) return true;
    std::memcpy(key_, key, (size_t)bytes);
    keyN_ = bytes;
    return false;
}

void Layout::mixf(float f) { uint32_t u; std::memcpy(&u, &f, 4); mix(u); }

int Layout::open(uint32_t boxId, const Decl& d, const char* label) {
    if (dropDepth_ > 0 || n_ >= cap_ || depth_ >= kMaxDepth || !s_ || s_->owner != this) { dropDepth_++; dropped_++; return -1; }
    Scratch& S = *s_;
    const int i = n_++;
    const int p = depth_ ? stack_[depth_ - 1] : -1;
    S.decl[i] = d; S.id[i] = boxId; S.label[i] = label; S.parent[i] = (int16_t)p;
    S.first[i] = S.last[i] = S.next[i] = -1;
    if (p >= 0) {
        if (S.first[p] < 0) S.first[p] = (int16_t)i; else S.next[S.last[p]] = (int16_t)i;
        S.last[p] = (int16_t)i;
    }
    stack_[depth_++] = (int16_t)i;
    // (everything that changes the result goes into the hash: the tree's shape and each field; the id and label too,
    // as the results keep them)
    // (the Decl as its 23 words, read from the copy just made: four independent lanes, so the multiplies overlap
    // instead of waiting on each other; bools are whole bytes, 0 or 1, so the words are always the same for the same
    // fields)
    uint32_t w[24];
    std::memcpy(w, &S.decl[i], sizeof(Decl));
    w[23] = boxId;
    uint64_t h0 = hash_ ^ ((uint64_t)(uint16_t)p << 32), h1 = (uint64_t)(uintptr_t)label, h2 = 0x9e3779b97f4a7c15ull, h3 = 0xc2b2ae3d27d4eb4full;
    for (int k = 0; k < 24; k += 4) {
        h0 = (h0 ^ w[k]) * 0x100000001b3ull; h1 = (h1 ^ w[k + 1]) * 0x100000001b3ull;
        h2 = (h2 ^ w[k + 2]) * 0x100000001b3ull; h3 = (h3 ^ w[k + 3]) * 0x100000001b3ull;
    }
    hash_ = (h0 ^ (h1 * 31) ^ (h2 * 961) ^ (h3 * 29791)) * 0x100000001b3ull;
    return i;
}

void Layout::close() {
    if (dropDepth_ > 0) { dropDepth_--; return; }
    if (depth_ > 0) depth_--;
}

int Layout::find(uint32_t boxId) const {
    for (int i = 0; i < outN_; i++) if (out_[i].id == boxId) return i;
    return -1;
}

bool Layout::solve() {
    mix((uint64_t)n_);
    depth_ = 0;
    Scratch* S = s_;
    if (!S || S->owner != this) { s_ = nullptr; lastHash_ = 0; keyN_ = -1; return false; }   // (another Layout began meanwhile: keep the last result)
    S->owner = nullptr;
    if (hash_ == lastHash_ && n_ == lastN_) { s_ = nullptr; return false; }
    // (the results' id, parent and label: copied now, so a cached frame never touches them)
    outN_ = n_;
    for (int i = 0; i < n_; i++) { Out& o = out_[i]; o.id = S->id[i]; o.parent = S->parent[i]; o.label = S->label[i]; }
    sizeAxis(0);
    sizeAxis(1);
    place();
    s_ = nullptr;
    lastHash_ = hash_; lastN_ = n_;
    solves_++;
    return true;
}

static float clampSize(const Size& s, float v) { return std::min(std::max(v, s.min), s.max); }

void Layout::sizeAxis(int a) {
    Scratch& S = *s_;
    const Decl* decl = S.decl.data();
    const int16_t *first = S.first.data(), *next = S.next.data();
    float* fit = S.fit.data();
    auto sizeOf = [a](const Decl& d) -> const Size& { return a ? d.h : d.w; };
    auto padSum = [a](const Decl& d) { return d.pad[a] + d.pad[a + 2]; };
    auto mainAxis = [a](const Decl& d) { return (d.dir == ROW) == (a == 0); };
    // bottom-up: what each box needs for its children (in declaration order every child comes after its parent)
    for (int i = n_ - 1; i >= 0; i--) {
        const Decl& d = decl[i];
        const Size& sz = sizeOf(d);
        float content = 0;
        int k = 0;
        for (int c = first[i]; c >= 0; c = next[c]) {
            if (decl[c].floating || sizeOf(decl[c]).mode == PERCENT) continue;   // (out of the flow / sized from us)
            const float f = fit[2 * c + a];
            if (mainAxis(d)) content += f + (k ? d.gap : 0); else content = std::max(content, f);
            k++;
        }
        fit[2 * i + a] = clampSize(sz, sz.mode == FIXED ? sz.v : content + padSum(d));
    }
    // top-down: the final sizes; a box sizes its children once its own is known
    auto setSize = [&](int i, float v) { if (a) out_[i].r.h = v; else out_[i].r.w = v; };
    auto getSize = [&](int i) { return a ? out_[i].r.h : out_[i].r.w; };
    auto outerSize = [&](const Size& s, int i, float space) {   // a root's or a floating box's size in `space`
        return s.mode == GROW ? clampSize(s, space) : s.mode == PERCENT ? clampSize(s, space * s.v) : fit[2 * i + a];
    };
    for (int i = 0; i < n_; i++) {
        const Decl& d = decl[i];
        if (out_[i].parent < 0) setSize(i, outerSize(sizeOf(d), i, a ? H_ : W_));
        const float inner = std::max(0.0f, getSize(i) - padSum(d));
        if (mainAxis(d)) {
            float used = 0;
            int k = 0, grow = 0;
            for (int c = first[i]; c >= 0; c = next[c]) {
                const Decl& cd = decl[c];
                if (cd.floating) { setSize(c, outerSize(sizeOf(cd), c, inner)); continue; }
                const Size& s = sizeOf(cd);
                const float v = s.mode == PERCENT ? clampSize(s, inner * s.v) : fit[2 * c + a];
                setSize(c, v);
                used += v + (k ? d.gap : 0);
                grow += s.mode == GROW;
                k++;
            }
            if (grow && used < inner) {   // the room left, shared by the growing children (each within its max)
                const float each = (inner - used) / grow;
                for (int c = first[i]; c >= 0; c = next[c])
                    if (!decl[c].floating && sizeOf(decl[c]).mode == GROW) setSize(c, clampSize(sizeOf(decl[c]), getSize(c) + each));
            }
        } else {
            for (int c = first[i]; c >= 0; c = next[c]) {
                const Decl& cd = decl[c];
                const Size& s = sizeOf(cd);
                if (cd.floating) { setSize(c, outerSize(s, c, inner)); continue; }
                const Align al = cd.self >= 0 ? (Align)cd.self : d.cross;
                setSize(c, s.mode == GROW || (al == STRETCH && s.mode == FIT) ? clampSize(s, inner)
                         : s.mode == PERCENT ? clampSize(s, inner * s.v) : fit[2 * c + a]);
            }
        }
    }
}

void Layout::place() {
    Scratch& S = *s_;
    const Decl* decl = S.decl.data();
    const int16_t *first = S.first.data(), *next = S.next.data();
    Rect* clip = S.clip.data();
    for (int i = 0; i < n_; i++) {
        Rect& r = out_[i].r;
        const Decl& d = decl[i];
        if (out_[i].parent < 0) {   // a root: on the screen, by its anchor
            // ((anchor + offset) first: a box anchored to an edge lands exactly on the point it was given)
            r.x = (W_ * d.ax + d.ox) - r.w * d.px;
            r.y = (H_ * d.ay + d.oy) - r.h * d.py;
            clip[i] = {0, 0, W_, H_};
        }
        const Rect& cr = clip[i];
        out_[i].visible = r.x <= cr.x + cr.w && r.y <= cr.y + cr.h && r.x + r.w >= cr.x && r.y + r.h >= cr.y;
        // what this box's children are culled against: ours, narrowed to this box when it clips
        Rect inner = cr;
        if (d.clip) {
            const float x0 = std::max(cr.x, r.x), y0 = std::max(cr.y, r.y);
            const float x1 = std::min(cr.x + cr.w, r.x + r.w), y1 = std::min(cr.y + cr.h, r.y + r.h);
            inner = {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
        }
        const int m = d.dir == ROW ? 0 : 1, c = 1 - m;   // main and cross axes
        const float pos[2] = {r.x, r.y}, size[2] = {r.w, r.h};
        float sum = 0;
        int k = 0;
        for (int ch = first[i]; ch >= 0; ch = next[ch])
            if (!decl[ch].floating) { sum += m ? out_[ch].r.h : out_[ch].r.w; k++; }
        const float innerMain = size[m] - d.pad[m] - d.pad[m + 2];
        const float free = innerMain - sum - d.gap * std::max(0, k - 1);
        float at = pos[m] + d.pad[m], step = d.gap;
        if (d.main == CENTER) at += free * 0.5f;
        else if (d.main == END) at += free;
        else if (d.main == BETWEEN && k > 1 && free > 0) step += free / (k - 1);
        const float innerCross = size[c] - d.pad[c] - d.pad[c + 2];
        const float sx = d.clip ? d.scrollX : 0, sy = d.clip ? d.scrollY : 0;
        for (int ch = first[i]; ch >= 0; ch = next[ch]) {
            Rect& q = out_[ch].r;
            const Decl& cd = decl[ch];
            clip[ch] = inner;
            if (cd.floating) {
                q.x = (r.x + r.w * cd.ax + cd.ox) - q.w * cd.px - sx;
                q.y = (r.y + r.h * cd.ay + cd.oy) - q.h * cd.py - sy;
                continue;
            }
            float* qp[2] = {&q.x, &q.y};
            const float qs[2] = {q.w, q.h};
            *qp[m] = at;
            at += qs[m] + step;
            const Align al = cd.self >= 0 ? (Align)cd.self : d.cross;
            float cpos = pos[c] + d.pad[c];
            if (al == CENTER) cpos += (innerCross - qs[c]) * 0.5f;
            else if (al == END) cpos += innerCross - qs[c];
            *qp[c] = cpos;
            q.x -= sx; q.y -= sy;
        }
    }
}

}   // namespace lay
