// Editor: the parts of the repo's workflow q edits here. Notes now; Structure and Rules come next, as sibling
// sections in the left list (whether they change GitHub itself is still being decided).
import { html, matches, plural } from './util.js';
import { notes, noteCard, noteForm, sortNotes, KINDS, KIND_LABEL } from './notes.js';
import { filterState, search, segmented, bind } from './filters.js';

let fs, focusId = null;

const SECTIONS = [
  { key: 'notes', label: 'Notes', hint: 'On branches, PRs, runs, commits, versions, local copies' },
  { key: 'structure', label: 'Structure', soon: true, hint: 'Branches, workflows and how they connect' },
  { key: 'rules', label: 'Rules & limits', soon: true, hint: 'Branch protection, required checks, limits' },
];

function want(n, st) {
  if (st.kind !== 'all' && n.target.kind !== st.kind) return false;
  if (st.status !== 'all' && n.status !== st.status) return false;
  return matches(`${n.text} ${n.author} ${n.target.kind} ${KIND_LABEL[n.target.kind]} ${n.target.key} ${n.id}`, st.q);
}

export const editor = {
  title: 'Editor',
  mount(el) {
    const p = new URLSearchParams(location.hash.replace(/^#/, ''));
    focusId = p.get('note');
    fs = filterState('editor', { kind: 'all', status: 'open', q: '' });
    if (focusId) { fs.set('kind', 'all'); fs.set('status', 'all'); fs.set('q', ''); }
    el.innerHTML = String(html`<div class="ed-layout">
      <nav class="ed-nav" aria-label="Editor sections">
        ${SECTIONS.map((s) => (s.soon
          ? html`<span class="ed-sec is-soon" aria-disabled="true" title="${s.hint}: coming next"><span class="ed-sec-l">${s.label}</span><span class="tag">coming next</span><span class="ed-sec-h">${s.hint}</span></span>`
          : html`<a class="ed-sec on" href="#tab=editor" aria-current="page"><span class="ed-sec-l">${s.label} <span class="count" id="ed-n-count" hidden></span></span><span class="ed-sec-h">${s.hint}</span></a>`))}
        <p class="ed-nav-note muted small">Structure and rules are next. Whether they change GitHub itself is still being decided; notes stay on this PC.</p>
      </nav>
      <div class="ed-main">
        <section class="card">
          <h2>Notes</h2>
          <p class="hint">Write a note about a branch, PR, run, commit, version or local copy, or a general one. It shows on that item's rows and details; pinned notes show on the Overview. The server keeps them in <code>editor/notes.json</code> (previous version: <code>notes.json.bak</code>) and as <code>editor/notes.md</code> for other tools and agents; every change is in the History tab.</p>
          <div id="ed-compose"></div>
        </section>
        <div class="toolbar" id="ed-bar">
          ${segmented('kind', fs.st.kind, [['all', 'All'], ...KINDS.map(([k, l]) => [k, l === 'Pull request' ? 'PR' : l])])}
          ${segmented('status', fs.st.status, [['open', 'Open'], ['done', 'Done'], ['all', 'All']])}
          ${search('q', fs.st.q, 'Search notes…')}
        </div>
        <div id="ed-list"></div>
      </div>
    </div>`);
    bind(el.querySelector('#ed-bar'), fs, () => this.update());
  },
  update() {
    const box = document.getElementById('ed-list'), comp = document.getElementById('ed-compose');
    if (!box) return;
    const cnt = document.getElementById('ed-n-count');
    if (notes.available === false) {
      comp.innerHTML = '';
      box.innerHTML = '<section class="card"><p class="empty">The GitHub manager server running now is older than the notes. Restart it (close it and run <code>github-manager.cmd</code> again) to use them.</p></section>';
      return;
    }
    if (!notes.list) { box.innerHTML = '<section class="card"><p class="muted">Loading the notes…</p></section>'; return; }
    comp.innerHTML = String(html`${notes.loadError ? html`<p class="msg warn small">${notes.loadError}</p>` : ''}${noteForm({ fk: 'e:new', pickTarget: true, cancel: false })}`);
    const all = notes.list, open = all.filter((n) => n.status === 'open').length;
    if (cnt) { cnt.textContent = String(open); cnt.hidden = !open; }
    const list = sortNotes(all.filter((n) => want(n, fs.st)));
    box.innerHTML = String(html`<section class="card">
      <p class="hint">${plural(list.length, 'note')}${list.length !== all.length ? ` of ${all.length}` : ''} · ${open} open, ${all.length - open} done in all.</p>
      ${list.length ? html`<ul class="notes">${list.map((n) => noteCard(n, { scope: 'e' }))}</ul>`
        : html`<p class="empty">${all.length ? 'No notes match.' : 'No notes yet. Write the first one above.'}</p>`}
    </section>`);
    if (focusId) {
      const n = document.getElementById(`e-note-${focusId}`);
      focusId = null;
      if (n) { n.classList.add('flash'); n.scrollIntoView({ block: 'center' }); }
    }
  },
};
