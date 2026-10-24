/* ===== PROVENANCE ===================================================================================
   Who made each exported sound (docs: the editor-dashboard PROVENANCE spec). Every export writes or updates
   provenance.json in the editor's own folder (where sound-editor.html is):
     {"format": 1, "tool": "sound-editor", "items": {"sounds/<kit>/<SOUND>.wav": {"origin", "by", "date", "tool", "note", "source"}}}
   One entry per id, the latest export wins, every other entry (and any field this editor doesn't set) is kept.
   Honesty rule: "human" only when a person really made the audio.
     - a dropped / uploaded file (or an empty missing-sound slot): no origin until the owner picks one (the export waits)
     - a sound made only by editing a shipped one (trim, fades, filters, levelling): the shipped sound's origin + a note
     - the editor's generator (the owner designs it by hand): human, "made in the sound editor's generator" */
(function () {
  'use strict';
  const Q = window.Q;
  const P = Q.prov = {};
  P.BY = 'q';
  P.TOOL = 'sound-editor';
  P.FILE = 'provenance.json';
  P.ORIGINS = ['human', 'third-party', 'ai'];
  P.LABEL = { human: 'human-made', 'third-party': 'third-party', ai: 'AI-made' };

  // --- the shipped sounds' origins (assets/sfx/CREDITS.md, tools/import_sfx.py, tools/import_voice.py, make_voice.py) ---
  // DEFAULT and RETRO are synthesized in src/client/audio.cpp; HEAVY METAL, MAGICAL and MOUTH mix Kenney CC0 clips, except
  // SUPER / HYPER / LOCATOR (sound ids 30-32), which are DEFAULT's synthesized sounds in every kit.
  const DEFAULT_IN_EVERY_KIT = ['SUPER', 'HYPER', 'LOCATOR'];
  // the lines each voice has a recording of (tools/import_voice.py: ALL + MORE); the rest are Piper text to speech
  // (MALE / FEMALE, tools/make_voice.py) or silent (the other voices)
  const RECORDED = ['THREE', 'TWO', 'ONE', 'GO', 'YOU_DIED', 'HEADSHOT', 'AIR_ROCKET', 'SPREE', 'RAMPAGE', 'GODLIKE', 'UNSTOPPABLE',
    'ONE_MINUTE', 'THIRTY_SECONDS', 'YOU_WIN', 'YOU_LOSE', 'ROUND_OVER', 'BOMB_PLANTED', 'BOMB_DEFUSED', 'HILL_TAKEN', 'HILL_LOST',
    'POINT_TAKEN', 'POINT_LOST', 'ROUND_WON', 'ROUND_LOST', 'OVERTIME', 'HIGH_SCORE', 'NEW_LEADER'];
  const VOICE_FOLDER = { old_man: 'oldman', e_girl: 'egirl' };

  // {origin, source, what} of the shipped sound at `key` (null: a new sound, nothing shipped)
  P.defaultOrigin = function (key) {
    const a = Q.assets[key];
    if (!a || a.custom) return null;
    if (a.shippedOverride) return { origin: 'human', source: 'the owner\'s file in assets/overrides', what: 'owner\'s own sound' };
    if (a.kind === 'voice') {
      if (RECORDED.includes(a.name)) return { origin: 'ai', source: 'recorded announcer line, assets/voice/' + (VOICE_FOLDER[a.pack] || a.pack) + '/ (tools/import_voice.py); its maker is not recorded in the repository', what: 'AI announcer line' };
      if (a.pack === 'male' || a.pack === 'female') return { origin: 'ai', source: 'Piper text to speech (tools/make_voice.py)', what: 'AI text-to-speech line' };
      return { origin: 'ai', source: 'no recording: exported as silence', what: 'silent AI placeholder' };
    }
    if (a.pack === 'default') return { origin: 'ai', source: 'synthesized in src/client/audio.cpp', what: 'AI default' };
    if (a.pack === 'retro') return { origin: 'ai', source: 'DEFAULT chiptuned, synthesized in src/client/audio.cpp', what: 'AI default (RETRO)' };
    if (a.sameAsDefault || DEFAULT_IN_EVERY_KIT.includes(a.name)) return { origin: 'ai', source: 'DEFAULT\'s sound, synthesized in src/client/audio.cpp', what: 'AI default' };
    return { origin: 'third-party', source: 'Kenney audio packs (CC0 1.0), mixed by tools/import_sfx.py (assets/sfx/CREDITS.md)', what: 'Kenney CC0 sound' };
  };

  // what an edit of a shipped sound did: "trim, high-pass, levelling"
  P.opsOf = function (e) {
    const ops = [];
    if (!e) return ops;
    if ((e.trim && (e.trim[0] > 0 || e.trim[1] != null))) ops.push('trim');
    if (e.fadeIn > 0 || e.fadeOut > 0) ops.push('fades');
    let lev = false;
    for (const st of e.chain || []) {
      if (!st.on) continue;
      if (st.group) { lev = true; continue; }
      const l = (Q.STEPS && Q.STEPS[st.type] ? Q.STEPS[st.type].label : st.type).toLowerCase();
      if (!ops.includes(l)) ops.push(l);
    }
    if (lev) ops.push('levelling');
    return ops;
  };

  // the kind of audio an edit starts from: 'gen' (the generator), 'file' (dropped / uploaded / an empty slot), 'shipped'
  P.audioKind = function (key) {
    const e = Q.state.edits[key], a = Q.assets[key];
    if (e && e.made === 'gen') return 'gen';
    if (e && e.src) return 'file';
    if (!a || a.custom) return 'file';
    return 'shipped';
  };

  /* The origin an export of `key` records: {origin (null = not chosen yet), note, source, choose (the owner picks it),
     why (shown in the editor)}. */
  P.originOf = function (key) {
    const e = Q.state.edits[key], kind = P.audioKind(key);
    if (kind === 'gen') return { origin: 'human', note: 'made in the sound editor\'s generator', source: '', choose: false, why: 'You designed it in the generator by hand: human-made.' };
    if (kind === 'file') {
      const o = (e && e.origin) || {}, src = e && e.src && Q.sources[e.src];
      const file = src ? 'file ' + src.name : 'an empty slot';
      return { origin: P.ORIGINS.includes(o.origin) ? o.origin : null, note: o.note || '', source: o.source || '', choose: true, file, fileName: src ? src.name : '',
        why: 'Audio from ' + file + ': say who made it. Human only if a person made this sound.' };
    }
    const d = P.defaultOrigin(key), ops = P.opsOf(e);
    return { origin: d.origin, note: (ops.length ? ops.join(', ') : 'unchanged re-export') + ' of the ' + d.what, source: d.source, choose: false,
      why: 'An edit of the shipped sound keeps its origin (' + d.origin + '): editing it doesn\'t make it a new sound. Drop your own file or use Design... for a new one.' };
  };
  // the owner's choice for a dropped file: {origin, note, source}; returns false when `key` has no choice to make
  P.setOrigin = function (key, origin, note, source) {
    const e = Q.state.edits[key];
    if (!e || P.audioKind(key) !== 'file') return false;
    if (!origin) { delete e.origin; return true; }
    e.origin = { origin, note: note == null ? (e.origin && e.origin.note) || '' : note, source: source == null ? (e.origin && e.origin.source) || '' : source };
    return true;
  };
  // ready to export: an origin, and for a third-party file where it came from (pack / site / licence)
  P.problem = function (key) {
    const o = P.originOf(key);
    if (!o.origin) return 'choose who made it';
    if (o.choose && o.origin === 'third-party' && !o.source.trim()) return 'say where it came from (pack / site / licence)';
    return null;
  };
  // exported keys still waiting for an origin (or a third-party source)
  P.missing = keys => (keys || Q.exportList()).filter(k => P.problem(k));

  P.today = function () { const d = new Date(), z = n => String(n).padStart(2, '0'); return d.getFullYear() + '-' + z(d.getMonth() + 1) + '-' + z(d.getDate()); };
  // the sidecar entries of an export: {id: entry}, ids = the export paths (sounds/<kit>/<SOUND>.wav)
  P.entries = function (keys) {
    const out = {}, date = P.today();
    for (const k of keys || Q.exportList()) {
      const o = P.originOf(k);
      if (!o.origin) continue;          // (the export dialog doesn't let this happen)
      const it = { origin: o.origin, by: P.BY, date, tool: P.TOOL };
      const note = [o.note, o.choose && o.fileName ? 'from ' + o.fileName : ''].filter(Boolean).join('; ');
      if (note) it.note = note;
      if (o.source) it.source = o.source;
      out[k + '.wav'] = it;
    }
    return out;
  };
  // merge into an existing sidecar (text or object): the exported ids are replaced (their fields this editor doesn't
  // set are kept), every other entry and top-level field stays. Ids sorted.
  const OURS = ['origin', 'by', 'date', 'tool', 'note', 'source', 'version', 'wip'];
  P.merge = function (existing, entries) {
    let base = existing;
    if (typeof base === 'string') { try { base = base.trim() ? JSON.parse(base) : {}; } catch (e) { throw new Error(P.FILE + ' there is not valid JSON (' + e.message + '): fix or remove it first'); } }
    base = base && typeof base === 'object' && !Array.isArray(base) ? base : {};
    const items = Object.assign({}, base.items && typeof base.items === 'object' ? base.items : {});
    for (const [id, it] of Object.entries(entries)) {
      const keep = {};
      for (const [f, v] of Object.entries(items[id] || {})) if (!OURS.includes(f)) keep[f] = v;
      items[id] = Object.assign({}, it, keep);
    }
    const sorted = {};
    for (const id of Object.keys(items).sort()) sorted[id] = items[id];
    return Object.assign({}, base, { format: 1, tool: P.TOOL, items: sorted });
  };
  P.text = obj => JSON.stringify(obj, null, 2) + '\n';

  // --- IndexedDB: the sound-editor folder's handle and the last sidecar written (any failure: just not remembered) ----
  function idb() {
    return new Promise((res, rej) => {
      const r = indexedDB.open('quark-sound-editor', 1);
      r.onupgradeneeded = () => r.result.createObjectStore('kv');
      r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
    });
  }
  async function kvGet(k) {
    try {
      const db = await idb();
      const v = await new Promise((res, rej) => { const r = db.transaction('kv').objectStore('kv').get(k); r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); });
      db.close(); return v == null ? null : v;
    } catch (e) { return null; }
  }
  async function kvPut(k, v) {
    try {
      const db = await idb();
      await new Promise((res, rej) => { const tx = db.transaction('kv', 'readwrite'); tx.objectStore('kv').put(v, k); tx.oncomplete = res; tx.onerror = () => rej(tx.error); });
      db.close(); return true;
    } catch (e) { return false; }
  }
  P.getDir = () => kvGet('provDir');
  P.setDir = h => kvPut('provDir', h);
  P.forgetDir = () => kvPut('provDir', null);
  // every entry this editor has exported (so a downloaded / zipped provenance.json is complete even without the folder)
  P.getCache = async () => (await kvGet('provCache')) || { format: 1, tool: P.TOOL, items: {} };
  P.setCache = obj => kvPut('provCache', obj);

  // --- the folder ----------------------------------------------------------------------------------------------------
  P.canPick = () => typeof window.showDirectoryPicker === 'function';
  P.permission = async function (dir, ask) {
    try {
      const o = { mode: 'readwrite' };
      if (dir.queryPermission && await dir.queryPermission(o) === 'granted') return 'granted';
      if (!ask) return dir.queryPermission ? await dir.queryPermission(o) : 'granted';
      return dir.requestPermission ? await dir.requestPermission(o) : 'granted';
    } catch (e) { return 'denied'; }
  };
  P.looksRight = async function (dir) {   // the folder sound-editor.html is in
    try { await dir.getFileHandle('sound-editor.html'); return true; } catch (e) { return false; }
  };
  async function readText(dir, name) {
    try { return await (await (await dir.getFileHandle(name)).getFile()).text(); } catch (e) { return null; }
  }
  // read, merge and write provenance.json in `dir`; returns {count, total, obj}
  P.writeTo = async function (dir, entries) {
    const existing = await readText(dir, P.FILE);
    const obj = P.merge(existing || '', entries);
    const fh = await dir.getFileHandle(P.FILE, { create: true }), w = await fh.createWritable();
    await w.write(P.text(obj)); await w.close();
    await P.setCache(P.merge(await P.getCache(), obj.items));
    return { count: Object.keys(entries).length, total: Object.keys(obj.items).length, obj };
  };
  // the sidecar to download / zip: everything this editor has exported, with these entries on top
  P.offline = async function (entries) {
    let base = await P.getCache();
    const dir = await P.getDir();
    if (dir && await P.permission(dir, false) === 'granted') { const t = await readText(dir, P.FILE); if (t) { try { base = P.merge(base, JSON.parse(t).items || {}); } catch (e) { } } }
    const obj = P.merge(base, entries);
    await P.setCache(obj);
    return obj;
  };
})();
