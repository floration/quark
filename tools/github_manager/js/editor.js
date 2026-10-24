// Editor: the parts of the repo's workflow q edits here, as sections in the left list (#tab=editor&sec=...):
// Notes (local), Structure (branch roles and descriptions: local; branch create / rename / delete: GitHub, queued),
// Rules & limits (GitHub rulesets: queued; agent rules: local + Claude Documents) and Pending changes (where queued
// GitHub changes are reviewed and applied, only when q presses Apply).
import { html, matches, plural } from './util.js';
import { notes, noteCard, noteForm, sortNotes, KINDS, KIND_LABEL } from './notes.js';
import { filterState, search, segmented, bind } from './filters.js';
import { ged, waitingChanges } from './ghedit.js';
import { structureSection } from './structure.js';
import { rulesSection } from './rules.js';
import { pendingSection } from './pending.js';

let fs, focusId = null, sec = 'notes';

const SECTIONS = [
  { key: 'notes', label: 'Notes', hint: 'On branches, PRs, runs, commits, versions, local copies', where: 'local + Claude Documents' },
  { key: 'structure', label: 'Structure', hint: 'Branches by role, descriptions; create, rename, delete', where: 'local · GitHub (queued)' },
  { key: 'rules', label: 'Rules & limits', hint: 'GitHub rulesets; the rules every Claude follows', where: 'GitHub (queued) · local + Claude Documents' },
  { key: 'pending', label: 'Pending changes', hint: 'Review the diff, then apply to GitHub', where: 'GitHub, on Apply only' },
];

function want(n, st) {
  if (st.kind !== 'all' && n.target.kind !== st.kind) return false;
  if (st.status !== 'all' && n.status !== st.status) return false;
  return matches(`${n.text} ${n.author} ${n.target.kind} ${KIND_LABEL[n.target.kind]} ${n.target.key} ${n.id}`, st.q);
}

const notesSection = {
  mount(main) {
    fs = filterState('editor', { kind: 'all', status: 'open', q: '' });
    if (focusId) { fs.set('kind', 'all'); fs.set('status', 'all'); fs.set('q', ''); }
    main.innerHTML = String(html`<section class="card">
        <h2>Notes <span class="tag">local + Claude Documents</span></h2>
        <p class="hint">Write a note about a branch, PR, run, commit, version or local copy, or a general one. It shows on that item's rows and details; pinned notes show on the Overview. The server keeps them in <code>editor/notes.json</code> (previous version: <code>notes.json.bak</code>) and as <code>editor/notes.md</code> for other tools and agents, and writes them to the Claude Documents as <code>10-github-notes.md</code>; every change is in the History tab. Nothing is sent to GitHub.</p>
        <div id="ed-compose"></div>
      </section>
      <div class="toolbar" id="ed-bar">
        ${segmented('kind', fs.st.kind, [['all', 'All'], ...KINDS.map(([k, l]) => [k, l === 'Pull request' ? 'PR' : l])])}
        ${segmented('status', fs.st.status, [['open', 'Open'], ['done', 'Done'], ['all', 'All']])}
        ${search('q', fs.st.q, 'Search notes…')}
      </div>
      <div id="ed-list"></div>`);
    bind(main.querySelector('#ed-bar'), fs, () => this.update());
  },
  update() {
    const box = document.getElementById('ed-list'), comp = document.getElementById('ed-compose');
    if (!box) return;
    if (notes.available === false) {
      comp.innerHTML = '';
      box.innerHTML = '<section class="card"><p class="empty">The GitHub manager server running now is older than the notes. Restart it (close it and run <code>github-manager.cmd</code> again) to use them.</p></section>';
      return;
    }
    if (!notes.list) { box.innerHTML = '<section class="card"><p class="muted">Loading the notes…</p></section>'; return; }
    comp.innerHTML = String(html`${notes.loadError ? html`<p class="msg warn small">${notes.loadError}</p>` : ''}${noteForm({ fk: 'e:new', pickTarget: true, cancel: false })}`);
    const all = notes.list, open = all.filter((n) => n.status === 'open').length;
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

const IMPL = { notes: notesSection, structure: structureSection, rules: rulesSection, pending: pendingSection };

function navCounts() {
  const set = (key, n, cls = '') => {
    const c = document.getElementById(`ed-n-${key}`);
    if (c) { c.textContent = String(n); c.className = `count ${cls}`; c.hidden = !n; }
  };
  set('notes', (notes.list || []).filter((n) => n.status === 'open').length);
  set('pending', waitingChanges().length, 'warn');
  const failed = ((ged.pending && ged.pending.changes) || []).filter((c) => ['failed', 'stale', 'refused'].includes(c.status)).length;
  const f = document.getElementById('ed-n-pending-bad');
  if (f) { f.hidden = !failed; f.textContent = failed ? `${failed} need${failed === 1 ? 's' : ''} a look` : ''; }
  const ds = ged.rules && ged.rules.docsSync;
  const d = document.getElementById('ed-docs');
  if (d && ds) {
    d.className = `ed-nav-note small ${ds.state === 'failed' ? 'err' : 'muted'}`;
    d.textContent = ds.state === 'off' ? `Claude Documents copy: ${ds.offReason || 'off'}.`
      : ds.state === 'failed' ? 'The Claude Documents copy failed: see Rules & limits.'
        : ds.state === 'waiting' || ds.state === 'syncing' ? 'Writing the rules and notes to the Claude Documents…'
          : 'Notes and agent rules are also written to the Claude Documents. GitHub changes wait in Pending changes until you apply them.';
  }
}

export const editor = {
  title: 'Editor',
  mount(el) {
    const p = new URLSearchParams(location.hash.replace(/^#/, ''));
    focusId = p.get('note');
    sec = IMPL[p.get('sec')] ? p.get('sec') : 'notes';
    if (focusId) sec = 'notes';
    el.innerHTML = String(html`<div class="ed-layout">
      <nav class="ed-nav" aria-label="Editor sections">
        ${SECTIONS.map((s) => html`<a class="ed-sec${s.key === sec ? ' on' : ''}" href="#tab=editor&sec=${s.key}"${s.key === sec ? ' aria-current="page"' : ''}>
          <span class="ed-sec-l">${s.label} <span class="count" id="ed-n-${s.key}" hidden></span></span>
          <span class="ed-sec-h">${s.hint}</span>
          <span class="ed-sec-w">${s.where}</span>
          ${s.key === 'pending' ? html`<span class="ed-sec-bad err small" id="ed-n-pending-bad" hidden></span>` : ''}</a>`)}
        <p class="ed-nav-note muted small" id="ed-docs">Notes and agent rules are also written to the Claude Documents. GitHub changes wait in Pending changes until you apply them.</p>
      </nav>
      <div class="ed-main" id="ed-main"></div>
    </div>`);
    IMPL[sec].mount(el.querySelector('#ed-main'));
  },
  update() {
    navCounts();
    IMPL[sec].update();
  },
};
