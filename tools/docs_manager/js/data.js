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

  // HALT items: a red banner above every other one, on every tab. Rebuilt only when the data changes (not on every
  // 30 s poll) and never while q is typing in it, so an answer box keeps its text and focus.
  let haltVer = -1;
  function haltingList() {
    const t = e => Date.parse(e.haltingSince || e.date || '') || 0;
    return st.data ? st.data.entries.filter(DM.isHalting).sort((a, b) => t(b) - t(a)) : [];
  }
  function paintHalt(box) {
    const key = (st.version || 0) + '|' + (DM.edit && DM.edit.canEdit() ? 1 : 0) + '|' + (st.prefs && st.prefs.haltOpen ? 1 : 0);
    if (key === haltVer && box.childElementCount) return;
    if (box.contains(document.activeElement) && box.childElementCount) return;
    haltVer = key;
    clear(box);
    const list = haltingList();
    box.hidden = !list.length;
    if (!list.length) return;
    const open = !st.prefs || st.prefs.haltOpen !== false;
    const n = list.length;
    const open1 = id => ev => { ev.preventDefault(); DM.app.openEntry(id); };
    box.appendChild(h('div', {class: 'banner b-halt', role: 'alert'},
      h('div', {class: 'halt-h'},
        h('span', {class: 'halt-pill'}, 'HALT'),
        h('b', null, n + (n === 1 ? ' Claude agent is' : ' Claude agents are') + ' stopped until you act'),
        h('span', {class: 'halt-sub'}, 'nothing moves on ' + (n === 1 ? 'it' : 'these') + ' until you do the step below'),
        h('span', {class: 'spacer'}),
        h('button', {class: 'mini', 'aria-expanded': String(open), onclick: () => { st.prefs.haltOpen = !open; DM.app.save(); paintHalt(box); }}, open ? 'Hide list' : 'Show list')),
      open ? h('ol', {class: 'halt-list'}, list.map(e => h('li', {class: 'halt-it'},
        h('div', {class: 'halt-it-h'},
          h('a', {class: 'halt-t', href: '#entry=' + encodeURIComponent(e.id), onclick: open1(e.id)}, DM.md.into(h('span'), DM.trimTitle(e.title), true)),
          h('span', {class: 'dotsep'}), threadLink(e.thread, e.threadUrl),
          h('span', {class: 'dotsep'}), h('span', {class: 'faint'}, DM.fileShort(e.file)),
          h('span', {class: 'spacer'}),
          h('span', {class: 'faint halt-age'}, e.firstSeen && e.firstSeen.time ? ['written ', DM.timeEl(e.firstSeen.time)] : ['written ', DM.dateOnly(e.date)],
            e.haltingSince && e.firstSeen && e.haltingSince !== e.firstSeen.timeUtc ? [' · halting ', DM.entries.haltTime(e)] : null)),
        h('div', {class: 'halt-what'}, h('span', {class: 'why-k'}, 'What to do: '), DM.md.into(h('span'), e.haltWhat || DM.sentences(e.body)[0] || DM.trimTitle(e.title), true)),
        DM.edit && (DM.edit.canAnswer(e) || DM.edit.editButton(e)) ? h('div', {class: 'halt-act'},
          DM.edit.editButton(e), DM.edit.canAnswer(e) ? DM.edit.answerBox(e) : null) : null))) : null));
  }

  function banners() {
    const root = $('#banners');
    if (!root) return;
    let hb = $('#bHalt'), box = $('#bOther');
    if (!hb) { clear(root); hb = h('div', {id: 'bHalt'}); box = h('div', {id: 'bOther'}); root.append(hb, box); }
    if (st.data) paintHalt(hb);
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
        '. Other Claudes can’t see those changes yet. ', unsyncedHow()));
    }
  }

  // Saves made from this page that couldn't be pushed go up by themselves (every 5 min) or with Send now; edits
  // made in a text editor on the PC go up with sync.cmd (or by saving the file from the Edit tab).
  function unsyncedHow() {
    const es = DM.edit && DM.edit.canEdit() ? DM.edit.editStatus() : null;
    if (es && Date.now() - es.at > 20000) { DM.edit.refreshStatus(); }
    const n = es && es.status && es.status.unpushed || 0;
    if (n) return [n + (n === 1 ? ' save from this page is' : ' saves from this page are') + ' waiting to go up: the manager tries again every 5 minutes. ',
      h('button', {class: 'mini', onclick: () => DM.edit.pushNow()}, 'Send now')];
    return ['They go up when ', h('code', null, 'sync.cmd'), ' runs', DM.edit && DM.edit.canEdit() ? ', or when you save the file from the Edit tab' : '', '.'];
  }

  document.addEventListener('visibilitychange', () => { if (!document.hidden && (!st.lastTry || Date.now() - st.lastTry > 5000)) load(); });

  DM.data = {load, threadUrl, threadLink, banners, setData, haltingList};
})(window.DM);
