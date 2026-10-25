// Pull requests: open first, then the recent closed / merged ones.
import { store } from './store.js';
import { html, matches, ms, gh, unread, unknownLine } from './util.js';
import { prRow } from './parts.js';
import { filterState, search, segmented, bind } from './filters.js';

let fs;
export const pulls = {
  title: 'Pull requests',
  mount(el) {
    fs = filterState('pulls', { state: 'all', q: '' });
    el.innerHTML = String(html`<div class="toolbar" id="pr-bar">
      ${segmented('state', fs.st.state, [['all', 'All'], ['open', 'Open'], ['merged', 'Merged'], ['closed', 'Closed unmerged']])}
      ${search('q', fs.st.q, 'Search title, branch, #number…')}
      <span class="grow"></span><a class="small" href="${gh('/pulls')}" target="_blank" rel="noopener">Pull requests on GitHub</a>
    </div><div id="pr-list"></div>`);
    bind(el.querySelector('#pr-bar'), fs, () => this.update());
  },
  update() {
    const st = fs.st;
    const all = store.state.pulls || [];
    const ok = (p) => (st.state === 'all' || (st.state === 'open' && p.state === 'open') || (st.state === 'merged' && p.merged)
      || (st.state === 'closed' && p.state === 'closed' && !p.merged)) && matches(`#${p.number} ${p.title} ${p.head} ${p.base} ${p.author}`, st.q);
    const list = all.filter(ok);
    const open = list.filter((p) => p.state === 'open').sort((a, b) => ms(b.updatedAt) - ms(a.updatedAt));
    const closed = list.filter((p) => p.state !== 'open').sort((a, b) => ms(b.mergedAt || b.updatedAt) - ms(a.mergedAt || a.updatedAt));
    // not read since the server started: what's open (and what closed) is unknown, so no "Open 0 / none" under it
    if (unread('pulls')) {
      document.getElementById('pr-list').innerHTML = String(html`<section class="card"><h2>Open <span class="count warn">?</span></h2>${unknownLine('pulls')}<p class="small muted">What's open and what closed show here once the server reads GitHub (sign in with <code>gh auth login</code>, then Refresh).</p></section>`);
      return;
    }
    document.getElementById('pr-list').innerHTML = String(html`
      ${st.state === 'all' || st.state === 'open' ? html`<section class="card"><h2>Open <span class="count">${open.length}</span></h2>
        ${open.length ? html`<ul class="rows cols">${open.map(prRow)}</ul>` : html`<p class="empty">No open pull requests${st.q ? ' match' : ''}.</p>`}</section>` : ''}
      ${st.state !== 'open' ? html`<section class="card"><h2>Recently closed <span class="count">${closed.length}</span></h2>
        <p class="hint">The last 30 closed pull requests (${all.filter((p) => p.merged).length} merged).</p>
        ${closed.length ? html`<ul class="rows cols">${closed.map(prRow)}</ul>` : html`<p class="empty">None match.</p>`}</section>` : ''}`);
  },
};
