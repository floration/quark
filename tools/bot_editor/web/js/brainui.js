// Brain page: ONE in-memory AST and its views. Code (the DSL, parsed back on edit / blur, errors with line:col, the
// last good AST kept), Blocks, Graph, Values and Diff are all printers of that AST; an edit in any of them replaces
// the AST and the others redraw from it. Open / save / save as / download / reset; undo / redo; provenance on save.

import { parse, print, printDecl, highlightLine, BrainError } from "./dsl.js";
import { validate, clone, numberIds, carryIds, declKey, slotKey, diffBrains, declAreas, highlightSets, CATALOG, symbols, deepEqual, stripIds } from "./ast.js";
import { BlocksView } from "./blocks.js";
import { GraphView, overviewGraph } from "./graph.js";
import { ValuesView } from "./values.js";
import { api } from "./api.js";

const $ = (id) => document.getElementById(id);
const esc = (x) => String(x ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const LINE_H = 19;

export class BrainPage {
    constructor(app) {
        this.app = app;
        this.name = "default";          // the file it came from / goes to
        this.ast = null;                // the current (good) AST, ids numbered
        this.codeText = "";             // the Code view's text (may not parse)
        this.codeError = null;
        this.view = "code";
        this.sel = null;                // selected declaration key
        this.hist = []; this.histIx = -1;
        this.dirtyViews = new Set();
        this.blocks = new BlocksView($("vBlocks"), this);
        this.graph = new GraphView($("vGraph"), this);
        this.values = new ValuesView($("values"), this);
        this._wire();
        app.on("brains", () => this._fillSel());
        app.on("catalog", () => this._afterChange(null));
        app.on("page", (p) => { if (p === "brain") this._showView(this.view); });
    }

    async start() {
        this._fillSel();
        let st = null;
        try { st = await api.state("brain"); } catch {}
        // each view's mode / zoom / pan (server-side, like the brain itself)
        try { this.viewState = (await api.state("views")) || {}; } catch { this.viewState = {}; }
        if (typeof this.viewState !== "object") this.viewState = {};
        this.blocks.restoreState(this.viewState.blocks);
        this.graph.restoreState(this.viewState.graph);
        if (st && st.text) {
            // st.text: the last version that parsed; st.draft: the Code view's text when it didn't (kept, never lost)
            try {
                this.name = st.name || "default";
                this._set(numberIds(JSON.parse(JSON.stringify(parse(st.text)))), null, true);
                this.saved = st.savedText || null;
                if (st.draft && st.draft !== st.text) { this.codeText = st.draft; $("codeTa").value = st.draft; this._codeEdited(false); }
            } catch (e) {
                if (!(e instanceof BrainError)) throw e;
                // an older state file kept only the broken text: open it as text, on top of the default
                await this.open("default");
                this.name = st.name || "default"; this.codeText = st.text; $("codeTa").value = st.text; this._codeEdited(false);
                this.app.toast(`Your last brain text didn't parse (${e.message}): it's in the Code view`, 8000);
                st = { restored: true };
            }
        }
        if (!st) await this.open("default");
        $("brainSel").value = this.name;
        this._showView(new URLSearchParams(location.search).get("view") || "code");
    }

    // ---------------------------------------------------------------- the one AST
    // A user brain file only overrides what it writes: shown merged over the default brain
    mergeOverDefault(ast) {
        const def = this.app.defaultAst();
        if (!def || ast.name === "default") return ast;
        const mine = new Map(ast.decls.map((d) => [declKey(d), d]));
        const decls = def.decls.map((d) => { const m = mine.get(declKey(d)); if (m) { mine.delete(declKey(d)); return m; } return clone(d); });
        for (const d of ast.decls) if (mine.has(declKey(d))) decls.push(d);
        return numberIds(stripIds({ ...ast, decls }));
    }
    mergedWithDefault() { return this.ast; }
    // what the file holds: only the declarations that differ from the default (or everything, when asked)
    fileText() {
        if ($("brainWhole") && $("brainWhole").checked) return print(this.ast);
        const def = this.app.defaultAst();
        if (!def) return print(this.ast);
        const changed = new Set(diffBrains(this.ast, def).filter((d) => d.change !== "default").map((d) => d.key));
        return print({ ...this.ast, decls: this.ast.decls.filter((d) => changed.has(declKey(d))) });
    }
    currentText() { return print(this.ast); }

    async open(name) {
        let text = this.app.brainText.get(name);
        if (text === undefined) text = await this.app.loadBrainText(name);
        let ast;
        try { ast = numberIds(JSON.parse(JSON.stringify(parse(text)))); }
        catch (e) { this.app.toast(`${name}.qbrain doesn't parse (${e.message}): opened as text`, 6000); this.name = name; this.codeText = text; this._codeFromText(text); return; }
        this.name = name;
        this.hist = []; this.histIx = -1;
        this._set(this.mergeOverDefault(ast), null, true);
        this.saved = print(this.ast);
        this._brainInfo();
        $("brainSel").value = name;
        this._persist();
    }

    // every view calls this with the new AST it made; `from` = the view (it keeps its own state)
    brainEdited(ast, from) { this._set(numberIds(ast), from); }

    _set(ast, from, noHistory = false) {
        this.ast = ast;
        if (from !== "code") { this.codeText = print(ast); this.codeError = null; }
        if (!noHistory || this.histIx < 0) this._push();
        this._afterChange(from);
    }
    _push() {
        const t = print(this.ast);
        if (this.hist[this.histIx] === t) return;
        this.hist.splice(this.histIx + 1);
        this.hist.push(t);
        if (this.hist.length > 200) this.hist.shift();
        this.histIx = this.hist.length - 1;
    }
    undo(d) {
        const i = this.histIx + d;
        if (i < 0 || i >= this.hist.length) return;
        this.histIx = i;
        this.ast = carryIds(this.ast, JSON.parse(JSON.stringify(parse(this.hist[i]))));
        this.codeText = this.hist[i]; this.codeError = null;
        this._afterChange(null);
    }

    _afterChange(from) {
        this.check = this.ast ? validate(this.ast) : null;
        for (const v of ["code", "blocks", "graph", "values", "diff"]) if (v !== from) this.dirtyViews.add(v);
        if (from === "code") this.dirtyViews.delete("code");
        this._refreshView();
        this._items();
        this._problems();
        this._info();
        clearTimeout(this._persistT);
        this._persistT = setTimeout(() => this._persist(), 800);
    }
    // the work in progress as a file through serve.py (the shell's browser storage starts empty)
    _persist() { if (this.ast) api.saveState("brain", { name: this.name, text: print(this.ast), draft: this.codeError ? this.codeText : null, savedText: this.saved || null }).catch(() => {}); }

    // a view's own settings (mode, zoom, pan...), saved with the page's state
    saveViewState(view, obj) {
        if (!this.viewState) this.viewState = {};
        this.viewState[view] = obj;
        clearTimeout(this._viewsT);
        this._viewsT = setTimeout(() => api.saveState("views", this.viewState).catch(() => {}), 600);
    }

    // ---------------------------------------------------------------- views
    _showView(v) {
        this.view = v;
        document.querySelectorAll("#viewTabs [data-view]").forEach((b) => b.classList.toggle("on", b.dataset.view === v));
        for (const id of ["code", "blocks", "graph", "values", "diff"]) $("v" + id[0].toUpperCase() + id.slice(1)).classList.toggle("on", id === v);
        this.dirtyViews.add(v);
        this._refreshView(true);
        try { localStorage.setItem("qbe.view", v); } catch {}
    }
    _refreshView(focus) {
        const v = this.view;
        if (!this.dirtyViews.has(v) || !this.ast) return;
        this.dirtyViews.delete(v);
        if (v === "code") this._codeFromText(this.codeText);
        else if (v === "blocks") this.blocks.setBrain(this.ast, this.sel || undefined);
        else if (v === "graph") { this.graph.setBrain(this.ast, this.sel && /^(slot|curve):/.test(this.sel) ? this.sel : undefined); if (focus) requestAnimationFrame(() => this.graph.shown()); }
        else if (v === "values") { this.values.setBrain(this.ast, this.app.defaultAst()); if (this.sel) this.values.scrollTo(this.sel); }
        else if (v === "diff") this._diff();
    }
    openDecl(key, view) {
        this.sel = key;
        if (view) this._showView(view);
        if (this.view === "code") this._scrollCodeTo(key);
        else if (this.view === "blocks") this.blocks.select(key);
        else if (this.view === "graph") this.graph.select(key);
        else if (this.view === "values") this.values.scrollTo(key);
        this._items(); this._info();
    }

    // ---- code
    _codeFromText(text) {
        const ta = $("codeTa");
        if (ta.value !== text) { const pos = ta.selectionStart; ta.value = text; if (document.activeElement === ta) ta.selectionStart = ta.selectionEnd = Math.min(pos, text.length); }
        this._highlight();
    }
    _highlight() {
        const ta = $("codeTa"), text = ta.value, lines = text.split("\n");
        const sets = this.ast ? highlightSets(this.ast) : {};
        $("codeHl").innerHTML = lines.map((l) => highlightLine(l, sets)).join("\n") + "\n";
        const err = this.codeError;
        const probLines = new Set(((this.check && this.check.problems) || []).filter((p) => p.severity === "error").map((p) => p.line));
        let g = "";
        for (let i = 1; i <= lines.length; i++) g += (err && err.line === i) || (!err && probLines.has(i)) ? `<span class="e">${i}</span>\n` : i + "\n";
        $("gutter").innerHTML = g;
        const cols = lines.reduce((m, l) => Math.max(m, l.length), 0);
        ta.style.height = (lines.length * LINE_H + 40) + "px";
        ta.style.width = `calc(${cols + 4}ch + 20px)`;
        $("codeHl").style.width = ta.style.width;
        const old = $("codeScroll").querySelector(".errline");
        if (old) old.remove();
        if (err) { const e = document.createElement("div"); e.className = "errline"; e.style.top = (8 + (err.line - 1) * LINE_H) + "px"; $("codeScroll").append(e); }
        const st = $("codeStatus");
        if (err) { st.className = "bad"; st.textContent = `${err.line}:${err.col}  ${err.msg}   (the other views show the last version that parsed)`; }
        else { st.className = ""; const n = this.check ? this.check.errors.length : 0; st.textContent = `${lines.length} lines · parsed${n ? ` · ${n} check error${n > 1 ? "s" : ""} (right)` : " · checks clean"}`; }
    }
    _codeEdited(final) {
        const text = $("codeTa").value;
        this.codeText = text;
        try {
            const ast = carryIds(this.ast, JSON.parse(JSON.stringify(parse(text))));
            this.codeError = null;
            const same = this.ast && deepEqual(stripIds(ast), stripIds(this.ast));
            this.ast = ast;
            if (!same) { this._push(); this._afterChange("code"); }
            if (final) { const canon = print(ast); if (canon !== text) { this.codeText = canon; this._codeFromText(canon); } }
        } catch (e) {
            if (!(e instanceof BrainError)) throw e;
            this.codeError = e;
            this._persist();
        }
        this._highlight();
    }
    _scrollCodeTo(key) {
        if (!this.ast) return;
        const d = this.ast.decls.find((x) => declKey(x) === key);
        if (!d) return;
        const head = printDecl({ ...d, comments: undefined, trail: undefined })[0];
        const lines = $("codeTa").value.split("\n");
        const i = lines.findIndex((l) => l.startsWith(head.split(" = ")[0].split(":")[0]));
        if (i >= 0) $("codeScroll").scrollTop = Math.max(0, i * LINE_H - 60);
    }

    // ---- diff
    _diff() {
        const R = $("diff"), def = this.app.defaultAst();
        if (!def) { R.innerHTML = `<div class="muted">No default brain loaded.</div>`; return; }
        const items = diffBrains(this.ast, def).filter((d) => d.change !== "default");
        if (!items.length) { R.innerHTML = `<div class="muted">Same as the default brain.</div>`; return; }
        R.innerHTML = `<div class="muted" style="margin-bottom:8px">${items.length} declaration${items.length > 1 ? "s" : ""} differ from the default brain. Saving writes only these (unless “whole brain” is ticked).</div>`;
        for (const it of items) {
            const box = document.createElement("div"); box.className = "dItem";
            const a = it.def ? printDecl({ ...it.def, comments: undefined }) : [], b = printDecl({ ...it.decl, comments: undefined });
            box.innerHTML = `<header><b style="font-family:var(--mono)">${esc(it.key)}</b><span class="chip">${esc(it.change)}</span><span class="chip">${esc(it.area)}</span><span class="grow"></span></header><pre></pre>`;
            const pre = box.querySelector("pre");
            const A = new Set(a), Bs = new Set(b);
            let h = "";
            for (const l of a) if (!Bs.has(l)) h += `<span class="del">- ${esc(l)}</span>`;
            for (const l of b) h += Bs.has(l) && A.has(l) ? `  ${esc(l)}\n` : `<span class="add">+ ${esc(l)}</span>`;
            pre.innerHTML = h;
            const bt = document.createElement("button"); bt.textContent = it.change === "added" ? "remove" : "reset to default";
            bt.onclick = () => this._resetDecl(it.key);
            box.querySelector("header").append(bt);
            R.append(box);
        }
    }
    _resetDecl(key) {
        const def = this.app.defaultAst(), ast = clone(this.ast);
        const i = ast.decls.findIndex((d) => declKey(d) === key), o = def.decls.find((d) => declKey(d) === key);
        if (i < 0) return;
        if (o) ast.decls[i] = clone(o); else ast.decls.splice(i, 1);
        this.brainEdited(ast, null);
    }

    // ---- left: items, right: problems / info
    _brainInfo(changed) {
        if (!changed) { const def = this.app.defaultAst(); changed = new Set(def && this.ast ? diffBrains(this.ast, def).filter((d) => d.change !== "default").map((d) => d.key) : []); }
        $("brainInfo").innerHTML = `${esc(this.name)}.qbrain · ${changed.size} change${changed.size === 1 ? "" : "s"} vs default${this.saved !== undefined && this.saved !== null && this.saved !== print(this.ast) ? ' · <span class="warn">unsaved</span>' : ""}`;
    }
    _items() {
        const L = $("items");
        if (!this.ast) { L.textContent = ""; return; }
        const def = this.app.defaultAst();
        const changed = new Set(def ? diffBrains(this.ast, def).filter((d) => d.change !== "default").map((d) => d.key) : []);
        const areas = declAreas(this.ast);
        const f = ($("itemFilter").value || "").toLowerCase();
        const groups = new Map();
        const label = (d) => (d.kind === "slot" ? slotKey(d) : d.name);
        for (const d of this.ast.decls) {
            const k = declKey(d), area = areas.get(k) || "";
            if (f && !(label(d).toLowerCase().includes(f) || area.includes(f) || d.kind.includes(f))) continue;
            const g = d.kind === "slot" ? "slots · " + (area.startsWith("mode/") ? "objectives" : area) : d.kind === "param" ? "params · " + area : d.kind + "s";
            if (!groups.has(g)) groups.set(g, []);
            groups.get(g).push(d);
        }
        const order = [...groups.keys()].sort((a, b) => (a.startsWith("slots") ? 0 : a.startsWith("curve") ? 1 : a.startsWith("params") ? 2 : 3) - (b.startsWith("slots") ? 0 : b.startsWith("curve") ? 1 : b.startsWith("params") ? 2 : 3) || a.localeCompare(b));
        let h = "";
        for (const g of order) {
            h += `<div class="grp">${esc(g)}</div>`;
            for (const d of groups.get(g)) { const k = declKey(d); h += `<div class="it${k === this.sel ? " sel" : ""}" data-k="${esc(k)}"><span>${esc(label(d))}</span>${changed.has(k) ? '<span class="mod" title="differs from the default">●</span>' : ""}</div>`; }
        }
        L.innerHTML = h;
        L.querySelectorAll(".it").forEach((e) => (e.onclick = () => {
            const k = e.dataset.k;
            const d = this.ast.decls.find((x) => declKey(x) === k);
            const wholeBlocks = this.view === "blocks" && this.blocks.mode === "whole";
            const view = wholeBlocks ? "blocks" : d && d.kind === "param" || (d && (d.kind === "list" || d.kind === "table" || (d.kind === "curve" && d.form !== "expr"))) ? (this.view === "code" ? "code" : "values") : this.view === "values" || this.view === "diff" ? "code" : this.view;
            this.openDecl(k, view);
        }));
        this._brainInfo(changed);
    }
    _problems() {
        const P = $("problems"), c = this.check;
        if (!c) return;
        const n = c.problems.length;
        $("checkSummary").innerHTML = n ? `${c.errors.length} error${c.errors.length === 1 ? "" : "s"}, ${n - c.errors.length} warning${n - c.errors.length === 1 ? "" : "s"} <span class="muted">(the editor's checks; the game's checkbrain is the judge${this.app.config.quarkTests ? "" : ": quark_tests not found"})</span>` : "Clean. <span class='muted'>(the editor's checks)</span>";
        const g = this.gameCheck;
        if (g) $("checkSummary").insertAdjacentHTML("beforeend", `<div class="muted" style="font-size:12px;margin-top:4px">Last save, the game's check: <b>${esc(g.label)}</b> · ${g.interp.length ? `interpreted: ${esc(g.interp.join(", "))}` : "every slot native"}</div>`);
        P.innerHTML = c.problems.slice(0, 200).map((p, i) => `<div class="prob ${p.severity}" data-i="${i}">${p.line ? p.line + ":" + p.col + " " : ""}${esc(p.where || "")}: ${esc(p.msg)}</div>`).join("");
        P.querySelectorAll(".prob").forEach((e) => (e.onclick = () => {
            const p = c.problems[+e.dataset.i];
            if (p.line) { this._showView("code"); $("codeScroll").scrollTop = Math.max(0, (p.line - 1) * LINE_H - 80); }
        }));
    }
    _info() {
        const I = $("itemInfo");
        const d = this.ast && this.sel && this.ast.decls.find((x) => declKey(x) === this.sel);
        if (!d) { I.innerHTML = "Pick an item on the left."; return; }
        const area = declAreas(this.ast).get(this.sel);
        let h = `<div><b style="font-family:var(--mono)">${esc(d.kind === "slot" ? slotKey(d) : d.name)}</b> <span class="chip">${esc(d.kind)}</span> <span class="chip">${esc(area)}</span></div>`;
        if (d.trail) h += `<div style="margin-top:4px">${esc(d.trail.replace(/^#\s?/, ""))}</div>`;
        if (d.kind === "slot") {
            const c = CATALOG.slots[d.name];
            if (c) {
                h += `<div style="margin-top:6px">called ${esc(c.when)}</div>`;
                h += `<div style="margin-top:4px">reads: ${Object.entries(c.sensors).map(([k, t]) => `<span class="chip">${esc(k)}: ${esc(t)}</span>`).join(" ") || "-"}</div>`;
                h += `<div style="margin-top:4px">sets: ${Object.entries(c.outputs).map(([k, t]) => `<span class="chip">${esc(k)}: ${esc(t)}</span>`).join(" ") || "an action (goto / fallback / seek / evade)"}</div>`;
            }
            const info = this.check && this.check.info[slotKey(d)];
            if (info) h += `<div style="margin-top:4px">dice draws per call (worst path): <b>${info.draws}</b> <span class="muted">(their order matters for replaying a match)</span></div>`;
        }
        const ov = overviewGraph(this.ast);
        const me = ov.nodes.find((n) => n.gid === (d.kind === "slot" ? "slot:" + slotKey(d) : d.kind + ":" + d.name));
        if (me) {
            const users = ov.edges.filter((e) => e.from === me.gid).map((e) => e.to.split(":")[1]);
            const uses = ov.edges.filter((e) => e.to === me.gid).map((e) => e.from.split(":")[1]);
            if (users.length) h += `<div style="margin-top:6px">used by: ${users.map((u) => `<span class="chip">${esc(u)}</span>`).join(" ")}</div>`;
            if (uses.length) h += `<div style="margin-top:6px">reads: ${uses.map((u) => `<span class="chip">${esc(u)}</span>`).join(" ")}</div>`;
        }
        I.innerHTML = h;
    }

    // ---------------------------------------------------------------- files
    _fillSel() {
        const s = $("brainSel");
        s.textContent = "";
        for (const b of this.app.brainList) { const o = document.createElement("option"); o.value = b.name; o.textContent = b.name + (b.source === "default" ? " (built in)" : ""); s.append(o); }
        if (!this.app.brainList.some((b) => b.name === this.name)) { const o = document.createElement("option"); o.value = this.name; o.textContent = this.name + " (not saved)"; s.append(o); }
        s.value = this.name;
    }
    _provenance() {
        const def = this.app.defaultAst();
        const areas = def ? [...new Set(diffBrains(this.ast, def).filter((d) => d.change !== "default").map((d) => d.area).filter(Boolean))] : [];
        return { areas, human: $("brainHuman").checked };
    }
    async save(asName) {
        if (!this.ast) return;
        if (this.codeError) { this.app.toast("The code has an error: fix it first (" + this.codeError.message + ")", 5000); return; }
        let name = asName || this.name;
        if (name === "default") {
            name = await this.app.prompt("Save as", "mybrain", "The built-in default can't be overwritten: save your brain under a name (letters, digits, _ . -). It goes to " + this.app.brainsDir);
            if (!name) return;
        }
        if (!/^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$/.test(name) || name === "default") { this.app.toast("Bad name: letters, digits, _ . - (not 'default')"); return; }
        const ast = { ...this.ast, name };
        this.ast = ast;
        const text = this.fileText();
        try {
            const r = await api.saveBrain(name, text, this._provenance());
            this.name = name;
            this.app.brainText.set(name, text);
            this.app.brainAst.set(name, numberIds(JSON.parse(JSON.stringify(parse(text)))));
            await this.app.refreshBrains();
            this.saved = print(this.ast);
            this._fillSel();
            this._afterChange(null);
            this.app.toast(`Saved ${r.path}`);
            this._checkWithGame(text);
        } catch (e) { this.app.toast("Save failed: " + e.message, 6000); }
    }
    async _checkWithGame(text) {
        if (!this.app.config.quarkTests && !this.app.config.maptool) return;
        try {
            const r = await api.checkBrain(text);
            if (!r.available) return;
            const res = r.result || {};
            if (r.via === "simcheck") {   // quark_maptool simcheck: {brainSupport, ok, label?, error?: "line:col message"}
                if (res.brainSupport && !res.ok && res.error) this.app.toast(`The game's brain compiler: ${res.error}`, 8000);
                else if (res.brainSupport && res.ok) this.app.toast(`The game compiles it (${res.label || "ok"})`);
                return;
            }
            const errs = res.errors || [];
            if (errs.length) { this.app.toast(`quark_tests checkbrain: ${errs.length} error(s): ${errs[0].line}:${errs[0].col} ${errs[0].msg || errs[0].message}`, 8000); this.gameCheck = null; }
            else if (res.ok) {
                // the game's verdict: its label, and which slots leave the native code (only changed ones do)
                const interp = (res.slots || []).filter((x) => !x.native).map((x) => x.slot);
                this.gameCheck = { label: res.label, interp, warnings: (res.warnings || []).length };
                this.app.toast(`The game compiles it: ${res.label} · ${interp.length ? interp.length + " slot" + (interp.length === 1 ? "" : "s") + " interpreted (" + interp.join(", ") + "), the rest native" : "every slot native (only numbers changed)"}`, 6000);
                this._problems && this._problems();
            }
        } catch {}
    }

    _wire() {
        document.querySelectorAll("#viewTabs [data-view]").forEach((b) => (b.onclick = () => this._showView(b.dataset.view)));
        $("brainSel").onchange = () => this.open($("brainSel").value);
        $("brainNew").onclick = async () => {
            const n = await this.app.prompt("New brain", "mybrain", "Starts as a copy of the default brain.");
            if (!n) return;
            const def = this.app.defaultAst();
            this.name = n; this.saved = null;
            this._set(numberIds(stripIds({ ...clone(def), name: n })), null);
            this._fillSel();
        };
        $("brainSave").onclick = () => this.save();
        $("brainSaveAs").onclick = async () => { const n = await this.app.prompt("Save as", this.name === "default" ? "mybrain" : this.name + "2", "Letters, digits, _ . -"); if (n) this.save(n); };
        $("brainDownload").onclick = () => {
            if (this.codeError) { this.app.toast("The code has an error"); return; }
            const n = this.name === "default" ? "mybrain" : this.name;
            this.app.download(n + ".qbrain", this.fileText());
            api.provenance(n, this._provenance()).catch(() => {});
        };
        $("brainUpload").onclick = async () => {
            const f = await this.app.pickFile(".qbrain,.txt");
            if (!f) return;
            try { const ast = numberIds(JSON.parse(JSON.stringify(parse(f.text)))); this.name = f.name.replace(/\.qbrain$|\.txt$/i, "").replace(/[^A-Za-z0-9_.-]/g, "_") || "uploaded"; this.saved = null; this._set(this.mergeOverDefault(ast), null); this._fillSel(); }
            catch (e) { this.app.toast("Can't read it: " + e.message, 6000); }
        };
        $("brainResetAll").onclick = () => { const def = this.app.defaultAst(); if (def) this.brainEdited(stripIds({ ...clone(def), name: this.ast.name }), null); };
        $("itemFilter").oninput = () => this._items();
        $("undo").onclick = () => this.undo(-1);
        $("redo").onclick = () => this.undo(1);
        const ta = $("codeTa");
        ta.addEventListener("input", () => { this._highlight(); clearTimeout(this._codeT); this._codeT = setTimeout(() => this._codeEdited(false), 250); });
        ta.addEventListener("blur", () => { clearTimeout(this._codeT); this._codeEdited(true); });
        ta.addEventListener("keydown", (e) => {
            if (e.key === "Tab") { e.preventDefault(); const s = ta.selectionStart; ta.setRangeText("  ", s, ta.selectionEnd, "end"); ta.dispatchEvent(new Event("input")); }
            else if (e.key === "Enter") {   // keep the indent (one more after a ':')
                const s = ta.selectionStart, line = ta.value.slice(ta.value.lastIndexOf("\n", s - 1) + 1, s);
                const ind = /^ */.exec(line)[0] + (/:\s*(#.*)?$/.test(line) ? "  " : "");
                e.preventDefault(); ta.setRangeText("\n" + ind, s, ta.selectionEnd, "end"); ta.dispatchEvent(new Event("input"));
            }
        });
        $("codeScroll").addEventListener("scroll", () => { $("gutter").scrollTop = $("codeScroll").scrollTop; });
        window.addEventListener("keydown", (e) => {
            if (this.app.page !== "brain") return;
            const inCode = document.activeElement === ta;
            if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") { e.preventDefault(); if (inCode) this._codeEdited(true); this.save(); }
            else if (!inCode && (e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z" && !/INPUT|SELECT/.test(document.activeElement.tagName)) { e.preventDefault(); this.undo(e.shiftKey ? 1 : -1); }
            else if (!inCode && (e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "y" && !/INPUT|SELECT/.test(document.activeElement.tagName)) { e.preventDefault(); this.undo(1); }
        });
        // "whole brain" option next to Save
        const lab = document.createElement("label");
        lab.className = "check muted";
        lab.innerHTML = `<input type="checkbox" id="brainWhole"><span>save the whole brain (else only what differs from the default)</span>`;
        $("brainHuman").parentElement.after(lab);
    }
}
