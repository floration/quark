// Adapted from tools/bot_editor/web/js/sim.js and batch.js at c7aae27 (the run stream over SSE, the run bar), cut down
// to what a mode needs: the Sim view runs `quark_maptool modesim` (tools/mode_editor/native/MODESIM.md) on the file
// being edited and draws its NDJSON answer: a timeline (scores, phase bands, round results, markers, interpreted slot
// calls), a top-down player plot, an event log and an end card. Compare runs the built-in base mode with the same
// seed; Batch streams `modebatch` (win rates, lengths, rounds, per bot, CSV).
//
// When the helper has no modesim (or isn't there) the view says so and runs nothing: there is no stand-in simulation,
// so a result on this page is always the game's. Until the game runs a .qmode's logic (mode editor phase 3) modesim
// plays the base mode with the file's key=value rules and says `logicApplied: false`: the view shows that plainly.
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
const teamOf = (t) => (t === 1 || t === "b" || t === "B" ? "b" : t === 0 || t === "a" || t === "A" ? "a" : null);

// NDJSON lines (objects) -> the timeline. Lenient about field names the helper may add; never invents values.
export function timelineModel(lines) {
    const m = { start: null, end: null, frames: [], bands: [], markers: [], rounds: [], slots: [], players: new Map(), names: new Map(), errors: [], warnings: [],
        t0: 0, t1: 0, maxScore: 1 };
    let band = null;
    const nm = (id) => (id === undefined || id === null || id < 0 ? "world" : m.names.get(id) || "#" + id);
    for (const L of lines) {
        if (!L || typeof L !== "object") continue;
        const type = L.type || L.kind || (L.t !== undefined && L.phase !== undefined ? "frame" : "");
        if (type === "start") {
            m.start = L;
            for (const b of L.bots || []) if (b.id !== undefined) m.names.set(b.id, b.name || "#" + b.id);
        } else if (type === "end") m.end = L;
        else if (type === "error") m.errors.push(L.error || L.message || JSON.stringify(L));
        else if (type === "warning") m.warnings.push(L.warning || L.message || "");
        else if (type === "slot") m.slots.push({ t: +L.t || 0, slot: L.slot, ns: L.ns ?? null, actions: L.actions || [] });
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
        case "kill": return e.killer === e.victim || e.killer === undefined || e.killer < 0 ?`${nm(e.victim)} died (${e.weapon || "?"})` : `${nm(e.killer)} killed ${nm(e.victim)} (${e.weapon || "?"})`;
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

export class SimView {
    constructor(root, app) {
        this.root = root; this.app = app;   // app: the ModePage
        this.status = null;                 // /api/modesim/status
        this.runs = [];                     // [{label, lines, model, id, es, done}]
        this.batches = [];                  // [{label, lines, id, es, done}]
    }
    async refresh() {
        try { this.status = await api.modesimStatus(); } catch (e) { this.status = { available: false, reason: e.message }; }
        if (this.status.available && !this.maps) { try { this.maps = (await api.maps()).maps; } catch { this.maps = []; } }
        this.render();
    }
    show() { if (!this.status) this.refresh(); else this.render(); }

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
        const maps = (this.maps || []).filter((m) => m.source === "default" || m.source === "user");
        const base = this.app.base;
        const prefer = maps.find((m) => m.for === base) || maps[0];
        const two = base === "duel" || base === "1v1" || base === "tourney";
        R.innerHTML = `<div class="sim-form row">
            <label>Map ${maps.length ? `<select id="smMap">${maps.map((m) => `<option value="${esc(m.name)}" ${m === prefer ? "selected" : ""}>${esc(m.name)}${m.for ? " (" + esc(m.for) + ")" : ""}${m.source === "user" ? " [yours]" : ""}</option>`).join("")}</select>` : `<input id="smMap" placeholder="map name" title="no built maps/ folder was found: type a map name the helper knows" style="width:110px">`}</label>
            <label>Bots <input id="smBots" type="number" value="${two ? 2 : 8}" min="1" max="32" style="width:52px"></label>
            <label>Skill <select id="smSkill"><option value="0">beginner</option><option value="1">intermediate</option><option value="2" selected>advanced</option><option value="3">expert</option></select></label>
            <label>Seed <input id="smSeed" type="number" value="1" style="width:64px"></label>
            <label title="the most game time a run plays (it stops at the match end)">Max s <input id="smSecs" type="number" value="1800" min="5" max="7200" style="width:64px"></label>
            <label title="the server's tick rate">Hz <select id="smHz"><option>125</option><option>250</option><option>500</option><option>1000</option></select></label>
            <label title="paced at real time (else as fast as the CPU goes)"><input id="smReal" type="checkbox"> real time</label>
            <label title="slot calls with their cost (needs the game's logic hook)"><input id="smTrace" type="checkbox"> trace slots</label>
            <label title="also run the built-in ${esc(base)} with the same seed"><input id="smCompare" type="checkbox"> compare with built-in</label>
            <button id="smRun" class="primary">▶ Run</button><button id="smStop" disabled>■ Stop</button>
            ${st.batch ? `<span class="sp"></span><label>Batch seeds <input id="smN" type="number" value="20" min="2" max="1000" style="width:60px"></label><button id="smBatch">Run batch</button>` : '<span class="muted" title="quark_maptool has no modebatch">(no modebatch)</span>'}
          </div><div class="badge real" style="margin:4px 0;display:inline-block">quark_maptool modesim · ${esc(st.maptool)}</div><div id="smOut"></div>`;
        R.querySelector("#smRun").onclick = () => this.run();
        R.querySelector("#smStop").onclick = () => this.stop();
        const b = R.querySelector("#smBatch");
        if (b) b.onclick = () => this.batch();
        this._drawRuns();
    }
    _req(rules) {
        const R = this.root;
        const req = { map: R.querySelector("#smMap").value, bots: +R.querySelector("#smBots").value, skill: +R.querySelector("#smSkill").value,
            seed: +R.querySelector("#smSeed").value, seconds: +R.querySelector("#smSecs").value, tickRate: +R.querySelector("#smHz").value, rate: 4,
            realtime: R.querySelector("#smReal").checked, speed: 1, until: "matchEnd", trace: R.querySelector("#smTrace").checked };
        if (rules && rules.trim()) req.rules = rules; else req.mode = this.app.base;
        return req;
    }
    _busy(on) { const b1 = this.root.querySelector("#smRun"), b2 = this.root.querySelector("#smStop"), b3 = this.root.querySelector("#smBatch"); if (b1) b1.disabled = on; if (b3) b3.disabled = on; if (b2) b2.disabled = !on; }
    async run() {
        const text = this.app.saveText();
        if (text == null) { this.app.app.toast("The code has an error: fix it first"); return; }
        this.stop();
        this.batches = [];
        this.runs = [{ label: "this file", lines: [], model: null }];
        if (this.root.querySelector("#smCompare").checked) this.runs.push({ label: "built-in " + this.app.base, lines: [], model: null });
        this._busy(true);
        this._drawRuns();
        await Promise.all(this.runs.map((r, i) => this._start(r, ["modesim", "--stream"], this._req(i === 0 ? text : this.app.builtinText()), () => this._soon())));
    }
    // one helper run streamed into r.lines (SSE from serve.py); onLine after each batch of lines and at the end
    async _start(r, args, request, onLine) {
        try {
            const { id } = await api.post("/api/run", { args, request }, "json");
            r.id = id;
            const es = new EventSource(`/api/run/${id}/events`);
            r.es = es;
            es.onmessage = (ev) => {
                for (const l of ev.data.split("\n")) { if (!l) continue; try { r.lines.push(JSON.parse(l)); } catch { r.lines.push({ type: "error", error: "not JSON: " + l.slice(0, 200) }); } }
                onLine();
            };
            es.addEventListener("exit", (ev) => {
                es.close(); r.done = true;
                try { const x = JSON.parse(ev.data); r.code = x.code; if (x.code && !r.lines.some((l) => l.type === "error" || l.error)) r.lines.push({ type: "error", error: `${args[0]} exited with ${x.code}: ${x.stderr || ""}` }); } catch {}
                onLine();
                if (this.runs.every((q) => q.done) && this.batches.every((q) => q.done)) this._busy(false);
            });
            es.onerror = () => { if (!r.done) { es.close(); r.done = true; onLine(); if (this.runs.every((q) => q.done) && this.batches.every((q) => q.done)) this._busy(false); } };
        } catch (e) { r.lines.push({ type: "error", error: e.message }); r.done = true; onLine(); if (this.runs.every((q) => q.done)) this._busy(false); }
    }
    stop() {
        for (const r of [...this.runs, ...this.batches]) { if (r.es) r.es.close(); if (r.id && !r.done) api.post(`/api/run/${r.id}/stop`, {}).catch(() => {}); r.done = true; }
        this._busy(false);
    }
    _soon() { if (this._raf) return; this._raf = requestAnimationFrame(() => { this._raf = 0; for (const r of this.runs) r.model = timelineModel(r.lines); this._drawRuns(); }); }

    _drawRuns() {
        const O = this.root.querySelector("#smOut");
        if (!O) return;
        if (this.batches.length) return this._drawBatches();
        if (!this.runs.length) { O.innerHTML = `<div class="muted">No run yet. A run uses the current text (saved or not) as the request's rules.</div>`; return; }
        O.innerHTML = `<div class="sim-runs">${this.runs.map((r, i) => `<div class="sim-run" data-i="${i}"><h4>${esc(r.label)}${r.done ? "" : ' <span class="muted">running…</span>'}</h4><div class="sim-start"></div><canvas class="tl" height="190"></canvas><div class="row" style="align-items:flex-start;gap:8px"><canvas class="plot" width="260" height="200"></canvas><div class="sim-end grow"></div></div><div class="sim-log"></div></div>`).join("")}</div>`;
        O.querySelectorAll(".sim-run").forEach((el) => this._drawRun(el, this.runs[+el.dataset.i]));
    }
    _drawRun(el, r) {
        const M = r.model || timelineModel(r.lines);
        const s = M.start;
        if (s) {
            const lg = s.logic || {};
            const logicChip = !lg.present ? `<span class="chip">no logic: ${s.custom ? "key rules only" : "the built-in mode"}</span>`
                : s.logicApplied ? `<span class="badge real">LOGIC RUN BY THE SERVER</span>`
                : `<span class="badge mock" title="${esc(lg.why || "")}">LOGIC NOT RUN: base ${esc(s.base)} with the key rules</span>`;
            const would = (lg.interpreted || []).length ? `<span class="chip" title="slots that differ from the built-in ${esc(s.base)}">${s.logicApplied ? "interpreted" : "would interpret"}: ${esc(lg.interpreted.join(", "))}</span>` : "";
            el.querySelector(".sim-start").innerHTML = `<span class="chip">${esc(s.label || "")}</span> <span class="chip">base ${esc(s.base || "")}</span> <span class="chip">${esc(s.map || "")}</span> ${logicChip} ${would}
                ${(s.warnings || []).concat(M.warnings).map((w) => `<div class="warn" style="font-size:11px">⚠ ${esc(typeof w === "string" ? w : w.msg)}</div>`).join("")}
                ${!s.logicApplied && lg.present && lg.why ? `<div class="muted" style="font-size:11px">${esc(lg.why)}</div>` : ""}`;
        } else el.querySelector(".sim-start").innerHTML = "";
        // timeline canvas
        const cv = el.querySelector("canvas.tl");
        const W = cv.width = Math.max(300, el.clientWidth - 4), H = cv.height;
        const g = cv.getContext("2d");
        g.fillStyle = "#0b1020"; g.fillRect(0, 0, W, H);
        const span = Math.max(1, M.t1 - M.t0), X = (t) => 30 + (W - 40) * (t - M.t0) / span, Y = (v) => H - 40 - (H - 64) * v / M.maxScore;
        for (const b of M.bands) { g.fillStyle = PHASE_COLORS[b.phase] || "#222"; g.globalAlpha = 0.55; g.fillRect(X(b.t0), 18, Math.max(1, X(b.t1) - X(b.t0)), H - 58); g.globalAlpha = 1; }
        g.strokeStyle = "#334155"; g.beginPath(); g.moveTo(30, H - 40); g.lineTo(W - 10, H - 40); g.stroke();
        g.fillStyle = "#94a3b8"; g.font = "10px monospace"; g.fillText(String(M.maxScore), 2, 26); g.fillText("0", 2, H - 40);
        for (let k = 0; k <= 4; k++) { const t = M.t0 + span * k / 4; g.fillText(t.toFixed(0) + "s", X(t) - 8, H - 28); }
        const line = (pts, color) => { if (pts.length < 1) return; g.strokeStyle = color; g.lineWidth = 2; g.beginPath(); pts.forEach(([t, v], i) => (i ? g.lineTo(X(t), Y(v)) : g.moveTo(X(t), Y(v)))); g.stroke(); g.lineWidth = 1; };
        const teams = s ? s.teams !== false : true;
        const fa = M.frames.filter((f) => f.scoreA !== null), fb = M.frames.filter((f) => f.scoreB !== null);
        const lineColor = new Map();   // (free for all and the like: one line per player, its colour in the table too)
        { let i = 0; for (const P of M.players.values()) lineColor.set(P.id, `hsl(${(i++ * 67) % 360} 70% 60%)`); }
        if (teams && (fa.length || fb.length)) { line(fa.map((f) => [f.t, f.scoreA]), TEAM_COLOR.a); line(fb.map((f) => [f.t, f.scoreB]), TEAM_COLOR.b); }
        else for (const P of M.players.values()) line(P.scores, lineColor.get(P.id));
        // round results: a coloured tick on the top strip for the round's winner
        for (const rd of M.rounds) { g.fillStyle = TEAM_COLOR[rd.winner] || "#9ca3af"; g.fillRect(X(rd.t) - 2, 2, 4, 12); }
        for (const mk of M.markers) { if (mk.kind === "roundEnd") continue; g.fillStyle = MARK[mk.kind] || "#cbd5e1"; g.fillRect(X(mk.t) - 1, 18, 2, 7); }
        g.fillStyle = "#94a3b8"; g.fillText("rounds", W - 50, 12);
        // slot strip
        if (M.slots.length) { const maxNs = Math.max(1, ...M.slots.map((x) => x.ns || 0)); for (const sl of M.slots) { const h = 2 + 10 * (sl.ns || 0) / maxNs; g.fillStyle = "#a78bfa"; g.fillRect(X(sl.t), H - 12 - h, 1, h); } g.fillStyle = "#94a3b8"; g.fillText("slots", 2, H - 12); }
        // player plot (last frame), positions over the whole run set the scale
        const pc = el.querySelector("canvas.plot"), pg = pc.getContext("2d");
        pg.fillStyle = "#0b1020"; pg.fillRect(0, 0, pc.width, pc.height);
        const last = M.frames[M.frames.length - 1];
        const pos = M.frames.flatMap((f) => f.players.map((p) => p.pos)).filter(Array.isArray);
        if (last && pos.length) {
            let x0 = Infinity, x1 = -Infinity, z0 = Infinity, z1 = -Infinity;
            for (const p of pos) { const z = p.length > 2 ? p[2] : p[1]; x0 = Math.min(x0, p[0]); x1 = Math.max(x1, p[0]); z0 = Math.min(z0, z); z1 = Math.max(z1, z); }
            const sc = Math.min((pc.width - 20) / Math.max(1, x1 - x0), (pc.height - 30) / Math.max(1, z1 - z0));
            const P = (p) => [10 + (p[0] - x0) * sc, 10 + ((p.length > 2 ? p[2] : p[1]) - z0) * sc];
            pg.fillStyle = "#1e293b"; for (const p of pos) { const [x, y] = P(p); pg.fillRect(x, y, 1, 1); }   // (where anyone went)
            const ob = last.obj && last.obj.bomb;
            if (ob && Array.isArray(ob.pos)) { const [x, y] = P(ob.pos); pg.strokeStyle = MARK.plant; pg.strokeRect(x - 4, y - 4, 8, 8); }
            for (const p of last.players) if (Array.isArray(p.pos)) {
                const [x, y] = P(p.pos);
                const tm = teams ? teamOf(p.team) : null;
                pg.fillStyle = p.alive === false ? "#475569" : tm ? TEAM_COLOR[tm] : "#e5e7eb";
                pg.beginPath(); pg.arc(x, y, p.it ? 6 : 4, 0, 7); pg.fill();
            }
            pg.fillStyle = "#94a3b8"; pg.font = "10px monospace";
            pg.fillText(`t=${last.t.toFixed(1)} ${last.phase}${last.round ? " r" + last.round : ""}${last.clock !== null ? " clock " + Math.round(last.clock) : ""}`, 4, pc.height - 4);
        } else { pg.fillStyle = "#64748b"; pg.font = "11px sans-serif"; pg.fillText("no positions in the frames", 10, 20); }
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
            const rows = ((e.result && e.result.players) || []).slice().sort((a, b) => (b.score - a.score) || (b.kills - a.kills));
            card = `<div class="sim-card"><b>${esc(wText)}</b> <span class="muted">(${esc(e.reason || "")})</span>
                <div>rounds ${esc(e.rounds ?? "-")} · score ${esc(e.result ? e.result.scoreA + " - " + e.result.scoreB : "-")} · length ${esc((e.t ?? M.t1).toFixed ? (e.t ?? M.t1).toFixed(1) : e.t)} s</div>
                ${pf.speedup ? `<div class="muted">${esc(Math.round(pf.speedup))}x real time · ${esc(pf.ticks)} ticks · server tick ns avg ${esc(tn.avg)} / p99 ${esc(tn.p99)} / max ${esc(tn.max)}</div>` : ""}
                <div class="muted">logic ns per tick: ${lns ? `avg ${esc(lns.avg)} · p99 ${esc(lns.p99)} · max ${esc(lns.max)}` : "not measured (needs the game's logic hook)"}</div>
                ${rows.length ? `<table class="sim-tab"><tr><th></th><th>score</th><th>K</th><th>D</th><th>obj s</th></tr>${rows.map((p) => `<tr><td style="color:${teams ? TEAM_COLOR[teamOf(p.team)] || "inherit" : lineColor.get(p.id) || "inherit"}">${p.won ? "★ " : ""}${esc(p.name)}</td><td>${esc(p.score)}</td><td>${esc(p.kills)}</td><td>${esc(p.deaths)}</td><td>${esc(p.objTime)}</td></tr>`).join("")}</table>` : ""}</div>`;
        }
        el.querySelector(".sim-end").innerHTML = (M.errors.length ? M.errors.map((x) => `<div class="bad">${esc(x)}</div>`).join("") : "")
            + (e ? card : `<div class="muted">${r.done ? "The run ended without an end line." : "…"}</div>`);
        el.querySelector(".sim-log").innerHTML = M.markers.slice(-400).map((x) => `<div><span class="muted">${x.t.toFixed(1)}</span> <b style="color:${MARK[x.kind] || "#cbd5e1"}">${esc(x.kind)}</b> ${esc(x.text)}</div>`).join("");
    }

    async batch() {
        const text = this.app.saveText();
        if (text == null) { this.app.app.toast("The code has an error: fix it first"); return; }
        this.stop();
        this.runs = [];
        const n = +this.root.querySelector("#smN").value;
        const req = (rules) => ({ ...this._req(rules), realtime: false, trace: false, seeds: n, firstSeed: +this.root.querySelector("#smSeed").value });
        this.batches = [{ label: "this file", lines: [], request: req(text) }];
        if (this.root.querySelector("#smCompare").checked) this.batches.push({ label: "built-in " + this.app.base, lines: [], request: req(this.app.builtinText()) });
        this._busy(true);
        this._drawBatches();
        // one after the other (each already uses every core but one)
        for (const b of this.batches) {
            if (b.done) continue;
            await new Promise((res) => { this._start(b, ["modebatch", "--stream"], b.request, () => { this._soonBatch(); if (b.done) res(); }); });
        }
    }
    _soonBatch() { if (this._rafB) return; this._rafB = requestAnimationFrame(() => { this._rafB = 0; this._drawBatches(); }); }
    _drawBatches() {
        const O = this.root.querySelector("#smOut");
        if (!O) return;
        O.innerHTML = `<div class="sim-runs">${this.batches.map((b, i) => this._batchHtml(b, i)).join("")}</div>`;
        O.querySelectorAll("[data-csv]").forEach((btn) => (btn.onclick = () => { const B = batchModel(this.batches[+btn.dataset.csv].lines); this.app.app.download("modebatch.csv", B.end.csv, "text/csv"); }));
    }
    _batchHtml(b, i) {
        const B = batchModel(b.lines);
        const s = B.start, e = B.end;
        const pr = B.progress || { done: 0, total: s ? s.matches : "?" };
        let h = `<div class="sim-run"><h4>${esc(b.label)} <span class="muted">${e ? "" : b.done ? "(stopped)" : `running… ${pr.done}/${pr.total}`}</span></h4>`;
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
        const L = e.length || {};
        h += `<div class="sim-card" style="margin-top:6px">${e.matchesPlayed} played · ${e.timeouts} timeouts (hit the max s) · ${e.errors} errors<br>length avg ${esc(L.avg)} s (min ${esc(L.min)}, median ${esc(L.p50)}, max ${esc(L.max)}) · rounds ${esc(e.rounds)} · score spread ${esc(e.scoreSpread)}
              <div class="muted">${esc((e.perf.matchesPerSecond || 0).toFixed(2))} matches/s · ${esc(Math.round(e.perf.speedupPerMatch || 0))}x real time per match · ${esc(e.perf.threads)} threads · ${esc((e.perf.wallSeconds || 0).toFixed(1))} s</div></div>`;
        h += `<table class="sim-tab"><tr><th>bot</th><th>team</th><th>wins</th><th>K/match</th><th>D/match</th><th>score</th><th>obj s</th></tr>${(e.players || []).map((p) => `<tr><td>${esc(p.name)}</td><td style="color:${TEAM_COLOR[p.team] || "inherit"}">${esc(e.teams ? p.team.toUpperCase() : "")}</td><td>${pct(p.winRate)}</td><td>${esc(p.kills)}</td><td>${esc(p.deaths)}</td><td>${esc(p.score)}</td><td>${esc(p.objTime)}</td></tr>`).join("")}</table>`;
        h += `<button data-csv="${i}" style="margin-top:6px">⇩ CSV (one row per match)</button></div>`;
        return h;
    }
}
