/* Change log tab: every commit of the docs, grouped by Dallas day, with entry-level items in sentences. */
'use strict';
(function (DM) {
  const {h, clear, $} = DM;
  const st = DM.state;
  const DOCS_REPO = 'https://github.com/floration/quark-claude-docs/commit/';
  const ADDED = {'01': 'New bug', '02': 'Work started', '03': 'New possible bug', '04': 'New performance fix', '05': 'New performance idea',
    '06': 'News for q', '07': 'New question for q', '08': 'New note', '00': 'Rules changed', 'RE': 'README changed'};
  const KIND_RANK = {status: 0, added: 1, removed: 2, moved: 3, edited: 4};
  const rankOf = it => it.halting === true ? -2 : it.halting === false ? -1 : (KIND_RANK[it.kind] ?? 9);

  function titleLink(it) {
    const e = it.entryId && st.entryById[it.entryId];
    const raw = String(it.title || (e && e.title) || 'an entry').replace(/[.:,;]+$/, '') || '(untitled)';
    const t = DM.md.into(h('span', {class: 'it-t'}), raw, true);
    if (!e) return t;
    return h('a', {href: '#entry=' + encodeURIComponent(e.id), onclick: ev => { ev.preventDefault(); DM.app.openEntry(e.id); }}, t);
  }
  function stWord(s) { return h('span', {class: 'stw s-' + String(s || '').replace(/[^a-z-]/g, '')}, DM.statusWord(s)); }

  // One change item as a sentence. inDialog: no file name (the dialog shows it).
  function sentence(it, short) {
    const f = String(it.file || '').slice(0, 2);
    const wrap = h('span', {class: 'sent', title: it.summary || null});
    const halt = it.halting === true || it.halting === false
      ? h('span', {class: it.halting ? 'lead halt-lead' : 'lead'}, it.halting ? 'Now halting a Claude agent' : 'No longer halting') : null;
    if (short === 'entry') { // inside the entry's own dialog: no title
      if (halt) wrap.append(halt, it.kind === 'status' ? [' (', it.from ? stWord(it.from) : '', it.from ? ' \u2192 ' : 'now ', stWord(it.to), ')'] :
        it.kind === 'added' ? ' (written' + (it.file ? ' in ' + DM.fileTitle(it.file) : '') + ')' : it.kind === 'removed' ? ' (removed)' : '');
      else if (it.kind === 'status') wrap.append(it.from ? stWord(it.from) : '', it.from ? ' \u2192 ' : 'now ', stWord(it.to));
      else if (it.kind === 'added') wrap.append('Written', it.file ? ' in ' + DM.fileTitle(it.file) : '');
      else if (it.kind === 'moved') wrap.append('Moved (' + movePlace(it.from) + ' \u2192 ' + movePlace(it.to) + ')');
      else wrap.append({edited: 'Edited', removed: 'Removed'}[it.kind] || it.summary || it.kind || 'Changed');
      return wrap;
    }
    if (halt) {
      wrap.append(halt, ': ', titleLink(it));
      if (it.kind === 'status') wrap.append(' (', it.from ? stWord(it.from) : '', it.from ? ' → ' : 'now ', stWord(it.to), ')');
      else if (it.kind === 'added') wrap.append(h('span', {class: 'faint'}, ' (' + (ADDED[f] || 'new entry').toLowerCase() + ')'));
      else if (it.kind === 'removed') wrap.append(h('span', {class: 'faint'}, ' (removed)'));
      else if (it.kind === 'moved') wrap.append(h('span', {class: 'faint'}, ' (moved to ' + movePlace(it.to) + ')'));
    } else switch (it.kind) {
      case 'added': wrap.append(h('span', {class: 'lead'}, (ADDED[f] || 'New entry') + ': '), titleLink(it)); break;
      case 'status': wrap.append(titleLink(it), ': ', it.from ? stWord(it.from) : '', it.from ? ' → ' : 'now ', stWord(it.to)); break;
      case 'removed': wrap.append(h('span', {class: 'lead'}, 'Removed: '), titleLink(it)); break;
      case 'moved': wrap.append(h('span', {class: 'lead'}, 'Moved: '), titleLink(it), it.from || it.to ? h('span', {class: 'faint'}, ' (' + movePlace(it.from) + ' → ' + movePlace(it.to) + ')') : ''); break;
      case 'edited': wrap.append(h('span', {class: 'lead'}, 'Edited: '), titleLink(it)); break;
      default: wrap.append(it.summary || titleLink(it));
    }
    if (!short && it.file) wrap.append(h('span', {class: 'faint'}, ' · ' + DM.fileShort(it.file)));
    return wrap;
  }
  function movePlace(p) { return !p ? '?' : /\.md$/.test(p) ? DM.fileTitle(p) : p; }

  function commitBlock(c, full, maxItems) {
    const items = c.items.slice().sort((a, b) => rankOf(a) - rankOf(b));
    const shown = maxItems ? items.slice(0, maxItems) : items;
    const head = h('div', {class: 'cm-h'},
      full ? null : DM.timeEl(c.time, 'smart'),
      DM.md.into(h('span', {class: 'cm-s'}), c.subject || '(no subject)', true),
      c.thread && (full || !String(c.subject || '').toLowerCase().startsWith(c.thread.toLowerCase())) ? DM.data.threadLink(c.thread) : null,
      full && c.author ? h('span', {class: 'faint'}, c.author) : null,
      full && c.sha ? DM.ext(DM.safeUrl(c.url) || DOCS_REPO + c.sha, String(c.sha).slice(0, 7), 'sha', 'This change on GitHub (Claude Documents repo)') : null);
    const rest = [head];
    if (shown.length) rest.push(h('ul', {class: 'cm-items'}, shown.map(it => h('li', {class: 'k-' + (it.halting === true ? 'halt' : it.kind || 'edited')}, h('i', {class: 'kd'}), sentence(it)))));
    if (items.length > shown.length) rest.push(h('div', {class: 'faint more-items'}, '+ ' + (items.length - shown.length) + ' more in this change'));
    const cl = full && Array.isArray(c.links) ? c.links.filter(l => l && l.kind !== 'thread') : [];
    if (cl.length) rest.push(h('div', {class: 'chips'}, cl.map(DM.chip)));
    if (full && !c.items.length && c.files.length) rest.push(h('div', {class: 'faint'}, 'Files: ', c.files.map(DM.fileTitle).join(', ')));
    if (!full) return h('div', {class: 'commit'}, rest);
    return h('div', {class: 'commit full'}, DM.timeEl(c.time, 'clock'), h('div', {class: 'cm-r'}, rest));
  }

  function matches(c, f) {
    if (f.thread && (c.thread || '') !== f.thread && !c.items.some(it => { const e = st.entryById[it.entryId]; return e && e.thread === f.thread; })) return false;
    if (f.file && !c.files.includes(f.file) && !c.items.some(it => it.file === f.file)) return false;
    if (f.kind && !c.items.some(it => it.kind === f.kind)) return false;
    if (f.q) {
      const hay = (c.subject + ' ' + c.thread + ' ' + c.author + ' ' + c.sha + ' ' + c.items.map(it => it.title + ' ' + (it.summary || '') + ' ' + (it.to || '')).join(' ')).toLowerCase();
      if (!f.q.toLowerCase().split(/\s+/).filter(Boolean).every(w => hay.includes(w))) return false;
    }
    return true;
  }

  let limit = 120;
  function fill(sel, opts, v) { clear(sel); for (const [k, l] of opts) sel.appendChild(h('option', {value: k}, l)); sel.value = opts.some(o => o[0] === v) ? v : ''; }
  function render() {
    const f = st.prefs.changes, d = st.data;
    const threads = [...new Set(d.changes.map(c => c.thread).filter(Boolean).concat(d.threads.map(t => t.title)))].sort((a, b) => a.localeCompare(b));
    fill($('#cThread'), [['', 'Any thread']].concat(threads.map(t => [t, t])), f.thread);
    fill($('#cFile'), [['', 'Any file']].concat(d.files.map(x => [x.name, DM.fileTitle(x.name)])), f.file);
    $('#cKind').value = f.kind;
    if (document.activeElement !== $('#cQ')) $('#cQ').value = f.q;
    const list = d.changes.filter(c => matches(c, f));
    $('#cCount').textContent = list.length + ' of ' + d.changes.length + ' changes';
    const body = clear($('#cBody'));
    if (!list.length) { body.appendChild(h('div', {class: 'empty'}, 'No changes match.')); return; }
    let day = null, box = null;
    const frag = document.createDocumentFragment();
    for (const c of list.slice(0, limit)) {
      const k = DM.dayKey(c.time);
      if (k !== day) {
        day = k;
        const n = list.filter(x => DM.dayKey(x.time) === k).length;
        frag.appendChild(h('h3', {class: 'day'}, DM.dayHeader(k), h('span', {class: 'faint'}, ' · ' + n + (n === 1 ? ' change' : ' changes'))));
        box = h('div', {class: 'day-box'});
        frag.appendChild(box);
      }
      box.appendChild(commitBlock(c, true));
    }
    body.appendChild(frag);
    if (list.length > limit) body.appendChild(h('button', {class: 'more-btn', onclick: () => { limit += 200; render(); }}, 'Show older changes (' + (list.length - limit) + ' more)'));
  }

  let qT = null;
  $('#cQ').addEventListener('input', () => { clearTimeout(qT); qT = setTimeout(() => { st.prefs.changes.q = $('#cQ').value; DM.app.save(); render(); }, 120); });
  for (const [id, key] of [['#cThread', 'thread'], ['#cFile', 'file'], ['#cKind', 'kind']])
    $(id).addEventListener('change', () => { st.prefs.changes[key] = $(id).value; DM.app.save(); render(); $('#cScroll').scrollTop = 0; });

  (DM.views = DM.views || {}).changes = {render};
  DM.changes = {sentence, commitBlock};
})(window.DM);
