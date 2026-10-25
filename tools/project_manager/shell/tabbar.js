'use strict';
// Tab bar view: renders the tab list main.js sends, forwards clicks, shows the Editor Manager's facts and the Dallas time.
// Also: drag a tab among its siblings (saved to tabs.json by main.js), right-click menus (main.js builds them), the
// "+N" list of tabs that do not fit, the find box (Ctrl+F) and the zoom level while it is not 100 %.
(() => {
  const api = window.quarkTabbar;
  const tabsEl = document.getElementById('tabs');
  const errEl = document.getElementById('err');
  const updEl = document.getElementById('update');
  const statsEl = document.getElementById('stats');
  const moreEl = document.getElementById('more');
  const zoomEl = document.getElementById('zoom');
  const findEl = document.getElementById('find');
  const findQ = document.getElementById('find-q');
  const findN = document.getElementById('find-n');
  const msgEl = updEl.querySelector('.msg');
  const label = { up: 'running', down: 'not running', starting: 'starting', restarting: 'stopped, restarting by itself shortly', missing: 'not installed', unknown: 'not checked yet' };
  let last = '';
  let lastStats = '';

  // Ctrl+1..8 = tabs 1..8, Ctrl+9 = the last tab (as in main.js)
  const shortcut = (i, count) => (i < 8 ? i + 1 : i === count - 1 ? 9 : 0);

  function tabButton(t, i, count, active, kids, open) {
    const b = document.createElement('button');
    b.className = 'tab' + (t.id === active ? ' active' : '') + (t.depth >= 2 ? ' child' : '');
    b.dataset.status = t.status;
    b.dataset.id = t.id;
    b.dataset.parent = t.parent || '';
    b.setAttribute('role', 'tab');
    b.setAttribute('aria-selected', t.id === active ? 'true' : 'false');
    const what = t.kind === 'file' && t.status === 'up' ? 'local page' : (label[t.status] || t.status);
    const n = shortcut(i, count);
    b.title = t.name + ': ' + what + (t.missing ? ' (' + t.missing + ')' : '') + (n ? '  [Ctrl+' + n + ']' : '');
    const dot = document.createElement('span'); dot.className = 'dot';
    const name = document.createElement('span'); name.className = 'name'; name.textContent = t.name;
    // (the short name fit() switches to when the bar is too narrow: "Map Editor" -> "Map", the group label says Editors;
    //  "Editor Manager" -> "Editor Mgr", since a bare "Editor" next to the EDITORS label reads as one of them)
    let short = t.id === active ? '' : t.name.replace(/\s+(Editor|Manager)$/, '');
    if (/^(Editor|Manager)$/i.test(short)) short = t.name.replace(/\s+Manager$/, ' Mgr');
    if (short && short !== t.name) { name.dataset.long = t.name; name.dataset.short = short; }
    b.append(dot, name);
    const halt = Number(t.badge) || 0;
    if (halt > 0 && t.badgeKind === 'note') {
      // the Play tab: play notes waiting to be written (a game it started has closed)
      const bd = document.createElement('span'); bd.className = 'badge note'; bd.textContent = halt > 99 ? '99+' : String(halt);
      b.append(bd);
      b.title += '\n' + halt + (halt === 1 ? ' play note' : ' play notes') + ' to write';
    } else if (halt > 0) {
      // the Supervisor Board's halting count: things q must do that block a Claude agent
      const bd = document.createElement('span'); bd.className = 'badge'; bd.textContent = halt > 99 ? '99+' : String(halt);
      b.append(bd);
      b.title += '\n' + halt + ' halting: Claude is blocked on you';
      b.classList.add('halting');
    }
    if (t.updated) {
      // reinstalled while its page had unsaved changes: the old page stays until q reloads it
      const u = document.createElement('span'); u.className = 'upd'; u.textContent = '\u21bb';
      b.append(u);
      b.title += '\nUpdated: save, then reload (Ctrl+R) to get the new version';
    }
    if (kids && t.depth >= 1) {
      // a parent below the top level (Editor Manager): a caret that shows whether its child tabs are open
      const c = document.createElement('span'); c.className = 'caret' + (open ? ' open' : ''); c.textContent = open ? '' : String(kids);
      b.append(c);
    }
    if (n) { const k = document.createElement('span'); k.className = 'key'; k.textContent = String(n); b.append(k); }
    b.addEventListener('mousedown', (e) => { if (e.button === 0) api.open(t.id); });
    b.addEventListener('pointerdown', (e) => { if (e.button === 0) dragStart(e, b); });
    b.addEventListener('contextmenu', (e) => { e.preventDefault(); api.command('tab-menu', t.id); });
    b.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') api.open(t.id); });
    return b;
  }

  function render(data) {
    if (!data) return;
    renderStats(data.stats || null);
    renderZoom(data.zoom);
    const key = JSON.stringify([data.tabs, data.active, data.updated, data.updateWhenDone, data.error]);
    if (key === last || (drag && drag.active)) { if (key !== last) pending = data; return; } // nothing changed (or a drag runs): no DOM work
    last = key;
    pending = null;
    const byId = new Map(data.tabs.map((t) => [t.id, t]));
    const chain = new Set(); // the active tab and its ancestors
    for (let t = byId.get(data.active), n = 0; t && n < 50; t = byId.get(t.parent), n++) chain.add(t.id);
    const kids = new Map();
    data.tabs.forEach((t) => { if (t.parent) kids.set(t.parent, (kids.get(t.parent) || 0) + 1); });
    // top level and its children always show; deeper tabs show while their parent (or a sibling) is active
    const visible = (t) => t.depth <= 1 || chain.has(t.parent);
    tabsEl.textContent = '';
    let group = null;
    data.tabs.forEach((t, i) => {
      if (!visible(t)) return;
      if (t.depth >= 2) {
        if (!group) {
          const p = byId.get(t.parent);
          group = document.createElement('div'); group.className = 'group'; group.setAttribute('role', 'group');
          const l = document.createElement('span'); l.className = 'glabel'; l.textContent = (p && (p.childLabel || p.name)) || '';
          group.append(l);
          tabsEl.append(group);
        }
      } else if (group) {
        // (a group followed by more tabs: a line closes it)
        group = null;
        const g = document.createElement('span'); g.className = 'gend'; tabsEl.append(g);
      }
      const b = tabButton(t, i, data.tabs.length, data.active, kids.get(t.id) || 0, chain.has(t.id));
      (group || tabsEl).append(b);
      if (t.depth === 0 && kids.get(t.id)) { const s = document.createElement('span'); s.className = 'sep'; tabsEl.append(s); }
    });
    if (drag) drag.b = tabsEl.querySelector('.tab[data-id="' + CSS.escape(drag.id) + '"]') || drag.b; // (pressed, not moved yet)
    const act = tabsEl.querySelector('.tab.active');
    if (act) act.scrollIntoView({ block: 'nearest', inline: 'nearest' });
    updEl.hidden = data.updated !== true;
    msgEl.textContent = data.updateWhenDone ? 'Quark Project updated — restarts when the installs finish' : 'Quark Project updated — reopen to finish';
    errEl.hidden = !data.error;
    errEl.textContent = data.error || '';
    errEl.title = data.error || '';
    fit();
  }
  let pending = null; // (a tab list that came during a drag: drawn when it ends)

  function renderZoom(z) {
    const pct = Math.round((Number(z) || 1) * 100);
    const hide = pct === 100;
    if (zoomEl.hidden === hide && zoomEl.textContent === pct + ' %') return;
    zoomEl.hidden = hide;
    zoomEl.textContent = pct + ' %';
    fit();
  }

  // ---- the Editor Manager's facts (right side): Testing · Production · Human-made [bar] · Refreshed ----
  const fmt = new Intl.DateTimeFormat('en-US', { timeZone: 'America/Chicago', hour: 'numeric', minute: '2-digit' });
  const fmtLong = new Intl.DateTimeFormat('en-US', { timeZone: 'America/Chicago', dateStyle: 'medium', timeStyle: 'medium' });
  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }
  // one fact: a long and a short label (fit() picks), the value, an optional muted extra
  function fact(cls, long, short, value, extra) {
    const f = el('span', 'f ' + cls);
    f.append(el('span', 'k l', long), el('span', 'k s', short), el('span', 'v' + (value ? '' : ' none'), value || 'none'));
    if (extra) f.append(extra);
    return f;
  }
  function renderStats(st) {
    const key = JSON.stringify(st);
    if (key === lastStats) return;
    lastStats = key;
    statsEl.textContent = '';
    statsEl.hidden = !st;
    if (!st) { fit(); return; }
    const h = st.human || {};
    const at = st.at ? new Date(st.at) : null;
    const when = at && !isNaN(at) ? fmt.format(at) : '';
    statsEl.classList.toggle('stale', !!st.stale);
    statsEl.append(fact('t', 'Testing', 'Test', st.testing.version));
    statsEl.append(fact('p', 'Production', 'Prod', st.production.version));
    const n = h.h + h.t + h.a;
    const hb = el('span', 'hbar');
    if (n > 0) for (const [c, v] of [['h', h.h], ['t', h.t], ['a', h.a]]) { const i = el('i', c); i.style.width = (100 * v / n) + '%'; hb.append(i); }
    statsEl.append(fact('h', 'Human-made', 'Human', h.pct == null ? '' : h.pct + ' %', hb));
    statsEl.append(fact('r', 'Refreshed', '', when, el('span', 'x', st.fetched ? 'with fetch' : 'no fetch')));
    statsEl.title = [
      'Editor Manager' + (st.stale ? ' (not answering: the last facts it gave)' : '') + ' (click to open)',
      'Testing: ' + (st.testing.version || 'none') + (st.testing.sha ? '  ' + st.testing.sha : '  (no origin/testing)'),
      'Production: ' + (st.production.version || 'none') + (st.production.tag ? '  ' + st.production.tag : '') + (st.production.sha ? '  ' + st.production.sha : ''),
      'Human-made: ' + (h.pct == null ? '-' : h.pct + ' %') + (n ? '  (human ' + h.h + ', third-party ' + h.t + ', AI ' + h.a + ' of ' + n + ')' : ''),
      'Last refresh: ' + (at && !isNaN(at) ? fmtLong.format(at) + ' Dallas' : '-') + ', ' + (st.fetched ? 'with git fetch' : 'no fetch'),
    ].join('\n');
    fit();
  }
  // Narrowing: the facts shrink first (the first level at which every tab shows its whole name: 1 short labels,
  // 2 no Refreshed, 3 no Production / bar, 4 hidden), then the tabs shorten, and with the room that frees the facts
  // come back at the fullest level that still fits; then the tabs that still do not fit go behind "+N". Runs only on a
  // render, a resize or a scroll of the strip (a few layouts, no timers).
  const squeezed = () => tabsEl.scrollWidth > tabsEl.clientWidth + 1
    || [...tabsEl.querySelectorAll('.tab .name')].some((n) => n.scrollWidth > n.clientWidth + 1);
  function shortNames(on) {
    for (const n of tabsEl.querySelectorAll('.tab .name[data-short]')) n.textContent = on ? n.dataset.short : n.dataset.long;
  }
  function fitStats() {
    // the fullest facts level (0..3) at which the tabs fit as they are now, else 4 (hidden) -> true when they fit
    if (statsEl.hidden) return !squeezed();
    for (let lv = 0; lv <= 4; lv++) {
      statsEl.dataset.lv = String(lv);
      if (lv === 4 || !squeezed()) break;
    }
    return statsEl.dataset.lv !== '4';
  }
  function fit() {
    shortNames(false);
    if (!fitStats() && squeezed()) {
      shortNames(true); // (then the names drop "Editor" / "Manager" before they are cut,
      fitStats();       //  and the facts take what that frees)
    }
    updateMore();
  }
  let hiddenIds = [];
  function cutTabs() {
    // the tabs not wholly inside the strip's visible part
    const r = tabsEl.getBoundingClientRect();
    return [...tabsEl.querySelectorAll('.tab')].filter((b) => {
      const x = b.getBoundingClientRect();
      return x.right > r.right + 1 || x.left < r.left - 1;
    });
  }
  function updateMore() {
    let cut = tabsEl.scrollWidth > tabsEl.clientWidth + 1 ? cutTabs() : [];
    if (cut.length && moreEl.hidden) { moreEl.hidden = false; cut = cutTabs(); } // (the button takes room: count again)
    moreEl.hidden = !cut.length;
    hiddenIds = cut.map((b) => b.dataset.id);
    moreEl.textContent = '+' + cut.length;
    moreEl.title = cut.length ? 'Not shown: ' + cut.map((b) => b.querySelector('.name').textContent).join(', ') + ' (click to choose; the mouse wheel scrolls the tabs)' : '';
    tabsEl.classList.toggle('cut', tabsEl.scrollLeft + tabsEl.clientWidth < tabsEl.scrollWidth - 1);
  }
  let fitQueued = false;
  addEventListener('resize', () => {
    if (fitQueued) return;
    fitQueued = true;
    requestAnimationFrame(() => { fitQueued = false; fit(); });
  });
  statsEl.addEventListener('click', () => api.open('dashboard'));
  moreEl.addEventListener('click', () => api.command('more-menu', hiddenIds));
  zoomEl.addEventListener('click', () => api.command('zoom-reset'));
  let scrollQueued = false;
  tabsEl.addEventListener('scroll', () => {
    if (scrollQueued) return;
    scrollQueued = true;
    requestAnimationFrame(() => { scrollQueued = false; updateMore(); });
  }, { passive: true });

  // ---- drag a tab among its siblings: the new order goes to main.js, which writes tabs.json ----
  let drag = null;
  function dragStart(e, b) {
    drag = { b, id: b.dataset.id, parent: b.dataset.parent, x0: e.clientX, pointer: e.pointerId, active: false, sibs: [], to: -1 };
  }
  function dragEnd(commit) {
    const d = drag;
    drag = null;
    if (!d) return;
    if (d.active) {
      d.b.classList.remove('dragging');
      d.b.style.transform = '';
      d.sibs.forEach((x) => x.el.classList.remove('drop-before', 'drop-after'));
      try { d.b.releasePointerCapture(d.pointer); } catch { /* already released */ }
      if (commit && d.to >= 0) {
        const others = d.sibs.filter((x) => x.el !== d.b);
        const before = d.to < others.length ? others[d.to].el.dataset.id : null;
        const i = d.sibs.findIndex((x) => x.el === d.b);
        const nowBefore = d.sibs[i + 1] ? d.sibs[i + 1].el.dataset.id : null;
        if (before !== nowBefore) api.command('reorder', d.id, before);
      }
    }
    if (pending) render(pending);
  }
  addEventListener('pointermove', (e) => {
    const d = drag;
    if (!d || e.pointerId !== d.pointer) return;
    const dx = e.clientX - d.x0;
    if (!d.active) {
      if (Math.abs(dx) < 6) return;
      // (siblings: tabs with the same parent; their places are taken once, before anything moves)
      d.sibs = [...tabsEl.querySelectorAll('.tab')].filter((x) => x.dataset.parent === d.parent)
        .map((el) => { const r = el.getBoundingClientRect(); return { el, mid: r.left + r.width / 2 }; });
      if (d.sibs.length < 2) { drag = null; return; }
      d.active = true;
      d.b.classList.add('dragging');
      try { d.b.setPointerCapture(d.pointer); } catch { /* not down any more */ }
    }
    d.b.style.transform = 'translateX(' + dx + 'px)';
    const others = d.sibs.filter((x) => x.el !== d.b);
    d.to = others.filter((x) => x.mid < e.clientX).length;
    others.forEach((x, i) => {
      x.el.classList.toggle('drop-before', i === d.to);
      x.el.classList.toggle('drop-after', d.to === others.length && i === others.length - 1);
    });
  });
  addEventListener('pointerup', (e) => { if (drag && e.pointerId === drag.pointer) dragEnd(true); });
  addEventListener('pointercancel', () => dragEnd(false));
  addEventListener('keydown', (e) => { if (e.key === 'Escape' && drag) { e.preventDefault(); dragEnd(false); } });

  // ---- find in page ----
  let findTimer = 0;
  function showFind(d) {
    if (!d || !d.open) {
      findEl.hidden = true;
      document.body.classList.remove('finding');
      findN.textContent = '';
      fit();
      return;
    }
    const was = !findEl.hidden;
    findEl.hidden = false;
    document.body.classList.add('finding');
    if (typeof d.text === 'string' && d.text && d.text !== findQ.value) { findQ.value = d.text; api.command('find', { text: d.text }); }
    else if (!was && findQ.value) api.command('find', { text: findQ.value });
    findQ.focus();
    findQ.select();
    fit();
  }
  findQ.addEventListener('input', () => {
    // (a short pause while typing: one search per burst of keys, not one per key)
    clearTimeout(findTimer);
    findTimer = setTimeout(() => api.command('find', { text: findQ.value }), 60);
  });
  findQ.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); clearTimeout(findTimer); api.command('find', { text: findQ.value, forward: !e.shiftKey, again: true }); }
    else if (e.key === 'Escape') { e.preventDefault(); api.command('find', { close: true }); }
  });
  document.getElementById('find-next').addEventListener('click', () => api.command('find', { text: findQ.value, forward: true, again: true }));
  document.getElementById('find-prev').addEventListener('click', () => api.command('find', { text: findQ.value, forward: false, again: true }));
  document.getElementById('find-close').addEventListener('click', () => api.command('find', { close: true }));
  api.onFind(showFind);
  api.onFindResult((r) => {
    const n = Number(r && r.matches) || 0;
    findN.textContent = n ? (Number(r.active) || 0) + ' / ' + n : (findQ.value ? 'no match' : '');
    findN.classList.toggle('none', !n && !!findQ.value);
  });

  document.getElementById('relaunch').addEventListener('click', () => api.command('relaunch'));
  document.getElementById('reload').addEventListener('click', (e) => api.command(e.shiftKey ? 'reload-tabs' : 'reload'));
  tabsEl.addEventListener('wheel', (e) => { if (e.deltaY) { tabsEl.scrollLeft += e.deltaY; e.preventDefault(); } }, { passive: false });
  // hold Ctrl to see the Ctrl+N numbers (main.js sees Ctrl in every tab and tells this view)
  api.onCtrl((down) => document.body.classList.toggle('keys', down));

  // Dallas time, redrawn once a minute (aligned to the minute: no per-second wakeups)
  const timeEl = document.getElementById('time');
  function tick() {
    timeEl.textContent = fmt.format(new Date());
    setTimeout(tick, 60000 - (Date.now() % 60000) + 20);
  }
  tick();

  api.onTabs(render);
  api.tabs().then(render);
  api.command('ready');
})();
