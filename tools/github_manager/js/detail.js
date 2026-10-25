// The detail drawer: one PR / branch / run / commit / version / local copy, reached by #pr=, #branch=, #run=, …
import { store, findCommit } from './store.js';
import {
  html, when, relOnly, dur, plural, enc, gh, ghLink, docsLink, dashLink, branchChip, commitChip, prChip,
  localChip, runBadge, badge, RUNNING, isLive, ms, elapsed, short, kindLabel, branchVsTesting, unread, unknownLine,
} from './util.js';
import { runRow, prState, checksBadge, eventRow, eventsFor, runOutcome, jobsOf, testsLink } from './parts.js';
import { notesBlock, md } from './notes.js';
import { localCard } from './local.js';

export const ITEM_KEYS = ['run', 'pr', 'branch', 'commit', 'version', 'local'];

const head = (kicker, title, links) => html`<header class="d-head"><div class="eyebrow">${kicker}</div><h2 class="d-title">${title}</h2>${links ? html`<div class="d-links">${links}</div>` : ''}</header>`;
const notFound = (what, id) => html`${head(what, id)}<p class="empty">Not in the current data. It may be older than what the server keeps, or not polled yet.</p>`;
const ext = (url, text) => html`<a href="${url}" target="_blank" rel="noopener">${text}</a>`;
const runsOf = (pred) => (store.state.runs || []).filter(pred);
const historyBlock = (evs, what) => html`<h3>History</h3>${!store.historyLoaded ? html`<p class="muted small">Loading…</p>` : evs.length ? html`<ul class="events">${evs.map((e) => eventRow(e, { showDay: true }))}</ul>` : html`<p class="muted small">Nothing in the local log for this ${what} yet.</p>`}`;

function runDetail(id) {
  const r = store.idx.runs.get(String(id));
  if (!r) return html`${notFound('CI run', `run ${id}`)}${notesBlock('run', id)}`;
  const jobs = jobsOf(r);
  const order = (j) => (j.conclusion === 'failure' ? 0 : j.status === 'in_progress' ? 1 : j.status === 'queued' ? 3 : 2);
  const sorted = [...jobs].sort((a, b) => order(a) - order(b));
  const job = (j) => {
    const st = j.status === 'completed' ? (j.conclusion || 'unknown') : j.status === 'in_progress' ? 'running' : 'queued';
    const t = j.status === 'in_progress' && j.startedAt ? (isLive(r) ? elapsed(j.startedAt) : html`started ${relOnly(j.startedAt)}`)
      : j.startedAt && j.completedAt ? dur((ms(j.completedAt) - ms(j.startedAt)) / 1000) : '';
    const failed = j.conclusion === 'failure';
    return html`<li class="job${failed ? ' is-failed' : ''}">
      <div class="job-main">${badge(st)}<span class="job-name">${j.name}</span><span class="muted small">${t}</span>
        <span class="grow"></span>
        ${testsLink(r, j)}
        ${j.hasLog || failed || j.status === 'completed' ? html`<button class="${failed ? 'primary' : ''} small" data-action="open-log" data-run="${r.id}" data-job="${j.id}">View log</button>` : ''}
        ${ghLink(j.url, 'Open the job on GitHub')}</div>
      ${j.failedSteps && j.failedSteps.length ? html`<div class="job-steps">failed at ${j.failedSteps.map((s, i) => html`${i ? ', ' : ''}<q>${s}</q>`)}</div>` : ''}
    </li>`;
  };
  const counts = {};
  for (const j of jobs) { const k = j.status === 'completed' ? j.conclusion : j.status; counts[k] = (counts[k] || 0) + 1; }
  const pr = store.idx.pullByHead.get(r.branch);
  return html`${head(`CI run ${r.id} · ${r.workflow}`, r.title || r.workflow, html`${ext(r.url, 'Open on GitHub')}${r.attempt > 1 ? html` <span class="muted">attempt ${r.attempt}</span>` : ''}`)}
    <div class="d-status">${runBadge(r)} <span>${runOutcome(r)}</span></div>
    ${unread('runs') ? unknownLine('runs', true) : ''}
    <dl class="kv">
      <dt>Branch</dt><dd>${branchChip(r.branch)} ${pr ? prChip(pr.number, pr.title) : ''}</dd>
      <dt>Commit</dt><dd>${commitChip(r.sha)} <span class="muted">${r.event}</span></dd>
      <dt>Started</dt><dd>${when(r.createdAt, { secs: true })}</dd>
      ${isLive(r) ? html`<dt>Running for</dt><dd>${elapsed(r.createdAt)}</dd>` : RUNNING.has(r.status) ? '' : html`<dt>Took</dt><dd>${dur(r.durationSec)}</dd>`}
    </dl>
    ${notesBlock('run', r.id)}
    <h3>Jobs ${jobs.length ? html`<span class="muted small">${Object.entries(counts).map(([k, n]) => `${n} ${k === 'success' ? 'passed' : k === 'failure' ? 'failed' : k}`).join(' · ')}</span>` : ''}</h3>
    ${jobs.length ? html`<ul class="jobs">${sorted.map(job)}</ul>` : html`<p class="muted small">${RUNNING.has(r.status) ? 'No jobs have started yet.' : 'The server has no job list for this run.'}</p>`}
    ${historyBlock(eventsFor((e) => e.ref && String(e.ref.run) === String(r.id)), 'run')}`;
}

function prDetail(n) {
  const p = store.idx.pulls.get(String(n));
  if (!p) return html`${notFound('Pull request', `#${n}`)}${unread('pulls') ? unknownLine('pulls') : ''}${notesBlock('pr', n)}`;
  const b = store.idx.branches.get(p.head);
  const checkIds = new Set(((p.checks && p.checks.runs) || []).map((r) => String(r.id)));
  const runs = runsOf((r) => r.branch === p.head && !checkIds.has(String(r.id))).slice(0, 10);
  return html`${head(html`Pull request ${prChip(p.number, p.title)}`, p.title, html`${ext(p.url, 'Open on GitHub')} · ${ext(`${p.url}/files`, 'Files changed')} · ${docsLink(`PR #${p.number}`)}`)}
    <div class="d-status">${prState(p)} ${p.state === 'open' ? checksBadge(p) : ''} <span class="diffstat"><span class="add">+${p.additions ?? 0}</span> <span class="del">−${p.deletions ?? 0}</span> <span class="muted">${plural(p.changedFiles ?? 0, 'file')}</span></span></div>
    <dl class="kv">
      <dt>From</dt><dd>${branchChip(p.head)} <span class="muted">into</span> ${branchChip(p.base)}</dd>
      <dt>Author</dt><dd>${p.author}</dd>
      <dt>Opened</dt><dd>${when(p.createdAt)}</dd>
      <dt>${p.merged ? 'Merged' : p.state === 'closed' ? 'Closed' : 'Updated'}</dt><dd>${when(p.merged ? p.mergedAt : p.updatedAt)}</dd>
      ${b ? html`<dt>Branch now</dt><dd>${branchVsTesting(b)} · head ${commitChip(b.sha)}</dd>` : ''}
    </dl>
    ${notesBlock('pr', p.number)}
    <h3>Checks</h3>
    ${(p.checks && p.checks.runs && p.checks.runs.length) ? html`<ul class="rows">${p.checks.runs.map((r) => runRow(store.idx.runs.get(String(r.id)) || r, { showBranch: false }))}</ul>` : html`<p class="muted small">No check runs on the head commit.</p>`}
    ${runs.length ? html`<h3>Other recent runs on ${p.head}</h3><ul class="rows">${runs.map((r) => runRow(r, { showBranch: false }))}</ul>` : ''}
    ${historyBlock(eventsFor((e) => e.ref && (String(e.ref.pr) === String(p.number) || e.ref.branch === p.head)), 'pull request')}`;
}

function branchDetail(name) {
  const b = store.idx.branches.get(name);
  const runs = runsOf((r) => r.branch === name);
  const pulls = (store.state.pulls || []).filter((p) => p.head === name);
  const locals = (store.state.local || []).filter((l) => l.branch === name);
  const evs = eventsFor((e) => e.ref && (e.ref.branch === name || (e.ref.run != null && store.idx.runs.get(String(e.ref.run))?.branch === name) || (e.ref.pr != null && store.idx.pulls.get(String(e.ref.pr))?.head === name)));
  if (!b && !runs.length && !pulls.length) return html`${notFound('Branch', name)}${notesBlock('branch', name)}${historyBlock(evs, 'branch')}`;
  const path = name.split('/').map(enc).join('/');
  return html`${head(`Branch${b ? ` · ${kindLabel(b.kind)}` : ' · deleted or not polled'}`, name, html`${ext(gh(`/tree/${path}`), 'Open on GitHub')} · ${ext(gh(`/compare/testing...${path}`), 'Compare with testing')} · ${ext(gh(`/commits/${path}`), 'Commits')} · ${docsLink(name)} · <a href="${dashLink(`branch=${enc(name)}`)}" target="quark-dashboard">dashboard</a>`)}
    ${b ? html`<dl class="kv">
      <dt>Head</dt><dd>${commitChip(b.sha)} <span class="subj">${b.subject}</span></dd>
      <dt>By</dt><dd>${b.author} · ${when(b.date)}</dd>
      <dt>vs testing</dt><dd>${b.kind === 'testing' ? html`<span class="muted">this is testing</span>` : branchVsTesting(b)}</dd>
      <dt>Last CI</dt><dd>${b.lastRun ? html`${runBadge(b.lastRun)} <a href="#run=${b.lastRun.id}">${runOutcome(b.lastRun)}</a>` : html`<span class="muted">no runs</span>`}</dd>
      ${locals.length ? html`<dt>On this PC</dt><dd>${locals.map((l) => localChip(l.name))}</dd>` : ''}
      <dt>Role</dt><dd>${b.role || b.kind}${b.roleSet ? ' (set by you)' : ''}${b.stale ? html` <span class="tag t-warn">stale</span>` : ''} · <a href="#tab=editor&sec=structure">Structure</a></dd>
      ${b.description ? html`<dt>About</dt><dd class="md">${md(b.description)}</dd>` : ''}
    </dl>` : ''}
    ${unread('pulls') ? unknownLine('pulls') : ''}${unread('runs') ? unknownLine('runs', true) : ''}
    ${notesBlock('branch', name)}
    ${pulls.length ? html`<h3>Pull requests</h3><ul class="rows compact">${pulls.map((p) => html`<li class="row" data-action="row" data-href="#pr=${p.number}" tabindex="0"><div class="r-main">${prState(p)} ${prChip(p.number, p.title)} <a class="r-title" href="#pr=${p.number}">${p.title}</a></div><div class="r-meta">${p.state === 'open' ? checksBadge(p) : ''} <span class="muted">into ${p.base} · ${relOnly(p.updatedAt)}</span></div></li>`)}</ul>` : ''}
    <h3>CI runs ${runs.length ? html`<span class="count">${runs.length}</span>` : ''}</h3>
    ${runs.length ? html`<ul class="rows">${runs.slice(0, 15).map((r) => runRow(r, { showBranch: false }))}</ul>${runs.length > 15 ? html`<p class="small"><a href="#tab=runs&on=${enc(name)}">All ${runs.length} runs on this branch</a></p>` : ''}` : html`<p class="muted small">No runs known.</p>`}
    ${historyBlock(evs, 'branch')}`;
}

function commitDetail(sha) {
  const f = findCommit(sha);
  const s = store.state;
  const runs = runsOf((r) => r.sha && sha && r.sha.startsWith(sha.toLowerCase()));
  const full = f ? f.commit.sha : sha;
  const links = html`${ext(gh(`/commit/${full}`), 'Open on GitHub')} · <a href="${dashLink(`commit=${full}`)}" target="quark-dashboard">dashboard</a>`;
  if (!f && !runs.length) return html`${head('Commit', short(sha))}<p class="empty">This commit isn't in the data the server sent (it lists testing since production, each branch's head and local commits).</p><div class="d-links">${links}</div>${/^[0-9a-f]{4,64}$/i.test(sha) ? notesBlock('commit', sha) : ''}`;
  const c = f ? f.commit : { sha, subject: runs[0].title };
  const where = f ? f.where : [];
  const said = [];
  for (const w of where) {
    if (w.kind === 'testing') said.push(html`<li>on <b>testing</b>, not yet in production ${s.production ? `(${s.production.tag})` : ''}</li>`);
    if (w.kind === 'local') said.push(html`<li>local only: in ${localChip(w.name)}, not on testing</li>`);
    if (w.kind === 'local-head') said.push(html`<li>checked out in ${localChip(w.name)}</li>`);
    if (w.kind === 'branch-head') said.push(html`<li>head of ${branchChip(w.name)}</li>`);
    if (w.kind === 'production') said.push(html`<li>the production release ${s.production.tag}</li>`);
  }
  return html`${head(html`Commit ${commitChip(c.sha)}`, c.subject || short(c.sha), links)}
    <dl class="kv">
      ${c.author ? html`<dt>Author</dt><dd>${c.author}</dd>` : ''}
      ${c.date ? html`<dt>Date</dt><dd>${when(c.date)}</dd>` : ''}
      <dt>SHA</dt><dd class="mono small">${c.sha}</dd>
    </dl>
    ${notesBlock('commit', c.sha)}
    ${said.length ? html`<h3>Where it is</h3><ul class="where">${said}</ul>` : ''}
    ${runs.length ? html`<h3>CI runs on this commit</h3><ul class="rows">${runs.map((r) => runRow(r))}</ul>` : ''}`;
}

function versionDetail(v) {
  const s = store.state, p = s.production || {}, t = s.testing || {};
  const locals = (s.local || []).filter((l) => l.version === v);
  const rows = [];
  if (p.version === v) rows.push(html`<li><b>production</b>: ${ext(p.url, p.tag)} published ${when(p.publishedAt)}</li>`);
  if (t.version === v) rows.push(html`<li><b>testing</b>: head ${commitChip(t.sha)} ${t.prerelease ? html`· prerelease ${ext(t.prerelease.url, t.prerelease.tag)} ${relOnly(t.prerelease.publishedAt)}` : '· no prerelease yet'}</li>`);
  for (const l of locals) rows.push(html`<li>local copy ${localChip(l.name)} on ${branchChip(l.branch)}</li>`);
  const evs = eventsFor((e) => e.ref && e.ref.tag && (e.ref.tag === `v${v}` || e.ref.tag.startsWith(`test-${v}-`)), 20);
  const isProdOrOlder = p.version && cmp(v, p.version) <= 0;
  // a version newer than testing has no changelog (nor release) yet: no links to files that don't exist
  const future = t.version && cmp(v, t.version) > 0;
  if (future) {
    return html`${head('Version', v)}${notesBlock('version', v)}<p class="muted">${v} is newer than testing (${t.version}): it has no changelog or release yet.</p>`;
  }
  return html`${head('Version', v, html`${ext(gh(`/blob/testing/changelogs/${v}.md`), 'Short changelog')} · ${ext(gh(`/blob/testing/changelogs/full/${v}.md`), 'Full changelog')}${isProdOrOlder ? html` · ${ext(gh(`/releases/tag/v${v}`), `Release v${v}`)} · ${ext(`https://floration.github.io/quark/updates/${enc(v)}/`, 'Website page')}` : t.version === v && t.prerelease ? html` · ${ext(t.prerelease.url, t.prerelease.tag)}` : ''}`)}
    ${notesBlock('version', v)}
    ${unread('releases') ? unknownLine('releases') : ''}
    ${rows.length ? html`<h3>Where it is</h3><ul class="where">${rows}</ul>` : html`<p class="muted">Neither production, testing nor a local copy is on ${v} right now.</p>`}
    <h3>Releases and tags</h3>
    ${!store.historyLoaded ? html`<p class="muted small">Loading…</p>` : evs.length ? html`<ul class="events">${evs.map((e) => eventRow(e, { showDay: true }))}</ul>` : html`<p class="muted small">No tag or release for ${v} in the local log.</p>`}`;
}
function cmp(a, b) {
  const pa = String(a).split('.').map(Number), pb = String(b).split('.').map(Number);
  for (let i = 0; i < 3; i++) if ((pa[i] || 0) !== (pb[i] || 0)) return (pa[i] || 0) - (pb[i] || 0);
  return 0;
}

function localDetail(name) {
  const l = store.idx.local.get(name);
  if (!l) return html`${notFound('Local copy', name)}${notesBlock('local', name)}`;
  return html`${head('Local copy on this PC', name)}${localCard(l, { full: true, extra: notesBlock('local', name) })}
    ${historyBlock(eventsFor((e) => e.ref && e.ref.local === name), 'local copy')}`;
}

export function renderDetail(kind, id) {
  if (!store.state) return '';
  switch (kind) {
    case 'run': return runDetail(id);
    case 'pr': return prDetail(id);
    case 'branch': return branchDetail(id.replace(/^origin\//, ''));
    case 'commit': return commitDetail(id);
    case 'version': return versionDetail(id);
    case 'local': return localDetail(id);
    default: return '';
  }
}
