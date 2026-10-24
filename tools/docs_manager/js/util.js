/* Claude Documents manager: shared helpers (DOM, escaping, Dallas dates, humanized labels). */
'use strict';
window.DM = window.DM || {};

(function (DM) {
  const TZ = 'America/Chicago';
  const DONE = new Set(['fixed', 'wontfix', 'answered', 'told', 'done']);
  DM.DONE = DONE;

  // ---------- DOM ----------
  // h('div', {class: 'x', title: 'y', onclick: fn}, 'text', child, [more]). Strings become text nodes, never HTML.
  function h(tag, props, ...kids) {
    const el = document.createElement(tag);
    if (props) setProps(el, props);
    append(el, kids);
    return el;
  }
  function setProps(el, props) {
    for (const k in props) {
      const v = props[k];
      if (v == null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'text') el.textContent = v;
      else if (k === 'dataset') Object.assign(el.dataset, v);
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'href') { const u = safeHref(v); if (u) el.setAttribute('href', u); }
      else el.setAttribute(k, v === true ? '' : String(v));
    }
  }
  function append(el, kids) {
    for (const c of kids) {
      if (c == null || c === false) continue;
      if (Array.isArray(c)) append(el, c);
      else if (c instanceof Node) el.appendChild(c);
      else el.appendChild(document.createTextNode(String(c)));
    }
  }
  const SVGNS = 'http://www.w3.org/2000/svg';
  function s(tag, attrs, ...kids) {
    const el = document.createElementNS(SVGNS, tag);
    if (attrs) for (const k in attrs) {
      const v = attrs[k];
      if (v == null || v === false) continue;
      if (k === 'text') el.textContent = v;
      else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else el.setAttribute(k, String(v));
    }
    for (const c of kids.flat()) if (c != null && c !== false) el.appendChild(c instanceof Node ? c : document.createTextNode(String(c)));
    return el;
  }
  function clear(el) { while (el.firstChild) el.removeChild(el.firstChild); return el; }
  function $(sel, root) { return (root || document).querySelector(sel); }
  function $$(sel, root) { return Array.from((root || document).querySelectorAll(sel)); }

  // Only http(s) leaves the page; '#...' stays inside it. Anything else (javascript:, data:, file:) is dropped.
  function safeUrl(u) {
    if (typeof u !== 'string') return null;
    const t = u.trim();
    return /^https?:\/\/[^\s"'<>]+$/i.test(t) ? t : null;
  }
  function safeHref(u) {
    if (typeof u === 'string' && /^#[^\s"'<>]*$/.test(u)) return u;
    return safeUrl(u);
  }
  // An external link (new tab) when the url is safe, plain text otherwise.
  function ext(url, text, cls, title) {
    const u = safeUrl(url);
    if (!u) return h('span', {class: cls, title}, text);
    return h('a', {class: cls, href: u, target: '_blank', rel: 'noopener noreferrer', title: title || u}, text);
  }

  function esc(t) {
    return String(t == null ? '' : t).replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
  }

  // ---------- dates (Dallas) ----------
  const fmtCache = {};
  function fmt(opts) {
    const key = JSON.stringify(opts);
    return fmtCache[key] || (fmtCache[key] = new Intl.DateTimeFormat('en-US', Object.assign({timeZone: TZ}, opts)));
  }
  function toDate(iso) {
    if (!iso) return null;
    const d = iso instanceof Date ? iso : new Date(iso);
    return isNaN(d) ? null : d;
  }
  function parts(d) {
    const o = {};
    for (const p of fmt({year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit', hourCycle: 'h23'}).formatToParts(d)) o[p.type] = p.value;
    return o;
  }
  // "2026-10-23" as Dallas calendar day
  function dayKey(iso) { const d = toDate(iso); if (!d) return ''; const p = parts(d); return `${p.year}-${p.month}-${p.day}`; }
  function hhmm(iso) { const d = toDate(iso); if (!d) return ''; const p = parts(d); return `${p.hour === '24' ? '00' : p.hour}:${p.minute}`; }
  function dayLabel(iso) { const d = toDate(iso); return d ? fmt({weekday: 'short', month: 'short', day: 'numeric'}).format(d).replace(',', '') : ''; }
  function full(iso) {
    const d = toDate(iso);
    if (!d) return '';
    return fmt({weekday: 'short', year: 'numeric', month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23', timeZoneName: 'short'}).format(d) + ' (Dallas)';
  }
  // A date-only value ("2026-10-24") is a calendar day, not an instant: format it without a zone shift.
  function dateOnly(ymd) {
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(ymd || '');
    if (!m) return ymd || '';
    const d = new Date(Date.UTC(+m[1], +m[2] - 1, +m[3], 12));
    return new Intl.DateTimeFormat('en-US', {timeZone: 'UTC', weekday: 'short', month: 'short', day: 'numeric'}).format(d).replace(',', '');
  }
  function rel(iso, now) {
    const d = toDate(iso);
    if (!d) return '';
    const sec = Math.round(((now || Date.now()) - d.getTime()) / 1000);
    const a = Math.abs(sec), fut = sec < 0;
    let t;
    if (a < 45) return 'just now';
    if (a < 3600) t = Math.round(a / 60) + ' min';
    else if (a < 86400) { const hr = a / 3600; t = (hr < 10 ? Math.round(hr * 2) / 2 : Math.round(hr)) + ' h'; }
    else {
      const days = Math.round(a / 86400);
      if (days === 1) return fut ? 'tomorrow' : 'yesterday';
      t = days < 14 ? days + ' days' : days < 60 ? Math.round(days / 7) + ' weeks' : Math.round(days / 30) + ' months';
    }
    return fut ? 'in ' + t : t + ' ago';
  }
  // Day header in the change log: "Today, Sat Oct 24", "Yesterday, Fri Oct 23", "Thu Oct 22".
  function dayHeader(key) {
    const today = dayKey(new Date());
    const y = dayKey(new Date(Date.now() - 86400000));
    const [Y, M, D] = key.split('-').map(Number);
    const lbl = new Intl.DateTimeFormat('en-US', {timeZone: 'UTC', weekday: 'long', month: 'long', day: 'numeric'}).format(new Date(Date.UTC(Y, M - 1, D, 12))).replace(',', '');
    return key === today ? 'Today, ' + lbl : key === y ? 'Yesterday, ' + lbl : lbl;
  }
  // <time> that shows relative time and the exact Dallas time on hover; refreshed by DM.tickTimes().
  function timeEl(iso, mode, prefix) {
    const d = toDate(iso);
    if (!d) return h('span', {class: 'faint'}, '');
    const el = h('time', {datetime: d.toISOString(), title: full(d), class: 't'});
    el.dataset.mode = mode || 'rel';
    if (prefix) el.dataset.prefix = prefix;
    paintTime(el);
    return el;
  }
  function paintTime(el) {
    const iso = el.getAttribute('datetime'), m = el.dataset.mode, p = el.dataset.prefix || '';
    let t;
    if (m === 'clock') t = hhmm(iso);
    else if (m === 'clockrel') t = hhmm(iso) + ' (' + rel(iso) + ')';
    else if (m === 'day') t = dayLabel(iso);
    else if (m === 'dayclock') t = dayLabel(iso) + ' ' + hhmm(iso);
    else if (m === 'smart') { // today: "14:02 (3 h ago)", older: "Fri Oct 23 14:02"
      t = dayKey(iso) === dayKey(new Date()) ? hhmm(iso) + ' · ' + rel(iso) : dayLabel(iso) + ' ' + hhmm(iso);
    } else t = rel(iso);
    el.textContent = p + t;
  }
  function tickTimes() { for (const el of document.querySelectorAll('time.t')) paintTime(el); }

  // ---------- humanized labels ----------
  const FILE_TITLES = {
    '00-agent-rules.md': 'Agent rules', '01-known-bugs.md': 'Known bugs', '02-currently-working-on.md': 'Currently working on',
    '03-possible-bugs.md': 'Possible bugs', '04-known-performance-improvements.md': 'Known performance improvements',
    '05-possible-performance-improvements.md': 'Possible performance improvements', '06-things-to-tell-the-user.md': 'Things to tell q',
    '07-things-to-ask-the-user.md': 'Things to ask q', '08-other.md': 'Other notes', '09-answers-from-q.md': 'Answers from q', 'README.md': 'README',
  };
  const FILE_SHORT = {'01': 'Bugs', '02': 'Working on', '03': 'Possible bugs', '04': 'Perf fixes', '05': 'Perf ideas', '06': 'To tell q', '07': 'To ask q', '08': 'Other', '09': 'Answers'};
  function fileTitle(name) {
    const f = DM.state && DM.state.fileByName && DM.state.fileByName[name];
    let t = (f && f.title) || FILE_TITLES[name] || String(name || '').replace(/^\d+-/, '').replace(/\.md$/, '').replace(/-/g, ' ');
    t = t.replace(/\bthe user\b/gi, 'q').replace(/\bthe owner\b/gi, 'q');
    return t.charAt(0).toUpperCase() + t.slice(1);
  }
  function fileShort(name) { return FILE_SHORT[String(name || '').slice(0, 2)] || fileTitle(name); }

  // Status words -> plain language
  const STATUS_WORDS = {open: 'Open', 'in-progress': 'In progress', fixed: 'Fixed', wontfix: "Won't fix", answered: 'Answered',
    told: 'Told q', done: 'Done', unknown: 'No status'};
  function statusWord(s) { return STATUS_WORDS[s] || (s ? s.charAt(0).toUpperCase() + s.slice(1) : 'No status'); }

  // What an entry waits on, as board columns. "claude" splits into working (in-progress) and nobody yet (open).
  const WAIT = [
    {key: 'owner', label: "q's decision", pill: 'Waiting on q', cls: 'q'},
    {key: 'push', label: 'q to push', pill: 'Waiting on q to push', cls: 'q'},
    {key: 'ci', label: 'CI', pill: 'Waiting for CI', cls: 'ci'},
    {key: 'pr', label: 'PR review', pill: 'PR open', cls: 'pr'},
    {key: 'tests', label: 'Tests / repro', pill: 'Needs testing', cls: 'tests'},
    {key: 'claude', label: 'Claude working', pill: 'Claude is on it', cls: 'claude'},
    {key: 'nobody', label: 'Nobody yet', pill: 'Not started', cls: 'nobody'},
  ];
  const WAIT_BY = Object.fromEntries(WAIT.map(w => [w.key, w]));
  DM.WAIT = WAIT; DM.WAIT_BY = WAIT_BY;
  function isDone(e) { return DONE.has(e.status); }
  // Board keys for an entry (several possible), most blocking first.
  function waitKeys(e) {
    if (isDone(e)) return [];
    const w = new Set(Array.isArray(e.waitingOn) ? e.waitingOn : []);
    const out = [];
    for (const k of ['push', 'owner', 'ci', 'pr', 'tests']) if (w.has(k)) out.push(k);
    if (w.has('claude') || !out.length) out.push(e.status === 'in-progress' ? 'claude' : 'nobody');
    return out;
  }
  // The one pill an entry wears: {cls, label, key}
  function pill(e) {
    if (isDone(e)) {
      const label = e.status === 'told' ? 'Told q' : e.status === 'wontfix' ? "Won't fix" : statusWord(e.status);
      return {cls: e.status === 'wontfix' ? 'wontfix' : 'done', label, key: 'done'};
    }
    const k = waitKeys(e)[0];
    if (e.status === 'unknown' && k === 'nobody') return {cls: 'unknown', label: 'No status', key: 'nobody'};
    const w = WAIT_BY[k];
    return {cls: w.cls, label: w.pill, key: k};
  }
  function pillEl(e, small) {
    const p = pill(e);
    return h('span', {class: 'pill ' + p.cls + (small ? ' sm' : ''), title: e.waitingWhy || statusWord(e.status)}, p.label);
  }
  function needsQ(e) { return !isDone(e) && Array.isArray(e.waitingOn) && (e.waitingOn.includes('owner') || e.waitingOn.includes('push')); }
  const PRI_RANK = {P1: 1, P2: 2, P3: 3};

  // Plain-text cleanup of a markdown snippet (for SVG labels, tooltips).
  function plain(md) { return String(md || '').replace(/`([^`]*)`/g, '$1').replace(/\*\*|__/g, '').replace(/\[([^\]]*)\]\([^)]*\)/g, '$1').replace(/\s+/g, ' ').trim(); }
  function trimTitle(t) { return plain(t).replace(/[.:,;]+$/, ''); }
  function sentences(text) { return plain(text).split(/(?<=[.?!])\s+(?=[A-Z0-9(`"'*_])/).filter(Boolean); }

  // Sibling "GitHub manager" (local server) deep link for a chip, or null.
  const GHM = 'http://127.0.0.1:8767/';
  function ghManagerUrl(link) {
    if (!link) return null;
    const url = String(link.url || ''), label = String(link.label || '');
    let m;
    switch (link.kind) {
      case 'pr': m = /\/pull\/(\d+)/.exec(url) || /#(\d+)/.exec(label); return m ? GHM + '#pr=' + m[1] : null;
      case 'branch': { const b = decodeURIComponentSafe((/\/tree\/(.+)$/.exec(url) || [])[1] || label).replace(/^origin\//, ''); return b ? GHM + '#branch=' + encodeURIComponent(b) : null; }
      case 'run': m = /\/runs\/(\d+)/.exec(url) || /(\d{6,})/.exec(label); return m ? GHM + '#run=' + m[1] : null;
      case 'commit': m = /\/commit\/([0-9a-f]{7,40})/i.exec(url) || /([0-9a-f]{7,40})/i.exec(label); return m ? GHM + '#commit=' + m[1] : null;
      case 'version': case 'release': m = /(\d+\.\d+\.\d+)/.exec(label) || /(\d+\.\d+\.\d+)/.exec(url); return m ? GHM + '#version=' + m[1] : null;
    }
    return null;
  }
  // "claude/project-thread-iekl8z" -> "thread-iekl8z" for tight spaces (full name stays in the tooltip)
  function shortBranch(b) { return String(b || '').replace(/^origin\//, '').replace(/^claude\/project-/, ''); }
  function decodeURIComponentSafe(s) { try { return decodeURIComponent(s); } catch (_) { return s; } }

  // Link chip with state colour (+ a small GitHub-manager link).
  const STATE_WORDS = {open: 'open', merged: 'merged', closed: 'closed', success: 'CI passed', failure: 'CI failing', in_progress: 'CI running'};
  const KIND_WORDS = {pr: 'Pull request', commit: 'Commit', version: 'Version', run: 'CI run', release: 'Release', branch: 'Branch',
    thread: 'Thread', artifact: 'Claude page', url: 'Link', file: 'File'};
  function chip(link) {
    const st = link.state || '';
    const word = STATE_WORDS[st];
    const label = String(link.label || link.url || link.kind || '');
    const title = (KIND_WORDS[link.kind] || 'Link') + ': ' + label + (word ? ' (' + word + ')' : '') + (safeUrl(link.url) ? '\n' + link.url : '');
    const wrap = h('span', {class: 'lchip k-' + (link.kind || 'url') + (st ? ' s-' + st : '')});
    wrap.appendChild(ext(link.url, [h('i', {class: 'ldot'}), label, word && link.kind !== 'pr' ? h('small', null, ' ' + word) : null,
      link.kind === 'pr' && st ? h('small', null, ' ' + st) : null], 'lc-main', title));
    const g = ghManagerUrl(link);
    if (g) wrap.appendChild(h('a', {class: 'lc-gm', href: g, target: 'quark-github', rel: 'noopener', title: 'Open in the GitHub Manager'}, 'mgr'));
    return wrap;
  }

  // ---------- storage ----------
  const LS_KEY = 'quark.docsManager.v1';
  function loadPrefs() { try { return JSON.parse(localStorage.getItem(LS_KEY) || '{}') || {}; } catch (_) { return {}; } }
  function savePrefs(p) { try { localStorage.setItem(LS_KEY, JSON.stringify(p)); } catch (_) { /* private window etc. */ } }

  // ---------- floating tooltip ----------
  let tipEl = null;
  function tip(content, x, y) {
    if (!tipEl) { tipEl = h('div', {class: 'tip', role: 'tooltip'}); document.body.appendChild(tipEl); }
    if (content == null) { tipEl.hidden = true; return; }
    clear(tipEl); append(tipEl, [content]); tipEl.hidden = false;
    const r = tipEl.getBoundingClientRect();
    const vx = Math.min(x + 14, innerWidth - r.width - 8), vy = y + 16 + r.height > innerHeight - 4 ? y - r.height - 10 : y + 16;
    tipEl.style.left = Math.max(4, vx) + 'px'; tipEl.style.top = Math.max(4, vy) + 'px';
  }

  Object.assign(DM, {h, s, clear, $, $$, append, esc, safeUrl, safeHref, ext, dayKey, hhmm, dayLabel, full, dateOnly, rel, dayHeader,
    timeEl, tickTimes, fileTitle, fileShort, statusWord, isDone, waitKeys, pill, pillEl, needsQ, PRI_RANK, plain, trimTitle, sentences,
    chip, ghManagerUrl, shortBranch, loadPrefs, savePrefs, tip, toDate});
})(window.DM);
