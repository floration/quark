// CI runs: running first, then newest; filter by branch, workflow, result and text.
import { store } from './store.js';
import { html, RUNNING, runState, matches, plural, ms, unread, unknownLine } from './util.js';
import { runRow } from './parts.js';
import { filterState, search, select, segmented, bind, refillSelect } from './filters.js';

let fs, root;
const RESULTS = [['all', 'All'], ['running', 'Running'], ['failure', 'Failed'], ['success', 'Passed'], ['cancelled', 'Cancelled / skipped']];

function want(r, st) {
  const s = runState(r);
  if (st.result === 'running' && !(s === 'running' || s === 'queued' || s === 'unread')) return false;
  if (st.result === 'failure' && !['failure', 'timed_out', 'startup_failure'].includes(s)) return false;
  if (st.result === 'success' && s !== 'success') return false;
  if (st.result === 'cancelled' && !['cancelled', 'skipped'].includes(s)) return false;
  if (st.on && r.branch !== st.on) return false;
  if (st.wf && r.workflow !== st.wf) return false;
  return matches(`${r.title} ${r.branch} ${r.workflow} ${r.sha} ${r.id} ${r.event} ${(r.jobs || []).map((j) => j.name).join(' ')}`, st.q);
}

export const runs = {
  title: 'CI runs',
  mount(el) {
    fs = filterState('runs', { result: 'all', on: '', wf: '', q: '' });
    el.innerHTML = String(html`<div class="toolbar" id="runs-bar">
      ${segmented('result', fs.st.result, RESULTS)}
      ${select('on', fs.st.on, 'Branch', [['', 'All branches']])}
      ${select('wf', fs.st.wf, 'Workflow', [['', 'All workflows']])}
      ${search('q', fs.st.q, 'Search title, sha, job…')}
    </div><div id="runs-list"></div>`);
    root = el;
    bind(el.querySelector('#runs-bar'), fs, () => this.update());
  },
  update() {
    const s = store.state, all = s.runs || [];
    const branches = [...new Set(all.map((r) => r.branch).filter(Boolean))].sort();
    const wfs = [...new Set(all.map((r) => r.workflow).filter(Boolean))].sort();
    refillSelect(root, 'on', fs.st.on, [['', 'All branches'], ...branches.map((b) => [b, b])]);
    refillSelect(root, 'wf', fs.st.wf, [['', 'All workflows'], ...wfs.map((w) => [w, w])]);
    const list = all.filter((r) => want(r, fs.st));
    const run = list.filter((r) => RUNNING.has(r.status)).sort((a, b) => ms(a.createdAt) - ms(b.createdAt));
    const done = list.filter((r) => !RUNNING.has(r.status));
    // GitHub's runs not read since the server started: the list is the local cache, and its "running" ones are only
    // what was running when GitHub was last read
    const stale = unread('runs');
    const staleRun = all.filter((r) => RUNNING.has(r.status)).length;
    document.getElementById('runs-list').innerHTML = String(html`
      ${stale ? html`<section class="card">${unknownLine('runs', true)}</section>` : ''}
      ${run.length ? html`<section class="card"><h2>${stale ? 'Last seen running' : 'Running now'} <span class="count ${stale ? 'warn' : 'run'}">${run.length}</span></h2><ul class="rows cols">${run.map((r) => runRow(r))}</ul></section>` : ''}
      ${fs.st.result === 'running' ? (run.length ? '' : html`<section class="card"><p class="empty">${stale ? (staleRun ? 'No run that was running when GitHub was last read matches.' : 'Nothing was running when GitHub was last read.') : 'Nothing is running right now.'}</p></section>`) : html`<section class="card"><h2>${run.length ? 'Finished' : 'Runs'} <span class="count">${done.length}</span></h2>
        <p class="hint">${s.runsTotal > all.length ? html`The newest ${all.length} of the ${s.runsTotal} runs the server keeps (an older one opens by its link)` : html`${plural(all.length, 'run')}: every run the server keeps`}. It keeps every run GitHub lists, fetched once and stored on this PC. Click a run for its jobs and logs.</p>
        ${done.length ? html`<ul class="rows cols">${done.slice(0, 200).map((r) => runRow(r))}</ul>` : html`<p class="empty">No runs match.</p>`}
        ${done.length > 200 ? html`<p class="small muted">Showing the newest 200; narrow the filters to see older ones.</p>` : ''}
      </section>`}`);
  },
};
