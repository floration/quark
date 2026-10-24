// Small canvas charts (no libraries, nothing fetched): paired horizontal bars (edited vs standard), small line
// charts (damage vs distance, a crosshair readout), a scatter (other games, hover names each dot). Colours come from
// CSS tokens (--s-edit, --s-std, --s-other, --s-hi, --plot-*), so light / dark follow the theme. Every chart has a
// hover tooltip; values in text use the text colours, never the series colour.

const tip = (() => {
    let el = null;
    return {
        show(x, y, html) {
            if (!el) { el = document.createElement("div"); el.className = "chart-tip"; document.body.append(el); }
            el.innerHTML = html; el.style.display = "block";
            const r = el.getBoundingClientRect();
            el.style.left = Math.min(window.innerWidth - r.width - 8, x + 14) + "px";
            el.style.top = Math.max(4, Math.min(window.innerHeight - r.height - 4, y - r.height / 2)) + "px";
        },
        hide() { if (el) el.style.display = "none"; },
    };
})();
export const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

function setup(cv) {
    const dpr = window.devicePixelRatio || 1, W = cv.clientWidth || 300, H = cv.clientHeight || 160;
    if (cv.width !== Math.round(W * dpr) || cv.height !== Math.round(H * dpr)) { cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); }
    const ctx = cv.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);
    const css = getComputedStyle(cv);
    const col = (n, d) => (css.getPropertyValue(n) || "").trim() || d;
    const C = { edit: col("--s-edit", "#3987e5"), std: col("--s-std", "#8a8f98"), other: col("--s-other", "#8a8f98"), hi: col("--s-hi", "#d95926"),
        grid: col("--plot-grid", "#26303c"), text: col("--plot-text", "#8b97a6"), ink: col("--plot-ink", "#d8e1ec"), bg: col("--plot-bg", "transparent") };
    ctx.font = "11px ui-sans-serif, system-ui, sans-serif";
    return { ctx, W, H, C };
}
const nice = (x) => { if (!(x > 0)) return 1; const p = Math.pow(10, Math.floor(Math.log10(x))), f = x / p; return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10) * p; };
export const fmt = (v) => { if (v == null || !Number.isFinite(v)) return "–"; const a = Math.abs(v); return (a >= 100 ? v.toFixed(0) : a >= 10 ? v.toFixed(1) : a >= 1 ? v.toFixed(2) : v.toFixed(3)).replace(/(\.\d*?)0+$/, "$1").replace(/\.$/, "") || "0"; };
function rr(ctx, x, y, w, h, r) {   // a bar rounded at its data end (right)
    r = Math.min(r, w / 2, h / 2);
    ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + w - r, y); ctx.quadraticCurveTo(x + w, y, x + w, y + r);
    ctx.lineTo(x + w, y + h - r); ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h); ctx.lineTo(x, y + h); ctx.closePath(); ctx.fill();
}

// rows: [{label, a, b}] (a = edited, b = standard); opts: {unit, lowerBetter}
export function barPairs(cv, rows, opts = {}) {
    const { ctx, W, H, C } = setup(cv);
    const L = opts.labelW || 150, R = 46, T = 6, rowH = Math.max(14, Math.min(26, (H - T - 18) / Math.max(1, rows.length)));
    const fin = rows.flatMap((r) => [r.a, r.b]).filter((x) => Number.isFinite(x));
    const max = nice(Math.max(1e-9, ...fin));
    const X = (v) => L + (W - L - R) * Math.max(0, Math.min(1, v / max));
    ctx.strokeStyle = C.grid; ctx.lineWidth = 1; ctx.fillStyle = C.text; ctx.textAlign = "center";
    for (let k = 0; k <= 4; k++) { const x = X(max * k / 4); ctx.beginPath(); ctx.moveTo(x + 0.5, T); ctx.lineTo(x + 0.5, T + rows.length * rowH); ctx.stroke(); ctx.fillText(fmt(max * k / 4) + (k === 4 && opts.unit ? " " + opts.unit : ""), x, T + rows.length * rowH + 12); }
    const bh = Math.max(4, (rowH - 6) / 2);
    rows.forEach((r, i) => {
        const y = T + i * rowH + 2;
        ctx.fillStyle = C.ink; ctx.textAlign = "right"; ctx.fillText(r.label.length > 24 ? r.label.slice(0, 23) + "…" : r.label, L - 6, y + rowH / 2 + 2);
        ctx.fillStyle = C.std; if (Number.isFinite(r.b) && r.b > 0) rr(ctx, L, y + bh + 2, X(r.b) - L, bh, 3);
        ctx.fillStyle = C.edit; if (Number.isFinite(r.a) && r.a > 0) rr(ctx, L, y, X(r.a) - L, bh, 3);
        ctx.fillStyle = C.text; ctx.textAlign = "left";
        const txt = Number.isFinite(r.a) ? fmt(r.a) : "∞";
        ctx.fillText(txt, Math.min(W - R + 4, (Number.isFinite(r.a) ? X(r.a) : L) + 4), y + bh - 1);
    });
    cv._hit = (mx, my) => {
        const i = Math.floor((my - T) / rowH);
        if (i < 0 || i >= rows.length) return null;
        const r = rows[i], d = Number.isFinite(r.a) && Number.isFinite(r.b) ? r.a - r.b : null;
        const better = d == null || Math.abs(d) < 1e-9 ? "" : (opts.lowerBetter ? d < 0 : d > 0) ? " (stronger)" : " (weaker)";
        return `<b>${esc(r.label)}</b><br><span class="sw" style="background:${C.edit}"></span>edited ${fmt(r.a)} ${opts.unit || ""}<br><span class="sw" style="background:${C.std}"></span>standard ${fmt(r.b)} ${opts.unit || ""}${d != null && Math.abs(d) > 1e-9 ? `<br>${d > 0 ? "+" : ""}${fmt(d)}${better}` : ""}`;
    };
    hover(cv);
}

// series: [{label, color: "edit"|"std"|"hi"|css, dash?, points: [[x, y]]}]; opts: {xMax, yMax?, xUnit, yUnit, title}
export function lines(cv, series, opts = {}) {
    const { ctx, W, H, C } = setup(cv);
    const L = 34, R = 8, T = opts.title ? 22 : 6, B = 18;
    const xMax = opts.xMax || Math.max(1, ...series.flatMap((s) => s.points.map((p) => p[0])));
    const yMax = nice(opts.yMax || Math.max(1e-9, ...series.flatMap((s) => s.points.map((p) => p[1]).filter(Number.isFinite))) * 1.05);
    const X = (x) => L + (W - L - R) * x / xMax, Y = (y) => T + (H - T - B) * (1 - Math.max(0, Math.min(1.05, y / yMax)));
    if (opts.title) { ctx.fillStyle = C.ink; ctx.textAlign = "left"; ctx.font = "600 11.5px ui-sans-serif, system-ui, sans-serif"; ctx.fillText(opts.title, 2, 11); ctx.font = "11px ui-sans-serif, system-ui, sans-serif"; }
    ctx.strokeStyle = C.grid; ctx.lineWidth = 1; ctx.fillStyle = C.text;
    for (let k = 0; k <= 3; k++) { const y = Y(yMax * k / 3); ctx.beginPath(); ctx.moveTo(L, y + 0.5); ctx.lineTo(W - R, y + 0.5); ctx.stroke(); ctx.textAlign = "right"; ctx.fillText(fmt(yMax * k / 3), L - 4, y + 3); }
    ctx.textAlign = "center";
    for (let k = 0; k <= 4; k++) { const x = X(xMax * k / 4); ctx.fillText(fmt(xMax * k / 4) + (k === 4 && opts.xUnit ? opts.xUnit : ""), Math.min(W - 12, x), H - 5); }
    const colOf = (s) => (s.color === "edit" ? C.edit : s.color === "std" ? C.std : s.color === "hi" ? C.hi : s.color || C.edit);
    for (const s of series) {
        ctx.strokeStyle = colOf(s); ctx.lineWidth = 2; ctx.setLineDash(s.dash ? [5, 4] : []);
        ctx.beginPath();
        s.points.forEach((p, i) => { const x = X(p[0]), y = Y(Number.isFinite(p[1]) ? p[1] : 0); if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y); });
        ctx.stroke();
    }
    ctx.setLineDash([]);
    cv._hit = (mx, my, draw) => {
        if (mx < L || mx > W - R) return null;
        const x = (mx - L) / (W - L - R) * xMax;
        const rowsH = series.map((s) => {
            let best = s.points[0];
            for (const p of s.points) if (Math.abs(p[0] - x) < Math.abs(best[0] - x)) best = p;
            return { s, p: best };
        });
        if (draw) {   // the crosshair
            lines(cv, series, opts);
            const c2 = cv.getContext("2d");
            c2.strokeStyle = C.text; c2.lineWidth = 1; c2.beginPath(); c2.moveTo(mx + 0.5, T); c2.lineTo(mx + 0.5, H - B); c2.stroke();
            for (const r of rowsH) { c2.fillStyle = colOf(r.s); c2.beginPath(); c2.arc(X(r.p[0]), Y(r.p[1]), 4, 0, 7); c2.fill(); }
        }
        return `<b>${fmt(rowsH[0] && rowsH[0].p[0])} ${opts.xUnit || ""}</b>` + rowsH.map((r) => `<br><span class="sw" style="background:${colOf(r.s)}"></span>${esc(r.s.label)}: ${fmt(r.p[1])} ${opts.yUnit || ""}`).join("");
    };
    cv._redraw = () => lines(cv, series, opts);
    hover(cv, true);
}

// points: [{x, y, label, sub, hi}] (hi: Quark's, drawn on top in --s-hi); opts: {logX, logY, xLabel, yLabel}
export function scatter(cv, points, opts = {}) {
    const { ctx, W, H, C } = setup(cv);
    const L = 44, R = 10, T = 8, B = 30;
    const P = points.filter((p) => Number.isFinite(p.x) && Number.isFinite(p.y) && (!opts.logX || p.x > 0) && (!opts.logY || p.y > 0));
    const tx = (v) => (opts.logX ? Math.log10(v) : v), ty = (v) => (opts.logY ? Math.log10(v) : v);
    let x0 = Math.min(...P.map((p) => tx(p.x))), x1 = Math.max(...P.map((p) => tx(p.x))), y0 = Math.min(...P.map((p) => ty(p.y))), y1 = Math.max(...P.map((p) => ty(p.y)));
    if (!P.length) { x0 = 0; x1 = 1; y0 = 0; y1 = 1; }
    if (x1 - x0 < 1e-6) { x0 -= 0.5; x1 += 0.5; } if (y1 - y0 < 1e-6) { y0 -= 0.5; y1 += 0.5; }
    const px = (x1 - x0) * 0.05, py = (y1 - y0) * 0.06; x0 -= px; x1 += px; y0 -= py; y1 += py;
    const X = (v) => L + (W - L - R) * (tx(v) - x0) / (x1 - x0), Y = (v) => T + (H - T - B) * (1 - (ty(v) - y0) / (y1 - y0));
    ctx.strokeStyle = C.grid; ctx.fillStyle = C.text; ctx.lineWidth = 1;
    const ticks = (a, b, log) => { const out = []; if (log) { for (let e = Math.floor(a); e <= Math.ceil(b); e++) for (const m of [1, 2, 5]) { const v = Math.log10(m) + e; if (v >= a && v <= b) out.push(Math.pow(10, v)); } } else { const st = nice((b - a) / 5); for (let v = Math.ceil(a / st) * st; v <= b; v += st) out.push(v); } return out; };
    for (const v of ticks(x0, x1, opts.logX)) { const x = X(v); ctx.beginPath(); ctx.moveTo(x + 0.5, T); ctx.lineTo(x + 0.5, H - B); ctx.stroke(); ctx.textAlign = "center"; ctx.fillText(fmt(v), x, H - B + 12); }
    for (const v of ticks(y0, y1, opts.logY)) { const y = Y(v); ctx.beginPath(); ctx.moveTo(L, y + 0.5); ctx.lineTo(W - R, y + 0.5); ctx.stroke(); ctx.textAlign = "right"; ctx.fillText(fmt(v), L - 4, y + 3); }
    ctx.textAlign = "center"; ctx.fillText(opts.xLabel || "", L + (W - L - R) / 2, H - 4);
    ctx.save(); ctx.translate(10, T + (H - T - B) / 2); ctx.rotate(-Math.PI / 2); ctx.fillText(opts.yLabel || "", 0, 0); ctx.restore();
    const surf = getComputedStyle(cv).getPropertyValue("--plot-surface").trim() || "#10161e";
    for (const pass of [0, 1]) for (const p of P) {
        if (!!p.hi !== !!pass) continue;
        ctx.fillStyle = p.hi ? (p.edited === false ? C.std : C.hi) : C.other; ctx.globalAlpha = p.hi ? 1 : 0.65;
        ctx.beginPath(); ctx.arc(X(p.x), Y(p.y), p.hi ? 5 : 4, 0, 7); ctx.fill();
        if (p.hi) { ctx.strokeStyle = surf; ctx.lineWidth = 2; ctx.stroke(); }
    }
    ctx.globalAlpha = 1;
    // direct labels for Quark's dots only (selective)
    ctx.fillStyle = C.ink; ctx.textAlign = "left";
    for (const p of P) if (p.hi && p.edited !== false && p.short) ctx.fillText(p.short, X(p.x) + 7, Y(p.y) + 3);
    cv._hit = (mx, my) => {
        let best = null, bd = 12;
        for (const p of P) { const d = Math.hypot(X(p.x) - mx, Y(p.y) - my); if (d < bd) { bd = d; best = p; } }
        return best ? `<b>${esc(best.label)}</b>${best.sub ? "<br>" + esc(best.sub) : ""}<br>${esc(opts.xLabel || "x")}: ${fmt(best.x)}<br>${esc(opts.yLabel || "y")}: ${fmt(best.y)}` : null;
    };
    hover(cv);
}

function hover(cv, crosshair = false) {
    if (cv._hoverOn) return;
    cv._hoverOn = true;
    cv.addEventListener("pointermove", (e) => {
        const r = cv.getBoundingClientRect();
        const h = cv._hit && cv._hit(e.clientX - r.left, e.clientY - r.top, crosshair);
        if (h) tip.show(e.clientX, e.clientY, h); else tip.hide();
    });
    cv.addEventListener("pointerleave", () => { tip.hide(); if (cv._redraw) cv._redraw(); });
}
