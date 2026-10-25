// Branches: one table, filter by kind / CI / text, sort by age or by how far ahead.
import { store } from './store.js';
import { html, relOnly, matches, ms, enc, runState, plural, aheadBehind, prChip, ghLink, docsLink, commitChip, kindLabel, unread, unknownLine } from './util.js';
import { lastRunCell, abBar, checksBadge } from './parts.js';
import { noteBadge } from './notes.js';
import { filterState, search, select, segmented, bind } from './filters.js';

let fs;
const KINDS = [['all', 'All'], ['claude', 'Work (claude/*)'], ['main', 'Testing + production'], ['infra', 'Infra'], ['other', 'Other'], ['stale', 'Stale']];

function want(b, st) {
  if (st.kind === 'main' && !(b.kind === 'production' || b.kind === 'testing')) return false;
  if (st.kind === 'stale' && !b.stale) return false;
  if (!['all', 'main', 'stale'].includes(st.kind) && b.kind !== st.kind) return false;
  const s = runState(b.lastRun);
  if (st.ci === 'failing' && !['failure', 'timed_out', 'startup_failure'].includes(s)) return false;
  if (st.ci === 'running' && !(s === 'running' || s === 'queued' || s === 'unread')) return false;
  if (st.ci === 'passing' && s !== 'success') return false;
  if (st.ahead === 'ahead' && !(b.ahead > 0)) return false;
  return matches(`${b.name} ${b.subject} ${b.author} ${b.sha} ${b.pr ? '#' + b.pr : ''}`, st.q);
}
const SORTS = {
  recent: (a, b) => ms(b.date) - ms(a.date),
  ahead: (a, b) => (b.ahead || 0) - (a.ahead || 0) || ms(b.date) - ms(a.date),
  behind: (a, b) => (b.behind || 0) - (a.behind || 0),
  name: (a, b) => a.name.localeCompare(b.name),
};
const rank = (b) => (b.kind === 'production' ? 0 : b.kind === 'testing' ? 1 : 2);

export const branches = {
  title: 'Branches',
  mount(el) {
    fs = filterState('branches', { kind: 'all', ci: '', ahead: '', sort: 'recent', q: '' });
    el.innerHTML = String(html`<div class="toolbar" id="br-bar">
      ${segmented('kind', fs.st.kind, KINDS)}
      ${select('ci', fs.st.ci, 'Last CI', [['', 'Any'], ['failing', 'Failed'], ['running', 'Running'], ['passing', 'Passed']])}
      ${select('ahead', fs.st.ahead, 'Show', [['', 'All'], ['ahead', 'Ahead of testing only']])}
      ${select('sort', fs.st.sort, 'Sort', [['recent', 'Newest commit'], ['ahead', 'Most ahead'], ['behind', 'Most behind'], ['name', 'Name']])}
      ${search('q', fs.st.q, 'Search name, subject, sha…')}
    </div><div id="br-list"></div>`);
    bind(el.querySelector('#br-bar'), fs, () => this.update());
  },
  update() {
    const all = store.state.branches || [];
    const list = all.filter((b) => want(b, fs.st)).sort((a, b) => rank(a) - rank(b) || SORTS[fs.st.sort](a, b));
    const max = Math.max(1, ...list.map((b) => Math.max(b.ahead || 0, b.behind || 0)));
    const prsUnread = unread('pulls');
    const row = (b) => {
      const p = b.pr != null ? store.idx.pulls.get(String(b.pr)) : null;
      return html`<tr class="k-${b.kind}" data-action="row" data-href="#branch=${enc(b.name)}" tabindex="0">
        <td class="b-name"><span class="lane-tag k-${b.kind}" title="${b.roleSet ? 'Role set in the Editor (Structure)' : 'Role by its name'}">${kindLabel(b.kind)}</span> <a href="#branch=${enc(b.name)}" class="bn" title="${b.description || b.name}">${b.name}</a>${ghLink(b.url, 'Open the branch on GitHub')}${b.stale ? html` <a class="tag t-warn" href="#tab=editor&sec=structure" title="Stale: no recent commit, merged into or behind testing (see Editor, Structure)">stale</a>` : ''}${noteBadge('branch', b.name, `#branch=${enc(b.name)}`)}<span class="b-sub muted small" title="${b.subject}">${commitChip(b.sha)} ${b.subject}</span></td>
        <td class="b-head"><span class="subj" title="${b.subject}">${b.subject}</span><span class="muted small">${commitChip(b.sha)} ${b.author}</span></td>
        <td class="b-age nowrap">${relOnly(b.date)}</td>
        <td class="b-ab"><span class="ab-text">${b.kind === 'testing' ? html`<span class="muted">the reference</span>` : b.unrelated ? html`<span class="muted" title="An orphan branch: it shares no commit with testing, so there is no ahead / behind">no shared history</span>` : aheadBehind(b.ahead, b.behind).replace(' of testing', '')}</span>${b.kind === 'testing' || b.unrelated ? '' : abBar(b.ahead, b.behind, max)}</td>
        <td class="b-pr nowrap">${p ? html`${prChip(p.number, p.title)} <span class="muted small">${p.state === 'open' ? (p.draft ? 'draft' : 'open') : p.merged ? 'merged' : 'closed'}</span>${p.state === 'open' ? html` <span class="pr-checks">${checksBadge(p)}</span>` : ''}` : prsUnread ? html`<span class="tag t-warn" title="GitHub's pull requests haven't been read since the server started: unknown, not none">?</span>` : html`<span class="muted">—</span>`}</td>
        <td class="b-ci nowrap">${lastRunCell(b.lastRun)}</td>
        <td class="b-docs">${docsLink(b.name)}</td>
      </tr>`;
    };
    document.getElementById('br-list').innerHTML = String(html`<section class="card flush">
      <p class="hint pad">${plural(list.length, 'branch', 'branches')}${list.length !== all.length ? ` of ${all.length}` : ''}. Ahead / behind count commits against <b>testing</b>. The kind comes from the name, or from the role set in <a href="#tab=editor&sec=structure">Editor → Structure</a>. Click a row for its runs, PR and history.</p>
      ${unread('pulls') || unread('runs') ? html`<div class="pad">${unread('pulls') ? unknownLine('pulls') : ''}${unread('runs') ? unknownLine('runs', true) : ''}</div>` : ''}
      ${list.length ? html`<div class="tablewrap"><table class="t branches"><thead><tr><th>Branch</th><th>Head commit</th><th>Age</th><th>vs testing</th><th>PR</th><th>Last CI</th><th></th></tr></thead>
      <tbody>${list.map(row)}</tbody></table></div>` : html`<p class="empty pad">No branches match.</p>`}
    </section>`);
  },
};
