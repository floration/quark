'use strict';
// Tab bar view: renders the tab list main.js sends, forwards clicks, shows the Dallas time.
(() => {
  const api = window.quarkTabbar;
  const tabsEl = document.getElementById('tabs');
  const errEl = document.getElementById('err');
  const updEl = document.getElementById('update');
  const label = { up: 'running', down: 'not running', starting: 'starting', restarting: 'stopped, restarting by itself shortly', missing: 'not installed', unknown: 'checking' };
  let last = '';

  // Ctrl+1..8 = tabs 1..8, Ctrl+9 = the last tab (as in main.js)
  const shortcut = (i, count) => (i < 8 ? i + 1 : i === count - 1 ? 9 : 0);

  function tabButton(t, i, count, active, kids, open) {
    const b = document.createElement('button');
    b.className = 'tab' + (t.id === active ? ' active' : '') + (t.depth >= 2 ? ' child' : '');
    b.dataset.status = t.status;
    b.setAttribute('role', 'tab');
    b.setAttribute('aria-selected', t.id === active ? 'true' : 'false');
    const what = t.kind === 'file' && t.status === 'up' ? 'local page' : (label[t.status] || t.status);
    const n = shortcut(i, count);
    b.title = t.name + ': ' + what + (t.missing ? ' (' + t.missing + ')' : '') + (n ? '  [Ctrl+' + n + ']' : '');
    const dot = document.createElement('span'); dot.className = 'dot';
    const name = document.createElement('span'); name.className = 'name'; name.textContent = t.name;
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
    if (kids && t.depth >= 1) {
      // a parent below the top level (Editor Manager): a caret that shows whether its child tabs are open
      const c = document.createElement('span'); c.className = 'caret' + (open ? ' open' : ''); c.textContent = open ? '' : String(kids);
      b.append(c);
    }
    if (n) { const k = document.createElement('span'); k.className = 'key'; k.textContent = String(n); b.append(k); }
    b.addEventListener('mousedown', (e) => { if (e.button === 0) api.open(t.id); });
    b.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') api.open(t.id); });
    return b;
  }

  function render(data) {
    if (!data) return;
    const key = JSON.stringify(data);
    if (key === last) return; // nothing changed: no DOM work
    last = key;
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
      } else group = null;
      const b = tabButton(t, i, data.tabs.length, data.active, kids.get(t.id) || 0, chain.has(t.id));
      (group || tabsEl).append(b);
      if (t.depth === 0 && kids.get(t.id)) { const s = document.createElement('span'); s.className = 'sep'; tabsEl.append(s); }
    });
    const act = tabsEl.querySelector('.tab.active');
    if (act) act.scrollIntoView({ block: 'nearest', inline: 'nearest' });
    updEl.hidden = data.updated !== true;
    errEl.hidden = !data.error;
    errEl.textContent = data.error || '';
  }

  document.getElementById('relaunch').addEventListener('click', () => api.command('relaunch'));
  document.getElementById('reload').addEventListener('click', (e) => api.command(e.shiftKey ? 'reload-tabs' : 'reload'));
  tabsEl.addEventListener('wheel', (e) => { if (e.deltaY) { tabsEl.scrollLeft += e.deltaY; e.preventDefault(); } }, { passive: false });
  // hold Ctrl to see the Ctrl+N numbers
  addEventListener('keydown', (e) => { if (e.key === 'Control') document.body.classList.add('keys'); });
  addEventListener('keyup', (e) => { if (e.key === 'Control') document.body.classList.remove('keys'); });
  addEventListener('blur', () => document.body.classList.remove('keys'));

  // Dallas time, redrawn once a minute (aligned to the minute: no per-second wakeups)
  const fmt = new Intl.DateTimeFormat('en-US', { timeZone: 'America/Chicago', hour: 'numeric', minute: '2-digit' });
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
