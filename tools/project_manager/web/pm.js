/* Quark project manager page: renders GET /api/pm (see SPEC.md section 2/3).
   Vanilla JS, no build step, no external requests. Every value from the server is treated as untrusted text (it
   comes from git, GitHub and the Claude Manager): everything goes through esc(), links through linkAttrs(). Every field may
   be missing; a missing value renders as "unknown" and a section that still fails to draw shows its own error
   instead of breaking the page. */
(function () {
  'use strict';

  // ---------------------------------------------------------------- config
  const Q = new URLSearchParams(location.search);
  const EMBED = Q.get('embed') === '1';
  const FIXTURE = Q.get('fixture') === '1';
  // ?fixture=1&down=oracle,github marks those sources down in the sample (to see the grey cards).
  const SIM_DOWN = FIXTURE ? (Q.get('down') || '').split(',').map(s => s.trim()).filter(Boolean) : [];
  const TZ = 'America/Chicago';
  const POLL_MS = 15000;
  const ORACLE_MIN_MS = 120000;
  const REFRESH_MIN_MS = 10000;
  const UPDATE_MIN_MS = 60000;
  // after Refresh / Check now / Update now: poll this often until the new data is in (or the time is up)
  const SETTLE = { refresh: { every: 1500, max: 15000 }, oracle: { every: 3000, max: 55000 }, update: { every: 2000, max: 20000 } };
  const FULL_HISTORY_PAGE = 300;
  const FIXTURE_URL = 'static/fixtures/pm-sample.json';
  const DEFAULT_TAB_PORTS = { 8765: 'dashboard', 8766: 'docs', 8767: 'github', 8768: 'project' };
  const TAB_NAMES = { dashboard: 'Editor Manager', docs: 'Claude Manager', github: 'GitHub Manager', project: 'Supervisor Board' };

  const SOURCES = {
    github:    { name: 'GitHub Manager', label: 'GitHub Manager', every: 30, local: true, fix: 'Start the GitHub Manager (its tab in the Supervisor Board, or github-manager.cmd in Quark\\github-manager).' },
    docs:      { name: 'Claude Manager', label: 'Claude Manager', every: 30, local: true, fix: 'Start the Claude Manager (its tab in the Supervisor Board, or docs-manager.cmd in Quark\\docs-manager).' },
    editors:   { name: 'Editor Manager', label: 'Editor Manager', every: 30, local: true, fix: 'Start the Editor Manager (its tab in the Supervisor Board, or editor-dashboard.cmd in Quark\\editor-dashboard).' },
    builds:    { name: 'Local builds',        every: 60,  local: false, fix: 'Check that Quark\\production, testing and working exist and hold a BUILD.txt.' },
    repo:      { name: 'Repository',          every: 60,  local: false, fix: 'Check that Quark\\repository exists and git is on PATH.' },
    servers:   { name: 'Ranking Worker',      every: 120, local: false, fix: 'Check the internet connection (the server list comes from the ranking Worker over HTTPS).' },
    oracle:    { name: 'Oracle box',          every: 600, local: false, fix: 'Check the Oracle SSH settings in Quark\\project-manager\\config.json (oracle.host, user, key).' },
    checklist: { name: 'Release checklist',   every: 300, local: false, fix: 'Check that Quark\\repository\\docs\\RELEASE_CHECKLIST.md exists.' },
  };

  const BUCKETS = [
    { id: 'owner',  label: 'owner',  hint: 'waiting on you',          color: 'var(--s2)' },
    { id: 'push',   label: 'push',   hint: 'waiting for you to say push', color: 'var(--s4)' },
    { id: 'pr',     label: 'PR',     hint: 'waiting on a pull request', color: 'var(--s7)' },
    { id: 'ci',     label: 'CI',     hint: 'waiting on a CI run',     color: 'var(--s1)' },
    { id: 'tests',  label: 'tests',  hint: 'waiting on tests',        color: 'var(--s3)' },
    { id: 'claude', label: 'Claude', hint: 'Claude is working on it', color: 'var(--s5)' },
    { id: 'other',  label: 'other',  hint: 'waiting on something else', color: 'var(--idle)' },
  ];
  const LANES = [
    { id: 'release', label: 'Releases', color: 'var(--s1)', match: /version|release|tag|prerelease|sha|testing|production|push/ },
    { id: 'pr',      label: 'PRs',      color: 'var(--s7)', match: /pr|pull/ },
    { id: 'ci',      label: 'CI',       color: 'var(--s2)', match: /ci|run|build/ },
    { id: 'unit',    label: 'Oracle',   color: 'var(--s3)', match: /unit|oracle|server|bot/ },
    { id: 'entry',   label: 'Docs',     color: 'var(--s5)', match: /entry|doc|progress|question/ },
    { id: 'human',   label: 'Assets',   color: 'var(--s4)', match: /human|asset|provenance/ },
    { id: 'other',   label: 'Other',    color: 'var(--idle)', match: /.^/ },
  ];

  // ---------------------------------------------------------------- state
  const state = {
    pm: null, history: null, tabs: null,
    // nowAnchor: the server's clock (serverNow) at the last answer, fetchedPerf: performance.now() then
    genMs: 0, nowAnchor: 0, fetchedPerf: 0, lastOkMs: 0, error: null, historyAt: 0,
    sig: '', timer: 0, lastPollPerf: -1e9, refreshAt: -1e9, oracleClickAt: -1e9, updateClickAt: -1e15, updateMsg: '',
    showAllHistory: false, openDetails: new Set(), toast: '',
    shown: {},          // section id -> the html it shows, without its live ages (only a changed card is redrawn)
    settle: {},         // kind -> {until, done()}: re-poll soon after Refresh / Check now / Update now (SETTLE)
    haltKeys: null,     // the halting items seen last time (a new one brings up the notice)
    ckBusy: new Set(),  // checklist sections being saved
    ckMsg: '',
    full: null, fullLimit: FULL_HISTORY_PAGE, fullErr: '', // the full event history (dialog)
  };
  const charts = {};

  // ---------------------------------------------------------------- small helpers
  function esc(v) {
    return String(v == null ? '' : v).replace(/[&<>"'`]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;', '`': '&#96;' }[c]));
  }
  function isObj(v) { return v !== null && typeof v === 'object' && !Array.isArray(v); }
  function obj(v) { return isObj(v) ? v : {}; }
  function arr(v) { return Array.isArray(v) ? v : []; }
  function has(v) { return v !== null && v !== undefined && v !== ''; }
  function pick(o) { // pick(o, 'a', 'b.c', ...) -> first present value
    for (let i = 1; i < arguments.length; i++) {
      let cur = o;
      for (const k of String(arguments[i]).split('.')) { cur = isObj(cur) || Array.isArray(cur) ? cur[k] : undefined; if (cur === undefined) break; }
      if (has(cur)) return cur;
    }
    return undefined;
  }
  function num(v) { const n = typeof v === 'string' && v.trim() !== '' ? Number(v) : v; return typeof n === 'number' && isFinite(n) ? n : null; }
  function unk(v, word) { return has(v) && (typeof v !== 'object') ? esc(v) : '<span class="muted">' + esc(word || 'unknown') + '</span>'; }
  function short(sha) { return has(sha) ? String(sha).slice(0, 7) : ''; }
  function plural(n, one, many) { return n + ' ' + (n === 1 ? one : (many || one + 's')); }
  function nowMs() { return state.nowAnchor ? state.nowAnchor + (performance.now() - state.fetchedPerf) : Date.now(); }
  function parseTime(v) {
    if (!has(v)) return null;
    if (typeof v === 'number') return v > 1e12 ? v : v * 1000;
    const t = Date.parse(String(v));
    return isNaN(t) ? null : t;
  }

  // Dallas time formatting ----------------------------------------------------
  const fmtCache = {};
  function fmt(opts) {
    const k = JSON.stringify(opts);
    if (!fmtCache[k]) {
      try { fmtCache[k] = new Intl.DateTimeFormat('en-US', Object.assign({ timeZone: TZ }, opts)); }
      catch (e) { fmtCache[k] = new Intl.DateTimeFormat('en-US', opts); }
    }
    return fmtCache[k];
  }
  function dayKey(ms) { return fmt({ year: 'numeric', month: '2-digit', day: '2-digit' }).format(ms); }
  function fmtWhen(ms) {
    if (ms == null) return 'unknown';
    const n = nowMs();
    const time = fmt({ hour: 'numeric', minute: '2-digit' }).format(ms);
    if (dayKey(ms) === dayKey(n)) return time;
    if (dayKey(ms) === dayKey(n - 86400000)) return 'yesterday ' + time;
    const sameYear = fmt({ year: 'numeric' }).format(ms) === fmt({ year: 'numeric' }).format(n);
    return fmt(sameYear ? { month: 'short', day: 'numeric' } : { month: 'short', day: 'numeric', year: 'numeric' }).format(ms) + ', ' + time;
  }
  function fmtFull(ms) {
    if (ms == null) return 'unknown';
    return fmt({ weekday: 'short', month: 'short', day: 'numeric', year: 'numeric', hour: 'numeric', minute: '2-digit', second: '2-digit', timeZoneName: 'short' }).format(ms) + ' (Dallas)';
  }
  function fmtAge(ms) {
    if (ms == null) return 'unknown';
    let d = nowMs() - ms;
    const fut = d < 0; d = Math.abs(d);
    const s = Math.round(d / 1000);
    let t;
    if (s < 10) return fut ? 'in a moment' : 'just now';
    if (s < 60) t = s + ' s';
    else if (s < 3600) t = Math.round(s / 60) + ' min';
    else if (s < 86400 * 2) t = Math.round(s / 3600) + ' h';
    else if (s < 86400 * 60) t = Math.round(s / 86400) + ' days';
    else t = Math.round(s / (86400 * 30)) + ' months';
    return fut ? 'in ' + t : t + ' ago';
  }
  function fmtDur(ms) {
    if (ms == null || ms < 0) return '0:00';
    const s = Math.floor(ms / 1000), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60;
    return (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(r).padStart(2, '0');
  }
  // live age / elapsed spans: drawn empty and filled by the 1 s ticker, so a card's html does not change as time passes
  // (renderAll compares it without these attributes and redraws only cards whose data changed)
  function ageSpan(ms, prefix) {
    if (ms == null) return '<span class="muted">unknown</span>';
    return '<span data-age="' + ms + '"' + (prefix ? ' data-prefix="' + esc(prefix) + '"' : '') + '></span>';
  }
  function whenSpan(ms) {
    if (ms == null) return '<span class="muted">unknown</span>';
    return '<span title="' + esc(fmtFull(ms)) + '">' + esc(fmtWhen(ms)) + '</span>';
  }

  // links -------------------------------------------------------------------
  function tabList() { return arr(state.tabs && (Array.isArray(state.tabs) ? state.tabs : state.tabs.tabs)).filter(isObj); }
  function tabIdForPort(port) {
    for (const t of tabList()) {
      const p = num(t.port) || (function () { try { return num(new URL(t.url).port); } catch (e) { return null; } })();
      if (p === port && t.id) return String(t.id);
    }
    return DEFAULT_TAB_PORTS[port] || null;
  }
  function isLocalHost(h) { return h === '127.0.0.1' || h === 'localhost'; }
  // -> attribute string for <a>, or null when the URL is not something we open
  function linkAttrs(url) {
    if (!has(url) || typeof url !== 'string') return null;
    let u;
    try { u = new URL(url, location.href); } catch (e) { return null; }
    if (u.protocol === 'https:') return 'href="' + esc(u.href) + '" target="_blank" rel="noopener noreferrer"';
    if (u.protocol === 'http:') {
      const port = num(u.port);
      if (isLocalHost(u.hostname) && port) {
        const tab = tabIdForPort(port);
        if (tab) {
          const clean = new URL(u.href); clean.searchParams.delete('embed');
          return 'href="' + esc(clean.href) + '" data-tab="' + esc(tab) + '" data-hash="' + esc(u.hash) + '"' +
            (u.origin === location.origin ? '' : ' target="_blank" rel="noopener"');
        }
      }
      return 'href="' + esc(u.href) + '" target="_blank" rel="noopener noreferrer"';
    }
    return null;
  }
  function tabHref(tabId, hash) {
    let base = null;
    for (const t of tabList()) if (t.id === tabId && typeof t.url === 'string' && /^https?:/.test(t.url)) base = t.url;
    if (!base) {
      const port = Object.keys(DEFAULT_TAB_PORTS).find(p => DEFAULT_TAB_PORTS[p] === tabId);
      if (!port) return null;
      base = 'http://127.0.0.1:' + port + '/';
    }
    try { const u = new URL(base); u.search = ''; u.hash = hash || ''; return u.href; } catch (e) { return null; }
  }
  function link(url, html, cls) {
    const a = linkAttrs(url);
    return a ? '<a ' + a + (cls ? ' class="' + cls + '"' : '') + '>' + html + '</a>' : html;
  }
  function tabLink(tabId, hash, html, cls) { return link(tabHref(tabId, hash), html, cls); }
  function linkLabel(url) {
    const a = linkAttrs(url);
    if (!a) return '';
    try {
      const u = new URL(url, location.href);
      if (u.protocol === 'http:' && isLocalHost(u.hostname)) { const t = tabIdForPort(num(u.port)); return TAB_NAMES[t] || t || 'Open'; }
      if (/github\.com$/.test(u.hostname)) return 'GitHub';
      if (/claude\.ai$/.test(u.hostname)) return 'Thread';
      return u.hostname;
    } catch (e) { return 'Open'; }
  }
  function ext(url) {
    const a = linkAttrs(url);
    return a && /^https:/.test(url) ? ' <a ' + a + ' class="ext" title="Open on the web">&#8599;</a>' : '';
  }

  // inline markdown-ish text: escape first, then **bold** and `code`
  function mdInline(s) {
    return esc(s).replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>').replace(/&#96;([^&]+?)&#96;/g, '<code>$1</code>');
  }

  // icons -------------------------------------------------------------------
  const IC = {
    crit: '<svg class="ic" viewBox="0 0 20 20" aria-hidden="true"><path d="M7 2h6l5 5v6l-5 5H7l-5-5V7z" fill="currentColor"/><path d="M10 6v5" stroke="#fff" stroke-width="2" stroke-linecap="round"/><circle cx="10" cy="14" r="1.2" fill="#fff"/></svg>',
    warn: '<svg class="ic" viewBox="0 0 20 20" aria-hidden="true"><path d="M10 2.5 18.5 17h-17z" fill="currentColor"/><path d="M10 8v4.5" stroke="var(--surface)" stroke-width="2" stroke-linecap="round"/><circle cx="10" cy="14.8" r="1.1" fill="var(--surface)"/></svg>',
    step: '<svg class="ic" viewBox="0 0 20 20" aria-hidden="true"><circle cx="10" cy="10" r="8" fill="none" stroke="currentColor" stroke-width="2"/><path d="M8 6.5 11.5 10 8 13.5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    ok: '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2.5 6.2 5 8.5 9.5 3.5" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    bad: '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M3 3l6 6M9 3 3 9" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>',
    run: '<svg viewBox="0 0 12 12" aria-hidden="true"><circle cx="6" cy="6" r="4" fill="none" stroke="currentColor" stroke-opacity=".3" stroke-width="1.8"/><path class="spin" d="M6 2a4 4 0 0 1 4 4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>',
    idle: '<svg viewBox="0 0 12 12" aria-hidden="true"><circle cx="6" cy="6" r="3" fill="currentColor"/></svg>',
    ahead: '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M6 10V2.5M2.8 5.5 6 2.3l3.2 3.2" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    wait: '<svg viewBox="0 0 12 12" aria-hidden="true"><circle cx="6" cy="6" r="4.2" fill="none" stroke="currentColor" stroke-width="1.6"/><path d="M6 3.6V6l1.6 1.2" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg>',
    halt: '<svg class="ic" viewBox="0 0 20 20" aria-hidden="true"><path d="M7 2h6l5 5v6l-5 5H7l-5-5V7z" fill="currentColor"/><rect x="6.5" y="8.6" width="7" height="2.8" rx="0.6" fill="var(--halt-bg)"/></svg>',
    down: '<svg viewBox="0 0 20 20" aria-hidden="true"><circle cx="10" cy="10" r="8" fill="none" stroke="currentColor" stroke-width="1.8"/><path d="M6.5 10h7" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>',
  };
  function pill(kind, text, title) {
    const icon = { good: IC.ok, crit: IC.bad, run: IC.run, idle: IC.idle, warn: IC.wait, ahead: IC.ahead }[kind] || '';
    return '<span class="pill p-' + kind + '"' + (title ? ' title="' + esc(title) + '"' : '') + '>' + icon + esc(text) + '</span>';
  }
  function bucketPill(b, extra) {
    return '<span class="pill bucket" style="--bc:' + b.color + '"><span class="pd"></span>' + esc(b.label) + (extra != null ? ' <span class="muted">' + esc(extra) + '</span>' : '') + '</span>';
  }

  // sources -----------------------------------------------------------------
  function src(key) {
    const s = obj(obj(state.pm && state.pm.sources)[key]);
    if (SIM_DOWN.indexOf(key) >= 0) return { ok: false, fetchedAt: s.fetchedAt, error: key === 'oracle' ? 'SSH key not found' : SOURCES[key] && SOURCES[key].local ? SOURCES[key].name + ' not running' : 'simulated failure' };
    return s;
  }
  function srcOk(key) { return src(key).ok === true; }
  function data(key) { const s = src(key); return s.ok === true ? obj(s.data) : {}; }
  function derived() { return obj(state.pm && state.pm.derived); }
  function srcMeta(keys, label) {
    const parts = [];
    for (const key of keys) {
      const s = src(key), info = SOURCES[key] || { name: key, every: 60 };
      const t = parseTime(s.fetchedAt);
      let cls = 'dot', note = '';
      if (s.ok !== true) { cls += ' dot-down'; note = ' (down)'; }
      else if (t != null && nowMs() - t > info.every * 3000 + 30000) { cls += ' dot-stale'; note = ' (stale)'; }
      parts.push('<span class="' + cls + '"></span><b>' + esc(label && keys.length === 1 ? label : (info.label || key)) + '</b>' + esc(note) + ' ' + (t != null ? ageSpan(t) : '<span class="muted">never</span>'));
    }
    return '<span class="src" title="Where this card\'s data comes from, and how old it is">' + parts.join('<span class="muted">&middot;</span>') + '</span>';
  }
  function fixFor(key, err) {
    const e = String(err || '');
    if (/ssh key not found/i.test(e)) return 'Put the Oracle SSH key where Quark\\project-manager\\config.json says (oracle.key), or fix that path.';
    if (/ssh(\.exe)? not found|no ssh/i.test(e)) return 'Install the Windows OpenSSH client (Settings > Apps > Optional features).';
    return (SOURCES[key] && SOURCES[key].fix) || 'Check the PM server log.';
  }
  function downBlock(key) {
    const s = src(key), info = SOURCES[key] || { name: key, local: false };
    const title = info.local ? info.name + ' not running' : info.name + ' unavailable';
    const t = parseTime(s.fetchedAt);
    return '<div class="down">' + IC.down + '<div><strong>' + esc(title) + '</strong><div class="fix">' + esc(fixFor(key, s.error)) + '</div>' +
      (has(s.error) ? '<div class="err">' + esc(s.error) + (t != null ? ' &middot; last good data ' + ageSpan(t) : '') + '</div>' : '') + '</div></div>';
  }
  function head(id, title, extra, meta) {
    return '<div class="card-head"><h2 id="' + id + '">' + esc(title) + (extra || '') + '</h2>' + (meta || '') + '</div>';
  }
  function count(n) { return ' <span class="count">' + esc(n) + '</span>'; }

  // ---------------------------------------------------------------- 1. pipeline
  function stageTime(stage, item) {
    const t = parseTime(pick(item, 'time', 'at', 'date'));
    if (t != null) return t;
    const g = data('github'), b = buildFolders();
    if (stage === 'testing') return parseTime(pick(g, 'testing.prerelease.publishedAt', 'testing.committedAt', 'testing.date'));
    if (stage === 'production') return parseTime(pick(g, 'production.publishedAt', 'production.date'));
    if (stage === 'working') { const w = b.find(f => f.name === 'working'); return w ? parseTime(w.mtime) : null; }
    return null;
  }
  function stageFallback(stage) {
    const v = obj(derived().versions), g = data('github');
    if (stage === 'working') { const w = buildFolders().find(f => f.name === 'working') || {}; return { stage, version: v.working || w.version, sha: w.commit, state: 'unknown' }; }
    if (stage === 'testing') return { stage, version: v.testing || pick(g, 'testing.version'), sha: pick(g, 'testing.sha'), state: 'unknown' };
    return { stage, version: v.production || pick(g, 'production.version', 'production.tag'), sha: pick(g, 'production.sha'), state: 'unknown' };
  }
  // derived.* holds the server's normalized copies (prs, runs, localCopies, programs); the raw source data is the fallback
  function openPrs() { const d = derived(); if (Array.isArray(d.prs) && srcOk('github')) return d.prs.filter(isObj); const g = data('github'); return arr(pick(g, 'prs', 'pulls', 'openPrs', 'openPRs', 'pullRequests')).filter(isObj); }
  // a PR's base branch; an older GitHub Manager summary has none: a PR from testing goes to the default branch
  // (production), any other goes into testing (the only branch work is merged into)
  function prBase(pr) { return has(pr.base) ? String(pr.base) : String(pr.head || '') === 'testing' ? null : 'testing'; }
  function intoTesting(pr) { return prBase(pr) === 'testing'; }
  // (derived.localCopies merges the GitHub Manager's view with this board's own git reads: shown even when it is down)
  function locals() { const d = derived(); if (Array.isArray(d.localCopies)) return d.localCopies.filter(isObj); const g = data('github'); return arr(pick(g, 'locals', 'localCopies', 'local')).filter(isObj); }
  function workingCopy() { return locals().find(l => /working/i.test(String(l.name || ''))) || null; }

  function renderPipeline() {
    const d = derived(), v = obj(d.versions), pipe = arr(d.pipeline);
    const stages = ['working', 'testing', 'production'].map(st => {
      const item = pipe.find(p => isObj(p) && p.stage === st);
      return Object.assign({}, stageFallback(st), obj(item), { stage: st });
    });
    const stateText = { ok: ['good', 'in step'], ahead: ['ahead', 'ahead'], behind: ['warn', 'behind'], unknown: ['idle', 'unknown'] };
    const stageHtml = s => {
      const stt = stateText[s.state] ? s.state : 'unknown';
      const t = stageTime(s.stage, s);
      const sha = short(s.sha);
      const shaHtml = sha ? (s.stage === 'testing' ? tabLink('github', '#branch=testing', '<span class="mono">' + esc(sha) + '</span>') :
        s.stage === 'working' ? tabLink('github', '#local=working-repo', '<span class="mono">' + esc(sha) + '</span>') : '<span class="mono">' + esc(sha) + '</span>') : '<span class="muted">sha unknown</span>';
      return '<div class="stage ' + stt + '"><div class="stage-name">' + esc(s.stage) + pill(stateText[stt][0], stateText[stt][1]) + '</div>' +
        '<div class="stage-ver">' + unk(s.version) + '</div>' +
        '<div class="stage-meta">' + shaHtml + '<span>' + ageSpan(t) + '</span></div>' +
        (has(s.note) ? '<div class="stage-note">' + esc(s.note) + '</div>' : '') + '</div>';
    };
    // what is pending between stages
    const wt = [], tp = [];
    const w = workingCopy();
    const wItem = stages[0];
    if (has(wItem.pending)) wt.push(esc(wItem.pending));
    if (w) {
      const ah = num(w.ahead);
      if (ah) wt.push(tabLink('github', '#local=' + encodeURIComponent(w.name), esc(plural(ah, 'commit')) + ' to push'));
      const df = num(pick(w, 'dirtyFiles', 'dirtyCount', 'uncommitted'));
      if (w.dirty || df) wt.push(tabLink('github', '#local=' + encodeURIComponent(w.name), df ? esc(plural(df, 'file')) + ' uncommitted' : 'uncommitted changes'));
    }
    const tItem = stages[1], pItem = stages[2];
    if (has(tItem.pending)) tp.push(esc(tItem.pending));
    for (const pr of openPrs()) {
      if (num(pr.number) == null) continue;
      // PRs into testing belong before testing, the testing -> default branch PR before production
      if (intoTesting(pr)) wt.push(tabLink('github', '#pr=' + num(pr.number), 'PR #' + esc(num(pr.number)) + ' into testing'));
      else tp.push(tabLink('github', '#pr=' + num(pr.number), 'PR #' + esc(num(pr.number)) + ' open'));
    }
    const tv = tItem.version, pv = pItem.version;
    const ckp = checklistProgress();
    if (has(tv) && has(pv) && String(tv) !== String(pv)) {
      tp.push('<a href="#checklist" data-open="checklist">hand-test checklist</a>' + (ckp.total ? ' <span class="ck-frac' + (ckp.done === ckp.total ? ' all' : '') + '">' + ckp.done + '/' + ckp.total + '</span>' : ''));
    }
    const arrows = arr(d.arrows).filter(isObj);
    const serverArrow = (from, list) => {
      const a = arrows.find(x => x.from === from);
      if (list.length || !a || !has(a.label)) return '';
      if (/^(up to date|same version|in step)$/i.test(String(a.label))) return String(a.label);
      list.push(linkAttrs(a.link) ? link(a.link, esc(a.label)) : esc(a.label));
      return '';
    };
    const wtNone = serverArrow('working', wt), tpNone = serverArrow('testing', tp);
    const arrow = (labels, none) => '<div class="connector' + (labels.length ? ' pending' : '') + '">' +
      '<div class="arrow-row"><svg class="arr-h" viewBox="0 0 100 18" preserveAspectRatio="none" aria-hidden="true"><line x1="2" y1="9" x2="97" y2="9" stroke="currentColor" stroke-width="2" vector-effect="non-scaling-stroke"' + (labels.length ? '' : ' stroke-dasharray="4 4"') + '/></svg>' +
      '<svg class="arr-head" viewBox="0 0 12 18" aria-hidden="true"><path d="M2 3 10 9 2 15" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/></svg></div>' +
      '<svg class="arr-v" viewBox="0 0 18 34" aria-hidden="true"><line x1="9" y1="2" x2="9" y2="30" stroke="currentColor" stroke-width="2"' + (labels.length ? '' : ' stroke-dasharray="4 4"') + '/><path d="M3 24 9 31 15 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/></svg>' +
      '<div class="conn-labels">' + (labels.length ? labels.map(l => '<span class="conn-label">' + l + '</span>').join('') : '<span class="conn-label none">' + esc(none || 'nothing pending') + '</span>') + '</div></div>';

    // footer: prerelease, nextFree, checklist
    const g = data('github');
    const preTag = v.testingPrerelease || pick(g, 'testing.prerelease.tag');
    const preUrl = pick(g, 'testing.prerelease.url');
    const prodTag = pick(g, 'production.tag') || v.productionTag;
    const ck = checklistItems();
    const ckHtml = srcOk('checklist') ? renderChecklist(ck, ckp) : downBlock('checklist');
    const html = head('h-pipeline', 'Release pipeline', '', srcMeta(['github', 'builds'])) +
      '<div class="pipeline">' + stageHtml(stages[0]) + arrow(wt, wtNone) + stageHtml(stages[1]) + arrow(tp, tpNone) + stageHtml(stages[2]) + '</div>' +
      '<div class="pipe-foot">' +
      '<span class="kv">Testing prerelease <b>' + (has(preTag) ? link(preUrl, esc(preTag)) : '<span class="muted">unknown</span>') + '</b></span>' +
      '<span class="kv">Production tag <b>' + unk(prodTag) + '</b></span>' +
      '<span class="kv" title="Testing VERSION with the patch +1. Informational: the coordinator assigns versions.">Next free version <b>' + unk(v.nextFree) + '</b></span>' +
      '<details id="checklist" data-key="checklist"><summary>Before production' + (ck.length ? ' (' + (ckp.version ? ckp.done + ' of ' + ck.length + ' tested for ' + esc(ckp.version) : ck.length + ' sections') + ')' : '') + '</summary>' + ckHtml + '</details>' +
      '</div>';
    return html;
  }
  // q's ticks for the testing version (POST /api/checklist; derived.checklist). Keyed by heading without its number.
  function sectionKey(h) { return String(h || '').replace(/^\s*\d+[.)]\s*/, '').trim().slice(0, 200); }
  function checklistProgress() {
    const c = obj(derived().checklist), done = obj(c.done), ck = checklistItems();
    const version = has(c.version) ? String(c.version) : (has(obj(derived().versions).testing) ? String(derived().versions.testing) : null);
    return { version, done: ck.filter(h => done[sectionKey(h)]).length, total: ck.length, ticks: done };
  }
  function renderChecklist(ck, p) {
    if (!ck.length) return '<p class="empty">The checklist has no sections.</p>';
    const url = pick(src('checklist').data, 'url');
    const items = ck.map(h => {
      const k = sectionKey(h), t = obj(p.ticks[k]), on = !!p.ticks[k], at = parseTime(t.at);
      return '<li><label' + (at != null ? ' title="' + esc('ticked ' + fmtFull(at) + (has(t.build) ? ' on ' + t.build : '')) + '"' : '') + '>' +
        '<input type="checkbox" data-ck="' + esc(k) + '"' + (on ? ' checked' : '') + (p.version ? '' : ' disabled') + '> ' +
        '<span>' + esc(String(h).replace(/^\d+\.\s*/, '')) + '</span></label></li>';
    }).join('');
    return '<div class="ck-head small muted">' + (p.version ? 'Hand-test of <b>' + esc(p.version) + '</b>: tick a section when it passed (saved on this PC, per version).' : 'Testing version unknown: ticks are saved per version.') +
      (linkAttrs(url) ? ' ' + link(url, 'RELEASE_CHECKLIST.md') + ' &#8599;' : '') + '</div>' +
      (state.ckMsg ? '<div class="ck-msg small">' + esc(state.ckMsg) + '</div>' : '') +
      '<div class="ck-bar" role="progressbar" aria-valuemin="0" aria-valuemax="' + p.total + '" aria-valuenow="' + p.done + '"><i style="width:' + (p.total ? Math.round(p.done / p.total * 100) : 0) + '%"></i></div>' +
      '<ol class="checklist">' + items + '</ol>';
  }
  function checklistItems() {
    const c = src('checklist').data;
    if (Array.isArray(c)) return c.filter(x => typeof x === 'string' || isObj(x)).map(x => typeof x === 'string' ? x : (x.title || x.heading || ''));
    return arr(pick(obj(c), 'headings', 'sections', 'items')).map(x => typeof x === 'string' ? x : (obj(x).title || obj(x).heading || ''));
  }

  // ---------------------------------------------------------------- 0. halting (q must act; a Claude agent is blocked)
  const HALT_SOURCES = {
    docs: 'Claude Manager', oracle: 'Oracle box (this board)', updates: 'Tool updates (this board)',
    repo: 'Repository (this board)', github: 'GitHub Manager', editors: 'Editor Manager',
  };
  const HALT_GOTO = { oracle: ['#sec-servers', 'Servers & bot'], updates: ['#sec-tools', 'Tools & updates'] };
  function haltingItems() { return arr(derived().halting).filter(isObj); }
  function haltKey(h) { return String(has(h.id) ? h.id : '') + '|' + String(h.source || '') + '|' + String(h.what || h.title || ''); }
  // the `code` spans of the entry text (a command, a setting, a path): each gets a Copy button
  function haltCodes(h) {
    const out = [], seen = new Set();
    const re = /`([^`\n]{2,200})`/g;
    let m;
    while ((m = re.exec(String(h.body || ''))) && out.length < 4) { const c = m[1].trim(); if (c && !seen.has(c)) { seen.add(c); out.push(c); } }
    return out;
  }
  function copyBtn(text, label) {
    return '<button type="button" class="btn small copy" data-copy="' + esc(text) + '" title="' + esc('Copy: ' + text) + '">' + esc(label || 'Copy') + '</button>';
  }
  function renderHalting() {
    const items = haltingItems();
    const sec = document.getElementById('sec-halting');
    if (sec.hidden !== (items.length === 0)) sec.hidden = items.length === 0;
    if (!items.length) return '';
    const li = items.map(h => {
      const t = parseTime(h.since);
      const go = [];
      if (linkAttrs(h.link)) go.push(link(h.link, (/^http:\/\/(127\.0\.0\.1|localhost):8766\//.test(String(h.link)) ? 'Open entry' : esc(linkLabel(h.link) || 'Open')) + ' &rarr;'));
      if (linkAttrs(h.threadUrl) && h.threadUrl !== h.link) go.push(link(h.threadUrl, esc(has(h.thread) ? h.thread : 'Thread') + ' &#8599;'));
      if (!go.length && HALT_GOTO[h.source]) go.push('<a href="' + HALT_GOTO[h.source][0] + '">' + esc(HALT_GOTO[h.source][1]) + ' &darr;</a>');
      const what = has(h.what) ? h.what : (has(h.title) ? h.title : 'Something needs you');
      go.push(copyBtn(what, 'Copy'));
      const codes = haltCodes(h);
      const blocks = has(h.blocks) ? h.blocks : (has(h.thread) ? h.thread : null);
      const meta = [
        '<span class="h-src">' + esc(HALT_SOURCES[h.source] || h.source || 'unknown source') + '</span>',
        t != null ? '<span>since ' + whenSpan(t) + ' Dallas &middot; ' + ageSpan(t) + '</span>' : '<span>since unknown</span>',
      ];
      if (has(h.title) && h.title !== what) meta.push('<span>' + esc(h.title) + '</span>');
      if (has(h.detail)) meta.push('<span class="h-detail">' + esc(h.detail) + '</span>');
      return '<li class="halt"><div class="h-what">' + esc(what) + '</div><div class="h-go">' + go.join('') + '</div>' +
        (codes.length ? '<div class="h-codes">' + codes.map(c => '<span class="h-code"><code>' + esc(c) + '</code>' + copyBtn(c) + '</span>').join('') + '</div>' : '') +
        (blocks ? '<div class="h-blocks">Blocks <b>' + esc(blocks) + '</b></div>' : '') +
        '<div class="h-meta">' + meta.join('') + '</div></li>';
    }).join('');
    return head('h-halting', 'HALTING \u2014 Claude is blocked on you', count(items.length)).replace('<h2 id="h-halting">', '<h2 id="h-halting">' + IC.halt) +
      '<ol class="halts">' + li + '</ol>';
  }
  // the count in the page title and for the Quark Project shell (red badge on this tab and the taskbar)
  let lastHaltBadge = null;
  function publishHalting() {
    const items = haltingItems(), n = items.length;
    window.quarkHalting = n;
    document.title = (n ? '(' + n + ') ' : '') + 'Supervisor Board';
    // a HALT that was not there at the last poll (never on the first load: those are already on the badge)
    const keys = new Set(items.map(haltKey));
    if (state.haltKeys) {
      const fresh = items.filter(h => !state.haltKeys.has(haltKey(h)));
      if (fresh.length) haltNotice(fresh);
    }
    state.haltKeys = keys;
    if (!n) { const t = document.getElementById('halt-toast'); if (t) t.hidden = true; }
    if (n === lastHaltBadge) return;
    lastHaltBadge = n;
    const sh = window.quarkShell;
    if (sh && typeof sh.setBadge === 'function') { try { sh.setBadge(n); } catch (err) { /* older shell */ } }
  }
  // the notice: a red toast at the top (stays until dismissed or the item is gone) and a short chime (Mute on the
  // toast, remembered in this browser)
  function chimeMuted() { try { return localStorage.getItem('pm.haltChime') === 'off'; } catch (e) { return false; } }
  function haltNotice(fresh) {
    const el = document.getElementById('halt-toast');
    if (!el) return;
    const h = fresh[0];
    el.innerHTML = IC.halt + '<div class="ht-txt"><b>' + esc(fresh.length > 1 ? fresh.length + ' new HALT items' : 'New HALT') + '</b> ' +
      esc(has(h.what) ? h.what : (h.title || 'Claude is blocked on you')) + '</div>' +
      '<a href="#sec-halting" class="ht-go" data-dismiss="1">Show</a>' +
      '<button type="button" class="btn small ghost ht-mute" id="halt-mute">' + (chimeMuted() ? 'Unmute' : 'Mute') + '</button>' +
      '<button type="button" class="btn small ghost ht-x" data-dismiss="1" aria-label="Dismiss">&times;</button>';
    el.hidden = false;
    if (!chimeMuted()) chime();
  }
  let audio = null;
  function chime() {
    try {
      const AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) return;
      audio = audio || new AC();
      if (audio.state === 'suspended') audio.resume();
      const t0 = audio.currentTime + 0.02;
      [[880, 0], [660, 0.16]].forEach(([f, dt]) => {
        const o = audio.createOscillator(), g = audio.createGain();
        o.type = 'sine'; o.frequency.value = f;
        g.gain.setValueAtTime(0.0001, t0 + dt);
        g.gain.exponentialRampToValueAtTime(0.18, t0 + dt + 0.02);
        g.gain.exponentialRampToValueAtTime(0.0001, t0 + dt + 0.3);
        o.connect(g).connect(audio.destination);
        o.start(t0 + dt); o.stop(t0 + dt + 0.32);
      });
    } catch (e) { /* no audio here (autoplay policy, no device): the toast is the notice */ }
  }

  // ---------------------------------------------------------------- 2. needs you
  function renderNeeds() {
    const d = derived();
    const att = arr(d.attention).filter(isObj);
    const steps = arr(d.nextSteps).filter(s => isObj(s) && String(s.owner || '').toLowerCase() === 'q');
    const qs = arr(d.questions).filter(isObj);
    const recFor = s => {
      if (has(s.recommendation)) return s.recommendation;
      const q = qs.find(x => (has(x.link) && x.link === s.link) || (has(x.text) && String(s.text || '').indexOf(String(x.text)) >= 0));
      return q && has(q.recommendation) ? q.recommendation : null;
    };
    const items = [];
    att.filter(a => a.level === 'red').forEach(a => items.push({ cls: 'red', icon: IC.crit, a }));
    att.filter(a => a.level !== 'red').forEach(a => items.push({ cls: 'amber', icon: IC.warn, a }));
    steps.forEach(a => items.push({ cls: 'step', icon: IC.step, a }));
    const sec = document.getElementById('sec-needs');
    sec.classList.toggle('calm', items.length === 0);
    const meta = '<span class="src" title="Worked out by the PM server from every source"><span class="dot' + (state.pm ? '' : ' dot-down') + '"></span><b>derived</b> ' + (state.genMs ? ageSpan(state.genMs) : '<span class="muted">never</span>') + '</span>';
    if (!items.length) return head('h-needs', 'Needs you', count(0), meta) + '<p class="empty">Nothing needs you right now.</p>';
    const li = items.map(({ cls, icon, a }) => {
      const why = has(a.why) ? a.why : (a.source && SOURCES[a.source] ? 'from ' + SOURCES[a.source].name : (cls === 'step' ? '' : (a.level === 'red' ? 'needs fixing' : 'worth a look')));
      const lab = linkLabel(a.link);
      const pr = obj(a.progress), pt = num(pr.total), pd = num(pr.done);
      return '<li class="need ' + cls + '">' + icon + '<span class="txt">' + unk(a.text, 'no text') + '</span>' +
        (pt ? '<a href="#checklist" data-open="checklist" class="go">' + esc(pd + ' of ' + pt) + ' tested &rarr;</a>' : lab ? link(a.link, esc(lab) + ' &rarr;', 'go') : '<span></span>') +
        (why ? '<span class="why">' + esc(why) + '</span>' : '') +
        (has(a.fix) ? '<span class="why"><b>Fix:</b> ' + esc(a.fix) + '</span>' : '') +
        (cls === 'step' && recFor(a) != null ? '<span class="rec"><b>Recommended:</b> ' + esc(recFor(a)) + '</span>' : '') + '</li>';
    }).join('');
    return head('h-needs', 'Needs you', count(items.length), meta) + '<ul class="needs">' + li + '</ul>';
  }

  // ---------------------------------------------------------------- 3. in progress
  function bucketOf(e) {
    if (BUCKETS.some(b => b.id === e.bucket)) return e.bucket;
    const w = String(e.waitingOn || e.bucket || '').toLowerCase();
    if (/push/.test(w)) return 'push';
    if (/owner|\byou\b|^q$|\bq\b|user/.test(w)) return 'owner';
    if (/\bprs?\b|pull/.test(w)) return 'pr';
    if (/\bci\b|\brun\b|workflow|build/.test(w)) return 'ci';
    if (/test/.test(w)) return 'tests';
    if (/claude|agent|worker/.test(w)) return 'claude';
    return 'other';
  }
  function renderProgress() {
    const d = derived();
    const entries = arr(d.inProgress).filter(isObj);
    const meta = srcMeta(['docs']);
    let html = head('h-progress', 'In progress', count(entries.length), meta);
    if (!srcOk('docs')) html += downBlock('docs');
    if (!entries.length) return html + '<p class="empty">No in-progress entries.</p>';
    const groups = {};
    for (const e of entries) (groups[bucketOf(e)] = groups[bucketOf(e)] || []).push(e);
    const shown = BUCKETS.filter(b => b.id !== 'other' || groups.other);
    charts.buckets = shown.map(b => ({ label: b.label, color: b.color, value: (groups[b.id] || []).length, hint: b.hint }));
    html += '<div class="bucket-chart" data-chart="buckets" role="img" aria-label="' + esc('Entries per bucket: ' + charts.buckets.map(c => c.label + ' ' + c.value).join(', ')) + '"></div>';
    for (const b of BUCKETS) {
      const g = groups[b.id];
      if (!g) continue;
      html += '<div class="group-head">' + bucketPill(b, g.length) + '<span class="why">' + esc(b.hint) + '</span></div><ul class="rows">';
      for (const e of g) {
        const bits = [];
        if (e.halting === true) bits.push(pill('crit', 'halting', 'q must act: shown at the top of the page'));
        if (has(e.status)) bits.push(esc(e.status));
        if (has(e.where)) bits.push('<span class="mono">' + esc(e.where) + '</span>');
        if (has(e.waitingWhy)) bits.push(esc(e.waitingWhy));
        if (b.id === 'other' && has(e.waitingOn)) bits.unshift('waiting on ' + esc(e.waitingOn));
        const wl = arr(e.waitingOnList).filter(x => typeof x === 'string' && x);
        if (wl.length > 1) bits.push('waiting on ' + wl.map(esc).join(', '));
        const tl = linkAttrs(e.threadUrl) ? link(e.threadUrl, esc(has(e.thread) ? e.thread : 'thread') + ' &#8599;') : (has(e.thread) ? '<span class="muted">' + esc(e.thread) + '</span>' : '');
        const entry = linkAttrs(e.entryLink) ? e.entryLink : (has(e.id) ? tabHref('docs', '#entry=' + encodeURIComponent(e.id)) : null);
        html += '<li class="row"><span class="t">' + (entry ? link(entry, unk(e.title, 'untitled'), 'entry') : unk(e.title, 'untitled')) + '</span><span class="r">' + tl + '</span>' +
          '<span class="m">' + (bits.length ? bits.join('<span class="sep">&middot;</span>') : '<span class="muted">no details</span>') + '</span></li>';
      }
      html += '</ul>';
    }
    return html; // (derived.nextSteps owned by claude are these same entries: not listed twice)
  }
  function drawBuckets(el, w) {
    const rows = charts.buckets || [];
    const max = Math.max(1, ...rows.map(r => r.value));
    const rowH = 22, labW = 62, numW = 28, barW = Math.max(40, w - labW - numW);
    let s = '<svg width="' + w + '" height="' + (rows.length * rowH) + '" viewBox="0 0 ' + w + ' ' + (rows.length * rowH) + '">';
    rows.forEach((r, i) => {
      const y = i * rowH, bw = r.value ? Math.max(4, (r.value / max) * barW) : 0;
      s += '<text x="0" y="' + (y + 15) + '">' + esc(r.label) + '</text>';
      s += '<rect class="track" x="' + labW + '" y="' + (y + 5) + '" width="' + barW + '" height="12" rx="3"/>';
      if (bw) s += '<rect x="' + labW + '" y="' + (y + 5) + '" width="' + bw.toFixed(1) + '" height="12" rx="3" fill="' + r.color + '" data-tip="' + esc(r.label + ': ' + plural(r.value, 'entry', 'entries') + ' (' + r.hint + ')') + '"/>';
      s += '<text class="v" x="' + (labW + barW + 8) + '" y="' + (y + 15) + '">' + r.value + '</text>';
    });
    el.innerHTML = s + '</svg>';
  }

  // ---------------------------------------------------------------- 4. CI & PRs
  function renderCi() {
    let html = head('h-ci', 'CI & PRs', '', srcMeta(['github']));
    if (!srcOk('github')) return html + downBlock('github');
    const g = data('github');
    const runsObj = isObj(derived().runs) ? derived().runs : obj(g.runs);
    const running = arr(pick(runsObj, 'running', 'inProgress') || pick(g, 'running', 'runningRuns', 'runsRunning')).filter(isObj);
    const failed = obj(pick(runsObj, 'lastFailed', 'lastFailure') || pick(g, 'lastFailed', 'lastFailedRun', 'lastFailure'));
    const lastT = obj(pick(runsObj, 'lastTesting') || pick(g, 'lastTestingRun'));
    html += '<div class="sub-head">Running now' + (running.length ? ' (' + running.length + ')' : '') + '</div>';
    if (!running.length) html += '<p class="empty">No CI runs in progress.' + (has(lastT.conclusion) ? ' Last run on testing: ' + esc(lastT.conclusion) + ', ' + ageSpan(parseTime(lastT.finishedAt)) + '.' : '') + '</p>';
    for (const r of running) {
      const start = parseTime(pick(r, 'startedAt', 'createdAt', 'started'));
      const id = pick(r, 'id', 'runId');
      const name = esc(pick(r, 'name', 'workflow') || 'run') + ' <span class="muted">on</span> ' + unk(r.branch, 'branch unknown');
      const bits = [has(r.job) ? esc(r.job) : null, has(r.event) ? esc(r.event) : null, has(r.sha) ? '<span class="mono">' + esc(short(r.sha)) + '</span>' : null].filter(Boolean);
      html += '<div class="run">' + pill('run', 'running') + '<span class="t">' + (id != null ? tabLink('github', '#run=' + encodeURIComponent(id), name) : name) + ext(r.url) + '</span>' +
        '<span class="elapsed"' + (start != null ? ' data-start="' + start + '" title="started ' + esc(fmtFull(start)) + '"' : '') + '>' + (start != null ? '' : 'unknown') + '</span>' +
        '<span class="m">' + (bits.join(' &middot; ') || '&nbsp;') + (start != null ? ' &middot; started ' + esc(fmtWhen(start)) : '') + '</span></div>';
    }
    html += '<div class="sub-head">Last failure</div>';
    if (Object.keys(failed).length) {
      const id = pick(failed, 'id', 'runId');
      const ft = parseTime(pick(failed, 'finishedAt', 'updatedAt', 'completedAt'));
      const name = esc(pick(failed, 'name', 'workflow') || 'run') + ' <span class="muted">on</span> ' + unk(failed.branch, 'branch unknown');
      html += '<div class="fail">' + pill('crit', failed.conclusion || 'failed') + '<span class="t">' + (id != null ? tabLink('github', '#run=' + encodeURIComponent(id), name) : name) + ext(failed.url) + '</span>' +
        '<span class="small muted">' + ageSpan(ft) + '</span>' +
        '<span class="m">' + [has(failed.job) ? esc(failed.job) : null, has(failed.sha) ? '<span class="mono">' + esc(short(failed.sha)) + '</span>' : null, ft != null ? esc(fmtWhen(ft)) : null].filter(Boolean).join(' &middot; ') + '</span></div>';
    } else html += '<p class="empty">No failed runs on record.</p>';
    const prs = openPrs().filter(isObj);
    html += '<div class="sub-head">Open pull requests' + (prs.length ? ' (' + prs.length + ')' : '') + '</div>';
    if (!prs.length) html += '<p class="empty">No open pull requests.</p>';
    for (const pr of prs) {
      const n = num(pr.number);
      const ck = String(pick(pr, 'checks', 'checkState', 'state') || '').toLowerCase();
      const ckPill = /success|pass|green/.test(ck) ? pill('good', 'checks passed', pr.checksText) :
        /fail|error|red/.test(ck) ? pill('crit', 'checks failed', pr.checksText) :
        /pend|run|queue|progress/.test(ck) ? pill('run', 'checks running', pr.checksText) : pill('idle', 'no checks', pr.checksText);
      const title = '<span class="prnum">#' + (n != null ? n : '?') + '</span> ' + unk(pr.title, 'untitled');
      const up = parseTime(pr.updatedAt);
      html += '<div class="pr">' + ckPill + '<span class="t">' + (n != null ? tabLink('github', '#pr=' + n, title) : title) + ext(pr.url) + '</span>' +
        (pr.draft ? pill('idle', 'draft') : '<span></span>') +
        '<span class="m"><span class="mono">' + esc(pr.head || '?') + '</span> &rarr; ' + (prBase(pr) != null ? '<span class="mono"' + (has(pr.base) ? '' : ' title="the GitHub Manager did not say; a PR from any branch but testing goes into testing"') + '>' + esc(prBase(pr)) + '</span>' : '<span class="muted" title="a PR from testing goes to the default branch (production)">default branch</span>') +
        (has(pr.checksText) ? ' &middot; ' + esc(pr.checksText) : '') + (up != null ? ' &middot; updated ' + ageSpan(up) : '') + '</span></div>';
    }
    return html;
  }

  // ---------------------------------------------------------------- 5. assets
  function categories(p) {
    const c = pick(p, 'byCategory', 'by_category', 'categories');
    const out = [];
    const norm = (name, x) => ({ name: String(name || pick(x, 'category', 'name') || 'unknown'), link: x.link, human: num(pick(x, 'human')) || 0, third: num(pick(x, 'thirdParty', 'third_party', 'third-party', 'third')) || 0, ai: num(pick(x, 'ai', 'AI')) || 0 });
    if (Array.isArray(c)) c.filter(isObj).forEach(x => out.push(norm(null, x)));
    else if (isObj(c)) Object.keys(c).forEach(k => isObj(c[k]) && out.push(norm(k, c[k])));
    return out;
  }
  function renderAssets() {
    let html = head('h-assets', 'Assets', '', srcMeta(['editors']));
    const d = derived();
    if (!srcOk('editors')) {
      html += downBlock('editors');
      if (num(d.humanPct) != null) html += '<p class="small muted" style="margin-top:8px">Last known: ' + esc(num(d.humanPct)) + '% human-made.</p>';
      return html;
    }
    // derived.assets is the server's normalized copy; the raw editors data (snake or camel case) is the fallback
    const e = data('editors'), da = isObj(d.assets) ? d.assets : null;
    const p = da || obj(e.provenance);
    const cats = categories(p);
    const sum = k => cats.reduce((a, c) => a + c[k], 0);
    const first = (o, ...ks) => { const v = num(pick(o, ...ks)); return v; };
    const orSum = (v, k) => v != null ? v : sum(k);
    const human = orSum(first(p, 'human'), 'human');
    const third = orSum(first(p, 'thirdParty', 'third_party'), 'third');
    const ai = orSum(first(p, 'ai'), 'ai');
    const total = first(p, 'total') != null ? first(p, 'total') : human + third + ai;
    let pct = first(p, 'humanPct', 'pct_human', 'human_pct');
    if (pct == null) pct = num(d.humanPct);
    if (pct == null && total) pct = Math.round(human / total * 1000) / 10;
    const pending = num(pick(da || {}, 'pendingMarks')) != null ? num(da.pendingMarks) : first(e, 'pendingMarks', 'pending_marks');
    const remaining = num(pick(da || {}, 'remaining')) != null ? num(da.remaining) : first(e, 'remaining');
    const assetsLink = (da && linkAttrs(da.link)) ? da.link : tabHref('dashboard', '#view=assets');
    html += '<div class="assets-top"><div><div class="hero">' + (pct != null ? esc(pct) + '<small>%</small>' : '<span class="muted">?</span>') + '</div>' +
      '<div class="hero-cap">human-made' + (total ? ', ' + esc(human) + ' of ' + esc(total) + ' assets' : '') + '</div></div>' +
      '<div class="mini-stats"><div><b>' + unk(num(third)) + '</b>third-party</div><div><b>' + unk(num(ai)) + '</b>AI-made</div>' +
      '<div><b>' + unk(pending) + '</b>' + link(assetsLink, 'pending marks') + '</div>' +
      '<div><b>' + unk(remaining) + '</b>left to replace</div></div></div>';
    html += '<div class="legend"><span><i style="background:var(--human)"></i>human</span><span><i style="background:var(--third)"></i>third-party</span><span><i style="background:var(--ai)"></i>AI</span></div>';
    if (cats.length) {
      charts.stack = cats;
      html += '<div class="stack-chart" data-chart="stack" role="img" aria-label="' + esc('Per category, human / third-party / AI: ' + cats.map(c => c.name + ' ' + c.human + '/' + c.third + '/' + c.ai).join(', ')) + '"></div>';
    } else html += '<p class="empty">No per-category numbers.</p>';
    const progs = (Array.isArray(derived().programs) && derived().programs.length ? derived().programs : arr(e.programs)).filter(isObj);
    html += '<div class="sub-head">Editor changes</div>';
    if (!progs.length) html += '<p class="empty">No programs reported.</p>';
    else {
      const cell = v => { const n = num(v); return '<td class="n' + (n ? '' : ' z') + '">' + (n == null ? '?' : n) + '</td>'; };
      html += '<div class="table-wrap"><table class="t"><thead><tr><th>Program</th><th class="n">ahead of testing</th><th class="n">ahead of production</th><th class="n">uncommitted</th><th class="n">files changed</th></tr></thead><tbody>' +
        progs.map(pg => '<tr><td>' + (has(pg.id) ? tabLink('dashboard', '#view=changes&program=' + encodeURIComponent(pg.id), unk(pg.name || pg.id)) : unk(pg.name)) + '</td>' +
          cell(pick(pg, 'ahead_testing', 'aheadTesting')) + cell(pick(pg, 'ahead_production', 'aheadProduction')) + cell(pg.uncommitted) + cell(pick(pg, 'files_changed', 'filesChanged')) + '</tr>').join('') + '</tbody></table></div>';
    }
    return html;
  }
  function drawStack(el, w) {
    const cats = charts.stack || [];
    const rowH = 26, labW = 74, numW = 40, barW = Math.max(60, w - labW - numW), gap = 2;
    let s = '<svg width="' + w + '" height="' + (cats.length * rowH) + '" viewBox="0 0 ' + w + ' ' + (cats.length * rowH) + '">';
    cats.forEach((c, i) => {
      const y = i * rowH, tot = c.human + c.third + c.ai;
      const label = esc(c.name);
      const href = linkAttrs(c.link) ? c.link : tabHref('dashboard', '#view=assets&category=' + encodeURIComponent(c.name));
      const la = linkAttrs(href);
      s += la ? '<a ' + la + '><text x="0" y="' + (y + 17) + '">' + label + '</text></a>' : '<text x="0" y="' + (y + 17) + '">' + label + '</text>';
      if (!tot) { s += '<rect x="' + labW + '" y="' + (y + 7) + '" width="' + barW + '" height="14" rx="4" fill="var(--surface-2)"/>'; }
      let x = labW;
      const parts = [['human', c.human, 'var(--human)'], ['third-party', c.third, 'var(--third)'], ['AI', c.ai, 'var(--ai)']].filter(p => p[1] > 0);
      const usable = barW - gap * Math.max(0, parts.length - 1);
      parts.forEach((p, j) => {
        const pw = Math.max(2, usable * p[1] / tot);
        const first = j === 0, last = j === parts.length - 1;
        s += segPath(x, y + 7, pw, 14, 4, first, last, p[2], c.name + ' ' + p[0] + ': ' + p[1] + ' of ' + tot + ' (' + Math.round(p[1] / tot * 100) + '%)');
        x += pw + gap;
      });
      const hp = tot ? Math.round(c.human / tot * 100) : 0;
      s += '<text class="v" x="' + (labW + barW + 8) + '" y="' + (y + 18) + '">' + hp + '%</text>';
    });
    el.innerHTML = s + '</svg>';
  }
  // a bar segment with rounded outer ends only
  function segPath(x, y, w, h, r, roundL, roundR, fill, tip) {
    r = Math.min(r, w / 2, h / 2);
    const rl = roundL ? r : 0, rr = roundR ? r : 0;
    const d = 'M' + (x + rl) + ',' + y + 'H' + (x + w - rr) + (rr ? 'A' + rr + ',' + rr + ' 0 0 1 ' + (x + w) + ',' + (y + rr) + 'V' + (y + h - rr) + 'A' + rr + ',' + rr + ' 0 0 1 ' + (x + w - rr) + ',' + (y + h) : 'V' + (y + h)) +
      'H' + (x + rl) + (rl ? 'A' + rl + ',' + rl + ' 0 0 1 ' + x + ',' + (y + h - rl) + 'V' + (y + rl) + 'A' + rl + ',' + rl + ' 0 0 1 ' + (x + rl) + ',' + y : 'V' + y) + 'Z';
    return '<path class="seg" d="' + d + '" fill="' + fill + '" data-tip="' + esc(tip) + '"/>';
  }

  // ---------------------------------------------------------------- 6. servers & bot
  function queueCount(q) {
    if (num(q) != null) return num(q);
    if (typeof q !== 'string') return null;
    const t = q.trim();
    if (/^\d+$/.test(t)) return Number(t);
    const m = t.match(/(\d+)\s*(players?|queued|in queue)/i) || t.match(/^(?:queued|players)\s*[:=]\s*(\d+)/im);
    if (m) return Number(m[1]);
    return t ? t.split(/\r?\n/).filter(l => l.trim()).length : 0;
  }
  function renderServers() {
    let html = head('h-servers', 'Servers & bot', '', srcMeta(['servers', 'oracle']));
    if (!srcOk('servers')) html += downBlock('servers');
    else {
      const s = data('servers');
      const lst = k => { const x = obj(s[k]); const addrs = arr(x.addresses); const c = num(x.count) != null ? num(x.count) : (Array.isArray(x.addresses) ? addrs.length : null); return { c, v: x.version, addrs }; };
      const p = lst('production'), t = lst('testing');
      const q = queueCount(pick(s, 'queued', 'queue'));
      const stat = (k, x, sub) => '<div class="stat" title="' + esc(x.addrs.join('\n')) + '"><div class="k">' + esc(k) + '</div><div class="v' + (x.c === 0 ? ' zero' : '') + '">' + (x.c == null ? '<span class="muted">?</span>' : esc(x.c)) + '</div><div class="d">' + esc(sub) + '</div></div>';
      html += '<div class="stats">' + stat('Production servers', p, has(p.v) ? 'listed for ' + p.v : 'version unknown') + stat('Testing servers', t, has(t.v) ? 'listed for ' + t.v : 'version unknown') +
        '<div class="stat"><div class="k">Players queued</div><div class="v">' + (q == null ? '<span class="muted">?</span>' : esc(q)) + '</div><div class="d">ranked queue</div></div></div>';
    }
    // Oracle box
    const os = src('oracle'), od = data('oracle');
    const host = pick(od, 'host') || 'ubuntu@129.151.8.15';
    html += '<div class="box"><div class="box-head"><h3>Oracle box <span class="muted small mono">' + esc(host) + '</span></h3>' +
      '<span class="src">checked ' + (parseTime(os.fetchedAt) != null ? ageSpan(parseTime(os.fetchedAt)) : '<span class="muted">never</span>') +
      ' <button type="button" class="btn small" id="oracle-check" title="One read-only SSH check, at most every 2 minutes">Check now</button></span></div>';
    if (!srcOk('oracle')) html += downBlock('oracle');
    else {
      const load = arr(od.load).map(num);
      const used = num(od.memUsedMb), tot = num(od.memTotalMb);
      charts.mem = { used, tot };
      html += '<dl class="box-facts"><dt>Uptime</dt><dd>' + unk(od.uptime) + '</dd>' +
        '<dt>Load</dt><dd>' + (load.length ? load.map(l => l == null ? '?' : l.toFixed(2)).join(' &nbsp;') + ' <span class="muted small">(1, 5, 15 min)</span>' : '<span class="muted">unknown</span>') + '</dd>' +
        '<dt>Memory</dt><dd>' + (used != null && tot ? '<div class="membar" data-chart="mem"></div><span class="small">' + esc(used) + ' of ' + esc(tot) + ' MB used (' + Math.round(used / tot * 100) + '%)</span>' : '<span class="muted">unknown</span>') + '</dd></dl>';
      const units = obj(od.units);
      const names = Object.keys(units);
      html += '<div class="units">' + (names.length ? names.map(n => {
        const u = obj(units[n]);
        const a = String(u.active || ''), sub = String(u.sub || '');
        const since = parseTime(u.since);
        const kind = a === 'active' ? 'good' : a === 'failed' ? 'crit' : /activating|reloading|deactivating/.test(a) ? 'run' : a === 'inactive' ? 'warn' : 'idle';
        return pill(kind, n + (a ? ': ' + a : '') + (sub && sub !== a ? ' (' + sub + ')' : ''), since != null ? 'since ' + fmtFull(since) : '');
      }).join('') : '<span class="muted small">No units reported.</span>') + '</div>';
      const lines = (lab, l) => arr(l).length ? '<div class="loglines"><span class="lbl">' + esc(lab) + '</span>' + arr(l).map(x => esc(x)).join('\n') + '</div>' : '';
      html += lines('Last bot update (quark-discord-update)', od.botUpdate) + lines('Last server update (quark-update)', od.serverUpdate);
    }
    return html + '</div>';
  }
  function drawMem(el, w) {
    const m = charts.mem || {};
    const W = Math.min(360, w), f = m.tot ? Math.min(1, m.used / m.tot) : 0;
    const col = f > 0.9 ? 'var(--crit)' : f > 0.75 ? 'var(--warn)' : 'var(--s1)';
    el.innerHTML = '<svg width="' + W + '" height="12" viewBox="0 0 ' + W + ' 12" role="img" aria-label="memory ' + Math.round(f * 100) + '% used">' +
      '<rect x="0" y="2" width="' + W + '" height="8" rx="4" fill="var(--surface-2)" stroke="var(--border)"/>' +
      '<rect x="0" y="2" width="' + Math.max(4, W * f).toFixed(1) + '" height="8" rx="4" fill="' + col + '" data-tip="' + esc((m.used || 0) + ' MB of ' + (m.tot || 0) + ' MB') + '"/></svg>';
  }

  // ---------------------------------------------------------------- 7. local copies & builds
  function buildFolders() {
    const b = src('builds').ok === true ? src('builds').data : null;
    let list = [];
    if (Array.isArray(b)) list = b;
    else if (isObj(b)) {
      const f = b.folders || b.builds;
      if (Array.isArray(f)) list = f;
      else { const o = isObj(f) ? f : b; list = Object.keys(o).filter(k => isObj(o[k])).map(k => Object.assign({ name: k }, o[k])); }
    }
    return list.filter(isObj).map(x => Object.assign({}, x, { name: String(x.name || x.folder || '?') }));
  }
  function renderLocal() {
    let html = head('h-local', 'Local copies & builds', '', srcMeta(['builds', 'github']));
    const cards = [];
    if (!srcOk('builds')) cards.push('<div class="loc">' + downBlock('builds') + '</div>');
    for (const f of buildFolders()) {
      const missing = f.present === false || f.missing === true || f.exists === false;
      const noMarker = !missing && has(f.note) && !has(f.version); // (e.g. "no BUILD.txt")
      const mt = parseTime(pick(f, 'mtime', 'builtAt', 'time'));
      const lines = arr(f.lines || f.raw).map(String);
      cards.push('<div class="loc"><div class="n"><span>' + esc(f.name) + '</span><span class="kind">game folder</span></div>' +
        (missing ? '<div class="v muted">not found</div>' + (has(f.note) ? '<div class="m">' + esc(f.note) + '</div>' : '') :
          noMarker ? '<div class="v muted">' + esc(f.note) + '</div>' :
          '<div class="v">' + unk(f.version) + (has(f.commit) ? ' <span class="mono muted">' + esc(short(f.commit)) + '</span>' : '') + '</div>' +
          '<div class="m">built ' + ageSpan(mt) + (mt != null ? ' &middot; ' + esc(fmtWhen(mt)) : '') + '</div>' +
          (lines.length ? '<details data-key="build-' + esc(f.name) + '"><summary>BUILD.txt</summary><pre>' + lines.map(esc).join('\n') + '</pre></details>' : '')) + '</div>');
    }
    const copies = locals().filter(isObj);
    if (!srcOk('github') && !copies.length) cards.push('<div class="loc">' + downBlock('github') + '</div>');
    const repo = data('repo');
    for (const l of copies) {
      const ah = num(l.ahead), bh = num(l.behind), df = num(pick(l, 'dirtyFiles', 'dirtyCount', 'uncommitted'));
      const flags = [];
      if (ah) flags.push(pill('ahead', ah + ' ahead'));
      if (bh) flags.push(pill('warn', bh + ' behind'));
      if (l.dirty || df) flags.push(pill('warn', df ? df + ' uncommitted' : 'uncommitted', has(l.dirtySince) ? 'since ' + fmtFull(parseTime(l.dirtySince)) : ''));
      if (!ah && !bh && !(l.dirty || df) && (ah === 0 || bh === 0 || l.dirty === false)) flags.push(pill('good', 'in step, clean'));
      if (!flags.length) flags.push(pill('idle', 'state unknown'));
      const isRepo = /^repository$/i.test(String(l.name || ''));
      const name = esc(l.name || '?');
      cards.push('<div class="loc"><div class="n"><span>' + (has(l.name) ? tabLink('github', '#local=' + encodeURIComponent(l.name), name) : name) + '</span><span class="kind">repo copy</span></div>' +
        '<div class="v"><span class="mono">' + esc(l.branch || '?') + '</span> ' + (has(l.sha) ? '<span class="mono muted">' + esc(short(l.sha)) + '</span>' : '') + '</div>' +
        (isRepo && srcOk('repo') ? '<div class="m">VERSION ' + unk(repo.version) + ' &middot; full changelog ' + (repo.fullChangelog === true ? 'present' : repo.fullChangelog === false ? '<b>missing</b>' : 'unknown') + '</div>' : '') +
        '<div class="flags">' + flags.join('') + '</div></div>');
    }
    if (!cards.length) return html + '<p class="empty">No local folders reported.</p>';
    const note = !srcOk('github') && copies.length ? '<p class="small muted" style="margin:-4px 0 8px">GitHub Manager not running: the repo copies come from this board&rsquo;s own git reads.</p>' : '';
    return html + note + '<div class="locals">' + cards.join('') + '</div>';
  }

  // ---------------------------------------------------------------- 7b. tools & updates (pm/updater.py, installs.json)
  function updatesInfo() { return obj(state.pm && state.pm.updates); }
  const TOOL_STATES = {
    ok: ['good', 'up to date'], updating: ['run', 'updating'], failed: ['crit', 'failed'], pending: ['idle', 'pending'],
    disabled: ['idle', 'disabled'], waiting: ['warn', 'waiting (not merged yet)'],
  };
  function updateWaitMs() {
    // what the server said (nextAllowedIn when it answered), or the last click here
    const u = updatesInfo();
    const srv = num(u.nextAllowedIn) != null ? num(u.nextAllowedIn) * 1000 - (performance.now() - state.fetchedPerf) : 0;
    return Math.max(srv, UPDATE_MIN_MS - (nowMs() - state.updateClickAt));
  }
  function tmeta(e, ports) {
    // installed stamp (-> the new one when it differs), and the servers restarted after an install
    const parts = [];
    const inst = has(e.installedStamp) ? '<span class="mono" title="installed from these sources">' + esc(short(e.installedStamp)) + '</span>' : '';
    const cur = has(e.currentStamp) && e.currentStamp !== e.installedStamp ? '<span class="mono" title="repository\\ now">' + esc(short(e.currentStamp)) + '</span>' : '';
    if (inst || cur) parts.push(inst && cur ? inst + ' &rarr; ' + cur : inst || 'new: ' + cur);
    if (ports.length) parts.push('restarts ' + ports.map(p => esc(p)).join(', '));
    return parts.length ? '<div class="t-meta small muted">' + parts.join(' &middot; ') + '</div>' : '';
  }
  function renderTools() {
    const u = updatesInfo();
    const last = parseTime(u.lastRunAt);
    const meta = '<span class="src" title="Checked when the app opens and on Update now (installs.json in Quark\\repository)">' +
      (u.running === true ? pill('run', 'checking now') : 'checked ' + (last != null ? ageSpan(last) : '<span class="muted">not yet</span>')) +
      (u.enabled === false ? '' : ' <button type="button" class="btn small" id="tools-update" title="Pull testing into Quark\\repository and install what changed (at most once a minute)">Update now</button>') + '</span>';
    let html = head('h-tools', 'Tools & updates', '', meta);
    if (!state.pm) return html + '<p class="empty">Loading&hellip;</p>';
    if (!isObj(state.pm.updates)) return html + '<p class="empty">This PM server does not report updates (an older version: reopen Quark Project).</p>';
    if (u.enabled === false) html += '<p class="empty">Automatic installs are off (the PM server was started with --no-update).</p>';
    if (u.selfUpdated === true) html += '<div class="tools-banner">' + pill('ahead', 'Quark Project updated') + ' <span>Reopen the app to finish (the Restart button in the tab bar does it).</span></div>';
    if (state.updateMsg) html += '<p class="small muted">' + esc(state.updateMsg) + '</p>';
    const r = obj(u.repo);
    if (Object.keys(r).length) {
      const bh = num(r.behind);
      const st = has(r.error) ? pill(r.ffOk === true ? 'warn' : 'crit', r.ffOk === true ? 'note' : 'not updated') :
        r.ffOk === true ? pill('good', num(r.pulled) ? 'pulled ' + plural(num(r.pulled), 'commit') : 'up to date') : bh ? pill('warn', bh + ' behind') : pill('idle', 'unknown');
      html += '<div class="tools-repo"><span class="mono">repository\\</span> <span class="mono">' + esc(r.branch || '?') + '</span> ' +
        (has(r.head) ? '<span class="mono muted">' + esc(short(r.head)) + '</span> ' : '') + st +
        (has(r.error) ? '<div class="err small">' + esc(r.error) + '</div>' : '') + '</div>';
    }
    const ents = arr(u.entries).filter(isObj);
    if (!ents.length) return html + (u.enabled === false ? '' : '<p class="empty">' + (u.running === true || !last ? 'Checking the tools&hellip;' : 'No tools in installs.json.') + '</p>');
    html += '<ul class="tools">' + ents.map(e => {
      const s = TOOL_STATES[e.state] || ['idle', typeof e.state === 'string' && e.state ? e.state : 'unknown'];
      const text = e.state === 'failed' && num(e.exitCode) != null ? 'failed (exit ' + num(e.exitCode) + ')' : s[1];
      const ran = parseTime(e.lastRunAt);
      const tailLines = arr(e.logTail).map(String);
      const ports = arr(Array.isArray(e.restartsPort) ? e.restartsPort : [e.restartsPort]).filter(p => num(p) != null);
      return '<li><div class="t-main"><span class="t-name">' + esc(e.name || e.id || '?') + '</span>' + pill(s[0], text, has(e.note) ? String(e.note) : '') +
        '<span class="t-when small muted">' + (ran != null ? 'last run ' + whenSpan(ran) : 'never run here') + '</span></div>' +
        (has(e.note) ? '<div class="small muted">' + esc(e.note) + '</div>' : '') +
        tmeta(e, ports) +
        (tailLines.length ? '<details data-key="tool-' + esc(e.id) + '"><summary>log</summary><pre>' + tailLines.map(esc).join('\n') + '</pre></details>' : '') + '</li>';
    }).join('') + '</ul>';
    return html;
  }

  // ---------------------------------------------------------------- 8. updates
  function renderChangelogs() {
    let html = head('h-changelogs', 'Changelogs', '', srcMeta(['repo']));
    if (!srcOk('repo')) return html + downBlock('repo');
    const r = data('repo');
    const cls = arr(pick(r, 'changelogs', 'changes')).filter(isObj).slice(0, 8);
    if (has(r.version)) html += '<p class="small muted" style="margin:-4px 0 8px">repository VERSION <b>' + esc(r.version) + '</b> &middot; full changelog ' + (r.fullChangelog === true ? 'written' : r.fullChangelog === false ? '<span class="warn">not written yet</span>' : 'unknown') + '</p>';
    if (!cls.length) return html + '<p class="empty">No changelogs found.</p>';
    cls.forEach((c, i) => {
      const lines = arr(c.lines).map(String);
      const mt = parseTime(c.committedAt) != null ? parseTime(c.committedAt) : parseTime(c.mtime); // (committedAt: when git added it)
      let summary = '';
      const blocks = []; // {h, items}
      for (const raw of lines) {
        const l = raw.trim();
        if (!l || /^#\s/.test(l)) continue;
        if (/^##+\s/.test(l)) { blocks.push({ h: l.replace(/^#+\s*/, ''), items: [] }); continue; }
        if (/^[-*]\s/.test(l)) { if (!blocks.length) blocks.push({ h: '', items: [] }); blocks[blocks.length - 1].items.push(l.replace(/^[-*]\s+/, '')); continue; }
        if (!summary && !blocks.length) summary = l; else if (blocks.length) blocks[blocks.length - 1].items.push(l);
      }
      const total = blocks.reduce((a, b) => a + b.items.length, 0);
      const bl = b => (b.h ? '<h4>' + mdInline(b.h) + '</h4>' : '') + '<ul>' + b.items.map(x => '<li>' + mdInline(x) + '</li>').join('') + '</ul>';
      html += '<div class="cl"><div class="cl-head"><b>' + unk(c.version) + '</b><span class="small muted">' + whenSpan(mt) + '</span></div>' +
        (summary ? '<div class="cl-sum">' + mdInline(summary) + '</div>' : '') +
        (total ? (i === 0 ? blocks.map(bl).join('') : '<details data-key="cl-' + esc(c.version) + '"><summary>' + esc(plural(total, 'line')) + '</summary>' + blocks.map(bl).join('') + '</details>') : '') + '</div>';
    });
    return html;
  }
  function laneOf(kind) {
    const k = String(kind || '').toLowerCase();
    return LANES.find(l => l.match.test(k)) || LANES[LANES.length - 1];
  }
  function historyItems() {
    const h = state.history;
    return arr(Array.isArray(h) ? h : obj(h).events || obj(h).items).filter(isObj)
      .map(e => ({ t: parseTime(e.time), kind: e.kind, text: e.text, link: e.link, lane: laneOf(e.kind) }))
      .sort((a, b) => (b.t || 0) - (a.t || 0)).slice(0, 30);
  }
  function renderHistory() {
    const meta = '<span class="src"><span class="dot' + (state.history ? '' : ' dot-down') + '"></span><b>PM event log</b> ' + (state.historyAt ? ageSpan(state.historyAt) : '<span class="muted">never</span>') + '</span>';
    let html = head('h-history', 'Event log', '', meta);
    const items = historyItems();
    if (!state.history) return html + '<div class="down">' + IC.down + '<div><strong>Event log not loaded</strong><div class="fix">The PM server&rsquo;s /api/history did not answer; it fills in once the server is up.</div></div></div>';
    if (!items.length) return html + '<p class="empty">Nothing has changed since the PM server started logging.</p>';
    const lanes = LANES.filter(l => items.some(i => i.lane === l));
    charts.timeline = { items: items.filter(i => i.t != null), lanes };
    html += '<div class="tl-chart" data-chart="timeline" role="img" aria-label="Events over the last 7 days, by kind"></div>';
    const shown = state.showAllHistory ? items : items.slice(0, 12);
    let lastDay = '', li = '';
    for (const e of shown) {
      const dk = e.t != null ? dayKey(e.t) : 'unknown';
      if (dk !== lastDay) {
        lastDay = dk;
        const lab = e.t == null ? 'Unknown time' : dk === dayKey(nowMs()) ? 'Today' : dk === dayKey(nowMs() - 86400000) ? 'Yesterday' : fmt({ weekday: 'long', month: 'short', day: 'numeric' }).format(e.t);
        li += '<li class="day">' + esc(lab) + '</li>';
      }
      li += '<li style="--kc:' + e.lane.color + '"><span class="tx">' + (linkAttrs(e.link) ? link(e.link, unk(e.text, 'event')) : unk(e.text, 'event')) + ' <span class="muted small">' + esc(e.lane.label) + '</span></span>' +
        '<span class="tm" title="' + esc(e.t != null ? fmtFull(e.t) : '') + '">' + esc(e.t != null ? fmt({ hour: 'numeric', minute: '2-digit' }).format(e.t) : '?') + '</span></li>';
    }
    html += '<ul class="timeline">' + li + '</ul>';
    html += '<div class="hist-btns">' + (items.length > 12 ? '<button type="button" class="btn ghost small show-more" id="history-more">' + (state.showAllHistory ? 'Show fewer' : 'Show all ' + items.length) + '</button>' : '') +
      '<button type="button" class="btn ghost small show-more" id="history-all" title="Every event since the PM server started logging (history.jsonl)">Full history&hellip;</button></div>';
    return html;
  }
  function drawTimeline(el, w) {
    const tl = charts.timeline || { items: [], lanes: [] };
    const n = nowMs(), span = 7 * 86400000, t0 = n - span;
    const labW = 62, padR = 8, laneH = 22, top = 4, axisH = 18;
    const lanes = tl.lanes.length ? tl.lanes : [LANES[LANES.length - 1]];
    const H = top + lanes.length * laneH + axisH, plotW = Math.max(60, w - labW - padR);
    const x = t => labW + (Math.max(t0, Math.min(n, t)) - t0) / span * plotW;
    let s = '<svg width="' + w + '" height="' + H + '" viewBox="0 0 ' + w + ' ' + H + '">';
    // day gridlines at Dallas midnights (approximate: step back from now by whole days in Dallas)
    for (let i = 0; i <= 7; i++) {
      const dayStart = dallasMidnight(n - i * 86400000);
      if (dayStart < t0) continue;
      const gx = x(dayStart);
      s += '<line class="gl" x1="' + gx.toFixed(1) + '" x2="' + gx.toFixed(1) + '" y1="' + top + '" y2="' + (H - axisH + 2) + '"/>';
      if (gx < w - 20) s += '<text x="' + (gx + 3).toFixed(1) + '" y="' + (H - 4) + '">' + esc(i === 0 ? 'today' : fmt({ weekday: 'short' }).format(dayStart + 3600000)) + '</text>';
    }
    lanes.forEach((l, i) => {
      const y = top + i * laneH + laneH / 2;
      s += '<text class="lane" x="0" y="' + (y + 4) + '">' + esc(l.label) + '</text><line class="lane-line" x1="' + labW + '" x2="' + (w - padR) + '" y1="' + y + '" y2="' + y + '"/>';
    });
    for (const e of tl.items) {
      if (e.t < t0) continue;
      const li = Math.max(0, lanes.indexOf(e.lane));
      const y = top + li * laneH + laneH / 2;
      s += '<circle cx="' + x(e.t).toFixed(1) + '" cy="' + y + '" r="5" fill="' + e.lane.color + '" data-tip="' + esc(fmtWhen(e.t) + ': ' + (e.text || e.kind || 'event')) + '"/>';
    }
    el.innerHTML = s + '</svg>';
  }
  function dallasMidnight(ms) {
    // the start of the Dallas calendar day holding ms
    const parts = fmt({ hour: 'numeric', minute: 'numeric', second: 'numeric', hourCycle: 'h23' }).formatToParts(ms);
    const g = t => Number((parts.find(p => p.type === t) || {}).value || 0);
    return ms - ((g('hour') % 24) * 3600 + g('minute') * 60 + g('second')) * 1000 - (ms % 1000);
  }

  // ---------------------------------------------------------------- 9. tabs
  // Tabs as a tree by each tab's optional `parent` (managers under "project", editors under "dashboard");
  // a tab without a known parent is a root. The nav bar shows the same order, flattened.
  function tabTree(tabs) {
    const ids = new Set(tabs.map(t => String(t.id)));
    const kids = {}, roots = [], seen = new Set(), out = [];
    for (const t of tabs) {
      const par = has(t.parent) && String(t.parent) !== String(t.id) && ids.has(String(t.parent)) ? String(t.parent) : null;
      if (par) (kids[par] = kids[par] || []).push(t); else roots.push(t);
    }
    const walk = (t, depth) => {
      if (seen.has(t)) return;
      seen.add(t); out.push({ t, depth });
      for (const k of kids[String(t.id)] || []) walk(k, depth + 1);
    };
    roots.forEach(r => walk(r, 0));
    tabs.forEach(t => { if (!seen.has(t)) out.push({ t, depth: 0 }); }); // (a parent cycle: shown at the top level)
    return out;
  }
  function renderTabs() {
    const tabs = tabList().filter(isObj);
    const sec = document.getElementById('sec-tabs');
    const nav = document.getElementById('nav-tabs');
    if (EMBED) { if (!sec.hidden) sec.hidden = true; return null; }
    const tree = tabTree(tabs);
    const item = (t, inNav) => {
      const installed = t.installed !== false;
      const up = t.up === true || (!has(t.port) && installed);
      const name = esc(t.name || TAB_NAMES[t.id] || t.id || '?');
      const self = t.id === 'project';
      if (!installed) return inNav ? '<a class="off" title="not installed">' + name + '</a>' : '<span class="tablink off" title="not installed"><span class="sd"></span>' + name + ' <span class="small">not installed</span></span>';
      const href = typeof t.url === 'string' ? t.url.replace(/([?&])embed=1&?/, '$1').replace(/[?&]$/, '') : '';
      const safe = /^https?:/.test(href) ? esc(href) : /^file:/.test(href) ? esc(href) : '#';
      const attrs = 'href="' + safe + '" data-tab="' + esc(t.id || '') + '" data-hash=""' + (self ? ' aria-current="page"' : ' target="_blank" rel="noopener"');
      return inNav ? '<a ' + attrs + (has(t.port) && !up ? ' class="off" title="not running"' : '') + '>' + (has(t.port) ? '<span class="sd' + (up ? ' up' : '') + '"></span>' : '') + name + '</a>'
        : '<a class="tablink" ' + attrs + '><span class="sd' + (up ? ' up' : '') + '"></span>' + name + (has(t.port) && !up ? ' <span class="small muted">not running</span>' : '') + '</a>';
    };
    // nav: every tab but this page (the brand is this page), a separator before the editors
    if (nav) {
      let h = '', prev = -1;
      for (const { t, depth } of tree) {
        if (t.id === 'project') continue;
        if (depth >= 2 && prev < 2 && h) h += '<span class="qt-nav-sep"></span>';
        h += item(t, true); prev = depth;
      }
      if (state.shown.nav !== h) { state.shown.nav = h; nav.innerHTML = h; }
    }
    if (!tabs.length) { if (!sec.hidden) sec.hidden = true; return null; }
    if (sec.hidden) sec.hidden = false;
    // card: nested lists
    let html = '', depth = -1;
    for (const { t, depth: d } of tree) {
      if (d > depth) html += '<ul class="tabtree">'.repeat(d - depth);
      else html += '</li>' + '</ul></li>'.repeat(depth - d);
      html += '<li>' + item(t, false);
      depth = d;
    }
    html += '</li>' + '</ul></li>'.repeat(Math.max(0, depth)) + '</ul>';
    return head('h-tabs', 'Tabs', '', '<span class="src"><b>/api/tabs</b></span>') + html;
  }

  // ---------------------------------------------------------------- play notes (the Play tab, /play)
  function renderPlay() {
    const p = obj(state.pm && state.pm.play);
    const el = document.getElementById('sec-play');
    const open = num(p.openNotes), pend = num(p.pending), run = num(p.running);
    const hide = open == null && pend == null;
    if (el.hidden !== hide) el.hidden = hide;
    if (hide) return '';
    const bits = [esc(plural(open || 0, 'play note')) + ' open'];
    if (pend) bits.push('<b>' + esc(pend) + ' waiting for you to write</b>');
    if (run) bits.push(esc(plural(run, 'game')) + ' running');
    return '<a href="/play" data-tab="play" data-hash="" title="The Play tab: start a build, write play notes">Play</a> &middot; ' + bits.join(' &middot; ');
  }

  // ---------------------------------------------------------------- render
  const SECTIONS = [
    ['sec-halting', renderHalting], ['sec-pipeline', renderPipeline], ['sec-needs', renderNeeds], ['sec-progress', renderProgress], ['sec-ci', renderCi],
    ['sec-assets', renderAssets], ['sec-servers', renderServers], ['sec-local', renderLocal], ['sec-tools', renderTools],
    ['sec-changelogs', renderChangelogs], ['sec-history', renderHistory], ['sec-tabs', renderTabs], ['sec-play', renderPlay],
  ];
  const DRAW = { buckets: drawBuckets, stack: drawStack, mem: drawMem, timeline: drawTimeline };

  // Each card is drawn to a string and compared with what it shows. Live ages / elapsed times are attributes the
  // ticker reads (data-age, data-start): a card whose html differs only there keeps its DOM (text selection, hover,
  // focus, open <details>) and gets just those attributes updated; only a card whose data changed is replaced.
  const LIVE_ATTR = / data-(?:age|start)="[^"]*"/g;
  function patchSection(el, id, html) {
    const key = html.replace(LIVE_ATTR, '');
    if (state.shown[id] === key) {
      const vals = (html.match(LIVE_ATTR) || []).map(v => v.match(/data-(age|start)="([^"]*)"/));
      const live = el.querySelectorAll('[data-age],[data-start]');
      if (vals.length === live.length && vals.every((m, i) => m[1] in live[i].dataset)) {
        vals.forEach((m, i) => { if (live[i].dataset[m[1]] !== m[2]) live[i].dataset[m[1]] = m[2]; });
        return false;
      }
    }
    state.shown[id] = key;
    el.querySelectorAll('details[data-key]').forEach(d => { if (d.open) state.openDetails.add(d.dataset.key); else state.openDetails.delete(d.dataset.key); });
    el.innerHTML = html;
    el.querySelectorAll('details[data-key]').forEach(d => { if (state.openDetails.has(d.dataset.key)) d.open = true; });
    return true;
  }
  function renderAll() {
    const changed = [];
    for (const [id, fn] of SECTIONS) {
      const el = document.getElementById(id);
      if (!el) continue;
      let html;
      try { html = fn(); }
      catch (err) { html = '<div class="down">' + IC.down + '<div><strong>Could not draw this card</strong><div class="err">' + esc(err && err.message || err) + '</div></div></div>'; if (window.console) console.error(err); }
      if (html != null && patchSection(el, id, html)) changed.push(el);
    }
    publishHalting();
    changed.forEach(el => drawCharts(el));
    tick();
  }
  function rerender(id) { // one card now (a click that changes only it)
    const sec = SECTIONS.find(x => x[0] === id), el = document.getElementById(id);
    if (sec && el && patchSection(el, id, sec[1]())) drawCharts(el);
    tick();
  }
  function drawCharts(root) {
    (root || document).querySelectorAll('[data-chart]').forEach(el => {
      const fn = DRAW[el.dataset.chart];
      if (!fn) return;
      try { fn(el, Math.max(120, Math.floor(el.clientWidth || el.parentNode.clientWidth || 400))); }
      catch (err) { el.innerHTML = '<span class="small muted">chart unavailable</span>'; if (window.console) console.error(err); }
    });
  }

  function renderStatus() {
    const el = document.getElementById('status-text'), line = document.getElementById('statusline');
    if (!el) return;
    let txt, bad = false;
    if (state.error && !state.pm) { txt = 'PM server not reachable (' + state.error + '). Start it with project-manager.cmd.'; bad = true; }
    else if (state.error) { txt = 'PM server not reachable (' + state.error + '); showing data from ' + fmtAge(state.lastOkMs) + '.'; bad = true; }
    else if (!state.pm) txt = 'Loading…';
    else txt = (FIXTURE ? 'Sample data (fixtures/pm-sample.json) · ' : '') + 'Generated ' + fmtWhen(state.genMs) + ' Dallas · ' + fmtAge(state.genMs) + (FIXTURE ? '' : ' · refreshes every 15 s');
    setText(el, txt);
    if (line.classList.contains('bad') !== bad) line.classList.toggle('bad', bad);
  }

  // ---------------------------------------------------------------- ticker (1 s): ages, elapsed, clock, buttons
  // (every write is skipped when the value is the same: an idle page makes no DOM changes but the ones it shows)
  function setText(el, t) { if (el.textContent !== t) el.textContent = t; }
  function tick() {
    document.querySelectorAll('[data-age]').forEach(el => {
      const ms = Number(el.dataset.age);
      setText(el, (el.dataset.prefix || '') + fmtAge(ms));
      const t = fmtFull(ms);
      if (el.title !== t) el.title = t;
    });
    document.querySelectorAll('[data-start]').forEach(el => setText(el, fmtDur(nowMs() - Number(el.dataset.start))));
    const c = document.getElementById('clock');
    if (c) setText(c, fmt({ weekday: 'short', hour: 'numeric', minute: '2-digit', timeZoneName: 'short' }).format(nowMs()) + ' Dallas');
    const b = document.getElementById('oracle-check');
    if (b) {
      const last = Math.max(parseTime(src('oracle').fetchedAt) || -1e15, state.oracleClickAt);
      const wait = ORACLE_MIN_MS - (nowMs() - last);
      const checking = !!state.settle.oracle;
      b.disabled = checking || wait > 0;
      setText(b, checking ? 'Checking…' : wait > 0 ? 'Check in ' + fmtDur(wait) : 'Check now');
    }
    const ub = document.getElementById('tools-update');
    if (ub) {
      const u = updatesInfo();
      const w = updateWaitMs();
      ub.disabled = u.running === true || w > 0;
      setText(ub, u.running === true ? 'Updating…' : w > 0 ? 'Update in ' + fmtDur(w) : 'Update now');
    }
    const r = document.getElementById('refresh-btn');
    if (r) {
      const w2 = REFRESH_MIN_MS - (performance.now() - state.refreshAt);
      const busy = !!state.settle.refresh;
      if (r.disabled !== (busy || w2 > 0)) r.disabled = busy || w2 > 0;
      setText(r, busy ? 'Refreshing…' : 'Refresh');
    }
    renderStatus();
  }

  // ---------------------------------------------------------------- data
  async function getJson(url, opts) {
    const ctl = typeof AbortController === 'function' ? new AbortController() : null;
    const to = setTimeout(() => ctl && ctl.abort(), 8000);
    try {
      const r = await fetch(url, Object.assign({ cache: 'no-store', signal: ctl ? ctl.signal : undefined }, opts || {}));
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return await r.json();
    } finally { clearTimeout(to); }
  }
  async function poll(refresh) {
    state.lastPollPerf = performance.now();
    try {
      if (FIXTURE) {
        const f = await getJson(FIXTURE_URL);
        apply(f, f.history, f.tabs);
      } else {
        const pm = await getJson('/api/pm' + (refresh ? '?refresh=1' : ''));
        // the sources refresh after this answer: poll again until each has fetched since (not 15 s later)
        if (refresh && isObj(pm) && obj(pm.refresh).accepted === true) {
          const since = (parseTime(pm.serverNow) || Date.now()) - 1000; // (fetchedAt has whole seconds)
          settleOn('refresh', () => Object.keys(SOURCES).every(k => k === 'oracle' || src(k).pending === true ||
            (parseTime(src(k).fetchedAt) || 0) >= since));
        }
        // /api/pm carries `history` (latest 30) and `tabs`; the separate endpoints are the fallback
        const [hist, tabs] = await Promise.allSettled([
          isObj(pm) && pm.history != null ? Promise.resolve(pm.history) : getJson('/api/history?limit=30'),
          isObj(pm) && pm.tabs != null ? Promise.resolve(pm.tabs) : getJson('/api/tabs')]);
        apply(pm, hist.status === 'fulfilled' ? hist.value : state.history, tabs.status === 'fulfilled' ? tabs.value : state.tabs, hist.status === 'fulfilled');
      }
      state.error = null;
    } catch (err) {
      state.error = (err && err.name === 'AbortError') ? 'timed out' : String(err && err.message || err || 'error');
      if (!state.pm) renderAll(); else renderStatus();
    }
    // a settle ends when its data is in or its time is up
    for (const k of Object.keys(state.settle)) {
      const st = state.settle[k];
      if (performance.now() > st.until || (state.pm && !state.error && st.done())) delete state.settle[k];
    }
    schedule();
    tick();
  }
  function settleOn(kind, done) {
    if (FIXTURE) return;
    state.settle[kind] = { until: performance.now() + SETTLE[kind].max, done };
    schedule();
  }
  // (serverNow and the refresh answer change on every poll; the rest is what is shown)
  const VOLATILE = { serverNow: 1, refresh: 1 };
  function apply(pm, history, tabs, historyFresh) {
    if (!isObj(pm)) throw new Error('bad /api/pm answer');
    const sig = JSON.stringify([pm, history, tabs], (k, v) => VOLATILE[k] ? undefined : v);
    state.genMs = parseTime(pm.generatedAt) || Date.now();
    state.nowAnchor = parseTime(pm.serverNow) || state.genMs; // (an older server, or the sample: its generatedAt)
    state.fetchedPerf = performance.now();
    state.lastOkMs = state.nowAnchor;
    if (historyFresh !== false) state.historyAt = state.nowAnchor;
    if (sig === state.sig) { tick(); return; } // nothing changed: ages are kept fresh by the ticker
    state.sig = sig; state.pm = pm; state.history = history == null ? null : history; state.tabs = tabs == null ? null : tabs;
    renderAll();
  }
  function schedule() {
    clearTimeout(state.timer);
    if (document.hidden) return; // paused while hidden; resumes on visibilitychange
    const kinds = Object.keys(state.settle);
    state.timer = setTimeout(() => poll(false), kinds.length ? Math.min(...kinds.map(k => SETTLE[k].every)) : POLL_MS);
  }

  // ---------------------------------------------------------------- events
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) clearTimeout(state.timer);
    else if (performance.now() - state.lastPollPerf > POLL_MS) poll(false);
    else schedule();
  });
  document.addEventListener('click', e => {
    const t = e.target;
    if (!(t instanceof Element)) return;
    const a = t.closest('a[data-tab]');
    if (a && window.quarkShell && typeof window.quarkShell.open === 'function') {
      e.preventDefault();
      try { window.quarkShell.open(a.dataset.tab, a.dataset.hash || ''); } catch (err) { /* the shell logs it */ }
      return;
    }
    if (a && a.getAttribute('href') === '#') { e.preventDefault(); return; }
    const op = t.closest('[data-open]');
    if (op) { const d = document.getElementById(op.dataset.open); if (d) { e.preventDefault(); d.open = true; d.scrollIntoView({ block: 'nearest', behavior: 'smooth' }); } return; }
    if (t.closest('#history-more')) { state.showAllHistory = !state.showAllHistory; rerender('sec-history'); return; }
    if (t.closest('#history-all')) { openFullHistory(); return; }
    if (t.closest('#fh-close')) { document.getElementById('history-dialog').close(); return; }
    if (t.closest('#fh-more')) { state.fullLimit += FULL_HISTORY_PAGE; renderFullHistory(); return; }
    const cp = t.closest('[data-copy]');
    if (cp) { copyText(cp.dataset.copy, cp); return; }
    if (t.closest('#halt-mute')) {
      const off = !chimeMuted();
      try { localStorage.setItem('pm.haltChime', off ? 'off' : 'on'); } catch (err) { /* private window: not kept */ }
      t.closest('#halt-mute').textContent = off ? 'Unmute' : 'Mute';
      return;
    }
    if (t.closest('[data-dismiss]')) { document.getElementById('halt-toast').hidden = true; if (!t.closest('a')) return; }
    if (t.closest('#refresh-btn')) { state.refreshAt = performance.now(); tick(); poll(true); return; }
    if (t.closest('#tools-update')) {
      state.updateClickAt = nowMs(); state.updateMsg = ''; tick();
      if (FIXTURE) return;
      const before = updatesInfo().lastRunAt;
      fetch('/api/updates/run', { method: 'POST', cache: 'no-store' })
        .then(r => r.json())
        .then(j => {
          const o = obj(j);
          if (o.accepted !== true) { state.updateMsg = o.enabled === false ? 'Automatic installs are off on this server.' : 'Not started: an update ran less than a minute ago or is running.'; rerender('sec-tools'); }
          else settleOn('update', () => updatesInfo().running === true || updatesInfo().lastRunAt !== before);
        }, () => { state.updateMsg = 'The PM server did not answer.'; rerender('sec-tools'); });
      return;
    }
    if (t.closest('#oracle-check')) {
      state.oracleClickAt = nowMs();
      if (FIXTURE) { tick(); return; }
      // the SSH check takes ~11 s (up to 45 s): "Checking…" and a poll every 3 s until its result is in
      const since = nowMs() - 1000;
      settleOn('oracle', () => (parseTime(src('oracle').fetchedAt) || 0) >= since);
      tick();
      fetch('/api/refresh?source=oracle', { method: 'POST', cache: 'no-store' })
        .then(r => r.json())
        .then(j => { if (obj(j).accepted !== true) { delete state.settle.oracle; tick(); } }, () => { delete state.settle.oracle; tick(); });
    }
  });
  document.addEventListener('change', e => {
    const t = e.target;
    if (t instanceof HTMLInputElement && t.dataset.ck != null) tickSection(t);
    if (t instanceof Element && t.closest('#fh-lane')) { state.fullLimit = FULL_HISTORY_PAGE; renderFullHistory(); }
  });
  document.addEventListener('input', e => {
    if (e.target instanceof Element && e.target.closest('#fh-q')) { state.fullLimit = FULL_HISTORY_PAGE; renderFullHistory(); }
  });

  // copy (a HALT item's text or command): the async clipboard, else a hidden textarea
  function copyText(text, btn) {
    const done = ok => {
      if (!btn) return;
      const was = btn.dataset.label || btn.textContent;
      btn.dataset.label = was;
      btn.textContent = ok ? 'Copied' : 'Copy failed';
      clearTimeout(btn._t);
      btn._t = setTimeout(() => { btn.textContent = was; }, 1500);
    };
    const fallback = () => {
      const ta = document.createElement('textarea');
      ta.value = text; ta.setAttribute('readonly', ''); ta.style.position = 'fixed'; ta.style.opacity = '0';
      document.body.appendChild(ta); ta.select();
      let ok = false;
      try { ok = document.execCommand('copy'); } catch (err) { ok = false; }
      ta.remove();
      done(ok);
    };
    if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') navigator.clipboard.writeText(text).then(() => done(true), fallback);
    else fallback();
  }

  // a checklist tick: saved on the PM server per testing version (POST /api/checklist), shown at once
  function tickSection(input) {
    const p = checklistProgress(), k = input.dataset.ck, on = input.checked;
    if (!p.version || state.ckBusy.has(k)) { input.checked = !on; return; }
    const d = derived();
    const local = view => {
      d.checklist = Object.assign({}, obj(d.checklist), view, { count: Object.keys(obj(view.done)).length });
      const keys = checklistItems().map(sectionKey);
      for (const st of arr(d.nextSteps)) if (isObj(st) && isObj(st.progress)) st.progress = { done: keys.filter(x => obj(view.done)[x]).length, total: keys.length };
      state.sig = ''; // (the next poll redraws from the server's copy)
      rerender('sec-pipeline'); rerender('sec-needs');
    };
    const done = Object.assign({}, p.ticks);
    if (on) done[k] = { at: new Date(nowMs()).toISOString(), build: obj(d.versions).testingServer || null }; else delete done[k];
    state.ckMsg = '';
    local({ version: p.version, done });
    if (FIXTURE) return;
    state.ckBusy.add(k);
    fetch('/api/checklist', { method: 'POST', cache: 'no-store', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ version: p.version, section: k, done: on, build: obj(d.versions).testingServer || null }) })
      .then(r => r.json().then(j => ({ ok: r.ok, j })))
      .then(({ ok, j }) => { if (!ok || obj(j).ok !== true) throw new Error(obj(j).message || 'not saved'); local(obj(j).checklist); })
      .catch(err => {
        const back = Object.assign({}, done);
        if (on) delete back[k]; else back[k] = p.ticks[k];
        state.ckMsg = 'Not saved: ' + (err && err.message || 'the PM server did not answer') + '.';
        local({ version: p.version, done: back });
      })
      .finally(() => state.ckBusy.delete(k));
  }

  // ---------------------------------------------------------------- full event history (a dialog; /api/history)
  function openFullHistory() {
    const dlg = document.getElementById('history-dialog');
    if (!dlg) return;
    state.fullLimit = FULL_HISTORY_PAGE; state.fullErr = '';
    if (!dlg.open) dlg.showModal();
    renderFullHistory();
    const load = FIXTURE ? Promise.resolve(state.history) : getJson('/api/history?limit=10000');
    load.then(h => { state.full = arr(Array.isArray(h) ? h : obj(h).events).filter(isObj); renderFullHistory(); },
      err => { state.fullErr = 'Could not load the event log (' + (err && err.message || 'error') + ').'; renderFullHistory(); });
  }
  function renderFullHistory() {
    const body = document.getElementById('fh-body'), meta = document.getElementById('fh-meta'), sel = document.getElementById('fh-lane');
    if (!body) return;
    if (sel && sel.options.length <= 1) sel.innerHTML = '<option value="">All kinds</option>' + LANES.map(l => '<option value="' + l.id + '">' + esc(l.label) + '</option>').join('');
    if (state.fullErr) { body.innerHTML = '<p class="empty">' + esc(state.fullErr) + '</p>'; return; }
    if (!state.full) { body.innerHTML = '<p class="empty">Loading&hellip;</p>'; return; }
    const lane = sel ? sel.value : '', q = (document.getElementById('fh-q') || {}).value || '';
    const ql = q.trim().toLowerCase();
    const all = state.full.map(e => ({ t: parseTime(e.time), kind: e.kind, text: e.text, link: e.link, lane: laneOf(e.kind) }))
      .sort((a, b) => (b.t || 0) - (a.t || 0));
    const hits = all.filter(e => (!lane || e.lane.id === lane) && (!ql || String(e.text || '').toLowerCase().indexOf(ql) >= 0));
    meta.textContent = hits.length === all.length ? plural(all.length, 'event') : hits.length + ' of ' + plural(all.length, 'event');
    let lastDay = '', li = '';
    for (const e of hits.slice(0, state.fullLimit)) {
      const dk = e.t != null ? dayKey(e.t) : 'unknown';
      if (dk !== lastDay) { lastDay = dk; li += '<li class="day">' + esc(e.t == null ? 'Unknown time' : fmt({ weekday: 'long', month: 'short', day: 'numeric', year: 'numeric' }).format(e.t)) + '</li>'; }
      li += '<li style="--kc:' + e.lane.color + '"><span class="tx">' + (linkAttrs(e.link) ? link(e.link, unk(e.text, 'event')) : unk(e.text, 'event')) + ' <span class="muted small">' + esc(e.lane.label) + '</span></span>' +
        '<span class="tm" title="' + esc(e.t != null ? fmtFull(e.t) : '') + '">' + esc(e.t != null ? fmt({ hour: 'numeric', minute: '2-digit' }).format(e.t) : '?') + '</span></li>';
    }
    body.innerHTML = hits.length ? '<ul class="timeline">' + li + '</ul>' + (hits.length > state.fullLimit ? '<button type="button" class="btn ghost small show-more" id="fh-more">Show ' + Math.min(FULL_HISTORY_PAGE, hits.length - state.fullLimit) + ' more</button>' : '')
      : '<p class="empty">' + (all.length ? 'No event matches.' : 'Nothing logged yet.') + '</p>';
  }
  // tooltips for SVG marks
  const tip = document.createElement('div');
  tip.className = 'tip'; tip.hidden = true; tip.setAttribute('role', 'tooltip');
  document.body.appendChild(tip);
  document.addEventListener('pointermove', e => {
    const m = e.target instanceof Element ? e.target.closest('[data-tip]') : null;
    if (!m) { tip.hidden = true; return; }
    tip.textContent = m.getAttribute('data-tip');
    tip.hidden = false;
    const r = tip.getBoundingClientRect();
    tip.style.left = Math.min(window.innerWidth - r.width - 8, e.clientX + 12) + 'px';
    tip.style.top = Math.max(8, e.clientY - r.height - 10) + 'px';
  });
  let rz = 0;
  window.addEventListener('resize', () => { clearTimeout(rz); rz = setTimeout(drawCharts, 120); });

  // ---------------------------------------------------------------- masonry: the card grid without gaps
  // CSS grid with 4 px rows: each card spans as many rows as it is tall (+ the gap), and `dense` packing puts the next
  // card under the shortest column. A ResizeObserver keeps the spans right as cards change; nothing runs while idle.
  const grid = document.getElementById('cards');
  if (grid && typeof ResizeObserver === 'function') {
    const ROW = 4, GAP = 16;
    const ro = new ResizeObserver(entries => {
      for (const en of entries) {
        const el = en.target, h = el.getBoundingClientRect().height;
        const span = 'span ' + Math.max(1, Math.ceil((h + GAP) / ROW));
        if (el.style.gridRowEnd !== span) el.style.gridRowEnd = span;
      }
    });
    for (const c of grid.children) ro.observe(c);
    grid.classList.add('masonry');
  }

  if (EMBED) document.body.classList.add('embed');
  setInterval(tick, 1000);
  tick();
  poll(false);
})();
