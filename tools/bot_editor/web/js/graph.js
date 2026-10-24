// Graph view: one slot (or expression curve) as an SVG node graph, Blueprint style. Every AST node is a graph node;
// expression children are data edges into typed input ports, statement order is exec edges (if has an exec output
// per arm). A name (param, sensor, weapon...) used several times is ONE source node with several edges.
// toGraph / fromGraph are pure (node-testable): fromGraph(toGraph(d)) deep-equals d. An "overview" graph shows how
// slots use params, curves, lists and tables. "whole" (the default) puts every slot's graph on one canvas.

import { decompose, compose, CATALOG, symbols, slotKey, declKey, walkStmts, stmtExprs, walkExpr, newExpr, newStmt, WEAPONS, BRAIN_MODES, POINTS, wholeGroups } from "./ast.js";
import { exprNumText, printDeclHead, roundTo } from "./dsl.js";

const W = 176, HEAD = 24, LINE = 18, COLW = 216, INDENT = 230, GAP = 16;

// ---------------------------------------------------------------- model
export function toGraph(decl) {
    const nodes = [], edges = [];
    let n = 0;
    const gid = () => "g" + ++n;
    const sources = new Map();
    const addExpr = (e) => {
        if (e.op === "name") {
            let s = sources.get(e.name);
            if (!s) { s = { gid: gid(), kind: "source", cat: "expr", op: "name", fields: { name: e.name }, ins: [], outs: [] }; sources.set(e.name, s); nodes.push(s); }
            return { node: s, expr: { ...e } };
        }
        const p = decompose(e);
        const node = { gid: gid(), kind: "expr", cat: "expr", op: p.op, fields: p.fields, ins: p.inputs.map(([k]) => k), outs: [] };
        nodes.push(node);
        for (const [k, c] of p.inputs) { const r = addExpr(c); edges.push({ kind: "data", from: r.node.gid, to: node.gid, port: k, ...(r.expr ? { expr: r.expr } : {}) }); }
        return { node };
    };
    const addStmts = (list, fromNode, port) => {
        let prev = fromNode, prevPort = port;
        for (const s of list) {
            const p = decompose(s);
            const node = { gid: gid(), kind: "stmt", cat: "stmt", op: p.op, fields: p.fields, ins: p.inputs.map(([k]) => k), outs: [...p.stacks.map(([k]) => k), "next"] };
            nodes.push(node);
            edges.push({ kind: "exec", from: prev.gid, port: prevPort, to: node.gid });
            for (const [k, c] of p.inputs) { const r = addExpr(c); edges.push({ kind: "data", from: r.node.gid, to: node.gid, port: k, ...(r.expr ? { expr: r.expr } : {}) }); }
            for (const [k, body] of p.stacks) addStmts(body, node, k);
            prev = node; prevPort = "next";
        }
    };
    const p = decompose(decl);
    const entry = { gid: gid(), kind: "entry", cat: "decl", op: p.op, fields: p.fields, ins: p.inputs.map(([k]) => k), outs: p.stacks.map(([k]) => k) };
    nodes.push(entry);
    for (const [k, c] of p.inputs) { const r = addExpr(c); edges.push({ kind: "data", from: r.node.gid, to: entry.gid, port: k, ...(r.expr ? { expr: r.expr } : {}) }); }
    for (const [k, body] of p.stacks) addStmts(body, entry, k);
    const g = { nodes, edges, next: n };
    layoutGraph(g);
    return g;
}

export function fromGraph(g) {
    const byId = new Map(g.nodes.map((x) => [x.gid, x]));
    const dataIn = new Map(), execOut = new Map();
    for (const e of g.edges) {
        if (e.kind === "data") dataIn.set(e.to + "|" + e.port, e);
        else execOut.set(e.from + "|" + e.port, e);
    }
    const seen = new Set();
    const expr = (edge) => {
        if (!edge) return { op: "hole" };
        const src = byId.get(edge.from);
        if (!src) return { op: "hole" };
        if (src.kind === "source") return { ...(edge.expr || {}), op: "name", name: src.fields.name };
        if (seen.has(src.gid)) return { op: "hole" };   // (a cycle: not a tree)
        seen.add(src.gid);
        const r = build(src);
        seen.delete(src.gid);
        return r;
    };
    const chain = (node, port) => {
        const out = [];
        const visited = new Set();
        let e = execOut.get(node.gid + "|" + port);
        while (e) {
            const t = byId.get(e.to);
            if (!t || visited.has(t.gid) || t.kind !== "stmt") break;
            visited.add(t.gid);
            out.push(build(t));
            e = execOut.get(t.gid + "|next");
        }
        return out;
    };
    const build = (node) => compose({ cat: node.cat, op: node.op, fields: node.fields,
        inputs: node.ins.map((k) => [k, expr(dataIn.get(node.gid + "|" + k))]),
        stacks: node.kind === "stmt" || node.kind === "entry" ? node.outs.filter((k) => k !== "next").map((k) => [k, chain(node, k)]) : [] });
    const entry = g.nodes.find((x) => x.kind === "entry");
    return build(entry);
}

// heights and layout (statements in a column, if-arms indented, expression trees to the left of their consumer)
export function nodeHeight(n) {
    const rows = Math.max(n.ins.length, n.outs.filter((k) => k !== "next").length);
    return HEAD + Math.max(1, rows) * LINE + 6;
}
export function layoutGraph(g, keep = null) {
    const byId = new Map(g.nodes.map((x) => [x.gid, x]));
    const dataInto = new Map(), execOut = new Map();
    for (const e of g.edges) {
        if (e.kind === "data") { if (!dataInto.has(e.to)) dataInto.set(e.to, []); dataInto.get(e.to).push(e); }
        else execOut.set(e.from + "|" + e.port, e);
    }
    const placed = new Set();
    let y = 0;
    const placeExprTree = (node, x, top) => {   // returns the bottom of what it placed
        let cur = top;
        const ins = (dataInto.get(node.gid) || []).slice().sort((a, b) => node.ins.indexOf(a.port) - node.ins.indexOf(b.port));
        for (const e of ins) {
            const s = byId.get(e.from);
            if (!s || placed.has(s.gid)) continue;
            placed.add(s.gid);
            const bottom = placeExprTree(s, x - COLW, cur);
            const h = nodeHeight(s);
            const kidsTop = cur;
            s.x = x - COLW;
            s.y = Math.max(kidsTop, (kidsTop + bottom) / 2 - h / 2);
            cur = Math.max(bottom, s.y + h) + GAP / 2;
        }
        return cur;
    };
    const placeStmt = (node, depth) => {
        placed.add(node.gid);
        node.x = depth * INDENT;
        node.y = y;
        const bottom = placeExprTree(node, node.x, y);
        y = Math.max(y + nodeHeight(node), bottom) + GAP;
        for (const k of node.outs) {
            if (k === "next") continue;
            let e = execOut.get(node.gid + "|" + k);
            while (e) { const t = byId.get(e.to); if (!t || placed.has(t.gid)) break; placeStmt(t, depth + 1); e = execOut.get(t.gid + "|next"); }
        }
    };
    const entry = g.nodes.find((x) => x.kind === "entry");
    if (entry) {
        placed.add(entry.gid);
        entry.x = 0; entry.y = 0;
        const bottom = placeExprTree(entry, 0, 0);
        y = Math.max(nodeHeight(entry), bottom) + GAP * 2;
        for (const k of entry.outs) {
            let e = execOut.get(entry.gid + "|" + k);
            while (e) { const t = byId.get(e.to); if (!t || placed.has(t.gid)) break; placeStmt(t, 0); e = execOut.get(t.gid + "|next"); }
        }
    }
    // loose nodes: a row at the bottom
    let lx = 0;
    for (const n of g.nodes) if (!placed.has(n.gid)) { if (keep && keep.has(n.gid)) continue; n.x = lx; n.y = y + GAP; lx += COLW; }
    return g;
}

// the overview: which slots read which params / curves / lists / tables
export function overviewGraph(brain) {
    const S = symbols(brain);
    const nodes = [], edges = [];
    const id = new Map();
    const add = (kind, name, label, x) => { const g = { gid: kind + ":" + name, kind, name, label, x, y: 0 }; nodes.push(g); id.set(kind + ":" + name, g); return g; };
    for (const d of brain.decls) {
        if (d.kind === "param") add("param", d.name, d.name, 0);
        else if (d.kind === "curve" || d.kind === "list" || d.kind === "table") add(d.kind, d.name, d.name, 320);
        else if (d.kind === "slot") add("slot", slotKey(d), slotKey(d), 640);
    }
    const use = (fromKind, from, toKey) => { const a = id.get(fromKind + ":" + from), b = id.get(toKey); if (a && b && !edges.some((e) => e.from === a.gid && e.to === b.gid)) edges.push({ from: a.gid, to: b.gid }); };
    const scan = (e, to) => walkExpr(e, (x) => {
        if (x.op === "name") { if (S.params.has(x.name)) use("param", x.name, to); if (S.lists.has(x.name)) use("list", x.name, to); }
        if (x.op === "call" && S.curves.has(x.fn)) use("curve", x.fn, to);
        if (x.op === "index") use("table", x.table, to);
        if (x.op === "in") use("list", x.list, to);
    });
    for (const d of brain.decls) {
        if (d.kind === "curve" && d.form === "expr") scan(d.body, "curve:" + d.name);
        if (d.kind === "slot") walkStmts(d.body, (s) => stmtExprs(s).forEach((x) => scan(x, "slot:" + slotKey(d))));
    }
    // rows: slots in file order; params / lists next to the first slot that uses them (barycentre)
    const slots = nodes.filter((n) => n.kind === "slot");
    slots.forEach((n, i) => (n.y = i * 30));
    const users = (g) => edges.filter((e) => e.from === g.gid).map((e) => id.get(e.to));
    const mids = nodes.filter((n) => n.kind === "curve" || n.kind === "list" || n.kind === "table");
    const bary = (g) => { const u = users(g).filter((x) => x.kind === "slot"); return u.length ? u.reduce((a, x) => a + x.y, 0) / u.length : 1e6; };
    const stackCol = (col) => { col.sort((a, b) => a._b - b._b); let yy = -1e9; for (const n of col) { n.y = Math.max(yy + 26, n._b === 1e6 ? yy + 26 : n._b); yy = n.y; } };
    for (const m of mids) m._b = bary(m);
    stackCol(mids);
    const params = nodes.filter((n) => n.kind === "param");
    for (const p of params) { const u = users(p); p._b = u.length ? u.reduce((a, x) => a + x.y, 0) / u.length : 1e6; }
    stackCol(params);
    return { nodes, edges };
}

// ---------------------------------------------------------------- the whole brain as one graph
// Every slot (and expression curve) is a frame holding its own toGraph graph (node coordinates stay frame-local, so
// fromGraph / editing work unchanged); frames are shelf-packed into one cluster per area, clusters into a grid.
// With `shared`, every param / curve / list / table (and sensors read by 2+ slots) is one node left of everything,
// with an edge to each use (and a bundle per frame for the zoomed-out view).
export const PILL_H = 22;
const FPAD = 16, FTITLE = 30, CPAD = 22, CTITLE = 120, FGAP = 26, CGAP = 70, SW = 170, SROW = 26;
export const pillWidth = (name) => Math.min(W, 22 + name.length * 7.4);
export function frameBounds(f) {
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (const n of f.g.nodes) {
        const h = n.kind === "source" ? PILL_H : nodeHeight(n) + (n.outs.includes("next") ? 10 : 0);
        x0 = Math.min(x0, n.x); y0 = Math.min(y0, n.y); x1 = Math.max(x1, n.x + W); y1 = Math.max(y1, n.y + h);
    }
    if (!Number.isFinite(x0)) { x0 = 0; y0 = 0; x1 = W; y1 = 40; }
    f.dx = FPAD - x0; f.dy = FTITLE + FPAD - y0;   // frame-local node coordinates -> frame box
    f.w = Math.max(x1 - x0 + 2 * FPAD, 12 + f.title.length * 8.5); f.h = y1 - y0 + 2 * FPAD + FTITLE;
    return f;
}
// grow / shrink a frame to its nodes without moving them on screen
export function refitFrame(f) { const ox = f.x + f.dx, oy = f.y + f.dy; frameBounds(f); f.x = ox - f.dx; f.y = oy - f.dy; return f; }
function shelf(items, maxW, gap) {
    let x = 0, y = 0, rowH = 0, w = 0;
    for (const it of items) {
        if (x > 0 && x + it.w > maxW) { x = 0; y += rowH + gap; rowH = 0; }
        it.px = x; it.py = y;
        x += it.w + gap; rowH = Math.max(rowH, it.h); w = Math.max(w, x - gap);
    }
    return { w, h: y + rowH };
}
// keep node positions the user moved (by AST id, else by source name / gid), as the slot view does
export function keepMoved(oldG, g) {
    const key = (n) => n.fields.id || (n.kind === "source" ? "src:" + n.fields.name : n.gid);
    const pos = new Map();
    for (const n of oldG.nodes) if (n.moved) pos.set(key(n), [n.x, n.y]);
    if (!pos.size) return g;
    for (const n of g.nodes) { const p = pos.get(key(n)); if (p) { n.x = p[0]; n.y = p[1]; n.moved = true; } }
    return g;
}
export function wholeGraph(brain, opts = {}) {
    const frames = [], clusters = [];
    for (const grp of wholeGroups(brain)) {
        const fs = [];
        for (const i of grp.decls) {
            const d = brain.decls[i];
            if (!(d.kind === "slot" || (d.kind === "curve" && d.form === "expr"))) continue;
            const key = declKey(d);
            const g = toGraph(d);
            if (opts.keep && opts.keep.has(key)) keepMoved(opts.keep.get(key), g);
            const f = frameBounds({ key, ix: i, area: grp.area, title: d.kind === "slot" ? (d.name === "objective" ? "objective " + d.mode : d.name) : "curve " + d.name, g, x: 0, y: 0 });
            fs.push(f); frames.push(f);
        }
        if (fs.length) clusters.push({ area: grp.area, label: grp.label, frames: fs });
    }
    // frames inside a cluster: tallest first (shelves waste less), about as wide as tall
    for (const c of clusters) {
        const area = c.frames.reduce((a, f) => a + (f.w + FGAP) * (f.h + FGAP), 0);
        const maxW = Math.max(...c.frames.map((f) => f.w), Math.sqrt(area * 1.5));
        const r = shelf([...c.frames].sort((a, b) => b.h - a.h), maxW, FGAP);
        c.w = Math.max(r.w, 24 * c.label.length) + 2 * CPAD; c.h = r.h + CTITLE + CPAD;
    }
    const total = clusters.reduce((a, c) => a + (c.w + CGAP) * (c.h + CGAP), 0);
    shelf(clusters, Math.max(...clusters.map((c) => c.w), Math.sqrt(total * 1.9)), CGAP);
    for (const c of clusters) {
        c.x = c.px; c.y = c.py;
        for (const f of c.frames) { f.x = c.x + CPAD + f.px; f.y = c.y + CTITLE + f.py; }
    }
    const wg = { frames, clusters, shared: null };
    if (opts.shared) wg.shared = sharedInputs(brain, frames);
    wholeBounds(wg);
    return wg;
}
export function wholeBounds(wg) {
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    const box = (x, y, w, h) => { x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x + w); y1 = Math.max(y1, y + h); };
    for (const c of wg.clusters) box(c.x, c.y, c.w, c.h);
    for (const f of wg.frames) box(f.x, f.y, f.w, f.h);
    if (wg.shared) for (const n of wg.shared.nodes) box(n.x, n.y, SW, PILL_H);
    if (!Number.isFinite(x0)) { x0 = 0; y0 = 0; x1 = 100; y1 = 100; }
    wg.bounds = { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
    return wg.bounds;
}
// the shared inputs: what each frame reads from outside itself
export function sharedInputs(brain, frames) {
    const S = symbols(brain);
    const items = new Map();
    const use = (kind, name, fi, gid) => { const id = kind + ":" + name; let it = items.get(id); if (!it) items.set(id, (it = { id, kind, name, uses: [] })); it.uses.push({ f: fi, gid }); };
    frames.forEach((f, fi) => {
        for (const n of f.g.nodes) {
            if (n.kind === "source") {
                const nm = n.fields.name;
                if (S.params.has(nm)) use("param", nm, fi, n.gid);
                else if (S.lists.has(nm)) use("list", nm, fi, n.gid);
                else if (!(WEAPONS.includes(nm) || BRAIN_MODES.includes(nm) || POINTS.includes(nm) || nm === "none")) use("sensor", nm, fi, n.gid);
            } else if (n.op === "call" && S.curves.has(n.fields.fn)) use("curve", n.fields.fn, fi, n.gid);
            else if (n.op === "index") use("table", n.fields.table, fi, n.gid);
            else if (n.op === "in") use("list", n.fields.list, fi, n.gid);
        }
    });
    const nodes = [...items.values()].filter((it) => it.kind !== "sensor" || new Set(it.uses.map((u) => u.f)).size >= 2);
    // one column left of everything, each node at the mean height of what it feeds (then pushed apart)
    let left = Infinity;
    for (const f of frames) left = Math.min(left, f.x);
    const at = (u) => { const f = frames[u.f], n = f.g.nodes.find((x) => x.gid === u.gid); return f.y + f.dy + (n ? n.y : 0); };
    for (const it of nodes) { it.b = it.uses.reduce((a, u) => a + at(u), 0) / it.uses.length; it.frames = [...new Set(it.uses.map((u) => u.f))]; }
    nodes.sort((a, b) => a.b - b.b);
    let y = -Infinity;
    for (const it of nodes) { it.x = (Number.isFinite(left) ? left : 0) - SW - 260; it.y = Math.max(it.b, y + SROW); y = it.y; }
    return { nodes, edges: nodes.reduce((a, it) => a + it.uses.length, 0) };
}
// the brain back from the whole-brain graph: each frame's declaration from its graph, the rest unchanged
export function wholeGraphToBrain(brain, wg) {
    const byIx = new Map(wg.frames.map((f) => [f.ix, f]));
    return { ...brain, decls: brain.decls.map((d, i) => (byIx.has(i) ? fromGraph(byIx.get(i).g) : d)) };
}

// ---------------------------------------------------------------- the view
const SVGNS = "http://www.w3.org/2000/svg";
const svg = (tag, attrs = {}, parent) => { const e = document.createElementNS(SVGNS, tag); for (const k in attrs) e.setAttribute(k, attrs[k]); if (parent) parent.append(e); return e; };
const PORT_LABEL = (node, k) => {
    if (node.op === "if") { const m = /^arms\.(\d+)\.(cond|body)$/.exec(k); if (m) return (m[1] === "0" ? "if" : "elif") + (m[2] === "cond" ? "" : " then"); if (k === "else.body") return "else"; }
    if (/^args\.(\d+)$/.test(k)) return "arg " + (+k.split(".")[1] + 1);
    if (/^values\.(\d+)$/.test(k)) { const outs = node._slotOuts; const i = +k.split(".")[1]; return outs && outs[i] ? outs[i] : "value " + (i + 1); }
    return { a: node.op === "?:" ? "then" : "a", b: node.op === "?:" ? "else" : "b", c: "if", value: "value", point: "point", index: "weapon", body: node.kind === "entry" ? (node.op === "curve" ? "= expr" : "body") : "body" }[k] || k;
};
function nodeTitle(n) {
    const f = n.fields;
    if (n.kind === "entry") return n.op === "slot" ? `slot ${f.name === "objective" ? "objective " + f.mode : f.name}` : `curve ${f.name}(${f.arg})`;
    if (n.kind === "source") return f.name;
    switch (n.op) {
        case "num": return exprNumText(f.v, f.type);
        case "bool": return f.v ? "yes" : "no";
        case "call": return f.fn + "()";
        case "index": return f.table + "[ ]";
        case "in": return "in " + f.list;
        case "neg": return "−x";
        case "set": return "set " + f.name;
        case "hole": return "_";
        default: return n.op;
    }
}
const CLASS = (n, S) => {
    if (n.kind === "entry") return "hat";
    if (n.kind === "source") { const nm = n.fields.name; return S && S.params.has(nm) ? "param" : WEAPONS.includes(nm) || BRAIN_MODES.includes(nm) || nm === "none" ? "data" : S && S.lists.has(nm) ? "data" : "sensor"; }
    if (n.cat === "stmt") return n.op === "if" ? "logic" : n.op === "set" || n.op === "return" ? "set" : "action";
    if (n.op === "call") return ["roll", "gauss", "chance"].includes(n.fields.fn) ? "random" : CATALOG.builtins[n.fields.fn] ? "math" : "curve";
    if (["num", "+", "-", "*", "/", "neg"].includes(n.op)) return "math";
    return "logic";
};

const KMIN = 0.04, KMAX = 2.5, LOD_LABELS = 0.55, LOD_FAR = 0.3;
const AREA_COLOR = { perception: "#2b8aa8", targeting: "#8b5cf6", aim: "#c77d2e", weapons: "#6b7b8f", "fight-movement": "#c0463a", roam: "#3f9d63", mode: "#b59a28" };
const SKIND_CLASS = { param: "param", curve: "curve", list: "data", table: "data", sensor: "sensor" };

// Three modes: "whole" (the default: every slot's graph on one canvas, clusters by area, level of detail by zoom,
// frames off screen not drawn), "slot" (one slot or expression curve) and "overview" (who uses what).
export class GraphView {
    constructor(root, app) {
        this.root = root; this.app = app;
        root.classList.add("graph");
        this.bar = document.createElement("div"); this.bar.className = "gr-bar";
        this.svgEl = svg("svg", { class: "gr-svg" });
        this.side = document.createElement("div"); this.side.className = "gr-side";
        this.stage = document.createElement("div"); this.stage.className = "gr-stage";
        this.mini = document.createElement("canvas"); this.mini.className = "gr-minimap";
        this.mini.title = "the whole brain: click or drag to move there";
        this.stage.append(this.svgEl, this.mini);
        const wrap = document.createElement("div"); wrap.className = "gr-wrap";
        wrap.append(this.stage, this.side);
        root.append(this.bar, wrap);
        this.view = { x: 40, y: 40, k: 0.9 };
        this.wholeView = null;   // the whole mode's own pan / zoom (kept when switching modes, saved)
        this.layer = svg("g", {}, this.svgEl);
        this.g = null; this.sel = null; this.mode = "whole"; this.sharedOn = false;
        this.wg = null; this.fr = null; this.search = { q: "", hits: [], i: -1 };
        this._lodLevel = -1;
        this._bar();
        this._input();
        this._miniInput();
    }

    restoreState(st) {
        if (!st) return;
        if (["whole", "slot", "overview"].includes(st.mode)) this.mode = st.mode;
        this.sharedOn = !!st.shared;
        if (st.view && [st.view.x, st.view.y, st.view.k].every(Number.isFinite)) { this.wholeView = { ...st.view }; if (this.mode === "whole") this.view = { ...st.view }; }
        if (typeof st.slot === "string") this.key = st.slot;
        this._bar();
    }
    _save() {
        clearTimeout(this._saveT);
        this._saveT = setTimeout(() => {
            const v = this.mode === "whole" ? this.view : this.wholeView;
            if (this.app.saveViewState) this.app.saveViewState("graph", { mode: this.mode, shared: this.sharedOn, view: v ? { x: Math.round(v.x), y: Math.round(v.y), k: +v.k.toFixed(4) } : null, slot: this.key || null });
        }, 400);
    }
    // the page shows this view: whole mode keeps its pan / zoom (fit the first time), the others fit
    shown() {
        if (this.mode === "whole" && this.wholeView) { this.view = { ...this.wholeView }; this._applyView(); return; }
        this.fit();
    }

    setBrain(brain, key) {
        this.brain = brain;
        this.S = symbols(brain);
        if (key !== undefined && key !== null && brain.decls.some((d) => declKey(d) === key && (d.kind === "slot" || (d.kind === "curve" && d.form === "expr")))) this.key = key;
        if (!this.key || !brain.decls.some((d) => declKey(d) === this.key)) { const d = brain.decls.find((x) => x.kind === "slot"); this.key = d && declKey(d); }
        const d = brain.decls.find((x) => declKey(x) === this.key);
        if (this.mode === "whole") {
            const keep = new Map();
            if (this.wg) for (const f of this.wg.frames) if (f.g.nodes.some((n) => n.moved)) keep.set(f.key, f.g);
            this.wg = wholeGraph(brain, { shared: this.sharedOn, keep });
            for (const f of this.wg.frames) this._annotate(brain.decls[f.ix], f.g);
            this.fr = this.wg.frames.find((f) => f.key === this.key) || null;
            this.g = this.fr ? this.fr.g : null;
        } else if (this.mode === "overview") { this.ov = overviewGraph(brain); this.g = null; }
        else if (d && (d.kind === "slot" || (d.kind === "curve" && d.form === "expr"))) {
            const old = this.g && this.gKey === this.key ? this.g : null;
            this.g = toGraph(d);
            this.gKey = this.key;
            this._annotate(d, this.g);
            if (old) keepMoved(old, this.g);
        } else this.g = null;
        this.sel = null;
        this.render();
    }
    // open a declaration: whole mode flies to its frame, else show that slot
    select(key) {
        if (this.mode === "whole") {
            const f = this.wg && this.wg.frames.find((x) => x.key === key);
            if (f) { this._setCurrent(f); this.zoomToFrame(f); }
            else if (this.wg && this.wg.shared) { const it = this.wg.shared.nodes.find((n) => key === n.kind + ":" + n.name); if (it) this._animateTo(it.x + SW / 2, it.y, Math.max(this.view.k, 0.8)); }
            return;
        }
        this.mode = "slot"; this._bar(); this.setBrain(this.brain, key); this._save();
    }
    setMode(m, key) {
        if (this.mode === "whole") this.wholeView = { ...this.view };
        this.mode = m;
        this._bar();
        this.setBrain(this.brain, key);
        if (m === "whole" && this.wholeView) { this.view = { ...this.wholeView }; this._applyView(); }
        else requestAnimationFrame(() => this.fit());
        this._save();
    }

    _annotate(d, g) {
        const cat = d && d.kind === "slot" ? CATALOG.slots[d.name] : null;
        for (const n of g.nodes) if (n.op === "return") n._slotOuts = cat ? Object.keys(cat.outputs) : null;
    }

    _changed() {
        const d = fromGraph(this.g);
        const brain = { ...this.brain, decls: this.brain.decls.map((x) => (declKey(x) === this.key ? d : x)) };
        this.brain = brain;
        this.S = symbols(brain);
        this.app.brainEdited(brain, "graph");
        if (this.mode === "whole" && this.fr) {
            this._annotate(d, this.g);
            this._renderFrameBody(this.fr);
            this._refitFrame(this.fr);
            if (this.sharedOn) { this.wg.shared = sharedInputs(brain, this.wg.frames); wholeBounds(this.wg); this._renderShared(); }
            this._side();
            this._miniDraw();
            return;
        }
        this.render();
    }

    _bar() {
        const b = this.bar;
        b.textContent = "";
        const btn = (t, f, on, title) => { const e = document.createElement("button"); e.textContent = t; if (on) e.className = "on"; e.onclick = f; if (title) e.title = title; b.append(e); return e; };
        btn("Whole brain", () => this.setMode("whole"), this.mode === "whole", "every slot's graph on one canvas, by area");
        btn("One slot", () => this.setMode("slot"), this.mode === "slot");
        btn("Overview (who uses what)", () => this.setMode("overview"), this.mode === "overview");
        if (this.mode === "slot") {
            const crumb = document.createElement("span"); crumb.className = "bk-crumb";
            const a = document.createElement("a"); a.href = "#"; a.textContent = "Whole brain";
            a.onclick = (e) => { e.preventDefault(); this.setMode("whole"); if (this.key) requestAnimationFrame(() => this.select(this.key)); };
            const sep = document.createElement("span"); sep.className = "muted"; sep.textContent = " › ";
            const t = document.createElement("b"); t.textContent = this.key ? this.key.replace(/^slot:/, "slot ").replace(/^curve:/, "curve ") : "";
            crumb.append(a, sep, t);
            b.append(crumb);
        }
        if (this.mode === "whole") {
            const lab = document.createElement("label"); lab.className = "gr-check";
            const cb = document.createElement("input"); cb.type = "checkbox"; cb.checked = this.sharedOn;
            cb.onchange = () => { this.sharedOn = cb.checked; this.wg.shared = this.sharedOn ? sharedInputs(this.brain, this.wg.frames) : null; wholeBounds(this.wg); this.render(); this._save(); };
            lab.append(cb, document.createTextNode(" show shared inputs"));
            lab.title = "params, curves, lists, tables and sensors read by several slots as nodes, with an edge to every use";
            b.append(lab);
            const inp = document.createElement("input"); inp.className = "gr-search"; inp.placeholder = "search slots and nodes…"; inp.value = this.search.q;
            inp.oninput = () => this._search(inp.value, true);
            inp.onkeydown = (e) => {
                if (e.key === "Enter") { e.preventDefault(); this._searchStep(e.shiftKey ? -1 : 1); }
                else if (e.key === "Escape") { inp.value = ""; this._search(""); }
            };
            this.searchEl = inp;
            b.append(inp);
            this.searchCount = document.createElement("span"); this.searchCount.className = "muted gr-count";
            b.append(this.searchCount);
        }
        if (this.mode !== "overview") btn("Auto layout", () => this._autoLayout(), false, "lay the nodes out again");
        btn("Fit all", () => this.fit(), false, "zoom to show everything");
        if (this.mode !== "overview") {
            const sel = document.createElement("select");
            sel.className = "gr-add";
            const groups = { "Add node…": [], Logic: ["if", "and", "or", "not", "<", ">", "==", "?:"], Maths: ["+", "-", "*", "/", "num", "min", "max", "clamp"], Random: ["roll", "gauss", "chance"],
                Statements: ["set", "return", "goto", "fallback", "pass"], Names: ["name"] };
            for (const [g, ops] of Object.entries(groups)) {
                const og = document.createElement("optgroup"); og.label = g;
                if (!ops.length) { const o = document.createElement("option"); o.textContent = g; o.value = ""; sel.append(o); continue; }
                for (const op of ops) { const o = document.createElement("option"); o.textContent = op; o.value = op; og.append(o); }
                sel.append(og);
            }
            sel.title = this.mode === "whole" ? "adds to the slot you last clicked in" : "";
            sel.onchange = () => { if (sel.value) this._addNode(sel.value); sel.value = ""; };
            b.append(sel);
            const hint = document.createElement("span"); hint.className = "muted";
            hint.textContent = this.mode === "whole" ? "wheel zooms · drag pans · double-click a slot's title to open it alone" : "drag a port to another to connect · Alt+click an input to cut · Del removes the selected node";
            b.append(hint);
        }
        this.mini.style.display = this.mode === "whole" ? "" : "none";
    }
    _autoLayout() {
        if (this.mode === "whole") {
            for (const f of this.wg.frames) f.g.nodes.forEach((n) => (n.moved = false));
            this.wg = null;
            this.setBrain(this.brain);
            return;
        }
        if (this.g) { this.g.nodes.forEach((n) => (n.moved = false)); layoutGraph(this.g); this.render(); }
    }

    _addNode(op) {
        if (!this.g) { if (this.app.app && this.app.app.toast) this.app.app.toast("click inside a slot first: the new node goes there", 3000); return; }
        let node;
        const stmtOps = ["if", "set", "return", "goto", "fallback", "pass", "seek", "evade"];
        let at = this._toWorld(this.svgEl.clientWidth / 2, this.svgEl.clientHeight / 2);
        if (this.mode === "whole" && this.fr) {
            const f = this.fr;
            at = { x: Math.max(f.x, Math.min(f.x + f.w - W, at.x)) - f.x - f.dx, y: Math.max(f.y, Math.min(f.y + f.h - 40, at.y)) - f.y - f.dy };
        }
        const gid = "g" + ++this.g.next;
        if (op === "name") node = { gid, kind: "source", cat: "expr", op: "name", fields: { name: "fov" }, ins: [], outs: [] };
        else {
            const ast = stmtOps.includes(op) ? newStmt(op) : ["roll", "gauss", "chance", "min", "max", "clamp"].includes(op) ? newExpr("call", { fn: op }) : newExpr(op);
            const p = decompose(ast);
            node = { gid, kind: p.cat === "stmt" ? "stmt" : "expr", cat: p.cat, op: p.op, fields: p.fields, ins: p.inputs.map(([k]) => k), outs: p.cat === "stmt" ? [...p.stacks.map(([k]) => k), "next"] : [] };
        }
        node.x = at.x; node.y = at.y; node.moved = true;
        this.g.nodes.push(node);
        this.sel = node.gid;
        if (this.mode === "whole") { this._renderFrameBody(this.fr); this._side(); } else this.render();
    }

    fit() {
        const w = this.svgEl.clientWidth || 800, h = this.svgEl.clientHeight || 600;
        if (this.mode === "whole") {
            if (!this.wg) return;
            const b = this.wg.bounds;
            this.view.k = Math.max(KMIN, Math.min(1.2, Math.min((w - 40) / b.w, (h - 40) / b.h)));
            this.view.x = -b.x * this.view.k + (w - b.w * this.view.k) / 2; this.view.y = -b.y * this.view.k + 20;
            this._applyView();
            return;
        }
        const ns = this.mode === "overview" ? this.ov && this.ov.nodes : this.g && this.g.nodes;
        if (!ns || !ns.length) return;
        let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (const n of ns) { x0 = Math.min(x0, n.x); y0 = Math.min(y0, n.y); x1 = Math.max(x1, n.x + W); y1 = Math.max(y1, n.y + (n.ins ? nodeHeight(n) : 24)); }
        this.view.k = Math.max(0.2, Math.min(1.2, Math.min(w / (x1 - x0 + 80), h / (y1 - y0 + 80))));
        this.view.x = -x0 * this.view.k + 40; this.view.y = -y0 * this.view.k + 30;
        this._applyView();
    }
    zoomToFrame(f) {
        const w = this.svgEl.clientWidth || 800, h = this.svgEl.clientHeight || 600;
        const k = Math.max(KMIN, Math.min(1.1, Math.min((w - 60) / f.w, (h - 60) / f.h)));
        this._animateTo(f.x + f.w / 2, f.y + Math.min(f.h, h / k) / 2 - 10, k);
    }
    // fly to a world point at zoom k (a short eased animation)
    _animateTo(cx, cy, k) {
        const w = this.svgEl.clientWidth || 800, h = this.svgEl.clientHeight || 600;
        const from = { ...this.view }, to = { k, x: w / 2 - cx * k, y: h / 2 - cy * k };
        const t0 = performance.now(), T = 280;
        cancelAnimationFrame(this._anim);
        const step = (now) => {
            const t = Math.min(1, (now - t0) / T), e = 1 - Math.pow(1 - t, 3);
            const kk = from.k * Math.pow(to.k / from.k, e);
            // interpolate the screen position of the target point, so it glides to the middle
            const sx0 = from.x + cx * from.k, sy0 = from.y + cy * from.k;
            const sx = sx0 + (w / 2 - sx0) * e, sy = sy0 + (h / 2 - sy0) * e;
            this.view = { k: kk, x: sx - cx * kk, y: sy - cy * kk };
            this._applyView();
            if (t < 1) this._anim = requestAnimationFrame(step); else this._save();
        };
        this._anim = requestAnimationFrame(step);
    }
    _applyView() {
        this.layer.setAttribute("transform", `translate(${this.view.x},${this.view.y}) scale(${this.view.k})`);
        if (this.mode === "whole") {
            this.wholeView = { ...this.view };
            this._lod();
            this._cull();
            if (!this._miniQ) this._miniQ = requestAnimationFrame(() => { this._miniQ = 0; this._miniDraw(); });
        }
    }
    _toWorld(px, py) { return { x: (px - this.view.x) / this.view.k, y: (py - this.view.y) / this.view.k }; }

    // level of detail: port labels go below LOD_LABELS, frame contents below LOD_FAR (titles and bundles only)
    _lod() {
        const k = this.view.k, lvl = k < LOD_FAR ? 2 : k < LOD_LABELS ? 1 : 0;
        // zoomed out, titles keep a readable size on screen: set on the ~70 title texts only (a CSS variable on the
        // svg would restyle every node, measured at 33-50 ms a zoom step)
        if (lvl === 2 && this.wg) {
            for (const f of this.wg.frames) if (f.farTitle) { f.farTitle.style.fontSize = Math.min(13 / k, f.fmax) + "px"; f.sub.style.fontSize = Math.min(10 / k, f.fmax * 0.75) + "px"; }
            for (const t of this._ctitles || []) t.style.fontSize = Math.min(15 / k, 150) + "px";
        } else if (this._lodLevel === 2 && this._ctitles) for (const t of this._ctitles) t.style.fontSize = "";
        if (lvl === this._lodLevel) return;
        this._lodLevel = lvl;
        this.svgEl.classList.toggle("lod1", lvl >= 1);
        this.svgEl.classList.toggle("lod2", lvl === 2);
    }
    // frames off screen are not drawn (display none: no paint, no hit test)
    _cull() {
        if (!this.wg) return;
        const w = this.svgEl.clientWidth || 800, h = this.svgEl.clientHeight || 600;
        const a = this._toWorld(-100, -100), b = this._toWorld(w + 100, h + 100);
        for (const f of this.wg.frames) {
            const vis = f.x < b.x && f.x + f.w > a.x && f.y < b.y && f.y + f.h > a.y;
            if (vis !== f._vis && f.el) { f._vis = vis; f.el.style.display = vis ? "" : "none"; }
        }
    }

    // ---- rendering
    render() {
        const L = this.layer;
        L.textContent = "";
        this._lodLevel = -1;
        this.svgEl.classList.remove("lod1", "lod2");
        this._applyView();
        if (this.mode === "whole") return this._renderWhole();
        if (this.mode === "overview") return this._renderOverview();
        if (!this.g) { const t = svg("text", { x: 0, y: 20, class: "gr-muted" }, L); t.textContent = "Pick a slot or an expression curve on the left (steps / points curves are edited in Values)."; this._side(); return; }
        this.portPos = new Map();
        const edgesL = svg("g", { class: "gr-edges" }, L);
        const nodesL = svg("g", {}, L);
        for (const n of this.g.nodes) this._renderNode(n, nodesL);
        this._renderEdges(this.g, edgesL);
        if (this._wire) svg("path", { d: this._wire, class: "gr-edge wire" }, L);
        this._side();
    }
    _renderEdges(g, parent) {
        const byId = new Map(g.nodes.map((n) => [n.gid, n]));
        for (const e of g.edges) {
            const a = byId.get(e.from), b = byId.get(e.to);
            if (!a || !b) continue;
            let p0, p1, d;
            if (e.kind === "data") {
                p0 = this.portPos.get(a.gid + ">out"); p1 = this.portPos.get(b.gid + "<" + e.port);
                if (!p0 || !p1) continue;
                const dx = Math.max(40, Math.abs(p1.x - p0.x) * 0.5);
                d = `M${p0.x},${p0.y} C${p0.x + dx},${p0.y} ${p1.x - dx},${p1.y} ${p1.x},${p1.y}`;
            } else {
                p0 = this.portPos.get(a.gid + "}" + e.port); p1 = this.portPos.get(b.gid + "{");
                if (!p0 || !p1) continue;
                const dy = Math.max(30, Math.abs(p1.y - p0.y) * 0.4);
                d = e.port === "next" ? `M${p0.x},${p0.y} C${p0.x},${p0.y + dy} ${p1.x},${p1.y - dy} ${p1.x},${p1.y}` : `M${p0.x},${p0.y} C${p0.x + 60},${p0.y} ${p1.x - 10},${p1.y - dy} ${p1.x},${p1.y}`;
            }
            const sel = this.sel && g === this.g && (e.from === this.sel || e.to === this.sel);
            svg("path", { d, class: `gr-edge ${e.kind}${sel ? " sel" : ""}` }, parent);
        }
    }

    _renderWhole() {
        const L = this.layer, wg = this.wg;
        if (!wg) return;
        const cl = svg("g", { class: "gr-clusters" }, L);
        this._ctitles = [];
        for (const c of wg.clusters) {
            const g = svg("g", { transform: `translate(${c.x},${c.y})`, style: `--ac:${AREA_COLOR[c.area] || "#6b7d93"}` }, cl);
            svg("rect", { width: c.w, height: c.h, rx: 18, class: "gr-cluster" }, g);
            const t = svg("text", { x: 22, y: CTITLE - 14, class: "gr-ctitle" }, g); t.textContent = c.label; this._ctitles.push(t);
        }
        this.sharedEdgesL = svg("g", { class: "gr-sedges" }, L);
        const fl = svg("g", { class: "gr-frames" }, L);
        for (const f of wg.frames) { f._vis = true; fl.append(this._frameEl(f)); }
        this.sharedNodesL = svg("g", { class: "gr-snodes" }, L);
        this._renderShared();
        this._lod();
        this._cull();
        this._applySearch();
        this._side();
        this._miniDraw();
    }
    _frameEl(f) {
        const g = svg("g", { class: "gr-frame" + (f === this.fr ? " cur" : ""), transform: `translate(${f.x},${f.y})`, style: `--ac:${AREA_COLOR[f.area] || "#6b7d93"}` });
        g._frame = f; f.el = g;
        f.box = svg("rect", { width: f.w, height: f.h, rx: 10, class: "gr-fbox" }, g);
        const t = svg("text", { x: 12, y: 21, class: "gr-ftitle" }, g); t.textContent = f.title;
        f.fmax = Math.max(10, Math.min(f.h * 0.5, (f.w - 16) / (f.title.length * 0.62)));
        f.farTitle = svg("text", { x: f.w / 2, y: f.h / 2, class: "gr-ftitle-far", "text-anchor": "middle", "dominant-baseline": "middle" }, g);
        f.farTitle.textContent = f.title;
        const sub = svg("text", { x: f.w / 2, y: f.h / 2, dy: "1.5em", class: "gr-fsub", "text-anchor": "middle" }, g);
        sub.textContent = `${f.g.nodes.length} nodes`;
        f.sub = sub;
        f.body = svg("g", { class: "gr-fbody", transform: `translate(${f.dx},${f.dy})` }, g);
        this._renderFrameBody(f);
        return g;
    }
    _renderFrameBody(f) {
        const body = f.body;
        body.textContent = "";
        const keepPP = this.portPos;
        this.portPos = f.portPos = new Map();
        f.nodeEls = new Map();
        const edgesL = svg("g", { class: "gr-edges" }, body);
        const nodesL = svg("g", {}, body);
        for (const n of f.g.nodes) f.nodeEls.set(n.gid, this._renderNode(n, nodesL, true, f.g === this.g));
        this._renderEdges(f.g, edgesL);
        if (this._wire && this._wireFrame === f) svg("path", { d: this._wire, class: "gr-edge wire" }, body);
        if (this.search.q) for (const h of this.search.hits) if (h.f === f && h.gid && f.nodeEls.get(h.gid)) f.nodeEls.get(h.gid).classList.add("hit");
        if (keepPP && f !== this.fr) this.portPos = keepPP;
        if (f === this.fr) this.portPos = f.portPos;
    }
    _refitFrame(f) {
        refitFrame(f);
        f.el.setAttribute("transform", `translate(${f.x},${f.y})`);
        f.box.setAttribute("width", f.w); f.box.setAttribute("height", f.h);
        f.body.setAttribute("transform", `translate(${f.dx},${f.dy})`);
        f.fmax = Math.max(10, Math.min(f.h * 0.5, (f.w - 16) / (f.title.length * 0.62)));
        f.farTitle.setAttribute("x", f.w / 2); f.farTitle.setAttribute("y", f.h / 2);
        f.sub.setAttribute("x", f.w / 2); f.sub.setAttribute("y", f.h / 2); f.sub.textContent = `${f.g.nodes.length} nodes`;
        wholeBounds(this.wg);
    }
    // shared inputs: nodes in a column, an edge to each use (near) and one bundle per frame (far)
    _renderShared() {
        const E = this.sharedEdgesL, N = this.sharedNodesL;
        if (!E) return;
        E.textContent = ""; N.textContent = "";
        const sh = this.wg.shared;
        if (!sh) return;
        const frames = this.wg.frames;
        const near = svg("g", { class: "gr-snear" }, E), far = svg("g", { class: "gr-sfar" }, E);
        for (const it of sh.nodes) {
            const x0 = it.x + SW, y0 = it.y + PILL_H / 2;
            const hl = this.sharedSel === it.id ? " sel" : "";
            for (const u of it.uses) {
                const f = frames[u.f], n = f.g.nodes.find((x) => x.gid === u.gid);
                if (!n) continue;
                const x1 = f.x + f.dx + n.x, y1 = f.y + f.dy + n.y + (n.kind === "source" ? PILL_H / 2 : 12);
                const dx = Math.max(80, (x1 - x0) * 0.45);
                svg("path", { d: `M${x0},${y0} C${x0 + dx},${y0} ${x1 - dx},${y1} ${x1},${y1}`, class: `gr-sedge c-${SKIND_CLASS[it.kind]}${hl}` }, near);
            }
            for (const fi of it.frames) {
                const f = frames[fi], x1 = f.x, y1 = f.y + 16;
                const dx = Math.max(120, (x1 - x0) * 0.45);
                svg("path", { d: `M${x0},${y0} C${x0 + dx},${y0} ${x1 - dx},${y1} ${x1},${y1}`, class: `gr-bundle c-${SKIND_CLASS[it.kind]}${hl}` }, far);
            }
            const g = svg("g", { class: `gr-snode c-${SKIND_CLASS[it.kind]}${hl}`, transform: `translate(${it.x},${it.y})` }, N);
            g._shared = it;
            svg("rect", { width: SW, height: PILL_H, rx: PILL_H / 2, class: "gr-pillbox" }, g);
            const t = svg("text", { x: 10, y: 15, class: "gr-pilltext" }, g);
            t.textContent = (it.kind === "param" ? "" : it.kind + " ") + it.name + (it.kind === "curve" ? "()" : "");
            const c = svg("title", {}, g); c.textContent = `${it.kind} ${it.name}: used ${it.uses.length}× in ${it.frames.length} slot${it.frames.length === 1 ? "" : "s"}`;
            if (this.search.q && this.search.hits.some((h) => h.shared === it)) g.classList.add("hit");
        }
    }

    _renderNode(n, parent, whole = false, selectable = true) {
        if (whole && n.kind === "source") {   // a compact pill
            const pw = pillWidth(n.fields.name);
            const g = svg("g", { class: `gr-node gr-pill c-${CLASS(n, this.S)}${selectable && this.sel === n.gid ? " sel" : ""}`, transform: `translate(${n.x},${n.y})` }, parent);
            g._node = n;
            svg("rect", { width: pw, height: PILL_H, rx: PILL_H / 2, class: "gr-body" }, g);
            const t = svg("text", { x: 9, y: 15, class: "gr-pilltext" }, g); t.textContent = n.fields.name;
            const c = svg("circle", { cx: pw, cy: PILL_H / 2, r: 4.5, class: "gr-port out" }, g);
            c._port = { node: n, dir: "out" };
            this.portPos.set(n.gid + ">out", { x: n.x + pw, y: n.y + PILL_H / 2 });
            return g;
        }
        const h = nodeHeight(n);
        const g = svg("g", { class: `gr-node c-${CLASS(n, this.S)}${selectable && this.sel === n.gid ? " sel" : ""}`, transform: `translate(${n.x},${n.y})` }, parent);
        g._node = n;
        svg("rect", { width: W, height: h, rx: 6, class: "gr-body" }, g);
        svg("rect", { width: W, height: HEAD, rx: 6, class: "gr-head" }, g);
        const title = svg("text", { x: 10, y: 16, class: "gr-title" }, g);
        let t = nodeTitle(n);
        if (t.length > 24) t = t.slice(0, 23) + "…";
        title.textContent = t;
        if (n.fields.trail || (n.fields.comments && n.fields.comments.some((c) => c))) { const c = svg("title", {}, g); c.textContent = [...(n.fields.comments || []), n.fields.trail || ""].filter(Boolean).join("\n"); }
        // exec in (statements)
        if (n.kind === "stmt") { svg("path", { d: "M-2,4 L8,12 L-2,20 Z", class: "gr-exec" }, g); this.portPos.set(n.gid + "{", { x: n.x + 3, y: n.y + 2 }); }
        // data inputs
        n.ins.forEach((k, i) => {
            const y = HEAD + i * LINE + 12;
            const c = svg("circle", { cx: 0, cy: y, r: 5, class: "gr-port in" }, g);
            c._port = { node: n, key: k, dir: "in" };
            const tx = svg("text", { x: 10, y: y + 4, class: "gr-plabel" }, g); tx.textContent = PORT_LABEL(n, k);
            this.portPos.set(n.gid + "<" + k, { x: n.x, y: n.y + y });
        });
        // data output (expressions, sources)
        if (n.kind === "expr" || n.kind === "source") {
            const c = svg("circle", { cx: W, cy: 12, r: 5, class: "gr-port out" }, g);
            c._port = { node: n, dir: "out" };
            this.portPos.set(n.gid + ">out", { x: n.x + W, y: n.y + 12 });
        }
        // exec outputs
        const stacks = n.outs.filter((k) => k !== "next");
        stacks.forEach((k, i) => {
            const y = HEAD + i * LINE + 12;
            const p = svg("path", { d: `M${W - 8},${y - 6} L${W + 2},${y} L${W - 8},${y + 6} Z`, class: "gr-exec out" }, g);
            p._port = { node: n, key: k, dir: "exec" };
            const tx = svg("text", { x: W - 12, y: y + 4, class: "gr-plabel", "text-anchor": "end" }, g); tx.textContent = PORT_LABEL(n, k);
            this.portPos.set(n.gid + "}" + k, { x: n.x + W, y: n.y + y });
        });
        if (n.outs.includes("next")) {
            const p = svg("path", { d: `M${W / 2 - 6},${h - 2} L${W / 2},${h + 8} L${W / 2 + 6},${h - 2} Z`, class: "gr-exec out" }, g);
            p._port = { node: n, key: "next", dir: "exec" };
            this.portPos.set(n.gid + "}next", { x: n.x + W / 2, y: n.y + h + 6 });
        }
        return g;
    }

    _renderOverview() {
        const L = this.layer, ov = this.ov;
        const by = new Map(ov.nodes.map((n) => [n.gid, n]));
        const eg = svg("g", { class: "gr-edges" }, L);
        for (const e of ov.edges) {
            const a = by.get(e.from), b = by.get(e.to);
            const hl = this.ovSel && (this.ovSel === a.gid || this.ovSel === b.gid);
            const x0 = a.x + 150, y0 = a.y + 10, x1 = b.x, y1 = b.y + 10;
            svg("path", { d: `M${x0},${y0} C${x0 + 80},${y0} ${x1 - 80},${y1} ${x1},${y1}`, class: "gr-edge ov" + (hl ? " sel" : "") }, eg);
        }
        for (const n of ov.nodes) {
            const g = svg("g", { class: `gr-ov c-${n.kind === "param" ? "param" : n.kind === "slot" ? "hat" : n.kind === "curve" ? "curve" : "data"}${this.ovSel === n.gid ? " sel" : ""}`, transform: `translate(${n.x},${n.y})` }, L);
            svg("rect", { width: 150, height: 20, rx: 4 }, g);
            const t = svg("text", { x: 8, y: 14 }, g); t.textContent = n.label;
            g._ov = n;
        }
        this.side.textContent = "";
        const p = document.createElement("div"); p.className = "muted pad";
        p.textContent = "Params (left) and curves / lists / tables (middle) feed the slots (right) that read them. Click one to highlight its links; double-click a slot to open its graph.";
        this.side.append(p);
    }

    // ---- search (whole mode): slot titles, node titles, shared inputs; Enter steps through, each one zoomed to
    _search(q, jump = false) {
        this.search = { q: q.trim().toLowerCase(), hits: [], i: -1 };
        const s = this.search;
        if (s.q && this.wg) {
            for (const f of this.wg.frames) if (f.title.toLowerCase().includes(s.q) || f.key.toLowerCase().includes(s.q)) s.hits.push({ f });
            for (const f of this.wg.frames) for (const n of f.g.nodes) if (n.kind !== "entry" && nodeTitle(n).toLowerCase().includes(s.q)) s.hits.push({ f, gid: n.gid });
            if (this.wg.shared) for (const it of this.wg.shared.nodes) if (it.name.toLowerCase().includes(s.q)) s.hits.push({ shared: it });
        }
        this._applySearch();
        if (jump && s.hits.length) this._searchStep(1);
    }
    _searchStep(d) {
        const s = this.search;
        if (!s.hits.length) return;
        s.i = (s.i + d + s.hits.length) % s.hits.length;
        const h = s.hits[s.i];
        this._applySearch();
        if (h.shared) this._animateTo(h.shared.x + SW / 2, h.shared.y + PILL_H / 2, Math.max(0.9, this.view.k));
        else if (h.gid) {
            const n = h.f.g.nodes.find((x) => x.gid === h.gid);
            this._setCurrent(h.f);
            this.sel = h.gid; this._renderFrameBody(h.f); this._side();
            this._animateTo(h.f.x + h.f.dx + n.x + W / 2, h.f.y + h.f.dy + n.y + 20, 1.1);
        } else { this._setCurrent(h.f); this.zoomToFrame(h.f); }
    }
    _applySearch() {
        if (!this.wg) return;
        const s = this.search;
        for (const e of this.layer.querySelectorAll(".hit, .hit-cur")) e.classList.remove("hit", "hit-cur");
        if (this.searchCount) this.searchCount.textContent = s.q ? (s.hits.length ? `${s.i >= 0 ? s.i + 1 : 0} / ${s.hits.length}` : "no match") : "";
        this.svgEl.classList.toggle("searching", !!s.q && s.hits.length > 0);
        s.hits.forEach((h, i) => {
            let e = null;
            if (h.shared) e = this.sharedNodesL && [...this.sharedNodesL.children].find((x) => x._shared === h.shared);
            else if (h.gid) { e = h.f.nodeEls && h.f.nodeEls.get(h.gid); if (h.f.el) h.f.el.classList.add("hit"); }
            else e = h.f.el;
            if (e) e.classList.add("hit", ...(i === s.i ? ["hit-cur"] : []));
        });
    }
    _setCurrent(f) {
        if (this.fr === f) return;
        const old = this.fr;
        this.fr = f; this.g = f.g; this.key = f.key; this.sel = null;
        if (old && old.el) { old.el.classList.remove("cur"); this._renderFrameBody(old); }
        if (f.el) f.el.classList.add("cur");
        this.portPos = f.portPos;
        this._save();
    }

    // ---- mini-map (whole mode)
    _miniDraw() {
        const cv = this.mini;
        if (this.mode !== "whole" || !this.wg) return;
        const Wc = cv.clientWidth || 200, Hc = cv.clientHeight || 140, dpr = window.devicePixelRatio || 1;
        if (cv.width !== Math.round(Wc * dpr)) { cv.width = Math.round(Wc * dpr); cv.height = Math.round(Hc * dpr); }
        const ctx = cv.getContext("2d");
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx.clearRect(0, 0, Wc, Hc);
        const b = this.wg.bounds, s = Math.min((Wc - 8) / b.w, (Hc - 8) / b.h);
        const ox = (Wc - b.w * s) / 2 - b.x * s, oy = (Hc - b.h * s) / 2 - b.y * s;
        this._miniT = { s, ox, oy };
        ctx.fillStyle = "rgba(11,15,20,.85)"; ctx.fillRect(0, 0, Wc, Hc);
        for (const c of this.wg.clusters) { ctx.strokeStyle = AREA_COLOR[c.area] || "#6b7d93"; ctx.globalAlpha = 0.5; ctx.strokeRect(ox + c.x * s, oy + c.y * s, c.w * s, c.h * s); }
        for (const f of this.wg.frames) {
            ctx.globalAlpha = f === this.fr ? 1 : 0.75;
            ctx.fillStyle = AREA_COLOR[f.area] || "#6b7d93";
            ctx.fillRect(ox + f.x * s, oy + f.y * s, Math.max(1, f.w * s), Math.max(1, f.h * s));
        }
        if (this.search.q) { ctx.globalAlpha = 1; ctx.fillStyle = "#fbbf24"; for (const h of this.search.hits) { if (h.f) ctx.fillRect(ox + (h.f.x + h.f.w / 2) * s - 1.5, oy + (h.f.y + h.f.h / 2) * s - 1.5, 3, 3); } }
        if (this.wg.shared) { ctx.globalAlpha = 0.8; ctx.fillStyle = "#3b6fd1"; for (const n of this.wg.shared.nodes) ctx.fillRect(ox + n.x * s, oy + n.y * s, Math.max(1, SW * s), 1); }
        ctx.globalAlpha = 1;
        const w = this.svgEl.clientWidth || 800, h = this.svgEl.clientHeight || 600;
        const a = this._toWorld(0, 0), z = this._toWorld(w, h);
        ctx.strokeStyle = "#fbbf24"; ctx.lineWidth = 1.5;
        ctx.strokeRect(ox + a.x * s, oy + a.y * s, (z.x - a.x) * s, (z.y - a.y) * s);
    }
    _miniInput() {
        const go = (e) => {
            const T = this._miniT;
            if (!T) return;
            const r = this.mini.getBoundingClientRect();
            const wx = (e.clientX - r.left - T.ox) / T.s, wy = (e.clientY - r.top - T.oy) / T.s;
            const w = this.svgEl.clientWidth || 800, h = this.svgEl.clientHeight || 600;
            this.view.x = w / 2 - wx * this.view.k; this.view.y = h / 2 - wy * this.view.k;
            this._applyView();
        };
        this.mini.addEventListener("pointerdown", (e) => { e.stopPropagation(); this.mini.setPointerCapture(e.pointerId); this._miniDrag = true; go(e); });
        this.mini.addEventListener("pointermove", (e) => { if (this._miniDrag) go(e); });
        this.mini.addEventListener("pointerup", () => { if (this._miniDrag) { this._miniDrag = false; this._save(); } });
        this.mini.addEventListener("wheel", (e) => e.stopPropagation());
    }

    // ---- inspector
    _side() {
        const s = this.side;
        s.textContent = "";
        const n = this.g && this.sel && this.g.nodes.find((x) => x.gid === this.sel);
        if (!n) {
            const p = document.createElement("div"); p.className = "muted pad";
            if (this.mode === "whole") {
                p.textContent = (this.fr ? `In ${this.fr.title}. ` : "") + "Click a node to edit it. Zoom in for the ports (wheel); zoomed out you see each slot as a frame. Exec edges (white) give the order of the statements; data edges (coloured) carry values into ports.";
                s.append(p);
                if (this.sharedOn && this.sharedSel) {
                    const it = this.wg.shared.nodes.find((x) => x.id === this.sharedSel);
                    if (it) {
                        const h = document.createElement("h4"); h.textContent = `${it.kind} ${it.name}`; s.append(h);
                        for (const fi of it.frames) { const f = this.wg.frames[fi]; const b = document.createElement("div"); b.className = "gr-use"; b.textContent = `${f.title} (${it.uses.filter((u) => u.f === fi).length}×)`; b.onclick = () => { this._setCurrent(f); this.zoomToFrame(f); }; s.append(b); }
                    }
                }
                return;
            }
            p.textContent = "Select a node to edit it. Exec edges (white) give the order of the statements; data edges (coloured) carry values into ports.";
            s.append(p);
            return;
        }
        const h = document.createElement("h4"); h.textContent = nodeTitle(n) + (this.mode === "whole" && this.fr ? `  (${this.fr.title})` : ""); s.append(h);
        const row = (label, input) => { const r = document.createElement("label"); r.className = "gr-field"; const sp = document.createElement("span"); sp.textContent = label; r.append(sp, input); s.append(r); };
        const text = (v, on) => { const i = document.createElement("input"); i.value = v ?? ""; i.onchange = () => on(i.value); return i; };
        const choose = (opts, v, on) => { const i = document.createElement("select"); for (const o of opts) { const e = document.createElement("option"); e.textContent = o; e.value = o; if (o === v) e.selected = true; i.append(e); } i.onchange = () => on(i.value); return i; };
        const f = n.fields;
        const done = () => this._changed();
        if (n.kind === "source") row("name", text(f.name, (v) => { if (/^[A-Za-z_][A-Za-z0-9_.]*$/.test(v)) { f.name = v; done(); } }));
        else if (n.op === "num") { row("value", text(String(f.v), (v) => { if (Number.isFinite(+v)) { f.v = roundTo(+v, f.type); done(); } })); row("type", choose(["f32", "f64", "int"], f.type, (v) => { f.type = v; done(); })); }
        else if (n.op === "bool") row("value", choose(["yes", "no"], f.v ? "yes" : "no", (v) => { f.v = v === "yes"; done(); }));
        else if (["+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!=", "and", "or"].includes(n.op)) row("operator", choose(["+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!=", "and", "or"], n.op, (v) => { n.op = v; done(); }));
        else if (n.op === "call") row("function", text(f.fn, (v) => { f.fn = v; done(); }));
        else if (n.op === "set") row("sets", text(f.name, (v) => { f.name = v; done(); }));
        else if (n.op === "in") row("list", choose([...this.S.lists.keys()], f.list, (v) => { f.list = v; done(); }));
        else if (n.op === "index") row("table", choose([...this.S.tables.keys()], f.table, (v) => { f.table = v; done(); }));
        if (n.kind === "stmt" || n.kind === "entry") row("comment", text(f.trail ? f.trail.replace(/^#\s?/, "") : "", (v) => { if (v.trim()) f.trail = "# " + v.trim(); else delete f.trail; done(); }));
        if (n.kind !== "entry") {
            const del = document.createElement("button"); del.textContent = "Delete node"; del.className = "danger";
            del.onclick = () => this._delete(n);
            s.append(del);
        }
        if (n.kind === "source") {
            const uses = this.g.edges.filter((e) => e.from === n.gid).length;
            const p = document.createElement("div"); p.className = "muted pad"; p.textContent = `used ${uses} time${uses === 1 ? "" : "s"} in this slot`;
            s.append(p);
        }
    }

    _delete(n) {
        // a statement's exec chain closes over the gap: prev -> next
        const inE = this.g.edges.find((e) => e.kind === "exec" && e.to === n.gid);
        const outE = this.g.edges.find((e) => e.kind === "exec" && e.from === n.gid && e.port === "next");
        this.g.edges = this.g.edges.filter((e) => e.from !== n.gid && e.to !== n.gid);
        if (inE && outE) this.g.edges.push({ kind: "exec", from: inE.from, port: inE.port, to: outE.to });
        this.g.nodes = this.g.nodes.filter((x) => x !== n);
        this.sel = null;
        this._changed();
    }

    // the selected node / wire changed: redraw only what shows it
    _redraw() { if (this.mode === "whole") { if (this.fr) this._renderFrameBody(this.fr); this._side(); } else this.render(); }

    // ---- input
    _input() {
        const el = this.svgEl;
        el.addEventListener("wheel", (e) => {
            e.preventDefault();
            const r = el.getBoundingClientRect(), px = e.clientX - r.left, py = e.clientY - r.top;
            const w = this._toWorld(px, py);
            const kmin = this.mode === "whole" ? KMIN : 0.15;
            cancelAnimationFrame(this._anim);
            this.view.k = Math.max(kmin, Math.min(KMAX, this.view.k * Math.exp(-e.deltaY * 0.0015)));
            this.view.x = px - w.x * this.view.k; this.view.y = py - w.y * this.view.k;
            this._applyView();
            this._save();
        }, { passive: false });
        el.addEventListener("pointerdown", (e) => {
            const r = el.getBoundingClientRect(), px = e.clientX - r.left, py = e.clientY - r.top;
            const port = e.target._port;
            const ng = e.target.closest && e.target.closest(".gr-node");
            const ov = e.target.closest && e.target.closest(".gr-ov");
            const sn = e.target.closest && e.target.closest(".gr-snode");
            const fe = e.target.closest && e.target.closest(".gr-frame");
            cancelAnimationFrame(this._anim);
            this._downTarget = e.target;   // (with the pointer captured, the dblclick's target is the svg)
            el.setPointerCapture(e.pointerId);
            if (ov) { this.ovSel = ov._ov.gid; this.render(); return; }
            if (sn) { this.sharedSel = this.sharedSel === sn._shared.id ? null : sn._shared.id; this._renderShared(); this.sel = null; this._side(); this._drag = { kind: "pan", sx: px, sy: py, x: this.view.x, y: this.view.y }; return; }
            if (this.mode === "whole" && fe && this._lodLevel < 2) this._setCurrent(fe._frame);
            if (port && port.dir === "in" && e.altKey) {
                this.g.edges = this.g.edges.filter((x) => !(x.kind === "data" && x.to === port.node.gid && x.port === port.key));
                this._changed(); return;
            }
            if (port && (port.dir === "out" || port.dir === "exec")) { this._drag = { kind: "wire", port, px, py }; this._wireFrame = this.mode === "whole" ? this.fr : null; return; }
            if (ng && this._lodLevel < 2) {
                const n = ng._node;
                this.sel = n.gid;
                this._drag = { kind: "node", n, sx: px, sy: py, x: n.x, y: n.y };
                this._redraw();
                return;
            }
            this._drag = { kind: "pan", sx: px, sy: py, x: this.view.x, y: this.view.y };
        });
        el.addEventListener("pointermove", (e) => {
            const d = this._drag; if (!d) return;
            const r = el.getBoundingClientRect(), px = e.clientX - r.left, py = e.clientY - r.top;
            if (d.kind === "pan") { this.view.x = d.x + px - d.sx; this.view.y = d.y + py - d.sy; this._applyView(); }
            else if (d.kind === "node") {
                d.n.x = d.x + (px - d.sx) / this.view.k; d.n.y = d.y + (py - d.sy) / this.view.k; d.n.moved = true; d.moved = true;
                if (this._moveQ) return;   // (at most one redraw a frame)
                this._moveQ = requestAnimationFrame(() => { this._moveQ = 0; if (this.mode === "whole") { if (this.fr) this._renderFrameBody(this.fr); } else this.render(); });
            }
            else if (d.kind === "wire") {
                const pp = this.portPos;
                const a = d.port.dir === "out" ? pp.get(d.port.node.gid + ">out") : pp.get(d.port.node.gid + "}" + d.port.key);
                let b = this._toWorld(px, py);
                if (this._wireFrame) b = { x: b.x - this._wireFrame.x - this._wireFrame.dx, y: b.y - this._wireFrame.y - this._wireFrame.dy };
                if (!a) return;
                this._wire = `M${a.x},${a.y} C${a.x + 60},${a.y} ${b.x - 60},${b.y} ${b.x},${b.y}`;
                this._redraw();
            }
        });
        el.addEventListener("pointerup", (e) => {
            const d = this._drag; this._drag = null;
            if (!d) return;
            if (d.kind === "pan") { this._save(); return; }
            if (d.kind === "node") {
                if (d.moved && this.mode === "whole" && this.fr) { cancelAnimationFrame(this._moveQ); this._moveQ = 0; this._renderFrameBody(this.fr); this._refitFrame(this.fr); this._renderShared(); this._miniDraw(); }
                return;
            }
            if (d.kind !== "wire") return;
            this._wire = null;
            const under = document.elementFromPoint(e.clientX, e.clientY);
            const tp = under && under._port, tn = under && under.closest && under.closest(".gr-node");
            const mine = (node) => this.g && this.g.nodes.includes(node);
            if (d.port.dir === "out" && tp && tp.dir === "in" && tp.node !== d.port.node && mine(tp.node)) {
                this.g.edges = this.g.edges.filter((x) => !(x.kind === "data" && x.to === tp.node.gid && x.port === tp.key));
                const src = d.port.node;
                this.g.edges.push({ kind: "data", from: src.gid, to: tp.node.gid, port: tp.key, ...(src.kind === "source" ? { expr: { op: "name", name: src.fields.name } } : {}) });
                // an expression feeds one input (it's a tree): drop its other outgoing edge
                if (src.kind !== "source") this.g.edges = this.g.edges.filter((x) => !(x.kind === "data" && x.from === src.gid && !(x.to === tp.node.gid && x.port === tp.key)));
                this._changed(); return;
            }
            if (d.port.dir === "exec" && tn && tn._node.kind === "stmt" && tn._node !== d.port.node && mine(tn._node)) {
                const t = tn._node;
                this.g.edges = this.g.edges.filter((x) => !(x.kind === "exec" && ((x.from === d.port.node.gid && x.port === d.port.key) || x.to === t.gid)));
                this.g.edges.push({ kind: "exec", from: d.port.node.gid, port: d.port.key, to: t.gid });
                this._changed(); return;
            }
            this._redraw();
        });
        el.addEventListener("dblclick", (e) => {
            const tg = e.target === el && this._downTarget ? this._downTarget : e.target;
            e = { target: tg };
            const ov = e.target.closest && e.target.closest(".gr-ov");
            if (ov && ov._ov.kind === "slot") { this.app.openDecl("slot:" + ov._ov.name); return; }
            const fe = e.target.closest && e.target.closest(".gr-frame");
            if (this.mode === "whole" && fe && (this._lodLevel === 2 || e.target.classList.contains("gr-ftitle") || e.target.classList.contains("gr-fbox"))) this.setMode("slot", fe._frame.key);
        });
        window.addEventListener("keydown", (e) => {
            if (!this.root.offsetParent || this.mode === "overview" || !this.sel || !this.g) return;
            if ((e.key === "Delete" || e.key === "Backspace") && !/INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName)) {
                const n = this.g.nodes.find((x) => x.gid === this.sel);
                if (n && n.kind !== "entry") { e.preventDefault(); this._delete(n); }
            }
        });
        new ResizeObserver(() => { if (this.mode === "whole") { this._cull(); this._miniDraw(); } }).observe(this.svgEl);
    }
}
