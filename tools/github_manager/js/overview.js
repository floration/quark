// Overview: production vs testing, what's running, latest failures, branch lanes, the CI day, PRs, local copies.
import { store } from './store.js';
import {
  html, when, relOnly, plural, versionChip, commitChip, branchChip, prChip, ghLink, aheadBehind, runBadge,
  enc, localChip, gh, RUNNING, ms, docsLink, shortBranch,
} from './util.js';
import { failureSentence, commitList, eventRow, checksBadge, lastRunCell, runningSentence } from './parts.js';
import { laneChart, ciTimeline } from './charts.js';
import { notes, noteCard, sortNotes, noteBadge } from './notes.js';

const preVersion = (tag) => (/^test-(\d+\.\d+\.\d+)/.exec(tag || '') || [])[1];
const FAILED = new Set(['failure', 'timed_out', 'startup_failure']);

function releases(s) {
  const p = s.production || {}, t = s.testing || {};
  const pre = t.prerelease;
  return html`<section class="releases">
    <div class="card rel-card k-production">
      <div class="eyebrow">Production <span class="muted">· what players get</span></div>
      <div class="rel-version">${versionChip(p.version, p.url)}</div>
      <div class="rel-lines">
        <div>tag <span class="chip c-tag"><a href="#version=${enc(p.version || '')}">${p.tag || '—'}</a>${ghLink(p.url, 'Open the release on GitHub')}</span> ${commitChip(p.sha)}</div>
        <div>published ${when(p.publishedAt)}</div>
        <div>branch ${branchChip(s.defaultBranch, { label: 'default branch' })}</div>
      </div>
    </div>
    <div class="rel-gap">
      <span class="rel-arrow" aria-hidden="true"></span>
      <span class="rel-gap-n">${t.aheadOfProduction ?? '?'}</span>
      <span class="rel-gap-t">${t.aheadOfProduction === 1 ? 'commit' : 'commits'} on testing not yet in production</span>
      <button class="linkbtn small" data-action="scrollto" data-target="notinprod">see them</button>
    </div>
    <div class="card rel-card k-testing">
      <div class="eyebrow">Testing <span class="muted">· the TESTING channel</span></div>
      <div class="rel-version">${versionChip(t.version, pre ? pre.url : null)}</div>
      <div class="rel-lines">
        <div>head ${commitChip(t.sha)} <span class="subj" title="${t.subject}">${t.subject}</span></div>
        <div>committed ${when(t.date)}</div>
        <div>${pre ? html`prerelease <span class="chip c-tag"><a href="#version=${enc(preVersion(pre.tag) || t.version || '')}">${pre.tag}</a>${ghLink(pre.url, 'Open the prerelease on GitHub')}</span> ${relOnly(pre.publishedAt)}${preVersion(pre.tag) && t.version && preVersion(pre.tag) !== t.version ? html` <span class="tag t-warn" title="The newest prerelease is for an older version: ${t.version}'s TESTING build hasn't been published yet (its run may still be going, or it failed)">not ${t.version} yet</span>` : ''}` : html`<span class="muted">no prerelease for this head yet</span>`}</div>
      </div>
    </div>
  </section>`;
}

// Pinned notes (open ones first); read-only here, the Editor tab edits them.
function pinnedNotes() {
  if (!notes.list) return '';
  const pinned = sortNotes(notes.list.filter((n) => n.pinned));
  const open = notes.list.filter((n) => n.status === 'open').length;
  if (!pinned.length) {
    return html`<section class="card notes-card is-empty"><h2>Notes</h2><p class="small muted">Nothing pinned${open ? html` · <a href="#tab=editor">${plural(open, 'open note')}</a>` : ''}. Pin a note in the <a href="#tab=editor">Editor</a> to keep it here.</p></section>`;
  }
  return html`<section class="card notes-card">
    <h2>Notes <span class="count">${pinned.length} pinned</span><span class="grow"></span><a class="small" href="#tab=editor">${open ? `All notes (${open} open)` : 'All notes'}</a></h2>
    <ul class="notes pinned">${pinned.slice(0, 6).map((n) => noteCard(n, { scope: 'o', actions: false }))}</ul>
    ${pinned.length > 6 ? html`<p class="small"><a href="#tab=editor">${plural(pinned.length - 6, 'more pinned note')}</a></p>` : ''}
  </section>`;
}

function running(s) {
  const rs = store.idx.running;
  return html`<section class="card">
    <h2>Running now ${rs.length ? html`<span class="count run">${rs.length}</span>` : ''}</h2>
    ${rs.length ? html`<ul class="rows">${rs.map((r) => html`<li class="row" data-action="row" data-href="#run=${r.id}" tabindex="0">
        <div class="r-main">${runBadge(r)}<a class="r-title" href="#run=${r.id}">${r.workflow}</a> <span class="muted">on</span> ${branchChip(r.branch)}</div>
        <div class="r-meta"><span class="subj">${r.title}</span></div>
        <div class="r-out">${runningSentence(r)} ${ghLink(r.url)}</div></li>`)}</ul>`
      : html`<p class="empty">Nothing is running. ${lastFinished(s)}</p>`}
  </section>`;
}
function lastFinished(s) {
  const r = (s.runs || []).find((x) => !RUNNING.has(x.status));
  return r ? html`The last run finished ${relOnly(r.updatedAt)} (${r.workflow} on ${r.branch}, ${runOutcomeWord(r)}).` : '';
}
const runOutcomeWord = (r) => ({ success: 'passed', failure: 'failed', cancelled: 'cancelled', skipped: 'skipped' }[r.conclusion] || r.conclusion);

function failures(s) {
  // latest failure per branch+workflow that hasn't been followed by a pass there
  const seen = new Set(), out = [];
  for (const r of s.runs || []) {
    const k = `${r.branch}|${r.workflow}`;
    if (seen.has(k) || RUNNING.has(r.status)) continue;
    seen.add(k);
    if (FAILED.has(r.conclusion)) out.push(r);
  }
  const older = (s.runs || []).filter((r) => FAILED.has(r.conclusion) && !out.includes(r)).length;
  return html`<section class="card">
    <h2>Failing now ${out.length ? html`<span class="count bad">${out.length}</span>` : ''}</h2>
    <p class="hint">The latest run of each branch and workflow, where that run failed.</p>
    ${out.length ? html`<ul class="rows">${out.slice(0, 6).map((r) => html`<li class="row" data-action="row" data-href="#run=${r.id}" tabindex="0">
        <div class="r-main">${runBadge(r)}<a class="r-title" href="#run=${r.id}">${r.workflow}</a> <span class="muted">on</span> ${branchChip(r.branch)} ${prOfBranch(r.branch)}</div>
        <div class="r-out">${failureSentence(r)} ${ghLink(r.url)}</div></li>`)}</ul>`
      : html`<p class="empty good">Every branch's latest run passed.</p>`}
    ${older ? html`<p class="small"><a href="#tab=runs&result=failure">${plural(older, 'older failed run')}</a> in the CI runs tab.</p>` : ''}
  </section>`;
}
function prOfBranch(b) {
  const p = store.idx.pullByHead.get(b);
  return p && p.state === 'open' ? prChip(p.number, p.title) : '';
}

function lanes(s) {
  const t = s.testing || {}, rows = [];
  const prodB = (s.branches || []).find((b) => b.kind === 'production');
  rows.push({ label: 'production', sub: `${s.production?.tag || ''}`, tag: 'production', tagText: 'prod', href: `#version=${enc(s.production?.version || '')}`,
    ahead: prodB ? prodB.ahead : 0, behind: t.aheadOfProduction ?? (prodB ? prodB.behind : 0), state: prodB ? lastRunCell(prodB.lastRun) : '' });
  rows.push({ label: 'testing', sub: t.version, tag: 'testing', tagText: 'test', href: '#branch=testing', isRef: true, ahead: 0, behind: 0,
    state: lastRunCell((s.branches || []).find((b) => b.kind === 'testing')?.lastRun), tip: `testing ${t.version}: the reference line` });
  const brs = (s.branches || []).filter((b) => b.kind !== 'production' && b.kind !== 'testing');
  const open = (b) => { const p = store.idx.pullByHead.get(b.name); return p && p.state === 'open'; };
  brs.sort((a, b) => (open(b) - open(a)) || (ms(b.date) - ms(a.date)));
  const shown = brs.filter((b) => open(b) || b.ahead > 0 || Date.now() - ms(b.date) < 3 * 86400e3).slice(0, 12);
  for (const b of shown) {
    const p = store.idx.pullByHead.get(b.name);
    rows.push({ label: shortBranch(b.name), sub: p && p.state === 'open' ? `PR #${p.number}` : '', tag: b.kind, href: `#branch=${enc(b.name)}`,
      ahead: b.ahead, behind: b.behind, state: lastRunCell(b.lastRun), tip: `${b.name}: ${aheadBehind(b.ahead, b.behind)}${p ? ` · PR #${p.number} ${p.state}` : ''}` });
  }
  for (const l of s.local || []) {
    rows.push({ label: l.name, sub: shortBranch(l.branch), tag: 'local', tagText: 'PC', href: `#local=${enc(l.name)}`, ahead: l.aheadOfTesting, behind: l.behindTesting,
      state: l.dirty ? html`<span class="tag t-dirty">${plural(l.dirty, 'file')} changed</span>` : html`<span class="muted small">clean</span>`,
      tip: `Local copy ${l.name} on ${l.branch}: ${aheadBehind(l.aheadOfTesting, l.behindTesting)}${l.dirty ? `, ${l.dirty} uncommitted` : ''}` });
  }
  const hidden = brs.length - shown.length;
  return html`<section class="card wide">
    <h2>Where everything stands against testing</h2>
    <p class="hint">Production, each work branch and each copy on this PC, measured in commits against <b>testing ${t.version}</b>.</p>
    ${laneChart(rows, { refLabel: [t.version, t.subject && t.subject.slice(0, 60)].filter(Boolean).join(' · ') })}
    ${hidden ? html`<p class="small muted">${plural(hidden, 'older branch', 'older branches')} with nothing new not shown; <a href="#tab=branches">see all branches</a>.</p>` : ''}
  </section>`;
}

function timeline(s) {
  return html`<section class="card wide"><h2>CI over the last 24 hours</h2>${ciTimeline(s.runs || [])}</section>`;
}

function openPulls(s) {
  const open = (s.pulls || []).filter((p) => p.state === 'open');
  return html`<section class="card">
    <h2>Open pull requests ${open.length ? html`<span class="count">${open.length}</span>` : ''}</h2>
    ${open.length ? html`<ul class="rows compact">${open.map((p) => html`<li class="row" data-action="row" data-href="#pr=${p.number}" tabindex="0">
      <div class="r-main">${prChip(p.number, p.title)} <a class="r-title" href="#pr=${p.number}">${p.title}</a>${p.draft ? html` <span class="tag t-draft">draft</span>` : ''}${noteBadge('pr', p.number, `#pr=${p.number}`)}</div>
      <div class="r-meta">${checksBadge(p)} ${branchChip(p.head)} <span class="muted">→ ${p.base} · updated ${relOnly(p.updatedAt)}</span> ${docsLink(`PR #${p.number}`)}</div></li>`)}</ul>`
      : html`<p class="empty">No open pull requests.</p>`}
    <p class="small"><a href="#tab=pulls">All pull requests</a></p>
  </section>`;
}

function locals(s) {
  const ls = s.local || [];
  return html`<section class="card">
    <h2>Copies on this PC</h2>
    ${ls.length ? html`<ul class="rows compact">${ls.map((l) => html`<li class="row" data-action="row" data-href="#local=${enc(l.name)}" tabindex="0">
      <div class="r-main">${localChip(l.name)} <span class="muted">on</span> ${branchChip(l.branch)} ${versionChip(l.version)}${l.dirty ? html` <span class="tag t-dirty">${plural(l.dirty, 'uncommitted file')}</span>` : ''}${l.error ? html` <span class="tag t-closed">error</span>` : ''}${noteBadge('local', l.name, `#local=${enc(l.name)}`)}</div>
      <div class="r-meta muted">${l.error ? l.error : html`${aheadBehind(l.aheadOfTesting, l.behindTesting)}${l.worktreeOf ? ` · worktree of ${l.worktreeOf}` : ''} · head ${relOnly(l.date)}`}</div></li>`)}</ul>`
      : html`<p class="empty">No local copies found.</p>`}
    <p class="small"><a href="#tab=local">Local copies</a></p>
  </section>`;
}

function notInProd(s) {
  const t = s.testing || {}, cs = t.commitsSinceProduction || [];
  return html`<section class="card" id="notinprod">
    <h2>On testing, not yet in production <span class="count">${t.aheadOfProduction ?? cs.length}</span></h2>
    <p class="hint">What ${t.version} has that ${s.production?.tag || 'production'} doesn't (first-parent, newest first). <a href="${gh(`/compare/${s.production?.tag || ''}...testing`)}" target="_blank" rel="noopener">Compare on GitHub</a></p>
    ${commitList(cs, { limit: 10, id: 'notinprod-more' })}
    ${t.aheadOfProduction > cs.length ? html`<p class="small muted">Showing the newest ${cs.length}.</p>` : ''}
  </section>`;
}

function activity() {
  if (!store.historyLoaded) return html`<section class="card"><h2>Recent activity</h2><p class="muted">Loading the local log…</p></section>`;
  const ev = store.history.slice(0, 10);
  return html`<section class="card">
    <h2>Recent activity</h2>
    ${ev.length ? html`<ul class="events">${ev.map((e) => eventRow(e, { showDay: true }))}</ul>` : html`<p class="empty">The log is empty so far.</p>`}
    <p class="small"><a href="#tab=history">Full history</a></p>
  </section>`;
}

export const overview = {
  title: 'Overview',
  wantsHistory: true,
  mount(el) { el.innerHTML = '<div id="ov"></div>'; },
  update() {
    const s = store.state;
    if (!s.production && !s.testing && !(s.poll && s.poll.lastOk)) {
      document.getElementById('ov').innerHTML = String(html`<section class="card offline">
        <h2>Waiting for the first poll</h2>
        <p>The server is running and is reading GitHub for the first time${s.poll && s.poll.lastError ? '' : ' (the first poll also makes its own mirror of the repo, so it can take a minute or two)'}. This page fills in by itself.</p>
        ${s.poll && s.poll.lastError ? html`<p class="msg bad">${s.poll.lastError}</p>` : ''}</section>`);
      return;
    }
    document.getElementById('ov').innerHTML = String(html`
      ${releases(s)}
      ${pinnedNotes()}
      <div class="grid2">${running(s)}${failures(s)}</div>
      ${lanes(s)}
      ${timeline(s)}
      <div class="grid2">${openPulls(s)}${locals(s)}</div>
      <div class="grid2">${notInProd(s)}${activity()}</div>`);
  },
};
