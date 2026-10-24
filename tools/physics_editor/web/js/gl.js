// A small WebGL renderer shared by the Path tab's 3D view and the Drive tab: the map's boxes (one static buffer, face
// shading, a 1 m grid on every surface so speed reads at a glance, fog), coloured line strips (trajectories) and a few
// dynamic boxes (the player, the hook point). Nothing is allocated per frame: dynamic data goes into preallocated
// typed arrays, uploaded with bufferSubData.
import { MATERIALS } from "./qmap.js";

const VS = `attribute vec3 p; attribute vec3 c; attribute vec3 n;
uniform mat4 m; uniform vec3 eye;
varying vec3 vc; varying vec3 wp; varying vec3 vn; varying float vd;
void main(){ gl_Position = m * vec4(p, 1.0); vc = c; wp = p; vn = n; vd = length(p - eye); }`;
const FS = `precision highp float;
varying vec3 vc; varying vec3 wp; varying vec3 vn; varying float vd;
uniform vec3 fog; uniform float grid; uniform float fogFar;
void main(){
  vec2 uv = abs(vn.y) > 0.5 ? wp.xz : (abs(vn.x) > 0.5 ? wp.zy : wp.xy);
  vec2 f = abs(fract(uv) - 0.5);
  vec2 F = abs(fract(uv / 10.0) - 0.5);
  float g = step(0.475, max(f.x, f.y)) * clamp(1.0 - vd / 70.0, 0.0, 1.0);
  float G = step(0.4975, max(F.x, F.y)) * clamp(1.0 - vd / 250.0, 0.0, 1.0);
  vec3 col = mix(vc, vec3(1.0), grid * (0.13 * g + 0.22 * G));
  float k = clamp((vd - fogFar * 0.25) / fogFar, 0.0, 0.7);
  gl_FragColor = vec4(mix(col, fog, k), 1.0);
}`;
const LVS = `attribute vec3 p; attribute vec3 c; uniform mat4 m; varying vec3 vc; void main(){ gl_Position = m * vec4(p, 1.0); vc = c; }`;
const LFS = `precision mediump float; varying vec3 vc; void main(){ gl_FragColor = vec4(vc, 1.0); }`;

const RAMP = [[44, 58, 76], [40, 86, 108], [42, 120, 120], [84, 150, 104], [160, 168, 96], [214, 186, 112], [236, 222, 180]];
function ramp(t) { t = Math.max(0, Math.min(1, t)) * (RAMP.length - 1); const i = Math.min(RAMP.length - 2, Math.floor(t)), f = t - i, a = RAMP[i], b = RAMP[i + 1]; return [0, 1, 2].map((k) => (a[k] + (b[k] - a[k]) * f) / 255); }
export const hexRgb = (h) => { h = (h || "#888").trim(); if (h.startsWith("rgb")) { const m = h.match(/[\d.]+/g); return [m[0] / 255, m[1] / 255, m[2] / 255]; } return [parseInt(h.slice(1, 3), 16) / 255, parseInt(h.slice(3, 5), 16) / 255, parseInt(h.slice(5, 7), 16) / 255]; };

// 4x4 matrices, column major
export function perspective(fovY, aspect, near, far, out = new Float32Array(16)) {
    const f = 1 / Math.tan(fovY / 2);
    out.fill(0); out[0] = f / aspect; out[5] = f; out[10] = (far + near) / (near - far); out[11] = -1; out[14] = (2 * far * near) / (near - far);
    return out;
}
export function lookAt(e, c, up = [0, 1, 0], out = new Float32Array(16)) {
    let fx = c[0] - e[0], fy = c[1] - e[1], fz = c[2] - e[2]; const fl = Math.hypot(fx, fy, fz) || 1; fx /= fl; fy /= fl; fz /= fl;
    let sx = fy * up[2] - fz * up[1], sy = fz * up[0] - fx * up[2], sz = fx * up[1] - fy * up[0]; const sl = Math.hypot(sx, sy, sz) || 1; sx /= sl; sy /= sl; sz /= sl;
    const ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
    out.set([sx, ux, -fx, 0, sy, uy, -fy, 0, sz, uz, -fz, 0, -(sx * e[0] + sy * e[1] + sz * e[2]), -(ux * e[0] + uy * e[1] + uz * e[2]), fx * e[0] + fy * e[1] + fz * e[2], 1]);
    return out;
}
export function mul(a, b, out = new Float32Array(16)) {
    for (let i = 0; i < 4; i++) for (let j = 0; j < 4; j++) { let s = 0; for (let k = 0; k < 4; k++) s += a[k * 4 + j] * b[i * 4 + k]; out[i * 4 + j] = s; }
    return out;
}

const MAX_DYN = 16, MAX_LINE = 120000;

export class GLScene {
    constructor(canvas) {
        this.c = canvas;
        const gl = canvas.getContext("webgl", { antialias: true, alpha: false, powerPreference: "high-performance", desynchronized: true });
        this.gl = gl;
        if (!gl) return;
        const sh = (t, s) => { const x = gl.createShader(t); gl.shaderSource(x, s); gl.compileShader(x); if (!gl.getShaderParameter(x, gl.COMPILE_STATUS)) console.warn(gl.getShaderInfoLog(x)); return x; };
        const prog = (v, f) => { const p = gl.createProgram(); gl.attachShader(p, sh(gl.VERTEX_SHADER, v)); gl.attachShader(p, sh(gl.FRAGMENT_SHADER, f)); gl.linkProgram(p); return p; };
        this.pr = prog(VS, FS);
        this.a = { p: gl.getAttribLocation(this.pr, "p"), c: gl.getAttribLocation(this.pr, "c"), n: gl.getAttribLocation(this.pr, "n") };
        this.u = { m: gl.getUniformLocation(this.pr, "m"), eye: gl.getUniformLocation(this.pr, "eye"), fog: gl.getUniformLocation(this.pr, "fog"), grid: gl.getUniformLocation(this.pr, "grid"), fogFar: gl.getUniformLocation(this.pr, "fogFar") };
        this.lpr = prog(LVS, LFS);
        this.la = { p: gl.getAttribLocation(this.lpr, "p"), c: gl.getAttribLocation(this.lpr, "c") }; this.lu = gl.getUniformLocation(this.lpr, "m");
        this.buf = gl.createBuffer(); this.n = 0;
        this.dynBuf = gl.createBuffer(); this.dyn = new Float32Array(MAX_DYN * 36 * 9); this.nDyn = 0;
        gl.bindBuffer(gl.ARRAY_BUFFER, this.dynBuf); gl.bufferData(gl.ARRAY_BUFFER, this.dyn.byteLength, gl.DYNAMIC_DRAW);
        this.lineBuf = gl.createBuffer(); this.lines = new Float32Array(MAX_LINE * 6); this.nLine = 0; this.lineSegs = [];
        gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuf); gl.bufferData(gl.ARRAY_BUFFER, this.lines.byteLength, gl.DYNAMIC_DRAW);
        this.M = new Float32Array(16); this.V = new Float32Array(16); this.P = new Float32Array(16);
        this.fog = [0.043, 0.059, 0.078];
    }
    setMap(map, slice = Infinity) {
        const gl = this.gl; if (!gl || !map) return;
        this.map = map;
        const B = map.boxes, y0 = map.bounds.y0, y1 = Math.max(y0 + 1, map.bounds.y1);
        let count = 0;
        for (let i = 0; i < map.nBoxes; i++) { const mt = MATERIALS[map.mats[i]]; if (mt === "clip" || mt === "far" || B[i * 6 + 1] >= slice) continue; count++; }
        const lads = map.ladders || [];
        const d = new Float32Array((count + lads.length) * 36 * 9);
        let o = 0;
        for (let i = 0; i < map.nBoxes; i++) {
            const mt = MATERIALS[map.mats[i]], k = i * 6;
            if (mt === "clip" || mt === "far" || B[k + 1] >= slice) continue;
            let col = ramp((B[k + 4] - y0) / (y1 - y0));
            if (mt === "lava") col = [0.8, 0.27, 0.08]; else if (mt === "water") col = [0.12, 0.31, 0.56]; else if (mt === "wallbang") col = [0.3, 0.31, 0.34];
            else if (mt === "glow") col = [0.98, 0.85, 0.5]; else if (mt === "accent") col = [0.62, 0.42, 0.3]; else if (mt === "metal") col = [0.45, 0.5, 0.56]; else if (mt === "trim") col = [0.7, 0.72, 0.6];
            o = boxVerts(d, o, B[k], B[k + 1], B[k + 2], B[k + 3], Math.min(B[k + 4], slice), B[k + 5], col);
        }
        for (const l of lads) o = boxVerts(d, o, l.x0, l.y0, l.z0, l.x1, l.y1, l.z1, [0.25, 0.72, 0.36]);   // ladders: green
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buf); gl.bufferData(gl.ARRAY_BUFFER, d, gl.STATIC_DRAW);
        this.n = o / 9;
    }
    clearDyn() { this.nDyn = 0; }
    addBox(x0, y0, z0, x1, y1, z1, col) { if (this.nDyn >= MAX_DYN) return; boxVerts(this.dyn, this.nDyn * 36 * 9, x0, y0, z0, x1, y1, z1, col); this.nDyn++; }
    // line strips: [{pts: Float32Array|array of [x,y,z], color: [r,g,b]}] (as GL_LINES pairs)
    setStrips(strips) {
        let o = 0; const L = this.lines;
        for (const s of strips) {
            const p = s.pts, c = s.color;
            for (let i = 1; i < p.length; i++) {
                if (o + 12 > L.length) break;
                const a = p[i - 1], b = p[i];
                L[o++] = a[0]; L[o++] = a[1]; L[o++] = a[2]; L[o++] = c[0]; L[o++] = c[1]; L[o++] = c[2];
                L[o++] = b[0]; L[o++] = b[1]; L[o++] = b[2]; L[o++] = c[0]; L[o++] = c[1]; L[o++] = c[2];
            }
        }
        this.nLine = o / 6;
        const gl = this.gl; if (!gl) return;
        gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuf); gl.bufferSubData(gl.ARRAY_BUFFER, 0, L.subarray(0, o));
    }
    resize() {
        const dpr = Math.min(2, window.devicePixelRatio || 1), W = Math.max(1, Math.round(this.c.clientWidth * dpr)), H = Math.max(1, Math.round(this.c.clientHeight * dpr));
        if (this.c.width !== W || this.c.height !== H) { this.c.width = W; this.c.height = H; return true; }
        return false;
    }
    // eye: [x,y,z]; view: Float32Array(16) (lookAt); fovY radians
    render(eye, view, fovY, opts = {}) {
        const gl = this.gl; if (!gl) return;
        this.resize();
        const W = this.c.width, H = this.c.height;
        perspective(fovY, W / H, opts.near || 0.05, opts.far || 3000, this.P);
        mul(this.P, view, this.M);
        gl.viewport(0, 0, W, H);
        gl.clearColor(this.fog[0], this.fog[1], this.fog[2], 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.enable(gl.DEPTH_TEST); gl.enable(gl.CULL_FACE);
        gl.useProgram(this.pr);
        gl.uniformMatrix4fv(this.u.m, false, this.M); gl.uniform3f(this.u.eye, eye[0], eye[1], eye[2]);
        gl.uniform3f(this.u.fog, this.fog[0], this.fog[1], this.fog[2]); gl.uniform1f(this.u.grid, opts.grid ?? 1); gl.uniform1f(this.u.fogFar, opts.fogFar || 400);
        const draw = (buf, n) => {
            if (!n) return;
            gl.bindBuffer(gl.ARRAY_BUFFER, buf);
            gl.enableVertexAttribArray(this.a.p); gl.vertexAttribPointer(this.a.p, 3, gl.FLOAT, false, 36, 0);
            gl.enableVertexAttribArray(this.a.c); gl.vertexAttribPointer(this.a.c, 3, gl.FLOAT, false, 36, 12);
            gl.enableVertexAttribArray(this.a.n); gl.vertexAttribPointer(this.a.n, 3, gl.FLOAT, false, 36, 24);
            gl.drawArrays(gl.TRIANGLES, 0, n);
        };
        draw(this.buf, this.n);
        if (this.nDyn) {
            gl.bindBuffer(gl.ARRAY_BUFFER, this.dynBuf); gl.bufferSubData(gl.ARRAY_BUFFER, 0, this.dyn.subarray(0, this.nDyn * 36 * 9));
            draw(this.dynBuf, this.nDyn * 36);
        }
        gl.disableVertexAttribArray(this.a.n);
        if (this.nLine) {
            gl.useProgram(this.lpr);
            gl.uniformMatrix4fv(this.lu, false, this.M);
            gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuf);
            gl.enableVertexAttribArray(this.la.p); gl.vertexAttribPointer(this.la.p, 3, gl.FLOAT, false, 24, 0);
            gl.enableVertexAttribArray(this.la.c); gl.vertexAttribPointer(this.la.c, 3, gl.FLOAT, false, 24, 12);
            gl.disable(gl.DEPTH_TEST);
            gl.drawArrays(gl.LINES, 0, this.nLine);
            gl.enable(gl.DEPTH_TEST);
        }
    }
}

// 12 triangles, per-vertex position / colour / normal; shade per face (top 1, sides .8 / .65, bottom .45); CCW outside
function boxVerts(d, o, x0, y0, z0, x1, y1, z1, c) {
    const face = (a, b, cc, dd, k, n) => {
        const r = c[0] * k, g = c[1] * k, bl = c[2] * k;
        for (const p of [a, b, cc, a, cc, dd]) { d[o++] = p[0]; d[o++] = p[1]; d[o++] = p[2]; d[o++] = r; d[o++] = g; d[o++] = bl; d[o++] = n[0]; d[o++] = n[1]; d[o++] = n[2]; }
    };
    const A = [x0, y0, z0], B = [x1, y0, z0], C = [x1, y0, z1], D = [x0, y0, z1], E = [x0, y1, z0], F = [x1, y1, z0], G = [x1, y1, z1], H = [x0, y1, z1];
    face(H, G, F, E, 1.0, [0, 1, 0]);
    face(A, B, C, D, 0.45, [0, -1, 0]);
    face(D, C, G, H, 0.8, [0, 0, 1]);
    face(B, A, E, F, 0.8, [0, 0, -1]);
    face(C, B, F, G, 0.65, [1, 0, 0]);
    face(A, D, H, E, 0.65, [-1, 0, 0]);
    return o;
}
