// Local copies on q's PC: branch, head, uncommitted files, commits not on testing, version (branch-centred;
// the editor dashboard has the per-program file detail).
import { store } from './store.js';
import { html, relOnly, plural, enc, branchChip, commitChip, versionChip, aheadBehind, dashLink, docsLink, gh } from './util.js';
import { commitList } from './parts.js';

export function localCard(l, { full = false } = {}) {
  const b = store.idx.branches.get(l.branch);
  const p = b ? (b.pr != null ? store.idx.pulls.get(String(b.pr)) : null) : store.idx.pullByHead.get(l.branch);
  const files = l.dirtyFiles || [];
  const boxId = `dirty-${l.name.replace(/\W/g, '_')}`;
  return html`<section class="card local-card${l.dirty ? ' is-dirty' : ''}${l.error ? ' is-error' : ''}" id="local-${enc(l.name)}">
    <header class="lc-head">
      <h2><a href="#local=${enc(l.name)}">${l.name}</a></h2>
      ${l.worktreeOf ? html`<span class="tag">worktree of ${l.worktreeOf}</span>` : ''}
      ${l.dirty ? html`<span class="tag t-dirty">${plural(l.dirty, 'uncommitted file')}</span>` : html`<span class="tag t-clean">clean</span>`}
      <span class="grow"></span>
      <a class="small" href="${dashLink('view=changes&program=overview')}" target="quark-dashboard" title="The editor dashboard's changes, by program">changes by program</a>
      <a class="small" href="${dashLink(`branch=${enc(l.branch || '')}`)}" target="quark-dashboard">branch in dashboard</a>
    </header>
    <div class="lc-path mono muted" title="${l.path}">${l.path}</div>
    ${l.error ? html`<div class="msg bad small">${l.error}</div>` : ''}
    <dl class="kv">
      <dt>Branch</dt><dd>${branchChip(l.branch)} ${l.upstream ? html`<span class="muted small">tracks ${l.upstream}</span>` : html`<span class="muted small">no upstream</span>`} ${p ? html`<span class="small">PR ${html`<a href="#pr=${p.number}">#${p.number}</a>`} ${p.state === 'open' ? 'open' : p.merged ? 'merged' : 'closed'}</span>` : ''} ${docsLink(l.branch || l.name)}</dd>
      <dt>Head</dt><dd>${commitChip(l.sha, null)} <span class="subj">${l.subject}</span> <span class="muted">${relOnly(l.date)}</span>${b && b.sha !== l.sha ? html` <span class="tag t-warn" title="The local head isn't GitHub's ${l.branch} head (${b.sha.slice(0, 7)}): unpushed commits, or not pulled yet">GitHub's head is ${b.sha.slice(0, 7)}</span>` : ''}</dd>
      <dt>Version</dt><dd>${versionChip(l.version)} <span class="muted small">(VERSION file)</span></dd>
      <dt>vs testing</dt><dd>${aheadBehind(l.aheadOfTesting, l.behindTesting)}${l.aheadOfProduction != null ? html` <span class="muted">· ${plural(l.aheadOfProduction, 'commit')} ahead of production</span>` : ''}</dd>
    </dl>
    ${l.dirty ? html`<h3>Uncommitted files</h3>
      <ul class="files" id="${boxId}">${files.slice(0, full ? 50 : 8).map((f) => { const m = /^\s*(\S{1,2})\s+(.*)$/.exec(f) || [0, '', f]; return html`<li title="${f}"><span class="fst">${m[1]}</span> <span class="mono">${m[2]}</span></li>`; })}</ul>
      ${!full && files.length > 8 ? html`<p class="small"><a href="#local=${enc(l.name)}">and ${files.length - 8} more</a></p>` : ''}
      ${l.dirty > files.length ? html`<p class="small muted">${l.dirty - files.length} more not listed (the server lists the first ${files.length}).</p>` : ''}` : ''}
    <h3>Commits not on testing ${l.commits && l.commits.length ? html`<span class="count">${l.commits.length}</span>` : ''}</h3>
    ${commitList(l.commits, { limit: full ? 100 : 5, empty: 'None: everything here is on testing.', id: `lc-more-${l.name.replace(/\W/g, '_')}`, showAuthor: false })}
    ${l.aheadOfTesting ? html`<p class="small"><a href="${gh(`/compare/testing...${(l.branch || '').split('/').map(enc).join('/')}`)}" target="_blank" rel="noopener">Compare ${l.branch} with testing on GitHub</a> <span class="muted">(shows what's pushed)</span></p>` : ''}
  </section>`;
}

export const local = {
  title: 'Local copies',
  mount(el) { el.innerHTML = '<p class="hint">Each copy of the repo on this PC (the server only reads them: <code>git status</code> and <code>git worktree list</code>). Ahead / behind count commits against GitHub\'s testing.</p><div id="lc-list" class="lc-grid"></div>'; },
  update() {
    const ls = store.state.local || [];
    document.getElementById('lc-list').innerHTML = ls.length ? ls.map((l) => String(localCard(l))).join('') : '<p class="empty">No local copies found. The server looks for them in its config.</p>';
  },
};
