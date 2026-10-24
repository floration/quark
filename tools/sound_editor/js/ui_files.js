/* ===== UI: files ======================================================================================
   Drag and drop (onto a cell, or anywhere: matched by file name), the upload button, the project file, export. */
(function () {
  'use strict';
  const Q = window.Q, U = Q.ui, h = U.h, fmt = U.fmt;
  const F = U.files = {};
  const AUDIO = /\.(wav|ogg|mp3|flac|oga|opus|m4a|aac|aiff?|webm)$/i;

  // --- name matching: "ROCKET.wav", "heavy_metal/ROCKET.ogg", "male_GO.wav", "sounds/magical/hit-med.flac" -------------
  const norm = s => s.toUpperCase().replace(/[\s\-.]+/g, '_').replace(/[^A-Z0-9_]/g, '');
  function allNames(kind) {
    const base = kind === 'voice' ? Q.LINES : Q.SOUNDS;
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
    // the sound / line name: exact, else the longest known name the stem starts with (ROCKET_V2 -> ROCKET)
    const find = k => { const names = allNames(k); if (names.includes(stem)) return stem; return names.filter(n => stem.startsWith(n + '_') || stem === n).sort((a, b) => b.length - a.length)[0] || names.filter(n => stem.includes(n)).sort((a, b) => b.length - a.length)[0] || null; };
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
    if (!name && fallback) return { kind: fallback.kind, pack: fallback.pack, name: fallback.name, guessed: true };
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
    const ok = await U.dialog('Assign ' + rows.length + ' file' + (rows.length > 1 ? 's' : ''), [h('p', 'Matched by file name and folder (ROCKET.wav, heavy_metal/ROCKET.ogg, male_GO.wav). A file naming no kit / voice goes to ' + (fb ? 'the selected sound\'s ' + (fb.kind === 'voice' ? 'voice' : 'kit') + ' (' + Q.packName(fb.kind, fb.pack) + ')' : 'DEFAULT (voices: MALE)') + '. Check the targets, untick what to skip. An edit\'s filter chain stays on its new audio.'), table],
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

  // --- export -----------------------------------------------------------------------------------------------------------
  F.exportDialog = async function () {
    const keys = Q.exportList();
    if (!keys.length) { U.toast('Nothing edited yet: there is nothing to export'); return; }
    const lv = new Set(Q.levelList());
    const rep = h('div'), prog = h('div.progress', h('i'));
    const replaceOther = h('input', { type: 'checkbox', checked: true });
    const tbl = h('table', h('tr', h('th', 'file'), h('th', 'level'), h('th', 'note')), keys.map(k => {
      const a = Q.assets[k];
      return h('tr', h('td.num', k + '.wav'), h('td', lv.has(k) ? 'yours (sound-levels.txt)' : 'game matches it'), h('td.faint', a.custom ? 'new: needs a game hook' : a.kind === 'sound' && a.pack === 'default' ? 'DEFAULT: also used by kits with DEFAULT\'s sound and RETRO' : ''));
    }));
    const progress = (i, n) => { prog.firstChild.style.width = (100 * i / n) + '%'; };
    const body = [h('p', keys.length + ' files, 48 kHz 16-bit mono WAV, rendered through trim, fades and chain. ' + lv.size + ' keep their own level: they are listed in sound-levels.txt (merged with the one already there).'),
      h('p', 'Pick the folder: ', h('code', '%APPDATA%\\quark\\overrides'), ' to try them in the game without rebuilding (restart the game), or the repository\'s ', h('code', 'assets\\overrides'), ' to ship them with the next build. Picking ', h('code', '%APPDATA%\\quark'), ' or the repository folder itself also works: the overrides folder inside is used.'),
      h('label', replaceOther, 'move a same-named .ogg / .flac / .mp3 already there to .bak (the game would otherwise pick either)'),
      tbl, prog, rep];
    const btns = [{ label: 'Close', value: null },
      { label: 'Download .zip', onclick: async () => { try { const r = await Q.exportZip(progress); rep.textContent = 'Zip downloaded: ' + r.files.length + ' files (unzip it into the overrides folder).'; } catch (e) { U.fail('zip', e); } return false; } }];
    if (Q.canPickFolder()) btns.push({ label: 'Choose folder and write', primary: true, onclick: async () => {
      let dir;
      try { dir = await window.showDirectoryPicker({ id: 'quark-overrides', mode: 'readwrite' }); } catch (e) { return false; }
      try {
        const r = await Q.exportToFolder(dir, { replaceOther: replaceOther.checked }, progress);
        rep.innerHTML = '';
        rep.append(h('p', 'Wrote ' + r.written.length + ' files into ' + r.where + (r.guess ? ' (no overrides folder found there: written into the folder you picked)' : '') + '.'),
          r.removed.length ? h('p', 'Moved to .bak: ' + r.removed.join(', ')) : null,
          r.conflicts.length ? h('p', { style: { color: 'var(--warn)' } }, 'Also there with another extension (the game may pick either): ' + r.conflicts.join(', ')) : null);
        U.toast('Exported ' + r.written.length + ' files');
      } catch (e) { U.fail('export', e); }
      return false;
    } });
    else body.unshift(h('p', { style: { color: 'var(--warn)' } }, 'This browser cannot write folders (use Chrome or Edge); download the zip instead.'));
    await U.dialog('Export to the game', body, btns);
  };

  F.init = function () {
    // page-level drop: auto-assign
    window.addEventListener('dragover', ev => { if ([...(ev.dataTransfer.types || [])].includes('Files')) { ev.preventDefault(); hint(true); } });
    window.addEventListener('drop', async ev => { ev.preventDefault(); hint(false); const files = await F.collect(ev.dataTransfer); F.autoAssign(files); });
    U.$('#btnUpload').onclick = () => U.$('#fileUpload').click();
    U.$('#fileUpload').onchange = ev => { const fs = [...ev.target.files].map(f => ({ file: f, path: f.webkitRelativePath || f.name })); ev.target.value = ''; F.autoAssign(fs.filter(x => AUDIO.test(x.file.name))); };
    U.$('#btnExport').onclick = () => F.exportDialog();
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
      Q.state = { edits: {}, groups: [], customs: [], meta: {} }; Q.clearHistory(); await Q.clearAutosave(); Q.onStateChanged('new project');
    };
  };
})();
