// The Values tab's equation and curve editors for one setting.
//   ExprEditor: a one-line expression field with syntax highlighting, autocomplete (setting keys, p.* inputs,
//   functions), the error as you type, a static / dynamic badge, the computed value and, for a dynamic one, its plot
//   over the first input it reads.
//   CurveEditor: builds curve(input, x0, x1, y0, y1, "ease") visually: the input (p.* or a setting), its bounds, the
//   output bounds as multipliers (kept inside the setting's min..max), the easing, a live plot with draggable ends.
// Both only hold the text; the Values tab stores it in the physics set (expr.<key>=) and re-runs.
import { highlightExpr, completions, parseCurve, formatCurve, curveFn, ease, EASINGS, EASE_DOC, INPUTS, STANDARD_INPUTS, sweep, check } from "./expr.js";

const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
export const fmtV = (v) => (v == null || !Number.isFinite(v) ? "–" : Math.abs(v) >= 100 ? v.toFixed(1) : Math.abs(v) >= 10 ? v.toFixed(2) : (+v.toFixed(3)).toString());
const cssv = (n) => getComputedStyle(document.body).getPropertyValue(n).trim();

// a small plot of y(x) (multiplier) with the setting's min / max as dashed lines; returns the mapping for handles
export function drawPlot(cv, pts, opt = {}) {
    const dpr = window.devicePixelRatio || 1, W = cv.clientWidth, H = cv.clientHeight;
    if (!W || !H) return null;
    if (cv.width !== Math.round(W * dpr) || cv.height !== Math.round(H * dpr)) { cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); }
    const g = cv.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = cssv("--bg"); g.fillRect(0, 0, W, H);
    const L = 40, R = 10, T = 8, B = 18;
    const xr = opt.x || [pts[0][0], pts[pts.length - 1][0]];
    let lo = Infinity, hi = -Infinity;
    for (const [, y] of pts) if (Number.isFinite(y)) { lo = Math.min(lo, y); hi = Math.max(hi, y); }
    for (const y of opt.extraY || []) { lo = Math.min(lo, y); hi = Math.max(hi, y); }
    if (!Number.isFinite(lo)) { lo = 0; hi = 1; }
    if (opt.y) [lo, hi] = opt.y;
    if (hi - lo < 1e-6) { hi += 0.5; lo -= 0.5; }
    const lo0 = lo, hi0 = hi;
    const pad = (hi - lo) * 0.12; lo -= pad; hi += pad;
    const X = (x) => L + (W - L - R) * (x - xr[0]) / (xr[1] - xr[0] || 1), Y = (y) => H - B - (H - T - B) * (y - lo) / (hi - lo);
    const IX = (px) => xr[0] + (px - L) / (W - L - R) * (xr[1] - xr[0]), IY = (py) => lo + (H - B - py) / (H - T - B) * (hi - lo);
    g.strokeStyle = cssv("--line"); g.lineWidth = 1; g.strokeRect(L + 0.5, T + 0.5, W - L - R, H - T - B);
    g.font = "10px system-ui"; g.fillStyle = cssv("--fg3"); g.textBaseline = "middle";
    const step = nice((hi - lo) / 3);
    g.textAlign = "right";
    for (let v = Math.ceil(lo / step) * step; v <= hi; v += step) { const y = Math.round(Y(v)) + 0.5; g.globalAlpha = 0.35; g.beginPath(); g.moveTo(L, y); g.lineTo(W - R, y); g.stroke(); g.globalAlpha = 1; g.fillText("×" + +v.toFixed(2), L - 3, y); }
    const xs = nice((xr[1] - xr[0]) / 4);
    g.textAlign = "center";
    for (let v = Math.ceil(xr[0] / xs) * xs; v <= xr[1] + 1e-9; v += xs) g.fillText(+v.toFixed(2) + (opt.xUnit ? "" : ""), X(v), H - 8);
    if (opt.xLabel) { g.textAlign = "right"; g.fillText(opt.xLabel, W - R - 2, T + 8); }
    // the setting's limits
    g.setLineDash([4, 3]); g.strokeStyle = cssv("--bad"); g.globalAlpha = 0.6;
    for (const y of [opt.min, opt.max]) if (y != null && y > lo && y < hi) { g.beginPath(); g.moveTo(L, Y(y)); g.lineTo(W - R, Y(y)); g.stroke(); }
    g.globalAlpha = 1; g.setLineDash([]);
    if (opt.bounds) {   // the input bounds, shaded
        g.fillStyle = cssv("--acc"); g.globalAlpha = 0.07;
        g.fillRect(X(opt.bounds[0]), T, X(opt.bounds[1]) - X(opt.bounds[0]), H - T - B); g.globalAlpha = 1;
    }
    g.strokeStyle = cssv("--acc"); g.lineWidth = 2; g.beginPath();
    pts.forEach(([x, y], i) => { const px = X(x), py = Y(Number.isFinite(y) ? y : lo); i ? g.lineTo(px, py) : g.moveTo(px, py); });
    g.stroke();
    if (opt.marker != null) { g.strokeStyle = cssv("--fg2"); g.setLineDash([2, 3]); g.beginPath(); g.moveTo(X(opt.marker), T); g.lineTo(X(opt.marker), H - B); g.stroke(); g.setLineDash([]); }
    for (const h of opt.handles || []) {
        g.fillStyle = h.hot ? cssv("--warn") : cssv("--fg"); g.strokeStyle = cssv("--bg"); g.lineWidth = 2;
        g.beginPath(); g.arc(X(h.x), Y(h.y), 5.5, 0, 7); g.fill(); g.stroke();
    }
    return { X, Y, IX, IY, L, R, T, B, W, H, lo, hi, lo0, hi0 };
}
const nice = (x) => { if (!(x > 0)) return 1; const e = Math.pow(10, Math.floor(Math.log10(x))), f = x / e; return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * e; };

const inputMeta = (inp, cat) => {
    if (inp.startsWith("p.")) { const i = INPUTS.find((x) => x.key === inp.slice(2)); return { label: inp, unit: i ? i.unit : "", min: i && i.min != null ? i.min : 0, max: i ? i.max : 10, doc: i ? i.doc : "" }; }
    const p = cat.byKey.get(inp); return { label: inp, unit: "x", min: p ? p.min : 0, max: p ? p.max : 5, doc: p ? p.label + " (its multiplier)" : "" };
};

// ---------------------------------------------------------------- equation
export class ExprEditor {
    constructor(host, { cat, key, onText }) {
        this.cat = cat; this.key = key; this.onText = onText; this.p = cat.byKey.get(key);
        host.innerHTML = `<div class="xed"><div class="xwrap"><pre class="xhl"></pre><input class="xin" spellcheck="false" autocomplete="off" placeholder="e.g. sqrt(speed) or 1 + p.hspeed / 40"></div>
            <div class="xac" style="display:none"></div><div class="xmsg"></div><canvas class="xplot" style="display:none"></canvas>
            <div class="xhelp muted small">settings by key (<code>speed</code>, <code>friction</code>…: their multiplier) · <code>p.speed p.hspeed p.vspeed p.air p.ground p.charges p.dashing p.hooked p.height</code> · <code>sqrt abs min max pow exp log floor ceil sign clamp lerp step ease curve</code> · <code>+ - * / ^ &lt; &gt; == ?:</code> · Tab completes</div></div>`;
        this.el = host.firstElementChild;
        this.inp = this.el.querySelector(".xin"); this.hl = this.el.querySelector(".xhl"); this.ac = this.el.querySelector(".xac");
        this.msg = this.el.querySelector(".xmsg"); this.plot = this.el.querySelector(".xplot");
        this.inp.addEventListener("input", () => { this._paint(); this._complete(); this.onText(this.inp.value); });
        this.inp.addEventListener("scroll", () => (this.hl.scrollLeft = this.inp.scrollLeft));
        this.inp.addEventListener("keydown", (e) => this._key(e));
        this.inp.addEventListener("blur", () => setTimeout(() => (this.ac.style.display = "none"), 150));
        this.inp.addEventListener("click", () => this._complete());
    }
    get text() { return this.inp.value; }
    setText(t) { if (document.activeElement === this.inp && this.inp.value === t) return; if (document.activeElement !== this.inp) this.inp.value = t; this._paint(); }
    _paint() {
        const t = this.inp.value, c = check(t, this.cat.keySet);
        this.hl.innerHTML = highlightExpr(t, this.cat.keySet, c.ok ? null : c.error) + " ";
        this.hl.scrollLeft = this.inp.scrollLeft;
        this.lastCheck = c;
    }
    // info: resolve()'s info for this key (value, dynamic, error...), helperValue (physexpr) if any, ph: the set
    status(info, ph, game) {
        const p = this.p;
        if (!info || !info.ok) {
            const e = info && info.error ? info.error : this.lastCheck && this.lastCheck.error;
            this.msg.innerHTML = `<span class="err">✗ ${esc(e ? e.msg : "not an expression")}</span>${e && e.s != null && this.inp.value ? ` <span class="muted">at column ${e.s + 1}</span>` : ""} <span class="muted">· the setting keeps its fixed value until this is fixed</span>`;
            this.plot.style.display = "none";
            return;
        }
        const abs = p.base * info.value;
        let h = `<span class="xbadge ${info.dynamic ? "dyn" : "sta"}">${info.dynamic ? "dynamic" : "static"}</span> = <b>×${fmtV(info.value)}</b> → ${fmtV(abs)} ${esc(p.unit || "")}`;
        h += info.dynamic ? ` <span class="muted">standing still (${info.inputs.map((i) => "p." + i).join(", ") || "via " + info.deps.join(", ")}: worked out every 1 ms step in pmove)</span>`
            : ` <span class="muted">worked out once when the rules load${info.deps.length ? " (from " + info.deps.join(", ") + ")" : ""}</span>`;
        if (info.clamped) h += ` <span class="warn">clamped from ×${fmtV(info.raw)} to ${p.min}..${p.max}</span>`;
        if (info.nonFinite) h += ` <span class="warn">not a finite number here: the fixed value is used</span>`;
        if (game != null) h += ` <span class="${Math.abs(game - info.value) < 1e-4 ? "ok" : "warn"}">· the game's evaluator: ×${fmtV(game)}</span>`;
        this.msg.innerHTML = h;
        // a dynamic one: its value over the first input it reads
        const inp = info.inputs[0];
        if (info.dynamic && inp) {
            this.plot.style.display = "";
            const m = inputMeta("p." + inp, this.cat);
            const pts = sweep(ph, this.cat, this.key, "p." + inp, m.min, m.max, 100);
            drawPlot(this.plot, pts, { min: p.min, max: p.max, xLabel: `p.${inp} (${m.unit})` });
        } else this.plot.style.display = "none";
    }
    _complete() {
        const r = completions(this.inp.value, this.inp.selectionStart, this.cat);
        this.acState = r;
        if (!r.items.length || (r.items.length === 1 && r.items[0].text === r.word)) { this.ac.style.display = "none"; return; }
        this.acSel = 0;
        this.ac.innerHTML = r.items.map((it, i) => `<div class="xai${i ? "" : " on"}" data-i="${i}"><span class="xk-${it.kind}">${esc(it.text)}</span> <span class="muted">${esc(it.doc)}</span></div>`).join("");
        this.ac.style.display = "";
        this.ac.querySelectorAll(".xai").forEach((d) => d.addEventListener("mousedown", (e) => { e.preventDefault(); this._accept(+d.dataset.i); }));
    }
    _accept(i) {
        const r = this.acState, it = r && r.items[i];
        if (!it) return;
        const v = this.inp.value;
        this.inp.value = v.slice(0, r.from) + it.text + v.slice(r.to);
        const c = r.from + it.text.length;
        this.inp.setSelectionRange(c, c);
        this.ac.style.display = "none";
        this._paint(); this.onText(this.inp.value);
    }
    _key(e) {
        const open = this.ac.style.display !== "none";
        if (open && (e.key === "ArrowDown" || e.key === "ArrowUp")) {
            e.preventDefault();
            const n = this.acState.items.length;
            this.acSel = (this.acSel + (e.key === "ArrowDown" ? 1 : n - 1)) % n;
            this.ac.querySelectorAll(".xai").forEach((d, i) => d.classList.toggle("on", i === this.acSel));
        } else if (open && (e.key === "Tab" || e.key === "Enter")) { e.preventDefault(); this._accept(this.acSel); }
        else if (e.key === "Escape") this.ac.style.display = "none";
        else if (e.key === "Tab") e.preventDefault();
    }
}

// ---------------------------------------------------------------- curve
export class CurveEditor {
    constructor(host, { cat, key, onText }) {
        this.cat = cat; this.key = key; this.onText = onText; this.p = cat.byKey.get(key);
        const p = this.p;
        const opts = INPUTS.map((i) => `<option value="p.${i.key}">p.${i.key}${i.unit ? " (" + i.unit + ")" : ""}</option>`).join("")
            + `<optgroup label="another setting (its multiplier)">` + cat.all.filter((q) => q.key !== key).map((q) => `<option value="${q.key}">${q.key}</option>`).join("") + "</optgroup>";
        host.innerHTML = `<div class="ced"><div class="crow">
              <label>input <select class="cin">${opts}</select></label>
              <label>from <input type="number" class="cx0" step="any"></label><label>to <input type="number" class="cx1" step="any"></label>
              <span class="sep2"></span>
              <label title="the multiplier at and below the input's lower bound">× at from <input type="number" class="cy0" step="0.01" min="${p.min}" max="${p.max}"></label>
              <label title="the multiplier at and above the input's upper bound">× at to <input type="number" class="cy1" step="0.01" min="${p.min}" max="${p.max}"></label>
              <span class="cabs muted small"></span>
            </div><div class="crow ceases">${EASINGS.map((e) => `<button data-e="${e}" title="${esc(EASE_DOC[e])}"><canvas width="34" height="20"></canvas>${e}</button>`).join("")}</div>
            <div class="cplotw"><canvas class="cplot"></canvas></div>
            <div class="cmsg muted small"></div></div>`;
        this.el = host.firstElementChild;
        const q = (s) => this.el.querySelector(s);
        this.cin = q(".cin"); this.cx0 = q(".cx0"); this.cx1 = q(".cx1"); this.cy0 = q(".cy0"); this.cy1 = q(".cy1");
        this.cv = q(".cplot"); this.cmsg = q(".cmsg"); this.cabs = q(".cabs");
        for (const b of this.el.querySelectorAll(".ceases button")) {
            const c = b.querySelector("canvas"), g = c.getContext("2d");
            g.strokeStyle = "#5aa9ff"; g.lineWidth = 1.6; g.beginPath();
            for (let i = 0; i <= 20; i++) { const t = i / 20, y = ease(t, b.dataset.e); i ? g.lineTo(2 + t * 30, 17 - y * 14) : g.moveTo(2, 17 - y * 14); }
            g.stroke();
            b.onclick = () => { if (!this.c) return; this.c.ease = b.dataset.e; this._emit(); };
        }
        this.cin.onchange = () => {
            if (!this.c) return;
            const m = inputMeta(this.cin.value, cat);
            this.c.input = this.cin.value; this.c.x0 = m.min; this.c.x1 = m.max;
            this._emit();
        };
        const num = (el, f) => (el.oninput = () => { if (!this.c || el.value === "" || !Number.isFinite(+el.value)) return; f(+el.value); this._emit(true); });
        num(this.cx0, (v) => (this.c.x0 = v)); num(this.cx1, (v) => (this.c.x1 = v));
        num(this.cy0, (v) => (this.c.y0 = this._clampY(v))); num(this.cy1, (v) => (this.c.y1 = this._clampY(v)));
        // leaving a field shows what was kept (a multiplier outside the setting's range was clamped)
        for (const [el, k] of [[this.cx0, "x0"], [this.cx1, "x1"], [this.cy0, "y0"], [this.cy1, "y1"]]) el.addEventListener("change", () => { if (this.c) el.value = +(+this.c[k]).toPrecision(4); });
        // the curve's ends: drag them (x: the input bound, y: the multiplier there)
        this.cv.addEventListener("pointerdown", (e) => {
            if (!this.c || !this.map) return;
            const r = this.cv.getBoundingClientRect(), mx = e.clientX - r.left, my = e.clientY - r.top, M = this.map;
            const d0 = Math.hypot(M.X(this.c.x0) - mx, M.Y(this.c.y0) - my), d1 = Math.hypot(M.X(this.c.x1) - mx, M.Y(this.c.y1) - my);
            const h = Math.min(d0, d1) < 14 ? (d0 <= d1 ? 0 : 1) : -1;
            if (h < 0) return;
            this.drag = { h, view: { x: this.viewX, y: [M.lo0, M.hi0] } };
            this.cv.setPointerCapture(e.pointerId);
        });
        this.cv.addEventListener("pointermove", (e) => {
            const r = this.cv.getBoundingClientRect(), mx = e.clientX - r.left, my = e.clientY - r.top;
            if (!this.drag) {
                if (this.map && this.c) {
                    const M = this.map, near = [0, 1].map((h) => Math.hypot(M.X(h ? this.c.x1 : this.c.x0) - mx, M.Y(h ? this.c.y1 : this.c.y0) - my) < 14);
                    this.cv.style.cursor = near[0] || near[1] ? "grab" : "default";
                }
                return;
            }
            const M = this.map, x = M.IX(mx), y = this._clampY(M.IY(my));
            const snap = (v, s) => Math.round(v / s) * s;
            const sx = nice((this.viewX[1] - this.viewX[0]) / 40), sy = 0.01;
            if (this.drag.h === 0) { this.c.x0 = snap(x, sx); this.c.y0 = +snap(y, sy).toFixed(2); }
            else { this.c.x1 = snap(x, sx); this.c.y1 = +snap(y, sy).toFixed(2); }
            this._emit();
        });
        const up = () => { if (this.drag) { this.drag = null; this.draw(); } };
        this.cv.addEventListener("pointerup", up); this.cv.addEventListener("pointercancel", up);
        new ResizeObserver(() => this.draw()).observe(this.cv);
    }
    _clampY(v) { return Math.max(this.p.min, Math.min(this.p.max, v)); }
    setText(t) {
        const c = parseCurve(t, this.cat.keySet);
        if (!c) {
            this.c = null;
            this.cmsg.innerHTML = `<span class="warn">This setting's expression isn't a single curve(...): edit it in Equation mode, or</span> <button class="cnew">start a curve here</button>`;
            this.cmsg.querySelector(".cnew").onclick = () => { this.c = this.defaultCurve(); this._emit(); };
            this.draw();
            return;
        }
        if (this.drag && this.c) return;   // (our own drag's text coming back)
        this.c = c;
        this._fields();
        this.draw();
    }
    defaultCurve(v = 1) { const y1 = this._clampY(+(Math.max(v, 0.2) * 1.5).toFixed(2)); return { input: "p.hspeed", x0: 0, x1: 20, y0: this._clampY(v), y1, ease: "inout" }; }
    _fields() {
        const c = this.c, act = document.activeElement;
        if (this.cin.value !== c.input) this.cin.value = c.input;
        for (const [el, v] of [[this.cx0, c.x0], [this.cx1, c.x1], [this.cy0, c.y0], [this.cy1, c.y1]]) if (el !== act) el.value = +(+v).toPrecision(4);
        this.el.querySelectorAll(".ceases button").forEach((b) => b.classList.toggle("on", b.dataset.e === c.ease));
        const p = this.p, m = inputMeta(c.input, this.cat);
        this.cabs.textContent = `= ${fmtV(p.base * c.y0)} → ${fmtV(p.base * c.y1)} ${p.unit || ""}`;
        this.cmsg.innerHTML = `${esc(c.input)} ${fmtV(c.x0)} → ${fmtV(c.x1)} ${esc(m.unit)} maps to ×${fmtV(c.y0)} → ×${fmtV(c.y1)} (${esc(EASE_DOC[c.ease])}), flat outside · ${esc(m.doc)} · drag the end points`;
    }
    _emit(fromField) {
        if (!fromField) this._fields(); else this._fields();
        this.draw();
        this.onText(formatCurve(this.c));
    }
    draw() {
        const cv = this.cv;
        if (!cv.clientWidth) return;
        if (!this.c) { this.map = drawPlot(cv, [[0, 1], [1, 1]], { min: this.p.min, max: this.p.max }); return; }
        const c = this.c, span = Math.max(Math.abs(c.x1 - c.x0), 1e-3);
        let xr = this.drag ? this.drag.view.x : [Math.min(c.x0, c.x1) - span * 0.18, Math.max(c.x0, c.x1) + span * 0.18];
        this.viewX = xr;
        const pts = [];
        for (let i = 0; i <= 160; i++) { const x = xr[0] + (xr[1] - xr[0]) * i / 160; pts.push([x, this._clampY(curveFn(x, c.x0, c.x1, c.y0, c.y1, c.ease))]); }
        const m = inputMeta(c.input, this.cat);
        this.map = drawPlot(cv, pts, { x: xr, y: this.drag ? this.drag.view.y : null, min: this.p.min, max: this.p.max, bounds: [Math.min(c.x0, c.x1), Math.max(c.x0, c.x1)],
            extraY: [c.y0, c.y1], xLabel: `${c.input}${m.unit && m.unit !== "x" ? " (" + m.unit + ")" : ""}`, handles: [{ x: c.x0, y: c.y0, hot: this.drag && this.drag.h === 0 }, { x: c.x1, y: c.y1, hot: this.drag && this.drag.h === 1 }] });
    }
}
export { STANDARD_INPUTS };
