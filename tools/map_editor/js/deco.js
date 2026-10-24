// Quark map editor: the decoration engine (core). Plain JS, no imports: it runs as a page module (the page loads every
// js/deco*.js in name order: this file first, then deco_rules.js (the generators), then deco_ui.js (the panel)) and
// under node (tools/map_editor/test/deco_test.js requires the three files). Everything hangs off globalThis.MapDeco.
// What it does and the JSON shapes: tools/map_editor/DECO.md.
//
// Decorations are ordinary brushes (no map format change). A rule = generator + params + target + constraints + seed;
// the sidecar entry 'deco' keeps the rules, the named tags (saved selections) and, per rule, the exact boxes it made
// last time, so Remove / Regenerate find them again by exact box + material even after the brushes were reordered.
(function (G) {
'use strict';
const D = G.MapDeco = G.MapDeco || {};
D.FORMAT = 1;                       // the sidecar 'deco' entry's version
const EPS = 1e-4;
const MATS = ['box', 'accent', 'wallbang', 'metal', 'glow', 'far', 'water', 'lava', 'trim', 'clip'];
const isSolid = (m) => m < 5 || m === 9;   // Map::gridSolid: what players collide with (glow IS solid, trim is not)
const isVisual = (m) => m < 5;             // drawn and solid: what hides a face (clip is invisible)
D.MATS = MATS; D.isSolid = isSolid;
D.generators = D.generators || {};

// ---------------------------------------------------------------- small helpers
const r4 = (v) => { const r = Math.round(v * 1e4) / 1e4; return r === 0 ? 0 : r; };
function box(mat, x0, y0, z0, x1, y1, z1) {
    return { mat, min: [r4(Math.min(x0, x1)), r4(Math.min(y0, y1)), r4(Math.min(z0, z1))],
             max: [r4(Math.max(x0, x1)), r4(Math.max(y0, y1)), r4(Math.max(z0, z1))] };
}
function boxLH(mat, lo, hi) { return box(mat, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]); }
const overlap = (a, b, e = EPS) => a.min[0] < b.max[0] - e && a.max[0] > b.min[0] + e && a.min[1] < b.max[1] - e &&
    a.max[1] > b.min[1] + e && a.min[2] < b.max[2] - e && a.max[2] > b.min[2] + e;
const within = (inner, outer, e = 1e-3) => inner.min[0] >= outer.min[0] - e && inner.max[0] <= outer.max[0] + e &&
    inner.min[1] >= outer.min[1] - e && inner.max[1] <= outer.max[1] + e && inner.min[2] >= outer.min[2] - e && inner.max[2] <= outer.max[2] + e;
const near = (a, b, tol) => Math.abs(a - b) <= tol + 1e-6 * Math.abs(a);
function sameBox(a, b, tol = 2e-4) {
    if (a.mat !== b.mat) return false;
    for (let k = 0; k < 3; k++) if (!near(a.min[k], b.min[k], tol) || !near(a.max[k], b.max[k], tol)) return false;
    return true;
}
const keyOf = (b) => b.mat + '|' + b.min.map(r4).join(',') + '|' + b.max.map(r4).join(',');
// a deep copy of JSON data that keeps -0 (JSON.stringify writes it as 0, which would change a map's text on save)
const NEG0 = '\u0001-0\u0001';
const clone = (o) => o === undefined ? undefined : JSON.parse(JSON.stringify(o, (k, v) => (v === 0 && Object.is(v, -0) ? NEG0 : v)), (k, v) => (v === NEG0 ? -0 : v));
function bboxOf(list) {
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (const b of list) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], b.min[k]); hi[k] = Math.max(hi[k], b.max[k]); }
    return { min: lo, max: hi };
}
D.box = box; D.sameBox = sameBox; D.boxKey = keyOf; D.overlap = overlap;

// seeded randomness: FNV-1a over the keys, then mulberry32 (stable on every platform and in every browser)
function hash(...keys) {
    let h = 2166136261 >>> 0;
    const s = keys.join('\u0001');
    for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619) >>> 0; }
    return h >>> 0;
}
function rng(...keys) {
    let a = hash(...keys);
    return function () {
        a = (a + 0x6D2B79F5) >>> 0;
        let t = a;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}
D.rng = rng;

// 1D interval sets: sorted, disjoint [[a, b], ...]
function ivUnion(list) {
    const s = list.filter((r) => r[1] - r[0] > EPS).map((r) => [r[0], r[1]]).sort((p, q) => p[0] - q[0]);
    const out = [];
    for (const r of s) {
        const l = out[out.length - 1];
        if (l && r[0] <= l[1] + EPS) l[1] = Math.max(l[1], r[1]); else out.push(r);
    }
    return out;
}
function ivSub(A, B) {
    let out = A.map((r) => [r[0], r[1]]);
    for (const b of B) {
        const next = [];
        for (const a of out) {
            if (b[1] <= a[0] + EPS || b[0] >= a[1] - EPS) { next.push(a); continue; }
            if (b[0] > a[0] + EPS) next.push([a[0], b[0]]);
            if (b[1] < a[1] - EPS) next.push([b[1], a[1]]);
        }
        out = next;
    }
    return out.filter((r) => r[1] - r[0] > EPS);
}
function ivAnd(A, B) {
    const out = [];
    for (const a of A) for (const b of B) {
        const lo = Math.max(a[0], b[0]), hi = Math.min(a[1], b[1]);
        if (hi - lo > EPS) out.push([lo, hi]);
    }
    return ivUnion(out);
}
const ivEq = (A, B) => A.length === B.length && A.every((a, i) => Math.abs(a[0] - B[i][0]) < EPS && Math.abs(a[1] - B[i][1]) < EPS);
D.iv = { union: ivUnion, sub: ivSub, and: ivAnd };

// ---------------------------------------------------------------- spatial index (uniform grid over x / z)
let STAMP = 1;   // (one counter for every index: the same entry sits in several)
class Index {
    constructor(cell = 4) { this.cell = cell; this.cells = new Map(); this.big = new Set(); }
    _range(b) {
        const c = this.cell;
        return [Math.floor(b.min[0] / c), Math.floor(b.min[2] / c), Math.floor(b.max[0] / c), Math.floor(b.max[2] / c)];
    }
    add(it) {
        const [i0, j0, i1, j1] = this._range(it);
        if ((i1 - i0 + 1) * (j1 - j0 + 1) > 4096) { this.big.add(it); it._big = true; return; }
        for (let i = i0; i <= i1; i++) for (let j = j0; j <= j1; j++) {
            const k = (i + 4096) * 16384 + (j + 4096);
            let l = this.cells.get(k);
            if (!l) this.cells.set(k, l = []);
            l.push(it);
        }
    }
    remove(it) {
        if (it._big) { this.big.delete(it); return; }
        const [i0, j0, i1, j1] = this._range(it);
        for (let i = i0; i <= i1; i++) for (let j = j0; j <= j1; j++) {
            const l = this.cells.get((i + 4096) * 16384 + (j + 4096));
            const p = l ? l.indexOf(it) : -1;
            if (p >= 0) l.splice(p, 1);
        }
    }
    // every item whose box touches lo..hi (closed: callers decide strictness)
    query(lo, hi) {
        const s = ++STAMP, out = [], c = this.cell;
        const i0 = Math.floor(lo[0] / c), j0 = Math.floor(lo[2] / c), i1 = Math.floor(hi[0] / c), j1 = Math.floor(hi[2] / c);
        const test = (it) => {
            if (it._s === s) return;
            it._s = s;
            if (it.min[0] <= hi[0] && it.max[0] >= lo[0] && it.min[1] <= hi[1] && it.max[1] >= lo[1] && it.min[2] <= hi[2] && it.max[2] >= lo[2]) out.push(it);
        };
        if ((i1 - i0 + 1) * (j1 - j0 + 1) > 20000) { for (const l of this.cells.values()) l.forEach(test); }
        else for (let i = i0; i <= i1; i++) for (let j = j0; j <= j1; j++) {
            const l = this.cells.get((i + 4096) * 16384 + (j + 4096));
            if (l) l.forEach(test);
        }
        this.big.forEach(test);
        return out;
    }
}

// ---------------------------------------------------------------- groups (the game's Map::groupBoxes, map.cpp)
function groupBoxes(g) {
    const out = [], mn = g.min, mx = g.max, sz = [mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]];
    const add = (a, c, mat = g.mat) => { if (c[0] - a[0] > 1e-3 && c[1] - a[1] > 1e-3 && c[2] - a[2] > 1e-3) out.push({ mat, min: a, max: c }); };
    if (g.type === 'custom' || !g.type) {
        for (const p of g.parts || []) add([mn[0] + p.min[0] * sz[0], mn[1] + p.min[1] * sz[1], mn[2] + p.min[2] * sz[2]],
                                           [mn[0] + p.max[0] * sz[0], mn[1] + p.max[1] * sz[1], mn[2] + p.max[2] * sz[2]], p.mat);
    } else if (g.type === 'room') {
        const t = Math.min(0.25, Math.min(sz[0], sz[1], sz[2]) * 0.25);
        add(mn.slice(), [mx[0], mn[1] + t, mx[2]]);
        add([mn[0], mx[1] - t, mn[2]], mx.slice());
        const y0 = mn[1] + t, y1 = mx[1] - t, dh = Math.min(2.4, (y1 - y0) * 0.85);
        for (let w = 0; w < 4; w++) {
            const alongX = w < 2, a = alongX ? 0 : 2;
            const s0 = alongX ? mn[0] : mn[2] + t, s1 = alongX ? mx[0] : mx[2] - t;
            const d0 = alongX ? (w === 0 ? mn[2] : mx[2] - t) : (w === 2 ? mn[0] : mx[0] - t), d1 = d0 + t;
            const wall = (u0, v0, u1, v1) => { const p = [0, v0, 0], q = [0, v1, 0]; p[a] = u0; q[a] = u1; p[2 - a] = d0; q[2 - a] = d1; add(p, q); };
            const len = s1 - s0, dw = Math.min(1.6, len - 2 * t - 0.5);
            if (dw < 0.9 || dh < 1.0) { wall(s0, y0, s1, y1); continue; }
            const m = (s0 + s1) * 0.5;
            wall(s0, y0, m - dw * 0.5, y1); wall(m + dw * 0.5, y0, s1, y1); wall(m - dw * 0.5, y0 + dh, m + dw * 0.5, y1);
        }
    } else if (g.type === 'stairs') {
        const a = (g.dir | 0) < 2 ? 0 : 2, neg = (g.dir | 0) & 1;
        const n = Math.min(128, Math.max(1, Math.ceil(sz[1] / 0.5 - 1e-4)));
        const run = sz[a] / n, rise = sz[1] / n;
        for (let k = 0; k < n; k++) {
            const p = mn.slice(), q = mx.slice();
            q[1] = mn[1] + rise * (k + 1);
            if (neg) { q[a] = mx[a] - run * k; p[a] = q[a] - run; } else { p[a] = mn[a] + run * k; q[a] = p[a] + run; }
            add(p, q);
        }
    }
    return out;
}
D.groupBoxes = groupBoxes;

// ---------------------------------------------------------------- the world: every box of the map, indexed
function playerOf(map) {
    const p = map.player || {};
    return { hw: p.halfWidth || 0.4, h: p.height || 1.8, crouch: p.crouchHeight || 1.2, eye: p.eye || 1.6, step: p.stepUp || 0.55, jump: p.jumpHeight || 1.22 };
}
function stairsOf(map) {
    const out = [];
    for (const s of map.stairs || []) out.push({ x0: Math.min(s.x0, s.x1), z0: Math.min(s.z0, s.z1), x1: Math.max(s.x0, s.x1), z1: Math.max(s.z0, s.z1),
        base: Math.min(s.base, s.top), top: Math.max(s.base, s.top), axis: s.axis === 'z' ? 2 : 0, rising: s.rising < 0 ? -1 : 1, group: -1 });
    (map.groups || []).forEach((g, i) => {
        if (g.type !== 'stairs') return;
        out.push({ x0: g.min[0], z0: g.min[2], x1: g.max[0], z1: g.max[2], base: g.min[1], top: g.max[1], axis: (g.dir | 0) < 2 ? 0 : 2,
                   rising: ((g.dir | 0) & 1) ? -1 : 1, group: i });
    });
    return out;
}
function buildWorld(map) {
    const W = { all: [], index: new Index(4), solid: new Index(4) };
    const put = (b, src, i) => {
        const e = { mat: b.mat | 0, min: b.min.slice(), max: b.max.slice(), src, i };
        W.all.push(e); W.index.add(e);
        if (isSolid(e.mat)) W.solid.add(e);
        return e;
    };
    (map.brushes || []).forEach((b, i) => put(b, 'b', i));
    if (Array.isArray(map.generated)) map.generated.forEach((b, i) => put(b, 'g', i));
    else (map.groups || []).forEach((g, gi) => groupBoxes(g).forEach((b) => put(b, 'g', gi)));
    W.keys = new Set(W.all.map(keyOf));
    W.add = (b, rule) => {
        const e = put(b, 'd', -1); e.rule = rule; e.deco = true; W.keys.add(keyOf(e)); return e;
    };
    W.find = (b, pred) => {   // the first entry with exactly this box + material (pred: extra filter)
        const lo = b.min.map((v) => v - 1e-3), hi = b.max.map((v) => v + 1e-3);
        return W.index.query(lo, hi).find((e) => sameBox(e, b) && (!pred || pred(e))) || null;
    };
    return W;
}

// ---------------------------------------------------------------- plane regions: the exposed part of a set of faces
// A face plane: axis (0 x, 1 y, 2 z), sign (+1: the face looks toward +axis), coord. In-plane coordinates u / v:
// side faces: u = the other horizontal axis, v = y; top / bottom faces: u = x, v = z. A region is a list of bands
// (v0..v1, sorted) each with the exposed u-intervals: the faces minus whatever visible solid stands right against them.
function planeUV(axis) { return axis === 1 ? [0, 2] : [axis === 0 ? 2 : 0, 1]; }
function rectsAt(rects, v) { return ivUnion(rects.filter((r) => r[1] < v - 1e-7 && r[3] > v + 1e-7).map((r) => [r[0], r[2]])); }
function bandsOf(pos, neg) {
    const vs = [...new Set([].concat(...pos.map((r) => [r[1], r[3]]), ...neg.map((r) => [r[1], r[3]])))].sort((a, b) => a - b);
    const lo = Math.min(...pos.map((r) => r[1])), hi = Math.max(...pos.map((r) => r[3]));
    const bands = [];
    for (let k = 0; k + 1 < vs.length; k++) {
        const a = vs[k], b = vs[k + 1];
        if (b - a < EPS || b <= lo || a >= hi) continue;
        const m = (a + b) / 2;
        const iv = ivSub(rectsAt(pos, m), rectsAt(neg, m));
        if (!iv.length) continue;
        const l = bands[bands.length - 1];
        if (l && Math.abs(l.v1 - a) < EPS && ivEq(l.iv, iv)) l.v1 = b; else bands.push({ v0: a, v1: b, iv });
    }
    return bands;
}
function makeRegion(W, axis, sign, coord, pos, opts = {}) {
    const [u, v] = planeUV(axis);
    const R = { axis, sign, coord, u, v, pos, key: axis + (sign > 0 ? '+' : '-') + r4(coord) };
    let u0 = Infinity, v0 = Infinity, u1 = -Infinity, v1 = -Infinity;
    for (const r of pos) { u0 = Math.min(u0, r[0]); v0 = Math.min(v0, r[1]); u1 = Math.max(u1, r[2]); v1 = Math.max(v1, r[3]); }
    R.ext = [u0, v0, u1, v1];
    const lo = [0, 0, 0], hi = [0, 0, 0], g = 0.06;
    lo[u] = u0 - g; hi[u] = u1 + g; lo[v] = v0 - g; hi[v] = v1 + g;
    lo[axis] = sign > 0 ? coord : coord - 0.01; hi[axis] = sign > 0 ? coord + 0.01 : coord;
    const coverOk = opts.cover || ((e) => isVisual(e.mat) && !(opts.ignore && opts.ignore.has(e)));
    R.covers = []; R.cont = [];
    const plo = lo.slice(), phi = hi.slice(); plo[axis] = coord - 1e-3; phi[axis] = coord + 1e-3;
    for (const e of W.index.query(plo, phi)) {
        if (!coverOk(e)) continue;
        const rect = [e.min[u], e.min[v], e.max[u], e.max[v]];
        const front = sign > 0 ? (e.min[axis] < coord + 0.01 - EPS && e.max[axis] > coord + EPS) : (e.max[axis] > coord - 0.01 + EPS && e.min[axis] < coord - EPS);
        if (front) R.covers.push(rect);
        else if (Math.abs((sign > 0 ? e.max[axis] : e.min[axis]) - coord) < 1e-3) R.cont.push(rect);   // a coplanar face
    }
    R.bands = bandsOf(pos, R.covers);
    R.world = (pu, pv, d = 0) => { const p = [0, 0, 0]; p[u] = pu; p[v] = pv; p[axis] = coord + sign * d; return p; };
    R.box = (mat, pu0, pu1, pv0, pv1, d0, d1) => {
        const p = [0, 0, 0], q = [0, 0, 0];
        p[u] = pu0; q[u] = pu1; p[v] = pv0; q[v] = pv1; p[axis] = coord + sign * d0; q[axis] = coord + sign * d1;
        return boxLH(mat, p, q);
    };
    return R;
}
// u-intervals where the whole strip v0..v1 is exposed
function alongU(R, v0, v1) {
    let c = v0, res = null;
    for (const b of R.bands) {
        if (b.v1 <= v0 + EPS || b.v0 >= v1 - EPS) continue;
        if (b.v0 > c + EPS) return [];
        res = res ? ivAnd(res, b.iv) : b.iv.slice();
        c = b.v1;
        if (!res.length) return [];
    }
    return c < v1 - EPS || !res ? [] : res;
}
// v-runs where the whole column u0..u1 is exposed
function alongV(R, u0, u1) {
    const runs = [];
    for (const b of R.bands) {
        if (!b.iv.some((r) => r[0] <= u0 + EPS && r[1] >= u1 - EPS)) continue;
        const l = runs[runs.length - 1];
        if (l && Math.abs(l[1] - b.v0) < EPS) l[1] = b.v1; else runs.push([b.v0, b.v1]);
    }
    return runs;
}
// the region's boundary: {o: 'v-'|'v+'|'u-'|'u+' (outward), at, a, b, kind: 'cover' (something stands there: a floor
// under a wall, a wall on a floor, a ceiling) | 'open' (the face just ends: a drop, a wall's top, a corner)}. Seams
// with coplanar faces that aren't in the region ('cont') are left out.
function edgesOf(R) {
    const raw = [], B = R.bands;
    for (let k = 0; k < B.length; k++) {
        const b = B[k], below = k > 0 && Math.abs(B[k - 1].v1 - b.v0) < EPS ? B[k - 1].iv : [];
        const above = k + 1 < B.length && Math.abs(B[k + 1].v0 - b.v1) < EPS ? B[k + 1].iv : [];
        for (const r of ivSub(b.iv, below)) raw.push({ o: 'v-', at: b.v0, a: r[0], b: r[1] });
        for (const r of ivSub(b.iv, above)) raw.push({ o: 'v+', at: b.v1, a: r[0], b: r[1] });
        for (const r of b.iv) { raw.push({ o: 'u-', at: r[0], a: b.v0, b: b.v1 }); raw.push({ o: 'u+', at: r[1], a: b.v0, b: b.v1 }); }
    }
    // merge the u-edges that continue from band to band
    const uE = raw.filter((e) => e.o[0] === 'u').sort((p, q) => (p.o < q.o ? -1 : p.o > q.o ? 1 : p.at - q.at || p.a - q.a));
    const merged = [];
    for (const e of uE) {
        const l = merged[merged.length - 1];
        if (l && l.o === e.o && Math.abs(l.at - e.at) < EPS && Math.abs(l.b - e.a) < EPS) l.b = e.b; else merged.push(Object.assign({}, e));
    }
    const out = [];
    for (const e of raw.filter((x) => x.o[0] === 'v').concat(merged)) {
        const outward = e.o[1] === '+' ? 1 : -1, p = e.at + outward * 0.005, seg = [[e.a, e.b]];
        let C, F;
        if (e.o[0] === 'v') { C = rectsAt(R.covers, p); F = rectsAt(R.cont.concat(R.pos), p); }
        else {   // columns: the v-intervals of the rects that contain u = p
            const col = (rs) => ivUnion(rs.filter((r) => r[0] < p && r[2] > p).map((r) => [r[1], r[3]]));
            C = col(R.covers); F = col(R.cont.concat(R.pos));
        }
        for (const r of ivAnd(seg, C)) out.push(Object.assign({}, e, { a: r[0], b: r[1], kind: 'cover' }));
        for (const r of ivSub(ivSub(seg, C), F)) out.push(Object.assign({}, e, { a: r[0], b: r[1], kind: 'open' }));
    }
    return out.filter((e) => e.b - e.a > 0.01);
}
D.region = { make: makeRegion, alongU, alongV, edges: edgesOf, bands: bandsOf };

// ---------------------------------------------------------------- target resolution
const ROLES = ['any', 'wall', 'floor', 'ceiling', 'block', 'step'];
D.ROLES = ROLES;
function roleOf(ctx, e) {
    if (e._role) return e._role;
    const w = e.max[0] - e.min[0], h = e.max[1] - e.min[1], d = e.max[2] - e.min[2];
    const cx = (e.min[0] + e.max[0]) / 2, cz = (e.min[2] + e.max[2]) / 2;
    const r = {};
    r.step = ctx.stairs.some((s) => e.min[0] >= s.x0 - 0.02 && e.max[0] <= s.x1 + 0.02 && e.min[2] >= s.z0 - 0.02 && e.max[2] <= s.z1 + 0.02 &&
                                    e.max[1] > s.base + EPS && e.max[1] <= s.top + 0.02);
    r.wall = h >= 2.0 && !r.step;
    r.floor = w >= 1.5 && d >= 1.5 && !r.step && ctx.headroom(cx, e.max[1], cz) >= ctx.P.h;   // a walkable top
    r.ceiling = h <= 3 && w >= 1.5 && d >= 1.5 && ctx.clearBelow(cx, e.min[1], cz) >= 2.2;
    r.block = !r.wall && !r.floor && !r.step && h >= 0.5 && w >= 0.6 && d >= 0.6 && Math.max(w, d) <= 12;
    return (e._role = r);
}
function passFilter(ctx, e, f) {
    const w = e.max[0] - e.min[0], h = e.max[1] - e.min[1], d = e.max[2] - e.min[2];
    if (f.mats && f.mats.length) { if (!f.mats.includes(e.mat)) return false; }
    else if (!(e.mat < 5)) return false;   // default: visible solids only (no trim, far, water, lava, clip)
    if (f.role && f.role !== 'any' && !roleOf(ctx, e)[f.role]) return false;
    if (f.minHeight != null && h < f.minHeight - EPS) return false;
    if (f.maxHeight != null && h > f.maxHeight + EPS) return false;
    if (f.minLength != null && Math.max(w, d) < f.minLength - EPS) return false;
    if (f.maxLength != null && Math.max(w, d) > f.maxLength + EPS) return false;
    if (f.minThick != null && Math.min(w, d, h) < f.minThick - EPS) return false;
    if (f.yMin != null && e.max[1] < f.yMin - EPS) return false;
    if (f.yMax != null && e.min[1] > f.yMax + EPS) return false;
    if (f.region && f.region.min && f.region.max && !overlap(e, f.region, -EPS)) return false;
    if (!(f.mats && f.mats.length) && Math.min(w, d, h) < 0.12) return false;   // thin boxes are other decorations
    return true;
}
function refsToEntries(ctx, refs) {
    const out = [], used = new Set();
    for (const r of refs || []) {
        const e = ctx.W.find(r, (x) => !used.has(x) && !x.deco);
        if (e) { used.add(e); out.push(e); } else ctx.rep.warnings.push('target box not found (moved or deleted?): ' + keyOf(r));
    }
    return out;
}
function groupEntries(ctx, gi) {
    const g = (ctx.map.groups || [])[gi];
    if (!g) { ctx.rep.warnings.push('group ' + gi + ' does not exist'); return []; }
    return groupBoxes(g).map((b) => ctx.W.find(b) || Object.assign({ src: 'g', i: gi }, b));
}
function resolveTarget(ctx, t, override) {
    t = override || t || { kind: 'all' };
    let list;
    if (!t.kind && (t.brushes || t.groups)) {    // a live selection: indices
        list = (t.brushes || []).map((i) => ctx.W.all.find((e) => e.src === 'b' && e.i === i)).filter(Boolean);
        for (const gi of t.groups || []) list = list.concat(groupEntries(ctx, gi));
    } else if (t.kind === 'all') list = ctx.W.all.slice();
    else if (t.kind === 'set') {
        list = refsToEntries(ctx, t.brushes);
        for (const gi of t.groups || []) list = list.concat(groupEntries(ctx, gi));
    } else if (t.kind === 'group') list = [].concat(...[].concat(t.index != null ? [t.index] : [], t.indices || []).map((gi) => groupEntries(ctx, gi)));
    else if (t.kind === 'tag') {
        const tag = ctx.deco && ctx.deco.tags && ctx.deco.tags[t.name];
        if (!tag) { ctx.rep.warnings.push('no tag named ' + JSON.stringify(t.name)); list = []; }
        else { list = refsToEntries(ctx, tag.brushes); for (const gi of tag.groups || []) list = list.concat(groupEntries(ctx, gi)); }
    } else if (t.kind === 'filter') list = ctx.W.all.slice();
    else { ctx.rep.warnings.push('unknown target kind ' + t.kind); list = []; }
    const f = t.filter || (t.kind === 'all' || t.kind === 'filter' ? {} : null);
    list = list.filter((e) => !e.deco && !ctx.decoExclude(e));
    if (f) list = list.filter((e) => passFilter(ctx, e, f));
    else list = list.filter((e) => e.mat !== 5 && e.mat !== 6 && e.mat !== 7);
    const seen = new Set();   // canonical order (by box): independent of the brushes' order in the file
    list = list.filter((e) => { const k = keyOf(e); if (seen.has(k)) return false; seen.add(k); return true; });
    return list.sort((p, q) => { for (let k = 0; k < 3; k++) { if (p.min[k] !== q.min[k]) return p.min[k] - q.min[k]; if (p.max[k] !== q.max[k]) return p.max[k] - q.max[k]; } return p.mat - q.mat; });
}
D.resolveTarget = function (map, target, deco) {
    const ctx = makeCtx(map, buildWorld(map), deco, null);
    return resolveTarget(ctx, target).map((e) => ({ src: e.src, i: e.i, mat: e.mat, min: e.min, max: e.max }));
};

// ---------------------------------------------------------------- keep-out volumes
function keepouts(ctx) {
    const map = ctx.map, P = ctx.P, C = ctx.C, K = [];
    const objSolidOnly = C.objectives === 'solid';
    const add = (lo, hi, why, solidOnly) => K.push({ min: lo, max: hi, why, solidOnly: !!solidOnly });
    const c = C.spawnClear;
    for (const s of map.spawns || []) {
        const p = s.pos;
        add([p[0] - P.hw - c, p[1] - 0.05, p[2] - P.hw - c], [p[0] + P.hw + c, p[1] + P.h + c, p[2] + P.hw + c], 'spawn', false);
        // the view from it: the game wants 2 m clear at eye height straight ahead (spawn_facing); keep 2.5 m
        const fx = -Math.sin(s.yaw || 0) * 2.5, fz = -Math.cos(s.yaw || 0) * 2.5, ey = p[1] + P.eye;
        add([Math.min(p[0], p[0] + fx) - 0.35, ey - 0.45, Math.min(p[2], p[2] + fz) - 0.35], [Math.max(p[0], p[0] + fx) + 0.35, ey + 0.45, Math.max(p[2], p[2] + fz) + 0.35], 'spawn view', true);
    }
    const vol = (b, why) => { if (b && b.min && b.max) add(b.min.slice(), b.max.slice(), why, objSolidOnly); };
    (map.sites || []).forEach((b) => vol(b, 'bomb site'));
    if (map.hill) vol(map.hill, 'hill');
    (map.points || []).forEach((b) => vol(b, 'capture point'));
    (map.checkpoints || []).forEach((b) => vol(b, 'checkpoint'));
    const path = map.path || [];
    for (let k = 0; k + 1 < path.length; k++) {   // the payload cart's sweep (prefabs.cart_clear)
        const a = path[k], b = path[k + 1], lo = Math.min(a[1], b[1]) + 0.02, hi = Math.max(a[1], b[1]) + 1.6;
        if (Math.abs(b[0] - a[0]) >= Math.abs(b[2] - a[2])) add([Math.min(a[0], b[0]) - 1.3, lo, Math.min(a[2], b[2]) - 1.0], [Math.max(a[0], b[0]) + 1.3, hi, Math.max(a[2], b[2]) + 1.0], 'payload track', objSolidOnly);
        else add([Math.min(a[0], b[0]) - 1.0, lo, Math.min(a[2], b[2]) - 1.3], [Math.max(a[0], b[0]) + 1.0, hi, Math.max(a[2], b[2]) + 1.3], 'payload track', objSolidOnly);
    }
    // a ladder, the hull's room to climb it, and to step off the top (headroom over the top rung)
    for (const l of map.ladders || []) add([l.min[0] - 1.2, l.min[1], l.min[2] - 1.2], [l.max[0] + 1.2, l.max[1] + P.h + 0.4, l.max[2] + 1.2], 'ladder', true);
    for (const s of ctx.stairs) {
        const lo = [s.x0, s.base + 0.02, s.z0], hi = [s.x1, s.top + P.h, s.z1];
        lo[s.axis] -= 0.6; hi[s.axis] += 0.6;
        add(lo, hi, 'stairs', true);
    }
    for (const o of ctx.openings()) if (o.kind === 'door') {   // doorways and a step in front of and behind them
        const lo = o.box.min.slice(), hi = o.box.max.slice();
        lo[o.thick] -= 1.0; hi[o.thick] += 1.0; hi[1] = Math.max(hi[1], lo[1] + P.h + 0.2);
        add(lo, hi, 'doorway', true);
    }
    for (const b of C.keepOut || []) if (b && b.min && b.max) add(b.min.slice(), b.max.slice(), b.why || 'keep-out box', !!b.solidOnly);
    return K;
}
function keepHit(ctx, b) {
    for (const k of ctx.keep) if ((!k.solidOnly || isSolid(b.mat)) && overlap(b, k, 1e-3)) return k;
    return null;
}
// the parts of lo..hi along `axis` that no keep-out (applying to mat) touches
function freeIntervals(ctx, b, axis) {
    const blocked = [];
    for (const k of ctx.keep) {
        if (k.solidOnly && !isSolid(b.mat)) continue;
        let hit = true;
        for (let a = 0; a < 3; a++) if (a !== axis && !(b.min[a] < k.max[a] - 1e-3 && b.max[a] > k.min[a] + 1e-3)) { hit = false; break; }
        if (hit) blocked.push([k.min[axis], k.max[axis]]);
    }
    return ivSub([[b.min[axis], b.max[axis]]], ivUnion(blocked));
}

// ---------------------------------------------------------------- probes: ground, headroom, play space
function groundBelow(ctx, x, y, z) {   // the highest solid top at or under (x, y, z); -Infinity if none
    let g = -Infinity;
    for (const e of ctx.W.solid.query([x, -1e9, z], [x, y + EPS, z])) {
        if (e.mat === 9) continue;
        if (e.min[0] <= x && e.max[0] >= x && e.min[2] <= z && e.max[2] >= z && e.max[1] <= y + EPS && e.max[1] > g) g = e.max[1];
    }
    return g;
}
function inSolid(ctx, x, y, z) {
    return ctx.W.solid.query([x, y, z], [x, y, z]).some((e) => e.min[0] < x && e.max[0] > x && e.min[1] < y && e.max[1] > y && e.min[2] < z && e.max[2] > z);
}
function headroom(ctx, x, y, z) {   // free height over a surface at y
    let top = Infinity;
    for (const e of ctx.W.solid.query([x, y, z], [x, 1e9, z])) if (e.min[1] >= y - EPS && e.min[1] < top && e.min[0] < x && e.max[0] > x && e.min[2] < z && e.max[2] > z) top = e.min[1];
    return top - y;
}
function clearBelow(ctx, x, y, z) { const g = groundBelow(ctx, x, y - 0.01, z); return g === -Infinity ? -1 : y - g; }
// is (u, v) on region R's face seen from where a player can be? (a point 0.3 m out: not inside a wall, ground under it
// within 30 m with standing room: the outside faces of the outer walls and faces under the floor fail)
function playAt(ctx, R, pu, pv) {
    const p = R.world(pu, pv, 0.3);
    if (R.axis === 1 && R.sign < 0) p[1] -= 0.3; else if (R.axis === 1) p[1] += 0.3;
    if (inSolid(ctx, p[0], p[1], p[2])) return false;
    const g = groundBelow(ctx, p[0], p[1], p[2]);
    if (g === -Infinity || p[1] - g > 30) return false;
    return headroom(ctx, p[0], g, p[2]) >= ctx.P.h * 0.9 || R.axis !== 1;
}
// the parts of u0..u1 (at height v) facing play, sampled every `step` metres
function playRuns(ctx, R, u0, u1, v, step = 1.0) {
    const n = Math.max(1, Math.ceil((u1 - u0) / step)), ok = [];
    for (let i = 0; i < n; i++) ok.push(playAt(ctx, R, u0 + (u1 - u0) * (i + 0.5) / n, v));
    const out = [];
    for (let i = 0; i < n; i++) {
        if (!ok[i]) continue;
        const a = u0 + (u1 - u0) * i / n, b = u0 + (u1 - u0) * (i + 1) / n, l = out[out.length - 1];
        if (l && Math.abs(l[1] - a) < EPS) l[1] = b; else out.push([a, b]);
    }
    return out;
}

// ---------------------------------------------------------------- openings (doors / windows) between pairs of jambs
function findOpenings(ctx) {
    if (ctx.W._openings) return ctx.W._openings;
    const W = ctx.W, out = [], seen = new Set(), P = ctx.P;
    const maxW = 6, minW = 0.5;
    const cands = W.all.filter((e) => isVisual(e.mat) && !e.deco && e.max[1] - e.min[1] >= 0.5);
    for (const E of cands) for (const A of [0, 2]) {
        const B = 2 - A, a = E.max[A];
        const lo = [0, E.min[1], 0], hi = [0, E.max[1], 0];
        lo[A] = a + minW; hi[A] = a + maxW; lo[B] = E.min[B]; hi[B] = E.max[B];
        for (const F of W.index.query(lo, hi)) {
            if (!isVisual(F.mat) || F.deco || F.min[A] < a + minW - EPS || F === E) continue;
            const b = F.min[A];
            const t0 = Math.max(E.min[B], F.min[B]), t1 = Math.min(E.max[B], F.max[B]);
            const y0 = Math.max(E.min[1], F.min[1]), y1 = Math.min(E.max[1], F.max[1]);
            if (t1 - t0 < 0.05 || y1 - y0 < 0.5) continue;
            // what fills the gap between the jambs: each one must span it all (a sill, a floor, a lintel), or no opening
            const glo = [0, y0 - 0.01, 0], ghi = [0, y1 + 0.01, 0];
            glo[A] = a; ghi[A] = b; glo[B] = t0; ghi[B] = t1;
            let ok = true; const fill = [];
            for (const S of W.solid.query(glo, ghi)) {
                if (S.mat === 9 || !(S.min[A] < b - EPS && S.max[A] > a + EPS && S.min[B] < t1 - EPS && S.max[B] > t0 + EPS && S.min[1] < y1 + 1e-3 && S.max[1] > y0 - 1e-3)) continue;
                if (S.min[A] <= a + 1e-3 && S.max[A] >= b - 1e-3 && S.min[B] <= t0 + 1e-3 && S.max[B] >= t1 - 1e-3) fill.push([S.min[1], S.max[1]]);
                else { ok = false; break; }
            }
            if (!ok) continue;
            const filled = ivUnion(fill);
            for (const [f0, f1] of ivSub([[y0, y1]], filled)) {
                const boundedBelow = filled.some((r) => Math.abs(r[1] - f0) < 1e-3), boundedAbove = filled.some((r) => Math.abs(r[0] - f1) < 1e-3);
                if (!boundedBelow || !boundedAbove || f1 - f0 < 0.4) continue;
                const mid = [0, (f0 + f1) / 2, 0]; mid[A] = (a + b) / 2;
                const front = mid.slice(), back = mid.slice(); front[B] = t0 - 0.25; back[B] = t1 + 0.25;
                if (inSolid(ctx, ...front) || inSolid(ctx, ...back)) continue;   // a niche, not an opening
                const gf = groundBelow(ctx, front[0], f0 + 0.1, front[2]), gb = groundBelow(ctx, back[0], f0 + 0.1, back[2]);
                const door = f1 - f0 >= P.h && (Math.abs(gf - f0) <= P.step || Math.abs(gb - f0) <= P.step);
                const lo2 = [0, f0, 0], hi2 = [0, f1, 0]; lo2[A] = a; hi2[A] = b; lo2[B] = t0; hi2[B] = t1;
                const o = { kind: door ? 'door' : 'window', along: A, thick: B, a, b, t0, t1, y0: f0, y1: f1, box: { min: lo2, max: hi2 }, jambs: [E, F] };
                const k = A + ':' + r4(a) + ':' + r4(b) + ':' + r4(t0) + ':' + r4(t1) + ':' + r4(f0);
                if (!seen.has(k)) { seen.add(k); out.push(o); }
            }
        }
    }
    return (W._openings = out);
}

// ---------------------------------------------------------------- the walk test: does a solid decoration cut a route?
// A local walk graph around the new boxes, before and after: standing spots every 0.1 m (the player's hull, 0.8 x 1.8,
// on every solid top in reach; anything no higher than a step-up is stepped onto, so steps and stairs connect), joined
// to their neighbours within a step-up. Every two spots on the edge of the area that were joined inside it before must
// still be joined after, with the new boxes grown by the passage margin (so a passage keeps the player's width plus
// the margin, and headroom plus the margin). Drops and jumps are not modelled (a railing's own check).
function embedded(ctx, b) {
    return ctx.W.solid.query(b.min, b.max).some((e) => !e.deco && e.mat !== 9 && within(b, e, 1e-4));
}
function cutsRoute(ctx, solids) {   // standing, then crouched (a crouch-only passage must stay one too)
    return cutsRouteAt(ctx, solids, ctx.P.h) || cutsRouteAt(ctx, solids, ctx.P.crouch);
}
function cutsRouteAt(ctx, solids, H) {
    const P = ctx.P, m = ctx.C.passMargin, hw = P.hw, st = P.step;
    solids = solids.filter((b) => !embedded(ctx, b));
    if (!solids.length) return false;
    const bb = bboxOf(solids), pad = 2 * hw + m + 0.6, cs = 0.1;
    const x0 = bb.min[0] - pad, z0 = bb.min[2] - pad, x1 = bb.max[0] + pad, z1 = bb.max[2] + pad;
    const nx = Math.ceil((x1 - x0) / cs), nz = Math.ceil((z1 - z0) / cs);
    if (nx * nz > 160000 && solids.length > 1) return solids.some((b) => cutsRouteAt(ctx, [b], H));
    const yLo = bb.min[1] - H - m - 0.05, yHi = bb.max[1] + st + 0.05;
    const N = nx * nz, sup = new Array(N), obs = new Array(N), obsA = new Array(N), supA = new Array(N);
    const raster = (lo0, lo2, hi0, hi2, fn) => {   // the columns whose centre is in [lo, hi] (x / z)
        const i0 = Math.max(0, Math.ceil((lo0 - x0) / cs - 0.5 - 1e-6)), i1 = Math.min(nx - 1, Math.floor((hi0 - x0) / cs - 0.5 + 1e-6));
        const j0 = Math.max(0, Math.ceil((lo2 - z0) / cs - 0.5 - 1e-6)), j1 = Math.min(nz - 1, Math.floor((hi2 - z0) / cs - 0.5 + 1e-6));
        for (let i = i0; i <= i1; i++) for (let j = j0; j <= j1; j++) fn(i * nz + j);
    };
    const push = (arr, c, v) => { (arr[c] || (arr[c] = [])).push(v); };
    const env = ctx.W.solid.query([x0 - hw, yLo - 0.1, z0 - hw], [x1 + hw, yHi + H + 0.1, z1 + hw]);
    for (const e of env) {
        if (e.max[1] >= yLo && e.max[1] <= yHi && e.mat !== 9) raster(e.min[0], e.min[2], e.max[0], e.max[2], (c) => push(sup, c, e.max[1]));
        raster(e.min[0] - hw + 1e-3, e.min[2] - hw + 1e-3, e.max[0] + hw - 1e-3, e.max[2] + hw - 1e-3, (c) => push(obs, c, [e.min[1], e.max[1]]));
    }
    for (const b of solids) {
        if (b.max[1] >= yLo && b.max[1] <= yHi && b.mat !== 9) raster(b.min[0], b.min[2], b.max[0], b.max[2], (c) => push(supA, c, b.max[1]));
        raster(b.min[0] - m - hw + 1e-3, b.min[2] - m - hw + 1e-3, b.max[0] + m + hw - 1e-3, b.max[2] + m + hw - 1e-3, (c) => push(obsA, c, [b.min[1] - m, b.max[1]]));
    }
    const free = (list, y) => !list || list.every((r) => !(r[0] < y + H - 1e-4 && r[1] > y + st + 1e-4));
    // nodes: per column the distinct surfaces; before (base) and after (base + the new boxes' tops)
    const nodeY = [], colStart = new Int32Array(N + 1), okB = [], okA = [];
    for (let c = 0; c < N; c++) {
        colStart[c] = nodeY.length;
        const ys = [...new Set([].concat(sup[c] || [], supA[c] || []).map((y) => Math.round(y * 1000) / 1000))].sort((a, b) => a - b);
        for (const y of ys) {
            const base = (sup[c] || []).some((s) => Math.abs(s - y) < 1e-3);
            nodeY.push(y);
            okB.push(base && free(obs[c], y));
            okA.push(free(obs[c], y) && free(obsA[c], y));
        }
    }
    colStart[N] = nodeY.length;
    const uf = (n) => { const p = new Int32Array(n); for (let i = 0; i < n; i++) p[i] = i; return p; };
    const pb = uf(nodeY.length), pa = uf(nodeY.length);
    const find = (p, x) => { while (p[x] !== x) { p[x] = p[p[x]]; x = p[x]; } return x; };
    const join = (p, a, b) => { a = find(p, a); b = find(p, b); if (a !== b) p[a] = b; };
    for (let i = 0; i < nx; i++) for (let j = 0; j < nz; j++) {
        const c = i * nz + j;
        for (const d of [c + nz, c + 1]) {
            if ((d === c + 1 && j + 1 >= nz) || (d === c + nz && i + 1 >= nx)) continue;
            for (let p = colStart[c]; p < colStart[c + 1]; p++) for (let q = colStart[d]; q < colStart[d + 1]; q++) {
                if (Math.abs(nodeY[p] - nodeY[q]) > st + 1e-4) continue;
                if (okB[p] && okB[q]) join(pb, p, q);
                if (okA[p] && okA[q]) join(pa, p, q);
            }
        }
    }
    const comp = new Map();   // before-component -> after-component of its first edge spot
    const edge = (c) => { for (let p = colStart[c]; p < colStart[c + 1]; p++) {
        if (!okB[p]) continue;
        const rb = find(pb, p), ra = okA[p] ? find(pa, p) : -1 - p;
        if (!comp.has(rb)) comp.set(rb, ra); else if (comp.get(rb) !== ra) return true;
    } return false; };
    for (let i = 0; i < nx; i++) { if (edge(i * nz) || edge(i * nz + nz - 1)) return true; }
    for (let j = 0; j < nz; j++) { if (edge(j) || edge((nx - 1) * nz + j)) return true; }
    return false;
}
D.cutsRoute = (map, solids, constraints) => cutsRoute(makeCtx(map, buildWorld(map), null, null, constraints), solids);

// ---------------------------------------------------------------- the run context and emitting units
const DEFAULT_C = { symmetry: 'auto', maxCount: 0, spawnClear: 0.75, objectives: 'all', passMargin: 0.1, allowBlockDrops: false, keepOut: [] };
D.DEFAULT_CONSTRAINTS = DEFAULT_C;
function mirrorBox(b, sym) {
    if (sym === 'point') return box(b.mat, -b.max[0], b.min[1], -b.max[2], -b.min[0], b.max[1], -b.min[2]);
    if (sym === 'mirrorX') return box(b.mat, -b.max[0], b.min[1], b.min[2], -b.min[0], b.max[1], b.max[2]);
    if (sym === 'mirrorZ') return box(b.mat, b.min[0], b.min[1], -b.max[2], b.max[0], b.max[1], -b.min[2]);
    return null;
}
// the map's own symmetry, for symmetry 'auto': point (x, z) -> (-x, -z) for ranked maps (the game checks it) and for
// maps whose solids are 90 % point / x / z mirrored (the shipped team maps), else none
function detectSymmetry(map, W) {
    if (W._sym) return W._sym;
    if (Array.isArray(map.ranked) && map.ranked.length) return (W._sym = 'point');
    const sol = W.all.filter((e) => e.mat < 5 && e.src === 'b' && !e.deco);
    const keys = new Set(sol.map(keyOf));
    let best = 'none', bestF = 0.9;
    for (const s of ['point', 'mirrorX', 'mirrorZ']) {
        const f = sol.length ? sol.filter((e) => keys.has(keyOf(mirrorBox(e, s)))).length / sol.length : 0;
        if (f >= bestF) { best = s; bestF = f; }
    }
    return (W._sym = best);
}
D.detectSymmetry = (map) => detectSymmetry(map, buildWorld(map));
function makeCtx(map, W, deco, rule, cons) {
    const ctx = { map, W, deco, P: playerOf(map), stairs: stairsOf(map), C: Object.assign({}, DEFAULT_C, cons || (rule && rule.constraints) || {}) };
    ctx.rep = { warnings: [] };
    ctx.decoExclude = () => false;
    ctx.headroom = (x, y, z) => headroom(ctx, x, y, z);
    ctx.clearBelow = (x, y, z) => clearBelow(ctx, x, y, z);
    ctx.groundBelow = (x, y, z) => groundBelow(ctx, x, y, z);
    ctx.inSolid = (x, y, z) => inSolid(ctx, x, y, z);
    ctx.openings = () => findOpenings(ctx);
    ctx.playAt = (R, u, v) => playAt(ctx, R, u, v);
    ctx.playRuns = (R, u0, u1, v, s) => playRuns(ctx, R, u0, u1, v, s);
    ctx.freeIntervals = (b, axis) => freeIntervals(ctx, b, axis);
    ctx.alongU = alongU; ctx.alongV = alongV; ctx.edges = edgesOf;
    ctx.box = box; ctx.iv = D.iv; ctx.isSolid = isSolid; ctx.mirrorBox = mirrorBox;
    return ctx;
}
// regions of the targets' faces: which = 'side' | 'top' | 'bottom'; one region per plane (coplanar faces merged)
function regionsOf(ctx, T, which) {
    const planes = new Map();
    const addFace = (axis, sign, coord, rect) => {
        const k = axis + (sign > 0 ? '+' : '-') + r4(coord);
        let p = planes.get(k);
        if (!p) planes.set(k, p = { axis, sign, coord, rects: [] });
        p.rects.push(rect);
    };
    for (const e of T) {
        if (which === 'side') {
            for (const axis of [0, 2]) {
                const u = axis === 0 ? 2 : 0, rect = [e.min[u], e.min[1], e.max[u], e.max[1]];
                addFace(axis, 1, e.max[axis], rect); addFace(axis, -1, e.min[axis], rect);
            }
        } else if (which === 'top') addFace(1, 1, e.max[1], [e.min[0], e.min[2], e.max[0], e.max[2]]);
        else addFace(1, -1, e.min[1], [e.min[0], e.min[2], e.max[0], e.max[2]]);
    }
    const out = [];
    for (const k of [...planes.keys()].sort()) {
        const p = planes.get(k);
        const R = makeRegion(ctx.W, p.axis, p.sign, p.coord, p.rects);
        if (R.bands.length) out.push(R);
    }
    return out;
}
function emit(ctx, unit) {
    const S = ctx.state, rep = S.rep;
    let boxes = (unit.boxes || []).map((b) => box(b.mat | 0, b.min[0], b.min[1], b.min[2], b.max[0], b.max[1], b.max[2]))
        .filter((b) => b.max[0] - b.min[0] > 0.004 && b.max[1] - b.min[1] > 0.004 && b.max[2] - b.min[2] > 0.004);
    if (!boxes.length) return false;
    if (unit.split && boxes.length === 1) {   // a strip: cut out the keep-outs and emit the pieces
        const b = boxes[0], A = unit.axis != null ? unit.axis : (b.max[0] - b.min[0] >= b.max[2] - b.min[2] ? 0 : 2);
        const parts = freeIntervals(ctx, b, A).filter((r) => r[1] - r[0] >= (unit.minLength || 0.2));
        if (!(parts.length === 1 && Math.abs(parts[0][0] - b.min[A]) < 1e-3 && Math.abs(parts[0][1] - b.max[A]) < 1e-3)) {
            if (parts.length === 0) { const k = keepHit(ctx, b); reject(ctx, 'keepout:' + (k ? k.why : 'too short between keep-outs'), b); return false; }
            let any = false;
            for (const r of parts) {
                const lo = b.min.slice(), hi = b.max.slice(); lo[A] = r[0]; hi[A] = r[1];
                any = emit(ctx, { boxes: [boxLH(b.mat, lo, hi)] }) || any;
            }
            return any;
        }
    }
    const variants = [boxes];
    const sym = ctx.C.symmetry;
    if (sym && sym !== 'none') {
        const mb = boxes.map((b) => mirrorBox(b, sym));
        if (!mb.every((b, i) => keyOf(b) === keyOf(boxes[i]))) variants.push(mb);
    }
    const added = [];
    const undo = () => { for (const e of added) { ctx.W.index.remove(e); ctx.W.solid.remove(e); ctx.W.keys.delete(keyOf(e)); S.out.pop(); } };
    let n = 0;
    for (let vi = 0; vi < variants.length; vi++) {
        const bs = variants[vi].filter((b) => !ctx.W.keys.has(keyOf(b)));   // exact duplicates (of anything) are dropped
        if (!bs.length) { if (vi === 0 && variants.length === 1) { rep.rejected.dup = (rep.rejected.dup || 0) + 1; return false; } continue; }
        let why = null, at = bs[0];
        for (const b of bs) {
            if (b.min.some((v) => Math.abs(v) > 4096) || b.max.some((v) => Math.abs(v) > 4096)) { why = 'outside the map extent'; at = b; break; }
            const k = keepHit(ctx, b);
            if (k) { why = 'keepout:' + k.why; at = b; break; }
        }
        const solids = bs.filter((b) => isSolid(b.mat));
        if (!why && solids.length && cutsRoute(ctx, solids)) why = 'blocks a passage';
        if (!why && unit.blocksDrop && solids.length && !ctx.C.allowBlockDrops) why = 'blocks a drop';
        if (!why && ctx.C.maxCount > 0 && S.units + n >= ctx.C.maxCount) why = 'maxCount';
        if (why) { undo(); reject(ctx, why, at); return false; }
        for (const b of bs) { added.push(ctx.W.add(b, S.ruleId)); S.out.push(b); }
        n++;
    }
    S.units += n;
    return n > 0;
}
function reject(ctx, why, b) {
    const rep = ctx.state.rep, k = why.startsWith('keepout:') ? why : why;
    rep.rejected[k] = (rep.rejected[k] || 0) + 1;
    if (rep.samples.length < 40 && b) rep.samples.push({ why, at: [r4((b.min[0] + b.max[0]) / 2), r4((b.min[1] + b.max[1]) / 2), r4((b.min[2] + b.max[2]) / 2)] });
}

// ---------------------------------------------------------------- rules
let RULE_N = 0;
D.newRule = function (gen, extra) {
    const G2 = D.generators[gen];
    if (!G2) throw new Error('unknown generator ' + gen);
    const params = {};
    for (const p of G2.params) params[p.key] = clone(p.default);
    return Object.assign({ id: 'r' + Date.now().toString(36) + (++RULE_N).toString(36) + Math.floor(Math.random() * 1296).toString(36), name: G2.label, gen, enabled: true,
                           params, target: clone(G2.target || { kind: 'all' }), constraints: Object.assign({}, DEFAULT_C, G2.constraints || {}), seed: 1, generated: [] }, extra || {});
};
// a rule from a preset entry ({name, gen, params, target, constraints, seed}: anything left out takes the default)
D.ruleFromPreset = function (p) {
    const r = D.newRule(p.gen);
    if (p.name) r.name = p.name;
    if (p.params) Object.assign(r.params, clone(p.params));
    if (p.target) r.target = clone(p.target);
    if (p.constraints) Object.assign(r.constraints, clone(p.constraints));
    if (p.seed != null) r.seed = p.seed | 0;
    if (p.enabled === false) r.enabled = false;
    return r;
};
// a rule as a preset entry (no id, no record of what it made)
D.ruleToPreset = (r) => ({ name: r.name, gen: r.gen, params: clone(r.params), target: clone(r.target), constraints: clone(r.constraints), seed: r.seed });
function normParams(rule) {
    const G2 = D.generators[rule.gen], P = {};
    for (const p of G2.params) {
        let v = rule.params && rule.params[p.key] !== undefined ? rule.params[p.key] : clone(p.default);
        if (p.type === 'number' || p.type === 'mat') { v = Number(v); if (!isFinite(v)) v = p.default; if (p.min != null) v = Math.max(p.min, v); if (p.max != null) v = Math.min(p.max, v); }
        if (p.type === 'mat') v = Math.round(v);
        if (p.type === 'bool') v = !!v;
        if (p.type === 'select' && !p.options.some((o) => (o.value !== undefined ? o.value : o) === v)) v = p.default;
        P[p.key] = v;
    }
    return P;
}
function runRule(ctx, rule, override) {
    const G2 = D.generators[rule.gen];
    const rep = { id: rule.id, name: rule.name, gen: rule.gen, targets: 0, units: 0, boxes: 0, rejected: {}, samples: [], warnings: [], ms: 0 };
    if (!G2) { rep.warnings.push('unknown generator ' + rule.gen); return { boxes: [], rep }; }
    const t0 = Date.now();
    ctx.C = Object.assign({}, DEFAULT_C, rule.constraints || {});
    if (ctx.C.symmetry === 'auto' || !ctx.C.symmetry) ctx.C.symmetry = detectSymmetry(ctx.map, ctx.W);
    rep.symmetry = ctx.C.symmetry;
    ctx.keep = keepouts(ctx).filter((k) => !(G2.ignoreKeep || []).includes(k.why));
    ctx.rep = rep;
    ctx.state = { rep, out: [], units: 0, ruleId: rule.id };
    const T = resolveTarget(ctx, rule.target, override);
    rep.targets = T.length;
    const P = normParams(rule);
    ctx.rng = (...keys) => rng(rule.seed | 0, rule.gen, ...keys);
    ctx.regions = (which) => regionsOf(ctx, T, which);
    ctx.emit = (unit) => emit(ctx, unit);
    try { G2.run(ctx, P, T); } catch (err) { rep.warnings.push('generator failed: ' + (err && err.message || err)); }
    rep.units = ctx.state.units; rep.boxes = ctx.state.out.length; rep.ms = Date.now() - t0;
    return { boxes: ctx.state.out, rep };
}

// generate(map, rules, targets, opts) -> {brushes, perRule: {id: [boxes]}, report}
//   rules: a rule or a list (run in order; each sees the boxes the earlier ones made). targets (optional): a target for
//   every rule instead of its own (a target object, or a live selection {brushes: [i], groups: [i]}).
//   opts.deco: the sidecar entry (for tags, and so that boxes any rule made are never targets);
//   opts.exclude: boxes to treat as gone (e.g. the old output of the rules being regenerated).
D.generate = function (map, rules, targets, opts = {}) {
    rules = Array.isArray(rules) ? rules : [rules];
    let m = map;
    if (opts.exclude && opts.exclude.length) m = Object.assign({}, map, { brushes: removeBoxes(map.brushes || [], opts.exclude).kept });
    const W = buildWorld(m);
    const deco = opts.deco || null;
    const ctx = makeCtx(m, W, deco, null);
    const decoBoxes = [];
    for (const r of (deco && deco.rules) || []) for (const b of r.generated || []) decoBoxes.push(b);
    for (const r of rules) for (const b of r.generated || []) decoBoxes.push(b);
    const decoSet = new Set();
    for (const b of decoBoxes) { const e = W.find(b, (x) => x.src === 'b' && !decoSet.has(x)); if (e) decoSet.add(e); }
    ctx.decoExclude = (e) => decoSet.has(e);
    const t0 = Date.now(), all = [], perRule = {}, reps = [];
    for (const r of rules) {
        if (r.enabled === false && !opts.includeDisabled) continue;
        const { boxes, rep } = runRule(ctx, r, targets || null);
        perRule[r.id] = boxes; reps.push(rep); all.push(...boxes);
    }
    const total = (map.brushes || []).length + ((map.generated || []).length) + all.length;
    const report = { rules: reps, boxes: all.length, ms: Date.now() - t0, warnings: [] };
    if (total > 60000) report.warnings.push('the map would have ' + total + ' brushes: over the game\'s limit of 60000');
    return { brushes: all, perRule, report };
};

// removeBoxes(brushes, boxes) -> {kept, removed, missing}: each box takes out one brush with exactly that box + material
function removeBoxes(brushes, boxes) {
    const idx = new Map();
    brushes.forEach((b, i) => { const k = keyOf(b); (idx.get(k) || idx.set(k, []).get(k)).push(i); });
    const gone = new Set(), missing = [];
    for (const b of boxes) {
        const l = idx.get(keyOf(b));
        let hit = l && l.find((i) => !gone.has(i));
        if (hit === undefined) {   // rounding: fall back to a tolerant scan
            hit = brushes.findIndex((x, i) => !gone.has(i) && sameBox(x, b));
            if (hit < 0) { missing.push(b); continue; }
        }
        gone.add(hit);
    }
    return { kept: brushes.filter((_, i) => !gone.has(i)), removed: brushes.filter((_, i) => gone.has(i)), missing };
}
D.removeBoxes = removeBoxes;

// ---------------------------------------------------------------- the sidecar entry and the high-level operations
D.emptyDeco = () => ({ version: D.FORMAT, rules: [], tags: {} });
D.normDeco = function (d) {
    d = d && typeof d === 'object' ? clone(d) : D.emptyDeco();
    d.version = D.FORMAT; d.rules = Array.isArray(d.rules) ? d.rules : []; d.tags = d.tags && typeof d.tags === 'object' ? d.tags : {};
    for (const r of d.rules) { r.generated = Array.isArray(r.generated) ? r.generated : []; if (r.enabled === undefined) r.enabled = true; }
    return d;
};
function pick(deco, ids) {
    if (ids == null) return deco.rules.filter((r) => r.enabled !== false);
    const s = new Set([].concat(ids));
    return deco.rules.filter((r) => s.has(r.id));
}
// apply(map, deco, ruleIds?) -> {map, deco, report, added, removed, missing}: takes out what those rules made last time
// (exact box + material), runs them again in the sidecar's order, adds the new boxes at the end of map.brushes and
// records them. ruleIds left out: every enabled rule (Regenerate all).
D.apply = function (map, deco, ruleIds, targets) {
    deco = D.normDeco(deco);
    const run = pick(deco, ruleIds), old = [].concat(...run.map((r) => r.generated));
    const rm = removeBoxes(map.brushes || [], old);
    const m = Object.assign({}, map, { brushes: rm.kept });
    const others = { rules: deco.rules.filter((r) => !run.includes(r)), tags: deco.tags };
    const g = D.generate(m, run.map((r) => Object.assign({}, r, { generated: [] })), targets, { deco: others });
    const out = clone(map);
    out.brushes = rm.kept.map(clone).concat(g.brushes.map(clone));
    for (const r of run) {
        r.generated = (g.perRule[r.id] || []).map(clone);
        const rep = g.report.rules.find((x) => x.id === r.id);
        r.last = rep ? { units: rep.units, boxes: rep.boxes, rejected: rep.rejected } : null;
    }
    return { map: out, deco, report: g.report, added: g.brushes, removed: rm.removed, missing: rm.missing };
};
// remove(map, deco, ruleIds?) -> {map, deco, removed, missing}: takes out what those rules made (they stay in the sidecar)
D.remove = function (map, deco, ruleIds) {
    deco = D.normDeco(deco);
    const run = ruleIds == null ? deco.rules : pick(deco, ruleIds);
    const rm = removeBoxes(map.brushes || [], [].concat(...run.map((r) => r.generated)));
    for (const r of run) { r.generated = []; r.last = null; }
    const out = clone(map); out.brushes = rm.kept.map(clone);
    return { map: out, deco, removed: rm.removed, missing: rm.missing };
};
D.regenerateAll = (map, deco) => D.apply(map, deco, null);
// preview(map, deco, rules) -> {add, remove, report}: what apply would do, without changing anything. rules: rule
// objects (an unsaved draft is fine: it replaces the sidecar's rule with the same id) or ids.
D.preview = function (map, deco, rules, targets) {
    deco = D.normDeco(deco);
    const list = [].concat(rules == null ? pick(deco, null) : rules).map((r) => typeof r === 'string' ? deco.rules.find((x) => x.id === r) : r).filter(Boolean);
    const ids = new Set(list.map((r) => r.id));
    const stored = deco.rules.filter((r) => ids.has(r.id));
    const old = [].concat(...stored.map((r) => r.generated));
    const rm = removeBoxes(map.brushes || [], old);
    const m = Object.assign({}, map, { brushes: rm.kept });
    const others = { rules: deco.rules.filter((r) => !ids.has(r.id)), tags: deco.tags };
    const ordered = deco.rules.filter((r) => ids.has(r.id)).map((r) => list.find((x) => x.id === r.id)).concat(list.filter((r) => !deco.rules.some((x) => x.id === r.id)));
    const g = D.generate(m, ordered.map((r) => Object.assign({}, r, { generated: [], enabled: true })), targets, { deco: others });
    return { add: g.brushes, remove: rm.removed, report: g.report };
};
// a tag (saved selection) from a live selection: the boxes themselves (found again by box + material) and group indices
D.tagFromSelection = function (map, sel) {
    return { brushes: (sel.brushes || []).map((i) => map.brushes[i]).filter(Boolean).map((b) => ({ mat: b.mat, min: b.min.slice(), max: b.max.slice() })),
             groups: (sel.groups || []).slice() };
};
// the live selection a tag / target stands for now: {brushes: [i], groups: [i]}
D.selectionOf = function (map, deco, target) {
    deco = D.normDeco(deco);
    const ctx = makeCtx(map, buildWorld(map), deco, null);
    const list = resolveTarget(ctx, target);
    return { brushes: [...new Set(list.filter((e) => e.src === 'b').map((e) => e.i))].sort((a, b) => a - b),
             groups: [...new Set([].concat(target.groups || [], target.kind === 'group' ? [target.index] : [], target.kind === 'tag' && deco.tags[target.name] ? deco.tags[target.name].groups || [] : []))].filter((x) => x != null) };
};
D.decoIndices = function (map, deco) {   // indices of the brushes the sidecar's rules made (to draw / select them)
    deco = D.normDeco(deco);
    const idx = new Map();
    (map.brushes || []).forEach((b, i) => { const k = keyOf(b); (idx.get(k) || idx.set(k, []).get(k)).push(i); });
    const out = {}, used = new Set();
    for (const r of deco.rules) out[r.id] = r.generated.map((b) => { const l = idx.get(keyOf(b)); const i = l && l.find((j) => !used.has(j)); if (i !== undefined) used.add(i); return i; }).filter((i) => i !== undefined);
    return out;
};
D._internal = { buildWorld, makeCtx, keepouts, cutsRoute, findOpenings, regionsOf, resolveTarget, playerOf, stairsOf };

if (typeof module !== 'undefined' && module.exports) module.exports = D;
})(typeof globalThis !== 'undefined' ? globalThis : window);
