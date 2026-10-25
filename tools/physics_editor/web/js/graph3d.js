// Movement graph › 3D: the runs' trajectories over the map (the Path / Drive WebGL renderer, gl.js): an orbit camera
// (drag: orbit, right drag or Alt: pan, wheel: zoom), a trail per set in its colour, 0.1 s ticks, events as coloured
// posts, and a scrubber (or ▶) that puts each run's player box where it was at that time. A / B side by side: two
// views with one camera. Rendered only when something changed (or while playing).
import { GLScene, lookAt, hexRgb } from "./gl.js";
import { EVENT_COL, QUIET, sampleAt } from "./graph.js";

const $ = (id) => document.getElementById(id);

export class Graph3D {
    constructor(app, graph) {
        this.app = app; this.graph = graph;
        this.cA = $("g3dA"); this.cB = $("g3dB");
        this.orbit = { yaw: 0.8, pitch: 0.55, dist: 40, x: 0, y: 0, z: 0 };
        this.t = null; this.playing = false;
        for (const c of [this.cA, this.cB]) this._wire(c);
        $("g3dT").oninput = () => { this.playing = false; $("g3dPlay").textContent = "▶"; this.t = +$("g3dT").value / 1000 * this.tMax; this._boxes(); };
        $("g3dPlay").onclick = () => {
            this.playing = !this.playing; $("g3dPlay").textContent = this.playing ? "❚❚" : "▶";
            if (this.playing && (this.t == null || this.t >= this.tMax - 1e-3)) this.t = 0;
            this.last = performance.now(); this.dirty = true;
        };
        $("g3dTicks").onchange = $("g3dEvents").onchange = () => this._strips();
        $("g3dFit").onclick = () => this.fit(true);
    }
    _wire(c) {
        c.addEventListener("contextmenu", (e) => e.preventDefault());
        c.addEventListener("pointerdown", (e) => { c.setPointerCapture(e.pointerId); this.od = { b: e.button, x: e.clientX, y: e.clientY }; });
        c.addEventListener("pointermove", (e) => {
            const d = this.od; if (!d) return;
            const dx = e.clientX - d.x, dy = e.clientY - d.y; d.x = e.clientX; d.y = e.clientY; const O = this.orbit;
            if (d.b === 0 && !e.altKey) { O.yaw -= dx * 0.006; O.pitch = Math.max(-0.2, Math.min(1.55, O.pitch + dy * 0.006)); }
            else { const k = O.dist * 0.0018, cy = Math.cos(O.yaw), sy = Math.sin(O.yaw); O.x -= (dx * cy - dy * sy) * k; O.z -= (-dx * sy - dy * cy) * k; }
            this.dirty = true; this._camLock();
        });
        c.addEventListener("pointerup", () => (this.od = null));
        c.addEventListener("wheel", (e) => { e.preventDefault(); this.orbit.dist = Math.max(2, Math.min(2000, this.orbit.dist * Math.exp(e.deltaY * 0.0012))); this.dirty = true; this._camLock(); }, { passive: false });
    }
    camera() { return { ...this.orbit }; }
    _camLock() { const l = this.graph.locks.cam3d; if (l && l.on) { l.cam = this.camera(); try { localStorage.setItem("qpe.locks", JSON.stringify(this.graph.locks)); } catch {} } }
    side() { return this.graph.cmp === "ab" && this.graph.ab.layout === "side" && this.graph.runs().length === 2; }
    enter() {
        if (!this.sA) {
            this.sA = new GLScene(this.cA);
            if (!this.sA.gl) { $("g3dLblA").textContent = "WebGL isn't available in this browser"; return; }
            this.sB = new GLScene(this.cB);
            this.setMap();
        }
        const side = this.side();
        $("g3d").classList.toggle("side", side);
        this.cB.style.display = side ? "" : "none"; $("g3dLblB").style.display = side ? "" : "none";
        const runs = this.graph.runs();
        const atEnd = this.t == null || this.tMax == null || this.t >= this.tMax - 1e-6;
        this.tMax = Math.max(0.1, ...runs.map((r) => (r.trace.length ? r.trace[r.trace.length - 1].t : 0)));
        if (atEnd || this.t > this.tMax) this.t = this.tMax;   // at the end stays at the end when a re-run is longer
        const l = this.graph.locks.cam3d;
        if (l && l.on && l.cam) Object.assign(this.orbit, l.cam);
        else {
            // refit only for a new scenario / map (a re-run after a value change keeps where you orbited to)
            const key = `${this.app.presetId}|${this.app.mapChoice && this.app.mapChoice.name}|${JSON.stringify(this.app.scenario || null)}`;
            if (key !== this.fitKey && runs.some((r) => r.trace.length)) { this.fitKey = key; this.fit(); }
        }
        this._strips();
        this.on = true; this.dirty = true;
        if (!this.loopOn) { this.loopOn = true; requestAnimationFrame(this._loop.bind(this)); }
    }
    leave() { this.on = false; this.playing = false; $("g3dPlay").textContent = "▶"; }
    setMap() { const m = this.app.mapChoice && this.app.mapChoice.map; if (this.sA && m) { this.sA.setMap(m); this.sB.setMap(m); this.dirty = true; } }
    fit(force) {
        const l = this.graph.locks.cam3d;
        if (l && l.on && l.cam && !force) return;
        const runs = this.graph.runs();
        let x0 = 1e9, z0 = 1e9, x1 = -1e9, z1 = -1e9, y0 = 1e9;
        for (const r of runs) for (const p of r.trace) { x0 = Math.min(x0, p.pos[0]); x1 = Math.max(x1, p.pos[0]); z0 = Math.min(z0, p.pos[2]); z1 = Math.max(z1, p.pos[2]); y0 = Math.min(y0, p.pos[1]); }
        if (x0 > x1) return;
        const O = this.orbit;
        O.x = (x0 + x1) / 2; O.z = (z0 + z1) / 2; O.y = y0;
        O.dist = Math.max(12, Math.hypot(x1 - x0, z1 - z0) * 1.15);
        if (force && l && l.on) this._camLock();
        this.dirty = true;
    }
    _strips() {
        if (!this.sA || !this.sA.gl) return;
        const runs = this.graph.runs(), side = this.side();
        const ticks = $("g3dTicks").checked, evs = $("g3dEvents").checked;
        const build = (list) => {
            const strips = [];
            for (const r of list) {
                const col = hexRgb(r.color);
                strips.push({ pts: r.trace.map((p) => [p.pos[0], p.pos[1] + 0.9, p.pos[2]]), color: col });
                if (ticks) {
                    let next = 0;
                    for (const p of r.trace) if (p.t >= next - 1e-6) {
                        const [x, y, z] = p.pos, w = 0.18;
                        strips.push({ pts: [[x - w, y + 0.9, z], [x + w, y + 0.9, z]], color: col }, { pts: [[x, y + 0.9, z - w], [x, y + 0.9, z + w]], color: col }, { pts: [[x, y + 0.6, z], [x, y + 1.2, z]], color: col });
                        next = p.t + 0.1;
                    }
                }
                if (evs) for (const e of r.events) {
                    if (QUIET.has(e.kind) || !e.pos) continue;
                    strips.push({ pts: [[e.pos[0], e.pos[1], e.pos[2]], [e.pos[0], e.pos[1] + 2.4, e.pos[2]]], color: hexRgb(EVENT_COL[e.kind] || "#9aa4b1") });
                }
            }
            return strips;
        };
        if (side) { this.sA.setStrips(build([runs[0]])); this.sB.setStrips(build([runs[1]])); }
        else this.sA.setStrips(build(runs));
        $("g3dLblA").textContent = side ? runs[0].label : `${runs.length} run${runs.length === 1 ? "" : "s"} · drag: orbit · right drag: pan · wheel: zoom`;
        $("g3dLblB").textContent = side ? runs[1].label : "";
        this._boxes();
    }
    // each run's player box at the scrubber's time, and the readout
    _boxes() {
        if (!this.sA || !this.sA.gl) return;
        const runs = this.graph.runs(), side = this.side(), t = this.t ?? this.tMax;
        this.sA.clearDyn(); this.sB.clearDyn();
        const parts = [];
        runs.forEach((r, i) => {
            const p = sampleAt(r.trace, t);
            if (!p) return;
            const c = hexRgb(r.color).map((x) => Math.min(1, x * 1.15));
            (side && i === 1 ? this.sB : this.sA).addBox(p.pos[0] - 0.4, p.pos[1], p.pos[2] - 0.4, p.pos[0] + 0.4, p.pos[1] + 1.8, p.pos[2] + 0.4, c);
            parts.push(`<span style="color:${r.color}">${r.label.slice(0, 22)} ${(+p.hspeed).toFixed(1)} m/s · ${(+p.pos[1]).toFixed(2)} m</span>`);
        });
        $("g3dT").value = Math.round(t / this.tMax * 1000);
        $("g3dTime").innerHTML = `t = ${t.toFixed(2)} s · ${parts.join(" · ")}`;
        this.dirty = true;
    }
    _loop(now) {
        if (!this.on || this.app.page !== "graph" || this.graph.view !== "3d") { this.loopOn = false; return; }
        requestAnimationFrame(this._loop.bind(this));
        if (this.playing) {
            const dt = Math.min(0.1, (now - (this.last || now)) / 1000); this.last = now;
            this.t = Math.min(this.tMax, (this.t || 0) + dt);
            if (this.t >= this.tMax) { this.playing = false; $("g3dPlay").textContent = "▶"; }
            this._boxes();
        }
        const side = this.side();
        if (this.sA.resize()) this.dirty = true;
        if (side && this.sB.resize()) this.dirty = true;
        if (!this.dirty) return;
        this.dirty = false;
        const O = this.orbit, eye = [O.x + Math.cos(O.pitch) * Math.sin(O.yaw) * O.dist, O.y + Math.sin(O.pitch) * O.dist, O.z + Math.cos(O.pitch) * Math.cos(O.yaw) * O.dist];
        const view = lookAt(eye, [O.x, O.y, O.z]), opt = { grid: 1, fogFar: Math.max(400, O.dist * 4), lineWidth: 2 };
        this.sA.render(eye, view, 0.9, opt);
        if (side) this.sB.render(eye, view, 0.9, opt);
    }
}
