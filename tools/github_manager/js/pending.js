// Pending changes: what q queued to change on GitHub (branches, branch rulesets). Each shows before -> after (a
// sentence for a branch, the JSON diff for a ruleset), the exact `gh api` call, warnings, and the result of the last
// try. Nothing reaches GitHub until Apply; the server re-reads GitHub and re-checks the production lock right
// before each call.
import { html, raw, relOnly, plural, branchChip } from './util.js';
import { ged, gui, act, onAction, waitingChanges, applyChange, applyAll, discardChange, repreviewChange, loadRulesets, redraw, toast, LOCK } from './ghedit.js';

const STATUS = {
  pending: ['wait', 'waiting'], applying: ['run', 'applying…'], applied: ['good', 'applied'], failed: ['bad', 'failed'],
  stale: ['warn', 'changed on GitHub'], refused: ['bad', 'refused by the lock'],
};
const TYPE = {
  'branch.create': 'create branch', 'branch.delete': 'delete branch', 'branch.rename': 'rename branch',
  'ruleset.create': 'new ruleset', 'ruleset.update': 'change ruleset', 'ruleset.delete': 'delete ruleset',
};
const ICON = { good: '✓', bad: '✕', warn: '!', wait: '…' };

function statusBadge(c) {
  const [cls, label] = STATUS[c.status] || ['off', c.status];
  return html`<span class="badge s-${cls}">${cls === 'run' ? raw('<span class="spin" aria-hidden="true"></span>') : html`<span class="ico" aria-hidden="true">${ICON[cls] || '–'}</span>`}${label}</span>`;
}

// the "N changes waiting" line on the Structure and Rules sections
export function pendingBar() {
  const n = waitingChanges().length;
  if (!n) return '';
  return html`<a class="pd-bar" href="#tab=editor&sec=pending"><span class="count warn">${n}</span> ${plural(n, 'change')} waiting in <b>Pending changes</b>: nothing is sent to GitHub until you apply ${n === 1 ? 'it' : 'them'} there →</a>`;
}

function diffBlock(lines) {
  if (!lines || !lines.length) return '';
  return html`<pre class="diff" aria-label="JSON diff, before to after">${lines.map((l) => {
    const c = l.startsWith('@@') ? 'd-hunk' : l.startsWith('+') ? 'd-add' : l.startsWith('-') ? 'd-del' : 'd-ctx';
    return html`<span class="${c}">${l}</span>`;
  })}</pre>`;
}

function result(c) {
  const r = c.result;
  if (!r) return '';
  if (r.ok) return html`<p class="msg good small" role="status">Applied on GitHub ${relOnly(r.at)}${r.httpStatus ? ` (HTTP ${r.httpStatus})` : ''}${r.url ? html` · <a href="${r.url}" target="_blank" rel="noopener">open on GitHub</a>` : ''}.</p>`;
  const what = c.status === 'stale' ? 'Not applied: GitHub changed since the preview. Preview it again, check the new diff, then apply.'
    : c.status === 'refused' ? 'Not applied: refused by the production lock.' : 'Not applied: GitHub (or gh) answered with an error.';
  return html`<div class="msg ${c.status === 'stale' ? 'warn' : 'bad'} small pd-err" role="alert"><b>${what}</b> <span class="muted">${relOnly(r.at)}</span><code class="pd-errtext">${r.error}</code></div>`;
}

function changeCard(c) {
  const busy = gui.busy.has(`apply:${c.id}`) || gui.busy.has('apply-all');
  const done = c.status === 'applied';
  const isRs = c.type.startsWith('ruleset.');
  // out of date or refused: it has to be previewed again (and looked at) before Apply comes back
  const review = c.status === 'stale' || c.status === 'refused';
  const body = c.call && c.call.body != null ? JSON.stringify(c.call.body, null, 2) : null;
  return html`<section class="card pd pd-${c.status}" id="pd-${c.id}">
    <div class="pd-top">
      ${statusBadge(c)}
      <span class="tag">${TYPE[c.type] || c.type}</span>
      <h3 class="pd-sum">${c.summary}</h3>
      <span class="grow"></span>
      <span class="pd-acts">
        ${done || review ? '' : html`<button type="button" class="primary" data-gact="pd-apply" data-id="${c.id}"${busy ? ' disabled' : ''} title="Send this one change to GitHub now">${gui.busy.has(`apply:${c.id}`) ? 'Applying…' : c.status === 'failed' ? 'Apply again' : 'Apply'}</button>`}
        ${done ? '' : html`<button type="button"${review ? ' class="primary"' : ''} data-gact="pd-preview" data-id="${c.id}"${busy ? ' disabled' : ''} title="Read GitHub again and rebuild the before / after">Preview again</button>`}
        <button type="button" data-gact="pd-discard" data-id="${c.id}"${busy ? ' disabled' : ''}>${done ? 'Clear' : 'Discard'}</button>
      </span>
    </div>
    <p class="muted small pd-when">Queued ${relOnly(c.createdAt)}${c.previewedAt && c.previewedAt !== c.createdAt ? html` · previewed again ${relOnly(c.previewedAt)}` : ''}${c.ref && c.ref.branch ? html` · ${branchChip(c.ref.branch)}` : ''} · <code>${c.id}</code></p>
    ${result(c)}
    ${gui.errors.has(`apply:${c.id}`) ? html`<p class="msg bad small" role="alert">${gui.errors.get(`apply:${c.id}`)}</p>` : ''}
    ${(c.warnings || []).length ? html`<ul class="pd-warn">${c.warnings.map((w) => html`<li><span class="ico" aria-hidden="true">!</span> ${w}</li>`)}</ul>` : ''}
    <div class="pd-ba">
      <div class="pd-b"><span class="eyebrow">${done ? 'Before' : 'Before (GitHub now)'}</span><p>${c.beforeText}</p></div>
      <span class="pd-arrow" aria-hidden="true">→</span>
      <div class="pd-a"><span class="eyebrow">After</span><p>${c.afterText}</p></div>
    </div>
    ${isRs ? html`<details class="pd-diff" open><summary>Ruleset JSON, before → after <span class="muted small">(${(c.diff || []).filter((l) => l.startsWith('+') && !l.startsWith('+++')).length} lines added, ${(c.diff || []).filter((l) => l.startsWith('-') && !l.startsWith('---')).length} removed)</span></summary>${diffBlock(c.diff)}</details>` : ''}
    <details class="pd-call"${done ? '' : ' open'}><summary>The exact call Apply makes</summary>
      <pre class="pd-cmd">${c.command}</pre>
      ${body && isRs ? html`<details class="pd-stdin"><summary class="small">with this JSON on stdin (${body.split('\n').length} lines; the diff above shows what changes)</summary><pre class="pd-body">${body}</pre></details>` : body ? html`<p class="muted small">with this JSON on stdin:</p><pre class="pd-body">${body}</pre>` : ''}
    </details>
  </section>`;
}

export const pendingSection = {
  mount(main) {
    main.innerHTML = '<div id="pd-root"></div>';
  },
  update() {
    const box = document.getElementById('pd-root');
    if (!box) return;
    if (ged.available === false) { box.innerHTML = String(html`<section class="card"><p class="empty">The GitHub manager server running now is older than the GitHub editor. Restart it to use Pending changes.</p></section>`); return; }
    if (!ged.pending) { box.innerHTML = '<section class="card"><p class="muted">Loading the pending changes…</p></section>'; return; }
    const all = ged.pending.changes || [];
    const waiting = all.filter((c) => c.status !== 'applied'), done = all.filter((c) => c.status === 'applied');
    const ready = all.filter((c) => c.status === 'pending');
    const confirming = gui.confirm.has('apply-all');
    box.innerHTML = String(html`<section class="card pd-head">
      <h2>Pending changes ${waiting.length ? html`<span class="count warn">${waiting.length} waiting</span>` : ''}<span class="grow"></span>
        ${ready.length > 1 && !confirming ? html`<button type="button" class="primary" data-gact="pd-apply-all"${gui.busy.has('apply-all') ? ' disabled' : ''}>Apply all (${ready.length})</button>` : ''}</h2>
      <p class="hint">${raw(LOCK)} Nothing changes on GitHub until you press <b>Apply</b>. Right before each call the server reads GitHub again, checks the production lock once more (only q pushes to production: nothing touches the default branch, its rulesets or v* tags) and refuses a change whose preview is out of date: “changed on GitHub since preview, review again”. The calls run with your own <code>gh</code> login on this PC; each result goes into the History tab.</p>
      ${confirming ? html`<div class="msg warn pd-confirm" role="alert">Apply ${plural(ready.length, 'waiting change')} to GitHub, one after another (stopping at the first that doesn't go through)?
        <button type="button" class="primary" data-gact="pd-apply-all-yes">Apply ${ready.length}</button><button type="button" data-gact="pd-apply-all-no">Cancel</button></div>` : ''}
      ${!all.length ? html`<p class="empty">Nothing is queued. Queue a branch change in <a href="#tab=editor&sec=structure">Structure</a> or a ruleset change in <a href="#tab=editor&sec=rules">Rules &amp; limits</a>.</p>` : ''}
      ${ged.pending.loadError ? html`<p class="msg warn small">${ged.pending.loadError}</p>` : ''}
    </section>
    ${waiting.map(changeCard)}
    ${done.length ? html`<h3 class="pd-done-h">Applied <span class="count">${done.length}</span></h3>${done.slice().reverse().map(changeCard)}` : ''}`);
  },
};

const find = (id) => ((ged.pending && ged.pending.changes) || []).find((c) => c.id === id);
function after(c) {
  if (c && c.type.startsWith('ruleset.')) loadRulesets(true);
}
onAction('pd-apply', async (b) => {
  const c = find(b.dataset.id);
  if (!c) return;
  let out = null;
  await act(`apply:${c.id}`, async () => { out = await applyChange(c); });
  if (!out) return;
  if (out.status === 'applied') toast(`Applied on GitHub: ${out.summary}`);
  else toast(`Not applied (${STATUS[out.status] ? STATUS[out.status][1] : out.status})`, true);
  after(out);
});
onAction('pd-preview', async (b) => {
  await act(`apply:${b.dataset.id}`, () => repreviewChange(b.dataset.id), 'Previewed again from GitHub: check the new before / after');
});
onAction('pd-discard', async (b) => {
  const c = find(b.dataset.id);
  await act(`apply:${b.dataset.id}`, () => discardChange(b.dataset.id), c && c.status === 'applied' ? 'Cleared' : 'Discarded: nothing was sent to GitHub');
});
onAction('pd-apply-all', () => { gui.confirm.add('apply-all'); redraw(); });
onAction('pd-apply-all-no', () => { gui.confirm.delete('apply-all'); redraw(); });
onAction('pd-apply-all-yes', async () => {
  gui.confirm.delete('apply-all');
  let res = null;
  await act('apply-all', async () => { res = await applyAll(); });
  if (!res) return;
  toast(res.stoppedAt ? `Applied ${res.applied}; stopped at one that didn't go through` : `Applied ${res.applied} on GitHub`, !!res.stoppedAt);
  if (res.results.some((c) => c.type && c.type.startsWith('ruleset.'))) loadRulesets(true);
});
