// Graph view: an SVG with pan / zoom. The scenes are computed from the model here: a workflow (triggers -> jobs by
// needs, each job's steps as a stack, coloured by the last CI result), quark_tests (the run -> the par lane in
// parallel -> the alone lane one by one, bar length = timing), a group or a command (-> its test functions -> what
// they call, expanded on click), a function, a script's steps. Layout: longest-path layers + barycentre ordering.

import { el, clip, secs } from "./util.js";
import { ciFor, worstConclusion, conclClass, sharedPorts, lintFor, itemKey, wfOf } from "./model.js";

const NS = "http://www.w3.org/2000/svg";
const S = (tag, attrs, parent) => { const e = document.createElementNS(NS, tag); for (const k in attrs) e.setAttribute(k, attrs[k]); if (parent) parent.append(e); return e; };
const HEAD = 24, ROW = 16;

// ---------------------------------------------------------------- layout (pure)
// nodes: [{id, w, h}], edges: [{from, to}] -> sets n.x, n.y; left to right
export function layered(nodes, edges, { gapX = 70, gapY = 18 } = {}) {
    const by = new Map(nodes.map((n) => [n.id, n]));
    const out = new Map(nodes.map((n) => [n.id, []])), inn = new Map(nodes.map((n) => [n.id, []]));
    for (const e of edges) if (by.has(e.from) && by.has(e.to) && e.from !== e.to) { out.get(e.from).push(e.to); inn.get(e.to).push(e.from); }
    // longest path from the sources (a back edge in a cycle is ignored)
    const layer = new Map(), state = new Map();
    const visit = (id) => {
        if (state.get(id) === 2) return layer.get(id);
        if (state.get(id) === 1) return -1;
        state.set(id, 1);
        let l = 0;
        for (const p of inn.get(id)) { const lp = visit(p); if (lp >= 0) l = Math.max(l, lp + 1); }
        state.set(id, 2); layer.set(id, l);
        return l;
    };
    for (const n of nodes) visit(n.id);
    for (const n of nodes) if (n.layer !== undefined) layer.set(n.id, n.layer);
    const L = [];
    for (const n of nodes) { const l = layer.get(n.id); (L[l] = L[l] || []).push(n); }
    for (let i = 0; i < L.length; i++) L[i] = L[i] || [];
    // barycentre sweeps (down by predecessors, up by successors)
    const pos = new Map();
    const index = () => L.forEach((col) => col.forEach((n, i) => pos.set(n.id, i)));
    index();
    const bary = (n, nbrs) => { const ps = nbrs.get(n.id).map((x) => pos.get(x)).filter((x) => x !== undefined); return ps.length ? ps.reduce((a, b) => a + b, 0) / ps.length : pos.get(n.id); };
    for (let it = 0; it < 4; it++) {
        for (let i = 1; i < L.length; i++) { L[i].forEach((n) => (n._b = bary(n, inn))); L[i].sort((a, b) => a._b - b._b); index(); }
        for (let i = L.length - 2; i >= 0; i--) { L[i].forEach((n) => (n._b = bary(n, out))); L[i].sort((a, b) => a._b - b._b); index(); }
    }
    // coordinates: columns by the widest node, each column centred on the tallest
    const colH = L.map((col) => col.reduce((s, n) => s + n.h, 0) + gapY * Math.max(0, col.length - 1));
    const H = Math.max(0, ...colH);
    let x = 0;
    L.forEach((col, i) => {
        const w = Math.max(0, ...col.map((n) => n.w));
        let y = (H - colH[i]) / 2;
        for (const n of col) { n.x = x; n.y = y; y += n.h + gapY; }
        x += w + gapX;
    });
    return { width: x - gapX, height: H };
}

// ---------------------------------------------------------------- scenes (pure: nodes with text rows, edges)
// node: {id, w, h, title, sub, rows: [{text, cls}], cls, sel, badge, nav: selection to open}
const nodeH = (rows) => HEAD + Math.max(1, rows.length) * ROW + 8;
function mk(id, title, rows, extra = {}) { const r = rows || []; return { id, title, rows: r, w: extra.w || 230, ...extra, h: nodeH(r) + (extra.sub ? ROW : 0) }; }

export function workflowScene(ix, ci, suite, selJob) {
    const nodes = [], edges = [];
    const trig = (suite.when || []).filter((w) => w.trigger !== "tag" || !(suite.when || []).some((x) => x.trigger === "push" && (x.tags || []).length && x.line === w.line));
    trig.forEach((w, i) => nodes.push(mk("t" + i, w.trigger === "push" ? "push" : w.trigger, [{ text: clip(w.text, 34), cls: "muted" }], { cls: "c-trigger", w: 210, layer: 0, nav: null, line: w.line })));
    const jobs = suite.items || [];
    const ids = new Set(jobs.map((j) => j.jobId));
    for (const j of jobs) {
        const res = ciFor(ci, j);
        const c = worstConclusion(res);
        const rows = [];
        if (j.ifText || j.if) rows.push({ text: "if " + clip(j.ifText || j.if, 36), cls: "cond" });
        for (const s of j.steps || []) rows.push({ text: (s.if ? "? " : "• ") + clip(s.name || s.uses || s.run || "step", 36), cls: s.if ? "cond" : "" });
        const lint = (ix.itemLint.get(itemKey(j)) || []).length;
        nodes.push(mk("j:" + j.jobId, j.name && j.name !== j.jobId ? j.name : j.jobId, rows, { w: 270, cls: "c-job " + (c ? "cc-" + conclClass(c) : ""), sel: selJob === j, nav: { kind: "item", item: j }, badge: lint ? "⚠ " + lint : null, sub: [j.runsOn, j.matrix ? "matrix " + Object.keys(j.matrix).join(",") : null, c ? c : null].filter(Boolean).join(" · ") }));
        const needs = (j.needs || []).filter((n) => ids.has(n));
        if (needs.length) needs.forEach((n, k) => edges.push({ from: "j:" + n, to: "j:" + j.jobId, label: k === 0 && j.if ? clip(j.ifText || j.if, 30) : null, cond: !!j.if }));
        else trig.forEach((w, i) => edges.push({ from: "t" + i, to: "j:" + j.jobId, label: i === 0 && j.if ? clip(j.ifText || j.if, 30) : null, cond: !!j.if }));
        for (const n of j.needs || []) if (!ids.has(n)) nodes.at(-1).badge = "needs " + n + "?";
    }
    return { nodes, edges, layout: "layered" };
}

// the run -> par (parallel bars) -> alone (serial): fixed placement, not layered
export function suiteLanes(ix, selName, y0 = 0) {
    const groups = [...ix.groups.values()];
    const par = groups.filter((g) => g.list === "par").sort((a, b) => a.order - b.order);
    const alone = groups.filter((g) => g.list !== "par").sort((a, b) => a.order - b.order);
    const shared = sharedPorts(ix);
    const maxSec = Math.max(30, ...groups.map((g) => (g.timing && g.timing.sec) || 0));
    const scale = 240 / maxSec;
    const nodes = [], edges = [], bars = [];
    const run = { id: "run", title: "quark_tests", rows: [{ text: "every group", cls: "muted" }, { text: "par first, then alone", cls: "muted" }], w: 150, h: nodeH([1, 2]), x: 0, y: y0 + 10, cls: "c-run" };
    nodes.push(run);
    // the par lane: a box of bars
    const px = 210, rowH = 20;
    const parBox = { id: "par", title: `par: ${par.length} groups at once (one process each)`, rows: [], w: 380, h: HEAD + par.length * rowH + 12, x: px, y: y0, cls: "c-lane", box: true };
    nodes.push(parBox);
    par.forEach((g, i) => {
        const sec = (g.timing && g.timing.sec) || 0;
        bars.push({ x: px + 10, y: y0 + HEAD + 4 + i * rowH, w: Math.max(4, sec * scale), label: g.name, sec, sel: g.name === selName, warn: shared.has(g.name) || (ix.itemLint.get(itemKey(g)) || []).length > 0, nav: { kind: "item", item: g }, title: (g.timing ? secs(sec) + " (" + g.timing.source + ")" : "no timing yet") + (shared.has(g.name) ? "\nshares a TP() port with " + shared.get(g.name).map((s) => s.with.join(", ") + " (" + s.port + ")").join("; ") : "") });
    });
    edges.push({ from: "run", to: "par" });
    // the alone lane: a chain, one under the other (one at a time, top to bottom)
    const ax = px + parBox.w + 200;
    let ay = y0, prev = "par";
    alone.forEach((g) => {
        const sec = (g.timing && g.timing.sec) || 0;
        const n = { id: "a:" + g.name, title: g.name, rows: [{ text: (g.timing ? secs(sec) : "no timing") + " · " + (g.calls || []).length + " calls", cls: "muted" }], w: 200, h: nodeH([1]), x: ax, y: ay, cls: "c-group alone", sel: g.name === selName, nav: { kind: "item", item: g }, badge: (ix.itemLint.get(itemKey(g)) || []).length ? "⚠" : null };
        nodes.push(n);
        edges.push({ from: prev, to: n.id, label: prev === "par" ? "when every par group is done" : null });
        prev = n.id; ay += n.h + 30;
    });
    const parWall = Math.max(0, ...par.map((g) => (g.timing && g.timing.sec) || 0));
    const aloneSum = alone.reduce((s, g) => s + ((g.timing && g.timing.sec) || 0), 0);
    return { nodes, edges, bars, layout: "fixed", note: `about ${secs(parWall)} for par (its longest group, with enough cores) + ${secs(aloneSum)} for alone`, bottom: Math.max(y0 + parBox.h, ay) };
}

// a group / command -> its calls -> (expanded) what each calls
export function callScene(ix, root, expanded) {
    const fns = ix.m.functions || {};
    const nodes = [], edges = [], seen = new Set();
    const shared = root.kind === "group" ? sharedPorts(ix).get(root.name) : null;
    const rootRows = [{ text: root.run ? root.run.cmd : "", cls: "mono" }];
    if (root.kind === "group") rootRows.push({ text: root.list === "par" ? "par: in parallel" : "alone: one at a time", cls: "muted" });
    if (shared) rootRows.push({ text: "⚠ shared port " + shared.map((s) => s.port).join(", "), cls: "warn" });
    nodes.push(mk("root", (root.kind === "group" ? "group " : "") + root.name, rootRows, { cls: "c-group", w: 240, sel: true, nav: null, badge: (ix.itemLint.get(itemKey(root)) || []).length ? "⚠ " + (ix.itemLint.get(itemKey(root)) || []).length : null }));
    const addFn = (name, from, args, depth) => {
        const id = "f:" + name;
        if (!seen.has(id)) {
            seen.add(id);
            const f = fns[name];
            const rows = f ? [{ text: (f.checks || []).length + " checks · " + (f.calls || []).length + " calls · " + f.file.split("/").pop() + ":" + f.line, cls: "muted" }] : [{ text: "not defined in tests/", cls: "warn" }];
            if (args) rows.unshift({ text: "args " + clip(args, 30), cls: "mono" });
            const lint = f ? (ix.fnLint.get(name) || []).length : 1;
            nodes.push(mk(id, name, rows, { cls: "c-fn" + (f ? "" : " missing") + (expanded.has(name) ? " open" : ""), fn: name, w: 250, badge: lint ? "⚠" : f && (f.calls || []).length ? (expanded.has(name) ? "−" : "+") : null, nav: f ? { kind: "fn", fn: f } : null }));
            if (f && expanded.has(name) && depth < 6) for (const c of f.calls || []) addFn(c, id, null, depth + 1);
        }
        edges.push({ from, to: id });
    };
    for (const c of root.calls || []) addFn(c.fn, "root", c.args || null, 0);
    return { nodes, edges, layout: "layered" };
}
export function fnScene(ix, f, expanded) {
    const fns = ix.m.functions || {};
    const nodes = [], edges = [], seen = new Set();
    for (const u of f.usedBy || []) {
        const it = [...ix.items.values()].find((x) => x.id === u);
        nodes.push(mk("u:" + u, it ? (it.kind === "group" ? "group " : "") + it.name : u, [{ text: it && it.run ? it.run.cmd : "", cls: "mono muted" }], { cls: "c-group", w: 220, nav: it ? { kind: "item", item: it } : null, layer: 0 }));
        edges.push({ from: "u:" + u, to: "f:" + f.name });
    }
    const add = (name, from, depth) => {
        const id = "f:" + name;
        if (!seen.has(id)) {
            seen.add(id);
            const g = fns[name];
            nodes.push(mk(id, name, g ? [{ text: (g.checks || []).length + " checks", cls: "muted" }, { text: g.args ? "(" + clip(g.args, 26) + ")" : "()", cls: "mono muted" }] : [{ text: "not in tests/", cls: "warn" }], { cls: "c-fn" + (name === f.name ? "" : "") + (expanded.has(name) ? " open" : ""), fn: name, w: 210, sel: name === f.name, badge: g && (g.calls || []).length && name !== f.name ? (expanded.has(name) ? "−" : "+") : null, nav: g ? { kind: "fn", fn: g } : null }));
            if (g && (name === f.name || expanded.has(name)) && depth < 6) for (const c of g.calls || []) add(c, id, depth + 1);
        }
        if (from) edges.push({ from, to: id });
    };
    add(f.name, null, 0);
    return { nodes, edges, layout: "layered" };
}
export function stepsScene(ix, suite, selItem) {
    const nodes = [], edges = [];
    const when = (suite.when || [])[0];
    nodes.push(mk("w", when ? when.trigger : suite.title, [{ text: clip(when ? when.text : "", 40), cls: "muted" }], { cls: "c-trigger", w: 260 }));
    let prev = "w";
    (suite.items || []).forEach((s, i) => {
        const txt = String(s.text || "").split("\n").filter((l) => l.trim()).slice(0, 4).map((l) => ({ text: clip(l.trim(), 44), cls: "mono" }));
        const id = "s" + i;
        nodes.push(mk(id, (i + 1) + ". " + s.name, txt, { cls: "c-step", w: 330, sel: selItem === s, nav: { kind: "item", item: s } }));
        edges.push({ from: prev, to: id });
        prev = id;
    });
    return { nodes, edges, layout: "chain" };
}

// ---------------------------------------------------------------- the view
export class GraphView {
    constructor(root, app) {
        this.app = app; this.root = root;
        root.classList.add("graph");
        this.bar = el("div", "gr-bar");
        this.svg = S("svg", { class: "gr-svg" });
        this.layer = S("g", {}, this.svg);
        root.append(this.bar, this.svg);
        this.view = { x: 30, y: 30, k: 1 };
        this.expanded = new Set();
        this._input();
    }
    show(sel) {
        const key = sel && (sel.kind === "item" ? itemKey(sel.item) : sel.kind === "fn" ? "fn:" + sel.fn.name : sel.kind === "suite" ? sel.suite.id : "");
        if (key !== this.key) { this.expanded = new Set(); this.key = key; this._fitNext = true; }
        this.sel = sel;
        this.render();
    }
    render() {
        const ix = this.app.ix, sel = this.sel;
        this.layer.textContent = "";
        this.bar.textContent = "";
        if (!ix || !sel) return;
        const parts = [];
        let hint = "";
        if (sel.kind === "suite" && sel.suite.kind === "ci" || sel.kind === "item" && sel.item.kind === "job") {
            const suite = sel.kind === "suite" ? sel.suite : sel.item._suite;
            parts.push(this._place(workflowScene(ix, this.app.ci, suite, sel.item), 0));
            hint = "triggers → jobs by needs · dashed: conditional (its if) · colour: last CI result · click a job to open it";
        } else if (sel.kind === "suite" && sel.suite.id === "quark_tests") {
            parts.push(suiteLanes(ix, null, 0));
            hint = "par groups run at once (bar = time), then the alone groups one by one · click a group";
        } else if (sel.kind === "item" && (sel.item.kind === "group" || sel.item.kind === "shortcut" || sel.item.kind === "tool")) {
            const calls = this._place(callScene(ix, sel.item, this.expanded), 0);
            parts.push(calls);
            if (sel.item.kind === "group") parts.push(suiteLanes(ix, sel.item.name, calls.bottom + 90));
            hint = "click a function to show what it calls · double-click to open it";
        } else if (sel.kind === "fn") {
            parts.push(this._place(fnScene(ix, sel.fn, this.expanded), 0));
            hint = "who runs it → it → what it calls (click to expand, double-click to open)";
        } else {
            const suite = sel.kind === "suite" ? sel.suite : sel.item._suite;
            parts.push(this._place(stepsScene(ix, suite, sel.kind === "item" ? sel.item : null), 0));
            hint = "the steps in the order they run · click one to open it";
        }
        const btn = (t, f) => { const b = el("button", null, t); b.onclick = f; this.bar.append(b); return b; };
        btn("Fit", () => this.fit());
        btn("1:1", () => { this.view.k = 1; this._apply(); });
        if (this.expanded.size) btn("Collapse all", () => { this.expanded.clear(); this.render(); });
        this.bar.append(el("span", "muted small", hint));
        for (const p of parts) if (p.note) this.bar.append(el("span", "gr-note small", p.note));
        const edgesG = S("g", { class: "gr-edges" }, this.layer), nodesG = S("g", {}, this.layer), labelsG = S("g", {}, this.layer);
        this.nodes = [];
        for (const p of parts) {
            const by = new Map(p.nodes.map((n) => [n.id, n]));
            for (const e of p.edges) { const a = by.get(e.from), b = by.get(e.to); if (a && b) this._edge(a, b, e, edgesG, labelsG, p.layout); }
            for (const n of p.nodes) this._node(n, nodesG);
            for (const b of p.bars || []) this._bar(b, nodesG);
            this.nodes.push(...p.nodes);
        }
        this._apply();
        if (this._fitNext) { this._fitNext = false; requestAnimationFrame(() => this.focus()); }
    }
    _place(scene, y0) {
        if (scene.layout === "chain") {
            let y = y0;
            for (const n of scene.nodes) { n.x = 0; n.y = y; y += n.h + 34; }
            scene.bottom = y;
        } else {
            const r = layered(scene.nodes, scene.edges, { gapX: scene.edges.some((e) => e.label) ? 190 : 90, gapY: 22 });
            for (const n of scene.nodes) n.y += y0;
            scene.bottom = y0 + r.height;
        }
        return scene;
    }
    _node(n, parent) {
        const g = S("g", { class: "gr-node " + (n.cls || "") + (n.sel ? " sel" : "") + (n.nav || n.fn ? " click" : ""), transform: `translate(${n.x},${n.y})` }, parent);
        g._n = n;
        S("rect", { width: n.w, height: n.h, rx: 7, class: "gr-body" }, g);
        if (!n.box) S("rect", { width: n.w, height: HEAD, rx: 7, class: "gr-head" }, g), S("rect", { y: HEAD - 7, width: n.w, height: 7, class: "gr-head" }, g);
        const t = S("text", { x: 10, y: 16, class: "gr-title" }, g);
        t.textContent = clip(n.title, Math.floor((n.w - (n.badge ? 40 : 16)) / 7.4));
        if (n.badge) { const b = S("text", { x: n.w - 8, y: 16, class: "gr-badge", "text-anchor": "end" }, g); b.textContent = n.badge; }
        if (n.sub) { const s = S("text", { x: 10, y: HEAD + 14, class: "gr-sub" }, g); s.textContent = clip(n.sub, Math.floor(n.w / 6.4)); }
        const off = n.sub ? ROW : 0;
        n.rows.forEach((r, i) => {
            const tx = S("text", { x: 10, y: HEAD + 14 + off + i * ROW, class: "gr-row " + (r.cls || "") }, g);
            tx.textContent = clip(r.text, Math.floor((n.w - 16) / (r.cls && r.cls.includes("mono") ? 7 : 6.4)));
        });
        const tip = S("title", {}, g);
        tip.textContent = [n.title, n.sub, ...n.rows.map((r) => r.text)].filter(Boolean).join("\n");
    }
    _bar(b, parent) {
        const g = S("g", { class: "gr-barrow click" + (b.sel ? " sel" : "") + (b.warn ? " warn" : ""), transform: `translate(${b.x},${b.y})` }, parent);
        g._n = { nav: b.nav };
        S("rect", { width: 360, height: 17, rx: 3, class: "gr-bartrack" }, g);
        S("rect", { width: b.w, height: 17, rx: 3, class: "gr-barfill" }, g);
        const t = S("text", { x: 6, y: 13, class: "gr-barlabel" }, g);
        t.textContent = (b.warn ? "⚠ " : "") + b.label;
        const s = S("text", { x: 354, y: 13, class: "gr-barsec", "text-anchor": "end" }, g);
        s.textContent = b.sec ? secs(b.sec) : "–";
        const tip = S("title", {}, g); tip.textContent = b.label + ": " + b.title;
    }
    _edge(a, b, e, parent, labels, layout) {
        let d, mx, my;
        if (layout === "chain" || Math.abs(a.x - b.x) < 1) {
            const x = layout === "chain" ? a.x + 40 : a.x + a.w / 2, y0 = a.y + a.h, y1 = b.y;
            d = `M${x},${y0} L${x},${y1}`; mx = x + 8; my = (y0 + y1) / 2;
        } else {
            const x0 = a.x + a.w, y0 = a.y + Math.min(a.h / 2, 40), x1 = b.x, y1 = b.y + Math.min(b.h / 2, 40);
            const dx = Math.max(30, (x1 - x0) * 0.5);
            d = `M${x0},${y0} C${x0 + dx},${y0} ${x1 - dx},${y1} ${x1},${y1}`;
            mx = (x0 + x1) / 2; my = (y0 + y1) / 2;
        }
        S("path", { d, class: "gr-edge" + (e.cond ? " cond" : ""), "marker-end": "url(#gr-arrow)" }, parent);
        if (e.label) {   // at the target end, in the gap before it (the gap is wide enough for a short label)
            const lx = layout === "chain" ? mx : b.x - 10, ly = layout === "chain" ? my : b.y + Math.min(b.h / 2, 40) - 7;
            const g = S("g", { transform: `translate(${lx},${ly})` }, labels);
            const t = S("text", { class: "gr-elabel", "text-anchor": layout === "chain" ? "start" : "end", y: 0 }, g);
            t.textContent = e.label;
            requestAnimationFrame(() => { try { const bb = t.getBBox(); const r = S("rect", { x: bb.x - 4, y: bb.y - 2, width: bb.width + 8, height: bb.height + 4, rx: 4, class: "gr-elabelbg" }); g.insertBefore(r, t); } catch { } });
        }
    }
    // the selected node in view at a readable zoom (else everything)
    focus() {
        const n = (this.nodes || []).find((x) => x.sel);
        if (!n) return this.fit(true);
        const w = this.svg.clientWidth || 800, h = this.svg.clientHeight || 600;
        let bb; try { bb = this.layer.getBBox(); } catch { return; }
        const kFit = Math.min(w / (bb.width + 60), h / (bb.height + 60));
        const k = Math.max(0.8, Math.min(1, kFit));
        this.view.k = k;
        if (kFit >= 0.8) return this.fit(true);
        this.view.x = Math.min(30 - bb.x * k, w / 2 - (n.x + n.w / 2) * k);
        this.view.y = Math.min(20 - bb.y * k, h / 2 - (n.y + n.h / 2) * k);
        this._apply();
    }
    fit(onlyShrink = false) {
        if (!this.nodes || !this.nodes.length) return;
        let bb;
        try { bb = this.layer.getBBox(); } catch { return; }
        const w = this.svg.clientWidth || 800, h = this.svg.clientHeight || 600;
        let k = Math.min(w / (bb.width + 60), h / (bb.height + 60));
        k = Math.max(0.25, Math.min(onlyShrink ? 1 : 1.4, k));
        this.view.k = k;
        this.view.x = (w - bb.width * k) / 2 - bb.x * k;
        this.view.y = Math.max(20 - bb.y * k, (h - bb.height * k) / 2 - bb.y * k);
        if (onlyShrink && bb.height * k > h) this.view.y = 20 - bb.y * k;
        this._apply();
    }
    _apply() { this.layer.setAttribute("transform", `translate(${this.view.x},${this.view.y}) scale(${this.view.k})`); }
    _input() {
        const defs = S("defs", {}, this.svg);
        const mkr = S("marker", { id: "gr-arrow", viewBox: "0 0 10 10", refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse" }, defs);
        S("path", { d: "M0,0 L10,5 L0,10 z", class: "gr-arrowhead" }, mkr);
        const sv = this.svg;
        sv.addEventListener("wheel", (e) => {
            e.preventDefault();
            const r = sv.getBoundingClientRect(), px = e.clientX - r.left, py = e.clientY - r.top;
            const wx = (px - this.view.x) / this.view.k, wy = (py - this.view.y) / this.view.k;
            this.view.k = Math.max(0.15, Math.min(3, this.view.k * Math.exp(-e.deltaY * 0.0015)));
            this.view.x = px - wx * this.view.k; this.view.y = py - wy * this.view.k;
            this._apply();
        }, { passive: false });
        let drag = null;
        sv.addEventListener("pointerdown", (e) => { if (e.button !== 0) return; drag = { sx: e.clientX, sy: e.clientY, x: this.view.x, y: this.view.y, moved: false, target: e.target }; sv.setPointerCapture(e.pointerId); });
        sv.addEventListener("pointermove", (e) => {
            if (!drag) return;
            const dx = e.clientX - drag.sx, dy = e.clientY - drag.sy;
            if (Math.abs(dx) + Math.abs(dy) > 4) drag.moved = true;
            if (drag.moved) { this.view.x = drag.x + dx; this.view.y = drag.y + dy; this._apply(); sv.classList.add("panning"); }
        });
        sv.addEventListener("pointerup", () => {
            const d = drag; drag = null; sv.classList.remove("panning");
            if (!d || d.moved) return;
            const g = d.target.closest && d.target.closest(".click");
            if (!g || !g._n) return;
            const n = g._n;
            if (n.fn && this.app.ix.m.functions[n.fn] && (this.app.ix.m.functions[n.fn].calls || []).length && !(this.sel.kind === "fn" && this.sel.fn.name === n.fn)) {
                clearTimeout(this._clickT);
                this._clickT = setTimeout(() => { if (this.expanded.has(n.fn)) this.expanded.delete(n.fn); else this.expanded.add(n.fn); this.render(); }, 220);
                return;
            }
            if (n.nav) this.app.select(n.nav);
        });
        sv.addEventListener("dblclick", (e) => {
            const g = e.target.closest && e.target.closest(".click");
            if (g && g._n && g._n.nav) { clearTimeout(this._clickT); this.app.select(g._n.nav); }
        });
    }
}
