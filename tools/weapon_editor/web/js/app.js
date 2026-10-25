// The weapons editor page: one weapon set (a .qweapons text, the source of truth) shown and edited by every tab.
// Weapon (fields with sliders), Values (the comparison table and charts), Other games, Code / Blocks / Graph (the
// logic: three views of one AST), Range (test shots) and Fights (bot vs bot). The side panel opens / saves the set,
// lists the 11 weapons and writes the "play in game" custom mode.

import { api } from "./api.js";
import { createHelper } from "./helper.js";
import { WEAPONS } from "./catalog.js";
import { parse, print } from "./dsl.js";
import { setCatalog, catalog, fieldValues, labelsOf, changedFields, withField, withLabel, carryIds, numberIds, symbols, slotKey, sameVal } from "./ast.js";
import { computeStats, fmtNum } from "./stats.js";
import { $, fin, fmtT } from "./util.js";
import { WeaponView } from "./weapon.js";
import { ValuesView } from "./values.js";
import { GamesView } from "./games.js";
import { CodeView } from "./code.js";
import { RangeView } from "./range.js";
import { FightsView } from "./fights.js";
import { BlocksView } from "./blocks.js";
import { GraphView } from "./graph.js";

export const BASE_MODES = ["ffa", "vs", "duel", "1v1", "tourney", "bomb", "koth", "payload", "npayload", "points", "tag", "hide", "race"];
const SET_RE = /^[A-Za-z0-9][A-Za-z0-9 _-]{0,35}$/;
export const KIND_TITLE = { hitscan: "hitscan", projectile: "projectile", beam: "beam", melee: "melee", support: "support" };

class App {
    constructor() {
        this.handlers = {}; this.page = "weapon"; this.stale = new Set();
        this.set = { name: "my weapons", saved: null, baseText: null };
        this.text = ""; this.ast = null; this.parseOk = false; this.problems = []; this.gameProblems = null;
        this.sel = "rifle"; this.health = 100; this.dist = 0;
        this.cmp = { name: "", text: "", ast: null, values: null };
        this.hist = { undo: [], redo: [], key: null, at: 0, since: 0 };
    }
    on(ev, f) { (this.handlers[ev] || (this.handlers[ev] = [])).push(f); }
    emit(ev, ...a) { for (const f of this.handlers[ev] || []) f(...a); }
    toast(msg, ms = 2500) { const t = $("toast"); t.textContent = msg; t.style.display = "block"; clearTimeout(this._tt); this._tt = setTimeout(() => (t.style.display = "none"), ms); }

    async init() {
        if (new URLSearchParams(location.search).get("embed") === "1") document.documentElement.classList.add("embed");
        this.config = await api.config().catch(() => ({}));
        $("dataDir").textContent = this.config.dataDir || "";
        $("dataDir").title = "the game's data folder: " + (this.config.dataDir || "?");
        this.helper = await createHelper(this.config);
        setCatalog(this.helper.catalog);
        this.cat = catalog();
        this.stdAst = parse(this.cat.defaultText);
        this.stdValues = new Map(this.cat.defaults);
        const b = $("helperBadge");
        b.textContent = this.helper.label; b.className = "badge " + (this.helper.kind === "real" ? "real" : "mock"); b.title = this.helper.title;
        this.ui = (await api.state("ui").catch(() => null)) || {};
        this.viewState = this.ui.views || {};
        if (WEAPONS.includes(this.ui.sel)) this.sel = this.ui.sel;
        if (this.ui.health) this.health = this.ui.health;
        $("healthIn").value = this.health;
        // views
        this.weaponView = new WeaponView(this, $("weaponWrap"));
        this.valuesView = new ValuesView(this, $("valuesWrap"));
        this.gamesView = new GamesView(this, $("gamesWrap"));
        this.codeView = new CodeView(this);
        this.blocks = new BlocksView($("vBlocks"), this);
        this.graph = new GraphView($("vGraph"), this);
        this.blocks.restoreState(this.viewState.blocks);
        this.graph.restoreState(this.viewState.graph);
        this.rangeView = new RangeView(this);
        this.fightsView = new FightsView(this);
        this.views = { weapon: this.weaponView, values: this.valuesView, games: this.gamesView, code: this.codeView, range: this.rangeView, fights: this.fightsView };
        this._side();
        this._tabs();
        // what to open: the unsaved draft, the last set, or a new one
        const draft = await api.state("draft").catch(() => null);
        await this.refreshSets();
        if (draft && typeof draft.text === "string" && draft.text.trim()) {
            this.set = { name: draft.name || "my weapons", saved: draft.saved || null, baseText: draft.baseText ?? null };
            this.setText(draft.text, null, true);
        } else if (this.ui.last && this.sets.some((s) => s.name === this.ui.last)) await this.openSet(this.ui.last);
        else this.newSet("my weapons", true);
        await this.setCompare(this.ui.cmp || "");
        this.showPage(this.ui.page && $("pg-" + this.ui.page) ? this.ui.page : "weapon");
        document.addEventListener("keydown", (e) => {
            if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") { e.preventDefault(); this.save(); }
            if ((e.ctrlKey || e.metaKey) && e.key === "Enter" && this.page === "range") { e.preventDefault(); this.rangeView.run(); }
            // undo / redo: the set's own history everywhere, the Code text included; other boxes keep the browser's
            const k = e.key.toLowerCase(), t = e.target;
            if ((e.ctrlKey || e.metaKey) && !e.altKey && (k === "z" || k === "y") && !(t && /^(INPUT|SELECT)$/.test(t.tagName)) && !(t && t.tagName === "TEXTAREA" && t.id !== "codeTa")) {
                e.preventDefault();
                if (k === "y" || e.shiftKey) this.redo(); else this.undo();
            }
        });
        window.addEventListener("beforeunload", () => this._saveDraftNow());
        this.ready = true;
        this._selfCheck();
    }
    // the page's numbers against the game's wpnstats, once per load (helper.js selfCheck): a mismatch is a warning on
    // the badge, never a block
    async _selfCheck() {
        const r = await this.helper.selfCheck(this.cat.defaultText, this.stdValues, this.stdAst).catch(() => null);
        const b = $("helperBadge");
        if (!r) return;
        if (!r.diffs.length) { b.title += "\nThe page's numbers (stats.js) match the game's wpnstats on the standard set."; return; }
        b.textContent += " ≠"; b.classList.add("drift");
        b.title += `\nThe page's numbers differ from the game's wpnstats in ${r.diffs.length} place${r.diffs.length > 1 ? "s" : ""} (stats.js needs updating to the game):\n` + r.diffs.slice(0, 12).join("\n");
        console.warn("weapons editor: stats.js differs from wpnstats:", r.diffs);
        this.toast(`the page's numbers differ from the game's in ${r.diffs.length} place${r.diffs.length > 1 ? "s" : ""}: see the header badge`, 6000);
    }

    // ---------------------------------------------------------------- the set
    get dirty() { if (!this.ready && !this.text) return false; return this.set.baseText === null ? this.text !== this._newText(this.set.name) : this.text !== this.set.baseText; }
    _newText(name) { return `version 1\nweapons "${name}"\n`; }
    // ---- undo / redo: one history of the set's text, whatever changed it (Weapon fields, Copy values, Reset, Blocks,
    // Graph, Code). Edits of one kind in a row (`key`: a slider's drag, a burst of typing) are one step while they come
    // less than a second apart (and for 4 s at most); opening or starting a set starts a new history.
    _record(prev, next, key) {
        const H = this.hist, now = performance.now();
        if (prev === next) return;
        if (!(key && key === H.key && now - H.at < 1000 && now - H.since < 4000)) {
            H.undo.push(prev); if (H.undo.length > 300) H.undo.shift();
            H.since = now;
        }
        H.key = key; H.at = now; H.redo.length = 0;
        this._histButtons();
    }
    _resetHistory() { this.hist = { undo: [], redo: [], key: null, at: 0, since: 0 }; this._histButtons(); }
    _histButtons() { const u = $("undoBtn"), r = $("redoBtn"); if (u) u.disabled = !this.hist.undo.length; if (r) r.disabled = !this.hist.redo.length; }
    undo() { this._step(this.hist.undo, this.hist.redo, "undo"); }
    redo() { this._step(this.hist.redo, this.hist.undo, "redo"); }
    _step(from, to, what) {
        if (!from.length) { this.toast(`nothing to ${what}`, 1500); return; }
        const prev = this.text, text = from.pop();
        to.push(prev);
        this.hist.key = null;
        this.setText(text, "undo");
        this._histButtons();
        if (this.page === "code") this.codeView.caretAtChange(prev, text);
    }

    // a new text (from the code view, a file, a load): parse, keep ids where the tree didn't change, derive, tell
    setText(text, from = null, quiet = false) {
        if (from === null) this._resetHistory();
        else if (from !== "undo") this._record(this.text, text, from);
        this.text = text;
        const c = this.helper.checkLocal(text);
        if (c.set) { this.ast = carryIds(this.ast, c.set); this.parseOk = true; }
        else this.parseOk = false;
        if (!this.ast) this.ast = parse(this._newText(this.set.name));
        this.problems = c.problems;
        this.gameProblems = null;
        this._derive();
        this._changed(from);
        this._scheduleGameCheck();
        if (!quiet) this._saveDraft();
    }
    // a new tree (blocks / graph / a field): its text, re-read so problems have their lines
    brainEdited(ast, from, key = from) {
        numberIds(ast);
        const text = print(ast);
        this._record(this.text, text, key);
        this.text = text;
        let parsed = null;
        try { parsed = parse(text); } catch { parsed = null; }
        this.ast = parsed ? carryIds(ast, parsed) : ast;
        this.parseOk = !!parsed;
        const c = this.helper.checkLocal(text);
        this.problems = c.problems;
        this.gameProblems = null;
        this._derive();
        this._changed(from);
        this._scheduleGameCheck();
        this._saveDraft();
    }
    setField(key, v, from = "weapon") {
        if (!this.parseOk) { this.toast("the code has an error: fix it on the Code tab first (a field edit rewrites the text)", 4000); return false; }
        this.brainEdited(withField(this.ast, key, v), from, from + ":" + key);
        return true;
    }
    setLabel(weapon, text) {
        if (!this.parseOk) { this.toast("fix the code's error first", 3000); return; }
        this.brainEdited(withLabel(this.ast, weapon, text), "weapon", "label:" + weapon);
    }
    resetWeapon(w) {
        if (!this.parseOk) return;
        const keys = new Set(this.cat.fieldsOf(w).map((f) => f.key));
        this.brainEdited({ ...this.ast, decls: this.ast.decls.filter((d) => !(d.kind === "param" && keys.has(d.name)) && !(d.kind === "label" && d.weapon === w)) }, "reset", null);
    }
    // a variant: weapon `from`'s values (this set's, the comparison set's or the standard ones) onto weapon `to`, for
    // every field both have that a set can write (the alt fire's own fields and the kind stay `to`'s). The game has 11
    // weapon slots and no way to add a 12th without changing it, so a variant is one of them retuned.
    copyWeapon(from, to, src = "set") {
        if (!this.parseOk) { this.toast("the code has an error: fix it on the Code tab first", 3500); return; }
        const vals = src === "cmp" ? this.cmp.values : src === "std" ? this.stdValues : this.values;
        const mine = new Map(this.cat.fieldsOf(to).filter((f) => !f.readOnly && !f.alt).map((f) => [f.field, f]));
        let ast = this.ast, n = 0;
        for (const f of this.cat.fieldsOf(from)) {
            const g = mine.get(f.field);
            if (!g || f.readOnly || f.alt || !vals.has(f.key)) continue;
            const v = vals.get(f.key);
            if (sameVal(v, this.values.get(g.key), g.type)) continue;
            ast = withField(ast, g.key, v); n++;
        }
        if (!n) { this.toast("nothing to copy: the values are the same", 2500); return; }
        this.brainEdited(ast, "copy", null);
        const fromT = (src === "set" && this.titles.get(from)) || this.cat.byId.get(from).title;
        this.toast(`${n} value${n > 1 ? "s" : ""} of ${fromT}${src === "cmp" ? ` ("${this.cmp.name}")` : src === "std" ? " (standard)" : ""} copied to ${this.cat.byId.get(to).title}; it stays a ${this.cat.byId.get(to).kind} weapon with its own alt fire`, 5000);
    }
    _derive() {
        this.values = fieldValues(this.ast);
        this.labels = labelsOf(this.ast);
        this.changed = new Set(changedFields(this.values));
        this.titles = new Map(this.cat.weapons.map((w) => [w.id, this.labels.get(w.id) ? `${w.title} (${this.labels.get(w.id)})` : w.title]));
        this.stats = computeStats(this.values, { health: this.health, dist: this.dist, set: this.ast, titles: this.titles });
        if (this.cmp.values) this.cmpStats = computeStats(this.cmp.values, { health: this.health, dist: this.dist, set: this.cmp.ast, titles: this.titles });
    }
    _changed(from) {
        this._sideRefresh();
        for (const [k, v] of Object.entries(this.views)) { if (k === from && v.liveUpdate) { v.liveUpdate(); continue; } if (this.page === k) v.refresh(from); else this.stale.add(k); }
        if (from !== "blocks") { if (this.page === "blocks") this.blocks.setBrain(this.ast); else this.stale.add("blocks"); }
        if (from !== "graph") { if (this.page === "graph") this.graph.setBrain(this.ast); else this.stale.add("graph"); }
        this._checkInfo();
        this.emit("set", from);
    }
    // the game's problems (authoritative), then the page's that the game didn't report: one at the same line and
    // column (or line, when either has no column) with the same severity is the same mistake in other words
    allProblems() {
        const g = this.gameProblems ? this.gameProblems.problems : [];
        const same = (p, q) => p.severity === q.severity && p.line === q.line && (p.col === q.col || !p.col || !q.col);
        return [...g, ...this.problems.filter((p) => !g.some((q) => q.msg === p.msg && q.line === p.line || same(p, q)))];
    }
    _checkInfo() {
        const p = this.allProblems(), e = p.filter((x) => x.severity === "error").length, w = p.length - e;
        const el = $("checkInfo");
        el.innerHTML = e ? `<a href="#" class="err">${e} error${e > 1 ? "s" : ""}</a>` : w ? `<a href="#" class="warn">${w} warning${w > 1 ? "s" : ""}</a>` : `<span class="ok">✓ checked</span>`;
        el.title = (this.gameProblems ? "checked by " + this.gameProblems.via : "checked by the page") + (p.length ? "\n" + p.slice(0, 8).map((x) => `${x.line}: ${x.msg}`).join("\n") : "");
        const a = el.querySelector("a"); if (a) a.onclick = (ev) => { ev.preventDefault(); this.showPage("code"); };
    }
    _scheduleGameCheck() {
        clearTimeout(this._gc);
        const text = this.text;
        this._gc = setTimeout(async () => {
            const r = await this.helper.checkGame(text);
            if (r && text === this.text) { this.gameProblems = r; this._checkInfo(); if (this.page === "code") this.codeView.refresh("check"); }
        }, 700);
    }
    _saveDraft() { clearTimeout(this._ds); this._ds = setTimeout(() => this._saveDraftNow(), 800); }
    _saveDraftNow() {
        if (!this.ready) return;
        const d = this.dirty ? { name: this.set.name, saved: this.set.saved, baseText: this.set.baseText, text: this.text } : null;
        api.saveState("draft", d).catch(() => {});
    }
    saveViewState(view, obj) { this.viewState[view] = obj; this.saveUi(); }
    saveUi() {
        clearTimeout(this._us);
        this._us = setTimeout(() => api.saveState("ui", { ...this.ui, views: this.viewState, sel: this.sel, page: this.page, last: this.set.saved, cmp: this.cmp.name, health: this.health }).catch(() => {}), 500);
    }
    // the standard falloff of a weapon (for a <weapon>.falloff curve's dashed line)
    falloffRef(curveName) {
        const m = /^(\w+)\.falloff$/.exec(curveName || "");
        if (!m || !WEAPONS.includes(m[1])) return null;
        const v = (k) => +this.stdValues.get(m[1] + "." + k);
        const s = v("falloffStart"), e = v("falloffEnd"), mn = v("falloffMin");
        return (d) => (mn >= 1 || e <= s ? 1 : d <= s ? 1 : d >= e ? mn : 1 + (mn - 1) * (d - s) / (e - s));
    }
    selectWeapon(w) {
        if (!WEAPONS.includes(w) || w === this.sel) return;
        this.sel = w;
        this._sideRefresh();
        this.weaponView.refresh("select");
        this.emit("select", w);
        this.saveUi();
    }

    async refreshSets() {
        const r = await api.sets().catch(() => ({ sets: [] }));
        this.sets = r.sets || [];
        this.setsDir = r.dir;
        const sel = $("setSel");
        sel.innerHTML = "";
        const o0 = document.createElement("option"); o0.value = ""; o0.textContent = this.set.saved ? "(open a set…)" : "(not saved yet)"; sel.append(o0);
        for (const s of this.sets) { const o = document.createElement("option"); o.value = s.name; o.textContent = `${s.name}  (${s.params || 0} values${s.slots ? ", " + s.slots + " slots" : ""})`; sel.append(o); }
        sel.value = this.set.saved && this.sets.some((s) => s.name === this.set.saved) ? this.set.saved : "";
        const cs = $("cmpSel"), cur = cs.value || this.cmp.name;
        cs.innerHTML = `<option value="">the standard weapons</option>` + this.sets.map((s) => `<option value="${s.name.replace(/"/g, "&quot;")}">${s.name}</option>`).join("");
        cs.value = this.sets.some((s) => s.name === cur) ? cur : "";
    }
    async openSet(name) {
        if (this.dirty && !confirm(`"${this.set.name}" has unsaved changes. Open "${name}" anyway (the changes are lost)?`)) { $("setSel").value = this.set.saved || ""; return; }
        try {
            const text = await api.set(name);
            this.set = { name, saved: name, baseText: text };
            this.ast = null;
            this.setText(text, null, true);
            this._saveDraftNow();
            $("setSel").value = name;
            this.saveUi();
        } catch (e) { this.toast("can't open " + name + ": " + e.message, 4000); }
    }
    newSet(name = "my weapons", quiet = false) {
        if (!quiet && this.dirty && !confirm(`"${this.set.name}" has unsaved changes. Start a new set anyway?`)) return;
        let n = name, k = 2;
        while ((this.sets || []).some((s) => s.name === n)) n = `${name} ${k++}`;
        this.set = { name: n, saved: null, baseText: null };
        this.ast = null;
        this.setText(this._newText(n), null, true);
        $("setSel").value = "";
    }
    // one save at a time: a double click (or Ctrl+S held) waits for the save in flight instead of asking twice
    // (overwrite: the caller already asked about replacing another set of that name)
    save(asName, overwrite = false) {
        if (this._saving) return this._saving;
        this._saving = this._save(asName, overwrite).finally(() => { this._saving = null; });
        return this._saving;
    }
    async _save(asName, overwrite) {
        let name = asName || $("setName").value.trim() || this.set.name;
        if (!SET_RE.test(name)) { this.toast("a set's name: letters, digits, space, _ and -, 36 at most, starting with a letter or digit", 4500); return false; }
        // another name (Save as, a renamed set) must not replace a set that's already there without asking; the server
        // checks it too (expect null: "must not exist"), for a file made since the list was read
        const mine = this.set.saved === name;
        const askOverwrite = () => overwrite || confirm(`custom_weapons/${name}.qweapons already exists (another set).\n\nOK: overwrite it with this one (the old file is kept as .bak).\nCancel: don't save.`);
        if (!mine && (this.sets || []).some((s) => s.name === name)) { if (!askOverwrite()) return false; overwrite = true; }
        // the header carries the set's name
        if (this.parseOk && this.ast.name !== name) { this.brainEdited({ ...this.ast, name }, "save"); }
        const expect = mine ? this.set.baseText : overwrite ? undefined : null;
        try {
            await api.saveSet(name, this.text, expect);
        } catch (e) {
            if (e.status === 409 && e.data && e.data.exists) {
                if (!askOverwrite()) return false;
                await api.saveSet(name, this.text, undefined, true);
            } else if (e.status === 409) {
                if (!confirm(`custom_weapons/${name}.qweapons changed on disk since it was opened (another editor, or by hand).\n\nOK: overwrite it with this set (the disk version is kept as .bak).\nCancel: don't save (then choose whether to open the disk version).`)) {
                    const disk = e.data && typeof e.data.text === "string" ? e.data.text : null;
                    if (disk !== null && confirm(`Open the version on disk instead? Your unsaved changes here are lost (Download keeps a copy).`)) {
                        this.set = { name, saved: name, baseText: disk };
                        this.ast = null;
                        this.setText(disk, null, true);
                        this._saveDraftNow();
                        await this.refreshSets();
                    }
                    return false;
                }
                await api.saveSet(name, this.text, undefined, true);
            } else { this.toast("save failed: " + e.message, 4000); return false; }
        }
        this.set = { name, saved: name, baseText: this.text };
        await this.refreshSets();
        this._sideRefresh();
        this._saveDraftNow();
        this.saveUi();
        this.toast(`saved custom_weapons/${name}.qweapons`);
        if (this.page === "code") this.codeView.refresh("save");
        return true;
    }
    async setCompare(name) {
        this.cmp = { name: "", text: this.cat.defaultText, ast: this.stdAst, values: this.stdValues };
        if (name) {
            try { const t = await api.set(name); const a = parse(t); this.cmp = { name, text: t, ast: a, values: fieldValues(a) }; }
            catch (e) { this.toast(`can't compare with ${name}: ${e.message}`, 4000); }
        }
        $("cmpSel").value = this.cmp.name;
        this._derive();
        this._changed("compare");
        this.saveUi();
    }
    get cmpLabel() { return this.cmp.name ? `"${this.cmp.name}"` : "standard"; }

    // ---------------------------------------------------------------- side panel
    _side() {
        $("setSel").onchange = () => { const v = $("setSel").value; if (v) this.openSet(v); };
        $("setName").onchange = () => { const v = $("setName").value.trim(); if (SET_RE.test(v)) { this.set.name = v; this._sideRefresh(); } else this.toast("letters, digits, space, _ -; 36 at most", 3000); };
        $("setNew").onclick = () => this.newSet();
        $("undoBtn").onclick = () => this.undo();
        $("redoBtn").onclick = () => this.redo();
        $("setSave").onclick = () => this.save();
        $("setSaveAs").onclick = () => { const n = prompt("Save the set as (custom_weapons/<name>.qweapons):", this.set.name + " copy"); if (n) { $("setName").value = n.trim(); this.save(n.trim()); } };
        $("setOpenFile").onclick = () => $("fileIn").click();
        $("fileIn").onchange = async () => {
            const f = $("fileIn").files[0]; if (!f) return;
            $("fileIn").value = "";
            if (this.dirty && !confirm(`"${this.set.name}" has unsaved changes. Open ${f.name} anyway (the changes are lost)?`)) return;
            const text = await f.text();
            const name = f.name.replace(/\.(qweapons|txt)$/i, "").replace(/[^A-Za-z0-9 _-]/g, "").slice(0, 36) || "imported";
            this.set = { name, saved: null, baseText: null };
            this.ast = null;
            this.setText(text);
        };
        $("setDownload").onclick = () => {
            const a = document.createElement("a");
            a.href = URL.createObjectURL(new Blob([this.text], { type: "text/plain" }));
            a.download = this.set.name + ".qweapons"; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 2000);
        };
        $("setDelete").onclick = async () => {
            if (!this.set.saved) { this.toast("not saved: nothing to delete"); return; }
            if (!confirm(`Delete custom_weapons/${this.set.saved}.qweapons? (kept as .qweapons.bak)`)) return;
            await api.deleteSet(this.set.saved).catch((e) => this.toast(e.message));
            this.set.saved = null; this.set.baseText = null;
            await this.refreshSets(); this._sideRefresh();
        };
        $("setResetAll").onclick = () => {
            if (!this.parseOk) return;
            if (!confirm("Every weapon back to the standard values and names? (params of the set's own, curves and slots stay)")) return;
            const fields = this.cat.byKey;
            this.brainEdited({ ...this.ast, decls: this.ast.decls.filter((d) => !(d.kind === "param" && fields.has(d.name)) && d.kind !== "label") }, "reset", null);
        };
        const bm = $("baseMode");
        bm.innerHTML = BASE_MODES.map((m) => `<option>${m}</option>`).join("");
        bm.value = this.ui.base || "ffa";
        bm.onchange = () => { this.ui.base = bm.value; this.saveUi(); };
        $("playBtn").onclick = () => this.playInGame();
        $("cmpSel").onchange = () => this.setCompare($("cmpSel").value);
        $("healthIn").onchange = () => { const h = Math.max(1, Math.min(1000, +$("healthIn").value || 100)); this.health = h; $("healthIn").value = h; this._derive(); this._changed("health"); this.saveUi(); };
        $("distSel").onchange = () => { this.dist = +$("distSel").value || 0; this._derive(); this._changed("health"); };
    }
    async playInGame() {
        if (this.allProblems().some((p) => p.severity === "error")) { this.toast("the set has errors: the game would play the standard weapons. Fix them first (Code tab).", 5000); return; }
        if (!this.set.saved || this.dirty || this.set.saved !== this.set.name) {
            // the game reads the set from its file: say so before writing it (an existing one is kept as .bak)
            const exists = (this.sets || []).some((s) => s.name === this.set.name);
            if (!confirm(`Play in game saves the set first: ${exists ? "overwrite" : "write"} custom_weapons/${this.set.name}.qweapons${exists ? " (the old file is kept as .bak)" : ""}, then write the custom mode. Go on?`)) return;
            if (!(await this.save(this.set.name, exists))) return;
        }
        try {
            const r = await api.playMode(this.set.saved, $("baseMode").value);
            $("playInfo").innerHTML = `${r.created ? "wrote" : "updated"} <code>custom_modes/${r.name}.qmode</code> (weaponset=${this.set.saved}). In the game: Host &gt; Custom modes &gt; <b>${r.name}</b>.`;
            this.toast(`${r.name}.qmode ${r.created ? "written" : "updated"}`);
        } catch (e) { this.toast("couldn't write the mode: " + e.message, 4000); }
    }
    _sideRefresh() {
        if (document.activeElement !== $("setName")) $("setName").value = this.set.name;
        $("playName").textContent = this.set.name;
        const s = this.set;
        $("setInfo").textContent = (s.saved ? `custom_weapons/${s.saved}.qweapons` : "not saved yet") + (this.dirty ? " · unsaved changes" : s.saved ? " · saved" : "");
        $("setInfo").className = "small " + (this.dirty ? "warn" : "muted");
        $("setDelete").disabled = !s.saved;
        // the weapon list
        const S = symbols(this.ast);
        const list = $("wList");
        list.innerHTML = "";
        let nChanged = 0;
        for (const w of this.cat.weapons) {
            const n = [...this.changed].filter((k) => k.startsWith(w.id + ".")).length;
            if (n || this.labels.get(w.id)) nChanged++;
            const logic = S.slots.has("hit " + w.id) || S.slots.has("kill " + w.id) || S.curves.has(w.id + ".falloff");
            const row = document.createElement("div");
            row.className = "wrow" + (w.id === this.sel ? " sel" : "");
            row.innerHTML = `<span>${w.title}${this.labels.get(w.id) ? ` <span class="lab">"${this.labels.get(w.id)}"</span>` : ""}</span><span class="k">${KIND_TITLE[w.kind] || w.kind || ""}${logic ? ' <span class="logic" title="this weapon has its own logic (slot or falloff curve)">ƒ</span>' : ""}</span><span class="mod" title="${n} value${n === 1 ? "" : "s"} changed">${n || ""}</span>`;
            row.onclick = () => this.selectWeapon(w.id);
            list.append(row);
        }
        const gen = S.slots.has("hit") || S.slots.has("kill");
        $("wCount").textContent = `${nChanged} changed${gen ? " · logic for all" : ""}`;
        this._quick();
    }
    _quick() {
        const rows = this.stats.rows.filter((r) => r.weapon === this.sel && !r.support);
        const cmp = new Map((this.cmpStats ? this.cmpStats.rows : []).map((r) => [r.id, r]));
        $("qnName").textContent = this.titles.get(this.sel) || this.sel;
        const t = $("quick");
        const d = (a, b, lower) => { if (!Number.isFinite(a) || !Number.isFinite(b) || Math.abs(a - b) < 1e-9) return ""; const up = a > b; const good = lower ? !up : up; return ` <span class="${good ? "up" : "down"}">${up ? "+" : ""}${fmtNum(a - b)}</span>`; };
        let h = `<tr><th></th><th>STK b/h</th><th>TTK b/h</th><th>DPS b</th></tr>`;
        for (const r of rows) {
            const c = cmp.get(r.id) || {};
            h += `<tr><td title="${r.label}${r.logic ? "\nthe set's slot " + r.logic + " is counted" : ""}">${r.label.split(" · ").slice(1).join(" · ") || "primary"}${r.logic ? ' <span class="logic">ƒ</span>' : ""}</td><td class="n">${fin(r.stkBody)}/${fin(r.stkHead)}${d(r.stkBody, c.stkBody, true)}</td><td class="n">${fmtT(r.ttkBody)}/${fmtT(r.ttkHead)}${d(r.ttkBody, c.ttkBody, true)}</td><td class="n">${fmtNum(r.dpsBody)}${d(r.dpsBody, c.dpsBody)}</td></tr>`;
        }
        if (!rows.length) h += `<tr><td colspan="4" class="muted">a support item: no damage</td></tr>`;
        t.innerHTML = h;
    }

    // ---------------------------------------------------------------- tabs
    _tabs() { for (const b of document.querySelectorAll("#tabs button")) b.onclick = () => this.showPage(b.dataset.page); }
    showPage(p) {
        this.page = p;
        for (const b of document.querySelectorAll("#tabs button")) b.classList.toggle("on", b.dataset.page === p);
        for (const s of document.querySelectorAll(".page")) s.classList.toggle("on", s.id === "pg-" + p);
        if (p === "blocks") { this.blocks.setBrain(this.ast); this.stale.delete("blocks"); }
        else if (p === "graph") { this.graph.setBrain(this.ast); this.stale.delete("graph"); requestAnimationFrame(() => this.graph.shown()); }
        else if (this.views[p]) { this.views[p].refresh(this.stale.has(p) ? "stale" : "show"); this.stale.delete(p); if (this.views[p].shown) this.views[p].shown(); }
        this.saveUi();
    }
    // open a declaration in the code view (from a weapon card, a problem...)
    openDecl(key) {
        this.showPage("code");
        const d = this.ast.decls.find((x) => (x.kind === "slot" ? "slot:" + slotKey(x) : x.kind + ":" + (x.kind === "label" ? x.weapon : x.name)) === key);
        if (d && d._loc) this.codeView.goto(d._loc.line);
    }
}

export const app = new App();
window.weaponsApp = app;   // (for the page test)
app.init().catch((e) => { console.error(e); document.body.insertAdjacentHTML("afterbegin", `<pre style="color:#f87171;padding:10px">${String(e.stack || e)}</pre>`); });
