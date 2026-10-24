// Movement graph: the runs over time.
//   Curves  one chart per quantity (horizontal speed, speed, height, vertical speed, distance, dash charges) on a
//           shared time axis; the edited set solid, the standard dashed, compared sets and drives in their colours;
//           events as markers; a hover readout. Under them, the metrics table.
//   3D      the trajectories over the map (graph3d.js): orbit camera, a trail per set, 0.1 s ticks, events, a scrubber.
//   all runs / A / B: every run, or two sets against each other (edited, standard, any saved .qmode, any picked game),
//           overlaid or side by side, the table showing A, B and B − A.
//   lock units: per axis, a fixed range that stays put across runs and between A and B (unlocked = automatic).
import { metricRows, computeMetrics, mergeMetrics } from "./sim.js";
import { Graph3D } from "./graph3d.js";

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
export const EVENT_COL = { jump: "#48c78e", land: "#9aa4b1", dash: "#5aa9ff", dashEnd: "#5aa9ff", dashJump: "#3fd0c9", superDash: "#ff7a45", wall: "#f0a35a", bounce: "#e86a8a",
    hook: "#b57ff0", unhook: "#b57ff0", ladder: "#d9a93a", ladderOff: "#d9a93a", step: "#6b7480", stop: "#9aa4b1" };
export const QUIET = new Set(["step", "dashEnd", "stop", "wall"]);
const LOCK_KEY = "qpe.locks";
const AXES = [{ key: "t", label: "time", unit: "s" }, ...SERIES.map((s) => ({ key: s.key, label: s.label, unit: s.unit })), { key: "cam3d", label: "3D camera", unit: "", cam: true }];
const AB_COL = ["#5aa9ff", "#f0a35a"];

export class GraphView {
    constructor(app) {
        this.app = app; this.c = $("chartCanvas"); this.hover = $("hover"); this.mx = -1;
        this.on = new Set(SERIES.filter((s) => s.on).map((s) => s.key));
        this.base = document.createElement("canvas");
        this.view = "curves"; this.cmp = "all"; this.ab = { a: "edit", b: "std", layout: "overlay" }; this.abRuns = [];
        try {
            const st = JSON.parse(localStorage.getItem("qpe.graph") || "{}");
            if (st.view) this.view = st.view; if (st.cmp) this.cmp = st.cmp; if (st.ab) this.ab = { ...this.ab, ...st.ab };
            if (Array.isArray(st.on)) this.on = new Set(st.on);
        } catch {}
        this.locks = {};
        try { this.locks = JSON.parse(localStorage.getItem(LOCK_KEY) || "{}") || {}; } catch {}
        const bar = $("graphBar");
        bar.innerHTML = SERIES.map((s) => `<label class="check"><input type="checkbox" data-k="${s.key}" ${this.on.has(s.key) ? "checked" : ""}> ${s.label}</label>`).join("")
            + `<label class="check"><input type="checkbox" id="gEvents" checked> events</label><span class="grow"></span><span id="gLegend" class="small"></span><button id="gClear" title="remove the drives sent here">clear drives</button>`;
        bar.querySelectorAll("input[data-k]").forEach((cb) => (cb.onchange = () => { cb.checked ? this.on.add(cb.dataset.k) : this.on.delete(cb.dataset.k); this._save(); this.draw(); }));
        $("gEvents").onchange = () => this.draw();
        $("gClear").onclick = () => { app.extraRuns = []; this.draw(); this.table(); app.emit("results"); };
        this.c.addEventListener("pointermove", (e) => { const r = this.c.getBoundingClientRect(); this.mx = e.clientX - r.left; this.my = e.clientY - r.top; this._overlay(); });
        this.c.addEventListener("pointerleave", () => { this.mx = -1; this._overlay(); });
        // the top bar: view, compare, A / B, lock units
        $("gView").querySelectorAll("button").forEach((b) => (b.onclick = () => this.setView(b.dataset.v)));
        $("gCmp").querySelectorAll("button").forEach((b) => (b.onclick = () => this.setCmp(b.dataset.c)));
        $("abLay").querySelectorAll("button").forEach((b) => (b.onclick = () => { this.ab.layout = b.dataset.l; this._save(); this._bar(); this.refresh(); }));
        $("abA").onchange = () => { this.ab.a = $("abA").value; this._save(); this.runAB(); };
        $("abB").onchange = () => { this.ab.b = $("abB").value; this._save(); this.runAB(); };
        $("abSwap").onclick = () => { [this.ab.a, this.ab.b] = [this.ab.b, this.ab.a]; this._save(); this._choices(); this.runAB(); };
        $("gLockBtn").onclick = () => { const p = $("gLock"); p.style.display = p.style.display === "none" ? "" : "none"; this._lockPanel(); this.draw(); };
        this.g3 = new Graph3D(app, this);
        app.on("results", () => {
            if (this.cmp === "ab") this.runAB();
            if (app.page === "graph") this.refresh(); else this.stale = true;
        });
        app.on("page", (p) => { if (p === "graph") { this._choices(); this.refresh(); } else this.g3.leave(); });
        app.on("modes", () => this._choices());
        app.on("games", () => { this._choices(); if (this.cmp === "ab" && (this.ab.a.startsWith("game:") || this.ab.b.startsWith("game:"))) this.runAB(); });
        app.on("map", () => this.g3.setMap());
        new ResizeObserver(() => { if (app.page === "graph" && this.view === "curves") this.draw(); }).observe($("charts"));
        this._bar();
    }
    _save() { try { localStorage.setItem("qpe.graph", JSON.stringify({ view: this.view, cmp: this.cmp, ab: this.ab, on: [...this.on] })); } catch {} }
    setView(v) { this.view = v; this._save(); this._bar(); this.refresh(); }
    setCmp(c) { this.cmp = c; this._save(); this._bar(); this._choices(); if (c === "ab") this.runAB(); this.refresh(); }
    _bar() {
        $("gView").querySelectorAll("button").forEach((b) => b.classList.toggle("on", b.dataset.v === this.view));
        $("gCmp").querySelectorAll("button").forEach((b) => b.classList.toggle("on", b.dataset.c === this.cmp));
        $("abLay").querySelectorAll("button").forEach((b) => b.classList.toggle("on", b.dataset.l === this.ab.layout));
        $("abBar").style.display = this.cmp === "ab" ? "" : "none";
        $("graphBar").style.display = this.view === "curves" ? "" : "none";
        $("g3d").style.display = this.view === "3d" ? "" : "none";
        this.c.style.display = this.view === "curves" ? "" : "none";
        const anyLock = AXES.some((a) => this.locks[a.key] && this.locks[a.key].on);
        $("gLockBtn").classList.toggle("on", anyLock);
        $("gLockBtn").innerHTML = `lock units${anyLock ? " (" + AXES.filter((a) => this.locks[a.key] && this.locks[a.key].on).length + ")" : ""} ▾`;
    }
    _choices() {
        const list = this.app.setChoices();
        for (const [id, k] of [["abA", "a"], ["abB", "b"]]) {
            const sel = $(id);
            sel.innerHTML = list.map((c) => `<option value="${c.id}">${c.label.replace(/[<>&]/g, "")}</option>`).join("");
            if (!list.some((c) => c.id === this.ab[k])) this.ab[k] = k === "a" ? "edit" : "std";
            sel.value = this.ab[k];
        }
    }
    // A and B: their own run of the scenario (one helper call; a few ms)
    async runAB() {
        const seq = (this._abSeq = (this._abSeq || 0) + 1);
        const app = this.app;
        if (!app.scenario || !app.helper) return;
        const [A, B] = await Promise.all([app.physicsOf(this.ab.a), app.physicsOf(this.ab.b)]);
        if (seq !== this._abSeq) return;
        if (!A || !B) { $("gNote").innerHTML = `<span class="err">A or B isn't available</span>`; return; }
        try {
            const scenario = app.scenario;
            const r = await app.helper.sim({ physics: A.physics, compare: [B.physics], scenario, map: app.mapChoice, sampleMs: 10 });
            if (seq !== this._abSeq) return;
            this.abRuns = r.runs.slice(0, 2).map((run, i) => ({ ...run, label: (i ? "B: " : "A: ") + (i ? B : A).label, color: AB_COL[i], ab: i ? "B" : "A",
                metrics: mergeMetrics(run.metrics, computeMetrics(run, scenario)) }));
            const notes = [A, B].map((x, i) => (x.game ? `${i ? "B" : "A"} = ${x.game.game.name}: only its ${x.game.mapped.length} mapped settings are applied (${app.cat.all.length + 2 - x.game.mapped.length} stay at Quark's standard)` : "")).filter(Boolean);
            $("gNote").innerHTML = notes.length ? `<span class="warn">⚠ ${notes.join(" · ")}</span>` : `<span class="muted">A / B: ${r.via}, ${(+r.ms || 0).toFixed(1)} ms</span>`;
            if (app.page === "graph") this.refresh();
        } catch (e) { $("gNote").innerHTML = `<span class="err">${e.message}</span>`; }
    }
    runs() { return this.cmp === "ab" ? this.abRuns : this.app.allRuns(); }
    panels() { const r = this.runs(); return this.cmp === "ab" && this.ab.layout === "side" && r.length === 2 ? [[r[0]], [r[1]]] : [r]; }
    refresh() {
        this.stale = false;
        if (this.cmp !== "ab") $("gNote").textContent = "";
        if (this.view === "3d") this.g3.enter(); else { this.g3.leave(); this.draw(); }
        this.table();
        if ($("gLock").style.display !== "none") this._lockPanel();
    }
    // ---- lock units
    _lockPanel() {
        const p = $("gLock");
        const auto = this.autoRanges || {};
        p.innerHTML = `<div class="muted small">A locked axis keeps its range across runs and between sets (A / B side by side always share one). Unlocked: automatic.</div>`
            + AXES.map((a) => {
                const l = this.locks[a.key] || {}, au = auto[a.key];
                if (a.cam) return `<label class="lk"><input type="checkbox" data-k="${a.key}" ${l.on ? "checked" : ""}> ${a.label} <span class="muted small">(keep the 3D view where it is: no refit on new runs)</span></label>`;
                return `<label class="lk"><input type="checkbox" data-k="${a.key}" ${l.on ? "checked" : ""}> ${a.label}${a.unit ? " (" + a.unit + ")" : ""}</label>
                    <input type="number" step="any" data-lo="${a.key}" value="${l.on ? l.lo : ""}" placeholder="${au ? +au[0].toFixed(2) : "auto"}"><input type="number" step="any" data-hi="${a.key}" value="${l.on ? l.hi : ""}" placeholder="${au ? +au[1].toFixed(2) : "auto"}">`;
            }).join("");
        const save = () => { try { localStorage.setItem(LOCK_KEY, JSON.stringify(this.locks)); } catch {} this._bar(); this.draw(); if (this.view === "3d") this.g3.dirty = true; };
        p.querySelectorAll("input[type=checkbox]").forEach((cb) => (cb.onchange = () => {
            const k = cb.dataset.k, au = (this.autoRanges || {})[k];
            this.locks[k] = cb.checked ? { on: true, lo: au ? +au[0].toFixed(2) : 0, hi: au ? +au[1].toFixed(2) : 1 } : { on: false };
            if (k === "cam3d" && cb.checked) this.locks[k].cam = this.g3.camera();
            save(); this._lockPanel();
        }));
        p.querySelectorAll("input[data-lo], input[data-hi]").forEach((inp) => (inp.oninput = () => {
            const k = inp.dataset.lo || inp.dataset.hi, v = +inp.value;
            if (inp.value === "" || !Number.isFinite(v)) return;
            const l = this.locks[k] && this.locks[k].on ? this.locks[k] : { on: true, lo: 0, hi: 1, ...((this.autoRanges || {})[k] ? { lo: this.autoRanges[k][0], hi: this.autoRanges[k][1] } : {}) };
            if (inp.dataset.lo) l.lo = v; else l.hi = v;
            this.locks[k] = l;
            p.querySelector(`input[data-k="${k}"]`).checked = true;
            save();
        }));
    }
    lock(k) { const l = this.locks[k]; return l && l.on && Number.isFinite(l.lo) && Number.isFinite(l.hi) && l.hi > l.lo ? [l.lo, l.hi] : null; }

    _layout(nPanels = 1) {
        const W = this.c.clientWidth, H = this.c.clientHeight;
        const ser = SERIES.filter((s) => this.on.has(s.key));
        const L = 54, R = 12, T = nPanels > 1 ? 22 : 8, B = 22, gap = 14, pgap = 18;
        const h = ser.length ? (H - T - B - gap * (ser.length - 1)) / ser.length : 0;
        const pw = (W - L * nPanels - R - pgap * (nPanels - 1)) / nPanels;
        return { W, H, L, R, T, B, gap, h, ser, pw, pgap, n: nPanels, px: (i) => L + i * (pw + pgap + L) };
    }
    draw() {
        if (this.view !== "curves") return;
        const runs = this.runs(), panels = this.panels();
        const dpr = window.devicePixelRatio || 1, W = this.c.clientWidth, H = this.c.clientHeight;
        if (!W || !H) return;
        for (const cv of [this.c, this.base]) if (cv.width !== Math.round(W * dpr) || cv.height !== Math.round(H * dpr)) { cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); }
        const g = this.base.getContext("2d");
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        const css = getComputedStyle(document.body);
        const fg3 = css.getPropertyValue("--fg3"), line = css.getPropertyValue("--line"), bg = css.getPropertyValue("--bg");
        g.fillStyle = bg; g.fillRect(0, 0, W, H);
        const lay = this._layout(panels.length);
        this.lay = lay;
        let tMax = 0.1;
        for (const r of runs) if (r.trace.length) tMax = Math.max(tMax, r.trace[r.trace.length - 1].t);
        this.autoRanges = { t: [0, tMax] };
        const tl = this.lock("t");
        const [t0, t1] = tl || [0, tMax];
        this.tr = [t0, t1];
        const Xp = (pi) => (t) => lay.px(pi) + lay.pw * (t - t0) / (t1 - t0);
        this.Xp = Xp;
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
            this.autoRanges[s.key] = [lo, hi];
            const lk = this.lock(s.key);
            if (lk) [lo, hi] = lk;
            const Y = (v) => y1 - (y1 - y0) * (v - lo) / (hi - lo);
            this.ranges.push({ s, y0, y1, Y, lo, hi, locked: !!lk });
            panels.forEach((pruns, pi) => {
                const X = Xp(pi), px0 = lay.px(pi);
                g.strokeStyle = line; g.lineWidth = 1; g.fillStyle = fg3;
                g.strokeRect(px0 + 0.5, y0 + 0.5, lay.pw, lay.h);
                const step = niceStep((hi - lo) / Math.max(1, Math.min(4, Math.floor(lay.h / 26))));   // fewer ticks on a short panel
                for (let v = Math.ceil(lo / step) * step; v <= hi; v += step) {
                    const y = Math.round(Y(v)) + 0.5;
                    g.beginPath(); g.moveTo(px0, y); g.lineTo(px0 + lay.pw, y); g.globalAlpha = 0.5; g.stroke(); g.globalAlpha = 1;
                    g.textAlign = "right"; g.fillText(+v.toFixed(2) + "", px0 - 4, y);
                }
                if (pi === 0) { g.save(); g.translate(10, (y0 + y1) / 2); g.rotate(-Math.PI / 2); g.textAlign = "center";
                    let lab = s.label + (s.unit ? ` (${s.unit})` : "") + (lk ? " 🔒" : "");
                    while (lab.length > 3 && g.measureText(lab).width > lay.h - 4) lab = lab.slice(0, -2).trimEnd() + "…";   // fits its panel
                    g.fillText(lab, 0, 0); g.restore(); }
                // events of the panel's first run
                if ($("gEvents").checked && pruns[0]) {
                    let lastLab = -1e9;
                    for (const e of pruns[0].events) {
                        if (QUIET.has(e.kind) || e.t < t0 || e.t > t1) continue;
                        g.strokeStyle = EVENT_COL[e.kind] || fg3; g.globalAlpha = 0.45; g.setLineDash([2, 3]);
                        const x = Math.round(X(e.t)) + 0.5; g.beginPath(); g.moveTo(x, y0); g.lineTo(x, y1); g.stroke();
                        g.setLineDash([]); g.globalAlpha = 1;
                        if (si === 0 && x - lastLab > 44) { lastLab = x; g.fillStyle = EVENT_COL[e.kind] || fg3; g.textAlign = "left"; g.fillText(e.tech || e.kind, x + 2, y0 + 7); }
                    }
                }
                // the curves (all runs: the standard dashed, drives dotted; A / B: B dashed when overlaid)
                g.save(); g.beginPath(); g.rect(px0, y0, lay.pw, lay.h); g.clip();
                pruns.forEach((r) => {
                    const ri = runs.indexOf(r);
                    g.strokeStyle = r.color; g.lineWidth = ri === 0 ? 2 : 1.5;
                    g.setLineDash(r.drive ? [1, 3] : ri === 1 && panels.length === 1 ? [6, 4] : []);
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
        });
        // time axis, panel titles
        g.fillStyle = fg3; g.textAlign = "center";
        const ts = niceStep((t1 - t0) / (8 / panels.length));
        panels.forEach((pruns, pi) => {
            const X = Xp(pi);
            for (let t = Math.ceil(t0 / ts) * ts; t <= t1 + 1e-9; t += ts) g.fillText(+t.toFixed(2) + " s", X(t), H - 10);
            if (panels.length > 1 && pruns[0]) { g.fillStyle = pruns[0].color; g.font = "600 12px system-ui"; g.fillText(pruns[0].label, lay.px(pi) + lay.pw / 2, 11); g.font = "11px system-ui"; g.fillStyle = fg3; }
        });
        $("gLegend").innerHTML = runs.map((r, i) => `<span style="margin-left:8px"><span class="swatch" style="background:${r.color}"></span>${esc(r.label)}${i === 1 && !r.drive && panels.length === 1 ? " (dashed)" : ""}</span>`).join("")
            + (tl || lay.ser.some((s) => this.lock(s.key)) ? ` <span class="muted">· 🔒 locked axes</span>` : "");
        this._overlay();
    }
    _overlay() {
        const W = this.c.clientWidth, H = this.c.clientHeight, dpr = window.devicePixelRatio || 1;
        if (!W || !this.base.width || !this.lay) return;
        const g = this.c.getContext("2d");
        g.setTransform(1, 0, 0, 1, 0, 0); g.drawImage(this.base, 0, 0);
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        const lay = this.lay, panels = this.panels();
        let pi = -1;
        for (let i = 0; i < lay.n; i++) if (this.mx >= lay.px(i) && this.mx <= lay.px(i) + lay.pw) pi = i;
        if (pi < 0 || !this.ranges) { this.hover.style.display = "none"; return; }
        const [t0, t1] = this.tr;
        const t = t0 + (this.mx - lay.px(pi)) / lay.pw * (t1 - t0);
        g.strokeStyle = getComputedStyle(document.body).getPropertyValue("--fg"); g.globalAlpha = 0.5;
        for (let i = 0; i < lay.n; i++) { const x = Math.round(this.Xp(i)(t)) + 0.5; g.beginPath(); g.moveTo(x, lay.T); g.lineTo(x, H - lay.B); g.stroke(); }
        g.globalAlpha = 1;
        const runs = this.runs();
        let txt = `t = ${t.toFixed(3)} s`;
        const ab = this.cmp === "ab" && runs.length === 2;
        for (const rg of this.ranges) {
            txt += `\n${rg.s.label}:`;
            const vals = [];
            panels.forEach((pr, pj) => pr.forEach((r) => {
                const p = sampleAt(r.trace, t);
                if (!p) return;
                const v = rg.s.get(p, r);
                vals.push(v);
                g.fillStyle = r.color; g.beginPath(); g.arc(this.Xp(pj)(p.t), rg.Y(v), 3, 0, 7); g.fill();
                txt += `  ${r.label.slice(0, 16)} ${(+v).toFixed(2)}`;
            }));
            if (ab && vals.length === 2) { const d = vals[1] - vals[0]; txt += `  B−A ${d >= 0 ? "+" : ""}${d.toFixed(2)}`; }
        }
        const ev = panels[pi][0] && panels[pi][0].events.filter((e) => Math.abs(e.t - t) < (t1 - t0) / 120);
        if (ev && ev.length) txt += "\n" + ev.map((e) => `${e.kind}${e.tech ? " (" + e.tech + ")" : ""} at ${e.t.toFixed(3)} s`).join("\n");
        const hv = this.hover;
        hv.textContent = txt; hv.style.display = "block";
        const left = this.mx + 14 + hv.offsetWidth > W ? this.mx - 14 - hv.offsetWidth : this.mx + 14;
        hv.style.left = left + "px"; hv.style.top = Math.min(H - hv.offsetHeight - 4, Math.max(4, this.my - 20)) + "px";
    }
    table() {
        const runs = this.runs(), el = $("metrics");
        if (!runs.length) { el.innerHTML = this.cmp === "ab" ? `<div class="muted small" style="padding:6px 8px">running A and B…</div>` : ""; return; }
        const ab = this.cmp === "ab" && runs.length === 2;
        // the difference: B − A (A / B), or edited − standard
        const [ra, rb] = ab ? [runs[1], runs[0]] : [runs[0], runs[1] && !runs[1].drive ? runs[1] : null];
        const dl = ab ? "B − A" : "edited − standard";
        let h = `<table class="met"><tr><th>metric</th>${runs.map((r) => `<th><span class="swatch" style="background:${r.color}"></span>${esc(r.label)}</th>`).join("")}${rb ? `<th>${dl}</th><th>%</th>` : ""}</tr>`;
        for (const m of metricRows(runs.map((r) => r.metrics || {}), this.app.presetMetrics())) {
            const a = ra.metrics[m.key], b = rb ? rb.metrics[m.key] : null;
            h += `<tr><td>${m.label}${m.unit ? ` <span class="muted">${m.unit}</span>` : ""}</td>`;
            h += runs.map((r) => `<td class="n${Array.isArray(r.metrics[m.key]) ? " hops" : ""}">${fmtMetric(r.metrics[m.key], m.unit)}</td>`).join("");
            if (rb) {
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
export const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
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
