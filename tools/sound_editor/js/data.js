/* ===== DATA =========================================================================================
   The game's sound tables (src/client/audio.h / audio.cpp, game.cpp), the asset registry filled by the data
   scripts (data/*.js call window.qseRegister), and the project state (edits, groups, sources). */
(function () {
  'use strict';
  const Q = window.Q = window.Q || {};
  Q.RATE = 48000;

  // Audio::kitDir order. The settings name the fifth kit MOUTH (audio.h's enum calls it kPackWooden).
  Q.KITS = [
    { dir: 'default', name: 'DEFAULT', color: '#3987e5', shape: 'circle' },
    { dir: 'heavy_metal', name: 'HEAVY METAL', color: '#d95926', shape: 'square' },
    { dir: 'magical', name: 'MAGICAL', color: '#199e70', shape: 'diamond' },
    { dir: 'retro', name: 'RETRO', color: '#c98500', shape: 'triangle' },
    { dir: 'mouth', name: 'MOUTH', sub: 'wooden', color: '#d55181', shape: 'cross' },
  ];
  // announcerVoiceName order
  Q.VOICES = [
    { dir: 'male', name: 'MALE', color: '#3987e5', shape: 'circle' },
    { dir: 'female', name: 'FEMALE', color: '#d95926', shape: 'square' },
    { dir: 'british', name: 'BRITISH', color: '#199e70', shape: 'diamond' },
    { dir: 'old_man', name: 'OLD MAN', color: '#c98500', shape: 'triangle' },
    { dir: 'e_girl', name: 'E-GIRL', color: '#d55181', shape: 'cross' },
    { dir: 'india', name: 'INDIA', color: '#008300', shape: 'circle' },
    { dir: 'spy', name: 'SPY', color: '#9085e9', shape: 'square' },
  ];
  Q.kitByDir = d => Q.KITS.find(k => k.dir === d);
  Q.voiceByDir = d => Q.VOICES.find(k => k.dir === d);
  Q.packName = (kind, dir) => (kind === 'voice' ? Q.voiceByDir(dir) : Q.kitByDir(dir) || { name: dir.toUpperCase() }).name;

  // SoundId order (sound ids < SND_VOICE_THREE), and the announcer's lines in their order
  Q.SOUNDS = ['HIT', 'KILL', 'HURT', 'STEP', 'JUMP', 'LAND', 'RIFLE', 'ROCKET', 'KNIFE', 'SNIPER', 'LASER', 'SHOTGUN', 'STICKY',
    'CROSSBOW', 'BUBBLE', 'SYRINGE', 'EXPLOSION', 'STICKY_ATTACH', 'BOLT_HIT', 'DASH', 'GRAPPLE_FIRE', 'GRAPPLE_HIT', 'DEATH', 'STREAK',
    'TYPING', 'CHECKPOINT', 'FINISH', 'HIT_MED', 'HIT_HEAVY', 'HIT_BRUTAL', 'SUPER', 'HYPER', 'LOCATOR'];
  Q.LINES = ['THREE', 'TWO', 'ONE', 'GO', 'BOMB_PLANTED', 'BOMB_DEFUSED', 'HILL_TAKEN', 'HILL_LOST', 'POINT_TAKEN', 'POINT_LOST',
    'ROUND_WON', 'ROUND_LOST', 'OVERTIME', 'YOU_WIN', 'YOU_LOSE', 'HIGH_SCORE', 'NEW_LEADER', 'YOU_DIED', 'HEADSHOT',
    'AIR_ROCKET', 'SPREE', 'RAMPAGE', 'UNSTOPPABLE', 'GODLIKE', 'ONE_MINUTE', 'THIRTY_SECONDS', 'ROUND_OVER'];
  // display order of the sound matrix: grouped the way a sound designer thinks about them
  // each voice's quips (the QUIP key; never a call-out), named 'quips/<NAME>' so a key is their file's path
  // (voices/<voice>/quips/<NAME>.wav): the manifest's list (build.py), the game's default set until recorded ones ship
  Q.QUIPS = [];
  // the default quips plus your own new ones (Missing > Add your own > quip: state.customs 'quips/<NAME>'); the game takes
  // any voices/<voice>/quips/<NAME> (up to 64 per voice), each levelled to the default quip of its name, else to HEADSHOT
  Q.allQuips = () => Q.QUIPS.concat(Q.state.customs.filter(c => c.kind === 'voice' && c.name.startsWith('quips/') && !Q.QUIPS.includes(c.name)).map(c => c.name));
  Q.SOUND_GROUPS = [
    ['Hitsounds', ['HIT', 'HIT_MED', 'HIT_HEAVY', 'HIT_BRUTAL', 'KILL']],
    ['Weapons', ['RIFLE', 'ROCKET', 'KNIFE', 'SNIPER', 'LASER', 'SHOTGUN', 'STICKY', 'CROSSBOW', 'BUBBLE', 'SYRINGE']],
    ['Impacts', ['EXPLOSION', 'STICKY_ATTACH', 'BOLT_HIT']],
    ['Movement', ['STEP', 'JUMP', 'LAND', 'DASH', 'SUPER', 'HYPER', 'GRAPPLE_FIRE', 'GRAPPLE_HIT']],
    ['You / others', ['HURT', 'DEATH', 'STREAK', 'TYPING', 'LOCATOR']],
    ['Race', ['CHECKPOINT', 'FINISH']],
  ];
  Q.WEAPONS = ['RIFLE', 'ROCKET', 'KNIFE', 'SNIPER', 'LASER', 'SHOTGUN', 'STICKY', 'CROSSBOW', 'BUBBLE', 'SYRINGE'];
  Q.MOVEMENT = ['STEP', 'JUMP', 'LAND', 'DASH', 'SUPER', 'HYPER', 'GRAPPLE_FIRE', 'GRAPPLE_HIT'];
  Q.HIT_TIERS = ['HIT', 'HIT_MED', 'HIT_HEAVY', 'HIT_BRUTAL'];
  Q.HIT_TIER_GAIN = [0.7, 0.85, 1.0, 1.15];   // kHitTierGain (audio.h)
  Q.hitTier = name => Q.HIT_TIERS.indexOf(name);
  Q.SOUND_CAP = 3.0;   // prepareClip: sounds cut at 3 s, voice lines at 5 s
  Q.VOICE_CAP = 5.0;

  // Q.MISSING, Q.USES, Q.WEAK: audit.js

  // --- the asset registry --------------------------------------------------------------------------------
  // Q.assets[key]: key = 'sounds/<kit>/<NAME>' or 'voices/<voice>/<LINE>' (the export / override path, no extension)
  Q.assets = {};
  Q.manifest = null;
  Q.raw = {};            // pack id -> {NAME: {m, d}} base64 as registered, decoded on demand
  Q.loaded = {};         // 'kit:default' -> true once decoded
  Q.loading = {};        // 'kit:default' -> Promise
  Q.key = (kind, pack, name) => (kind === 'voice' ? 'voices/' : 'sounds/') + pack + '/' + name;
  Q.parseKey = key => { const [d, pack, ...rest] = key.split('/'); return { kind: d === 'voices' ? 'voice' : 'sound', pack, name: rest.join('/') }; };

  window.qseRegister = function (obj) {
    if (obj.type === 'manifest') { Q.manifest = obj; Q.onManifest && Q.onManifest(obj); return; }
    Q.raw[obj.type + ':' + obj.id] = obj.clips;
    const w = Q.waiters && Q.waiters[obj.type + ':' + obj.id];
    if (w) w.resolve();
  };
  Q.waiters = {};

  // the base folder of the page (data/ sits next to it)
  function loadScript(src) {
    return new Promise((res, rej) => {
      const s = document.createElement('script');
      s.src = src; s.async = true;
      s.onload = () => res(); s.onerror = () => rej(new Error('could not load ' + src));
      document.head.appendChild(s);
    });
  }
  Q.loadScript = loadScript;

  // decode context: everything at 48 kHz (decodeAudioData resamples any upload to the context's rate)
  let decodeCtx = null;
  Q.decodeCtx = () => decodeCtx || (decodeCtx = new OfflineAudioContext(1, 1, Q.RATE));
  Q.b64ToBytes = function (b64) {
    const bin = atob(b64), u = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
    return u;
  };
  Q.bytesToB64 = function (u8) {
    let s = '';
    for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
    return btoa(s);
  };
  // any audio file's bytes -> mono Float32Array at 48 kHz (channels averaged, like the game's decoder)
  Q.decodeBytes = async function (bytes) {
    const ab = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
    const buf = await Q.decodeCtx().decodeAudioData(ab);
    const n = buf.length, out = new Float32Array(n), ch = buf.numberOfChannels;
    for (let c = 0; c < ch; c++) { const d = buf.getChannelData(c); for (let i = 0; i < n; i++) out[i] += d[i] / ch; }
    return out;
  };

  // load + decode one pack ('kit' / 'voice', dir). Resolves when every clip of it is in Q.assets[..].orig
  Q.loadPack = function (type, dir) {
    const id = type + ':' + dir;
    if (Q.loading[id]) return Q.loading[id];
    const p = (async () => {
      if (!Q.raw[id]) {
        const waited = new Promise(resolve => { Q.waiters[id] = { resolve }; });
        await loadScript('data/' + type + '_' + dir + '.js');
        await waited;
      }
      const clips = Q.raw[id], kind = type === 'kit' ? 'sound' : 'voice';
      await Promise.all(Object.keys(clips).map(async name => {
        const a = Q.assets[Q.key(kind, dir, name)];
        if (!a) return;
        try { a.orig = await Q.decodeBytes(Q.b64ToBytes(clips[name].d)); }
        catch (e) { a.orig = new Float32Array(0); a.error = String(e); }
      }));
      delete Q.raw[id];        // (decoded: the base64 can go)
      Q.loaded[id] = true;
      Q.onPackLoaded && Q.onPackLoaded(type, dir);
    })();
    Q.loading[id] = p;
    p.catch(() => { delete Q.loading[id]; });
    return p;
  };

  Q.buildAssets = function (man) {
    for (const key of Object.keys(man.items)) {
      const it = man.items[key];
      Q.assets[key] = {
        key, kind: it.kind, pack: it.pack, name: it.name, desc: it.desc, when: it.when, guide: it.guide,
        sameAsDefault: !!it.sameAsDefault, derived: it.derived || null, shippedOverride: !!it.shippedOverride,
        cap: it.kind === 'voice' ? Q.VOICE_CAP : Q.SOUND_CAP, orig: null, custom: false,
      };
    }
  };
  // a sound the game doesn't have yet (Q.MISSING or the owner's own name): one asset per kit / voice, silent until given
  Q.addCustomAssets = function (name, kind, when) {
    const packs = kind === 'voice' ? Q.VOICES : Q.KITS;
    for (const p of packs) {
      const key = Q.key(kind, p.dir, name);
      if (Q.assets[key]) continue;
      Q.assets[key] = { key, kind, pack: p.dir, name, desc: when, when, guide: null, cap: kind === 'voice' ? Q.VOICE_CAP : Q.SOUND_CAP,
        orig: new Float32Array(0), custom: true };
    }
  };

  // --- project state ---------------------------------------------------------------------------------------
  /* edits[key] = {src: sourceId|null (null = the shipped sound), trim: [start, end|null] seconds in the source,
                   fadeIn, fadeOut (seconds), chain: [{id, type, on, p: {...}, group?}], levelSet: bool}
     sources[id] = {name, data: Float32Array (48 kHz mono), bytes?: Uint8Array (the dropped file, for the project file)}
     groups = [{id, name, members: [key], target: {mode, value, ref}, measure, ceiling, limiter, offsets: {key: dB}}]
     customs = [{name, kind, when}]   meta[key] = {fav, note} */
  Q.state = { edits: {}, groups: [], customs: [], meta: {} };
  Q.sources = {};
  let srcSeq = 1;
  Q.addSource = function (name, data, bytes) {
    let id;
    do { id = 's' + Date.now().toString(36) + (srcSeq++); } while (Q.sources[id]);
    Q.sources[id] = { name, data, bytes: bytes || null };
    return id;
  };
  Q.newEdit = () => ({ src: null, trim: [0, null], fadeIn: 0, fadeOut: 0, chain: [], levelSet: false });
  Q.sourceOf = function (key) {   // the clip an edit starts from
    const e = Q.state.edits[key], a = Q.assets[key];
    if (e && e.src && Q.sources[e.src]) return Q.sources[e.src].data;
    return a && a.orig ? a.orig : new Float32Array(0);
  };
  Q.isEdited = key => !!Q.state.edits[key];
  Q.uid = () => Math.random().toString(36).slice(2, 9);

  // --- undo / redo: snapshots of the state (sources are immutable and referenced by id) -------------------------
  const undo = [], redo = [];
  Q.snapshot = () => JSON.stringify(Q.state);
  Q.commit = function (label, mutate) {   // one undoable step
    const before = Q.snapshot();
    mutate();
    const after = Q.snapshot();
    if (after === before) return false;
    undo.push({ label, s: before });
    if (undo.length > 200) undo.shift();
    redo.length = 0;
    Q.onStateChanged && Q.onStateChanged(label);
    return true;
  };
  // an undoable step whose changes are made asynchronously (group levelling): begin, change Q.state, end
  Q.beginStep = label => ({ label, s: Q.snapshot() });
  Q.endStep = function (tok) {
    if (Q.snapshot() === tok.s) return false;
    undo.push({ label: tok.label, s: tok.s });
    if (undo.length > 200) undo.shift();
    redo.length = 0;
    Q.onStateChanged && Q.onStateChanged(tok.label);
    return true;
  };
  Q.undo = function () {
    const u = undo.pop(); if (!u) return null;
    redo.push({ label: u.label, s: Q.snapshot() });
    Q.state = JSON.parse(u.s);
    Q.onStateChanged && Q.onStateChanged('undo ' + u.label, true);
    return u.label;
  };
  Q.redo = function () {
    const r = redo.pop(); if (!r) return null;
    undo.push({ label: r.label, s: Q.snapshot() });
    Q.state = JSON.parse(r.s);
    Q.onStateChanged && Q.onStateChanged('redo ' + r.label, true);
    return r.label;
  };
  Q.canUndo = () => undo.length > 0;
  Q.canRedo = () => redo.length > 0;
  Q.undoLabel = () => undo.length ? undo[undo.length - 1].label : '';
  Q.redoLabel = () => redo.length ? redo[redo.length - 1].label : '';
  Q.clearHistory = () => { undo.length = 0; redo.length = 0; };
})();
