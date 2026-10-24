/* Board tab: switches between Kanban (here), Timeline (timeline.js) and Flow graph (flow.js). */
'use strict';
(function (DM) {
  const {h, clear, $, $$} = DM;
  const st = DM.state;

  function sel(opts, value, onchange, label) {
    const s = h('select', {'aria-label': label, onchange: ev => onchange(ev.target.value)});
    for (const [v, l] of opts) s.appendChild(h('option', {value: v}, l));
    s.value = opts.some(o => o[0] === value) ? value : '';
    return s;
  }
  function check(label, value, onchange, title) {
    return h('label', {class: 'chk', title}, h('input', {type: 'checkbox', checked: value || null, onchange: ev => onchange(ev.target.checked)}), label);
  }

  // ---------- kanban ----------
  function miniCard(e) {
    const keys = DM.waitKeys(e);
    const gh = e.links.filter(l => ['pr', 'branch', 'run'].includes(l.kind) && l.state);
    return h('a', {class: 'kcard' + (DM.isDone(e) ? ' done' : ''), href: '#entry=' + encodeURIComponent(e.id), title: (e.waitingWhy || '') ,
      onclick: ev => { ev.preventDefault(); DM.app.openEntry(e.id); }},
      h('div', {class: 'kc-t'}, e.priority ? h('span', {class: 'pri ' + e.priority}, e.priority) : null, ' ', DM.trimTitle(e.title)),
      h('div', {class: 'kc-m'}, DM.fileShort(e.file), e.thread ? ' · ' + e.thread : '',
        e.lastChanged && e.lastChanged.time ? [' · ', DM.timeEl(e.lastChanged.time)] : null),
      keys.length > 1 || gh.length ? h('div', {class: 'kc-tags'},
        keys.slice(1).map(k => h('span', {class: 'tag ' + DM.WAIT_BY[k].cls}, 'also ' + DM.WAIT_BY[k].label)),
        gh.slice(0, 3).map(l => h('span', {class: 'tag gh s-' + l.state, title: l.label}, l.kind === 'branch' ? DM.shortBranch(l.label) : l.label, ' ', l.state.replace('_', ' ')))) : null);
  }

  function kanban(body, tools) {
    const p = st.prefs.kanban, d = st.data;
    const files = d.files.filter(f => f.kind !== 'document').map(f => [f.name, DM.fileTitle(f.name)]);
    const threads = [...new Set(d.entries.map(e => e.thread).filter(Boolean))].sort((a, b) => a.localeCompare(b)).map(t => [t, t]);
    tools.append(
      sel([['', 'All files']].concat(files), p.file, v => { p.file = v; DM.app.save(); draw(); }, 'File'),
      sel([['', 'All threads']].concat(threads), p.thread, v => { p.thread = v; DM.app.save(); draw(); }, 'Thread'),
      check('show finished (last 7 days)', p.showDone, v => { p.showDone = v; DM.app.save(); draw(); }));
    const pass = e => (!p.file || e.file === p.file) && (!p.thread || e.thread === p.thread);
    const open = d.entries.filter(e => !DM.isDone(e) && pass(e));
    const sortE = (a, b) => (DM.PRI_RANK[a.priority] || 9) - (DM.PRI_RANK[b.priority] || 9) ||
      (Date.parse((b.lastChanged || {}).time || '') || 0) - (Date.parse((a.lastChanged || {}).time || '') || 0);
    const cols = DM.WAIT.map(w => ({w, list: open.filter(e => DM.waitKeys(e)[0] === w.key).sort(sortE)}));
    if (p.showDone) {
      const since = Date.now() - 7 * 86400000;
      cols.push({w: {key: 'done', label: 'Finished', cls: 'done'}, list: d.entries.filter(e => DM.isDone(e) && pass(e) &&
        Date.parse((e.lastChanged || {}).time || '') > since).sort(sortE)});
    }
    const grid = h('div', {class: 'kanban', style: {gridTemplateColumns: 'repeat(' + cols.length + ', minmax(160px, 1fr))'}});
    for (const {w, list} of cols) {
      grid.appendChild(h('div', {class: 'kcol ' + w.cls},
        h('div', {class: 'kcol-h'}, h('span', null, w.label), h('b', null, String(list.length))),
        list.length ? list.map(miniCard) : h('div', {class: 'faint kempty'}, 'nothing')));
    }
    body.appendChild(grid);
  }

  const SUBS = {kanban: {render: kanban}};
  function draw() {
    const sub = SUBS[st.prefs.boardSub] ? st.prefs.boardSub : 'kanban';
    for (const b of $$('#boardSeg button')) b.classList.toggle('on', b.dataset.sub === sub);
    const body = clear($('#boardBody')), tools = clear($('#boardTools'));
    body.className = 'board-' + sub;
    SUBS[sub].render(body, tools);
  }
  for (const b of $$('#boardSeg button')) b.addEventListener('click', () => {
    if (st.prefs.boardSub === b.dataset.sub) return;
    st.prefs.boardSub = b.dataset.sub; DM.app.save(); draw(); $('#boardScroll').scrollTop = 0;
  });

  (DM.views = DM.views || {}).board = {render: draw};
  DM.board = {SUBS, sel, check, redraw: () => { if (st.prefs.tab === 'board') draw(); }};
})(window.DM);
