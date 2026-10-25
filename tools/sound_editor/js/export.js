/* ===== EXPORT =======================================================================================
   Writing what the game reads: sounds/<kit>/<SOUND>.wav, voices/<voice>/<LINE>.wav (48 kHz 16-bit mono) and
   sound-levels.txt, into a folder (File System Access API) or a .zip. Also the project file and the autosave. */
(function () {
  'use strict';
  const Q = window.Q;

  // --- which assets to export ----------------------------------------------------------------------------------
  Q.exportList = function () {
    return Object.keys(Q.state.edits).filter(k => Q.assets[k] && !Q.isNoop(k)).sort();
  };
  /* An edit that plays exactly the shipped sound (a step added then removed, every step bypassed, an auto trim inside the
     leading silence the game cuts anyway) is left out of the export: its file would only pin the old sound over a later
     shipped improvement (and a kit's copy would be levelled to DEFAULT's instead of keeping its own level). */
  Q.isNoop = function (key) {
    const e = Q.state.edits[key], a = Q.assets[key];
    if (!e || !a || a.custom || e.src || e.made || e.fadeIn > 0 || e.fadeOut > 0 || (e.chain || []).some(st => st.on)) return false;
    const x = a.orig, t0 = e.trim[0] || 0;
    if (!x) return false;
    if (e.trim[1] != null && e.trim[1] < x.length / Q.RATE - 1e-6) return false;
    return t0 <= 0 || t0 * Q.RATE <= Q.leadingSilence(x);
  };
  Q.noopList = () => Object.keys(Q.state.edits).filter(k => Q.assets[k] && Q.isNoop(k)).sort();
  Q.levelList = function () {   // exported files whose level was set in the editor (the game keeps their level)
    return Q.exportList().filter(k => Q.state.edits[k].levelSet);
  };
  /* Quips: any quip file for a voice replaces that voice's whole default set in the game (audio.cpp loadQuips), so an
     export with one quip of a voice also writes the voice's other default quips, as the game plays them now (its line;
     your edit of that line when there is one), unless the voice already has shipped quip files (its set is those then)
     or the line is silent (the game has no quip of it). Returns [{key, line (the line key whose clip it is), voice}]. */
  Q.isQuip = key => /^voices\/[^/]+\/quips\//.test(key);
  Q.quipCompanions = function (keys) {
    const voices = [...new Set(keys.filter(Q.isQuip).map(k => Q.parseKey(k).pack))], out = [];
    for (const v of voices) {
      const mine = Q.QUIPS.map(n => Q.key('voice', v, n)).filter(k => Q.assets[k]);
      if (mine.some(k => Q.assets[k].shippedOverride)) continue;
      for (const k of mine) {
        if (keys.includes(k) || (Q.state.edits[k] && !Q.isNoop(k))) continue;   // (your quip edits: exported now or before)
        const line = Q.key('voice', v, Q.parseKey(k).name.slice(6));
        if (!Q.state.edits[line]) { const x = Q.assets[k].orig; if (!x || x.length < 2 || QLoud.samplePeak(x) < 1e-5) continue; }
        out.push({ key: k, line, voice: v });
      }
    }
    return out;
  };
  /* What was last exported (key -> {sig: the edit as exported, date}): the export dialog ticks only what changed since,
     so files are not rewritten and their provenance dates stay true. Kept in the autosave and the project file (not in
     the undoable state: an undo doesn't un-write a file). */
  Q.lastExport = {};
  Q.exportSig = key => Q.editSig(key);
  Q.changedSinceExport = key => !Q.lastExport[key] || Q.lastExport[key].sig !== Q.exportSig(key);
  Q.markExported = function (keys, companions) {
    const date = new Date().toISOString();
    for (const k of keys) Q.lastExport[k] = { sig: Q.exportSig(k), date };
    for (const c of companions || []) Q.lastExport[c.key] = { sig: 'companion:' + Q.editSig(c.line), date };
    Q.autosaveSoon && Q.autosaveSoon();
  };
  // what the game plays as that default quip now: your edit of its line, else the shipped quip (= the line)
  Q.companionOut = async function (c) { return Q.state.edits[c.line] ? Q.getOut(c.line) : Q.assets[c.key].orig; };
  Q.ensureVoicesFor = keys => Promise.all([...new Set(keys.filter(Q.isQuip).map(k => Q.parseKey(k).pack))].map(v => Q.loadPack('voice', v)));
  // sound-levels.txt: merge with an existing file. Lines for files exported now follow this export (listed when their
  // level is yours, dropped when not); every other line (other files, comments) is kept as it was.
  Q.mergeLevels = function (existing, exported, levelSet) {
    const out = [], seen = new Set(), exp = new Set(exported.map(k => k.toLowerCase())), lv = new Set(levelSet.map(k => k.toLowerCase()));
    const lines = (existing || '').split(/\r?\n/);
    let header = false;
    for (const raw of lines) {
      const l = raw.trim();
      if (!l) continue;
      if (l.startsWith('#')) { out.push(raw); header = true; continue; }
      const p = l.replace(/\\/g, '/').replace(/\.(wav|ogg|flac|mp3)$/i, ''), lp = p.toLowerCase();
      if (seen.has(lp)) continue;
      if (exp.has(lp) && !lv.has(lp)) continue;     // exported now without a level of its own: dropped
      seen.add(lp); out.push(p);
    }
    if (!header) out.unshift('# Quark: replacements that keep their own level (the game does not rescale them to the sound they replace).',
      '# One path per line, without extension. Written by the sound editor; lines it did not export are kept.');
    for (const k of levelSet) if (!seen.has(k.toLowerCase())) { out.push(k); seen.add(k.toLowerCase()); }
    return out.join('\n') + '\n';
  };

  Q.renderExport = async function (key) { return Q.encodeWav(await Q.getOut(key), 48000); };

  // --- a store-only zip ------------------------------------------------------------------------------------------
  const crcTable = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
  Q.crc32 = function (u8) { let c = 0xffffffff; for (let i = 0; i < u8.length; i++) c = crcTable[(c ^ u8[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  Q.zip = function (files) {   // files: [{name, data: Uint8Array}]
    const enc = new TextEncoder(), parts = [], central = [];
    const d = new Date(), time = (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1);
    const date = ((d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
    let off = 0;
    for (const f of files) {
      const name = enc.encode(f.name), crc = Q.crc32(f.data), h = new DataView(new ArrayBuffer(30));
      h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(6, 0x0800, true); h.setUint16(8, 0, true);
      h.setUint16(10, time, true); h.setUint16(12, date, true); h.setUint32(14, crc, true);
      h.setUint32(18, f.data.length, true); h.setUint32(22, f.data.length, true); h.setUint16(26, name.length, true); h.setUint16(28, 0, true);
      parts.push(new Uint8Array(h.buffer), name, f.data);
      const c = new DataView(new ArrayBuffer(46));
      c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x0800, true); c.setUint16(10, 0, true);
      c.setUint16(12, time, true); c.setUint16(14, date, true); c.setUint32(16, crc, true); c.setUint32(20, f.data.length, true);
      c.setUint32(24, f.data.length, true); c.setUint16(28, name.length, true); c.setUint32(42, off, true);
      central.push(new Uint8Array(c.buffer), name);
      off += 30 + name.length + f.data.length;
    }
    const cdSize = central.reduce((s, u) => s + u.length, 0), e = new DataView(new ArrayBuffer(22));
    e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true);
    e.setUint32(12, cdSize, true); e.setUint32(16, off, true);
    return new Blob([...parts, ...central, new Uint8Array(e.buffer)], { type: 'application/zip' });
  };

  Q.download = function (blob, name) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob); a.download = name;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 4000);
  };

  /* every exported file: [{name, data}] + sound-levels.txt (merged with `existingLevels`; opts.noLevels leaves it out).
     opts.keys: the edits to write (default all), opts.companions: the default quips written with them (Q.quipCompanions),
     opts.skip(name): a companion not to write (already in the folder). */
  Q.buildExportFiles = async function (existingLevels, onProgress, opts) {
    opts = opts || {};
    const keys = opts.keys || Q.exportList(), comp = (opts.companions || []).filter(c => !(opts.skip && opts.skip(c.key + '.wav'))), files = [];
    const n = keys.length + comp.length;
    for (let i = 0; i < keys.length; i++) {
      files.push({ name: keys[i] + '.wav', data: await Q.renderExport(keys[i]) });
      onProgress && onProgress(i + 1, n);
    }
    for (let i = 0; i < comp.length; i++) {
      files.push({ name: comp[i].key + '.wav', data: Q.encodeWav(await Q.companionOut(comp[i]), 48000), companion: true });
      onProgress && onProgress(keys.length + i + 1, n);
    }
    // (a default quip the game levels itself: an old line of it in sound-levels.txt goes)
    const levelSet = keys.filter(k => Q.state.edits[k] && Q.state.edits[k].levelSet);
    const levels = Q.mergeLevels(existingLevels, keys.concat(comp.map(c => c.key)), levelSet);
    if (!opts.noLevels) files.push({ name: 'sound-levels.txt', data: new TextEncoder().encode(levels) });
    return { files, keys, companions: comp, levels, levelSet };
  };

  /* The zip also carries provenance.json at its root (it belongs in the sound-editor folder, not in overrides): every
     entry this editor has exported, with this export's on top (js/provenance.js). A zip can't merge sound-levels.txt
     with the one in the overrides folder, so it never carries that name (unzipping would wipe the lines written before):
     with opts.levelsText (the existing file, picked in the dialog) it carries the merged sound-levels.txt, else only
     sound-levels-add.txt, the lines to add by hand (r.levelsAdd). */
  Q.LEVELS_ADD = 'sound-levels-add.txt';
  Q.exportZip = async function (onProgress, opts) {
    opts = opts || {};
    const merged = typeof opts.levelsText === 'string';
    const r = await Q.buildExportFiles(merged ? opts.levelsText : '', onProgress, Object.assign({}, opts, { noLevels: !merged }));
    if (!merged && r.levelSet.length) {
      r.levelsAdd = r.levelSet.slice();
      r.files.push({ name: Q.LEVELS_ADD, data: new TextEncoder().encode('# Add these lines to sound-levels.txt in the overrides folder (create it if there is none):\n# the game keeps these files\' own level.\n' + r.levelSet.join('\n') + '\n') });
    }
    if (Q.prov) {
      r.provenance = await Q.prov.offline(Q.prov.entries(r.keys, r.companions));
      r.files.push({ name: Q.prov.FILE, data: new TextEncoder().encode(Q.prov.text(r.provenance)) });
    }
    const blob = Q.zip(r.files);
    Q.lastZip = blob;   // (tests read it back)
    Q.lastZipNames = r.files.map(f => f.name);
    Q.download(blob, 'quark-overrides.zip');
    return r;
  };

  // --- the File System Access API: write into the overrides folder ------------------------------------------------------
  Q.canPickFolder = () => typeof window.showDirectoryPicker === 'function';
  async function childDir(dir, name) { try { return await dir.getDirectoryHandle(name); } catch (e) { return null; } }
  // the folder the owner picked may be the overrides folder itself, the data folder (…/quark) or the repository
  Q.resolveOverrides = async function (dir) {
    if (dir.name.toLowerCase() === 'overrides') return { dir, where: dir.name };
    const ov = await childDir(dir, 'overrides');
    if (ov) return { dir: ov, where: dir.name + '/overrides' };
    const assets = await childDir(dir, 'assets');
    if (assets) { const o2 = await childDir(assets, 'overrides'); if (o2) return { dir: o2, where: dir.name + '/assets/overrides' }; }
    return { dir, where: dir.name, guess: true };
  };
  async function hasAudio(dir, path) {   // the folder has this file (any audio extension the game reads)
    const parts = path.split('/'), file = parts.pop(), stem = file.replace(/\.[^.]+$/, '').toLowerCase();
    let d = dir;
    for (const p of parts) { d = await childDir(d, p); if (!d) return false; }
    for await (const [n, h] of d.entries()) { const m = n.match(/^(.*)\.(ogg|flac|mp3|wav)$/i); if (h.kind === 'file' && m && m[1].toLowerCase() === stem) return true; }
    return false;
  }
  /* opts: replaceOther (move a same-named .ogg / .flac / .mp3 to .bak), keys / companions (Q.buildExportFiles). A default
     quip already in the folder is not written again: it is already part of that voice's set (maybe your earlier edit). */
  Q.exportToFolder = async function (root, opts, onProgress) {
    opts = opts || {};
    const res = await Q.resolveOverrides(root), dir = res.dir;
    let existing = '';
    try { existing = await (await (await dir.getFileHandle('sound-levels.txt')).getFile()).text(); } catch (e) { }
    const there = new Set();
    for (const c of opts.companions || []) if (await hasAudio(dir, c.key + '.wav')) there.add(c.key + '.wav');
    const r = await Q.buildExportFiles(existing, onProgress, Object.assign({}, opts, { skip: n => there.has(n) }));
    const report = { where: res.where, guess: !!res.guess, written: [], conflicts: [], removed: [], keptQuips: [...there], keys: r.keys, companions: r.companions };
    for (const f of r.files) {
      const parts = f.name.split('/'), file = parts.pop();
      let d = dir;
      for (const p of parts) d = await d.getDirectoryHandle(p, { create: true });
      // the same name with another audio extension would compete with this file (the game takes any one of them)
      const stem = file.replace(/\.[^.]+$/, '').toLowerCase();
      if (file.endsWith('.wav')) {
        for await (const [n, h] of d.entries()) {
          if (h.kind !== 'file' || n === file) continue;
          const m = n.match(/^(.*)\.(ogg|flac|mp3|wav)$/i);
          if (m && m[1].toLowerCase() === stem) {
            if (opts.replaceOther) {
              const bak = await d.getFileHandle(n + '.bak', { create: true }), w = await bak.createWritable();
              await w.write(await (await h.getFile()).arrayBuffer()); await w.close();
              await d.removeEntry(n); report.removed.push(parts.concat(n).join('/'));
            } else report.conflicts.push(parts.concat(n).join('/'));
          }
        }
      }
      const fh = await d.getFileHandle(file, { create: true }), w = await fh.createWritable();
      await w.write(f.data); await w.close();
      report.written.push(f.name);
    }
    report.levels = r.levels;
    return report;
  };

  /* Taking files back out of the overrides folder (sounds you reverted, or no longer want): each is renamed to .bak
     (the game ignores it; delete it by hand once sure), its sound-levels.txt line goes. Returns {moved, where}. */
  const AUDIO_EXT = /^(.*)\.(wav|ogg|flac|mp3)$/i;
  Q.listOverrides = async function (dir) {
    const out = [];
    const files = async (d, pre) => { const r = []; for await (const [n, hd] of d.entries()) { const m = n.match(AUDIO_EXT); if (hd.kind === 'file' && m) r.push({ path: pre + n, key: pre + m[1].toUpperCase() }); } return r; };
    for (const [top, packs] of [['sounds', Q.KITS], ['voices', Q.VOICES]]) {
      const t = await childDir(dir, top); if (!t) continue;
      for (const p of packs) {
        const d = await childDir(t, p.dir); if (!d) continue;
        out.push(...await files(d, top + '/' + p.dir + '/'));
        if (top === 'voices') { const qd = await childDir(d, 'quips'); if (qd) out.push(...await files(qd, top + '/' + p.dir + '/quips/')); }
      }
    }
    return out.sort((a, b) => a.path.localeCompare(b.path));
  };
  Q.removeOverrides = async function (dir, paths) {
    const moved = [];
    for (const path of paths) {
      const parts = path.split('/'), file = parts.pop();
      let d = dir;
      for (const p of parts) d = await d.getDirectoryHandle(p);
      const h = await d.getFileHandle(file), bak = await d.getFileHandle(file + '.bak', { create: true }), w = await bak.createWritable();
      await w.write(await (await h.getFile()).arrayBuffer()); await w.close();
      await d.removeEntry(file);
      moved.push(path);
    }
    const keys = paths.map(p => p.replace(/\.[^.]+$/, ''));
    try {
      const fh = await dir.getFileHandle('sound-levels.txt'), text = await (await fh.getFile()).text();
      const merged = Q.mergeLevels(text, keys, []);
      if (merged !== text) { const w = await fh.createWritable(); await w.write(merged); await w.close(); }
    } catch (e) { }
    for (const k of keys) for (const e of Object.keys(Q.lastExport)) if (e.toLowerCase() === k.toLowerCase()) delete Q.lastExport[e];
    Q.autosaveSoon && Q.autosaveSoon();
    return { moved };
  };

  // --- project file (all edits, groups, chains, the dropped audio embedded) -----------------------------------------
  function f32ToB64(f) { return Q.bytesToB64(new Uint8Array(f.buffer, f.byteOffset, f.byteLength)); }
  function b64ToF32(b) { const u = Q.b64ToBytes(b); return new Float32Array(u.buffer, u.byteOffset, u.byteLength / 4); }
  Q.usedSources = function () { const s = new Set(); for (const e of Object.values(Q.state.edits)) if (e.src) s.add(e.src); return s; };
  Q.projectJSON = function () {
    const sources = {};
    for (const id of Q.usedSources()) {
      const s = Q.sources[id]; if (!s) continue;
      sources[id] = s.bytes ? { name: s.name, file: Q.bytesToB64(s.bytes) } : { name: s.name, f32: f32ToB64(s.data) };
    }
    return JSON.stringify({ format: 'quark-sound-editor', version: 1, game: Q.manifest && Q.manifest.version, saved: new Date().toISOString(),
      state: Q.state, mix: Q.mixToJSON(), lastExport: Q.lastExport, sources });
  };
  Q.importProject = async function (text) {
    const p = JSON.parse(text);
    if (p.format !== 'quark-sound-editor') throw new Error('not a sound editor project');
    for (const [id, s] of Object.entries(p.sources || {})) {
      const data = s.file ? await Q.decodeBytes(Q.b64ToBytes(s.file)) : b64ToF32(s.f32);
      Q.sources[id] = { name: s.name, data, bytes: s.file ? Q.b64ToBytes(s.file) : null };
    }
    Q.state = Object.assign({ edits: {}, groups: [], customs: [], meta: {} }, p.state);
    if (p.mix) Q.mixFromJSON(p.mix);
    Q.lastExport = p.lastExport && typeof p.lastExport === 'object' ? p.lastExport : {};
    Q.clearHistory();
    return p;
  };
  Q.mixToJSON = () => ({ soloSound: [...Q.mix.soloSound], muteSound: [...Q.mix.muteSound], soloPack: [...Q.mix.soloPack], mutePack: [...Q.mix.mutePack] });
  Q.mixFromJSON = m => { for (const k of ['soloSound', 'muteSound', 'soloPack', 'mutePack']) Q.mix[k] = new Set(m[k] || []); };

  // --- autosave (IndexedDB; any failure just means no autosave) ------------------------------------------------------
  function idb() {
    return new Promise((res, rej) => {
      const r = indexedDB.open('quark-sound-editor', 1);
      r.onupgradeneeded = () => r.result.createObjectStore('kv');
      r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
    });
  }
  Q.autosave = async function () {
    try {
      const db = await idb(), sources = {};
      for (const id of Q.usedSources()) { const s = Q.sources[id]; if (s) sources[id] = { name: s.name, data: s.data, bytes: s.bytes }; }
      await new Promise((res, rej) => {
        const tx = db.transaction('kv', 'readwrite');
        tx.objectStore('kv').put({ state: Q.state, mix: Q.mixToJSON(), lastExport: Q.lastExport, sources, saved: Date.now() }, 'autosave');
        tx.oncomplete = res; tx.onerror = () => rej(tx.error);
      });
      db.close();
      return true;
    } catch (e) { return false; }
  };
  Q.loadAutosave = async function () {
    try {
      const db = await idb();
      const v = await new Promise((res, rej) => { const r = db.transaction('kv').objectStore('kv').get('autosave'); r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); });
      db.close();
      return v || null;
    } catch (e) { return null; }
  };
  Q.clearAutosave = async function () {
    try { const db = await idb(); await new Promise(res => { const tx = db.transaction('kv', 'readwrite'); tx.objectStore('kv').delete('autosave'); tx.oncomplete = res; tx.onerror = res; }); db.close(); } catch (e) { }
  };

  // stereo WAV (a scene's mix)
  Q.encodeWavStereo = function (buf) {
    const n = buf.length, l = buf.getChannelData(0), r = buf.getChannelData(1), ab = new ArrayBuffer(44 + n * 4), d = new DataView(ab);
    const w = (o, s) => { for (let i = 0; i < s.length; i++) d.setUint8(o + i, s.charCodeAt(i)); };
    w(0, 'RIFF'); d.setUint32(4, 36 + n * 4, true); w(8, 'WAVE'); w(12, 'fmt '); d.setUint32(16, 16, true); d.setUint16(20, 1, true);
    d.setUint16(22, 2, true); d.setUint32(24, 48000, true); d.setUint32(28, 192000, true); d.setUint16(32, 4, true); d.setUint16(34, 16, true);
    w(36, 'data'); d.setUint32(40, n * 4, true);
    for (let i = 0; i < n; i++) {
      d.setInt16(44 + i * 4, Math.round(Math.max(-1, Math.min(1, l[i])) * 32767), true);
      d.setInt16(46 + i * 4, Math.round(Math.max(-1, Math.min(1, r[i])) * 32767), true);
    }
    return new Uint8Array(ab);
  };
})();
