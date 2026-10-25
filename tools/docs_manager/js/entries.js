/* Entries tab + the entry card used everywhere (Entries, dialog, Threads). */
'use strict';
(function (DM) {
  const {h, clear, $} = DM;
  const st = DM.state;

  function chipsFor(e) {
    const seen = new Set();
    return e.links.filter(l => l.kind !== 'thread' && !seen.has(l.kind + '|' + l.url + '|' + l.label) && seen.add(l.kind + '|' + l.url + '|' + l.label)).map(DM.chip);
  }

  // waitingWhy texts that only repeat the pill
  const GENERIC_WHY = /^(waiting on q'?s decision|a claude thread is working on it|nobody has started it( yet)?|nothing to do)\.?$/i;

  function noteLabel(e) {
    if (e.status === 'answered') return 'q answered: ';
    if (e.status === 'told') return 'Told: ';
    if (e.status === 'wontfix') return 'Why not: ';
    if (e.status === 'unknown') return 'Status text: ';
    return 'Note: ';
  }

  function card(e, opts) {
    opts = opts || {};
    const done = DM.isDone(e);
    const expanded = opts.full || st.expanded.has(e.id);
    const long = e.body.length > 320 || (e.body.match(/\n/g) || []).length > 3;
    const el = h('article', {class: 'card' + (done ? ' done' : '') + (DM.isHalting(e) ? ' halting' : DM.needsQ(e) ? ' needq' : ''), id: opts.inDialog ? null : 'card-' + DM.app.cssId(e.id)});
    const title = DM.md.into(h('span', {class: 'ttl'}), e.title, true);
    el.appendChild(h('div', {class: 'card-h'},
      DM.pillEl(e),
      DM.priEl(e),
      opts.inDialog ? h('h3', {class: 'ttl-w'}, title) :
        h('a', {class: 'ttl-w', href: '#entry=' + encodeURIComponent(e.id), title: 'Open details',
          onclick: ev => { ev.preventDefault(); DM.app.openEntry(e.id); }}, title)));
    el.appendChild(h('div', {class: 'card-m'},
      h('a', {href: '#file=' + encodeURIComponent(e.file), class: 'fl', title: e.file, onclick: ev => { ev.preventDefault(); DM.app.openFile(e.file); }}, DM.fileTitle(e.file)),
      e.section ? h('span', {class: 'faint'}, ' › ' + e.section) : null,
      h('span', {class: 'dotsep'}), DM.data.threadLink(e.thread, e.threadUrl),
      e.date ? [h('span', {class: 'dotsep'}), h('span', {title: 'Date written in the entry: ' + e.date}, DM.dateOnly(e.date))] : null,
      e.pcUnsynced ? h('span', {class: 'badge pc', title: 'Edited on the PC, not synced yet'}, 'PC edit') : null,
      e.archived ? h('span', {class: 'badge arch', title: 'In the file\u2019s Archive section'}, 'archived') : null));
    if (e.body) {
      const b = DM.md.into(h('div', {class: 'md card-b' + (long && !expanded ? ' clamp' : '')}), e.body);
      el.appendChild(b);
      if (long && !opts.full) el.appendChild(h('button', {class: 'mini more', onclick: () => {
        if (st.expanded.has(e.id)) st.expanded.delete(e.id); else st.expanded.add(e.id);
        el.replaceWith(card(e, opts));
      }}, expanded ? 'Show less' : 'Show more'));
    }
    if (DM.isHalting(e)) el.appendChild(h('div', {class: 'why halt-why'}, h('span', {class: 'why-k'}, 'A Claude agent is stopped until you: '),
      e.haltWhat || DM.sentences(e.body)[0] || DM.trimTitle(e.title), e.haltingSince ? [' ', h('span', {class: 'faint'}, '(halting since ', haltTime(e), ')')] : null));
    else if (!done && e.waitingWhy && !GENERIC_WHY.test(e.waitingWhy.trim()) && e.status !== 'unknown')
      el.appendChild(h('div', {class: 'why'}, /^waiting\b/i.test(e.waitingWhy) ? null : h('span', {class: 'why-k'}, 'Waiting: '), e.waitingWhy));
    if (e.statusNote) el.appendChild(h('div', {class: 'note'}, h('span', {class: 'why-k'}, noteLabel(e)), DM.md.into(h('span'), e.statusNote, true)));
    const chips = chipsFor(e);
    if (chips.length) el.appendChild(h('div', {class: 'chips'}, chips));
    const lc = e.lastChanged || {}, fs = e.firstSeen || {};
    el.appendChild(h('div', {class: 'card-f'},
      fs.time ? h('span', null, 'First written ', DM.timeEl(fs.time)) : null,
      lc.time && lc.time !== fs.time ? [h('span', {class: 'dotsep'}), h('span', null, 'last changed ', DM.timeEl(lc.time), lc.what ? ' (' + humanWhat(lc.what) + ')' : '')] : null,
      e.status === 'unknown' ? [h('span', {class: 'dotsep'}), h('span', {title: 'The entry has no "_date, thread, status_" line at its end'}, 'no status line')] : null,
      DM.edit ? DM.edit.editButton(e) : null, DM.edit ? DM.edit.markButton(e) : null));
    if (DM.edit && DM.edit.canAnswer(e)) el.appendChild(DM.edit.answerBox(e));
    return el;
  }
  // When an entry became halting: a relative <time> (a date-only value is shown as the day it was written).
  function haltTime(e) {
    const s = e.haltingSince;
    return /^\d{4}-\d{2}-\d{2}$/.test(s || '') ? h('span', {title: 'Date written in the entry'}, DM.dateOnly(s)) : DM.timeEl(s);
  }
  function humanWhat(w) {
    return String(w).replace(/\bin-progress\b/g, 'in progress').replace(/\bwontfix\b/g, "won't fix").replace(/->/g, '→');
  }

  // ---------- filters ----------
  function matchQ(e, q) {
    if (!q) return true;
    const hay = (e.title + ' ' + e.body + ' ' + e.thread + ' ' + e.section + ' ' + DM.fileTitle(e.file) + ' ' + e.file + ' ' + (e.statusNote || '') + ' ' +
      (e.waitingWhy || '') + ' ' + e.links.map(l => l.label).join(' ') + ' ' + DM.pill(e).label + ' ' + (e.priority || '')).toLowerCase();
    return q.toLowerCase().split(/\s+/).filter(Boolean).every(w => hay.includes(w));
  }
  // Status filter groups use values no status word can take ('notdone' = open work, '__finished', '__ref').
  function matches(e, f) {
    f = f || st.prefs.entries;
    if (f.file && e.file !== f.file) return false;
    if (f.status === 'notdone') { if (!DM.isOpen(e)) return false; }
    else if (f.status === '__finished') { if (!DM.isDone(e)) return false; }
    else if (f.status === '__ref') { if (!DM.isRef(e)) return false; }
    else if (f.status && e.status !== f.status) return false;
    if (f.wait === 'none') { if (DM.isOpen(e)) return false; }
    else if (f.wait === 'q') { if (!DM.needsQ(e)) return false; }
    else if (f.wait === 'prci') { if (!DM.waitKeys(e).some(x => x === 'pr' || x === 'ci')) return false; }
    else if (f.wait && !DM.waitKeys(e).includes(f.wait)) return false;
    if (f.thread === '__none' ? !!e.thread : f.thread && e.thread !== f.thread) return false;
    return matchQ(e, f.q);
  }
  function entryFiles() { return st.data.files.filter(f => f.kind !== 'document').map(f => f.name); }

  let selKeys = {};
  function fillSelect(sel, opts, value) {
    const key = JSON.stringify(opts);
    if (selKeys[sel.id] !== key) {                                   // (options unchanged: keep the element as it is)
      selKeys[sel.id] = key;
      clear(sel);
      for (const [v, label] of opts) sel.appendChild(h('option', {value: v}, label));
    }
    sel.value = opts.some(o => o[0] === value) ? value : '';
  }
  // Every count the filter menus show, in one pass over the entries.
  function counts() {
    const c = {file: {}, status: {}, wait: {}, thread: {}, open: 0, done: 0, ref: 0, q: 0, prci: 0};
    const inc = (o, k) => { o[k] = (o[k] || 0) + 1; };
    for (const e of st.data.entries) {
      inc(c.file, e.file); inc(c.status, e.status); inc(c.thread, e.thread || '__none');
      if (DM.isDone(e)) c.done++; else if (DM.isRef(e)) c.ref++; else c.open++;
      const keys = DM.waitKeys(e);
      for (const k of keys) inc(c.wait, k);
      if (keys.includes('pr') || keys.includes('ci')) c.prci++;
      if (DM.needsQ(e)) c.q++;
    }
    return c;
  }
  function syncControls() {
    const f = st.prefs.entries, c = counts();
    const n = (k, o) => ' (' + ((o || {})[k] || 0) + ')';
    fillSelect($('#eFile'), [['', 'All files']].concat(entryFiles().map(x => [x, DM.fileTitle(x) + n(x, c.file)])), f.file);
    const statuses = Object.keys(c.status).sort((a, b) => order(a) - order(b));
    fillSelect($('#eStatus'), [['', 'Any status'], ['notdone', 'Not finished (' + c.open + ')'], ['__finished', 'Finished (' + c.done + ')'],
      ['__ref', 'Reference or archived (' + c.ref + ')']].concat(statuses.map(x => [x, DM.statusWord(x) + n(x, c.status)])), f.status);
    fillSelect($('#eWait'), [['', 'Anyone'], ['q', 'q (decision or push) (' + c.q + ')'], ['prci', 'PR or CI (' + c.prci + ')']]
      .concat(DM.WAIT.map(w => [w.key, w.label + n(w.key, c.wait)])).concat([['none', 'Nothing: finished or reference (' + (c.done + c.ref) + ')']]), f.wait);
    const threads = Object.keys(c.thread).filter(t => t !== '__none').sort((a, b) => a.localeCompare(b));
    fillSelect($('#eThread'), [['', 'Any thread']].concat(threads.map(t => [t, t + n(t, c.thread)])).concat(
      c.thread.__none ? [['__none', 'No thread (' + c.thread.__none + ')']] : []), f.thread);
    $('#eSort').value = f.sort;
    if (document.activeElement !== $('#eQ')) $('#eQ').value = f.q;
  }
  function order(s) { return ['open', 'in-progress', 'unknown', 'done', 'fixed', 'answered', 'told', 'wontfix'].indexOf(s) + 1 || 99; }

  function sorted(list) {
    const t = e => (e.lastChanged && e.lastChanged.time) || (e.firstSeen && e.firstSeen.time) || '';
    const tm = new Map(list.map(e => [e, Date.parse(t(e)) || 0]));
    const byT = (a, b) => tm.get(b) - tm.get(a);
    const s = st.prefs.entries.sort;
    if (s === 'priority') return list.sort((a, b) => (DM.PRI_RANK[a.priority] || 9) - (DM.PRI_RANK[b.priority] || 9) || (DM.isOpen(b) - DM.isOpen(a)) || byT(a, b));
    if (s === 'date') return list.sort((a, b) => String(b.date || '').localeCompare(String(a.date || '')) || byT(a, b));
    if (s === 'file') { const idx = new Map(st.data.entries.map((e, i) => [e, i])); return list.sort((a, b) => idx.get(a) - idx.get(b)); }
    return list.sort(byT);
  }

  // ---------- the card list: cached cards, built in slices ----------
  // A card is rebuilt only when its entry's data (or the page's edit mode) changed, so a refresh that touches two
  // entries costs two cards, not 400. Cards that aren't cached yet are built in ~8 ms slices between frames
  // (the first screenful at once), so showing every entry never blocks scrolling or typing.
  const cache = new Map();          // entry id -> {key, el}
  const sigs = new WeakMap();       // entry object -> its JSON (entry objects are new after each load)
  let cacheMode = '', job = null, shownIds = '';
  const SLICE_MS = 8, FIRST = 30;
  function cardFor(e) {
    let sig = sigs.get(e);
    if (sig === undefined) { sig = JSON.stringify(e); sigs.set(e, sig); }
    const key = (st.expanded.has(e.id) ? '1' : '0') + sig;
    const hit = cache.get(e.id);
    if (hit && hit.key === key) return hit.el;
    const el = card(e);
    cache.set(e.id, {key, el});
    return el;
  }
  function isCached(e) { const hit = cache.get(e.id), sig = sigs.get(e); return !!(hit && sig !== undefined && hit.key === (st.expanded.has(e.id) ? '1' : '0') + sig); }

  function render() {
    syncControls();
    const mode = DM.edit && DM.edit.canEdit() ? 'edit' : 'read';     // (cards carry Edit / Answer buttons only when editing)
    if (mode !== cacheMode) { cache.clear(); cacheMode = mode; shownIds = ''; }
    const list = sorted(st.data.entries.filter(e => matches(e)));
    const body = $('#eBody');
    $('#eCount').textContent = list.length + ' of ' + st.data.entries.length + ' entries';
    if (job) { cancelAnimationFrame(job.raf); job = null; }
    if (!list.length) {
      shownIds = '';
      clear(body).classList.remove('cols');
      body.appendChild(h('div', {class: 'empty'}, 'No entries match these filters. ', h('button', {class: 'mini', onclick: reset}, 'Clear filters')));
      return;
    }
    // n balanced columns filled left to right in sort order (no grid holes under short cards)
    const n = colCount();
    lastCols = n;
    // Same entries in the same order and columns (a refresh, a save): swap only the cards whose entry changed, so the
    // browser restyles a couple of cards instead of all of them.
    const ids = list.map(e => e.id).join('\n') + '|' + n;
    if (ids === shownIds && body.classList.contains('cols') && body.children.length === n) {
      const colEls = [...body.children];
      list.forEach((e, i) => {
        const el = cardFor(e), col = colEls[i % n], at = col.children[Math.floor(i / n)];
        if (at !== el) { if (at) at.replaceWith(el); else col.appendChild(el); }
      });
      return;
    }
    shownIds = ids;
    const cols = Array.from({length: n}, () => h('div', {class: 'col'}));
    job = {list, cols, i: 0, raf: 0};
    pump(job, true);
    clear(body).classList.add('cols');
    body.style.setProperty('--ecols', n);
    body.append(...cols);
    if (cache.size > 2 * st.data.entries.length) for (const id of cache.keys()) if (!st.entryById[id]) cache.delete(id);
  }
  function pump(j, first) {
    const t0 = performance.now();
    while (j.i < j.list.length) {
      const e = j.list[j.i];
      if (!(first && j.i < FIRST) && !isCached(e) && performance.now() - t0 > SLICE_MS) break;
      j.cols[j.i % j.cols.length].appendChild(cardFor(e));
      j.i++;
    }
    if (j.i < j.list.length) j.raf = requestAnimationFrame(() => { if (job === j) pump(j, false); });
    else if (job === j) job = null;
  }
  // Build every card still waiting (before jumping to one).
  function flush() {
    const j = job;
    if (!j) return;
    cancelAnimationFrame(j.raf); job = null;
    for (; j.i < j.list.length; j.i++) j.cols[j.i % j.cols.length].appendChild(cardFor(j.list[j.i]));
  }
  let lastCols = 0;
  const CARD_MIN = 400, GAP = 10;   // q's 2560 px screen: six cards across; 1280 px: three
  function colCount() { const w = ($('#eScroll').clientWidth || innerWidth) - 24; return Math.max(1, Math.floor((w + GAP) / (CARD_MIN + GAP))); }
  new ResizeObserver(() => { if (st.data && st.prefs.tab === 'entries' && colCount() !== lastCols) DM.app.render('entries'); }).observe($('#eScroll'));
  function reset() { Object.assign(st.prefs.entries, st.DEF.entries, {status: '', sort: st.prefs.entries.sort}); DM.app.save(); render(); }

  // Set filters from elsewhere (overview counts, board) and go to the tab.
  function filterTo(f) {
    Object.assign(st.prefs.entries, st.DEF.entries, {status: ''}, f);
    DM.app.save(); DM.app.rerender('entries'); DM.app.show('entries');
    $('#eScroll').scrollTop = 0;
  }

  let qT = null;
  $('#eQ').addEventListener('input', () => { clearTimeout(qT); qT = setTimeout(() => { st.prefs.entries.q = $('#eQ').value; DM.app.save(); render(); }, 120); });
  for (const [id, key] of [['#eFile', 'file'], ['#eStatus', 'status'], ['#eWait', 'wait'], ['#eThread', 'thread'], ['#eSort', 'sort']])
    $(id).addEventListener('change', () => { st.prefs.entries[key] = $(id).value; DM.app.save(); render(); $('#eScroll').scrollTop = 0; });
  $('#eReset').addEventListener('click', reset);

  DM.entries = {card, matches, syncControls, filterTo, chipsFor, humanWhat, haltTime, flush};
  (DM.views = DM.views || {}).entries = {render};
})(window.DM);
