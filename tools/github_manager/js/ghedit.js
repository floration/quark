// The GitHub editor's data and behaviour, shared by the Structure, Rules & limits and Pending changes sections:
// /api/structure, /api/rules, /api/pending (ETags, loaded with every poll), /api/github/rulesets (when the Rules
// section opens), the writes, and the delegated form / button handling. Forms keep their drafts here (keyed by a
// form key), so the redraw every poll causes never loses what is being typed.
import { BASE } from './store.js';
import { enc } from './util.js';

export const ged = {
  available: null,     // false: the server running now is older than the GitHub editor (404)
  structure: null, rules: null, pending: null,
  rulesets: null,      // the last /api/github/rulesets answer
  rulesetsBusy: false,
  etags: {},
};

// UI state that survives redraws: open forms / confirms, busy buttons, errors, drafts
export const gui = { open: new Set(), confirm: new Set(), busy: new Set(), errors: new Map(), drafts: new Map() };
let hooks = { onChange() {}, toast() {} };
const actions = {}, submits = {}, inputs = {};

// ---- loading ----
async function getJSON(name, path) {
  const r = await fetch(BASE + path, { cache: 'no-cache' });
  if (r.status === 404) { ged.available = false; return false; }
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  const tag = r.headers.get('ETag') || '';
  if (tag && tag === ged.etags[name] && ged[name]) return false;
  ged[name] = await r.json();
  ged.etags[name] = tag;
  ged.available = true;
  return true;
}
// -> true when anything changed
export async function loadEditorData() {
  try {
    const got = await Promise.all([getJSON('structure', '/api/structure'), getJSON('rules', '/api/rules'), getJSON('pending', '/api/pending')]);
    return got.some(Boolean);
  } catch (e) {
    console.warn('editor:', e.message || e);
    return false;
  }
}
export async function loadRulesets(fresh) {
  if (ged.rulesetsBusy) return;
  ged.rulesetsBusy = true;
  hooks.onChange();
  try {
    const r = await fetch(`${BASE}/api/github/rulesets${fresh ? '?fresh=1' : ''}`, { cache: 'no-store' });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    ged.rulesets = await r.json();
  } catch (e) {
    ged.rulesets = { ...(ged.rulesets || { rulesets: [], checks: [] }), error: `could not reach the server: ${e.message || e}` };
  } finally {
    ged.rulesetsBusy = false;
    hooks.onChange();
  }
}

// ---- writing ----
export class SendError extends Error {
  constructor(msg, body, status) { super(msg); this.body = body; this.status = status; }
}
export async function send(method, path, body) {
  const r = await fetch(BASE + path, { method, cache: 'no-store', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body || {}) });
  let j = null;
  try { j = await r.json(); } catch { /* not JSON */ }
  if (!r.ok) throw new SendError((j && j.error) || `HTTP ${r.status}`, j, r.status);
  return j;
}
function took(name, obj) { ged[name] = obj; ged.etags[name] = ''; }
export async function putStructure(branches) {
  took('structure', await send('PUT', '/api/structure', { branches, rev: ged.structure.rev }));
}
export async function putRules(rules) {
  took('rules', await send('PUT', '/api/rules', { rules, rev: ged.rules.rev }));
}
export async function refreshPending() { ged.etags.pending = ''; await getJSON('pending', '/api/pending'); }
export async function queueChange(type, params) {
  try {
    return (await send('POST', '/api/pending', { type, params })).change;
  } finally { await refreshPending(); }
}
export async function discardChange(id) { try { await send('DELETE', `/api/pending/${enc(id)}`); } finally { await refreshPending(); } }
export async function repreviewChange(id) { try { return (await send('POST', `/api/pending/${enc(id)}/preview`)).change; } finally { await refreshPending(); } }
// -> the change (any status); the server answers 409 / 422 / 502 with the change for stale / refused / failed
export async function applyChange(c) {
  try {
    return (await send('POST', `/api/pending/${enc(c.id)}/apply`, { previewHash: c.previewHash })).change;
  } catch (e) {
    if (e.body && e.body.change) return e.body.change;
    throw e;
  } finally { await refreshPending(); }
}
export async function applyAll() { try { return await send('POST', '/api/pending/apply-all', {}); } finally { await refreshPending(); } }

export const waitingChanges = () => ((ged.pending && ged.pending.changes) || []).filter((c) => c.status !== 'applied');
export const queuedFor = (pred) => waitingChanges().filter(pred);

// ---- forms: <form data-gform="<kind>:<key>"> with fields [data-gf="<field>"] ----
export function draft(fk, init) {
  if (!gui.drafts.has(fk)) gui.drafts.set(fk, typeof init === 'function' ? init() : { ...(init || {}) });
  return gui.drafts.get(fk);
}
export function closeForm(fk) { gui.open.delete(fk); gui.drafts.delete(fk); gui.errors.delete(fk); }
function readField(d, n) {
  const f = n.dataset.gf;
  if (n.type === 'checkbox' && n.dataset.v != null) {
    const set = new Set(d[f] || []);
    if (n.checked) set.add(n.dataset.v); else set.delete(n.dataset.v);
    d[f] = [...set];
  } else if (n.type === 'checkbox') d[f] = n.checked;
  else if (n.type === 'number') d[f] = n.value === '' ? '' : Number(n.value);
  else d[f] = n.value;
}
export function readForm(form) {
  const fk = form.dataset.gform, d = draft(fk);
  for (const n of form.querySelectorAll('[data-gf]')) if (!(n.type === 'checkbox' && n.dataset.v != null)) readField(d, n);
  return d;
}
// run an async action with busy / error state on a key; -> true when it worked
export async function act(key, run, okText) {
  gui.busy.add(key);
  gui.errors.delete(key);
  hooks.onChange();
  try {
    const r = await run();
    if (okText) hooks.toast(typeof okText === 'function' ? okText(r) : okText);
    return true;
  } catch (e) {
    gui.errors.set(key, String(e.message || e));
    hooks.toast(String(e.message || e), true);
    return false;
  } finally {
    gui.busy.delete(key);
    hooks.onChange();
  }
}
export const onAction = (name, fn) => { actions[name] = fn; };
export const onSubmit = (kind, fn) => { submits[kind] = fn; };
export const onInput = (kind, fn) => { inputs[kind] = fn; };
export const redraw = () => hooks.onChange();
export const toast = (t, bad) => hooks.toast(t, bad);

export function initGhEdit(h) {
  hooks = { ...hooks, ...h };
  document.addEventListener('click', (e) => {
    const b = e.target.closest('[data-gact]');
    if (!b || b.disabled) return;
    const fn = actions[b.dataset.gact];
    if (!fn) return;
    e.preventDefault();
    fn(b, e);
  });
  document.addEventListener('submit', (e) => {
    const form = e.target.closest('[data-gform]');
    if (!form) return;
    e.preventDefault();
    const fk = form.dataset.gform, fn = submits[fk.split(':')[0]];
    if (fn) fn(fk, readForm(form), form);
  });
  const onField = (e) => {
    const n = e.target.closest && e.target.closest('[data-gf]');
    const form = n && n.closest('[data-gform]');
    if (!form) return;
    const fk = form.dataset.gform, d = draft(fk);
    readField(d, n);
    const fn = inputs[fk.split(':')[0]];
    if (fn) fn(fk, d, n, e);
  };
  document.addEventListener('input', onField);  // checkboxes and selects send input too
  document.addEventListener('keydown', (e) => {
    const form = e.target.closest && e.target.closest('[data-gform]');
    if (form && e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); form.requestSubmit(); }
  });
}

// a small lock icon (never colour alone: always with a word or a title)
export const LOCK = '<svg class="lock-ico" viewBox="0 0 16 16" width="12" height="12" aria-hidden="true"><path fill="currentColor" d="M4.5 7V5a3.5 3.5 0 1 1 7 0v2h.75c.41 0 .75.34.75.75v6.5c0 .41-.34.75-.75.75h-8.5a.75.75 0 0 1-.75-.75v-6.5c0-.41.34-.75.75-.75h.75Zm1.5 0h4V5a2 2 0 1 0-4 0v2Z"/></svg>';
