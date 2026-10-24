/* Data: fetch /api/data (or ?data=<fixture>), poll every 30 s with If-None-Match, derive lookups, banners. */
'use strict';
(function (DM) {
  const {h, clear, $} = DM;
  const POLL_MS = 30000;
  const st = DM.state = DM.state || {};
  let etag = null, lastText = null, timer = null, busy = false;

  // ?data=<path> loads a fixture from the same origin instead of /api/data (for testing with python -m http.server).
  function source() {
    const p = new URLSearchParams(location.search).get('data');
    if (p) {
      try {
        const u = new URL(p, location.href);
        if (u.origin === location.origin) return u.pathname + u.search;
      } catch (_) { /* fall through */ }
      console.warn('ignoring ?data= that is not on this server:', p);
    }
    return '/api/data';
  }
  st.source = source();

  async function load() {
    if (busy) return;
    busy = true;
    st.lastTry = new Date();
    try {
      const headers = {};
      if (etag) headers['If-None-Match'] = etag;
      const r = await fetch(st.source, {headers, cache: 'no-store'});
      if (r.status === 304) { ok(); return; }
      if (!r.ok) throw new Error('the server answered ' + r.status + ' ' + r.statusText);
      const txt = await r.text();
      etag = r.headers.get('ETag');
      if (txt === lastText) { ok(); return; }
      const d = JSON.parse(txt);
      lastText = txt;
      setData(d);
      ok();
    } catch (e) {
      st.offline = String(e && e.message || e);
      banners();
      DM.app && DM.app.status();
    } finally {
      busy = false;
      schedule();
    }
  }
  function ok() { st.offline = null; st.lastOk = new Date(); banners(); DM.app && DM.app.status(); }
  function schedule() { clearTimeout(timer); timer = setTimeout(load, POLL_MS); }

  // Normalize + index the payload so views don't have to guard every field.
  function setData(d) {
    const arr = v => Array.isArray(v) ? v : [];
    d.files = arr(d.files); d.entries = arr(d.entries); d.changes = arr(d.changes); d.threads = arr(d.threads);
    d.github = d.github || {}; d.github.prs = arr(d.github.prs); d.github.runs = arr(d.github.runs); d.github.releases = arr(d.github.releases);
    for (const e of d.entries) {
      e.waitingOn = arr(e.waitingOn); e.links = arr(e.links).filter(l => l && typeof l === 'object');
      e.status = e.status || 'unknown'; e.title = e.title == null ? '(untitled)' : String(e.title); e.body = e.body == null ? '' : String(e.body);
      e.thread = e.thread || '';
    }
    for (const c of d.changes) { c.items = arr(c.items); c.files = arr(c.files); }
    st.data = d;
    st.fileByName = Object.fromEntries(d.files.map(f => [f.name, f]));
    st.entryById = Object.fromEntries(d.entries.map(e => [e.id, e]));
    // thread lookups (title, case-insensitive)
    st.threadByKey = {};
    for (const t of d.threads) if (t && t.title) st.threadByKey[t.title.toLowerCase()] = t;
    for (const e of d.entries) if (e.thread && !st.threadByKey[e.thread.toLowerCase()]) st.threadByKey[e.thread.toLowerCase()] = {title: e.thread, url: e.threadUrl, entries: 0, lastActivity: null};
    // change items per entry (newest first, because changes are newest first)
    st.historyByEntry = {};
    for (const c of d.changes) for (const it of c.items) {
      if (!it.entryId) continue;
      (st.historyByEntry[it.entryId] = st.historyByEntry[it.entryId] || []).push({c, it});
    }
    st.version = (st.version || 0) + 1;
    DM.app && DM.app.dataChanged();
  }

  function threadUrl(name) {
    if (!name) return null;
    const t = st.threadByKey && st.threadByKey[String(name).toLowerCase()];
    return t && t.url || null;
  }
  // A thread name as a link to its Claude thread (new tab), or plain text.
  function threadLink(name, url) {
    if (!name) return DM.h('span', {class: 'faint'}, 'no thread');
    return DM.ext(url || threadUrl(name), name, 'thread', 'Open the thread "' + name + '" on claude.ai');
  }

  function banners() {
    const box = $('#banners');
    if (!box) return;
    clear(box);
    const d = st.data;
    if (st.offline) {
      box.appendChild(h('div', {class: 'banner b-err', role: 'status'},
        h('b', null, 'Offline. '),
        st.data ? 'Can’t reach the docs manager server right now, so this page shows what it last loaded' + (st.lastOk ? ' (at ' + DM.hhmm(st.lastOk) + ')' : '') + '. ' :
          'Can’t load the data. ',
        st.source === '/api/data' ? 'Is serve.py still running? (Start it again with docs-manager.cmd.) ' : 'Could not read ' + st.source + '. ',
        'Trying again every 30 s. ', h('span', {class: 'faint'}, '(' + st.offline + ')'),
        h('button', {class: 'mini', onclick: () => load()}, 'Retry now')));
    }
    if (d && d.fetchOk === false) {
      box.appendChild(h('div', {class: 'banner b-warn', role: 'status'},
        h('b', null, 'Claude Documents may be out of date. '),
        'The manager couldn’t download the latest version from GitHub, so it shows its own copy',
        d.mirrorFetchedAt ? [' from ', DM.timeEl(d.mirrorFetchedAt, 'dayclock'), ' (', DM.timeEl(d.mirrorFetchedAt), ')'] : '',
        '. New entries written since then are missing. Usually this means no internet or git isn’t signed in; it fixes itself when GitHub is reachable again.'));
    }
    if (d && d.ghOk === false) {
      box.appendChild(h('div', {class: 'banner b-warn', role: 'status'},
        h('b', null, 'GitHub details unavailable. '),
        'PR, CI and release states come from the gh tool, and it failed or isn’t signed in',
        d.ghFetchedAt ? [' (last good data ', DM.timeEl(d.ghFetchedAt), ')'] : '',
        '. Entries are complete, but link colours, "waiting for CI" and PR states may be missing or old. Fix: run ', h('code', null, 'gh auth login'), ' on the PC.'));
    }
    const unsynced = d ? d.files.filter(f => f.pcUnsynced) : [];
    if (unsynced.length) {
      box.appendChild(h('div', {class: 'banner b-info'},
        h('b', null, 'Edited on the PC, not synced yet: '),
        unsynced.map((f, i) => [i ? ', ' : '', h('a', {href: '#tab=files&file=' + encodeURIComponent(f.name), onclick: ev => { ev.preventDefault(); DM.app.openFile(f.name); }}, DM.fileTitle(f.name))]),
        '. Those changes are only on this PC until ', h('code', null, 'sync.cmd'), ' pushes them; other Claudes can’t see them yet.'));
    }
  }

  document.addEventListener('visibilitychange', () => { if (!document.hidden && (!st.lastTry || Date.now() - st.lastTry > 5000)) load(); });

  DM.data = {load, threadUrl, threadLink, banners, setData};
})(window.DM);
