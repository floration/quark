// Path: where each run went. Top-down (x right, z down) and a side view (along the edited run's main direction, height
// up) of the test room or map with the trajectories, 0.1 s ticks and events; 3D: an orbit view of the same.
// Redrawn only when something changed (runs, camera, size): no per-frame work while idle.
import { MATERIALS } from "./qmap.js";
import { GLScene, lookAt, hexRgb } from "./gl.js";

const $ = (id) => document.getElementById(id);
const RAMP = [[30, 40, 54], [32, 70, 90], [34, 104, 104], [74, 138, 92], [150, 160, 84], [214, 186, 112], [236, 222, 180]];
const ramp = (t) => { t = Math.max(0, Math.min(1, t)) * (RAMP.length - 1); const i = Math.min(RAMP.length - 2, Math.floor(t)), f = t - i, a = RAMP[i], b = RAMP[i + 1]; return `rgb(${[0, 1, 2].map((k) => Math.round(a[k] + (b[k] - a[k]) * f)).join(",")})`; };
const EV = { jump: "#48c78e", land: "#9aa4b1", dash: "#5aa9ff", dashJump: "#3fd0c9", superDash: "#ff7a45", bounce: "#e86a8a", hook: "#b57ff0", unhook: "#b57ff0", ladder: "#d9a93a", ladderOff: "#d9a93a" };

class View2D {
    constructor(canvas, draw) {
        this.c = canvas; this.drawFn = draw; this.cam = { x: 0, y: 0, s: 8 }; this.flipY = false;
        canvas.addEventListener("pointerdown", (e) => { canvas.setPointerCapture(e.pointerId); this.drag = { x: e.clientX, y: e.clientY }; });
        canvas.addEventListener("pointermove", (e) => {
            if (!this.drag) return;
            this.cam.x -= (e.clientX - this.drag.x) / this.cam.s; this.cam.y -= (e.clientY - this.drag.y) / this.cam.s * (this.flipY ? -1 : 1);
            this.drag = { x: e.clientX, y: e.clientY }; this.request();
        });
        canvas.addEventListener("pointerup", () => (this.drag = null));
        canvas.addEventListener("wheel", (e) => {
            e.preventDefault();
            const r = canvas.getBoundingClientRect(), mx = e.clientX - r.left, my = e.clientY - r.top;
            const [wx, wy] = this.toWorld(mx, my), k = Math.exp(-e.deltaY * 0.0015);
            this.cam.s = Math.max(0.2, Math.min(400, this.cam.s * k));
            const [nx, ny] = this.toWorld(mx, my); this.cam.x += wx - nx; this.cam.y += wy - ny;
            this.request();
        }, { passive: false });
        new ResizeObserver(() => this.request()).observe(canvas);
    }
    toWorld(px, py) { const W = this.c.clientWidth, H = this.c.clientHeight; return [this.cam.x + (px - W / 2) / this.cam.s, this.cam.y + (py - H / 2) / this.cam.s * (this.flipY ? -1 : 1)]; }
    X(x) { return this.c.clientWidth / 2 + (x - this.cam.x) * this.cam.s; }
    Y(y) { return this.c.clientHeight / 2 + (y - this.cam.y) * this.cam.s * (this.flipY ? -1 : 1); }
    fit(x0, y0, x1, y1) {
        const W = this.c.clientWidth || 400, H = this.c.clientHeight || 300;
        this.cam.x = (x0 + x1) / 2; this.cam.y = (y0 + y1) / 2;
        this.cam.s = Math.max(0.2, Math.min(200, Math.min((W - 40) / Math.max(1, x1 - x0), (H - 40) / Math.max(1, y1 - y0))));
        this.request();
    }
    request() { if (this.raf) return; this.raf = requestAnimationFrame(() => { this.raf = 0; this.paint(); }); }
    paint() {
        const c = this.c, dpr = window.devicePixelRatio || 1, W = c.clientWidth, H = c.clientHeight;
        if (!W || !H) return;
        if (c.width !== Math.round(W * dpr) || c.height !== Math.round(H * dpr)) { c.width = Math.round(W * dpr); c.height = Math.round(H * dpr); }
        const g = c.getContext("2d", { alpha: false }); g.setTransform(dpr, 0, 0, dpr, 0, 0);
        this.drawFn(g, W, H, this);
    }
}

export class PathView {
    constructor(app) {
        this.app = app;
        this.top = new View2D($("topCanvas"), (g, W, H, v) => this._drawTop(g, W, H, v));
        this.side = new View2D($("sideCanvas"), (g, W, H, v) => this._drawSide(g, W, H, v));
        this.side.flipY = true;
        this.three = false;
        $("pathFit").onclick = () => this.fit();
        $("path3d").onclick = (e) => { this.three = !this.three; e.target.classList.toggle("on", this.three); $("pathViews").classList.toggle("three", this.three); this._3d(); this.redraw(); };
        $("pathEvents").onchange = $("pathTicks").onchange = () => { this.redraw(); this._strips(); };
        app.on("results", () => { this._axis(); if (this._needFit) this.fit(); this.redraw(); this._strips(); });
        app.on("map", () => { this._needFit = true; this.scene && this.scene.setMap(app.mapChoice.map); this.redraw(); });
        app.on("page", (p) => { if (p === "path") { if (this._needFit) this.fit(); this.redraw(); if (this.three) this._3d(); } });
        this._needFit = true;
    }
    runs() { return this.app.allRuns(); }
    _axis() {
        // the side view's axis: from the start toward the edited run's farthest point
        const r = this.runs()[0];
        if (!r || !r.trace.length) { this.axis = { o: [0, 0, 0], u: [1, 0] }; return; }
        const o = r.trace[0].pos; let best = 0, u = [1, 0];
        for (const p of r.trace) { const dx = p.pos[0] - o[0], dz = p.pos[2] - o[2], d = Math.hypot(dx, dz); if (d > best + 1e-6) { best = d; u = [dx / d, dz / d]; } }
        if (best < 0.5) { const yaw = (this.app.scenario.start.yaw || 0) * Math.PI / 180; u = [-Math.sin(yaw), -Math.cos(yaw)]; }
        this.axis = { o, u };
        const deg = Math.round(Math.atan2(-u[0], -u[1]) * 180 / Math.PI);
        $("sideLbl").textContent = `side: along yaw ${deg}° from the start (distance →, height ↑) · drag: pan · wheel: zoom`;
    }
    fit() {
        const runs = this.runs();
        if (!runs.length || !this.top.c.clientWidth) { this._needFit = true; return; }
        this._needFit = false;
        this._axis();
        let x0 = 1e9, z0 = 1e9, x1 = -1e9, z1 = -1e9, s0 = 1e9, s1 = -1e9, y0 = 1e9, y1 = -1e9;
        const { o, u } = this.axis;
        for (const r of runs) for (const p of r.trace) {
            x0 = Math.min(x0, p.pos[0]); x1 = Math.max(x1, p.pos[0]); z0 = Math.min(z0, p.pos[2]); z1 = Math.max(z1, p.pos[2]);
            const s = (p.pos[0] - o[0]) * u[0] + (p.pos[2] - o[2]) * u[1]; s0 = Math.min(s0, s); s1 = Math.max(s1, s);
            y0 = Math.min(y0, p.pos[1]); y1 = Math.max(y1, p.pos[1] + 1.8);
        }
        const pad = 4, grow = (a, b, min) => { const c = (a + b) / 2, h = Math.max(min / 2, (b - a) / 2 + pad); return [c - h, c + h]; };
        const [tx0, tx1] = grow(x0, x1, 24), [tz0, tz1] = grow(z0, z1, 24), [ss0, ss1] = grow(s0, s1, 20);
        this.top.fit(tx0, tz0, tx1, tz1);
        this.side.fit(ss0, y0 - 1, ss1, Math.max(y1 + 1, y0 + 6));
        if (this.orbit) { this.orbit.x = (x0 + x1) / 2; this.orbit.z = (z0 + z1) / 2; this.orbit.y = y0; this.orbit.dist = Math.max(20, Math.hypot(x1 - x0, z1 - z0) * 1.1); this.dirty3 = true; }
    }
    redraw() {
        if (this.app.page !== "path") return;
        this.top.request(); this.side.request();
        const r = this.runs()[0], stale = this.app.staleNote();
        $("pathInfo").innerHTML = (stale ? `<span class="warn">⚠ ${stale}</span>${r ? " · " : ""}` : "") + (r ? `${this.runs().length} runs · ${this.app.mapChoice.name}` : "");
        $("pathViews").classList.toggle("stale", !!stale);
    }

    _bg(g, W, H) { const css = getComputedStyle(document.body); g.fillStyle = css.getPropertyValue("--bg"); g.fillRect(0, 0, W, H); return css; }
    _grid(g, W, H, v, css) {
        const step = v.cam.s > 30 ? 1 : v.cam.s > 6 ? 5 : v.cam.s > 1.5 ? 20 : 100;
        const [ax, ay] = v.toWorld(0, 0), [bx, by] = v.toWorld(W, H);
        g.strokeStyle = css.getPropertyValue("--line"); g.lineWidth = 1; g.globalAlpha = 0.45; g.beginPath();
        for (let x = Math.floor(Math.min(ax, bx) / step) * step; x <= Math.max(ax, bx); x += step) { const px = Math.round(v.X(x)) + 0.5; g.moveTo(px, 0); g.lineTo(px, H); }
        for (let y = Math.floor(Math.min(ay, by) / step) * step; y <= Math.max(ay, by); y += step) { const py = Math.round(v.Y(y)) + 0.5; g.moveTo(0, py); g.lineTo(W, py); }
        g.stroke(); g.globalAlpha = 1;
        g.fillStyle = css.getPropertyValue("--fg3"); g.font = "10px system-ui"; g.fillText(`grid ${step} m`, 8, H - 8);
    }
    _drawTop(g, W, H, v) {
        const css = this._bg(g, W, H);
        const m = this.app.mapChoice.map;
        if (m) {
            if (!m._order) { m._order = Array.from({ length: m.nBoxes }, (_, i) => i).sort((a, b) => m.boxes[a * 6 + 4] - m.boxes[b * 6 + 4]); }
            const B = m.boxes, y0 = m.bounds.y0, y1 = Math.max(y0 + 1, m.bounds.y1);
            const [wx0, wz0] = v.toWorld(0, 0), [wx1, wz1] = v.toWorld(W, H);
            for (const i of m._order) {
                const o = i * 6, mt = MATERIALS[m.mats[i]];
                if (mt === "clip" || mt === "far") continue;
                if (B[o + 3] < wx0 || B[o] > wx1 || B[o + 5] < wz0 || B[o + 2] > wz1) continue;
                g.fillStyle = mt === "water" ? "#1f4f8f" : mt === "lava" ? "#cc4514" : ramp((B[o + 4] - y0) / (y1 - y0));
                const x = v.X(B[o]), z = v.Y(B[o + 2]), w = (B[o + 3] - B[o]) * v.cam.s, h = (B[o + 5] - B[o + 2]) * v.cam.s;
                g.fillRect(x, z, w, h);
                if (w > 3 && h > 3) { g.strokeStyle = "rgba(0,0,0,.35)"; g.strokeRect(x + 0.5, z + 0.5, w - 1, h - 1); }
            }
            g.fillStyle = "#3fbf6f";
            for (const l of m.ladders || []) g.fillRect(v.X(l.x0), v.Y(l.z0), Math.max(2, (l.x1 - l.x0) * v.cam.s), Math.max(2, (l.z1 - l.z0) * v.cam.s));
        }
        this._grid(g, W, H, v, css);
        this._runs(g, v, (p) => [p.pos[0], p.pos[2]], css, true);
    }
    _drawSide(g, W, H, v) {
        const css = this._bg(g, W, H);
        const m = this.app.mapChoice.map;
        if (!this.axis) this._axis();
        const { o, u } = this.axis;
        if (m) {
            // boxes the vertical plane through the axis cuts: the interval of s inside each box's footprint
            const B = m.boxes, y0 = m.bounds.y0, y1 = Math.max(y0 + 1, m.bounds.y1);
            const [s0w] = v.toWorld(0, 0), [s1w] = v.toWorld(W, 0);
            for (let i = 0; i < m.nBoxes; i++) {
                const k = i * 6, mt = MATERIALS[m.mats[i]];
                if (mt === "clip" || mt === "far") continue;
                let a = s0w - 1, b = s1w + 1, ok = true;
                for (const [ax, d, lo, hi] of [[o[0], u[0], B[k], B[k + 3]], [o[2], u[1], B[k + 2], B[k + 5]]]) {
                    if (Math.abs(d) < 1e-9) { if (ax < lo || ax > hi) { ok = false; break; } continue; }
                    let t0 = (lo - ax) / d, t1 = (hi - ax) / d; if (t0 > t1) [t0, t1] = [t1, t0];
                    a = Math.max(a, t0); b = Math.min(b, t1); if (a > b) { ok = false; break; }
                }
                if (!ok) continue;
                g.fillStyle = ramp((B[k + 4] - y0) / (y1 - y0));
                g.fillRect(v.X(a), v.Y(B[k + 4]), (b - a) * v.cam.s, (B[k + 4] - B[k + 1]) * v.cam.s);
            }
        }
        this._grid(g, W, H, v, css);
        this._runs(g, v, (p) => [(p.pos[0] - o[0]) * u[0] + (p.pos[2] - o[2]) * u[1], p.pos[1]], css, false);
    }
    _runs(g, v, proj, css, top) {
        const runs = this.runs();
        const ticks = $("pathTicks").checked, evs = $("pathEvents").checked;
        for (let ri = runs.length - 1; ri >= 0; ri--) {
            const r = runs[ri]; if (!r.trace.length) continue;
            g.strokeStyle = r.color; g.lineWidth = ri === 0 ? 2.2 : 1.6; g.setLineDash(r.drive ? [1, 3] : ri === 1 ? [6, 4] : []);
            g.beginPath();
            r.trace.forEach((p, i) => { const [a, b] = proj(p); i ? g.lineTo(v.X(a), v.Y(b)) : g.moveTo(v.X(a), v.Y(b)); });
            g.stroke(); g.setLineDash([]);
            if (ticks && v.cam.s > 2) {
                g.fillStyle = r.color; let next = 0;
                for (const p of r.trace) if (p.t >= next - 1e-6) { const [a, b] = proj(p); g.fillRect(v.X(a) - 1.5, v.Y(b) - 1.5, 3, 3); next = p.t + 0.1; }
            }
            if (evs && ri === 0) {
                g.font = "10px system-ui";
                for (const e of r.events) {
                    if (!EV[e.kind] || !e.pos) continue;
                    const [a, b] = proj({ pos: e.pos }); g.strokeStyle = EV[e.kind]; g.lineWidth = 1.5;
                    g.beginPath(); g.arc(v.X(a), v.Y(b), 4, 0, 7); g.stroke();
                    if (v.cam.s > 4) { g.fillStyle = EV[e.kind]; g.fillText(e.tech || e.kind, v.X(a) + 6, v.Y(b) - 6); }
                }
            }
        }
        // the start: a dot and the facing
        const r0 = runs[0];
        if (r0 && r0.trace.length) {
            const p = r0.trace[0], [a, b] = proj(p);
            g.fillStyle = css.getPropertyValue("--fg"); g.beginPath(); g.arc(v.X(a), v.Y(b), 4, 0, 7); g.fill();
            if (top) {
                const yaw = (this.app.scenario.start.yaw || 0) * Math.PI / 180, fx = -Math.sin(yaw), fz = -Math.cos(yaw);
                g.strokeStyle = g.fillStyle; g.lineWidth = 2; g.beginPath(); g.moveTo(v.X(a), v.Y(b)); g.lineTo(v.X(a) + fx * 18, v.Y(b) + fz * 18); g.stroke();
            }
        }
    }

    // ---- 3D (orbit)
    _3d() {
        if (!this.three) { this.on3 = false; return; }
        if (!this.scene) {
            const c = $("p3dCanvas");
            this.scene = new GLScene(c);
            this.scene.setMap(this.app.mapChoice.map);
            this.orbit = { yaw: 0.8, pitch: 0.6, dist: 60, x: 0, y: 0, z: 0 };
            this.fit();
            c.addEventListener("contextmenu", (e) => e.preventDefault());
            c.addEventListener("pointerdown", (e) => { c.setPointerCapture(e.pointerId); this.od = { b: e.button, x: e.clientX, y: e.clientY }; });
            c.addEventListener("pointermove", (e) => {
                const d = this.od; if (!d) return;
                const dx = e.clientX - d.x, dy = e.clientY - d.y; d.x = e.clientX; d.y = e.clientY; const O = this.orbit;
                if (d.b === 0 && !e.altKey) { O.yaw -= dx * 0.006; O.pitch = Math.max(-0.2, Math.min(1.55, O.pitch + dy * 0.006)); }
                else { const k = O.dist * 0.0018, cy = Math.cos(O.yaw), sy = Math.sin(O.yaw); O.x -= (dx * cy - dy * sy) * k; O.z -= (-dx * sy - dy * cy) * k; }
                this.dirty3 = true;
            });
            c.addEventListener("pointerup", () => (this.od = null));
            c.addEventListener("wheel", (e) => { e.preventDefault(); this.orbit.dist = Math.max(3, Math.min(2000, this.orbit.dist * Math.exp(e.deltaY * 0.0012))); this.dirty3 = true; }, { passive: false });
            this._strips();
        }
        this.on3 = true; this.dirty3 = true;
        const loop = () => {
            if (!this.on3 || this.app.page !== "path") { this.on3 = false; return; }
            requestAnimationFrame(loop);
            if (this.scene.resize()) this.dirty3 = true;
            if (!this.dirty3) return;
            this.dirty3 = false;
            const O = this.orbit, eye = [O.x + Math.cos(O.pitch) * Math.sin(O.yaw) * O.dist, O.y + Math.sin(O.pitch) * O.dist, O.z + Math.cos(O.pitch) * Math.cos(O.yaw) * O.dist];
            this.scene.render(eye, lookAt(eye, [O.x, O.y, O.z]), 0.9, { grid: 1, fogFar: Math.max(400, O.dist * 4), lineWidth: 3 });
        };
        requestAnimationFrame(loop);
    }
    _strips() {
        if (!this.scene) return;
        // (the edited set's run last: drawn on top where the runs overlap, so it keeps its colour)
        this.scene.setStrips(this.runs().map((r) => ({ pts: r.trace.map((p) => [p.pos[0], p.pos[1] + 0.9, p.pos[2]]), color: hexRgb(r.color) })).reverse());
        this.scene.clearDyn();
        const r = this.runs()[0];
        if (r && r.trace.length) { const p = r.trace[0].pos; this.scene.addBox(p[0] - 0.4, p[1], p[2] - 0.4, p[0] + 0.4, p[1] + 1.8, p[2] + 0.4, [0.9, 0.9, 0.95]); }
        this.dirty3 = true;
    }
}
