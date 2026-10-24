// Adapted from tools/bot_editor/web/js/sim.js and batch.js at c7aae27 (the run stream over SSE, the run bar), cut down
// to what a mode needs: the Sim view runs `quark_maptool modesim` (design 5.4) on the file being edited and draws its
// NDJSON answer: a timeline (scores, phase bands, markers, interpreted slot calls), a top-down player plot, an event
// log and an end card. Compare runs the built-in base mode with the same seed; Batch runs `modebatch`.
//
// When the helper has no modesim (or isn't there) the view says so and runs nothing: there is no stand-in simulation,
// so a result on this page is always the game's.
//
// timelineModel() is pure (no DOM): the tests and the view both use it.

import { api } from "./api.js";

const esc = (x) => String(x ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const PHASE_COLORS = { warmup: "#334155", freeze: "#1e3a5f", live: "#14532d", roundEnd: "#5b3a12", matchEnd: "#4c1d24", overtime: "#713f12" };
const MARK = { kill: "#f87171", roundEnd: "#fbbf24", plant: "#fb923c", defuse: "#60a5fa", explode: "#ef4444", capture: "#a78bfa", checkpoint: "#34d399",
    finish: "#22c55e", announce: "#e5e7eb", delivered: "#22d3ee", cartCheckpoint: "#22d3ee" };

// NDJSON lines (objects) -> the timeline. Lenient about field names the helper may add; never invents values.
export function timelineModel(lines) {
    const m = { start: null, end: null, frames: [], bands: [], markers: [], slots: [], players: new Map(), errors: [], t0: 0, t1: 0, maxScore: 1 };
    let band = null;
    for (const L of lines) {
        if (!L || typeof L !== "object") continue;
        const type = L.type || L.kind || (L.t !== undefined && L.phase !== undefined ? "frame" : "");
        if (type === "start") m.start = L;
        else if (type === "end") m.end = L;
        else if (type === "error") m.errors.push(L.error || L.message || JSON.stringify(L));
        else if (type === "slot") m.slots.push({ t: +L.t || 0, slot: L.slot, ns: L.ns ?? null, actions: L.actions || [] });
        else if (type === "frame") {
            const t = +L.t || 0;
            const f = { t, phase: L.phase || "?", round: L.round ?? null, scoreA: L.scoreA ?? null, scoreB: L.scoreB ?? null, players: L.players || [] };
            m.frames.push(f);
            if (!band || band.phase !== f.phase) { if (band) band.t1 = t; band = { phase: f.phase, t0: t, t1: t }; m.bands.push(band); }
            band.t1 = t;
            for (const p of f.players) {
                if (p.id === undefined) continue;
                let P = m.players.get(p.id);
                if (!P) { P = { id: p.id, name: p.name || "#" + p.id, team: p.team, scores: [] }; m.players.set(p.id, P); }
                if (p.score !== undefined) P.scores.push([t, p.score]);
                if (p.score !== undefined) m.maxScore = Math.max(m.maxScore, p.score);
            }
            if (f.scoreA !== null) m.maxScore = Math.max(m.maxScore, f.scoreA);
            if (f.scoreB !== null) m.maxScore = Math.max(m.maxScore, f.scoreB);
            for (const e of L.events || []) m.markers.push({ t: e.t ?? t, kind: e.type || e.kind || "event", text: e.text || e.msg || describeEvent(e), raw: e });
        }
    }
    if (m.frames.length) { m.t0 = m.frames[0].t; m.t1 = m.frames[m.frames.length - 1].t; }
    return m;
}
function describeEvent(e) {
    const o = { ...e }; delete o.t; delete o.type; delete o.kind;
    return Object.entries(o).map(([k, v]) => `${k}=${typeof v === "object" ? JSON.stringify(v) : v}`).join(" ");
}

export class SimView {
    constructor(root, app) {
        this.root = root; this.app = app;   // app: the ModePage
        this.status = null;                 // /api/modesim/status
        this.runs = [];                     // [{label, lines, model, id}]
        this.cursor = null;
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
              <p class="muted">The Sim view runs the game's own headless helper, <code>quark_maptool modesim</code> (design 5.4), on the file you are editing. Until a build has that command this view runs nothing: there is no stand-in, so nothing here is ever a made-up result.</p>
              <p class="muted">${st.maptool ? `Found quark_maptool at <code>${esc(st.maptool)}</code>${st.commands ? `; its commands: ${esc(st.commands.join(", "))}` : ""}.` : "quark_maptool wasn't found in this repository's build folders or on PATH (set \"maptool\" in mode_editor.json or pass --maptool)."}</p>
              <button id="simRecheck">Check again</button></div>`;
            R.querySelector("#simRecheck").onclick = () => { this.status = null; this.render(); this.refresh(); };
            return;
        }
        const maps = (this.maps || []).filter((m) => m.source === "default" || m.source === "user");
        const base = this.app.base;
        const prefer = maps.find((m) => m.for === base) || maps[0];
        R.innerHTML = `<div class="sim-form row">
            <label>Map ${maps.length ? `<select id="smMap">${maps.map((m) => `<option value="${esc(m.name)}" ${m === prefer ? "selected" : ""}>${esc(m.name)}${m.for ? " (" + esc(m.for) + ")" : ""}</option>`).join("")}</select>` : `<input id="smMap" placeholder="map name" title="no built maps/ folder was found: type a map name the helper knows" style="width:110px">`}</label>
            <label>Bots <input id="smBots" type="number" value="${base === "duel" || base === "1v1" ? 2 : 8}" min="1" max="32" style="width:52px"></label>
            <label>Skill <select id="smSkill"><option value="0">beginner</option><option value="1" selected>intermediate</option><option value="2">advanced</option><option value="3">expert</option></select></label>
            <label>Seed <input id="smSeed" type="number" value="1" style="width:64px"></label>
            <label>Seconds <input id="smSecs" type="number" value="900" min="5" max="7200" style="width:64px"></label>
            <label title="slot calls with their cost"><input id="smTrace" type="checkbox" checked> trace slots</label>
            <label title="also run the built-in ${esc(base)} with the same seed"><input id="smCompare" type="checkbox"> compare with built-in</label>
            <button id="smRun" class="primary">▶ Run</button><button id="smStop" disabled>■ Stop</button>
            ${st.batch ? `<span class="sp"></span><label>Batch seeds <input id="smN" type="number" value="20" min="2" max="1000" style="width:60px"></label><button id="smBatch">Run batch</button>` : '<span class="muted" title="quark_maptool has no modebatch">(no modebatch)</span>'}
          </div><div class="badge real" style="margin:4px 0">quark_maptool modesim · ${esc(st.maptool)}</div><div id="smOut"></div>`;
        R.querySelector("#smRun").onclick = () => this.run();
        R.querySelector("#smStop").onclick = () => this.stop();
        const b = R.querySelector("#smBatch");
        if (b) b.onclick = () => this.batch();
        this._drawRuns();
    }
    _req(rules) {
        const R = this.root;
        return { map: R.querySelector("#smMap").value, rules, bots: +R.querySelector("#smBots").value, skill: +R.querySelector("#smSkill").value,
            seed: +R.querySelector("#smSeed").value, seconds: +R.querySelector("#smSecs").value, tickRate: 60, rate: 10, realtime: false, speed: 0,
            until: "end", trace: R.querySelector("#smTrace").checked };
    }
    async run() {
        const text = this.app.saveText();
        if (text == null) { this.app.app.toast("The code has an error: fix it first"); return; }
        this.stop();
        this.runs = [{ label: "this file", lines: [], model: null }];
        if (this.root.querySelector("#smCompare").checked) this.runs.push({ label: "built-in " + this.app.base, lines: [], model: null });
        this.root.querySelector("#smRun").disabled = true; this.root.querySelector("#smStop").disabled = false;
        await Promise.all(this.runs.map((r, i) => this._start(r, this._req(i === 0 ? text : this.app.builtinText()))));
    }
    async _start(r, request) {
        try {
            const { id } = await api.post("/api/run", { args: ["modesim"], request }, "json");
            r.id = id;
            const es = new EventSource(`/api/run/${id}/events`);
            r.es = es;
            es.onmessage = (ev) => {
                for (const l of ev.data.split("\n")) { try { r.lines.push(JSON.parse(l)); } catch { r.lines.push({ type: "error", error: "not JSON: " + l.slice(0, 200) }); } }
                this._soon();
            };
            es.addEventListener("exit", (ev) => {
                es.close(); r.done = true;
                try { const x = JSON.parse(ev.data); r.code = x.code; if (x.code) r.lines.push({ type: "error", error: `modesim exited with ${x.code}: ${x.stderr || ""}` }); } catch {}
                this._soon();
                if (this.runs.every((q) => q.done)) { const b1 = this.root.querySelector("#smRun"), b2 = this.root.querySelector("#smStop"); if (b1) b1.disabled = false; if (b2) b2.disabled = true; }
            });
            es.onerror = () => { if (!r.done) { es.close(); r.done = true; this._soon(); } };
        } catch (e) { r.lines.push({ type: "error", error: e.message }); r.done = true; this._soon(); }
    }
    stop() {
        for (const r of this.runs) { if (r.es) r.es.close(); if (r.id && !r.done) api.post(`/api/run/${r.id}/stop`, {}).catch(() => {}); r.done = true; }
        const b1 = this.root.querySelector("#smRun"), b2 = this.root.querySelector("#smStop");
        if (b1) b1.disabled = false; if (b2) b2.disabled = true;
    }
    _soon() { if (this._raf) return; this._raf = requestAnimationFrame(() => { this._raf = 0; for (const r of this.runs) r.model = timelineModel(r.lines); this._drawRuns(); }); }

    _drawRuns() {
        const O = this.root.querySelector("#smOut");
        if (!O) return;
        if (!this.runs.length) { O.innerHTML = `<div class="muted">No run yet. A run uses the current text (saved or not) as the request's rules.</div>`; return; }
        O.innerHTML = `<div class="sim-runs">${this.runs.map((r, i) => `<div class="sim-run" data-i="${i}"><h4>${esc(r.label)}${r.done ? "" : ' <span class="muted">running…</span>'}</h4><div class="sim-start"></div><canvas class="tl" height="170"></canvas><div class="row"><canvas class="plot" width="260" height="200"></canvas><div class="sim-end grow"></div></div><div class="sim-log"></div></div>`).join("")}</div>`;
        O.querySelectorAll(".sim-run").forEach((el) => this._drawRun(el, this.runs[+el.dataset.i]));
    }
    _drawRun(el, r) {
        const M = r.model || timelineModel(r.lines);
        const s = M.start;
        el.querySelector(".sim-start").innerHTML = s ? `<span class="chip">${esc(s.label || "")}</span> <span class="chip">base ${esc(s.base || "")}</span> ${(s.interpreted || s.interp || []).length ? `<span class="chip">interpreted: ${esc((s.interpreted || s.interp).join(", "))}</span>` : '<span class="chip">every slot native</span>'} ${(s.warnings || []).map((w) => `<span class="warn">${esc(typeof w === "string" ? w : w.msg)}</span>`).join(" ")}` : "";
        // timeline canvas
        const cv = el.querySelector("canvas.tl");
        const W = cv.width = Math.max(300, el.clientWidth - 4), H = cv.height;
        const g = cv.getContext("2d");
        g.fillStyle = "#0b1020"; g.fillRect(0, 0, W, H);
        const span = Math.max(1, M.t1 - M.t0), X = (t) => 30 + (W - 40) * (t - M.t0) / span, Y = (v) => H - 40 - (H - 60) * v / M.maxScore;
        for (const b of M.bands) { g.fillStyle = PHASE_COLORS[b.phase] || "#222"; g.fillRect(X(b.t0), 8, Math.max(1, X(b.t1) - X(b.t0)), H - 48); }
        g.strokeStyle = "#334155"; g.beginPath(); g.moveTo(30, H - 40); g.lineTo(W - 10, H - 40); g.stroke();
        g.fillStyle = "#94a3b8"; g.font = "10px monospace"; g.fillText(String(M.maxScore), 2, 16); g.fillText("0", 2, H - 40);
        for (let k = 0; k <= 4; k++) { const t = M.t0 + span * k / 4; g.fillText(t.toFixed(0) + "s", X(t) - 8, H - 28); }
        const line = (pts, color) => { if (pts.length < 1) return; g.strokeStyle = color; g.lineWidth = 2; g.beginPath(); pts.forEach(([t, v], i) => (i ? g.lineTo(X(t), Y(v)) : g.moveTo(X(t), Y(v)))); g.stroke(); g.lineWidth = 1; };
        const fa = M.frames.filter((f) => f.scoreA !== null), fb = M.frames.filter((f) => f.scoreB !== null);
        if (fa.length || fb.length) { line(fa.map((f) => [f.t, f.scoreA]), "#60a5fa"); line(fb.map((f) => [f.t, f.scoreB]), "#f87171"); }
        else { let i = 0; for (const P of M.players.values()) line(P.scores, `hsl(${(i++ * 67) % 360} 70% 60%)`); }
        for (const mk of M.markers) { g.fillStyle = MARK[mk.kind] || "#cbd5e1"; g.fillRect(X(mk.t) - 1, 8, 2, 8); }
        // slot strip
        if (M.slots.length) { const maxNs = Math.max(1, ...M.slots.map((x) => x.ns || 0)); for (const sl of M.slots) { const h = 2 + 10 * (sl.ns || 0) / maxNs; g.fillStyle = "#a78bfa"; g.fillRect(X(sl.t), H - 12 - h, 1, h); } g.fillStyle = "#94a3b8"; g.fillText("slots", 2, H - 12); }
        // player plot (last frame)
        const pc = el.querySelector("canvas.plot"), pg = pc.getContext("2d");
        pg.fillStyle = "#0b1020"; pg.fillRect(0, 0, pc.width, pc.height);
        const last = M.frames[M.frames.length - 1];
        const pos = (M.frames.flatMap((f) => f.players.map((p) => p.pos)).filter(Array.isArray));
        if (last && pos.length) {
            let x0 = Infinity, x1 = -Infinity, z0 = Infinity, z1 = -Infinity;
            for (const p of pos) { const z = p.length > 2 ? p[2] : p[1]; x0 = Math.min(x0, p[0]); x1 = Math.max(x1, p[0]); z0 = Math.min(z0, z); z1 = Math.max(z1, z); }
            const sc = Math.min((pc.width - 20) / Math.max(1, x1 - x0), (pc.height - 20) / Math.max(1, z1 - z0));
            for (const p of last.players) if (Array.isArray(p.pos)) {
                const z = p.pos.length > 2 ? p.pos[2] : p.pos[1];
                pg.fillStyle = p.alive === false ? "#475569" : p.team === 1 || p.team === "B" ? "#f87171" : p.team === 0 || p.team === "A" ? "#60a5fa" : "#e5e7eb";
                pg.beginPath(); pg.arc(10 + (p.pos[0] - x0) * sc, 10 + (z - z0) * sc, 4, 0, 7); pg.fill();
            }
            pg.fillStyle = "#94a3b8"; pg.font = "10px monospace"; pg.fillText(`t=${last.t.toFixed(1)} ${last.phase}`, 4, pc.height - 4);
        } else { pg.fillStyle = "#64748b"; pg.font = "11px sans-serif"; pg.fillText("no positions in the frames", 10, 20); }
        // end card
        const e = M.end;
        el.querySelector(".sim-end").innerHTML = (M.errors.length ? M.errors.map((x) => `<div class="bad">${esc(x)}</div>`).join("") : "")
            + (e ? `<div class="sim-card"><b>${esc(e.winner ?? "no winner")}</b> <span class="muted">${esc(e.reason || "")}</span><div>rounds ${esc(e.rounds ?? "-")} · ${esc(e.result || "")} · length ${esc(M.t1.toFixed(1))} s</div>${e.perf ? `<div class="muted">logic ns per tick: avg ${esc(e.perf.logicNs ? e.perf.logicNs.avg : e.perf.avg)} · p99 ${esc(e.perf.logicNs ? e.perf.logicNs.p99 : e.perf.p99)} · max ${esc(e.perf.logicNs ? e.perf.logicNs.max : e.perf.max)} (${esc(e.perf.ticks)} ticks)</div>` : ""}</div>`
            : `<div class="muted">${r.done ? "The run ended without an end line." : "…"}</div>`);
        el.querySelector(".sim-log").innerHTML = M.markers.slice(-300).map((x) => `<div><span class="muted">${x.t.toFixed(1)}</span> <b style="color:${MARK[x.kind] || "#cbd5e1"}">${esc(x.kind)}</b> ${esc(x.text)}</div>`).join("");
    }

    async batch() {
        const text = this.app.saveText();
        if (text == null) { this.app.app.toast("The code has an error: fix it first"); return; }
        const n = +this.root.querySelector("#smN").value;
        const O = this.root.querySelector("#smOut");
        O.innerHTML = `<div class="muted">Running modebatch: ${n} seeds for this file${this.root.querySelector("#smCompare").checked ? " and the built-in" : ""}…</div>`;
        const one = async (rules) => {
            const req = { ...this._req(rules), seeds: n, firstSeed: +this.root.querySelector("#smSeed").value };
            const r = await api.post("/api/helper", { args: ["modebatch"], request: req, timeout: 3600 }, "json");
            return r;
        };
        try {
            const res = [["this file", await one(text)]];
            if (this.root.querySelector("#smCompare").checked) res.push(["built-in " + this.app.base, await one(this.app.builtinText())]);
            O.innerHTML = res.map(([label, r]) => `<div class="sim-run"><h4>${esc(label)} <span class="muted">exit ${esc(r.code)}</span></h4>${r.answer && r.answer.csv ? `<button data-csv="${esc(label)}">⇩ CSV</button>` : ""}<pre class="mono" style="white-space:pre-wrap">${esc(JSON.stringify(r.answer, (k, v) => (k === "csv" ? "(csv)" : v), 1)).slice(0, 20000)}</pre>${r.stderr ? `<div class="bad">${esc(r.stderr)}</div>` : ""}</div>`).join("");
            O.querySelectorAll("[data-csv]").forEach((b) => (b.onclick = () => { const r = res.find((x) => x[0] === b.dataset.csv)[1]; this.app.app.download("modebatch.csv", r.answer.csv, "text/csv"); }));
        } catch (e) { O.innerHTML = `<div class="bad">${esc(e.message)}</div>`; }
    }
}
