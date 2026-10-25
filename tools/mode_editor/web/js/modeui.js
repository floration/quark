// Adapted from tools/bot_editor/web/js/brainui.js at c7aae27. The mode page: ONE in-memory AST (the logic merged
// over the built-in texts: common, then the base mode, then the file) and its views. Code (parsed back on edit / blur,
// errors at line:col, the last good AST kept, the broken draft kept too), Blocks, Graph, Values, Diff, Phases and Sim
// all print that AST; an edit in any of them replaces it and the others redraw.
//
// The file around it (qfile.js): the game's key=value lines stay as written; this page only patches the mode keys it
// owns and replaces the logic block. Save sends the base file's sha256 / mtime and serve.py refuses (409) when the file
// changed on disk since it was loaded. Open / new from a base / save / save as / download / open file; undo / redo;
// provenance on save.

import { parse, print, printDecl, highlightLine, ModeError } from "./qmode.js";
import { validate, clone, numberIds, carryIds, declKey, slotKey, diffLogic, interpretedSlots, highlightSets, CATALOG, symbols, deepEqual, stripIds, mergeLogic } from "./lang.js";
import { splitQmode, patchQmode, modeKeys, newQmodeText, MODE_KEYS, OWNER_TEXT, PHYSICS_EDITOR_URL } from "./qfile.js";
import { MODES, MODE_TITLES, logicBase } from "./catalog.js";
import { BlocksView } from "./blocks.js";
import { GraphView, overviewGraph } from "./graph.js";
import { ValuesView } from "./values.js";
import { PhasesView } from "./phases.js";
import { SimView } from "./sim.js";
import { api } from "./api.js";

const $ = (id) => document.getElementById(id);
const esc = (x) => String(x ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const LINE_H = 19;
const VIEWS = ["code", "blocks", "graph", "values", "diff", "phases", "sim"];
export const NAME_RE = /^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$/;
const parseIds = (text, line0) => numberIds(clone(parse(text, line0)));

// ---------------------------------------------------------------- the model, without DOM (tests use it)
// A loaded mode: the file text it came from (null for a built-in or a new one), the owned keys changed since, and the
// merged AST. fileTextFor() makes the text to write.
export function loadModeText(text, defaults, fallbackBase) {
    const sp = splitQmode(text);
    const keys = modeKeys(text);
    const base = String(keys.mode || fallbackBase || "ffa").trim().toLowerCase();
    let user = null;
    if (sp.logicText) user = parse(sp.logicText, sp.logicLine);
    const def = defaults(base);
    const ast = numberIds(stripIds(mergeLogic([def, user], logicBase(base))));
    return { base, keys, ast, def, user, logicLine: sp.logicLine };
}
// the logic block to write: only what differs from the built-in (common + base), or all of it; "" when nothing differs
export function logicTextFor(ast, def, whole) {
    if (whole || !def) return print(ast);
    const changed = new Set(diffLogic(ast, def).filter((d) => d.change !== "default").map((d) => d.key));
    if (!changed.size) return "";
    const decls = ast.decls.filter((d) => changed.has(declKey(d)));
    // no blank line between `base` and the first declaration written (its blank lines separated it from a decl that
    // isn't written), so a file loads and saves back to the same text
    const c = decls[0].comments;
    if (Array.isArray(c) && c.length && c[0].trim() === "") {
        let i = 0;
        while (i < c.length && c[i].trim() === "") i++;
        decls[0] = { ...decls[0], comments: c.slice(i) };
    }
    return print({ ...ast, decls });
}
// an ordered line diff (longest common subsequence): [[" " | "-" | "+", line]] in the order the lines read
export function lineDiff(a, b) {
    const n = a.length, m = b.length, L = Array.from({ length: n + 1 }, () => new Int32Array(m + 1));
    for (let i = n - 1; i >= 0; i--) for (let j = m - 1; j >= 0; j--) L[i][j] = a[i] === b[j] ? L[i + 1][j + 1] + 1 : Math.max(L[i + 1][j], L[i][j + 1]);
    const out = [];
    let i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i] === b[j]) { out.push([" ", a[i]]); i++; j++; }
        else if (L[i + 1][j] >= L[i][j + 1]) out.push(["-", a[i++]]);
        else out.push(["+", b[j++]]);
    }
    while (i < n) out.push(["-", a[i++]]);
    while (j < m) out.push(["+", b[j++]]);
    return out;
}
export function fileTextFor(original, keySet, ast, def, whole) { return patchQmode(original, keySet, logicTextFor(ast, def, whole)); }
// A line of the saved file (checkmode counts the file's lines) -> the same line in the Code view, which shows the whole
// merged logic: both are the canonical printer's lines, so the line keeps its offset under its declaration's first line
// (the nth time that line starts a declaration in each). null: a key line, or a declaration the Code view doesn't have.
export function codeLineFor(fileText, fileLine, codeText) {
    const sp = splitQmode(fileText);
    if (!sp.logicLine || fileLine < sp.logicLine) return null;
    const file = fileText.split("\n").map((l) => l.replace(/\r$/, "")), code = codeText.split("\n");
    const head = (l) => /^[A-Za-z_]/.test(l || "");   // (a declaration, `logic` and `base` start at column 0; comments with #)
    let h = Math.min(fileLine, file.length) - 1;
    while (h >= sp.logicLine - 1 && !head(file[h])) h--;
    if (h < sp.logicLine - 1) return null;
    let nth = 0;
    for (let i = sp.logicLine - 1; i < h; i++) if (file[i] === file[h]) nth++;
    for (let i = 0, k = 0; i < code.length; i++) if (code[i] === file[h] && k++ === nth) return Math.min(code.length, i + 1 + (fileLine - 1 - h));
    return null;
}

export class ModePage {
    constructor(app) {
        this.app = app;
        this.name = "ffa";              // the file name (user mode) or the built-in mode's name
        this.builtin = true;            // a built-in mode can't be overwritten: Save asks for a name
        this.base = "ffa";
        this.fileText = null;           // the file as loaded from disk (null: built-in / new / opened from a file)
        this.fileMeta = null;           // {mtime, sha256} of the loaded file
        this.keyBase = "";              // the key=value text edits patch (the file, or newQmodeText for a new mode)
        this.keySet = {};               // owned keys changed in the page
        this.ast = null; this.def = null;
        this.codeText = ""; this.codeError = null;
        this.view = "code"; this.sel = null;
        this.hist = []; this.histIx = -1;
        this.dirtyViews = new Set();
        this.blocks = new BlocksView($("vBlocks"), this);
        this.graph = new GraphView($("vGraph"), this);
        this.values = new ValuesView($("values"), this);
        this.phases = new PhasesView($("phases"), this);
        this.sim = new SimView($("vSim"), this);
        this._wire();
        app.on("modes", () => this._fillSel());
        app.on("catalog", () => this._afterChange(null));
    }

    async start() {
        this._fillSel();
        let st = null;
        try { st = await api.state("mode"); } catch {}
        let restored = false;
        if (st && st.name) {
            try {
                if (st.fresh && typeof st.keyBase === "string") {
                    // a new / opened-from-file mode that was never saved: its key lines come from the state file
                    this._adopt(st.name, false, st.keyBase, null, loadModeText(st.keyBase, (b) => this.app.defaultFor(b)));
                    this.fileText = null;
                } else await this.open(st.name, st.builtin, true);
                if (st.base === this.base && st.text) {
                    // "unsaved" = differs from what is on disk now (keys already equal to the file's are dropped)
                    const diskSig = this.savedSig, disk = modeKeys(this.keyBase);
                    this.keySet = Object.fromEntries(Object.entries(st.keySet || {}).filter(([k, v]) => disk[k] !== v));
                    this._set(carryIds(this.ast, parseIds(st.text)), null);
                    this.savedSig = st.fresh ? null : diskSig;
                    this._modeInfo();
                    if (st.draft && st.draft !== st.text) { this.codeText = st.draft; $("codeTa").value = st.draft; this._codeEdited(false); }
                }
                restored = true;
            } catch (e) { this.app.toast(`Couldn't restore your last mode (${e.message})`, 6000); }
        }
        if (!restored) await this.open("ffa", true);
        this._showView(new URLSearchParams(location.search).get("view") || "code");
    }

    // ---------------------------------------------------------------- open / new
    async open(name, builtin, quiet) {
        let text, meta = null;
        if (builtin) { text = newQmodeText(name, name); }
        else {
            const r = await api.mode(name);
            text = r.text; meta = { mtime: r.mtime, sha256: r.sha256 };
        }
        this._adoptText(name, builtin, text, meta, quiet);
    }
    // a mode's text into the page; logic that doesn't parse is kept as text: the key lines are known, the logic goes to
    // the Code view as a draft (its error at line:col)
    _adoptText(name, builtin, text, meta, quiet) {
        let m;
        try { m = loadModeText(text, (b) => this.app.defaultFor(b), builtin ? name : null); }
        catch (e) {
            if (!(e instanceof ModeError)) throw e;
            this._adopt(name, builtin, text, meta, loadModeText(patchQmode(text, {}, ""), (b) => this.app.defaultFor(b)));
            this.codeText = splitQmode(text).logicText; $("codeTa").value = this.codeText; this._codeEdited(false);
            if (!quiet) this.app.toast(`${name}.qmode's logic doesn't parse (${e.message}): it's in the Code view`, 8000);
            return;
        }
        this._adopt(name, builtin, text, meta, m);
        if (m.user && m.user.base && m.user.base !== m.base) this.app.toast(`the logic says base ${m.user.base} but the file's mode=${m.base}: the game refuses that (Checks)`, 8000);
    }
    _adopt(name, builtin, text, meta, m) {
        this.name = name; this.builtin = !!builtin;
        this.fileText = builtin ? null : text; this.fileMeta = meta;
        this.keyBase = text; this.keySet = {};
        this.base = m.base; this.def = m.def;
        this.hist = []; this.histIx = -1; this.sel = null;
        this.gameCheck = null;
        this._set(m.ast, null);
        this.savedSig = this._sig();
        this._fillSel();
        this._modeInfo();
        this._persist();
        this._loadProvenance();
    }
    // "I wrote this myself" says what provenance.json says of this mode (a built-in or new one: unticked), so a re-save
    // keeps a human-made mode human and another mode never inherits the tick
    async _loadProvenance() {
        const box = $("modeHuman"), name = this.name;
        box.checked = false;
        if (this.builtin || !this.fileMeta) return;
        try {
            const r = await api.provenanceOf(name);
            if (this.name === name && !this.builtin) box.checked = !!(r.item && r.item.origin === "human");
        } catch {}
    }
    async newFrom(name, base) {
        const text = newQmodeText(name, base);
        const m = loadModeText(text, (b) => this.app.defaultFor(b));
        this._adopt(name, false, text, null, m);
        this.fileText = null; this.savedSig = null;
        this._fillSel(); this._modeInfo();
    }

    // ---------------------------------------------------------------- the one AST and the keys
    brainEdited(ast, from) { this._set(numberIds(ast), from); }
    setKey(k, v) {
        const cur = modeKeys(patchQmode(this.keyBase, this.keySet, ""))[k];
        if (cur === v) return;
        this.keySet = { ...this.keySet, [k]: v };
        this._push(); this._afterChange("keys");
    }
    keyText() { return patchQmode(this.keyBase, this.keySet, ""); }
    currentKeys() { return modeKeys(this.keyText()); }
    _set(ast, from) {
        this.ast = ast;
        if (from !== "code") { this.codeText = print(ast); this.codeError = null; }
        this._push();
        this._afterChange(from);
    }
    _sig() { return this.ast ? JSON.stringify([print(this.ast), this.keySet]) : null; }
    _push() {
        const t = this._sig();
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
        const [text, keys] = JSON.parse(this.hist[i]);
        this.ast = carryIds(this.ast, parseIds(text));
        this.keySet = keys;
        this.codeText = text; this.codeError = null;
        this._afterChange(null);
    }

    // located: the tree the Code view's text parsed to (its nodes carry line:col; this.ast is a clone, which drops them)
    _afterChange(from, located) {
        if (!this.ast) return;
        // checks run on the canonical text so problems carry line:col of the Code view
        if (!located) { try { located = parse(this.codeText); } catch { located = this.ast; } }
        this.check = validate(located, { base: this.base, def: this.def });
        if (this.ast.base && this.ast.base !== logicBase(this.base)) this.check.problems.unshift({ severity: "error", msg: `base ${this.ast.base} doesn't match the file's mode=${this.base}`, where: "base", line: 2, col: 1 });
        this.check.errors = this.check.problems.filter((p) => p.severity === "error");
        this.interp = this.def ? interpretedSlots(this.ast, this.def) : new Set();
        for (const v of VIEWS) if (v !== from) this.dirtyViews.add(v);
        if (from === "code") this.dirtyViews.delete("code");
        this._refreshView();
        this._items(); this._problems(); this._info(); this._modeInfo();
        clearTimeout(this._persistT);
        this._persistT = setTimeout(() => this._persist(), 800);
    }
    // the work in progress as a file through serve.py (the shell's browser storage starts empty)
    _persist() {
        if (!this.ast) return;
        const fresh = !this.builtin && !this.fileMeta;
        api.saveState("mode", { name: this.name, builtin: this.builtin, base: this.base, text: print(this.ast), keySet: this.keySet,
            fresh, keyBase: fresh ? this.keyBase : null,
            draft: this.codeError ? this.codeText : null, savedSig: this.savedSig || null }).catch(() => {});
    }
    // the text a save would write (null while the code has an error); keySet: the keys to write (default: the page's)
    saveText(keySet = this.keySet) {
        if (this.codeError) return null;
        return fileTextFor(this.keyBase, keySet, this.ast, this.def, $("modeWhole").checked);
    }
    // the built-in base mode with the same keys (Sim's compare): the key lines without a logic block
    builtinText() { return patchQmode(this.keyBase, this.keySet, ""); }
    // a built-in mode as it ships (Sim sends it as `mode`, not as rules)
    isPristineBuiltin() { return this.builtin && this.savedSig === this._sig(); }
    // whether the game takes this base as a custom mode's (customrules.cpp ignores mode=tourney)
    baseTakesCustom() { return !(CATALOG.modeInfo[this.base] && CATALOG.modeInfo[this.base].noCustom); }
    // whether a run of this text differs from the built-in base mode with the same keys (Sim's compare): its logic
    // differs, or the game would play it as another base (a mode=tourney custom file loads as a free for all)
    differsFromBuiltin() {
        if (this.isPristineBuiltin()) return false;
        if (!this.baseTakesCustom()) return true;
        try { return !this.def || logicTextFor(this.ast, this.def, false) !== ""; } catch { return true; }
    }

    // ---------------------------------------------------------------- views
    _showView(v) {
        if (!VIEWS.includes(v)) v = "code";
        this.view = v;
        document.querySelectorAll("#viewTabs [data-view]").forEach((b) => b.classList.toggle("on", b.dataset.view === v));
        for (const id of VIEWS) $("v" + id[0].toUpperCase() + id.slice(1)).classList.toggle("on", id === v);
        this.dirtyViews.add(v);
        this._refreshView(true);
    }
    _refreshView(focus) {
        const v = this.view;
        if (!this.dirtyViews.has(v) || !this.ast) return;
        this.dirtyViews.delete(v);
        if (v === "code") { this._keyLines(); this._codeFromText(this.codeText); }
        else if (v === "blocks") this.blocks.setBrain(this.ast, this.sel || undefined);
        else if (v === "graph") { this.graph.setBrain(this.ast, this.sel && /^(slot|curve|proc):/.test(this.sel) ? this.sel : undefined); if (focus) requestAnimationFrame(() => this.graph.fit()); }
        else if (v === "values") { this.values.setBrain(this.ast, this.def, { keys: splitQmode(this.keyText()).lines, modeKeys: this.currentKeys() }); if (this.sel) this.values.scrollTo(this.sel); }
        else if (v === "diff") this._diff();
        else if (v === "phases") this.phases.setBrain(this.ast, this.def, this.check);
        else if (v === "sim") this.sim.show();
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
    // Phases: a slot nobody writes -> add an empty one. The game interprets every slot the file writes, an empty one too
    // (quark_tests checkmode: `slot reset:` / `pass` is interpreted, 1 instruction), so the dialog says so.
    async addSlot(name) {
        const cat = CATALOG.slots[name];
        if (!cat) return;
        if (cat.timer) { this.app.toast("a timer slot is `slot timer <name>:` after a `timer <name>` declaration (write it in Code)"); return; }
        if (!confirm(`Add an empty slot ${name}? (${cat.when})\n\nIt runs interpreted from now on (the game interprets every slot the file writes, an empty one too: about one instruction a call). Empty, ${cat.kind === "query" ? "the query falls back to the native answer" : "the event does nothing"}.`)) return;
        const ast = clone(this.ast);
        ast.decls.push({ kind: "slot", name, body: [{ op: "pass" }], comments: [], trail: null });
        this.brainEdited(ast, null);
        this.openDecl("slot:" + name, "code");
    }

    // ---- code
    _keyLines() {
        const sp = splitQmode(this.keyText());
        const box = $("keyLines");
        box.innerHTML = `<div class="kl-head">key lines <span class="muted">(read-only here: edit this editor's keys in Values; the others keep exactly what they say)</span> <a href="#" id="klToggle">${this._klOpen ? "hide" : "show"} ${sp.lines.length}</a></div>`
            + (this._klOpen ? `<pre>${sp.lines.map((l) => `<span class="kl own-${l.owner}" title="${esc(OWNER_TEXT[l.owner])}">${esc(l.raw.replace(/\r$/, ""))}</span>`).join("\n")}</pre>` : "");
        $("klToggle").onclick = (e) => { e.preventDefault(); this._klOpen = !this._klOpen; this._keyLines(); };
    }
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
        if (err) { st.className = "bad"; st.textContent = `${err.line}:${err.col}  ${err.msg}   (the other views show the last version that parsed; your text is kept)`; }
        else { st.className = ""; const n = this.check ? this.check.errors.length : 0; st.textContent = `${lines.length} lines · parsed${n ? ` · ${n} check error${n > 1 ? "s" : ""} (right)` : " · checks clean"} · the whole logic (built-in + this file); a save writes only what differs`; }
    }
    _codeEdited(final) {
        const text = $("codeTa").value;
        this.codeText = text;
        const hadError = !!this.codeError;
        try {
            const parsed = parse(text), ast = carryIds(this.ast, clone(parsed));
            this.codeError = null;
            const same = this.ast && deepEqual(stripIds(ast), stripIds(this.ast));
            this.ast = ast;
            if (!same) { this._push(); this._afterChange("code", parsed); }
            if (final) { const canon = print(ast); if (canon !== text) { this.codeText = canon; this._codeFromText(canon); this._afterChange(null); } }
        } catch (e) {
            if (!(e instanceof ModeError)) throw e;
            this.codeError = e;
            this._persist();
        }
        if (hadError !== !!this.codeError) { this._problems(); this._modeInfo(); }
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

    // ---- diff vs built-in
    _diff() {
        const R = $("diff"), def = this.def;
        R.textContent = "";
        const missing = this.app.missingDefault(this.base);
        if (missing) R.insertAdjacentHTML("beforeend", `<div class="warn" style="margin-bottom:8px">${esc(missing)}</div>`);
        // keys
        const k = this.currentKeys(), d0 = modeKeys(newQmodeText(k.custommode || this.name, this.base));
        const kd = ["timelimit", "scorelimit", "rounds", "maps"].filter((x) => (k[x] ?? "") !== (d0[x] ?? ""));
        if (kd.length) {
            const box = document.createElement("div"); box.className = "dItem";
            box.innerHTML = `<header><b>mode keys</b><span class="chip">changed</span><span class="grow"></span></header><pre>${kd.map((x) => `<span class="del">- ${esc(x)}=${esc(d0[x] ?? "")}</span><span class="add">+ ${esc(x)}=${esc(k[x] ?? "")}</span>`).join("")}</pre>`;
            const bt = document.createElement("button"); bt.textContent = "reset keys";
            bt.onclick = () => { const set = { ...this.keySet }; for (const x of kd) set[x] = d0[x] ?? ""; this.keySet = set; this._push(); this._afterChange("keys"); };
            box.querySelector("header").append(bt);
            R.append(box);
        }
        const items = def ? diffLogic(this.ast, def).filter((d) => d.change !== "default") : [];
        const head = document.createElement("div"); head.className = "muted"; head.style.marginBottom = "8px";
        head.innerHTML = items.length ? `${items.length} declaration${items.length > 1 ? "s" : ""} differ from the built-in ${esc(this.base)} (common + ${esc(this.base)}). A save writes only these (unless “whole logic” is ticked). Interpreted slots: <b>${esc([...this.interp].join(", ") || "none (all native)")}</b>.`
            : `Same logic as the built-in ${esc(this.base)}${kd.length ? "" : " and the same keys"}: the file gets no logic block, and every slot runs native.`;
        R.prepend(head);
        for (const it of items) {
            const box = document.createElement("div"); box.className = "dItem";
            const a = it.def ? printDecl({ ...it.def, comments: undefined }) : [], b = printDecl({ ...it.decl, comments: undefined });
            box.innerHTML = `<header><b style="font-family:var(--mono)">${esc(it.key)}</b><span class="chip">${esc(it.change)}</span>${it.decl.kind === "slot" && this.interp.has(slotKey(it.decl)) ? '<span class="chip interp">interpreted</span>' : ""}<span class="grow"></span></header><pre></pre>`;
            let h = "";
            for (const [op, l] of lineDiff(a, b)) h += op === "-" ? `<span class="del">- ${esc(l)}</span>` : op === "+" ? `<span class="add">+ ${esc(l)}</span>` : `  ${esc(l)}\n`;
            box.querySelector("pre").innerHTML = h;
            const bt = document.createElement("button"); bt.textContent = it.change === "added" ? "remove" : "reset to built-in";
            bt.onclick = () => this._resetDecl(it.key);
            box.querySelector("header").append(bt);
            R.append(box);
        }
    }
    _resetDecl(key) {
        const ast = clone(this.ast);
        const i = ast.decls.findIndex((d) => declKey(d) === key), o = this.def && this.def.decls.find((d) => declKey(d) === key);
        if (i < 0) return;
        if (o) ast.decls[i] = clone(o); else ast.decls.splice(i, 1);
        this.brainEdited(ast, null);
    }
    resetAll() {
        if (!this.def) return;
        this.keySet = {};
        const k0 = modeKeys(newQmodeText("x", this.base)), cur = modeKeys(this.keyBase);
        for (const x of ["timelimit", "scorelimit", "rounds", "maps"]) if (cur[x] !== undefined && cur[x] !== k0[x]) this.keySet[x] = k0[x];
        this.brainEdited(stripIds(clone(this.def)), null);
    }

    // ---- left: items, right: problems / info
    _modeInfo() {
        if (!this.ast) return;
        const changed = this.def ? diffLogic(this.ast, this.def).filter((d) => d.change !== "default").length : 0;
        const unsaved = this.savedSig !== this._sig();
        $("modeInfo").innerHTML = `<b>${esc(this.name)}</b>${this.builtin ? " (built-in)" : ".qmode"} · base <b>${esc(this.base)}</b> · ${changed} change${changed === 1 ? "" : "s"} vs built-in · ${this.interp.size ? `<span class="warn">${this.interp.size} interpreted</span>` : "all native"}${unsaved ? ' · <span class="warn">unsaved</span>' : ""}`
            + (this.codeError ? `<div class="bad">The code doesn't parse (${esc(this.codeError.line)}:${esc(this.codeError.col)}): these counts are the last version that parsed.</div>` : "")
            + (CATALOG.modeInfo[this.base] && CATALOG.modeInfo[this.base].noCustom ? `<div class="bad">The game doesn't take ${esc(this.base)} as a custom mode's base: customrules.cpp ignores mode=${esc(this.base)}, so a .qmode with it loads as a free for all (CustomRules' default base) and logic written for ${esc(this.base)} doesn't match it.</div>` : "");
    }
    _items() {
        const L = $("items");
        if (!this.ast) { L.textContent = ""; return; }
        const changed = new Set(this.def ? diffLogic(this.ast, this.def).filter((d) => d.change !== "default").map((d) => d.key) : []);
        const f = ($("itemFilter").value || "").toLowerCase();
        const groups = new Map();
        const label = (d) => (d.kind === "slot" ? (d.name === "timer" ? "timer " + d.timer : d.name) : d.kind === "use" ? "use " + d.object : d.kind === "proc" ? d.name + "()" : d.name);
        const G = { slot: "slots", proc: "procs", param: "params", state: "state", timer: "timers", curve: "curves", use: "objects" };
        for (const d of this.ast.decls) {
            if (f && !(label(d).toLowerCase().includes(f) || d.kind.includes(f))) continue;
            const g = G[d.kind] || d.kind;
            if (!groups.has(g)) groups.set(g, []);
            groups.get(g).push(d);
        }
        const order = ["slots", "procs", "objects", "params", "state", "timers", "curves"];
        let h = "";
        for (const g of [...groups.keys()].sort((a, b) => order.indexOf(a) - order.indexOf(b))) {
            h += `<div class="grp">${esc(g)}</div>`;
            for (const d of groups.get(g)) {
                const k = declKey(d);
                const interp = d.kind === "slot" && this.interp.has(slotKey(d));
                h += `<div class="it${k === this.sel ? " sel" : ""}" data-k="${esc(k)}"><span>${esc(label(d))}</span>${interp ? '<span class="ha" title="runs interpreted">i</span>' : ""}${changed.has(k) ? '<span class="mod" title="differs from the built-in">●</span>' : ""}</div>`;
            }
        }
        L.innerHTML = h;
        L.querySelectorAll(".it").forEach((e) => (e.onclick = () => {
            const k = e.dataset.k;
            const d = this.ast.decls.find((x) => declKey(x) === k);
            const valueish = d && (d.kind === "param" || d.kind === "state" || d.kind === "timer" || d.kind === "use" || (d.kind === "curve" && d.form !== "expr"));
            const view = valueish ? (this.view === "code" ? "code" : "values") : ["values", "diff", "phases", "sim"].includes(this.view) ? "code" : this.view;
            this.openDecl(k, view);
        }));
        this._modeInfo();
    }
    // the problems: the code's parse error (the checks below it are the last version that parsed), the game's errors
    // from the last save (at their Code view line), then the editor's checks
    _problems() {
        const P = $("problems"), c = this.check;
        if (!c) return;
        const g = this.gameCheck;
        const list = [];
        if (this.codeError) list.push({ severity: "error", where: "parse", msg: `${this.codeError.msg} (the checks below are of the last version that parsed)`, line: this.codeError.line, col: this.codeError.col });
        for (const e of (g && g.errorList) || []) list.push({ severity: "error", where: "game (last save)", msg: e.msg + (e.fileLine ? ` (file line ${e.fileLine})` : ""), line: e.line, col: e.col });
        list.push(...c.problems);
        const nErr = list.filter((p) => p.severity === "error").length, n = list.length;
        const judge = this.app.config.checkmode ? "the game's checkmode runs on save" : "quark_tests checkmode not found: these are the editor's checks only";
        $("checkSummary").innerHTML = n ? `${nErr} error${nErr === 1 ? "" : "s"}, ${n - nErr} warning${n - nErr === 1 ? "" : "s"} <span class="muted">(${judge})</span>` : `Clean. <span class='muted'>(${judge})</span>`;
        if (g) $("checkSummary").insertAdjacentHTML("beforeend", `<div class="muted" style="font-size:12px;margin-top:4px">Last save, the game's check: <b>${esc(g.label || "")}</b> · ${g.errors ? `<span class="bad">${g.errors} error(s)</span>` : g.interp.length ? `interpreted: ${esc(g.interp.join(", "))}` : "every slot native"}</div>`);
        P.innerHTML = list.slice(0, 200).map((p, i) => `<div class="prob ${p.severity}" data-i="${i}">${p.line ? p.line + ":" + p.col + " " : ""}${esc(p.where || "")}: ${esc(p.msg)}</div>`).join("");
        P.querySelectorAll(".prob").forEach((e) => (e.onclick = () => {
            const p = list[+e.dataset.i];
            if (p.line) { this._showView("code"); $("codeScroll").scrollTop = Math.max(0, (p.line - 1) * LINE_H - 80); }
        }));
    }
    _info() {
        const I = $("itemInfo");
        const d = this.ast && this.sel && this.ast.decls.find((x) => declKey(x) === this.sel);
        if (!d) { I.innerHTML = "Pick an item on the left."; return; }
        let h = `<div><b style="font-family:var(--mono)">${esc(d.kind === "slot" ? slotKey(d) : d.kind === "use" ? d.object : d.name)}</b> <span class="chip">${esc(d.kind)}</span></div>`;
        if (d.trail) h += `<div style="margin-top:4px">${esc(d.trail.replace(/^#\s?/, ""))}</div>`;
        if (d.kind === "slot") {
            const c = CATALOG.slots[d.name];
            if (c) {
                h += `<div style="margin-top:6px">${esc(c.kind)} · called ${esc(c.when)}</div>`;
                const args = Object.entries(c.args || {});
                h += `<div style="margin-top:4px">args: ${args.map(([k, t]) => `<span class="chip">${esc(k)}: ${esc(t)}</span>`).join(" ") || "-"}</div>`;
                if (c.out) h += `<div style="margin-top:4px">returns: <span class="chip">${esc(c.out[0])}: ${esc(c.out[1])}</span></div>`;
            }
            h += `<div style="margin-top:4px">${this.interp.has(slotKey(d)) ? '<span class="warn">runs interpreted</span> (this file changes it or a proc it calls)' : "runs native (the built-in text)"}</div>`;
        }
        if (d.kind === "use") { const o = CATALOG.objects[d.object]; if (o) h += `<div style="margin-top:4px">${esc(o.doc)}</div><div style="margin-top:4px">events: ${o.events.map((e) => `<span class="chip">${esc(e)}</span>`).join(" ") || "-"}</div>`; }
        const info = this.check && this.check.info[this.sel];
        if (info) h += `<div style="margin-top:4px">cost: <b>${info.insns}</b> instructions (procs inlined) · worst <b>${info.worst}</b> a call</div>`;
        const ov = overviewGraph(this.ast);
        const gid = d.kind === "slot" ? "slot:" + slotKey(d) : d.kind === "use" ? "use:" + d.object : d.kind + ":" + d.name;
        const me = ov.nodes.find((n) => n.gid === gid);
        if (me) {
            const users = ov.edges.filter((e) => e.from === me.gid).map((e) => e.to.split(":")[1]);
            const uses = ov.edges.filter((e) => e.to === me.gid).map((e) => e.from.split(":")[1]);
            if (users.length) h += `<div style="margin-top:6px">used by: ${users.map((u) => `<span class="chip">${esc(u)}</span>`).join(" ")}</div>`;
            if (uses.length) h += `<div style="margin-top:6px">reads: ${uses.map((u) => `<span class="chip">${esc(u)}</span>`).join(" ")}</div>`;
        }
        I.innerHTML = h;
    }

    // ---------------------------------------------------------------- files
    // Rename / duplicate / delete one of the user's saved modes (serve.py: the old file stays as <name>.qmode.bak;
    // provenance moves or is copied with it). Refused (409) when the file changed on disk since the page loaded it.
    async fileOp(op) {
        const onDisk = !this.builtin && this.fileMeta && this.app.userModes.some((u) => u.name === this.name);
        if (!onDisk) { this.app.toast(this.builtin ? "A built-in mode has no file: Save as makes one" : "This mode isn't saved yet", 4000); return; }
        const from = this.name, dirty = this.savedSig !== this._sig();
        let to = null;
        if (op === "delete") {
            if (!confirm(`Delete ${from}.qmode?${dirty ? "\n\nIts unsaved changes are lost too." : ""}\n\nThe file is kept as ${from}.qmode.bak in ${this.app.modesDir} (the game doesn't load it; Backups… restores it).`)) return;
        } else {
            const what = op === "rename" ? "Rename" : "Duplicate";
            to = await this.app.prompt(`${what} ${from}`, op === "rename" ? from : from + "2",
                op === "rename" ? `The file and its custommode= line take the new name (${from}.qmode stays as ${from}.qmode.bak).${dirty ? " Your unsaved changes stay in the page." : ""}`
                    : `A copy of the saved file${dirty ? " (not your unsaved changes: Save as copies those)" : ""}, with custommode= set to the new name.`,
                (n) => this.app.modeNameProblem(n) || (n === from ? "That's its name now." : this.app.userModes.some((u) => u.name === n) ? `${n}.qmode already exists.` : null));
            if (!to) return;
        }
        let r;
        try { r = await api.modeOp(op, from, to, this.fileMeta.sha256); }
        catch (e) {
            if (e.status === 409 && e.data && e.data.changed) { this.app.toast(e.message + " Reload it (open it again) first.", 8000); return; }
            this.app.toast(`${op} failed: ${e.message}`, 8000); return;
        }
        await this.app.refreshModes();
        if (op === "delete") {
            this.savedSig = this._sig();   // (nothing to lose any more: open the base mode without asking)
            await this.open(this.base, true);
            this.app.toast(`Deleted ${from}.qmode (kept as ${r.backup})`, 6000);
        } else if (op === "rename") {
            const keep = { ast: this.ast, keySet: { ...this.keySet } };
            delete keep.keySet.custommode;
            this.name = to; this.fileText = r.text; this.fileMeta = { mtime: r.mtime, sha256: r.sha256 }; this.keyBase = r.text;
            this.keySet = keep.keySet; this.ast = keep.ast;
            if (!dirty) this.savedSig = this._sig();
            this._fillSel(); this._afterChange(null); this._persist();
            this.app.toast(`Renamed to ${to}.qmode (the old file is ${r.backup})`, 6000);
        } else {
            this._fillSel();
            this.app.toast(`Copied to ${to}.qmode (open it from the list)`, 5000);
        }
    }
    // The backups serve.py keeps (<name>.qmode.bak, the newest, to .bak5): a list with a Restore for each. A backup goes
    // back under its own name when that's free, else under a name asked for (never over a mode without asking).
    async backups() {
        let r;
        try { r = await api.backups(); } catch (e) { this.app.toast("Can't list the backups: " + e.message, 6000); return; }
        const m = document.createElement("div");
        m.className = "modal";
        const when = (t) => new Date(t * 1000).toLocaleString();
        const rows = r.backups.map((b, i) => `<tr><td><b>${esc(b.name)}</b></td><td class="muted">${b.n === 1 ? "newest" : "older " + (b.n - 1)}</td><td>${esc(when(b.mtime))}</td><td class="muted">${esc(b.size)} B</td>
            <td>${b.exists ? '<span class="muted">the mode exists</span>' : '<span class="warn">deleted or renamed</span>'}</td><td><button data-i="${i}">Restore…</button></td></tr>`).join("");
        m.innerHTML = `<div style="max-width:880px"><h3>Backups</h3><p class="muted" style="font-size:12px">A save keeps the file it replaces, and a rename or delete keeps the file it removes, as <code>&lt;name&gt;.qmode.bak</code> in ${esc(r.dir)} (the ${esc(r.keep)} newest of each name: .bak, .bak2 ... the game loads only *.qmode).</p>
            ${rows ? `<div style="max-height:60vh;overflow:auto"><table class="sim-tab bak-tab">${rows}</table></div>` : '<p class="muted">No backups yet.</p>'}
            <div class="row" style="justify-content:flex-end;margin-top:10px"><button data-x="close">Close</button></div></div>`;
        m.querySelector("[data-x=close]").onclick = () => m.remove();
        m.querySelectorAll("button[data-i]").forEach((bt) => (bt.onclick = async () => {
            const b = r.backups[+bt.dataset.i];
            const free = (n) => !this.app.userModes.some((u) => u.name.toLowerCase() === n.toLowerCase());
            let to = b.name;
            if (!free(to)) {
                let alt = b.name + "-restored";
                for (let k = 2; !free(alt); k++) alt = `${b.name}-restored${k}`;
                to = await this.app.prompt(`Restore ${b.file}`, alt, `${b.name}.qmode exists, so the backup goes back under another name (its custommode= line takes it).`,
                    (n) => this.app.modeNameProblem(n) || (free(n) ? null : `${n}.qmode already exists.`));
                if (!to) return;
            }
            let res;
            try { res = await api.restore(b.name, b.n, to, false); } catch (e) { this.app.toast("Restore failed: " + e.message, 8000); return; }
            m.remove();
            await this.app.refreshModes();
            if (this.savedSig === this._sig() || confirm(`Restored ${to}.qmode. Open it? This mode's unsaved changes are lost.`)) await this.open(to, false);
            this.app.toast(`Restored ${b.file} as ${to}.qmode`, 6000);
        }));
        document.body.append(m);
    }
    _fillSel() {
        const s = $("modeSel");
        s.textContent = "";
        const g1 = document.createElement("optgroup"); g1.label = "built-in modes";
        for (const m of MODES) { const o = document.createElement("option"); o.value = "b:" + m; o.textContent = `${m} · ${MODE_TITLES[m] || ""}`; g1.append(o); }
        s.append(g1);
        const g2 = document.createElement("optgroup"); g2.label = "your modes (" + (this.app.modesDir || "custom_modes") + ")";
        for (const u of this.app.userModes) { const o = document.createElement("option"); o.value = "u:" + u.name; o.textContent = u.name; g2.append(o); }
        if (!this.builtin && !this.app.userModes.some((u) => u.name === this.name)) { const o = document.createElement("option"); o.value = "u:" + this.name; o.textContent = this.name + " (not saved)"; g2.append(o); }
        s.append(g2);
        s.value = (this.builtin ? "b:" : "u:") + this.name;
        const onDisk = !this.builtin && !!this.fileMeta && this.app.userModes.some((u) => u.name === this.name);
        for (const id of ["modeRename", "modeDuplicate", "modeDelete"]) $(id).disabled = !onDisk;
    }
    _provenance(name) { return { items: ["gamemodes/" + name], human: $("modeHuman").checked }; }
    // One save at a time: a second click (or Ctrl+S) while one is on its way would carry the old file's sha256 and get a
    // false "changed on disk" 409, so Save is disabled until the answer is in.
    async save(asName) {
        if (!this.ast || this._saving) return;
        this._saving = true;
        $("modeSave").disabled = $("modeSaveAs").disabled = true;
        try { await this._save(asName); }
        finally { this._saving = false; $("modeSave").disabled = $("modeSaveAs").disabled = false; }
    }
    async _save(asName) {
        if (this.codeError) { this.app.toast("The code has an error: fix it first (" + this.codeError.message + ")", 5000); return; }
        const errs = (this.check && this.check.errors) || [];
        if (errs.length && !confirm(`The checks found ${errs.length} error${errs.length > 1 ? "s" : ""} (first: ${errs[0].line ? errs[0].line + ":" + errs[0].col + " " : ""}${errs[0].msg}).\n\nThe game refuses to play a mode whose logic doesn't compile. Save it anyway?`)) return;
        let name = asName || (this.builtin ? null : this.name);
        if (!name) {
            name = await this.app.prompt("Save as", "my" + this.base, `A built-in mode can't be overwritten: save yours under a name (letters, digits, _ . -). It goes to ${this.app.modesDir}`, (n) => this.app.modeNameProblem(n));
            if (!name) return;
        }
        if (!NAME_RE.test(name)) { this.app.toast("Bad name: letters, digits, _ . - (up to 64)"); return; }
        const renamed = name !== this.name || this.builtin;
        // the new name goes only into the text sent: a declined replace or a failed save leaves the page's keys as they
        // were (else the next Save would write custommode=<other name> into this file)
        const keySet = renamed ? { ...this.keySet, custommode: name } : this.keySet;
        let text;
        try { text = this.saveText(keySet); } catch (e) { this.app.toast("Can't write it: " + e.message, 8000); return; }
        const sameFile = !renamed && this.fileText !== null && this.fileMeta;
        const body = { text, baseSha256: sameFile ? this.fileMeta.sha256 : null, baseMtime: sameFile ? this.fileMeta.mtime : null, provenance: this._provenance(name), overwrite: false };
        let r;
        try { r = await api.saveMode(name, body); }
        catch (e) {
            if (e.status === 409 && e.data && e.data.exists && !sameFile) {
                if (!confirm(`${name}.qmode already exists. Replace it? (its key lines from the other editors are NOT merged: open it instead to keep them)`)) return;
                try { r = await api.saveMode(name, { ...body, overwrite: true }); } catch (e2) { this.app.toast("Save failed: " + e2.message, 8000); return; }
            } else if (e.status === 409) { this._conflict(name, e.message); return; }
            else { this.app.toast("Save failed: " + e.message, 8000); return; }
        }
        this.name = name; this.builtin = false;
        this.fileText = text; this.fileMeta = { mtime: r.mtime, sha256: r.sha256 };
        this.keyBase = text; this.keySet = {};
        this.savedSig = this._sig();
        await this.app.refreshModes();
        this._fillSel();
        this._afterChange(null);
        this.app.toast(`Saved ${r.path}`);
        this._gameResult(r.check, text);
    }
    // the file changed on disk since it was loaded: offer to reload it and keep this page's logic / key edits
    _conflict(name, msg) {
        const m = document.createElement("div");
        m.className = "modal";
        m.innerHTML = `<div><h3>Not saved: ${esc(name)}.qmode changed on disk</h3><p class="muted" style="font-size:12px">${esc(msg)}</p>
          <p style="font-size:13px">Another program (the physics editor, the game, a text editor) saved this file after you opened it. Saving now would undo its changes, so nothing was written.</p>
          <p style="font-size:13px"><b>Reload and keep my edits</b> reads the file again and puts your logic and your mode keys back on top of it (the lines you don't own come from the new file). Then save again.</p>
          <div class="row" style="justify-content:flex-end;margin-top:10px"><button data-x="0">Cancel</button><button class="primary" data-x="1">Reload and keep my edits</button></div></div>`;
        m.querySelectorAll("button").forEach((b) => (b.onclick = async () => {
            m.remove();
            if (b.dataset.x !== "1") return;
            try {
                const r = await api.mode(name);
                const keep = { ast: this.ast, keySet: this.keySet };
                this.fileText = r.text; this.fileMeta = { mtime: r.mtime, sha256: r.sha256 }; this.keyBase = r.text;
                this.keySet = keep.keySet; this.ast = keep.ast;
                this._afterChange(null);
                this.app.toast("Reloaded: the other lines are the new file's; your logic and keys are kept. Save again.", 6000);
            } catch (e) { this.app.toast("Reload failed: " + e.message, 6000); }
        }));
        document.body.append(m);
    }
    // checkmode's answer for the saved file: {ok, errors: [{line, col, msg}] (file lines), interpreted: [slot], slots:
    // [{slot, interpreted}], label}. Its errors go in Checks at their Code view line.
    _gameResult(c, fileText) {
        if (!c || !c.available) { this.gameCheck = null; this._problems(); return; }
        const res = c.result || {};
        const errs = res.errors || [];
        const interp = Array.isArray(res.interpreted) ? res.interpreted : (res.slots || []).filter((x) => x.interpreted).map((x) => x.slot);
        const errorList = errs.map((e) => {
            const at = fileText && e.line ? codeLineFor(fileText, e.line, this.codeText) : null;
            return { msg: e.msg || e.message || String(e), line: at, col: at ? e.col : null, fileLine: e.line || null };
        });
        this.gameCheck = { label: res.label, interp, errors: errs.length, errorList };
        if (errs.length) { const e = errorList[0]; this.app.toast(`quark_tests checkmode: ${errs.length} error(s): ${e.line ? e.line + ":" + e.col : "file line " + e.fileLine} ${e.msg} (in Checks)`, 9000); }
        else if (res.ok) this.app.toast(`The game compiles it: ${res.label || "ok"} · ${interp.length ? interp.length + " interpreted (" + interp.join(", ") + ")" : "every slot native"}`, 6000);
        this._problems();
    }

    _wire() {
        document.querySelectorAll("#viewTabs [data-view]").forEach((b) => (b.onclick = () => this._showView(b.dataset.view)));
        $("modeSel").onchange = async () => {
            const v = $("modeSel").value;
            if (this.savedSig !== this._sig() && !confirm("Leave this mode? Its unsaved changes are lost (undo can't bring them back).")) { this._fillSel(); return; }
            try { await this.open(v.slice(2), v.startsWith("b:")); } catch (e) { this.app.toast("Can't open it: " + e.message, 6000); this._fillSel(); }
        };
        $("modeNew").onclick = async () => {
            const r = await this.app.promptNew(this.base);
            if (!r) return;
            this.newFrom(r.name, r.base);
        };
        $("modeSave").onclick = () => this.save();
        $("modeSaveAs").onclick = async () => { const n = await this.app.prompt("Save as", this.builtin ? "my" + this.base : this.name + "2", "Letters, digits, _ . - · saved to " + this.app.modesDir, (x) => this.app.modeNameProblem(x)); if (n) this.save(n); };
        $("modeRename").onclick = () => this.fileOp("rename");
        $("modeDuplicate").onclick = () => this.fileOp("duplicate");
        $("modeDelete").onclick = () => this.fileOp("delete");
        $("modeBackups").onclick = () => this.backups();
        $("modeDownload").onclick = () => {
            const text = this.saveText();
            if (text == null) { this.app.toast("The code has an error"); return; }
            const n = this.builtin ? "my" + this.base : this.name;
            this.app.download(n + ".qmode", this.builtin ? patchQmode(text, { custommode: n }, splitQmode(text).logicText) : text);
            api.provenance(n, this._provenance(n)).catch(() => {});
        };
        $("modeUpload").onclick = async () => {
            const f = await this.app.pickFile(".qmode,.txt");
            if (!f) return;
            // its name: the file's, unless that's a built-in mode's or one of yours (then asked for, as Save as would)
            let name = f.name.replace(/\.qmode$|\.txt$/i, "").replace(/[^A-Za-z0-9_.-]/g, "_").replace(/^[_.-]+/, "").slice(0, 64) || "uploaded";
            const problem = (n) => this.app.modeNameProblem(n) || (this.app.userModes.some((u) => u.name.toLowerCase() === n.toLowerCase()) ? `${n}.qmode is one of your modes already: open it from the list, or pick another name.` : null);
            if (problem(name)) {
                let alt = "my" + name;
                for (let k = 2; problem(alt) && k < 100; k++) alt = `my${name}${k}`;
                name = await this.app.prompt(`Open ${f.name}`, alt, `${problem(name)} The file opens under this name (nothing is written until you save).`, problem);
                if (!name) return;
            }
            try {
                this._adoptText(name, false, f.text, null, false);   // (logic that doesn't parse: a Code view draft, as open() keeps it)
                if (this.currentKeys().custommode !== name) this.keySet = { ...this.keySet, custommode: name };   // (the in-game name follows the file's)
                this.fileText = null; this.savedSig = null; this._fillSel(); this._modeInfo(); this._persist();
            } catch (e) { this.app.toast("Can't read it: " + e.message, 6000); }
        };
        $("modeResetAll").onclick = () => this.resetAll();
        $("itemFilter").oninput = () => this._items();
        $("undo").onclick = () => this.undo(-1);
        $("redo").onclick = () => this.undo(1);
        $("modeWhole").onchange = () => this._diff();
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
            const inCode = document.activeElement === ta;
            if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") { e.preventDefault(); if (inCode) this._codeEdited(true); this.save(); }
            else if (!inCode && (e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z" && !/INPUT|SELECT/.test(document.activeElement.tagName)) { e.preventDefault(); this.undo(e.shiftKey ? 1 : -1); }
            else if (!inCode && (e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "y" && !/INPUT|SELECT/.test(document.activeElement.tagName)) { e.preventDefault(); this.undo(1); }
        });
        window.addEventListener("beforeunload", () => this._persist());
    }
}
export { PHYSICS_EDITOR_URL, MODE_KEYS };
