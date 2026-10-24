/* App shell: tabs, remembered prefs, URL hash deep links, keyboard, the detail dialog, status line. */
'use strict';
(function (DM) {
  const {h, clear, $, $$} = DM;
  const st = DM.state;
  const TABS = ['overview', 'board', 'entries', 'changes', 'files', 'threads'];
  const VIEWS = DM.views = DM.views || {}; // tab -> {render(), scroller()}, filled by the view modules
  const stale = new Set(TABS);

  const params = new URLSearchParams(location.search);
  if (params.get('embed') === '1') document.documentElement.classList.add('embed');

  // ---------- prefs (tab + filters), remembered per browser ----------
  const DEF = {
    tab: 'overview', boardSub: 'kanban',
    entries: {q: '', file: '', status: 'notdone', wait: '', thread: '', sort: 'changed'},
    changes: {q: '', thread: '', file: '', kind: ''},
    threads: {q: '', openOnly: true},
    kanban: {file: '', thread: '', showDone: false},
    timeline: {range: '3d', openOnly: false},
    flow: {showDone: false, linkedOnly: true, collapsed: {}},
    files: {sel: '', raw: false},
  };
  function merge(def, got) {
    const out = {};
    for (const k in def) {
      const g = got && got[k];
      out[k] = def[k] && typeof def[k] === 'object' && !Array.isArray(def[k]) ? merge(def[k], g) :
        (g === undefined || typeof g !== typeof def[k] ? def[k] : g);
    }
    if (def.collapsed && got && got.collapsed && typeof got.collapsed === 'object') out.collapsed = got.collapsed;
    return out;
  }
  st.prefs = merge(DEF, DM.loadPrefs());
  st.DEF = DEF;
  let saveT = null;
  function save() { clearTimeout(saveT); saveT = setTimeout(() => DM.savePrefs(st.prefs), 200); }
  st.expanded = new Set(); // entry cards with the full body open (kept across refreshes, not across reloads)

  // ---------- views ----------
  function register(tab, view) { VIEWS[tab] = view; }
  function scroller(tab) { const v = VIEWS[tab]; return v && v.scroller ? v.scroller() : $('#view-' + tab + ' .scroll'); }

  function render(tab) {
    if (!st.data || !VIEWS[tab]) return;
    const sc = scroller(tab);
    const top = sc ? sc.scrollTop : 0, left = sc ? sc.scrollLeft : 0;
    try { VIEWS[tab].render(); } catch (e) { console.error('render ' + tab, e); }
    if (sc) { sc.scrollTop = top; sc.scrollLeft = left; }
    stale.delete(tab);
  }

  function show(tab, opts) {
    if (!TABS.includes(tab)) tab = 'overview';
    st.prefs.tab = tab; save();
    for (const b of $$('#tabs button')) { const on = b.dataset.tab === tab; b.classList.toggle('on', on); b.setAttribute('aria-selected', on); }
    for (const v of $$('.view')) v.classList.toggle('on', v.dataset.view === tab);
    if (stale.has(tab)) render(tab);
    if (!(opts && opts.keepHash)) setHash('tab=' + tab);
  }

  function dataChanged() {
    $('#loading').hidden = true;
    for (const t of TABS) stale.add(t);
    render(st.prefs.tab);
    if (!st.routed) { st.routed = true; route(); }
  }
  function rerender(tab) { stale.add(tab); if (st.prefs.tab === tab) render(tab); }

  // ---------- hash deep links: #tab=..., #entry=<id>, #search=<text>, #file=<name> ----------
  function setHash(hs) {
    const want = '#' + hs;
    if (location.hash !== want) { try { history.replaceState(null, '', want); } catch (_) { /* file:// or sandbox */ } }
  }
  function route() {
    const hs = new URLSearchParams(location.hash.replace(/^#/, ''));
    if (hs.has('entry')) { openEntryInList(hs.get('entry')); return; }
    if (hs.has('search')) {
      Object.assign(st.prefs.entries, DEF.entries, {q: hs.get('search'), status: ''});
      save(); stale.add('entries'); show('entries', {keepHash: true}); syncEntryControls(); return;
    }
    if (hs.has('file')) { openFile(hs.get('file')); return; }
    show(hs.get('tab') || st.prefs.tab);
  }
  window.addEventListener('hashchange', () => { if (st.data) route(); });

  function syncEntryControls() { if (DM.entries && DM.entries.syncControls) DM.entries.syncControls(); }

  // Open the Entries tab, scrolled to one entry, highlighted and expanded.
  function openEntryInList(id) {
    const e = st.entryById && st.entryById[id];
    if (!e) { show('entries'); return; }
    if (!DM.entries.matches(e)) { Object.assign(st.prefs.entries, DEF.entries, {status: '', sort: st.prefs.entries.sort}); save(); syncEntryControls(); }
    st.expanded.add(id);
    stale.add('entries');
    show('entries', {keepHash: true});
    setHash('entry=' + encodeURIComponent(id));
    const card = document.getElementById('card-' + cssId(id));
    if (card) {
      card.scrollIntoView({block: 'center'});
      card.classList.remove('flash'); void card.offsetWidth; card.classList.add('flash');
    }
  }
  function cssId(id) { return String(id).replace(/[^A-Za-z0-9_-]/g, '_'); }
  function openFile(name) {
    st.prefs.files.sel = name; save(); stale.add('files'); show('files', {keepHash: true}); setHash('file=' + encodeURIComponent(name));
  }

  // ---------- dialog ----------
  const dlg = $('#dlg');
  function openDialog(title, body) {
    clear($('#dlgTitle')).append(title);
    clear($('#dlgBody')).append(body);
    if (!dlg.open) { try { dlg.showModal(); } catch (_) { dlg.setAttribute('open', ''); } }
    $('#dlgBody').scrollTop = 0;
  }
  function closeDialog() { if (dlg.open) dlg.close(); }
  $('#dlgClose').addEventListener('click', closeDialog);
  dlg.addEventListener('click', ev => { if (ev.target === dlg) closeDialog(); });

  function openEntry(id) {
    const e = st.entryById[id];
    if (!e) return;
    const body = h('div', null, DM.entries.card(e, {full: true, inDialog: true}));
    const hist = st.historyByEntry[id] || [];
    if (hist.length) {
      body.appendChild(h('h4', {class: 'side-h'}, 'History'));
      body.appendChild(h('ul', {class: 'hist'}, hist.map(({c, it}) => h('li', null,
        DM.timeEl(c.time, 'dayclock'), ' ', h('span', {class: 'faint'}, '(', DM.timeEl(c.time), ') '),
        DM.changes.sentence(it, 'entry'), c.thread ? h('span', {class: 'faint'}, ' · ', c.thread) : null))));
    }
    body.appendChild(h('div', {class: 'dlg-f'}, h('button', {onclick: () => { closeDialog(); openEntryInList(id); }}, 'Show in Entries'),
      h('button', {onclick: () => copy(location.href.split('#')[0] + '#entry=' + encodeURIComponent(id))}, 'Copy link')));
    openDialog(h('span', null, DM.fileTitle(e.file), e.section ? ' › ' + e.section : ''), body);
  }
  function openChange(sha) {
    const c = st.data.changes.find(x => x.sha === sha);
    if (!c) return;
    openDialog(h('span', null, DM.dayLabel(c.time), ' ', DM.hhmm(c.time), ' · ', c.subject || 'change'), DM.changes.commitBlock(c, true));
  }
  function copy(text) {
    try { navigator.clipboard.writeText(text).then(() => toast('Link copied'), () => toast(text)); } catch (_) { toast(text); }
  }
  let toastT = null;
  function toast(msg) {
    let t = $('#toast');
    if (!t) { t = h('div', {id: 'toast', class: 'toast', role: 'status'}); document.body.appendChild(t); }
    t.textContent = msg; t.hidden = false;
    clearTimeout(toastT); toastT = setTimeout(() => { t.hidden = true; }, 1800);
  }

  function help() {
    const keys = [['/', 'Search (current tab, else Entries)'], ['1 – 6', 'Switch tabs'], ['Esc', 'Close a dialog, leave the search box'],
      ['Wheel / drag', 'Zoom / pan the timeline'], ['Double-click', 'Timeline: show everything']];
    openDialog('About this page', h('div', null,
      h('p', null, 'The Claude Manager: a read-only view of Claude Documents, the notebook every Claude on Quark writes. It never changes the documents; it reads its own copy of the repo every minute and checks here every 30 s.'),
      h('p', null, '"Waiting on" is a guess from each entry’s text and its linked PRs and CI runs: q’s decision, q to push, PR review, CI, tests, Claude working, or nobody yet.'),
      h('div', {class: 'keys'}, keys.map(([k, d]) => [h('kbd', null, k), h('span', null, d)])),
      st.data ? h('p', {class: 'faint'}, 'Data: ', st.source, ' · docs commit ', String(st.data.mirrorHead || '').slice(0, 7) || '?',
        st.data.generatedAt ? [' · made ', DM.timeEl(st.data.generatedAt, 'dayclock')] : '') : null));
  }

  // ---------- status line ----------
  function status() {
    const el = $('#status');
    clear(el);
    const d = st.data;
    if (st.offline) el.append(h('span', {class: 'live-off'}, '● offline'), ' ');
    else if (d) el.append(h('span', {class: d.fetchOk === false ? 'live-warn' : 'live-ok', title: d.fetchOk === false ? 'Docs may be out of date' : 'Live'}, '●'), ' ');
    if (st.lastOk) {
      const t = h('span', {title: [
        'Last checked ' + DM.full(st.lastOk),
        d && d.mirrorFetchedAt ? 'Docs downloaded from GitHub ' + DM.full(d.mirrorFetchedAt) : '',
        d && d.mirrorHead ? 'Docs commit ' + String(d.mirrorHead).slice(0, 12) : '',
        d && d.ghFetchedAt ? 'PR/CI data from ' + DM.full(d.ghFetchedAt) : ''].filter(Boolean).join('\n')}, 'Updated ' + DM.hhmm(st.lastOk));
      el.append(t);
    }
  }

  // ---------- events ----------
  for (const b of $$('#tabs button')) b.addEventListener('click', () => show(b.dataset.tab));
  $('#btnRefresh').addEventListener('click', () => DM.data.load());
  $('#btnHelp').addEventListener('click', help);

  const SEARCH = {entries: '#eQ', changes: '#cQ', threads: '#tQ'};
  document.addEventListener('keydown', ev => {
    if (ev.ctrlKey || ev.metaKey || ev.altKey) return;
    const tgt = ev.target;
    const typing = tgt && (tgt.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(tgt.tagName));
    if (ev.key === 'Escape') { if (typing && tgt.blur) tgt.blur(); return; }
    if (typing || dlg.open) return;
    if (ev.key === '/') {
      ev.preventDefault();
      let tab = st.prefs.tab;
      if (!SEARCH[tab]) { tab = 'entries'; show(tab); }
      const i = $(SEARCH[tab]); i.focus(); i.select();
    } else if (/^[1-6]$/.test(ev.key)) {
      show(TABS[+ev.key - 1]);
    } else if (ev.key === '?') help();
  });

  setInterval(() => { DM.tickTimes(); status(); }, 30000);

  // Shared tools bar: dim the tools that aren't running (probe every 60 s; an opaque answer means it's up).
  function probeTools() {
    for (const a of $$('.qt-nav a[data-tool]')) {
      if (a.getAttribute('aria-current') === 'page') continue;
      const url = a.getAttribute('href');
      const ctl = typeof AbortController === 'function' ? new AbortController() : null;
      const to = setTimeout(() => ctl && ctl.abort(), 4000);
      fetch(url, {mode: 'no-cors', cache: 'no-store', signal: ctl ? ctl.signal : undefined})
        .then(() => { a.classList.remove('down'); a.title = ''; }, () => { a.classList.add('down'); a.title = 'Not running right now'; })
        .finally(() => clearTimeout(to));
    }
  }
  if (!document.documentElement.classList.contains('embed')) { probeTools(); setInterval(probeTools, 60000); }

  DM.app = {register, show, render, rerender, save, dataChanged, openEntry, openEntryInList, openChange, openFile, openDialog, closeDialog,
    status, toast, copy, cssId};

  // boot after all view modules registered (this file loads last)
  show(st.prefs.tab, {keepHash: true});
  DM.data.load();
})(window.DM);
