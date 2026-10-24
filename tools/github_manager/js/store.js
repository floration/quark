// Talks to the local server (/api/*) and keeps the latest state plus lookup indexes.
import { ms, RUNNING } from './util.js';

// When the page is opened from disk (file://) the API still lives on the server.
const BASE = location.protocol === 'file:' ? 'http://127.0.0.1:8767' : '';

export const store = {
  state: null,        // last /api/state
  idx: null,          // lookups built from it
  history: [],        // events, newest first
  historyLoaded: false,
  reachable: null,    // null until the first answer; false while the server can't be reached
  lastFetchAt: 0,     // when the page last got a state (ms)
  fetchError: '',
  etag: '',           // ETag of the state we have: an unchanged answer (a 304 the browser turned into 200) isn't re-parsed
  extraRuns: new Map(),  // runs older than the RUNS newest, fetched one by one for a #run= deep link
};

// The page asks for the newest RUNS runs (the server keeps every run; one older run comes from /api/run?id=).
const RUNS = 300;
const runMisses = new Set();

async function getJSON(path, opts) {
  // no-cache: the browser revalidates with the ETag and the server answers 304 when nothing changed
  const r = await fetch(BASE + path, { cache: 'no-cache', ...opts });
  if (!r.ok) throw new Error(`${path}: HTTP ${r.status}`);
  return r.json();
}

function index(s) {
  const idx = { runs: new Map(), branches: new Map(), pulls: new Map(), pullByHead: new Map(), local: new Map(), commits: new Map() };
  for (const r of s.runs || []) idx.runs.set(String(r.id), r);
  for (const r of s.running || []) if (!idx.runs.has(String(r.id))) idx.runs.set(String(r.id), r);
  for (const [id, r] of store.extraRuns) if (!idx.runs.has(id)) idx.runs.set(id, r);
  for (const b of s.branches || []) idx.branches.set(b.name, b);
  // newest PR per head wins (open before closed)
  const pulls = [...(s.pulls || [])].sort((a, b) => (a.state === b.state ? ms(b.updatedAt) - ms(a.updatedAt) : a.state === 'open' ? -1 : 1));
  for (const p of pulls) {
    idx.pulls.set(String(p.number), p);
    if (!idx.pullByHead.has(p.head)) idx.pullByHead.set(p.head, p);
  }
  for (const l of s.local || []) idx.local.set(l.name, l);
  // commits we know something about: where they were seen
  const seen = (c, where) => {
    if (!c || !c.sha) return;
    const e = idx.commits.get(c.sha) || { commit: c, where: [] };
    if (!e.commit.subject && c.subject) e.commit = c;
    e.where.push(where);
    idx.commits.set(c.sha, e);
  };
  const t = s.testing || {};
  for (const c of t.commitsSinceProduction || []) seen(c, { kind: 'testing' });
  for (const l of s.local || []) for (const c of l.commits || []) seen(c, { kind: 'local', name: l.name });
  for (const l of s.local || []) if (l.sha) seen({ sha: l.sha, short: l.sha.slice(0, 7), subject: l.subject, date: l.date, url: null }, { kind: 'local-head', name: l.name });
  for (const b of s.branches || []) seen({ sha: b.sha, short: b.sha.slice(0, 7), subject: b.subject, author: b.author, date: b.date, url: null }, { kind: 'branch-head', name: b.name });
  if (s.production && s.production.sha) seen({ sha: s.production.sha, short: s.production.sha.slice(0, 7), subject: `production ${s.production.tag}`, url: null }, { kind: 'production' });
  idx.running = (s.running && s.running.length ? s.running : (s.runs || []).filter((r) => RUNNING.has(r.status)))
    .map((r) => idx.runs.get(String(r.id)) || r);
  return idx;
}

export function findCommit(sha) {
  if (!store.idx || !sha) return null;
  sha = sha.toLowerCase();
  if (store.idx.commits.has(sha)) return store.idx.commits.get(sha);
  for (const [k, v] of store.idx.commits) if (k.startsWith(sha)) return v;
  return null;
}

function accept(s) {
  store.state = s;
  store.idx = index(s);
  store.reachable = true;
  store.lastFetchAt = Date.now();
  store.fetchError = '';
}

// A #run=<id> deep link to a run older than the ones in the state: fetch that one run, then re-route.
async function ensureLinkedRun() {
  if (!store.idx) return;
  const id = new URLSearchParams(location.hash.replace(/^#/, '')).get('run');
  if (!id || !/^\d+$/.test(id) || store.idx.runs.has(id) || runMisses.has(id)) return;
  runMisses.add(id);  // one try per id (a 404 stays "not in the current data")
  try {
    const r = await fetch(`${BASE}/api/run?id=${id}`, { cache: 'no-cache' });
    if (!r.ok) return;
    store.extraRuns.set(id, await r.json());
    store.idx = index(store.state);
    window.dispatchEvent(new HashChangeEvent('hashchange'));
  } catch (e) { /* the drawer keeps saying it isn't in the data */ }
}
window.addEventListener('hashchange', () => { ensureLinkedRun(); });

export async function loadState() {
  try {
    const r = await fetch(`${BASE}/api/state?runs=${RUNS}`, { cache: 'no-cache' });
    if (!r.ok) throw new Error(`/api/state: HTTP ${r.status}`);
    const tag = r.headers.get('ETag') || '';
    if (tag && tag === store.etag && store.state) {
      store.reachable = true; store.lastFetchAt = Date.now(); store.fetchError = '';
    } else {
      accept(await r.json());
      store.etag = tag;
    }
    ensureLinkedRun();
    return true;
  } catch (e) {
    store.reachable = false;
    store.fetchError = String(e.message || e);
    return false;
  }
}

// POST /api/refresh polls GitHub now and returns the new state; the server skips the poll when one ran in the last
// 10 s (header X-Refresh: skipped ...). Returns true when it polled.
export async function refreshNow() {
  const r = await fetch(`${BASE}/api/refresh?runs=${RUNS}`, { method: 'POST', cache: 'no-store' });
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  accept(await r.json());
  store.etag = r.headers.get('ETag') || '';
  ensureLinkedRun();
  return !/^skipped/.test(r.headers.get('X-Refresh') || '');
}

const evKey = (e) => e.id || `${e.at}|${e.type}|${e.text}`;
// Loads the whole local log once, then only what is newer than the newest event we have.
export async function loadHistory() {
  const since = store.historyLoaded && store.history.length ? store.history[0].at : null;
  const q = since ? `?since=${encodeURIComponent(since)}&limit=1000` : '?limit=5000';
  let got = await getJSON('/api/history' + q);
  if (got && !Array.isArray(got)) got = got.events || [];
  if (!since) store.history = got;
  else if (got.length) {
    const have = new Set(store.history.slice(0, 200).map(evKey));
    store.history = got.filter((e) => !have.has(evKey(e))).concat(store.history);
  }
  store.history.sort((a, b) => ms(b.at) - ms(a.at));
  store.historyLoaded = true;
  return got.length > 0 || !since;
}

export async function loadLog(run, job) {
  const r = await fetch(`${BASE}/api/log?run=${encodeURIComponent(run)}&job=${encodeURIComponent(job)}`, { cache: 'no-cache' });
  if (r.status === 404) return { missing: (await r.text()).slice(0, 300) };
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return { text: await r.text() };
}
export const logUrl = (run, job) => `${BASE}/api/log?run=${encodeURIComponent(run)}&job=${encodeURIComponent(job)}`;
