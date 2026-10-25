/* The Play tab (play.html): GET /api/play (pm/play.py). Start a build (launch options remembered per build), stop it,
   read its log, install / cancel / remove older GitHub releases (checked like the launcher), and write a play note when
   a game this page started has closed (the form opens by itself; the shell brings the window up on #note=<session id>),
   or without a game, and edit / remove a note while no agent has taken it. Vanilla JS, no build step, no external
   requests; every server value goes through esc(). */
(function () {
  'use strict';

  const Q = new URLSearchParams(location.search);
  const EMBED = Q.get('embed') === '1';
  const TZ = 'America/Chicago';
  const FAST_MS = 2500, SLOW_MS = 10000, HIDDEN_MS = 15000;
  const FIELDS = ['did', 'noticed', 'change', 'other'];
  const FIELD_LABELS = [['did', 'Did'], ['noticed', 'Noticed'], ['change', 'Wants changed'], ['other', 'Also']];
  const ACTIVE = ['downloading', 'verifying', 'extracting'];
  const MAX_FIELD = 4000;
  const RECENT_MAX = 10;
  // a few starting points for the options box (src/client/main.cpp flags); the boxes' own history comes first
  const PRESETS = ['--host ffa --map bastion', '--host duel --map clockwork', '--host koth --map summit', '--perf', '--editor bastion'];
  const CLAUDE_MANAGER = 'http://127.0.0.1:8766/#file=';
  const WIDE = window.matchMedia ? window.matchMedia('(min-width: 1800px)') : { matches: false };

  const state = {
    v: null, error: null, timer: 0, sigs: {}, relFilter: 'all', relList: [], relSig: null, refreshUntil: 0,
    later: new Set(), note: null, busy: false, openNotes: new Set(), logSession: null,
  };

  // ---------------------------------------------------------------- helpers
  const $ = (id) => document.getElementById(id);
  function esc(v) { return String(v == null ? '' : v).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])); }
  const arr = (v) => Array.isArray(v) ? v : [];
  const obj = (v) => (v && typeof v === 'object' && !Array.isArray(v)) ? v : {};
  const has = (v) => v !== null && v !== undefined && v !== '';
  function store(k, v) { try { if (v === undefined) return localStorage.getItem(k); if (v === null) localStorage.removeItem(k); else localStorage.setItem(k, v); } catch (e) { /* private / blocked storage */ } return null; }
  function storeJSON(k, dflt) { try { const x = JSON.parse(store(k) || 'null'); return x == null ? dflt : x; } catch (e) { return dflt; } }
  function session(k, v) { try { if (v === undefined) return sessionStorage.getItem(k); sessionStorage.setItem(k, v); } catch (e) { /* blocked */ } return null; }
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
  // how a game ended (pm/play.py exit_info): a crash in red, an error exit in yellow, stopped from here in grey
  const EXIT_PILL = { crash: ['crit', 'crashed'], error: ['warn', 'error exit'], stopped: ['idle', 'stopped'], unknown: ['idle', 'exit unknown'] };
  function exitPill(x) { x = obj(x); const p = EXIT_PILL[x.kind]; return p ? pill(p[0], p[1], x.text) : ''; }
  const CHECK_PILL = {
    signed: ['good', 'signed', 'Its signature and signed release manifest verified against the release key (like the launcher)'],
    sha256: ['idle', 'unsigned', 'From before signing (1.16.x or older): matched its SHA256SUMS; there is no signature to check'],
    none: ['warn', 'not checked', 'From before signing (1.16.x or older): no signature and no SHA256SUMS to check it against'],
  };
  function checkPill(c, note) { const p = CHECK_PILL[c]; return p ? pill(p[0], p[1], note || p[2]) : ''; }
  function toast(msg, bad) {
    const t = $('toast'); t.textContent = msg; t.className = 'toast' + (bad ? ' bad' : ''); t.hidden = false;
    clearTimeout(toast.t); toast.t = setTimeout(() => { t.hidden = true; }, bad ? 7000 : 3500);
  }
  function setHTML(id, html) { if (state.sigs[id] === html) return false; state.sigs[id] = html; $(id).innerHTML = html; return true; }
  async function api(path, body) {
    const opts = body === undefined ? { cache: 'no-store' } : { method: 'POST', cache: 'no-store', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
    let r;
    try { r = await fetch(path, opts); } catch (e) {
      const d = new Error('The Supervisor Board server is not answering, so nothing was done.'); d.down = true; d.detail = e.message; throw d;
    }
    let j = null;
    try { j = await r.json(); } catch (e) { /* not JSON */ }
    if (!r.ok) { const e = new Error((j && j.message) || 'HTTP ' + r.status); e.status = r.status; e.reason = j && j.reason; throw e; }
    return j;
  }
  function shellPoke() { try { if (window.quarkShell && window.quarkShell.playChanged) window.quarkShell.playChanged(); } catch (e) { /* older shell */ } }
  function sessionById(id) { return arr(state.v && state.v.sessions).find(s => s.id === id) || null; }
  function noteById(id) { return arr(state.v && state.v.notes).find(n => n.id === id) || null; }

  // ---------------------------------------------------------------- launch options (per build, plus recent ones)
  let argsBy = storeJSON('play-args-by-build', null);
  let recent = arr(storeJSON('play-args-recent', []));
  const legacyArgs = store('play-args');  // (before: one box for every build; it becomes each build's starting value)
  if (!argsBy) argsBy = {};
  function saveArgs() { store('play-args-by-build', JSON.stringify(argsBy)); }
  function argsFor(id) { return typeof argsBy[id] === 'string' ? argsBy[id] : (legacyArgs || ''); }
  function remember(text) {
    text = String(text || '').trim();
    if (!text) return;
    recent = [text].concat(recent.filter(x => x !== text)).slice(0, RECENT_MAX);
    store('play-args-recent', JSON.stringify(recent));
    renderOptsList();
  }
  function renderOptsList() {
    const seen = new Set();
    $('opts-list').innerHTML = recent.concat(PRESETS).filter(x => !seen.has(x) && seen.add(x)).map(x => '<option value="' + esc(x) + '"></option>').join('');
  }

  // ---------------------------------------------------------------- render: builds (keyed: each card's options box
  // stays the same element across polls, so typing in it is never interrupted)
  const cards = new Map();
  function makeCard(b) {
    const el = document.createElement('div');
    el.className = 'build';
    el.innerHTML = '<div class="bd"></div><input type="text" class="opt" list="opts-list" spellcheck="false" autocomplete="off" maxlength="400" placeholder="launch options">' +
      '<div class="pick" hidden><select class="pk-mode" aria-label="Mode (--host)" title="--host: the mode to start a match in"></select><select class="pk-map" aria-label="Map (--map)" title="--map: maps from this build\'s maps folder"></select></div>' +
      '<div class="act"></div>';
    const c = { el, bd: el.firstChild, input: el.children[1], pick: el.children[2], act: el.lastChild, sb: null, sa: null, mapsSig: null, id: b.id };
    c.mode = c.pick.firstChild; c.map = c.pick.lastChild;
    c.mode.addEventListener('change', () => pickChanged(c, 'mode'));
    c.map.addEventListener('change', () => pickChanged(c, 'map'));
    c.input.value = argsFor(b.id);
    c.input.dataset.build = b.id;
    c.input.setAttribute('aria-label', 'Launch options for ' + b.label);
    return c;
  }
  function renderBuilds() {
    const v = state.v, host = $('builds');
    const list = arr(v.builds), sess = arr(v.sessions);
    const empty = host.querySelector('.empty');
    if (!list.length) { cards.clear(); host.innerHTML = '<p class="empty">No game folders found in Quark\\.</p>'; return; }
    if (empty) empty.remove();
    const keep = new Set();
    list.forEach((b, i) => {
      keep.add(b.id);
      let c = cards.get(b.id);
      if (!c) { c = makeCard(b); cards.set(b.id, c); }
      const run = arr(b.running).map(sessionById).filter(Boolean);
      const meta = [];
      if (has(b.tag) && b.tag !== b.label) meta.push('<span class="mono">' + esc(b.tag) + '</span>');
      if (has(b.commit)) meta.push('<span class="mono">' + esc(String(b.commit).slice(0, 7)) + '</span>' + (b.dirty ? ' <span class="warn">dirty</span>' : ''));
      if (has(b.mtime)) meta.push('built ' + esc(when(b.mtime)));
      const last = sess.find(s => s.build === b.id && s.state !== 'running');
      const lines = arr(b.lines);
      const bd = '<div class="n"><span>' + esc(b.label) + '</span><span class="kind">' + (b.kind === 'version' ? 'older version' : 'game folder') + '</span></div>' +
        '<div class="v">' + (has(b.version) ? esc(b.version) : '<span class="muted">unknown version</span>') + (b.kind === 'version' ? ' ' + (checkPill(b.check, b.checkNote) || pill('idle', 'check not recorded', 'Installed before the Play tab recorded how it checked a download')) : '') + '</div>' +
        (meta.length ? '<div class="m">' + meta.join(' &middot; ') + '</div>' : '') +
        (b.playable ? '' : '<div class="m">' + esc(b.missing || 'not playable') + '</div>') +
        (last ? '<div class="m">last played ' + esc(when(last.startedAt)) + ' for ' + esc(dur(last.durationSec)) + ' ' + exitPill(last.exit) + '</div>' : '') +
        (lines.length ? '<details><summary class="small muted">BUILD.txt</summary><pre class="small mono">' + lines.map(esc).join('\n') + '</pre></details>' : '');
      const r0 = run[0];
      const act = (b.playable ? '<button type="button" class="btn primary" data-play="' + esc(b.id) + '">Play</button>' : '') +
        (run.length ? pill('good', run.length > 1 ? run.length + ' running' : 'running ' + dur((Date.now() - (ms(r0.startedAt) || Date.now())) / 1000)) +
          (r0.stopping ? ' <span class="small muted">stopping&hellip;</span>' : ' <button type="button" class="btn small" data-stop="' + esc(r0.id) + '" title="Ask the game to close; it is ended after 5 s if it doesn\'t">Stop</button>') : '') +
        '<span class="grow"></span>' +
        (b.present ? '<button type="button" class="btn small ghost" data-open="build" data-build="' + esc(b.id) + '" title="Open ' + esc(b.folder) + '">Folder</button>' : '') +
        (b.kind === 'version' ? '<button type="button" class="btn small ghost danger" data-remove="' + esc(b.id.replace(/^versions\//, '')) + '" title="Delete this folder (Quark\\versions\\' + esc(b.label) + ')">Remove</button>' : '');
      if (c.sb !== bd) { c.sb = bd; c.bd.innerHTML = bd; }
      if (c.sa !== act) { c.sa = act; c.act.innerHTML = act; }
      c.el.className = 'build' + (b.playable ? '' : ' off') + (run.length ? ' running' : '');
      c.input.hidden = !b.playable;
      if (c.mapsSig !== (b.playable ? b.maps : null)) {
        c.mapsSig = b.playable ? b.maps : null;
        c.pick.hidden = !c.mapsSig;
        if (c.mapsSig) loadMaps(b.id, c.mapsSig).then(() => fillPick(c));
      }
      if (host.children[i] !== c.el) host.insertBefore(c.el, host.children[i] || null);
    });
    for (const [id, c] of cards) if (!keep.has(id)) { c.el.remove(); cards.delete(id); }
  }

  // ---------------------------------------------------------------- mode and map pickers: they write --host / --map
  // into the options box (and show what it says); the lists come from the build's own maps folder, fetched once per set
  const mapSets = {};   // sig -> {maps, modes, titles} (or the promise while it loads)
  function loadMaps(id, sig) {
    if (!mapSets[sig]) {
      mapSets[sig] = api('/api/play/maps?build=' + encodeURIComponent(id)).then(j => { mapSets[sig] = j; return j; },
        () => { delete mapSets[sig]; return null; });
    }
    return Promise.resolve(mapSets[sig]);
  }
  const ARG_RE = (flag) => new RegExp('(^|\\s)' + flag + '\\s+("[^"]*"|\\S+)');
  function getArg(text, flag) { const m = ARG_RE(flag).exec(text); return m ? m[2].replace(/^"|"$/g, '') : ''; }
  function setArg(text, flag, val) {
    const re = ARG_RE(flag);
    if (!val) return text.replace(re, '').replace(/\s{2,}/g, ' ').trim();
    return re.test(text) ? text.replace(re, (m, pre) => pre + flag + ' ' + val) : (text.trim() + ' ' + flag + ' ' + val).trim();
  }
  function fillPick(c) {
    const set = mapSets[c.mapsSig];
    if (!set || typeof set.then === 'function') return;
    const text = c.input.value, mode = getArg(text, '--host').toLowerCase(), map = getArg(text, '--map');
    const titles = obj(set.titles), maps = arr(set.maps);
    const opt = (v, t, sel) => '<option value="' + esc(v) + '"' + (sel ? ' selected' : '') + '>' + esc(t) + '</option>';
    const modes = arr(set.modes).slice();
    if (mode && !modes.includes(mode)) modes.push(mode);  // (typed by hand: shown as it is)
    const mh = opt('', 'mode', !mode) + modes.map(m => opt(m, m + (titles[m] && titles[m].toLowerCase() !== m ? ' \u00b7 ' + titles[m].toLowerCase() : ''), m === mode)).join('');
    const label = (m) => m.name + (m.title && m.title.toLowerCase() !== m.name ? ' (' + m.title + ')' : '') + (m.handmade ? ' \u00b7 hand-made' : '');
    let gh;
    if (mode) {
      const built = maps.filter(m => m.for === mode), offered = maps.filter(m => m.for !== mode && arr(m.modes).includes(mode));
      const other = maps.filter(m => !arr(m.modes).includes(mode));
      const grp = (t, l) => l.length ? '<optgroup label="' + esc(t) + '">' + l.map(m => opt(m.name, label(m), m.name === map)).join('') + '</optgroup>' : '';
      gh = grp('Built for ' + mode, built) + grp('Also offered for ' + mode, offered) + grp('Other maps (may not work in ' + mode + ')', other);
    } else gh = maps.map(m => opt(m.name, label(m) + (m.for ? ' \u00b7 ' + m.for : ''), m.name === map)).join('');
    if (map && !maps.some(m => m.name === map)) gh += opt(map, map + ' (not in this build)', true);
    const html = mh + '\u0000' + opt('', 'map', !map) + gh;
    if (c.pickHTML === html) return;
    c.pickHTML = html;
    const [a, b] = html.split('\u0000');
    c.mode.innerHTML = a; c.map.innerHTML = b;
  }
  function pickChanged(c, which) {
    const set = obj(mapSets[c.mapsSig]), maps = arr(set.maps);
    let text = c.input.value;
    if (which === 'mode') {
      const mode = c.mode.value;
      text = setArg(text, '--host', mode);
      const cur = maps.find(m => m.name === getArg(text, '--map'));
      // a new mode: its own map, the way the game's menus pick it, unless the chosen one is offered for it too
      if (mode && !(cur && arr(cur.modes).includes(mode))) {
        const own = maps.find(m => m.for === mode) || maps.find(m => arr(m.modes).includes(mode));
        if (own) text = setArg(text, '--map', own.name);
      }
    } else {
      const name = c.map.value, m = maps.find(x => x.name === name);
      text = setArg(text, '--map', name);
      if (name && !getArg(text, '--host') && m && m.for) text = setArg(text, '--host', m.for);  // (--map alone starts nothing)
    }
    c.input.value = text;
    c.input.dispatchEvent(new Event('input', { bubbles: true }));  // (remembered for this build, like typing)
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
    const inShell = !!(window.quarkShell && typeof window.quarkShell.open === 'function');
    const file = d.file || '12-play-notes.md';
    let html = '<div class="card-head"><h2 id="h-notes">Play notes <span class="count">' + esc(v.openNotes || 0) + ' open</span></h2>' +
      '<span class="rel-tools"><span class="src">Claude Documents &middot; <a href="' + esc(CLAUDE_MANAGER + encodeURIComponent(file)) + '"' + (inShell ? '' : ' target="_blank" rel="noopener"') +
      ' data-tab="docs" data-hash="file=' + esc(encodeURIComponent(file)) + '" title="Every note, in the Claude Manager tab">' + esc(file) + '</a></span>' +
      (d.filePresent ? '<button type="button" class="btn small ghost" data-open="notes-file" title="Open ' + esc(file) + ' in its default app">Open file</button>' : '') +
      '<button type="button" class="btn small" id="note-add" title="A note for a game played some other way (the launcher, a server)">Add a note</button></span></div>';
    const bad = d.present && (sy.ok === false || sy.unpushed > 0);
    const syncTxt = !d.present ? 'No Claude Documents folder on this PC: notes are kept in project-manager\\play-notes.jsonl.'
      : has(sy.error) ? sy.error : sy.at ? 'Up to date with GitHub (' + when(sy.pushedAt || sy.at) + ').' : 'Nothing sent yet.';
    html += '<div class="sync' + (bad || !d.present ? ' bad' : '') + '"><span>' + esc(syncTxt) + '</span>' +
      (bad ? '<button type="button" class="btn small" id="sync-now">Try again</button>' : '') + '</div>';
    // edits and removals that never reached the file (an agent took the entry first): every one, until dismissed
    const blocked = arr(v.blocked);
    if (blocked.length) {
      html += '<div class="blocked"><b>' + (blocked.length === 1 ? 'A change of yours didn\'t reach ' : blocked.length + ' changes of yours didn\'t reach ') + esc(file) + '</b>' +
        '<span class="small muted"> An agent took the entry before it went in; it is tried again if the entry is set back to open.</span><ul>' +
        blocked.map(x => '<li><span><span class="mono small">' + esc(x.stamp || '') + '</span> ' + esc(String(x.title || '').replace(/^Play note: /, '')) + ': your ' + esc(x.change) + ' wasn\'t applied (' + esc(x.why) + ')</span>' +
          '<span class="grow"></span>' + blockedButtons(x.id)).join('') + '</ul></div>';
    }
    const notes = arr(v.notes);
    if (!notes.length) return setHTML('sec-notes', html + '<p class="empty">No notes yet. Play a build above; when the game closes this page asks what you did.</p>');
    html += '<ul class="notes">' + notes.map(n => {
      const f = obj(n.fields);
      const st = has(n.status) ? pill(STATUS_PILL[n.status] || 'idle', n.status, 'status in 12-play-notes.md (agents change it)') : pill('idle', d.present ? 'not in the file yet' : 'on this PC');
      const parts = FIELD_LABELS.filter(([k]) => has(f[k])).map(([k, l]) => '<div class="f"><b>' + l + ':</b> ' + esc(f[k]) + '</div>');
      const open = state.openNotes.has(n.id);
      const acts = n.removed === 'pending' ? '<span class="small muted">removing&hellip;</span>'
        : n.blocked ? blockedButtons(n.id)
        : n.locked ? '<span class="small muted" title="Edits and removals stop once an agent has the entry">' + esc(n.locked) + '</span>'
        : '<button type="button" class="btn small ghost" data-edit="' + esc(n.id) + '">Edit</button><button type="button" class="btn small ghost danger" data-del="' + esc(n.id) + '">Remove</button>';
      return '<li class="note' + (open ? ' open' : '') + (n.removed ? ' removed-' + esc(n.removed) : '') + '" data-id="' + esc(n.id) + '"' + (has(n.logTail) ? ' data-tail="1"' : '') + '><div class="t"><span>' + esc(String(n.title || '').replace(/^Play note: /, '')) + '</span>' + st + '</div>' + parts.join('') +
        (has(n.logTail) && open ? '<pre class="tail small mono">' + esc(n.logTail) + '</pre>' : '') +
        '<div class="w">' + esc(n.stamp || '') + (n.freeform ? ' &middot; played outside the board' : ' &middot; played ' + esc(dur(n.durationSec))) + ' ' + exitPill(n.exit) +
        (has(n.args) ? ' &middot; <span class="mono">' + esc(n.args) + '</span>' : '') + (n.editedAt ? ' &middot; edited' : '') + ' &middot; ' + esc(n.where || '') + '</div>' +
        '<div class="na"><button type="button" class="btn small ghost" data-more="' + esc(n.id) + '"' + (open || has(n.logTail) ? '' : ' hidden') + '>' + (open ? 'less' : 'more' + (has(n.logTail) ? ' (and the log lines)' : '')) + '</button><span class="grow"></span>' + acts + '</div></li>';
    }).join('') + '</ul>';
    if ((v.notesTotal || 0) > notes.length) html += '<p class="small muted more-notes">The newest ' + notes.length + ' of ' + esc(v.notesTotal) + '; every note is in ' + esc(file) + ' (Claude Manager) and project-manager\\play-notes.jsonl.</p>';
    if (setHTML('sec-notes', html)) {
      measureNotes();
      for (const n of notes) if (n.locked || n.removed) store('play-draft-edit-' + n.id, null);  // (no edit to finish)
    }
  }
  function blockedButtons(id) {
    return '<button type="button" class="btn small" data-follow="' + esc(id) + '" title="Add a new note that refers to this one">Write a follow-up</button>' +
      '<button type="button" class="btn small ghost" data-dismiss="' + esc(id) + '" title="Seen: take it off this list (it is not tried again)">Dismiss</button>';
  }
  // "more" only where a box really is cut: measured after rendering (a 200-character box with one long line wraps past
  // the 4-line clamp, a 300-character one on a wide card may not), and again when the cards change width
  function measureNotes() {
    for (const li of document.querySelectorAll('#sec-notes li.note:not(.open)')) {
      const b = li.querySelector('[data-more]');
      if (b) b.hidden = !li.dataset.tail && !Array.from(li.querySelectorAll('.f')).some(f => f.scrollHeight > f.clientHeight + 1);
    }
  }
  if (window.ResizeObserver) {
    let w = 0, raf = 0;
    new ResizeObserver((es) => {
      const nw = Math.round(es[0].contentRect.width);
      if (nw === w) return;
      w = nw; cancelAnimationFrame(raf); raf = requestAnimationFrame(measureNotes);
    }).observe($('sec-notes'));
  }

  const SESSION_STATE = { running: ['good', 'running'], needsNote: ['warn', 'note waiting'], noted: ['good', 'noted'], skipped: ['idle', 'skipped'] };
  function renderSessions() {
    const list = arr(state.v.sessions).slice(0, 15);
    let html = '<div class="card-head"><h2 id="h-sessions">Recent games</h2><span class="src">project-manager\\play-sessions.json</span></div>';
    if (!list.length) return setHTML('sec-sessions', html + '<p class="empty">Nothing played from here yet.</p>');
    html += '<table class="t"><thead><tr><th>Build</th><th>Started</th><th class="num">Played</th><th>State</th><th></th></tr></thead><tbody>' + list.map(s => {
      const st = SESSION_STATE[s.state] || ['idle', s.state || '?'];
      const played = s.state === 'running' ? dur((Date.now() - (ms(s.startedAt) || Date.now())) / 1000) : dur(s.durationSec);
      const btns = [];
      if (s.state === 'running') btns.push(s.stopping ? '<span class="small muted">stopping&hellip;</span>' : '<button type="button" class="btn small" data-stop="' + esc(s.id) + '">Stop</button>');
      if (s.state === 'needsNote' || s.state === 'skipped') btns.push('<button type="button" class="btn small" data-note="' + esc(s.id) + '">Write note</button>');
      if (s.hasLog) btns.push('<button type="button" class="btn small ghost" data-log="' + esc(s.id) + '" title="The game\'s output (project-manager\\logs)">Log</button>');
      return '<tr><td>' + esc(s.label) + ' <span class="muted small">' + esc(s.version || '') + '</span>' + (has(s.args) ? '<div class="small mono muted">' + esc(s.args) + '</div>' : '') + '</td>' +
        '<td class="nowrap">' + esc(when(s.startedAt)) + '</td><td class="num">' + esc(played) + (s.endApprox ? '?' : '') + '</td><td>' + pill(st[0], st[1]) + ' ' + exitPill(s.exit) + '</td>' +
        '<td class="nowrap">' + btns.join(' ') + '</td></tr>';
    }).join('') + '</tbody></table>';
    setHTML('sec-sessions', html);
  }

  function releaseRow(r, inst, showChannel) {
    const i = obj(inst[r.tag]);
    let act;
    if (ACTIVE.includes(i.state)) {
      const f = i.total ? Math.min(1, (i.got || 0) / i.total) : 0;
      act = '<span class="prog"><i style="width:' + (f * 100).toFixed(1) + '%"></i></span> <span class="small muted">' + esc(i.state === 'downloading' ? Math.round(f * 100) + '% of ' + size(i.total) : i.state + '…') + '</span>' +
        (i.cancel ? ' <span class="small muted">cancelling&hellip;</span>' : ' <button type="button" class="btn small ghost" data-cancel="' + esc(r.tag) + '">Cancel</button>');
    } else if (r.installed) {
      act = '<button type="button" class="btn small primary" data-play="versions/' + esc(r.tag) + '">Play</button> <button type="button" class="btn small ghost danger" data-remove="' + esc(r.tag) + '">Remove</button> ' + checkPill(r.check);
    } else if (!r.zip) act = '<span class="muted small">no Windows zip</span>';
    else if (r.needsSig && !(r.sig && r.manifest && r.manifestSig)) act = '<span class="muted small" title="It claims 1.17.0 or later but has no signature: the launcher doesn\'t offer it either">unsigned: not offered</span>';
    else act = '<button type="button" class="btn small" data-install="' + esc(r.tag) + '" title="' + (r.needsSig ? 'Checked like the launcher: signature and signed release manifest' : 'From before signing: checked against SHA256SUMS when it has one') + '">Install</button>' +
      (i.state === 'failed' ? ' <span class="err small">' + esc(i.error) + '</span>' : i.state === 'cancelled' ? ' <span class="small muted">cancelled</span>' : '');
    const ch = r.channel === 'production' ? pill('good', 'production') : r.channel === 'testing' ? pill('run', 'testing') : pill('idle', r.channel);
    const marks = arr(r.folders).map(f => pill('run', 'in ' + f, 'The ' + f + ' folder holds this release')).join(' ') +
      (r.newer ? ' ' + pill('warn', 'newer than ' + r.newer, 'Newer than what the ' + r.newer + ' folder holds') : '');
    return '<tr' + (r.zip ? '' : ' class="off"') + '><td><span class="mono">' + esc(r.tag) + '</span>' + (r.nameSaysMore ? ' <span class="muted small">' + esc(r.name) + '</span>' : '') + (marks ? ' ' + marks : '') + '</td>' +
      (showChannel ? '<td>' + ch + '</td>' : '') + '<td class="nowrap">' + esc(when(r.publishedAt)) + '</td><td class="num">' + (r.zip ? esc(size(r.zip.size)) : '') + '</td><td class="nowrap">' + act + '</td></tr>';
  }
  function releaseTable(list, inst, showChannel, title) {
    return '<div class="rel-col">' + (title ? '<h3 class="rel-h">' + title + ' <span class="count">' + list.length + '</span></h3>' : '') +
      '<div class="rel-wrap"><table class="t"><thead><tr><th>Release</th>' + (showChannel ? '<th>Channel</th>' : '') + '<th>Published</th><th class="num">Size</th><th>On this PC</th></tr></thead><tbody>' +
      (list.length ? list.map(r => releaseRow(r, inst, showChannel)).join('') : '<tr><td class="muted" colspan="5">None.</td></tr>') + '</tbody></table></div></div>';
  }
  function renderReleases() {
    const v = state.v, rel = obj(v.releases), inst = obj(v.installs), all = state.relList;
    const seg = ['all', 'production', 'testing'].map(f => '<button type="button" data-filter="' + f + '"' + (state.relFilter === f ? ' class="on"' : '') + '>' + f[0].toUpperCase() + f.slice(1) + '</button>').join('');
    const list = all.filter(r => state.relFilter === 'all' || r.channel === state.relFilter);
    const count = state.relFilter === 'all' ? String(all.length) : list.length + ' of ' + all.length;
    let html = '<div class="card-head"><h2 id="h-releases">Older versions <span class="count" title="' + esc(state.relFilter === 'all' ? 'every release on GitHub' : state.relFilter + ' releases shown of all') + '">' + esc(count) + '</span></h2>' +
      '<span class="rel-tools"><span class="seg">' + seg + '</span><span class="src">GitHub releases &middot; ' + (rel.busy ? 'fetching&hellip;' : rel.fetchedAt ? 'checked ' + esc(when(rel.fetchedAt)) : 'not fetched yet') + '</span>' +
      '<button type="button" class="btn small" id="rel-refresh">Refresh</button></span></div>';
    if (rel.ok === false) html += '<div class="down"><div><strong>Could not get the release list</strong><div class="err">' + esc(rel.error) + '</div></div></div>';
    else if (has(rel.error)) html += '<p class="small warn">' + esc(rel.error) + '</p>';
    if (!list.length) {
      setHTML('sec-releases', html + (rel.ok === false ? '' : '<p class="empty">' + (rel.busy || rel.ok == null ? 'Loading the list from GitHub&hellip;' : 'No releases.') + '</p>'));
      return tickRefresh();
    }
    // wide screens: production and testing side by side instead of one long table with empty space beside it
    if (state.relFilter === 'all' && WIDE.matches) {
      html += '<div class="rel-cols">' + releaseTable(list.filter(r => r.channel === 'production'), inst, false, 'Production') +
        releaseTable(list.filter(r => r.channel !== 'production'), inst, false, 'Testing') + '</div>';
    } else html += releaseTable(list, inst, true, '');
    setHTML('sec-releases', html);
    tickRefresh();
  }
  // the Refresh button: disabled while fetching and for the 30 s the server ignores another press, with a countdown
  function tickRefresh() {
    const b = $('rel-refresh');
    if (!b || !state.v) return;
    const left = Math.ceil((state.refreshUntil - Date.now()) / 1000);
    const busy = !!obj(state.v.releases).busy;
    b.disabled = busy || left > 0 || !!state.error;
    b.textContent = left > 0 && !busy ? 'Refresh (' + left + ' s)' : 'Refresh';
    b.title = left > 0 ? 'Checked just now; GitHub is asked at most every 30 s' : 'Ask GitHub for new releases now';
  }

  function renderStatus() {
    const el = $('status-text'), line = $('statusline');
    let txt = 'Loading…', bad = false;
    if (state.error) { txt = 'Not connected to the Supervisor Board server.'; bad = true; }
    else if (state.v) {
      const n = Number(state.v.running) || 0;
      txt = (n ? n + (n === 1 ? ' game running' : ' games running') + ' · ' : '') + 'Games start hidden from any console, from their own folder; when one closes this page asks for a note.';
    }
    el.textContent = txt;
    line.classList.toggle('bad', bad);
    $('sec-down').hidden = !state.error;
    $('down-detail').textContent = state.error ? 'Last try: ' + when(Date.now()) + ' (' + state.error + ')' : '';
    document.body.classList.toggle('down', !!state.error);
    tickRefresh();
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

  // ---------------------------------------------------------------- the note form: a game's note, a note without a
  // game, or an edit of a saved one
  const dlg = $('note-dlg'), form = $('note-form');
  function wantedNote() {
    const m = /(?:^|[#&])note=([^&]+)/.exec(location.hash);
    return m ? decodeURIComponent(m[1]) : null;
  }
  function saveLater() { session('play-later', JSON.stringify(Array.from(state.later))); }
  function maybeOpenNote() {
    if (dlg.open || $('log-dlg').open || !state.v) return;
    const pend = arr(state.v.pending);
    const want = wantedNote();
    const s = want ? sessionById(want) : null;
    if (s && (s.state === 'needsNote' || s.state === 'skipped') && !state.later.has(s.id)) return openNote(s.id);
    const next = pend.find(p => !state.later.has(p.id));
    if (next) openNote(next.id);
  }
  // the open form's session was noted or skipped somewhere else (another page, the API): close it, say so
  function closeStaleNote() {
    const n = state.note;
    if (!dlg.open || !n || n.kind !== 'session' || state.busy) return;
    const s = sessionById(n.id);
    const gone = !s || s.state === 'noted' || (n.from === 'needsNote' && s.state !== 'needsNote');
    if (!gone) return;
    const what = s && s.state === 'noted' ? 'already saved' : s && s.state === 'skipped' ? 'already skipped' : 'gone';
    store(draftKey(), null);
    closeNote(false);
    toast('That play note was ' + what + ' (from another window).');
  }
  // Save / Skip answered 409 for a stale session: close the form, nothing is written twice
  function staleAnswer(err) {
    if (err.status !== 409 || !['already-noted', 'running', 'locked'].includes(err.reason)) return false;
    store(draftKey(), null);
    closeNote(false);
    toast(err.reason === 'already-noted' ? 'That play note was already saved.' : err.message, err.reason === 'locked');
    poll();
    return true;
  }
  function draftKey() { const n = state.note; return !n ? 'play-draft-none' : n.kind === 'free' ? 'play-draft-free' : n.kind === 'edit' ? 'play-draft-edit-' + n.id : 'play-draft-' + n.id; }
  function fillFields(values) {
    for (const k of FIELDS) form.elements[k].value = typeof values[k] === 'string' ? values[k] : '';
    updateCounts();
  }
  // Drafts (localStorage, per form): {did, noticed, change, other, at, base}. A draft is kept only while it differs
  // from what the form started from (the saved note for an edit, empty boxes otherwise), is dropped after DRAFT_DAYS,
  // and an edit's draft only counts for the saved text it was made from (`base`): changed or locked since, it's gone.
  const DRAFT_DAYS = 14;
  const fieldsSig = (f) => JSON.stringify(FIELDS.map(k => typeof obj(f)[k] === 'string' ? f[k] : ''));
  function readDraft(key, base) {
    const d = storeJSON(key, null);
    if (!d || typeof d !== 'object') return null;
    const stale = !d.at || Date.now() - d.at > DRAFT_DAYS * 864e5;
    if ((stale && (key.startsWith('play-draft-edit-') || d.at)) || (base !== undefined && d.base !== base)) { store(key, null); return null; }
    return d;
  }
  function formFields() { const d = {}; for (const k of FIELDS) d[k] = form.elements[k].value; return d; }
  // the row over the boxes: shown while the form differs from where it started, with Discard draft
  function updateDraftRow() {
    const n = state.note;
    const differs = !!n && fieldsSig(formFields()) !== fieldsSig(n.start);
    $('note-draft').hidden = !differs;
    if (!differs) return;
    $('note-draft-txt').textContent = (n.kind === 'edit' ? 'Changed from the saved note' : 'Not saved yet') +
      (n.restoredAt ? ' (your unsaved draft from ' + when(n.restoredAt) + ')' : '; kept as a draft if you close this');
  }
  function discardDraft() {
    const n = state.note;
    if (!n) return;
    store(draftKey(), null);
    n.restoredAt = null;
    fillFields(n.start);
    updateDraftRow();
    form.elements.did.focus();
  }
  // at start: drafts too old, edit drafts from before `base` was recorded (they came back over the saved note)
  try {
    for (let i = localStorage.length - 1; i >= 0; i--) { const k = localStorage.key(i); if (k && k.startsWith('play-draft-')) readDraft(k); }
  } catch (e) { /* storage blocked */ }
  function showDialog(title) {
    $('note-h').textContent = title;
    $('note-err').hidden = true;
    updateDraftRow();
    try { dlg.showModal(); } catch (e) { dlg.setAttribute('open', ''); }
    setTimeout(() => form.elements.did.focus(), 30);
  }
  function setButtons(kind, s) {
    $('note-skip').hidden = !(kind === 'session' && s && s.state === 'needsNote');
    $('note-later').textContent = kind === 'session' ? 'Later' : 'Cancel';
    $('note-later').title = kind === 'session' ? 'Close this; every waiting note stays waiting (the Play tab shows a badge)' : 'Close without saving (what you typed is kept as a draft)';
    $('note-delete').hidden = kind !== 'edit';
    $('note-for').hidden = kind !== 'free';
    $('note-attach-row').hidden = !(kind === 'session' && s && s.hasLog);
  }
  function openNote(id) {
    const s = sessionById(id);
    if (!s) return;
    state.note = { kind: 'session', id, from: s.state };
    const pend = arr(state.v.pending);
    const idx = pend.findIndex(p => p.id === id);
    $('note-count').textContent = pend.length > 1 && idx >= 0 ? (idx + 1) + ' of ' + pend.length + ' waiting' : '';
    const x = obj(s.exit);
    $('note-meta').hidden = false;
    $('note-meta').innerHTML = '<span><b>' + esc(s.label) + '</b> ' + esc(s.version || '') + (s.commit ? ' <span class="mono">' + esc(String(s.commit).slice(0, 7)) + '</span>' : '') + '</span>' +
      '<span>played <b>' + esc(dur(s.durationSec)) + '</b>' + (s.endApprox ? ' or less' : '') + '</span><span>closed ' + esc(when(s.endedAt)) + '</span>' +
      (has(s.args) ? '<span class="mono">' + esc(s.args) + '</span>' : '') +
      (x.kind && x.kind !== 'ok' ? '<span class="' + (x.kind === 'crash' ? 'err' : x.kind === 'error' ? 'warn' : 'muted') + '">' + esc(x.text) + '</span>' : '') +
      (s.hasLog ? '<button type="button" class="btn small ghost" data-log="' + esc(s.id) + '">Log</button>' : '');
    const draft = readDraft(draftKey());
    state.note.start = {};
    state.note.restoredAt = draft && draft.at;
    fillFields(draft || {});
    $('note-attach').checked = x.kind === 'crash' || x.kind === 'error';
    setButtons('session', s);
    showDialog('How was it?');
  }
  // about: a change that didn't reach the file ({title, stamp, change, why, build, label, version}): the follow-up's
  // build, and a first line in "Anything else" that names the earlier note
  function openFreeNote(about) {
    state.note = { kind: 'free' };
    $('note-count').textContent = '';
    $('note-meta').hidden = true;
    const bl = arr(state.v && state.v.builds).filter(b => b.present);
    const saved = readDraft(draftKey());
    const draft = obj(saved);
    state.note.start = {};
    state.note.restoredAt = saved && saved.at;
    if (about) {
      const line = 'Follow-up to my note "' + String(about.title || '').replace(/^Play note: /, '') + '" (' + (about.stamp || '') + '): my ' + about.change + ' didn\'t go in, ' + about.why + '. ';
      draft.build = bl.some(b => b.id === about.build) ? about.build : '';
      if (!draft.build) { draft.label = about.label || 'launcher'; draft.version = about.version || ''; }
      draft.other = line + (typeof draft.other === 'string' && draft.other ? '\n' + draft.other : '');
    }
    $('note-build').innerHTML = bl.map(b => '<option value="' + esc(b.id) + '">' + esc(b.label) + (has(b.version) && b.version !== b.label ? ' ' + esc(b.version) : '') + '</option>').join('') + '<option value="">The launcher / another version</option>';
    $('note-build').value = typeof draft.build === 'string' && (draft.build === '' || bl.some(b => b.id === draft.build)) ? draft.build : (bl[0] ? bl[0].id : '');
    form.elements.label.value = typeof draft.label === 'string' ? draft.label : 'launcher';
    form.elements.version.value = typeof draft.version === 'string' ? draft.version : '';
    otherToggle();
    fillFields(draft);
    setButtons('free');
    showDialog('Add a play note');
  }
  function openEdit(id) {
    const n = noteById(id);
    if (!n) return;
    state.note = { kind: 'edit', id, start: obj(n.fields), base: fieldsSig(n.fields) };
    $('note-count').textContent = '';
    $('note-meta').hidden = false;
    $('note-meta').innerHTML = '<span><b>' + esc(String(n.title || '').replace(/^Play note: /, '')) + '</b></span><span>' + esc(n.stamp || '') + '</span><span class="muted">' + esc(n.where || '') + '</span>';
    const draft = readDraft(draftKey(), state.note.base);
    state.note.restoredAt = draft && draft.at;
    fillFields(draft || obj(n.fields));
    setButtons('edit');
    showDialog('Edit play note');
  }
  function otherToggle() { const other = $('note-build').value === ''; $('note-other-label').hidden = !other; $('note-other-version').hidden = !other; }
  function closeNote(later) {
    if (later && state.note && state.note.kind === 'session') {
      // Later = every waiting note, for this page session (a reload in the same tab keeps it); a game that closes
      // afterwards still asks
      for (const p of arr(state.v && state.v.pending)) state.later.add(p.id);
      state.later.add(state.note.id);
      saveLater();
    }
    state.note = null;
    if (/note=/.test(location.hash)) history.replaceState(null, '', location.pathname + location.search);
    if (dlg.open) dlg.close();
  }
  function updateCounts() {
    for (const k of FIELDS) {
      const ta = form.elements[k];
      let c = ta.parentNode.querySelector('.cnt');
      if (!c) { c = document.createElement('span'); c.className = 'cnt small'; ta.parentNode.insertBefore(c, ta); }
      const n = ta.value.length;
      c.textContent = n > MAX_FIELD * 0.75 ? n + ' / ' + MAX_FIELD : '';
      c.classList.toggle('warn', n >= MAX_FIELD);
    }
  }
  form.addEventListener('input', () => {
    updateCounts();
    if (!state.note) return;
    const d = formFields();
    if (fieldsSig(d) === fieldsSig(state.note.start)) { store(draftKey(), null); state.note.restoredAt = null; updateDraftRow(); return; }
    if (state.note.kind === 'free') { d.build = $('note-build').value; d.label = form.elements.label.value; d.version = form.elements.version.value; }
    d.at = Date.now();
    if (state.note.base) d.base = state.note.base;
    store(draftKey(), JSON.stringify(d));
    updateDraftRow();
  });
  $('note-build').addEventListener('change', otherToggle);
  $('note-discard').addEventListener('click', discardDraft);
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const n = state.note;
    if (!n || state.busy) return;
    const body = {};
    for (const k of FIELDS) body[k] = form.elements[k].value;
    let path = '/api/play/note';
    if (n.kind === 'session') { body.session = n.id; body.attachLog = !$('note-attach-row').hidden && $('note-attach').checked; }
    else if (n.kind === 'free') { const b = $('note-build').value; if (b) body.build = b; else { body.label = form.elements.label.value; body.version = form.elements.version.value; } }
    else { path = '/api/play/note-edit'; body.id = n.id; }
    state.busy = true; $('note-save').disabled = true;
    try {
      const j = await api(path, body);
      store(draftKey(), null);
      closeNote(false);
      toast(j.noop ? 'Nothing changed.' : (j.message || 'Saved.'));
      shellPoke();
      poll();
    } catch (err) {
      if (!staleAnswer(err)) { const el = $('note-err'); el.textContent = err.message; el.hidden = false; }
    } finally { state.busy = false; $('note-save').disabled = false; }
  });
  form.addEventListener('keydown', (e) => { if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); form.requestSubmit(); } });
  dlg.addEventListener('cancel', (e) => { e.preventDefault(); closeNote(true); });  // (Esc = Later / Cancel)
  $('note-later').addEventListener('click', () => closeNote(true));
  $('note-skip').addEventListener('click', async () => {
    const n = state.note;
    if (!n || n.kind !== 'session') return;
    try {
      const j = await api('/api/play/skip', { session: n.id });
      store(draftKey(), null); closeNote(false);
      if (j && j.session && j.session.noop) toast('That play note was already skipped.');
      shellPoke(); poll();
    } catch (err) { if (!staleAnswer(err)) { const el = $('note-err'); el.textContent = err.message; el.hidden = false; } }
  });
  $('note-delete').addEventListener('click', () => { if (state.note && state.note.kind === 'edit') removeNote(state.note.id); });
  async function removeNote(id) {
    const n = noteById(id);
    if (!confirm('Remove this play note?\n\n' + (n ? n.title : id) + '\n\nIt is removed from 12-play-notes.md too (it is still open, so no agent has taken it).')) return;
    try {
      const j = await api('/api/play/note-delete', { id });
      store('play-draft-edit-' + id, null);
      if (state.note && state.note.id === id) closeNote(false);
      toast(j.message || 'Removed.');
      poll();
    } catch (err) { if (!staleAnswer(err)) toast(err.message, true); }
  }
  window.addEventListener('hashchange', () => {
    const id = wantedNote();
    if (id) { state.later.delete(id); saveLater(); if (!dlg.open) maybeOpenNote(); }
  });

  // ---------------------------------------------------------------- the log view
  const logDlg = $('log-dlg');
  async function openLog(id) {
    state.logSession = id;
    $('log-text').textContent = 'Loading…';
    $('log-sub').textContent = '';
    if (!logDlg.open) { try { logDlg.showModal(); } catch (e) { logDlg.setAttribute('open', ''); } }
    try {
      const j = await api('/api/play/log?session=' + encodeURIComponent(id));
      const x = obj(j.exit);
      $('log-h').textContent = 'Game log: ' + (j.label || '') + ' ' + (j.version || '');
      $('log-sub').textContent = (j.state === 'running' ? 'still running' : x.text || '') + ' · ' + size(j.size) + (j.truncated ? ' (showing the end)' : '');
      const pre = $('log-text');
      pre.textContent = j.text || '(the game wrote nothing)';
      pre.scrollTop = pre.scrollHeight;
    } catch (err) { $('log-text').textContent = err.message; }
  }
  $('log-close').addEventListener('click', () => logDlg.close());
  $('log-reload').addEventListener('click', () => { if (state.logSession) openLog(state.logSession); });
  $('log-open').addEventListener('click', () => { if (state.logSession) openPath({ what: 'log', session: state.logSession }); });
  logDlg.addEventListener('close', () => { state.logSession = null; maybeOpenNote(); });
  async function openPath(body) {
    try { await api('/api/play/open', body); } catch (err) { toast(err.message, true); }
  }

  // ---------------------------------------------------------------- actions
  // A pressed button is disabled until the request and the poll after it are done, then enabled again: the cards and
  // tables keep their HTML when it doesn't change, so a game that crashed before that poll (the card is "not running"
  // again, the same HTML) would otherwise keep a disabled Play button until a reload.
  async function launch(id, btn) {
    const c = cards.get(id);
    const text = c ? c.input.value : argsFor(id);
    if (btn) btn.disabled = true;
    try {
      const j = await api('/api/play/launch', { build: id, args: text });
      remember(text);
      toast('Started ' + (j.session.label || '') + (j.session.args ? ' with ' + j.session.args : '') + '.');
      shellPoke();
      await poll();
    } catch (err) { toast(err.message, true); } finally { if (btn) btn.disabled = false; }
  }
  document.addEventListener('input', (e) => {
    const t = e.target;
    if (t instanceof HTMLInputElement && t.classList.contains('opt') && t.dataset.build) {
      argsBy[t.dataset.build] = t.value; saveArgs();
      const c = cards.get(t.dataset.build);
      if (c && c.mapsSig) fillPick(c);
    }
  });
  document.addEventListener('keydown', (e) => {
    const t = e.target;
    if (e.key === 'Enter' && t instanceof HTMLInputElement && t.classList.contains('opt') && t.dataset.build && !document.body.classList.contains('down')) {
      e.preventDefault();
      const c = cards.get(t.dataset.build);
      const btn = c && c.act.querySelector('[data-play]');
      if (btn && !btn.disabled) launch(t.dataset.build, btn);
    }
  });
  document.addEventListener('click', async (e) => {
    const t = e.target instanceof Element ? e.target : null;
    if (!t) return;
    const a = t.closest('a[data-tab]');
    if (a && window.quarkShell && typeof window.quarkShell.open === 'function') {
      e.preventDefault();
      try { window.quarkShell.open(a.dataset.tab, decodeURIComponent(a.dataset.hash || '')); } catch (err) { /* the shell logs it */ }
      return;
    }
    const b = t.closest('button');
    if (!b) return;
    const d = b.dataset;
    try {
      if (d.play) { await launch(d.play, b); return; }
      if (d.stop) {
        b.disabled = true;
        const j = await api('/api/play/stop', { session: d.stop });
        toast(j.session && j.session.noop ? 'It is already being stopped.' : 'Asking ' + ((j.session && j.session.label) || 'the game') + ' to close…');
        await poll();
      } else if (d.open) {
        openPath({ what: d.open, build: d.build });
      } else if (d.log) {
        openLog(d.log);
      } else if (d.install) {
        b.disabled = true;
        await api('/api/play/install', { tag: d.install });
        await poll();
      } else if (d.cancel) {
        b.disabled = true;
        await api('/api/play/install-cancel', { tag: d.cancel });
        await poll();
      } else if (d.remove) {
        if (!confirm('Remove ' + d.remove + '?\n\nThis deletes the folder Quark\\versions\\' + d.remove + ' (you can install it again).')) return;
        await api('/api/play/remove', { tag: d.remove });
        toast('Removed ' + d.remove + '.');
        poll();
      } else if (d.note) {
        state.later.delete(d.note); saveLater();
        openNote(d.note);
      } else if (d.edit) {
        openEdit(d.edit);
      } else if (d.del) {
        removeNote(d.del);
      } else if (d.more) {
        if (state.openNotes.has(d.more)) state.openNotes.delete(d.more); else state.openNotes.add(d.more);
        renderNotes();
      } else if (d.filter) {
        state.relFilter = d.filter; store('play-filter', state.relFilter); renderReleases();
      } else if (d.follow) {
        const x = arr(state.v.blocked).find(y => y.id === d.follow);
        if (x) openFreeNote(x);
      } else if (d.dismiss) {
        b.disabled = true;
        await api('/api/play/note-dismiss', { id: d.dismiss });
        await poll();
      } else if (b.id === 'note-add') {
        openFreeNote();
      } else if (b.id === 'rel-refresh') {
        b.disabled = true; apply(await api('/api/play?refresh=1' + (state.relSig ? '&rel=' + encodeURIComponent(state.relSig) : ''))); setTimeout(poll, 1500);
      } else if (b.id === 'sync-now') {
        b.disabled = true; await api('/api/play/sync', {}); await new Promise(r => setTimeout(r, 2500)); await poll();
      } else if (b.id === 'down-retry') {
        poll();
      }
    } catch (err) {
      if (err.down) { state.error = err.detail || err.message; renderStatus(); }
      toast(err.message, true);
    } finally {
      b.disabled = false;  // (see launch; a button the poll replaced is gone anyway)
      if (b.id === 'rel-refresh') tickRefresh();
    }
  });

  // ---------------------------------------------------------------- polling (the release list is sent only when it
  // changed: the page names the sig of the one it has)
  function apply(v) {
    v = obj(v);
    const rel = obj(v.releases);
    if (Array.isArray(rel.list)) { state.relList = rel.list; state.relSig = rel.sig || null; }
    state.refreshUntil = Date.now() + (Number(rel.refreshIn) || 0) * 1000;
    state.v = v; state.error = null;
    renderAll();
  }
  function busy() {
    const v = state.v;
    if (!v) return true;
    return Number(v.running) > 0 || obj(v.releases).busy ||
      Object.values(obj(v.installs)).some(i => ACTIVE.includes(i.state));
  }
  async function poll() {
    clearTimeout(state.timer);
    try { apply(await api('/api/play' + (state.relSig ? '?rel=' + encodeURIComponent(state.relSig) : ''))); }
    catch (err) { state.error = err.detail || err.message; renderStatus(); }
    state.timer = setTimeout(poll, document.hidden ? HIDDEN_MS : state.error ? FAST_MS * 2 : busy() ? FAST_MS : SLOW_MS);
  }
  document.addEventListener('visibilitychange', () => { if (!document.hidden) poll(); });
  if (WIDE.addEventListener) WIDE.addEventListener('change', () => { if (state.v) renderReleases(); });
  setInterval(() => {
    const c = $('clock');
    if (c) c.textContent = fmt({ weekday: 'short', hour: 'numeric', minute: '2-digit', timeZoneName: 'short' }).format(Date.now()) + ' Dallas';
    if (state.refreshUntil > Date.now() - 1500) tickRefresh();
  }, 1000);

  state.relFilter = ['all', 'production', 'testing'].includes(store('play-filter')) ? store('play-filter') : 'all';
  try { state.later = new Set(arr(JSON.parse(session('play-later') || '[]'))); } catch (e) { state.later = new Set(); }
  if (EMBED) document.body.classList.add('embed');
  renderOptsList();
  poll();
})();
