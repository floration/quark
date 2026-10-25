// Rules & limits, two kinds, kept apart on purpose:
//  - "Enforced by GitHub": the repository's rulesets, read from GitHub. Branch rulesets for non-production branches
//    can be created / changed / deleted with a form; each edit is queued as a pending change (JSON before -> after,
//    the exact call) and applied only from Pending changes. Rulesets on the default branch or v* tags are locked.
//  - "Followed by every Claude": the agent rules and limits (editor/rules.json, also written to the Claude
//    Documents). Nothing enforces them but the agents themselves.
import { html, raw, relOnly, matches, short } from './util.js';
import { md } from './notes.js';
import { filterState, search, segmented, bind } from './filters.js';
import { ged, gui, act, draft, closeForm, onAction, onSubmit, onInput, putRules, queueChange, loadRulesets, loadEditorData, queuedFor, redraw, LOCK } from './ghedit.js';
import { pendingBar } from './pending.js';

const KIND_LABEL = { push: 'push', merge: 'merge', version: 'version', ci: 'CI', limit: 'limit', other: 'other' };
let fs;

// ---- the production lock, as the page can tell before asking the server (the server decides) ----
// wide: matches at least what GitHub matches (for Branches); narrow: at most (for Except, so it counts only when it
// surely leaves the default branch out). The same two the server uses.
const globRe = (p) => new RegExp(`^${p.replace(/\[[^\]]*\]/g, '\u0001').replace(/[.+^${}()|\\]/g, '\\$&').replace(/\*+/g, '.*').replace(/\?/g, '.').replace(/\u0001/g, '.')}$`);
const narrowRe = (p) => (p.includes('[') ? null : new RegExp(`^${p.split(/(\*\*|\*|\?)/).map((x) => ({ '**': '.*', '*': '[^/]*', '?': '[^/]' }[x] || x.replace(/[.+^${}()|\\]/g, '\\$&'))).join('')}$`));
const full = (p) => (p.startsWith('refs/') ? p : `refs/heads/${p}`);
function lockProblems(include, exclude, defaultBranch) {
  const out = [];
  // claude/* may cover the default branch (it sits under claude/) when Except surely takes it out again
  const spared = defaultBranch && exclude.some((x) => x === '~DEFAULT_BRANCH' || narrowRe(full(x))?.test(`refs/heads/${defaultBranch}`));
  for (const raw0 of include) {
    const p = raw0.trim();
    if (!p) continue;
    if (p === '~DEFAULT_BRANCH') out.push('~DEFAULT_BRANCH is the default branch');
    else if (p === '~ALL') out.push('~ALL includes the default branch');
    else if (defaultBranch && !spared && globRe(full(p)).test(`refs/heads/${defaultBranch}`)) out.push(`${p} matches the default branch ${defaultBranch} (add it under Except to leave it out)`);
  }
  return out;
}
const lines = (t) => String(t || '').split(/[\n,]+/).map((x) => x.trim()).filter(Boolean);

// ---- Enforced by GitHub ----
function enforcement(e) {
  return e === 'active' ? html`<span class="badge s-good"><span class="ico" aria-hidden="true">✓</span>active</span>`
    : html`<span class="badge s-off"><span class="ico" aria-hidden="true">–</span>${e}</span>`;
}

function rulesetForm(fk, rs) {
  const v = ged.rulesets || {};
  const base = rs ? rs.form : { name: '', enforcement: 'active', include: ['testing'], exclude: [], blockForcePush: true, blockDeletion: true, linearHistory: false, requirePr: false, approvals: 0, requiredChecks: [], strictChecks: false, adminBypass: false };
  const d = draft(fk, () => ({ ...base, include: base.include.join('\n'), exclude: base.exclude.join('\n'), requiredChecks: [...base.requiredChecks], chkq: '' }));
  const err = gui.errors.get(fk), busy = gui.busy.has(fk);
  const problems = lockProblems(lines(d.include), lines(d.exclude), v.defaultBranch);
  const checks = [...new Set([...(v.checks || []), ...d.requiredChecks])].sort((a, b) => a.localeCompare(b));
  const shown = checks.filter((c) => d.requiredChecks.includes(c) || matches(c, d.chkq));
  const ck = (f, label, hint) => html`<label class="chk rs-opt"><input type="checkbox" data-gf="${f}"${d[f] ? ' checked' : ''}> <span><b>${label}</b>${hint ? html` <span class="muted small">${hint}</span>` : ''}</span></label>`;
  return html`<form class="gform rs-form" data-gform="${fk}" novalidate>
    <div class="gf-row">
      <label class="f gf-grow"><span class="f-l">Name</span><input type="text" data-gf="name" value="${d.name}" placeholder="e.g. testing guard" maxlength="100" autocomplete="off"></label>
      <label class="f"><span class="f-l">Enforcement</span><select data-gf="enforcement">${['active', 'disabled'].map((e) => html`<option value="${e}"${d.enforcement === e ? ' selected' : ''}>${e}</option>`)}</select></label>
    </div>
    <div class="rs-cols">
      <label class="f f-col"><span class="f-l">Branches (one per line; <code>*</code> and <code>**</code> patterns work)</span><textarea data-gf="include" rows="3" spellcheck="false" placeholder="testing">${d.include}</textarea></label>
      <label class="f f-col"><span class="f-l">Except (optional)</span><textarea data-gf="exclude" rows="3" spellcheck="false">${d.exclude}</textarea></label>
    </div>
    ${problems.length ? html`<p class="msg bad small" role="alert">${raw(LOCK)} The production lock will refuse this: ${problems.join('; ')}. Name the branches instead, or leave the default branch out under Except (production is q's; only q changes its rules, on GitHub).</p>` : ''}
    <fieldset class="rs-rules"><legend>Rules</legend>
      ${ck('blockForcePush', 'Block force pushes', '(non_fast_forward)')}
      ${ck('blockDeletion', 'Block deletion', '(deletion)')}
      ${ck('linearHistory', 'Require linear history', '(no merge commits)')}
      <div class="rs-opt-row">${ck('requirePr', 'Require a pull request', '(pull_request)')}${d.requirePr ? html`<label class="f"><span class="f-l">approvals</span><input type="number" data-gf="approvals" value="${d.approvals}" min="0" max="10" step="1" class="num"></label>` : ''}</div>
      <div class="rs-checks">
        <div class="rs-checks-h"><b>Required status checks</b> <span class="muted small">${d.requiredChecks.length ? `${d.requiredChecks.length} chosen` : 'none'} · names seen in recent runs</span>
          <label class="f"><span class="sr">Filter checks</span><input type="search" data-gf="chkq" value="${d.chkq}" placeholder="Filter…" autocomplete="off"></label>
          ${d.requiredChecks.length ? ck('strictChecks', 'Branch must be up to date', '(strict)') : ''}</div>
        <div class="rs-checklist">${shown.length ? shown.map((c) => html`<label class="chk"><input type="checkbox" data-gf="requiredChecks" data-v="${c}"${d.requiredChecks.includes(c) ? ' checked' : ''}> ${c}</label>`) : html`<span class="muted small">${checks.length ? 'No check matches.' : 'No check names seen yet (they come from the CI runs).'}</span>`}</div>
      </div>
      ${ck('adminBypass', 'Repository admins can bypass', '(you, when you need to)')}
    </fieldset>
    ${rs && rs.otherRules && rs.otherRules.length ? html`<p class="muted small">Kept as they are (the form doesn't edit them): ${rs.otherRules.join(', ')}.</p>` : ''}
    <div class="gf-bar"><span class="muted small">Queued in Pending changes with the JSON diff and the exact <code>gh api</code> call; GitHub changes only when you apply it there.</span><span class="grow"></span>
      <button type="button" data-gact="rl-cancel" data-fk="${fk}">Cancel</button><button type="submit" class="primary"${busy ? ' disabled' : ''}>${busy ? 'Reading GitHub…' : rs ? 'Queue change' : 'Queue new ruleset'}</button></div>
    ${err ? html`<p class="msg bad small" role="alert">${err}</p>` : ''}
  </form>`;
}

function rulesetCard(rs) {
  const fkE = `rs-edit:${rs.id}`, ckD = `rs-del:${rs.id}`;
  const queued = queuedFor((c) => c.type.startsWith('ruleset.') && c.params.id === rs.id);
  return html`<li class="rs${rs.locked ? ' is-locked' : ''}" id="rs-${rs.id}">
    <div class="rs-head">
      ${rs.locked ? html`<span class="rs-lock" title="Locked">${raw(LOCK)}</span>` : ''}
      <b class="rs-name">${rs.name}</b>
      <span class="tag">${rs.target}</span>${enforcement(rs.enforcement)}
      <span class="small">on <b class="rs-on">${rs.applies}</b></span>
      ${queued.map((c) => html`<a class="tag t-queued" href="#tab=editor&sec=pending" title="${c.summary}">${c.type.slice(8)} queued</a>`)}
      <a class="small" href="${rs.url}" target="_blank" rel="noopener" title="Open the ruleset on GitHub">GitHub ↗</a>
      <span class="grow"></span>
      ${rs.locked ? html`<span class="tag t-lock">${raw(LOCK)} locked</span>` : gui.open.has(fkE) ? '' : html`<span class="rs-acts"><button type="button" class="small" data-gact="rl-open" data-fk="${fkE}">Edit</button><button type="button" class="small danger" data-gact="rl-confirm" data-ck="${ckD}">Delete…</button></span>`}
    </div>
    <ul class="rs-chips">${rs.summary.map((t) => html`<li>${t}</li>`)}</ul>
    ${rs.locked ? html`<p class="rs-why small">${raw(LOCK)} <b>Locked: only q changes this, on GitHub.</b> Why: ${rs.lockReasons.join('; ')}.</p>` : ''}
    <p class="muted small">Ruleset ${rs.id}${rs.updatedAt ? html` · changed ${relOnly(rs.updatedAt)}` : ''}${rs.currentUserCanBypass ? ` · your bypass: ${rs.currentUserCanBypass}` : ''}</p>
    ${gui.confirm.has(ckD) ? html`<div class="msg warn small rs-confirm" role="alert">Queue deleting the ruleset <b>${rs.name}</b>? It is previewed in Pending changes; GitHub keeps enforcing it until you apply the delete there.
      <button type="button" class="small danger" data-gact="rl-del" data-id="${rs.id}" data-ck="${ckD}">Queue delete</button><button type="button" class="small" data-gact="rl-unconfirm" data-ck="${ckD}">Keep</button>
      ${gui.errors.has(ckD) ? html`<span class="err">${gui.errors.get(ckD)}</span>` : ''}</div>` : ''}
    ${gui.open.has(fkE) ? rulesetForm(fkE, rs) : ''}
  </li>`;
}

function githubCard() {
  const v = ged.rulesets;
  const fkN = 'rs-new:new';
  const list = (v && v.rulesets) || [];
  return html`<section class="card rl-gh">
    <h2><span class="kind-mark k-gh">Enforced by GitHub</span> Rulesets <span class="tag">GitHub</span>${v ? html`<span class="count">${list.length}</span>` : ''}<span class="grow"></span>
      <button type="button" data-gact="rl-reload"${ged.rulesetsBusy ? ' disabled' : ''} title="Read the rulesets from GitHub now">${ged.rulesetsBusy ? 'Reading GitHub…' : 'Reload from GitHub'}</button>
      ${gui.open.has(fkN) ? '' : html`<button type="button" class="primary" data-gact="rl-open" data-fk="${fkN}"${v ? '' : ' disabled'}>+ New branch ruleset</button>`}</h2>
    <p class="hint">GitHub itself enforces these on every push, for everyone. A change here is queued in Pending changes (JSON before → after, the exact <code>gh api</code> call) and sent only when you press Apply there. ${raw(LOCK)} Rulesets on the default branch (production) or <code>v*</code> tags are locked: only q changes them, on GitHub.</p>
    ${pendingBar()}
    ${v && v.error ? html`<p class="msg warn small" role="alert">Could not read the rulesets from GitHub: ${v.error}${list.length ? ' (showing the last ones read)' : ''}</p>` : ''}
    ${v && v.fetchedAt ? html`<p class="muted small">Read from GitHub ${relOnly(v.fetchedAt)} · default branch <code>${v.defaultBranch || '?'}</code></p>` : ''}
    ${gui.open.has(fkN) ? rulesetForm(fkN, null) : ''}
    ${!v ? html`<p class="muted">Reading the rulesets from GitHub…</p>` : list.length ? html`<ul class="rs-list">${list.map(rulesetCard)}</ul>` : html`<p class="empty">No rulesets on GitHub.</p>`}
  </section>`;
}

// ---- Followed by every Claude ----
const toSend = (r) => ({ id: r.id, title: r.title, text: r.text, scope: r.scope, kind: r.kind, value: r.kind === 'limit' ? r.value : null, unit: r.kind === 'limit' ? (r.unit || '') : '', locked: !!r.locked });
const isHard = (r) => ged.rules && r.id === ged.rules.hardRule;
const scopeText = (s) => (s.type === 'all' ? 'everywhere' : html`${s.type} <code>${s.match}</code>`);

function ruleForm(fk, r, kind) {
  const d = draft(fk, () => (r ? { title: r.title, text: r.text, kind: r.kind, scopeType: r.scope.type, scopeMatch: r.scope.match, value: r.value ?? '', unit: r.unit || '', locked: !!r.locked }
    : { title: '', text: '', kind: kind || 'other', scopeType: 'all', scopeMatch: '', value: '', unit: '', locked: false }));
  const err = gui.errors.get(fk), busy = gui.busy.has(fk);
  return html`<form class="gform ar-form" data-gform="${fk}" novalidate>
    <div class="gf-row">
      <label class="f gf-grow"><span class="f-l">Title</span><input type="text" data-gf="title" value="${d.title}" maxlength="200" autocomplete="off"></label>
      <label class="f"><span class="f-l">Kind</span><select data-gf="kind">${(ged.rules.kinds || []).map((k) => html`<option value="${k}"${d.kind === k ? ' selected' : ''}>${KIND_LABEL[k] || k}</option>`)}</select></label>
      <label class="f"><span class="f-l">Applies to</span><select data-gf="scopeType">${[['all', 'everything'], ['branch', 'branches'], ['tool', 'a tool']].map(([v, t]) => html`<option value="${v}"${d.scopeType === v ? ' selected' : ''}>${t}</option>`)}</select></label>
      ${d.scopeType !== 'all' ? html`<label class="f"><span class="sr">Which</span><input type="text" data-gf="scopeMatch" value="${d.scopeMatch}" placeholder="${d.scopeType === 'branch' ? 'testing, claude/*' : 'gh, quark.exe'}" size="16" autocomplete="off"></label>` : ''}
    </div>
    ${d.kind === 'limit' ? html`<div class="gf-row"><label class="f"><span class="f-l">Value</span><input type="number" data-gf="value" value="${d.value}" min="0" step="any" class="num"></label>
      <label class="f"><span class="f-l">Unit</span><input type="text" data-gf="unit" value="${d.unit}" placeholder="days, PRs, minutes" size="10" maxlength="24" autocomplete="off"></label></div>` : ''}
    <textarea data-gf="text" rows="3" placeholder="The rule, as an agent should read it (markdown works)">${d.text}</textarea>
    <div class="gf-bar"><label class="chk"><input type="checkbox" data-gf="locked"${d.locked ? ' checked' : ''}> ${raw(LOCK)} Locked (q's: agents never change it or work around it)</label>
      <span class="grow"></span>${r ? html`<span class="muted small">id <code>${r.id}</code></span>` : ''}
      <button type="button" data-gact="rl-cancel" data-fk="${fk}">Cancel</button><button type="submit" class="primary"${busy ? ' disabled' : ''}>${busy ? 'Saving…' : r ? 'Save' : 'Add rule'}</button></div>
    ${err ? html`<p class="msg bad small" role="alert">${err}</p>` : ''}
  </form>`;
}

function ruleActions(r, fk, ck) {
  if (isHard(r)) return html`<span class="tag t-lock" title="The production lock: it can't be edited, unlocked or removed">${raw(LOCK)} locked for good</span>`;
  if (gui.open.has(fk)) return '';
  return html`<span class="ar-acts"><button type="button" class="small" data-gact="rl-open" data-fk="${fk}">Edit</button><button type="button" class="small danger" data-gact="rl-confirm" data-ck="${ck}">Delete…</button></span>`;
}
function confirmRule(r, ck) {
  if (!gui.confirm.has(ck)) return '';
  return html`<div class="msg warn small" role="alert">Delete the rule <b>${r.title}</b>? It stays in the History tab. <button type="button" class="small danger" data-gact="rl-rule-del" data-id="${r.id}" data-ck="${ck}">Delete</button><button type="button" class="small" data-gact="rl-unconfirm" data-ck="${ck}">Keep</button>${gui.errors.has(ck) ? html` <span class="err">${gui.errors.get(ck)}</span>` : ''}</div>`;
}

function ruleCard(r) {
  const fk = `ar-edit:${r.id}`, ck = `ar-del:${r.id}`;
  return html`<li class="ar k-${r.kind}${r.locked ? ' is-locked' : ''}" id="ar-${r.id}">
    <div class="ar-head">
      ${r.locked ? html`<span class="rs-lock" title="${isHard(r) ? 'Locked for good (the production lock)' : 'Locked by q'}">${raw(LOCK)}</span>` : ''}
      <b class="ar-title">${r.title}</b>
      <span class="tag k-${r.kind}">${KIND_LABEL[r.kind] || r.kind}</span>
      <span class="small muted">${scopeText(r.scope)}</span>
      <span class="grow"></span>
      <span class="muted small" title="${r.updatedAt}">${r.id} · ${relOnly(r.updatedAt)}</span>
      ${ruleActions(r, fk, ck)}
    </div>
    ${gui.open.has(fk) ? ruleForm(fk, r) : html`<div class="ar-text md">${md(r.text)}</div>`}
    ${confirmRule(r, ck)}
  </li>`;
}

function limitRow(r) {
  const fk = `ar-edit:${r.id}`, ck = `ar-del:${r.id}`, lk = `lim:${r.id}`;
  if (gui.open.has(fk)) return html`<li class="lim editing">${ruleForm(fk, r)}</li>`;
  const d = draft(lk, { value: r.value });
  const changed = d.value !== '' && Number(d.value) !== r.value;
  return html`<li class="lim${r.locked ? ' is-locked' : ''}" id="ar-${r.id}">
    <form class="gform lim-form" data-gform="${lk}" novalidate>
      <span class="lim-t">${r.locked ? html`<span class="rs-lock" title="Locked by q">${raw(LOCK)}</span> ` : ''}<b>${r.title}</b><span class="muted small"> · ${scopeText(r.scope)}</span></span>
      <label class="f lim-v"><span class="sr">${r.title}</span><input type="number" data-gf="value" value="${d.value}" min="0" step="any" class="num"${r.locked ? ' disabled' : ''}> <span class="muted">${r.unit}</span></label>
      ${changed ? html`<button type="submit" class="small primary"${gui.busy.has(lk) ? ' disabled' : ''}>Save</button><button type="button" class="small" data-gact="rl-lim-reset" data-fk="${lk}">Undo</button>` : ''}
      <span class="grow"></span>
      ${ruleActions(r, fk, ck)}
    </form>
    <div class="lim-text small md">${md(r.text)}</div>
    ${gui.errors.has(lk) ? html`<p class="msg bad small" role="alert">${gui.errors.get(lk)}</p>` : ''}
    ${confirmRule(r, ck)}
  </li>`;
}

function docsLine(ds) {
  if (!ds) return '';
  const files = (ds.files || []).map((f, i) => html`${i ? ', ' : ''}<code>${f}</code>`);
  // the notes there couldn't be read back on this new install: they are left as they are, not replaced by an empty file
  const held = ds.notesHeld ? html`<p class="msg warn small docs-line" role="alert">${ds.notesHeld}.</p>` : '';
  if (ds.state === 'off') return html`<p class="muted small docs-line">Claude Documents copy: ${ds.offReason || 'off'}.</p>`;
  if (ds.state === 'failed') return html`<p class="msg bad small docs-line" role="alert"><b>The Claude Documents copy failed</b> (${relOnly(ds.lastAttempt)}): <code>${ds.lastError}</code>. The rules and notes are saved on this PC; the copy is tried again on every poll until it works.</p>${held}`;
  if (ds.state === 'waiting' || ds.state === 'syncing') return html`<p class="small docs-line"><span class="spin s-run" aria-hidden="true"></span> ${ds.state === 'syncing' ? 'Writing' : 'About to write'} ${files} to the Claude Documents (<code>${ds.repo}</code>): one commit for a burst of edits.</p>${held}`;
  return html`<p class="muted small docs-line">✓ Also in the Claude Documents (<code>${ds.repo}</code>): ${files}${ds.lastOk ? html`, pushed ${relOnly(ds.lastOk)}` : ''}${ds.lastCommit ? html` · commit <code>${short(ds.lastCommit)}</code>` : ''}.</p>${held}`;
}

function wantRule(r, st) {
  if (st.rkind !== 'all' && r.kind !== st.rkind) return false;
  return matches(`${r.title} ${r.text} ${r.id} ${r.kind} ${r.scope.type} ${r.scope.match}`, st.q);
}

export const rulesSection = {
  mount(main) {
    fs = filterState('editor-rules', { rkind: 'all', q: '' });
    main.innerHTML = String(html`<div id="rl-gh-box"></div>
      <section class="card rl-agents">
        <div id="rl-ahead"></div>
        <div class="toolbar" id="rl-bar">
          ${segmented('rkind', fs.st.rkind, [['all', 'All'], ...['push', 'merge', 'version', 'ci', 'limit', 'other'].map((k) => [k, KIND_LABEL[k] === 'CI' ? 'CI' : KIND_LABEL[k][0].toUpperCase() + KIND_LABEL[k].slice(1)])])}
          ${search('q', fs.st.q, 'Search rules…')}
        </div>
        <div id="rl-list"></div>
      </section>
      <div id="rl-lim-box"></div>`);
    bind(main.querySelector('#rl-bar'), fs, () => this.update());
    if (!ged.rulesets && !ged.rulesetsBusy) loadRulesets(false);
  },
  update() {
    const gh = document.getElementById('rl-gh-box'), head = document.getElementById('rl-ahead'), list = document.getElementById('rl-list'), lim = document.getElementById('rl-lim-box');
    if (!gh) return;
    if (ged.available === false) { gh.innerHTML = String(html`<section class="card"><p class="empty">The GitHub manager server running now is older than the GitHub editor. Restart it to use Rules &amp; limits.</p></section>`); head.innerHTML = list.innerHTML = lim.innerHTML = ''; return; }
    gh.innerHTML = String(githubCard());
    const R = ged.rules;
    if (!R) { list.innerHTML = '<p class="muted">Loading the rules…</p>'; return; }
    const fkN = 'ar-new:new';
    head.innerHTML = String(html`<h2><span class="kind-mark k-agent">Followed by every Claude</span> Agent rules <span class="tag">local + Claude Documents</span><span class="count">${R.rules.length}</span><span class="grow"></span>
        ${gui.open.has(fkN) ? '' : html`<button type="button" class="primary" data-gact="rl-open" data-fk="${fkN}">+ Add rule</button>`}</h2>
      <p class="hint">What every Claude reads and follows: nothing enforces these but the agents. Saved on this PC (<code>editor/rules.json</code>; every change in the History tab) and written to the Claude Documents. ${raw(LOCK)} Locked rules are q's: agents never change them or work around them. “Only q pushes to production” is locked for good.</p>
      ${docsLine(R.docsSync)}
      ${R.loadError ? html`<p class="msg warn small">${R.loadError}</p>` : ''}
      ${gui.open.has(fkN) ? ruleForm(fkN, null, fs.st.rkind !== 'all' ? fs.st.rkind : 'other') : ''}`);
    const rules = R.rules.filter((r) => r.kind !== 'limit' && wantRule(r, fs.st));
    const limits = R.rules.filter((r) => r.kind === 'limit' && wantRule(r, fs.st));
    list.innerHTML = String(html`${fs.st.rkind === 'limit' ? html`<p class="muted small">Limits are listed below.</p>` : rules.length ? html`<ul class="ar-list">${rules.map(ruleCard)}</ul>` : html`<p class="empty">No rules match.</p>`}`);
    lim.innerHTML = String(html`<section class="card rl-limits" id="rl-limits">
      <h2><span class="kind-mark k-agent">Followed by every Claude</span> Limits <span class="count">${limits.length}</span><span class="grow"></span>
        ${gui.open.has('ar-new:limit') ? '' : html`<button type="button" data-gact="rl-open" data-fk="ar-new:limit">+ Add limit</button>`}</h2>
      <p class="hint">Numbers the agents keep to. Change a value and press Save. The stale branch age also sets which branches the Structure section lists as stale.</p>
      ${gui.open.has('ar-new:limit') ? ruleForm('ar-new:limit', null, 'limit') : ''}
      ${limits.length ? html`<ul class="lim-list">${limits.map(limitRow)}</ul>` : html`<p class="empty">No limits match.</p>`}
    </section>`);
  },
};

// ---- behaviour ----
onAction('rl-open', (b) => { gui.open.add(b.dataset.fk); redraw(); requestAnimationFrame(() => { const f = document.querySelector(`[data-gform="${CSS.escape(b.dataset.fk)}"] [data-gf]`); if (f) f.focus(); }); });
onAction('rl-cancel', (b) => { closeForm(b.dataset.fk); redraw(); });
onAction('rl-confirm', (b) => { gui.confirm.add(b.dataset.ck); redraw(); });
onAction('rl-unconfirm', (b) => { gui.confirm.delete(b.dataset.ck); gui.errors.delete(b.dataset.ck); redraw(); });
onAction('rl-reload', () => loadRulesets(true));
onAction('rl-lim-reset', (b) => { gui.drafts.delete(b.dataset.fk); redraw(); });
onAction('rl-del', async (b) => {
  const ok = await act(b.dataset.ck, () => queueChange('ruleset.delete', { id: Number(b.dataset.id) }), 'Queued: review and apply it in Pending changes');
  if (ok) { gui.confirm.delete(b.dataset.ck); redraw(); }
});
// redraw when a field that changes the form's shape changes
onInput('rs-edit', (fk, d, n) => { if (['requirePr', 'include', 'exclude', 'chkq', 'requiredChecks'].includes(n.dataset.gf)) redraw(); });
onInput('rs-new', (fk, d, n) => { if (['requirePr', 'include', 'exclude', 'chkq', 'requiredChecks'].includes(n.dataset.gf)) redraw(); });
onInput('ar-edit', (fk, d, n) => { if (['kind', 'scopeType'].includes(n.dataset.gf)) redraw(); });
onInput('ar-new', (fk, d, n) => { if (['kind', 'scopeType'].includes(n.dataset.gf)) redraw(); });
onInput('lim', () => redraw());

function rulesetBody(d) {
  return {
    name: (d.name || '').trim(), enforcement: d.enforcement, include: lines(d.include), exclude: lines(d.exclude),
    blockForcePush: !!d.blockForcePush, blockDeletion: !!d.blockDeletion, linearHistory: !!d.linearHistory,
    requirePr: !!d.requirePr, approvals: d.requirePr ? Math.max(0, Math.min(10, Math.round(Number(d.approvals) || 0))) : 0,
    requiredChecks: [...d.requiredChecks], strictChecks: d.requiredChecks.length ? !!d.strictChecks : false, adminBypass: !!d.adminBypass,
  };
}
const queued = 'Queued: review the diff and apply it in Pending changes';
onSubmit('rs-edit', async (fk, d) => {
  const id = Number(fk.slice('rs-edit:'.length));
  if (await act(fk, () => queueChange('ruleset.update', { id, form: rulesetBody(d) }), queued)) { closeForm(fk); redraw(); }
});
onSubmit('rs-new', async (fk, d) => {
  if (!(d.name || '').trim()) { gui.errors.set(fk, 'Name the ruleset first.'); redraw(); return; }
  if (await act(fk, () => queueChange('ruleset.create', { form: rulesetBody(d) }), queued)) { closeForm(fk); redraw(); }
});

let docsPeek = null;
async function saveRules(fk, list, okText) {
  const ok = await act(fk, () => putRules(list.map(toSend)), okText);
  if (ok) {
    // the Claude Documents copy is written about 10 s after the last change: look again then
    clearTimeout(docsPeek);
    docsPeek = setTimeout(() => loadEditorData().then((c) => { if (c) redraw(); }), 12000);
  }
  return ok;
}
// a limit's value, checked here first (the server checks again): -> an error sentence or ''
const LIMIT_MAX = 1e9;
const limitError = (v) => (v === '' || v == null || !isFinite(Number(v)) ? 'Give the limit a value (a number).'
  : Number(v) < 0 ? 'The limit must be 0 or more.' : Number(v) > LIMIT_MAX ? `The limit must be at most ${LIMIT_MAX}.` : '');
function ruleFromDraft(d, id) {
  const r = { id: id || '', title: (d.title || '').trim(), text: d.text || '', kind: d.kind, scope: { type: d.scopeType, match: d.scopeType === 'all' ? '' : (d.scopeMatch || '').trim() }, locked: !!d.locked };
  if (d.kind === 'limit') { r.value = d.value === '' ? null : Number(d.value); r.unit = (d.unit || '').trim(); }
  return r;
}
onSubmit('ar-edit', async (fk, d) => {
  const id = fk.slice('ar-edit:'.length);
  if (d.kind === 'limit' && limitError(d.value)) { gui.errors.set(fk, limitError(d.value)); redraw(); return; }
  const list = ged.rules.rules.map((r) => (r.id === id ? ruleFromDraft(d, id) : r));
  if (await saveRules(fk, list, 'Rule saved')) { closeForm(fk); gui.drafts.delete(`lim:${id}`); redraw(); }
});
onSubmit('ar-new', async (fk, d) => {
  if (!(d.title || '').trim() || !(d.text || '').trim()) { gui.errors.set(fk, 'A rule needs a title and its text.'); redraw(); return; }
  if (d.kind === 'limit' && limitError(d.value)) { gui.errors.set(fk, limitError(d.value)); redraw(); return; }
  const list = [...ged.rules.rules, ruleFromDraft(d)];
  if (await saveRules(fk, list, 'Rule added')) { closeForm(fk); redraw(); }
});
onSubmit('lim', async (fk, d) => {
  const id = fk.slice('lim:'.length);
  if (limitError(d.value)) { gui.errors.set(fk, limitError(d.value)); redraw(); return; }
  const list = ged.rules.rules.map((r) => (r.id === id ? { ...r, value: d.value === '' ? null : Number(d.value) } : r));
  if (await saveRules(fk, list, 'Limit saved')) { gui.drafts.delete(fk); redraw(); }
});
onAction('rl-rule-del', async (b) => {
  const list = ged.rules.rules.filter((r) => r.id !== b.dataset.id);
  if (await saveRules(b.dataset.ck, list, 'Rule deleted (it stays in the History tab)')) { gui.confirm.delete(b.dataset.ck); redraw(); }
});
