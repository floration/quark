// Boot and shared state: the physics set being edited, the scenario, the runs. The tabs (values.js, code.js,
// blocks.js, graph.js, pathview.js, drive.js, export.js) wire themselves to this and to each other through events:
//   physics (from)  the edited set changed (from: "values" | "code" | "load" | ...): views refresh, runs re-run
//   scenario        the scenario changed           results   new runs to show            map   the test map changed
//   games           the picked reference games (Values › Compare with games) or their data changed
//   exprcheck       the helper's own evaluator (physexpr) answered for the edited set's equations
//   theme           light / dark changed (canvases redraw with the new colours)
import { api, isReservedName, sameName } from "./api.js";
import { createHelper } from "./helper.js";
import { parseQmap, openRun, nearBoxes } from "./qmap.js";
import { standardPhysics, clonePhysics, changedKeys } from "./params.js";
import { parseQmode, mergeQmode } from "./qmode.js";
import { computeMetrics, mergeMetrics, metricRows, missingMetric } from "./sim.js";
import { ValuesView } from "./values.js";
import { CodeView } from "./code.js";
import { BlocksView } from "./blocks.js";
import { GraphView, fmtMetric } from "./graph.js";
import { PathView } from "./pathview.js";
import { DriveView } from "./drive.js";
import { ExportView } from "./export.js";
import { MoveCodeView } from "./movecode.js";
import { CodeGraphView } from "./codegraph.js";
import { check as exprCheck, resolve } from "./expr.js";
import { loadPicks, savePicks, loadGame, gamePhysics } from "./refs.js";

const $ = (id) => document.getElementById(id);
export const RUN_COLORS = ["--run0", "--run1", "--run2", "--run3", "--run4", "--run5"];
export const cssVar = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim() || "#888";
const NAME_RE = /^[A-Za-z0-9][A-Za-z0-9 _-]{0,39}$/;
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

// Light / dark: the system's by default; ?theme=light|dark or the header button forces one (<html data-theme>, the
// Quark tools' convention, quark-tools.css). Applied before anything draws.
const THEMES = ["auto", "light", "dark"];
function themeChoice() {
    const q = new URLSearchParams(location.search).get("theme");
    if (THEMES.includes(q)) return q;
    try { const t = localStorage.getItem("qpe.theme"); if (THEMES.includes(t)) return t; } catch {}
    return "auto";
}
function applyTheme(t) { if (t === "auto") delete document.documentElement.dataset.theme; else document.documentElement.dataset.theme = t; }
applyTheme(themeChoice());

class App {
    constructor() { this.handlers = {}; this.compareSel = new Set(); this.extraRuns = []; this.runSeq = 0; this.undoStack = []; this.redoStack = []; }
    on(e, f) { (this.handlers[e] || (this.handlers[e] = [])).push(f); }
    emit(e, ...a) { for (const f of this.handlers[e] || []) f(...a); }
    toast(msg, ms = 2600) {
        const t = $("toast"); t.textContent = msg; t.style.display = "block";
        clearTimeout(this._tt); this._tt = setTimeout(() => (t.style.display = "none"), ms);
    }
    download(name, text, type = "text/plain") {
        const a = document.createElement("a");
        a.href = URL.createObjectURL(new Blob([text], { type })); a.download = name; a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 2000);
    }
    pickFile(accept) {
        return new Promise((res) => {
            const f = $("fileIn"); f.accept = accept; f.value = "";
            f.onchange = () => { const file = f.files[0]; if (!file) return res(null); file.text().then((text) => res({ name: file.name, text })); };
            f.click();
        });
    }
    // an in-page yes / no (never window.confirm: it blocks the page, and the Quarkology app's shell may not show it)
    confirm(title, msg, okLabel = "OK", danger = false) {
        return new Promise((res) => {
            const m = document.createElement("div");
            m.className = "modal";
            m.innerHTML = `<div role="alertdialog" aria-modal="true"><h3></h3><p class="small"></p><div class="row" style="justify-content:flex-end;margin-top:10px"><button data-x="0">Cancel</button><button data-x="1" class="${danger ? "danger" : "primary"}"></button></div></div>`;
            m.querySelector("h3").textContent = title; m.querySelector("p").textContent = msg;
            const ok = m.querySelector('[data-x="1"]'); ok.textContent = okLabel;
            const done = (yes) => { m.remove(); document.removeEventListener("keydown", key, true); res(yes); };
            const key = (e) => { if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); done(false); } };
            m.querySelectorAll("button").forEach((b) => (b.onclick = () => done(b.dataset.x === "1")));
            m.addEventListener("mousedown", (e) => { if (e.target === m) done(false); });
            document.addEventListener("keydown", key, true);
            document.body.append(m); (danger ? m.querySelector('[data-x="0"]') : ok).focus();
        });
    }
    // before something replaces the edited set (another set, New, a file): unsaved changes are asked about
    async discardOk(what) {
        if (!this.set.dirty) return true;
        const n = this.set.name || this.set.saved || "the edited set";
        return this.confirm("Discard the unsaved changes?", `${n} has changes that aren't saved: ${what} replaces them, and Undo can't bring them back.`, "Discard", true);
    }
    prompt(title, value, help = "") {
        return new Promise((res) => {
            const m = document.createElement("div");
            m.className = "modal";
            m.innerHTML = `<div><h3></h3><p class="muted small"></p><input style="width:100%"><div class="row" style="justify-content:flex-end;margin-top:10px"><button data-x="0">Cancel</button><button class="primary" data-x="1">OK</button></div></div>`;
            m.querySelector("h3").textContent = title; m.querySelector("p").textContent = help;
            const inp = m.querySelector("input"); inp.value = value || "";
            const done = (ok) => { m.remove(); res(ok ? inp.value.trim() : null); };
            m.querySelectorAll("button").forEach((b) => (b.onclick = () => done(b.dataset.x === "1")));
            inp.onkeydown = (e) => { if (e.key === "Enter") done(true); if (e.key === "Escape") done(false); };
            document.body.append(m); inp.focus(); inp.select();
        });
    }

    async boot() {
        if (new URLSearchParams(location.search).get("embed") === "1") document.body.classList.add("embed");
        try { this.config = await api.config(); }
        catch (e) { document.body.innerHTML = `<div style="padding:12px">Can't reach serve.py (${e.message}). Start it with tools/physics_editor/run.sh or run.cmd.</div>`; return; }
        this.helper = await createHelper(this.config);
        this.cat = this.helper.catalog;
        this.cat.keySet = new Set(this.cat.all.map((p) => p.key));
        exprCheck.cat = this.cat;
        this.knownMove = new Set(this.cat.params.map((p) => p.key));
        this.games = { picks: loadPicks(), list: [] };
        const hb = $("helperBadge");
        hb.textContent = this.helper.kind === "real" ? "quark_maptool" : "MOCK PHYSICS";
        hb.className = "badge " + this.helper.kind;
        hb.title = this.helper.kind === "real" ? "the game's own pmove(): " + this.config.maptool
            : "a stand-in pmove in the page (" + this.helper.why + "): results are NOT the game's. Build quark_maptool (cmake --build build --target quark_maptool) or pass --maptool.";
        $("dataDir").textContent = this.config.dataDir; $("dataDir").title = "the game's data folder; custom modes: " + this.config.modes;
        this.std = standardPhysics(this.cat);
        this.physics = clonePhysics(this.std);
        this.set = { name: "", saved: null, baseText: "", text: mergeQmode("", this.physics, "my physics", this.knownMove), dirty: false };
        const room = await this.helper.room();
        this.room = { room: true, name: "test room", text: room.text, info: room.info, map: parseQmap(room.text, "test room") };
        this.mapChoice = this.room;
        this.presets = await this.helper.presets();
        this.scenario = JSON.parse(JSON.stringify(this.presets[0].scenario));
        this.presetId = this.presets[0].id;

        this.values = new ValuesView(this, $("pg-values"));
        this.code = new CodeView(this);
        this.moveCode = new MoveCodeView(this);
        this.codeGraph = new CodeGraphView(this);
        this.blocks = new BlocksView(this, $("pg-blocks"));
        this.graph = new GraphView(this);
        this.path = new PathView(this);
        this.drive = new DriveView(this);
        this.exporter = new ExportView(this);
        this._side();
        document.querySelectorAll("#tabs button").forEach((b) => (b.onclick = () => this.showPage(b.dataset.page)));
        window.addEventListener("keydown", (e) => {
            if ((e.ctrlKey || e.metaKey) && e.key === "s") { e.preventDefault(); this.saveSet(); }
            if ((e.ctrlKey || e.metaKey) && e.key === "Enter") { e.preventDefault(); this.run(); }
            // undo / redo, except where the browser's own text undo belongs (the Code text, the equation and name fields)
            const k = e.key.toLowerCase(), t = e.target;
            const texty = t && (t.tagName === "TEXTAREA" || t.isContentEditable || (t.tagName === "INPUT" && /^(text|search|)$/.test(t.type || "")));
            if ((e.ctrlKey || e.metaKey) && !e.altKey && (k === "z" || k === "y") && !texty && !document.pointerLockElement && !document.querySelector(".modal")) {
                e.preventDefault();
                if (k === "y" || e.shiftKey) this.redo(); else this.undo();
            }
        });
        // the session's last change is written when the page goes (the autosave waits 0.8 s for edits to settle)
        window.addEventListener("pagehide", () => this.flushAutosave());
        document.addEventListener("visibilitychange", () => { if (document.visibilityState === "hidden") this.flushAutosave(); });
        this._themeUi();
        await Promise.all([this.refreshModes(), this.refreshMaps(), this.refreshScenarios()]);
        await this.restore();
        this.setGamePicks(this.games.picks, true);
        let want = new URLSearchParams(location.search).get("page");
        try { want = want || localStorage.getItem("qpe.page"); } catch {}
        this.showPage(want && document.getElementById("pg-" + want) ? want : "values");
        this.emit("physics", "load"); this.emit("scenario"); this.emit("map");
        this.run();
    }

    showPage(p) {
        document.querySelectorAll("#tabs button").forEach((b) => b.classList.toggle("on", b.dataset.page === p));
        document.querySelectorAll(".page").forEach((s) => s.classList.toggle("on", s.id === "pg-" + p));
        this.page = p;
        try { localStorage.setItem("qpe.page", p); } catch {}
        this.emit("page", p);
    }

    // ---------------------------------------------------------------- the physics set
    // group: edits in a row to one thing (a slider drag, typing in one field) are one undo step; none: a step of its own
    setPhysics(ph, from, group) {
        this._record(group);
        this.physics = ph;
        if (from !== "code") this.set.text = mergeQmode(this.set.text, ph, this.set.name || "my physics", this.knownMove);
        this.set.dirty = true;
        this.emit("physics", from);
        this.scheduleRun();
        this.scheduleExprCheck();
        this.autosave();
        this._setInfo();
    }
    setCodeText(text) {
        this.set.text = text;
        const p = parseQmode(text, this.cat);
        this.codeParse = p;
        this.setPhysics(p.physics, "code", "code");
        return p;
    }

    // ---------------------------------------------------------------- undo / redo
    // Every change to the edited set (Values, the Code text, a game's values, a reset) is a step; opening another set
    // starts a new history (the unsaved-changes question comes first).
    _record(group) {
        const now = performance.now();
        if (group && group === this._hGroup && now - this._hAt < 1500) { this._hAt = now; return; }
        this.undoStack.push(this._snap());
        if (this.undoStack.length > 300) this.undoStack.shift();
        this.redoStack = [];
        this._hGroup = group; this._hAt = now;
        this._undoUi();
    }
    _snap() { return { physics: clonePhysics(this.physics), text: this.set.text, dirty: this.set.dirty }; }
    undo() { this._step(this.undoStack, this.redoStack, "Undo"); }
    redo() { this._step(this.redoStack, this.undoStack, "Redo"); }
    _step(from, to, what) {
        if (!from.length) return this.toast(`Nothing to ${what.toLowerCase()}`, 1500);
        to.push(this._snap());
        const s = from.pop();
        this.physics = s.physics;
        this.set.text = s.text;
        this.set.dirty = this.set.saved ? s.text !== this.set.baseText : s.dirty;
        this.codeParse = parseQmode(s.text, this.cat);
        this._hGroup = null;
        this.emit("physics", "undo");
        this.scheduleRun(); this.scheduleExprCheck(); this.autosave(); this._setInfo(); this._undoUi();
    }
    _clearHistory() { this.undoStack = []; this.redoStack = []; this._hGroup = null; this._undoUi(); }
    _undoUi() {
        const u = $("setUndo"), r = $("setRedo");
        if (!u) return;
        u.disabled = !this.undoStack.length; r.disabled = !this.redoStack.length;
        u.title = `undo (Ctrl+Z)${this.undoStack.length ? ": " + this.undoStack.length + " step" + (this.undoStack.length > 1 ? "s" : "") : ""}`;
        r.title = `redo (Ctrl+Shift+Z / Ctrl+Y)${this.redoStack.length ? ": " + this.redoStack.length : ""}`;
    }

    // ---------------------------------------------------------------- light / dark
    _themeUi() {
        const b = $("themeBtn");
        const show = () => { const t = themeChoice(); b.textContent = { auto: "◐", light: "☀", dark: "☾" }[t]; b.title = `theme: ${t === "auto" ? "the system's" : t} (click: ${THEMES[(THEMES.indexOf(t) + 1) % 3] === "auto" ? "the system's" : THEMES[(THEMES.indexOf(t) + 1) % 3]})`; };
        b.onclick = () => {
            const t = THEMES[(THEMES.indexOf(themeChoice()) + 1) % 3];
            try { localStorage.setItem("qpe.theme", t); } catch {}
            if (new URLSearchParams(location.search).has("theme")) { const u = new URL(location.href); u.searchParams.delete("theme"); history.replaceState(null, "", u); }
            applyTheme(t); show(); this.emit("theme");
        };
        try { matchMedia("(prefers-color-scheme: light)").addEventListener("change", () => { if (themeChoice() === "auto") this.emit("theme"); }); } catch {}
        this.on("theme", () => { if (this.results) this.emit("results"); });
        show();
    }
    async refreshModes() {
        try { const r = await api.modes(); this.modes = r.modes; this.modesDir = r.dir; } catch { this.modes = []; }
        const sel = $("setSel");
        sel.innerHTML = "";
        sel.append(new Option("(unsaved set)", ""));
        for (const m of this.modes) sel.append(new Option(m.name + (m.moved ? `  (${m.moved} changed)` : "") + (m.logic ? "  · logic" : ""), m.name));
        sel.value = this.set.saved || "";
        this._compareList();
        this.emit("modes");
    }
    async openSet(name) {
        if (!name) return;
        const text = await api.mode(name);
        const p = parseQmode(text, this.cat);
        this.set = { name, saved: name, baseText: text, text, dirty: false };
        this.codeParse = p;
        this.physics = p.physics;
        this._clearHistory();
        $("setName").value = name; $("setSel").value = name;
        this.emit("physics", "load");
        this.scheduleRun(); this.autosave(); this._setInfo();
        if (p.problems.some((x) => x.level === "error")) this.toast(`${name}.qmode has problems: see the Code tab`, 4000);
    }
    newSet() {
        this.physics = clonePhysics(this.std);
        this.set = { name: "", saved: null, baseText: "", text: mergeQmode("", this.physics, "my physics", this.knownMove), dirty: false };
        this._clearHistory();
        $("setName").value = ""; $("setSel").value = "";
        this.emit("physics", "load"); this.scheduleRun(); this.autosave(); this._setInfo();
    }
    async saveSet(asName) {
        let name = asName || $("setName").value.trim() || this.set.saved;
        if (!name) name = await this.prompt("Save as a custom mode", "my physics", "custom_modes/<name>.qmode in the game's data folder: letters, digits, space, _ and -; 40 at most");
        if (!name) return;
        if (!NAME_RE.test(name) || name.endsWith(" ")) { this.toast("Bad name: letters, digits, space, _ and - (40 at most)", 4000); return; }
        if (isReservedName(name)) { this.toast(`Bad name: ${name} is a device name on Windows (con, nul, aux, com1...), not a file`, 5000); return; }
        // a file whose name differs only in case is the same file on Windows: written into (asked first), under its own name
        const same = (this.modes || []).find((m) => sameName(m.name, name));
        if (same) name = same.name;
        const exists = !!same;
        if (exists && !(this.set.saved && sameName(name, this.set.saved)) && !(await this.confirm(`Write into ${name}.qmode?`, `${name}.qmode exists: write this set's movement lines into it? Its other lines (the mode, weapons, the gamemode editor's logic) are kept.`, "Write"))) return;
        const body = { physics: this.physics };
        if (!exists) {
            if (this.set.saved && this.set.saved !== name) body.from = this.set.saved;
            else if (!this.set.saved && this.set.baseText) body.baseText = this.set.baseText;
        }
        try {
            const r = await api.saveMode(name, body);
            this.set = { name, saved: name, baseText: r.text, text: r.text, dirty: false };
            this.codeParse = parseQmode(r.text, this.cat);
            this.physics = this.codeParse.physics;
            $("setName").value = name;
            await this.refreshModes();
            $("setSel").value = name;
            this.emit("physics", "saved"); this._setInfo(); this.autosave();
            this.toast(`Saved ${r.path}${r.created ? " (new)" : ""}: playable now (Host › Custom modes)`, 4000);
        } catch (e) { this.toast("Save failed: " + e.message, 6000); }
    }
    // the Code text's custommode= line says the name a save will write (Save renames it the same way: serve.py)
    _nameLine() {
        const n = this.set.name || this.set.saved || "my physics";
        const own = n === this.set.saved && /^custommode=([^\r\n]*)/m.exec(this.set.baseText || "");   // (saving into its own file keeps its line)
        const t = this.set.text.replace(/^custommode=[^\r\n]*/m, "custommode=" + (own ? own[1] : n));
        if (t === this.set.text) return;
        this.set.text = t; this.codeParse = parseQmode(t, this.cat);
        this.code.load();
    }
    _setInfo() {
        const n = changedKeys(this.physics, this.cat).length;
        $("setInfo").innerHTML = `${n ? `<span class="warn">${n} changed</span> from the standard physics` : "the standard physics"}`
            + (this.set.saved ? ` · <span title="${this.modesDir || ""}">${this.set.saved}.qmode</span>${this.set.dirty ? " <span class='warn'>(unsaved)</span>" : ""}` : " · not saved yet");
    }

    // Values: show one setting's row (from the Code tab's chips and the Code graph)
    gotoSetting(key) {
        this.showPage("values");
        const v = this.values, r = v.rows.get(key);
        if (!r) return;
        const f = document.getElementById("vFilter"), o = document.getElementById("vOnly");
        if (r.tr.style.display === "none") { f.value = ""; o.checked = false; v._apply(); }
        r.tr.scrollIntoView({ block: "center" });
        r.tr.classList.remove("flash"); void r.tr.offsetWidth; r.tr.classList.add("flash");
    }

    // ---------------------------------------------------------------- other games (references/)
    async setGamePicks(ids, quiet) {
        ids = [...new Set(ids)];
        this.games.picks = ids;
        savePicks(ids);
        const old = new Map(this.games.list.map((g) => [g.id, g]));
        this.games.list = ids.map((id) => old.get(id) || { id, game: null });
        if (this.values && this.values.picker) this.values.picker.set(ids);
        for (const id of [...this.compareSel]) if (id.startsWith("game:") && !ids.includes(id.slice(5))) this.compareSel.delete(id);
        this.emit("games"); this._compareList();
        const seq = (this._gseq = (this._gseq || 0) + 1);
        await Promise.all(this.games.list.filter((g) => !g.game && !g.error).map(async (g) => {
            try { const game = await loadGame(g.id); Object.assign(g, { game }, gamePhysics(game, this.cat)); }
            catch (e) { g.error = e.message; }
        }));
        if (seq !== this._gseq) return;
        this.emit("games"); this._compareList();
        if (!quiet || [...this.compareSel].some((x) => x.startsWith("game:"))) this.run();
    }
    gameSet(id) { const g = this.games.list.find((x) => x.id === id); return g && g.physics ? g : null; }

    // the game's own evaluator (physexpr) on the edited set's equations, when the helper has it
    scheduleExprCheck() {
        if (!this.helper.hasExpr) return;
        clearTimeout(this._xt);
        this._xt = setTimeout(async () => {
            const ph = this.physics;
            if (!ph.expr || !Object.keys(ph.expr).length) { this.exprCheck = null; this.emit("exprcheck"); return; }
            try { this.exprCheck = await this.helper.exprCheck(ph); } catch (e) { this.exprCheck = { error: e.message }; }
            this.emit("exprcheck");
        }, 250);
    }

    // ---------------------------------------------------------------- maps, scenarios
    async refreshMaps() {
        try { const r = await api.maps(); this.maps = r.maps; } catch { this.maps = []; }
        const sel = $("mapSel");
        sel.innerHTML = "";
        sel.append(new Option("Test room" + (this.room.info && this.room.info.mock ? " (MOCK)" : ""), "room"));
        for (const m of this.maps) sel.append(new Option(`${m.name}${m.source !== "default" ? " (" + m.source + ")" : ""}`, m.source + ":" + m.name));
    }
    async chooseMap(v) {
        if (v === "room") {
            this.mapChoice = this.room; $("spawnRow").style.display = "none";
            if (this.presets.some((x) => x.id === this.presetId)) this.applyPreset(this.presetId, true);
            else {   // a custom scenario keeps its steps; its start goes back to where it was in the room (else the room's origin,
                     // a preset's start)
                const st = this._roomStart || (this.presets.find((x) => x.id === "standing_jump") || this.presets[0]).scenario.start;
                this.scenario.start = { ...this.scenario.start, pos: [...st.pos], yaw: st.yaw };
                if (st.onGround != null) this.scenario.start.onGround = st.onGround; else delete this.scenario.start.onGround;
                this.setScenario(this.scenario, "spawn");
            }
        } else {
            if (this.mapChoice.room && this.scenario) { const st = this.scenario.start; this._roomStart = { pos: [...st.pos], yaw: st.yaw, onGround: st.onGround }; }
            const [source, ...rest] = v.split(":"); const name = rest.join(":");
            try {
                const text = await api.map(name, source);
                const map = parseQmap(text, name);
                this.mapChoice = { room: false, name, source, text, map };
                const ss = $("spawnSel"); ss.innerHTML = "";
                map.spawns.forEach((s, i) => ss.append(new Option(`${s.raw} spawn ${i + 1} (${s.x.toFixed(0)}, ${s.y.toFixed(0)}, ${s.z.toFixed(0)})`, i)));
                $("spawnRow").style.display = map.spawns.length ? "" : "none";
                this.useSpawn(0);
            } catch (e) { this.toast("Couldn't load the map: " + e.message, 5000); return; }
        }
        $("mapSel").value = v;
        this.emit("map"); this.autosave(); this._presetMapNote();
    }
    useSpawn(i) {
        const s = this.mapChoice.map && this.mapChoice.map.spawns[i | 0];
        if (!s) return;
        this.scenario.start = { ...this.scenario.start, pos: [s.x, s.y, s.z], yaw: s.yaw / (Math.PI / 180) };
        delete this.scenario.start.onGround;
        this.setScenario(this.scenario, "spawn");
    }
    applyPreset(id, keepMap) {
        const p = this.presets.find((x) => x.id === id);
        if (!p) return;
        this.presetId = id;
        $("presetSel").value = id;
        $("presetDesc").textContent = p.desc || "";
        const sc = JSON.parse(JSON.stringify(p.scenario));
        if (!this.mapChoice.room && !keepMap) { this.scenario = sc; this.useSpawn($("spawnSel").value); this._presetMapNote(); return; }
        this.setScenario(sc, "preset");
        this._presetMapNote();
    }
    // The presets are made for the test room (a run-up, a gap, a wall where they need one). On a real map a wall or a
    // ledge in front of the spawn cuts them short (a jump distance of 0): said under the preset, with a button that
    // starts from the spawn and direction with the most open floor ahead.
    _presetMapNote() {
        const box = $("presetNote");
        if (this.mapChoice.room || !this.mapChoice.map) { box.style.display = "none"; return; }
        const preset = this.presets.find((x) => x.id === this.presetId);
        box.style.display = "";
        box.innerHTML = (preset ? `<b>${esc(preset.label)}</b> was made for the test room: on ${esc(this.mapChoice.name)} a wall or a ledge ahead can cut it short (a distance of 0).`
            : `On ${esc(this.mapChoice.name)} the scenario starts at the chosen spawn, facing its way.`)
            + ` <button id="aimBtn" title="start at the spawn and direction with the longest run of open floor ahead (walls and drops checked every 0.5 m, up to 40 m)">aim along open floor</button>`;
        $("aimBtn").onclick = () => this.aimOpenFloor();
    }
    aimOpenFloor() {
        const map = this.mapChoice.map;
        if (!map || !map.spawns.length) return;
        const cur = +$("spawnSel").value || 0;
        let best = { d: -1 };
        map.spawns.forEach((sp, i) => {
            const boxes = nearBoxes(map, sp.x, sp.z, 42);
            for (let k = 0; k < 24; k++) {   // (every 15 degrees; the spawn's own direction first, so a tie keeps it)
                const yaw = sp.yaw + k * Math.PI / 12, d = openRun(boxes, sp.x, sp.y, sp.z, yaw);
                if (d > best.d + 1e-6 || (Math.abs(d - best.d) < 1e-6 && i === cur && best.i !== cur)) best = { d, i, yaw };
            }
        });
        if (best.d <= 0) return this.toast("No open floor ahead of any spawn on this map", 4000);
        $("spawnSel").value = best.i;
        const deg = ((best.yaw / (Math.PI / 180)) % 360 + 540) % 360 - 180;
        this.scenario.start = { ...this.scenario.start, pos: [map.spawns[best.i].x, map.spawns[best.i].y, map.spawns[best.i].z], yaw: +deg.toFixed(1) };
        delete this.scenario.start.onGround;
        this.setScenario(this.scenario, "spawn");
        this.toast(`Starts at spawn ${best.i + 1}, facing ${deg.toFixed(0)}°: ${best.d.toFixed(1)} m of open floor ahead`, 4000);
    }
    setScenario(sc, from) {
        this.scenario = sc;
        this.emit("scenario", from);
        this.scheduleRun();
        this.autosave();
    }
    // a saved scenario (physics_editor/scenarios/<name>.json): its map first (a map that isn't there any more: the
    // test room, said so), then its steps
    async loadScenario(n) {
        let sc;
        try { sc = await api.scenario(n); } catch (e) { this.toast(`Couldn't load the scenario ${n}: ${e.message}`, 5000); return; }
        if (!sc || typeof sc !== "object" || !Array.isArray(sc.steps) || !sc.start) { this.toast(`${n}.json isn't a scenario (no start / steps)`, 5000); return; }
        let want = sc.map && sc.map !== "room" && sc.map.name ? sc.map.source + ":" + sc.map.name : "room";
        if (want !== "room" && !this.maps.some((m) => m.source + ":" + m.name === want)) { this.toast(`${n}: the map ${sc.map.name} isn't there any more: running it in the test room`, 5000); want = "room"; }
        if (want !== $("mapSel").value) await this.chooseMap(want);
        delete sc.map;
        if (want !== "room") this._roomStart = null;   // (its own start was on the map: back in the room it starts at the origin)
        this.presetId = "custom"; $("presetSel").value = "custom"; $("presetDesc").textContent = "saved scenario " + n;
        this.setScenario(sc, "load");
        this._presetMapNote();
    }
    async refreshScenarios() {
        try { const r = await api.scenarios(); this.savedScenarios = r.scenarios; } catch { this.savedScenarios = []; }
        const sel = $("scLoad"); sel.innerHTML = "";
        sel.append(new Option("Load…", ""));
        for (const s of this.savedScenarios) sel.append(new Option(s.name, s.name));
    }

    // ---------------------------------------------------------------- runs
    compareSets() {
        const out = [{ name: "standard", physics: this.std }];
        for (const n of this.compareSel) {
            if (n.startsWith("game:")) { const g = this.gameSet(n.slice(5)); if (g) out.push({ name: g.game.name + " (game)", physics: g.physics, game: g.id }); continue; }
            const s = this.compareCache && this.compareCache.get(n); if (s) out.push({ name: n, physics: s });
        }
        return out;
    }
    // every set the Movement graph's A / B can be: [{id, label, physics()}] (saved sets load on demand)
    setChoices() {
        const out = [{ id: "edit", label: "the edited set" }, { id: "std", label: "standard physics" }];
        for (const m of this.modes || []) out.push({ id: "mode:" + m.name, label: m.name + ".qmode" });
        for (const g of this.games.list) if (g.physics) out.push({ id: "game:" + g.id, label: g.game.name + ` (game: ${g.mapped.length} of ${this.cat.all.length + 2} settings)` });
        return out;
    }
    async physicsOf(id) {
        if (id === "edit") return { physics: this.physics, label: this.set.name || this.set.saved || "edited" };
        if (id === "std") return { physics: this.std, label: "standard" };
        if (id.startsWith("game:")) { const g = this.gameSet(id.slice(5)); return g ? { physics: g.physics, label: g.game.name, game: g } : null; }
        if (id.startsWith("mode:")) {
            const n = id.slice(5);
            if (!this.compareCache.has(n)) { try { this.compareCache.set(n, parseQmode(await api.mode(n), this.cat).physics); } catch { return null; } }
            return { physics: this.compareCache.get(n), label: n };
        }
        return null;
    }
    scheduleRun() {
        if (!$("autoRun").checked) return;
        clearTimeout(this._rt);
        this._rt = setTimeout(() => this.run(), this.helper.kind === "real" ? 90 : 60);
    }
    async run() {
        clearTimeout(this._rt);
        const seq = ++this.runSeq;
        if (this._running) { this._again = true; return; }   // (one run at a time: the newest settings run next)
        this._running = true; this._again = false;
        const info = $("runInfo"); info.classList.add("busy");
        const cmp = this.compareSets();
        const scenario = this.scenario;
        try {
            const r = await this.helper.sim({ physics: this.physics, compare: cmp.map((c) => c.physics), scenario, map: this.mapChoice, sampleMs: 10 });
            const labels = [this.set.name || this.set.saved || "edited", ...cmp.map((c) => c.name)];
            const runs = r.runs.map((run, i) => ({ ...run, label: labels[i] || "run " + i, color: cssVar(RUN_COLORS[i % RUN_COLORS.length]),
                metrics: mergeMetrics(run.metrics, computeMetrics(run, scenario)) }));
            this.results = { runs, scenario, map: this.mapChoice, ms: r.ms, via: r.via, at: Date.now() };
            this.runFailed = null;
            info.textContent = `${r.via}: ${runs.length} run${runs.length > 1 ? "s" : ""} in ${(+r.ms || 0).toFixed(1)} ms${r.wallMs ? ` (${r.wallMs} ms ${r.worker ? "through the helper kept running" : "with the process"})` : ""}`;
            const dyn = this.helper.kind === "real" && !this.helper.hasExpr && [this.physics, ...cmp.map((c) => c.physics)].some((p) => p.expr && resolve(p, this.cat).dynamicKeys.length);
            if (dyn) info.insertAdjacentHTML("beforeend", `<div class="warn">this quark_maptool doesn't run equations yet: dynamic ones run at their value standing still</div>`);
            this._mini();
            this.emit("results");
        } catch (e) {
            // the results on screen aren't this set's any more: kept, greyed, as the last good run when it was the same
            // scenario on the same map; dropped when it wasn't (an old run drawn over another map reads as current)
            if (this.results && (this.results.map !== this.mapChoice || this.results.scenario !== scenario)) this.results = null;
            const line = this._errLine(e.message);
            this.runFailed = { msg: e.message, line };
            info.innerHTML = `<span class="err">${esc(e.message)}</span>${line ? ` <a href="#" id="runErrLine">line ${line} of the .qmode</a>` : ""}`;
            if (line) $("runErrLine").onclick = (ev) => { ev.preventDefault(); this.showPage("code"); this.moveCode.showSub("qmode"); this.code.goto(line); };
            this._mini();
            this.emit("results");
        } finally {
            info.classList.remove("busy");
            this._running = false;
            if (this._again || seq !== this.runSeq) { this._again = false; this.run(); }
        }
    }
    // the metric keys the chosen preset is about (the helper's physpresets list them), while its scenario is unchanged
    presetMetrics() {
        const p = this.presets && this.presets.find((x) => x.id === this.presetId);
        return p && p.metrics && $("presetSel").value === this.presetId ? p.metrics : [];
    }
    allRuns() { return [...((this.results && this.results.runs) || []), ...this.extraRuns]; }
    // the line of the set's text a helper error names (physics.move.<key> / expr.<key> / <key>): the one the game reads
    _errLine(msg) {
        const lines = (this.set.text || "").split("\n");
        for (const m of String(msg).matchAll(/\b(physics\.)?(move\.|expr\.|move_)?([a-z][a-z_]*)\b/g)) {
            const [, ph, pre, k] = m;
            const want = pre === "move." ? ["move_" + k] : pre ? [pre + k] : ph ? [k] : [];   // (a bare word is no key)
            if (!want.length) continue;
            for (let i = lines.length - 1; i >= 0; i--) if (want.includes(lines[i].split("=")[0])) return i + 1;
        }
        return 0;
    }
    // a "last good run" state is shown everywhere results are (Results here, the Movement graph, Path): greyed + said
    staleNote() { return this.runFailed ? (this.results ? "last good run: the current one failed" : "the run failed: no results") : ""; }
    _mini() {
        $("miniMetrics").classList.toggle("stale", !!this.runFailed);
        if (!this.results) { $("resName").textContent = ""; $("miniMetrics").innerHTML = `<tr><td class="err">the run failed (above): no results</td></tr>`; return; }
        const runs = this.results.runs, a = runs[0].metrics, b = runs[1] ? runs[1].metrics : {};
        $("resName").textContent = "(" + (this.results.scenario.name || "scenario") + ")" + (this.runFailed ? " · last good run" : "");
        const rows = metricRows([a], this.presetMetrics()).filter((m) => typeof a[m.key] !== "object").slice(0, 9);
        $("miniMetrics").innerHTML = rows.map((m) => {
            const va = a[m.key], vb = b[m.key];
            if (va == null) return `<tr class="nomet"><td>${m.label}</td><td class="muted" colspan="2">${missingMetric(m.key, a, this.results.scenario)}</td></tr>`;
            const d = typeof va === "number" && typeof vb === "number" ? va - vb : null;
            const cls = d == null || Math.abs(d) < 1e-3 ? "same" : d > 0 ? "up" : "down";
            return `<tr><td>${m.label}</td><td class="n">${fmtMetric(va, m.unit)} ${m.unit}</td><td class="n ${cls}">${d == null || Math.abs(d) < 1e-3 ? "=" : (d > 0 ? "+" : "") + fmtMetric(d, m.unit)}</td></tr>`;
        }).join("") || `<tr><td class="muted">no metrics</td></tr>`;
    }

    // ---------------------------------------------------------------- the side panel
    _side() {
        $("setSel").onchange = async (e) => {
            const v = e.target.value;
            if (!(await this.discardOk(v ? `opening ${v}.qmode` : "a new set"))) { e.target.value = this.set.saved || ""; return; }
            v ? this.openSet(v) : this.newSet();
        };
        $("setNew").onclick = async () => { if (await this.discardOk("a new set")) this.newSet(); };
        $("setUndo").onclick = () => this.undo();
        $("setRedo").onclick = () => this.redo();
        $("setSave").onclick = () => this.saveSet();
        $("setSaveAs").onclick = async () => { const n = await this.prompt("Save as", this.set.name || "my physics", "a new custom mode: letters, digits, space, _ and -"); if (n) this.saveSet(n); };
        $("setName").oninput = (e) => { this.set.name = e.target.value.trim(); this._nameLine(); this.autosave(); };
        $("setOpenFile").onclick = async () => {
            if (!(await this.discardOk("opening a file"))) return;
            const f = await this.pickFile(".qmode,.txt");
            if (!f) return;
            const p = parseQmode(f.text, this.cat);
            this.set = { name: (p.name || f.name.replace(/\.[^.]*$/, "")).trim(), saved: null, baseText: f.text, text: f.text, dirty: true };
            $("setName").value = this.set.name; $("setSel").value = "";
            this.codeParse = p; this.physics = p.physics; this._clearHistory();
            this.emit("physics", "load"); this.scheduleRun(); this.autosave(); this._setInfo();
            this.toast(`Opened ${f.name}: Save writes it to custom_modes/`, 3500);
        };
        $("setDownload").onclick = () => this.download((this.set.name || "physics") + ".qmode", this.set.text);
        $("setResetAll").onclick = async () => {
            const n = changedKeys(this.physics, this.cat).length + (this.physics.imports ? 1 : 0);
            if (!n) return this.toast("Every value is at the standard physics already");
            if (await this.confirm("Reset every value?", `Every movement value (and equation) goes back to the standard physics. Undo (Ctrl+Z) takes it back.`, "Reset all", true))
                this.setPhysics(clonePhysics(this.std), "reset");
        };
        const ps = $("presetSel");
        for (const p of this.presets) ps.append(new Option(p.label, p.id));
        ps.append(new Option("(custom: the Blocks tab)", "custom"));
        ps.value = this.presetId; $("presetDesc").textContent = this.presets[0].desc || "";
        ps.onchange = (e) => { if (e.target.value !== "custom") this.applyPreset(e.target.value); };
        $("mapSel").onchange = (e) => this.chooseMap(e.target.value);
        $("spawnSel").onchange = (e) => this.useSpawn(e.target.value);
        $("runBtn").onclick = () => this.run();
        $("scSave").onclick = async () => {
            const n = await this.prompt("Save the scenario", (this.scenario.name || "scenario").replace(/[^A-Za-z0-9_.-]+/g, "_"), "physics_editor/scenarios/<name>.json in the data folder");
            if (!n) return;
            if (!/^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$/.test(n) || isReservedName(n)) return this.toast("Bad name: letters, digits, _ . - (and not a Windows device name: con, nul...)", 4000);
            if (this.savedScenarios.some((s) => sameName(s.name, n)) && !(await this.confirm("Replace the saved scenario?", `${n}.json is saved already: write this scenario over it?`, "Replace"))) return;
            try {
                await api.saveScenario(n, { ...this.scenario, name: this.scenario.name || n, map: this.mapChoice.room ? "room" : { name: this.mapChoice.name, source: this.mapChoice.source } });
                await this.refreshScenarios(); this.toast("Saved scenario " + n);
            } catch (err) { this.toast("Saving the scenario failed: " + err.message, 6000); }
        };
        $("scLoad").onchange = async (e) => {
            const n = e.target.value; e.target.value = "";
            if (n) await this.loadScenario(n);
        };
        this.on("scenario", (from) => { if (from === "blocks") { this.presetId = "custom"; $("presetSel").value = "custom"; $("presetDesc").textContent = "your own scenario (Blocks)"; this._presetMapNote(); } });
        this._setInfo();
    }
    _compareList() {
        const box = $("cmpList");
        box.innerHTML = `<label class="check"><input type="checkbox" checked disabled><span class="swatch" style="background:var(--run1)"></span>standard physics</label>`;
        this.compareCache = this.compareCache || new Map();
        const order = this.compareSets().map((c) => (c.game ? "game:" + c.game : c.name));
        const col = (key) => RUN_COLORS[(order.indexOf(key) + 1) % RUN_COLORS.length];
        for (const g of (this.games && this.games.list) || []) {
            if (!g.physics) continue;
            const key = "game:" + g.id, on = this.compareSel.has(key);
            const l = document.createElement("label");
            l.className = "check game";
            l.title = `${g.game.name}'s values as a physics set: only the ${g.mapped.length} settings it maps are applied, the other ${this.cat.all.length + 2 - g.mapped.length} stay at Quark's standard`;
            l.innerHTML = `<input type="checkbox" ${on ? "checked" : ""}><span class="swatch" style="background:var(${col(key)});${on ? "" : "opacity:0"}"></span>`;
            l.append(document.createTextNode(g.game.name));
            l.insertAdjacentHTML("beforeend", ` <span class="muted small">game · ${g.mapped.length} mapped</span>`);
            l.querySelector("input").onchange = (e) => { e.target.checked ? this.compareSel.add(key) : this.compareSel.delete(key); this._compareList(); this.run(); this.autosave(); };
            box.append(l);
        }
        for (const m of this.modes || []) {
            const l = document.createElement("label");
            l.className = "check";
            const on = this.compareSel.has(m.name);
            l.innerHTML = `<input type="checkbox" ${on ? "checked" : ""}><span class="swatch" style="background:var(${col(m.name)});${on ? "" : "opacity:0"}"></span>`;
            l.append(document.createTextNode(m.name));
            l.querySelector("input").onchange = async (e) => {
                if (e.target.checked) {
                    try { const t = await api.mode(m.name); this.compareCache.set(m.name, parseQmode(t, this.cat).physics); this.compareSel.add(m.name); } catch {}
                } else this.compareSel.delete(m.name);
                this._compareList(); this.run(); this.autosave();
            };
            box.append(l);
        }
        if (!(this.modes || []).length) box.insertAdjacentHTML("beforeend", `<div class="muted small">saved custom modes show here</div>`);
    }

    // ---------------------------------------------------------------- the session (the data folder, not the browser)
    autosave() {
        clearTimeout(this._as);
        this._as = setTimeout(() => this.flushAutosave(), 800);
    }
    flushAutosave() {
        if (!this._as) return;
        clearTimeout(this._as); this._as = 0;
        api.saveState("session", { set: this.set, physics: this.physics, scenario: this.scenario, presetId: this.presetId,
            map: this.mapChoice.room ? "room" : { name: this.mapChoice.name, source: this.mapChoice.source }, compare: [...this.compareSel] }, true).catch(() => {});
    }
    async restore() {
        let s = null;
        try { s = await api.state("session"); } catch {}
        if (!s || typeof s !== "object") return;
        try {
            if (s.set && typeof s.set.text === "string") {
                this.set = { name: s.set.name || "", saved: s.set.saved || null, baseText: s.set.baseText || "", text: s.set.text, dirty: !!s.set.dirty };
                this.codeParse = parseQmode(this.set.text, this.cat);
                this.physics = this.codeParse.physics;
                $("setName").value = this.set.name; $("setSel").value = this.set.saved && (this.modes || []).some((m) => m.name === this.set.saved) ? this.set.saved : "";
            }
            if (s.map && s.map !== "room" && s.map.name) { $("mapSel").value = s.map.source + ":" + s.map.name; if ($("mapSel").value) await this.chooseMap($("mapSel").value); }
            if (s.scenario && Array.isArray(s.scenario.steps)) this.scenario = s.scenario;
            if (s.presetId) { this.presetId = s.presetId; $("presetSel").value = this.presets.some((p) => p.id === s.presetId) ? s.presetId : "custom"; const p = this.presets.find((x) => x.id === s.presetId); $("presetDesc").textContent = p ? p.desc || "" : "your own scenario"; }
            for (const n of s.compare || []) {
                if (n.startsWith("game:")) { if (this.games.picks.includes(n.slice(5))) this.compareSel.add(n); continue; }
                if (!(this.modes || []).some((m) => m.name === n)) continue;
                try { const t = await api.mode(n); this.compareCache.set(n, parseQmode(t, this.cat).physics); this.compareSel.add(n); } catch {}
            }
            this._compareList(); this._setInfo(); this._presetMapNote();
        } catch (e) { console.warn("session not restored", e); }
    }
}

const app = new App();
window.qpe = app;   // (for the console and the headless checks)
app.boot().catch((e) => { console.error(e); app.toast("Error: " + e.message, 10000); });
