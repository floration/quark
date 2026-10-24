// Adapted from tools/bot_editor/web/js/sim.js and batch.js at c7aae27 (the run stream over SSE, the run bar), cut down
// to what a mode needs: the Sim view runs `quark_maptool modesim` (tools/mode_editor/native/MODESIM.md) on the file
// being edited and draws its NDJSON answer: a timeline (scores, phase bands, round results, markers, the interpreted
// slots' cost per frame), a top-down plot of the map with the players on it, an event log and an end card (with the
// logic's ns per tick). Compare runs the built-in base mode with the same seed on the same time axis; Batch streams
// `modebatch` (win rates, lengths, rounds, per bot, CSV).
//
// When the helper has no modesim (or isn't there) the view says so and runs nothing: there is no stand-in simulation,
// so a result on this page is always the game's. Each run says whether the server ran the file's logic.
//
// The form (map, bots, brains, seed...) lives in the view, not the DOM: leaving the tab and coming back keeps it, a
// run in progress stays running, and the form plus the running runs' ids are kept through serve.py (/api/state sim)
// so a reloaded page picks its runs up again. Named setups are scenarios (<data>/mode_editor/scenarios/<name>.json).
//
// timelineModel() and batchModel() are pure (no DOM): the tests and the view both use them.

import { api } from "./api.js";

const esc = (x) => String(x ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const PHASE_COLORS = { warmup: "#334155", freeze: "#1e3a5f", live: "#14532d", roundEnd: "#5b3a12", matchEnd: "#4c1d24" };
const MARK = { kill: "#f87171", roundEnd: "#fbbf24", matchEnd: "#fde047", plant: "#fb923c", defuse: "#60a5fa", explode: "#ef4444", capture: "#a78bfa",
    checkpoint: "#34d399", finish: "#22c55e", log: "#e5e7eb", cartCheckpoint: "#22d3ee", overtime: "#facc15", hill: "#c084fc", tag: "#f472b6",
    it: "#f472b6", bombDropped: "#fdba74", modifier: "#93c5fd", tiebreak: "#93c5fd", tourney: "#93c5fd" };
// events that are drawn some other way (phase bands, the score lines) or too frequent to mark
const NOT_MARKERS = new Set(["phase", "score", "bombCarried", "bombReset"]);
const TEAM_COLOR = { a: "#f05252", b: "#3f83f8" };   // (style.css --team-a / --team-b)
const SLOT_COLORS = ["#a78bfa", "#22d3ee", "#fbbf24", "#34d399", "#f472b6", "#fb923c", "#60a5fa", "#e879f9", "#a3e635", "#f87171"];
const teamOf = (t) => (t === 1 || t === "b" || t === "B" ? "b" : t === 0 || t === "a" || t === "A" ? "a" : null);
const NAME_RE = /^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$/;
const us = (ns) => (ns >= 10000 ? (ns / 1000).toFixed(0) + " µs" : ns >= 1000 ? (ns / 1000).toFixed(1) + " µs" : Math.round(ns) + " ns");

// NDJSON lines (objects) -> the timeline. Lenient about field names the helper may add; never invents values.
export function timelineModel(lines) {
    const m = { start: null, end: null, frames: [], bands: [], markers: [], rounds: [], slots: [], slotFrames: [], slotNames: [], players: new Map(), names: new Map(),
        errors: [], warnings: [], t0: 0, t1: 0, maxScore: 1, maxSlotNs: 0 };
    let band = null;
    const nm = (id) => (id === undefined || id === null || id < 0 ? "world" : m.names.get(id) || "#" + id);
    const slotName = (n) => { if (!m.slotNames.includes(n)) m.slotNames.push(n); };
    for (const L of lines) {
        if (!L || typeof L !== "object") continue;
        const type = L.type || L.kind || (L.t !== undefined && L.phase !== undefined ? "frame" : "");
        if (type === "start") {
            m.start = L;
            for (const b of L.bots || []) if (b.id !== undefined) m.names.set(b.id, b.name || "#" + b.id);
        } else if (type === "end") m.end = L;
        else if (type === "error") m.errors.push(L.error || L.message || JSON.stringify(L));
        else if (type === "warning") m.warnings.push(L.warning || L.message || "");
        else if (type === "slot") { m.slots.push({ t: +L.t || 0, slot: L.slot, ns: L.ns ?? null, actions: L.actions || [] }); slotName(L.slot); }
        else if (type === "frame") {
            const t = +L.t || 0;
            const f = { t, phase: L.phase || "?", round: L.round ?? null, clock: L.clock ?? null, scoreA: L.scoreA ?? null, scoreB: L.scoreB ?? null,
                obj: L.obj || null, flags: L.flags || [], players: L.players || L.bots || [] };
            m.frames.push(f);
            if (!band || band.phase !== f.phase) { if (band) band.t1 = t; band = { phase: f.phase, t0: t, t1: t }; m.bands.push(band); }
            band.t1 = t;
            for (const p of f.players) {
                if (p.id === undefined) continue;
                let P = m.players.get(p.id);
                if (!P) { P = { id: p.id, name: p.name || nm(p.id), team: teamOf(p.team), scores: [] }; m.players.set(p.id, P); }
                if (p.score !== undefined) { P.scores.push([t, p.score]); m.maxScore = Math.max(m.maxScore, p.score); }
            }
            if (f.scoreA !== null) m.maxScore = Math.max(m.maxScore, f.scoreA);
            if (f.scoreB !== null) m.maxScore = Math.max(m.maxScore, f.scoreB);
            // trace: the interpreted slots' calls since the last frame [{slot, calls, ns, avg, max}]
            if (Array.isArray(L.slots) && L.slots.length) {
                let ns = 0;
                for (const s of L.slots) { slotName(s.slot); ns += s.ns || 0; }
                m.slotFrames.push({ t, slots: L.slots, ns });
                m.maxSlotNs = Math.max(m.maxSlotNs, ns);
            }
            for (const e of L.events || []) {
                const kind = e.type || e.kind || "event";
                if (kind === "roundEnd") m.rounds.push({ t: e.t ?? t, round: e.round, winner: teamOf(e.winner), text: e.text || "", reason: e.reason || "" });
                if (NOT_MARKERS.has(kind)) continue;
                m.markers.push({ t: e.t ?? t, kind, text: describeEvent(e, nm), raw: e });
            }
        }
    }
    if (m.frames.length) { m.t0 = m.frames[0].t; m.t1 = m.frames[m.frames.length - 1].t; }
    return m;
}
function describeEvent(e, nm) {
    const k = e.type || e.kind;
    if (e.text && k !== "log") return e.text;
    switch (k) {
        case "kill": return e.killer === e.victim || e.killer === undefined || e.killer < 0 ? `${nm(e.victim)} died (${e.weapon || "?"})` : `${nm(e.killer)} killed ${nm(e.victim)} (${e.weapon || "?"})`;
        case "plant": return `bomb planted${e.who !== undefined ? " by " + nm(e.who) : ""}`;
        case "defuse": return `bomb defused${e.who !== undefined ? " by " + nm(e.who) : ""}`;
        case "explode": return "the bomb went off";
        case "bombDropped": return "bomb dropped";
        case "capture": return `team ${String(e.team || "?").toUpperCase()} took point ${e.point || ""}`;
        case "hill": return e.owner ? `team ${e.owner.toUpperCase()} holds the hill` : "nobody holds the hill";
        case "checkpoint": return `${nm(e.who)} reached checkpoint ${e.index}`;
        case "finish": return `${nm(e.who)} finished`;
        case "tag": return `${nm(e.who)} tagged ${nm(e.victim)}`;
        case "it": return `${nm(e.who)} is it`;
        case "cartCheckpoint": return `cart checkpoint (+${e.bonus} s)`;
        case "overtime": return "overtime";
        case "log": return e.text || "";
    }
    const o = { ...e }; delete o.t; delete o.type; delete o.kind;
    return Object.entries(o).map(([k2, v]) => `${k2}=${typeof v === "object" ? JSON.stringify(v) : v}`).join(" ");
}

// modebatch NDJSON lines -> {start, matches, progress, end, errors}
export function batchModel(lines) {
    const b = { start: null, matches: [], progress: null, end: null, errors: [], warnings: [] };
    for (const L of lines) {
        if (!L || typeof L !== "object") continue;
        if (L.type === "start") b.start = L;
        else if (L.type === "match") { b.matches.push(L); b.progress = { done: L.done, total: L.total }; }
        else if (L.type === "progress") b.progress = { done: L.done, total: L.total };
        else if (L.type === "end") b.end = L;
        else if (L.type === "warning") b.warnings.push(L.warning);
        else if (L.type === "error" || L.error) b.errors.push(L.error || JSON.stringify(L));
    }
    return b;
}

// The time and score axes runs share: compared runs are drawn on the same scale, so their curves read against each other
export function sharedAxes(models) {
    const ms = models.filter((m) => m && m.frames.length);
    if (!ms.length) return { t0: 0, t1: 1, maxScore: 1 };
    return { t0: Math.min(...ms.map((m) => m.t0)), t1: Math.max(...ms.map((m) => m.t1)), maxScore: Math.max(...ms.map((m) => m.maxScore)) };
}

// what a mode's end card shows: team score and rounds only where the mode has them, the objective column only when
// someone got objective time
export function endCardFields(start, end) {
    const teams = start ? start.teams !== false : true;
    const players = (end && end.result && end.result.players) || [];
    return { teams, rounds: teams || (end && end.rounds > 0), objTime: players.some((p) => p.objTime > 0) };
}

const FORM_DEFAULT = { map: "", bots: null, skill: 2, seed: 1, secs: 1800, hz: 125, real: false, trace: true, compare: false, brainA: "default", brainB: "", n: 20 };

export class SimView {
    constructor(root, app) {
        this.root = root; this.app = app;   // app: the ModePage
        this.status = null;                 // /api/modesim/status
        this.form = { ...FORM_DEFAULT };    // map "" / bots null: the mode's own (its map, 2 or 8 bots)
        this.runs = [];                     // [{label, kind, lines, model, id, es, done, stopped}]
        this.batches = [];                  // [{label, kind, lines, id, es, done, stopped, request}]
        this.brains = []; this.scenarios = []; this.scenario = "";
        this.busy = false;
    }
    async refresh() {
        try { this.status = await api.modesimStatus(); } catch (e) { this.status = { available: false, reason: e.message }; }
        if (this.status.available) {
            const [maps, brains, scen] = await Promise.all([this.maps ? null : api.maps().catch(() => ({ maps: [] })), api.brains().catch(() => ({ brains: [] })), api.scenarios().catch(() => ({ scenarios: [] }))]);
            if (maps) this.maps = maps.maps;
            this.brains = brains.brains || []; this.brainsDir = brains.dir;
            this.scenarios = scen.scenarios || []; this.scenariosDir = scen.dir;
            if (!this._restored) { this._restored = true; await this._restore(); }
        }
        this.render();
    }
    show() { if (!this.status) this.refresh(); else this.render(); }

    // ---- the form, kept here (not in the DOM) and through serve.py
    _maps() { return (this.maps || []).filter((m) => m.source === "default" || m.source === "user"); }
    _twoPlayer() { const b = this.app.base; return b === "duel" || b === "1v1" || b === "tourney"; }
    _value(k) {
        const f = this.form;
        if (k === "map") { const maps = this._maps(); return f.map && maps.some((m) => m.name === f.map) ? f.map : f.map && !maps.length ? f.map : (maps.find((m) => m.for === this.app.base) || maps[0] || { name: "" }).name; }
        if (k === "bots") return f.bots ?? (this._twoPlayer() ? 2 : 8);
        return f[k];
    }
    _readForm() {
        const R = this.root, v = (id) => R.querySelector("#" + id);
        if (!v("smRun")) return;
        const map = v("smMap").value, autoMap = this._value("map");
        const bots = +v("smBots").value;
        this.form = { map: map === autoMap && !this.form.map ? "" : map, bots: bots === (this._twoPlayer() ? 2 : 8) && this.form.bots === null ? null : bots,
            skill: +v("smSkill").value, seed: +v("smSeed").value, secs: +v("smSecs").value, hz: +v("smHz").value, real: v("smReal").checked,
            trace: v("smTrace").checked, compare: v("smCompare").checked, brainA: v("smBrainA").value, brainB: v("smBrainB").value, n: v("smN") ? +v("smN").value : this.form.n };
        this._persistSoon();
    }
    _persistSoon() { clearTimeout(this._pt); this._pt = setTimeout(() => this._persist(), 400); }
    _persist() {
        const ids = [...this.runs, ...this.batches].filter((r) => r.id).map((r) => ({ id: r.id, label: r.label, kind: r.kind }));
        api.saveState("sim", { form: this.form, scenario: this.scenario, runs: ids }).catch(() => {});
    }
    async _restore() {
        let st = null;
        try { st = await api.state("sim"); } catch {}
        if (!st) return;
        if (st.form) this.form = { ...FORM_DEFAULT, ...st.form };
        this.scenario = st.scenario || "";
        // the runs the page had going: serve.py keeps their lines, so they replay from the start and go on streaming
        for (const x of st.runs || []) {
            let info;
            try { info = await api.run(x.id); } catch { continue; }
            const r = { label: x.label, kind: x.kind, lines: [], model: null, id: x.id, done: false };
            (x.kind === "batch" ? this.batches : this.runs).push(r);
            this._attach(r, x.kind === "batch" ? () => this._soonBatch() : () => this._soon(), info.args && info.args[0]);
        }
        this.busy = [...this.runs, ...this.batches].some((r) => !r.done);
    }

    render() {
        const R = this.root, st = this.status;
        if (!st) { R.innerHTML = `<div class="pad muted">Looking for quark_maptool…</div>`; return; }
        if (!st.available) {
            R.innerHTML = `<div class="sim-off"><div class="badge mock">SIMULATION UNAVAILABLE</div>
              <p>${esc(st.reason || "quark_maptool modesim isn't available.")}</p>
              <p class="muted">The Sim view runs the game's own headless helper, <code>quark_maptool modesim</code> (tools/mode_editor/native/MODESIM.md), on the file you are editing. Until a build has that command this view runs nothing: there is no stand-in, so nothing here is ever a made-up result.</p>
              <p class="muted">${st.maptool ? `Found quark_maptool at <code>${esc(st.maptool)}</code>${st.commands ? `; its commands: ${esc(st.commands.join(", "))}` : ""}.` : "quark_maptool wasn't found in this repository's build folders or on PATH (set \"maptool\" in mode_editor.json or pass --maptool)."}</p>
              <button id="simRecheck">Check again</button></div>`;
            R.querySelector("#simRecheck").onclick = () => { this.status = null; this.render(); this.refresh(); };
            return;
        }
        const maps = this._maps(), base = this.app.base, f = this.form, map = this._value("map");
        const opt = (v, label, sel) => `<option value="${esc(v)}" ${sel ? "selected" : ""}>${esc(label)}</option>`;
        const brainOpts = (sel, extra) => (extra ? opt("", extra, !sel) : "") + opt("default", "built-in", sel === "default") + this.brains.map((b) => opt(b, b, sel === b)).join("")
            + (sel && sel !== "default" && !this.brains.includes(sel) ? opt(sel, sel + " (missing)", true) : "");
        const brainTitle = this.brains.length ? `the bot editor's brains in ${this.brainsDir}` : `no brains yet in ${this.brainsDir || "<data>/bots"} (the bot editor saves them there)`;
        R.innerHTML = `<div class="sim-form">
          <div class="row">
            <label>Map ${maps.length ? `<select id="smMap">${maps.map((m) => opt(m.name, `${m.name}${m.for ? " (" + m.for + ")" : ""}${m.source === "user" ? " [yours]" : ""}`, m.name === map)).join("")}</select>` : `<input id="smMap" value="${esc(map)}" placeholder="map name" title="no built maps/ folder was found: type a map name the helper knows" style="width:110px">`}</label>
            <label>Bots <input id="smBots" type="number" value="${esc(this._value("bots"))}" min="1" max="32" style="width:52px"></label>
            <label>Skill <select id="smSkill">${["beginner", "intermediate", "advanced", "expert"].map((s, i) => opt(i, s, f.skill === i)).join("")}</select></label>
            <label title="${esc(brainTitle)}">Brain <select id="smBrainA">${brainOpts(f.brainA)}</select></label>
            <label title="the odd bots' brain (team B in team modes): one brain against another">vs <select id="smBrainB">${brainOpts(f.brainB, "same")}</select></label>
            <label>Seed <input id="smSeed" type="number" value="${esc(f.seed)}" style="width:64px"></label>
            <label title="the most game time a run plays (it stops at the match end)">Max s <input id="smSecs" type="number" value="${esc(f.secs)}" min="5" max="7200" style="width:64px"></label>
            <label title="the server's tick rate">Hz <select id="smHz">${[125, 250, 500, 1000].map((h) => opt(h, h, f.hz === h)).join("")}</select></label>
          </div>
          <div class="row">
            <label title="paced at real time (else as fast as the CPU goes)"><input id="smReal" type="checkbox" ${f.real ? "checked" : ""}> real time</label>
            <label title="the interpreted slots' calls and cost per frame (the strip under the timeline) and per slot (the end card)"><input id="smTrace" type="checkbox" ${f.trace ? "checked" : ""}> trace slots</label>
            <label title="also run the built-in ${esc(base)} with the same seed, on the same time axis"><input id="smCompare" type="checkbox" ${f.compare ? "checked" : ""}> compare with built-in</label>
            <button id="smRun" class="primary" ${this.busy ? "disabled" : ""}>▶ Run</button><button id="smStop" ${this.busy ? "" : "disabled"}>■ Stop</button>
            ${st.batch ? `<label>Batch seeds <input id="smN" type="number" value="${esc(f.n)}" min="2" max="1000" style="width:60px"></label><button id="smBatch" ${this.busy ? "disabled" : ""}>Run batch</button>` : '<span class="muted" title="quark_maptool has no modebatch">(no modebatch)</span>'}
            <span class="sp"></span>
            <label title="saved setups (${esc(this.scenariosDir || "<data>/mode_editor/scenarios")})">Scenario <select id="smScen">${opt("", "—", !this.scenario)}${this.scenarios.map((s) => opt(s.name, s.name, s.name === this.scenario)).join("")}</select></label>
            <button id="smScenSave" title="save this setup as a scenario">Save…</button><button id="smScenDel" title="delete the scenario" ${this.scenario ? "" : "disabled"}>✕</button>
          </div>
          <div class="muted sim-src">quark_maptool modesim · ${esc(st.maptool)}</div>
        </div><div id="smOut"></div>`;
        R.querySelectorAll(".sim-form input, .sim-form select:not(#smScen)").forEach((e) => (e.onchange = () => this._readForm()));
        R.querySelector("#smRun").onclick = () => this.run();
        R.querySelector("#smStop").onclick = () => this.stop();
        const b = R.querySelector("#smBatch");
        if (b) b.onclick = () => this.batch();
        R.querySelector("#smScen").onchange = (e) => this._loadScenario(e.target.value);
        R.querySelector("#smScenSave").onclick = () => this._saveScenario();
        R.querySelector("#smScenDel").onclick = () => this._deleteScenario();
        this._drawRuns();
    }
    _loadScenario(name) {
        this.scenario = name;
        const s = this.scenarios.find((x) => x.name === name);
        if (s && s.scenario.form) this.form = { ...FORM_DEFAULT, ...s.scenario.form };
        this._persist();
        this.render();
    }
    async _saveScenario() {
        this._readForm();
        const name = await this.app.app.prompt("Save scenario", this.scenario || this.app.base + "-" + (this._value("map") || "map"),
            `This setup (map, bots, brains, seed, time, rate...) as a scenario in ${this.scenariosDir}.`, (n) => (NAME_RE.test(n) ? null : "Letters, digits, _ . - (up to 64)"));
        if (!name) return;
        const sc = { form: this.form, base: this.app.base, saved: new Date().toISOString().slice(0, 19) };
        try { await api.saveScenario(name, sc); } catch (e) { this.app.app.toast("Couldn't save it: " + e.message, 6000); return; }
        this.scenarios = this.scenarios.filter((s) => s.name !== name).concat([{ name, scenario: sc }]).sort((a, b) => a.name.localeCompare(b.name));
        this.scenario = name;
        this._persist();
        this.render();
    }
    async _deleteScenario() {
        const name = this.scenario;
        if (!name || !confirm(`Delete the scenario ${name}?`)) return;
        try { await api.deleteScenario(name); } catch (e) { this.app.app.toast("Couldn't delete it: " + e.message, 6000); return; }
        this.scenarios = this.scenarios.filter((s) => s.name !== name);
        this.scenario = "";
        this._persist();
        this.render();
    }

    // rules: the .qmode text, or null for the built-in mode itself (sent as `mode`, never as rules: a built-in tourney
    // as rules would play the custom mode's base, a free for all)
    _req(rules) {
        const f = this.form, n = this._value("bots");
        const req = { map: this._value("map"), skill: f.skill, seed: f.seed, seconds: f.secs, tickRate: f.hz, rate: 4,
            realtime: f.real, speed: 1, until: "matchEnd", trace: f.trace };
        const a = f.brainA || "default", b = f.brainB || a;
        req.bots = a === b ? n : Array.from({ length: n }, (_, i) => ({ brain: i % 2 ? b : a }));
        if (a === b && a !== "default") req.brain = a;
        if (rules && rules.trim()) req.rules = rules; else req.mode = this.app.base;
        return req;
    }
    _setBusy(on) {
        this.busy = on;
        const q = (id) => this.root.querySelector("#" + id);
        if (q("smRun")) q("smRun").disabled = on;
        if (q("smBatch")) q("smBatch").disabled = on;
        if (q("smStop")) q("smStop").disabled = !on;
    }
    _allDone() { if ([...this.runs, ...this.batches].every((q) => q.done)) { this._setBusy(false); this._persist(); } }
    _rules() {
        const text = this.app.saveText();
        if (text == null) { this.app.app.toast("The code has an error: fix it first"); return undefined; }
        return this.app.isPristineBuiltin() ? null : text;
    }
    // the compare run: the built-in base mode with this file's keys (none for a base the game won't take as custom)
    _builtinRules() { return this.app.baseTakesCustom() ? this.app.builtinText() : null; }
    async run() {
        this._readForm();
        const rules = this._rules();
        if (rules === undefined) return;
        this.stop(true);
        this.batches = [];
        this.runs = [{ label: "this file", kind: "run", lines: [], model: null }];
        if (this.form.compare) this.runs.push({ label: "built-in " + this.app.base, kind: "run", lines: [], model: null });
        this._setBusy(true);
        this._drawRuns();
        await Promise.all(this.runs.map((r, i) => this._start(r, ["modesim", "--stream"], this._req(i === 0 ? rules : this._builtinRules()), () => this._soon())));
        this._persist();
    }
    // one helper run streamed into r.lines (SSE from serve.py); onLine after each batch of lines and at the end
    async _start(r, args, request, onLine) {
        try {
            const { id } = await api.post("/api/run", { args, request }, "json");
            r.id = id;
            this._attach(r, onLine, args[0]);
        } catch (e) { r.lines.push({ type: "error", error: e.message }); r.done = true; onLine(); this._allDone(); }
    }
    _attach(r, onLine, cmd) {
        const es = new EventSource(`/api/run/${r.id}/events`);
        r.es = es;
        es.onmessage = (ev) => {
            for (const l of ev.data.split("\n")) { if (!l) continue; try { r.lines.push(JSON.parse(l)); } catch { r.lines.push({ type: "error", error: "not JSON: " + l.slice(0, 200) }); } }
            onLine();
        };
        es.addEventListener("exit", (ev) => {
            es.close(); r.done = true;
            try { const x = JSON.parse(ev.data); r.code = x.code; if (x.code && !r.lines.some((l) => l.type === "error" || l.error)) r.lines.push({ type: "error", error: `${cmd || "the helper"} exited with ${x.code}: ${x.stderr || ""}` }); } catch {}
            onLine();
            this._allDone();
        });
        es.onerror = () => { if (!r.done) { es.close(); r.done = true; onLine(); this._allDone(); } };
    }
    stop(quiet) {
        let any = false;
        for (const r of [...this.runs, ...this.batches]) {
            if (r.es) r.es.close();
            if (r.id && !r.done) { api.post(`/api/run/${r.id}/stop`, {}).catch(() => {}); r.stopped = true; any = true; }
            r.done = true;
        }
        this._setBusy(false);
        if (quiet) return;
        this._persist();
        if (any) { for (const r of this.runs) r.model = timelineModel(r.lines); this._drawRuns(); }
    }
    _soon() { if (this._raf) return; this._raf = requestAnimationFrame(() => { this._raf = 0; for (const r of this.runs) r.model = timelineModel(r.lines); this._drawRuns(); }); }

    _drawRuns() {
        const O = this.root.querySelector("#smOut");
        if (!O) return;
        if (this.batches.length) return this._drawBatches();
        if (!this.runs.length) { O.innerHTML = `<div class="muted" style="margin-top:8px">No run yet. A run uses the current text (saved or not) as the request's rules.</div>`; return; }
        for (const r of this.runs) if (!r.model) r.model = timelineModel(r.lines);
        const axes = sharedAxes(this.runs.map((r) => r.model));
        O.innerHTML = `<div class="sim-runs${this.runs.length > 1 ? " two" : ""}">${this.runs.map((r, i) => `<div class="sim-run" data-i="${i}"><h4>${esc(r.label)}${r.stopped ? ' <span class="muted">(stopped)</span>' : r.done ? "" : ' <span class="muted">running…</span>'}</h4><div class="sim-start"></div><canvas class="tl"></canvas><div class="sim-body"><canvas class="plot"></canvas><div class="sim-end"></div></div><div class="sim-log"></div></div>`).join("")}</div>`;
        O.querySelectorAll(".sim-run").forEach((el) => this._drawRun(el, this.runs[+el.dataset.i], axes));
    }
    _drawRun(el, r, axes) {
        const M = r.model || timelineModel(r.lines);
        const s = M.start;
        if (s) {
            const lg = s.logic || {};
            const logicChip = !lg.present ? `<span class="chip">no logic: ${s.custom ? "key rules only" : "the built-in mode"}</span>`
                : s.logicApplied ? `<span class="badge real">LOGIC RUN BY THE SERVER</span>`
                : `<span class="badge mock" title="${esc(lg.why || "")}">LOGIC NOT RUN: base ${esc(s.base)} with the key rules</span>`;
            const would = (lg.interpreted || []).length ? `<span class="chip" title="slots that differ from the built-in ${esc(s.base)}">${s.logicApplied ? "interpreted" : "would interpret"}: ${esc(lg.interpreted.join(", "))}</span>` : "";
            const brains = s.brains ? Object.entries(s.brains).map(([n, l]) => `<span class="chip" title="${esc(l)}">brain ${esc(n)}</span>`).join(" ") : "";
            el.querySelector(".sim-start").innerHTML = `<span class="chip">${esc(s.label || "")}</span> <span class="chip">base ${esc(s.base || "")}</span> <span class="chip">${esc(s.map || "")}</span> ${logicChip} ${would} ${brains}
                ${(s.warnings || []).concat(M.warnings).map((w) => `<div class="warn" style="font-size:11px">⚠ ${esc(typeof w === "string" ? w : w.msg)}</div>`).join("")}
                ${!s.logicApplied && lg.present && lg.why ? `<div class="muted" style="font-size:11px">${esc(lg.why)}</div>` : ""}`;
        } else el.querySelector(".sim-start").innerHTML = "";
        const fields = endCardFields(s, M.end);
        const teams = fields.teams;
        const lineColor = new Map();   // (free for all and the like: one line per player, its colour in the table too)
        { let i = 0; for (const P of M.players.values()) lineColor.set(P.id, `hsl(${(i++ * 67) % 360} 70% 60%)`); }
        this._timeline(el.querySelector("canvas.tl"), M, axes, teams, lineColor, el.clientWidth - 18);
        // the body: the map plot beside the end card (stacked when the card is narrow)
        const W = el.clientWidth - 18, side = W >= 720;
        el.querySelector(".sim-body").classList.toggle("stack", !side);
        this._plot(el.querySelector("canvas.plot"), r, M, teams, side ? Math.max(260, Math.min(640, Math.round(W * 0.42))) : Math.min(560, W));
        // end card
        const e = M.end;
        const nm = (id) => M.names.get(id) || "#" + id;
        let card = "";
        if (e) {
            const w = e.winner;
            const wText = !w ? (e.leader ? `no winner yet: ${e.leader.team ? "TEAM " + e.leader.team.toUpperCase() : nm(e.leader.player)} ahead` : "no winner") : typeof w === "string" ? w : w.text || w.name || "winner";
            const pf = e.perf || {};
            const tn = pf.tickNs || {};
            const lns = pf.logicNs;
            const lg = (s && s.logic) || {};
            const logicLine = lns ? `<b>logic per tick</b> avg ${esc(us(lns.avg))} · p99 ${esc(us(lns.p99))} · max ${esc(us(lns.max))} <span class="muted">(${tn.avg ? esc((100 * lns.avg / tn.avg).toFixed(1)) + "% of the server tick" : ""})</span>`
                : !lg.present ? "no logic: every slot ran native" : s && s.logicApplied ? "logic per tick: not measured" : "logic per tick: the server didn't run the logic";
            const rows = ((e.result && e.result.players) || []).slice().sort((a, b) => (b.score - a.score) || (b.kills - a.kills));
            const len = e.t ?? M.t1;
            const slots = pf.slots || [];
            card = `<div class="sim-card"><b>${esc(wText)}</b> <span class="muted">(${esc(e.reason || "")})</span>
                <div>${fields.rounds ? `rounds ${esc(e.rounds ?? "-")} · ` : ""}${teams && e.result ? `score ${esc(e.result.scoreA + " - " + e.result.scoreB)} · ` : ""}length ${esc(typeof len === "number" ? len.toFixed(1) : len)} s</div>
                ${pf.speedup ? `<div class="muted">${esc(Math.round(pf.speedup))}x real time · ${esc(pf.ticks)} ticks · server tick avg ${esc(us(tn.avg))} / p99 ${esc(us(tn.p99))} / max ${esc(us(tn.max))}</div>` : ""}
                <div>${logicLine}</div>
                ${rows.length ? `<table class="sim-tab"><tr><th></th><th>score</th><th>K</th><th>D</th>${fields.objTime ? "<th>obj s</th>" : ""}</tr>${rows.map((p) => `<tr><td style="color:${teams ? TEAM_COLOR[teamOf(p.team)] || "inherit" : lineColor.get(p.id) || "inherit"}">${p.won ? "★ " : ""}${esc(p.name)}</td><td>${esc(p.score)}</td><td>${esc(p.kills)}</td><td>${esc(p.deaths)}</td>${fields.objTime ? `<td>${esc(p.objTime)}</td>` : ""}</tr>`).join("")}</table>` : ""}
                ${slots.length ? `<table class="sim-tab"><tr><th>slot</th><th>calls</th><th>total</th><th>avg</th><th>max</th></tr>${slots.map((x) => `<tr><td style="color:${this._slotColor(M, x.slot)}">${esc(x.slot)}</td><td>${esc(x.calls)}</td><td>${esc(us(x.ns))}</td><td>${esc(us(x.avg))}</td><td>${esc(us(x.max))}</td></tr>`).join("")}</table>` : ""}</div>`;
        }
        el.querySelector(".sim-end").innerHTML = (M.errors.length ? M.errors.map((x) => `<div class="bad">${esc(x)}</div>`).join("") : "")
            + (e ? card : `<div class="muted">${r.stopped ? "Stopped before the end." : r.done ? "The run ended without an end line." : "…"}</div>`);
        el.querySelector(".sim-log").innerHTML = M.markers.slice(-400).map((x) => `<div><span class="muted">${x.t.toFixed(1)}</span> <b style="color:${MARK[x.kind] || "#cbd5e1"}">${esc(x.kind)}</b> ${esc(x.text)}</div>`).join("");
    }
    _slotColor(M, name) { const i = M.slotNames.indexOf(name); return SLOT_COLORS[(i < 0 ? 0 : i) % SLOT_COLORS.length]; }

    // the timeline: phase bands, scores (team lines or one per player), round results on top, markers, and under it the
    // interpreted slots' cost per frame (trace), on the axes the compared runs share
    _timeline(cv, M, axes, teams, lineColor, width) {
        const strip = M.slotFrames.length || M.slots.length ? 34 : 0;
        const W = cv.width = Math.max(300, width), H = cv.height = 190 + strip;
        const g = cv.getContext("2d");
        g.fillStyle = "#0b1020"; g.fillRect(0, 0, W, H);
        const top = 18, bottom = 190 - 40;   // (the score area; the time labels under it, then the slot strip)
        const span = Math.max(1, axes.t1 - axes.t0), X = (t) => 30 + (W - 40) * (t - axes.t0) / span, Y = (v) => bottom - (bottom - top - 6) * v / axes.maxScore;
        for (const b of M.bands) { g.fillStyle = PHASE_COLORS[b.phase] || "#222"; g.globalAlpha = 0.55; g.fillRect(X(b.t0), top, Math.max(1, X(b.t1) - X(b.t0)), bottom - top); g.globalAlpha = 1; }
        g.strokeStyle = "#334155"; g.beginPath(); g.moveTo(30, bottom); g.lineTo(W - 10, bottom); g.stroke();
        g.fillStyle = "#94a3b8"; g.font = "10px monospace"; g.fillText(String(axes.maxScore), 2, 26); g.fillText("0", 2, bottom);
        for (let k = 0; k <= 4; k++) { const t = axes.t0 + span * k / 4; g.fillText(t.toFixed(0) + "s", X(t) - 8, bottom + 12); }
        const line = (pts, color) => { if (pts.length < 1) return; g.strokeStyle = color; g.lineWidth = 2; g.beginPath(); pts.forEach(([t, v], i) => (i ? g.lineTo(X(t), Y(v)) : g.moveTo(X(t), Y(v)))); g.stroke(); g.lineWidth = 1; };
        const fa = M.frames.filter((f) => f.scoreA !== null), fb = M.frames.filter((f) => f.scoreB !== null);
        if (teams && (fa.length || fb.length)) { line(fa.map((f) => [f.t, f.scoreA]), TEAM_COLOR.a); line(fb.map((f) => [f.t, f.scoreB]), TEAM_COLOR.b); }
        else for (const P of M.players.values()) line(P.scores, lineColor.get(P.id));
        // round results: a coloured tick on the top strip for the round's winner
        for (const rd of M.rounds) { g.fillStyle = TEAM_COLOR[rd.winner] || "#9ca3af"; g.fillRect(X(rd.t) - 2, 2, 4, 12); }
        for (const mk of M.markers) { if (mk.kind === "roundEnd") continue; g.fillStyle = MARK[mk.kind] || "#cbd5e1"; g.fillRect(X(mk.t) - 1, top, 2, 7); }
        if (M.rounds.length) { g.fillStyle = "#94a3b8"; g.fillText("rounds", W - 50, 12); }
        if (!strip) return;
        // the slot strip: each frame's interpreted calls stacked by slot (height: ns, against the costliest frame)
        const y0 = H - 4, hMax = strip - 10;
        g.fillStyle = "#94a3b8"; g.fillText("slots", 2, H - 12);
        const maxNs = Math.max(1, M.maxSlotNs, ...M.slots.map((x) => x.ns || 0));
        for (const fr of M.slotFrames) {
            let y = y0;
            for (const sl of fr.slots) { const h = Math.max(1, hMax * (sl.ns || 0) / maxNs); g.fillStyle = this._slotColor(M, sl.slot); g.fillRect(X(fr.t) - 1, y - h, 2, h); y -= h; }
        }
        for (const sl of M.slots) { const h = 2 + hMax * (sl.ns || 0) / maxNs; g.fillStyle = this._slotColor(M, sl.slot); g.fillRect(X(sl.t), y0 - h, 1, h); }
        g.fillStyle = "#64748b"; g.fillText(`max ${us(maxNs)} a frame`, W - 120, H - strip + 8);
    }

    // the top-down plot: the map (start.mapView: boxes shaded by height, objectives, spawns), the trails of where anyone
    // went, and everyone where they are now. The map layer is drawn once per run and size.
    _plot(pc, r, M, teams, width) {
        const mv = M.start && M.start.mapView;
        const pos = M.frames.flatMap((f) => f.players.map((p) => p.pos)).filter(Array.isArray);
        let x0 = Infinity, x1 = -Infinity, z0 = Infinity, z1 = -Infinity;
        if (mv && mv.bounds) [x0, z0, x1, z1] = mv.bounds;
        for (const p of pos) { const z = p.length > 2 ? p[2] : p[1]; x0 = Math.min(x0, p[0]); x1 = Math.max(x1, p[0]); z0 = Math.min(z0, z); z1 = Math.max(z1, z); }
        const dx = Math.max(1, x1 - x0), dz = Math.max(1, z1 - z0);
        const W = pc.width = Math.max(200, width), H = pc.height = Math.max(180, Math.min(640, Math.round((W - 20) * dz / dx) + 30));
        const pg = pc.getContext("2d");
        if (!isFinite(x0)) { pg.fillStyle = "#0b1020"; pg.fillRect(0, 0, W, H); pg.fillStyle = "#64748b"; pg.font = "11px sans-serif"; pg.fillText("no positions in the frames", 10, 20); return; }
        const sc = Math.min((W - 20) / dx, (H - 30) / dz);
        const P = (x, z) => [10 + (x - x0) * sc, 10 + (z - z0) * sc];
        const key = `${W}x${H}:${x0},${z0},${sc}`;
        if (!r._layer || r._layer.key !== key) {   // (the map: hundreds of boxes, drawn once)
            const c = document.createElement("canvas"); c.width = W; c.height = H;
            const g = c.getContext("2d");
            g.fillStyle = "#0b1020"; g.fillRect(0, 0, W, H);
            if (mv) {
                const [hy0, hy1] = mv.heights || [0, 1];
                for (const b of mv.boxes || []) {
                    const k = Math.max(0, Math.min(1, (b[4] - hy0) / Math.max(0.1, hy1 - hy0)));
                    g.fillStyle = `rgb(${Math.round(20 + 40 * k)},${Math.round(28 + 46 * k)},${Math.round(40 + 54 * k)})`;
                    const [a, bz] = P(b[0], b[1]), [c2, d] = P(b[2], b[3]);
                    g.fillRect(a, bz, Math.max(1, c2 - a), Math.max(1, d - bz));
                }
                const zone = (bx, color, label) => { const [a, bz] = P(bx[0], bx[1]), [c2, d] = P(bx[2], bx[3]); g.strokeStyle = color; g.lineWidth = 1.5; g.strokeRect(a, bz, c2 - a, d - bz); if (label) { g.fillStyle = color; g.font = "bold 11px sans-serif"; g.fillText(label, a + 2, bz + 11); } };
                (mv.sites || []).forEach((s, i) => zone(s, MARK.plant, "site " + String.fromCharCode(65 + i)));
                if (mv.hill) zone(mv.hill, MARK.hill, "hill");
                (mv.points || []).forEach((s, i) => zone(s, MARK.capture, String.fromCharCode(65 + i)));
                (mv.checkpoints || []).forEach((s, i, all) => zone(s, MARK.checkpoint, i === all.length - 1 ? "finish" : String(i + 1)));
                if ((mv.cart || []).length > 1) { g.strokeStyle = MARK.cartCheckpoint; g.lineWidth = 2; g.setLineDash([5, 4]); g.beginPath(); mv.cart.forEach((p, i) => { const [a, b] = P(p[0], p[1]); i ? g.lineTo(a, b) : g.moveTo(a, b); }); g.stroke(); g.setLineDash([]); }
                for (const sp of mv.spawns || []) { const [a, b] = P(sp[0], sp[1]); g.fillStyle = sp[2] === 0 ? TEAM_COLOR.a : sp[2] === 1 ? TEAM_COLOR.b : "#9ca3af"; g.globalAlpha = 0.7; g.beginPath(); g.moveTo(a, b - 4); g.lineTo(a + 3.5, b + 3); g.lineTo(a - 3.5, b + 3); g.fill(); g.globalAlpha = 1; }
                g.lineWidth = 1;
            }
            r._layer = { key, canvas: c };
        }
        pg.drawImage(r._layer.canvas, 0, 0);
        pg.fillStyle = "rgba(148,163,184,.35)"; for (const p of pos) { const [x, y] = P(p[0], p.length > 2 ? p[2] : p[1]); pg.fillRect(x, y, 1, 1); }   // (where anyone went)
        const last = M.frames[M.frames.length - 1];
        if (!last) return;
        const ob = last.obj && last.obj.bomb;
        if (ob && Array.isArray(ob.pos)) { const [x, y] = P(ob.pos[0], ob.pos[2]); pg.strokeStyle = MARK.plant; pg.lineWidth = 2; pg.strokeRect(x - 4, y - 4, 8, 8); pg.lineWidth = 1; }
        for (const p of last.players) if (Array.isArray(p.pos)) {
            const [x, y] = P(p.pos[0], p.pos.length > 2 ? p.pos[2] : p.pos[1]);
            const tm = teams ? teamOf(p.team) : null;
            pg.fillStyle = p.alive === false ? "#475569" : tm ? TEAM_COLOR[tm] : "#e5e7eb";
            pg.beginPath(); pg.arc(x, y, p.it ? 6 : 4, 0, 7); pg.fill();
        }
        pg.fillStyle = "#94a3b8"; pg.font = "10px monospace";
        pg.fillText(`t=${last.t.toFixed(1)} ${last.phase}${last.round ? " r" + last.round : ""}${last.clock !== null ? " clock " + Math.round(last.clock) : ""}`, 4, H - 4);
    }

    async batch() {
        this._readForm();
        const rules = this._rules();
        if (rules === undefined) return;
        this.stop(true);
        this.runs = [];
        const req = (r) => ({ ...this._req(r), realtime: false, trace: false, seeds: this.form.n, firstSeed: this.form.seed });
        this.batches = [{ label: "this file", kind: "batch", lines: [], request: req(rules) }];
        if (this.form.compare) this.batches.push({ label: "built-in " + this.app.base, kind: "batch", lines: [], request: req(this._builtinRules()) });
        this._setBusy(true);
        this._drawBatches();
        // one after the other (each already uses every core but one)
        for (const b of this.batches) {
            if (b.done) continue;
            await new Promise((res) => { this._start(b, ["modebatch", "--stream"], b.request, () => { this._soonBatch(); if (b.done) res(); }); this._persist(); });
        }
    }
    _soonBatch() { if (this._rafB) return; this._rafB = requestAnimationFrame(() => { this._rafB = 0; this._drawBatches(); }); }
    _drawBatches() {
        const O = this.root.querySelector("#smOut");
        if (!O) return;
        O.innerHTML = `<div class="sim-runs${this.batches.length > 1 ? " two" : ""}">${this.batches.map((b, i) => this._batchHtml(b, i)).join("")}</div>`;
        O.querySelectorAll("[data-csv]").forEach((btn) => (btn.onclick = () => { const B = batchModel(this.batches[+btn.dataset.csv].lines); this.app.app.download("modebatch.csv", B.end.csv, "text/csv"); }));
    }
    _batchHtml(b, i) {
        const B = batchModel(b.lines);
        const s = B.start, e = B.end;
        const pr = B.progress || { done: 0, total: s ? s.matches : "?" };
        let h = `<div class="sim-run"><h4>${esc(b.label)} <span class="muted">${e ? "" : b.stopped ? "(stopped)" : b.done ? "(ended)" : b.id ? `running… ${pr.done}/${pr.total}` : "waiting…"}</span></h4>`;
        if (s) h += `<div><span class="chip">${esc(s.label)}</span> <span class="chip">base ${esc(s.base)}</span> <span class="chip">${esc(s.map)}</span> <span class="chip">${esc(s.matches)} seeds · ${esc(s.threads)} threads</span></div>`;
        h += B.errors.map((x) => `<div class="bad">${esc(x)}</div>`).join("");
        if (!e) return h + (B.matches.length ? `<div class="muted">${B.matches.slice(-5).map((m) => `seed ${m.seed}: ${esc(m.winner ? m.winner.text : m.reason)} (${m.t} s)`).join("<br>")}</div>` : "") + "</div>";
        if (e.logic && e.logic.present) h += e.logicApplied ? `<div class="badge real">LOGIC RUN BY THE SERVER</div>` : `<div class="badge mock" title="${esc(e.logic.why || "")}">LOGIC NOT RUN: base ${esc(e.base)} with the key rules</div>`;
        const pct = (x) => (100 * (x || 0)).toFixed(0) + "%";
        if (e.teams) {
            const a = e.teams.a, bb = e.teams.b;
            h += `<div class="sim-card" style="margin-top:6px"><b>win rate</b> <span style="color:${TEAM_COLOR.a}">TEAM A ${pct(a.winRate)} (${a.wins})</span> · <span style="color:${TEAM_COLOR.b}">TEAM B ${pct(bb.winRate)} (${bb.wins})</span> · draws ${e.teams.draws}
              <div class="bar" style="display:flex;height:8px;border-radius:4px;overflow:hidden;margin:4px 0"><span style="background:${TEAM_COLOR.a};width:${pct(a.winRate)}"></span><span style="background:#6b7280;width:${pct(e.matchesPlayed ? e.teams.draws / e.matchesPlayed : 0)}"></span><span style="background:${TEAM_COLOR.b};width:${pct(bb.winRate)}"></span></div></div>`;
        } else h += `<div class="sim-card" style="margin-top:6px">draws ${esc(e.draws)}</div>`;
        const L = e.length || {}, ln = e.perf.logicNs;
        const objTime = (e.players || []).some((p) => p.objTime > 0);
        h += `<div class="sim-card" style="margin-top:6px">${e.matchesPlayed} played · ${e.timeouts} timeouts (hit the max s) · ${e.errors} errors<br>length avg ${esc(L.avg)} s (min ${esc(L.min)}, median ${esc(L.p50)}, max ${esc(L.max)})${e.teams || e.rounds > 0 ? ` · rounds ${esc(e.rounds)}` : ""} · score spread ${esc(e.scoreSpread)}
              ${ln ? `<div><b>logic per tick</b> avg ${esc(us(ln.avg))} · worst p99 ${esc(us(ln.p99))} · worst max ${esc(us(ln.max))} <span class="muted">(${esc(ln.matches)} matches)</span></div>` : ""}
              <div class="muted">${esc((e.perf.matchesPerSecond || 0).toFixed(2))} matches/s · ${esc(Math.round(e.perf.speedupPerMatch || 0))}x real time per match · ${esc(e.perf.threads)} threads · ${esc((e.perf.wallSeconds || 0).toFixed(1))} s</div></div>`;
        h += `<table class="sim-tab"><tr><th>bot</th>${e.teams ? "<th>team</th>" : ""}<th>wins</th><th>K/match</th><th>D/match</th><th>score</th>${objTime ? "<th>obj s</th>" : ""}</tr>${(e.players || []).map((p) => `<tr><td>${esc(p.name)}</td>${e.teams ? `<td style="color:${TEAM_COLOR[p.team] || "inherit"}">${esc(p.team.toUpperCase())}</td>` : ""}<td>${pct(p.winRate)}</td><td>${esc(p.kills)}</td><td>${esc(p.deaths)}</td><td>${esc(p.score)}</td>${objTime ? `<td>${esc(p.objTime)}</td>` : ""}</tr>`).join("")}</table>`;
        h += `<button data-csv="${i}" style="margin-top:6px">⇩ CSV (one row per match)</button></div>`;
        return h;
    }
}
