// A simple 3D view (WebGL 1): the map's boxes with baked face shading, coloured by height like the 2D view, and the
// bots (placed, or live) as columns. Orbit: left drag, pan: right / middle drag, zoom: wheel. One static vertex buffer
// per map / slice; the bots' buffer is preallocated and refilled each frame (no allocation per frame).

import { MATERIALS, TEAM_MODES } from "./qmap.js";
import { botColor } from "./mapview.js";

const VS = `attribute vec3 p; attribute vec3 c; uniform mat4 m; varying vec3 vc; varying float vd;
void main(){ gl_Position = m * vec4(p,1.0); vc = c; vd = gl_Position.w; }`;
const FS = `precision mediump float; varying vec3 vc; varying float vd; uniform vec3 fog;
void main(){ float f = clamp((vd - 60.0) / 400.0, 0.0, 0.6); gl_FragColor = vec4(mix(vc, fog, f), 1.0); }`;
const RAMP = [[22, 34, 48], [26, 64, 84], [30, 104, 104], [70, 140, 92], [150, 160, 84], [214, 186, 112], [236, 222, 180]];
function ramp(t) { t = Math.max(0, Math.min(1, t)) * (RAMP.length - 1); const i = Math.min(RAMP.length - 2, Math.floor(t)), f = t - i, a = RAMP[i], b = RAMP[i + 1]; return [0, 1, 2].map((k) => (a[k] + (b[k] - a[k]) * f) / 255); }
const hex = (h) => [parseInt(h.slice(1, 3), 16) / 255, parseInt(h.slice(3, 5), 16) / 255, parseInt(h.slice(5, 7), 16) / 255];
const MAXBOTS = 64;

export class Map3D {
    constructor(canvas, sim) {
        this.c = canvas; this.sim = sim;
        const gl = canvas.getContext("webgl", { antialias: true });
        this.gl = gl;
        if (!gl) return;
        const sh = (t, s) => { const x = gl.createShader(t); gl.shaderSource(x, s); gl.compileShader(x); return x; };
        const pr = gl.createProgram();
        gl.attachShader(pr, sh(gl.VERTEX_SHADER, VS)); gl.attachShader(pr, sh(gl.FRAGMENT_SHADER, FS)); gl.linkProgram(pr);
        this.pr = pr; this.aP = gl.getAttribLocation(pr, "p"); this.aC = gl.getAttribLocation(pr, "c"); this.uM = gl.getUniformLocation(pr, "m"); this.uF = gl.getUniformLocation(pr, "fog");
        this.buf = gl.createBuffer(); this.n = 0;
        this.botBuf = gl.createBuffer(); this.botData = new Float32Array(MAXBOTS * 36 * 6 * 2);
        this.cam = { yaw: 0.7, pitch: 0.75, dist: 80, x: 0, y: 0, z: 0 };
        this.slice = Infinity;
        this.M = new Float32Array(16);
        this._loop = this._loop.bind(this);
        this._input();
    }
    setMap(m) { this.dirty = true; this.map = m; const b = m.bounds; this.cam.x = (b.x0 + b.x1) / 2; this.cam.z = (b.z0 + b.z1) / 2; this.cam.y = b.y0; this.cam.dist = Math.max(30, Math.hypot(b.x1 - b.x0, b.z1 - b.z0) * 0.8); this._build(); }
    setSlice(y) { this.slice = y; this._build(); this.dirty = true; }
    start() { this.on = true; this.dirty = true; cancelAnimationFrame(this.raf); this.raf = requestAnimationFrame(this._loop); }
    stop() { this.on = false; cancelAnimationFrame(this.raf); }

    _build() {
        const gl = this.gl, m = this.map;
        if (!gl || !m) return;
        const B = m.boxes, y0 = m.bounds.y0, y1 = Math.max(y0 + 1, Math.min(m.bounds.y1, this.slice));
        let count = 0;
        for (let i = 0; i < m.nBoxes; i++) { const mt = MATERIALS[m.mats[i]]; if (mt === "clip" || mt === "far" || B[i * 6 + 1] >= this.slice) continue; count++; }
        const d = new Float32Array(count * 36 * 6);
        let o = 0;
        for (let i = 0; i < m.nBoxes; i++) {
            const mt = MATERIALS[m.mats[i]], k = i * 6;
            if (mt === "clip" || mt === "far" || B[k + 1] >= this.slice) continue;
            let col = ramp((Math.min(B[k + 4], this.slice) - y0) / (y1 - y0));
            if (mt === "lava") col = [0.8, 0.27, 0.08]; else if (mt === "water") col = [0.12, 0.31, 0.56]; else if (mt === "wallbang") col = [0.24, 0.25, 0.28]; else if (mt === "glow") col = [0.98, 0.85, 0.5];
            o = box(d, o, B[k], B[k + 1], B[k + 2], B[k + 3], Math.min(B[k + 4], this.slice), B[k + 5], col);
        }
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buf);
        gl.bufferData(gl.ARRAY_BUFFER, d, gl.STATIC_DRAW);
        this.n = o / 6;
    }

    _matrix() {
        const c = this.cam, w = this.c.clientWidth, h = this.c.clientHeight;
        const ex = c.x + Math.cos(c.pitch) * Math.sin(c.yaw) * c.dist, ey = c.y + Math.sin(c.pitch) * c.dist, ez = c.z + Math.cos(c.pitch) * Math.cos(c.yaw) * c.dist;
        // view (lookAt) then perspective
        let fx = c.x - ex, fy = c.y - ey, fz = c.z - ez; const fl = Math.hypot(fx, fy, fz); fx /= fl; fy /= fl; fz /= fl;
        let sx = fy * 0 - fz * 1, sy = fz * 0 - fx * 0, sz = fx * 1 - fy * 0; const sl = Math.hypot(sx, sy, sz) || 1; sx /= sl; sy /= sl; sz /= sl;
        const ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
        const f = 1 / Math.tan(0.5 * 1.0), a = w / Math.max(1, h), n = 0.3, far = 2000;
        const V = [sx, ux, -fx, 0, sy, uy, -fy, 0, sz, uz, -fz, 0, -(sx * ex + sy * ey + sz * ez), -(ux * ex + uy * ey + uz * ez), fx * ex + fy * ey + fz * ez, 1];
        const P = [f / a, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + n) / (n - far), -1, 0, 0, (2 * far * n) / (n - far), 0];
        const M = this.M;
        for (let i = 0; i < 4; i++) for (let j = 0; j < 4; j++) { let s = 0; for (let k = 0; k < 4; k++) s += P[k * 4 + j] * V[i * 4 + k]; M[i * 4 + j] = s; }
        return M;
    }

    _bots() {
        // live bots when a run is shown, else the placed ones
        const d = this.botData; let o = 0, n = 0;
        const put = (x, y, z, col) => { if (n++ >= MAXBOTS) return; o = box(d, o, x - 0.4, y, z - 0.4, x + 0.4, y + 1.8, z + 0.4, col); };
        const live = this.sim.rec && this.sim.live;
        if (live && live.rec.frames.length) {
            const t = live.displayTime(), R = live.rec, F = R.frames[R.indexAt(t)], s = {};
            for (let j = 0; j < F.bots.length; j++) { const b = live.botAt(j, t, s); if (!b || !b.alive) continue; const info = R.bots.get(b.id) || {}; put(b.x, b.y, b.z, hex(info.color || "#cccccc")); }
        } else { const teams = TEAM_MODES.has(this.sim.sc.mode); this.sim.sc.bots.forEach((b, i) => { if (b.pos) put(b.pos.x, b.pos.y, b.pos.z, hex(botColor(b.team, i, teams))); }); }
        return o / 6;
    }

    _loop() {
        if (!this.on) return;
        this.raf = requestAnimationFrame(this._loop);
        const gl = this.gl;
        if (!gl) return;
        const dpr = window.devicePixelRatio || 1, W = Math.round(this.c.clientWidth * dpr), H = Math.round(this.c.clientHeight * dpr);
        if (this.c.width !== W || this.c.height !== H) { this.c.width = W; this.c.height = H; this.dirty = true; }
        // draw only when something shown changed: the camera / size / map (dirty) or the bots
        const live = this.sim.rec && this.sim.live, sc = this.sim.sc;
        const botKey = live && live.rec.frames.length ? "L" + live.displayTime() + ":" + live.rec.frames.length
            : "P" + (sc ? sc.bots.map((b) => (b.pos ? `${b.team}${b.pos.x},${b.pos.y},${b.pos.z}` : "-")).join(";") : "");
        if (!this.dirty && botKey === this.botKey) return;
        this.dirty = false; this.botKey = botKey;
        gl.viewport(0, 0, W, H);
        gl.clearColor(0.043, 0.059, 0.078, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.enable(gl.DEPTH_TEST); gl.enable(gl.CULL_FACE);
        gl.useProgram(this.pr);
        gl.uniformMatrix4fv(this.uM, false, this._matrix());
        gl.uniform3f(this.uF, 0.043, 0.059, 0.078);
        const draw = (buf, n) => {
            gl.bindBuffer(gl.ARRAY_BUFFER, buf);
            gl.enableVertexAttribArray(this.aP); gl.vertexAttribPointer(this.aP, 3, gl.FLOAT, false, 24, 0);
            gl.enableVertexAttribArray(this.aC); gl.vertexAttribPointer(this.aC, 3, gl.FLOAT, false, 24, 12);
            gl.drawArrays(gl.TRIANGLES, 0, n);
        };
        draw(this.buf, this.n);
        const nb = this._bots();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.botBuf);
        gl.bufferData(gl.ARRAY_BUFFER, this.botData.subarray(0, nb * 6), gl.DYNAMIC_DRAW);
        draw(this.botBuf, nb);
    }

    _input() {
        const c = this.c;
        c.addEventListener("contextmenu", (e) => e.preventDefault());
        c.addEventListener("pointerdown", (e) => { c.setPointerCapture(e.pointerId); this.drag = { b: e.button, x: e.clientX, y: e.clientY }; });
        c.addEventListener("pointermove", (e) => {
            const d = this.drag; if (!d) return;
            const dx = e.clientX - d.x, dy = e.clientY - d.y; d.x = e.clientX; d.y = e.clientY;
            this.dirty = true;
            if (d.b === 0) { this.cam.yaw -= dx * 0.006; this.cam.pitch = Math.max(0.05, Math.min(1.55, this.cam.pitch + dy * 0.006)); }
            else { const k = this.cam.dist * 0.0018, cy = Math.cos(this.cam.yaw), sy = Math.sin(this.cam.yaw); this.cam.x -= (dx * cy - dy * sy) * k * 1.0; this.cam.z -= (-dx * sy - dy * cy) * k * 1.0; }
        });
        c.addEventListener("pointerup", () => (this.drag = null));
        c.addEventListener("wheel", (e) => { e.preventDefault(); this.dirty = true; this.cam.dist = Math.max(4, Math.min(1500, this.cam.dist * Math.exp(e.deltaY * 0.0012))); }, { passive: false });
    }
}

// 12 triangles; shade per face (top 1, sides .78 / .62, bottom .4); CCW from outside
function box(d, o, x0, y0, z0, x1, y1, z1, c) {
    const face = (a, b, cc, dd, k) => {
        const r = c[0] * k, g = c[1] * k, bl = c[2] * k;
        for (const p of [a, b, cc, a, cc, dd]) { d[o++] = p[0]; d[o++] = p[1]; d[o++] = p[2]; d[o++] = r; d[o++] = g; d[o++] = bl; }
    };
    const A = [x0, y0, z0], B = [x1, y0, z0], C = [x1, y0, z1], D = [x0, y0, z1], E = [x0, y1, z0], F = [x1, y1, z0], G = [x1, y1, z1], H = [x0, y1, z1];
    face(H, G, F, E, 1.0);       // top (+y)
    face(A, B, C, D, 0.4);       // bottom
    face(D, C, G, H, 0.78);      // +z
    face(B, A, E, F, 0.78);      // -z
    face(C, B, F, G, 0.62);      // +x
    face(A, D, H, E, 0.62);      // -x
    return o;
}
