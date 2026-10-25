// Quark UI customizer - the editor: page list, canvas editing, inspector, undo, autosave, import/export.
'use strict';

(() => {
  const $ = (id) => document.getElementById(id);
  const readJson = (id) => {
    const el = $(id), t = el ? el.textContent.trim() : '';
    if (!t || t.startsWith('/*__')) return null;
    try { return JSON.parse(t); } catch (err) { console.warn('bad embedded JSON in #' + id, err); return null; }
  };
  const build = readJson('buildInfo') || {};
  // storage namespace: a proposal build (build.py --layout) has its own, so it never reads or overwrites the plain tool's
  const NS = build.storeNs || 'quark-ui-customizer';
  const STORE_KEY = NS + '/layout/v1';
  const PREFS_KEY = NS + '/prefs/v1';
  const PAGE_KEY = NS + '/page/v1';   // the page on screen (small, written on every page switch; the layout is not)
  const UNDO_MAX = 200;
  const INTERACTIVE = new Set(['button', 'toggle', 'cycle', 'slider', 'text', 'key', 'custom', 'colorpicker', 'dropdown', 'handle', 'view']);
  const KNOWN = new Set(['kind', 'x', 'y', 'w', 'h', 'label', 'value', 'options', 'min', 'max', 'def', 'fmt', 'maxLen', 'enabled', 'color',
    'textScale', 'align', 'style', 'action', 'id', 'where', 'scope', 'dynamic', 'uid', 'origId', 'notes', 'copyOf']);
  const PALETTE = [['button', 'Button'], ['toggle', 'Toggle'], ['cycle', 'Cycle'], ['slider', 'Slider'], ['text', 'Text field'],
    ['key', 'Keybind'], ['label', 'Label'], ['panel', 'Panel'], ['colorpicker', 'Color picker'], ['dropdown', 'Dropdown'],
    ['rect', 'Rectangle'], ['view', '3D view']];
  const GROUP_NAMES = { main: 'main menu', ingame: 'in-game', 'editor-online': 'editor online' };

  // ---- data ------------------------------------------------------------------------------------------------------
  const dump = readJson('dumpData');
  const baseline = QModel.normalizeDump(dump || {});
  // build.py --layout: a layout (EXPORT LAYOUT format) the tool starts from when this browser has nothing saved;
  // Reset goes back to it, "Reset to game default" to the game's layout
  const startLayout = (() => {
    const s = readJson('startLayout');
    if (!s) return null;
    try { const doc = QModel.sanitizeWorking(s.doc || s); return doc.pages.length ? { data: s, json: JSON.stringify(doc) } : null; } catch (e) { console.warn('embedded start layout unreadable', e); return null; }
  })();
  const W = Number(baseline.source.width) || 1280, H = Number(baseline.source.height) || 720;
  const SIG = QModel.signature(baseline);
  const baseEls = new Map(), basePageOf = new Map();   // origId -> baseline element / the origId of its page
  for (const p of baseline.pages) for (const e of p.elements) { baseEls.set(e.origId, e); basePageOf.set(e.origId, p.origId); }
  QRender.setStyle(baseline.style);

  QFonts.init(readJson('fontCatalog'), readJson('fontFiles'), { ns: NS });
  const S = {
    doc: null, pageUid: null, sel: new Set(), undo: [], redo: [], clip: null, hover: null, drag: null, gesture: null,
    diff: null, search: '', guides: [], lastNudge: null, saveState: '', provState: '', pasteN: 0,
    prefs: { grid: false, gridSize: 10, ghost: true, shot: false, shotOpacity: 50, zoom: 'fit', widgetsOnly: false, specLayout: 'changed' },
    // font trial (saved with the layout): null = the font's own default
    font: { id: QFonts.DEFAULT_ID, weight: null, ls: null, upper: null, sizeK: null, mult: 1, minTs: 0, cmp: '', side: false },
    flip: false, sheet: false, tour: 0, originFilter: '', view: '',
  };

  const store = {
    get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); return true; } catch (e) { return false; } },
    del(k) { try { localStorage.removeItem(k); } catch (e) { /* none */ } },
  };

  function loadInitial() {
    try { Object.assign(S.prefs, JSON.parse(store.get(PREFS_KEY) || '{}')); } catch (e) { /* defaults */ }
    let doc = null;
    const raw = store.get(STORE_KEY);
    if (raw) {
      try {
        const saved = JSON.parse(raw);
        doc = QModel.sanitizeWorking(QModel.expandBaseRefs(baseline, saved.doc));
        const refs = saved.doc && Array.isArray(saved.doc.pages) && saved.doc.pages.some((p) => p && p.$base);
        if (saved.baseSig !== SIG) setTimeout(() => toast('The saved layout was made against another dump (' + String(saved.source && saved.source.commit || '?').slice(0, 8) + '). ' +
          (refs ? 'Your changed pages are kept as you left them; the others now show this dump.' : 'Kept it as it was.') + ' Reset starts over.', 7000), 300);
        S.pageUid = store.get(PAGE_KEY) || saved.pageUid || null;
        if (saved.font) S.font = sanitizeFont(saved.font);
        else if (fontOfDoc(doc)) S.font = fontOfDoc(doc);
      } catch (e) { console.warn('saved layout unreadable, starting from the default', e); doc = null; }
    }
    if (!doc && startLayout) {
      doc = JSON.parse(startLayout.json);
      S.pageUid = startLayout.data.pageUid || null;
      S.font = startFont(doc);
    }
    S.doc = doc || QModel.makeWorking(baseline);
    if (!S.doc.pages.length) S.doc.pages.push(QModel.newPage('new-page', 'New page'));
    if (!page()) S.pageUid = S.doc.pages[0].uid;
    markProposal();
    syncSeen();
  }

  const page = () => S.doc.pages.find((p) => p.uid === S.pageUid) || null;
  const curPage = () => page() || S.doc.pages[0];
  const selected = () => { const p = curPage(); return p ? p.elements.filter((e) => S.sel.has(e.uid)) : []; };
  const getDiff = () => S.diff || (S.diff = QModel.diff(baseline, S.doc));
  const diffEntry = (p) => getDiff().pages.find((e) => e.page === p);

  // ---- undo -----------------------------------------------------------------------------------------------------
  // A snapshot is one JSON string per page; a page that didn't change reuses the previous snapshot's string, so 200
  // undo steps over a 100-page layout cost about one copy of the layout plus the pages that were edited.
  // Only the pages an edit can have changed are stringified again: the current page, pages handed to touch() (a move
  // to another page) and page objects not seen yet (new, imported, restored). Every edit ends in changed(), which
  // refreshes them, so a page nobody touched keeps its string: an edit costs one page, not the whole layout (1.7 MB).
  // Something that changes many pages outside an undo step calls forgetPages().
  let pageJson = new WeakMap();   // page object -> its JSON as of the last snapshot
  const touched = new Set();
  const touch = (p) => { if (p) touched.add(p); };
  const forgetPages = () => { pageJson = new WeakMap(); };
  function docSnap() {
    touch(curPage());
    const out = S.doc.pages.map((p) => {
      const c = pageJson.get(p);
      if (c !== undefined && !touched.has(p)) return c;
      const j = JSON.stringify(p);
      if (c === j) return c;
      pageJson.set(p, j);
      return j;
    });
    touched.clear();
    return out;
  }
  const snap = () => ({ pages: docSnap(), pageUid: S.pageUid, sel: [...S.sel] });
  const differs = (s) => { const now = docSnap(); return now.length !== s.pages.length || now.some((j, i) => j !== s.pages[i]); };
  function restore(s) {
    // pages the snapshot has unchanged keep their objects (and cached strings); the others are parsed
    const keep = new Map();
    for (const p of S.doc.pages) { const j = pageJson.get(p); if (j !== undefined) keep.set(j, p); }
    const pages = s.pages.map((j) => {
      let p = keep.get(j);
      if (p) keep.delete(j);
      else { p = JSON.parse(j); pageJson.set(p, j); }
      return p;
    });
    S.doc = Object.assign({}, S.doc && S.doc.font ? { font: S.doc.font } : {}, { pages });
    S.pageUid = s.pageUid;
    S.sel = new Set(s.sel);
    if (!page()) S.pageUid = S.doc.pages[0] && S.doc.pages[0].uid;
  }
  function pushUndo(before) {
    S.undo.push(before);
    if (S.undo.length > UNDO_MAX) S.undo.shift();
    S.redo = [];
  }
  // Runs fn as one undo step (nothing is recorded when it changed nothing). Refused while a resolution preview is on
  // (readOnly), except for what replaces the whole layout and shows in the preview (import, reset: opts.anyView).
  function commit(fn, opts = {}) {
    if (!opts.anyView && readOnly()) return;
    const before = snap();
    fn();
    if (differs(before)) { pushUndo(before); changed(opts); }
    else if (!opts.quiet) renderAll();
  }
  function undo() {
    endGesture();
    if (!S.undo.length) return toast('Nothing to undo');
    S.redo.push(snap());
    restore(S.undo.pop());
    changed({ history: true });
  }
  function redo() {
    endGesture();
    if (!S.redo.length) return toast('Nothing to redo');
    S.undo.push(snap());
    restore(S.redo.pop());
    changed({ history: true });
  }
  // The resolution preview is read only: every edit path asks this first (it says why when it refuses).
  function readOnly() {
    if (!viewOf()) return false;
    toast('Preview at ' + S.view + ' is read only: pick View: 1280x720 to edit');
    return true;
  }
  // Inspector typing: the first input of a field starts a gesture, its change event ends it (one undo step).
  function beginGesture() { if (!S.gesture) S.gesture = snap(); }
  function endGesture() {
    if (!S.gesture) return;
    if (differs(S.gesture)) pushUndo(S.gesture);
    S.gesture = null;
  }

  // ---- provenance marks ------------------------------------------------------------------------------------------
  // page.prov (QModel.markHuman / markAi): which pages the owner changed. A page's body without its mark is remembered
  // after every change; an edit that changes it marks the page 'human' (with today's date). The mark is part of the page,
  // so undo restores it with the page: undoing every edit of a page brings back its loaded state, mark included.
  // Only pages whose snapshot string changed since the last change are looked at (docSnap() keeps that cheap).
  let seenPages = new WeakMap();   // page object -> {json: its snapshot string, body: its JSON without the mark}, as of the last change
  const pageBody = (p) => { const { prov, ...rest } = p; return JSON.stringify(rest); };
  // After undo / redo / import / reset: what is there now is the starting point (no marks).
  function syncSeen() {
    const now = docSnap();
    S.doc.pages.forEach((p, i) => { const s = seenPages.get(p); if (!s || s.json !== now[i]) seenPages.set(p, { json: now[i], body: pageBody(p) }); });
  }
  function markEdits() {
    const today = QModel.ymd(new Date());
    const now = docSnap();
    S.doc.pages.forEach((p, i) => {
      const s = seenPages.get(p);
      if (s && s.json === now[i]) return;
      const body = pageBody(p);
      if (!s || s.body !== body) { QModel.markHuman(p, today); touch(p); }   // (a page an edit made is the owner's too)
      seenPages.set(p, { json: null, body });
    });
    const after = docSnap();   // (the marks are part of the page: its string again)
    S.doc.pages.forEach((p, i) => { const s = seenPages.get(p); if (s.json === null) s.json = after[i]; });
  }
  // a proposal build: every page that differs from the game default and has no mark is the AI's
  const markProposal = () => { if (startLayout) { QModel.markAi(baseline, S.doc); forgetPages(); S.diff = null; } };

  // ---- save ------------------------------------------------------------------------------------------------------
  let saveTimer = 0;
  // the layout carries its font too (doc.font {id, weight}: what a redesign script sets, e.g. redesign/logo/apply-logo.js)
  const syncDocFont = () => { if (S.doc) QModel.setLayoutFont(S.doc, S.font.id, S.font.weight != null ? S.font.weight : null); };
  // S.font from a layout's doc.font (null when it has none)
  const fontOfDoc = (doc) => { const f = QModel.layoutFont(doc); return f ? sanitizeFont(Object.assign({}, S.font, { id: f.id, weight: f.weight, ls: null, upper: null, sizeK: null })) : null; };
  const payloadHead = () => (syncDocFont(), { format: 'quark-ui-layout', version: 1, savedAt: new Date().toISOString(), baseSig: SIG, source: baseline.source,
    pageUid: S.pageUid, font: S.font });
  const payload = () => Object.assign(payloadHead(), { doc: S.doc });
  // The autosave: the pages from the snapshot strings (no second stringify of the whole layout), and a page that is
  // still the game's as a reference to the dump (QModel.baseRefJson): a few KB instead of 1.7 MB per save.
  function payloadJson() {
    const head = payloadHead(), { pages, ...rest } = S.doc, open = (o) => JSON.stringify(o).slice(0, -1) + (Object.keys(o).length ? ',' : '');
    const entries = new Map(getDiff().pages.map((e) => [e.page, e]));
    const snap = docSnap().map((j, i) => QModel.baseRefJson(entries.get(S.doc.pages[i]), j));
    return open(head) + '"doc":' + open(rest) + '"pages":[' + snap.join(',') + ']}}';
  }
  function saveNow() {
    clearTimeout(saveTimer);
    saveTimer = 0;
    S.saveState = store.set(STORE_KEY, payloadJson()) ? 'saved in this browser' : 'NOT saved (browser storage unavailable: export the layout)';
    renderStatus();
  }
  function scheduleSave() { clearTimeout(saveTimer); S.saveState = 'saving...'; saveTimer = setTimeout(saveNow, 400); }
  const savePrefs = () => store.set(PREFS_KEY, JSON.stringify(S.prefs));

  // Something in the layout changed: recompute the diff, mark the pages the edit changed as made by the owner (not for
  // undo / redo / import / reset: opts.history, opts.noMark), save, redraw (the inspector too unless it's being typed in).
  function changed(opts = {}) {
    S.diff = null;
    if (opts.history || opts.noMark) syncSeen();
    else markEdits();
    scheduleSave();
    if (opts.keepInspector) { renderPages(); requestRender(); renderOffCanvas(); renderStatus(); refreshInspectorMarks(); updateButtons(); }
    else renderAll();
  }

  // ---- helpers ---------------------------------------------------------------------------------------------------
  let toastTimer = 0;
  function toast(msg, ms = 2200) {
    const t = $('toast');
    t.textContent = msg;
    t.classList.add('on');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => t.classList.remove('on'), ms);
  }
  function el(tag, attrs = {}, ...kids) {
    const n = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (v === undefined || v === null || v === false) continue;
      if (k === 'class') n.className = v;
      else if (k === 'text') n.textContent = v;
      else if (k.startsWith('on')) n.addEventListener(k.slice(2), v);
      else if (k in n && typeof v !== 'string') n[k] = v;
      else n.setAttribute(k, v === true ? '' : v);
    }
    for (const c of kids.flat()) if (c != null && c !== false) n.append(c.nodeType ? c : document.createTextNode(String(c)));
    return n;
  }
  // In-page dialogs instead of prompt() / confirm() / alert(): the Quark Project app's Electron window has no
  // window.prompt() (it throws), and native boxes block the page. Resolves with the typed text (input), true (OK) or
  // null (Cancel, Esc, a click outside). check(text) returns why the text can't be used ('' = fine).
  let dlgDone = null;
  function ask({ title, text = '', input = null, ok = 'OK', cancel = 'Cancel', danger = false, check = null }) {
    if (dlgDone) dlgDone(null);
    const inp = $('dlgInput'), err = $('dlgErr');
    $('dlgTitle').textContent = title;
    $('dlgText').textContent = text;
    inp.hidden = input === null;
    inp.value = input === null ? '' : String(input);
    err.textContent = '';
    $('dlgOk').textContent = ok;
    $('dlgOk').className = danger ? 'danger primary' : 'primary';
    $('dlgCancel').hidden = !cancel;
    $('dlgCancel').textContent = cancel || '';
    $('dlg').hidden = false;
    const back = document.activeElement;
    if (input !== null) { inp.focus(); inp.select(); } else $('dlgOk').focus();
    return new Promise((resolve) => {
      const why = () => (check && input !== null ? check(inp.value) || '' : '');
      inp.oninput = () => { err.textContent = why(); };
      $('dlgForm').onsubmit = (ev) => {
        ev.preventDefault();
        const w = why();
        if (w) { err.textContent = w; inp.focus(); return; }
        finish(input === null ? true : inp.value);
      };
      $('dlgCancel').onclick = () => finish(null);
      function finish(v) {
        dlgDone = null;
        $('dlg').hidden = true;
        if (back && back.focus && back !== document.body) back.focus();
        resolve(v);
      }
      dlgDone = finish;
    });
  }
  const confirmed = (title, text, ok, danger = true) => ask({ title, text, ok, danger }).then((r) => r === true);
  const tell = (title, text) => ask({ title, text, cancel: null });
  function download(name, text, type) {
    const url = URL.createObjectURL(new Blob([text], { type }));
    const a = el('a', { href: url, download: name });
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 2000);
  }
  const isTyping = (t) => t && (t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' ||
    (t.tagName === 'INPUT' && !['checkbox', 'range', 'button', 'file', 'color'].includes(t.type)));
  const r1 = (n) => Math.round(n);
  // Page groups: a screen with sub-pages is a group with its sub-pages (host, host/advanced ...); screens on their own
  // (connecting, demos, join) share "menu". The main menu comes first, then the other menus, then the rest as listed.
  const segOf = (p) => String(p.path || p.id || '').split(/[/ ]/)[0];
  function pageGroups(pages) {
    const n = new Map();
    for (const p of pages) n.set(segOf(p), (n.get(segOf(p)) || 0) + 1);
    const groups = new Map([['main menu', []], ['menu', []]]);
    for (const p of pages) {
      const seg = segOf(p);
      const g = GROUP_NAMES[seg] || (n.get(seg) > 1 ? seg : p.editor ? 'editor' : p.inGame ? 'in-game' : 'menu');
      if (!groups.has(g)) groups.set(g, []);
      groups.get(g).push(p);
    }
    for (const [g, list] of groups) if (!list.length) groups.delete(g);
    return groups;
  }
  // Who made a page: in this layout, a changed page is the owner's (or the AI proposal's: its mark, as Export records
  // it); a page as the game ships it is what the game's registry says (assets/provenance/ui.json).
  const ORIGINS = build.origins || {};
  function originOf(p, entry) {
    if (entry && entry.status !== 'unchanged') {
      const m = QModel.sanitizeProv(p.prov);
      return { origin: m ? m.origin : startLayout ? 'ai' : 'human', why: m ? (m.origin === 'ai' ? 'AI proposal' : 'changed by you' + (m.from === 'ai' ? ' (on the AI proposal)' : '')) : 'changed in this layout' };
    }
    const o = ORIGINS[QModel.registryKey(QModel.provKey(p))];
    return { origin: o || 'unknown', why: o ? 'the game\'s page (assets/provenance/ui.json)' : 'not in the game\'s provenance registry' };
  }
  // Full-screen rects / panels are the page's backdrop: not picked by clicks or rubber bands (select them below the page).
  const isBackdrop = (e) => ((e.kind === 'rect' || e.kind === 'panel') && e.x <= 1 && e.y <= 1 && e.w >= W * 0.95 && e.h >= H * 0.95) ||
    (e.kind === 'view' && e.w * e.h >= W * H * 0.4);   // (the editor's / spectator's big 3D view)
  // Decoration: the game's own label / rect / panel draws. "Widgets only" makes them unpickable on the canvas.
  const isDecor = (e) => e.kind === 'label' || e.kind === 'rect' || e.kind === 'panel';
  const pickable = (e) => !isBackdrop(e) && !(S.prefs.widgetsOnly && isDecor(e)) && boxOf(e);
  const shotUrl = (p) => {
    const s = String(p).replace(/\\/g, '/');
    return /^(extract\/|[a-z]+:|\/|\.\.?\/)/i.test(s) ? s : 'extract/' + s;
  };
  function uniquePageId(id) {
    let out = id, k = 2;
    while (S.doc.pages.some((p) => p.id === out)) out = id + '-' + k++;
    return out;
  }
  // Panels and plain rectangles go under the widgets (drawn first); everything else on top.
  function insertElement(p, e, index) {
    if (index != null) { p.elements.splice(index, 0, e); return; }
    if (e.kind === 'panel' || e.kind === 'rect') {
      const i = p.elements.findIndex((x) => x.kind !== 'panel' && x.kind !== 'rect');
      p.elements.splice(i < 0 ? p.elements.length : i, 0, e);
    } else p.elements.push(e);
  }
  const allUids = () => { const s = new Set(); for (const p of S.doc.pages) for (const e of p.elements) s.add(e.uid); return s; };
  const placedOrig = () => { const s = new Set(); for (const p of S.doc.pages) for (const e of p.elements) if (e.origId) s.add(e.origId); return s; };

  // ---- canvas ----------------------------------------------------------------------------------------------------
  const cv = $('cv'), ov = $('ov'), shot = $('shot');
  const ctx = cv.getContext('2d'), octx = ov.getContext('2d');
  let zoom = 1, dpr = 1, raf = 0;

  // ---- preview at another resolution (read only) -------------------------------------------------------------------
  // The game lays the UI out in 720p units times ui.s() = height / 720, and its text at scale max(1, round(2 * s))
  // (Ui::begin). In those units every screen is 720 tall and 720 * aspect wide: a 16:9 screen is the dump's frame (only
  // the text rounds: 1080p and 1440p are exact), 16:10 is narrower (1152), 21:9 wider (1680). The dump has the widgets'
  // positions, not the code's anchors, so this guesses them as the menus are written: a widget (or the panel around it)
  // that hugs the left / right edge stays on that edge, a full-width bar stretches, everything else keeps its offset
  // from the centre (cx + (x - 640) * s). Widgets that end up off the screen are outlined in red.
  const VIEWS = [['', 'View: 1280x720 (edit)'], ['1920x1080', '1920x1080 16:9'], ['2560x1440', '2560x1440 16:9'], ['3840x2160', '3840x2160 16:9'],
    ['1920x1200', '1920x1200 16:10'], ['2560x1600', '2560x1600 16:10'], ['2560x1080', '2560x1080 21:9'], ['3440x1440', '3440x1440 21:9'],
    ['5120x1440', '5120x1440 32:9'], ['1280x1024', '1280x1024 5:4']];
  const EDGE = 48;   // (720p px: closer than this to a side edge = pinned to it)
  function viewOf() {
    if (!S.view || S.sheet) return null;
    const [w, h] = S.view.split('x').map(Number), s = h / H;
    return { w, h, s, fw: Math.round(w / s), ts: Math.max(1, Math.round(2 * s)), k: Math.max(1, Math.round(2 * s)) / (2 * s) };
  }
  const frameW = () => { const v = viewOf(); return v ? v.fw : W; };
  function anchorOf(b) {
    if (b.x <= 1 && b.x + b.w >= W - 1) return 'stretch';
    if (b.x < EDGE && b.x + b.w <= W / 2) return 'left';
    if (b.x + b.w > W - EDGE && b.x >= W / 2) return 'right';
    return 'center';
  }
  // The page with every widget moved as the game would at that size: {page, clipped: [boxes], pinned: n}.
  function previewPage(p, v) {
    const boxes = p.elements.map((e) => boxOf(e));
    const conts = p.elements.map((e, i) => ({ e, b: boxes[i] }))
      .filter((c) => c.b && (c.e.kind === 'panel' || c.e.kind === 'rect') && !isBackdrop(c.e) && anchorOf(c.b) !== 'stretch');
    const inside = (a, c) => a !== c && a.x >= c.x - 0.5 && a.y >= c.y - 0.5 && a.x + a.w <= c.x + c.w + 0.5 && a.y + a.h <= c.y + c.h + 0.5;
    const clipped = [];
    let pinned = 0;
    const elements = p.elements.map((e, i) => {
      const b = boxes[i];
      if (!b) return e;
      let outer = null;   // (the biggest panel around it decides: a panel's labels move with the panel)
      for (const c of conts) if (inside(b, c.b) && (!outer || c.b.w * c.b.h > outer.w * outer.h)) outer = c.b;
      const a = anchorOf(outer || b);
      let dx = a === 'left' ? 0 : a === 'right' ? v.fw - W : (v.fw - W) / 2;
      const m = Object.assign({}, e);
      if (a === 'stretch') { m.x = 0; m.w = v.fw; dx = 0; } else m.x = e.x + dx;
      if (a === 'left' || a === 'right') pinned++;
      const nb = { x: b.x + dx, y: b.y, w: a === 'stretch' ? v.fw : b.w, h: b.h };
      if (nb.x < -0.5 || nb.x + nb.w > v.fw + 0.5) clipped.push(nb);
      return m;
    });
    return { page: Object.assign({}, p, { elements }), clipped, pinned };
  }
  // draws page p (a preview's mapped page when one is on) with the preview's text rounding
  function drawFrame(c, p, hover) {
    const v = viewOf();
    if (!v) { QRender.drawPage(c, p, W, H, hover); return null; }
    const pv = previewPage(p, v);
    QRender.setTextOpts({ mult: S.font.mult * v.k, minTs: S.font.minTs });
    try { QRender.drawPage(c, pv.page, v.fw, H, null); } finally { QRender.setTextOpts({ mult: S.font.mult, minTs: S.font.minTs }); }
    return pv;
  }
  function setView(id) {
    S.view = VIEWS.some(([v]) => v === id) ? id : '';
    if (S.view) { S.sel = new Set(); S.hover = null; ov.style.cursor = ''; }
    $('sView').value = S.view;
    $('stage').classList.toggle('preview', !!S.view);
    // (read only: the palette and + Page are off, the inspector is drawn disabled; commit() refuses the rest)
    for (const b of [$('bAddPage'), ...$('palette').children]) b.disabled = !!S.view;
    layoutStage();
    renderAfterSelect();
  }

  function layoutStage() {
    const wrap = $('stageWrap');
    const side = sideOn(), FW = frameW();
    wrap.classList.toggle('side', side || !!viewOf());
    $('stage2').hidden = !side;
    const fit = side ? Math.max(0.1, Math.min((wrap.clientWidth - 36) / (2 * FW), (wrap.clientHeight - 36) / H))
      : Math.max(0.1, Math.min((wrap.clientWidth - 24) / FW, (wrap.clientHeight - (viewOf() ? 36 : 24)) / H));
    zoom = S.prefs.zoom === 'fit' ? fit : Number(S.prefs.zoom) || 1;
    dpr = window.devicePixelRatio || 1;
    const st = $('stage');
    st.style.width = FW * zoom + 'px';
    st.style.height = H * zoom + 'px';
    for (const c of [cv, ov]) { c.width = Math.round(FW * zoom * dpr); c.height = Math.round(H * zoom * dpr); }
    if (side) {
      const s2 = $('stage2'), c2 = $('cv2');
      s2.style.width = FW * zoom + 'px';
      s2.style.height = H * zoom + 'px';
      c2.width = Math.round(FW * zoom * dpr); c2.height = Math.round(H * zoom * dpr);
    }
    renderCaptions();
    requestRender();
  }
  function requestRender() { if (!raf) raf = requestAnimationFrame(draw); }
  const boxOf = (e) => QRender.box(ctx, e);

  function draw() {
    raf = 0;
    if (S.sheet) { drawSheet(); return; }
    const p = curPage();
    const k = zoom * dpr;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, cv.width, cv.height);
    ctx.setTransform(k, 0, 0, k, 0, 0);
    const pv = p ? drawFrame(ctx, p, S.drag ? null : S.hover) : null;
    if (pv) drawPreviewOverlay(pv, k);
    else drawOverlay(p, k);
    updateShot(p);
    if (sideOn()) {   // the other font, same page, not editable
      const c2 = $('cv2').getContext('2d');
      c2.setTransform(1, 0, 0, 1, 0, 0);
      c2.clearRect(0, 0, c2.canvas.width, c2.canvas.height);
      c2.setTransform(k, 0, 0, k, 0, 0);
      withFont(otherId(), () => { if (p) drawFrame(c2, p, null); });
    }
  }
  // the dump's 1280x720 frame (dashed) and the widgets the preview puts off the screen (red)
  function drawPreviewOverlay(pv, k) {
    const o = octx, px = 1 / zoom, v = viewOf();
    o.setTransform(1, 0, 0, 1, 0, 0);
    o.clearRect(0, 0, ov.width, ov.height);
    o.setTransform(k, 0, 0, k, 0, 0);
    o.lineWidth = px;
    o.setLineDash([6 * px, 4 * px]);
    o.strokeStyle = 'rgba(64,217,255,0.45)';
    o.strokeRect((v.fw - W) / 2, 0, W, H);
    o.setLineDash([]);
    o.strokeStyle = 'rgba(255,90,90,0.95)';
    o.lineWidth = 2 * px;
    for (const b of pv.clipped) o.strokeRect(Math.max(0, b.x), b.y, Math.min(v.fw, b.x + b.w) - Math.max(0, b.x), b.h);
    $('cap1').textContent = `${v.w}x${v.h}: UI scale ${v.s.toFixed(2)}, text scale ${v.ts}${v.k !== 1 ? ` (text ${Math.round(v.k * 100)}% of the 720p size)` : ''}` +
      `  -  ${pv.pinned} pinned to an edge${pv.clipped.length ? `, ${pv.clipped.length} off the screen (red)` : ''}  -  read only: View: 1280x720 to edit`;
  }

  function drawOverlay(p, k) {
    const o = octx, px = 1 / zoom;   // one screen pixel in game pixels
    o.setTransform(1, 0, 0, 1, 0, 0);
    o.clearRect(0, 0, ov.width, ov.height);
    o.setTransform(k, 0, 0, k, 0, 0);
    if (!p) return;
    if (p.hidden) {
      o.fillStyle = 'rgba(80,0,0,0.35)';
      o.fillRect(0, 0, W, H);
      o.fillStyle = 'rgba(255,120,120,0.9)';
      o.font = 'bold 28px Segoe UI, sans-serif';
      o.fillText('HIDDEN PAGE (marked removed)', 24, H - 24);
    }
    if (S.prefs.grid) {
      const g = Math.max(2, S.prefs.gridSize);
      if (g * zoom >= 4) {
        o.beginPath();
        for (let x = g; x < W; x += g) { o.moveTo(x, 0); o.lineTo(x, H); }
        for (let y = g; y < H; y += g) { o.moveTo(0, y); o.lineTo(W, y); }
        o.strokeStyle = 'rgba(255,255,255,0.06)';
        o.lineWidth = px;
        o.stroke();
      }
    }
    // centre lines (faint) and snap guides
    o.strokeStyle = 'rgba(64,217,255,0.12)';
    o.lineWidth = px;
    o.setLineDash([6 * px, 6 * px]);
    o.beginPath(); o.moveTo(W / 2, 0); o.lineTo(W / 2, H); o.moveTo(0, H / 2); o.lineTo(W, H / 2); o.stroke();
    o.setLineDash([]);
    for (const gd of S.guides) {
      o.strokeStyle = 'rgba(255,79,216,0.8)';
      o.beginPath();
      if (gd.x != null) { o.moveTo(gd.x, 0); o.lineTo(gd.x, H); } else { o.moveTo(0, gd.y); o.lineTo(W, gd.y); }
      o.stroke();
    }
    // where moved / resized widgets are by default (straight from the baseline, not the whole diff: this runs every
    // frame of a drag)
    if (S.prefs.ghost) {
      const off = (a, b) => Math.abs(a - b) >= 0.005;
      for (const e of p.elements) {
        const base = e.origId && basePageOf.get(e.origId) === p.origId ? baseEls.get(e.origId) : null;
        if (!base || !(off(base.x, e.x) || off(base.y, e.y) || off(base.w, e.w) || off(base.h, e.h))) continue;
        const b = QRender.box(ctx, base), c = boxOf(e);
        if (!b) continue;
        o.setLineDash([4 * px, 3 * px]);
        o.strokeStyle = 'rgba(255,179,71,0.75)';
        o.strokeRect(b.x, b.y, b.w, b.h);
        o.setLineDash([]);
        if (c) {
          o.strokeStyle = 'rgba(255,179,71,0.4)';
          o.beginPath(); o.moveTo(b.x + b.w / 2, b.y + b.h / 2); o.lineTo(c.x + c.w / 2, c.y + c.h / 2); o.stroke();
        }
      }
    }
    // hover, selection, handles
    const hov = !S.drag && S.hover && !S.sel.has(S.hover) ? p.elements.find((e) => e.uid === S.hover) : null;
    if (hov) { const b = boxOf(hov); if (b) { o.strokeStyle = 'rgba(255,255,255,0.55)'; o.lineWidth = px; o.strokeRect(b.x, b.y, b.w, b.h); } }
    const sel = selected();
    o.lineWidth = 1.5 * px;
    for (const e of sel) {
      const b = boxOf(e);
      if (!b) continue;
      o.strokeStyle = '#ff4fd8';
      o.strokeRect(b.x - px, b.y - px, b.w + 2 * px, b.h + 2 * px);
    }
    if (sel.length === 1) {
      const b = boxOf(sel[0]);
      if (b) for (const h of handles(b)) {
        const s = 7 * px;
        o.fillStyle = '#fff';
        o.fillRect(h.x - s / 2, h.y - s / 2, s, s);
        o.strokeStyle = '#ff4fd8';
        o.lineWidth = px;
        o.strokeRect(h.x - s / 2, h.y - s / 2, s, s);
      }
    }
    if (S.drag && S.drag.mode === 'band') {
      const r = bandRect();
      o.fillStyle = 'rgba(255,79,216,0.08)';
      o.fillRect(r.x, r.y, r.w, r.h);
      o.strokeStyle = 'rgba(255,79,216,0.8)';
      o.lineWidth = px;
      o.strokeRect(r.x, r.y, r.w, r.h);
    }
  }

  let shotErrFor = null;
  function updateShot(p) {
    const has = !!(p && p.screenshot) && !viewOf();
    $('cShot').disabled = !has;
    $('rShot').disabled = !has;
    $('shotGroup').title = has ? 'Reference screenshot: ' + p.screenshot : 'This page has no screenshot in the dump';
    if (has && S.prefs.shot) {
      const url = shotUrl(p.screenshot);
      if (shot.getAttribute('src') !== url) shot.setAttribute('src', url);
      shot.style.display = 'block';
      shot.style.opacity = S.prefs.shotOpacity / 100;
    } else shot.style.display = 'none';
  }
  shot.addEventListener('error', () => {
    const src = shot.getAttribute('src');
    if (src && shotErrFor !== src) { shotErrFor = src; toast('Screenshot not found: ' + src, 4000); }
  });

  function handles(b) {
    const xs = [b.x, b.x + b.w / 2, b.x + b.w], ys = [b.y, b.y + b.h / 2, b.y + b.h];
    return [
      { id: 'nw', x: xs[0], y: ys[0] }, { id: 'n', x: xs[1], y: ys[0] }, { id: 'ne', x: xs[2], y: ys[0] }, { id: 'e', x: xs[2], y: ys[1] },
      { id: 'se', x: xs[2], y: ys[2] }, { id: 's', x: xs[1], y: ys[2] }, { id: 'sw', x: xs[0], y: ys[2] }, { id: 'w', x: xs[0], y: ys[1] },
    ];
  }
  const CURSORS = { nw: 'nwse-resize', se: 'nwse-resize', ne: 'nesw-resize', sw: 'nesw-resize', n: 'ns-resize', s: 'ns-resize', e: 'ew-resize', w: 'ew-resize' };
  function handleAt(pt) {
    const sel = selected();
    if (sel.length !== 1) return null;
    const b = boxOf(sel[0]);
    if (!b || (sel[0].kind === 'label' && !(sel[0].w > 0))) return null;
    const r = 6 / zoom;
    return handles(b).find((h) => Math.abs(h.x - pt.x) <= r && Math.abs(h.y - pt.y) <= r) || null;
  }
  function elementsAt(pt) {
    const p = curPage();
    if (!p) return [];
    const out = [];
    for (let i = p.elements.length - 1; i >= 0; i--) {
      const e = p.elements[i], b = pickable(e);
      if (b && pt.x >= b.x && pt.x < b.x + b.w && pt.y >= b.y && pt.y < b.y + b.h) out.push(e);
    }
    return out;
  }
  // (a drag keeps the canvas origin it started with: nothing that reflows the page mid-drag can shift the widget)
  function toGame(ev, r = ov.getBoundingClientRect()) {
    return { x: (ev.clientX - r.left) / zoom, y: (ev.clientY - r.top) / zoom };
  }
  function bandRect() {
    const d = S.drag;
    return { x: Math.min(d.start.x, d.cur.x), y: Math.min(d.start.y, d.cur.y), w: Math.abs(d.cur.x - d.start.x), h: Math.abs(d.cur.y - d.start.y) };
  }

  // ---- mouse -----------------------------------------------------------------------------------------------------
  ov.addEventListener('mousedown', (ev) => {
    if (S.tour) toggleTour();
    if (ev.button !== 0) return;
    ev.preventDefault();
    if (readOnly()) return;
    if (document.activeElement && document.activeElement !== document.body) document.activeElement.blur();
    endGesture();
    const rect = ov.getBoundingClientRect();
    const pt = toGame(ev, rect);
    const h = handleAt(pt);
    if (h) {
      const e = selected()[0];
      S.drag = { mode: 'resize', handle: h.id, el: e, start: pt, rect, orig: { x: e.x, y: e.y, w: e.w, h: e.h }, before: snap(), moved: false };
      return;
    }
    const hits = ev.ctrlKey ? [] : elementsAt(pt);   // (Ctrl+drag: always a rubber band, even over a panel)
    if (hits.length) {
      let e = hits[0];
      if (ev.altKey) {   // the next one down under the cursor
        const i = hits.findIndex((x) => S.sel.has(x.uid));
        e = hits[(i + 1) % hits.length];
        S.sel = new Set([e.uid]);
      } else if (ev.shiftKey) {
        if (S.sel.has(e.uid)) { S.sel.delete(e.uid); renderAfterSelect(); return; }
        S.sel.add(e.uid);
      } else if (!S.sel.has(e.uid)) S.sel = new Set([e.uid]);
      const items = selected().map((x) => ({ el: x, x: x.x, y: x.y }));
      S.drag = { mode: 'move', start: pt, rect, primary: e, items, before: snap(), moved: false };
      renderAfterSelect();
      return;
    }
    const keep = ev.shiftKey || ev.ctrlKey;
    S.drag = { mode: 'band', start: pt, rect, cur: pt, initial: keep ? new Set(S.sel) : new Set() };
    if (!keep) S.sel = new Set();
    renderAfterSelect();
  });

  window.addEventListener('mousemove', (ev) => {
    const d = S.drag;
    const pt = toGame(ev, d ? d.rect : undefined);
    const v = viewOf(), inside = pt.x >= 0 && pt.y >= 0 && pt.x <= frameW() && pt.y <= H;
    $('stPos').textContent = !inside ? '' : v ? `${v.w}x${v.h}: x ${r1(pt.x * v.s)}  y ${r1(pt.y * v.s)}` : `x ${r1(pt.x)}  y ${r1(pt.y)}`;
    if (!d) {
      if (ev.target !== ov || viewOf()) { if (S.hover) { S.hover = null; requestRender(); } return; }
      const h = handleAt(pt);
      const top = elementsAt(pt)[0];
      ov.style.cursor = h ? CURSORS[h.id] : top ? 'move' : 'default';
      const hu = top ? top.uid : null;
      if (hu !== S.hover) { S.hover = hu; requestRender(); }
      return;
    }
    const noSnap = ev.altKey;
    if (d.mode === 'band') { d.cur = pt; S.sel = new Set(d.initial); for (const e of inBand()) S.sel.add(e.uid); requestRender(); return; }
    let dx = pt.x - d.start.x, dy = pt.y - d.start.y;
    if (!d.moved && Math.hypot(dx, dy) * zoom < 3) return;
    d.moved = true;
    if (d.mode === 'move') moveDrag(d, dx, dy, noSnap);
    else resizeDrag(d, dx, dy, noSnap);
    requestRender();   // (the diff is redone once, at mouseup)
    refreshInspectorGeom();
    renderSelStatus();
  });

  window.addEventListener('mouseup', () => {
    const d = S.drag;
    if (!d) return;
    S.drag = null;
    S.guides = [];
    if ((d.mode === 'move' || d.mode === 'resize') && d.moved && differs(d.before)) { pushUndo(d.before); changed(); }
    else renderAfterSelect();
  });

  function inBand() {
    const r = bandRect(), p = curPage();
    if (r.w < 2 && r.h < 2) return [];
    return p.elements.filter((e) => {
      const b = pickable(e);
      return b && b.x >= r.x && b.y >= r.y && b.x + b.w <= r.x + r.w && b.y + b.h <= r.y + r.h;
    });
  }

  function snapGrid(v) { const g = Math.max(2, S.prefs.gridSize); return Math.round(v / g) * g; }
  function moveDrag(d, dx, dy, noSnap) {
    S.guides = [];
    const p0 = d.items.find((i) => i.el === d.primary) || d.items[0];
    let nx = p0.x + dx, ny = p0.y + dy;
    // only an axis the pointer moved along snaps: a sideways drag never nudges y onto the grid or the centre line
    const ax = Math.abs(dx) * zoom >= 3, ay = Math.abs(dy) * zoom >= 3;
    if (!noSnap) {
      if (S.prefs.grid) { if (ax) nx = snapGrid(nx); if (ay) ny = snapGrid(ny); }
      const b = boxOf(d.primary) || { w: d.primary.w, h: d.primary.h };
      const tol = 6 / zoom;
      const offX = (d.primary.kind === 'label' && !(d.primary.w > 0)) ? b.x - d.primary.x : 0;   // (auto-sized labels)
      if (ax && Math.abs(nx + offX + b.w / 2 - W / 2) < tol) { nx = W / 2 - b.w / 2 - offX; S.guides.push({ x: W / 2 }); }
      if (ay && Math.abs(ny + b.h / 2 - H / 2) < tol) { ny = H / 2 - b.h / 2; S.guides.push({ y: H / 2 }); }
    }
    const ddx = Math.round(nx - p0.x), ddy = Math.round(ny - p0.y);
    for (const it of d.items) { it.el.x = it.x + ddx; it.el.y = it.y + ddy; }
  }
  function resizeDrag(d, dx, dy, noSnap) {
    const o = d.orig, hd = d.handle;
    let x0 = o.x, y0 = o.y, x1 = o.x + o.w, y1 = o.y + o.h;
    const sn = (v) => (!noSnap && S.prefs.grid ? snapGrid(v) : Math.round(v));
    if (hd.includes('w')) x0 = Math.min(sn(o.x + dx), x1 - 2);
    if (hd.includes('e')) x1 = Math.max(sn(o.x + o.w + dx), x0 + 2);
    if (hd.includes('n')) y0 = Math.min(sn(o.y + dy), y1 - 2);
    if (hd.includes('s')) y1 = Math.max(sn(o.y + o.h + dy), y0 + 2);
    Object.assign(d.el, { x: x0, y: y0, w: x1 - x0, h: y1 - y0 });
  }
  function renderAfterSelect() { requestRender(); renderInspector(); renderOffCanvas(); renderSelStatus(); updateButtons(); }

  // drag from the palette
  ov.addEventListener('dragover', (ev) => { if (ev.dataTransfer.types.includes('text/x-quark-kind')) ev.preventDefault(); });
  ov.addEventListener('drop', (ev) => {
    const kind = ev.dataTransfer.getData('text/x-quark-kind');
    if (!kind) return;
    ev.preventDefault();
    addWidget(kind, toGame(ev));
  });

  // ---- edit operations -------------------------------------------------------------------------------------------
  function addWidget(kind, pt) {
    const p = curPage();
    if (!p) return;
    commit(() => {
      const e = QModel.newElement(kind, pt ? pt.x : W / 2, pt ? pt.y : H / 2);
      if (S.prefs.grid) { e.x = snapGrid(e.x); e.y = snapGrid(e.y); }
      insertElement(p, e);
      S.sel = new Set([e.uid]);
    });
  }
  function deleteSel() {
    const ids = new Set(S.sel);
    if (!ids.size || readOnly()) return;
    commit(() => { const p = curPage(); p.elements = p.elements.filter((e) => !ids.has(e.uid)); S.sel = new Set(); });
    toast(`Deleted ${ids.size} widget${ids.size > 1 ? 's' : ''}`);
  }
  function duplicateSel() {
    const sel = selected();
    if (!sel.length) return;
    commit(() => {
      const p = curPage(), out = new Set();
      for (const e of sel) { const c = QModel.copyElement(e, 10, 10); insertElement(p, c); out.add(c.uid); }
      S.sel = out;
    });
  }
  function copySel(cut) {
    const sel = selected();
    if (!sel.length || (cut && readOnly())) return;
    S.clip = { mode: cut ? 'cut' : 'copy', items: QModel.clone(sel), fromPage: S.pageUid };
    S.pasteN = 0;
    if (cut) {
      const ids = new Set(sel.map((e) => e.uid));
      commit(() => { const p = curPage(); p.elements = p.elements.filter((e) => !ids.has(e.uid)); S.sel = new Set(); });
      toast(`Cut ${sel.length}: paste on any page (keeps the widget's identity)`);
    } else toast(`Copied ${sel.length}`);
  }
  function paste() {
    const c = S.clip, p = curPage();
    if (!c || !p) return toast('Clipboard is empty');
    commit(() => {
      const uids = allUids(), placed = placedOrig(), out = new Set();
      const samePage = c.fromPage === S.pageUid;
      if (c.mode === 'copy') S.pasteN++;
      const off = c.mode === 'copy' && samePage ? 10 * S.pasteN : 0;
      for (const item of c.items) {
        let e;
        if (c.mode === 'cut' && !uids.has(item.uid) && !(item.origId && placed.has(item.origId))) e = QModel.clone(item);   // the same widget, moved
        else e = QModel.copyElement(item, off, off);
        insertElement(p, e);
        out.add(e.uid);
      }
      S.sel = out;
      if (c.mode === 'cut') { c.mode = 'copy'; c.fromPage = S.pageUid; S.pasteN = 0; }
    });
  }
  function nudge(dx, dy, resize) {
    const sel = selected();
    if (!sel.length || readOnly()) return;
    const key = [...S.sel].join() + (resize ? 'r' : 'm');
    const now = Date.now();
    const merge = S.lastNudge && S.lastNudge.key === key && now - S.lastNudge.t < 1000 && S.undo.length && S.lastNudge.depth === S.undo.length;
    const before = snap();
    for (const e of sel) {
      if (resize) { e.w = Math.max(1, e.w + dx); e.h = Math.max(1, e.h + dy); } else { e.x += dx; e.y += dy; }
    }
    if (!merge) pushUndo(before);
    S.lastNudge = { key, t: now, depth: S.undo.length };
    changed();
  }
  function moveSelToPage(uid) {
    const sel = selected(), target = S.doc.pages.find((p) => p.uid === uid);
    if (!sel.length || !target || target.uid === S.pageUid || readOnly()) return;
    commit(() => {
      const ids = new Set(sel.map((e) => e.uid)), p = curPage();
      p.elements = p.elements.filter((e) => !ids.has(e.uid));
      for (const e of sel) insertElement(target, e);
      touch(target);
      S.sel = new Set();
    });
    toast(`Moved ${sel.length} to ${target.id}`);
  }
  function reorder(where) {
    const sel = selected();
    if (!sel.length) return;
    commit(() => {
      const p = curPage(), ids = new Set(sel.map((e) => e.uid));
      const rest = p.elements.filter((e) => !ids.has(e.uid));
      p.elements = where === 'front' ? rest.concat(sel) : sel.concat(rest);
    });
  }
  function revertSel() {
    const sel = selected().filter((e) => e.origId && baseEls.has(e.origId));
    if (!sel.length) return toast('Only original widgets can be reverted');
    commit(() => {
      for (const e of sel) {
        const b = baseEls.get(e.origId);
        for (const k of Object.keys(e)) if (!['uid', 'origId', 'notes'].includes(k)) delete e[k];
        Object.assign(e, QModel.clone(b), { uid: e.uid, notes: e.notes });
      }
    });
  }
  function restoreRemoved(origId) {
    const b = baseEls.get(origId), p = curPage();
    if (!b || placedOrig().has(origId)) return;
    commit(() => {
      const e = Object.assign(QModel.clone(b), { uid: QModel.uid('e'), notes: '' });
      const bp = baseline.pages.find((x) => x.elements.includes(b));
      insertElement(p, e, Math.min(bp ? bp.elements.indexOf(b) : p.elements.length, p.elements.length));
      S.sel = new Set([e.uid]);
    });
  }
  function align(how) {
    const sel = selected().map((e) => ({ e, b: boxOf(e) })).filter((x) => x.b);
    if (sel.length < 2) return;
    commit(() => {
      const L = Math.min(...sel.map((s) => s.b.x)), R = Math.max(...sel.map((s) => s.b.x + s.b.w));
      const T = Math.min(...sel.map((s) => s.b.y)), B = Math.max(...sel.map((s) => s.b.y + s.b.h));
      const mv = (s, nx, ny) => { s.e.x += Math.round(nx - s.b.x); s.e.y += Math.round(ny - s.b.y); };
      if (how === 'left') sel.forEach((s) => mv(s, L, s.b.y));
      if (how === 'right') sel.forEach((s) => mv(s, R - s.b.w, s.b.y));
      if (how === 'hcenter') sel.forEach((s) => mv(s, (L + R) / 2 - s.b.w / 2, s.b.y));
      if (how === 'top') sel.forEach((s) => mv(s, s.b.x, T));
      if (how === 'bottom') sel.forEach((s) => mv(s, s.b.x, B - s.b.h));
      if (how === 'vmiddle') sel.forEach((s) => mv(s, s.b.x, (T + B) / 2 - s.b.h / 2));
      if (how === 'width') { const w = sel[0].e.w; sel.forEach((s) => { s.e.w = w; }); }
      if (how === 'height') { const h = sel[0].e.h; sel.forEach((s) => { s.e.h = h; }); }
      if (how === 'distV' || how === 'distH') {
        const v = how === 'distV';
        const sorted = sel.slice().sort((a, b) => (v ? a.b.y - b.b.y : a.b.x - b.b.x));
        const total = sorted.reduce((n, s) => n + (v ? s.b.h : s.b.w), 0);
        const gap = ((v ? B - T : R - L) - total) / (sorted.length - 1);
        let at = v ? T : L;
        for (const s of sorted) { if (v) mv(s, s.b.x, at); else mv(s, at, s.b.y); at += (v ? s.b.h : s.b.w) + gap; }
      }
    });
  }

  // pages
  function selectPage(uid) {
    endGesture();
    if (S.pageUid === uid && !S.sheet) return;
    if (S.sheet) showSheet(false);
    S.pageUid = uid;
    S.sel = new Set();
    S.hover = null;
    renderAll();
    store.set(PAGE_KEY, uid);
  }
  function stepPage(d) {
    const list = visiblePageOrder();
    if (!list.length) return;
    const i = list.findIndex((p) => p.uid === S.pageUid);
    selectPage(list[(i + d + list.length) % list.length].uid);
  }
  async function addPage() {
    if (readOnly()) return;
    const id = await ask({ title: 'Add a page', text: 'Page id: a screen path, e.g. settings/accessibility (a taken id gets -2, -3 ...)',
      input: 'new-page', ok: 'Add page', check: (v) => (v.trim() ? '' : 'Give the page an id') });
    if (id === null) return;
    const p = QModel.newPage(uniquePageId(id.trim()), '');
    p.title = p.id;
    commit(() => {
      const i = S.doc.pages.findIndex((x) => x.uid === S.pageUid);
      S.doc.pages.splice(i + 1, 0, p);
      S.pageUid = p.uid;
      S.sel = new Set();
    });
  }
  function duplicatePage() {
    const src = curPage();
    commit(() => {
      const p = QModel.newPage(uniquePageId(src.id + '-copy'), src.title + ' (copy)');
      Object.assign(p, { background: src.background, inGame: src.inGame, scenarios: (src.scenarios || []).slice() });
      p.elements = src.elements.map((e) => QModel.copyElement(e));
      const i = S.doc.pages.indexOf(src);
      S.doc.pages.splice(i + 1, 0, p);
      S.pageUid = p.uid;
      S.sel = new Set();
    });
  }
  async function deletePage() {
    const p = curPage();
    if (p.origId || readOnly()) return;
    if (p.elements.length && !(await confirmed('Delete page', `Delete page "${p.id}" and its ${p.elements.length} widget(s)? (Undo brings it back.)`, 'Delete'))) return;
    if (!S.doc.pages.includes(p)) return;
    commit(() => {
      const i = S.doc.pages.indexOf(p);
      S.doc.pages.splice(i, 1);
      if (!S.doc.pages.length) S.doc.pages.push(QModel.newPage('new-page', 'New page'));
      S.pageUid = S.doc.pages[Math.max(0, i - 1)].uid;
      S.sel = new Set();
    });
  }

  // ---- page list -------------------------------------------------------------------------------------------------
  const visiblePageOrder = () => [...pageGroups(S.doc.pages).values()].flat();
  const ORIGIN_TAG = { human: 'HU', ai: 'AI', unknown: '?' };
  function renderPages() {
    const list = $('pageList');
    const q = S.search.trim().toLowerCase();
    const D = getDiff();
    const entries = new Map(D.pages.map((e) => [e.page, e]));
    const count = { human: 0, ai: 0, unknown: 0 };
    const shown = [], info = new Map();
    for (const p of S.doc.pages) {
      const entry = entries.get(p), o = originOf(p, entry);
      count[o.origin]++;
      if (S.originFilter && o.origin !== S.originFilter) continue;
      let matches = 0;
      if (q) {
        const hitPage = (p.id + ' ' + p.title).toLowerCase().includes(q);
        matches = p.elements.filter((e) => ((e.label || '') + ' ' + (e.action || '') + ' ' + (e.id || '')).toLowerCase().includes(q)).length;
        if (!hitPage && !matches) continue;
      }
      shown.push(p);
      info.set(p, { entry, o, matches });
    }
    const groups = pageGroups(shown);
    list.textContent = '';
    // who made the pages (the 100% human-made goal): click to list only those
    const chip = (k, label) => el('span', { class: 'ochip o-' + k + (S.originFilter === k ? ' on' : ''), text: `${label} ${count[k]}`,
      title: S.originFilter === k ? 'Show every page' : `List only the ${label} pages`, onclick: () => { S.originFilter = S.originFilter === k ? '' : k; renderPages(); } });
    list.append(el('div', { class: 'origins', title: 'Who made each page: HU human, AI AI-made (a changed page counts as yours; the others as the game\'s provenance registry says)' },
      'Made by', chip('human', 'human'), chip('ai', 'AI'), count.unknown ? chip('unknown', 'unknown') : null));
    list.append(el('div', { class: 'pg-group', text: 'tool only' }),
      el('div', { class: 'tpg' + (S.sheet ? ' cur' : ''), id: 'pgSheet', title: 'Every font trial drawing real game labels at text scale 1, 2 and 3 (not a game page)',
        text: 'Font sheet', onclick: () => showSheet(true) }));
    for (const [g, items] of groups) {
      list.append(el('div', { class: 'pg-group', text: g }));
      for (const p of items) {
        const { entry, o, matches } = info.get(p);
        const st = entry ? entry.status : 'unchanged';
        list.append(el('div', {
          class: 'pg' + (p.uid === S.pageUid && !S.sheet ? ' cur' : '') + (p.hidden ? ' hidden' : ''), title: p.id + (p.title !== p.id ? ' - ' + p.title : '') + '\nmade by: ' + o.origin + ' (' + o.why + ')',
          onclick: () => selectPage(p.uid),
        }, el('span', { class: 'org o-' + o.origin, text: ORIGIN_TAG[o.origin] }),
        el('span', { class: 't', text: p.title || p.id }),
        matches ? el('span', { class: 'badge match', text: matches }) : null,
        st !== 'unchanged' ? el('span', { class: 'badge ' + st, text: st === 'added' ? 'new' : st }) : null,
        el('span', { class: 'n', text: p.elements.length })));
      }
    }
    if (!groups.size) list.append(el('div', { class: 'pg-group', text: 'no match' }));
  }

  // ---- inspector -------------------------------------------------------------------------------------------------
  const hex = (c) => '#' + c.slice(0, 3).map((v) => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0')).join('');
  const unhex = (h) => [1, 3, 5].map((i) => Math.round(parseInt(h.slice(i, i + 2), 16) / 255 * 1000) / 1000);
  const insp = () => $('inspector');
  let markers = [];   // [{node, get: () => changed?}] refreshed without rebuilding the inspector

  // A bound input: edits on 'input' (one undo step per field edit, ended by 'change').
  function bind(input, apply) {
    input.addEventListener('input', () => { if (readOnly()) { renderInspector(); return; } beginGesture(); apply(); changed({ keepInspector: true }); });
    input.addEventListener('change', () => { endGesture(); });
    return input;
  }
  function fieldChanged(e, f) {
    if (!e.origId || !baseEls.has(e.origId)) return false;
    const b = baseEls.get(e.origId);
    const norm = (v) => (v === undefined || v === null || v === '' ? null : v);
    return JSON.stringify(norm(b[f])) !== JSON.stringify(norm(e[f]));
  }
  function mark(lab, e, f) {
    const upd = () => {
      const c = fieldChanged(e, f);
      lab.classList.toggle('chg', c);
      if (c) { const b = baseEls.get(e.origId)[f]; lab.title = 'Default: ' + (b === undefined ? '(none)' : JSON.stringify(b)); } else lab.title = '';
    };
    markers.push(upd);
    upd();
    return lab;
  }
  function refreshInspectorMarks() { for (const m of markers) m(); refreshStatusList(); }

  function textRow(e, f, name, opts = {}) {
    const lab = mark(el('label', { text: name }), e, f);
    const inp = opts.area ? el('textarea', { rows: opts.rows || 3 }) : el('input', { type: opts.number ? 'number' : 'text', step: opts.step || 'any' });
    const get = () => e[f];
    if (opts.lines) inp.value = Array.isArray(get()) ? get().join('\n') : '';
    else inp.value = get() === undefined || get() === null ? '' : String(get());
    if (opts.placeholder) inp.placeholder = opts.placeholder;
    inp.dataset.field = f;
    bind(inp, () => {
      const v = inp.value;
      if (opts.lines) { const a = v.split('\n').map((s) => s.trim()).filter(Boolean); if (a.length) e[f] = a; else delete e[f]; }
      else if (opts.number) { if (v === '' && !opts.required) delete e[f]; else { const n = Number(v); e[f] = Number.isFinite(n) ? n : 0; } }
      else if (v === '' && opts.optional) delete e[f];
      else e[f] = v;
    });
    return el('div', { class: 'row' }, lab, inp);
  }
  function selectRow(e, f, name, options, onChange) {
    const lab = mark(el('label', { text: name }), e, f);
    const s = el('select', {}, options.map((o) => el('option', { value: o, text: o || '(none)' })));
    s.value = e[f] == null ? '' : e[f];
    s.addEventListener('change', () => commit(() => { if (s.value === '') delete e[f]; else e[f] = s.value; if (onChange) onChange(); }));
    return el('div', { class: 'row' }, lab, s);
  }
  // label style: normal (no field) or "logo" (the title logo: Rubik 800, split cyan/red Q, thick outline)
  function styleRow(e) {
    const lab = mark(el('label', { text: 'style', title: 'logo: drawn like the app icon (see "Title logo" in the spec)' }), e, 'style');
    const s = el('select', {}, [['', 'normal'], ['logo', 'logo']].map(([v, t]) => el('option', { value: v, text: t })));
    s.value = e.style === 'logo' ? 'logo' : '';
    s.addEventListener('change', () => commit(() => { if (s.value) e.style = s.value; else delete e.style; }));
    return el('div', { class: 'row' }, lab, s);
  }
  function checkRow(e, f, name, defOn) {
    const lab = mark(el('label', { text: name }), e, f);
    const c = el('input', { type: 'checkbox' });
    c.checked = e[f] === undefined ? defOn : !!e[f];
    c.addEventListener('change', () => commit(() => { e[f] = c.checked; }, { keepInspector: true }));
    return el('div', { class: 'row' }, lab, c);
  }
  function colorRow(e, name) {
    const lab = mark(el('label', { text: name }), e, 'color');
    const c = Array.isArray(e.color) ? e.color : null;
    const pick = el('input', { type: 'color', value: hex(c || [1, 1, 1, 1]) });
    const alpha = el('input', { type: 'number', min: 0, max: 1, step: 0.05, value: c ? (c[3] == null ? 1 : c[3]) : 1, title: 'alpha 0..1' });
    const apply = () => { e.color = unhex(pick.value).concat([Math.max(0, Math.min(1, Number(alpha.value) || 0))]); };
    bind(pick, apply);
    bind(alpha, apply);
    const clear = el('button', { text: 'none', title: 'No colour (the default for its kind)', onclick: () => commit(() => { delete e.color; }) });
    return el('div', { class: 'row colorrow' }, lab, pick, alpha, clear);
  }
  function geomGrid(e) {
    const g = el('div', { class: 'grid4' });
    for (const f of ['x', 'y', 'w', 'h']) {
      const lab = mark(el('label', { text: f }), e, f);
      const inp = el('input', { type: 'number', step: 1, value: e[f] });
      inp.dataset.geom = f;
      bind(inp, () => { const n = Number(inp.value); e[f] = Number.isFinite(n) ? n : 0; });
      g.append(lab, inp);
    }
    return g;
  }
  function refreshInspectorGeom() {
    const sel = selected();
    if (sel.length !== 1) return;
    for (const inp of insp().querySelectorAll('input[data-geom]')) if (document.activeElement !== inp) inp.value = sel[0][inp.dataset.geom];
  }
  const sec = (title, ...kids) => el('div', { class: 'sec' }, title ? el('div', { class: 'sec-t', text: title }) : null, ...kids);
  let statusList = null, statusEl = null;
  function refreshStatusList() {
    if (!statusList || !statusEl) return;
    const row = getDiff().byUid.get(statusEl.uid);
    statusList.textContent = '';
    if (!row) return;
    const flags = row.flags.length ? row.flags.join(', ') : 'unchanged';
    statusList.append(el('div', { class: 'readonly', text: 'Status: ' + flags }));
    if (row.changes.length) statusList.append(el('ul', { class: 'changes' }, row.changes.map((c) => el('li', { text: c.field + ': ' + JSON.stringify(c.old) + ' -> ' + JSON.stringify(c.new) }))));
  }
  function pageSelect() {
    const s = el('select', {}, el('option', { value: '', text: 'Move to page...' }),
      S.doc.pages.filter((p) => p.uid !== S.pageUid).map((p) => el('option', { value: p.uid, text: p.id })));
    s.addEventListener('change', () => { if (s.value) moveSelToPage(s.value); });
    return s;
  }

  function renderInspector() {
    markers = [];
    statusList = statusEl = null;
    const box = insp();
    box.textContent = '';
    const sel = selected();
    const parts = sel.length === 1 ? elementInspector(sel[0]) : sel.length > 1 ? multiInspector(sel) : pageInspector(curPage());
    if (!viewOf()) box.append(...parts);
    else box.append(el('div', { class: 'sub ro-note', text: 'Preview at ' + S.view + ': read only (View: 1280x720 to edit)' }), el('fieldset', { class: 'ro', disabled: true }, ...parts));
  }

  function elementInspector(e) {
    const k = e.kind;
    const out = [];
    const proposed = QModel.NEW_KINDS.includes(k);
    out.push(el('h2', {}, el('span', { class: 'kind', text: k }), e.label || '(no label)'));
    out.push(el('div', { class: 'sub', text: (e.origId ? 'orig ' + e.origId : e.copyOf ? 'new (copy of ' + e.copyOf + ')' : 'new widget') + (proposed ? '  -  proposed kind, not in the game yet' : '') }));
    statusList = el('div');
    statusEl = e;
    out.push(statusList);
    refreshStatusList();

    const main = [
      selectRow(e, 'kind', 'kind', QModel.KINDS.includes(k) ? QModel.KINDS : QModel.KINDS.concat([k]), () => {}),
      textRow(e, 'label', 'label'),
      geomGrid(e),
    ];
    if (['toggle', 'cycle', 'slider', 'text', 'key', 'dropdown'].includes(k) || e.value !== undefined) {
      const hint = { toggle: '1 = ON, 0 = OFF', cycle: 'index into options', dropdown: 'index into options', slider: 'number', key: 'the key shown', text: 'text shown' }[k];
      main.push(textRow(e, 'value', 'value', { placeholder: hint, optional: true }));
    }
    out.push(sec('', ...main));

    const props = [];
    if (['cycle', 'dropdown'].includes(k) || Array.isArray(e.options)) props.push(textRow(e, 'options', 'options', { area: true, lines: true, rows: 4, placeholder: 'one per line' }));
    if (k === 'slider' || e.min !== undefined) {
      props.push(textRow(e, 'min', 'min', { number: true }), textRow(e, 'max', 'max', { number: true }), textRow(e, 'def', 'default', { number: true }),
        textRow(e, 'fmt', 'format', { placeholder: '%.0f', optional: true }));
    }
    if (k === 'text' || e.maxLen !== undefined) props.push(textRow(e, 'maxLen', 'max length', { number: true }));
    if (k === 'label' || e.textScale !== undefined) props.push(textRow(e, 'textScale', 'text scale', { number: true, step: 0.1, placeholder: String(baseline.style.textScale || 2) }));
    if (k === 'label' || e.align !== undefined) props.push(selectRow(e, 'align', 'align', ['', 'left', 'center', 'right']));
    if (k === 'label' || e.style !== undefined) props.push(styleRow(e));
    if (['label', 'panel', 'rect', 'colorpicker'].includes(k) || e.color !== undefined) props.push(colorRow(e, 'color'));
    if (INTERACTIVE.has(k) || e.enabled !== undefined) props.push(checkRow(e, 'enabled', 'enabled', true));
    props.push(textRow(e, 'action', 'action', { placeholder: 'nav:host, cfg:fov, cmd:..., quit', optional: true }));
    props.push(textRow(e, 'id', 'id', { placeholder: 'stable id when the label shows data', optional: true }));
    out.push(sec('Properties', ...props));

    const ro = [];
    for (const f of ['where', 'scope', 'dynamic']) if (e[f] !== undefined) ro.push(el('div', { class: 'readonly', text: f + ': ' + JSON.stringify(e[f]) }));
    for (const f of Object.keys(e)) if (!KNOWN.has(f)) ro.push(el('div', { class: 'readonly', text: f + ': ' + JSON.stringify(e[f]) }));
    if (ro.length) out.push(sec('From the dump', ...ro));

    const notes = el('textarea', { rows: 4, placeholder: 'What should this do / look like? (goes into the spec)' });
    notes.value = e.notes || '';
    bind(notes, () => { e.notes = notes.value; });
    out.push(sec('Notes', notes));

    out.push(sec('', el('div', { class: 'row' }, el('label', { text: 'page' }), pageSelect()),
      el('div', { class: 'btns' },
        el('button', { text: 'Duplicate', onclick: duplicateSel }),
        el('button', { text: 'Delete', onclick: deleteSel }),
        el('button', { text: 'To front', onclick: () => reorder('front') }),
        el('button', { text: 'To back', onclick: () => reorder('back') }),
        e.origId ? el('button', { text: 'Revert to default', title: 'Put every property back as in the game (stays on this page)', onclick: revertSel }) : null)));
    return out;
  }

  function multiInspector(sel) {
    const notes = el('textarea', { rows: 3, placeholder: 'Note added to all selected widgets' });
    return [
      el('h2', {}, `${sel.length} widgets selected`),
      el('div', { class: 'sub', text: sel.map((e) => e.label || e.kind).join(', ') }),
      sec('Align', el('div', { class: 'btns' },
        ...[['left', 'Left'], ['hcenter', 'Centre'], ['right', 'Right'], ['top', 'Top'], ['vmiddle', 'Middle'], ['bottom', 'Bottom'],
          ['distH', 'Spread across'], ['distV', 'Spread down'], ['width', 'Same width'], ['height', 'Same height']]
          .map(([k, t]) => el('button', { text: t, onclick: () => align(k), title: k === 'width' || k === 'height' ? 'As the first selected widget' : '' })))),
      sec('Notes', notes, el('div', { class: 'btns' }, el('button', { text: 'Add note to all', onclick: () => {
        const t = notes.value.trim();
        if (t) commit(() => { for (const e of sel) e.notes = e.notes ? e.notes + '\n' + t : t; });
      } }))),
      sec('', el('div', { class: 'row' }, el('label', { text: 'page' }), pageSelect()),
        el('div', { class: 'btns' }, el('button', { text: 'Duplicate', onclick: duplicateSel }), el('button', { text: 'Delete', onclick: deleteSel }),
          el('button', { text: 'To front', onclick: () => reorder('front') }), el('button', { text: 'To back', onclick: () => reorder('back') }),
          el('button', { text: 'Revert to default', onclick: revertSel }))),
    ];
  }

  function pageInspector(p) {
    if (!p) return [el('div', { class: 'sub', text: 'No page' })];
    const entry = diffEntry(p);
    const out = [];
    out.push(el('h2', {}, el('span', { class: 'kind', text: 'page' }), p.title || p.id));
    const o = originOf(p, entry);
    out.push(el('div', { class: 'sub', text: (p.origId ? 'orig ' + p.origId : 'new page') + '  -  ' + (entry ? entry.status : '') + '  -  ' + p.elements.length + ' widgets' +
      '  -  made by ' + o.origin + ' (' + o.why + ')' }));
    const pageField = (f, name, opts = {}) => {
      const lab = el('label', { text: name });
      const bp = p.origId ? baseline.pages.find((b) => b.origId === p.origId) : null;
      const upd = () => { const c = bp && String(bp[f] == null ? '' : bp[f]) !== String(p[f] == null ? '' : p[f]); lab.classList.toggle('chg', !!c); lab.title = c ? 'Default: ' + bp[f] : ''; };
      markers.push(upd);
      upd();
      const inp = opts.area ? el('textarea', { rows: 4, placeholder: opts.placeholder || '' }) : el('input', { type: 'text', placeholder: opts.placeholder || '' });
      inp.value = p[f] == null ? '' : p[f];
      // (a value check fails: shown on the field and not applied; leaving the field puts the page's value back)
      const bad = () => (opts.check ? opts.check(inp.value) : '');
      inp.addEventListener('input', (ev) => { const w = bad(); inp.classList.toggle('bad', !!w); inp.title = w; if (w) ev.stopImmediatePropagation(); });
      inp.addEventListener('change', () => { if (bad()) { inp.value = p[f] == null ? '' : p[f]; inp.classList.remove('bad'); inp.title = ''; } });
      bind(inp, () => { p[f] = inp.value; });
      return el('div', { class: 'row' }, lab, inp);
    };
    const idCheck = (v) => (!v.trim() ? 'A page needs an id' : S.doc.pages.some((x) => x !== p && x.id === v) ? `Another page is "${v}" already: ids must be unique (the spec and provenance go by id)` : '');
    out.push(sec('', pageField('id', 'id', { placeholder: 'screen path, e.g. settings/video', check: idCheck }), pageField('title', 'title'), pageField('background', 'backdrop')));
    const ig = el('input', { type: 'checkbox' });
    ig.checked = !!p.inGame;
    ig.addEventListener('change', () => commit(() => { p.inGame = ig.checked; }));
    out.push(sec('', el('div', { class: 'row' }, el('label', { text: 'in game' }), ig),
      p.scenarios && p.scenarios.length ? el('div', { class: 'readonly', text: 'scenarios: ' + p.scenarios.join(', ') }) : null,
      el('div', { class: 'readonly', text: 'screenshot: ' + (p.screenshot || '(none)') })));
    const notes = el('textarea', { rows: 4, placeholder: 'Notes about this page (go into the spec)' });
    notes.value = p.notes || '';
    bind(notes, () => { p.notes = notes.value; });
    out.push(sec('Notes', notes));
    const btns = el('div', { class: 'btns' }, el('button', { text: 'Duplicate page', onclick: duplicatePage }));
    if (p.origId) btns.append(el('button', { class: 'danger', text: p.hidden ? 'Restore page' : 'Hide page (mark removed)', onclick: () => commit(() => { p.hidden = !p.hidden; }) }));
    else btns.append(el('button', { class: 'danger', text: 'Delete page', onclick: deletePage }));
    out.push(sec('', btns));
    if (entry && (entry.removed.length || entry.movedOut.length)) {
      const list = el('div');
      for (const b of entry.removed) list.append(el('div', { class: 'row' }, el('span', { class: 'readonly', text: `${b.kind} "${b.label}"` }), el('span', { class: 'spacer' }),
        el('button', { text: 'Restore', onclick: () => restoreRemoved(b.origId) })));
      for (const m of entry.movedOut) list.append(el('div', { class: 'readonly', text: `${m.base.kind} "${m.base.label}" moved to ${m.toPage.id}` }));
      out.push(sec('Gone from this page', list));
    }
    out.push(sec('', el('div', { class: 'readonly', text: 'Select a widget to edit it. Drag on empty space to select several.' })));
    return out;
  }

  // ---- off-canvas widgets, status, toolbar -----------------------------------------------------------------------
  function renderOffCanvas() {
    const box = $('offCanvas'), p = curPage();
    box.textContent = '';
    const off = p ? p.elements.filter((e) => !pickable(e) && !(S.prefs.widgetsOnly && isDecor(e) && !isBackdrop(e) && boxOf(e))) : [];
    if (!off.length) { box.append(el('span', { class: 'lbl', text: 'Every widget of this page can be picked on the canvas.' })); return; }
    box.append(el('span', { class: 'lbl', text: 'Not pickable on the canvas (keys, zero size, backdrop):' }));
    for (const e of off) {
      box.append(el('span', { class: 'chip' + (S.sel.has(e.uid) ? ' sel' : ''), title: (e.action || '') + (e.origId ? '  ' + e.origId : ''),
        text: (isBackdrop(e) ? 'backdrop ' : '') + e.kind + ' ' + (e.label || (e.kind === 'key' ? '?' : '')) + (e.action ? ' -> ' + e.action : ''),
        onclick: (ev) => { if (readOnly()) return; if (ev.shiftKey) { if (S.sel.has(e.uid)) S.sel.delete(e.uid); else S.sel.add(e.uid); } else S.sel = new Set([e.uid]); renderAfterSelect(); } }));
    }
  }
  function renderSelStatus() {
    const sel = selected();
    $('stSel').textContent = sel.length === 1 ? `${sel[0].kind} "${sel[0].label}"  ${sel[0].w}x${sel[0].h} at (${sel[0].x}, ${sel[0].y})`
      : sel.length ? sel.length + ' selected' : '';
  }
  function renderStatus() {
    renderSelStatus();
    const src = baseline.source;
    const fr = build.freshness;   // (a VERSION-only difference: a note here, not the red banner; showFreshness)
    const from = (dump ? (build.dump || 'dump') + ' @ ' + (src.commit || '?') + (fr && !fr.stale && fr.note ? ' (' + fr.note + ')' : '') : 'NO DATA (run install.py)') +
      (startLayout ? '  -  proposal ' + (build.layout || '') : '');
    const sample = build.sample ? '  [SAMPLE DATA]' : '';
    const fe = QFonts.get(activeId());
    const fl = fe ? `  -  font ${fe.name}${S.flip ? ' (compare, F)' : ''} [${LIC_WORD[licOf(fe)]}]` : '';
    $('stInfo').textContent = $('stInfo').title = `${from}${sample}${fl}  -  undo ${S.undo.length}  -  ${S.saveState}` + (S.provState ? '  -  ' + S.provState : '');
  }
  function updateButtons() {
    $('bUndo').disabled = !S.undo.length;
    $('bRedo').disabled = !S.redo.length;
    $('bDup').disabled = $('bDel').disabled = !S.sel.size;
  }
  function renderAll() {
    renderPages();
    renderInspector();
    renderOffCanvas();
    renderStatus();
    updateButtons();
    requestRender();
  }

  // ---- spec / layout files ---------------------------------------------------------------------------------------
  let lastSpec = null;
  const makeSpec = () => QModel.specMarkdown(baseline, S.doc, { dumpName: build.dump, font: fontDesc(), freshness: build.freshness, layout: S.prefs.specLayout });
  function showSpec(r, downloaded) {
    lastSpec = r;
    const kb = Math.max(1, Math.round(r.markdown.length / 1024));
    $('modalTitle').textContent = `${r.fileName}  (${kb} KB${downloaded ? ', downloaded' : ': Download again saves this one'})`;
    $('modalText').value = r.markdown;
    $('sSpecLayout').value = S.prefs.specLayout;
    $('modal').hidden = false;
  }
  function exportSpec() {
    endGesture();
    const r = makeSpec();
    download(r.fileName, r.markdown, 'text/markdown');
    exportProvenance();
    showSpec(r, true);
  }
  function exportLayout() {
    endGesture();
    download('ui-layout-' + QModel.stamp(new Date()) + '.json', JSON.stringify(payload(), null, 1), 'application/json');
    exportProvenance();
  }

  // ---- provenance.json -------------------------------------------------------------------------------------------
  // Export layout / Export spec also merge this layout's page origins (QModel.provenanceItems) into
  // ui-customizer/provenance.json: written in place through the File System Access API (Edge / Chrome, also on file://)
  // once the owner has picked the ui-customizer folder (the handle is kept in IndexedDB); without the API, or when the
  // picker is cancelled, a merged provenance.json is downloaded instead (merged with the copy embedded at build time).
  const PROV_FILE = 'provenance.json';
  let provKnown = readJson('provenanceData');   // the latest provenance.json content this page has seen
  let provDir = null;                           // FileSystemDirectoryHandle of the ui-customizer folder
  const HANDLE_DB = NS + '/handles';
  function handleStore(mode, fn) {
    return new Promise((res, rej) => {
      if (typeof indexedDB === 'undefined') return rej(new Error('no IndexedDB'));
      let r;
      try { r = indexedDB.open(HANDLE_DB, 1); } catch (e) { return rej(e); }
      r.onupgradeneeded = () => { if (!r.result.objectStoreNames.contains('h')) r.result.createObjectStore('h'); };
      r.onerror = () => rej(r.error);
      r.onblocked = () => rej(new Error('IndexedDB blocked'));
      r.onsuccess = () => {
        const db = r.result;
        try {
          const tx = db.transaction('h', mode), req = fn(tx.objectStore('h'));
          tx.oncomplete = () => { db.close(); res(req && req.result); };
          tx.onerror = tx.onabort = () => { db.close(); rej(tx.error || new Error('aborted')); };
        } catch (e) { db.close(); rej(e); }
      };
    });
  }
  async function loadProvDir() {
    try { const h = await handleStore('readonly', (st) => st.get('provDir')); if (h && typeof h.getFileHandle === 'function') provDir = h; } catch (e) { /* none */ }
  }
  async function hasFile(dir, name) {
    try { await dir.getFileHandle(name); return true; } catch (e) { return false; }
  }
  // The ui-customizer folder, writable: the remembered one (asking for permission again when needed), else picked.
  async function provFolder() {
    if (provDir) {
      try {
        let perm = await provDir.queryPermission({ mode: 'readwrite' });
        if (perm === 'prompt') perm = await provDir.requestPermission({ mode: 'readwrite' });
        if (perm === 'granted') return provDir;
      } catch (e) { console.warn('provenance folder permission', e); }
    }
    toast('Pick the ui-customizer folder (Quark\\ui-customizer, where ui-customizer.html is): provenance.json is updated there', 6000);
    const dir = await window.showDirectoryPicker({ id: 'quark-ui-customizer', mode: 'readwrite' });
    if (!(await hasFile(dir, 'ui-customizer.html')) && !(await hasFile(dir, PROV_FILE)) &&
      !(await confirmed('Is this the ui-customizer folder?', `"${dir.name}" has no ui-customizer.html or provenance.json. Write provenance.json there anyway?`, 'Write it here', false))) return null;
    provDir = dir;
    try { await handleStore('readwrite', (st) => st.put(dir, 'provDir')); } catch (e) { console.warn('could not remember the folder', e); }
    return dir;
  }
  function setProv(msg, ms) { S.provState = msg; renderStatus(); toast(msg, ms || 5000); }
  const provItems = () => QModel.provenanceItems(baseline, S.doc, {
    date: QModel.ymd(new Date()), defaultOrigin: startLayout ? 'ai' : 'human',
    aiNote: 'AI redesign (ui-customizer proposal' + (build.layout ? ', ' + build.layout : '') + ')',
  });
  const provText = (data) => JSON.stringify(data, null, 2) + '\n';
  const provCounts = (r, n) => `${n} changed page${n === 1 ? '' : 's'}: ${r.added} new, ${r.updated} updated, ${r.kept} kept; ${r.other} other item${r.other === 1 ? '' : 's'} kept`;
  async function exportProvenance() {
    const items = provItems(), n = Object.keys(items).length;
    if (!n) { setProv('provenance: no page differs from the game default, provenance.json left alone'); return; }
    let why = 'this browser cannot write files (no File System Access API)';
    if (typeof window.showDirectoryPicker === 'function') {
      try {
        const dir = await provFolder();
        if (!dir) why = 'no folder picked';
        else {
          const fh = await dir.getFileHandle(PROV_FILE, { create: true });
          const existing = QModel.parseProvenance(await (await fh.getFile()).text());
          const r = QModel.mergeProvenance(existing, items);
          const w = await fh.createWritable();
          await w.write(provText(r.data));
          await w.close();
          provKnown = r.data;
          setProv(`provenance.json updated in the ${dir.name} folder (${provCounts(r, n)})`);
          return;
        }
      } catch (e) {
        if (e && e.name === 'AbortError') why = 'folder picker cancelled';
        else { why = 'could not write it: ' + (e && e.message); console.warn('provenance', e); }
      }
    }
    const r = QModel.mergeProvenance(provKnown, items);
    provKnown = r.data;
    download(PROV_FILE, provText(r.data), 'application/json');
    setProv(`provenance.json downloaded (${why}): put it in the ui-customizer folder, replacing the old one (${provCounts(r, n)})`, 8000);
  }
  function importLayout(file) {
    const fr = new FileReader();
    fr.onload = () => {
      try {
        const data = JSON.parse(fr.result);
        const doc = QModel.sanitizeWorking(data.doc || data);
        commit(() => { S.doc = doc; S.pageUid = doc.pages[0] && doc.pages[0].uid; S.sel = new Set(); }, { noMark: true, anyView: true });
        markProposal();
        if (data.font) { S.font = sanitizeFont(data.font); applyFont(); }
        else if (fontOfDoc(doc)) { S.font = fontOfDoc(doc); applyFont(); scheduleSave(); }
        if (data.baseSig && data.baseSig !== SIG) toast('Imported. Note: it was made against a different dump (' + (data.source && data.source.commit) + ')', 5000);
        else toast('Layout imported (Undo puts the old one back)');
      } catch (err) { tell('Could not import that file', err.message + ' (nothing was changed)'); }
    };
    fr.readAsText(file);
  }
  async function resetAll() {
    if (startLayout) return resetToProposal();
    if (!(await confirmed('Reset', 'Reset the whole layout to the game\'s default? (Undo can bring your changes back while this tab is open.)', 'Reset'))) return;
    commit(() => { S.doc = QModel.makeWorking(baseline); S.pageUid = S.doc.pages[0].uid; S.sel = new Set(); }, { noMark: true, anyView: true });
    toast('Back to the default layout');
  }
  // proposal builds only: the font a start layout asks for (its saved font, else its doc.font), else the current one
  function startFont(doc) {
    const d = startLayout && startLayout.data;
    return d && d.font ? sanitizeFont(d.font) : fontOfDoc(doc) || S.font;
  }
  async function resetToProposal() {
    if (!(await confirmed('Reset', 'Reset the whole layout to the proposal this file was built with? (Undo can bring your changes back while this tab is open.)', 'Reset'))) return;
    const doc = JSON.parse(startLayout.json);
    const pu = startLayout.data.pageUid;
    commit(() => { S.doc = doc; S.pageUid = doc.pages.some((p) => p.uid === pu) ? pu : doc.pages[0].uid; S.sel = new Set(); }, { noMark: true, anyView: true });
    markProposal();
    S.font = startFont(doc);
    applyFont();
    scheduleSave();
    toast('Back to the proposal');
  }
  async function resetToGameDefault() {
    if (!(await confirmed('Reset to game default', 'Reset the whole layout to the game\'s default layout and font, dropping the proposal? (Undo can bring the layout back while this tab is open; Reset goes back to the proposal.)', 'Reset'))) return;
    commit(() => { S.doc = QModel.makeWorking(baseline); S.pageUid = S.doc.pages[0].uid; S.sel = new Set(); }, { noMark: true, anyView: true });
    S.font = sanitizeFont(Object.assign({}, S.font, { id: QFonts.DEFAULT_ID, weight: null, ls: null, upper: null, sizeK: null }));
    applyFont();
    scheduleSave();
    toast('Back to the game\'s default layout');
  }
  async function copySpec() {
    const t = $('modalText');
    try { await navigator.clipboard.writeText(t.value); toast('Copied'); } catch (e) {
      t.focus(); t.select();
      try { document.execCommand('copy'); toast('Copied'); } catch (e2) { toast('Copy failed: select the text and press Ctrl+C'); }
    }
  }

  // ---- font trials -----------------------------------------------------------------------------------------------
  // S.font: the font every page is drawn in (QFonts entry id + overrides), the text size multiplier and minimum text
  // scale, and a compare font (F flips to it, "side by side" draws it next to the page). Saved with the layout.
  const SIZES = [0.8, 0.85, 0.9, 0.95, 1, 1.05, 1.1, 1.15, 1.2, 1.25, 1.3, 1.35, 1.4, 1.45, 1.5];
  const MIN_SCALES = [0, 1.5, 1.75, 2, 2.5, 3];
  const LIC_WORD = { open: 'can ship', preview: 'preview only', unknown: 'licence?' };
  const licOf = (e) => (e && e.info && LIC_WORD[e.info.licence] ? e.info.licence : 'unknown');
  function sanitizeFont(f) {
    const d = { id: QFonts.DEFAULT_ID, weight: null, ls: null, upper: null, sizeK: null, mult: 1, minTs: 0, cmp: '', side: false };
    if (!f || typeof f !== 'object') return d;
    const n = (v, lo, hi) => (v === null || v === undefined || v === '' || !Number.isFinite(Number(v)) ? null : Math.max(lo, Math.min(hi, Number(v))));
    return {
      id: typeof f.id === 'string' && f.id ? f.id : d.id,
      weight: n(f.weight, 1, 1000), ls: n(f.ls, -0.2, 0.5), upper: typeof f.upper === 'boolean' ? f.upper : null, sizeK: n(f.sizeK, 0.3, 3),
      mult: n(f.mult, 0.5, 3) || 1, minTs: n(f.minTs, 0, 10) || 0, cmp: typeof f.cmp === 'string' ? f.cmp : '', side: !!f.side,
    };
  }
  const activeId = () => (S.flip && S.font.cmp && QFonts.get(S.font.cmp) ? S.font.cmp : S.font.id);
  const otherId = () => (activeId() === S.font.id ? S.font.cmp : S.font.id);
  const sideOn = () => !!(S.font.side && S.font.cmp && QFonts.get(S.font.cmp) && !S.sheet);
  // The render settings of a font: the main font carries the owner's overrides, others their own defaults.
  function cfgFor(id) {
    const e = QFonts.get(id) || QFonts.get(QFonts.DEFAULT_ID);
    if (!e) return {};
    const own = id === S.font.id;
    const weight = own && S.font.weight != null ? S.font.weight : e.weight;
    return {
      family: e.family, weight, stretch: QFonts.canvasStretch(e.stretch), width: e.stretch || '',
      sizeK: own && S.font.sizeK != null ? S.font.sizeK : e.sizeK || 1,
      ls: own && S.font.ls != null ? S.font.ls : e.letterSpacing || 0,
      upper: own && S.font.upper != null ? S.font.upper : !!e.upper,
    };
  }
  function withFont(id, fn) {
    const keep = QRender.getFontCfg();
    QRender.setFontCfg(cfgFor(id));
    try { fn(); } finally { QRender.setFontCfg(keep); }
  }
  let fontGen = 0, logoReady = false;
  // Loads the font(s) in use, then redraws with them (pages draw in the fallback until a font is ready).
  function applyFont() {
    const gen = ++fontGen;
    const set = () => {
      if (gen !== fontGen) return;
      QRender.setFontCfg(cfgFor(activeId()));
      QRender.setTextOpts({ mult: S.font.mult, minTs: S.font.minTs });
      syncFontUI();
      layoutStage();
      renderStatus();
      renderInspector();
    };
    set();
    if (QFonts.get('rubik') && !logoReady) {
      QFonts.ensure('rubik').then((e) => { logoReady = true; QRender.setLogoFont(e.family); requestRender(); }, (err) => console.warn('logo font', err));
    }
    const ids = [S.font.id, S.font.cmp].filter((id) => id && QFonts.get(id));
    Promise.all(ids.map((id) => QFonts.ensure(id))).then(set, (err) => { console.warn('font', err); toast('Could not load that font: ' + err.message, 4000); });
  }
  function setFont(id) {
    if (!QFonts.get(id)) return false;
    S.font = Object.assign(S.font, { id, weight: null, ls: null, upper: null, sizeK: null });
    if (S.font.cmp === id) S.font.cmp = '';
    S.flip = false;
    scheduleSave();
    applyFont();
    return true;
  }
  function setFontOpt(key, val) {
    S.font[key] = val;
    S.font = sanitizeFont(S.font);
    if (key === 'cmp' && !val) S.flip = false;
    scheduleSave();
    applyFont();
  }
  function flipFont() {
    if (!S.font.cmp || !QFonts.get(S.font.cmp)) { toast('Pick a compare font first (Fonts... > Compare)'); return; }
    S.flip = !S.flip;
    applyFont();
    const e = QFonts.get(activeId());
    toast((S.flip ? 'Compare font: ' : 'Font: ') + e.name, 1200);
  }

  function fontOptions(sel, withNone) {
    sel.textContent = '';
    if (withNone) sel.append(el('option', { value: '', text: '(none)' }));
    const groups = [['open', 'Can ship (open licence)'], ['preview', 'Preview only (Windows fonts, Microsoft licence)'], ['unknown', 'Licence unknown']];
    const fonts = QFonts.list();
    const opt = (e) => el('option', { value: e.id, text: `${e.name}${e.builtin ? ' (current)' : ''}  [${LIC_WORD[licOf(e)]}]` });
    for (const up of [false, true]) {
      for (const [lic, title] of groups) {
        const items = fonts.filter((e) => !!e.uploaded === up && licOf(e) === lic);
        // fonts with a group of their own (build.py) follow their licence's group, under that name
        const named = [...new Set(items.map((e) => e.group).filter(Boolean))];
        const plain = items.filter((e) => !e.group);
        if (plain.length) sel.append(el('optgroup', { label: (up ? 'Uploaded: ' : '') + title }, plain.map(opt)));
        for (const gname of named) sel.append(el('optgroup', { label: gname }, items.filter((e) => e.group === gname).map(opt)));
      }
    }
  }
  function renderFontMenus() {
    fontOptions($('sFont'), false);
    fontOptions($('sCmp'), true);
    $('sSize').textContent = '';
    for (const m of SIZES) $('sSize').append(el('option', { value: String(m), text: 'Text ' + m.toFixed(2) + 'x' }));
    $('sMinTs').textContent = '';
    for (const m of MIN_SCALES) $('sMinTs').append(el('option', { value: String(m), text: m ? 'Min scale ' + m : 'Min scale: game' }));
    syncFontUI();
  }
  function syncFontUI() {
    const e = QFonts.get(S.font.id);
    if (!e) return;
    const cfg = cfgFor(S.font.id);
    $('sFont').value = S.font.id;
    const lic = licOf(e);
    $('fontLic').className = 'lic ' + lic;
    $('fontLic').textContent = LIC_WORD[lic];
    $('fontLic').title = (e.info && e.info.licenceSummary) || '';
    const ws = $('sWeight');
    ws.textContent = '';
    const weights = e.weights.includes(cfg.weight) ? e.weights : e.weights.concat([cfg.weight]).sort((a, b) => a - b);
    for (const w of weights) ws.append(el('option', { value: String(w), text: 'wt ' + w }));
    ws.value = String(cfg.weight);
    ws.disabled = weights.length < 2;
    ws.title = weights.length < 2 ? 'This font has one weight' : 'Weight (variable font: any of these)';
    if (!SIZES.includes(S.font.mult)) $('sSize').append(el('option', { value: String(S.font.mult), text: 'Text ' + S.font.mult + 'x' }));
    $('sSize').value = String(S.font.mult);
    if (!MIN_SCALES.includes(S.font.minTs)) $('sMinTs').append(el('option', { value: String(S.font.minTs), text: 'Min scale ' + S.font.minTs }));
    $('sMinTs').value = String(S.font.minTs);
    $('sCmp').value = QFonts.get(S.font.cmp) ? S.font.cmp : '';
    $('cSide').checked = S.font.side;
    $('nLs').value = String(cfg.ls);
    $('nSizeK').value = String(Math.round(cfg.sizeK * 1000) / 1000);
    $('cUpper').checked = !!cfg.upper;
    $('bTour').textContent = S.tour ? 'Stop tour' : 'Tour';
    renderFontInfo();
    renderUploads();
    renderCaptions();
  }
  function renderCaptions() {
    if (viewOf()) { $('cap2').textContent = ''; requestRender(); return; }   // (drawPreviewOverlay writes cap1)
    const side = sideOn();
    const a = QFonts.get(activeId()), b = QFonts.get(otherId());
    $('cap1').textContent = side && a ? a.name + '  [' + LIC_WORD[licOf(a)] + ']  (edit here)' : '';
    $('cap2').textContent = side && b ? b.name + '  [' + LIC_WORD[licOf(b)] + ']  (compare, F swaps)' : '';
  }
  function renderFontInfo() {
    const box = $('fpInfo');
    if (!box) return;
    const e = QFonts.get(S.font.id);
    box.textContent = '';
    if (!e) return;
    const i = e.info || {}, lic = licOf(e), cfg = cfgFor(S.font.id);
    const kv = [];
    const add = (k, v) => { if (v !== undefined && v !== null && v !== '') kv.push(el('span', { text: k }), el('span', { text: String(v) })); };
    // (a variable font's subfamily is only its default instance, e.g. Rubik "Light": the weight in use is below)
    const variable = (i.axes || []).some((a) => a.tag === 'wght');
    add('family', [i.family, variable ? 'variable' : i.subfamily].filter(Boolean).join(' / '));
    add('file', i.uploaded ? i.fileName + ' (uploaded, kept in this browser)' : i.builtin ? 'assets/fonts/' + (i.fileName || 'Rubik.ttf') + ' (the game\'s font)' : i.path || i.fileName);
    add('why', e.why);
    add('cap height', i.capRatio ? `${i.capRatio} em (${i.capFrom || '?'}); the game's font ${QFonts.refCap()} em` : 'not known yet');
    add('size fix', `x ${Math.round(cfg.sizeK * 1000) / 1000}` + (S.font.sizeK != null ? ' (set by hand)' : ' (automatic)'));
    add('weights', e.weights.length > 1 ? e.weights.join(', ') : String(e.weights[0]));
    if (i.axes && i.axes.length) add('variable', i.axes.map((a) => `${a.tag} ${a.min}-${a.max}`).join(', ') + (e.stretch ? '; width ' + e.stretch : ''));
    if (i.kind) add('format', i.kind + (i.bytes ? ', ' + Math.round(i.bytes / 1024) + ' KB' : ''));
    add('copyright', i.copyright);
    add('licence URL', i.licenceUrl);
    box.append(el('div', { class: 'nm' }, e.name + ' ', el('span', { class: 'lic ' + lic, text: LIC_WORD[lic] })),
      el('div', { class: 'hint', text: i.licenceSummary || (i.kind === 'woff2' ? 'woff2: the licence fields could not be read (upload the .ttf / .otf to read them)' : 'no licence stated in the font: check before shipping') }),
      el('div', { class: 'kv' }, kv));
    if (i.licenceText) box.append(el('div', { class: 'lictext', title: 'name table ID 13 (licence description)', text: i.licenceText }));
  }
  function renderUploads() {
    const box = $('upList');
    if (!box) return;
    box.textContent = '';
    const ups = QFonts.list().filter((e) => e.uploaded);
    if (!ups.length) { box.append(el('div', { class: 'hint', text: 'None yet. Uploaded fonts stay in this browser until removed.' })); return; }
    for (const e of ups) {
      box.append(el('div', { class: 'up' },
        el('span', { class: 'lic ' + licOf(e), text: LIC_WORD[licOf(e)] }),
        el('span', { class: 't', text: e.name + '  (' + e.info.fileName + ')', title: e.info.licenceSummary || '' }),
        el('button', { text: 'Use', onclick: () => setFont(e.id) }),
        el('button', { class: 'danger', text: 'Remove', onclick: () => removeFont(e.id) })));
    }
  }
  async function removeFont(id) {
    const e = QFonts.get(id);
    if (!e || !(await confirmed('Remove font', `Remove the uploaded font "${e.name}" from this browser?`, 'Remove'))) return;
    await QFonts.remove(id);
    if (S.font.id === id) S.font = Object.assign(S.font, { id: QFonts.DEFAULT_ID, weight: null, ls: null, upper: null, sizeK: null });
    if (S.font.cmp === id) S.font.cmp = '';
    S.flip = false;
    renderFontMenus();
    scheduleSave();
    applyFont();
    toast('Removed ' + e.name);
  }
  async function addFonts(files) {
    let last = null;
    for (const f of files) {
      try {
        const r = await QFonts.addFile(f);
        last = r.entry;
        toast(`Added ${r.entry.name} [${LIC_WORD[licOf(r.entry)]}]` + (r.where ? ' (kept in this browser)' : ' (could not be stored: gone after a reload)'), 3500);
      } catch (err) { toast('Font not added: ' + err.message, 5000); console.warn(err); }
    }
    renderFontMenus();
    if (last) setFont(last.id);
    return last ? last.id : null;
  }
  // What the spec says about the font.
  function fontDesc() {
    const e = QFonts.get(S.font.id) || QFonts.get(QFonts.DEFAULT_ID);
    if (!e) return null;
    const i = e.info || {}, c = cfgFor(e.id);
    return { name: e.name, family: i.family || '', fileName: i.fileName || '', path: i.uploaded || i.builtin ? '' : i.path || '', uploaded: !!e.uploaded,
      builtin: !!e.builtin, weight: c.weight, stretch: c.width || '', sizeK: c.sizeK, mult: S.font.mult, minTs: S.font.minTs, letterSpacing: c.ls,
      upper: !!c.upper, licence: licOf(e), licenceSummary: i.licenceSummary || '', licenceText: i.licenceText || '', licenceUrl: i.licenceUrl || '',
      axes: i.axes || [], capRatio: i.capRatio || null, capFrom: i.capFrom || '' };
  }

  // ---- tour: every page in turn ----
  function toggleTour() {
    if (S.tour) { clearInterval(S.tour); S.tour = 0; toast('Tour stopped', 1000); }
    else {
      if (S.sheet) showSheet(false);
      S.tour = setInterval(() => stepPage(1), 1500);
      toast('Tour: a page every 1.5 s (any key or click stops)', 2500);
    }
    syncFontUI();
  }

  // ---- font sheet: every font on real labels at text scale 1, 2, 3 (tool only) ----
  function showSheet(on) {
    S.sheet = !!on;
    if (on) { if (S.tour) toggleTour(); S.sel = new Set(); S.hover = null; }
    $('stageWrap').hidden = S.sheet;
    $('offCanvas').hidden = S.sheet;
    $('sheetWrap').hidden = !S.sheet;
    renderPages();
    if (on) { renderInspector(); QFonts.ensureAll().then(() => { renderFontMenus(); requestRender(); }); }
    layoutStage();
  }
  let sheetSamples = null;
  function samples() {
    if (sheetSamples) return sheetSamples;
    const all = baseline.pages.flatMap((p) => p.elements);
    const ok = (t) => t && /[A-Z]/.test(t) && !/[^\x20-\x7e]/.test(t);
    const btn = all.find((e) => e.kind === 'button' && ok(e.label) && e.label.length >= 6 && e.label.length <= 12);
    const head = all.find((e) => e.kind === 'label' && ok(e.label) && Number(e.textScale) >= 3 && e.label.length >= 5 && e.label.length <= 18 && e.label !== 'QUARK') ||
      all.find((e) => e.kind === 'panel' && ok(e.label));
    const descs = all.filter((e) => e.kind === 'label' && ok(e.label) && (!e.textScale || Number(e.textScale) === 2) && e.label.length >= 25 && e.label.length <= 60);
    const help = all.filter((e) => e.kind === 'label' && ok(e.label) && Number(e.textScale) > 0 && Number(e.textScale) <= 1.3 && e.label.length >= 15 && e.label.length <= 60);
    sheetSamples = {
      button: btn ? btn.label : 'HOST GAME',
      header: head ? head.label : 'SETTINGS',
      desc: descs.length ? descs[Math.floor(descs.length / 2)].label : 'MOUSE SENSITIVITY: 2.40  FIELD OF VIEW: 100',
      help: help.length ? help[0].label : 'RIGHT-CLICK: RESET  -  ESC: BACK',
    };
    return sheetSamples;
  }
  function drawSheet() {
    const wrap = $('sheetWrap'), c = $('sheet'), g = c.getContext('2d');
    const fonts = QFonts.list();
    const SW = 1280, k = Math.max(0.5, (wrap.clientWidth - 16) / SW), d = window.devicePixelRatio || 1;
    const sm = samples(), m = S.font.mult;
    const rowH = (ts) => Math.ceil(Math.max(ts, S.font.minTs) * m * 9 + 22);
    const blockH = 34 + [1, 2, 3].reduce((a, ts) => a + rowH(ts) + 6, 0) + 14;
    const SH = 46 + fonts.length * blockH;
    c.style.width = SW * k + 'px';
    c.style.height = SH * k + 'px';
    c.width = Math.round(SW * k * d);
    c.height = Math.round(SH * k * d);
    g.setTransform(k * d, 0, 0, k * d, 0, 0);
    g.fillStyle = 'rgb(13,15,20)';
    g.fillRect(0, 0, SW, SH);
    g.fillStyle = '#d8dde5';
    g.font = '600 15px "Segoe UI", sans-serif';
    g.fillText(`Font sheet: ${fonts.length} fonts on real game labels at text scale 1, 2, 3 (text ${m}x${S.font.minTs ? ', min scale ' + S.font.minTs : ''}). Click a font name to use it.`, 14, 28);
    const style = baseline.style || {}, accent = Array.isArray(style.accent) ? style.accent : [0.25, 0.85, 1, 1];
    sheetHits = [];
    let y = 46;
    for (const e of fonts) {
      const lic = licOf(e);
      g.font = '600 14px "Segoe UI", sans-serif';
      g.fillStyle = e.id === S.font.id ? '#40d9ff' : '#ffffff';
      g.fillText(e.name + (e.id === S.font.id ? '  (in use)' : ''), 14, y + 20);
      const nw = g.measureText(e.name + (e.id === S.font.id ? '  (in use)' : '')).width;
      sheetHits.push({ id: e.id, x: 14, y, w: nw, h: 26 });
      g.font = '600 10px "Segoe UI", sans-serif';
      const tag = LIC_WORD[lic].toUpperCase();
      const tw = g.measureText(tag).width + 10;
      g.fillStyle = lic === 'open' ? '#1f4d2c' : lic === 'preview' ? '#4d3a1f' : '#3a3a3a';
      g.fillRect(24 + nw, y + 8, tw, 15);
      g.fillStyle = lic === 'open' ? '#6be38a' : lic === 'preview' ? '#ffb347' : '#8a94a3';
      g.fillText(tag, 29 + nw, y + 19);
      const cfg = cfgFor(e.id);
      g.font = '11px "Segoe UI", sans-serif';
      g.fillStyle = '#8a94a3';
      g.fillText(`weight ${cfg.weight}${cfg.width ? ', width ' + cfg.width : ''}, size fix x${Math.round((cfg.sizeK || 1) * 1000) / 1000}` +
        `${cfg.ls ? ', spacing ' + cfg.ls + ' em' : ''}${cfg.upper ? ', all caps' : ''}  -  ${e.why || ''}`, 34 + nw + tw, y + 20);
      y += 34;
      const keepStyle = baseline.style;
      for (const ts of [1, 2, 3]) {
        const h = rowH(ts);
        QRender.rect(g, 10, y, SW - 20, h, [0.05, 0.06, 0.08, 1]);   // the menu's clear colour, then a panel
        QRender.rect(g, 10, y, SW - 20, h, Array.isArray(style.panelColor) ? style.panelColor : [0.05, 0.06, 0.08, 0.75]);
        QRender.rect(g, 10, y, SW - 20, 2, accent);
        g.font = '11px "Segoe UI", sans-serif';
        g.fillStyle = '#8a94a3';
        g.fillText('x' + ts, 16, y + h / 2 + 4);
        withFont(e.id, () => {
          g.save();
          g.beginPath(); g.rect(10, y, SW - 20, h); g.clip();
          QRender.setStyle(Object.assign({}, keepStyle, { textScale: ts }));
          const bw = Math.round(QRender.textWidth(g, sm.button, ts * m) + 30);
          QRender.drawElement(g, { kind: 'button', x: 44, y: y + 6, w: bw, h: h - 10, label: sm.button, enabled: true }, false);
          let x = 44 + bw + 18;
          const lab = (t, c) => {
            const lh = 8 * Math.max(QRender.MIN_TS, Math.max(ts, S.font.minTs) * m);
            QRender.drawElement(g, { kind: 'label', x, y: y + (h - lh) / 2 + 2, w: 0, h: 0, label: t, textScale: ts, color: c }, false);
            x += QRender.textWidth(g, t, Math.max(ts, S.font.minTs) * m) + 22;
          };
          lab(sm.header, accent);
          lab(sm.desc, [1, 1, 1, 1]);
          lab(sm.help, [0.7, 0.72, 0.75, 1]);
          QRender.setStyle(keepStyle);
          g.restore();
        });
        y += h + 6;
      }
      y += 14;
    }
  }
  let sheetHits = [];
  $('sheet').addEventListener('click', (ev) => {
    const r = $('sheet').getBoundingClientRect(), k = r.width / 1280;
    const x = (ev.clientX - r.left) / k, y = (ev.clientY - r.top) / k;
    const h = sheetHits.find((b) => x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h);
    if (h) setFont(h.id);
  });

  function wireFonts() {
    renderFontMenus();
    $('sFont').onchange = () => setFont($('sFont').value);
    $('sWeight').onchange = () => setFontOpt('weight', Number($('sWeight').value));
    $('sSize').onchange = () => setFontOpt('mult', Number($('sSize').value));
    $('sMinTs').onchange = () => setFontOpt('minTs', Number($('sMinTs').value));
    $('sCmp').onchange = () => setFontOpt('cmp', $('sCmp').value);
    $('cSide').onchange = () => setFontOpt('side', $('cSide').checked);
    $('nLs').onchange = () => setFontOpt('ls', $('nLs').value === '' ? null : Number($('nLs').value));
    $('nSizeK').onchange = () => setFontOpt('sizeK', $('nSizeK').value === '' ? null : Number($('nSizeK').value));
    $('bSizeAuto').onclick = () => setFontOpt('sizeK', null);
    $('cUpper').onchange = () => setFontOpt('upper', $('cUpper').checked);
    $('bFonts').onclick = () => { $('fontPanel').hidden = !$('fontPanel').hidden; $('help').hidden = true; };
    $('fpClose').onclick = () => { $('fontPanel').hidden = true; };
    $('bTour').onclick = toggleTour;
    $('bUpload').onclick = () => $('fFont').click();
    $('fFont').onchange = () => { const fs = [...$('fFont').files]; $('fFont').value = ''; if (fs.length) addFonts(fs); };
    // drop font files anywhere (palette drags carry no files and are left to the canvas)
    let dragDepth = 0;
    const hasFiles = (ev) => ev.dataTransfer && [...ev.dataTransfer.types].includes('Files');
    window.addEventListener('dragenter', (ev) => { if (!hasFiles(ev)) return; dragDepth++; $('dropHint').hidden = false; });
    window.addEventListener('dragleave', (ev) => { if (!hasFiles(ev)) return; if (--dragDepth <= 0) { dragDepth = 0; $('dropHint').hidden = true; } });
    window.addEventListener('dragover', (ev) => { if (hasFiles(ev)) { ev.preventDefault(); ev.dataTransfer.dropEffect = 'copy'; } });
    window.addEventListener('drop', (ev) => {
      if (!hasFiles(ev)) return;
      ev.preventDefault();
      dragDepth = 0;
      $('dropHint').hidden = true;
      const fs = [...ev.dataTransfer.files].filter((f) => /\.(ttf|otf|woff2?)$/i.test(f.name));
      if (!fs.length) { toast('Drop .ttf .otf .woff or .woff2 font files'); return; }
      addFonts(fs);
    });
    QFonts.onChange(() => renderFontMenus());
  }

  // ---- keyboard --------------------------------------------------------------------------------------------------
  window.addEventListener('keydown', (ev) => {
    const k = ev.key, ctrl = ev.ctrlKey || ev.metaKey;
    if (!$('dlg').hidden) { if (k === 'Escape' && dlgDone) { ev.preventDefault(); dlgDone(null); } return; }
    if (!$('modal').hidden) { if (k === 'Escape') $('modal').hidden = true; return; }
    if (isTyping(ev.target)) {
      if (k === 'Escape') ev.target.blur();
      if (ctrl && (k === 'z' || k === 'y' || k === 'Z')) { /* the field's own undo */ }
      return;
    }
    const lk = k.toLowerCase();
    if (S.tour && k !== 'PageUp' && k !== 'PageDown') { toggleTour(); if (lk === 't' || k === 'Escape') { ev.preventDefault(); return; } }
    let handled = true;
    if (ctrl && lk === 'z' && !ev.shiftKey) undo();
    else if (ctrl && (lk === 'y' || (lk === 'z' && ev.shiftKey))) redo();
    else if (ctrl && lk === 'c') copySel(false);
    else if (ctrl && lk === 'x') copySel(true);
    else if (ctrl && lk === 'v') paste();
    else if (ctrl && lk === 'd') duplicateSel();
    else if (ctrl && lk === 'a' && readOnly()) { /* (preview: nothing to select) */ }
    else if (ctrl && lk === 'a') { S.sel = new Set(curPage().elements.filter(pickable).map((e) => e.uid)); renderAfterSelect(); }
    else if (ctrl && lk === 's') { saveNow(); toast('Saved in this browser (it saves by itself too)'); }
    else if (ctrl) handled = false;
    else if (k === 'Delete' || k === 'Backspace') deleteSel();
    else if (k.startsWith('Arrow')) {
      const st = ev.shiftKey ? 10 : 1;
      const d = { ArrowLeft: [-st, 0], ArrowRight: [st, 0], ArrowUp: [0, -st], ArrowDown: [0, st] }[k];
      nudge(d[0], d[1], ev.altKey);
    } else if (k === 'Escape') { $('help').hidden = true; $('fontPanel').hidden = true; S.sel = new Set(); renderAfterSelect(); }
    else if (k === 'PageUp') stepPage(-1);
    else if (k === 'PageDown') stepPage(1);
    else if (lk === 'g') toggle('grid');
    else if (lk === 'w') { toggle('widgetsOnly'); renderOffCanvas(); }
    else if (lk === 'o') toggle('ghost');
    else if (lk === 'r') toggle('shot');
    else if (lk === 'f') flipFont();
    else if (lk === 't') toggleTour();
    else if (k === '?' || k === 'F1') $('help').hidden = !$('help').hidden;
    else handled = false;
    if (handled) ev.preventDefault();
  });

  function toggle(pref) {
    S.prefs[pref] = !S.prefs[pref];
    savePrefs();
    syncToolbar();
    requestRender();
  }
  function syncToolbar() {
    $('cGrid').checked = S.prefs.grid;
    $('nGrid').value = S.prefs.gridSize;
    $('cGhost').checked = S.prefs.ghost;
    $('cWidgets').checked = S.prefs.widgetsOnly;
    $('cShot').checked = S.prefs.shot;
    $('rShot').value = S.prefs.shotOpacity;
    $('sZoom').value = String(S.prefs.zoom);
  }

  // ---- wiring ----------------------------------------------------------------------------------------------------
  function wire() {
    $('bUndo').onclick = undo;
    $('bRedo').onclick = redo;
    $('bDup').onclick = duplicateSel;
    $('bDel').onclick = deleteSel;
    $('cGrid').onchange = () => { S.prefs.grid = $('cGrid').checked; savePrefs(); requestRender(); };
    $('nGrid').onchange = () => { S.prefs.gridSize = Math.max(2, Math.min(100, Number($('nGrid').value) || 10)); savePrefs(); syncToolbar(); requestRender(); };
    $('cGhost').onchange = () => { S.prefs.ghost = $('cGhost').checked; savePrefs(); requestRender(); };
    $('cWidgets').onchange = () => { S.prefs.widgetsOnly = $('cWidgets').checked; savePrefs(); renderOffCanvas(); requestRender(); };
    $('cShot').onchange = () => { S.prefs.shot = $('cShot').checked; savePrefs(); requestRender(); };
    $('rShot').oninput = () => { S.prefs.shotOpacity = Number($('rShot').value); savePrefs(); requestRender(); };
    $('sZoom').onchange = () => { S.prefs.zoom = $('sZoom').value; savePrefs(); layoutStage(); };
    for (const [v, t] of VIEWS) $('sView').append(el('option', { value: v, text: t }));
    $('sView').onchange = () => setView($('sView').value);
    // Ctrl+wheel: the next zoom step, keeping the game point under the mouse where it is
    $('stageWrap').addEventListener('wheel', (ev) => {
      if (!ev.ctrlKey || S.sheet) return;
      ev.preventDefault();
      const steps = [...$('sZoom').options].map((o) => Number(o.value)).filter((v) => v > 0);
      const next = ev.deltaY < 0 ? steps.find((v) => v > zoom + 1e-6) : steps.slice().reverse().find((v) => v < zoom - 1e-6);
      if (!next) return;
      const pt = toGame(ev), wrap = $('stageWrap');
      S.prefs.zoom = String(next);
      savePrefs();
      syncToolbar();
      layoutStage();
      const r = ov.getBoundingClientRect();
      wrap.scrollLeft += r.left + pt.x * zoom - ev.clientX;
      wrap.scrollTop += r.top + pt.y * zoom - ev.clientY;
    }, { passive: false });
    $('bSpec').onclick = exportSpec;
    $('bExport').onclick = exportLayout;
    $('bImport').onclick = () => $('fImport').click();
    $('fImport').onchange = () => { const f = $('fImport').files[0]; if (f) importLayout(f); $('fImport').value = ''; };
    $('bReset').onclick = resetAll;
    if (startLayout) {
      $('proposalTag').hidden = false;
      $('bResetDefault').hidden = false;
      $('bResetDefault').onclick = resetToGameDefault;
      $('bReset').title = 'Back to the proposal this file was built with (' + (build.layout || 'embedded layout') + ')';
      document.title = 'Quark UI Customizer - PROPOSAL';
    }
    $('bHelp').onclick = () => { $('help').hidden = !$('help').hidden; };
    $('bAddPage').onclick = addPage;
    $('qSearch').oninput = () => { S.search = $('qSearch').value; renderPages(); };
    $('mClose').onclick = () => { $('modal').hidden = true; };
    $('mCopy').onclick = copySpec;
    $('mDownload').onclick = () => { if (lastSpec) download(lastSpec.fileName, lastSpec.markdown, 'text/markdown'); };
    $('sSpecLayout').onchange = () => { S.prefs.specLayout = $('sSpecLayout').value; savePrefs(); showSpec(makeSpec(), false); };
    $('modal').addEventListener('mousedown', (ev) => { if (ev.target === $('modal')) $('modal').hidden = true; });
    $('dlg').addEventListener('mousedown', (ev) => { if (ev.target === $('dlg') && dlgDone) dlgDone(null); });
    const pal = $('palette');
    for (const [kind, name] of PALETTE) {
      const proposed = QModel.NEW_KINDS.includes(kind);
      pal.append(el('button', {
        class: proposed ? 'proposed' : '', text: name, draggable: 'true',
        title: (proposed ? 'Not in the game yet: the spec asks for a new widget. ' : '') + 'Click to add in the middle, or drag onto the page',
        onclick: () => addWidget(kind), ondragstart: (ev) => { ev.dataTransfer.setData('text/x-quark-kind', kind); ev.dataTransfer.effectAllowed = 'copy'; },
      }));
    }
    wireFonts();
    window.addEventListener('resize', () => { layoutStage(); if (S.sheet) requestRender(); });
    window.addEventListener('beforeunload', () => { if (saveTimer) saveNow(); });
    if (document.fonts) {
      document.fonts.load('700 15px QuarkUI').then(() => { S.diff = null; requestRender(); }, () => {});
      document.fonts.addEventListener && document.fonts.addEventListener('loadingdone', requestRender);
    }
  }

  // ---- an out-of-date dump (build.py freshness: commits changed the UI code since the extract; when git can't tell,
  // the checkout's VERSION moved on). A VERSION-only bump is no reason for the red bar: renderStatus notes it. ----
  function showFreshness() {
    const f = build.freshness;
    if (!f || !f.stale) return;
    $('staleWhy').textContent = f.why;
    $('staleCmd').textContent = f.refresh;
    $('staleBar').hidden = false;
    $('bStaleHide').onclick = () => { $('staleBar').hidden = true; layoutStage(); };
    $('bStaleCopy').onclick = async () => {
      try { await navigator.clipboard.writeText(f.refresh); toast('Copied: ' + f.refresh); } catch (e) { toast('Copy failed: select the command and press Ctrl+C', 4000); }
    };
  }

  // ---- start -----------------------------------------------------------------------------------------------------
  loadInitial();
  loadProvDir();
  wire();
  showFreshness();
  syncToolbar();
  applyFont();
  QFonts.loadUploads().then((n) => {
    if (n) { renderFontMenus(); applyFont(); }
    if (!QFonts.get(S.font.id)) { toast('The saved font (' + S.font.id + ') is gone: back to the game\'s font', 4000); S.font.id = QFonts.DEFAULT_ID; applyFont(); }
  }, (e) => console.warn('uploaded fonts', e));
  layoutStage();
  renderAll();
  S.saveState = store.get(STORE_KEY) ? 'saved in this browser' : startLayout ? 'proposal layout' : 'default layout';
  renderStatus();
  if (!dump) toast('No layout data embedded: run tools/ui_customizer/install.py to build ui-customizer.html', 8000);
  else if (build.sample) toast('Using sample data (extract/ui-dump.json was not there at build time)', 4000);

  // for tests and the console
  window.QApp = { S, baseline, commit, undo, redo, addWidget, selectPage, paste, copySel, deleteSel, moveSelToPage, exportSpec,
    spec: (layout) => QModel.specMarkdown(baseline, S.doc, { dumpName: build.dump, font: fontDesc(), freshness: build.freshness, layout: layout || S.prefs.specLayout }).markdown,
    zoom: () => zoom, setView, preview: () => { const v = viewOf(), p = curPage(); return v && p ? Object.assign(previewPage(p, v), { view: v }) : null; },
    setFont, setFontOpt, showSheet, fontDesc, fonts: QFonts, addFontFile: (f) => addFonts([f]), tour: toggleTour,
    provItems, exportProvenance, forgetProvFolder: () => { provDir = null; } };
})();
