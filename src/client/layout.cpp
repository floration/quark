#include "layout.h"
#include <algorithm>
#include <cstring>

namespace lay {

Layout::Layout(int capacity)
    : cap_(capacity < 1 ? 1 : capacity > 32000 ? 32000 : capacity), decl_(cap_), ids_(cap_), labels_(cap_), parent_(cap_), first_(cap_),
      next_(cap_), last_(cap_), fit_(2 * cap_), rects_(cap_), clip_(cap_), visible_(cap_) {}

void Layout::begin(float W, float H) {
    n_ = depth_ = dropped_ = dropDepth_ = 0;
    W_ = W; H_ = H;
    hash_ = 0xcbf29ce484222325ull;
    mixf(W); mixf(H);
}

void Layout::mixf(float f) { uint32_t u; std::memcpy(&u, &f, 4); mix(u); }

int Layout::open(uint32_t boxId, const Decl& d, const char* label) {
    if (dropDepth_ > 0 || n_ >= cap_ || depth_ >= kMaxDepth) { dropDepth_++; dropped_++; return -1; }
    const int i = n_++;
    const int p = depth_ ? stack_[depth_ - 1] : -1;
    decl_[i] = d; ids_[i] = boxId; labels_[i] = label;
    parent_[i] = (int16_t)p; first_[i] = last_[i] = next_[i] = -1;
    if (p >= 0) {
        if (first_[p] < 0) first_[p] = (int16_t)i; else next_[last_[p]] = (int16_t)i;
        last_[p] = (int16_t)i;
    }
    stack_[depth_++] = (int16_t)i;
    // (everything that changes the result goes into the hash: the tree's shape and each field)
    mix(((uint64_t)(uint16_t)p << 32) | boxId);
    for (const Size* s : {&d.w, &d.h}) { mix(s->mode); mixf(s->v); mixf(s->min); mixf(s->max); }
    mix(d.dir | d.main << 8 | d.cross << 16 | (uint32_t)(uint8_t)d.self << 24 | (uint64_t)d.floating << 32 | (uint64_t)d.clip << 33 | (uint64_t)d.tag << 40);
    for (float f : d.pad) mixf(f);
    mixf(d.gap);
    if (d.floating) { mixf(d.ax); mixf(d.ay); mixf(d.px); mixf(d.py); mixf(d.ox); mixf(d.oy); }
    if (d.clip) { mixf(d.scrollX); mixf(d.scrollY); }
    return i;
}

void Layout::close() {
    if (dropDepth_ > 0) { dropDepth_--; return; }
    if (depth_ > 0) depth_--;
}

int Layout::find(uint32_t boxId) const {
    for (int i = 0; i < n_; i++) if (ids_[i] == boxId) return i;
    return -1;
}

bool Layout::solve() {
    mix((uint64_t)n_);
    depth_ = 0;
    if (hash_ == lastHash_ && n_ == lastN_) return false;
    sizeAxis(0);
    sizeAxis(1);
    place();
    lastHash_ = hash_; lastN_ = n_;
    solves_++;
    return true;
}

static float clampSize(const Size& s, float v) { return std::min(std::max(v, s.min), s.max); }

void Layout::sizeAxis(int a) {
    auto sizeOf = [a](const Decl& d) -> const Size& { return a ? d.h : d.w; };
    auto padSum = [a](const Decl& d) { return d.pad[a] + d.pad[a + 2]; };
    auto mainAxis = [a](const Decl& d) { return (d.dir == ROW) == (a == 0); };
    // bottom-up: what each box needs for its children (in declaration order every child comes after its parent)
    for (int i = n_ - 1; i >= 0; i--) {
        const Decl& d = decl_[i];
        const Size& sz = sizeOf(d);
        float content = 0;
        int k = 0;
        for (int c = first_[i]; c >= 0; c = next_[c]) {
            if (decl_[c].floating || sizeOf(decl_[c]).mode == PERCENT) continue;   // (out of the flow / sized from us)
            const float f = fit_[2 * c + a];
            if (mainAxis(d)) content += f + (k ? d.gap : 0); else content = std::max(content, f);
            k++;
        }
        fit_[2 * i + a] = clampSize(sz, sz.mode == FIXED ? sz.v : content + padSum(d));
    }
    // top-down: the final sizes; a box sizes its children once its own is known
    auto setSize = [&](int i, float v) { if (a) rects_[i].h = v; else rects_[i].w = v; };
    auto getSize = [&](int i) { return a ? rects_[i].h : rects_[i].w; };
    auto outerSize = [&](const Size& s, int i, float space) {   // a root's or a floating box's size in `space`
        return s.mode == GROW ? clampSize(s, space) : s.mode == PERCENT ? clampSize(s, space * s.v) : fit_[2 * i + a];
    };
    for (int i = 0; i < n_; i++) {
        const Decl& d = decl_[i];
        if (parent_[i] < 0) setSize(i, outerSize(sizeOf(d), i, a ? H_ : W_));
        const float inner = std::max(0.0f, getSize(i) - padSum(d));
        if (mainAxis(d)) {
            float used = 0;
            int k = 0, grow = 0;
            for (int c = first_[i]; c >= 0; c = next_[c]) {
                const Decl& cd = decl_[c];
                if (cd.floating) { setSize(c, outerSize(sizeOf(cd), c, inner)); continue; }
                const Size& s = sizeOf(cd);
                const float v = s.mode == PERCENT ? clampSize(s, inner * s.v) : fit_[2 * c + a];
                setSize(c, v);
                used += v + (k ? d.gap : 0);
                grow += s.mode == GROW;
                k++;
            }
            if (grow && used < inner) {   // the room left, shared by the growing children (each within its max)
                const float each = (inner - used) / grow;
                for (int c = first_[i]; c >= 0; c = next_[c])
                    if (!decl_[c].floating && sizeOf(decl_[c]).mode == GROW) setSize(c, clampSize(sizeOf(decl_[c]), getSize(c) + each));
            }
        } else {
            for (int c = first_[i]; c >= 0; c = next_[c]) {
                const Decl& cd = decl_[c];
                const Size& s = sizeOf(cd);
                if (cd.floating) { setSize(c, outerSize(s, c, inner)); continue; }
                const Align al = cd.self >= 0 ? (Align)cd.self : d.cross;
                setSize(c, s.mode == GROW || (al == STRETCH && s.mode == FIT) ? clampSize(s, inner)
                         : s.mode == PERCENT ? clampSize(s, inner * s.v) : fit_[2 * c + a]);
            }
        }
    }
}

void Layout::place() {
    for (int i = 0; i < n_; i++) {
        Rect& r = rects_[i];
        const Decl& d = decl_[i];
        if (parent_[i] < 0) {   // a root: on the screen, by its anchor
            r.x = W_ * d.ax - r.w * d.px + d.ox;
            r.y = H_ * d.ay - r.h * d.py + d.oy;
            clip_[i] = {0, 0, W_, H_};
        }
        const Rect& cr = clip_[i];
        visible_[i] = r.x <= cr.x + cr.w && r.y <= cr.y + cr.h && r.x + r.w >= cr.x && r.y + r.h >= cr.y;
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
        for (int ch = first_[i]; ch >= 0; ch = next_[ch])
            if (!decl_[ch].floating) { sum += m ? rects_[ch].h : rects_[ch].w; k++; }
        const float innerMain = size[m] - d.pad[m] - d.pad[m + 2];
        const float free = innerMain - sum - d.gap * std::max(0, k - 1);
        float at = pos[m] + d.pad[m], step = d.gap;
        if (d.main == CENTER) at += free * 0.5f;
        else if (d.main == END) at += free;
        else if (d.main == BETWEEN && k > 1 && free > 0) step += free / (k - 1);
        const float innerCross = size[c] - d.pad[c] - d.pad[c + 2];
        const float sx = d.clip ? d.scrollX : 0, sy = d.clip ? d.scrollY : 0;
        for (int ch = first_[i]; ch >= 0; ch = next_[ch]) {
            Rect& q = rects_[ch];
            const Decl& cd = decl_[ch];
            clip_[ch] = inner;
            if (cd.floating) {
                q.x = r.x + r.w * cd.ax - q.w * cd.px + cd.ox - sx;
                q.y = r.y + r.h * cd.ay - q.h * cd.py + cd.oy - sy;
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
