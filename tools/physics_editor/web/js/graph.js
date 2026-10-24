// Graph: curves over time for every run (the edited set, the standard physics dashed, any set ticked under "Compare
// with", and drives sent here from the Drive tab), one chart per quantity on a shared time axis, the edited run's
// events as markers, a hover readout; and the metrics table with the difference to the standard physics.
import { metricRows } from "./sim.js";

const $ = (id) => document.getElementById(id);
export const fmtMetric = (v, unit) => {
    if (v == null) return "";
    if (typeof v === "boolean") return v ? "yes" : "no";
    if (typeof v === "string") return v;
    if (Array.isArray(v)) return v.map((x) => (x && typeof x === "object" ? `${(+x.width).toFixed(1)}${x.cleared ? "✓" : "✗"}` : (+x).toFixed(1))).join(" ");
    if (typeof v === "object") return Array.isArray(v.pos) ? v.pos.map((x) => (+x).toFixed(1)).join(" ") : JSON.stringify(v);
    if (!unit && Number.isInteger(v) && Math.abs(v) < 1000) return String(v);   // (counts: jumps, landings, dashes, gaps)
    return Math.abs(v) >= 100 ? (+v).toFixed(1) : (+v).toFixed(2);
};

export const SERIES = [
    { key: "hspeed", label: "horizontal speed", unit: "m/s", get: (p) => p.hspeed, on: true },
    { key: "speed", label: "speed", unit: "m/s", get: (p) => p.speed, on: false },
    { key: "height", label: "height", unit: "m", get: (p) => p.pos[1], on: true },
    { key: "vy", label: "vertical speed", unit: "m/s", get: (p) => p.vel[1], on: false },
    { key: "dist", label: "distance from the start", unit: "m", get: (p, r) => Math.hypot(p.pos[0] - r.trace[0].pos[0], p.pos[2] - r.trace[0].pos[2]), on: false },
    { key: "charges", label: "dash charges", unit: "", get: (p) => p.dashCharges, on: true, step: true },
];
const EVENT_COL = { jump: "#48c78e", land: "#9aa4b1", dash: "#5aa9ff", dashEnd: "#5aa9ff", dashJump: "#3fd0c9", superDash: "#ff7a45", wall: "#f0a35a", bounce: "#e86a8a",
    hook: "#b57ff0", unhook: "#b57ff0", ladder: "#d9a93a", ladderOff: "#d9a93a", step: "#6b7480", stop: "#9aa4b1" };
const QUIET = new Set(["step", "dashEnd", "stop", "wall"]);

export class GraphView {
    constructor(app) {
        this.app = app; this.c = $("chartCanvas"); this.hover = $("hover"); this.mx = -1;
        this.on = new Set(SERIES.filter((s) => s.on).map((s) => s.key));
        this.base = document.createElement("canvas");
        const bar = $("graphBar");
        bar.innerHTML = SERIES.map((s) => `<label class="check"><input type="checkbox" data-k="${s.key}" ${s.on ? "checked" : ""}> ${s.label}</label>`).join("")
            + `<label class="check"><input type="checkbox" id="gEvents" checked> events</label><span class="grow"></span><span id="gLegend" class="small"></span><button id="gClear" title="remove the drives sent here">clear drives</button>`;
        bar.querySelectorAll("input[data-k]").forEach((cb) => (cb.onchange = () => { cb.checked ? this.on.add(cb.dataset.k) : this.on.delete(cb.dataset.k); this.draw(); }));
        $("gEvents").onchange = () => this.draw();
        $("gClear").onclick = () => { app.extraRuns = []; this.draw(); this.table(); app.emit("results"); };
        this.c.addEventListener("pointermove", (e) => { const r = this.c.getBoundingClientRect(); this.mx = e.clientX - r.left; this.my = e.clientY - r.top; this._overlay(); });
        this.c.addEventListener("pointerleave", () => { this.mx = -1; this._overlay(); });
        app.on("results", () => { if (app.page === "graph") { this.draw(); this.table(); } else this.stale = true; });
        app.on("page", (p) => { if (p === "graph") { this.draw(); this.table(); } });
        new ResizeObserver(() => { if (app.page === "graph") this.draw(); }).observe($("charts"));
    }
    _layout() {
        const W = this.c.clientWidth, H = this.c.clientHeight;
        const ser = SERIES.filter((s) => this.on.has(s.key));
        const L = 54, R = 12, T = 8, B = 22, gap = 14;
        const h = ser.length ? (H - T - B - gap * (ser.length - 1)) / ser.length : 0;
        return { W, H, L, R, T, B, gap, h, ser };
    }
    draw() {
        const runs = this.app.allRuns();
        const dpr = window.devicePixelRatio || 1, W = this.c.clientWidth, H = this.c.clientHeight;
        if (!W || !H) return;
        for (const cv of [this.c, this.base]) if (cv.width !== Math.round(W * dpr) || cv.height !== Math.round(H * dpr)) { cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); }
        const g = this.base.getContext("2d");
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        const css = getComputedStyle(document.body);
        const fg3 = css.getPropertyValue("--fg3"), line = css.getPropertyValue("--line"), bg = css.getPropertyValue("--bg");
        g.fillStyle = bg; g.fillRect(0, 0, W, H);
        const lay = this._layout();
        let tMax = 0.1;
        for (const r of runs) if (r.trace.length) tMax = Math.max(tMax, r.trace[r.trace.length - 1].t);
        this.tMax = tMax;
        const X = (t) => lay.L + (W - lay.L - lay.R) * t / tMax;
        this.X = X;
        g.font = "11px system-ui"; g.textBaseline = "middle";
        this.ranges = [];
        lay.ser.forEach((s, si) => {
            const y0 = lay.T + si * (lay.h + lay.gap), y1 = y0 + lay.h;
            let lo = Infinity, hi = -Infinity;
            for (const r of runs) for (const p of r.trace) { const v = s.get(p, r); if (v < lo) lo = v; if (v > hi) hi = v; }
            if (!Number.isFinite(lo)) { lo = 0; hi = 1; }
            if (s.key !== "vy" && s.key !== "height") lo = Math.min(0, lo);
            if (hi - lo < 1e-6) hi = lo + 1;
            const pad = (hi - lo) * 0.06; hi += pad; if (s.key === "vy" || s.key === "height") lo -= pad;
            const Y = (v) => y1 - (y1 - y0) * (v - lo) / (hi - lo);
            this.ranges.push({ s, y0, y1, Y, lo, hi });
            // grid
            g.strokeStyle = line; g.lineWidth = 1; g.fillStyle = fg3;
            g.strokeRect(lay.L + 0.5, y0 + 0.5, W - lay.L - lay.R, lay.h);
            const step = niceStep((hi - lo) / 4);
            for (let v = Math.ceil(lo / step) * step; v <= hi; v += step) {
                const y = Math.round(Y(v)) + 0.5;
                g.beginPath(); g.moveTo(lay.L, y); g.lineTo(W - lay.R, y); g.globalAlpha = 0.5; g.stroke(); g.globalAlpha = 1;
                g.textAlign = "right"; g.fillText(+v.toFixed(2) + "", lay.L - 4, y);
            }
            g.save(); g.translate(10, (y0 + y1) / 2); g.rotate(-Math.PI / 2); g.textAlign = "center"; g.fillText(s.label + (s.unit ? ` (${s.unit})` : ""), 0, 0); g.restore();
            // events of the first run
            if ($("gEvents").checked && runs[0]) {
                let lastLab = -1e9;
                for (const e of runs[0].events) {
                    if (QUIET.has(e.kind)) continue;
                    g.strokeStyle = EVENT_COL[e.kind] || fg3; g.globalAlpha = 0.45; g.setLineDash([2, 3]);
                    const x = Math.round(X(e.t)) + 0.5; g.beginPath(); g.moveTo(x, y0); g.lineTo(x, y1); g.stroke();
                    g.setLineDash([]); g.globalAlpha = 1;
                    if (si === 0 && x - lastLab > 44) { lastLab = x; g.fillStyle = EVENT_COL[e.kind] || fg3; g.textAlign = "left"; g.fillText(e.tech || e.kind, x + 2, y0 + 7); }
                }
            }
            // the curves (standard: dashed; drives: dotted)
            g.save(); g.beginPath(); g.rect(lay.L, y0, W - lay.L - lay.R, lay.h); g.clip();
            runs.forEach((r, ri) => {
                g.strokeStyle = r.color; g.lineWidth = ri === 0 ? 2 : 1.5;
                g.setLineDash(r.drive ? [1, 3] : ri === 1 ? [6, 4] : []);
                g.beginPath();
                let prev = null;
                for (let i = 0; i < r.trace.length; i++) {
                    const p = r.trace[i], x = X(p.t), y = Y(s.get(p, r));
                    if (!i) g.moveTo(x, y); else if (s.step) { g.lineTo(x, prev); g.lineTo(x, y); } else g.lineTo(x, y);
                    prev = y;
                }
                g.stroke();
            });
            g.setLineDash([]); g.restore();
        });
        // time axis
        g.fillStyle = fg3; g.textAlign = "center";
        const ts = niceStep(tMax / 8);
        for (let t = 0; t <= tMax + 1e-9; t += ts) g.fillText(+t.toFixed(2) + " s", X(t), H - 10);
        $("gLegend").innerHTML = runs.map((r, i) => `<span style="margin-left:8px"><span class="swatch" style="background:${r.color}"></span>${r.label}${i === 1 && !r.drive ? " (dashed)" : ""}</span>`).join("");
        this._overlay();
    }
    _overlay() {
        const W = this.c.clientWidth, H = this.c.clientHeight, dpr = window.devicePixelRatio || 1;
        if (!W || !this.base.width) return;
        const g = this.c.getContext("2d");
        g.setTransform(1, 0, 0, 1, 0, 0); g.drawImage(this.base, 0, 0);
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        const lay = this._layout();
        if (this.mx < lay.L || this.mx > W - lay.R || !this.ranges) { this.hover.style.display = "none"; return; }
        const t = (this.mx - lay.L) / (W - lay.L - lay.R) * this.tMax;
        g.strokeStyle = getComputedStyle(document.body).getPropertyValue("--fg"); g.globalAlpha = 0.5;
        g.beginPath(); g.moveTo(Math.round(this.mx) + 0.5, lay.T); g.lineTo(Math.round(this.mx) + 0.5, H - lay.B); g.stroke(); g.globalAlpha = 1;
        const runs = this.app.allRuns();
        let txt = `t = ${t.toFixed(3)} s`;
        for (const rg of this.ranges) {
            txt += `\n${rg.s.label}:`;
            runs.forEach((r) => {
                const p = sampleAt(r.trace, t);
                if (!p) return;
                const v = rg.s.get(p, r);
                g.fillStyle = r.color; g.beginPath(); g.arc(this.X(p.t), rg.Y(v), 3, 0, 7); g.fill();
                txt += `  ${r.label.slice(0, 14)} ${(+v).toFixed(2)}`;
            });
        }
        const ev = runs[0] && runs[0].events.filter((e) => Math.abs(e.t - t) < this.tMax / 120);
        if (ev && ev.length) txt += "\n" + ev.map((e) => `${e.kind}${e.tech ? " (" + e.tech + ")" : ""} at ${e.t.toFixed(3)} s`).join("\n");
        const hv = this.hover;
        hv.textContent = txt; hv.style.display = "block";
        const left = this.mx + 14 + hv.offsetWidth > W ? this.mx - 14 - hv.offsetWidth : this.mx + 14;
        hv.style.left = left + "px"; hv.style.top = Math.min(H - hv.offsetHeight - 4, Math.max(4, this.my - 20)) + "px";
    }
    table() {
        const runs = this.app.allRuns(), el = $("metrics");
        if (!runs.length) { el.innerHTML = ""; return; }
        const std = runs[1] && !runs[1].drive ? runs[1] : null;
        let h = `<table class="met"><tr><th>metric</th>${runs.map((r) => `<th><span class="swatch" style="background:${r.color}"></span>${esc(r.label)}</th>`).join("")}${std ? "<th>edited − standard</th><th>%</th>" : ""}</tr>`;
        for (const m of metricRows(runs.map((r) => r.metrics || {}), this.app.presetMetrics())) {
            const a = runs[0].metrics[m.key], b = std ? std.metrics[m.key] : null;
            h += `<tr><td>${m.label}${m.unit ? ` <span class="muted">${m.unit}</span>` : ""}</td>`;
            h += runs.map((r) => `<td class="n${Array.isArray(r.metrics[m.key]) ? " hops" : ""}">${fmtMetric(r.metrics[m.key], m.unit)}</td>`).join("");
            if (std) {
                if (typeof a === "number" && typeof b === "number") {
                    const d = a - b, pct = b ? d / Math.abs(b) * 100 : null, cls = Math.abs(d) < 1e-3 ? "same" : d > 0 ? "up" : "down";
                    h += `<td class="n ${cls}">${Math.abs(d) < 1e-3 ? "=" : (d > 0 ? "+" : "") + fmtMetric(d, m.unit)}</td><td class="n ${cls}">${pct == null || Math.abs(d) < 1e-3 ? "" : (pct > 0 ? "+" : "") + pct.toFixed(1) + "%"}</td>`;
                } else if (Array.isArray(a) && Array.isArray(b) && a.every((x) => typeof x === "number")) {
                    const n = Math.min(a.length, b.length);
                    h += `<td class="n hops">${Array.from({ length: n }, (_, i) => { const d = a[i] - b[i]; return `<span class="${Math.abs(d) < 0.05 ? "same" : d > 0 ? "up" : "down"}">${d > 0 ? "+" : ""}${d.toFixed(1)}</span>`; }).join(" ")}</td><td></td>`;
                } else h += "<td></td><td></td>";
            }
            h += "</tr>";
        }
        el.innerHTML = h + "</table>";
    }
}
function esc(s) { return String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]); }
export function niceStep(x) {
    if (!(x > 0)) return 1;
    const e = Math.pow(10, Math.floor(Math.log10(x))), f = x / e;
    return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * e;
}
export function sampleAt(tr, t) {
    if (!tr.length) return null;
    let lo = 0, hi = tr.length - 1;
    if (t >= tr[hi].t) return tr[hi];
    while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (tr[mid].t <= t) lo = mid; else hi = mid - 1; }
    return tr[lo];
}
