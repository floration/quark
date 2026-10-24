// Blocks view: a hand-written Scratch / Blockly-like editor over the brain AST. The model is a block tree made from
// the AST (toBlocks) and turned back into it (fromBlocks): fromBlocks(toBlocks(x)) deep-equals x. Statements stack
// (drag between stacks), expressions plug into inputs, the palette has a category per kind of block, dropping on
// the palette deletes, dropping on the workspace leaves a loose block (not in the brain; shown dashed).

import { decompose, compose, CATALOG, WEAPONS, BRAIN_MODES, POINTS, symbols, slotKey, newExpr, newStmt, declKey, wholeGroups } from "./ast.js";
import { exprNumText, printDeclHead, roundTo, canonNum } from "./dsl.js";
import { CurveWidget, LEVEL_COLORS, LEVEL_NAMES } from "./curves.js";
import { docOf, unitOf } from "./values.js";

let UID = 1;
// ---------------------------------------------------------------- model (no DOM: node-testable)
export function toBlocks(node) {
    const p = decompose(node);
    const b = { uid: UID++, cat: p.cat, op: p.op, fields: p.fields, inputs: {}, stacks: {} };
    for (const [k, e] of p.inputs) b.inputs[k] = toBlocks(e);
    for (const [k, list] of p.stacks) b.stacks[k] = list.map(toBlocks);
    return b;
}
export function fromBlocks(b) {
    if (!b) return { op: "hole" };
    const inputs = Object.entries(b.inputs).map(([k, v]) => [k, fromBlocks(v)]);
    const stacks = Object.entries(b.stacks).map(([k, v]) => [k, v.map(fromBlocks)]);
    return compose({ cat: b.cat, op: b.op, fields: b.fields, inputs, stacks });
}
export function brainToBlocks(brain) {
    const { decls, ...head } = brain;
    return { head: JSON.parse(JSON.stringify(head)), decls: decls.map(toBlocks) };
}
export function blocksToBrain(m) { return { ...JSON.parse(JSON.stringify(m.head)), decls: m.decls.map(fromBlocks) }; }

// ---------------------------------------------------------------- categories and colours
const CAT_OF = (b) => {
    if (b.cat === "decl") return "hat";
    if (b.cat === "stmt") return b.op === "if" ? "logic" : b.op === "set" || b.op === "return" ? "set" : "action";
    switch (b.op) {
        case "num": case "+": case "-": case "*": case "/": case "neg": return "math";
        case "bool": case "and": case "or": case "not": case "?:": case "<": case "<=": case ">": case ">=": case "==": case "!=": case "in": return "logic";
        case "call": return ["roll", "gauss", "chance"].includes(b.fields.fn) ? "random" : CATALOG.builtins[b.fields.fn] ? (["allowed", "firstAllowed", "site"].includes(b.fields.fn) ? "action" : "math") : "curve";
        case "index": return "data";
        case "name": return "name";
        default: return "hole";
    }
};
const BIN_OPS = ["+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!=", "and", "or"];

function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }

// ---------------------------------------------------------------- the view
// Two modes: "whole" (the default) puts every declaration on one zoomable canvas, grouped by area (params as a table,
// curves as small plots, lists / tables, every slot's full stack); "focus" shows one declaration. Both edit the same
// block model, so every edit goes through blocksToBrain -> app.brainEdited (the one AST).
const ZMIN = 0.25, ZMAX = 1.5;
export class BlocksView {
    constructor(root, app) {
        this.root = root; this.app = app;
        this.model = null; this.declIx = -1; this.floating = new Map();   // declKey -> [{block, x, y}]
        this.mode = "whole"; this.zoom = 1; this.zooms = { whole: 1, focus: 1 }; this.collapsed = new Set(); this.curIx = -1;
        root.classList.add("blocks");
        this.paletteEl = el("div", "bk-palette");
        this.catsEl = el("div", "bk-cats");
        this.itemsEl = el("div", "bk-items");
        this.paletteEl.append(this.catsEl, this.itemsEl);
        this.main = el("div", "bk-main");
        this.barEl = el("div", "bk-bar");
        const body = el("div", "bk-body");
        this.ws = el("div", "bk-ws");
        this.canvas = el("div", "bk-canvas");
        this.ws.append(this.canvas);
        this.sideEl = el("div", "bk-side");
        this.mini = el("canvas", "bk-minimap");
        this.mini.title = "the whole brain: click or drag to move there";
        this.outlineEl = el("div", "bk-outline");
        this.sideEl.append(this.mini, this.outlineEl);
        body.append(this.ws, this.sideEl);
        this.main.append(this.barEl, body);
        root.append(this.paletteEl, this.main);
        this.cat = "logic";
        this._drag = null;
        document.addEventListener("pointermove", (e) => this._dragMove(e));
        document.addEventListener("pointerup", (e) => this._dragEnd(e));
        this._wsInput();
    }

    // the saved mode / zoom / collapsed groups (server-side state through the brain page)
    restoreState(st) {
        if (!st) return;
        if (st.mode === "whole" || st.mode === "focus") this.mode = st.mode;
        // each mode keeps its own zoom (a single slot isn't drawn at the whole brain's fit-all 31%)
        const z = (v) => Math.max(ZMIN, Math.min(ZMAX, v));
        if (st.zooms && typeof st.zooms === "object") { for (const m of ["whole", "focus"]) if (Number.isFinite(st.zooms[m])) this.zooms[m] = z(st.zooms[m]); }
        else if (Number.isFinite(st.zoom)) this.zooms.whole = z(st.zoom);
        this.zoom = this.zooms[this.mode];
        if (Array.isArray(st.collapsed)) this.collapsed = new Set(st.collapsed);
        if (typeof st.focus === "string") this._focusKey = st.focus;
    }
    _save() {
        const d = this.brain && this.brain.decls[this.declIx];
        if (this.app.saveViewState) this.app.saveViewState("blocks", { mode: this.mode, zoom: this.zoom, zooms: { ...this.zooms, [this.mode]: this.zoom }, collapsed: [...this.collapsed], focus: d ? declKey(d) : this._focusKey || null });
    }

    // the brain changed elsewhere (or first show): rebuild the model from the AST
    setBrain(brain, declKeySel) {
        this.brain = brain;
        this.S = symbols(brain);
        this.model = brainToBlocks(brain);
        this._top = new WeakSet(this.model.decls.map((b) => b.stacks.body).filter(Boolean));
        this.declJson = this.model.decls.map((b) => JSON.stringify(fromBlocks(b)));
        if (this._focusKey && declKeySel === undefined) { declKeySel = this._focusKey; this._focusKey = null; }
        if (declKeySel !== undefined) this.declIx = brain.decls.findIndex((d) => declKey(d) === declKeySel);
        if (this.declIx < 0 || this.declIx >= brain.decls.length) this.declIx = brain.decls.findIndex((d) => d.kind === "slot");
        if (this.curIx < 0 || this.curIx >= brain.decls.length || brain.decls[this.curIx].kind !== "slot") this.curIx = this.declIx;
        this.render();
    }
    // open a declaration: in whole mode scroll to it (opening its group), else show it alone
    select(key) {
        const i = this.brain.decls.findIndex((d) => declKey(d) === key);
        if (i < 0) return;
        this.declIx = i;
        if (this.brain.decls[i].kind === "slot") this.curIx = i;
        if (this.mode === "whole") this.reveal(i); else this.render();
        this._save();
    }
    setMode(m, ix) {
        if (ix !== undefined && ix >= 0) { this.declIx = ix; if (this.brain.decls[ix].kind === "slot") this.curIx = ix; }
        const was = this.mode;
        this.zooms[was] = this.zoom;
        this.mode = m;
        this.zoom = this.zooms[m] ?? 1;
        this.render();
        if (m === "whole" && was !== "whole" && this.declIx >= 0) this.reveal(this.declIx);
        else if (m === "focus") this.ws.scrollTo(0, 0);
        this._save();
    }
    reveal(ix) {
        const g = this.groups && this.groups.find((x) => x.decls.includes(ix));
        if (g && this.collapsed.has(g.area)) { this.collapsed.delete(g.area); this.render(); }
        const card = this.cards && this.cards.get(ix);
        if (!card) return;
        const wr = this.ws.getBoundingClientRect(), cr = card.getBoundingClientRect();
        this.ws.scrollTop += cr.top - wr.top - 24;
        this.ws.scrollLeft += Math.min(0, cr.left - wr.left - 24);
        card.classList.remove("bk-flash"); void card.offsetWidth; card.classList.add("bk-flash");
    }

    _changed(touched = []) {
        const brain = blocksToBrain(this.model);
        this.brain = brain;
        this.S = symbols(brain);
        this.app.brainEdited(brain, "blocks");
        const json = brain.decls.map((d) => JSON.stringify(d));
        const dirty = new Set(touched);
        json.forEach((j, i) => { if (j !== this.declJson[i]) dirty.add(i); });
        this.declJson = json;
        if (this.mode !== "whole" || !this.cards) return this.render();
        // whole brain: redraw only the cards whose declaration changed (and the curve plots, which read params)
        this._palette();
        this._datalists();
        this._refreshCards([...dirty]);
        if ([...dirty].some((i) => brain.decls[i] && brain.decls[i].kind === "param")) this._drawCurves();
        this._measure();
    }

    // ---- toolbar: mode, breadcrumb, zoom
    _bar() {
        const b = this.barEl;
        b.textContent = "";
        const btn = (t, f, on, title) => { const e = el("button", on ? "on" : "", t); e.onclick = f; if (title) e.title = title; b.append(e); return e; };
        btn("Whole brain", () => this.setMode("whole"), this.mode === "whole", "every declaration on one canvas, grouped by area");
        btn("One at a time", () => this.setMode("focus"), this.mode === "focus", "one slot or curve");
        if (this.mode === "focus") {
            const d = this.brain && this.brain.decls[this.declIx];
            const crumb = el("span", "bk-crumb");
            const a = el("a", null, "Whole brain"); a.href = "#"; a.onclick = (e) => { e.preventDefault(); this.setMode("whole"); };
            crumb.append(a, el("span", "muted", " › "), el("span", "bk-title", d ? (d.kind === "slot" ? "slot " + slotKey(d) : d.kind + " " + d.name) : "—"));
            b.append(crumb);
        } else {
            b.append(el("span", "muted bk-hint", "click a slot's header to open it alone"));
        }
        b.append(el("span", "bk-spacer"));
        if (this.mode === "whole") {
            btn("Collapse all", () => { this.collapsed = new Set((this.groups || []).map((g) => g.area)); this.render(); this._save(); });
            btn("Expand all", () => { this.collapsed.clear(); this.render(); this._save(); });
        }
        btn("−", () => this.setZoom(this.zoom / 1.25), false, "zoom out (ctrl+wheel)");
        this.zoomLabel = el("span", "bk-zoom", Math.round(this.zoom * 100) + "%");
        this.zoomLabel.title = "click for 100%";
        this.zoomLabel.onclick = () => this.setZoom(1);
        b.append(this.zoomLabel);
        btn("+", () => this.setZoom(this.zoom * 1.25), false, "zoom in (ctrl+wheel)");
        btn("Fit all", () => this.fitAll(), false, "zoom so everything fits");
    }
    setZoom(z, px, py) {
        z = Math.max(ZMIN, Math.min(ZMAX, Math.round(z * 100) / 100));
        if (z === this.zoom) return;
        const ws = this.ws;
        if (px === undefined) { px = ws.clientWidth / 2; py = ws.clientHeight / 2; }
        const cx = (ws.scrollLeft + px) / this.zoom, cy = (ws.scrollTop + py) / this.zoom;
        this.zoom = z;
        this.canvas.style.zoom = z;
        this.canvas.style.setProperty("--z", z);
        ws.scrollLeft = cx * z - px; ws.scrollTop = cy * z - py;
        if (this.zoomLabel) this.zoomLabel.textContent = Math.round(z * 100) + "%";
        this._measure();
        clearTimeout(this._zsave); this._zsave = setTimeout(() => this._save(), 400);
    }
    // the largest zoom (25%..150%) at which everything fits the workspace (cards re-flow with the width, so search)
    fitAll() {
        const ws = this.ws, H = ws.clientHeight, Wd = ws.clientWidth;
        if (!H) return;
        const fits = (z) => { this.canvas.style.zoom = z; const r = this.canvas.getBoundingClientRect(); return r.height <= H + 1 && this.canvas.scrollWidth * z <= Wd + 2; };
        let lo = ZMIN, hi = ZMAX;
        if (fits(hi)) lo = hi;
        else for (let i = 0; i < 7; i++) { const m = (lo + hi) / 2; if (fits(m)) lo = m; else hi = m; }
        this.zoom = Math.floor(lo * 100) / 100;
        this.canvas.style.zoom = this.zoom;
        this.canvas.style.setProperty("--z", this.zoom);
        if (this.zoomLabel) this.zoomLabel.textContent = Math.round(this.zoom * 100) + "%";
        ws.scrollTo(0, 0);
        this._measure();
        this._save();
    }

    render() {
        this.root.classList.toggle("bk-whole", this.mode === "whole");
        this._bar();
        this.canvas.style.zoom = this.zoom;
        this.canvas.style.setProperty("--z", this.zoom);
        if (this.mode === "whole") return this._renderWhole();
        this.cards = null;
        this._curves = [];
        this.outlineEl.textContent = "";
        const dsel = this.brain && this.declIx >= 0 ? this.brain.decls[this.declIx] : null;
        this.slotCat = dsel && dsel.kind === "slot" ? CATALOG.slots[dsel.name] : null;
        this._palette();
        const c = this.canvas;
        c.textContent = "";
        if (!this.model || this.declIx < 0) { c.append(el("div", "muted", "Pick a slot or curve on the left.")); return; }
        const b = this.model.decls[this.declIx];
        const decl = this.brain.decls[this.declIx];
        this._datalists();
        const top = el("div", "bk-root");
        top.append(decl.kind === "slot" || (decl.kind === "curve" && decl.form === "expr") ? this._renderDecl(b, decl) : this._card(this.declIx, true));
        c.append(top);
        const fl = this.floating.get(declKey(decl)) || [];
        for (const f of fl) c.append(this._float(f, fl));
        if (fl.length) c.append(el("div", "bk-note", `${fl.length} loose block${fl.length > 1 ? "s" : ""} (not in the brain until plugged in)`));
        if (this._curves) requestAnimationFrame(() => this._drawCurves());
    }
    _float(f, fl) {
        const w = el("div", "bk-float");
        w.style.left = f.x + "px"; w.style.top = f.y + "px";
        const be = f.block.cat === "stmt" ? this._renderStmt(f.block) : this._renderExpr(f.block);
        be._src = { kind: "float", list: fl, item: f };
        w.append(be);
        return w;
    }

    // ---- whole brain
    _renderWhole() {
        const c = this.canvas;
        c.textContent = "";
        this._curves = [];
        this.cards = new Map();
        if (!this.model) return;
        const dcur = this.brain.decls[this.curIx];
        this.slotCat = dcur && dcur.kind === "slot" ? CATALOG.slots[dcur.name] : null;
        this._palette();
        this._datalists();
        this.groups = wholeGroups(this.brain);
        for (const g of this.groups) c.append(this._group(g));
        this._outline();
        requestAnimationFrame(() => { this._drawCurves(); this._measure(); });
    }
    _group(g) {
        const sec = el("section", "bk-group");
        sec.dataset.area = g.area;
        const coll = this.collapsed.has(g.area);
        const head = el("button", "bk-ghead");
        const count = {};
        for (const i of g.decls) { const k = this.brain.decls[i].kind; count[k] = (count[k] || 0) + 1; }
        const summary = Object.entries(count).map(([k, n]) => `${n} ${k}${n > 1 ? "s" : ""}`).join(" · ");
        head.append(el("span", "bk-caret", coll ? "▸" : "▾"), el("span", "bk-glabel", g.label), el("span", "muted", summary));
        head.onclick = () => { if (this.collapsed.has(g.area)) this.collapsed.delete(g.area); else this.collapsed.add(g.area); sec.replaceWith(this._group(g)); this._outline(); requestAnimationFrame(() => { this._drawCurves(); this._measure(); }); this._save(); };
        sec.append(head);
        if (coll) return sec;
        const body = el("div", "bk-gbody");
        const params = g.decls.filter((i) => this.brain.decls[i].kind === "param");
        if (params.length) body.append(this._paramsCard(params));
        const order = { curve: 0, list: 1, table: 2, slot: 3 };
        const rest = g.decls.filter((i) => this.brain.decls[i].kind !== "param").sort((a, b) => order[this.brain.decls[a].kind] - order[this.brain.decls[b].kind] || a - b);
        for (const i of rest) body.append(this._card(i));
        sec.append(body);
        return sec;
    }
    _refreshCards(ixs) {
        const done = new Set();
        for (const i of ixs) {
            const old = this.cards.get(i);
            if (!old || done.has(old)) continue;
            done.add(old);
            const nu = old._ixs.length > 1 || this.brain.decls[i].kind === "param" ? this._paramsCard(old._ixs) : this._card(i);
            old.replaceWith(nu);
        }
        if (done.size) requestAnimationFrame(() => this._drawCurves());
    }
    _card(ix, alone = false) {
        const b = this.model.decls[ix], d = this.brain.decls[ix];
        const card = el("div", "bk-card bk-k-" + d.kind + (ix === this.curIx && d.kind === "slot" ? " cur" : ""));
        card._ixs = [ix];
        card.dataset.key = declKey(d);
        if (!alone) this.cards.set(ix, card);
        if (d.kind === "slot") card.append(this._renderDecl(b, d, ix));
        else if (d.kind === "curve") {
            const w = el("div", "bk-hat");
            const head = el("div", "bk-hathead");
            head.append(el("span", "bk-kw", "curve"), el("span", "bk-title", `${d.name}(${d.arg} ${d.argType})` + (d.form === "expr" ? " =" : " = " + d.form)));
            w.append(this._comments(b.fields), head);
            if (d.form === "expr") w.append(this._slot(b, "body"));
            const plot = el("div", "bk-plot");
            const curve = d.form === "expr" ? fromBlocks(b) : Object.assign({ kind: "curve" }, b.fields);
            const cw = new CurveWidget(plot, curve, () => this.brain, () => { b.fields.points = curve.points; if (curve.form === "steps") b.fields.base = curve.base; this._changed(); }, { sliders: false });
            this._curves.push(cw);
            w.append(plot);
            if (d.form !== "expr") w.append(el("div", "muted bk-small", "drag a point · double-click adds · right-click removes"));
            if (b.fields.trail) w.append(el("div", "bk-trail", b.fields.trail));
            card.append(w);
        } else if (d.kind === "list") {
            const w = el("div", "bk-hat");
            const head = el("div", "bk-hathead");
            head.append(el("span", "bk-kw", "list"), el("span", "bk-title", d.name));
            const chips = el("div", "bk-chips");
            b.fields.items.forEach((it, k) => chips.append(el("span", "chip c-data", (k + 1) + ". " + it)));
            const inp = el("input", "bk-listin");
            inp.value = b.fields.items.join(" ");
            inp.title = "the weapons in order, separated by spaces";
            inp.onchange = () => {
                const ws = inp.value.trim().split(/[\s,]+/).filter(Boolean);
                if (ws.length && ws.every((x) => WEAPONS.includes(x) || x === "none")) { b.fields.items = ws; this._changed(); }
                else { inp.value = b.fields.items.join(" "); this.app.app && this.app.app.toast && this.app.app.toast("a list holds weapon names: " + WEAPONS.join(" "), 4000); }
            };
            w.append(this._comments(b.fields), head, chips, inp);
            if (b.fields.trail) w.append(el("div", "bk-trail", b.fields.trail));
            card.append(w);
        } else if (d.kind === "table") {
            const w = el("div", "bk-hat");
            const head = el("div", "bk-hathead");
            head.append(el("span", "bk-kw", "table"), el("span", "bk-title", d.name));
            const t = el("table", "bk-ptable");
            const row = (label, get, set) => {
                const tr = el("tr");
                const inp = this._numIn(get(), "f32", (v) => { set(v); this._changed(); });
                const td = el("td"); td.append(inp);
                tr.append(el("td", "n", label), td);
                t.append(tr);
            };
            b.fields.entries.forEach((e) => row(e[0], () => e[1], (v) => (e[1] = v)));
            if (b.fields.else !== undefined && b.fields.else !== null) row("else", () => b.fields.else, (v) => (b.fields.else = v));
            w.append(this._comments(b.fields), head, t);
            if (b.fields.trail) w.append(el("div", "bk-trail", b.fields.trail));
            card.append(w);
        } else if (d.kind === "param") return this._paramsCard([ix], alone);
        const fl = this.floating.get(declKey(d)) || [];
        if (!alone) for (const f of fl) card.append(this._float(f, fl));
        return card;
    }
    _numIn(v, type, on) {
        const inp = el("input", "bk-num");
        inp.value = canonNum(v, type === "int" ? "int" : type);
        inp.style.width = `calc(${Math.max(3, inp.value.length) + 1}ch + 12px)`;
        inp.onchange = () => { const x = Number(inp.value); if (Number.isFinite(x)) on(roundTo(type === "int" ? Math.trunc(x) : x, type)); else inp.value = canonNum(v, type); };
        return inp;
    }
    // the params of one area as a table: name, value (or one per level), what it does
    _paramsCard(ixs, alone = false) {
        const card = el("div", "bk-card bk-k-param");
        card._ixs = ixs;
        const w = el("div", "bk-hat");
        const head = el("div", "bk-hathead");
        head.append(el("span", "bk-kw", "params"), el("span", "muted", ixs.length + ""));
        const t = el("table", "bk-ptable");
        for (const i of ixs) {
            if (!alone) this.cards.set(i, card);
            const b = this.model.decls[i], f = b.fields;
            const tr = el("tr");
            tr.dataset.key = "param:" + f.name;
            const nm = el("td", "n", f.name);
            const doc = [docOf({ kind: "param", ...f }), unitOf({ kind: "param", ...f })].filter(Boolean).join(" · ");
            nm.title = `${f.name} ${f.type}${doc ? "\n" + doc : ""}`;
            const vals = el("td", "v");
            f.values.forEach((v, k) => {
                if (f.type === "bool") { const bt = el("button", "bk-toggle", v ? "yes" : "no"); bt.onclick = () => { f.values[k] = !v; this._changed(); }; vals.append(bt); }
                else { const inp = this._numIn(v, f.type, (x) => { f.values[k] = x; this._changed(); }); if (f.values.length === 4) { inp.title = LEVEL_NAMES[k]; inp.style.borderBottomColor = LEVEL_COLORS[k]; } vals.append(inp); }
            });
            tr.append(nm, vals, el("td", "u muted", unitOf({ kind: "param", ...f })));
            t.append(tr);
        }
        w.append(head, t);
        card.append(w);
        return card;
    }
    _drawCurves() {
        if (!this._curves) return;
        this._curves = this._curves.filter((w) => w.cv.isConnected);
        for (const w of this._curves) w.draw();
    }
    _outline() {
        const o = this.outlineEl;
        o.textContent = "";
        if (this.mode !== "whole" || !this.groups) return;
        for (const g of this.groups) {
            const h = el("div", "bk-ol-g" + (this.collapsed.has(g.area) ? " coll" : ""), g.label);
            h.onclick = () => { if (this.collapsed.has(g.area)) { this.collapsed.delete(g.area); this.render(); this._save(); } const sec = this.canvas.querySelector(`section[data-area="${g.area}"]`); if (sec) this.ws.scrollTop += sec.getBoundingClientRect().top - this.ws.getBoundingClientRect().top - 4; };
            o.append(h);
            const np = g.decls.filter((i) => this.brain.decls[i].kind === "param").length;
            if (np) { const it = el("div", "bk-ol-i muted", `params (${np})`); it.onclick = () => this.reveal(g.decls.find((i) => this.brain.decls[i].kind === "param")); o.append(it); }
            for (const i of g.decls) {
                const d = this.brain.decls[i];
                if (d.kind === "param") continue;
                const it = el("div", "bk-ol-i k-" + d.kind, d.kind === "slot" ? slotKey(d) : d.name);
                it.title = d.kind;
                it.onclick = () => this.reveal(i);
                o.append(it);
            }
        }
    }
    // the mini-map: every card as a box (from one layout read), the visible part as a frame
    _measure() {
        if (this.mode !== "whole" || !this.cards) { this.miniRects = null; this._miniDraw(); return; }
        const cr = this.canvas.getBoundingClientRect(), z = this.zoom;
        const rects = [];
        const seen = new Set();
        for (const card of this.cards.values()) {
            if (seen.has(card) || !card.isConnected) continue;
            seen.add(card);
            const r = card.getBoundingClientRect();
            const d = this.brain.decls[card._ixs[0]];
            rects.push({ x: (r.left - cr.left) / z, y: (r.top - cr.top) / z, w: r.width / z, h: r.height / z, kind: d ? d.kind : "slot" });
        }
        for (const sec of this.canvas.querySelectorAll(".bk-ghead")) { const r = sec.getBoundingClientRect(); rects.push({ x: (r.left - cr.left) / z, y: (r.top - cr.top) / z, w: r.width / z, h: r.height / z, kind: "group" }); }
        this.miniRects = rects;
        this.contentW = cr.width / z; this.contentH = cr.height / z;
        this._miniDraw();
    }
    _miniDraw() {
        const cv = this.mini;
        const W = cv.clientWidth || 160, H = cv.clientHeight || 220;
        const dpr = window.devicePixelRatio || 1;
        if (cv.width !== Math.round(W * dpr)) { cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); }
        const ctx = cv.getContext("2d");
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx.clearRect(0, 0, W, H);
        if (!this.miniRects || !this.contentH) return;
        const s = Math.min(W / this.contentW, H / this.contentH);
        this._miniS = s;
        const col = { slot: "#34506e", param: "#3b6fd1", curve: "#c2417c", list: "#6b7b8f", table: "#6b7b8f", group: "#9fb0c4" };
        for (const r of this.miniRects) { ctx.fillStyle = col[r.kind] || "#555"; ctx.globalAlpha = r.kind === "group" ? 0.5 : 0.85; ctx.fillRect(r.x * s, r.y * s, Math.max(1, r.w * s), Math.max(1, r.kind === "group" ? 1.5 : r.h * s)); }
        ctx.globalAlpha = 1;
        const z = this.zoom, ws = this.ws;
        ctx.strokeStyle = "#fbbf24"; ctx.lineWidth = 1.5;
        ctx.strokeRect(ws.scrollLeft / z * s + 0.5, ws.scrollTop / z * s + 0.5, Math.min(W, ws.clientWidth / z * s) - 1, ws.clientHeight / z * s - 1);
    }
    _wsInput() {
        const ws = this.ws;
        ws.addEventListener("wheel", (e) => {
            if (!e.ctrlKey && !e.metaKey) return;
            e.preventDefault();
            const r = ws.getBoundingClientRect();
            this.setZoom(this.zoom * Math.exp(-e.deltaY * 0.002), e.clientX - r.left, e.clientY - r.top);
        }, { passive: false });
        ws.addEventListener("scroll", () => { if (this._miniQ) return; this._miniQ = requestAnimationFrame(() => { this._miniQ = 0; this._miniDraw(); }); }, { passive: true });
        // the slot under the last click gives the palette its sensors
        ws.addEventListener("pointerdown", (e) => {
            if (this.mode !== "whole") return;
            const card = e.target.closest && e.target.closest(".bk-card");
            if (!card) return;
            const i = card._ixs[0], d = this.brain.decls[i];
            if (!d || d.kind !== "slot" || i === this.curIx) return;
            const prev = this.cards.get(this.curIx);
            if (prev) prev.classList.remove("cur");
            card.classList.add("cur");
            this.curIx = i;
            this.slotCat = CATALOG.slots[d.name] || null;
            this._palette();
            this._datalists();
        }, true);
        const go = (e) => {
            if (!this._miniS) return;
            const r = this.mini.getBoundingClientRect();
            const x = (e.clientX - r.left) / this._miniS, y = (e.clientY - r.top) / this._miniS;
            ws.scrollLeft = x * this.zoom - ws.clientWidth / 2; ws.scrollTop = y * this.zoom - ws.clientHeight / 2;
        };
        this.mini.addEventListener("pointerdown", (e) => { this.mini.setPointerCapture(e.pointerId); this._miniDrag = true; go(e); });
        this.mini.addEventListener("pointermove", (e) => { if (this._miniDrag) go(e); });
        this.mini.addEventListener("pointerup", () => { this._miniDrag = false; });
        new ResizeObserver(() => this._miniDraw()).observe(this.mini);
    }

    // ---- palette
    _palette() {
        const cats = [["logic", "Logic"], ["math", "Maths"], ["random", "Random"], ["param", "Params"], ["curve", "Curves"], ["sensor", "Sensors"], ["action", "Actions"], ["data", "Weapons / lists"]];
        this.catsEl.textContent = "";
        for (const [k, label] of cats) {
            const b = el("button", "bk-cat c-" + (k === "param" ? "name" : k === "sensor" ? "name" : k) + (this.cat === k ? " on" : ""), label);
            b.onclick = () => { this.cat = k; this._palette(); };
            this.catsEl.append(b);
        }
        const items = this.itemsEl;
        items.textContent = "";
        const add = (node, label) => {
            const blk = toBlocks(node);
            const e = blk.cat === "stmt" ? this._renderStmt(blk, true) : this._renderExpr(blk, true);
            e._src = { kind: "palette", node };
            this._draggable(e);
            const w = el("div", "bk-pitem");
            if (label) w.title = label;
            w.append(e);
            items.append(w);
        };
        const S = this.S || symbols({ decls: [] });
        const H = (s) => items.append(el("div", "bk-ph", s));
        if (this.cat === "logic") {
            add(newStmt("if"), "if"); add(newStmt("if", { else: true }), "if / else");
            ["and", "or"].forEach((o) => add(newExpr(o))); add(newExpr("not"));
            ["<", "<=", ">", ">=", "==", "!="].forEach((o) => add(newExpr(o)));
            add(newExpr("?:")); add({ op: "bool", v: true }); add({ op: "bool", v: false });
        } else if (this.cat === "math") {
            ["+", "-", "*", "/"].forEach((o) => add(newExpr(o))); add(newExpr("neg"));
            add({ op: "num", type: "f32", v: 1 }); add({ op: "num", type: "f64", v: 1 }); add({ op: "num", type: "int", v: 1 });
            ["min", "max", "clamp", "abs", "sqrt", "exp", "f32", "f64"].forEach((f) => add(newExpr("call", { fn: f })));
        } else if (this.cat === "random") {
            H("each draw uses the bot's dice: the order matters");
            add(newExpr("call", { fn: "roll" }), "uniform [0, 1): one draw");
            add(newExpr("call", { fn: "gauss" }), "normal (0, 1): two draws");
            add(newExpr("call", { fn: "chance" }), "an event `rate` times a second, this tick");
            add({ op: "<", a: newExpr("call", { fn: "roll" }), b: { op: "num", type: "f32", v: 0.5 } }, "true this share of the time");
        } else if (this.cat === "param") {
            for (const [n, p] of S.params) add({ op: "name", name: n }, p.trail || "");
        } else if (this.cat === "curve") {
            for (const [n] of S.curves) add({ op: "call", fn: n, args: [{ op: "hole" }] });
        } else if (this.cat === "sensor") {
            const sc = this.slotCat;
            const scd = this.brain && this.brain.decls[this.mode === "whole" ? this.curIx : this.declIx];
            if (sc) { H(`sensors of slot ${scd ? slotKey(scd) : ""}` + (this.mode === "whole" ? " (click a slot to switch)" : "")); for (const k in sc.sensors) add({ op: "name", name: k }, sc.sensors[k]); H("outputs (set them)"); for (const k in sc.outputs) add({ op: "set", name: k, value: { op: "hole" } }, sc.outputs[k]); }
            H("every slot"); for (const k in CATALOG.common) add({ op: "name", name: k }, CATALOG.common[k]);
        } else if (this.cat === "action") {
            add(newStmt("set", { name: "x" })); add(newStmt("return")); add(newStmt("return", { n: 0 }));
            add(newStmt("goto")); ["fallback", "seek", "evade", "pass"].forEach((o) => add(newStmt(o)));
            H("points"); POINTS.forEach((p) => add({ op: "name", name: p })); add(newExpr("call", { fn: "site" }));
            H("weapons"); add(newExpr("call", { fn: "allowed" })); add(newExpr("call", { fn: "firstAllowed" }));
        } else if (this.cat === "data") {
            H("lists"); for (const [n] of S.lists) { add({ op: "name", name: n }); add(newExpr("in", { list: n })); }
            H("tables"); for (const [n] of S.tables) add(newExpr("index", { table: n }));
            H("weapons"); [...WEAPONS, "none"].forEach((w) => add({ op: "name", name: w }));
            H("modes"); BRAIN_MODES.forEach((m) => add({ op: "name", name: m }));
        }
    }

    // ---- rendering
    _renderDecl(b, decl, ix) {
        const w = el("div", "bk-hat");
        const head = el("div", "bk-hathead");
        if (this.mode === "whole" && ix !== undefined) {
            head.classList.add("bk-focusable");
            head.title = "click to work on this one alone";
            head.onclick = (e) => { if (!e.target.closest("button,input,select")) this.setMode("focus", ix); };
        }
        if (decl.kind === "slot") {
            head.append(el("span", "bk-kw", "slot"), el("span", "bk-title", decl.name === "objective" ? `objective ${decl.mode}` : decl.name));
            const cat = CATALOG.slots[decl.name];
            if (cat) {
                const meta = el("div", "bk-meta");
                meta.append(el("span", "muted", "called " + cat.when));
                const sens = Object.entries(cat.sensors), outs = Object.entries(cat.outputs);
                if (sens.length) { const s = el("div", "bk-chips"); s.append(el("span", "muted", "reads")); sens.forEach(([k, t]) => s.append(el("span", "chip c-name", `${k}: ${t}`))); meta.append(s); }
                if (outs.length) { const s = el("div", "bk-chips"); s.append(el("span", "muted", "sets")); outs.forEach(([k, t]) => s.append(el("span", "chip c-set", `${k}: ${t}`))); meta.append(s); }
                head.append(meta);
            }
            w.append(this._comments(b.fields), head, this._stack(b.stacks.body, b, "body"));
        } else if (decl.kind === "curve" && decl.form === "expr") {
            head.append(el("span", "bk-kw", "curve"), el("span", "bk-title", `${decl.name}(${decl.arg} ${decl.argType}) =`));
            w.append(this._comments(b.fields), head, this._slot(b, "body"));
        } else {
            head.append(el("span", "bk-title", printDeclHead(decl)));
            w.append(head, el("div", "muted pad", "Edit its values in the Values panel (or the Code view)."));
        }
        if (b.fields.trail) w.append(el("div", "bk-trail", b.fields.trail));
        return w;
    }

    _comments(f) {
        const c = (f.comments || []).filter((x) => x !== "");
        if (!c.length) return document.createDocumentFragment();
        const n = el("div", "bk-comment");
        n.textContent = c.join("\n");
        return n;
    }

    _stack(list, owner, key) {
        const s = el("div", "bk-stack");
        s._drop = { kind: "stack", list };
        list.forEach((blk, i) => {
            const e = this._renderStmt(blk);
            e._src = { kind: "stack", list, index: i, block: blk };
            s.append(e);
        });
        if (!list.length) s.append(el("div", "bk-empty", "drop statements here"));
        return s;
    }

    _renderStmt(b, preview = false) {
        const w = el("div", "bk-block bk-stmt c-" + CAT_OF(b));
        w._blk = b;
        if (!preview) w.append(this._comments(b.fields));
        const row = el("div", "bk-row");
        w.append(row);
        const kw = (t) => row.append(el("span", "bk-kw", t));
        switch (b.op) {
            case "set":
                kw("set");
                row.append(this._nameField(b, "name", "bk-dl-set"));
                row.append(el("span", "bk-kw", "="), this._slot(b, "value", preview));
                break;
            case "return": {
                kw("return");
                const n = b.fields.values;
                for (let i = 0; i < n; i++) { if (i) row.append(el("span", "bk-kw", ",")); row.append(this._slot(b, "values." + i, preview)); }
                if (!preview) {
                    const plus = el("button", "bk-mini", "+"); plus.title = "one more value";
                    plus.onclick = (e) => { e.stopPropagation(); b.inputs["values." + n] = toBlocks({ op: "hole" }); b.fields.values = n + 1; this._changed(); };
                    row.append(plus);
                    if (n) { const minus = el("button", "bk-mini", "−"); minus.onclick = (e) => { e.stopPropagation(); delete b.inputs["values." + (n - 1)]; b.fields.values = n - 1; this._changed(); }; row.append(minus); }
                }
                break;
            }
            case "goto": kw("goto"); row.append(this._slot(b, "point", preview)); break;
            case "if": {
                row.remove();
                const arms = b.fields.arms;
                arms.forEach((a, i) => {
                    if (i && !preview) w.append(this._comments(a));
                    const r = el("div", "bk-row");
                    r.append(el("span", "bk-kw", i ? "elif" : "if"), this._slot(b, `arms.${i}.cond`, preview));
                    if (!preview && arms.length > 1) { const x = el("button", "bk-mini", "×"); x.title = "remove this arm"; x.onclick = (e) => { e.stopPropagation(); this._removeArm(b, i); }; r.append(x); }
                    if (a.trail && !preview) r.append(el("span", "bk-trail", a.trail));
                    w.append(r);
                    w.append(preview ? el("div", "bk-cmouth") : this._stack(b.stacks[`arms.${i}.body`], b, `arms.${i}.body`));
                });
                if (b.fields.else) {
                    const r = el("div", "bk-row");
                    r.append(el("span", "bk-kw", "else"));
                    if (!preview) { const x = el("button", "bk-mini", "×"); x.title = "remove else"; x.onclick = (e) => { e.stopPropagation(); delete b.fields.else; delete b.stacks["else.body"]; this._changed(); }; r.append(x); }
                    if (b.fields.else.trail && !preview) r.append(el("span", "bk-trail", b.fields.else.trail));
                    w.append(r, preview ? el("div", "bk-cmouth") : this._stack(b.stacks["else.body"], b, "else.body"));
                }
                if (!preview) {
                    const r = el("div", "bk-row bk-foot");
                    const ae = el("button", "bk-mini", "+ elif");
                    ae.onclick = (e) => { e.stopPropagation(); const i = b.fields.arms.length; b.fields.arms.push({}); b.inputs[`arms.${i}.cond`] = toBlocks({ op: "hole" }); b.stacks[`arms.${i}.body`] = [toBlocks({ op: "pass" })]; this._changed(); };
                    r.append(ae);
                    if (!b.fields.else) { const el2 = el("button", "bk-mini", "+ else"); el2.onclick = (e) => { e.stopPropagation(); b.fields.else = {}; b.stacks["else.body"] = [toBlocks({ op: "pass" })]; this._changed(); }; r.append(el2); }
                    w.append(r);
                }
                break;
            }
            default: kw(b.op);
        }
        if (b.fields.trail && !preview) row.append(el("span", "bk-trail", b.fields.trail));
        if (!preview) this._draggable(w);
        return w;
    }

    _removeArm(b, i) {
        const n = b.fields.arms.length;
        for (let k = i; k < n - 1; k++) { b.inputs[`arms.${k}.cond`] = b.inputs[`arms.${k + 1}.cond`]; b.stacks[`arms.${k}.body`] = b.stacks[`arms.${k + 1}.body`]; }
        delete b.inputs[`arms.${n - 1}.cond`]; delete b.stacks[`arms.${n - 1}.body`];
        b.fields.arms.splice(i, 1);
        this._changed();
    }

    _slot(owner, key, preview = false) {
        const s = el("span", "bk-slot");
        const child = owner.inputs[key];
        if (child && child.op !== "hole") {
            const e = this._renderExpr(child, preview);
            e._src = { kind: "input", owner, key, block: child };
            s.append(e);
        } else s.classList.add("empty");
        if (!preview) s._drop = { kind: "input", owner, key };
        return s;
    }

    _renderExpr(b, preview = false) {
        const w = el("span", "bk-block bk-expr c-" + CAT_OF(b));
        w._blk = b;
        const f = b.fields;
        const t = (x, cls = "bk-kw") => w.append(el("span", cls, x));
        switch (b.op) {
            case "num": {
                const inp = el("input", "bk-num");
                inp.value = exprNumText(f.v, f.type).replace(/f$/, "");
                inp.style.width = `calc(${Math.max(2, inp.value.length) + 1}ch + 12px)`;
                inp.oninput = () => { inp.style.width = `calc(${Math.max(2, inp.value.length) + 1}ch + 12px)`; };
                inp.disabled = preview;
                inp.onchange = () => { const v = Number(inp.value); if (Number.isFinite(v)) { f.v = roundTo(v, f.type); this._changed(); } else inp.value = String(f.v); };
                w.append(inp);
                const ty = el("button", "bk-type", f.type);
                ty.title = "type: click to change (f32 float, f64 double, int)";
                ty.onclick = (e) => { e.stopPropagation(); if (preview) return; f.type = f.type === "f32" ? "f64" : f.type === "f64" ? "int" : "f32"; if (f.type === "int") f.v = Math.trunc(f.v); this._changed(); };
                w.append(ty);
                break;
            }
            case "bool": {
                const bt = el("button", "bk-toggle", f.v ? "yes" : "no");
                bt.onclick = (e) => { e.stopPropagation(); if (preview) return; f.v = !f.v; this._changed(); };
                w.append(bt);
                break;
            }
            case "name": {
                w.classList.add(this._nameClass(f.name));
                if (preview) t(f.name, "bk-name"); else w.append(this._nameField(b, "name", "bk-dl-read"));
                break;
            }
            case "call": {
                t(f.fn, "bk-fn");
                t("(", "bk-p");
                for (let i = 0; i < f.args; i++) { if (i) t(",", "bk-p"); w.append(this._slot(b, "args." + i, preview)); }
                t(")", "bk-p");
                break;
            }
            case "index": t(f.table, "bk-fn"); t("[", "bk-p"); w.append(this._slot(b, "index", preview)); t("]", "bk-p"); break;
            case "neg": t("−"); w.append(this._slot(b, "a", preview)); break;
            case "not": t("not"); w.append(this._slot(b, "a", preview)); break;
            case "in": {
                w.append(this._slot(b, "a", preview)); t("in");
                const sel = this._select([...(this.S ? this.S.lists.keys() : [])], f.list, (v) => { f.list = v; this._changed(); }, preview);
                w.append(sel);
                break;
            }
            case "?:": w.append(this._slot(b, "c", preview)); t("?"); w.append(this._slot(b, "a", preview)); t(":"); w.append(this._slot(b, "b", preview)); break;
            case "hole": t("_", "muted"); break;
            default: {
                w.append(this._slot(b, "a", preview));
                w.append(this._select(BIN_OPS, b.op, (v) => { b.op = v; this._changed(); }, preview, "bk-op"));
                w.append(this._slot(b, "b", preview));
            }
        }
        if (!preview) this._draggable(w);
        return w;
    }

    _nameClass(n) {
        const S = this.S;
        if (S && S.params.has(n)) return "ref-param";
        if (WEAPONS.includes(n) || n === "none") return "ref-weapon";
        if (BRAIN_MODES.includes(n)) return "ref-mode";
        if (S && S.lists.has(n)) return "ref-list";
        if (POINTS.includes(n)) return "ref-point";
        return "ref-sensor";
    }
    _readable() {
        const out = [];
        if (this.slotCat) out.push(...Object.keys(this.slotCat.sensors), ...Object.keys(this.slotCat.outputs));
        out.push(...Object.keys(CATALOG.common));
        if (this.S) out.push(...this.S.params.keys(), ...this.S.lists.keys());
        out.push(...POINTS, ...WEAPONS, ...BRAIN_MODES);
        return out;
    }
    _settable() {
        const out = this.slotCat ? Object.keys(this.slotCat.outputs) : [];
        const d = this.brain && this.brain.decls[this.mode === "whole" ? this.curIx : this.declIx];
        if (d && d.kind === "slot") { const walk = (body) => body.forEach((s) => { if (s.op === "set") out.push(s.name); if (s.op === "if") { s.arms.forEach((a) => walk(a.body)); if (s.else) walk(s.else.body); } }); walk(d.body); }
        return out;
    }

    _nameField(b, key, listId) {
        const inp = el("input", "bk-namein");
        inp.value = b.fields[key];
        const fit = () => { inp.style.width = `calc(${Math.max(3, inp.value.length) + 1}ch + 12px)`; };
        fit();
        inp.setAttribute("list", listId);
        inp.onchange = () => { const v = inp.value.trim(); if (/^[A-Za-z_][A-Za-z0-9_.]*$/.test(v)) { b.fields[key] = v; this._changed(); } else inp.value = b.fields[key]; };
        inp.oninput = fit;
        return inp;
    }
    _datalists() {   // one shared list of names per kind (not one per field)
        const mk = (id, names) => {
            let dl = document.getElementById(id);
            if (!dl) { dl = el("datalist"); dl.id = id; document.body.append(dl); }
            const want = [...new Set(names)].join("\n");
            if (dl._names === want) return;
            dl._names = want; dl.textContent = "";
            for (const o of new Set(names)) { const op = el("option"); op.value = o; dl.append(op); }
        };
        mk("bk-dl-read", this._readable());
        mk("bk-dl-set", this._settable());
    }

    _select(options, value, onChange, disabled, cls = "bk-sel") {
        const s = el("select", cls);
        for (const o of options) { const op = el("option", null, o); op.value = o; if (o === value) op.selected = true; s.append(op); }
        if (!options.includes(value)) { const op = el("option", null, value); op.selected = true; s.prepend(op); }
        s.disabled = !!disabled;
        s.onchange = () => onChange(s.value);
        return s;
    }

    // ---- drag and drop (pointer events; works with mouse and pen)
    _draggable(w) {
        w.addEventListener("pointerdown", (e) => {
            if (e.button !== 0) return;
            if (e.target.closest("input,select,button")) return;
            const src = w._src || (w.parentElement && w.parentElement._src);
            e.stopPropagation();
            this._drag = { el: w, startX: e.clientX, startY: e.clientY, active: false, src: () => w._src };
        });
    }
    _dragMove(e) {
        const d = this._drag;
        if (!d) return;
        if (!d.active) {
            if (Math.abs(e.clientX - d.startX) + Math.abs(e.clientY - d.startY) < 5) return;
            d.active = true;
            const src = d.src();
            if (!src) { this._drag = null; return; }
            d.from = src;
            d.block = src.kind === "palette" ? toBlocks(JSON.parse(JSON.stringify(src.node))) : src.block || (src.item && src.item.block);
            const r = d.el.getBoundingClientRect();
            d.dx = e.clientX - r.left; d.dy = e.clientY - r.top;
            d.ghost = d.el.cloneNode(true);
            d.ghost.classList.add("bk-ghost");
            d.ghost.style.width = r.width / this.zoom + "px";
            d.ghost.style.zoom = this.zoom;
            document.body.append(d.ghost);
            if (src.kind !== "palette") d.el.classList.add("bk-dragging");
            this.root.classList.add("dragging");
        }
        d.ghost.style.left = (e.clientX - d.dx) / this.zoom + "px";
        d.ghost.style.top = (e.clientY - d.dy) / this.zoom + "px";
        this._clearHints();
        const t = this._target(e, d);
        d.target = t;
        if (t && t.kind === "stack") {
            const line = el("div", "bk-insert");
            const kids = [...t.el.children].filter((c) => c._blk);
            if (t.index < kids.length) t.el.insertBefore(line, kids[t.index]); else t.el.append(line);
            this._hint = line;
        } else if (t && t.kind === "input") { t.el.classList.add("bk-hot"); this._hintEl = t.el; }
        else if (t && t.kind === "trash") { this.paletteEl.classList.add("bk-trash"); }
    }
    _clearHints() {
        if (this._hint) { this._hint.remove(); this._hint = null; }
        if (this._hintEl) { this._hintEl.classList.remove("bk-hot"); this._hintEl = null; }
        this.paletteEl.classList.remove("bk-trash");
    }
    _target(e, d) {
        const under = document.elementFromPoint(e.clientX, e.clientY);
        if (!under) return null;
        if (this.paletteEl.contains(under)) return d.from.kind === "palette" ? null : { kind: "trash" };
        if (!this.ws.contains(under)) return null;
        let n = under;
        const isStmt = d.block.cat === "stmt";
        while (n && n !== this.ws) {
            if (n === d.el || (d.el.contains(n) && d.from.kind !== "palette")) return null;   // not into itself
            if (n._drop) {
                if (n._drop.kind === "stack" && isStmt) {
                    const kids = [...n.children].filter((c) => c._blk);
                    let index = kids.length;
                    for (let i = 0; i < kids.length; i++) { const r = kids[i].getBoundingClientRect(); if (e.clientY < r.top + r.height / 2) { index = i; break; } }
                    return { kind: "stack", el: n, list: n._drop.list, index };
                }
                if (n._drop.kind === "input" && !isStmt) return { kind: "input", el: n, owner: n._drop.owner, key: n._drop.key };
            }
            n = n.parentElement;
        }
        // a loose block: on the canvas (one declaration) or on the card it was dropped on (whole brain)
        const z = this.zoom;
        if (this.mode === "whole") {
            const card = under.closest(".bk-card");
            const ix = card && card._ixs[0], dd = ix !== undefined && this.brain.decls[ix];
            if (!dd || !(dd.kind === "slot" || (dd.kind === "curve" && dd.form === "expr"))) return null;
            const r = card.getBoundingClientRect();
            return { kind: "float", ix, x: (e.clientX - r.left - d.dx) / z, y: (e.clientY - r.top - d.dy) / z };
        }
        const r = this.canvas.getBoundingClientRect();
        return { kind: "float", ix: this.declIx, x: (e.clientX - r.left - d.dx) / z, y: (e.clientY - r.top - d.dy) / z };
    }
    // the declaration a block element sits in
    _ownerIx(elm) {
        if (this.mode !== "whole") return this.declIx;
        const card = elm && elm.closest && elm.closest(".bk-card");
        return card ? card._ixs[0] : this.declIx;
    }
    _dragEnd(e) {
        const d = this._drag;
        this._drag = null;
        if (!d || !d.active) return;
        d.ghost.remove();
        d.el.classList.remove("bk-dragging");
        this.root.classList.remove("dragging");
        this._clearHints();
        const t = d.target;
        if (!t) { this._redrawAfterDrag(d); return; }
        const tix = t.kind === "float" ? t.ix : t.kind === "trash" ? this._ownerIx(d.el) : this._ownerIx(t.el);
        const flOf = (ix) => { const key = declKey(this.brain.decls[ix]); if (!this.floating.has(key)) this.floating.set(key, []); return this.floating.get(key); };
        const fl = flOf(tix);
        const touched = [tix];
        if (d.from.kind !== "palette") touched.push(this._ownerIx(d.el));
        // take it out of where it was
        const src = d.from;
        let fixIndex = (list, i) => i;
        if (src.kind === "stack") {
            src.list.splice(src.index, 1);
            if (src.list.length === 0 && !this._top.has(src.list)) src.list.push(toBlocks({ op: "pass" }));
            fixIndex = (list, i) => (list === src.list && src.index < i ? i - 1 : i);
        } else if (src.kind === "input") src.owner.inputs[src.key] = toBlocks({ op: "hole" });
        else if (src.kind === "float") src.list.splice(src.list.indexOf(src.item), 1);
        // and put it where it goes
        if (t.kind === "stack") {
            let i = fixIndex(t.list, t.index);
            if (t.list.length === 1 && t.list[0].op === "pass" && d.block.op !== "pass") { t.list.splice(0, 1); i = 0; }
            t.list.splice(i, 0, d.block);
        } else if (t.kind === "input") {
            const old = t.owner.inputs[t.key];
            if (old && old.op !== "hole") fl.push({ block: old, x: 20, y: 20 + fl.length * 34 });
            t.owner.inputs[t.key] = d.block;
        } else if (t.kind === "float") fl.push({ block: d.block, x: Math.max(0, t.x), y: Math.max(0, t.y) });
        // (trash: dropped)
        this._changed(touched);
    }
    _redrawAfterDrag(d) {
        if (this.mode === "whole" && this.cards && d.from.kind !== "palette") this._refreshCards([this._ownerIx(d.el)]);
        else if (this.mode !== "whole") this.render();
    }
}
