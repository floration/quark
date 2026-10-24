// Quark UI customizer - font trials: the fonts embedded by build.py (fonts/catalog.json) plus fonts the owner uploads
// (.ttf .otf .woff .woff2, kept in IndexedDB). Reads a font's name table (licence IDs 13/14), OS/2 cap height and
// variation axes so every font can be drawn with its cap height matched to the game's UI font (Rubik) at the same
// textScale, as the game sizes the fonts of Settings > FONT (tools/make_font_atlas.py).
// The parser has no DOM: node tests it (test/fonts.test.js).
'use strict';

const QFonts = (() => {
  const REF_CAP = 0.7;   // Rubik: OS/2 sCapHeight 700 / 1000 units per em (the game's font; catalog.refCapRatio wins)
  let refCap = REF_CAP;

  // ---- parsing ------------------------------------------------------------------------------------------------
  const u8 = (b) => (b instanceof Uint8Array ? b : new Uint8Array(b));
  const tag4 = (d, o) => String.fromCharCode(d.getUint8(o), d.getUint8(o + 1), d.getUint8(o + 2), d.getUint8(o + 3));
  function kindOf(buf) {
    const b = u8(buf);
    if (b.length < 12) return null;
    const t = String.fromCharCode(b[0], b[1], b[2], b[3]);
    if (t === 'wOFF') return 'woff';
    if (t === 'wOF2') return 'woff2';
    if (t === 'OTTO') return 'otf';
    if ((b[0] === 0 && b[1] === 1 && b[2] === 0 && b[3] === 0) || t === 'true') return 'ttf';
    if (t === 'ttcf') return 'ttc';
    return null;
  }
  // {tag: DataView} of the tables an sfnt (ttf / otf) has
  function sfntTables(buf) {
    const b = u8(buf), d = new DataView(b.buffer, b.byteOffset, b.byteLength);
    const out = {};
    const n = d.getUint16(4);
    for (let i = 0; i < n; i++) {
      const r = 12 + 16 * i, off = d.getUint32(r + 8), len = d.getUint32(r + 12);
      if (off + len <= b.byteLength) out[tag4(d, r)] = new DataView(b.buffer, b.byteOffset + off, len);
    }
    return out;
  }
  // The same for WOFF 1 (zlib tables, unpacked with the browser's DecompressionStream); only the tables asked for.
  async function woffTables(buf, want) {
    const b = u8(buf), d = new DataView(b.buffer, b.byteOffset, b.byteLength);
    const out = {};
    const n = d.getUint16(12);
    for (let i = 0; i < n; i++) {
      const r = 44 + 20 * i, t = tag4(d, r);
      if (!want.includes(t)) continue;
      const off = d.getUint32(r + 4), comp = d.getUint32(r + 8), orig = d.getUint32(r + 12);
      let data = b.subarray(off, off + comp);
      if (comp < orig) {
        if (typeof DecompressionStream === 'undefined') continue;
        const ds = new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate'));
        data = new Uint8Array(await new Response(ds).arrayBuffer());
      }
      out[t] = new DataView(data.buffer, data.byteOffset, data.byteLength);
    }
    return out;
  }
  function names(t) {
    const out = {};
    const d = t.name;
    if (!d) return out;
    const cnt = d.getUint16(2), so = d.getUint16(4);
    for (let i = 0; i < cnt; i++) {
      const r = 6 + 12 * i;
      const p = d.getUint16(r), l = d.getUint16(r + 4), id = d.getUint16(r + 6), len = d.getUint16(r + 8), o = so + d.getUint16(r + 10);
      if (o + len > d.byteLength) continue;
      let s = '';
      if (p === 3 && l === 0x409) {   // Windows, English: UTF-16BE (wins)
        for (let k = 0; k + 1 < len; k += 2) s += String.fromCharCode(d.getUint16(o + k));
        out[id] = s;
      } else if ((p === 1 && l === 0) && out[id] === undefined) {
        for (let k = 0; k < len; k++) s += String.fromCharCode(d.getUint8(o + k));
        out[id] = s;
      } else if (p === 0 && out[id] === undefined) {   // Unicode platform: UTF-16BE
        for (let k = 0; k + 1 < len; k += 2) s += String.fromCharCode(d.getUint16(o + k));
        out[id] = s;
      }
    }
    return out;
  }
  // 'open' (OFL / Apache / Ubuntu: can ship), 'preview' (Microsoft: preview only) or 'unknown'. Same rules as build.py.
  function licenceOf(text, url) {
    const low = ((text || '') + ' ' + (url || '')).toLowerCase();
    if (low.includes('open font license') || low.includes('scripts.sil.org/ofl') || low.includes('openfontlicense')) return { status: 'open', summary: 'SIL Open Font License 1.1: can ship with the game (ship its licence text next to it)' };
    if (low.includes('apache license')) return { status: 'open', summary: 'Apache License 2.0: can ship with the game' };
    if (low.includes('ubuntu font licence')) return { status: 'open', summary: 'Ubuntu Font Licence: can ship with the game' };
    if (low.includes('bitstream vera fonts copyright')) return { status: 'open', summary: 'Bitstream Vera licence (DejaVu): can ship with the game' };
    if (low.includes('microsoft')) return { status: 'preview', summary: 'Microsoft-licensed Windows font: preview only, cannot ship with the game' };
    return { status: 'unknown', summary: 'no licence stated in the font: check before shipping' };
  }
  function infoFromTables(t) {
    const n = names(t);
    const info = { family: (n[16] || n[1] || '').trim(), subfamily: (n[17] || n[2] || '').trim(), fullName: (n[4] || '').trim(),
      copyright: (n[0] || '').trim(), licenceText: (n[13] || '').trim(), licenceUrl: (n[14] || '').trim(), axes: [] };
    Object.assign(info, (({ status, summary }) => ({ licence: status, licenceSummary: summary }))(licenceOf(info.licenceText, info.licenceUrl)));
    if (t.head) info.upem = t.head.getUint16(18);
    if (t['OS/2']) {
      const o = t['OS/2'];
      info.weightClass = o.getUint16(4);
      info.fsType = o.getUint16(8);
      if (o.getUint16(0) >= 2 && o.byteLength >= 90) info.capHeight = o.getInt16(88);
    }
    if (info.capHeight && info.upem) { info.capRatio = Math.round(info.capHeight / info.upem * 10000) / 10000; info.capFrom = 'OS/2 sCapHeight'; }
    if (t.fvar) {
      const f = t.fvar, ao = f.getUint16(4), ac = f.getUint16(8), as = f.getUint16(10);
      for (let i = 0; i < ac; i++) {
        const a = ao + i * as;
        const fx = (k) => Math.round(f.getInt32(a + 4 + 4 * k) / 65536 * 1000) / 1000;
        info.axes.push({ tag: tag4(f, a), min: fx(0), default: fx(1), max: fx(2) });
      }
    }
    return info;
  }
  // Everything a font file tells about itself; {kind} only for woff2 / ttc / unreadable files.
  async function parse(buf) {
    const kind = kindOf(buf);
    if (kind === 'ttf' || kind === 'otf') { try { return Object.assign({ kind }, infoFromTables(sfntTables(buf))); } catch (e) { return { kind, error: String(e) }; } }
    if (kind === 'woff') { try { return Object.assign({ kind }, infoFromTables(await woffTables(buf, ['name', 'OS/2', 'head', 'fvar']))); } catch (e) { return { kind, error: String(e) }; } }
    return { kind, axes: [] };
  }
  function parseSync(buf) { const k = kindOf(buf); return k === 'ttf' || k === 'otf' ? Object.assign({ kind: k }, infoFromTables(sfntTables(buf))) : { kind: k, axes: [] }; }

  // ---- registry -----------------------------------------------------------------------------------------------
  // entry: {id, name, family (CSS), file, files (one per static weight), weight, weights, stretch, letterSpacing, upper,
  // sizeK, info, uploaded, why, group}. stretch: a font-stretch keyword (set on the canvas) or a percentage such as
  // "85%": canvas fonts take no percentage and no font-variation-settings, so that width gets its own FontFace whose
  // stretch descriptor is pinned to it (the browser clamps the wdth axis into the descriptor's range).
  const entries = new Map();
  const faces = new Map();   // face key (files + pinned width) -> Promise of the loaded FontFace family
  const fileData = {};       // file key -> base64 (embedded) or ArrayBuffer (uploaded)
  const fileInfo = {};
  const listeners = [];
  const onChange = (fn) => listeners.push(fn);
  const emit = () => { for (const fn of listeners) try { fn(); } catch (e) { console.warn(e); } };

  const b64ToBuf = (s) => { const bin = atob(s); const b = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) b[i] = bin.charCodeAt(i); return b.buffer; };
  function bufToB64(buf) { const b = u8(buf); let s = ''; for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode.apply(null, b.subarray(i, i + 0x8000)); return btoa(s); }

  // opts.ns: the storage namespace (a build made with build.py --layout uses its own, so two builds don't share uploads)
  function init(catalog, files, opts) {
    catalog = catalog || {};
    if (opts && opts.ns) { DB = String(opts.ns); LS_PREFIX = DB + '/font/'; }
    refCap = Number(catalog.refCapRatio) || REF_CAP;
    Object.assign(fileInfo, catalog.files || {});
    Object.assign(fileData, files || {});
    for (const f of catalog.fonts || []) {
      const info = fileInfo[f.file] || {};
      entries.set(f.id, {
        id: f.id, name: f.name, file: f.file, files: f.files || [f.file], group: f.group || '', family: info.builtin ? 'QuarkUI' : null, weight: f.weight, weights: f.weights || [f.weight],
        stretch: f.stretch || '', letterSpacing: f.letterSpacing || 0, upper: !!f.upper, sizeK: f.sizeK || null, info, why: f.why || '',
        builtin: !!info.builtin, uploaded: false,
      });
    }
    if (!entries.size) {   // (no catalogue: the game's font only)
      entries.set('rubik', { id: 'rubik', name: 'Rubik', family: 'QuarkUI', weight: 700, weights: [700], stretch: '', letterSpacing: 0, upper: false,
        sizeK: 1, info: { family: 'Rubik', licence: 'open', licenceSummary: 'SIL Open Font License 1.1', fileName: 'Rubik.ttf' }, builtin: true, why: "the game's font today" });
    }
    const b = [...entries.values()].find((e) => e.builtin);
    defaultId = b ? b.id : [...entries.keys()][0];
  }
  let defaultId = 'rubik';   // (the entry of the game's own font: init sets it from the catalogue)
  const get = (id) => entries.get(id) || null;
  const list = () => [...entries.values()];

  function measureCap(family, weight, stretch) {
    try {
      const c = document.createElement('canvas').getContext('2d');
      c.font = `${weight} 200px ${family}`;
      if (stretch) c.fontStretch = stretch;
      const m = c.measureText('H');
      const r = m.actualBoundingBoxAscent / 200;
      return r > 0.2 && r < 1.2 ? Math.round(r * 10000) / 10000 : null;
    } catch (e) { return null; }
  }
  const KEYWORDS = ['ultra-condensed', 'extra-condensed', 'condensed', 'semi-condensed', 'normal', 'semi-expanded', 'expanded', 'extra-expanded', 'ultra-expanded'];
  const pctOf = (s) => { const m = /^\s*(\d+(?:\.\d+)?)%\s*$/.exec(String(s || '')); return m ? Number(m[1]) : null; };
  // What the canvas's fontStretch gets for an entry's width: keywords only ('' = normal; a percentage is in the face).
  const canvasStretch = (s) => (KEYWORDS.includes(s) ? s : '');
  // One CSS family for an entry's file(s): a variable file covers its wght range, static files one weight each.
  function faceFor(e) {
    const keys = e.files && e.files.length ? e.files : [e.file];
    const pct = pctOf(e.stretch);
    const fk = keys.join('+') + (pct != null ? '@' + pct : '');
    if (faces.has(fk)) return faces.get(fk);
    const fam = 'QF_' + fk.replace(/[^A-Za-z0-9_]/g, '_');
    const p = (async () => {
      for (const key of keys) {
        const info = fileInfo[key] || {};
        const src = fileData[key];
        if (src == null) throw new Error('no data for font ' + key);
        const buf = typeof src === 'string' ? b64ToBuf(src) : src;
        const wght = (info.axes || []).find((a) => a.tag === 'wght'), wdth = (info.axes || []).find((a) => a.tag === 'wdth');
        const desc = { weight: wght ? `${wght.min} ${wght.max}` : keys.length > 1 && info.weightClass ? String(info.weightClass) : '1 1000', style: 'normal' };
        if (wdth) desc.stretch = pct != null ? `${Math.max(wdth.min, Math.min(wdth.max, pct))}%` : `${wdth.min}% ${wdth.max}%`;
        const face = new FontFace(fam, buf.slice(0), desc);   // (a copy: the upload's buffer is saved too)
        await face.load();
        document.fonts.add(face);
      }
      return fam;
    })();
    faces.set(fk, p);
    return p;
  }
  // Loads a font (once); resolves with its entry, its family set and its size correction known.
  async function ensure(id) {
    const e = get(id);
    if (!e) throw new Error('unknown font ' + id);
    if (!e.family) e.family = await faceFor(e);
    else if (e.builtin && document.fonts) { try { await document.fonts.load(`${e.weight} 15px ${e.family}`); } catch (x) { /* fallback font */ } }
    if (!e.sizeK) {
      const r = (e.info && e.info.capRatio) || measureCap(e.family, e.weight, canvasStretch(e.stretch));
      if (r) { if (e.info && !e.info.capRatio) { e.info.capRatio = r; e.info.capFrom = 'measured H on a canvas'; } e.sizeK = Math.round(refCap / r * 10000) / 10000; } else e.sizeK = 1;
    }
    return e;
  }
  const ensureAll = () => Promise.all(list().map((e) => ensure(e.id).catch((x) => { console.warn('font', e.id, x); return null; })));

  // ---- uploads (IndexedDB, else localStorage) ---------------------------------------------------------------------
  const STORE = 'fonts';
  let DB = 'quark-ui-customizer', LS_PREFIX = 'quark-ui-customizer/font/';   // (init's opts.ns changes both)
  function idb() {
    return new Promise((res, rej) => {
      if (typeof indexedDB === 'undefined') return rej(new Error('no IndexedDB'));
      let r;
      try { r = indexedDB.open(DB, 1); } catch (e) { return rej(e); }
      r.onupgradeneeded = () => { if (!r.result.objectStoreNames.contains(STORE)) r.result.createObjectStore(STORE, { keyPath: 'id' }); };
      r.onsuccess = () => res(r.result);
      r.onerror = () => rej(r.error);
      r.onblocked = () => rej(new Error('IndexedDB blocked'));
    });
  }
  async function idbDo(mode, fn) {
    const db = await idb();
    return new Promise((res, rej) => {
      const tx = db.transaction(STORE, mode), st = tx.objectStore(STORE);
      const req = fn(st);
      tx.oncomplete = () => { db.close(); res(req && req.result); };
      tx.onerror = () => { db.close(); rej(tx.error); };
      tx.onabort = () => { db.close(); rej(tx.error || new Error('aborted')); };
    });
  }
  async function storePut(rec) {
    try { await idbDo('readwrite', (st) => st.put(rec)); return 'IndexedDB'; } catch (e) {
      try { localStorage.setItem(LS_PREFIX + rec.id, JSON.stringify(Object.assign({}, rec, { buf: undefined, b64: bufToB64(rec.buf) }))); return 'localStorage'; } catch (e2) { return null; }
    }
  }
  async function storeAll() {
    let out = [];
    try { out = (await idbDo('readonly', (st) => st.getAll())) || []; } catch (e) { /* none */ }
    try {
      for (let i = 0; i < localStorage.length; i++) {
        const k = localStorage.key(i);
        if (!k || !k.startsWith(LS_PREFIX)) continue;
        const r = JSON.parse(localStorage.getItem(k));
        if (r && r.b64 && !out.some((x) => x.id === r.id)) out.push(Object.assign(r, { buf: b64ToBuf(r.b64) }));
      }
    } catch (e) { /* none */ }
    return out;
  }
  async function storeDel(id) {
    try { await idbDo('readwrite', (st) => st.delete(id)); } catch (e) { /* none */ }
    try { localStorage.removeItem(LS_PREFIX + id); } catch (e) { /* none */ }
  }
  function hashBuf(buf) {
    const b = u8(buf);
    let h = 2166136261 >>> 0;
    const step = Math.max(1, Math.floor(b.length / 65536));
    for (let i = 0; i < b.length; i += step) h = Math.imul(h ^ b[i], 16777619) >>> 0;
    return (h ^ b.length).toString(36);
  }
  async function register(rec) {
    const info = Object.assign({ fileName: rec.fileName, bytes: u8(rec.buf).length, uploaded: true }, await parse(rec.buf));
    if (info.kind === 'ttc') throw new Error('font collections (.ttc) are not supported: use a single .ttf / .otf');
    if (!info.kind) throw new Error('not a font file (.ttf .otf .woff .woff2)');
    const key = rec.id;
    fileInfo[key] = info;
    fileData[key] = rec.buf;
    const wght = info.axes.find((a) => a.tag === 'wght');
    let weights, weight;
    if (wght) {
      weights = [];
      for (let w = Math.ceil(wght.min / 100) * 100; w <= wght.max; w += 100) weights.push(w);
      if (!weights.length) weights = [Math.round(wght.default)];
      weight = weights.includes(700) ? 700 : weights[weights.length - 1];
    } else { weight = info.weightClass || 400; weights = [weight]; }
    const base = info.fullName || [info.family, info.subfamily].filter(Boolean).join(' ') || rec.fileName.replace(/\.[^.]+$/, '');
    const e = { id: key, name: base, file: key, files: [key], group: '', family: null, weight, weights, stretch: '', letterSpacing: 0, upper: false, sizeK: null, info,
      why: 'uploaded (' + rec.fileName + ')', uploaded: true, builtin: false };
    entries.set(key, e);
    await ensure(key);
    return e;
  }
  // A dropped / picked file: registered, saved for the next visit; resolves with {entry, where}.
  async function addFile(file) {
    if (!/\.(ttf|otf|woff2?)$/i.test(file.name)) throw new Error(file.name + ': not a .ttf .otf .woff or .woff2 file');
    const buf = await file.arrayBuffer();
    const id = 'u-' + hashBuf(buf);
    const rec = { id, fileName: file.name, buf, addedAt: new Date().toISOString() };
    const e = entries.has(id) ? entries.get(id) : await register(rec);
    const where = await storePut(rec);
    emit();
    return { entry: e, where };
  }
  async function loadUploads() {
    const recs = await storeAll();
    let n = 0;
    for (const r of recs) { try { if (!entries.has(r.id)) { await register(r); n++; } } catch (e) { console.warn('stored font', r.fileName, e); } }
    if (n) emit();
    return n;
  }
  async function remove(id) {
    const e = get(id);
    if (!e || !e.uploaded) return false;
    entries.delete(id);
    await storeDel(id);
    emit();
    return true;
  }

  return { REF_CAP, get DEFAULT_ID() { return defaultId; }, kindOf, parse, parseSync, licenceOf, init, get, list, ensure, ensureAll, addFile, loadUploads, remove, onChange,
    measureCap, canvasStretch, refCap: () => refCap };
})();

if (typeof module !== 'undefined') module.exports = QFonts;
