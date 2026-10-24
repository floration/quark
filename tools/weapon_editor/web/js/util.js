// small shared helpers
import { fmtNum } from "./stats.js";
export const $ = (id) => document.getElementById(id);
export const fin = (x) => (Number.isFinite(x) ? String(x) : "∞");
export const fmtT = (x) => (Number.isFinite(x) ? fmtNum(x) : "∞");
export const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
export const el = (tag, cls, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html !== undefined) e.innerHTML = html; return e; };
export { fmtNum };
// "+1.5" in the colour of what it means: buff (stronger) or nerf; lowerBetter for shots / time to kill
export function diffSpan(a, b, lowerBetter = false, cls = "d") {
    if (!Number.isFinite(a) && !Number.isFinite(b)) return "";
    if (Number.isFinite(a) !== Number.isFinite(b)) { const good = lowerBetter ? Number.isFinite(a) : Number.isFinite(a); return `<span class="${cls} ${good ? "buff" : "nerf"}">${Number.isFinite(a) ? "was ∞" : "now ∞"}</span>`; }
    const d = a - b;
    if (Math.abs(d) <= 1e-6 * Math.max(1, Math.abs(b))) return "";
    const good = lowerBetter ? d < 0 : d > 0;
    return `<span class="${cls} ${good ? "buff" : "nerf"}">${d > 0 ? "+" : ""}${fmtNum(d)}</span>`;
}
