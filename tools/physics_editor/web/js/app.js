// Boot and shared state: the physics set being edited, the scenario, the runs. The tabs (values.js, code.js,
// blocks.js, graph.js, pathview.js, drive.js, export.js) wire themselves to this and to each other through events:
//   physics (from)  the edited set changed (from: "values" | "code" | "load" | ...): views refresh, runs re-run
//   scenario        the scenario changed           results   new runs to show            map   the test map changed
//   games           the picked reference games (Values › Compare with games) or their data changed
//   exprcheck       the helper's own evaluator (physexpr) answered for the edited set's equations
import { api } from "./api.js";
import { createHelper } from "./helper.js";
import { parseQmap } from "./qmap.js";
import { standardPhysics, clonePhysics, changedKeys } from "./params.js";
import { parseQmode, mergeQmode } from "./qmode.js";
import { computeMetrics, mergeMetrics, metricRows } from "./sim.js";
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

class App {
    constructor() { this.handlers = {}; this.compareSel = new Set(); this.extraRuns = []; this.runSeq = 0; }
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
        });
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
    setPhysics(ph, from) {
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
        this.setPhysics(p.physics, "code");
        return p;
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
        $("setName").value = name; $("setSel").value = name;
        this.emit("physics", "load");
        this.scheduleRun(); this.autosave(); this._setInfo();
        if (p.problems.some((x) => x.level === "error")) this.toast(`${name}.qmode has problems: see the Code tab`, 4000);
    }
    newSet() {
        this.physics = clonePhysics(this.std);
        this.set = { name: "", saved: null, baseText: "", text: mergeQmode("", this.physics, "my physics", this.knownMove), dirty: false };
        $("setName").value = ""; $("setSel").value = "";
        this.emit("physics", "load"); this.scheduleRun(); this.autosave(); this._setInfo();
    }
    async saveSet(asName) {
        let name = asName || $("setName").value.trim() || this.set.saved;
        if (!name) name = await this.prompt("Save as a custom mode", "my physics", "custom_modes/<name>.qmode in the game's data folder: letters, digits, space, _ and -; 40 at most");
        if (!name) return;
        if (!NAME_RE.test(name) || name.endsWith(" ")) { this.toast("Bad name: letters, digits, space, _ and - (40 at most)", 4000); return; }
        const exists = (this.modes || []).some((m) => m.name === name);
        if (exists && name !== this.set.saved && !confirm(`${name}.qmode exists: write this set's movement lines into it? (its other lines are kept)`)) return;
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
        if (v === "room") { this.mapChoice = this.room; $("spawnRow").style.display = "none"; this.applyPreset(this.presetId, true); }
        else {
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
        this.emit("map"); this.autosave();
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
        if (!this.mapChoice.room && !keepMap) { this.scenario = sc; this.useSpawn($("spawnSel").value); return; }
        this.setScenario(sc, "preset");
    }
    setScenario(sc, from) {
        this.scenario = sc;
        this.emit("scenario", from);
        this.scheduleRun();
        this.autosave();
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
            this.results = { runs, scenario, ms: r.ms, via: r.via, at: Date.now() };
            info.textContent = `${r.via}: ${runs.length} run${runs.length > 1 ? "s" : ""} in ${(+r.ms || 0).toFixed(1)} ms${r.wallMs ? ` (${r.wallMs} ms with the process)` : ""}`;
            const dyn = this.helper.kind === "real" && !this.helper.hasExpr && [this.physics, ...cmp.map((c) => c.physics)].some((p) => p.expr && resolve(p, this.cat).dynamicKeys.length);
            if (dyn) info.insertAdjacentHTML("beforeend", `<div class="warn">this quark_maptool doesn't run equations yet: dynamic ones run at their value standing still</div>`);
            this._mini();
            this.emit("results");
        } catch (e) {
            info.innerHTML = `<span class="err">${e.message}</span>`;
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
    _mini() {
        const runs = this.results.runs, a = runs[0].metrics, b = runs[1] ? runs[1].metrics : {};
        $("resName").textContent = "(" + (this.scenario.name || "scenario") + ")";
        const rows = metricRows([a], this.presetMetrics()).filter((m) => typeof a[m.key] !== "object").slice(0, 9);
        $("miniMetrics").innerHTML = rows.map((m) => {
            const va = a[m.key], vb = b[m.key];
            const d = typeof va === "number" && typeof vb === "number" ? va - vb : null;
            const cls = d == null || Math.abs(d) < 1e-3 ? "same" : d > 0 ? "up" : "down";
            return `<tr><td>${m.label}</td><td class="n">${fmtMetric(va, m.unit)} ${m.unit}</td><td class="n ${cls}">${d == null || Math.abs(d) < 1e-3 ? "=" : (d > 0 ? "+" : "") + fmtMetric(d, m.unit)}</td></tr>`;
        }).join("") || `<tr><td class="muted">no metrics</td></tr>`;
    }

    // ---------------------------------------------------------------- the side panel
    _side() {
        $("setSel").onchange = (e) => (e.target.value ? this.openSet(e.target.value) : this.newSet());
        $("setNew").onclick = () => this.newSet();
        $("setSave").onclick = () => this.saveSet();
        $("setSaveAs").onclick = async () => { const n = await this.prompt("Save as", this.set.name || "my physics", "a new custom mode: letters, digits, space, _ and -"); if (n) this.saveSet(n); };
        $("setName").oninput = (e) => { this.set.name = e.target.value.trim(); this.autosave(); };
        $("setOpenFile").onclick = async () => {
            const f = await this.pickFile(".qmode,.txt");
            if (!f) return;
            const p = parseQmode(f.text, this.cat);
            this.set = { name: (p.name || f.name.replace(/\.[^.]*$/, "")).trim(), saved: null, baseText: f.text, text: f.text, dirty: true };
            $("setName").value = this.set.name; $("setSel").value = "";
            this.codeParse = p; this.physics = p.physics;
            this.emit("physics", "load"); this.scheduleRun(); this.autosave(); this._setInfo();
            this.toast(`Opened ${f.name}: Save writes it to custom_modes/`, 3500);
        };
        $("setDownload").onclick = () => this.download((this.set.name || "physics") + ".qmode", this.set.text);
        $("setResetAll").onclick = () => this.setPhysics(clonePhysics(this.std), "reset");
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
            if (!/^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$/.test(n)) return this.toast("Bad name: letters, digits, _ . -", 4000);
            await api.saveScenario(n, { ...this.scenario, name: this.scenario.name || n, map: this.mapChoice.room ? "room" : { name: this.mapChoice.name, source: this.mapChoice.source } });
            await this.refreshScenarios(); this.toast("Saved scenario " + n);
        };
        $("scLoad").onchange = async (e) => {
            const n = e.target.value; e.target.value = "";
            if (!n) return;
            const sc = JSON.parse(await api.scenario(n));
            if (sc.map && sc.map !== "room" && sc.map.name) { $("mapSel").value = sc.map.source + ":" + sc.map.name; await this.chooseMap($("mapSel").value); }
            this.presetId = "custom"; $("presetSel").value = "custom"; $("presetDesc").textContent = "saved scenario " + n;
            this.setScenario(sc, "load");
        };
        this.on("scenario", (from) => { if (from === "blocks") { this.presetId = "custom"; $("presetSel").value = "custom"; $("presetDesc").textContent = "your own scenario (Blocks)"; } });
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
        this._as = setTimeout(() => {
            api.saveState("session", { set: this.set, physics: this.physics, scenario: this.scenario, presetId: this.presetId,
                map: this.mapChoice.room ? "room" : { name: this.mapChoice.name, source: this.mapChoice.source }, compare: [...this.compareSel] }).catch(() => {});
        }, 800);
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
            this._compareList(); this._setInfo();
        } catch (e) { console.warn("session not restored", e); }
    }
}

const app = new App();
window.qpe = app;   // (for the console and the headless checks)
app.boot().catch((e) => { console.error(e); app.toast("Error: " + e.message, 10000); });
