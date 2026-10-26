// Quark map editor: the decoration generators (loaded after deco.js). Each one: {label, help, target (the default
// target of a new rule), params: [form fields], run(ctx, P, T)}: T = the target boxes (canonical order), P = the
// rule's params (checked against the fields), ctx = the engine's helpers (deco.js makeCtx / runRule). A generator
// proposes units (a few boxes that go together) with ctx.emit(unit); the engine applies the constraints (keep-outs,
// the walk test for solid boxes, symmetry, max count, exact duplicates) and keeps or drops the whole unit.
// The decoration styles follow the shipped maps (tools/maps/prefabs.py: trim / dress / detail_pass / props / pillar /
// catwalk / crates / light / dais): flush trim bands and pilasters, glow lines, corner columns, floor seams, lamps.
(function (G) {
'use strict';
const D = G.MapDeco = G.MapDeco || {};
const GEN = D.generators = D.generators || {};
const MAT_OPTS = ['box', 'accent', 'wallbang', 'metal', 'glow', 'far', 'water', 'lava', 'trim', 'clip', 'nograpple'].map((n, i) => ({ value: i, label: i + ' ' + n }));
const num = (key, label, def, min, max, step, help) => ({ key, label, type: 'number', default: def, min, max, step: step || 0.05, help });
const mat = (key, label, def, help) => ({ key, label, type: 'mat', default: def, options: MAT_OPTS, help });
const sel = (key, label, def, options, help) => ({ key, label, type: 'select', default: def, options, help });
const bool = (key, label, def, help) => ({ key, label, type: 'bool', default: def, help });
const facing = () => sel('facing', 'Faces', 'play', [{ value: 'play', label: 'facing play' }, { value: 'all', label: 'all' }],
    'facing play: skip faces nobody sees (the outside of the outer walls, faces under the floor)');
const WALLS = { kind: 'filter', filter: { role: 'wall' } };

// the floor level of a side region: the lowest place a floor meets it (where a player could see it), else its bottom
function floorLevel(ctx, R) {
    let lv = Infinity;
    for (const e of ctx.edges(R)) if (e.o === 'v-' && e.kind === 'cover' && e.at < lv && ctx.playAt(R, (e.a + e.b) / 2, e.at + 0.9)) lv = e.at;
    return lv === Infinity ? R.bands[0].v0 : lv;
}
const regionTop = (R) => R.bands[R.bands.length - 1].v1;
// evenly spaced positions inside u0..u1 like prefabs.detail_pass: n = floor(len / spacing), none at the ends
function spaced(u0, u1, spacing) {
    const L = u1 - u0, n = Math.floor(L / spacing), out = [];
    for (let k = 1; k <= n; k++) out.push(u0 + L * k / (n + 1));
    return out;
}
// centred lines across a0..a1, `spacing` apart (at least one)
function lines(a0, a1, spacing) {
    if (a1 < a0) return [];
    const n = Math.max(1, Math.floor((a1 - a0) / spacing) + 1), mid = (a0 + a1) / 2, out = [];
    for (let k = 0; k < n; k++) out.push(mid + (k - (n - 1) / 2) * spacing);
    return out;
}

// ---------------------------------------------------------------- edge trim
GEN.edgeTrim = {
    label: 'Edge trim', target: WALLS,
    help: 'A band along the bottom and / or top edges of the faces: skirting where a wall meets the floor, a cornice under a ceiling or along a wall top (prefabs.trim / detail_pass).',
    params: [
        sel('edges', 'Edges', 'bottom', ['bottom', 'top', 'both']),
        sel('at', 'Where', 'any', [{ value: 'any', label: 'any edge' }, { value: 'cover', label: 'where a floor / ceiling meets it' }, { value: 'open', label: 'free edges (wall tops, lintels)' }]),
        num('height', 'Band height (m)', 0.22, 0.02, 2), num('depth', 'Sticks out (m)', 0.04, 0, 0.5, 0.01),
        num('sink', 'Sunk into the face (m)', 0, 0, 0.5, 0.01), mat('mat', 'Material', 8), num('minLength', 'Shortest piece (m)', 0.5, 0, 50), facing(),
    ],
    run(ctx, P) {
        for (const R of ctx.regions('side')) for (const e of ctx.edges(R)) {
            if (e.o[0] !== 'v') continue;
            const bottom = e.o === 'v-';
            if ((P.edges === 'bottom' && !bottom) || (P.edges === 'top' && bottom) || (P.at !== 'any' && e.kind !== P.at)) continue;
            const v0 = bottom ? e.at : e.at - P.height, v1 = bottom ? e.at + P.height : e.at;
            for (const f of ctx.iv.and([[e.a, e.b]], ctx.alongU(R, v0, v1))) {
                const runs = P.facing === 'play' ? ctx.playRuns(R, f[0], f[1], (v0 + v1) / 2) : [f];
                for (const r of runs) if (r[1] - r[0] >= P.minLength)
                    ctx.emit({ boxes: [R.box(P.mat, r[0], r[1], v0, v1, -P.sink, P.depth)], split: true, axis: R.u, minLength: P.minLength });
            }
        }
    },
};

// ---------------------------------------------------------------- horizontal bands at set heights
GEN.bands = {
    label: 'Wall bands', target: WALLS,
    help: 'Horizontal bands at set heights over the floor: a chair rail at 1.1 m, a glow line at 0.25 m (prefabs.dress).',
    params: [
        { key: 'heights', label: 'Heights (m, comma separated)', type: 'text', default: '1.1' },
        sel('base', 'Heights from', 'floor', [{ value: 'floor', label: 'the floor in front' }, { value: 'absolute', label: 'y = 0 (absolute)' }]),
        num('height', 'Band height (m)', 0.12, 0.02, 2), num('depth', 'Sticks out (m)', 0.04, 0, 0.5, 0.01), num('sink', 'Sunk into the face (m)', 0, 0, 0.5, 0.01),
        mat('mat', 'Material', 8), num('minLength', 'Shortest piece (m)', 0.5, 0, 50), facing(),
    ],
    run(ctx, P) {
        const hs = String(P.heights).split(/[ ,;]+/).map(Number).filter((v) => isFinite(v));
        for (const R of ctx.regions('side')) {
            const base = P.base === 'floor' ? floorLevel(ctx, R) : 0;
            for (const h of hs) {
                const v0 = base + h, v1 = v0 + P.height;
                for (const f of ctx.alongU(R, v0, v1)) {
                    const runs = P.facing === 'play' ? ctx.playRuns(R, f[0], f[1], (v0 + v1) / 2) : [f];
                    for (const r of runs) if (r[1] - r[0] >= P.minLength)
                        ctx.emit({ boxes: [R.box(P.mat, r[0], r[1], v0, v1, -P.sink, P.depth)], split: true, axis: R.u, minLength: P.minLength });
                }
            }
        }
    },
};

// ---------------------------------------------------------------- vertical strips / inset panels at spacing
GEN.wallStrips = {
    label: 'Wall strips / panels', target: WALLS,
    help: 'Vertical strips (pilasters) every few metres up each face, or inset panels between them (prefabs.detail_pass, dress bays).',
    params: [
        sel('style', 'Style', 'strip', ['strip', 'panel', 'both']), num('spacing', 'Spacing (m)', 4, 0.5, 50, 0.25),
        num('width', 'Strip width (m)', 0.3, 0.02, 4), num('depth', 'Sticks out (m)', 0.04, 0, 0.5, 0.01),
        num('bottom', 'Strip: gap at the bottom (m)', 0.3, 0, 10), num('top', 'Strip: gap at the top (m)', 0.4, 0, 10), num('minHeight', 'Strip: shortest (m)', 1.2, 0.1, 50),
        num('panelBottom', 'Panel: from (m over the floor)', 1.6, 0, 50), num('panelTop', 'Panel: to (m over the floor)', 5, 0.2, 60), num('gap', 'Panel: gap to the strips (m)', 0.5, 0, 10),
        mat('mat', 'Material', 8), facing(),
    ],
    run(ctx, P) {
        for (const R of ctx.regions('side')) {
            const [U0, , U1] = R.ext, cs = spaced(U0, U1, P.spacing);
            if (P.style !== 'panel') for (const c of cs) {
                for (const [a, b] of ctx.alongV(R, c - P.width / 2, c + P.width / 2)) {
                    const y0 = a + P.bottom, y1 = b - P.top;
                    if (y1 - y0 < P.minHeight || (P.facing === 'play' && !ctx.playAt(R, c, (y0 + y1) / 2))) continue;
                    ctx.emit({ boxes: [R.box(P.mat, c - P.width / 2, c + P.width / 2, y0, y1, 0, P.depth)] });
                }
            }
            if (P.style !== 'strip') {
                const fl = floorLevel(ctx, R), v0 = fl + P.panelBottom, v1 = Math.min(fl + P.panelTop, regionTop(R) - P.top);
                if (v1 - v0 < 0.5) continue;
                const bs = [U0].concat(cs, [U1]), half = P.style === 'both' ? P.width / 2 : 0;
                for (let k = 0; k + 1 < bs.length; k++) {
                    const lo = bs[k] + (k > 0 ? half : 0) + P.gap, hi = bs[k + 1] - (k + 2 < bs.length ? half : 0) - P.gap;
                    for (const f of ctx.iv.and([[lo, hi]], ctx.alongU(R, v0, v1))) {
                        if (f[1] - f[0] < 1.0 || (P.facing === 'play' && !ctx.playAt(R, (f[0] + f[1]) / 2, (v0 + v1) / 2))) continue;
                        ctx.emit({ boxes: [R.box(P.mat, f[0], f[1], v0, v1, 0, P.depth)] });
                    }
                }
            }
        }
    },
};

// ---------------------------------------------------------------- pillars / corner columns
GEN.pillars = {
    label: 'Pillars / corner columns', target: WALLS,
    help: 'Columns round the outside corners of walls and blocks, in the inside corners of rooms (prefabs.dress corner columns), or engaged pilasters along the faces; with a base and a capital (prefabs.pillar). Solid ones are walk-tested.',
    params: [
        sel('placement', 'Where', 'inner', [{ value: 'inner', label: 'inside corners' }, { value: 'outer', label: 'outside corners' }, { value: 'spacing', label: 'along the faces' }]),
        num('size', 'Column size (m)', 0.5, 0.05, 4), num('depth', 'Along the faces: sticks out (m)', 0.25, 0.02, 4),
        num('spacing', 'Along the faces: spacing (m)', 6, 1, 60, 0.5), num('minHeight', 'Shortest (m)', 2, 0.2, 60),
        bool('base', 'Base and capital', true), num('flare', 'Base / capital: wider by (m)', 0.1, 0, 1, 0.01), num('capHeight', 'Base / capital height (m)', 0.3, 0.05, 2),
        mat('mat', 'Material', 1), facing(),
    ],
    run(ctx, P) {
        const s = P.size;
        const unit = (b, R) => {
            const out = [b];
            if (P.base && b.max[1] - b.min[1] > 2 * P.capHeight + 0.2) {
                const f = P.flare, lo = b.min, hi = b.max;
                out.push(ctx.box(P.mat, lo[0] - f, lo[1], lo[2] - f, hi[0] + f, lo[1] + P.capHeight, hi[2] + f));
                out.push(ctx.box(P.mat, lo[0] - f, hi[1] - P.capHeight, lo[2] - f, hi[0] + f, hi[1], hi[2] + f));
            }
            ctx.emit({ boxes: out });
        };
        for (const R of ctx.regions('side')) {
            if (P.placement === 'spacing') {
                for (const c of spaced(R.ext[0], R.ext[2], P.spacing)) for (const [a, b] of ctx.alongV(R, c - s / 2, c + s / 2)) {
                    if (b - a < P.minHeight || (P.facing === 'play' && !ctx.playAt(R, c, (a + b) / 2))) continue;
                    unit(R.box(P.mat, c - s / 2, c + s / 2, a, b, 0, P.depth), R);
                }
                continue;
            }
            for (const e of ctx.edges(R)) {
                if (e.o[0] !== 'u' || e.b - e.a < P.minHeight) continue;
                const out = e.o[1] === '+' ? 1 : -1, mid = (e.a + e.b) / 2;
                if (P.placement === 'outer') {
                    if (e.kind !== 'open' || (P.facing === 'play' && !ctx.playAt(R, e.at - out * 0.1, mid))) continue;
                    const c = R.world(e.at + out * 0.05, mid, 0.05);
                    if (ctx.inSolid(c[0], c[1], c[2])) continue;
                    unit(R.box(P.mat, e.at - s / 2, e.at + s / 2, e.a, e.b, -s / 2, s / 2), R);
                } else {
                    if (e.kind !== 'cover') continue;
                    const c = R.world(e.at + out * 0.05, mid, s * 0.9);   // a wall that comes out of this face at least s
                    if (!ctx.inSolid(c[0], c[1], c[2]) || (P.facing === 'play' && !ctx.playAt(R, e.at - out * s * 1.5, mid))) continue;
                    unit(out > 0 ? R.box(P.mat, e.at - s, e.at, e.a, e.b, 0, s) : R.box(P.mat, e.at, e.at + s, e.a, e.b, 0, s), R);
                }
            }
        }
    },
};

// ---------------------------------------------------------------- floor border / seams
GEN.floorBorder = {
    label: 'Floor border / seams', target: { kind: 'filter', filter: { role: 'floor' } },
    help: 'A border inlaid round the floor where it meets the walls and / or along its free edges, and seams in a grid (prefabs.dress floor seams). Flush trim by default.',
    params: [
        sel('at', 'Border', 'walls', [{ value: 'walls', label: 'where walls stand on it' }, { value: 'edges', label: 'free edges (drops)' }, { value: 'both', label: 'both' }, { value: 'none', label: 'none' }]),
        num('width', 'Border width (m)', 0.25, 0.02, 4), num('sink', 'Sunk into the floor (m)', 0.04, 0, 1, 0.01), num('raise', 'Raised over the floor (m)', 0.01, 0, 0.5, 0.005),
        num('seams', 'Seams every (m, 0: none)', 0, 0, 64, 0.5), num('seamWidth', 'Seam width (m)', 0.12, 0.01, 2),
        mat('mat', 'Material', 8), num('minLength', 'Shortest piece (m)', 0.5, 0, 50), facing(),
    ],
    run(ctx, P, T) {
        const play = (R, u, v) => P.facing !== 'play' || ctx.playAt(R, u, v);
        const put = (R, u0, u1, v0, v1, axis) => {
            if (Math.max(u1 - u0, v1 - v0) < P.minLength || !play(R, (u0 + u1) / 2, (v0 + v1) / 2)) return;
            ctx.emit({ boxes: [R.box(P.mat, u0, u1, v0, v1, -P.sink, P.raise)], split: true, axis, minLength: P.minLength });
        };
        for (const R of ctx.regions('top')) {
            if (P.at !== 'none') for (const e of ctx.edges(R)) {
                const want = e.kind === 'cover' ? P.at !== 'edges' : P.at !== 'walls';
                if (!want) continue;
                const w = P.width, inward = e.o[1] === '-' ? 1 : -1;
                const c0 = Math.min(e.at, e.at + inward * w), c1 = Math.max(e.at, e.at + inward * w);
                if (e.o[0] === 'v') for (const f of ctx.iv.and([[e.a, e.b]], ctx.alongU(R, c0, c1))) put(R, f[0], f[1], c0, c1, R.u);
                else for (const f of ctx.iv.and([[e.a, e.b]], ctx.alongV(R, c0, c1))) put(R, c0, c1, f[0], f[1], R.v);
            }
            if (P.seams > 0) {
                const [U0, V0, U1, V1] = R.ext, h = P.seamWidth / 2, S = P.seams;
                for (let x = Math.ceil(U0 / S) * S; x <= U1; x += S) for (const f of ctx.alongV(R, x - h, x + h)) put(R, x - h, x + h, f[0], f[1], R.v);
                for (let z = Math.ceil(V0 / S) * S; z <= V1; z += S) for (const f of ctx.alongU(R, z - h, z + h)) put(R, f[0], f[1], z - h, z + h, R.u);
            }
        }
    },
};

// ---------------------------------------------------------------- ceiling light strips / lamps
GEN.lightStrip = {
    label: 'Ceiling light strips', target: { kind: 'filter', filter: { role: 'ceiling' } },
    help: 'Glow strips (or lamp fixtures) under ceilings and overhangs with headroom under them (prefabs.light fixtures, the roof-beam lamps). Glow is solid: strips hang only a few cm and are walk-tested.',
    params: [
        sel('style', 'Style', 'strip', [{ value: 'strip', label: 'strips' }, { value: 'lamps', label: 'lamps' }]),
        sel('along', 'Run along', 'long', [{ value: 'long', label: 'the long side' }, { value: 'x', label: 'x' }, { value: 'z', label: 'z' }]),
        num('spacing', 'Spacing between strips (m)', 4, 0.5, 60, 0.25), num('width', 'Strip width (m)', 0.2, 0.02, 4), num('depth', 'Hangs (m)', 0.05, 0.005, 1, 0.005),
        num('margin', 'Kept off the edges (m)', 0.6, 0, 20), num('minLength', 'Shortest strip (m)', 1, 0.1, 100),
        num('minClear', 'Headroom needed under it (m)', 2.4, 0, 60), num('lampSize', 'Lamp size (m)', 0.5, 0.05, 4), num('lampSpacing', 'Lamp spacing (m)', 4, 0.5, 60, 0.25),
        mat('mat', 'Material', 4),
    ],
    run(ctx, P) {
        for (const R of ctx.regions('bottom')) {
            const [U0, V0, U1, V1] = R.ext;
            const alongU = P.along === 'x' || (P.along === 'long' && U1 - U0 >= V1 - V0);   // (u = x, v = z on a ceiling)
            const [A0, A1] = alongU ? [V0, V1] : [U0, U1], m = P.margin, w = P.width;
            for (const p of lines(A0 + m + w / 2, A1 - m - w / 2, P.spacing)) {
                const fits = alongU ? ctx.alongU(R, p - w / 2 - m, p + w / 2 + m) : ctx.alongV(R, p - w / 2 - m, p + w / 2 + m);
                for (const f of fits) {
                    const a = f[0] + m, b = f[1] - m;
                    if (b - a < P.minLength) continue;
                    const mid = alongU ? R.world((a + b) / 2, p, 0) : R.world(p, (a + b) / 2, 0);
                    if (ctx.clearBelow(mid[0], mid[1], mid[2]) < P.minClear || !ctx.playAt(R, alongU ? (a + b) / 2 : p, alongU ? p : (a + b) / 2)) continue;
                    if (P.style === 'strip') {
                        ctx.emit({ boxes: [alongU ? R.box(P.mat, a, b, p - w / 2, p + w / 2, 0, P.depth) : R.box(P.mat, p - w / 2, p + w / 2, a, b, 0, P.depth)], split: true, axis: alongU ? R.u : R.v, minLength: P.minLength });
                    } else {
                        const L = P.lampSize / 2;
                        for (const c of lines(a + L, b - L, P.lampSpacing)) {
                            const at = alongU ? R.world(c, p, 0) : R.world(p, c, 0);
                            if (ctx.clearBelow(at[0], at[1], at[2]) < P.minClear) continue;
                            ctx.emit({ boxes: [alongU ? R.box(P.mat, c - L, c + L, p - L, p + L, 0, P.depth) : R.box(P.mat, p - L, p + L, c - L, c + L, 0, P.depth)] });
                        }
                    }
                }
            }
        }
    },
};

// ---------------------------------------------------------------- door / window frames
GEN.windowFrames = {
    label: 'Door / window frames', target: WALLS,
    help: 'Finds the openings in the walls (two jambs facing each other, a lintel over and a sill or floor under, open both sides) and frames them on both faces, with a sill ledge under windows and an optional lining inside. Doorways are kept clear of every solid decoration.',
    params: [
        sel('kinds', 'Openings', 'both', [{ value: 'both', label: 'doors and windows' }, { value: 'door', label: 'doors' }, { value: 'window', label: 'windows' }]),
        num('frame', 'Frame width (m)', 0.15, 0.02, 2), num('depth', 'Sticks out (m)', 0.05, 0.005, 0.5, 0.005), bool('sill', 'Window sill ledge', true),
        num('sillDepth', 'Sill: sticks out (m)', 0.15, 0.02, 1, 0.01), bool('reveal', 'Line the inside of the opening', false), num('revealDepth', 'Lining depth (m)', 0.03, 0.005, 0.5, 0.005),
        mat('mat', 'Material', 8),
    ],
    run(ctx, P, T) {
        const tset = new Set(T), W = ctx.W, fw = P.frame;
        for (const o of ctx.openings()) {
            if (P.kinds !== 'both' && o.kind !== P.kinds) continue;
            if (!o.jambs.some((j) => tset.has(j))) continue;
            const A = o.along, B = o.thick, boxes = [];
            for (const s of [-1, 1]) {
                const fc = s < 0 ? o.t0 : o.t1, lo = [0, o.y0 - 2, 0], hi = [0, o.y1 + 2, 0];
                lo[A] = o.a - 2; hi[A] = o.b + 2; lo[B] = fc - 1e-3; hi[B] = fc + 1e-3;
                const rects = W.index.query(lo, hi).filter((e) => e.mat < 5 && !e.deco && Math.abs((s > 0 ? e.max[B] : e.min[B]) - fc) < 1e-3)
                    .map((e) => [e.min[A], e.min[1], e.max[A], e.max[1]]);
                if (!rects.length) continue;
                const R = D.region.make(W, B, s, fc, rects);
                if (!ctx.playAt(R, (o.a + o.b) / 2, (o.y0 + o.y1) / 2)) continue;
                const piece = (u0, u1, v0, v1, d) => { for (const f of ctx.iv.and([[u0, u1]], ctx.alongU(R, v0, v1))) boxes.push(R.box(P.mat, f[0], f[1], v0, v1, 0, d)); };
                piece(o.a - fw, o.a, o.y0, o.y1 + fw, P.depth);
                piece(o.b, o.b + fw, o.y0, o.y1 + fw, P.depth);
                piece(o.a, o.b, o.y1, o.y1 + fw, P.depth);
                if (o.kind === 'window') {
                    piece(o.a - fw, o.b + fw, o.y0 - fw, o.y0, P.depth);
                    if (P.sill) piece(o.a - fw, o.b + fw, o.y0 - 0.06, o.y0, P.sillDepth);
                }
            }
            if (P.reveal) {
                const r = P.revealDepth, mk = (a0, a1, y0, y1) => { const p = [0, y0, 0], q = [0, y1, 0]; p[A] = a0; q[A] = a1; p[B] = o.t0; q[B] = o.t1; return D.box(P.mat, p[0], p[1], p[2], q[0], q[1], q[2]); };
                boxes.push(mk(o.a, o.a + r, o.y0, o.y1), mk(o.b - r, o.b, o.y0, o.y1), mk(o.a, o.b, o.y1 - r, o.y1));
            }
            if (boxes.length) ctx.emit({ boxes });
        }
    },
};

// ---------------------------------------------------------------- railings along drops
GEN.railings = {
    label: 'Railings along drops', target: { kind: 'filter', filter: { role: 'floor' } },
    help: 'Posts and rails along the free edges of floors where the drop is at least the given height (prefabs.catwalk rails). Trim (not solid) by default: a solid railing would stop players dropping off the edge, which the engine refuses unless "may block drops" is ticked.',
    params: [
        num('minDrop', 'Only over drops of at least (m)', 1.0, 0.1, 100), num('height', 'Height (m)', 1.0, 0.2, 3), num('postSpacing', 'Post spacing (m)', 1.5, 0.2, 20),
        num('post', 'Post size (m)', 0.08, 0.01, 1, 0.01), num('rail', 'Rail size (m)', 0.08, 0.01, 1, 0.01), bool('midRail', 'Middle rail', true),
        num('inset', 'Set back from the edge (m)', 0.05, 0, 2, 0.01), num('minLength', 'Shortest (m)', 1, 0.1, 100), mat('mat', 'Material', 8),
    ],
    run(ctx, P) {
        for (const R of ctx.regions('top')) {
            const top = R.coord;
            for (const e of ctx.edges(R)) {
                if (e.kind !== 'open' || e.b - e.a < P.minLength) continue;
                const out = e.o[1] === '+' ? 1 : -1, alongU = e.o[0] === 'v';
                const n = Math.max(1, Math.ceil((e.b - e.a) / 0.25)), ok = [];
                for (let i = 0; i < n; i++) {
                    const s = e.a + (e.b - e.a) * (i + 0.5) / n;
                    const p = alongU ? R.world(s, e.at + out * 0.05, 0) : R.world(e.at + out * 0.05, s, 0);
                    const g = ctx.groundBelow(p[0], top - 0.01, p[2]);
                    ok.push(top - g >= P.minDrop && !ctx.inSolid(p[0], top + 0.5, p[2]));
                }
                const runs = [];
                for (let i = 0; i < n; i++) if (ok[i]) {
                    const a = e.a + (e.b - e.a) * i / n, b = e.a + (e.b - e.a) * (i + 1) / n, l = runs[runs.length - 1];
                    if (l && Math.abs(l[1] - a) < 1e-6) l[1] = b; else runs.push([a, b]);
                }
                const inw = -out, c0 = e.at + inw * P.inset, c1 = e.at + inw * (P.inset + Math.max(P.post, P.rail));
                const lo = Math.min(c0, c1), hi = Math.max(c0, c1), axis = alongU ? R.u : R.v;
                const mk = (a, b, y0, y1, l0 = lo, l1 = hi) => alongU ? R.box(P.mat, a, b, l0, l1, y0, y1) : R.box(P.mat, l0, l1, a, b, y0, y1);
                for (const [a0, b0] of runs) {
                    if (b0 - a0 < P.minLength || !ctx.playAt(R, alongU ? (a0 + b0) / 2 : (lo + hi) / 2, alongU ? (lo + hi) / 2 : (a0 + b0) / 2)) continue;
                    for (const [a, b] of ctx.freeIntervals(mk(a0, b0, 0, P.height), axis)) {
                        if (b - a < P.minLength) continue;
                        const boxes = [mk(a, b, P.height - P.rail, P.height)];
                        if (P.midRail) boxes.push(mk(a, b, P.height / 2 - P.rail / 2, P.height / 2 + P.rail / 2));
                        const k = Math.max(1, Math.ceil((b - a) / P.postSpacing));
                        for (let i = 0; i <= k; i++) {
                            const c = Math.min(b - P.post / 2, Math.max(a + P.post / 2, a + (b - a) * i / k));
                            boxes.push(mk(c - P.post / 2, c + P.post / 2, 0, P.height - P.rail));
                        }
                        ctx.emit({ boxes, blocksDrop: true });
                    }
                }
            }
        }
    },
};

// ---------------------------------------------------------------- crates / props scattered on open floor
GEN.scatter = {
    label: 'Crates / props', target: { kind: 'filter', filter: { role: 'floor' } },
    help: 'Crates and props on a jittered grid over open floor, seeded (prefabs.clutter / scatter / crates): each kept off walls and other things by the clearance, off spawns and objectives, and walk-tested so no passage gets narrower than a player. "Near walls" keeps them in tidy stacks by the walls.',
    params: [
        num('step', 'Grid step (m)', 4, 0.5, 64, 0.25), num('jitter', 'Jitter (fraction of the step)', 0.35, 0, 0.5, 0.05), num('chance', 'Chance per grid cell', 0.6, 0, 1, 0.05),
        { key: 'shapes', label: 'Shapes (w d h material per line)', type: 'shapes', default: [{ w: 1.2, d: 1.2, h: 1.2, mat: 0 }, { w: 1.1, d: 1.1, h: 1.1, mat: 1 }, { w: 2.4, d: 1.2, h: 1.2, mat: 3 }] },
        num('stack', 'Chance of a second crate on top', 0.3, 0, 1, 0.05), num('clear', 'Clearance to anything else (m)', 1.2, 0, 10), num('edgeClear', 'Kept off the floor\'s edges (m)', 0.5, 0, 10),
        num('wallDist', 'Near walls: within (m, 0: anywhere)', 0, 0, 20), mat('stackMat', 'Top crate material', 1),
    ],
    run(ctx, P, T) {
        const shapes = (Array.isArray(P.shapes) ? P.shapes : []).filter((s) => s && s.w > 0 && s.d > 0 && s.h > 0);
        if (!shapes.length) return;
        const S = P.step;
        for (const R of ctx.regions('top')) {
            const [U0, V0, U1, V1] = R.ext, top = R.coord;
            for (let i = Math.floor(U0 / S); i * S < U1; i++) for (let j = Math.floor(V0 / S); j * S < V1; j++) {
                const rnd = ctx.rng('cell', Math.round(top * 100), i, j);
                if (rnd() >= P.chance) continue;
                const sh = shapes[Math.floor(rnd() * shapes.length) % shapes.length];
                const swap = rnd() < 0.5, w = swap ? sh.d : sh.w, d = swap ? sh.w : sh.d, h = sh.h;
                const cx = (i + 0.5) * S + (rnd() * 2 - 1) * P.jitter * S, cz = (j + 0.5) * S + (rnd() * 2 - 1) * P.jitter * S;
                const stack = rnd() < P.stack, sx = (rnd() - 0.5) * 0.2 * w, sz = (rnd() - 0.5) * 0.2 * d;
                const ec = P.edgeClear, x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
                if (!ctx.alongU(R, z0 - ec, z1 + ec).some((f) => f[0] <= x0 - ec + 1e-6 && f[1] >= x1 + ec - 1e-6)) continue;
                const hh = h * (stack ? 1.7 : 1);
                if (ctx.headroom(cx, top, cz) < hh + 0.5) continue;
                const c = P.clear;
                const near = ctx.W.solid.query([x0 - c, top + 0.05, z0 - c], [x1 + c, top + hh + 0.5, z1 + c])
                    .filter((e) => e.max[1] > top + 0.05 && e.min[1] < top + hh + 0.5 && e.min[0] < x1 + c && e.max[0] > x0 - c && e.min[2] < z1 + c && e.max[2] > z0 - c);
                if (near.length) continue;
                if (P.wallDist > 0) {
                    const q = P.wallDist;
                    const walls = ctx.W.solid.query([x0 - q, top + 0.1, z0 - q], [x1 + q, top + 2, z1 + q]).filter((e) => e.max[1] >= top + 2 && e.min[1] <= top + 0.1 &&
                        e.min[0] < x1 + q && e.max[0] > x0 - q && e.min[2] < z1 + q && e.max[2] > z0 - q);
                    if (!walls.length) continue;
                }
                if (!ctx.playAt(R, cx, cz)) continue;
                const boxes = [ctx.box(sh.mat | 0, x0, top, z0, x1, top + h, z1)];
                if (stack) boxes.push(ctx.box(P.stackMat, cx + sx - w * 0.4, top + h, cz + sz - d * 0.4, cx + sx + w * 0.4, top + h * 1.7, cz + sz + d * 0.4));
                ctx.emit({ boxes });
            }
        }
    },
};

// ---------------------------------------------------------------- kick plates on stairs
GEN.kickPlates = {
    label: 'Stair kick plates / nosings', target: { kind: 'all' }, ignoreKeep: ['stairs'],
    help: 'A plate along the foot of every riser and a nosing strip along the front edge of every tread, on the staircases the map records (stairs lines and stairs groups) that the target touches.',
    params: [
        num('height', 'Plate height (m, 0: the whole riser)', 0.1, 0, 2, 0.01), num('depth', 'Plate sticks out (m)', 0.02, 0.005, 0.3, 0.005), mat('mat', 'Plate material', 8),
        bool('nosing', 'Nosing strip on the treads', true), num('nosingWidth', 'Nosing width (m)', 0.06, 0.01, 1, 0.01), mat('nosingMat', 'Nosing material', 8),
    ],
    run(ctx, P, T) {
        const W = ctx.W;
        const isStep = (e, s) => e.min[0] >= s.x0 - 0.02 && e.max[0] <= s.x1 + 0.02 && e.min[2] >= s.z0 - 0.02 && e.max[2] <= s.z1 + 0.02 && e.max[1] > s.base + 1e-3 && e.max[1] <= s.top + 0.02;
        for (const s of ctx.stairs) {
            if (!T.some((e) => isStep(e, s))) continue;
            const lo = [s.x0, s.base, s.z0], hi = [s.x1, s.top, s.z1];
            const steps = W.index.query(lo, hi).filter((e) => e.mat < 5 && !e.deco && isStep(e, s)).sort((a, b) => a.max[1] - b.max[1]);
            const A = s.axis, sign = -s.rising, u = A === 0 ? 2 : 0;
            for (const E of steps) {
                const boxes = [];
                const coord = sign > 0 ? E.max[A] : E.min[A];
                const R = D.region.make(W, A, sign, coord, [[E.min[u], E.min[1], E.max[u], E.max[1]]]);
                for (const e of ctx.edges(R)) {
                    if (e.o !== 'v-') continue;
                    const v1 = P.height > 0 ? e.at + P.height : regionTopAt(R, e.at);
                    for (const f of ctx.iv.and([[e.a, e.b]], ctx.alongU(R, e.at, v1))) boxes.push(R.box(P.mat, f[0], f[1], e.at, v1, 0, P.depth));
                }
                if (P.nosing) {
                    const R2 = D.region.make(W, 1, 1, E.max[1], [[E.min[0], E.min[2], E.max[0], E.max[2]]]);
                    const want = (A === 0 ? 'u' : 'v') + (sign > 0 ? '+' : '-'), nw = P.nosingWidth;
                    for (const e of ctx.edges(R2)) {
                        if (e.o !== want || e.kind !== 'open') continue;
                        const c0 = sign > 0 ? e.at - nw : e.at, c1 = sign > 0 ? e.at : e.at + nw;
                        boxes.push(A === 0 ? R2.box(P.nosingMat, c0, c1, e.a, e.b, -0.02, 0.01) : R2.box(P.nosingMat, e.a, e.b, c0, c1, -0.02, 0.01));
                    }
                }
                if (boxes.length) ctx.emit({ boxes });
            }
        }
    },
};
function regionTopAt(R, v) { const b = R.bands.find((x) => x.v0 <= v + 1e-4 && x.v1 > v); return b ? b.v1 : v; }

if (typeof module !== 'undefined' && module.exports) module.exports = GEN;
})(typeof globalThis !== 'undefined' ? globalThis : window);
