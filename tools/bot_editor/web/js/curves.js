// A curve as an editable plot: one line per skill level (an expression curve can read per-level params).
// steps / points curves: drag points (x and y), double-click adds a point, right-click removes one.
// Expression curves: a slider per number literal in the expression (the plot follows as you drag).

import { evalCurve, symbols, walkExpr } from "./ast.js";
import { canonNum, exprNumText, roundTo } from "./dsl.js";

export const LEVEL_COLORS = ["#60a5fa", "#34d399", "#fbbf24", "#f87171"];
export const LEVEL_NAMES = ["beginner", "intermediate", "advanced", "expert"];

function xRange(c) {
    if (c.form !== "expr") { const xs = c.points.map((p) => p[0]); const mx = Math.max(1, ...xs); return [Math.min(0, ...xs), mx * 1.4]; }
    const a = c.arg.toLowerCase();
    if (/dist|range|len|m$/.test(a)) return [0, 100];
    if (/off|ang|yaw|rad/.test(a)) return [0, 1.6];
    if (/speed|vel/.test(a)) return [0, 20];
    return [0, 1];
}

export class CurveWidget {
    constructor(host, curve, getBrain, onChange) {
        this.host = host; this.c = curve; this.getBrain = getBrain; this.onChange = onChange;
        this.cv = document.createElement("canvas");
        host.append(this.cv);
        this.range = xRange(curve);
        this.ys = new Float64Array(4 * 121);   // sampled values (reused)
        this._input();
        this.draw();
        if (curve.form === "expr") this._sliders();
    }
    sample() {
        const b = this.getBrain(), S = symbols(b), [x0, x1] = this.range;
        let lo = Infinity, hi = -Infinity;
        for (let L = 0; L < 4; L++) for (let i = 0; i <= 120; i++) {
            const v = Number(evalCurve(this.c, x0 + (x1 - x0) * i / 120, b, L, S));
            this.ys[L * 121 + i] = v;
            if (Number.isFinite(v)) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
        }
        if (this.c.form !== "expr") for (const p of this.c.points) { lo = Math.min(lo, p[1]); hi = Math.max(hi, p[1]); }
        if (!Number.isFinite(lo)) { lo = 0; hi = 1; }
        if (hi - lo < 1e-6) { hi += 0.5; lo -= 0.5; }
        const pad = (hi - lo) * 0.12;
        this.yr = [Math.min(0, lo - pad) === 0 && lo >= 0 && lo < (hi - lo) ? 0 : lo - pad, hi + pad];
    }
    toPx(x, y) { const r = this.box, [x0, x1] = this.range, [y0, y1] = this.yr; return [r.l + (x - x0) / (x1 - x0) * r.w, r.t + r.h - (y - y0) / (y1 - y0) * r.h]; }
    fromPx(px, py) { const r = this.box, [x0, x1] = this.range, [y0, y1] = this.yr; return [x0 + (px - r.l) / r.w * (x1 - x0), y0 + (r.t + r.h - py) / r.h * (y1 - y0)]; }
    draw() {
        const cv = this.cv, dpr = window.devicePixelRatio || 1, W = cv.clientWidth || 320, H = cv.clientHeight || 180;
        if (cv.width !== Math.round(W * dpr)) { cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); }
        const ctx = cv.getContext("2d");
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx.clearRect(0, 0, W, H);
        if (!this._dragging) this.sample();
        this.box = { l: 40, t: 8, w: W - 50, h: H - 30 };
        const r = this.box;
        ctx.fillStyle = "#0b0f14"; ctx.fillRect(r.l, r.t, r.w, r.h);
        ctx.strokeStyle = "#1f2a37"; ctx.lineWidth = 1; ctx.font = "10px ui-monospace, monospace"; ctx.fillStyle = "#6b7d93";
        for (let k = 0; k <= 4; k++) {
            const y = this.yr[0] + (this.yr[1] - this.yr[0]) * k / 4, [, py] = this.toPx(0, y);
            ctx.beginPath(); ctx.moveTo(r.l, py); ctx.lineTo(r.l + r.w, py); ctx.stroke();
            ctx.textAlign = "right"; ctx.fillText(+y.toPrecision(3) + "", r.l - 4, py + 3);
            const x = this.range[0] + (this.range[1] - this.range[0]) * k / 4, [px] = this.toPx(x, 0);
            ctx.textAlign = "center"; ctx.fillText(+x.toPrecision(3) + "", px, r.t + r.h + 12);
        }
        ctx.textAlign = "right"; ctx.fillText(this.c.arg, r.l + r.w, r.t + r.h + 22);
        // one line per level (identical lines draw on top of each other)
        for (let L = 3; L >= 0; L--) {
            ctx.strokeStyle = LEVEL_COLORS[L]; ctx.lineWidth = 2; ctx.globalAlpha = 0.9;
            ctx.beginPath();
            for (let i = 0; i <= 120; i++) { const [px, py] = this.toPx(this.range[0] + (this.range[1] - this.range[0]) * i / 120, this.ys[L * 121 + i]); if (i) ctx.lineTo(px, py); else ctx.moveTo(px, py); }
            ctx.stroke();
        }
        ctx.globalAlpha = 1;
        if (this.c.form !== "expr") {
            for (let i = 0; i < this.c.points.length; i++) {
                const [px, py] = this.toPx(this.c.points[i][0], this.c.points[i][1]);
                ctx.fillStyle = i === this.hot ? "#fff" : "#fde047"; ctx.beginPath(); ctx.arc(px, py, 5, 0, 7); ctx.fill();
            }
            if (this.c.form === "steps") { const [px, py] = this.toPx(this.range[0], this.c.base); ctx.fillStyle = this.hot === -1 ? "#fff" : "#fb923c"; ctx.fillRect(px - 4, py - 4, 8, 8); }
        }
    }
    _hit(px, py) {
        if (this.c.form === "expr") return null;
        for (let i = 0; i < this.c.points.length; i++) { const [x, y] = this.toPx(this.c.points[i][0], this.c.points[i][1]); if (Math.hypot(x - px, y - py) < 8) return i; }
        if (this.c.form === "steps") { const [x, y] = this.toPx(this.range[0], this.c.base); if (Math.hypot(x - px, y - py) < 8) return -1; }
        return null;
    }
    _input() {
        const cv = this.cv;
        const pos = (e) => { const r = cv.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
        cv.addEventListener("contextmenu", (e) => {
            e.preventDefault();
            const i = this._hit(...pos(e));
            if (i !== null && i >= 0 && (this.c.form === "steps" || this.c.points.length > 1)) { this.c.points.splice(i, 1); this._commit(); }
        });
        cv.addEventListener("dblclick", (e) => {
            if (this.c.form === "expr" || this.c.points.length >= 16) return;
            const [x, y] = this.fromPx(...pos(e));
            this.c.points.push([roundTo(x, "f32"), roundTo(y, "f32")]);
            this.c.points.sort((a, b) => a[0] - b[0]);
            this._commit();
        });
        cv.addEventListener("pointerdown", (e) => {
            if (e.button !== 0) return;
            const i = this._hit(...pos(e));
            if (i === null) return;
            cv.setPointerCapture(e.pointerId);
            this._dragging = { i };
            this.hot = i;
        });
        cv.addEventListener("pointermove", (e) => {
            const d = this._dragging;
            const [px, py] = pos(e);
            if (!d) { const h = this._hit(px, py); if (h !== this.hot) { this.hot = h; this.draw(); } cv.style.cursor = h !== null ? "grab" : "crosshair"; this._tip(px, py); return; }
            let [x, y] = this.fromPx(px, py);
            const snap = (v) => +v.toPrecision(e.shiftKey ? 4 : 3);
            if (d.i === -1) this.c.base = roundTo(snap(y), "f32");
            else {
                const P = this.c.points, lo = d.i > 0 ? P[d.i - 1][0] : -Infinity, hi = d.i < P.length - 1 ? P[d.i + 1][0] : Infinity;
                const eps = this.c.form === "points" ? 1e-3 : 0;
                P[d.i] = [roundTo(Math.max(lo + eps, Math.min(hi - eps, snap(x))), "f32"), roundTo(snap(y), "f32")];
            }
            this._dragging = null; this.sample(); this._dragging = d;
            this.draw();
            this._tip(px, py);
        });
        cv.addEventListener("pointerup", () => { if (this._dragging) { this._dragging = null; this._commit(); } });
        cv.addEventListener("pointerleave", () => { if (this.tipEl) this.tipEl.textContent = ""; });
    }
    _tip(px, py) {
        if (!this.tipEl) return;
        const [x] = this.fromPx(px, py);
        if (x < this.range[0] || x > this.range[1]) return;
        const b = this.getBrain(), S = symbols(b);
        this.tipEl.innerHTML = `${this.c.arg} = ${+x.toPrecision(3)}: ` + [0, 1, 2, 3].map((L) => `<span style="color:${LEVEL_COLORS[L]}">${+Number(evalCurve(this.c, x, b, L, S)).toPrecision(4)}</span>`).join(" ");
    }
    _commit() { this.sample(); this.draw(); this.onChange(this.c); }

    _sliders() {
        const nums = [];
        walkExpr(this.c.body, (e) => { if (e.op === "num") nums.push(e); });
        if (!nums.length) return;
        const box = document.createElement("div");
        box.className = "sliders";
        nums.forEach((n, i) => {
            const lab = document.createElement("span"); lab.textContent = "#" + (i + 1);
            const r = document.createElement("input"); r.type = "range";
            const span = Math.max(1, Math.abs(n.v) * 2);
            r.min = n.v >= 0 ? 0 : -span; r.max = n.v >= 0 ? span : 0; r.step = span / 200; r.value = n.v;
            const out = document.createElement("span"); out.textContent = exprNumText(n.v, n.type);
            r.oninput = () => { n.v = roundTo(+(+r.value).toPrecision(3), n.type); out.textContent = exprNumText(n.v, n.type); this.sample(); this.draw(); };
            r.onchange = () => this.onChange(this.c);
            box.append(lab, r, out);
        });
        this.host.append(box);
    }
}
export { canonNum };
