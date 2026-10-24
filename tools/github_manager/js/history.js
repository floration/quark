// History: the server's local log (history.jsonl), a timeline grouped by Dallas day.
import { store } from './store.js';
import { html, matches, fmtDay, dayKey, plural } from './util.js';
import { eventRow, TYPE_GROUPS, groupOf } from './parts.js';
import { filterState, search, select, bind, refillSelect } from './filters.js';

let fs, root, shown = 300;
const refKey = (r) => (!r ? '' : r.pr != null ? `pr:${r.pr}` : r.branch ? `branch:${r.branch}` : r.local ? `local:${r.local}` : r.run != null ? `run:${r.run}` : r.tag ? `tag:${r.tag}` : '');

function refMatches(e, want) {
  if (!want) return true;
  if (refKey(e.ref) === want) return true;
  // a run belongs to its branch; a PR's events also match its head branch and vice versa
  const [k, v] = [want.slice(0, want.indexOf(':')), want.slice(want.indexOf(':') + 1)];
  const r = e.ref || {};
  if (k === 'branch') {
    if (r.run != null) { const run = store.idx.runs.get(String(r.run)); if (run && run.branch === v) return true; }
    if (r.pr != null) { const p = store.idx.pulls.get(String(r.pr)); if (p && p.head === v) return true; }
    if (r.local) { const l = store.idx.local.get(r.local); if (l && l.branch === v) return true; }
  }
  if (k === 'pr' && r.branch) { const p = store.idx.pulls.get(v); if (p && p.head === r.branch) return true; }
  if (k === 'pr' && r.run != null) { const p = store.idx.pulls.get(v); const run = store.idx.runs.get(String(r.run)); if (p && run && run.branch === p.head) return true; }
  return false;
}

export const history = {
  title: 'History',
  wantsHistory: true,
  mount(el) {
    const def = { q: '', ref: '' };
    for (const g of TYPE_GROUPS) def[`t_${g.key}`] = true;
    fs = filterState('history', def);
    for (const g of TYPE_GROUPS) if (typeof fs.st[`t_${g.key}`] === 'string') fs.st[`t_${g.key}`] = fs.st[`t_${g.key}`] !== '0';
    shown = 300;
    el.innerHTML = String(html`<div class="toolbar" id="hi-bar">
      <span class="f-l">Show</span>
      ${TYPE_GROUPS.map((g) => html`<label class="chk"><input type="checkbox" data-f="t_${g.key}"${fs.st[`t_${g.key}`] ? ' checked' : ''}> ${g.label}</label>`)}
      ${select('ref', fs.st.ref, 'For', [['', 'Everything']])}
      ${search('q', fs.st.q, 'Search the log…')}
    </div><div id="hi-list"></div>`);
    root = el;
    bind(el.querySelector('#hi-bar'), fs, () => { shown = 300; this.update(); });
    el.addEventListener('click', (e) => {
      if (e.target.closest('[data-more-history]')) { shown += 500; this.update(); }
    });
  },
  update() {
    const box = document.getElementById('hi-list');
    if (!store.historyLoaded) { box.innerHTML = '<section class="card"><p class="muted">Loading the local log…</p></section>'; return; }
    const evs = store.history;
    // the "For" list: branches and PRs (plus local copies) that appear in the log or the state
    const refs = new Map();
    for (const b of store.state.branches || []) refs.set(`branch:${b.name}`, `branch ${b.name}`);
    for (const p of store.state.pulls || []) refs.set(`pr:${p.number}`, `PR #${p.number} · ${p.title.slice(0, 50)}`);
    for (const l of store.state.local || []) refs.set(`local:${l.name}`, `local copy ${l.name}`);
    for (const e of evs) { const k = refKey(e.ref); if (k && !refs.has(k) && !k.startsWith('run:')) refs.set(k, k.replace(':', ' ')); }
    const opts = [...refs.entries()].sort((a, b) => a[0].localeCompare(b[0]));
    refillSelect(root, 'ref', fs.st.ref, [['', 'Everything'], ...opts]);
    const st = fs.st;
    const list = evs.filter((e) => st[`t_${groupOf(e.type)}`] !== false && refMatches(e, st.ref) && matches(`${e.text} ${e.type} ${JSON.stringify(e.ref || {})}`, st.q));
    const days = [];
    let cur = null;
    for (const e of list.slice(0, shown)) {
      const k = dayKey(e.at);
      if (!cur || cur.k !== k) { cur = { k, at: e.at, evs: [] }; days.push(cur); }
      cur.evs.push(e);
    }
    const oldest = evs.length ? evs[evs.length - 1].at : null;
    box.innerHTML = String(html`
      <p class="hint">${plural(list.length, 'event')}${list.length !== evs.length ? ` of ${evs.length}` : ''} in the local log${oldest ? html`, which goes back to ${fmtDay(oldest)}` : ''}. The server appends to it on every poll and never trims it.</p>
      ${days.length ? days.map((d) => html`<section class="card day"><h2 class="day-h">${fmtDay(d.at)} <span class="count">${d.evs.length}</span></h2><ul class="events">${d.evs.map((e) => eventRow(e))}</ul></section>`) : html`<section class="card"><p class="empty">Nothing in the log matches.</p></section>`}
      ${list.length > shown ? html`<p class="center"><button data-more-history>Show older events (${list.length - shown} more)</button></p>` : ''}`);
  },
};
