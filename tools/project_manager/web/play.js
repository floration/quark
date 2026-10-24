/* The Play tab (play.html): GET /api/play (pm/play.py). Start a build, install / remove older GitHub releases, and
   write a play note when a game this page started has closed (the form opens by itself; the shell brings the window
   up on #note=<session id>). Vanilla JS, no build step, no external requests; every server value goes through esc(). */
(function () {
  'use strict';

  const Q = new URLSearchParams(location.search);
  const EMBED = Q.get('embed') === '1';
  const TZ = 'America/Chicago';
  const FAST_MS = 2500, SLOW_MS = 10000, HIDDEN_MS = 15000;
  const FIELDS = ['did', 'noticed', 'change', 'other'];

  const state = { v: null, error: null, timer: 0, sigs: {}, relFilter: 'all', dismissed: new Set(), noteId: null, busy: false };

  // ---------------------------------------------------------------- helpers
  const $ = (id) => document.getElementById(id);
  function esc(v) { return String(v == null ? '' : v).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])); }
  const arr = (v) => Array.isArray(v) ? v : [];
  const obj = (v) => (v && typeof v === 'object' && !Array.isArray(v)) ? v : {};
  const has = (v) => v !== null && v !== undefined && v !== '';
  function store(k, v) { try { if (v === undefined) return localStorage.getItem(k); if (v === null) localStorage.removeItem(k); else localStorage.setItem(k, v); } catch (e) { /* private / blocked storage */ } return null; }
  const fmtCache = {};
  function fmt(opts) {
    const k = JSON.stringify(opts);
    if (!fmtCache[k]) { try { fmtCache[k] = new Intl.DateTimeFormat('en-US', Object.assign({ timeZone: TZ }, opts)); } catch (e) { fmtCache[k] = new Intl.DateTimeFormat('en-US', opts); } }
    return fmtCache[k];
  }
  function ms(v) { if (!has(v)) return null; const t = typeof v === 'number' ? (v > 1e12 ? v : v * 1000) : Date.parse(String(v)); return isNaN(t) ? null : t; }
  function when(v) {
    const t = ms(v);
    if (t == null) return 'unknown';
    const day = (x) => fmt({ year: 'numeric', month: '2-digit', day: '2-digit' }).format(x);
    const time = fmt({ hour: 'numeric', minute: '2-digit' }).format(t);
    if (day(t) === day(Date.now())) return time;
    if (day(t) === day(Date.now() - 86400000)) return 'yesterday ' + time;
    return fmt({ month: 'short', day: 'numeric', year: 'numeric' }).format(t) + ', ' + time;
  }
  function dur(sec) {
    if (sec == null || !isFinite(sec)) return 'unknown';
    sec = Math.max(0, Math.round(sec));
    if (sec < 60) return sec + ' s';
    const m = Math.floor(sec / 60);
    return m < 60 ? m + ' min' : Math.floor(m / 60) + ' h ' + (m % 60) + ' min';
  }
  function size(b) { b = Number(b) || 0; return b >= 1e9 ? (b / 1e9).toFixed(2) + ' GB' : b >= 1e6 ? Math.round(b / 1e6) + ' MB' : b >= 1e3 ? Math.round(b / 1e3) + ' KB' : b + ' B'; }
  function pill(kind, text, title) { return '<span class="pill p-' + kind + '"' + (title ? ' title="' + esc(title) + '"' : '') + '><span class="pd"></span>' + esc(text) + '</span>'; }
  const STATUS_PILL = { open: 'warn', 'in-progress': 'run', done: 'good', fixed: 'good', told: 'good', answered: 'good', wontfix: 'idle' };
  function toast(msg, bad) {
    const t = $('toast'); t.textContent = msg; t.className = 'toast' + (bad ? ' bad' : ''); t.hidden = false;
    clearTimeout(toast.t); toast.t = setTimeout(() => { t.hidden = true; }, bad ? 7000 : 3500);
  }
  function setHTML(id, html) { if (state.sigs[id] === html) return; state.sigs[id] = html; $(id).innerHTML = html; }
  async function api(path, body) {
    const opts = body === undefined ? { cache: 'no-store' } : { method: 'POST', cache: 'no-store', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
    const r = await fetch(path, opts);
    let j = null;
    try { j = await r.json(); } catch (e) { /* not JSON */ }
    if (!r.ok) { const e = new Error((j && j.message) || 'HTTP ' + r.status); e.status = r.status; e.reason = j && j.reason; throw e; }
    return j;
  }
  function shellPoke() { try { if (window.quarkShell && window.quarkShell.playChanged) window.quarkShell.playChanged(); } catch (e) { /* older shell */ } }

  // ---------------------------------------------------------------- render
  function sessionById(id) { return arr(state.v && state.v.sessions).find(s => s.id === id) || null; }
  function renderBuilds() {
    const v = state.v;
    const list = arr(v.builds);
    const sess = arr(v.sessions);
    const cards = list.map(b => {
      const run = arr(b.running).map(sessionById).filter(Boolean);
      const meta = [];
      if (has(b.commit)) meta.push('<span class="mono">' + esc(String(b.commit).slice(0, 7)) + '</span>' + (b.dirty ? ' <span class="warn">dirty</span>' : ''));
      if (has(b.mtime)) meta.push('built ' + esc(when(b.mtime)));
      const last = sess.find(s => s.build === b.id && s.state !== 'running');
      const lines = arr(b.lines);
      return '<div class="build' + (b.playable ? '' : ' off') + (run.length ? ' running' : '') + '">' +
        '<div class="n"><span>' + esc(b.label) + '</span><span class="kind">' + (b.kind === 'version' ? 'older version' : 'game folder') + '</span></div>' +
        '<div class="v">' + (has(b.version) ? esc(b.version) : '<span class="muted">unknown version</span>') + '</div>' +
        (meta.length ? '<div class="m">' + meta.join(' &middot; ') + '</div>' : '') +
        (b.playable ? '' : '<div class="m">' + esc(b.missing || 'not playable') + '</div>') +
        (last ? '<div class="m">last played ' + esc(when(last.startedAt)) + ' for ' + esc(dur(last.durationSec)) + '</div>' : '') +
        (lines.length ? '<details><summary class="small muted">BUILD.txt</summary><pre class="small mono">' + lines.map(esc).join('\n') + '</pre></details>' : '') +
        '<div class="act">' +
          (b.playable ? '<button type="button" class="btn primary" data-play="' + esc(b.id) + '">Play</button>' : '') +
          (run.length ? pill('good', run.length > 1 ? run.length + ' running' : 'running ' + dur((Date.now() - (ms(run[0].startedAt) || Date.now())) / 1000)) : '') +
          (b.kind === 'version' ? '<span class="grow"></span><button type="button" class="btn small ghost danger" data-remove="' + esc(b.id.replace(/^versions\//, '')) + '" title="Delete this folder (Quark\\versions\\' + esc(b.label) + ')">Remove</button>' : '') +
        '</div></div>';
    });
    setHTML('builds', cards.join('') || '<p class="empty">No game folders found in Quark\\.</p>');
  }

  function renderPending() {
    const p = arr(state.v.pending);
    const el = $('sec-pending');
    el.hidden = !p.length;
    if (!p.length) return setHTML('sec-pending', '');
    setHTML('sec-pending', '<span><b>' + (p.length === 1 ? 'A play note is waiting' : p.length + ' play notes are waiting') + '</b> &middot; ' +
      esc(p[0].label || '') + ' ' + esc(p[0].version || '') + ', played ' + esc(dur(p[0].durationSec)) + ', closed ' + esc(when(p[0].endedAt)) + '</span>' +
      '<button type="button" class="btn primary" data-note="' + esc(p[0].id) + '">Write the note</button>');
  }

  function renderNotes() {
    const v = state.v, d = obj(v.docs), sy = obj(d.sync);
    let html = '<div class="card-head"><h2 id="h-notes">Play notes <span class="count">' + esc(v.openNotes || 0) + ' open</span></h2>' +
      '<span class="src">Claude Documents &middot; ' + esc(d.file || '12-play-notes.md') + '</span></div>';
    const bad = sy.ok === false || sy.unpushed > 0;
    const syncTxt = !d.present ? 'No Claude Documents folder on this PC: notes are kept in project-manager\\play-notes.jsonl.'
      : has(sy.error) ? sy.error : sy.at ? 'Up to date with GitHub (' + when(sy.pushedAt || sy.at) + ').' : 'Nothing sent yet.';
    html += '<div class="sync' + (bad ? ' bad' : '') + '"><span>' + esc(syncTxt) + '</span>' +
      (bad ? '<button type="button" class="btn small" id="sync-now">Try again</button>' : '') + '</div>';
    const notes = arr(v.notes);
    if (!notes.length) return setHTML('sec-notes', html + '<p class="empty">No notes yet. Play a build above; when the game closes this page asks what you did.</p>');
    html += '<ul class="notes">' + notes.map(n => {
      const f = obj(n.fields);
      const st = has(n.status) ? pill(STATUS_PILL[n.status] || 'idle', n.status, 'status in 12-play-notes.md (agents change it)') : pill('idle', 'not in the file yet');
      const parts = [['did', 'Did'], ['noticed', 'Noticed'], ['change', 'Wants changed'], ['other', 'Also']]
        .filter(([k]) => has(f[k])).map(([k, l]) => '<div class="f"><b>' + l + ':</b> ' + esc(f[k]) + '</div>');
      return '<li class="note"><div class="t"><span>' + esc(String(n.title || '').replace(/^Play note: /, '')) + '</span>' + st + '</div>' + parts.join('') +
        '<div class="w">' + esc(n.stamp || '') + ' &middot; played ' + esc(dur(n.durationSec)) + (has(n.args) ? ' &middot; <span class="mono">' + esc(n.args) + '</span>' : '') + ' &middot; ' + esc(n.where || '') + '</div></li>';
    }).join('') + '</ul>';
    setHTML('sec-notes', html);
  }

  const SESSION_STATE = { running: ['good', 'running'], needsNote: ['warn', 'note waiting'], noted: ['good', 'noted'], skipped: ['idle', 'skipped'] };
  function renderSessions() {
    const list = arr(state.v.sessions).slice(0, 15);
    let html = '<div class="card-head"><h2 id="h-sessions">Recent games</h2><span class="src">project-manager\\play-sessions.json</span></div>';
    if (!list.length) return setHTML('sec-sessions', html + '<p class="empty">Nothing played from here yet.</p>');
    html += '<table class="t"><thead><tr><th>Build</th><th>Started</th><th class="num">Played</th><th>State</th><th></th></tr></thead><tbody>' + list.map(s => {
      const st = SESSION_STATE[s.state] || ['idle', s.state || '?'];
      const code = s.exitCode != null && s.exitCode !== 0 ? ' <span class="warn small" title="exit code">(' + esc(s.exitCode) + ')</span>' : '';
      const played = s.state === 'running' ? dur((Date.now() - (ms(s.startedAt) || Date.now())) / 1000) : dur(s.durationSec);
      return '<tr><td>' + esc(s.label) + ' <span class="muted small">' + esc(s.version || '') + '</span>' + (has(s.args) ? '<div class="small mono muted">' + esc(s.args) + '</div>' : '') + '</td>' +
        '<td class="nowrap">' + esc(when(s.startedAt)) + '</td><td class="num">' + esc(played) + (s.endApprox ? '?' : '') + '</td><td>' + pill(st[0], st[1]) + code + '</td>' +
        '<td>' + (s.state === 'needsNote' || s.state === 'skipped' ? '<button type="button" class="btn small" data-note="' + esc(s.id) + '">Write note</button>' : '') + '</td></tr>';
    }).join('') + '</tbody></table>';
    setHTML('sec-sessions', html);
  }

  function renderReleases() {
    const v = state.v, rel = obj(v.releases), inst = obj(v.installs);
    const seg = ['all', 'production', 'testing'].map(f => '<button type="button" data-filter="' + f + '"' + (state.relFilter === f ? ' class="on"' : '') + '>' + f[0].toUpperCase() + f.slice(1) + '</button>').join('');
    let html = '<div class="card-head"><h2 id="h-releases">Older versions <span class="count">' + arr(rel.list).length + '</span></h2>' +
      '<span class="rel-tools"><span class="seg">' + seg + '</span><span class="src">GitHub releases &middot; ' + (rel.busy ? 'fetching&hellip;' : rel.fetchedAt ? 'checked ' + esc(when(rel.fetchedAt)) : 'not fetched yet') + '</span>' +
      '<button type="button" class="btn small" id="rel-refresh"' + (rel.busy ? ' disabled' : '') + '>Refresh</button></span></div>';
    if (rel.ok === false) html += '<div class="down"><div><strong>Could not get the release list</strong><div class="err">' + esc(rel.error) + '</div></div></div>';
    const list = arr(rel.list).filter(r => state.relFilter === 'all' || r.channel === state.relFilter);
    if (!list.length) return setHTML('sec-releases', html + (rel.ok === false ? '' : '<p class="empty">' + (rel.busy || rel.ok == null ? 'Loading the list from GitHub&hellip;' : 'No releases.') + '</p>'));
    html += '<div class="rel-wrap"><table class="t"><thead><tr><th>Release</th><th>Channel</th><th>Published</th><th class="num">Size</th><th>On this PC</th></tr></thead><tbody>' + list.map(r => {
      const i = obj(inst[r.tag]);
      let act;
      if (['downloading', 'verifying', 'extracting'].includes(i.state)) {
        const f = i.total ? Math.min(1, (i.got || 0) / i.total) : 0;
        act = '<span class="prog"><i style="width:' + (f * 100).toFixed(1) + '%"></i></span> <span class="small muted">' + esc(i.state === 'downloading' ? Math.round(f * 100) + '% of ' + size(i.total) : i.state + '…') + '</span>';
      } else if (r.installed) {
        act = '<button type="button" class="btn small primary" data-play="versions/' + esc(r.tag) + '">Play</button> <button type="button" class="btn small ghost danger" data-remove="' + esc(r.tag) + '">Remove</button>' +
          (i.state === 'done' && i.verified === false ? ' <span class="warn small" title="' + esc(i.note || '') + '">not checked</span>' : '');
      } else if (!r.zip) act = '<span class="muted small">no Windows zip</span>';
      else act = '<button type="button" class="btn small" data-install="' + esc(r.tag) + '">Install</button>' + (i.state === 'failed' ? ' <span class="err small">' + esc(i.error) + '</span>' : '');
      const ch = r.channel === 'production' ? pill('good', 'production') : r.channel === 'testing' ? pill('run', 'testing') : pill('idle', r.channel);
      return '<tr' + (r.zip ? '' : ' class="off"') + '><td><span class="mono">' + esc(r.tag) + '</span>' + (r.name && r.name !== r.tag ? ' <span class="muted small">' + esc(r.name) + '</span>' : '') + '</td>' +
        '<td>' + ch + '</td><td class="nowrap">' + esc(when(r.publishedAt)) + '</td><td class="num">' + (r.zip ? esc(size(r.zip.size)) : '') + '</td><td class="nowrap">' + act + '</td></tr>';
    }).join('') + '</tbody></table></div>';
    setHTML('sec-releases', html);
  }

  function renderStatus() {
    const el = $('status-text'), line = $('statusline');
    let txt = 'Loading…', bad = false;
    if (state.error) { txt = 'The Supervisor Board server is not answering (' + state.error + ').'; bad = true; }
    else if (state.v) {
      const n = Number(state.v.running) || 0;
      txt = (n ? n + (n === 1 ? ' game running' : ' games running') + ' · ' : '') + 'Games start hidden from any console, from their own folder; when one closes this page asks for a note.';
    }
    el.textContent = txt;
    line.classList.toggle('bad', bad);
  }

  function renderAll() {
    if (!state.v) return renderStatus();
    for (const fn of [renderPending, renderBuilds, renderNotes, renderSessions, renderReleases]) {
      try { fn(); } catch (err) { if (window.console) console.error(err); }
    }
    renderStatus();
    closeStaleNote();
    maybeOpenNote();
  }

  // ---------------------------------------------------------------- the note form
  const dlg = $('note-dlg'), form = $('note-form');
  function wantedNote() {
    const m = /(?:^|[#&])note=([^&]+)/.exec(location.hash);
    return m ? decodeURIComponent(m[1]) : null;
  }
  function maybeOpenNote() {
    if (dlg.open || !state.v) return;
    const pend = arr(state.v.pending);
    const want = wantedNote();
    const s = want ? sessionById(want) : null;
    if (s && (s.state === 'needsNote' || s.state === 'skipped') && !state.dismissed.has(s.id)) return openNote(s.id);
    const next = pend.find(p => !state.dismissed.has(p.id));
    if (next) openNote(next.id);
  }
  // the open form's session was noted or skipped somewhere else (another page, the API): close it, say so
  function closeStaleNote() {
    if (!dlg.open || !state.noteId || state.busy) return;
    const s = sessionById(state.noteId);
    const gone = !s || s.state === 'noted' || (state.noteFrom === 'needsNote' && s.state !== 'needsNote');
    if (!gone) return;
    const what = s && s.state === 'noted' ? 'already saved' : s && s.state === 'skipped' ? 'already skipped' : 'gone';
    store('play-draft-' + state.noteId, null);
    closeNote(false);
    toast('That play note was ' + what + ' (from another window).');
  }
  // Save / Skip answered 409 for a stale session: close the form, nothing is written twice
  function staleAnswer(err) {
    if (err.status !== 409 || !['already-noted', 'running'].includes(err.reason)) return false;
    store('play-draft-' + state.noteId, null);
    closeNote(false);
    toast(err.reason === 'already-noted' ? 'That play note was already saved.' : err.message);
    poll();
    return true;
  }
  function openNote(id) {
    const s = sessionById(id);
    if (!s) return;
    state.noteId = id;
    state.noteFrom = s.state;
    const pend = arr(state.v.pending);
    const idx = pend.findIndex(p => p.id === id);
    $('note-count').textContent = pend.length > 1 && idx >= 0 ? (idx + 1) + ' of ' + pend.length + ' waiting' : '';
    $('note-meta').innerHTML = '<span><b>' + esc(s.label) + '</b> ' + esc(s.version || '') + (s.commit ? ' <span class="mono">' + esc(String(s.commit).slice(0, 7)) + '</span>' : '') + '</span>' +
      '<span>played <b>' + esc(dur(s.durationSec)) + '</b>' + (s.endApprox ? ' or less' : '') + '</span><span>closed ' + esc(when(s.endedAt)) + '</span>' +
      (has(s.args) ? '<span class="mono">' + esc(s.args) + '</span>' : '') +
      (s.exitCode != null && s.exitCode !== 0 ? '<span class="warn">exit code ' + esc(s.exitCode) + '</span>' : '');
    let draft = {};
    try { draft = JSON.parse(store('play-draft-' + id) || '{}') || {}; } catch (e) { draft = {}; }
    for (const k of FIELDS) form.elements[k].value = typeof draft[k] === 'string' ? draft[k] : '';
    $('note-err').hidden = true;
    $('note-skip').hidden = s.state !== 'needsNote';
    try { dlg.showModal(); } catch (e) { dlg.setAttribute('open', ''); }
    setTimeout(() => form.elements.did.focus(), 30);
  }
  function closeNote(dismiss) {
    if (dismiss && state.noteId) state.dismissed.add(state.noteId);
    state.noteId = null; state.noteFrom = null;
    if (/note=/.test(location.hash)) history.replaceState(null, '', location.pathname + location.search);
    if (dlg.open) dlg.close();
  }
  form.addEventListener('input', () => {
    if (!state.noteId) return;
    const d = {};
    for (const k of FIELDS) d[k] = form.elements[k].value;
    store('play-draft-' + state.noteId, JSON.stringify(d));
  });
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    if (!state.noteId || state.busy) return;
    const body = { session: state.noteId };
    for (const k of FIELDS) body[k] = form.elements[k].value;
    state.busy = true; $('note-save').disabled = true;
    try {
      const j = await api('/api/play/note', body);
      store('play-draft-' + state.noteId, null);
      closeNote(false);
      toast(j.message || 'Saved.');
      shellPoke();
      poll();
    } catch (err) {
      if (!staleAnswer(err)) { const el = $('note-err'); el.textContent = err.message; el.hidden = false; }
    } finally { state.busy = false; $('note-save').disabled = false; }
  });
  form.addEventListener('keydown', (e) => { if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); form.requestSubmit(); } });
  dlg.addEventListener('cancel', (e) => { e.preventDefault(); closeNote(true); });  // (Esc = Later)
  $('note-later').addEventListener('click', () => closeNote(true));
  $('note-skip').addEventListener('click', async () => {
    if (!state.noteId) return;
    try {
      const j = await api('/api/play/skip', { session: state.noteId });
      store('play-draft-' + state.noteId, null); closeNote(false);
      if (j && j.session && j.session.noop) toast('That play note was already skipped.');
      shellPoke(); poll();
    } catch (err) { if (!staleAnswer(err)) { const el = $('note-err'); el.textContent = err.message; el.hidden = false; } }
  });
  window.addEventListener('hashchange', () => {
    const id = wantedNote();
    if (id) { state.dismissed.delete(id); if (!dlg.open) maybeOpenNote(); }
  });

  // ---------------------------------------------------------------- actions
  const args = $('args');
  args.value = store('play-args') || '';
  args.addEventListener('input', () => store('play-args', args.value));
  document.addEventListener('click', async (e) => {
    const t = e.target instanceof Element ? e.target : null;
    if (!t) return;
    const b = t.closest('button');
    if (!b) return;
    try {
      if (b.dataset.play) {
        b.disabled = true;
        const j = await api('/api/play/launch', { build: b.dataset.play, args: args.value });
        toast('Started ' + (j.session.label || '') + (j.session.args ? ' with ' + j.session.args : '') + '.');
        shellPoke();
        poll();
      } else if (b.dataset.install) {
        b.disabled = true;
        await api('/api/play/install', { tag: b.dataset.install });
        poll();
      } else if (b.dataset.remove) {
        if (!confirm('Remove ' + b.dataset.remove + '?\n\nThis deletes the folder Quark\\versions\\' + b.dataset.remove + ' (you can install it again).')) return;
        await api('/api/play/remove', { tag: b.dataset.remove });
        toast('Removed ' + b.dataset.remove + '.');
        poll();
      } else if (b.dataset.note) {
        state.dismissed.delete(b.dataset.note);
        openNote(b.dataset.note);
      } else if (b.dataset.filter) {
        state.relFilter = b.dataset.filter; store('play-filter', state.relFilter); renderReleases();
      } else if (b.id === 'rel-refresh') {
        b.disabled = true; await api('/api/play?refresh=1').then(apply); setTimeout(poll, 1500);
      } else if (b.id === 'sync-now') {
        b.disabled = true; await api('/api/play/sync', {}); setTimeout(poll, 2500);
      }
    } catch (err) {
      toast(err.message, true);
      b.disabled = false;
    }
  });

  // ---------------------------------------------------------------- polling
  function apply(v) { state.v = obj(v); state.error = null; renderAll(); }
  function busy() {
    const v = state.v;
    if (!v) return true;
    return Number(v.running) > 0 || arr(v.pending).length > 0 || obj(v.releases).busy ||
      Object.values(obj(v.installs)).some(i => ['downloading', 'verifying', 'extracting'].includes(i.state));
  }
  async function poll() {
    clearTimeout(state.timer);
    try { apply(await api('/api/play')); }
    catch (err) { state.error = err.message; renderStatus(); }
    state.timer = setTimeout(poll, document.hidden ? HIDDEN_MS : busy() ? FAST_MS : SLOW_MS);
  }
  document.addEventListener('visibilitychange', () => { if (!document.hidden) poll(); });
  setInterval(() => {
    const c = $('clock');
    if (c) c.textContent = fmt({ weekday: 'short', hour: 'numeric', minute: '2-digit', timeZoneName: 'short' }).format(Date.now()) + ' Dallas';
  }, 1000);

  state.relFilter = ['all', 'production', 'testing'].includes(store('play-filter')) ? store('play-filter') : 'all';
  if (EMBED) document.body.classList.add('embed');
  poll();
})();
