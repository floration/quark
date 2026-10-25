/* ===== UI: files ======================================================================================
   Drag and drop (onto a cell, or anywhere: matched by file name), the upload button, the project file, export. */
(function () {
  'use strict';
  const Q = window.Q, U = Q.ui, h = U.h, fmt = U.fmt;
  const F = U.files = {};
  const AUDIO = /\.(wav|ogg|mp3|flac|oga|opus|m4a|aac|aiff?|webm)$/i;

  // --- name matching: "ROCKET.wav", "heavy_metal/ROCKET.ogg", "male_GO.wav", "sounds/magical/hit-med.flac" -------------
  // quips: "male_quips_HEADSHOT.wav", "voices/male/quips/HEADSHOT.wav", "quips/HEADSHOT.wav" (a quips folder or prefix)
  const norm = s => s.toUpperCase().replace(/[\s\-.]+/g, '_').replace(/[^A-Z0-9_]/g, '');
  const QUIP_TOK = /^QUIPS?$/, QUIP_PRE = /^QUIPS?_/;
  function allNames(kind) {
    const base = kind === 'voice' ? Q.LINES.concat(Q.QUIPS) : Q.SOUNDS;
    const extra = Q.MISSING.filter(m => m.name && (m.kind || 'sound') === kind).map(m => m.name).concat(Q.state.customs.filter(c => c.kind === kind).map(c => c.name));
    return base.concat(extra.filter(n => !base.includes(n)));
  }
  F.allNames = allNames;
  F.match = function (path, fallback) {
    const segs = path.replace(/\\/g, '/').split('/').filter(Boolean), file = segs.pop() || '', stemRaw = file.replace(/\.[^.]+$/, '');
    let stem = norm(stemRaw), kind = null, pack = null;
    const packs = [...Q.KITS.map(p => ({ kind: 'sound', dir: p.dir })), ...Q.VOICES.map(p => ({ kind: 'voice', dir: p.dir }))].sort((a, b) => b.dir.length - a.dir.length);
    const dirTok = d => norm(d);
    for (const s of segs.slice().reverse()) {   // the nearest folder naming a pack
      const n = norm(s);
      const p = packs.find(x => dirTok(x.dir) === n || (x.dir === 'mouth' && n === 'WOODEN'));
      if (p) { kind = p.kind; pack = p.dir; break; }
    }
    if (!kind && segs.some(s => /^voices?$/i.test(s))) kind = 'voice';
    if (!kind && segs.some(s => /^(sounds?|sfx)$/i.test(s))) kind = 'sound';
    if (!pack) {   // a prefix on the file name: male_GO, heavy_metal_ROCKET
      for (const x of packs) { const t = dirTok(x.dir) + '_'; if (stem.startsWith(t)) { kind = x.kind; pack = x.dir; stem = stem.slice(t.length); break; } }
      if (!pack && stem.startsWith('WOODEN_')) { kind = 'sound'; pack = 'mouth'; stem = stem.slice(7); }
    }
    // a quip (never the announcer line of the same name): a quips folder, or quips_ in front of the name
    let quip = kind !== 'sound' && segs.some(s => QUIP_TOK.test(norm(s)));
    if (kind !== 'sound' && QUIP_PRE.test(stem)) { quip = true; stem = stem.replace(QUIP_PRE, ''); }
    if (quip) kind = 'voice';
    // the sound / line name: exact, else the longest known name the stem starts with (ROCKET_V2 -> ROCKET)
    const find = k => {
      const names = quip ? Q.QUIPS.map(n => n.slice(6)) : allNames(k).filter(n => !n.startsWith('quips/'));
      const r = names.includes(stem) ? stem : names.filter(n => stem.startsWith(n + '_') || stem === n).sort((a, b) => b.length - a.length)[0] || names.filter(n => stem.includes(n)).sort((a, b) => b.length - a.length)[0] || null;
      return r && quip ? 'quips/' + r : r;
    };
    let name = null;
    if (kind) name = find(kind);
    else {
      const s = find('sound'), v = find('voice');
      // a name that is both: the existing one wins (HEADSHOT is a voice line; CHECKPOINT a sound)
      if (s && Q.SOUNDS.includes(s)) { kind = 'sound'; name = s; }
      else if (v && Q.LINES.includes(v)) { kind = 'voice'; name = v; }
      else if (s) { kind = 'sound'; name = s; } else if (v) { kind = 'voice'; name = v; }
    }
    if (!pack && kind) pack = fallback && fallback.kind === kind ? fallback.pack : (kind === 'voice' ? 'male' : 'default');
    if (!name && fallback && (!quip || (fallback.name || '').startsWith('quips/'))) return { kind: fallback.kind, pack: fallback.pack, name: fallback.name, guessed: true };
    return name ? { kind, pack, name, guessed: false } : null;
  };

  // --- collecting dropped files (folders too) ---------------------------------------------------------------------------
  async function entriesToFiles(entry, out) {
    if (entry.isFile) { const f = await new Promise((res, rej) => entry.file(res, rej)); out.push({ file: f, path: entry.fullPath.replace(/^\//, '') }); return; }
    if (entry.isDirectory) {
      const reader = entry.createReader();
      for (;;) { const batch = await new Promise((res, rej) => reader.readEntries(res, rej)); if (!batch.length) break; for (const e of batch) await entriesToFiles(e, out); }
    }
  }
  F.collect = async function (dt) {
    const out = [];
    const items = dt.items ? [...dt.items] : [];
    const entries = items.map(it => it.kind === 'file' && it.webkitGetAsEntry ? it.webkitGetAsEntry() : null);
    if (entries.length && entries.every(Boolean)) { for (const e of entries) await entriesToFiles(e, out); }
    else for (const f of dt.files || []) out.push({ file: f, path: f.webkitRelativePath || f.name });
    return out.filter(x => AUDIO.test(x.file.name));
  };

  async function decodeFile(file) {
    const bytes = new Uint8Array(await file.arrayBuffer());
    const data = await Q.decodeBytes(bytes);
    return { data, bytes };
  }

  // put decoded files into assets: [{key, name, data, bytes}] -> one undo step
  F.apply = function (list) {
    if (!list.length) return;
    Q.commit(list.length === 1 ? 'replace ' + Q.assets[list[0].key].name : 'replace ' + list.length + ' sounds', () => {
      for (const it of list) {
        const id = Q.addSource(it.name, it.data, it.bytes);
        const old = Q.state.edits[it.key];
        const e = Q.newEdit(); e.src = id;
        if (old) { e.chain = old.chain; e.levelSet = old.levelSet; }   // (your processing stays on the new take)
        Q.state.edits[it.key] = e;
      }
    });
    if (list.length === 1) U.select(list[0].key);
    U.toast(list.length === 1 ? Q.assets[list[0].key].name + ' / ' + Q.packName(Q.assets[list[0].key].kind, Q.assets[list[0].key].pack) + ' replaced by ' + list[0].name : list.length + ' sounds replaced');
  };

  // one file onto one asset
  F.assignToKey = async function (key, file) {
    try { const d = await decodeFile(file); F.apply([{ key, name: file.name, data: d.data, bytes: d.bytes }]); }
    catch (e) { U.fail('could not decode ' + file.name, e); }
  };

  // many files: match by name, show a confirm list
  F.autoAssign = async function (found, fallbackKey) {
    if (!found.length) { U.toast('No audio files in that drop (wav, ogg, mp3, flac)'); return; }
    const fb = fallbackKey && Q.assets[fallbackKey] ? Q.parseKey(fallbackKey) : (Q.sel ? Q.parseKey(Q.sel) : null);
    const rows = [];
    for (const f of found) {
      let d = null, err = null;
      try { d = await decodeFile(f.file); } catch (e) { err = 'cannot decode: ' + (e.message || e); }
      const m = F.match(f.path, found.length === 1 ? fb : (fb ? { kind: fb.kind, pack: fb.pack, name: null } : null));
      rows.push({ f, d, err, m: m && m.name ? m : null, guessed: m && m.guessed });
    }
    const packSel = [...Q.KITS.map(p => ['sound', p]), ...Q.VOICES.map(p => ['voice', p])];
    const table = h('table', h('tr', h('th', ''), h('th', 'file'), h('th', 'length'), h('th', 'pack'), h('th', 'sound / line'), h('th', '')));
    const ui = [];
    for (const r of rows) {
      const inc = h('input', { type: 'checkbox', checked: !!(r.m && r.d) });
      const ps = h('select', packSel.map(([k, p]) => h('option', { value: k + ':' + p.dir }, (k === 'voice' ? 'voice ' : 'kit ') + p.name)));
      const ns = h('select');
      const fillNames = () => { const [k] = ps.value.split(':'), cur = ns.value; ns.innerHTML = ''; for (const n of allNames(k)) ns.append(h('option', { value: n }, n)); if (allNames(k).includes(cur)) ns.value = cur; };
      ps.onchange = fillNames;
      if (r.m) { ps.value = r.m.kind + ':' + r.m.pack; fillNames(); ns.value = r.m.name; } else fillNames();
      const exists = () => { const [k, p] = ps.value.split(':'); return Q.state.edits[Q.key(k, p, ns.value)]; };
      const warn = h('span.faint');
      const upd = () => { warn.textContent = exists() ? 'replaces your edit\'s audio' : ''; };
      ps.addEventListener('change', upd); ns.addEventListener('change', upd); upd();
      if (!r.d) { inc.disabled = true; inc.checked = false; }
      table.append(h('tr', h('td', inc), h('td.num', { title: r.f.path }, r.f.path), h('td.num', r.d ? fmt(r.d.data.length / 48000, 2) + ' s' : h('span', { style: { color: 'var(--bad)' } }, r.err)),
        h('td', ps), h('td', ns), h('td', r.m ? (r.guessed ? h('span', { style: { color: 'var(--warn)' } }, 'guessed') : h('span', { style: { color: '#8ee08e' } }, 'matched')) : h('span', { style: { color: 'var(--warn)' } }, 'no match: pick one'), ' ', warn)));
      ui.push({ r, inc, ps, ns });
    }
    const ok = await U.dialog('Assign ' + rows.length + ' file' + (rows.length > 1 ? 's' : ''), [h('p', 'Matched by file name and folder (ROCKET.wav, heavy_metal/ROCKET.ogg, male_GO.wav; a quip: male_quips_HEADSHOT.wav or voices/male/quips/HEADSHOT.wav). A file naming no kit / voice goes to ' + (fb ? 'the selected sound\'s ' + (fb.kind === 'voice' ? 'voice' : 'kit') + ' (' + Q.packName(fb.kind, fb.pack) + ')' : 'DEFAULT (voices: MALE)') + '. Check the targets, untick what to skip. An edit\'s filter chain stays on its new audio.'), table],
      [{ label: 'Cancel', value: false }, { label: 'Assign', primary: true, value: true }]);
    if (!ok) return [];
    const list = [];
    for (const u of ui) {
      if (!u.inc.checked || !u.r.d) continue;
      const [k, p] = u.ps.value.split(':'), key = Q.key(k, p, u.ns.value);
      if (!Q.assets[key]) continue;
      list.push({ key, name: u.r.f.file.name, data: u.r.d.data, bytes: u.r.d.bytes });
    }
    F.apply(list);
    return list;
  };
  F.lastDialogRows = null;

  // --- drop targets ---------------------------------------------------------------------------------------------------
  F.dropTarget = function (el, key) {
    el.addEventListener('dragover', ev => { if ([...(ev.dataTransfer.types || [])].includes('Files')) { ev.preventDefault(); ev.stopPropagation(); ev.dataTransfer.dropEffect = 'copy'; el.classList.add('drop'); hint(false); } });
    el.addEventListener('dragleave', () => el.classList.remove('drop'));
    el.addEventListener('drop', async ev => {
      ev.preventDefault(); ev.stopPropagation(); el.classList.remove('drop'); hint(false);
      const files = await F.collect(ev.dataTransfer);
      if (files.length === 1) F.assignToKey(key, files[0].file);
      else F.autoAssign(files, key);
    });
  };
  let hintT = 0;
  function hint(on) { const d = U.$('#dropHint'); clearTimeout(hintT); if (on) { d.hidden = false; hintT = setTimeout(() => { d.hidden = true; }, 400); } else d.hidden = true; }

  // --- upload buttons -------------------------------------------------------------------------------------------------
  F.pickFor = function (key) {
    const inp = h('input', { type: 'file', accept: '.wav,.ogg,.mp3,.flac,audio/*' });
    inp.onchange = () => { if (inp.files[0]) F.assignToKey(key, inp.files[0]); };
    inp.click();
  };

  // --- origin (provenance.json): who made a dropped file ---------------------------------------------------------------
  const P = Q.prov;
  const ORIGIN_TEXT = { '': 'who made it? (choose)', human: 'human-made (a person made it)', 'third-party': 'third-party (a pack / site, its licence)', ai: 'AI-made' };
  const HINT = { '': 'note (optional)', human: 'who / how (optional), e.g. recorded by q', 'third-party': 'source: pack / site / licence (required)', ai: 'which tool (optional)' };
  // a select + a text box for one dropped file's origin; the text is the source for third-party, a note otherwise
  U.originPicker = function (key, opts) {
    opts = opts || {};
    const sel = h('select', { title: 'Who made this audio. Human only if a person made this sound.' }, Object.keys(ORIGIN_TEXT).map(v => h('option', { value: v }, ORIGIN_TEXT[v])));
    const txt = h('input', { type: 'text' });
    const sync = () => {
      const e = Q.state.edits[key], o = (e && e.origin) || {}, v = o.origin || '';
      sel.value = v; sel.classList.toggle('unset', !v);
      txt.placeholder = HINT[v]; txt.value = (v === 'third-party' ? o.source : o.note) || '';
      txt.classList.toggle('need', v === 'third-party' && !txt.value.trim());
    };
    // the box is the source for third-party, the note otherwise; the other field is kept only while the origin stays
    const set = (origin, text) => {
      const tp = origin === 'third-party';
      Q.commit(origin ? 'origin ' + Q.assets[key].name + ': ' + origin : 'clear origin', () => P.setOrigin(key, origin, tp ? null : text, tp ? text : null));
      sync(); opts.onchange && opts.onchange();
    };
    sel.onchange = () => {
      const e = Q.state.edits[key], o = (e && e.origin) || {};
      const same = o.origin === sel.value;
      set(sel.value, same ? (sel.value === 'third-party' ? o.source : o.note) || '' : '');
    };
    txt.onchange = () => { if (sel.value) set(sel.value, txt.value.trim()); };
    sync();
    return h('span.origin-pick', { 'data-key': key }, sel, txt);
  };

  // where this page is (the sound-editor folder), for the dialogs
  F.pageFolder = function () {
    try {
      if (location.protocol !== 'file:') return null;
      let p = decodeURIComponent(location.pathname).replace(/\/[^/]*$/, '');
      if (/^\/[A-Za-z]:/.test(p)) p = p.slice(1).replace(/\//g, '\\');
      return p;
    } catch (e) { return null; }
  };

  /* After an export: write / update provenance.json in the sound-editor folder. A remembered folder with permission is
     written straight away; otherwise one dialog asks for the folder (or the permission again, or offers the file as a
     download). Resolves to a line for the export report. */
  F.recordProvenance = async function (keys, companions) {
    const entries = P.entries(keys, companions), n = Object.keys(entries).length;
    if (!n) return '';
    const done = r => { const m = 'provenance.json in ' + r.where + ': ' + n + ' entr' + (n === 1 ? 'y' : 'ies') + ' written (' + r.total + ' in all).'; U.toast(m); return m; };
    let dir = await P.getDir();
    if (dir && await P.permission(dir, false) === 'granted') {
      try { const r = await P.writeTo(dir, entries); return done({ where: dir.name, total: r.total }); }
      catch (e) { U.fail('provenance.json', e); }
    }
    const here = F.pageFolder();
    const status = h('p.faint');
    let result = null, pending = null;
    const download = async () => {
      try {
        const obj = await P.offline(entries);
        Q.download(new Blob([P.text(obj)], { type: 'application/json' }), P.FILE);
        Q.lastProvenance = obj;   // (tests read it back)
        result = 'provenance.json downloaded (' + Object.keys(obj.items).length + ' entries): put it in the sound-editor folder, next to sound-editor.html.';
        return true;
      } catch (e) { U.fail('provenance.json', e); return false; }
    };
    const write = async d => {
      try {
        const r = await P.writeTo(d, entries);
        await P.setDir(d);
        result = done({ where: d.name, total: r.total });
        return true;
      } catch (e) { status.textContent = 'Could not write there: ' + (e.message || e); status.style.color = 'var(--bad)'; return false; }
    };
    const anyway = h('button', { type: 'button', hidden: true, onclick: async () => { if (pending && await write(pending)) U.closeDialog2(); } }, 'Write here anyway');
    const btns = [];
    if (dir && P.canPick()) btns.push({ label: 'Allow writing in ' + dir.name, primary: true, onclick: async () => {
      if (await P.permission(dir, true) !== 'granted') { status.textContent = 'Not allowed. Choose the folder again, or download the file.'; return false; }
      return await write(dir) ? undefined : false;
    } });
    if (P.canPick()) btns.push({ label: dir ? 'Choose another folder...' : 'Choose the sound-editor folder...', primary: !dir, onclick: async () => {
      let d;
      try { d = await window.showDirectoryPicker({ id: 'quark-sound-editor', mode: 'readwrite' }); } catch (e) { status.textContent = 'No folder chosen. Choose it, or download the file instead.'; return false; }
      if (!await P.looksRight(d)) {
        pending = d; anyway.hidden = false;
        status.textContent = '"' + d.name + '" has no sound-editor.html: it does not look like the sound-editor folder. Choose again, or write here anyway.';
        status.style.color = 'var(--warn)';
        return false;
      }
      return await write(d) ? undefined : false;
    } });
    btns.push({ node: anyway });
    btns.push({ label: 'Download provenance.json', primary: !P.canPick(), onclick: async () => (await download()) ? undefined : false });
    btns.push({ label: 'Skip', value: null });
    const body = [
      h('p', 'Quark keeps a record of who made every sound (human, third-party or AI), so the game can list what is still AI-made. The editor writes it as ', h('code', 'provenance.json'), ' in its own folder: the one ', h('code', 'sound-editor.html'), ' is in', here ? [': ', h('code', here)] : [' (usually ', h('code', 'Desktop\\Quark\\sound-editor'), ')'], '. Not the overrides folder.'),
      h('p', dir ? 'The editor remembers "' + dir.name + '" but needs your permission again this session.' : (P.canPick() ? 'Choose that folder once: the editor remembers it and updates the file on every export.' : 'This browser cannot write folders: download the file and put it in that folder (it holds every entry this editor has exported, so it can replace the one there).')),
      h('p.faint', n + ' entr' + (n === 1 ? 'y' : 'ies') + ' from this export; entries already in the file are kept, the ones exported now are replaced.'),
      status];
    await U.dialog('Record who made these sounds', body, btns, { second: true });
    return result || 'provenance.json not written (skipped): it records who made each sound, export again to write it.';
  };

  // --- export -----------------------------------------------------------------------------------------------------------
  const short = k => k.split('/').slice(1).join('/');
  const when = iso => { try { return new Date(iso).toLocaleString(); } catch (e) { return iso; } };
  // ask before writing into a folder that has no overrides folder in it (Desktop, Documents...)
  F.confirmGuess = function (res) {
    return U.dialog('No overrides folder there', [h('p', '"' + res.where + '" has no overrides folder (nor assets\\overrides) inside, and is not one itself. Write sounds\\, voices\\ and sound-levels.txt straight into it?'),
      h('p.faint', 'The game reads %APPDATA%\\quark\\overrides (to try them) or the repository\'s assets\\overrides (to ship them).')],
      [{ label: 'Cancel', value: false }, { label: 'Write here anyway', value: true }], { second: true });
  };
  /* Remove from the game: list the sounds in an overrides folder, untick / tick, move the ticked ones to .bak (their
     sound-levels.txt lines and provenance.json entries go too). Ticked at first: files the editor wrote whose edit is
     no longer in this project (reverted), so going back to the shipped sound is one click. */
  F.removeDialog = async function () {
    if (!Q.canPickFolder()) { U.toast('This browser cannot open folders (use Chrome or Edge): delete the files from the overrides folder by hand', true); return; }
    let dir;
    try { dir = await window.showDirectoryPicker({ id: 'quark-overrides', mode: 'readwrite' }); } catch (e) { return; }
    const res = await Q.resolveOverrides(dir);
    if (res.guess) { U.toast('"' + res.where + '" has no overrides folder: choose %APPDATA%\\quark\\overrides or the repository\'s assets\\overrides', true); return; }
    let files;
    try { files = await Q.listOverrides(res.dir); } catch (e) { U.fail('read ' + res.where, e); return; }
    if (!files.length) { U.toast('No sounds or voice lines in ' + res.where); return; }
    const known = k => Object.keys(Q.lastExport).find(e => e.toLowerCase() === k.toLowerCase());
    const status = f => { const e = Q.state.edits[f.key] && !Q.isNoop(f.key); return e ? 'your edit is in this project' : known(f.key) ? 'written by the editor; no edit of it now (reverted)' : 'not from this project'; };
    const rows = files.map(f => ({ f, cb: h('input', { type: 'checkbox', checked: !(Q.state.edits[f.key] && !Q.isNoop(f.key)) && !!known(f.key) }) }));
    const tbl = h('table', h('tr', h('th', ''), h('th', 'file in ' + res.where), h('th', '')), rows.map(r => h('tr', h('td', r.cb), h('td.num', r.f.path), h('td.faint', status(r.f)))));
    const ok = await U.dialog('Remove sounds from the game', [h('p', 'Tick the files to take out of ' + res.where + ': the game plays the shipped sound again (restart it). Each is renamed to .bak, so nothing is lost until you delete those; its sound-levels.txt line and provenance.json entry go too. Removing some of a voice\'s quips leaves it the others.'), tbl],
      [{ label: 'Cancel', value: false }, { label: 'Remove ticked', primary: true, value: true }], { second: true });
    const paths = rows.filter(r => r.cb.checked).map(r => r.f.path);
    if (!ok || !paths.length) return;
    try {
      const r = await Q.removeOverrides(res.dir, paths);
      const forgot = await P.forget(paths.map(p => p.replace(/\.[^.]+$/, '') + '.wav'));
      U.toast(r.moved.length + ' file' + (r.moved.length > 1 ? 's' : '') + ' moved to .bak in ' + res.where + (forgot ? '' : ' (provenance.json: allow the sound-editor folder on the next export to drop their entries)'));
      Q.lastRemoved = r.moved;   // (tests read it back)
    } catch (e) { U.fail('remove', e); }
  };
  F.exportDialog = async function () {
    const all = Q.exportList(), noop = Q.noopList();
    if (!all.length) { U.toast(noop.length ? 'Your edit' + (noop.length > 1 ? 's play' : ' plays') + ' the same as the shipped sound: nothing to export' : 'Nothing edited yet: there is nothing to export'); return; }
    try { await Q.ensureVoicesFor(all); } catch (e) { U.fail('load voices', e); return; }
    const changed = all.filter(Q.changedSinceExport), chosen = new Set(changed.length ? changed : all);
    const lv = new Set(Q.levelList());
    const rep = h('div'), prog = h('div.progress', h('i')), gate = h('div.prov-gate'), quipBox = h('div'), tblBox = h('div');
    const replaceOther = h('input', { type: 'checkbox', checked: true });
    const keepQuips = h('input', { type: 'checkbox', checked: true, onchange: () => refresh() });
    // a zip can't see the overrides folder: its sound-levels.txt is merged only if the owner picks it
    let levelsText = null;
    const levelsName = h('span.faint', 'not picked: the zip carries ' + Q.LEVELS_ADD + ' (the lines to add) instead');
    const levelsPick = h('input', { type: 'file', accept: '.txt,text/plain', hidden: true, onchange: async ev => {
      const f = ev.target.files[0]; ev.target.value = ''; if (!f) return;
      levelsText = await f.text(); levelsName.textContent = 'merging with ' + f.name + ' (' + levelsText.split(/\r?\n/).filter(l => l.trim() && !l.startsWith('#')).length + ' lines): the zip carries the merged sound-levels.txt';
    } });
    const plan = () => { const keys = all.filter(k => chosen.has(k)); return { keys, companions: keepQuips.checked ? Q.quipCompanions(keys) : [] }; };
    const originCell = k => {
      const o = P.originOf(k);
      if (o.choose) return U.originPicker(k, { onchange: updateGate });
      return h('span', { title: o.why }, h('span.chip.origin.' + o.origin, P.LABEL[o.origin]), ' ', h('span.faint', o.note));
    };
    const since = k => { const l = Q.lastExport[k]; return !l ? 'new' : Q.changedSinceExport(k) ? 'changed since ' + when(l.date) : 'unchanged since ' + when(l.date); };
    function renderTable() {
      const pl = plan();
      tblBox.innerHTML = '';
      tblBox.append(h('table.exp', h('tr', h('th', ''), h('th', 'file'), h('th', 'last export'), h('th', 'level'), h('th', 'origin (provenance.json)'), h('th', 'note')),
        all.map(k => {
          const a = Q.assets[k], cb = h('input', { type: 'checkbox', checked: chosen.has(k), title: 'Export this file', onchange: ev => { ev.target.checked ? chosen.add(k) : chosen.delete(k); refresh(); } });
          return h('tr' + (chosen.has(k) ? '' : '.off'), h('td', cb), h('td.num', k + '.wav'), h('td.faint', since(k)), h('td', lv.has(k) ? 'yours (sound-levels.txt)' : 'game matches it'), h('td', originCell(k)),
            h('td.faint', a.custom ? 'new: needs a game hook' : a.kind === 'sound' && a.pack === 'default' ? 'DEFAULT: also used by kits with DEFAULT\'s sound and RETRO' : ''));
        }),
        pl.companions.map(c => { const o = P.companionOrigin(c) || {}; return h('tr.comp', h('td', ''), h('td.num', c.key + '.wav'), h('td.faint', 'default quip'), h('td', 'game matches it'),
          h('td', o.origin ? h('span.chip.origin.' + o.origin, P.LABEL[o.origin]) : h('span.chip.origin.unset', 'from ' + short(c.line))),
          h('td.faint', 'keeps ' + Q.packName('voice', c.voice) + '\'s whole quip set' + (Q.state.edits[c.line] ? ' (your ' + short(c.line).split('/').pop() + ' line)' : ''))); })));
    }
    // quips: any quip file replaces the voice's whole default set in the game
    function renderQuips() {
      const pl = plan(), voices = [...new Set(pl.keys.filter(Q.isQuip).map(k => Q.parseKey(k).pack))];
      quipBox.innerHTML = '';
      if (!voices.length) return;
      const lose = voices.map(v => {
        const have = Q.QUIPS.filter(n => all.includes(Q.key('voice', v, n))).map(n => n.slice(6)), gone = Q.QUIPS.map(n => n.slice(6)).filter(n => !have.includes(n));
        return Q.packName('voice', v) + ' keeps only ' + have.join(', ') + (gone.length ? ' and loses ' + gone.join(', ') : '');
      });
      quipBox.append(h('label', keepQuips, 'keep each voice\'s other quips: write its default quips too (any quip file replaces the voice\'s whole set in the game)'),
        keepQuips.checked ? h('p.faint', pl.companions.length + ' default quip' + (pl.companions.length === 1 ? '' : 's') + ' added (rows marked "default quip"), as the game plays them now. A folder export skips the ones already there (an earlier export of yours stays).')
          : h('p', { style: { color: 'var(--warn)' } }, 'In the game: ' + lose.join('; ') + ' (unless its other quips are already in the overrides folder).'));
    }
    const progress = (i, n) => { prog.firstChild.style.width = (100 * i / n) + '%'; };
    let exportBtns = [];
    function updateGate() {
      const pl = plan();
      const miss = [...new Set(P.missing(pl.keys).concat(pl.companions.filter(c => Q.state.edits[c.line] && P.problem(c.line)).map(c => c.line)))];
      gate.textContent = !pl.keys.length ? 'Tick at least one file to export.' : miss.length ? miss.length + ' file' + (miss.length > 1 ? 's' : '') + ' still need' + (miss.length > 1 ? '' : 's') + ' an origin before export: ' + miss.map(k => short(k) + ' (' + P.problem(k) + ')').join(', ') + '. Human only if a person made the sound.' : '';
      for (const b of exportBtns) { b.disabled = !pl.keys.length || !!miss.length; b.title = !pl.keys.length ? 'Tick a file first' : miss.length ? 'Set every file\'s origin first' : ''; }
      return pl.keys.length && !miss.length;
    }
    function refresh() { renderTable(); renderQuips(); updateGate(); }
    const blocked = () => { if (updateGate()) return false; U.toast(plan().keys.length ? 'Set who made each dropped file first (the origin column)' : 'Tick at least one file to export', true); return true; };
    const choosable = all.filter(k => P.originOf(k).choose);
    // bulk: one origin for many dropped files
    const bSel = h('select', Object.keys(ORIGIN_TEXT).filter(Boolean).map(v => h('option', { value: v }, ORIGIN_TEXT[v])));
    const bTxt = h('input', { type: 'text', placeholder: HINT.human });
    bSel.onchange = () => { bTxt.placeholder = HINT[bSel.value]; };
    const bulk = which => {
      const ks = which === 'unset' ? choosable.filter(k => !P.originOf(k).origin) : choosable;
      if (!ks.length) { U.toast('No dropped files to set'); return; }
      const v = bSel.value, t = bTxt.value.trim();
      if (v === 'third-party' && !t) { U.toast('Third-party: say where they came from (pack / site / licence)', true); bTxt.focus(); return; }
      Q.commit('origin of ' + ks.length + ' files: ' + v, () => { for (const k of ks) P.setOrigin(k, v, v === 'third-party' ? '' : t, v === 'third-party' ? t : ''); });
      refresh();
    };
    const bulkRow = choosable.length > 1 ? h('div.prov-bulk', h('b', 'Set dropped files:'), bSel, bTxt,
      h('button', { type: 'button', onclick: () => bulk('unset') }, 'to those not set'), h('button', { type: 'button', onclick: () => bulk('all') }, 'to all ' + choosable.length)) : null;
    const pick = h('div.prov-bulk', h('b', 'Export:'),
      h('button', { type: 'button', onclick: () => { all.forEach(k => chosen.add(k)); refresh(); } }, 'every edit (' + all.length + ')'),
      h('button', { type: 'button', disabled: !changed.length || changed.length === all.length, onclick: () => { chosen.clear(); changed.forEach(k => chosen.add(k)); refresh(); } }, 'only changed since the last export (' + changed.length + ')'),
      h('span.faint', changed.length < all.length ? (all.length - changed.length) + ' unchanged since you last exported them: unticked, so they are not rewritten and their provenance date stays.' : ''));
    const body = [h('p', '48 kHz 16-bit mono WAV, rendered through trim, fades and chain. ' + lv.size + ' keep their own level: they are listed in sound-levels.txt (merged with the one already there).'),
      h('p', 'Pick the folder: ', h('code', '%APPDATA%\\quark\\overrides'), ' to try them in the game without rebuilding (restart the game), or the repository\'s ', h('code', 'assets\\overrides'), ' to ship them with the next build. Picking ', h('code', '%APPDATA%\\quark'), ' or the repository folder itself also works: the overrides folder inside is used.'),
      h('p', 'Every export also records who made each file in ', h('code', 'provenance.json'), ' in the sound-editor folder (where sound-editor.html is). A file you dropped in needs its origin: human only if a person made the sound. An edit of a shipped sound keeps the shipped sound\'s origin; a sound from Design... is yours. The .zip carries provenance.json at its root too: it belongs in the sound-editor folder, not in overrides.'),
      noop.length ? h('p.faint', 'Left out: ' + noop.length + ' edit' + (noop.length > 1 ? 's that play' : ' that plays') + ' the same as the shipped sound (' + noop.map(short).join(', ') + ').') : null,
      h('label', replaceOther, 'move a same-named .ogg / .flac / .mp3 already there to .bak (the game would otherwise pick either)'),
      h('div', { style: { margin: '4px 0' } }, h('button', { type: 'button', onclick: () => levelsPick.click() }, 'Zip: merge with the sound-levels.txt there...'), levelsPick, ' ', levelsName),
      quipBox, pick, bulkRow, tblBox, gate, prog, rep];
    const done = async (r, line) => {
      Q.markExported(r.keys, r.companions);
      rep.innerHTML = '';
      rep.append(...line.filter(Boolean));
      rep.append(h('p', await F.recordProvenance(r.keys, r.companions)));
      refresh();
    };
    const btns = [{ label: 'Remove from the game...', onclick: () => { F.removeDialog(); return false; } }, { label: 'Close', value: null },
      { label: 'Download .zip', onclick: async () => {
        if (blocked()) return false;
        try {
          const r = await Q.exportZip(progress, Object.assign(plan(), levelsText != null ? { levelsText } : {}));
          await done(r, [h('p', 'Zip downloaded: ' + r.files.length + ' files (unzip it into the overrides folder; move its provenance.json into the sound-editor folder instead).'),
            r.levelsAdd ? h('p', { style: { color: 'var(--warn)' } }, 'It has no sound-levels.txt (unzipping would replace yours): add the ' + r.levelsAdd.length + ' line' + (r.levelsAdd.length > 1 ? 's' : '') + ' in its ' + Q.LEVELS_ADD + ' to sound-levels.txt in the overrides folder (create it if there is none), or pick that file above and download again.') : null]);
        } catch (e) { U.fail('zip', e); }
        return false;
      } }];
    if (Q.canPickFolder()) btns.push({ label: 'Choose folder and write', primary: true, onclick: async () => {
      if (blocked()) return false;
      let dir;
      try { dir = await window.showDirectoryPicker({ id: 'quark-overrides', mode: 'readwrite' }); } catch (e) { return false; }
      try {
        const res = await Q.resolveOverrides(dir);
        if (res.guess && !await F.confirmGuess(res)) { rep.innerHTML = ''; rep.append(h('p.faint', 'Nothing written: choose the overrides folder.')); return false; }
        const r = await Q.exportToFolder(dir, Object.assign(plan(), { replaceOther: replaceOther.checked }), progress);
        U.toast('Exported ' + r.written.length + ' files');
        await done(r, [h('p', 'Wrote ' + r.written.length + ' files into ' + r.where + (r.guess ? ' (no overrides folder there: written into the folder you picked)' : '') + '.'),
          r.keptQuips.length ? h('p.faint', 'Already there, kept: ' + r.keptQuips.join(', ')) : null,
          r.removed.length ? h('p', 'Moved to .bak: ' + r.removed.join(', ')) : null,
          r.conflicts.length ? h('p', { style: { color: 'var(--warn)' } }, 'Also there with another extension (the game may pick either): ' + r.conflicts.join(', ')) : null]);
      } catch (e) { U.fail('export', e); }
      return false;
    } });
    else body.unshift(h('p', { style: { color: 'var(--warn)' } }, 'This browser cannot write folders (use Chrome or Edge); download the zip instead.'));
    refresh();
    const shown = U.dialog('Export to the game', body, btns);
    exportBtns = [...document.querySelectorAll('#dlg .dlg-f button')].filter(b => b.textContent === 'Download .zip' || b.textContent === 'Choose folder and write');
    updateGate();
    await shown;
  };

  F.init = function () {
    // page-level drop: auto-assign
    window.addEventListener('dragover', ev => { if ([...(ev.dataTransfer.types || [])].includes('Files')) { ev.preventDefault(); hint(true); } });
    window.addEventListener('drop', async ev => { ev.preventDefault(); hint(false); const files = await F.collect(ev.dataTransfer); F.autoAssign(files); });
    U.$('#btnUpload').onclick = () => U.$('#fileUpload').click();
    U.$('#btnUploadDir').onclick = () => U.$('#dirUpload').click();
    // (a folder's files carry their path, webkitRelativePath, so the folder names pick the kit / voice / quips)
    for (const id of ['#fileUpload', '#dirUpload']) U.$(id).onchange = ev => { const fs = [...ev.target.files].map(f => ({ file: f, path: f.webkitRelativePath || f.name })); ev.target.value = ''; F.autoAssign(fs.filter(x => AUDIO.test(x.file.name))); };
    U.$('#btnExport').onclick = () => F.exportDialog();
    U.$('#btnRemoveOverrides').onclick = () => F.removeDialog();
    U.$('#btnSaveProject').onclick = () => { Q.download(new Blob([Q.projectJSON()], { type: 'application/json' }), 'quark-sounds-project.json'); U.toast('Project saved (edits, groups, chains and your audio)'); };
    U.$('#btnLoadProject').onclick = () => U.$('#fileProject').click();
    U.$('#fileProject').onchange = async ev => {
      const f = ev.target.files[0]; ev.target.value = ''; if (!f) return;
      try { await Q.importProject(await f.text()); U.missing.ensureAssets(); Q.onStateChanged('open project'); U.toast('Opened ' + f.name + ': ' + Object.keys(Q.state.edits).length + ' edits'); }
      catch (e) { U.fail('open project', e); }
    };
    U.$('#btnResetProject').onclick = async () => {
      const ok = await U.dialog('New project', h('p', 'Discard every edit, group and note? (Save a project file first to keep them.)'), [{ label: 'Cancel', value: false }, { label: 'Discard all', primary: true, value: true }]);
      if (!ok) return;
      Q.state = { edits: {}, groups: [], customs: [], meta: {} }; Q.lastExport = {}; Q.clearHistory(); await Q.clearAutosave(); Q.onStateChanged('new project');
    };
  };
})();
