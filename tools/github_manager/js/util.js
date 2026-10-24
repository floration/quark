// Shared helpers: safe HTML templating, Dallas-time formatting, chips and status badges.

// ---- HTML templating: html`...${value}...` escapes every value unless it is raw() / another html`` result ----
class Raw { constructor(s) { this.s = s; } toString() { return this.s; } }
export const raw = (s) => new Raw(String(s));
const ESC = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
export const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ESC[c]);
function part(v) {
  if (v == null || v === false) return '';
  if (v instanceof Raw) return v.s;
  if (Array.isArray(v)) return v.map(part).join('');
  return esc(v);
}
export function html(strings, ...vals) {
  let out = strings[0];
  for (let i = 0; i < vals.length; i++) out += part(vals[i]) + strings[i + 1];
  return new Raw(out);
}

// ---- links to the sibling tools and GitHub ----
export const TOOLS = {
  dashboard: 'http://127.0.0.1:8765/',
  docs: 'http://127.0.0.1:8766/',
};
let repo = 'floration/quark';
export const setRepo = (r) => { if (r) repo = r; };
export const gh = (path = '') => `https://github.com/${repo}${path}`;
export const docsSearch = (text) => `${TOOLS.docs}#search=${encodeURIComponent(text)}`;
export const dashLink = (hash) => `${TOOLS.dashboard}#${hash}`;
export const enc = encodeURIComponent;

// ---- time (always Dallas) ----
const TZ = 'America/Chicago';
const F = {
  dt: new Intl.DateTimeFormat('en-US', { timeZone: TZ, month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit' }),
  dty: new Intl.DateTimeFormat('en-US', { timeZone: TZ, year: 'numeric', month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit' }),
  time: new Intl.DateTimeFormat('en-US', { timeZone: TZ, hour: 'numeric', minute: '2-digit' }),
  timeS: new Intl.DateTimeFormat('en-US', { timeZone: TZ, hour: 'numeric', minute: '2-digit', second: '2-digit' }),
  day: new Intl.DateTimeFormat('en-US', { timeZone: TZ, weekday: 'long', month: 'long', day: 'numeric', year: 'numeric' }),
  dayKey: new Intl.DateTimeFormat('en-CA', { timeZone: TZ, year: 'numeric', month: '2-digit', day: '2-digit' }),
  hour: new Intl.DateTimeFormat('en-US', { timeZone: TZ, hour: 'numeric' }),
  zone: new Intl.DateTimeFormat('en-US', { timeZone: TZ, timeZoneName: 'short' }),
};
export const ms = (iso) => (iso ? Date.parse(iso) : NaN);
const thisYear = Number(F.dayKey.format(new Date()).slice(0, 4));
export function fmtDate(iso) {
  const t = ms(iso);
  if (!isFinite(t)) return '';
  const y = Number(F.dayKey.format(t).slice(0, 4));
  return (y === thisYear ? F.dt : F.dty).format(t);
}
export const fmtTime = (iso, secs) => (isFinite(ms(iso)) ? (secs ? F.timeS : F.time).format(ms(iso)) : '');
export const fmtDay = (iso) => F.day.format(ms(iso));
export const dayKey = (iso) => F.dayKey.format(ms(iso));
export const fmtHour = (t) => F.hour.format(t);
export function zoneName() {
  const p = F.zone.formatToParts(new Date()).find((x) => x.type === 'timeZoneName');
  return p ? p.value : 'Dallas';
}

export function rel(iso, now = Date.now()) {
  const t = ms(iso);
  if (!isFinite(t)) return '';
  let d = Math.round((now - t) / 1000);
  const fut = d < 0;
  d = Math.abs(d);
  let s;
  if (d < 45) s = fut ? `${d} s` : (d < 10 ? 'just now' : `${d} s`);
  else if (d < 3600) s = `${Math.round(d / 60)} min`;
  else if (d < 86400) {
    const h = Math.floor(d / 3600), m = Math.floor((d % 3600) / 60);
    s = h < 6 && m ? `${h} h ${m} min` : `${Math.round(d / 3600)} h`;
  } else if (d < 86400 * 45) s = `${Math.round(d / 86400)} day${Math.round(d / 86400) === 1 ? '' : 's'}`;
  else s = `${Math.round(d / 86400 / 30)} months`;
  if (s === 'just now') return s;
  return fut ? `in ${s}` : `${s} ago`;
}

export function dur(sec) {
  if (sec == null || !isFinite(sec)) return '';
  sec = Math.max(0, Math.round(sec));
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
  if (h) return `${h} h ${String(m).padStart(2, '0')} min`;
  if (m) return `${m} min ${String(s).padStart(2, '0')} s`;
  return `${s} s`;
}
// running clock, e.g. 7:12 or 1:03:09
export function clock(sec) {
  sec = Math.max(0, Math.floor(sec));
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
  const mm = h ? String(m).padStart(2, '0') : String(m);
  return (h ? `${h}:` : '') + `${mm}:${String(s).padStart(2, '0')}`;
}

// A time shown as Dallas time with the relative age beside it; data-rel lets the ticker update it in place.
export function when(iso, { secs = false, short = false } = {}) {
  if (!iso) return html`<span class="muted">—</span>`;
  const abs = short && dayKey(iso) === dayKey(new Date().toISOString()) ? fmtTime(iso, secs) : fmtDate(iso);
  return html`<time datetime="${iso}" title="${fmtDate(iso)} ${zoneName()} (Dallas)"><span class="abs">${abs}</span> <span class="relp">(<span class="rel" data-rel="${iso}">${rel(iso)}</span>)</span></time>`;
}
export const relOnly = (iso) => html`<span class="rel" data-rel="${iso}" title="${fmtDate(iso)} ${zoneName()} (Dallas)">${rel(iso)}</span>`;
// live elapsed counter since iso (data-since is updated every second by app.js)
export const elapsed = (iso) => html`<span class="elapsed" data-since="${iso}">${clock((Date.now() - ms(iso)) / 1000)}</span>`;

// claude/project-thread-xq2e2m -> thread-xq2e2m (the full name stays in titles / tooltips)
export const shortBranch = (n) => String(n || '').replace(/^claude\/(project-)?/, '');
export const plural = (n, one, many = one + 's') => `${n} ${n === 1 ? one : many}`;
export const short = (sha) => (sha || '').slice(0, 7);

// ---- run status ----
export const RUNNING = new Set(['queued', 'in_progress', 'waiting', 'pending', 'requested']);
export function runState(r) {
  if (!r) return 'none';
  // a run GitHub calls queued / waiting while some of its jobs already ran (e.g. waiting for a macOS runner) is running
  if (RUNNING.has(r.status)) return r.status === 'in_progress' || (r.jobs || []).some((j) => j.status !== 'queued' && j.status !== 'waiting') ? 'running' : 'queued';
  return r.conclusion || 'unknown';
}
const STATE_LABEL = {
  running: 'running', queued: 'queued', success: 'passed', failure: 'failed', cancelled: 'cancelled',
  skipped: 'skipped', timed_out: 'timed out', action_required: 'needs action', neutral: 'neutral',
  stale: 'stale', startup_failure: 'failed to start', pending: 'pending', none: 'no checks', unknown: 'unknown',
};
const STATE_CLASS = {
  success: 'good', failure: 'bad', timed_out: 'bad', startup_failure: 'bad', action_required: 'warn',
  running: 'run', queued: 'wait', pending: 'run', cancelled: 'off', skipped: 'off', neutral: 'off', stale: 'off',
  none: 'off', unknown: 'off',
};
const STATE_ICON = { good: '✓', bad: '✕', warn: '!', off: '–', wait: '…' };
export const stateLabel = (s) => STATE_LABEL[s] || s;
export const stateClass = (s) => STATE_CLASS[s] || 'off';
// badge: icon + label, never colour alone
export function badge(state, label) {
  const c = stateClass(state);
  const icon = c === 'run' ? raw('<span class="spin" aria-hidden="true"></span>') : html`<span class="ico" aria-hidden="true">${STATE_ICON[c]}</span>`;
  return html`<span class="badge s-${c}">${icon}${label ?? stateLabel(state)}</span>`;
}
export const runBadge = (r) => badge(runState(r));

// ---- chips: in-page anchor + GitHub link ----
const ghIcon = raw('<svg viewBox="0 0 16 16" width="11" height="11" aria-hidden="true"><path fill="currentColor" d="M3.75 2h3.5a.75.75 0 0 1 0 1.5h-3.5a.25.25 0 0 0-.25.25v8.5c0 .14.11.25.25.25h8.5a.25.25 0 0 0 .25-.25v-3.5a.75.75 0 0 1 1.5 0v3.5A1.75 1.75 0 0 1 12.25 14h-8.5A1.75 1.75 0 0 1 2 12.25v-8.5C2 2.78 2.78 2 3.75 2Zm6.85-.5h3.65a.25.25 0 0 1 .25.25V5.4a.25.25 0 0 1-.43.17L12.7 4.2 9.28 7.6a.75.75 0 1 1-1.06-1.06L11.63 3.13l-1.2-1.2A.25.25 0 0 1 10.6 1.5Z"/></svg>');
export const ghLink = (url, title = 'Open on GitHub') =>
  url ? html`<a class="ghl" href="${url}" target="_blank" rel="noopener" title="${title}">${ghIcon}</a>` : '';
function chip(kind, href, label, url, title) {
  return html`<span class="chip c-${kind}"><a href="${href}" title="${title || ''}">${label}</a>${ghLink(url)}</span>`;
}
export const prChip = (n, title) => (n == null ? '' : chip('pr', `#pr=${n}`, `#${n}`, gh(`/pull/${n}`), title || `Pull request #${n}`));
export const branchChip = (name, { label } = {}) =>
  name ? chip('branch', `#branch=${enc(name)}`, label || name, gh(`/tree/${name.split('/').map(enc).join('/')}`), `Branch ${name}`) : '';
export const commitChip = (sha, url) => (sha ? chip('commit', `#commit=${sha}`, short(sha), url === undefined ? gh(`/commit/${sha}`) : url, `Commit ${sha}`) : '');
export const runChip = (r) => (r ? chip('run', `#run=${r.id}`, `run ${r.id}`, r.url || gh(`/actions/runs/${r.id}`), r.title) : '');
export function versionChip(v, url) {
  if (!v) return '';
  return chip('version', `#version=${enc(v)}`, v, url || gh(`/blob/testing/changelogs/${v}.md`), `Version ${v}`);
}
export const localChip = (name) => html`<span class="chip c-local"><a href="#local=${enc(name)}" title="Local copy ${name}">${name}</a></span>`;
export const docsLink = (text) => html`<a class="docsl" href="${docsSearch(text)}" target="_blank" rel="noopener" title="Search the Claude Manager for ${text}">docs</a>`;

// ---- misc ----
export function aheadBehind(a, b, ref = 'testing') {
  if (a == null && b == null) return '';
  if (!a && !b) return `even with ${ref}`;
  if (a && b) return `${plural(a, 'commit')} ahead of ${ref}, ${b} behind`;
  if (a) return `${plural(a, 'commit')} ahead of ${ref}`;
  return `${plural(b, 'commit')} behind ${ref}`;
}
export function debounce(fn, wait) {
  let t;
  return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), wait); };
}
export const matches = (text, q) => !q || String(text).toLowerCase().includes(q.toLowerCase());
