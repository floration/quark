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
const OWN_HEADER = /^# .*: a bot brain made in the Quark bot editor/;

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
        addEventListener("qbe-theme", () => { for (const v of ["blocks", "graph", "values"]) this.dirtyViews.add(v); if (this.app.page === "brain") this._refreshView(); });   // (canvases take the theme's colours when drawn)
    }

    async start() {
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
        this._fillSel();   // (after the name is restored: a brain that was never saved gets its "(not saved)" entry)
        this._brainInfo();   // (saved / unsaved: known only now)
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
    fileText(name = this.ast.name) {
        const def = this.app.defaultAst(), whole = !def || ($("brainWhole") && $("brainWhole").checked);
        let decls = this.ast.decls;
        if (!whole) { const changed = new Set(diffBrains(this.ast, def).filter((d) => d.change !== "default").map((d) => d.key)); decls = decls.filter((d) => changed.has(declKey(d))); }
        return print({ ...this.ast, name, decls: this._ownHeader(decls, name, whole) });
    }
    // A saved brain opens with its own comment, not the default's file header (the comments on the default's first
    // declaration before its first blank line: "The built-in bot brain ...").
    _ownHeader(decls, name, whole) {
        if (!decls.length) return decls;
        const def = this.app.defaultAst(), d0 = def && def.decls[0] && def.decls[0].comments || [];
        const defHead = new Set(d0.slice(0, Math.max(0, d0.indexOf(""))).filter(Boolean));
        const own = (d) => {
            const c = (d.comments || []).filter((x) => !defHead.has(x) && !OWN_HEADER.test(x));
            while (c[0] === "") c.shift();
            return c;
        };
        const line = `# ${name}: a bot brain made in the Quark bot editor` + (whole ? " (the whole brain)." : ": only what differs from the default brain (tools/bots/default.qbrain), which plays the rest.");
        return decls.map((d, i) => { const c = own(d); return { ...d, comments: i ? c : [line, "", ...c] }; });
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

    // unsaved work: an edit since the last save / open, a brain never saved (New, Open file), or a draft that doesn't parse
    isDirty() {
        if (!this.ast) return false;
        if (this.codeError) return true;
        return this.saved == null || this.saved !== print(this.ast);
    }
    // before replacing the open brain (Open, New, Open file): save, discard or stay. True: go on.
    async leaveOk(what) {
        if (!this.isDirty()) return true;
        const k = await this.app.choose(`${this.name}.qbrain has unsaved changes`, `${what} replaces the brain open in the editor.${this.codeError ? "\nThe Code view has an error, so it can't be saved as it is." : ""}`,
            [{ k: "cancel", label: "Cancel" }, { k: "discard", label: "Discard changes", danger: true }, ...(this.codeError ? [] : [{ k: "save", label: "Save first", primary: true }])]);
        if (k === "save") return await this.save();
        return k === "discard";
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
        const gNow = this._gameNow();
        const probLines = new Set([...((this.check && this.check.problems) || []), ...(gNow ? gNow.errors : [])].filter((p) => p.severity !== "warning").map((p) => p.line));
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
    // the brain this one is compared with: the default, the open file as last saved, or another saved brain (merged
    // over the default, as a bot plays it)
    async _diffBaseAst(base) {
        if (base === "default") return this.app.defaultAst();
        if (base === "(saved)") { try { return this.saved ? numberIds(JSON.parse(JSON.stringify(parse(this.saved)))) : null; } catch { return null; } }
        if (!this.app.brainAst.has(base)) await this.app.loadBrainText(base).catch(() => {});
        const a = this.app.brainAst.get(base);
        return a ? this.mergeOverDefault(a) : null;
    }
    async _diff() {
        const R = $("diff"), def = this.app.defaultAst();
        if (!def) { R.innerHTML = `<div class="muted">No default brain loaded.</div>`; return; }
        const opts = [["default", "the default brain"], ...(this.saved && this.saved !== print(this.ast) ? [["(saved)", `${this.name}.qbrain as saved (your unsaved changes)`]] : []),
            ...this.app.brainList.filter((b) => b.source === "user" && b.name !== this.name).map((b) => [b.name, b.name + ".qbrain"])];
        if (!opts.some((o) => o[0] === this.diffBase)) this.diffBase = "default";
        const base = this.diffBase, gen = (this._diffGen = (this._diffGen || 0) + 1);
        const B = await this._diffBaseAst(base);
        if (gen !== this._diffGen) return;   // (a newer redraw started meanwhile)
        const head = `<div class="row" style="margin-bottom:8px"><span>Compare with</span><select id="diffBase">${opts.map(([v, t]) => `<option value="${esc(v)}"${v === base ? " selected" : ""}>${esc(t)}</option>`).join("")}</select></div>`;
        const wire = () => { $("diffBase").onchange = () => { this.diffBase = $("diffBase").value; this._diff(); }; };
        if (!B) { R.innerHTML = head + `<div class="err">Couldn't read ${esc(base)}.</div>`; wire(); return; }
        const baseName = base === "default" ? "the default brain" : base === "(saved)" ? "the saved file" : base + ".qbrain";
        const items = diffBrains(this.ast, B).filter((d) => d.change !== "default");
        if (!items.length) { R.innerHTML = head + `<div class="muted">Same as ${esc(baseName)}.</div>`; wire(); return; }
        R.innerHTML = head + `<div class="muted" style="margin-bottom:8px">${items.length} declaration${items.length > 1 ? "s" : ""} differ from ${esc(baseName)}.${base === "default" ? " Saving writes only these (unless “whole brain” is ticked)." : " (- its line, + this brain's.)"}</div>`;
        wire();
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
            const bt = document.createElement("button"); bt.textContent = it.change === "added" ? "remove" : base === "default" ? "reset to default" : "take " + (base === "(saved)" ? "the saved one" : base + "'s");
            bt.onclick = () => this._resetDecl(it.key, B);
            box.querySelector("header").append(bt);
            R.append(box);
        }
    }
    _resetDecl(key, def = this.app.defaultAst()) {
        const ast = clone(this.ast);
        const i = ast.decls.findIndex((d) => declKey(d) === key), o = def.decls.find((d) => declKey(d) === key);
        if (i < 0) return;
        if (o) ast.decls[i] = clone(o); else ast.decls.splice(i, 1);
        this.brainEdited(ast, null);
    }

    // ---- left: items, right: problems / info
    _brainInfo(changed) {
        if (!changed) { const def = this.app.defaultAst(); changed = new Set(def && this.ast ? diffBrains(this.ast, def).filter((d) => d.change !== "default").map((d) => d.key) : []); }
        $("brainInfo").innerHTML = `${esc(this.name)}.qbrain · ${changed.size} change${changed.size === 1 ? "" : "s"} vs default${this.isDirty() ? ` · <span class="warn">${this.saved == null ? "not saved yet" : "unsaved"}</span>` : ""}`;
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
    // the game's problems when they are about the text the Code view shows now
    _gameNow() { const g = this.gameResult; return g && g.forText === this.codeText ? g : null; }
    _problems() {
        const P = $("problems"), c = this.check;
        if (!c) return;
        const n = c.problems.length, canGame = !!(this.app.config.quarkTests || this.app.config.maptool);
        $("checkSummary").innerHTML = n ? `${c.errors.length} error${c.errors.length === 1 ? "" : "s"}, ${n - c.errors.length} warning${n - c.errors.length === 1 ? "" : "s"} <span class="muted">(the editor's checks; the game's compiler is the judge${canGame ? ": Check now" : ": quark_tests not found"})</span>` : `Clean. <span class='muted'>(the editor's checks${canGame ? "; the game's compiler: Check now" : ""})</span>`;
        const g = this.gameResult, gNow = this._gameNow();
        if (g) $("checkSummary").insertAdjacentHTML("beforeend", `<div class="muted" style="font-size:12px;margin-top:4px">The game's compiler${gNow ? "" : " <span class='warn'>(the text changed since: check again)</span>"}: ${g.unsupported ? "this build has no brain code" : g.errors.length ? `<b class="err">${g.errors.length} error${g.errors.length > 1 ? "s" : ""}</b>` : `<b>${esc(g.label || "compiles")}</b>${g.interp ? " · " + (g.interp.length ? `interpreted: ${esc(g.interp.join(", "))}` : "every slot native") : ""}`}${g.warnings.length ? `, ${g.warnings.length} warning${g.warnings.length > 1 ? "s" : ""}` : ""}</div>`);
        const items = [...(gNow ? [...gNow.errors.map((p) => ({ ...p, severity: "error", where: "game" })), ...gNow.warnings.map((p) => ({ ...p, severity: "warning", where: "game" }))] : []), ...c.problems].slice(0, 200);
        P.innerHTML = items.map((p, i) => `<div class="prob ${p.severity}${p.where === "game" ? " game" : ""}" data-i="${i}" title="${p.line ? "go to line " + p.line : ""}">${p.line ? p.line + ":" + p.col + " " : ""}${esc(p.where || "")}: ${esc(p.msg)}</div>`).join("");
        P.querySelectorAll(".prob").forEach((e) => (e.onclick = () => { const p = items[+e.dataset.i]; this.gotoLine(p.line, p.col); }));
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
    // Save / Save as. A draft that doesn't parse can't be saved; a brain with check errors (the editor's or the game's
    // compiler's) only after a confirm, since a run or batch refuses a brain that doesn't compile. True: saved.
    async save(asName) {
        if (!this.ast) return false;
        if (this.codeError) { this.app.toast("The code has an error: fix it first (" + this.codeError.message + ")", 5000); return false; }
        let name = asName || this.name;
        if (name === "default") {
            name = await this.app.promptName("Save as", "mybrain", "The built-in default can't be overwritten: save your brain under a name (letters, digits, _ . -). It goes to " + this.app.brainsDir);
            if (!name || !(await this._overwriteOk(name))) return false;
        }
        if (!/^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$/.test(name) || name === "default") { this.app.toast("Bad name: letters, digits, _ . - (not 'default')"); return false; }
        // the game's compiler on the Code view's text (so its line numbers are the Code view's), under the saved name:
        // the hash in its label is of what the brain does, the same for this text as for the file
        const g = await this.gameCheck(this.codeText.replace(/^brain "[^"\n]*"$/m, `brain "${name}"`));
        const nEd = this.check ? this.check.errors.length : 0, nGame = g ? g.errors.length : 0;
        if (nEd || nGame) {
            const first = nGame ? g.errors[0] : this.check.errors[0];
            const k = await this.app.choose(`${name}.qbrain doesn't pass its checks`,
                `${nEd ? `The editor's checks: ${nEd} error${nEd > 1 ? "s" : ""}. ` : ""}${g ? `The game's compiler: ${nGame ? nGame + " error" + (nGame > 1 ? "s" : "") : "compiles it"}.` : ""}\nFirst: ${first.line ? first.line + ":" + first.col + " " : ""}${first.msg}\n\nSimulator runs and batches refuse a brain that doesn't compile. Every error is under Checks (right); click one for its line.`,
                [{ k: "save", label: "Save anyway", danger: true }, { k: "cancel", label: "Cancel", primary: true }]);
            if (k !== "save") return false;
        }
        const text = this.fileText(name);
        try {
            const r = await api.saveBrain(name, text, this._provenance());
            this.ast = { ...this.ast, name };
            this.codeText = print(this.ast);
            if (g) g.forText = this.codeText;   // (only the header line changed: its lines are still the Code view's)
            this.name = name;
            this.app.brainText.set(name, text);
            this.app.brainAst.set(name, numberIds(JSON.parse(JSON.stringify(parse(text)))));
            await this.app.refreshBrains();
            this.saved = print(this.ast);
            this._fillSel();
            this.dirtyViews.add("code");
            this._afterChange(null);
            this.app.toast(`Saved ${r.path}${g && !nGame && g.label ? ` · the game compiles it: ${g.label} · ${g.interp && g.interp.length ? g.interp.length + " slot" + (g.interp.length === 1 ? "" : "s") + " interpreted (" + g.interp.join(", ") + "), the rest native" : "every slot native (only numbers changed)"}` : ""}`, g ? 6000 : 2500);
            return true;
        } catch (e) { this.app.toast("Save failed: " + e.message, 6000); return false; }
    }
    async _overwriteOk(name) {
        if (name === this.name || !this.app.brainList.some((b) => b.source === "user" && b.name === name)) return true;
        return (await this.app.choose(`${name}.qbrain exists`, `Replace it with this brain? (Its current version is kept as ${name}.qbrain.bak.)`,
            [{ k: "no", label: "Cancel" }, { k: "yes", label: "Replace", danger: true }])) === "yes";
    }
    // The game's own compiler (quark_tests checkbrain, else quark_maptool simcheck) on a whole brain's text. Its errors
    // join the Checks list (and the gutter) while the Code view still shows the text it checked (forText).
    async gameCheck(text = this.codeText, forText = this.codeText) {
        if (!this.app.config.quarkTests && !this.app.config.maptool) return null;
        let r;
        try { r = await api.checkBrain(text); } catch (e) { this.app.toast("The game's check failed: " + e.message, 6000); return null; }
        if (!r || !r.available) return null;
        const res = r.result || {}, P = (e) => ({ line: e.line || 0, col: e.col || 0, msg: e.msg || e.message || String(e) });
        let g;
        if (r.via === "simcheck") {   // {brainSupport, ok, label?, error?: "line:col message"}
            const m = /^(\d+):(\d+):?\s*(.*)$/s.exec(res.error || "");
            g = { via: "quark_maptool simcheck", unsupported: !res.brainSupport, label: res.label, interp: null, warnings: [],
                errors: !res.brainSupport || res.ok ? [] : [m ? { line: +m[1], col: +m[2], msg: m[3] } : { line: 0, col: 0, msg: res.error || "doesn't compile" }] };
        } else if (res.raw != null) g = { via: "quark_tests checkbrain", errors: [{ line: 0, col: 0, msg: "no answer: " + String(res.stderr || res.raw).slice(-300) }], warnings: [] };
        else g = { via: "quark_tests checkbrain", label: res.label, errors: (res.errors || []).map(P), warnings: (res.warnings || []).map(P),
            // which slots leave the native code (only those whose code differs from the default)
            interp: res.ok ? (res.slots || []).filter((x) => !x.native).map((x) => x.slot) : null };
        g.forText = forText;
        this.gameResult = g;
        this._problems(); this._highlight();
        return g;
    }
    gotoLine(line, col = 1) {
        if (!line) return;
        this._showView("code");
        const ta = $("codeTa"), lines = ta.value.split("\n");
        let at = 0;
        for (let i = 0; i < Math.min(line - 1, lines.length); i++) at += lines[i].length + 1;
        at = Math.min(ta.value.length, at + Math.max(0, col - 1));
        $("codeScroll").scrollTop = Math.max(0, (line - 1) * LINE_H - 80);
        ta.focus({ preventScroll: true }); ta.setSelectionRange(at, at);
    }

    _wire() {
        document.querySelectorAll("#viewTabs [data-view]").forEach((b) => (b.onclick = () => this._showView(b.dataset.view)));
        $("brainSel").onchange = async () => {
            const n = $("brainSel").value;
            if (n === this.name) return;
            $("brainSel").value = this.name;   // (until it's sure the open brain may go)
            if (await this.leaveOk(`Opening ${n}`)) await this.open(n);
        };
        $("brainRefresh").onclick = async () => {
            const before = new Set(this.app.brainList.map((b) => b.name));
            try { await this.app.refreshBrains(); } catch (e) { this.app.toast("Couldn't list the brains: " + e.message); return; }
            for (const b of this.app.brainList) if (b.source === "user") { this.app.brainText.delete(b.name); this.app.brainAst.delete(b.name); }   // (re-read on use: changed outside the page)
            const added = this.app.brainList.filter((b) => !before.has(b.name)).map((b) => b.name);
            this.app.toast(`${this.app.brainList.length} brain${this.app.brainList.length === 1 ? "" : "s"} in ${this.app.brainsDir}${added.length ? " · new: " + added.join(", ") : ""}`);
        };
        $("brainCheck").onclick = async () => {
            if (this.codeError) { this.gotoLine(this.codeError.line, this.codeError.col); this.app.toast("The Code view doesn't parse yet: " + this.codeError.message, 5000); return; }
            $("brainCheck").disabled = true;
            const g = await this.gameCheck().finally(() => ($("brainCheck").disabled = false));
            if (!g) this.app.toast("The game's compiler isn't here (quark_tests / quark_maptool not found)");
            else this.app.toast(g.unsupported ? "This build has no brain code: it can't compile brains" : g.errors.length ? `The game's compiler: ${g.errors.length} error${g.errors.length > 1 ? "s" : ""} (under Checks)` : `The game compiles it: ${g.label || "ok"}`, 4000);
        };
        $("brainNew").onclick = async () => {
            if (!(await this.leaveOk("A new brain"))) return;
            const n = await this.app.promptName("New brain", "mybrain", "Starts as a copy of the default brain.", ["default", ...this.app.brainList.map((b) => b.name)]);
            if (!n) return;
            const def = this.app.defaultAst();
            this.name = n; this.saved = null; this.hist = []; this.histIx = -1;
            this._set(numberIds(stripIds({ ...clone(def), name: n })), null);
            this._fillSel();
        };
        $("brainSave").onclick = () => this.save();
        $("brainSaveAs").onclick = async () => {
            const n = await this.app.promptName("Save as", this.name === "default" ? "mybrain" : this.name + "2", "Letters, digits, _ . -");
            if (n && (await this._overwriteOk(n))) this.save(n);
        };
        $("brainDownload").onclick = () => {
            if (this.codeError) { this.app.toast("The code has an error"); return; }
            const n = this.name === "default" ? "mybrain" : this.name;
            this.app.download(n + ".qbrain", this.fileText());
            api.provenance(n, this._provenance()).catch(() => {});
        };
        $("brainUpload").onclick = async () => {
            if (!(await this.leaveOk("Opening a file"))) return;
            const f = await this.app.pickFile(".qbrain,.txt");
            if (!f) return;
            try {
                const ast = numberIds(JSON.parse(JSON.stringify(parse(f.text))));
                this.name = f.name.replace(/\.qbrain$|\.txt$/i, "").replace(/[^A-Za-z0-9_.-]/g, "_").replace(/^[^A-Za-z0-9]+/, "") || "uploaded";
                ast.name = this.name;   // (the game's label names the file, not what its header said)
                this.saved = null; this.hist = []; this.histIx = -1;
                this._set(this.mergeOverDefault(ast), null); this._fillSel();
            }
            catch (e) { this.app.toast("Can't read it: " + e.message, 6000); }
        };
        $("brainResetAll").onclick = async () => {
            const def = this.app.defaultAst();
            if (!def || !this.ast) return;
            if (diffBrains(this.ast, def).every((d) => d.change === "default")) { this.app.toast("Already the same as the default brain"); return; }
            if ((await this.app.choose("Reset every declaration?", "Every param, curve, list and slot goes back to the default brain's. Undo (Ctrl+Z) brings your version back.", [{ k: "no", label: "Cancel" }, { k: "yes", label: "Reset all", danger: true }])) === "yes")
                this.brainEdited(stripIds({ ...clone(def), name: this.ast.name }), null);
        };
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
