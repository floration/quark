/* Overview tab: Needs q first, then latest changes, the waiting-on board and counts per file. */
'use strict';
(function (DM) {
  const {h, clear, $} = DM;
  const st = DM.state;

  // Sentences of a raw markdown body (whitespace collapsed, markdown kept for rendering).
  function rawSentences(md) {
    return String(md || '').replace(/\s+/g, ' ').trim().split(/(?<=[.?!])\s+(?=[A-Z0-9(`"'*_\[])/).filter(Boolean);
  }
  function recommendation(e) {
    const s = rawSentences(e.body).find(x => /recommend/i.test(x));
    if (!s) return e.recommendation ? String(e.recommendation) : null;
    return s.replace(/^\s*(\*\*)?(recommendation|recommended|we recommend)\s*(\*\*)?\s*[:\-]\s*/i, '');
  }
  // Question sentences of the body; a very short one ("OK?") takes the sentence before it as its context.
  function questionsOf(e) {
    const ss = rawSentences(e.body), out = [];
    ss.forEach((x, i) => {
      if (!/\?\s*$/.test(x.replace(/[)*_"']+$/, '')) || /recommend/i.test(x)) return;
      out.push(DM.plain(x).length < 16 && i > 0 && !out.includes(ss[i - 1]) ? ss[i - 1] + ' ' + x : x);
    });
    return out.slice(0, 6);
  }

  function needItem(e) {
    const isQ = /\?\s*$/.test(DM.plain(e.title));
    const qs = questionsOf(e);
    const rec = recommendation(e);
    const push = e.waitingOn.includes('push');
    const halt = DM.isHalting(e);
    const what = halt ? (e.haltWhat || rawSentences(e.body)[0] || '') : '';
    const ctx = rawSentences(e.body).filter(x => !qs.some(q => q.includes(x)) && !/recommend/i.test(x) && !(halt && DM.plain(x) === DM.plain(what))).slice(0, 2).join(' ');
    const el = h('div', {class: 'nq' + (halt ? ' halting' : e.priority === 'P1' ? ' p1' : '')});
    el.appendChild(h('div', {class: 'nq-h'},
      DM.priEl(e),
      h('a', {class: 'nq-t', href: '#entry=' + encodeURIComponent(e.id), onclick: ev => { ev.preventDefault(); DM.app.openEntry(e.id); }},
        DM.md.into(h('span'), DM.trimTitle(e.title) === DM.plain(e.title) ? e.title : e.title.replace(/[.:,;]+$/, ''), true)),
      h('span', {class: 'spacer'}),
      halt && e.haltingSince ? h('span', {class: 'faint nq-age'}, 'halting since ', DM.entries.haltTime(e)) :
      e.firstSeen && e.firstSeen.time ? h('span', {class: 'faint nq-age'}, push ? 'ready ' : 'asked ', DM.timeEl(e.firstSeen.time)) :
        e.date ? h('span', {class: 'faint nq-age'}, DM.dateOnly(e.date)) : null));
    if (halt) el.appendChild(h('div', {class: 'nq-ask halt-what'}, h('span', {class: 'why-k'}, 'What to do: '), DM.md.into(h('span'), what || DM.trimTitle(e.title), true)));
    if (qs.length && !(isQ && qs.length === 1)) el.appendChild(h('ul', {class: 'nq-qs'}, qs.map(q => DM.md.into(h('li'), q, true))));
    if (!halt && !isQ && !qs.length && (push || !e.file.startsWith('07'))) el.appendChild(h('div', {class: 'nq-ask'}, h('span', {class: 'why-k'}, push ? 'q’s part: ' : 'Needed from q: '),
      e.waitingWhy || (push ? 'Push to testing.' : 'A decision.')));
    if (ctx) el.appendChild(DM.md.into(h('div', {class: 'nq-ctx'}), ctx, true));
    if (rec) el.appendChild(h('div', {class: 'nq-rec'}, h('span', {class: 'why-k'}, 'Recommended: '), DM.md.into(h('span'), rec, true)));
    const chips = DM.entries.chipsFor(e).slice(0, 4);
    el.appendChild(h('div', {class: 'nq-m'},
      h('span', {class: 'faint'}, DM.fileTitle(e.file), e.section ? ' › ' + e.section : ''),
      h('span', {class: 'dotsep'}), DM.data.threadLink(e.thread, e.threadUrl),
      chips.length ? h('span', {class: 'chips inl'}, chips) : null,
      DM.edit ? DM.edit.editButton(e) : null));
    if (DM.edit && DM.edit.canAnswer(e)) el.appendChild(DM.edit.answerBox(e));
    return el;
  }

  function byPriThenNew(a, b) {
    const t = e => Date.parse((e.firstSeen && e.firstSeen.time) || '') || 0;
    return (DM.PRI_RANK[a.priority] || 9) - (DM.PRI_RANK[b.priority] || 9) || t(b) - t(a);
  }

  function section(title, count, sub, ...kids) {
    return h('section', {class: 'ov-sec'}, h('h2', null, title, count != null ? h('span', {class: 'cnt'}, count) : null,
      sub ? h('span', {class: 'sub'}, sub) : null), kids);
  }

  function render() {
    const d = st.data, E = d.entries;
    const root = clear($('#ovBody'));
    const open = E.filter(DM.isOpen);                       // (not finished, not a reference note, not archived)
    const halting = DM.data.haltingList();
    const need = open.filter(e => DM.needsQ(e) && !DM.isHalting(e));
    const pushes = need.filter(e => e.waitingOn.includes('push')).sort(byPriThenNew);
    const decisions = need.filter(e => !e.waitingOn.includes('push')).sort(byPriThenNew);
    const tell = open.filter(e => DM.waitKeys(e).includes('read')).sort(byPriThenNew);
    const weekAgo = Date.now() - 7 * 86400000;
    const finishedWeek = E.filter(e => DM.isDone(e) && e.lastChanged && Date.parse(e.lastChanged.time) > weekAgo).length;
    const k = key => open.filter(e => DM.waitKeys(e).includes(key)).length;

    // stat tiles
    const tile = (n, label, cls, onclick, title) => h('button', {class: 'tile ' + cls, onclick, title}, h('b', null, String(n)), h('span', null, label));
    root.appendChild(h('div', {class: 'tiles'},
      halting.length ? tile(halting.length, 'halting (Claude stopped)', 'halt', () => DM.entries.filterTo({wait: 'halt', status: 'notdone'}),
        'HALT items: a Claude agent can’t continue until you act') : null,
      tile(need.length, 'need q', 'q', () => DM.entries.filterTo({wait: 'q', status: 'notdone'}), 'Entries waiting on q: decisions and pushes'),
      tell.length ? tile(tell.length, 'for q to read', 'read', () => DM.entries.filterTo({wait: 'read', status: 'notdone'}), 'News in "Things to tell q" not told yet') : null,
      k('answered') ? tile(k('answered'), 'answered, Claude to act', 'answered', () => DM.entries.filterTo({wait: 'answered', status: 'notdone'}),
        'q answered these (09); a Claude still has to act on the answer') : null,
      tile(k('claude'), 'Claude working', 'claude', () => DM.entries.filterTo({wait: 'claude', status: 'notdone'})),
      tile(open.filter(e => DM.waitKeys(e).some(x => x === 'pr' || x === 'ci')).length, 'waiting for PR / CI', 'ci', () => DM.entries.filterTo({wait: 'prci', status: 'notdone'})),
      tile(k('tests'), 'need tests / repro', 'tests', () => DM.entries.filterTo({wait: 'tests', status: 'notdone'})),
      tile(k('nobody'), 'not started', 'nobody', () => DM.entries.filterTo({wait: 'nobody', status: 'notdone'})),
      tile(finishedWeek, 'finished this week', 'done', () => DM.entries.filterTo({status: '__finished', sort: 'changed'})),
      h('span', {class: 'spacer'}),
      h('span', {class: 'tiles-note faint'}, E.length + ' entries in ' + d.files.filter(f => f.kind !== 'document').length + ' files',
        d.changes[0] ? [h('br'), 'last change ', DM.timeEl(d.changes[0].time)] : null)));

    // Needs q (left) + latest changes (right)
    const left = h('div', {class: 'ov-col'});
    if (halting.length) {
      const hs = section('Halting', halting.length, 'a Claude agent is stopped on each of these until you act');
      hs.classList.add('halt-sec');
      hs.appendChild(h('div', {class: 'nq-group'}, halting.map(needItem)));
      left.appendChild(hs);
    }
    const nq = section('Needs q', need.length, need.length ? 'every item waiting on you, as a question' : null);
    nq.classList.add('needq-sec');
    if (!need.length) nq.appendChild(h('div', {class: 'empty ok'}, 'Nothing is waiting on you right now.'));
    if (decisions.length) nq.appendChild(h('div', {class: 'nq-group'}, h('h3', null, 'Decisions (' + decisions.length + ')'), decisions.map(needItem)));
    if (pushes.length) nq.appendChild(h('div', {class: 'nq-group'}, h('h3', null, 'Ready for you to push (' + pushes.length + ')'), pushes.map(needItem)));
    left.appendChild(nq);
    // Short lists q clears with one click per item (or all at once): news read, answers acted on.
    const quickList = (title, list, sub, allLabel) => {
      const sec = section(title, list.length, sub);
      const all = DM.edit && DM.edit.markAllButton(list, allLabel);
      if (all) sec.querySelector('h2').appendChild(all);
      sec.appendChild(h('ul', {class: 'tell'}, list.map(e => h('li', null,
        h('a', {href: '#entry=' + encodeURIComponent(e.id), onclick: ev => { ev.preventDefault(); DM.app.openEntry(e.id); }}, DM.md.into(h('span'), e.title, true)),
        ' ', h('span', {class: 'faint'}, e.thread || '', e.firstSeen && e.firstSeen.time ? [' · ', DM.timeEl(e.firstSeen.time)] : '',
          e.file.startsWith('06') ? '' : ' · ' + DM.fileShort(e.file)),
        DM.edit ? [' ', DM.edit.markButton(e)] : null))));
      left.appendChild(sec);
    };
    if (tell.length) quickList('Not told yet', tell, 'news for q in "Things to tell q"; nothing to decide', 'Mark all told');
    const acted = open.filter(e => DM.waitKeys(e).includes('answered')).sort(byPriThenNew);
    if (acted.length) quickList('Answered, Claude to act', acted, 'q answered these (09); once acted on, mark them done', 'Mark all done');
    const right = h('div', {class: 'ov-col'});
    const latest = section('Latest changes', null, null);
    // On a wide screen the board and the table sit under Needs q, so Latest changes (beside them) can be longer.
    const wide = wideNow();
    latest.appendChild(h('div', {class: 'mini-log'}, d.changes.slice(0, wide ? 30 : 15).map(c => DM.changes.commitBlock(c, false, 3))));
    latest.appendChild(h('button', {class: 'mini', onclick: () => DM.app.show('changes')}, 'Full change log'));
    right.appendChild(latest);
    root.appendChild(h('div', {class: 'ov-grid'}, left, right));
    const below = wide ? left : root;

    // waiting-on board (compact); the columns that are only sometimes used show only when they hold something
    const board = section('Waiting on', open.length, 'unfinished entries by what they wait for');
    const WCOLS = DM.WAIT.filter(w => !['halt', 'read', 'answered'].includes(w.key) || open.some(e => DM.waitKeys(e)[0] === w.key));
    const cols = h('div', {class: 'wboard'});
    cols.style.setProperty('--wn', WCOLS.length);
    for (const w of WCOLS) {
      const list = open.filter(e => DM.waitKeys(e)[0] === w.key).sort(byPriThenNew);
      cols.appendChild(h('div', {class: 'wcol ' + w.cls},
        h('div', {class: 'wcol-h'}, h('span', null, w.label), h('b', null, String(list.length))),
        list.slice(0, 6).map(e => h('a', {class: 'witem', href: '#entry=' + encodeURIComponent(e.id), title: DM.plain(e.title) + '\n' + (e.waitingWhy || ''),
          onclick: ev => { ev.preventDefault(); DM.app.openEntry(e.id); }},
          DM.priEl(e), ' ', DM.trimTitle(e.title))),
        list.length > 6 ? h('button', {class: 'mini more', onclick: () => DM.entries.filterTo({wait: w.key, status: 'notdone'})}, '+ ' + (list.length - 6) + ' more') : null));
    }
    board.appendChild(cols);
    below.appendChild(board);

    // counts per file
    const cnt = section('By file', null, 'click a number to see those entries');
    const COLS = [
      ['halt', 'Halting', e => DM.isHalting(e)],
      ['q', 'Needs q', e => DM.needsQ(e)],
      ['read', 'To read', e => DM.waitKeys(e).includes('read')],
      ['answered', 'Answered', e => DM.waitKeys(e).includes('answered')],
      ['prci', 'PR / CI', e => DM.waitKeys(e).some(x => x === 'pr' || x === 'ci')],
      ['tests', 'Tests', e => DM.waitKeys(e).includes('tests')],
      ['claude', 'Claude working', e => DM.waitKeys(e).includes('claude')],
      ['nobody', 'Not started', e => DM.waitKeys(e).includes('nobody')],
      ['done', 'Finished', e => DM.isDone(e)],
      ['ref', 'Reference', e => DM.isRef(e)],
    ];
    const tbl = h('table', {class: 'counts'}, h('thead', null, h('tr', null, h('th', null, 'File'), COLS.map(c => h('th', {class: 'n'}, c[1])), h('th', {class: 'n'}, 'Total'), h('th', null, ''))));
    const tb = h('tbody');
    for (const f of d.files.filter(f => f.kind !== 'document')) {
      const fe = E.filter(e => e.file === f.name);
      const nums = COLS.map(c => fe.filter(c[2]).length);
      const bar = h('div', {class: 'sbar', title: COLS.map((c, i) => c[1] + ': ' + nums[i]).join('\n')});
      const tot = fe.length || 1;
      COLS.forEach((c, i) => { if (nums[i]) bar.appendChild(h('i', {class: 'b-' + c[0], style: {width: (100 * nums[i] / tot) + '%'}})); });
      tb.appendChild(h('tr', null,
        h('td', null, h('a', {href: '#file=' + encodeURIComponent(f.name), onclick: ev => { ev.preventDefault(); DM.app.openFile(f.name); }}, DM.fileTitle(f.name)),
          f.pcUnsynced ? h('span', {class: 'badge pc', title: 'Edited on the PC, not synced yet'}, 'PC') : null),
        COLS.map((c, i) => h('td', {class: 'n'}, nums[i] ? h('button', {class: 'num-btn c-' + c[0], onclick: () => DM.entries.filterTo(
          c[0] === 'done' ? {file: f.name, status: '__finished'} : c[0] === 'ref' ? {file: f.name, status: '__ref'} : {file: f.name, status: 'notdone', wait: c[0]})},
          String(nums[i])) : h('span', {class: 'faint'}, '–'))),
        h('td', {class: 'n'}, h('button', {class: 'num-btn', onclick: () => DM.entries.filterTo({file: f.name})}, String(fe.length))),
        h('td', {class: 'barcell'}, bar)));
    }
    tbl.appendChild(tb);
    cnt.appendChild(h('div', {class: 'counts-wrap'}, tbl));
    below.appendChild(cnt);
  }
  const WIDE = 1800;
  let lastWide = null;
  function wideNow() { lastWide = ($('#ovScroll').clientWidth || innerWidth) >= WIDE; return lastWide; }
  new ResizeObserver(() => { if (st.data && st.prefs.tab === 'overview' && lastWide !== null && (($('#ovScroll').clientWidth || innerWidth) >= WIDE) !== lastWide) DM.app.render('overview'); }).observe($('#ovScroll'));

  (DM.views = DM.views || {}).overview = {render};
  DM.overview = {recommendation, questionsOf};
})(window.DM);
