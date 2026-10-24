/* Edit tab: edit a file (text + live preview, Ctrl+S), answer q's questions, add an entry. Also the "Answer" box
   and "Edit" button used on cards elsewhere. Saves go to the server (POST, same origin, X-Claude-Manager: 1),
   which writes the PC's Claude Documents folder, commits and pushes. Unsaved text is kept as a draft per browser. */
'use strict';
(function (DM) {
  const {h, clear, $} = DM;
  const st = DM.state;
  const NAME_RE = /^([0-9]{2}-[a-z0-9-]+|README)\.md$/;
  const ANSWERS = '09-answers-from-q.md';
  const STATUSES = ['open', 'in-progress', 'fixed', 'wontfix', 'answered', 'told', 'done'];
  const MARKER_RE = /^(<{7}|={7}|>{7})( |$)/m;
  const MARK_OPEN = /^<{7}( |$)/, MARK_CLOSE = /^>{7}( |$)/;
  const DRAFT_KEY = 'quark.docsManager.drafts.v1';

  // ---------- drafts (per browser; a convenience, the server is the record) ----------
  function loadDrafts() {
    try { const d = JSON.parse(localStorage.getItem(DRAFT_KEY) || '{}'); return {files: d.files || {}, answers: d.answers || {}}; } catch (_) { return {files: {}, answers: {}}; }
  }
  const drafts = loadDrafts();
  let draftT = null;
  function saveDrafts() { clearTimeout(draftT); draftT = setTimeout(() => { try { localStorage.setItem(DRAFT_KEY, JSON.stringify(drafts)); } catch (_) { /* private window */ } }, 250); }

  const answered = new Map(); // entryId -> {text, at}: answered from this page, until the data shows it
  const ed = {sub: 'file', file: null, base: null, loaded: false, conflict: null, busy: false, status: null, jump: null};

  function canEdit() { return !!(st.data && st.data.canEdit === true && st.source === '/api/data'); }
  function showTab() { return !!(st.data && st.source === '/api/data' && (st.data.canEdit === true || (st.data.readOnly === false && st.data.editReason))); }

  // ---------- server ----------
  async function post(path, body) {
    try {
      const r = await fetch(path, {method: 'POST', cache: 'no-store', headers: {'Content-Type': 'application/json', 'X-Claude-Manager': '1'}, body: JSON.stringify(body)});
      let js = null;
      try { js = await r.json(); } catch (_) { /* not JSON */ }
      return {status: r.status, js: js || {ok: false, message: 'The server answered ' + r.status + ' ' + r.statusText + '.'}};
    } catch (e) {
      return {status: 0, js: {ok: false, message: 'Couldn’t reach the Claude Manager server, so nothing was saved (your text is still here). Is it running?'}};
    }
  }
  async function getDoc(file, sync) {
    const r = await fetch('/api/doc?file=' + encodeURIComponent(file) + (sync ? '&sync=1' : ''), {cache: 'no-store', headers: sync ? {'X-Claude-Manager': '1'} : {}});
    const js = await r.json();
    if (!r.ok) throw new Error(js && js.message || 'the server answered ' + r.status);
    return js;
  }
  async function refreshStatus() {
    try { const r = await fetch('/api/edit/status', {cache: 'no-store'}); ed.status = await r.json(); } catch (_) { ed.status = null; }
    ed.statusAt = Date.now();
    paintStatus();
    DM.data.banners();                          // (the "edited on the PC" banner says whether saves are waiting to go up)
  }
  // The writer's status ({unpushed, pushError, busy, ...}) and how old it is, for the banner.
  function editStatus() { return {status: ed.status, at: ed.statusAt || 0}; }
  // After a write the server fetches its mirror; pick that up soon.
  function reloadSoon() { setTimeout(() => DM.data.load(), 1500); setTimeout(() => DM.data.load(), 6000); setTimeout(refreshStatus, 1600); }

  const clock = d => new Intl.DateTimeFormat('en-US', {timeZone: 'America/Chicago', hour: 'numeric', minute: '2-digit'}).format(d ? new Date(d) : new Date());
  // "Saved and pushed at 1:12 AM" or what went wrong, from a write result.
  function resultText(js, what) {
    const at = clock(js.savedAt);
    if (js.ok && js.committed === false && js.pushed && /^Nothing/.test(js.message || '')) return {cls: 'ok', text: js.message};
    if (js.ok && js.pushed) return {cls: 'ok', text: (what || 'Saved') + ' and pushed at ' + at + (js.merged ? ', merged with a change someone else made meanwhile' : '') + '.'};
    if (js.ok) return {cls: 'warn', text: (what || 'Saved') + ' on this PC at ' + at + '; it’ll go up next time sync works.' + (js.pushError ? ' (' + js.pushError + ')' : '')};
    return {cls: 'err', text: js.message || 'Not saved.'};
  }
  function say(r) {
    const el = $('#edMsg');
    if (!el) return;
    clear(el).className = 'ed-msg ' + (r ? r.cls : '');
    if (r) el.append(r.text);
  }

  // ---------- the Edit tab ----------
  let built = false;
  const ui = {};
  function build() {
    if (built) return;
    built = true;
    ed.sub = ['file', 'answer', 'add'].includes(st.prefs.editSub) ? st.prefs.editSub : 'file';
    const root = $('#view-edit');
    ui.seg = h('div', {class: 'seg', id: 'edSeg'},
      [['file', 'Edit a file'], ['answer', 'Answer questions'], ['add', 'Add an entry']].map(([k, label]) =>
        h('button', {dataset: {sub: k}, onclick: () => setSub(k)}, label)));
    ui.fileSel = h('select', {id: 'edFile', 'aria-label': 'File to edit', onchange: () => openFile(ui.fileSel.value)});
    ui.dirty = h('span', {class: 'ed-dirty', id: 'edDirty'});
    ui.save = h('button', {class: 'primary', id: 'edSave', title: 'Save, commit and push (Ctrl+S)', onclick: () => save()}, 'Save');
    ui.discard = h('button', {id: 'edDiscard', title: 'Throw away your unsaved changes and load the file again', onclick: discard}, 'Discard');
    ui.fileTools = h('span', {class: 'tb'}, h('label', null, 'File ', ui.fileSel), ui.save, ui.discard, ui.dirty);
    ui.pending = h('span', {class: 'ed-pending', id: 'edPending'});
    root.appendChild(h('div', {class: 'toolbar'}, ui.seg, h('span', {class: 'sep'}), ui.fileTools,
      h('span', {class: 'spacer'}), ui.pending, h('span', {id: 'edMsg', class: 'ed-msg', 'aria-live': 'polite'})));
    ui.reason = h('div', {class: 'banner b-warn', hidden: true});
    ui.conflict = h('div', {class: 'ed-conflict', id: 'edConflict', hidden: true});
    root.appendChild(ui.reason);
    root.appendChild(ui.conflict);

    // edit a file
    ui.text = h('textarea', {id: 'edText', class: 'ed-text', spellcheck: 'false', 'aria-label': 'File text', wrap: 'soft'});
    ui.text.addEventListener('input', onInput);
    for (const t of ['click', 'keyup']) ui.text.addEventListener(t, () => { if (ed.conflict) paintConflictCount(); });
    ui.prev = h('article', {class: 'doc md ed-prev', id: 'edPrev'});
    ui.prevHead = h('span', {class: 'ed-phl'});
    ui.sync = h('input', {type: 'checkbox', id: 'edSync', checked: st.prefs.editSync !== false || null,
      onchange: () => { st.prefs.editSync = ui.sync.checked; DM.app.save(); if (ui.sync.checked) syncFrom('text'); }});
    ui.prevScroll = h('div', {class: 'scroll'}, ui.prev);
    ui.measure = h('div', {class: 'ed-text ed-measure', 'aria-hidden': 'true'});
    ui.paneFile = h('div', {class: 'ed-split'},
      h('div', {class: 'ed-pane'}, h('div', {class: 'ed-ph'}, h('b', null, 'Text'), h('span', {class: 'faint'}, ' markdown; Ctrl+S saves')), ui.text, ui.measure),
      h('div', {class: 'ed-pane'}, h('div', {class: 'ed-ph'}, ui.prevHead, h('span', {class: 'spacer'}),
        h('label', {class: 'chk', title: 'Scroll the text and the preview together'}, ui.sync, 'Sync scroll')), ui.prevScroll));
    ui.text.addEventListener('scroll', () => syncFrom('text'), {passive: true});
    ui.prevScroll.addEventListener('scroll', () => syncFrom('prev'), {passive: true});
    new ResizeObserver(() => { tops = null; anchors = null; }).observe(ui.text);
    // answer questions
    ui.qList = h('div', {class: 'ed-qs', id: 'edQs'});
    ui.qFoot = h('div', {class: 'ed-qfoot'});
    ui.paneAnswer = h('div', {class: 'scroll'}, h('div', {class: 'ed-qwrap'}, ui.qList), ui.qFoot);
    // add an entry
    ui.paneAdd = h('div', {class: 'scroll'}, buildAddForm());
    root.appendChild(h('div', {class: 'ed-body'}, ui.paneFile, ui.paneAnswer, ui.paneAdd));
    window.addEventListener('beforeunload', ev => { if (isDirty()) { ev.preventDefault(); ev.returnValue = ''; } });
  }

  // ---------- line positions (scroll sync, jumping to a line or a conflict) ----------
  // tops[i]: y of source line i+1 inside the wrapped textarea, measured in a hidden copy with the same width and font;
  // anchors: [line, y] of every preview block that knows its source line (data-l). Both are dropped on edit / resize.
  let tops = null, anchors = null;
  function lineTops() {
    if (tops) return tops;
    const m = ui.measure, lines = ui.text.value.split('\n');
    m.style.width = ui.text.clientWidth + 'px';
    const frag = document.createDocumentFragment();
    for (const l of lines) { const d = document.createElement('div'); d.textContent = l || '\u200b'; frag.appendChild(d); }
    clear(m).appendChild(frag);
    tops = Array.from(m.children, d => d.offsetTop);
    tops.push(m.scrollHeight);
    clear(m);
    return tops;
  }
  function previewAnchors() {
    if (anchors) return anchors;
    const base = ui.prevScroll.getBoundingClientRect().top - ui.prevScroll.scrollTop;
    anchors = [[1, 0]];
    for (const el of ui.prev.querySelectorAll('[data-l]')) {
      const l = +el.dataset.l, y = el.getBoundingClientRect().top - base;
      if (l > anchors[anchors.length - 1][0] && y >= anchors[anchors.length - 1][1]) anchors.push([l, y]);
    }
    anchors.push([Infinity, ui.prevScroll.scrollHeight]);
    return anchors;
  }
  // Linear map between two sorted [a, b] lists: the b for a value of a.
  function interp(pairs, a, ai, bi) {
    let lo = 0, hi = pairs.length - 1;
    while (hi - lo > 1) { const mid = (lo + hi) >> 1; if (pairs[mid][ai] <= a) lo = mid; else hi = mid; }
    const p = pairs[lo], q = pairs[hi];
    if (!isFinite(q[ai]) || q[ai] === p[ai]) return p[bi];
    return p[bi] + (a - p[ai]) / (q[ai] - p[ai]) * (q[bi] - p[bi]);
  }
  let expect = {text: null, prev: null};   // scrollTop we just set on a pane: its echo event is ignored
  function syncFrom(src) {
    if (!ui.sync.checked || ed.conflict || !ed.loaded || ed.sub !== 'file') return;
    const el = src === 'text' ? ui.text : ui.prevScroll, other = src === 'text' ? 'prev' : 'text';
    if (expect[src] != null && Math.abs(el.scrollTop - expect[src]) < 2) { expect[src] = null; return; }
    expect[src] = null;
    const t = lineTops(), a = previewAnchors(), pairs = t.map((y, i) => [i + 1, y]);
    let y;
    if (src === 'text') y = interp(a, interp(pairs, ui.text.scrollTop, 1, 0), 0, 1);
    else y = interp(pairs, interp(a, ui.prevScroll.scrollTop, 1, 0), 0, 1);
    const target = src === 'text' ? ui.prevScroll : ui.text;
    y = Math.max(0, Math.min(Math.round(y), target.scrollHeight - target.clientHeight));
    if (Math.abs(target.scrollTop - y) < 2) return;
    expect[other] = y;
    target.scrollTop = y;
  }
  // Select [start, end) in the text and scroll it into the top third.
  function showRange(start, end) {
    const line = ui.text.value.slice(0, start).split('\n').length;
    ui.text.focus({preventScroll: true});
    ui.text.setSelectionRange(start, end);
    ui.text.scrollTop = Math.max(0, lineTops()[line - 1] - ui.text.clientHeight / 3);
  }

  function setSub(k) {
    ed.sub = k;
    st.prefs.editSub = k; DM.app.save();
    render();
  }

  // Files q can edit here: README and 00-NN, but not the ones another tool generates (10, 11: the GitHub Manager).
  function editable(name) { const f = st.fileByName && st.fileByName[name]; return NAME_RE.test(name) && !(f && f.generated); }
  function fileNames() {
    const names = new Set(st.data.files.map(f => f.name).filter(editable));
    return [...names].sort((a, b) => (a === 'README.md' ? -1 : b === 'README.md' ? 1 : a.localeCompare(b)));
  }
  function entryFileNames() { return fileNames().filter(n => /^0[1-9]-/.test(n)); }
  function pickDefault() { const n = fileNames(); return n.find(x => x.startsWith('07')) || n[0]; }
  function label(n) { return (n === 'README.md' ? 'README' : n.slice(0, 2) + ' ' + DM.fileTitle(n)) + (drafts.files[n] && n !== ed.file ? ' (unsaved draft)' : ''); }

  function render() {
    build();
    const on = canEdit();
    ui.reason.hidden = on;
    clear(ui.reason);
    if (!on) ui.reason.append(h('b', null, 'Editing is off. '), (st.data && st.data.editReason) || 'The manager can’t write to Claude Documents right now.');
    for (const b of ui.seg.children) b.classList.toggle('on', b.dataset.sub === ed.sub);
    ui.fileTools.hidden = ed.sub !== 'file';
    ui.paneFile.hidden = ed.sub !== 'file';
    ui.paneAnswer.hidden = ed.sub !== 'answer';
    ui.paneAdd.hidden = ed.sub !== 'add';
    for (const el of [ui.save, ui.discard, ui.text]) el.disabled = !on;
    // file picker
    const names = fileNames();
    if (!ed.file || !names.includes(ed.file)) ed.file = st.prefs.editFile && names.includes(st.prefs.editFile) ? st.prefs.editFile : pickDefault();
    clear(ui.fileSel);
    for (const n of names) ui.fileSel.appendChild(h('option', {value: n}, label(n)));
    ui.fileSel.value = ed.file;
    if (ed.sub === 'file' && !ed.loaded && !ed.loading) openFile(ed.file);
    if (ed.sub === 'answer') renderQuestions();
    if (ed.sub === 'add') syncAddForm();
    paintDirty(); paintStatus();
  }

  // ---------- edit a file ----------
  function isDirty() { return ed.loaded && ui.text && ui.text.value !== ed.base; }
  function paintDirty() {
    if (!ui.dirty) return;
    const d = isDirty();
    ui.dirty.textContent = !ed.loaded ? '' : d ? '● Unsaved changes' : 'No unsaved changes';
    ui.dirty.className = 'ed-dirty' + (d ? ' on' : '');
    ui.save.disabled = !canEdit() || !d || ed.busy;
    ui.discard.disabled = !canEdit() || (!d && !ed.conflict) || ed.busy;
  }
  function paintStatus() {
    if (!ui.pending) return;
    clear(ui.pending);
    const s = ed.status;
    if (s && s.busy) ui.pending.append(h('span', {class: 'ed-warn', title: s.busy}, 'The Claude Documents folder needs fixing first'));
    else if (s && s.unpushed) ui.pending.append(h('span', {class: 'ed-warn', title: s.pushError || ''}, s.unpushed + (s.unpushed === 1 ? ' save is' : ' saves are') + ' only on this PC'), ' ',
      h('button', {class: 'mini', onclick: pushNow}, 'Send now'));
  }
  async function pushNow() {
    say({cls: '', text: 'Sending…'});
    const r = await post('/api/edit/push', {});
    ed.status = r.js; ed.statusAt = Date.now(); paintStatus(); DM.data.banners();
    say(r.js.ok && r.js.pushed ? {cls: 'ok', text: 'Everything is on GitHub now (' + clock() + ').'} : {cls: 'warn', text: r.js.pushError || r.js.message || 'Still couldn’t reach GitHub.'});
    if (r.js.pushed) reloadSoon();
  }

  let prevT = null;
  function onInput() {
    paintDirty();
    clearTimeout(prevT);
    tops = null;
    if (ed.conflict) paintConflictCount();
    prevT = setTimeout(() => {
      if (!ed.conflict) paintPreview();
      if (isDirty()) drafts.files[ed.file] = {base: ed.base, text: ui.text.value, at: new Date().toISOString()};
      else delete drafts.files[ed.file];
      saveDrafts();
    }, 180);
  }
  function paintPreview() {
    clear(ui.prevHead).append(h('b', null, 'Preview'), h('span', {class: 'faint'}, ' as the Files tab shows it'));
    DM.md.into(ui.prev, ui.text.value, false, {lines: true});
    tops = null; anchors = null;
  }

  async function openFile(name, jump) {
    if (!name) return;
    build();
    if (ed.loaded && ed.file !== name && isDirty()) say({cls: '', text: 'Your unsaved edit to ' + ed.file + ' is kept as a draft.'});
    ed.file = name; st.prefs.editFile = name; DM.app.save();
    ed.jump = jump || null; ed.loading = true; ed.conflict = null; paintConflict();
    if (ui.fileSel.value !== name) ui.fileSel.value = name;
    ui.text.value = ''; ui.text.disabled = true; ed.loaded = false; paintDirty();
    clear(ui.prev).append(h('div', {class: 'empty'}, 'Loading ' + name + '…'));
    let doc;
    try { doc = await getDoc(name, canEdit()); } catch (e) {
      ed.loading = false;
      clear(ui.prev).append(h('div', {class: 'empty'}, 'Couldn’t load ' + name + ': ' + e.message));
      return;
    }
    if (ed.file !== name) return; // switched meanwhile
    ed.loading = false; ed.loaded = true;
    ed.base = doc.text; ed.eol = doc.eol;
    const dr = drafts.files[name];
    let text = doc.text;
    if (dr && dr.text !== doc.text) {
      text = dr.text;
      if (dr.base !== doc.text) ed.base = dr.base; // the file moved on: saving merges the draft into it
      say({cls: 'warn', text: 'Restored your unsaved edit from ' + clock(dr.at) + (dr.base !== doc.text ? '; the file changed since, so saving will merge the two.' : '.')});
    } else if (dr) { delete drafts.files[name]; saveDrafts(); }
    ui.text.value = text;
    ui.text.disabled = !canEdit();
    paintPreview(); paintDirty();
    for (const o of ui.fileSel.options) o.textContent = label(o.value);
    if (ed.jump) goTo(ed.jump);
    else { ui.text.scrollTop = 0; ui.prevScroll.scrollTop = 0; }
  }

  // Put the caret on an entry's line (found by its title near the given line) and scroll both panes there.
  function goTo(j) {
    const lines = ui.text.value.split('\n');
    let line = Math.max(1, Math.min(lines.length, j.line || 1));
    if (j.title) {
      const key = '**' + j.title.slice(0, 40);
      let best = -1;
      lines.forEach((l, i) => { if (l.includes(key) && (best < 0 || Math.abs(i + 1 - (j.line || 1)) < Math.abs(best + 1 - (j.line || 1)))) best = i; });
      if (best >= 0) line = best + 1;
    }
    let start = 0;
    for (let i = 0; i < line - 1; i++) start += lines[i].length + 1;
    showRange(start, start + (lines[line - 1] || '').length);
    if (j.title) {
      const want = DM.plain(j.title).slice(0, 40);
      const li = [...ui.prev.querySelectorAll('li')].find(el => el.textContent.includes(want));
      if (li) { expect.prev = null; li.scrollIntoView({block: 'center'}); li.classList.add('ed-hit'); setTimeout(() => li.classList.remove('ed-hit'), 2500); }
    } else syncFrom('text');
  }

  async function save() {
    if (!canEdit() || !ed.loaded || ed.busy) return;
    const text = ui.text.value;
    if (text === ed.base && !ed.conflict) { say({cls: '', text: 'Nothing to save.'}); return; }
    if (MARKER_RE.test(text) && !confirm('The text still has conflict marker lines (<<<<<<<, =======, >>>>>>>). Save it anyway?')) return;
    ed.busy = true; paintDirty(); say({cls: '', text: 'Saving…'});
    const file = ed.file;
    const r = await post('/api/doc/save', {file, base: ed.base, text});
    ed.busy = false;
    if (file !== ed.file) return;
    const js = r.js;
    if (r.status === 200 && js.ok) {
      ed.base = js.text;
      if (js.text !== text) { const pos = ui.text.selectionStart; ui.text.value = js.text; ui.text.setSelectionRange(pos, pos); }
      ed.conflict = null; paintConflict(); paintPreview();
      delete drafts.files[file]; saveDrafts();
      say(resultText(js));
      reloadSoon();
    } else if (r.status === 409 && js.error === 'conflict') {
      ed.conflict = {current: js.current, mine: js.mine || text, conflicted: js.conflicted, n: js.conflicts};
      ed.base = js.current;               // the next save is against the newest version
      ui.text.value = js.conflicted;
      drafts.files[file] = {base: ed.base, text: js.conflicted, at: new Date().toISOString()}; saveDrafts();
      paintConflict();
      gotoConflict(1, true);
      say({cls: 'err', text: 'Not saved: someone else changed the same lines. Fix the marked parts and save again.'});
    } else {
      say({cls: 'err', text: js.message || 'Not saved.'});
    }
    paintDirty(); refreshStatus();
  }

  function paintConflict() {
    if (!ui.conflict) return;
    const c = ed.conflict;
    ui.conflict.hidden = !c;
    clear(ui.conflict);
    if (!c) { if (ui.prevHead && ed.loaded) paintPreview(); return; }
    const showRaw = (title, t) => { clear(ui.prevHead).append(h('b', null, title), h('span', {class: 'faint'}, ' (read only)')); clear(ui.prev).append(h('pre', {class: 'rawtext'}, t)); };
    ui.cCount = h('b', {class: 'ed-ccount', 'aria-live': 'polite'});
    ui.conflict.append(
      h('div', null, h('b', null, 'Someone else changed the same lines of ' + ed.file + ' since you opened it. '),
        'The editor now holds both versions where they clash (', h('code', null, '<<<<<<< your edit'), ' … ', h('code', null, '======='), ' … ',
        h('code', null, '>>>>>>> the newest version'), '). Keep what you want, delete the marker lines, then save.'),
      h('div', {class: 'ed-cbtns'},
        ui.cCount,
        h('button', {class: 'mini', title: 'Select the previous clash (Shift+F8)', onclick: () => gotoConflict(-1)}, 'Previous'),
        h('button', {class: 'mini', title: 'Select the next clash (F8)', onclick: () => gotoConflict(1)}, 'Next'),
        h('span', {class: 'sep'}),
        h('span', {class: 'faint'}, 'Right side: '),
        h('button', {class: 'mini', onclick: () => showRaw('The newest version (on GitHub now)', c.current)}, 'Newest version'),
        h('button', {class: 'mini', onclick: () => showRaw('Your edit', c.mine)}, 'Your edit'),
        h('button', {class: 'mini', onclick: paintPreview}, 'Preview'),
        h('span', {class: 'sep'}),
        h('button', {class: 'mini', title: 'Put your whole edit back in the editor; saving then replaces the other change', onclick: () => { ui.text.value = c.mine; onInput(); }}, 'Use my version'),
        h('button', {class: 'mini', title: 'Load the newest version and drop your edit', onclick: () => { ui.text.value = c.current; ed.conflict = null; paintConflict(); onInput(); say(null); }}, 'Use the newest version'),
        h('button', {class: 'mini', title: 'Back to both, with markers', onclick: () => { ui.text.value = c.conflicted; onInput(); }}, 'Show both again')));
    showRaw('The newest version (on GitHub now)', c.current);
    paintConflictCount();
  }

  // ---------- conflict navigation: each <<<<<<< ... >>>>>>> block of the text ----------
  function conflictBlocks(text) {
    const out = [];
    let pos = 0, open = -1;
    for (const line of text.split('\n')) {
      if (MARK_OPEN.test(line)) open = pos;
      else if (MARK_CLOSE.test(line) && open >= 0) { out.push([open, pos + line.length]); open = -1; }
      pos += line.length + 1;
    }
    return out;
  }
  // Which block the caret is in (or after), as "2 of 5".
  function paintConflictCount() {
    if (!ui.cCount) return;
    const bl = conflictBlocks(ui.text.value), at = ui.text.selectionStart;
    const k = bl.findIndex(b => at >= b[0] && at <= b[1]);
    ui.cCount.textContent = !bl.length ? 'No clashes left: save when ready.' :
      (k >= 0 ? 'Clash ' + (k + 1) + ' of ' + bl.length : bl.length + (bl.length === 1 ? ' clash' : ' clashes') + ' left');
    ui.cCount.classList.toggle('ok', !bl.length);
  }
  function gotoConflict(dir, first) {
    const bl = conflictBlocks(ui.text.value);
    if (!bl.length) { paintConflictCount(); return; }
    const at = ui.text.selectionStart, cur = bl.findIndex(x => at >= x[0] && at <= x[1]);
    let i;
    if (first) i = 0;
    else if (cur >= 0) i = (cur + dir + bl.length) % bl.length;
    else if (dir > 0) { i = bl.findIndex(x => x[0] > at); if (i < 0) i = 0; }
    else { i = bl.length - 1; for (let k = bl.length - 1; k >= 0; k--) if (bl[k][1] < at) { i = k; break; } }
    const b = bl[i];
    showRange(b[0], b[1]);
    paintConflictCount();
  }

  async function discard() {
    if (isDirty() && !confirm('Throw away your unsaved changes to ' + ed.file + '?')) return;
    delete drafts.files[ed.file]; saveDrafts();
    ed.conflict = null; say(null);
    await openFile(ed.file);
  }

  document.addEventListener('keydown', ev => {
    if (ev.key === 'F8' && st.prefs.tab === 'edit' && ed.sub === 'file' && ed.conflict) { ev.preventDefault(); gotoConflict(ev.shiftKey ? -1 : 1); return; }
    if (!(ev.ctrlKey || ev.metaKey) || ev.altKey || ev.key.toLowerCase() !== 's') return;
    if (st.prefs.tab !== 'edit' || !canEdit()) return;
    ev.preventDefault();
    if (ed.sub === 'file') save();
    else if (ed.sub === 'answer') saveAllAnswers();
    else if (ed.sub === 'add') addEntry();
  }, true);

  // ---------- answers ----------
  function questionList() {
    return st.data.entries.filter(e => DM.isOpen(e) && !e.file.startsWith('09') && (e.file.startsWith('07') || DM.needsQ(e)))
      .sort((a, b) => (b.file.startsWith('07') - a.file.startsWith('07')) || (DM.PRI_RANK[a.priority] || 9) - (DM.PRI_RANK[b.priority] || 9) ||
        (Date.parse((b.firstSeen || {}).time || '') || 0) - (Date.parse((a.firstSeen || {}).time || '') || 0));
  }
  // Answers already in 09 by question title (the newest), for "You answered ...".
  function answersByTitle() {
    const m = {};
    for (const e of st.data.entries) if (e.file === ANSWERS && !(e.title in m)) m[e.title] = e;
    return m;
  }
  // Open 07 questions, anything waiting on q, and 06 news for q to read (a reply to it goes to 09 too).
  function canAnswer(e) { return canEdit() && DM.isOpen(e) && !e.file.startsWith('09') && (e.file.startsWith('07') || DM.needsQ(e) || DM.waitKeys(e).includes('read')); }

  // The Answer widget: a button, then a box; opts.open starts it open (the Answer questions list).
  function answerBox(e, opts) {
    opts = opts || {};
    const wrap = h('div', {class: 'ans', dataset: {entry: e.id}});
    const prior = answered.get(e.id);
    const in09 = st.data && answersByTitle()[e.title];
    function done(text, at) {
      clear(wrap).append(h('div', {class: 'ans-done'}, h('span', {class: 'why-k'}, 'You answered' + (at ? ' at ' + clock(at) : '') + ': '), text));
    }
    if (prior) { done(prior.text, prior.at); return wrap; }
    function collapsed() {
      DM.append(clear(wrap), [h('button', {class: 'mini ans-open', onclick: ev => { ev.stopPropagation(); expanded(true); }}, 'Answer'),
        drafts.answers[e.id] ? h('span', {class: 'faint ans-hint'}, ' draft kept') : null,
        in09 ? h('span', {class: 'faint ans-hint', title: in09.body}, ' you answered already (09), answer again?') : null]);
    }
    function expanded(focus) {
      const ta = h('textarea', {class: 'ans-t', rows: 3, placeholder: 'Your answer… (Ctrl+Enter saves)', 'aria-label': 'Your answer to ' + DM.plain(e.title)});
      ta.value = drafts.answers[e.id] || '';
      const msg = h('span', {class: 'ans-msg', 'aria-live': 'polite'});
      const btn = h('button', {class: 'primary mini', onclick: () => go()}, 'Save answer');
      ta.addEventListener('input', () => { if (ta.value.trim()) drafts.answers[e.id] = ta.value; else delete drafts.answers[e.id]; saveDrafts(); });
      ta.addEventListener('keydown', ev => { if ((ev.ctrlKey || ev.metaKey) && ev.key === 'Enter') { ev.preventDefault(); go(); } });
      async function go() {
        const text = ta.value.trim();
        if (!text) { msg.className = 'ans-msg err'; msg.textContent = 'Write an answer first.'; return; }
        btn.disabled = true; msg.className = 'ans-msg'; msg.textContent = 'Saving…';
        const r = await post('/api/answer', {answers: [{entryId: e.id, title: e.title, text}]});
        btn.disabled = false;
        const res = resultText(r.js, 'Answer saved');
        if (r.js.ok) {
          answered.set(e.id, {text, at: r.js.savedAt}); delete drafts.answers[e.id]; saveDrafts();
          done(text, r.js.savedAt);
          DM.app.toast(res.text); say(res); reloadSoon();
          if (ui.qFoot) paintQFoot();
        } else { msg.className = 'ans-msg err'; msg.textContent = res.text; }
      }
      clear(wrap).append(ta, h('div', {class: 'ans-row'}, btn,
        opts.open ? null : h('button', {class: 'mini', onclick: collapsed}, 'Cancel'),
        h('span', {class: 'faint ans-hint'}, e.file.startsWith('07') ? 'Goes into 09-answers-from-q.md and marks this question answered.' :
          'Goes into 09-answers-from-q.md; this entry itself stays as it is.'), msg));
      if (focus) ta.focus();
    }
    if (opts.open || drafts.answers[e.id]) expanded(false); else collapsed();
    return wrap;
  }

  let qVersion = -1;
  function renderQuestions(force) {
    if (!force && qVersion === st.version && ui.qList.childElementCount) return;
    if (!force && ui.qList.contains(document.activeElement)) return; // don't pull the box away while q types
    qVersion = st.version;
    const list = questionList();
    clear(ui.qList);
    ui.qList.appendChild(h('p', {class: 'faint ed-intro'}, 'Every open question for you. Each answer is added to ', h('code', null, ANSWERS),
      ' (newest first; agents read it and act on it) and, for questions in 07, the question is marked answered. "Save all" saves every filled box as one change.'));
    if (!list.length) ui.qList.appendChild(h('div', {class: 'empty ok'}, 'No open questions right now.'));
    let group = null;
    for (const e of list) {
      const g = e.file.startsWith('07') ? 'Questions in "Things to ask q" (07)' : 'Other things waiting on you';
      if (g !== group) { group = g; ui.qList.appendChild(h('h3', {class: 'ed-qg'}, g)); }
      const rec = DM.overview && DM.overview.recommendation(e);
      ui.qList.appendChild(h('article', {class: 'card ed-q' + (DM.isHalting(e) ? ' halting' : e.priority === 'P1' ? ' p1' : '')},
        h('div', {class: 'card-h'}, DM.priEl(e),
          h('a', {class: 'ttl-w', href: '#entry=' + encodeURIComponent(e.id), onclick: ev => { ev.preventDefault(); DM.app.openEntry(e.id); }}, DM.md.into(h('span'), e.title, true)),
          h('span', {class: 'spacer'}), editButton(e)),
        h('div', {class: 'card-m'}, DM.fileTitle(e.file), e.section ? ' › ' + e.section : '', h('span', {class: 'dotsep'}), DM.data.threadLink(e.thread, e.threadUrl),
          e.date ? [h('span', {class: 'dotsep'}), DM.dateOnly(e.date)] : null),
        e.body ? DM.md.into(h('div', {class: 'md card-b'}), e.body) : null,
        rec ? h('div', {class: 'nq-rec'}, h('span', {class: 'why-k'}, 'Recommended: '), DM.md.into(h('span'), rec, true)) : null,
        answerBox(e, {open: true})));
    }
    paintQFoot();
  }
  function paintQFoot() {
    clear(ui.qFoot);
    const ids = questionList().filter(e => drafts.answers[e.id] && !answered.has(e.id));
    ui.qFoot.hidden = !canEdit();
    ui.qFoot.append(h('button', {class: 'primary', disabled: !ids.length, onclick: saveAllAnswers}, ids.length > 1 ? 'Save all ' + ids.length + ' answers' : ids.length ? 'Save 1 answer' : 'Save all answers'),
      h('span', {class: 'faint'}, ' every filled box, as one change (Ctrl+S)'));
  }
  if (typeof document !== 'undefined') document.addEventListener('input', ev => { if (ev.target.classList && ev.target.classList.contains('ans-t') && ui.qFoot) setTimeout(paintQFoot, 0); });

  async function saveAllAnswers() {
    const es = questionList().filter(e => drafts.answers[e.id] && drafts.answers[e.id].trim() && !answered.has(e.id));
    if (!es.length) { say({cls: '', text: 'No answers to save: write one in a box first.'}); return; }
    say({cls: '', text: 'Saving ' + es.length + ' answers…'});
    const r = await post('/api/answer', {answers: es.map(e => ({entryId: e.id, title: e.title, text: drafts.answers[e.id].trim()}))});
    const res = resultText(r.js, es.length === 1 ? 'Answer saved' : es.length + ' answers saved');
    if (r.js.ok) {
      for (const e of es) { answered.set(e.id, {text: drafts.answers[e.id].trim(), at: r.js.savedAt}); delete drafts.answers[e.id]; }
      saveDrafts(); renderQuestions(true); reloadSoon();
    }
    say(res);
  }

  // ---------- add an entry ----------
  const af = {};
  function buildAddForm() {
    af.file = h('select', {id: 'adFile', onchange: () => loadSections()});
    af.section = h('select', {id: 'adSection'});
    af.title = h('input', {type: 'text', id: 'adTitle', placeholder: 'Short title', maxlength: 300});
    af.prio = h('select', {id: 'adPrio'}, [['', 'none'], ['HALT', 'HALT (a Claude is stopped)'], ['P1', 'P1 (highest)'], ['P2', 'P2'], ['P3', 'P3 (lowest)']].map(([v, l]) => h('option', {value: v}, l)));
    af.prioHint = h('span', {class: 'faint ad-h'}, 'HALT: a Claude agent can’t continue until you act. Start the text with what has to be done.');
    af.status = h('select', {id: 'adStatus'}, STATUSES.map(s => h('option', {value: s}, s + ' (' + DM.statusWord(s).toLowerCase() + ')')));
    af.body = h('textarea', {id: 'adBody', rows: 6, placeholder: 'What and where. Source.'});
    af.out = h('pre', {class: 'rawtext ad-line'});
    af.prev = h('div', {class: 'md ad-prev'});
    af.msg = h('div', {class: 'ad-msg', 'aria-live': 'polite'});
    af.btn = h('button', {class: 'primary', onclick: () => addEntry()}, 'Add entry');
    for (const el of [af.title, af.body]) el.addEventListener('input', paintAddPreview);
    for (const el of [af.prio, af.status, af.section]) el.addEventListener('change', paintAddPreview);
    af.prio.addEventListener('change', () => af.prioHint.classList.toggle('halt-hint', af.prio.value === 'HALT'));
    const row = (lbl, el, hint) => h('label', {class: 'ad-row'}, h('span', {class: 'ad-l'}, lbl), el, hint ? h('span', {class: 'faint ad-h'}, hint) : null);
    // (wide screens: the form on the left, what it writes beside it)
    return h('div', {class: 'ad-form'}, h('div', {class: 'ad-in'},
      h('p', {class: 'faint'}, 'Adds one entry at the top of a section (newest first), dated today, with q as its thread, and pushes it.'),
      row('File', af.file), row('Section', af.section), row('Title', af.title), h('label', {class: 'ad-row'}, h('span', {class: 'ad-l'}, 'Priority'), af.prio, af.prioHint), row('Status', af.status),
      row('Text', af.body, 'One paragraph; line breaks become spaces.'),
      h('div', {class: 'ad-row'}, h('span', {class: 'ad-l'}), h('div', null, af.btn, af.msg))),
      h('div', {class: 'ad-side'}, h('div', {class: 'ad-row'}, h('span', {class: 'ad-l'}, 'Writes'), h('div', {class: 'ad-out'}, af.out, af.prev))));
  }
  function syncAddForm() {
    const names = entryFileNames();
    const cur = af.file.value || (st.prefs.addFile && names.includes(st.prefs.addFile) ? st.prefs.addFile : names.find(n => n.startsWith('03')) || names[0]);
    clear(af.file);
    for (const n of names) af.file.appendChild(h('option', {value: n}, n.slice(0, 2) + ' ' + DM.fileTitle(n)));
    af.file.value = cur;
    if (af.loadedFor !== cur) loadSections();
    af.btn.disabled = !canEdit();
    paintAddPreview();
  }
  async function loadSections() {
    const name = af.file.value;
    af.loadedFor = name; st.prefs.addFile = name; DM.app.save();
    const keep = af.section.value;
    let secs = (st.fileByName[name] || {}).sections || [];
    try { secs = (await getDoc(name, false)).sections; } catch (_) { /* use the mirror's */ }
    if (af.file.value !== name) return;
    clear(af.section);
    for (const s of secs) af.section.appendChild(h('option', {value: s}, s || '(top of the file, no section)'));
    af.section.value = secs.includes(keep) ? keep : (secs.find(s => s.toLowerCase() !== 'archive') || secs[0] || '');
    paintAddPreview();
  }
  function today() {
    const p = {};
    for (const x of new Intl.DateTimeFormat('en-US', {timeZone: 'America/Chicago', year: 'numeric', month: '2-digit', day: '2-digit'}).formatToParts(new Date())) p[x.type] = x.value;
    return p.year + '-' + p.month + '-' + p.day;
  }
  function entryLine() { // mirrors docsedit.format_entry
    const t = af.title.value.replace(/\s+/g, ' ').trim(), pr = af.prio.value;
    let b = af.body.value.replace(/\s+/g, ' ').trim();
    if (!t) return '';
    const head = pr ? '**' + t.replace(/[ .:]+$/, '') + '** (' + pr + ').' : '**' + (/[.:?!]$/.test(t) ? t : t + '.') + '**';
    if (b && !/[.!?)`"']$/.test(b)) b += '.';
    return '- ' + head + (b ? ' ' + b : '') + ' _' + today() + ', q, ' + af.status.value + '_';
  }
  function paintAddPreview() {
    const l = entryLine();
    af.out.textContent = l || '(write a title)';
    DM.md.into(af.prev, l);
  }
  async function addEntry() {
    if (!canEdit()) return;
    if (!af.title.value.trim()) { af.msg.className = 'ad-msg err'; af.msg.textContent = 'The entry needs a title.'; af.title.focus(); return; }
    af.btn.disabled = true; af.msg.className = 'ad-msg'; af.msg.textContent = 'Saving…';
    const body = {file: af.file.value, section: af.section.value, title: af.title.value, priority: af.prio.value, body: af.body.value, status: af.status.value};
    const r = await post('/api/entry/add', body);
    af.btn.disabled = false;
    const res = resultText(r.js, 'Entry added');
    clear(af.msg).className = 'ad-msg ' + res.cls;
    af.msg.append(res.text);
    if (r.js.ok) {
      const file = r.js.file, line = r.js.line, title = af.title.value.trim();
      af.msg.append(' ', h('button', {class: 'mini', onclick: () => openAt(file, line, title)}, 'Open it in the editor'));
      af.title.value = ''; af.body.value = ''; paintAddPreview();
      if (ed.file === file && ed.loaded && !isDirty()) { ed.base = r.js.text; ui.text.value = r.js.text; paintPreview(); paintDirty(); }
      say(res); reloadSoon();
    }
  }

  // ---------- used elsewhere ----------
  function editButton(e) {
    if (!canEdit() || !editable(e.file)) return null;
    return h('button', {class: 'mini ed-btn', title: 'Edit this entry in ' + e.file, onclick: ev => { ev.stopPropagation(); DM.app.closeDialog(); openAt(e.file, e.line, e.title); }}, 'Edit');
  }
  function openAt(file, line, title) {
    if (!canEdit()) return;
    ed.sub = 'file'; st.prefs.editSub = 'file';
    DM.app.show('edit');
    if (ed.file === file && ed.loaded) { goTo({line, title}); return; }
    openFile(file, {line, title});
  }

  // #answer=<entryId>: straight to that question's answer box (the Supervisor Board's HALT links). An open question
  // goes to Edit > Answer questions; anything else answerable opens its card with the box open; otherwise the entry.
  function openAnswer(id) {
    const e = st.entryById[id];
    if (!e || !canAnswer(e)) { DM.app.openEntryInList(id); return false; }
    if (questionList().includes(e)) {
      ed.sub = 'answer'; st.prefs.editSub = 'answer'; DM.app.save();
      DM.app.show('edit', {keepHash: true});
      renderQuestions(true);
      const box = ui.qList.querySelector('.ans[data-entry="' + CSS.escape(id) + '"]');
      const ta = box && box.querySelector('textarea');
      if (box) {
        const art = box.closest('article');
        art.scrollIntoView({block: 'center'});
        art.classList.remove('flash'); void art.offsetWidth; art.classList.add('flash');
        if (ta) ta.focus({preventScroll: true});
      }
      return true;
    }
    DM.app.openEntry(id);
    const open = document.querySelector('#dlgBody .ans-open');
    if (open) open.click();
    return true;
  }

  function dataChanged() {
    const b = $('#tabs button[data-tab="edit"]');
    if (b) b.hidden = !showTab();
    for (const id of [...answered.keys()]) { const e = st.entryById[id]; if (!e || DM.isDone(e)) answered.delete(id); }
    if (ed.sub === 'answer' && st.prefs.tab === 'edit') renderQuestions();
  }

  DM.edit = {canEdit, showTab, answerBox, canAnswer, editButton, openAt, dataChanged, refreshStatus, resultText, editStatus, pushNow, editable, openAnswer};
  (DM.views = DM.views || {}).edit = {render: () => { render(); refreshStatus(); }, scroller: () => ed.sub === 'file' ? null : (ed.sub === 'answer' ? ui.paneAnswer : ui.paneAdd)};
})(window.DM);
