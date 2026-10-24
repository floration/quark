// The job log panel: fetched from /api/log, failed region (##[error]) scrolled into view, search with next / previous.
import { store, loadLog, logUrl } from './store.js';
import { html, esc, debounce, ghLink, plural } from './util.js';

let cur = null; // {run, job, lines, hits, at, errors}
const $ = (id) => document.getElementById(id);
const TS = /^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(\.\d+)?Z ?/;
const timeFmt = new Intl.DateTimeFormat('en-US', { timeZone: 'America/Chicago', hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23' });

export const isLogOpen = () => !$('logpanel').hidden;

function jobOf(run, job) {
  const r = store.idx && store.idx.runs.get(String(run));
  return { r, j: r && (r.jobs || []).find((x) => String(x.id) === String(job)) };
}

export async function openLog(run, job, onClose) {
  if (cur && cur.run === String(run) && cur.job === String(job) && isLogOpen()) return;
  const p = $('logpanel');
  const { r, j } = jobOf(run, job);
  cur = { run: String(run), job: String(job), lines: [], hits: [], at: -1, errors: [], onClose };
  p.hidden = false;
  document.body.classList.add('log-open');
  $('log-title').innerHTML = String(html`${j ? j.name : `job ${job}`} <span class="muted">· ${r ? `${r.workflow} on ${r.branch}` : `run ${run}`}</span>`);
  $('log-links').innerHTML = String(html`<a href="${logUrl(run, job)}" target="_blank" rel="noopener">raw</a> ${ghLink(j ? j.url : null, 'Open the job on GitHub')}`);
  $('log-body').innerHTML = '<p class="muted pad">Loading the log…</p>';
  $('log-q').value = '';
  $('log-count').textContent = '';
  $('log-errnav').hidden = true;
  let got;
  try {
    got = await loadLog(run, job);
  } catch (e) {
    if (cur && cur.job === String(job)) $('log-body').innerHTML = String(html`<p class="msg bad">Couldn't load the log: ${e.message}</p>`);
    return;
  }
  if (!cur || cur.job !== String(job)) return;
  if (got.missing != null) {
    $('log-body').innerHTML = String(html`<div class="pad"><p class="empty">No log for this job. ${got.missing || 'GitHub no longer has it and the server never saved it.'}</p>${j && j.failedSteps && j.failedSteps.length ? html`<p>It failed at <q>${j.failedSteps.join(', ')}</q>.</p>` : ''}</div>`);
    return;
  }
  render(got.text);
}

function render(text) {
  const lines = text.replace(/\r\n?/g, '\n').replace(/\n$/, '').split('\n');
  cur.lines = lines;
  const out = new Array(lines.length);
  const errors = [];
  for (let i = 0; i < lines.length; i++) {
    let s = lines[i], ts = '';
    const m = TS.exec(s);
    if (m) { ts = timeFmt.format(Date.parse(m[1] + 'Z')); s = s.slice(m[0].length); }
    let cls = '';
    if (s.startsWith('##[error]')) { cls = 'l-err'; s = s.slice(9); errors.push(i); }
    else if (s.startsWith('##[warning]')) { cls = 'l-warn'; s = s.slice(11); }
    else if (s.startsWith('##[group]')) { cls = 'l-group'; s = s.slice(9); }
    else if (s.startsWith('##[endgroup]')) { cls = 'l-end'; s = ''; }
    else if (/\b(FAILED|FAIL:|error:|Error:)/.test(s)) cls = 'l-bad';
    out[i] = `<div class="ll ${cls}" id="L${i + 1}"><span class="ln">${i + 1}</span><span class="lt">${ts}</span><span class="lx">${esc(s)}</span></div>`;
  }
  cur.errors = errors;
  $('log-body').innerHTML = `<div class="loglines">${out.join('')}</div>`;
  $('log-meta').textContent = `${lines.length.toLocaleString('en-US')} lines${errors.length ? ` · ${plural(errors.length, 'error')}` : ''}`;
  $('log-errnav').hidden = !errors.length;
  if (errors.length) jumpTo(errors[0], 'l-cur');
}

function jumpTo(i, mark = 'l-cur') {
  const body = $('log-body');
  for (const n of body.querySelectorAll('.l-cur')) n.classList.remove('l-cur');
  const n = document.getElementById(`L${i + 1}`);
  if (!n) return;
  n.classList.add(mark);
  // the first error line in the middle of the panel, with the lines before it (the failing test) visible above
  body.scrollTop = Math.max(0, n.offsetTop - body.clientHeight * 0.45);
}

function search(q) {
  const body = $('log-body');
  for (const n of body.querySelectorAll('.hit')) {
    n.classList.remove('hit');
    const x = n.querySelector('.lx');
    x.textContent = x.textContent; // drops the <mark>s
  }
  cur.hits = []; cur.at = -1;
  if (!q) { $('log-count').textContent = ''; return; }
  const ql = q.toLowerCase();
  for (let i = 0; i < cur.lines.length; i++) {
    if (!cur.lines[i].toLowerCase().includes(ql)) continue;
    const n = document.getElementById(`L${i + 1}`);
    const x = n && n.querySelector('.lx');
    if (!x || !x.textContent.toLowerCase().includes(ql)) continue; // the match was in the stripped timestamp
    cur.hits.push(i);
    n.classList.add('hit');
    x.innerHTML = esc(x.textContent).replace(new RegExp(esc(q).replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi'), (m) => `<mark>${m}</mark>`);
  }
  step(1);
}
function step(d) {
  if (!cur.hits.length) { $('log-count').textContent = $('log-q').value ? 'no matches' : ''; return; }
  cur.at = (cur.at + d + cur.hits.length) % cur.hits.length;
  jumpTo(cur.hits[cur.at]);
  $('log-count').textContent = `${cur.at + 1} / ${cur.hits.length}`;
}
let errAt = 0;
function stepErr(d) {
  if (!cur.errors.length) return;
  errAt = (errAt + d + cur.errors.length) % cur.errors.length;
  jumpTo(cur.errors[errAt]);
}

export function closeLog(silent) {
  const p = $('logpanel');
  if (p.hidden) return;
  p.hidden = true;
  document.body.classList.remove('log-open');
  const done = cur && cur.onClose;
  cur = null;
  $('log-body').innerHTML = '';
  if (!silent && done) done();
}

// wiring (the panel's markup is in index.html)
$('log-close').addEventListener('click', () => closeLog(false));
$('log-q').addEventListener('input', debounce((e) => cur && search(e.target.value.trim()), 180));
$('log-q').addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && cur) { e.preventDefault(); step(e.shiftKey ? -1 : 1); }
});
$('log-prev').addEventListener('click', () => cur && step(-1));
$('log-next').addEventListener('click', () => cur && step(1));
$('log-err-prev').addEventListener('click', () => cur && stepErr(-1));
$('log-err-next').addEventListener('click', () => cur && stepErr(1));
document.addEventListener('keydown', (e) => {
  if (!isLogOpen()) return;
  if (e.key === 'Escape') { e.preventDefault(); e.stopImmediatePropagation(); closeLog(false); }
  if ((e.ctrlKey || e.metaKey) && e.key === 'f') { e.preventDefault(); $('log-q').focus(); $('log-q').select(); }
});
