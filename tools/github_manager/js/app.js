// Entry point: routing (#tab=… and item deep links), polling, the top bar, live timers and the detail drawer.
import { store, loadState, refreshNow, loadHistory } from './store.js';
import { html, rel, fmtTime, zoneName, clock, ms, setRepo, gh, unread, plural } from './util.js';
import { overview } from './overview.js';
import { branches } from './branches.js';
import { local } from './local.js';
import { pulls } from './pulls.js';
import { runs } from './runs.js';
import { history } from './history.js';
import { editor } from './editor.js';
import { tests } from './tests.js';
import { loadNotes, initNotes, openCount, captureFocus, restoreFocus } from './notes.js';
import { loadEditorData, initGhEdit, waitingChanges, ged } from './ghedit.js';
import { renderDetail, ITEM_KEYS } from './detail.js';
import { openLog, closeLog, isLogOpen } from './logview.js';
import './tip.js';

const VIEWS = { overview, branches, local, pulls, runs, history, editor, tests };
const TAB_FOR = { pr: 'pulls', branch: 'branches', run: 'runs', local: 'local', commit: 'overview', version: 'overview' };
const VISIBLE_MS = 15000, HIDDEN_MS = 60000, RETRY_MS = 5000;

const $ = (id) => document.getElementById(id);
const el = { view: $('view'), tabs: $('tabs'), status: $('status'), banner: $('banner'), refresh: $('refresh'), drawer: $('drawer'), drawerBody: $('drawer-body'), scrim: $('scrim'), title: $('repo-link') };

let activeTab = null, activeItem = null, lastGenerated = null, pollTimer = null, firstRoute = true;

// ---- routing ----
function parseHash() {
  const p = new URLSearchParams(location.hash.replace(/^#/, ''));
  let item = null;
  for (const k of ITEM_KEYS) if (p.has(k)) { item = { kind: k, id: p.get(k), log: p.get('log') }; break; }
  let tab = p.get('tab');
  if (!VIEWS[tab]) tab = item ? (firstRoute || !activeTab ? TAB_FOR[item.kind] : activeTab) : 'overview';
  // filter parameters such as #tab=runs&result=failure remount the tab so its filter bar picks them up
  const filters = [...p.keys()].some((k) => k !== 'tab' && k !== 'log' && !ITEM_KEYS.includes(k));
  return { tab, item, filters };
}
function route() {
  const { tab, item, filters } = parseHash();
  firstRoute = false;
  if (!store.state) { activeTab = tab; activeItem = item; return; }
  if (tab !== activeTab || filters) mountTab(tab);
  activeItem = item;
  renderDrawer(true);
  if (item && item.kind === 'run' && item.log) openLog(item.id, item.log, logCloser(item.id));
  else if (isLogOpen()) closeLog(true);
}
export function hashFor(extra) {
  return `#tab=${activeTab}${extra ? '&' + extra : ''}`;
}
const logCloser = (run) => () => { location.hash = `#tab=${activeTab}&run=${run}`; };
function mountTab(tab) {
  activeTab = tab;
  for (const a of el.tabs.querySelectorAll('a[data-tab]')) {
    const on = a.dataset.tab === tab;
    a.classList.toggle('on', on);
    if (on) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current');
  }
  el.view.innerHTML = '';
  el.view.className = `view v-${tab}`;
  VIEWS[tab].mount(el.view, ctx);
  VIEWS[tab].update(ctx);
  document.title = `${VIEWS[tab].title} · GitHub manager`;
  window.scrollTo(0, 0);
  if (VIEWS[tab].wantsHistory) ensureHistory();
}

// ---- drawer (details of a PR / branch / run / commit / version / local copy) ----
function renderDrawer(fromRoute) {
  if (!activeItem) {
    el.drawer.hidden = true; el.scrim.hidden = true;
    document.body.classList.remove('drawer-open');
    return;
  }
  const scroll = fromRoute ? 0 : el.drawerBody.scrollTop;
  const focus = fromRoute ? null : captureFocus();
  el.drawerBody.innerHTML = String(renderDetail(activeItem.kind, activeItem.id, ctx));
  restoreFocus(focus);
  el.drawer.hidden = false; el.scrim.hidden = false;
  document.body.classList.add('drawer-open');
  el.drawerBody.scrollTop = scroll;
  if (fromRoute) el.drawer.focus({ preventScroll: true });
  ensureHistory();
}
function closeDrawer() { location.hash = `#tab=${activeTab}`; }
el.scrim.addEventListener('click', closeDrawer);
$('drawer-close').addEventListener('click', closeDrawer);
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && !isLogOpen() && activeItem && !e.target.closest('input,select,textarea')) closeDrawer();
});

// ---- history (fetched only when something on screen shows it) ----
let historyBusy = null;
function ensureHistory(force) {
  if (historyBusy || (!force && store.historyLoaded)) return historyBusy;
  historyBusy = loadHistory()
    .then((changed) => { if (changed) redraw(); })
    .catch((e) => console.warn('history:', e.message))
    .finally(() => { historyBusy = null; });
  return historyBusy;
}

// ---- top bar ----
function renderStatus() {
  const s = store.state;
  if (!s) { el.status.textContent = store.reachable === false ? 'Not connected' : 'Connecting…'; return; }
  const p = s.poll || {};
  const parts = [];
  parts.push(html`<span title="When the server last polled GitHub">GitHub polled <b>${p.lastOk ? rel(p.lastOk) : 'never'}</b>${p.lastOk ? html` <span class="muted">(${fmtTime(p.lastOk, true)})</span>` : ''}</span>`);
  if (p.nextAt) parts.push(html`<span title="The server polls GitHub every ${p.intervalSec || 60} s">next ${ms(p.nextAt) > Date.now() ? rel(p.nextAt) : 'any moment'}</span>`);
  if (p.rateRemaining != null) parts.push(html`<span title="GitHub API requests left this hour">${Number(p.rateRemaining).toLocaleString('en-US')} API calls left</span>`);
  parts.push(html`<span title="All times on this page are Dallas time">times: Dallas (${zoneName()})</span>`);
  el.status.innerHTML = parts.map(String).join('<span class="sep">·</span>');
}
function renderBanner() {
  const s = store.state, msgs = [];
  if (store.reachable === false && s) {
    msgs.push(html`<div class="msg bad"><b>Can't reach the GitHub manager server</b> (last answer ${rel(new Date(store.lastFetchAt).toISOString())}). Showing the last data it sent. ${s.shell ? 'The Quark Project app restarts it by itself' : html`Start it again with <code>github-manager.cmd</code> (it runs without a window)`}; this page reconnects by itself.</div>`);
  }
  if (s && s.poll && s.poll.lastError) {
    msgs.push(html`<div class="msg warn"><b>The last GitHub poll failed:</b> ${s.poll.lastError}. ${s.poll.lastOk ? html`Data is from the last good poll, ${rel(s.poll.lastOk)}.` : ''}</div>`);
  }
  if (s) msgs.push(...background(s));
  el.banner.innerHTML = msgs.map(String).join('');
  el.banner.hidden = !msgs.length;
}
// Background work, on every tab: the poll's warnings (log downloads refused, ...), the Claude Documents copy (its
// status from /api/rules, fresher than the poll's line about it, which it replaces) and the first start's backfill.
function background(s) {
  const p = s.poll || {}, ds = ged.rules && ged.rules.docsSync, out = [];
  for (const w of p.warnings || []) if (!(ds && /^Claude Documents/.test(w))) out.push(html`<div class="msg warn small">${w}</div>`);
  if (ds && ds.state === 'failed') {
    out.push(html`<div class="msg warn small"><b>The Claude Documents copy is failing</b>${ds.failingSince ? html` since ${fmtTime(ds.failingSince)} (${rel(ds.failingSince)})` : ''}: <code>${ds.lastError}</code>. The rules and notes are safe on this PC; the copy is tried again on every poll. <a href="#tab=editor&sec=rules">Rules &amp; limits</a></div>`);
  }
  if (ds && ds.notesHeld) out.push(html`<div class="msg warn small">${ds.notesHeld}.</div>`);
  const b = p.backlog || {};
  if (b.jobs || b.logs) {
    const what = [b.jobs ? `${plural(b.jobs, "run's job list", "runs' job lists")}` : '', b.logs ? plural(b.logs, 'failed-job log') : ''].filter(Boolean).join(' and ');
    out.push(html`<div class="msg info small"><span class="spin s-run" aria-hidden="true"></span> Backfilling the history from GitHub: ${what} still to download (a poll every 20 s until done; the page fills in as it goes).</div>`);
  }
  return out;
}
function renderTabCounts() {
  const s = store.state, idx = store.idx;
  if (!s) return;
  const set = (tab, text, cls = '') => {
    const c = el.tabs.querySelector(`a[data-tab="${tab}"] .count`);
    if (c) { c.textContent = text; c.className = `count ${cls}`; c.hidden = !text; }
  };
  set('branches', String((s.branches || []).length));
  const dirty = (s.local || []).filter((l) => l.dirty).length;
  set('local', String((s.local || []).length), dirty ? 'warn' : '');
  // GitHub not read yet: unknown, not 0 / "2 running" from the cache
  set('pulls', unread('pulls') ? '?' : String((s.pulls || []).filter((p) => p.state === 'open').length), unread('pulls') ? 'warn' : '');
  const running = idx.running.length;
  set('runs', unread('runs') ? '?' : running ? `${running} running` : '', unread('runs') ? 'warn' : running ? 'run' : '');
  const open = openCount(), waiting = waitingChanges().length;
  // queued GitHub changes outrank the note count: they wait for q's Apply
  set('editor', waiting ? `${waiting} to apply` : open ? String(open) : '', waiting ? 'warn' : '');
}
function renderOffline() {
  el.view.className = 'view';
  el.view.innerHTML = String(html`<section class="offline card">
    <h2>The GitHub manager server isn't running</h2>
    <p>This page gets everything from the local server at <code>http://127.0.0.1:8767/</code>, and nothing answered there.</p>
    <p>In the Quark Project app the server is started, and restarted, for you: give it a moment. Otherwise <b>run <code>github-manager.cmd</code></b>: it starts the server in the background, with no window. This page keeps trying every few seconds and fills in by itself.</p>
    <p class="muted small">${store.fetchError}</p></section>`);
}

// ---- drawing ----
function redraw() {
  if (!store.state) return;
  // a note being typed survives the redraw: its draft is kept by notes.js, the caret is put back here
  const focus = captureFocus();
  VIEWS[activeTab].update(ctx);
  restoreFocus(focus);
  renderDrawer(false);
}
function onState() {
  const s = store.state;
  setRepo(s.repo);
  el.title.href = gh('');
  el.title.textContent = s.repo || 'floration/quark';
  const changed = s.generatedAt !== lastGenerated;
  lastGenerated = s.generatedAt;
  renderStatus(); renderBanner(); renderTabCounts();
  if (!el.view.dataset.ready) {
    el.view.dataset.ready = '1';
    const r = parseHash();
    activeTab = null;
    mountTab(r.tab);
    activeItem = r.item;
    renderDrawer(true);
    if (r.item && r.item.kind === 'run' && r.item.log) openLog(r.item.id, r.item.log, logCloser(r.item.id));
  } else if (changed) {
    redraw();
    if (store.historyLoaded) ensureHistory(true);
  } else return false;
  return true;
}

// ---- polling: every 15 s while the page is visible, every 60 s while hidden ----
async function poll() {
  clearTimeout(pollTimer);
  const [ok, notesChanged, editorChanged] = await Promise.all([loadState(), loadNotes(), loadEditorData()]);
  if (ok) {
    if (!onState() && (notesChanged || editorChanged)) { redraw(); renderTabCounts(); }
  } else {
    renderStatus(); renderBanner();
    if (!store.state) renderOffline();
  }
  schedule(ok ? null : RETRY_MS);
}
// After an Apply the server polls GitHub at once: follow it closely (every second, for up to ~15 s) until that
// poll's state arrives, instead of waiting for the 15 s timer with a deleted branch still listed.
let following = null;
function followServerPoll() {
  if (following) return following;
  const was = store.state && store.state.generatedAt;
  following = (async () => {
    for (let i = 0; i < 15; i++) {
      await new Promise((r) => setTimeout(r, 1000));
      await poll();
      if (store.state && store.state.generatedAt !== was) break;
    }
  })().finally(() => { following = null; });
  return following;
}
function schedule(wait) {
  clearTimeout(pollTimer);
  pollTimer = setTimeout(poll, wait ?? (document.visibilityState === 'visible' ? VISIBLE_MS : HIDDEN_MS));
}
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible' && Date.now() - store.lastFetchAt > 5000) poll();
});
el.refresh.addEventListener('click', async () => {
  el.refresh.disabled = true;
  el.refresh.classList.add('busy');
  try {
    const polled = await refreshNow();
    onState();
    toast(polled ? 'Polled GitHub just now' : 'GitHub was polled less than 10 s ago; showing that');
  } catch (e) {
    toast(`Refresh failed: ${e.message}`, true);
    poll();
  } finally {
    el.refresh.disabled = false;
    el.refresh.classList.remove('busy');
    schedule();
  }
});

let toastTimer;
export function toast(text, bad) {
  const t = $('toast');
  t.textContent = text;
  t.className = `toast${bad ? ' bad' : ''}`;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 3500);
}

// ---- live timers, updated in place (no re-render) ----
setInterval(() => {
  if (document.visibilityState !== 'visible') return;
  const now = Date.now();
  for (const n of document.querySelectorAll('[data-since]')) n.textContent = clock((now - ms(n.dataset.since)) / 1000);
}, 1000);
setInterval(() => {
  if (document.visibilityState !== 'visible') return;
  for (const n of document.querySelectorAll('[data-rel]')) n.textContent = rel(n.dataset.rel);
  renderStatus();
}, 10000);

// ---- small delegated behaviours shared by the views ----
document.addEventListener('click', (e) => {
  const t = e.target.closest('[data-action]');
  if (!t) return;
  const a = t.dataset.action;
  if (a === 'toggle') {
    const box = document.getElementById(t.dataset.target);
    if (box) { box.hidden = !box.hidden; t.setAttribute('aria-expanded', String(!box.hidden)); t.textContent = box.hidden ? t.dataset.more : t.dataset.less; }
  } else if (a === 'scrollto') {
    const n = document.getElementById(t.dataset.target);
    if (n) n.scrollIntoView({ behavior: 'smooth', block: 'start' });
  } else if (a === 'open-log') {
    e.preventDefault();
    location.hash = `#tab=${activeTab}&run=${t.dataset.run}&log=${t.dataset.job}`;
  } else if (a === 'row') {
    if (e.target.closest('a,button,input,select,summary')) return;
    location.hash = t.dataset.href;
  }
});
document.addEventListener('keydown', (e) => {
  const t = e.target.closest && e.target.closest('[data-action="row"]');
  if (t && (e.key === 'Enter' || e.key === ' ') && e.target === t) { e.preventDefault(); location.hash = t.dataset.href; }
});

const ctx = { hashFor, toast, ensureHistory, get tab() { return activeTab; }, get item() { return activeItem; } };
export { ctx };

// a note was added / changed / deleted (or a form opened): redraw, and fetch the history the change was logged in
initNotes({
  onChange() { if (!store.state) return; redraw(); renderTabCounts(); if (store.historyLoaded) ensureHistory(true); },
  toast,
});
// the same for the GitHub editor (structure, rules, pending changes)
initGhEdit({
  onChange() { if (!store.state) return; redraw(); renderTabCounts(); if (store.historyLoaded) ensureHistory(true); },
  toast,
  reload: followServerPoll,
});

window.addEventListener('hashchange', route);
route();
poll();
