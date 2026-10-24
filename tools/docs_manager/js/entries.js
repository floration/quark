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
    const el = h('article', {class: 'card' + (done ? ' done' : '') + (DM.needsQ(e) ? ' needq' : ''), id: opts.inDialog ? null : 'card-' + DM.app.cssId(e.id)});
    const title = DM.md.into(h('span', {class: 'ttl'}), e.title, true);
    el.appendChild(h('div', {class: 'card-h'},
      DM.pillEl(e),
      e.priority ? h('span', {class: 'pri ' + e.priority, title: {P1: 'Priority 1 (highest)', P2: 'Priority 2', P3: 'Priority 3 (lowest)'}[e.priority] || e.priority}, e.priority) : null,
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
    if (!done && e.waitingWhy && !GENERIC_WHY.test(e.waitingWhy.trim()) && e.status !== 'unknown')
      el.appendChild(h('div', {class: 'why'}, /^waiting\b/i.test(e.waitingWhy) ? null : h('span', {class: 'why-k'}, 'Waiting: '), e.waitingWhy));
    if (e.statusNote) el.appendChild(h('div', {class: 'note'}, h('span', {class: 'why-k'}, noteLabel(e)), DM.md.into(h('span'), e.statusNote, true)));
    const chips = chipsFor(e);
    if (chips.length) el.appendChild(h('div', {class: 'chips'}, chips));
    const lc = e.lastChanged || {}, fs = e.firstSeen || {};
    el.appendChild(h('div', {class: 'card-f'},
      fs.time ? h('span', null, 'First written ', DM.timeEl(fs.time)) : null,
      lc.time && lc.time !== fs.time ? [h('span', {class: 'dotsep'}), h('span', null, 'last changed ', DM.timeEl(lc.time), lc.what ? ' (' + humanWhat(lc.what) + ')' : '')] : null,
      e.status === 'unknown' ? [h('span', {class: 'dotsep'}), h('span', {title: 'The entry has no "_date, thread, status_" line at its end'}, 'no status line')] : null,
      DM.edit ? DM.edit.editButton(e) : null));
    if (DM.edit && DM.edit.canAnswer(e)) el.appendChild(DM.edit.answerBox(e));
    return el;
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
  function matches(e, f) {
    f = f || st.prefs.entries;
    if (f.file && e.file !== f.file) return false;
    if (f.status === 'notdone' && DM.isDone(e)) return false;
    if (f.status === 'done' && !DM.isDone(e)) return false;
    if (f.status && !['notdone', 'done'].includes(f.status) && e.status !== f.status) return false;
    if (f.wait === 'none') { if (!DM.isDone(e)) return false; }
    else if (f.wait === 'q') { if (!DM.needsQ(e)) return false; }
    else if (f.wait === 'prci') { if (!DM.waitKeys(e).some(x => x === 'pr' || x === 'ci')) return false; }
    else if (f.wait && !DM.waitKeys(e).includes(f.wait)) return false;
    if (f.thread === '__none' ? !!e.thread : f.thread && e.thread !== f.thread) return false;
    return matchQ(e, f.q);
  }
  function entryFiles() { return st.data.files.filter(f => f.kind !== 'document').map(f => f.name); }

  function fillSelect(sel, opts, value) {
    clear(sel);
    for (const [v, label] of opts) sel.appendChild(h('option', {value: v}, label));
    sel.value = opts.some(o => o[0] === value) ? value : '';
  }
  function syncControls() {
    const f = st.prefs.entries, all = st.data.entries;
    const cnt = pred => all.filter(pred).length;
    fillSelect($('#eFile'), [['', 'All files']].concat(entryFiles().map(n => [n, DM.fileTitle(n) + ' (' + cnt(e => e.file === n) + ')'])), f.file);
    const statuses = [...new Set(all.map(e => e.status))].sort((a, b) => order(a) - order(b));
    fillSelect($('#eStatus'), [['', 'Any status'], ['notdone', 'Not finished (' + cnt(e => !DM.isDone(e)) + ')'], ['done', 'Finished (' + cnt(DM.isDone) + ')']]
      .concat(statuses.map(s => [s, DM.statusWord(s) + ' (' + cnt(e => e.status === s) + ')'])), f.status);
    fillSelect($('#eWait'), [['', 'Anyone'], ['q', 'q (decision or push) (' + cnt(DM.needsQ) + ')'],
      ['prci', 'PR or CI (' + cnt(e => DM.waitKeys(e).some(x => x === 'pr' || x === 'ci')) + ')']].concat(DM.WAIT.map(w => [w.key, w.label + ' (' + cnt(e => DM.waitKeys(e).includes(w.key)) + ')'])).concat([['none', 'Nothing, finished']]), f.wait);
    const threads = [...new Set(all.map(e => e.thread).filter(Boolean))].sort((a, b) => a.localeCompare(b));
    fillSelect($('#eThread'), [['', 'Any thread']].concat(threads.map(t => [t, t + ' (' + cnt(e => e.thread === t) + ')'])).concat(
      all.some(e => !e.thread) ? [['__none', 'No thread (' + cnt(e => !e.thread) + ')']] : []), f.thread);
    $('#eSort').value = f.sort;
    if (document.activeElement !== $('#eQ')) $('#eQ').value = f.q;
  }
  function order(s) { return ['open', 'in-progress', 'unknown', 'done', 'fixed', 'answered', 'told', 'wontfix'].indexOf(s) + 1 || 99; }

  function sorted(list) {
    const t = e => (e.lastChanged && e.lastChanged.time) || (e.firstSeen && e.firstSeen.time) || '';
    const byT = (a, b) => (Date.parse(t(b)) || 0) - (Date.parse(t(a)) || 0);
    const s = st.prefs.entries.sort;
    const idx = new Map(st.data.entries.map((e, i) => [e, i]));
    if (s === 'priority') return list.sort((a, b) => (DM.PRI_RANK[a.priority] || 9) - (DM.PRI_RANK[b.priority] || 9) || (DM.isDone(a) - DM.isDone(b)) || byT(a, b));
    if (s === 'date') return list.sort((a, b) => String(b.date || '').localeCompare(String(a.date || '')) || byT(a, b));
    if (s === 'file') return list.sort((a, b) => idx.get(a) - idx.get(b));
    return list.sort(byT);
  }

  function render() {
    syncControls();
    const list = sorted(st.data.entries.filter(e => matches(e)));
    const body = clear($('#eBody'));
    $('#eCount').textContent = list.length + ' of ' + st.data.entries.length + ' entries';
    if (!list.length) {
      body.appendChild(h('div', {class: 'empty'}, 'No entries match these filters. ', h('button', {class: 'mini', onclick: reset}, 'Clear filters')));
      return;
    }
    // n balanced columns filled left to right in sort order (no grid holes under short cards)
    const n = colCount();
    lastCols = n;
    body.classList.toggle('cols', n > 1);
    const cols = n > 1 ? Array.from({length: n}, () => h('div', {class: 'col'})) : [body];
    list.forEach((e, i) => cols[i % n].appendChild(card(e)));
    if (n > 1) body.append(...cols);
  }
  let lastCols = 0;
  function colCount() { const w = $('#eScroll').clientWidth || innerWidth; return w < 1060 ? 1 : w < 1800 ? 2 : 3; }
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

  DM.entries = {card, matches, syncControls, filterTo, chipsFor, humanWhat};
  (DM.views = DM.views || {}).entries = {render};
})(window.DM);
