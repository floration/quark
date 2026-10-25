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
const BATCH_COLORS = ["#a78bfa", "#94a3b8"];   // the distributions: this file, the built-in
const NAME_RE = /^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$/;
const DRAW_MS = 250;      // the most often a streaming run redraws
const LOG_LINES = 400;    // the event log keeps the last this many
const us = (ns) => (ns >= 10000 ? (ns / 1000).toFixed(0) + " µs" : ns >= 1000 ? (ns / 1000).toFixed(1) + " µs" : Math.round(ns) + " ns");

// NDJSON lines (objects) -> the timeline. Lenient about field names the helper may add; never invents values.
// A run's model grows as its lines come in: feedTimeline() reads only the lines after the ones it has read (m.fed), so
// a long run costs each line once, not once per redraw.
export function newTimeline() {
    return { start: null, end: null, frames: [], bands: [], markers: [], rounds: [], slots: [], slotFrames: [], slotNames: [], players: new Map(), names: new Map(),
        errors: [], warnings: [], t0: 0, t1: 0, maxScore: 1, maxSlotNs: 0, fed: 0,
        teamA: newSeries(), teamB: newSeries(),   // the team scores as steps (see addPoint)
        trail: [], bounds: null };                // where anyone went: x, z, x, z... and their [x0, z0, x1, z1]
}
// a score over time kept as its changes only (the point before a change and the change), plus where it is now: the
// same line as one point per frame, in a few points
function newSeries() { return { pts: [], t: null, v: null }; }
function addPoint(s, t, v) {
    if (s.v === null) s.pts.push([t, v]);
    else if (v !== s.v) { if (s.pts[s.pts.length - 1][0] !== s.t) s.pts.push([s.t, s.v]); s.pts.push([t, v]); }
    s.t = t; s.v = v;
}
export function seriesPoints(s) { return s.v === null ? [] : s.pts[s.pts.length - 1][0] === s.t ? s.pts : s.pts.concat([[s.t, s.v]]); }
export function feedTimeline(m, lines) {
    const nm = (id) => (id === undefined || id === null || id < 0 ? "world" : m.names.get(id) || "#" + id);
    const slotName = (n) => { if (!m.slotNames.includes(n)) m.slotNames.push(n); };
    for (let i = m.fed; i < lines.length; i++) {
        const L = lines[i];
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
            const band = m.bands[m.bands.length - 1];
            if (!band || band.phase !== f.phase) { if (band) band.t1 = t; m.bands.push({ phase: f.phase, t0: t, t1: t }); }
            else band.t1 = t;
            for (const p of f.players) {
                if (Array.isArray(p.pos)) {
                    const x = p.pos[0], z = p.pos.length > 2 ? p.pos[2] : p.pos[1], b = m.bounds;
                    m.trail.push(x, z);
                    if (!b) m.bounds = [x, z, x, z];
                    else { if (x < b[0]) b[0] = x; if (z < b[1]) b[1] = z; if (x > b[2]) b[2] = x; if (z > b[3]) b[3] = z; }
                }
                if (p.id === undefined) continue;
                let P = m.players.get(p.id);
                if (!P) { P = { id: p.id, name: p.name || nm(p.id), team: teamOf(p.team), score: newSeries() }; m.players.set(p.id, P); }
                if (p.score !== undefined) { addPoint(P.score, t, p.score); if (p.score > m.maxScore) m.maxScore = p.score; }
            }
            if (f.scoreA !== null) { addPoint(m.teamA, t, f.scoreA); m.maxScore = Math.max(m.maxScore, f.scoreA); }
            if (f.scoreB !== null) { addPoint(m.teamB, t, f.scoreB); m.maxScore = Math.max(m.maxScore, f.scoreB); }
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
    m.fed = lines.length;
    if (m.frames.length) { m.t0 = m.frames[0].t; m.t1 = m.frames[m.frames.length - 1].t; }
    return m;
}
export function timelineModel(lines) { return feedTimeline(newTimeline(), lines); }
// the frame at time t (the last one at or before it), by bisection
export function frameAt(m, t) {
    const F = m.frames;
    if (!F.length) return null;
    let lo = 0, hi = F.length - 1;
    while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (F[mid].t <= t) lo = mid; else hi = mid - 1; }
    return F[lo];
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

// a batch's CSV file name: the mode, the map and the seeds (and -builtin for a compare's built-in half)
export function csvName(mode, B, builtin) {
    const s = B.start || {}, seeds = Array.isArray(s.seeds) ? s.seeds : [];
    const part = (x) => String(x || "").replace(/^.*[\\/]/, "").replace(/\.qmap$/, "").replace(/[^A-Za-z0-9_.-]+/g, "_");
    const sd = seeds.length ? `seeds${seeds[0]}${seeds.length > 1 ? "-" + seeds[seeds.length - 1] : ""}` : "";
    return [part(mode), part(s.map), sd].filter(Boolean).join("-") + (builtin ? "-builtin" : "") + ".csv";
}
// one match's score spread (as modebatch's scoreSpread: |A - B| in team modes, else the winner's score minus the
// runner-up's)
export function matchSpread(m, teams) {
    if (teams) return m.scoreA != null && m.scoreB != null ? Math.abs(m.scoreA - m.scoreB) : null;
    const s = (m.players || []).map((p) => p.score).filter((x) => typeof x === "number").sort((a, b) => b - a);
    return s.length ? s[0] - (s[1] ?? 0) : null;
}
// values -> `bins` counts over [lo, hi] (hi itself in the last bin)
export function histogram(values, bins, lo, hi) {
    const out = new Array(bins).fill(0), w = (hi - lo) / bins || 1;
    for (const v of values) if (typeof v === "number" && isFinite(v)) out[Math.max(0, Math.min(bins - 1, Math.floor((v - lo) / w)))]++;
    return out;
}
// two modebatch end lines (this file, the built-in) -> the rows of the difference: [{label, a, b, d, fmt}], d = a - b
export function batchDelta(a, b) {
    const rows = [], row = (label, x, y, fmt) => { if (typeof x === "number" && typeof y === "number") rows.push({ label, a: x, b: y, d: x - y, fmt }); };
    const pa = (e) => e.matchesPlayed || 0;
    if (a.teams && b.teams) {
        row("team A wins", a.teams.a.winRate, b.teams.a.winRate, "pct");
        row("team B wins", a.teams.b.winRate, b.teams.b.winRate, "pct");
        row("draws", pa(a) ? a.teams.draws / pa(a) : null, pa(b) ? b.teams.draws / pa(b) : null, "pct");
    } else row("draws", pa(a) ? a.draws / pa(a) : null, pa(b) ? b.draws / pa(b) : null, "pct");
    row("timeouts", pa(a) ? a.timeouts / pa(a) : null, pa(b) ? b.timeouts / pa(b) : null, "pct");
    row("length avg", a.length && a.length.avg, b.length && b.length.avg, "s");
    row("length median", a.length && a.length.p50, b.length && b.length.p50, "s");
    if (a.rounds > 0 || b.rounds > 0) row("rounds", a.rounds, b.rounds, "n");
    row("score spread", a.scoreSpread, b.scoreSpread, "n");
    const ln = (e) => e.perf && e.perf.logicNs ? e.perf.logicNs.avg : 0;   // (no logic: 0 ns of it)
    if (ln(a) || ln(b)) row("logic per tick", ln(a), ln(b), "ns");
    return rows;
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

// the number fields' limits (the helper refuses or misreads anything else): id -> [min, max, whole numbers only, name]
const LIMITS = { smBots: [1, 32, true, "Bots"], smSeed: [0, 2147483647, true, "Seed"], smSecs: [5, 7200, false, "Max s"], smN: [2, 1000, true, "Batch seeds"] };
// a field's text -> null when it will do, else why not
export function fieldProblem(id, text) {
    const [lo, hi, whole, name] = LIMITS[id], v = Number(text);
    if (String(text).trim() === "" || !Number.isFinite(v)) return `${name}: a number`;
    if (whole && !Number.isInteger(v)) return `${name}: a whole number`;
    if (v < lo || v > hi) return `${name}: ${lo} to ${hi}`;
    return null;
}
// perBase: {<base mode>: {map, bots}}, the map and bot count picked for each base mode (none: the mode's own map, 2 or
// 8 bots), so a map chosen for one mode never carries over to another
const FORM_DEFAULT = { perBase: {}, skill: 2, seed: 1, secs: 1800, hz: 125, real: false, trace: true, compare: false, brainA: "default", brainB: "", n: 20 };

export class SimView {
    constructor(root, app) {
        this.root = root; this.app = app;   // app: the ModePage
        this.status = null;                 // /api/modesim/status
        this.form = { ...FORM_DEFAULT, perBase: {} };
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
    _mine() { return this.form.perBase[this.app.base] || { map: "", bots: null }; }
    _ownMap() { const maps = this._maps(); return (maps.find((m) => m.for === this.app.base) || maps[0] || { name: "" }).name; }
    _value(k) {
        const f = this.form, mine = this._mine();
        if (k === "map") { const maps = this._maps(); return mine.map && (maps.some((m) => m.name === mine.map) || !maps.length) ? mine.map : this._ownMap(); }
        if (k === "bots") return mine.bots ?? (this._twoPlayer() ? 2 : 8);
        return f[k];
    }
    // this base mode's map and bots: kept only when they differ from the mode's own
    _setMine(map, bots) {
        const perBase = { ...this.form.perBase }, mine = { map: map === this._ownMap() ? "" : map, bots: bots === (this._twoPlayer() ? 2 : 8) ? null : bots };
        if (mine.map || mine.bots !== null) perBase[this.app.base] = mine; else delete perBase[this.app.base];
        this.form.perBase = perBase;
    }
    // the form into this.form; a number out of its range is marked (red, the reason beside the buttons) and not taken.
    // -> the first problem, or null
    _readForm() {
        const R = this.root, v = (id) => R.querySelector("#" + id);
        if (!v("smRun") || this._formBase !== this.app.base) return null;   // (the form on screen is another mode's: a late change event)
        const bad = [], num = (id, old) => {
            const el = v(id);
            if (!el) return old;
            const p = fieldProblem(id, el.value);
            el.classList.toggle("invalid", !!p); el.title = p || "";
            if (p) { bad.push(p); return old; }
            return +el.value;
        };
        const f = this.form;
        this._setMine(v("smMap").value, num("smBots", this._value("bots")));
        this.form = { perBase: this.form.perBase,
            skill: +v("smSkill").value, seed: num("smSeed", f.seed), secs: num("smSecs", f.secs), hz: +v("smHz").value, real: v("smReal").checked,
            trace: v("smTrace").checked, compare: v("smCompare").checked, brainA: v("smBrainA").value, brainB: v("smBrainB").value, n: num("smN", f.n) };
        const e = v("smErr");
        if (e) e.textContent = bad.join(" · ");
        this._persistSoon();
        return bad[0] || null;
    }
    _persistSoon() { clearTimeout(this._pt); this._pt = setTimeout(() => this._persist(), 400); }
    // the runs started (by id) and the batches still waiting their turn (by request: a compare batch's built-in half
    // starts after the first, so a reload in between must still start it)
    _persist() {
        const ids = [...this.runs, ...this.batches].filter((r) => r.id || (r.kind === "batch" && !r.done))
            .map((r) => (r.id ? { id: r.id, label: r.label, kind: r.kind } : { label: r.label, kind: r.kind, request: r.request }));
        api.saveState("sim", { form: this.form, scenario: this.scenario, runs: ids }).catch(() => {});
    }
    async _restore() {
        let st = null;
        try { st = await api.state("sim"); } catch {}
        if (!st) return;
        if (st.form) { const { map, bots, ...rest } = st.form; this.form = { ...FORM_DEFAULT, ...rest, perBase: rest.perBase || {} }; }   // (map / bots: an older page's, for every mode)
        this.scenario = st.scenario || "";
        // the runs the page had going: serve.py keeps their lines, so they replay from the start and go on streaming
        for (const x of st.runs || []) {
            if (!x.id) {   // (a batch that hadn't started: it runs after the ones before it)
                if (x.kind === "batch" && x.request) this.batches.push(this._entry({ label: x.label, kind: "batch", request: x.request }));
                continue;
            }
            let info;
            try { info = await api.run(x.id); } catch { continue; }
            const r = this._entry({ label: x.label, kind: x.kind, id: x.id });
            (x.kind === "batch" ? this.batches : this.runs).push(r);
            this._attach(r, x.kind === "batch" ? () => this._soonBatch() : () => this._soon(), info.args && info.args[0]);
        }
        this.busy = [...this.runs, ...this.batches].some((r) => !r.done);
        if (this.batches.some((b) => !b.done)) this._runBatches();
    }
    // a run or batch: its lines, and `finished`, which resolves once it is done (ended, failed or stopped)
    _entry(o) {
        const r = { lines: [], model: null, done: false, ...o };
        r.finished = new Promise((res) => (r._fin = res));
        return r;
    }
    _finish(r) { r.done = true; r._fin(); }

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
        // (a user brain named default goes as "@default": "default" is the helper's built-in one, and serve.py sends the file)
        const bv = (b) => (b === "default" ? "@default" : b), mine = this.brains.map(bv);
        const brainOpts = (sel, extra) => (extra ? opt("", extra, !sel) : "") + opt("default", "built-in", sel === "default") + this.brains.map((b) => opt(bv(b), b === "default" ? "default (yours)" : b, sel === bv(b))).join("")
            + (sel && sel !== "default" && !mine.includes(sel) ? opt(sel, (sel === "@default" ? "default (yours)" : sel) + " (missing)", true) : "");
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
            ${this.app.differsFromBuiltin() ? `<label title="also run the built-in ${esc(base)} with the same seed, on the same time axis"><input id="smCompare" type="checkbox" ${f.compare ? "checked" : ""}> compare with built-in</label>`
                : `<label class="muted" title="this mode's logic is the built-in ${esc(base)}'s and the keys go to both: the built-in run would be the same match"><input id="smCompare" type="checkbox" disabled ${f.compare ? "checked" : ""}> compare with built-in (nothing differs)</label>`}
            <button id="smRun" class="primary" ${this.busy ? "disabled" : ""}>▶ Run</button><button id="smStop" ${this.busy ? "" : "disabled"}>■ Stop</button>
            ${st.batch ? `<label>Batch seeds <input id="smN" type="number" value="${esc(f.n)}" min="2" max="1000" style="width:60px"></label><button id="smBatch" ${this.busy ? "disabled" : ""}>Run batch</button>` : '<span class="muted" title="quark_maptool has no modebatch">(no modebatch)</span>'}
            <span id="smErr" class="bad"></span>
            <span class="sp"></span>
            <label title="saved setups (${esc(this.scenariosDir || "<data>/mode_editor/scenarios")})">Scenario <select id="smScen">${opt("", "—", !this.scenario)}${this.scenarios.map((s) => opt(s.name, s.name, s.name === this.scenario)).join("")}</select></label>
            <button id="smScenSave" title="save this setup as a scenario">Save…</button><button id="smScenDel" title="delete the scenario" ${this.scenario ? "" : "disabled"}>✕</button>
          </div>
          <div class="muted sim-src">quark_maptool modesim · ${esc(st.maptool)}</div>
        </div><div id="smOut"></div>`;
        this._formBase = base;   // (set after the old form is gone: a focused field sends its change while innerHTML removes it)
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
        // (a scenario's map and bots are for this mode now)
        if (s && s.scenario.form) { const { map, bots, perBase, ...rest } = s.scenario.form; this.form = { ...FORM_DEFAULT, ...rest, perBase: this.form.perBase }; this._setMine(map || this._ownMap(), bots ?? (this._twoPlayer() ? 2 : 8)); }
        this._persist();
        this.render();
    }
    async _saveScenario() {
        this._readForm();
        const name = await this.app.app.prompt("Save scenario", this.scenario || this.app.base + "-" + (this._value("map") || "map"),
            `This setup (map, bots, brains, seed, time, rate...) as a scenario in ${this.scenariosDir}.`, (n) => (NAME_RE.test(n) ? null : "Letters, digits, _ . - (up to 64)"));
        if (!name) return;
        const { perBase, ...rest } = this.form;
        const sc = { form: { ...rest, map: this._value("map"), bots: this._value("bots") }, base: this.app.base, saved: new Date().toISOString().slice(0, 19) };
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
            realtime: f.real, speed: 1, until: "matchEnd", trace: f.trace && !!rules && /^logic(?:$|[ \t\r])/m.test(rules) };   // (no logic: nothing to trace)
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
        const bad = this._readForm();
        if (bad) { this.app.app.toast("Not run: " + bad, 5000); return; }
        const rules = this._rules();
        if (rules === undefined) return;
        this.stop(true);
        this.batches = [];
        this.runs = [this._entry({ label: "this file", kind: "run" })];
        if (this.form.compare && this.app.differsFromBuiltin()) this.runs.push(this._entry({ label: "built-in " + this.app.base, kind: "run" }));
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
        } catch (e) { r.lines.push({ type: "error", error: e.message }); this._finish(r); onLine(); this._allDone(); }
    }
    _attach(r, onLine, cmd) {
        const es = new EventSource(`/api/run/${r.id}/events`);
        r.es = es;
        es.onmessage = (ev) => {
            for (const l of ev.data.split("\n")) { if (!l) continue; try { r.lines.push(JSON.parse(l)); } catch { r.lines.push({ type: "error", error: "not JSON: " + l.slice(0, 200) }); } }
            onLine();
        };
        es.addEventListener("exit", (ev) => {
            es.close(); this._finish(r);
            try { const x = JSON.parse(ev.data); r.code = x.code; if (x.code && !r.lines.some((l) => l.type === "error" || l.error)) r.lines.push({ type: "error", error: `${cmd || "the helper"} exited with ${x.code}: ${x.stderr || ""}` }); } catch {}
            onLine();
            this._allDone();
        });
        es.onerror = () => { if (!r.done) { es.close(); this._finish(r); onLine(); this._allDone(); } };
    }
    stop(quiet) {
        let any = false;
        for (const r of [...this.runs, ...this.batches]) {
            if (r.es) r.es.close();
            if (r.id && !r.done) { api.post(`/api/run/${r.id}/stop`, {}).catch(() => {}); r.stopped = true; any = true; }
            if (!r.done) this._finish(r);
        }
        this._setBusy(false);
        if (quiet) return;
        this._persist();
        if (any) this._drawRuns();
    }
    // Redraws while lines stream in: at most every DRAW_MS (a 16-bot two-hour run sends ~20000 lines; drawing on every
    // animation frame froze the page), right away once every run is done.
    _soon() { this._throttle("_drawT", () => this._updateRuns(), this.runs); }
    _throttle(slot, draw, list) {
        if (this[slot]) return;
        const now = performance.now(), wait = list.every((r) => r.done) ? 0 : Math.max(0, (this[slot + "At"] || 0) + DRAW_MS - now);
        this[slot] = setTimeout(() => requestAnimationFrame(() => { this[slot] = 0; this[slot + "At"] = performance.now(); draw(); }), wait);
    }

    // The run cards are built once per set of runs (and page render); a redraw only feeds the new lines into each model
    // and updates the parts that changed: the timeline, the plot (its map and trail layers are kept and only the new
    // trail points drawn), the end card when it changes, the log by appending.
    _drawRuns() {
        const O = this.root.querySelector("#smOut");
        if (!O) return;
        if (this.batches.length) return this._drawBatches();
        if (!this.runs.length) { O.innerHTML = `<div class="muted" style="margin-top:8px">No run yet. A run uses the current text (saved or not) as the request's rules.</div>`; return; }
        if (!this._built || this._built.O !== O || this._built.runs !== this.runs) {
            O.innerHTML = `<div class="sim-runs${this.runs.length > 1 ? " two" : ""}">${this.runs.map((r, i) => `<div class="sim-run" data-i="${i}"><h4>${esc(r.label)} <span class="muted st"></span></h4><div class="sim-start"></div><canvas class="tl"></canvas><div class="sim-body"><canvas class="plot"></canvas><div class="sim-end"></div></div><div class="sim-log"></div></div>`).join("")}</div>`;
            O.querySelectorAll(".sim-run").forEach((el) => {
                const r = this.runs[+el.dataset.i];
                const q = (s) => el.querySelector(s);
                r.view = { el, st: q(".st"), start: q(".sim-start"), tl: q("canvas.tl"), plot: q("canvas.plot"), body: q(".sim-body"), end: q(".sim-end"), log: q(".sim-log"), startKey: null, endKey: null, logN: 0 };
            });
            this._built = { O, runs: this.runs };
        }
        this._updateRuns();
    }
    _updateRuns() {
        if (!this._built || !this._built.O.isConnected || this._built.runs !== this.runs) return this._drawRuns();
        for (const r of this.runs) r.model = feedTimeline(r.model || newTimeline(), r.lines);
        const axes = sharedAxes(this.runs.map((r) => r.model));
        for (const r of this.runs) this._drawRun(r, axes);
    }
    _drawRun(r, axes) {
        const V = r.view, M = r.model, s = M.start;
        V.st.textContent = r.stopped ? "(stopped)" : r.done ? "" : "running…";
        const startKey = `${!!s}|${M.warnings.length}`;
        if (startKey !== V.startKey) {
            V.startKey = startKey;
            if (s) {
                const lg = s.logic || {};
                const logicChip = !lg.present ? `<span class="chip">no logic: ${s.custom ? "key rules only" : "the built-in mode"}</span>`
                    : s.logicApplied ? `<span class="badge real">LOGIC RUN BY THE SERVER</span>`
                    : `<span class="badge mock" title="${esc(lg.why || "")}">LOGIC NOT RUN: base ${esc(s.base)} with the key rules</span>`;
                const would = (lg.interpreted || []).length ? `<span class="chip" title="slots that differ from the built-in ${esc(s.base)}">${s.logicApplied ? "interpreted" : "would interpret"}: ${esc(lg.interpreted.join(", "))}</span>` : "";
                const brains = s.brains ? Object.entries(s.brains).map(([n, l]) => `<span class="chip" title="${esc(l)}">brain ${esc(n)}</span>`).join(" ") : "";
                V.start.innerHTML = `<span class="chip">${esc(s.label || "")}</span> <span class="chip">base ${esc(s.base || "")}</span> <span class="chip">${esc(s.map || "")}</span> ${logicChip} ${would} ${brains}
                    ${(s.warnings || []).concat(M.warnings).map((w) => `<div class="warn" style="font-size:11px">⚠ ${esc(typeof w === "string" ? w : w.msg)}</div>`).join("")}
                    ${!s.logicApplied && lg.present && lg.why ? `<div class="muted" style="font-size:11px">${esc(lg.why)}</div>` : ""}`;
            } else V.start.innerHTML = "";
        }
        const fields = endCardFields(s, M.end);
        const teams = fields.teams;
        if (!r.colors || r.colors.size !== M.players.size) {   // (free for all and the like: one line per player, its colour in the table too)
            r.colors = new Map(); let i = 0;
            for (const P of M.players.values()) r.colors.set(P.id, `hsl(${(i++ * 67) % 360} 70% 60%)`);
        }
        const W = V.el.clientWidth - 18;
        this._timeline(V.tl, M, axes, teams, r.colors, W);
        // the body: the map plot beside the end card (stacked when the card is narrow)
        const side = W >= 720;
        V.body.classList.toggle("stack", !side);
        this._plot(V.plot, r, M, teams, side ? Math.max(260, Math.min(640, Math.round(W * 0.42))) : Math.min(560, W));
        const endKey = `${!!M.end}|${r.done}|${r.stopped}|${M.errors.length}|${r.colors.size}`;
        if (endKey !== V.endKey) { V.endKey = endKey; V.end.innerHTML = this._endCard(r, M, s, fields); }
        // the log: the new markers appended, the last LOG_LINES kept
        if (M.markers.length > V.logN) {
            const add = M.markers.slice(Math.max(V.logN, M.markers.length - LOG_LINES));
            V.log.insertAdjacentHTML("beforeend", add.map((x) => `<div><span class="muted">${x.t.toFixed(1)}</span> <b style="color:${MARK[x.kind] || "#cbd5e1"}">${esc(x.kind)}</b> ${esc(x.text)}</div>`).join(""));
            for (let n = V.log.childElementCount - LOG_LINES; n > 0; n--) V.log.firstElementChild.remove();
            V.logN = M.markers.length;
        }
    }
    _endCard(r, M, s, fields) {
        const teams = fields.teams, e = M.end;
        const nm = (id) => M.names.get(id) || "#" + id;
        let card = "";
        if (e) {
            const w = e.winner;
            const wText = !w ? (e.leader ? `no winner yet: ${e.leader.team ? "TEAM " + e.leader.team.toUpperCase() : nm(e.leader.player)} ahead` : "no winner") : typeof w === "string" ? w : w.text || w.name || "winner";
            const pf = e.perf || {};
            const tn = pf.tickNs || {};
            const maxAt = (x) => (x.maxAt != null && x.max > 0 ? ` <span class="muted" title="tick ${esc(x.maxTick)} of the match">at ${esc(x.maxAt.toFixed(1))} s</span>` : "");
            const lns = pf.logicNs;
            const lg = (s && s.logic) || {};
            const logicLine = lns ? `<b>logic per tick</b> avg ${esc(us(lns.avg))} · p99 ${esc(us(lns.p99))}${lns.p999 != null ? ` · p99.9 ${esc(us(lns.p999))}` : ""} · max ${esc(us(lns.max))}${maxAt(lns)} <span class="muted">(${tn.avg ? esc((100 * lns.avg / tn.avg).toFixed(1)) + "% of the server tick" : ""})</span>`
                : !lg.present ? "no logic: every slot ran native" : s && s.logicApplied ? "logic per tick: not measured" : "logic per tick: the server didn't run the logic";
            // a slot that runs on events (kill...) is idle most ticks, so per tick it reads as free: per call says what it costs
            const cn = pf.callNs;
            const callLine = cn && cn.calls ? `<div><b>per call</b> <span class="muted">(${esc(cn.calls)} interpreted slot call${cn.calls === 1 ? "" : "s"})</span> avg ${esc(us(cn.avg))} · p99 ${esc(us(cn.p99))} · max ${esc(us(cn.max))}${cn.sampled ? ` <span class="muted">(the first ${esc(cn.sampled)} calls)</span>` : ""}</div>` : "";
            const rows = ((e.result && e.result.players) || []).slice().sort((a, b) => (b.score - a.score) || (b.kills - a.kills));
            const len = e.t ?? M.t1;
            const slots = pf.slots || [];
            card = `<div class="sim-card"><b>${esc(wText)}</b> <span class="muted">(${esc(e.reason || "")})</span>
                <div>${fields.rounds ? `rounds ${esc(e.rounds ?? "-")} · ` : ""}${teams && e.result ? `score ${esc(e.result.scoreA + " - " + e.result.scoreB)} · ` : ""}length ${esc(typeof len === "number" ? len.toFixed(1) : len)} s</div>
                ${pf.speedup ? `<div class="muted">${esc(Math.round(pf.speedup))}x real time · ${esc(pf.ticks)} ticks · server tick avg ${esc(us(tn.avg))} / p99 ${esc(us(tn.p99))}${tn.p999 != null ? ` / p99.9 ${esc(us(tn.p999))}` : ""} / max ${esc(us(tn.max))}${maxAt(tn)}${tn.maxTick === 0 && tn.maxAfterFirst != null ? ` (the match's first tick: the round's setup; after it max ${esc(us(tn.maxAfterFirst))})` : ""}</div>` : ""}
                <div>${logicLine}</div>${callLine}
                ${rows.length ? `<table class="sim-tab"><tr><th></th><th>score</th><th>K</th><th>D</th>${fields.objTime ? "<th>obj s</th>" : ""}</tr>${rows.map((p) => `<tr><td style="color:${teams ? TEAM_COLOR[teamOf(p.team)] || "inherit" : r.colors.get(p.id) || "inherit"}">${p.won ? "★ " : ""}${esc(p.name)}</td><td>${esc(p.score)}</td><td>${esc(p.kills)}</td><td>${esc(p.deaths)}</td>${fields.objTime ? `<td>${esc(p.objTime)}</td>` : ""}</tr>`).join("")}</table>` : ""}
                ${slots.length ? `<table class="sim-tab"><tr><th>slot</th><th>calls</th><th>total</th><th>avg</th><th>max</th></tr>${slots.map((x) => `<tr><td style="color:${this._slotColor(M, x.slot)}">${esc(x.slot)}</td><td>${esc(x.calls)}</td><td>${esc(us(x.ns))}</td><td>${esc(us(x.avg))}</td><td>${esc(us(x.max))}</td></tr>`).join("")}</table>` : ""}</div>`;
        }
        return (M.errors.length ? M.errors.map((x) => `<div class="bad">${esc(x)}</div>`).join("") : "")
            + (e ? card : `<div class="muted">${r.stopped ? "Stopped before the end." : r.done ? "The run ended without an end line." : "…"}</div>`);
    }
    _slotColor(M, name) { const i = M.slotNames.indexOf(name); return SLOT_COLORS[(i < 0 ? 0 : i) % SLOT_COLORS.length]; }

    // the timeline: phase bands, scores (team lines or one per player), round results on top, markers, and under it the
    // interpreted slots' cost per frame (trace), on the axes the compared runs share. Whatever is drawn per frame or per
    // event is drawn at most once per pixel column, so a redraw costs the canvas's width, not the run's length.
    _timeline(cv, M, axes, teams, lineColor, width) {
        const strip = M.slotFrames.length || M.slots.length ? 34 : 0;
        const W = Math.max(300, width), H = 190 + strip;
        if (cv.width !== W) cv.width = W;
        if (cv.height !== H) cv.height = H;
        const g = cv.getContext("2d");
        g.fillStyle = "#0b1020"; g.fillRect(0, 0, W, H);
        const top = 18, bottom = 190 - 40;   // (the score area; the time labels under it, then the slot strip)
        const span = Math.max(1, axes.t1 - axes.t0), X = (t) => 30 + (W - 40) * (t - axes.t0) / span, Y = (v) => bottom - (bottom - top - 6) * v / axes.maxScore;
        for (const b of M.bands) { g.fillStyle = PHASE_COLORS[b.phase] || "#222"; g.globalAlpha = 0.55; g.fillRect(X(b.t0), top, Math.max(1, X(b.t1) - X(b.t0)), bottom - top); g.globalAlpha = 1; }
        g.strokeStyle = "#334155"; g.beginPath(); g.moveTo(30, bottom); g.lineTo(W - 10, bottom); g.stroke();
        g.fillStyle = "#94a3b8"; g.font = "10px monospace"; g.fillText(String(axes.maxScore), 2, 26); g.fillText("0", 2, bottom);
        for (let k = 0; k <= 4; k++) {   // (the last label ends at the axis' end: it would run off the canvas)
            const t = axes.t0 + span * k / 4, l = t.toFixed(0) + "s";
            g.fillText(l, k === 4 ? W - 10 - g.measureText(l).width : X(t) - 8, bottom + 12);
        }
        const line = (pts, color) => { if (pts.length < 1) return; g.strokeStyle = color; g.lineWidth = 2; g.beginPath(); pts.forEach(([t, v], i) => (i ? g.lineTo(X(t), Y(v)) : g.moveTo(X(t), Y(v)))); g.stroke(); g.lineWidth = 1; };
        const fa = seriesPoints(M.teamA), fb = seriesPoints(M.teamB);
        if (teams && (fa.length || fb.length)) { line(fa, TEAM_COLOR.a); line(fb, TEAM_COLOR.b); }
        else for (const P of M.players.values()) line(seriesPoints(P.score), lineColor.get(P.id));
        // round results: a coloured tick on the top strip for the round's winner
        for (const rd of M.rounds) { g.fillStyle = TEAM_COLOR[rd.winner] || "#9ca3af"; g.fillRect(X(rd.t) - 2, 2, 4, 12); }
        { let lastX = -1, lastKind = ""; for (const mk of M.markers) {
            if (mk.kind === "roundEnd") continue;
            const x = Math.round(X(mk.t));
            if (x === lastX && mk.kind === lastKind) continue;
            lastX = x; lastKind = mk.kind;
            g.fillStyle = MARK[mk.kind] || "#cbd5e1"; g.fillRect(x - 1, top, 2, 7);
        } }
        if (M.rounds.length) { g.fillStyle = "#94a3b8"; g.fillText("rounds", W - 50, 12); }
        if (!strip) return;
        // the slot strip: each frame's interpreted calls stacked by slot (height: ns, against the costliest frame); per
        // pixel column the costliest frame in it
        const y0 = H - 4, hMax = strip - 10;
        g.fillStyle = "#94a3b8"; g.fillText("slots", 2, H - 12);
        let maxNs = Math.max(1, M.maxSlotNs);
        for (const x of M.slots) if ((x.ns || 0) > maxNs) maxNs = x.ns;
        const stack = (fr) => { let y = y0; const x = X(fr.t); for (const sl of fr.slots) { const h = Math.max(1, hMax * (sl.ns || 0) / maxNs); g.fillStyle = this._slotColor(M, sl.slot); g.fillRect(x - 1, y - h, 2, h); y -= h; } };
        let col = -1, best = null;
        for (const fr of M.slotFrames) {
            const c = Math.round(X(fr.t));
            if (c !== col) { if (best) stack(best); col = c; best = fr; }
            else if (fr.ns > best.ns) best = fr;
        }
        if (best) stack(best);
        for (const sl of M.slots) { const h = 2 + hMax * (sl.ns || 0) / maxNs; g.fillStyle = this._slotColor(M, sl.slot); g.fillRect(X(sl.t), y0 - h, 1, h); }
        g.fillStyle = "#64748b"; g.fillText(`max ${us(maxNs)} a frame`, W - 120, H - strip + 8);
    }

    // the top-down plot: the map (start.mapView: boxes shaded by height, objectives, spawns), the trails of where anyone
    // went, and everyone where they are now. The map layer is drawn once per run and size; the trail layer keeps what
    // it has drawn and adds only the positions that came in since (both are redrawn when the size or scale changes).
    _plot(pc, r, M, teams, width) {
        const mv = M.start && M.start.mapView;
        let x0 = Infinity, x1 = -Infinity, z0 = Infinity, z1 = -Infinity;
        if (mv && mv.bounds) [x0, z0, x1, z1] = mv.bounds;
        if (M.bounds) { x0 = Math.min(x0, M.bounds[0]); z0 = Math.min(z0, M.bounds[1]); x1 = Math.max(x1, M.bounds[2]); z1 = Math.max(z1, M.bounds[3]); }
        const dx = Math.max(1, x1 - x0), dz = Math.max(1, z1 - z0);
        const W = Math.max(200, width), H = Math.max(180, Math.min(640, Math.round((W - 20) * dz / dx) + 30));
        if (pc.width !== W) pc.width = W;
        if (pc.height !== H) pc.height = H;
        const pg = pc.getContext("2d");
        if (!isFinite(x0)) { pg.fillStyle = "#0b1020"; pg.fillRect(0, 0, W, H); pg.fillStyle = "#64748b"; pg.font = "11px sans-serif"; pg.fillText("no positions in the frames", 10, 20); return; }
        const sc = Math.min((W - 20) / dx, (H - 30) / dz);
        const P = (x, z) => [10 + (x - x0) * sc, 10 + (z - z0) * sc];
        const key = `${W}x${H}:${x0},${z0},${sc}:${!!mv}`;
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
        if (!r._trail || r._trail.key !== key) { const c = document.createElement("canvas"); c.width = W; c.height = H; r._trail = { key, canvas: c, n: 0 }; }
        if (r._trail.n < M.trail.length) {   // (where anyone went: only the positions since the last draw)
            const g = r._trail.canvas.getContext("2d"), T = M.trail;
            g.fillStyle = "rgba(148,163,184,.35)";
            for (let i = r._trail.n; i + 1 < T.length; i += 2) g.fillRect(10 + (T[i] - x0) * sc, 10 + (T[i + 1] - z0) * sc, 1, 1);
            r._trail.n = T.length;
        }
        pg.drawImage(r._layer.canvas, 0, 0);
        pg.drawImage(r._trail.canvas, 0, 0);
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
        const bad = this._readForm();
        if (bad) { this.app.app.toast("Not run: " + bad, 5000); return; }
        const rules = this._rules();
        if (rules === undefined) return;
        this.stop(true);
        this.runs = [];
        const req = (r) => ({ ...this._req(r), realtime: false, trace: false, seeds: this.form.n, firstSeed: this.form.seed });
        this.batches = [this._entry({ label: "this file", kind: "batch", request: req(rules) })];
        if (this.form.compare && this.app.differsFromBuiltin()) this.batches.push(this._entry({ label: "built-in " + this.app.base, kind: "batch", request: req(this._builtinRules()) }));
        this._setBusy(true);
        this._drawBatches();
        this._persist();
        await this._runBatches();
    }
    // the batches one after the other (each already uses every core but one): a running one (reattached after a
    // reload) is waited for, a waiting one is started. Stop marks them all done, which ends this loop.
    async _runBatches() {
        for (const b of this.batches) {
            if (!b.done && !b.id) { await this._start(b, ["modebatch", "--stream"], b.request, () => this._soonBatch()); this._persist(); }
            await b.finished;
        }
    }
    _soonBatch() { this._throttle("_drawB", () => this._drawBatches(), this.batches); }
    _drawBatches() {
        const O = this.root.querySelector("#smOut");
        if (!O) return;
        const models = this.batches.map((b) => batchModel(b.lines));
        O.innerHTML = `<div class="sim-runs${this.batches.length > 1 ? " two" : ""}">${this.batches.map((b, i) => this._batchHtml(b, i)).join("")}</div>${this._batchCompareHtml(models)}`;
        this._batchHistograms(O, models);
        O.querySelectorAll("[data-csv]").forEach((btn) => (btn.onclick = () => { const i = +btn.dataset.csv, B = models[i]; this.app.app.download(csvName(this.app.name, B, i > 0), B.end.csv, "text/csv"); }));
    }
    // under the cards: the difference from the built-in (when both halves of a compare are done) and the distributions
    // of match length and score spread, every batch on the same axes
    _batchCompareHtml(models) {
        let h = "";
        const [a, b] = models;
        if (models.length > 1 && a.end && b.end) {
            const f = (x, fmt) => fmt === "pct" ? (100 * x).toFixed(0) + "%" : fmt === "s" ? x.toFixed(1) + " s" : fmt === "ns" ? us(x) : String(+x.toFixed(2));
            const d = (r) => {
                if (!r.d) return `<span class="muted">same</span>`;
                const sg = r.d > 0 ? "+" : "−", v = Math.abs(r.d);
                const txt = r.fmt === "pct" ? `${sg}${(100 * v).toFixed(0)} pp` : r.fmt === "s" ? `${sg}${v.toFixed(1)} s` : r.fmt === "ns" ? sg + us(v) : sg + (+v.toFixed(2));
                const rel = r.fmt !== "pct" && r.b ? ` <span class="muted">(${sg}${Math.round(100 * v / Math.abs(r.b))}%)</span>` : "";
                return `<b>${txt}</b>${rel}`;
            };
            h += `<div class="sim-run sim-delta"><h4>This file vs the built-in ${esc(this.app.base)} <span class="muted">(${esc(a.end.matchesPlayed)} and ${esc(b.end.matchesPlayed)} matches, the same seeds)</span></h4>
                <table class="sim-tab"><tr><th></th><th>this file</th><th>built-in</th><th>difference</th></tr>${batchDelta(a.end, b.end).map((r) => `<tr><td>${esc(r.label)}</td><td>${esc(f(r.a, r.fmt))}</td><td>${esc(f(r.b, r.fmt))}</td><td>${d(r)}</td></tr>`).join("")}</table></div>`;
        }
        if (models.some((m) => m.matches.length)) h += `<div class="sim-run sim-dist"><h4>Distributions <span class="muted">(matches per bin${models.length > 1 ? ` · <span style="color:${BATCH_COLORS[0]}">■ this file</span> <span style="color:${BATCH_COLORS[1]}">■ built-in</span>` : ""})</span></h4><div class="sim-hists"><canvas data-h="len"></canvas><canvas data-h="spread"></canvas></div></div>`;
        return h;
    }
    _batchHistograms(O, models) {
        const teams = models.some((m) => m.start ? m.start.teams !== false : m.end && !!m.end.teams);
        for (const cv of O.querySelectorAll(".sim-hists canvas")) {
            const kind = cv.dataset.h;
            const vals = models.map((m) => m.matches.map((x) => (kind === "len" ? x.t : matchSpread(x, teams))).filter((v) => typeof v === "number"));
            const all = vals.flat();
            if (!all.length) continue;
            let lo = Math.min(...all), hi = Math.max(...all);
            if (kind === "spread") { lo = 0; hi = Math.max(1, Math.ceil(hi)); }
            const bins = kind === "spread" ? Math.min(20, hi + 1) : 20;
            if (hi <= lo) hi = lo + 1;
            if (kind === "spread") hi += 1;   // (integers: one bin each up to 20)
            const counts = vals.map((v) => histogram(v, bins, lo, hi));
            const W = cv.width = Math.max(280, cv.clientWidth || 400), H = cv.height = 150;
            const g = cv.getContext("2d");
            g.fillStyle = "#0b1020"; g.fillRect(0, 0, W, H);
            const top = 18, bottom = H - 22, left = 28, bw = (W - left - 8) / bins, max = Math.max(1, ...counts.flat());
            g.fillStyle = "#94a3b8"; g.font = "10px monospace";
            g.fillText(kind === "len" ? "match length (s)" : teams ? "score spread |A - B|" : "score spread (winner - runner-up)", left, 12);
            g.fillText(String(max), 2, top + 8); g.fillText("0", 2, bottom);
            counts.forEach((c, k) => {
                g.fillStyle = BATCH_COLORS[k]; g.globalAlpha = models.length > 1 ? 0.75 : 1;
                const w = models.length > 1 ? bw / 2 - 1 : bw - 2;
                c.forEach((n, i) => { if (n) { const h = (bottom - top) * n / max; g.fillRect(left + i * bw + 1 + k * (bw / 2), bottom - h, Math.max(1, w), h); } });
            });
            g.globalAlpha = 1; g.fillStyle = "#94a3b8";
            const lab = (v) => (kind === "len" ? v.toFixed(0) : String(Math.round(v)));
            g.fillText(lab(lo), left, H - 8);
            const hiLab = lab(kind === "spread" ? hi - 1 : hi);
            g.fillText(hiLab, W - 8 - g.measureText(hiLab).width, H - 8);
        }
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
              ${ln ? `<div><b>logic per tick</b> avg ${esc(us(ln.avg))} · worst p99 ${esc(us(ln.p99))}${ln.p999 != null ? ` · worst p99.9 ${esc(us(ln.p999))}` : ""} · worst max ${esc(us(ln.max))} <span class="muted">(${esc(ln.matches)} matches)</span></div>` : ""}
              <div class="muted">${esc((e.perf.matchesPerSecond || 0).toFixed(2))} matches/s · ${esc(Math.round(e.perf.speedupPerMatch || 0))}x real time per match · ${esc(e.perf.threads)} threads · ${esc((e.perf.wallSeconds || 0).toFixed(1))} s</div></div>`;
        h += `<table class="sim-tab"><tr><th>bot</th>${e.teams ? "<th>team</th>" : ""}<th>wins</th><th>K/match</th><th>D/match</th><th>score</th>${objTime ? "<th>obj s</th>" : ""}</tr>${(e.players || []).map((p) => `<tr><td>${esc(p.name)}</td>${e.teams ? `<td style="color:${TEAM_COLOR[p.team] || "inherit"}">${esc(p.team.toUpperCase())}</td>` : ""}<td>${pct(p.winRate)}</td><td>${esc(p.kills)}</td><td>${esc(p.deaths)}</td><td>${esc(p.score)}</td>${objTime ? `<td>${esc(p.objTime)}</td>` : ""}</tr>`).join("")}</table>`;
        h += `<button data-csv="${i}" style="margin-top:6px">⇩ CSV (one row per match)</button></div>`;
        return h;
    }
}
