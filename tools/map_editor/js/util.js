// Small helpers: DOM building, events, numbers, dialogs.
export function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === "class") e.className = v;
    else if (k === "style" && typeof v === "object") Object.assign(e.style, v);
    else if (k.startsWith("on") && typeof v === "function") e.addEventListener(k.slice(2), v);
    else if (k === "text") e.textContent = v;
    else if (k === "html") e.innerHTML = v;
    else if (v === true) e.setAttribute(k, "");
    else e.setAttribute(k, v);
  }
  for (const c of kids.flat()) if (c !== null && c !== undefined && c !== false) e.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return e;
}
// node's children replaced by kids (null / false skipped, arrays flattened)
export function fill(node, ...kids) { node.replaceChildren(...kids.flat(2).filter(x => x !== null && x !== undefined && x !== false)); return node; }
export const $ = (s, r = document) => r.querySelector(s);
export const $$ = (s, r = document) => [...r.querySelectorAll(s)];

export class Emitter {
  constructor() { this.h = {}; }
  on(ev, fn) { (this.h[ev] = this.h[ev] || []).push(fn); return () => this.off(ev, fn); }
  off(ev, fn) { this.h[ev] = (this.h[ev] || []).filter(f => f !== fn); }
  emit(ev, ...a) { for (const f of (this.h[ev] || []).slice()) { try { f(...a); } catch (e) { console.error(e); } } }
}

export const fmt = (v, d = 3) => (Math.round(v * 10 ** d) / 10 ** d).toString();
// "1 box", "2 boxes", "3 objects", "2 copies" (words ending in s / x / ch / sh take -es, a consonant + y -ies)
export const plural = (n, word) => n + " " + (n === 1 ? word : /[^aeiou]y$/.test(word) ? word.slice(0, -1) + "ies" : word + (/(s|x|ch|sh)$/.test(word) ? "es" : "s"));
export const fmtS = (v) => v === null || v === undefined || !isFinite(v) ? "-" : v.toFixed(1) + " s";
export const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
export const snap = (v, g) => Math.round(v / g) * g;
// a float that looks like the editor typed it: 1/8 m steps never pile up 0.30000000000000004
export const tidy = (v) => Math.round(v * 1e5) / 1e5;
export const rgbHex = (c) => "#" + c.map(x => Math.round(clamp(x, 0, 1) * 255).toString(16).padStart(2, "0")).join("");
export const hexRgb = (h) => [1, 3, 5].map(i => Math.round(parseInt(h.slice(i, i + 2), 16) / 255 * 1000) / 1000);
export const validMapName = (n) => /^[A-Za-z0-9_-]{1,40}$/.test(n) && !/^(con|prn|aux|nul|com[0-9]|lpt[0-9])$/i.test(n);

export function numInput(value, onChange, opts = {}) {
  const i = el("input", { type: "number", step: opts.step ?? "any", value: value === null || value === undefined ? "" : fmt(value, 4) });
  if (opts.min !== undefined) i.min = opts.min;
  if (opts.max !== undefined) i.max = opts.max;
  if (opts.width) i.style.width = opts.width;
  i.addEventListener("change", () => { const v = parseFloat(i.value); if (isFinite(v)) onChange(v); });
  return i;
}
export function select(options, value, onChange) {
  const s = el("select", {}, options.map(o => { const [v, t] = Array.isArray(o) ? o : [o, o]; return el("option", { value: v, selected: String(v) === String(value) }, t); }));
  s.addEventListener("change", () => onChange(s.value));
  return s;
}
export function check(label, value, onChange, title) {
  const c = el("input", { type: "checkbox", checked: !!value });
  c.addEventListener("change", () => onChange(c.checked));
  return el("label", { title }, c, label);
}

// A modal: body (nodes), buttons [{label, value, primary}] -> Promise<value> (Esc -> null)
export function dialog(body, buttons = [{ label: "OK", value: true, primary: true }]) {
  const d = $("#dlg"), b = $("#dlgBody"), bb = $("#dlgBtns");
  fill(b, body);
  bb.replaceChildren(...buttons.map(x => el("button", { value: String(x.value), class: x.primary ? "primary" : "" }, x.label)));
  return new Promise(res => {
    const done = () => { d.removeEventListener("close", done); const v = d.returnValue; const hit = buttons.find(x => String(x.value) === v); res(hit ? hit.value : null); };
    d.returnValue = "";
    d.addEventListener("close", done);
    d.showModal();
    const f = b.querySelector("input,select,textarea"); if (f) f.focus();
  });
}
export async function prompt(title, value = "", help = "") {
  const i = el("input", { type: "text", value, style: { width: "100%" } });
  i.addEventListener("keydown", e => { if (e.key === "Enter") { e.preventDefault(); $("#dlg").close("ok"); } });
  const r = await dialog([el("h2", {}, title), help ? el("p", { class: "dim" }, help) : null, i].filter(Boolean),
    [{ label: "Cancel", value: "cancel" }, { label: "OK", value: "ok", primary: true }]);
  return r === "ok" ? i.value.trim() : null;
}

let bannerTimer = 0;
export function banner(msg, err = false, ms = 5000) {
  const b = $("#banner");
  b.textContent = msg; b.className = err ? "err" : ""; b.hidden = !msg;
  clearTimeout(bannerTimer);
  if (msg && ms) bannerTimer = setTimeout(() => { b.hidden = true; }, ms);
}
