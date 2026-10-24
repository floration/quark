// Graph view: one slot (or expression curve) as an SVG node graph, Blueprint style. Every AST node is a graph node;
// expression children are data edges into typed input ports, statement order is exec edges (if has an exec output
// per arm). A name (param, sensor, weapon...) used several times is ONE source node with several edges.
// toGraph / fromGraph are pure (node-testable): fromGraph(toGraph(d)) deep-equals d. An "overview" graph shows how
// slots use params, curves, lists and tables.

import { decompose, compose, CATALOG, symbols, slotKey, declKey, walkStmts, stmtExprs, walkExpr, newExpr, newStmt, WEAPONS, BRAIN_MODES, POINTS } from "./ast.js";
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

export class GraphView {
    constructor(root, app) {
        this.root = root; this.app = app;
        root.classList.add("graph");
        this.bar = document.createElement("div"); this.bar.className = "gr-bar";
        this.svgEl = svg("svg", { class: "gr-svg" });
        this.side = document.createElement("div"); this.side.className = "gr-side";
        const wrap = document.createElement("div"); wrap.className = "gr-wrap";
        wrap.append(this.svgEl, this.side);
        root.append(this.bar, wrap);
        this.view = { x: 40, y: 40, k: 0.9 };
        this.layer = svg("g", {}, this.svgEl);
        this.g = null; this.sel = null; this.mode = "slot";
        this._bar();
        this._input();
    }

    setBrain(brain, key) {
        this.brain = brain;
        this.S = symbols(brain);
        if (key !== undefined) this.key = key;
        if (!this.key || !brain.decls.some((d) => declKey(d) === this.key)) { const d = brain.decls.find((x) => x.kind === "slot"); this.key = d && declKey(d); }
        const d = brain.decls.find((x) => declKey(x) === this.key);
        if (this.mode === "overview") { this.ov = overviewGraph(brain); this.g = null; }
        else if (d && (d.kind === "slot" || (d.kind === "curve" && d.form === "expr"))) {
            const old = this.g && this.gKey === this.key ? this.g : null;
            this.g = toGraph(d);
            this.gKey = this.key;
            this._annotate(d);
            if (old) this._keepPositions(old);
        } else this.g = null;
        this.sel = null;
        this.render();
    }
    select(key) { this.mode = "slot"; this._bar(); this.setBrain(this.brain, key); }

    _annotate(d) {
        const cat = d.kind === "slot" ? CATALOG.slots[d.name] : null;
        for (const n of this.g.nodes) if (n.op === "return") n._slotOuts = cat ? Object.keys(cat.outputs) : null;
    }
    // keep where the user put nodes: match by AST id (stable across edits) else by gid
    _keepPositions(old) {
        const pos = new Map();
        for (const n of old.nodes) if (n.moved) pos.set(n.fields.id || (n.kind === "source" ? "src:" + n.fields.name : n.gid), [n.x, n.y]);
        for (const n of this.g.nodes) { const p = pos.get(n.fields.id || (n.kind === "source" ? "src:" + n.fields.name : n.gid)); if (p) { n.x = p[0]; n.y = p[1]; n.moved = true; } }
    }

    _changed() {
        const d = fromGraph(this.g);
        const brain = { ...this.brain, decls: this.brain.decls.map((x) => (declKey(x) === this.key ? d : x)) };
        this.brain = brain;
        this.S = symbols(brain);
        this.app.brainEdited(brain, "graph");
        this.render();
    }

    _bar() {
        const b = this.bar;
        b.textContent = "";
        const btn = (t, f, on) => { const e = document.createElement("button"); e.textContent = t; if (on) e.className = "on"; e.onclick = f; b.append(e); return e; };
        btn("Slot", () => { this.mode = "slot"; this._bar(); this.setBrain(this.brain); }, this.mode === "slot");
        btn("Overview (who uses what)", () => { this.mode = "overview"; this._bar(); this.setBrain(this.brain); }, this.mode === "overview");
        if (this.mode === "slot") {
            btn("Auto layout", () => { if (this.g) { this.g.nodes.forEach((n) => (n.moved = false)); layoutGraph(this.g); this.render(); } });
            btn("Fit", () => this.fit());
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
            sel.onchange = () => { if (sel.value) this._addNode(sel.value); sel.value = ""; };
            b.append(sel);
            const hint = document.createElement("span"); hint.className = "muted";
            hint.textContent = "drag a port to another to connect · Alt+click an input to cut · Del removes the selected node";
            b.append(hint);
        }
    }

    _addNode(op) {
        if (!this.g) return;
        let node;
        const stmtOps = ["if", "set", "return", "goto", "fallback", "pass", "seek", "evade"];
        const at = this._toWorld(this.svgEl.clientWidth / 2, this.svgEl.clientHeight / 2);
        const gid = "g" + ++this.g.next;
        if (op === "name") node = { gid, kind: "source", cat: "expr", op: "name", fields: { name: "fov" }, ins: [], outs: [] };
        else {
            const ast = stmtOps.includes(op) ? newStmt(op) : ["roll", "gauss", "chance", "min", "max", "clamp"].includes(op) ? newExpr("call", { fn: op }) : newExpr(op);
            const p = decompose(ast);
            node = { gid, kind: p.cat === "stmt" ? "stmt" : "expr", cat: p.cat, op: p.op, fields: p.fields, ins: p.inputs.map(([k]) => k), outs: p.cat === "stmt" ? [...p.stacks.map(([k]) => k), "next"] : [] };
            // an if from the palette starts with a 'pass' in its arm: not wired here (wire the arm's exec output)
        }
        node.x = at.x; node.y = at.y; node.moved = true;
        this.g.nodes.push(node);
        this.sel = node.gid;
        this.render();
    }

    fit() {
        const ns = this.mode === "overview" ? this.ov && this.ov.nodes : this.g && this.g.nodes;
        if (!ns || !ns.length) return;
        let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (const n of ns) { x0 = Math.min(x0, n.x); y0 = Math.min(y0, n.y); x1 = Math.max(x1, n.x + W); y1 = Math.max(y1, n.y + (n.ins ? nodeHeight(n) : 24)); }
        const w = this.svgEl.clientWidth || 800, h = this.svgEl.clientHeight || 600;
        this.view.k = Math.max(0.2, Math.min(1.2, Math.min(w / (x1 - x0 + 80), h / (y1 - y0 + 80))));
        this.view.x = -x0 * this.view.k + 40; this.view.y = -y0 * this.view.k + 30;
        this._applyView();
    }
    _applyView() { this.layer.setAttribute("transform", `translate(${this.view.x},${this.view.y}) scale(${this.view.k})`); }
    _toWorld(px, py) { return { x: (px - this.view.x) / this.view.k, y: (py - this.view.y) / this.view.k }; }

    // ---- rendering
    render() {
        const L = this.layer;
        L.textContent = "";
        this._applyView();
        if (this.mode === "overview") return this._renderOverview();
        if (!this.g) { const t = svg("text", { x: 0, y: 20, class: "gr-muted" }, L); t.textContent = "Pick a slot or an expression curve on the left (steps / points curves are edited in Values)."; this._side(); return; }
        const byId = new Map(this.g.nodes.map((n) => [n.gid, n]));
        const edgesL = svg("g", { class: "gr-edges" }, L);
        const nodesL = svg("g", {}, L);
        this.portPos = new Map();
        for (const n of this.g.nodes) this._renderNode(n, nodesL);
        for (const e of this.g.edges) {
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
            const sel = this.sel && (e.from === this.sel || e.to === this.sel);
            svg("path", { d, class: `gr-edge ${e.kind}${sel ? " sel" : ""}` }, edgesL);
        }
        if (this._wire) svg("path", { d: this._wire, class: "gr-edge wire" }, L);
        this._side();
    }

    _renderNode(n, parent) {
        const h = nodeHeight(n);
        const g = svg("g", { class: `gr-node c-${CLASS(n, this.S)}${this.sel === n.gid ? " sel" : ""}`, transform: `translate(${n.x},${n.y})` }, parent);
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

    // ---- inspector
    _side() {
        const s = this.side;
        s.textContent = "";
        const n = this.g && this.sel && this.g.nodes.find((x) => x.gid === this.sel);
        if (!n) {
            const p = document.createElement("div"); p.className = "muted pad";
            p.textContent = "Select a node to edit it. Exec edges (white) give the order of the statements; data edges (coloured) carry values into ports.";
            s.append(p);
            return;
        }
        const h = document.createElement("h4"); h.textContent = nodeTitle(n); s.append(h);
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

    // ---- input
    _input() {
        const el = this.svgEl;
        el.addEventListener("wheel", (e) => {
            e.preventDefault();
            const r = el.getBoundingClientRect(), px = e.clientX - r.left, py = e.clientY - r.top;
            const w = this._toWorld(px, py);
            this.view.k = Math.max(0.15, Math.min(2.5, this.view.k * Math.exp(-e.deltaY * 0.0015)));
            this.view.x = px - w.x * this.view.k; this.view.y = py - w.y * this.view.k;
            this._applyView();
        }, { passive: false });
        el.addEventListener("pointerdown", (e) => {
            const r = el.getBoundingClientRect(), px = e.clientX - r.left, py = e.clientY - r.top;
            const port = e.target._port;
            const ng = e.target.closest && e.target.closest(".gr-node");
            const ov = e.target.closest && e.target.closest(".gr-ov");
            el.setPointerCapture(e.pointerId);
            if (ov) { this.ovSel = ov._ov.gid; this.render(); return; }
            if (port && port.dir === "in" && e.altKey) {
                this.g.edges = this.g.edges.filter((x) => !(x.kind === "data" && x.to === port.node.gid && x.port === port.key));
                this._changed(); return;
            }
            if (port && (port.dir === "out" || port.dir === "exec")) { this._drag = { kind: "wire", port, px, py }; return; }
            if (ng) {
                const n = ng._node;
                this.sel = n.gid;
                this._drag = { kind: "node", n, sx: px, sy: py, x: n.x, y: n.y };
                this.render();
                return;
            }
            this._drag = { kind: "pan", sx: px, sy: py, x: this.view.x, y: this.view.y };
        });
        el.addEventListener("pointermove", (e) => {
            const d = this._drag; if (!d) return;
            const r = el.getBoundingClientRect(), px = e.clientX - r.left, py = e.clientY - r.top;
            if (d.kind === "pan") { this.view.x = d.x + px - d.sx; this.view.y = d.y + py - d.sy; this._applyView(); }
            else if (d.kind === "node") { d.n.x = d.x + (px - d.sx) / this.view.k; d.n.y = d.y + (py - d.sy) / this.view.k; d.n.moved = true; this.render(); }
            else if (d.kind === "wire") {
                const a = d.port.dir === "out" ? this.portPos.get(d.port.node.gid + ">out") : this.portPos.get(d.port.node.gid + "}" + d.port.key);
                const b = this._toWorld(px, py);
                this._wire = `M${a.x},${a.y} C${a.x + 60},${a.y} ${b.x - 60},${b.y} ${b.x},${b.y}`;
                this.render();
            }
        });
        el.addEventListener("pointerup", (e) => {
            const d = this._drag; this._drag = null;
            if (!d || d.kind !== "wire") return;
            this._wire = null;
            const under = document.elementFromPoint(e.clientX, e.clientY);
            const tp = under && under._port, tn = under && under.closest && under.closest(".gr-node");
            if (d.port.dir === "out" && tp && tp.dir === "in" && tp.node !== d.port.node) {
                this.g.edges = this.g.edges.filter((x) => !(x.kind === "data" && x.to === tp.node.gid && x.port === tp.key));
                const src = d.port.node;
                this.g.edges.push({ kind: "data", from: src.gid, to: tp.node.gid, port: tp.key, ...(src.kind === "source" ? { expr: { op: "name", name: src.fields.name } } : {}) });
                // an expression feeds one input (it's a tree): drop its other outgoing edge
                if (src.kind !== "source") this.g.edges = this.g.edges.filter((x) => !(x.kind === "data" && x.from === src.gid && !(x.to === tp.node.gid && x.port === tp.key)));
                this._changed(); return;
            }
            if (d.port.dir === "exec" && tn && tn._node.kind === "stmt" && tn._node !== d.port.node) {
                const t = tn._node;
                this.g.edges = this.g.edges.filter((x) => !(x.kind === "exec" && ((x.from === d.port.node.gid && x.port === d.port.key) || x.to === t.gid)));
                this.g.edges.push({ kind: "exec", from: d.port.node.gid, port: d.port.key, to: t.gid });
                this._changed(); return;
            }
            this.render();
        });
        el.addEventListener("dblclick", (e) => {
            const ov = e.target.closest && e.target.closest(".gr-ov");
            if (ov && ov._ov.kind === "slot") { this.app.openDecl("slot:" + ov._ov.name); }
        });
        window.addEventListener("keydown", (e) => {
            if (!this.root.offsetParent || this.mode !== "slot" || !this.sel) return;
            if ((e.key === "Delete" || e.key === "Backspace") && !/INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName)) {
                const n = this.g.nodes.find((x) => x.gid === this.sel);
                if (n && n.kind !== "entry") { e.preventDefault(); this._delete(n); }
            }
        });
    }
}
