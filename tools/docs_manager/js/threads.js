/* Threads tab: each project thread, its link, its entries and its last activity. */
'use strict';
(function (DM) {
  const {h, clear, $} = DM;
  const st = DM.state;
  const showDone = new Set(); // threads whose finished entries are unfolded (kept across refreshes)

  function threadList() {
    const d = st.data, map = new Map();
    for (const t of d.threads) if (t && t.title) map.set(t.title, {title: t.title, url: t.url, last: Date.parse(t.lastActivity || '') || 0, entries: [], changes: []});
    for (const e of d.entries) {
      const k = e.thread || '';
      if (!map.has(k)) map.set(k, {title: k, url: e.threadUrl, last: 0, entries: [], changes: []});
      map.get(k).entries.push(e);
    }
    for (const c of d.changes) {
      const ths = new Set([c.thread || ''].concat(c.items.map(it => (st.entryById[it.entryId] || {}).thread || '').filter(Boolean)));
      if (c.thread) ths.delete('');
      for (const k of ths) if (map.has(k)) map.get(k).changes.push(c);
    }
    for (const t of map.values()) {
      for (const e of t.entries) t.last = Math.max(t.last, Date.parse((e.lastChanged || {}).time || '') || 0);
      for (const c of t.changes) t.last = Math.max(t.last, Date.parse(c.time) || 0);
    }
    return [...map.values()].filter(t => t.entries.length || t.changes.length || t.title).sort((a, b) => b.last - a.last);
  }

  function row(e) {
    return h('li', {class: 'trow' + (DM.isDone(e) ? ' done' : '')},
      DM.pillEl(e, true),
      e.priority ? h('span', {class: 'pri ' + e.priority}, e.priority) : null,
      h('a', {href: '#entry=' + encodeURIComponent(e.id), onclick: ev => { ev.preventDefault(); DM.app.openEntry(e.id); }, title: e.waitingWhy || ''}, DM.md.into(h('span'), e.title, true)),
      h('span', {class: 'faint'}, ' ' + DM.fileShort(e.file)));
  }

  function render() {
    const p = st.prefs.threads;
    if (document.activeElement !== $('#tQ')) $('#tQ').value = p.q;
    $('#tOpenOnly').checked = p.openOnly;
    let list = threadList();
    const total = list.length;
    if (p.openOnly) list = list.filter(t => t.entries.some(e => !DM.isDone(e)));
    if (p.q) {
      const ws = p.q.toLowerCase().split(/\s+/).filter(Boolean);
      list = list.filter(t => { const hay = (t.title + ' ' + t.entries.map(e => e.title).join(' ')).toLowerCase(); return ws.every(w => hay.includes(w)); });
    }
    $('#tCount').textContent = list.length + ' of ' + total + ' threads';
    const body = clear($('#tBody'));
    if (!list.length) { body.appendChild(h('div', {class: 'empty'}, 'No threads match.')); return; }
    for (const t of list) {
      const open = t.entries.filter(e => !DM.isDone(e)).sort((a, b) => (DM.needsQ(b) - DM.needsQ(a)) || (DM.PRI_RANK[a.priority] || 9) - (DM.PRI_RANK[b.priority] || 9));
      const done = t.entries.filter(DM.isDone);
      const counts = {};
      for (const e of t.entries) { const pl = DM.pill(e); const k = pl.cls + '|' + pl.label; counts[k] = (counts[k] || 0) + 1; }
      const key = t.title || '(none)';
      const card = h('article', {class: 'card tcard'},
        h('div', {class: 'tc-h'},
          t.title ? DM.ext(t.url, t.title, 'tc-t', 'Open the thread on claude.ai') : h('span', {class: 'tc-t faint'}, 'Entries with no thread'),
          h('span', {class: 'spacer'}),
          t.last ? h('span', {class: 'faint'}, 'active ', DM.timeEl(new Date(t.last)), ' · ') : null,
          h('span', {class: 'faint'}, t.entries.length + (t.entries.length === 1 ? ' entry' : ' entries'))),
        Object.keys(counts).length ? h('div', {class: 'tc-pills'}, Object.entries(counts).sort((a, b) => b[1] - a[1]).map(([k, n]) => {
          const [cls, label] = k.split('|');
          return h('span', {class: 'pill sm ' + cls}, n + ' ' + label.toLowerCase());
        })) : null,
        open.length ? h('ul', {class: 'tlist'}, open.map(row)) : h('div', {class: 'faint'}, t.entries.length ? 'Everything here is finished.' : 'No entries.'),
        done.length ? (showDone.has(key) ? [h('ul', {class: 'tlist'}, done.map(row)), h('button', {class: 'mini', onclick: () => { showDone.delete(key); render(); }}, 'Hide finished')] :
          h('button', {class: 'mini', onclick: () => { showDone.add(key); render(); }}, 'Show ' + done.length + ' finished')) : null,
        t.changes.length ? h('div', {class: 'tc-ch'}, h('span', {class: 'why-k'}, 'Latest: '), t.changes.slice(0, 2).map((c, i) => [i ? ' · ' : '',
          h('a', {href: '#tab=changes', onclick: ev => { ev.preventDefault(); DM.app.openChange(c.sha); }}, c.subject || 'change'), ' ', h('span', {class: 'faint'}, DM.timeEl(c.time))])) : null);
      body.appendChild(card);
    }
  }

  let qT = null;
  $('#tQ').addEventListener('input', () => { clearTimeout(qT); qT = setTimeout(() => { st.prefs.threads.q = $('#tQ').value; DM.app.save(); render(); }, 120); });
  $('#tOpenOnly').addEventListener('change', () => { st.prefs.threads.openOnly = $('#tOpenOnly').checked; DM.app.save(); render(); });

  (DM.views = DM.views || {}).threads = {render};
})(window.DM);
