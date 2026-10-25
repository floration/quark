// The two overview pictures, drawn as plain HTML/CSS (so they reflow with the page and need no measuring):
//  - laneChart: every branch and local copy against testing, behind to the left and ahead to the right
//  - ciTimeline: the last 24 h of CI runs, one lane per branch, bars coloured by result
import { html, shortBranch, ms, plural, runState, stateClass, stateLabel, fmtHour, fmtTime, dur, aheadBehind, isLive, enc } from './util.js';

const sq = (n, max) => (n ? Math.max(1.5, (Math.sqrt(n) / Math.sqrt(Math.max(1, max))) * 100) : 0);

// rows: [{label, sub, href, ahead, behind, tag, state, tip}]
export function laneChart(rows, { reference = 'testing', refLabel = '' } = {}) {
  const max = Math.max(1, ...rows.map((r) => Math.max(r.ahead || 0, r.behind || 0)));
  const row = (r) => html`<div class="lane${r.isRef ? ' is-ref' : ''}" role="listitem" data-action="row" data-href="${r.href}" tabindex="0" data-tip="${r.tip || `${r.label}: ${aheadBehind(r.ahead, r.behind, reference)}`}">
      <span class="lane-label"><span class="lane-tag k-${r.tag}">${r.tagText || r.tag}</span><a class="lane-name" href="${r.href}">${r.label}</a>${r.sub ? html`<span class="lane-sub">${r.sub}</span>` : ''}</span>
      <span class="lane-l">${r.behind ? html`<span class="lane-n">${r.behind}</span><span class="lane-bar b" style="width:${sq(r.behind, max).toFixed(1)}%"></span>` : ''}</span>
      <span class="lane-axis"></span>
      <span class="lane-r">${r.isRef ? html`<span class="lane-refnote">${refLabel}</span>` : r.ahead ? html`<span class="lane-bar a" style="width:${sq(r.ahead, max).toFixed(1)}%"></span><span class="lane-n">${r.ahead}</span>` : r.behind ? '' : html`<span class="lane-even">even</span>`}</span>
      <span class="lane-st">${r.state || ''}</span>
    </div>`;
  return html`<div class="lanes" role="list">
    <div class="lane lane-head" aria-hidden="true"><span class="lane-label"></span><span class="lane-l">← behind ${reference}</span><span class="lane-axis"></span><span class="lane-r">ahead of ${reference} →</span><span class="lane-st">last CI</span></div>
    ${rows.map(row)}
  </div>
  <div class="legend"><span><i class="sw sw-b"></i>commits on ${reference} that it doesn't have</span><span><i class="sw sw-a"></i>commits it has that ${reference} doesn't</span><span class="muted">bar length: square root of the count (the numbers are exact)</span></div>`;
}

export function ciTimeline(runs, { hours = 24, now = Date.now() } = {}) {
  const from = now - hours * 3600e3, span = now - from;
  const inWin = runs.filter((r) => {
    const end = isLive(r) ? now : ms(r.updatedAt);
    return end >= from && ms(r.createdAt) <= now;
  });
  if (!inWin.length) return html`<p class="muted">No CI runs in the last ${hours} hours.</p>`;
  const lanes = new Map();
  for (const r of inWin) {
    const k = r.branch || '(no branch)';
    if (!lanes.has(k)) lanes.set(k, []);
    lanes.get(k).push(r);
  }
  const order = [...lanes.entries()].sort((a, b) => Math.max(...b[1].map((r) => ms(r.createdAt))) - Math.max(...a[1].map((r) => ms(r.createdAt))));
  const ticks = [];
  // ticks on Dallas-local hours divisible by 3 (UTC offset is a whole hour for Dallas)
  const first = Math.ceil(from / 3600e3) * 3600e3;
  for (let t = first; t < now - 1.2 * 3600e3; t += 3600e3) {
    const h = Number(new Intl.DateTimeFormat('en-US', { timeZone: 'America/Chicago', hour: 'numeric', hourCycle: 'h23' }).format(t));
    if (h % 3 === 0) ticks.push(t);
  }
  const pct = (t) => ((Math.min(Math.max(t, from), now) - from) / span) * 100;
  const bar = (r) => {
    const st = runState(r);
    const s = ms(r.createdAt), e = isLive(r) ? now : ms(r.updatedAt);
    const left = pct(s), w = Math.max(0.5, pct(e) - left);
    const tip = `${r.workflow} on ${r.branch}: ${stateLabel(st)} · started ${fmtTime(r.createdAt)}${isLive(r) ? ' · still running' : st === 'unread' ? ' · last seen running (GitHub not read since the start)' : ` · ${dur(r.durationSec ?? (e - s) / 1000)}`} · ${r.title || ''}`;
    return html`<a class="tl-bar s-${stateClass(st)}" href="#run=${r.id}" style="left:${left.toFixed(2)}%;width:${w.toFixed(2)}%" data-tip="${tip}" aria-label="${tip}"></a>`;
  };
  const counts = {};
  for (const r of inWin) { const c = stateClass(runState(r)); counts[c] = (counts[c] || 0) + 1; }
  return html`<div class="tl">
    <div class="tl-axis"><span class="tl-label"></span><span class="tl-track">${ticks.map((t) => html`<span class="tl-tick" style="left:${pct(t).toFixed(2)}%">${fmtHour(t)}</span>`)}<span class="tl-tick now" style="left:100%">now</span></span></div>
    ${order.map(([name, rs]) => html`<div class="tl-lane"><a class="tl-label" href="#branch=${enc(name)}" title="${name}">${shortBranch(name)}</a><span class="tl-track">${ticks.map((t) => html`<span class="tl-grid" style="left:${pct(t).toFixed(2)}%"></span>`)}${rs.map(bar)}</span></div>`)}
  </div>
  <div class="legend">
    <span><i class="sw s-good"></i>passed${counts.good ? ` (${counts.good})` : ''}</span>
    <span><i class="sw s-bad"></i>failed${counts.bad ? ` (${counts.bad})` : ''}</span>
    <span><i class="sw s-run"></i>running${counts.run ? ` (${counts.run})` : ''}</span>
    <span><i class="sw s-wait"></i>queued${counts.wait ? ` (${counts.wait})` : ''}</span>
    <span><i class="sw s-off"></i>cancelled / skipped${counts.off ? ` (${counts.off})` : ''}</span>
    ${counts.warn ? html`<span><i class="sw s-warn"></i>last seen running (${counts.warn})</span>` : ''}
    <span class="muted">${plural(inWin.length, 'run')} in the last ${hours} h; hover a bar for details, click to open</span>
  </div>`;
}
