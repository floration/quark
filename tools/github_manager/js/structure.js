// Structure: the branch map grouped by role (production, testing, infra, work, other, stale), q's role overrides and
// descriptions (kept on this PC; they set the Branches tab's kind), and the branch changes that go to GitHub (create,
// rename, delete), queued as pending changes and applied only from Pending changes.
import { store } from './store.js';
import { html, raw, relOnly, plural, enc, prChip, ghLink, branchVsTesting } from './util.js';
import { lastRunCell } from './parts.js';
import { md } from './notes.js';
import { ged, gui, act, draft, closeForm, onAction, onSubmit, onInput, queuedFor, putStructure, queueChange, redraw, LOCK } from './ghedit.js';
import { pendingBar } from './pending.js';

const GROUPS = [
  ['production', 'Production'], ['testing', 'Testing'], ['infra', 'Infra'], ['work', 'Work (claude/*)'], ['other', 'Other'], ['stale', 'Stale'],
];
const ROLE_LABEL = { production: 'production', testing: 'testing', infra: 'infra', work: 'work', other: 'other' };
const KIND_CLASS = { production: 'k-production', testing: 'k-testing', infra: 'k-infra', work: 'k-claude', other: 'k-other', stale: 'k-stale' };

function groupHint(key, s) {
  const db = s.defaultBranch || 'the default branch';
  return {
    production: html`The default branch, <code>${db}</code> (read from GitHub). ${raw(LOCK)} Only q pushes or merges here and only q tags v* releases: the editor refuses to delete or rename it and any change to its rulesets.`,
    testing: html`Where every Claude's work lands; each push publishes a TESTING prerelease. ${raw(LOCK)} Can't be deleted or renamed here.`,
    infra: html`Long-lived branches for the launcher, the website and the Discord bot. ${raw(LOCK)} <code>${(s.protectedBranches || []).filter((b) => b !== 'testing').join(', ')}</code> can't be deleted or renamed here.`,
    work: html`<code>claude/*</code> branches, one per thread (and any branch you set to work).`,
    other: html`Everything else: Dependabot and hand-made branches.`,
    stale: html`Work and other branches with no commit in <b>${plural(s.staleDays, 'day')}</b> that are merged into testing (nothing ahead) or behind it. Candidates to delete: queue a delete, then review it in Pending changes.`,
  }[key];
}

const isProtected = (b, s) => b.name === s.defaultBranch || b.kind === 'production' || (s.protectedBranches || []).includes(b.name);
const groupOf = (b) => (b.stale ? 'stale' : b.role || 'other');

function roleForm(b) {
  const fk = `st-edit:${b.name}`;
  const e = (ged.structure.branches || {})[b.name] || {};
  const d = draft(fk, { role: e.role || '', description: e.description || '' });
  const fixed = b.role === 'production' || b.role === 'testing';
  const autoRole = b.roleSet ? null : b.role;
  const err = gui.errors.get(fk), busy = gui.busy.has(fk);
  return html`<form class="gform st-form" data-gform="${fk}" novalidate>
    <div class="gf-row">
      <label class="f"><span class="f-l">Role</span>${fixed ? html`<span class="tag">${b.role} (from the repository)</span>` : html`<select data-gf="role">
        <option value=""${!d.role ? ' selected' : ''}>Automatic${autoRole ? ` (${autoRole})` : ''}</option>
        ${['work', 'infra', 'other'].map((r) => html`<option value="${r}"${d.role === r ? ' selected' : ''}>${r}</option>`)}
      </select>`}</label>
      <span class="muted small">Local only: it sets how the Branches tab and this map group the branch.</span>
    </div>
    <textarea data-gf="description" rows="2" placeholder="What this branch is for (markdown works)">${d.description}</textarea>
    <div class="gf-bar"><span class="grow"></span><button type="button" data-gact="st-cancel" data-fk="${fk}">Cancel</button><button type="submit" class="primary"${busy ? ' disabled' : ''}>${busy ? 'Saving…' : 'Save'}</button></div>
    ${err ? html`<p class="msg bad small" role="alert">${err}</p>` : ''}
  </form>`;
}

// What the server will make of a typed branch name, said while it is typed (it drops refs/heads/ and origin/).
function nameHint(raw, current) {
  const v = String(raw || '').trim(), m = /^(refs\/heads\/|origin\/)/.exec(v);
  const name = m ? v.slice(m[1].length) : v;
  const out = [];
  if (m) out.push(html`<code>${m[1]}</code> is dropped: the branch will be <code>${name}</code>.`);
  if (name && name !== current && store.idx.branches.has(name)) out.push(html`A branch <code>${name}</code> already exists.`);
  else if (name && name !== current && !name.startsWith('claude/')) out.push(html`Not under <code>claude/</code>: it won't be counted as a work branch (Claude's branches are <code>claude/*</code>).`);
  return html`<p class="small gf-note st-namehint" data-name-hint${out.length ? '' : ' hidden'}>${out.map((x) => html`${x} `)}</p>`;
}
function refreshNameHint(n, current) {
  const p = n.closest('[data-gform]').querySelector('[data-name-hint]');
  if (p) p.outerHTML = String(nameHint(n.value, current));
}

function opForm(kind, b) {
  const fk = `st-${kind}:${b.name}`;
  const d = draft(fk, { newName: b.name });
  const err = gui.errors.get(fk), busy = gui.busy.has(fk);
  if (kind === 'rename') {
    return html`<form class="gform st-op" data-gform="${fk}" novalidate>
      <label class="f gf-grow"><span class="f-l">New name</span><input type="text" data-gf="newName" value="${d.newName}" autocomplete="off" spellcheck="false"></label>
      <button type="button" data-gact="st-cancel" data-fk="${fk}">Cancel</button><button type="submit" class="primary"${busy ? ' disabled' : ''}>Queue rename</button>
      ${nameHint(d.newName, b.name)}
      <p class="muted small gf-note">Queued in Pending changes with a preview (open PRs move with it); nothing changes on GitHub until you apply it there.</p>
      ${err ? html`<p class="msg bad small gf-note" role="alert">${err}</p>` : ''}
    </form>`;
  }
  const ahead = b.ahead > 0 ? html`<b>${plural(b.ahead, 'commit')} on it ${b.ahead === 1 ? 'is' : 'are'} not in testing.</b> ` : '';
  const pr = b.pr != null ? store.idx.pulls.get(String(b.pr)) : null;
  const openPr = pr && pr.state === 'open' ? html`<b>Open PR ${prChip(pr.number, pr.title)} comes from it.</b> ` : '';
  return html`<form class="gform st-op st-del" data-gform="${fk}" novalidate>
    <p class="small">${ahead}${openPr}Queue deleting <code>${b.name}</code>? Pending changes shows the preview (read from GitHub then); nothing is deleted until you apply it there.</p>
    <button type="button" data-gact="st-cancel" data-fk="${fk}">Cancel</button><button type="submit" class="danger"${busy ? ' disabled' : ''}>Queue delete</button>
    ${err ? html`<p class="msg bad small gf-note" role="alert">${err}</p>` : ''}
  </form>`;
}

function branchRow(b, s) {
  const prot = isProtected(b, s);
  const queued = queuedFor((c) => c.type.startsWith('branch.') && (c.params.name === b.name));
  const p = b.pr != null ? store.idx.pulls.get(String(b.pr)) : null;
  const k = (f) => `st-${f}:${b.name}`;
  return html`<li class="st-row${prot ? ' is-protected' : ''}${b.stale ? ' is-stale' : ''}" data-branch="${b.name}">
    <div class="st-main">
      ${prot ? html`<span class="st-lock" title="Protected: can't be deleted or renamed from the editor">${raw(LOCK)}</span>` : ''}
      <a class="st-name" href="#branch=${enc(b.name)}" title="${b.name}">${b.name}</a>${ghLink(b.url, 'Open the branch on GitHub')}
      <span class="lane-tag ${KIND_CLASS[b.role] || ''}" title="${b.roleSet ? 'Role set by you (automatic: by its name)' : 'Role by its name'}">${ROLE_LABEL[b.role] || b.role}</span>${b.roleSet ? html`<span class="muted small">role set by you</span>` : ''}
      ${b.stale ? html`<span class="tag t-warn" title="No commit in ${s.staleDays} days, merged into or behind testing">stale</span>` : ''}
      ${queued.map((c) => html`<a class="tag t-queued" href="#tab=editor&sec=pending" title="${c.summary}">${c.type.slice(7)} queued</a>`)}
      <span class="st-facts muted small">${b.kind === 'testing' ? 'the reference' : branchVsTesting(b)}${b.date ? html` · last commit ${relOnly(b.date)}` : ''}</span>
      ${p ? html`<span class="small">${prChip(p.number, p.title)} <span class="muted">${p.state === 'open' ? 'open' : p.merged ? 'merged' : 'closed'}</span></span>` : ''}
      <span class="st-ci small">${lastRunCell(b.lastRun)}</span>
      <span class="grow"></span>
      <span class="st-acts">
        <button type="button" class="small" data-gact="st-open" data-fk="${k('edit')}">${b.description ? 'Edit' : 'Describe'}</button>
        ${prot ? '' : html`<button type="button" class="small" data-gact="st-open" data-fk="${k('rename')}">Rename…</button><button type="button" class="small danger" data-gact="st-open" data-fk="${k('delete')}">Delete…</button>`}
      </span>
    </div>
    ${gui.open.has(k('edit')) ? roleForm(b) : b.description ? html`<div class="st-desc md">${md(b.description)}</div>` : ''}
    ${gui.open.has(k('rename')) ? opForm('rename', b) : ''}
    ${gui.open.has(k('delete')) ? opForm('delete', b) : ''}
  </li>`;
}

// the stale branches a "delete all" would queue: not protected, no delete queued for them yet
const staleToDelete = (list, s) => list.filter((b) => !isProtected(b, s) && !queuedFor((c) => c.type === 'branch.delete' && c.params.name === b.name).length);
function staleAll(list, s) {
  const todo = staleToDelete(list, s);
  if (todo.length < 2 || !gui.confirm.has('st-stale-all')) return '';
  return html`<div class="msg warn small st-stale-confirm" role="alert"><p><b>Queue deleting these ${todo.length} stale branches?</b> Each is previewed from GitHub (commits not in testing, open PRs) in Pending changes; nothing is deleted until you apply them there.</p>
    <p class="st-stale-names">${todo.map((b, i) => html`${i ? ', ' : ''}<code>${b.name}</code>`)}</p>
    <p><button type="button" class="danger" data-gact="st-stale-all-yes"${gui.busy.has('st-stale-all') ? ' disabled' : ''}>${gui.busy.has('st-stale-all') ? 'Reading GitHub…' : `Queue ${todo.length} deletes`}</button><button type="button" data-gact="st-stale-all-no">Keep them</button></p>
    ${gui.errors.has('st-stale-all') ? html`<p class="err">${gui.errors.get('st-stale-all')}</p>` : ''}</div>`;
}

function createForm(s) {
  const fk = 'st-create:new';
  const d = draft(fk, { name: 'claude/', from: 'testing', sha: '' });
  const err = gui.errors.get(fk), busy = gui.busy.has(fk);
  const names = (store.state.branches || []).map((b) => b.name);
  return html`<form class="gform st-create" data-gform="${fk}" novalidate>
    <div class="gf-row">
      <label class="f gf-grow"><span class="f-l">New branch</span><input type="text" data-gf="name" value="${d.name}" placeholder="claude/…" autocomplete="off" spellcheck="false"></label>
      <label class="f"><span class="f-l">from</span><select data-gf="from">
        ${names.map((n) => html`<option value="${n}"${d.from === n ? ' selected' : ''}>${n}</option>`)}
        <option value="@sha"${d.from === '@sha' ? ' selected' : ''}>a commit (sha)…</option>
      </select></label>
      ${d.from === '@sha' ? html`<label class="f"><span class="sr">Commit sha</span><input type="text" data-gf="sha" value="${d.sha}" placeholder="commit sha" size="14" autocomplete="off" spellcheck="false"></label>` : ''}
      <button type="button" data-gact="st-cancel" data-fk="${fk}">Cancel</button>
      <button type="submit" class="primary"${busy ? ' disabled' : ''}>${busy ? 'Reading GitHub…' : 'Queue create'}</button>
    </div>
    ${nameHint(d.name === 'claude/' ? '' : d.name, null)}
    <p class="muted small">Queued in Pending changes with a preview read from GitHub (the commit it will point at); created only when you apply it there.</p>
    ${err ? html`<p class="msg bad small" role="alert">${err}</p>` : ''}
  </form>`;
}

export const structureSection = {
  mount(main) { main.innerHTML = '<div id="st-root"></div>'; },
  update() {
    const box = document.getElementById('st-root');
    if (!box) return;
    if (ged.available === false) { box.innerHTML = String(html`<section class="card"><p class="empty">The GitHub manager server running now is older than the GitHub editor. Restart it (close it and run <code>github-manager.cmd</code> again) to use Structure.</p></section>`); return; }
    const s = ged.structure;
    if (!s || !store.state) { box.innerHTML = '<section class="card"><p class="muted">Loading the structure…</p></section>'; return; }
    const all = store.state.branches || [];
    const groups = new Map(GROUPS.map(([k]) => [k, []]));
    for (const b of all) (groups.get(groupOf(b)) || groups.get('other')).push(b);
    const described = Object.values(s.branches || {}).filter((e) => e.description).length;
    const set = all.filter((b) => b.roleSet).length;
    box.innerHTML = String(html`<section class="card">
      <h2>Structure <span class="tag">local + GitHub</span><span class="grow"></span>${gui.open.has('st-create:new') ? '' : html`<button type="button" class="primary" data-gact="st-open" data-fk="st-create:new">+ New branch</button>`}</h2>
      <p class="hint">How the branches are organised. <b>Roles and descriptions stay on this PC</b> (<code>editor/structure.json</code>) and set the Branches tab's kind. <b>Creating, renaming or deleting a branch changes GitHub</b>: it is queued in Pending changes with a preview and happens only when you press Apply there.</p>
      <p class="small st-stats">${plural(all.length, 'branch', 'branches')} · ${plural(set, 'role')} set by you · ${described} described · stale after <b>${plural(s.staleDays, 'day')}</b> <a class="small" href="#tab=editor&sec=rules&rkind=limit" title="The limit '${s.staleRule}' in Rules & limits">(change)</a></p>
      ${s.loadError ? html`<p class="msg warn small">${s.loadError}</p>` : ''}
      ${pendingBar()}
      ${gui.open.has('st-create:new') ? createForm(s) : ''}
    </section>
    ${GROUPS.map(([key, label]) => {
      const list = groups.get(key);
      if (!list.length && (key === 'stale' || key === 'other')) return '';
      return html`<section class="card st-group g-${key}">
        <h2><span class="lane-tag ${KIND_CLASS[key]}">${key}</span> ${label} <span class="count">${list.length}</span>${key === 'stale' && staleToDelete(list, s).length > 1 && !gui.confirm.has('st-stale-all') ? html`<span class="grow"></span><button type="button" class="danger" data-gact="st-stale-all">Queue delete for all ${staleToDelete(list, s).length}…</button>` : ''}</h2>
        <p class="hint small">${groupHint(key, s)}</p>
        ${key === 'stale' ? staleAll(list, s) : ''}
        ${list.length ? html`<ul class="st-list">${list.map((b) => branchRow(b, s))}</ul>` : html`<p class="empty small">None.</p>`}
      </section>`;
    })}`);
  },
};

onAction('st-open', (b) => { gui.open.add(b.dataset.fk); redraw(); requestAnimationFrame(() => { const f = document.querySelector(`[data-gform="${CSS.escape(b.dataset.fk)}"] [data-gf]`); if (f) f.focus(); }); });
onAction('st-cancel', (b) => { closeForm(b.dataset.fk); redraw(); });
onAction('st-stale-all', () => { gui.confirm.add('st-stale-all'); redraw(); });
onAction('st-stale-all-no', () => { gui.confirm.delete('st-stale-all'); gui.errors.delete('st-stale-all'); redraw(); });
// one delete after another (each reads GitHub for its preview); a refused one is reported and the rest still go
onAction('st-stale-all-yes', async () => {
  const s = ged.structure;
  const todo = staleToDelete((store.state.branches || []).filter((b) => b.stale), { ...s, defaultBranch: s.defaultBranch || store.state.defaultBranch });
  const refused = [];
  let queued = 0;
  await act('st-stale-all', async () => {
    for (const b of todo) {
      try { await queueChange('branch.delete', { name: b.name }); queued++; } catch (e) { refused.push(`${b.name}: ${e.message || e}`); }
    }
    if (refused.length) throw new Error(`Queued ${queued}; not queued: ${refused.join('; ')}`);
  }, () => `Queued ${plural(queued, 'delete')}: review and apply them in Pending changes`);
  if (!refused.length) gui.confirm.delete('st-stale-all');
  redraw();
});
onInput('st-create', (fk, d, n) => { if (n.dataset.gf === 'from') redraw(); else if (n.dataset.gf === 'name') refreshNameHint(n, null); });
onInput('st-rename', (fk, d, n) => { if (n.dataset.gf === 'newName') refreshNameHint(n, fk.slice('st-rename:'.length)); });

onSubmit('st-edit', async (fk, d) => {
  const name = fk.slice('st-edit:'.length);
  const b = (store.state.branches || []).find((x) => x.name === name);
  const fixed = b && (b.role === 'production' || b.role === 'testing');
  const ok = await act(fk, () => putStructure({ [name]: { role: fixed ? null : d.role || null, description: d.description || '' } }), 'Saved (on this PC)');
  if (ok) { closeForm(fk); redraw(); }
});
onSubmit('st-create', async (fk, d) => {
  const name = (d.name || '').trim(), from = d.from === '@sha' ? (d.sha || '').trim() : d.from;
  if (!name || name === 'claude/') { gui.errors.set(fk, 'Name the new branch first.'); redraw(); return; }
  if (!from) { gui.errors.set(fk, 'Give the commit sha to start from.'); redraw(); return; }
  const ok = await act(fk, () => queueChange('branch.create', { name, from }), 'Queued: review and apply it in Pending changes');
  if (ok) { closeForm(fk); redraw(); }
});
onSubmit('st-rename', async (fk, d) => {
  const name = fk.slice('st-rename:'.length);
  const ok = await act(fk, () => queueChange('branch.rename', { name, newName: (d.newName || '').trim() }), 'Queued: review and apply it in Pending changes');
  if (ok) { closeForm(fk); redraw(); }
});
onSubmit('st-delete', async (fk) => {
  const name = fk.slice('st-delete:'.length);
  const ok = await act(fk, () => queueChange('branch.delete', { name }), 'Queued: review and apply it in Pending changes');
  if (ok) { closeForm(fk); redraw(); }
});
