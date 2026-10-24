// Filter bars: drawn once when a tab mounts, so typing never loses focus when data refreshes underneath.
import { html, esc, debounce } from './util.js';

const KEY = 'ghm-filters';
function saved() { try { return JSON.parse(sessionStorage.getItem(KEY) || '{}'); } catch { return {}; } }
function save(all) { try { sessionStorage.setItem(KEY, JSON.stringify(all)); } catch { /* storage may be off */ } }

// Filter state per tab survives tab switches within the session.
export function filterState(tab, defaults) {
  const all = saved();
  const st = { ...defaults, ...(all[tab] || {}) };
  // hash parameters override, e.g. #tab=runs&result=failure
  const p = new URLSearchParams(location.hash.replace(/^#/, ''));
  for (const k of Object.keys(defaults)) if (p.has(k)) st[k] = p.get(k);
  return {
    st,
    set(k, v) { st[k] = v; const a = saved(); a[tab] = st; save(a); },
  };
}

export const search = (name, value, placeholder) =>
  html`<label class="f f-search"><span class="sr">Search</span><input type="search" data-f="${name}" value="${value}" placeholder="${placeholder}" autocomplete="off" spellcheck="false"></label>`;
export const select = (name, value, label, options) =>
  html`<label class="f"><span class="f-l">${label}</span><select data-f="${name}">${options.map(([v, t]) => html`<option value="${v}"${v === value ? ' selected' : ''}>${t}</option>`)}</select></label>`;
export const segmented = (name, value, options) =>
  html`<div class="seg" role="radiogroup">${options.map(([v, t]) => html`<button type="button" role="radio" data-seg="${name}" data-v="${v}" aria-checked="${v === value}" class="${v === value ? 'on' : ''}">${t}</button>`)}</div>`;
export const check = (name, value, label) =>
  html`<label class="f f-check"><input type="checkbox" data-f="${name}"${value ? ' checked' : ''}> ${label}</label>`;

// Wires a bar's controls to the filter state; onChange redraws the results only.
export function bind(root, fs, onChange) {
  const later = debounce(onChange, 120);
  root.addEventListener('input', (e) => {
    const n = e.target.closest('[data-f]');
    if (!n) return;
    fs.set(n.dataset.f, n.type === 'checkbox' ? n.checked : n.value);
    (n.type === 'search' ? later : onChange)();
  });
  root.addEventListener('click', (e) => {
    const b = e.target.closest('[data-seg]');
    if (!b) return;
    fs.set(b.dataset.seg, b.dataset.v);
    for (const x of root.querySelectorAll(`[data-seg="${b.dataset.seg}"]`)) {
      const on = x === b;
      x.classList.toggle('on', on);
      x.setAttribute('aria-checked', String(on));
    }
    onChange();
  });
}
// Refills a <select>'s options (e.g. the branch list) without redrawing the bar.
export function refillSelect(root, name, value, options) {
  const s = root.querySelector(`select[data-f="${name}"]`);
  if (!s) return;
  const want = options.map(([v, t]) => `<option value="${esc(v)}"${v === value ? ' selected' : ''}>${esc(t)}</option>`).join('');
  if (s.dataset.opts !== want) { s.innerHTML = want; s.dataset.opts = want; s.value = value; }
}
