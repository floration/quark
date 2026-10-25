/* ===== EXPORT ========================================================================================
   What the game reads: models/<kind>/<NAME>.glb, into a folder (File System Access API: the overrides folder, the
   data folder or the repository, like the sound editor) or a .zip; the provenance sidecar (provenance.json: who made
   each model, merged with the file already there); the project file; the autosave (IndexedDB). */
(function () {
  'use strict';
  const Q = window.Q;

  Q.exportList = () => Object.keys(Q.state.edits).filter(k => Q.models[k] && Q.current(k).type !== 'none').sort();
  Q.exportError = key => Q.problems(key).find(x => x.level === 'error') || null;   // (blocks that model's export)
  Q.exportableList = () => Q.exportList().filter(k => !Q.exportError(k));
  Q.exportPath = key => key + '.glb';

  // --- provenance ----------------------------------------------------------------------------------------------------
  // Q.state.prov[key] = { origin: 'human' | 'ai' | 'third-party', by, source, licence, note, auto, scratch } (auto: the
  // note is the editor's own description of the tweak, rewritten as the model changes, until the user writes one;
  // scratch: made with Start from scratch). It belongs to the edit: undo / redo carry it, Reset drops it.
  // Q.provLog: every entry this editor exported (kept in the browser), so a zip carries them all.
  Q.provId = key => key + '.gltf';   // ids are the export / override path with .gltf (the asset id the dashboard uses)
  Q.today = () => { const d = new Date(); return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0'); };
  Q.defaultBy = () => { try { return localStorage.getItem('qme.by') || 'q'; } catch (e) { return 'q'; } };
  Q.setDefaultBy = v => { try { localStorage.setItem('qme.by', v); } catch (e) { } };
  Q.provLog = {};
  try { Q.provLog = JSON.parse(localStorage.getItem('qme.provLog') || '{}') || {}; } catch (e) { Q.provLog = {}; }
  function saveLog() { try { localStorage.setItem('qme.provLog', JSON.stringify(Q.provLog)); } catch (e) { } }
  Q.ORIGINS = ['human', 'ai', 'third-party'];
  const SCRATCH_NOTE = 'made from scratch in the model editor';
  // what an edit starts from: the shipped override when there is one (what the game draws now), else the default;
  // its provenance is the repository's entry for the id (assets/provenance/*.json), a default without one is AI-made
  Q.baseOf = function (key) {
    const m = Q.models[key];
    if (!m) return { shipped: false, parts: null, prov: { origin: 'ai' } };
    const sh = m.shipped && m.shipped.mesh && !m.shipped.err ? m.shipped : null;
    const rp = m.prov && Q.ORIGINS.indexOf(m.prov.origin) >= 0 ? m.prov : null;
    return { shipped: !!sh, parts: sh ? sh.parts : m.defParts, prov: rp || { origin: 'ai', source: sh ? '' : 'box model in src/client/models.cpp' } };
  };
  Q.describeBase = function (key) {
    const b = Q.baseOf(key), p = b.prov;
    if (!b.shipped) return p.origin === 'ai' ? 'the AI default box model' : 'the default box model (' + p.origin + ')';
    return 'the shipped model (assets/overrides, ' + p.origin + (p.by ? ', by ' + p.by : '') + ')';
  };
  // what changed against what the edit started from, in words (the tweak note)
  Q.describeTweak = function (key) {
    const m = Q.models[key], cur = Q.current(key);
    if (!m || cur.type !== 'boxes') return '';
    const base = Q.baseOf(key).parts || [], parts = cur.parts;
    const same = (a, b) => a.c.every((v, i) => Math.abs(v - b.c[i]) < 1e-6) && a.h.every((v, i) => Math.abs(v - b.h[i]) < 1e-6);
    const eq3 = (x, y) => (!x && !y) || (!!x && !!y && x.every((v, i) => Math.abs(v - y[i]) < 1e-6));
    const sameCol = (a, b) => eq3(a.color, b.color) && eq3(a.mul, b.mul) && eq3(a.add, b.add) && eq3(a.teamMul, b.teamMul) && (a.slot | 0) === (b.slot | 0) &&
      (a.alpha == null ? 1 : a.alpha) === (b.alpha == null ? 1 : b.alpha) && !!a.crouch === !!b.crouch && (a.shadow !== false) === (b.shadow !== false) && !!a.visOnly === !!b.visOnly;
    let moved = 0, recol = 0;
    const n = Math.min(base.length, parts.length);
    for (let i = 0; i < n; i++) { if (!same(base[i], parts[i])) moved++; if (!sameCol(base[i], parts[i])) recol++; }
    const bits = [];
    if (moved) bits.push(moved + ' box' + (moved > 1 ? 'es' : '') + ' moved / resized');
    if (recol) bits.push(recol + ' recoloured / re-flagged');
    if (parts.length > base.length) bits.push((parts.length - base.length) + ' added');
    if (parts.length < base.length) bits.push((base.length - parts.length) + ' removed');
    return 'tweak of ' + Q.describeBase(key) + ' in the model editor' + (bits.length ? ': ' + bits.join(', ') : '');
  };
  // the entry for a model as it is now. Edited: the origin chosen (Start from scratch: human; an import: what its
  // dialog said), else a mesh import is third-party and a box tweak keeps the origin of what it started from (a tweak
  // of a shipped human-made model stays human, of the AI default ai). Not edited: the repository's entry.
  Q.provFor = function (key) {
    const e = Q.state.edits[key], base = Q.baseOf(key);
    if (!e) {
      const b = base.prov, out = { origin: b.origin, by: b.by || '', date: b.date || '', source: b.source || '', note: b.note || '' };
      if (b.licence) out.licence = b.licence;
      return out;
    }
    const p = (Q.state.prov && Q.state.prov[key]) || {};
    const origin = Q.ORIGINS.indexOf(p.origin) >= 0 ? p.origin : (e.type === 'mesh' ? 'third-party' : base.prov.origin);
    const out = { origin, by: p.by || Q.defaultBy(), date: Q.today(), source: p.source || '', note: '' };
    if (p.licence) out.licence = p.licence;
    else if (!p.origin && origin === 'third-party' && e.type === 'boxes' && base.prov.licence) out.licence = base.prov.licence;
    if (p.note && !p.auto) out.note = p.note;
    else if (p.scratch) out.note = SCRATCH_NOTE;
    else if (e.type === 'boxes') out.note = Q.describeTweak(key);
    if (!out.source) out.source = origin !== 'third-party' ? 'Quark model editor' : (!p.origin && base.prov.origin === 'third-party' ? base.prov.source || '' : '');
    return out;
  };
  Q.setProv = function (key, fields) {
    Q.state.prov = Q.state.prov || {};
    Q.state.prov[key] = Object.assign({}, Q.state.prov[key] || {}, fields);
    Q.onStateChanged && Q.onStateChanged('prov', key);
  };
  // a provenance record (a file's embedded one, a provenance.json entry) as the state it came from: a note this editor
  // wrote itself stays automatic (rewritten as the model changes)
  Q.provState = function (rec) {
    if (!rec || typeof rec !== 'object' || Q.ORIGINS.indexOf(rec.origin) < 0) return null;
    const note = typeof rec.note === 'string' ? rec.note : '';
    const auto = typeof rec.auto === 'boolean' ? rec.auto : (!note || note === SCRATCH_NOTE || /^tweak of /.test(note));
    const out = { origin: rec.origin, auto };
    for (const k of ['by', 'source', 'licence']) if (typeof rec[k] === 'string' && rec[k]) out[k] = rec[k];
    if (note && !auto) out.note = note;
    if (rec.scratch === true || note === SCRATCH_NOTE) out.scratch = true;
    return out;
  };
  // what this editor exported before for a model (this browser's log, else Quark\model-editor\provenance.json at build)
  Q.knownProv = key => Q.provLog[Q.provId(key)] || (Q.exportedProv || {})[Q.provId(key)] || null;
  Q.originOf = function (key) {   // the library's badge
    return Q.provFor(key).origin;
  };
  // provenance.json: merged with `existing` (its other entries kept), one entry per id, the latest export wins
  // (removeKeys: models whose file was moved to .bak: their entries go)
  Q.mergeProvenance = function (existingText, keys, removeKeys) {
    let doc = null;
    try { doc = existingText ? JSON.parse(existingText) : null; } catch (e) { doc = null; }
    if (!doc || typeof doc !== 'object' || Array.isArray(doc)) doc = {};
    const out = { format: 1, tool: 'model-editor', items: {} };
    for (const [k, v] of Object.entries(doc)) if (k !== 'format' && k !== 'tool' && k !== 'items') out[k] = v;   // (unknown fields kept)
    Object.assign(out.items, doc.items && typeof doc.items === 'object' ? doc.items : {});
    for (const [id, v] of Object.entries(Q.provLog)) if (!keys || keys.indexOf(id) < 0) { if (!out.items[id]) out.items[id] = v; }
    for (const key of removeKeys || []) delete out.items[Q.provId(key)];
    for (const key of keys || []) out.items[Q.provId(key)] = Q.provFor(key);
    return JSON.stringify(out, null, 2) + '\n';
  };
  Q.recordProvenance = function (keys, removeKeys) {
    for (const k of removeKeys || []) delete Q.provLog[Q.provId(k)];
    for (const k of keys) Q.provLog[Q.provId(k)] = Q.provFor(k);
    saveLog();
  };

  // --- zip (stored) ----------------------------------------------------------------------------------------------------
  const crcTable = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
  Q.crc32 = function (u8) { let c = 0xffffffff; for (let i = 0; i < u8.length; i++) c = crcTable[(c ^ u8[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  Q.zip = function (files) {
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

  // every model file to write; problems (over the triangle limit, empty) are skipped and reported
  Q.buildExportFiles = function (keys) {
    keys = keys || Q.exportList();
    const files = [], skipped = [];
    for (const k of keys) {
      const p = Q.exportError(k);
      if (p) { skipped.push({ key: k, why: p.text }); continue; }
      files.push({ key: k, name: Q.exportPath(k), data: Q.gltf.exportBytes(k) });
    }
    return { files, skipped };
  };
  Q.exportZip = function (keys) {
    const r = Q.buildExportFiles(keys);
    const done = r.files.map(f => f.key);
    const prov = Q.mergeProvenance('', done);
    const files = r.files.map(f => ({ name: f.name, data: f.data })).concat([{ name: 'provenance.json', data: new TextEncoder().encode(prov) }]);
    const blob = Q.zip(files);
    Q.lastZip = blob;
    Q.download(blob, 'quark-model-overrides.zip');
    Q.recordProvenance(done);
    return Object.assign(r, { provenance: prov });
  };
  Q.downloadProvenance = function () {   // (the models an export would write: not those its errors block)
    const text = Q.mergeProvenance('', Q.exportableList());
    Q.download(new Blob([text], { type: 'application/json' }), 'provenance.json');
    return text;
  };

  // --- folders (File System Access API) ------------------------------------------------------------------------------
  Q.canPickFolder = () => typeof window.showDirectoryPicker === 'function';
  async function childDir(dir, name) { try { return await dir.getDirectoryHandle(name); } catch (e) { return null; } }
  // the folder picked may be the overrides folder itself, the data folder (...\quark) or the repository
  Q.resolveOverrides = async function (dir) {
    if (dir.name.toLowerCase() === 'overrides') return { dir, where: dir.name };
    const ov = await childDir(dir, 'overrides');
    if (ov) return { dir: ov, where: dir.name + '/overrides' };
    const assets = await childDir(dir, 'assets');
    if (assets) { const o2 = await childDir(assets, 'overrides'); if (o2) return { dir: o2, where: dir.name + '/assets/overrides' }; }
    return { dir, where: dir.name, guess: true };
  };
  async function dirAt(root, parts, create) {
    let d = root;
    for (const p of parts) { d = create ? await d.getDirectoryHandle(p, { create: true }) : await childDir(d, p); if (!d) return null; }
    return d;
  }
  async function writeFile(dir, name, data) { const fh = await dir.getFileHandle(name, { create: true }), w = await fh.createWritable(); await w.write(data); await w.close(); }
  async function moveToBak(dir, name, h) {
    await writeFile(dir, name + '.bak', await (await h.getFile()).arrayBuffer());
    await dir.removeEntry(name);
  }
  // model files already in a folder: [{ key, name, dir, handle }]
  Q.scanModels = async function (ovDir) {
    const out = [], models = await childDir(ovDir, 'models');
    if (!models) return out;
    for await (const [kind, kh] of models.entries()) {
      if (kh.kind !== 'directory') continue;
      for await (const [n, h] of kh.entries()) {
        const m = n.match(/^(.*)\.(glb|gltf)$/i);
        if (h.kind !== 'file' || !m) continue;
        const key = Q.order.find(k => k.toLowerCase() === ('models/' + kind + '/' + m[1]).toLowerCase()) || ('models/' + kind + '/' + m[1]);
        out.push({ key, name: n, ext: m[2].toLowerCase(), dir: kh, handle: h });
      }
    }
    return out;
  };
  // write the edited models; a competing same-named .gltf (the game takes the .glb first, but it confuses) and the
  // files of models reset to their default (opts.removeKeys) are moved to .bak
  Q.exportToFolder = async function (root, opts) {
    opts = opts || {};
    const res = await Q.resolveOverrides(root), dir = res.dir;
    const r = Q.buildExportFiles(opts.keys);
    const report = { where: res.where, guess: !!res.guess, written: [], moved: [], skipped: r.skipped };
    for (const f of r.files) {
      const parts = f.name.split('/'), file = parts.pop(), d = await dirAt(dir, parts, true);
      const stem = file.replace(/\.[^.]+$/, '').toLowerCase();
      for await (const [n, h] of d.entries()) {
        if (h.kind !== 'file' || n === file) continue;
        const m = n.match(/^(.*)\.(glb|gltf)$/i);
        if (m && m[1].toLowerCase() === stem) { await moveToBak(d, n, h); report.moved.push(parts.concat(n).join('/')); }
      }
      await writeFile(d, file, f.data);
      report.written.push(f.name);
    }
    for (const key of opts.removeKeys || []) {
      const parts = key.split('/'), stem = parts.pop().toLowerCase(), d = await dirAt(dir, parts, false);
      if (!d) continue;
      for await (const [n, h] of d.entries()) {
        const m = n.match(/^(.*)\.(glb|gltf)$/i);
        if (h.kind === 'file' && m && m[1].toLowerCase() === stem) { await moveToBak(d, n, h); report.moved.push(parts.concat(n).join('/')); }
      }
    }
    const done = r.files.map(f => f.key), gone = (opts.removeKeys || []).filter(k => done.indexOf(k) < 0);
    Q.recordProvenance(done, gone);
    // the sidecar: into the model editor's folder (the export dialog asks for it and its access first)
    report.provenance = null;
    if (opts.provDir) {
      try { report.provenance = await Q.writeProvenance(opts.provDir, done, gone); } catch (e) { report.provenanceError = String(e.message || e); }
    }
    return report;
  };
  // provenance.json in the Quark\model-editor folder, merged with the file there (what the Editor Manager's dashboard
  // and tools/provenance.py read): its path, or throws. No permission prompt here: that needs a click of its own (the
  // export dialog's), and this runs after the long awaits of a write
  Q.writeProvenance = async function (pd, done, gone) {
    if (!(await Q.folderPermission(pd, false))) throw new Error('no access to ' + pd.name + ' (allow it in the export dialog)');
    let existing = '';
    try { existing = await (await (await pd.getFileHandle('provenance.json')).getFile()).text(); } catch (e) { }
    await writeFile(pd, 'provenance.json', new TextEncoder().encode(Q.mergeProvenance(existing, done, gone)));
    return pd.name + '/provenance.json';
  };
  // read / write access to a remembered folder; ask: show the browser's prompt (only from a click: it needs a user gesture)
  Q.folderPermission = async function (h, ask) {
    try {
      if (!h.queryPermission) return true;
      if ((await h.queryPermission({ mode: 'readwrite' })) === 'granted') return true;
      return ask !== false && (await h.requestPermission({ mode: 'readwrite' })) === 'granted';
    } catch (e) { return false; }
  };
  // pick the Quark\model-editor folder (remembered): { dir, warn } (warn: it has neither model-editor.html nor
  // provenance.json, so it may be the wrong folder), or null when the picker is cancelled
  Q.pickProvDir = async function () {
    let dir;
    try { dir = await window.showDirectoryPicker({ id: 'quark-model-editor', mode: 'readwrite' }); } catch (e) { return null; }
    let mine = false;
    for (const n of ['model-editor.html', 'provenance.json']) { try { await dir.getFileHandle(n); mine = true; break; } catch (e) { } }
    await Q.saveHandle('provDir', dir);
    return { dir, warn: mine ? '' : dir.name + ' has no model-editor.html: is it the Quark\\model-editor folder?' };
  };
  async function readJson(dir, parts) {
    try {
      let d = dir;
      for (const p of parts.slice(0, -1)) { d = await childDir(d, p); if (!d) return null; }
      return JSON.parse(await (await (await d.getFileHandle(parts[parts.length - 1])).getFile()).text());
    } catch (e) { return null; }
  }
  // the provenance entries that describe the files of an overrides folder, most specific last: the repository's
  // assets/provenance/*.json (the folder is a checkout's assets/overrides), the model editor folder's provenance.json
  // (a folder export writes there), the folder's own provenance.json (a zip unzipped there)
  Q.folderProvenance = async function (root, res, provDir) {
    const items = {}, add = doc => { if (doc && doc.items && typeof doc.items === 'object') for (const [id, v] of Object.entries(doc.items)) if (v && typeof v === 'object') items[id.toLowerCase()] = v; };
    if (/\/assets\/overrides$/.test(res.where)) {
      const prov = await childDir(await childDir(root, 'assets') || root, 'provenance');
      if (prov) for await (const [n, h] of prov.entries()) if (h.kind === 'file' && /\.json$/i.test(n)) add(await readJson(prov, [n]));
    }
    if (provDir) {
      let ok = false;
      try { ok = !provDir.queryPermission || (await provDir.queryPermission({ mode: 'read' })) === 'granted' || (await provDir.requestPermission({ mode: 'read' })) === 'granted'; } catch (e) { }
      if (ok) add(await readJson(provDir, ['provenance.json']));
    }
    add(await readJson(res.dir, ['provenance.json']));
    return items;
  };
  // load every model override in a folder as edits (to continue work, or to see what the game will draw). Each keeps
  // its origin: the provenance in the file (this editor writes it into every .glb), else the folder's provenance
  // entries, else this editor's own export log; a file with none is taken as what it started from (Q.provFor)
  Q.loadFromFolder = async function (root, opts) {
    const res = await Q.resolveOverrides(root), list = await Q.scanModels(res.dir), loaded = [], failed = [];
    const sidecar = await Q.folderProvenance(root, res, opts && opts.provDir);
    for (const f of list) {
      if (!Q.models[f.key]) { failed.push({ name: f.key, why: 'no such model in this export' }); continue; }
      if (f.ext === 'gltf' && list.some(o => o.key === f.key && o.ext === 'glb')) continue;   // (the game takes the .glb)
      try {
        const bytes = new Uint8Array(await (await f.handle.getFile()).arrayBuffer());
        const files = {};
        if (f.ext === 'gltf') {   // external buffers next to it
          for await (const [n, h] of f.dir.entries()) if (h.kind === 'file' && /\.bin$/i.test(n)) files[n] = new Uint8Array(await (await h.getFile()).arrayBuffer());
        }
        Q.importInto(f.key, bytes, f.name, files, { provFrom: rec => rec || sidecar[Q.provId(f.key).toLowerCase()] || Q.knownProv(f.key) });
        loaded.push(f.key);
      } catch (e) { failed.push({ name: f.key, why: String(e.message || e) }); }
    }
    return { where: res.where, loaded, failed };
  };

  // --- import ---------------------------------------------------------------------------------------------------------
  // a .glb / .gltf replaces model `key`: as boxes when it is a box model (extras.quark version 2, what the game draws
  // as boxes) or exactly boxes as the exporter lays them out (an older export of this editor), else as a mesh. A box
  // list the game would refuse is refused here too (the game logs it and keeps the default). Its provenance replaces
  // the model's (one undo step with the boxes): opts.prov (the import dialog's), or opts.provFrom(the file's embedded
  // record) -> a record (loading a folder); with neither, Q.provFor's defaults apply.
  Q.importInto = function (key, bytes, name, files, opts) {
    const mesh = Q.gltf.parse(bytes, name, files);   // (throws: shown to the user)
    const parts = Q.boxes.partsOf(mesh, key, false);
    if (parts && parts.error) throw new Error('box model: ' + parts.error + ' (the game would keep the default)');
    Q.beginChange(key);
    if (parts) Q.state.edits[key] = { type: 'boxes', parts };
    else {
      // external buffers baked in: keep the bytes of a self-contained .glb of what the loader read
      const self = files && Object.keys(files).length ? Q.gltf.writeGlb(Q.gltf.meshForWrite(mesh), name, null) : bytes;
      const id = 'src' + Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
      Q.sources[id] = { name, bytes: self, mesh: files && Object.keys(files).length ? Q.gltf.parse(self, name) : mesh };
      Q.state.edits[key] = { type: 'mesh', src: id };
    }
    Q.state.prov = Q.state.prov || {};
    let prov = opts && opts.prov ? Object.assign({}, opts.prov) : null;
    if (!prov && opts && opts.provFrom) prov = Q.provState(opts.provFrom(mesh.provenance));
    if (prov && prov.origin) Q.state.prov[key] = prov; else delete Q.state.prov[key];
    Q.endChange(key, 'import');
    return parts ? 'boxes' : 'mesh';
  };

  // --- problems (warnings shown in the panel; errors block the export) ------------------------------------------------
  // a box list against the game's checks (gltf.cpp boxesFrom), as it will be written
  const inRange = (v, lo, hi) => Array.isArray(v) && v.length === 3 && v.every(x => Number.isFinite(x) && x >= lo && x <= hi);
  Q.boxErrors = function (key, parts) {
    const errs = [], ref = Q.refOf(key);
    if (!parts.length) errs.push('no boxes: nothing to export');
    if (parts.length > Q.MAX_BOXES) errs.push(parts.length + ' boxes: the game takes at most ' + Q.MAX_BOXES + ' in a box model');
    parts.forEach((p, i) => {
      const nm = 'box ' + (i + 1) + (p.name ? ' (' + p.name + ')' : '') + ': ';
      if (!inRange(p.c, -64, 64)) errs.push(nm + 'its centre must be within 64 m');
      if (!inRange(p.h, 0, 32) || !p.h.every(x => x > 0)) errs.push(nm + 'its half size must be over 0 and at most 32 m');
      if (!inRange(Q.boxes.shownColor(p, ref), 0, 16)) errs.push(nm + 'its colour must be 0 to 16');
      if ((p.slot | 0) && (!inRange(p.mul || [1, 1, 1], 0, 16) || !inRange(p.add || [0, 0, 0], 0, 16) || !inRange(p.teamMul || [0, 0, 0], 0, 16)))
        errs.push(nm + 'its mul / add / teamMul must be 0 to 16 (a colour far from the slot\'s reference: try another slot)');
      const a = p.alpha == null ? 1 : p.alpha;
      if (!(a > 0 && a <= 1)) errs.push(nm + 'alpha must be over 0 and at most 1');
    });
    return errs;
  };
  Q.problems = function (key) {
    const out = [], m = Q.models[key], cur = Q.current(key);
    if (!m) return out;
    let tris = 0, slots = new Set(), alphaLow = false;
    if (cur.type === 'boxes') {
      tris = cur.parts.length * 12;
      for (const p of cur.parts) { slots.add(p.slot | 0); if ((p.alpha == null ? 1 : p.alpha) < 1) alphaLow = true; }
      const errs = Q.boxErrors(key, cur.parts);
      if (errs.length) out.push({ level: 'error', text: errs.slice(0, 3).join('; ') + (errs.length > 3 ? ' (and ' + (errs.length - 3) + ' more)' : '') + ': the game refuses the file and keeps the default' });
    } else if (cur.type === 'mesh') {
      tris = cur.mesh.tris;
      for (const p of cur.mesh.prims) { slots.add(p.slot); if (p.alpha < 0.999) alphaLow = true; }
      if (Q.isEdited(key) || cur.shipped) out.push({ level: 'info', text: 'a mesh (not a box model): the game draws it lit and shadowed but without the 1 px outline, and squashes it whole when crouching' });
    }
    if (tris > Q.MAX_TRIS) out.push({ level: 'error', text: tris + ' triangles: the game refuses more than ' + Q.MAX_TRIS + ' (it keeps the default)' });
    else if (tris > Q.MAX_TRIS * 0.5) out.push({ level: 'warn', text: tris + ' triangles: over half the game\'s limit of ' + Q.MAX_TRIS + ' (they stream every frame)' });
    const driven = Q.drivenSlots(m.kind, m.name), und = [...slots].filter(s => s && driven.indexOf(s) < 0);
    if (und.length) out.push({ level: 'info', text: 'the game gives a ' + (m.kind === 'prop' ? m.name : m.kind) + ' no ' + und.map(s => Q.SLOT_NAMES[s]).join(' / ') +
      ' colour: those parts are drawn as authored (they follow nothing)' });
    // bounds against the default
    const b = Q.scene.bounds(key), d = m.defParts ? Q.scene.boundsOf(m.defParts) : (m.defMesh ? Q.gltf.bounds(m.defMesh) : null);
    if (b && d && Q.isEdited(key)) {
      const grow = [];
      for (let k = 0; k < 3; k++) {
        const over = Math.max(b.mx[k] - d.mx[k], d.mn[k] - b.mn[k]), size = d.mx[k] - d.mn[k];
        if (over > Math.max(0.05, size * 0.25)) grow.push('XYZ'[k] + ' by ' + Math.round(over * 100) + ' cm');
      }
      if (grow.length) out.push({ level: 'warn', text: 'much bigger than the default (' + grow.join(', ') + ')' + (m.kind === 'player' ? ': the hitbox stays 0.8 x 1.8 x 0.8 m, shots at what sticks out miss' : m.name === 'CART' ? ': the cart\'s collision box does not grow with it' : '') });
    }
    if (m.kind === 'player' && b) {
      const out2 = [];
      if (b.mx[0] > Q.HALF_W + 0.1 || b.mn[0] < -Q.HALF_W - 0.1 || b.mx[2] > Q.HALF_W + 0.1 || b.mn[2] < -Q.HALF_W - 0.1) out2.push('wider than the hitbox');
      if (b.mx[1] > 2.05) out2.push('taller than 2 m');
      if (b.mn[1] < -0.02) out2.push('below the feet');
      if (out2.length && Q.isEdited(key)) out.push({ level: 'info', text: out2.join(', ') + ' (hitboxes are not changed by models)' });
    }
    if (alphaLow && cur.type === 'boxes' && m.kind !== 'projectile') out.push({ level: 'info', text: 'translucent boxes: the game\'s box path draws them opaque outside projectiles' });
    if ((Q.isEdited(key) || cur.shipped) && cur.type === 'boxes' && S().hasPartAnim(key)) out.push({ level: 'info', text: 'as a box model the game draws this still: no flame flicker / core blink / wobble (it still spins and turns it)' });
    if (m.refMismatch) out.push({ level: 'warn', text: 'this page\'s reference colours differ from this export\'s (a newer game?): colours may be off' });
    return out;
  };
  const S = () => Q.scene;

  // --- project file / autosave --------------------------------------------------------------------------------------------
  Q.usedSources = function () { const s = new Set(); for (const e of Object.values(Q.state.edits)) if (e.src) s.add(e.src); return s; };
  Q.projectJSON = function () {
    const sources = {};
    for (const id of Q.usedSources()) { const s = Q.sources[id]; if (s) sources[id] = { name: s.name, file: Q.bytesToB64(s.bytes) }; }
    return JSON.stringify({ format: 'quark-model-editor', version: 1, game: Q.manifest && Q.manifest.version, saved: new Date().toISOString(), state: Q.state, sources });
  };
  Q.importProject = function (text) {
    const p = JSON.parse(text);
    if (p.format !== 'quark-model-editor') throw new Error('not a model editor project');
    for (const [id, s] of Object.entries(p.sources || {})) Q.sources[id] = { name: s.name, bytes: Q.b64ToBytes(s.file) };
    Q.state = Object.assign({ edits: {}, prov: {} }, p.state);
    Q.normalizeEdits(); Q.dropStaleProv();
    for (const k of Object.keys(Q.state.edits)) Q.bump(k);
    Q.clearHistory();
    return p;
  };
  function idb() {
    return new Promise((res, rej) => {
      const r = indexedDB.open('quark-model-editor', 1);
      r.onupgradeneeded = () => r.result.createObjectStore('kv');
      r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
    });
  }
  async function kvPut(k, v) {
    const db = await idb();
    await new Promise((res, rej) => { const tx = db.transaction('kv', 'readwrite'); tx.objectStore('kv').put(v, k); tx.oncomplete = res; tx.onerror = () => rej(tx.error); });
    db.close();
  }
  async function kvGet(k) {
    const db = await idb();
    const v = await new Promise((res, rej) => { const r = db.transaction('kv').objectStore('kv').get(k); r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); });
    db.close();
    return v;
  }
  Q.autosave = async function () {
    try {
      const sources = {};
      for (const id of Q.usedSources()) { const s = Q.sources[id]; if (s) sources[id] = { name: s.name, bytes: s.bytes }; }
      await kvPut('autosave', { state: Q.state, sources, saved: Date.now() });
      return true;
    } catch (e) { return false; }
  };
  Q.loadAutosave = async function () { try { return (await kvGet('autosave')) || null; } catch (e) { return null; } };
  Q.clearAutosave = async function () { try { await kvPut('autosave', null); } catch (e) { } };
  // folder handles remembered between sessions (Chrome / Edge can store them in IndexedDB)
  Q.saveHandle = async (name, h) => { try { await kvPut('handle.' + name, h); } catch (e) { } };
  Q.loadHandle = async name => { try { return (await kvGet('handle.' + name)) || null; } catch (e) { return null; } };
})();
