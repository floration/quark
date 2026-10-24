// Pieces several views share: run rows, failure sentences, PR rows, commit lists, history event rows.
import {
  html, relOnly, when, dur, elapsed, ms, plural, badge, runBadge, runState, RUNNING, stateLabel,
  prChip, branchChip, commitChip, runChip, ghLink, docsLink, enc, aheadBehind, localChip, versionChip,
} from './util.js';
import { store } from './store.js';
import { noteBadge } from './notes.js';
import { testsJobUrl } from './tests.js';

// The workflow file of a run (release.yml): the server sends workflowFile; a run cached before it did borrows it
// from another run of the same workflow.
export function workflowFile(r) {
  if (r.workflowFile) return r.workflowFile;
  for (const x of (store.state && store.state.runs) || []) if (x.workflow === r.workflow && x.workflowFile) return x.workflowFile;
  return null;
}
// "tests editor" link for one job of a run (empty when the workflow file isn't known)
export function testsLink(r, job, { label = 'tests editor' } = {}) {
  const f = workflowFile(r);
  if (!f || !job) return '';
  return html`<a class="testsl small" href="${testsJobUrl(f, job.name, r.id)}" target="quark-tests-editor" rel="noopener" title="Open ${job.name}'s tests in the tests editor (${f})">${label}</a>`;
}

export const jobsOf = (r) => (r && store.idx && store.idx.runs.get(String(r.id)) || r || {}).jobs || [];
export const failedJobs = (r) => jobsOf(r).filter((j) => ['failure', 'timed_out', 'startup_failure'].includes(j.conclusion));

// "failed 12 min ago at step 'windows tests' in windows-tests (net), and 1 more job"
export function failureSentence(r, { withWhen = true } = {}) {
  const f = failedJobs(r);
  const t = withWhen ? html`failed ${relOnly(r.updatedAt)}` : 'failed';
  if (!f.length) return t;
  const j = f[0];
  const step = (j.failedSteps || [])[0];
  return html`${t} ${step ? html`at step <q>${step}</q> in ` : 'in '}<b>${j.name}</b>${f.length > 1 ? html`, and ${plural(f.length - 1, 'more job')}` : ''}`;
}

// "running 7:12 · 4 of 15 jobs done · now: linux-tests (core), windows …"
export function runningSentence(r) {
  const jobs = jobsOf(r);
  const done = jobs.filter((j) => j.status === 'completed').length;
  const now = jobs.filter((j) => j.status === 'in_progress').map((j) => j.name);
  const fail = failedJobs(r);
  const st = runState(r);
  const waiting = st === 'running' && r.status !== 'in_progress';
  return html`${st === 'queued' ? html`queued for ${elapsed(r.createdAt)}` : html`running for ${elapsed(r.createdAt)}${waiting ? ' (waiting for a runner)' : ''}`}${jobs.length ? html` · ${done} of ${jobs.length} jobs done` : ''}${now.length ? html` · now: ${now.slice(0, 3).join(', ')}${now.length > 3 ? ` +${now.length - 3}` : ''}` : ''}${fail.length ? html` · <span class="t-bad">${plural(fail.length, 'job')} failed already</span>` : ''}`;
}

export function runTiming(r) {
  if (RUNNING.has(r.status)) return '';
  return html`<span title="duration">${dur(r.durationSec ?? (ms(r.updatedAt) - ms(r.createdAt)) / 1000)}</span>`;
}

export function runOutcome(r) {
  const st = runState(r);
  if (st === 'running' || st === 'queued') return runningSentence(r);
  if (st === 'failure' || st === 'timed_out' || st === 'startup_failure') return failureSentence(r);
  if (st === 'success') return html`passed ${relOnly(r.updatedAt)} in ${dur(r.durationSec)}`;
  return html`${stateLabel(st)} ${relOnly(r.updatedAt)}`;
}

export function runRow(r, { showBranch = true } = {}) {
  const st = runState(r);
  return html`<li class="row run-row rs-${st}" data-action="row" data-href="#run=${r.id}" tabindex="0">
    <div class="r-main">${runBadge(r)}<a class="r-title" href="#run=${r.id}">${r.title || r.workflow}</a>${noteBadge('run', r.id, `#run=${r.id}`)}</div>
    <div class="r-meta">
      <span class="wf">${r.workflow}</span>
      ${showBranch ? branchChip(r.branch) : ''} ${commitChip(r.sha)}
      <span class="muted">${r.event}${r.attempt > 1 ? ` · attempt ${r.attempt}` : ''}</span>
      <span class="muted">started ${when(r.createdAt, { short: true })}</span>
      ${runTiming(r)}
      ${ghLink(r.url, 'Open the run on GitHub')}
    </div>
    ${st !== 'success' ? html`<div class="r-out">${runOutcome(r)}${failedJobs(r).slice(0, 3).map((j) => html` ${testsLink(r, j, { label: `tests: ${j.name}` })}`)}</div>` : ''}
  </li>`;
}

export function checksBadge(p) {
  const c = p.checks || { state: 'none', runs: [] };
  return badge(c.state === 'pending' ? 'running' : c.state, c.state === 'pending' ? 'checks running' : c.state === 'success' ? 'checks passed' : c.state === 'failure' ? 'checks failed' : 'no checks');
}

export function prState(p) {
  if (p.state === 'open') return p.draft ? html`<span class="tag t-draft">draft</span>` : html`<span class="tag t-open">open</span>`;
  return p.merged ? html`<span class="tag t-merged">merged</span>` : html`<span class="tag t-closed">closed</span>`;
}

export function prRow(p) {
  return html`<li class="row pr-row" data-action="row" data-href="#pr=${p.number}" tabindex="0">
    <div class="r-main">${prState(p)}<a class="r-title" href="#pr=${p.number}">${p.title}</a> ${prChip(p.number, p.title)}${noteBadge('pr', p.number, `#pr=${p.number}`)}</div>
    <div class="r-meta">
      ${branchChip(p.head)} <span class="muted">into</span> ${branchChip(p.base)}
      <span class="muted">by ${p.author}</span>
      <span class="muted">${p.state === 'open' ? html`opened ${relOnly(p.createdAt)}, updated ${relOnly(p.updatedAt)}` : p.merged ? html`merged ${when(p.mergedAt)}` : html`closed ${when(p.updatedAt)}`}</span>
      <span class="diffstat"><span class="add">+${p.additions ?? 0}</span> <span class="del">−${p.deletions ?? 0}</span> <span class="muted">${plural(p.changedFiles ?? 0, 'file')}</span></span>
      ${p.state === 'open' ? checksBadge(p) : ''}
      ${docsLink(`PR #${p.number}`)}
    </div>
  </li>`;
}

export function commitList(commits, { limit = 12, id, empty = 'none', showAuthor = true } = {}) {
  if (!commits || !commits.length) return html`<p class="muted small">${empty}</p>`;
  const row = (c) => html`<li class="commit"><span class="c-sha">${commitChip(c.sha, c.url)}</span><span class="c-subj" title="${c.subject}">${c.subject}</span><span class="c-meta muted">${showAuthor && c.author ? `${c.author} · ` : ''}${relOnly(c.date)}</span></li>`;
  const first = commits.slice(0, limit), rest = commits.slice(limit);
  const boxId = id || `more-${Math.random().toString(36).slice(2, 8)}`;
  return html`<ul class="commits">${first.map(row)}</ul>${rest.length ? html`<ul class="commits" id="${boxId}" hidden>${rest.map(row)}</ul><button class="linkbtn" data-action="toggle" data-target="${boxId}" data-more="Show ${rest.length} more" data-less="Show fewer" aria-expanded="false">Show ${rest.length} more</button>` : ''}`;
}

// ---- history events ----
const TYPE_ICON = {
  'run.started': '▶', 'run.completed': '✓', 'run.failed': '✕', 'pr.opened': '⊕', 'pr.merged': '⇥', 'pr.closed': '⊘', 'pr.updated': '↻',
  'branch.created': '+', 'branch.moved': '→', 'branch.deleted': '−', 'tag.created': '⌖', 'release.published': '★',
  'local.commit': '●', 'local.moved': '→', 'local.dirty': '✎', 'poll.error': '!', baseline: '◎',
  'note.added': '✎', 'note.edited': '✎', 'note.done': '✓', 'note.deleted': '−',
};
export const TYPE_GROUPS = [
  { key: 'run', label: 'CI runs' }, { key: 'pr', label: 'Pull requests' }, { key: 'branch', label: 'Branches' },
  { key: 'release', label: 'Tags & releases', types: ['tag', 'release'] }, { key: 'local', label: 'Local copies' }, { key: 'note', label: 'Notes' },
  { key: 'poll', label: 'Server', types: ['poll', 'baseline'] },
];
export const groupOf = (type) => {
  const p = (type || '').split('.')[0];
  return p === 'tag' ? 'release' : p === 'baseline' ? 'poll' : p;
};
function evClass(e) {
  if (e.type === 'run.failed' || e.type === 'poll.error') return 'bad';
  if (e.type === 'run.completed') return /cancel|skip/.test(e.text) ? 'off' : 'good';
  if (e.type === 'pr.merged' || e.type === 'release.published') return 'merged';
  if (e.type === 'run.started') return 'run';
  if (e.type === 'note.done') return 'good';
  if (e.type === 'note.added' || e.type === 'note.edited') return 'note';
  return 'off';
}
export function refChip(ref, type) {
  if (!ref) return '';
  if (ref.note) {
    // a note's event: its target (as the other events show it) and, unless it was deleted, the note itself
    const { note, ...target } = ref;
    const t = target.commit ? commitChip(target.commit) : target.version ? versionChip(target.version) : refChip(target);
    return html`${t}${type !== 'note.deleted' ? html` <span class="chip c-note"><a href="#tab=editor&note=${enc(note)}" title="Open the note in the Editor">note</a></span>` : ''}`;
  }
  if (ref.run != null) { const r = store.idx.runs.get(String(ref.run)); return r ? runChip(r) : html`<span class="chip c-run"><a href="#run=${ref.run}">run ${ref.run}</a></span>`; }
  if (ref.pr != null) return prChip(ref.pr);
  if (ref.branch) return branchChip(ref.branch);
  if (ref.local) return localChip(ref.local);
  if (ref.tag) {
    const m = /^(?:v|test-)(\d+\.\d+\.\d+)/.exec(ref.tag);
    return html`<span class="chip c-tag"><a href="${m ? `#version=${enc(m[1])}` : '#tab=history'}">${ref.tag}</a>${ghLink(`https://github.com/${store.state.repo}/releases/tag/${enc(ref.tag)}`)}</span>`;
  }
  return '';
}
export function eventRow(e, { showDay = false } = {}) {
  return html`<li class="ev ev-${evClass(e)}">
    <span class="ev-time" title="${e.at}">${showDay ? when(e.at) : html`<time datetime="${e.at}">${new Intl.DateTimeFormat('en-US', { timeZone: 'America/Chicago', hour: 'numeric', minute: '2-digit' }).format(ms(e.at))}</time>`}</span>
    <span class="ev-ico" aria-hidden="true">${TYPE_ICON[e.type] || '•'}</span>
    <span class="ev-text">${e.text}</span>
    <span class="ev-ref">${refChip(e.ref, e.type)}</span>
  </li>`;
}
export function eventsFor(pred, limit = 15) {
  const out = [];
  for (const e of store.history) { if (pred(e)) { out.push(e); if (out.length >= limit) break; } }
  return out;
}

export function lastRunCell(r) {
  if (!r) return html`<span class="muted">no runs</span>`;
  return html`<a class="lr" href="#run=${r.id}" title="${r.workflow}: ${r.title}">${runBadge(r)}</a> ${RUNNING.has(r.status) ? elapsed(r.createdAt) : relOnly(r.updatedAt)}`;
}

// a tiny inline diverging bar: behind (left, grey) | ahead (right, accent)
export function abBar(ahead, behind, max) {
  const w = (n) => (n ? Math.max(6, Math.round((Math.sqrt(n) / Math.sqrt(Math.max(1, max))) * 100)) : 0);
  return html`<span class="abbar" title="${aheadBehind(ahead, behind)}"><span class="ab-l"><span class="ab-b" style="width:${w(behind)}%"></span></span><span class="ab-r"><span class="ab-a" style="width:${w(ahead)}%"></span></span></span>`;
}

