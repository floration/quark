// Top-down map view: the map's boxes coloured by height (painter's order), ladders, spawns and objectives, with pan /
// zoom and a height slice (hide everything above Y: indoor maps, a skyscraper's floors). Overlays (placed bots, live
// bots) are drawn by callbacks each frame on top of a cached map layer.
//
// Speed: boxes are grouped into 64 m chunks x colour buckets as Path2D in world units, built once per map / slice;
// a frame fills only the chunks in view (one fill + one stroke per bucket per chunk). The map layer is redrawn only
// when the camera or slice changes; an overlay-only frame is one drawImage plus the overlay. No allocation per frame.

import { MATERIALS } from "./qmap.js";

const CHUNK = 64;          // m
const BANDS = 20;          // height colour bands
const RAMP = [[22, 34, 48], [26, 64, 84], [30, 104, 104], [70, 140, 92], [150, 160, 84], [214, 186, 112], [236, 222, 180]];
export const TEAM_COLORS = { a: "#f05252", b: "#3f83f8", any: "#9ca3af", race: "#a78bfa", none: "#e5e7eb" };

function rampColor(t) {
    t = Math.max(0, Math.min(1, t)) * (RAMP.length - 1);
    const i = Math.min(RAMP.length - 2, Math.floor(t)), f = t - i, a = RAMP[i], b = RAMP[i + 1];
    return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];
}
const rgb = (c, k = 1) => `rgb(${Math.round(c[0] * k)},${Math.round(c[1] * k)},${Math.round(c[2] * k)})`;

export class MapView {
    constructor(canvas, opts = {}) {
        this.canvas = canvas;
        this.ctx = canvas.getContext("2d", { alpha: false });
        this.layer = document.createElement("canvas");
        this.lctx = this.layer.getContext("2d", { alpha: false });
        this.map = null;
        this.cam = { x: 0, z: 0, s: 4 };         // world centre, pixels per metre (CSS px)
        this.slice = Infinity;                    // hide boxes whose bottom is at or above this
        this.show = { clip: false, trim: true, far: false, grid: true, spawns: true, objectives: true, ladders: true, labels: true };
        this.overlays = [];                       // fn(ctx, view) drawn in world space each frame
        this.screenOverlays = [];                 // fn(ctx, view) drawn in CSS-pixel space each frame
        this.dirtyLayer = true;
        this.dirty = true;
        this.dpr = window.devicePixelRatio || 1;
        this.w = 1; this.h = 1;
        this.handlers = opts.handlers || {};      // {down(world, ev) -> bool, move(world, ev), up(world, ev), hover(world, ev), dbl(world, ev)}
        this.frameMs = 0;                          // last frame's draw time (shown in the status bar)
        this._drag = null;
        this._raf = 0;
        this._installInput();
        this._ro = new ResizeObserver(() => this.resize());
        this._ro.observe(canvas.parentElement || canvas);
        this.resize();
        this._loop = this._loop.bind(this);
        this._raf = requestAnimationFrame(this._loop);
    }

    destroy() { cancelAnimationFrame(this._raf); this._ro.disconnect(); }

    setMap(map) {
        this.map = map;
        this.slice = Infinity;
        this._build();
        this.fit();
    }

    setSlice(y) { this.slice = y; this._build(); this.invalidate(true); }

    fit() {
        if (!this.map) return;
        const b = this.map.bounds;
        this.cam.x = (b.x0 + b.x1) / 2; this.cam.z = (b.z0 + b.z1) / 2;
        this.cam.s = Math.max(0.2, Math.min(60, 0.92 * Math.min(this.w / Math.max(1, b.x1 - b.x0), this.h / Math.max(1, b.z1 - b.z0))));
        this.invalidate(true);
    }

    invalidate(layer = false) { this.dirty = true; if (layer) this.dirtyLayer = true; }

    resize() {
        const r = this.canvas.getBoundingClientRect();
        this.dpr = window.devicePixelRatio || 1;
        this.w = Math.max(1, r.width); this.h = Math.max(1, r.height);
        const W = Math.round(this.w * this.dpr), H = Math.round(this.h * this.dpr);
        if (this.canvas.width !== W || this.canvas.height !== H) {
            this.canvas.width = W; this.canvas.height = H;
            this.layer.width = W; this.layer.height = H;
        }
        this.invalidate(true);
    }

    toWorld(px, py) { return { x: (px - this.w / 2) / this.cam.s + this.cam.x, z: (py - this.h / 2) / this.cam.s + this.cam.z }; }
    toScreen(x, z) { return { x: (x - this.cam.x) * this.cam.s + this.w / 2, y: (z - this.cam.z) * this.cam.s + this.h / 2 }; }

    // ---- build the chunked paths
    _build() {
        const m = this.map;
        this.chunks = new Map();
        if (!m) return;
        const B = m.boxes, n = m.nBoxes, y0 = m.bounds.y0, y1 = Math.max(m.bounds.y0 + 1, Math.min(m.bounds.y1, this.slice));
        // painter's order: by top height, then base materials before decoration
        const order = new Uint32Array(n);
        for (let i = 0; i < n; i++) order[i] = i;
        order.sort((a, b) => B[a * 6 + 4] - B[b * 6 + 4] || m.mats[a] - m.mats[b]);
        this.bucketStyle = [];
        const styleKey = new Map();
        const styleOf = (mat, band) => {
            const k = mat * 64 + band;
            let s = styleKey.get(k);
            if (s !== undefined) return s;
            const c = rampColor(band / (BANDS - 1));
            let fill = rgb(c), stroke = rgb(c, 0.55), alpha = 1;
            const name = MATERIALS[mat];
            if (name === "lava") { fill = "#b4401a"; stroke = "#f97316"; }
            else if (name === "water") { fill = "#1e4f8f"; stroke = "#3b82c4"; alpha = 0.75; }
            else if (name === "wallbang") { fill = "#3b3f46"; stroke = "#6b7280"; }
            else if (name === "glow") { stroke = "#fde68a"; }
            else if (name === "metal") { fill = rgb(c, 0.85); stroke = "#94a3b8"; }
            else if (name === "accent") { fill = rgb(c, 0.92); stroke = rgb(c, 0.4); }
            else if (name === "trim") { alpha = 0.55; }
            else if (name === "clip") { fill = "rgba(168,85,247,0.08)"; stroke = "rgba(168,85,247,0.6)"; }
            else if (name === "far") { fill = "#151b22"; stroke = "#1f2630"; }
            s = this.bucketStyle.length;
            this.bucketStyle.push({ fill, stroke, alpha, mat });
            styleKey.set(k, s);
            return s;
        };
        // one sequence of (style, Path2D) per chunk, in painter's order: consecutive boxes of the same style share a path
        for (let j = 0; j < n; j++) {
            const i = order[j], o = i * 6, mat = m.mats[i];
            if (B[o + 1] >= this.slice) continue;
            const top = Math.min(B[o + 4], this.slice);
            const band = Math.round(((top - y0) / (y1 - y0)) * (BANDS - 1));
            const st = styleOf(mat, Math.max(0, Math.min(BANDS - 1, band)));
            const cx = Math.floor(((B[o] + B[o + 3]) / 2) / CHUNK), cz = Math.floor(((B[o + 2] + B[o + 5]) / 2) / CHUNK);
            const key = cx * 100003 + cz;
            let ch = this.chunks.get(key);
            if (!ch) { ch = { x0: 1e9, z0: 1e9, x1: -1e9, z1: -1e9, runs: [] }; this.chunks.set(key, ch); }
            ch.x0 = Math.min(ch.x0, B[o]); ch.z0 = Math.min(ch.z0, B[o + 2]); ch.x1 = Math.max(ch.x1, B[o + 3]); ch.z1 = Math.max(ch.z1, B[o + 5]);
            let run = ch.runs[ch.runs.length - 1];
            if (!run || run.style !== st) { run = { style: st, path: new Path2D(), count: 0 }; ch.runs.push(run); }
            run.path.rect(B[o], B[o + 2], B[o + 3] - B[o], B[o + 5] - B[o + 2]);
            run.count++;
        }
        this.chunkList = [...this.chunks.values()];
        // ladders and markers
        this.ladderPath = new Path2D();
        for (const l of m.ladders) if (l.y0 < this.slice) this.ladderPath.rect(l.x0, l.z0, l.x1 - l.x0, l.z1 - l.z0);
    }

    // ---- frame
    _loop() {
        this._raf = requestAnimationFrame(this._loop);
        if (this.animating) this.dirty = true;
        if (!this.dirty) return;
        const t0 = performance.now();
        this.dirty = false;
        if (this.dirtyLayer) { this._drawLayer(); this.dirtyLayer = false; }
        const ctx = this.ctx;
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.drawImage(this.layer, 0, 0);
        this._worldTransform(ctx);
        for (const f of this.overlays) f(ctx, this);
        ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
        for (const f of this.screenOverlays) f(ctx, this);
        this.frameMs = performance.now() - t0;
    }

    _worldTransform(ctx) {
        const s = this.cam.s * this.dpr;
        ctx.setTransform(s, 0, 0, s, (this.w / 2 - this.cam.x * this.cam.s) * this.dpr, (this.h / 2 - this.cam.z * this.cam.s) * this.dpr);
    }

    _drawLayer() {
        const ctx = this.lctx, m = this.map;
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.fillStyle = "#0b0f14";
        ctx.fillRect(0, 0, this.layer.width, this.layer.height);
        if (!m) return;
        const v0 = this.toWorld(0, 0), v1 = this.toWorld(this.w, this.h);
        this._worldTransform(ctx);
        const px = 1 / this.cam.s;
        if (this.show.grid) this._grid(ctx, v0, v1, px);
        ctx.lineJoin = "miter";
        const thin = this.cam.s < 1.5;
        for (const ch of this.chunkList) {
            if (ch.x1 < v0.x || ch.x0 > v1.x || ch.z1 < v0.z || ch.z0 > v1.z) continue;
            for (const run of ch.runs) {
                const st = this.bucketStyle[run.style];
                const name = MATERIALS[st.mat];
                if ((name === "clip" && !this.show.clip) || (name === "trim" && !this.show.trim) || (name === "far" && !this.show.far)) continue;
                ctx.globalAlpha = st.alpha;
                ctx.fillStyle = st.fill;
                ctx.fill(run.path);
                if (!thin || name === "lava") {
                    ctx.strokeStyle = st.stroke;
                    ctx.lineWidth = px;
                    ctx.stroke(run.path);
                }
            }
        }
        ctx.globalAlpha = 1;
        if (this.show.ladders && m.ladders.length) {
            ctx.strokeStyle = "#4ade80"; ctx.lineWidth = 2 * px;
            ctx.fillStyle = "rgba(74,222,128,0.25)";
            ctx.fill(this.ladderPath); ctx.stroke(this.ladderPath);
        }
        if (this.show.objectives) this._objectives(ctx, px);
        if (this.show.spawns) this._spawns(ctx, px);
    }

    _grid(ctx, v0, v1, px) {
        let step = 1;
        while (step * this.cam.s < 24) step *= step === 1 ? 5 : 2;   // 1, 5, 10, 20, 40...
        ctx.lineWidth = px;
        ctx.strokeStyle = "#141b24";
        ctx.beginPath();
        for (let x = Math.floor(v0.x / step) * step; x <= v1.x; x += step) { ctx.moveTo(x, v0.z); ctx.lineTo(x, v1.z); }
        for (let z = Math.floor(v0.z / step) * step; z <= v1.z; z += step) { ctx.moveTo(v0.x, z); ctx.lineTo(v1.x, z); }
        ctx.stroke();
        ctx.strokeStyle = "#233040";
        ctx.beginPath(); ctx.moveTo(0, v0.z); ctx.lineTo(0, v1.z); ctx.moveTo(v0.x, 0); ctx.lineTo(v1.x, 0); ctx.stroke();
    }

    _label(ctx, text, x, z, color, px) {
        if (!this.show.labels) return;
        ctx.save();
        ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
        const p = this.toScreen(x, z);
        ctx.font = "600 11px system-ui, sans-serif";
        ctx.textAlign = "center"; ctx.textBaseline = "middle";
        ctx.lineWidth = 3; ctx.strokeStyle = "rgba(0,0,0,0.75)";
        ctx.strokeText(text, p.x, p.y);
        ctx.fillStyle = color; ctx.fillText(text, p.x, p.y);
        ctx.restore();
    }

    _zone(ctx, b, stroke, fill, px, label) {
        ctx.fillStyle = fill; ctx.strokeStyle = stroke; ctx.lineWidth = 2 * px;
        ctx.setLineDash([6 * px, 4 * px]);
        ctx.fillRect(b.x0, b.z0, b.x1 - b.x0, b.z1 - b.z0);
        ctx.strokeRect(b.x0, b.z0, b.x1 - b.x0, b.z1 - b.z0);
        ctx.setLineDash([]);
        if (label) this._label(ctx, label, (b.x0 + b.x1) / 2, (b.z0 + b.z1) / 2, stroke, px);
    }

    _objectives(ctx, px) {
        const m = this.map;
        m.sites.forEach((b, i) => this._zone(ctx, b, "#fb923c", "rgba(251,146,60,0.14)", px, "SITE " + "ABCDEFGH"[i % 8]));
        if (m.hill) this._zone(ctx, m.hill, "#facc15", "rgba(250,204,21,0.14)", px, "HILL");
        m.points.forEach((b, i) => this._zone(ctx, b, "#22d3ee", "rgba(34,211,238,0.12)", px, "P" + (i + 1)));
        m.checkpoints.forEach((b, i) => this._zone(ctx, b, "#c084fc", "rgba(192,132,252,0.12)", px, i === m.checkpoints.length - 1 ? "FINISH" : "CP" + (i + 1)));
        if (m.path.length > 1) {
            ctx.strokeStyle = "#e879f9"; ctx.lineWidth = 2.5 * px; ctx.setLineDash([8 * px, 5 * px]);
            ctx.beginPath(); ctx.moveTo(m.path[0].x, m.path[0].z);
            for (const p of m.path) ctx.lineTo(p.x, p.z);
            ctx.stroke(); ctx.setLineDash([]);
            this._label(ctx, "CART", m.path[0].x, m.path[0].z, "#e879f9", px);
        }
    }

    _spawns(ctx, px) {
        const r = Math.max(0.6, 7 * px);
        for (const s of this.map.spawns) {
            if (s.y >= this.slice) continue;
            const c = TEAM_COLORS[s.team] || TEAM_COLORS.any;
            const fx = -Math.sin(s.yaw), fz = -Math.cos(s.yaw);
            ctx.fillStyle = c; ctx.strokeStyle = "#000"; ctx.lineWidth = px;
            ctx.beginPath();
            ctx.moveTo(s.x + fx * r * 1.4, s.z + fz * r * 1.4);
            ctx.lineTo(s.x - fz * r * 0.8 - fx * r * 0.6, s.z + fx * r * 0.8 - fz * r * 0.6);
            ctx.lineTo(s.x + fz * r * 0.8 - fx * r * 0.6, s.z - fx * r * 0.8 - fz * r * 0.6);
            ctx.closePath(); ctx.globalAlpha = 0.8; ctx.fill(); ctx.globalAlpha = 1; ctx.stroke();
        }
    }

    // ---- input
    _installInput() {
        const c = this.canvas;
        const pos = (ev) => { const r = c.getBoundingClientRect(); return { x: ev.clientX - r.left, y: ev.clientY - r.top }; };
        c.addEventListener("wheel", (ev) => {
            ev.preventDefault();
            const p = pos(ev), before = this.toWorld(p.x, p.y);
            const k = Math.exp(-ev.deltaY * (ev.deltaMode ? 0.05 : 0.0015));
            this.cam.s = Math.max(0.15, Math.min(120, this.cam.s * k));
            const after = this.toWorld(p.x, p.y);
            this.cam.x += before.x - after.x; this.cam.z += before.z - after.z;
            this.invalidate(true);
        }, { passive: false });
        c.addEventListener("contextmenu", (ev) => ev.preventDefault());
        c.addEventListener("pointerdown", (ev) => {
            c.setPointerCapture(ev.pointerId);
            const p = pos(ev), w = this.toWorld(p.x, p.y);
            const pan = ev.button === 1 || ev.button === 2 || (ev.button === 0 && ev.altKey);
            if (!pan && this.handlers.down && this.handlers.down(w, ev)) { this._drag = { kind: "handler" }; return; }
            this._drag = { kind: "pan", sx: p.x, sy: p.y, cx: this.cam.x, cz: this.cam.z, moved: false, button: ev.button };
        });
        c.addEventListener("pointermove", (ev) => {
            const p = pos(ev), w = this.toWorld(p.x, p.y);
            this.mouse = w;
            const d = this._drag;
            if (d && d.kind === "pan") {
                if (Math.abs(p.x - d.sx) + Math.abs(p.y - d.sy) > 3) d.moved = true;
                this.cam.x = d.cx - (p.x - d.sx) / this.cam.s;
                this.cam.z = d.cz - (p.y - d.sy) / this.cam.s;
                this.invalidate(true);
            } else if (d && d.kind === "handler") { this.handlers.move && this.handlers.move(w, ev); this.invalidate(); }
            else { this.handlers.hover && this.handlers.hover(w, ev); }
            if (this.onMouse) this.onMouse(w);
        });
        const up = (ev) => {
            const d = this._drag; this._drag = null;
            const p = pos(ev), w = this.toWorld(p.x, p.y);
            if (d && d.kind === "handler") this.handlers.up && this.handlers.up(w, ev);
            else if (d && d.kind === "pan" && !d.moved && d.button === 0 && this.handlers.click) this.handlers.click(w, ev);
            this.invalidate();
        };
        c.addEventListener("pointerup", up);
        c.addEventListener("pointercancel", up);
        c.addEventListener("dblclick", (ev) => { const p = pos(ev); this.handlers.dbl && this.handlers.dbl(this.toWorld(p.x, p.y), ev); });
    }
}
