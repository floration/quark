/* ===== DATA =========================================================================================
   The namespace, the game's constants (colours, slots, sizes: from src/client/models.*, renderer.*, config.h, game.cpp),
   the model registry (built from the export manifest, data-driven: any models/<kind>/<NAME> works), the editing state
   and undo / redo. */
(function () {
  'use strict';
  const Q = window.Q = window.Q || {};

  // --- the data scripts (data/*.js call window.qmeRegister) ------------------------------------------------------
  Q.loadScript = function (src) {
    return new Promise((res, rej) => {
      const s = document.createElement('script');
      s.src = src; s.onload = () => { s.remove(); res(); }; s.onerror = () => { s.remove(); rej(new Error('cannot load ' + src)); };
      document.head.appendChild(s);
    });
  };
  window.qmeRegister = function (payload) {
    if (payload && payload.type === 'manifest') { Q.manifest = payload; if (Q.onManifest) Q.onManifest(payload); }
  };

  // --- the game's constants ------------------------------------------------------------------------------------------
  Q.SLOT_NAMES = ['body', 'team', 'glow', 'metal', 'base'];           // tintSlotName (models.cpp); body = no tint
  Q.SLOT_TITLES = ['none (as authored)', 'team / accent', 'glow (map)', 'metal (map)', 'base (map)'];
  Q.slotFromName = function (name) {                                   // tintSlotFromName: prefix, any case
    const l = String(name || '').toLowerCase();
    for (let s = 1; s < 5; s++) if (l.startsWith(Q.SLOT_NAMES[s])) return s;
    return 0;
  };
  const lin = c => [Math.pow(c[0], 2.2), Math.pow(c[1], 2.2), Math.pow(c[2], 2.2)];
  Q.lin = lin;
  Q.viewmodelMetal = m => lin(m).map(v => v * 1.6 + 0.05);           // models.cpp viewmodelMetal
  Q.viewmodelBase = b => lin(b).map(v => v * 1.1);                     // models.cpp viewmodelBase
  Q.crownColor = place => place === 1 ? [1.0, 0.78, 0.15] : [0.82, 0.85, 0.9];
  Q.DEFAULT_PALETTE = { id: '', title: 'Default (no map palette)', base: [0.62, 0.62, 0.64], accent: [0.40, 0.40, 0.43], metal: [0.30, 0.33, 0.37],
    glow: [1.00, 0.55, 0.15], sky: [0.62, 0.66, 0.72], sun: norm([0.4, 1.0, 0.25]) };   // renderer.h + map.h sunDir
  Q.TEAM = { friendly: [0.25, 0.85, 1.0], enemy: [1.0, 0.3, 0.25] };   // config.h colorFriendly / colorEnemy
  Q.OUTLINE_COLORS = { black: [0, 0, 0], white: [1, 1, 1], team: null, red: [1, 0.1, 0.1], yellow: [1, 0.9, 0.1], green: [0.1, 1, 0.2], cyan: [0.1, 0.95, 1], magenta: [1, 0.2, 1] };
  Q.SUN_COLOR = [0.78, 0.75, 0.70]; Q.SKY_LIGHT = [0.42, 0.45, 0.52]; Q.FOG = 0.0035;   // renderer.cpp drawScene
  Q.NEAR = 0.05; Q.FAR = 400; Q.FOV = 100;                              // camera; config.h fov
  Q.EYE = 1.6; Q.CROUCH_EYE = 1.05; Q.PLAYER_H = 1.8; Q.CROUCH_H = 1.2; Q.HALF_W = 0.4; Q.HEAD_FRAC = 0.2; Q.HEAD_W = 0.45;
  Q.MAX_TRIS = 20000;                                                  // kMaxOverrideTriangles
  Q.GUN_ARM = [0.12, 0.12, 0.14];                                      // the arm box colour (game.cpp)
  // projectile speeds (m/s, gamedefs.h; the knife / syringe / orb ones are typical) and their trails (game.cpp)
  Q.PROJ = {
    ROCKET: { speed: 48, trail: { from: -0.35, len: 3.5, w: 0.1, a: 0.4 } },
    STICKY: { speed: 22 },
    CROSSBOW: { speed: 76.5, trail: { from: 0, len: 4.0, w: 0.04, a: 0.5 } },
    BUBBLE: { speed: 15.6 },
    KNIFE: { speed: 30, trail: { from: 0, len: 2.0, w: 0.03, a: 0.4 } },
    SYRINGE: { speed: 30, trail: { from: 0, len: 1.5, w: 0.03, a: 0.5 } },
    ORB: { speed: 30, trail: { from: 0, len: 3.5, w: 0.12, a: 0.5 } },
  };
  function norm(v) { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; }
  Q.norm = norm;

  // The colours the export used (referencePalette, models.cpp): an override's slot colour is multiplied by
  // (current / reference). Team is white (crown: the #1 gold).
  Q.referencePalette = function (kind) {
    const p = [[1, 1, 1], [1, 1, 1], [1.0, 0.55, 0.15], Q.viewmodelMetal([0.30, 0.33, 0.37]), Q.viewmodelBase([0.62, 0.62, 0.64])];
    if (kind === 'crown') p[1] = Q.crownColor(1);
    return p;
  };
  // The palette the game builds where it draws each kind (game.cpp): slots it doesn't set stay white (ModelPalette's
  // default), so an override's material in such a slot gets white / reference (see Q.undrivenSlots).
  //   ctx: { kind, team, map (palette), use: 'world' | 'view' | 'held', vmColor, crownPlace }
  Q.runtimePalette = function (ctx) {
    const W = [1, 1, 1], p = [W, W.slice(), W.slice(), W.slice(), W.slice()], m = ctx.map || Q.DEFAULT_PALETTE;
    switch (ctx.use === 'held' ? 'held' : ctx.kind) {
      case 'player': p[1] = ctx.team; break;
      case 'weapon': p[1] = ctx.vmColor || Q.TEAM.friendly; p[2] = m.glow; p[3] = Q.viewmodelMetal(m.metal); p[4] = Q.viewmodelBase(m.base); break;
      case 'held': p[1] = ctx.team; p[2] = m.glow; p[3] = Q.viewmodelMetal(m.metal); p[4] = [0.85, 0.88, 0.92]; break;
      case 'projectile': p[1] = ctx.team; p[2] = m.glow; break;
      case 'crown': p[1] = Q.crownColor(ctx.crownPlace || 1); break;
      default: { const r = Q.referencePalette(ctx.kind); r[1] = ctx.team; return r; }   // (a kind this page doesn't know: export colours)
    }
    return p;
  };
  Q.undrivenSlots = function (kind) {   // slots the game leaves white for this kind (a material there is mis-tinted)
    return { player: [2, 3, 4], projectile: [3, 4], crown: [2, 3, 4] }[kind] || [];
  };
  // overrideTints (game.cpp): tint[s] = current / reference per channel (reference ~0: the current itself)
  Q.tints = function (kind, pal, out) {
    const ref = Q.referencePalette(kind);
    out = out || new Float32Array(15);
    out[0] = out[1] = out[2] = 1;
    for (let s = 1; s < 5; s++) for (let k = 0; k < 3; k++) { const b = ref[s][k]; out[s * 3 + k] = b > 1e-3 ? pal[s][k] / b : pal[s][k]; }
    return out;
  };

  // --- the registry ----------------------------------------------------------------------------------------------------
  // Q.models[key] = { key: 'models/player/STANDARD', kind, name, desc, defMesh, defParts | null, shipped: {name, bytes} | null }
  Q.models = {}; Q.order = []; Q.kinds = [];
  Q.KIND_ORDER = ['player', 'weapon', 'projectile', 'crown'];
  Q.KIND_TITLES = { player: 'Players', weapon: 'Weapons (first person)', projectile: 'Projectiles', crown: 'Crown' };
  Q.buildModels = function (man) {
    for (const it of man.models) {
      const m = { key: it.key, kind: it.kind, name: it.name, desc: it.desc || '', defMesh: null, defParts: null, shipped: null, err: null };
      try {
        m.defMesh = Q.gltf.parse(Q.b64ToBytes(it.data), it.file);
        m.defParts = Q.boxes.fromMesh(m.defMesh);
      } catch (e) { m.err = String(e && e.message || e); }
      if (it.shipped) {
        try { const b = Q.b64ToBytes(it.shipped.data); m.shipped = { name: it.shipped.name, bytes: b, mesh: Q.gltf.parse(b, it.shipped.name) }; }
        catch (e) { m.shipped = { name: it.shipped.name, err: String(e.message || e) }; }
      }
      Q.models[m.key] = m; Q.order.push(m.key);
      if (!Q.kinds.includes(m.kind)) Q.kinds.push(m.kind);
    }
    Q.kinds.sort((a, b) => (Q.KIND_ORDER.indexOf(a) + 1 || 99) - (Q.KIND_ORDER.indexOf(b) + 1 || 99) || a.localeCompare(b));
    Q.palettes = [Q.DEFAULT_PALETTE].concat((man.palettes || []).map(p => Object.assign({}, p, { sun: norm(p.sun || Q.DEFAULT_PALETTE.sun) })));
  };
  Q.kindTitle = k => Q.KIND_TITLES[k] || (k.charAt(0).toUpperCase() + k.slice(1));
  Q.findModel = (kind, name) => Q.models['models/' + kind + '/' + name] || null;

  // --- state: the edits (autosaved, saved in project files) -------------------------------------------------------------
  // state.edits[key] = { type: 'boxes', parts: [...] } | { type: 'mesh', src: sourceId }
  // part = { c: [x,y,z], h: [x,y,z], color: [r,g,b] (linear, may be > 1), slot: 0..4, alpha, shadow, visOnly, name }
  // Q.sources[id] = { name, bytes } (imported files, kept with the project)
  Q.state = { edits: {}, prov: {} };
  Q.sources = {};
  Q.ver = {};   // key -> change counter (caches: GPU meshes, thumbnails)
  Q.bump = key => { Q.ver[key] = (Q.ver[key] || 0) + 1; };
  Q.newPart = () => ({ c: [0, 0, 0], h: [0.05, 0.05, 0.05], color: [0.5, 0.5, 0.5], slot: 0, alpha: 1, shadow: true, visOnly: false, name: '' });
  Q.clonePart = p => ({ c: p.c.slice(), h: p.h.slice(), color: p.color.slice(), slot: p.slot | 0, alpha: p.alpha == null ? 1 : +p.alpha,
    shadow: p.shadow !== false, visOnly: !!p.visOnly, name: p.name || '' });
  Q.edit = key => Q.state.edits[key] || null;
  Q.isEdited = key => !!Q.state.edits[key];
  // what this model is now: { type: 'boxes', parts } | { type: 'mesh', mesh, name } | { type: 'none' }
  Q.current = function (key) {
    const m = Q.models[key], e = Q.state.edits[key];
    if (e && e.type === 'boxes') return { type: 'boxes', parts: e.parts, edited: true };
    if (e && e.type === 'mesh') {
      const s = Q.sources[e.src];
      if (s && !s.mesh) { try { s.mesh = Q.gltf.parse(s.bytes, s.name); } catch (err) { s.err = String(err.message || err); } }
      if (s && s.mesh) return { type: 'mesh', mesh: s.mesh, name: s.name, edited: true };
    }
    if (!m) return { type: 'none' };
    // a shipped override (assets/overrides) is what the game draws in place of the built-in boxes
    if (m.shipped && m.shipped.mesh) {
      if (m.shipped.parts === undefined) m.shipped.parts = Q.boxes.fromMesh(m.shipped.mesh);
      if (m.shipped.parts) return { type: 'boxes', parts: m.shipped.parts, edited: false, shipped: true };
      return { type: 'mesh', mesh: m.shipped.mesh, name: m.shipped.name, edited: false, shipped: true };
    }
    if (m.defParts) return { type: 'boxes', parts: m.defParts, edited: false };
    if (m.defMesh) return { type: 'mesh', mesh: m.defMesh, name: m.key, edited: false };
    return { type: 'none' };
  };
  // start editing the boxes (a copy of the default's, or of an imported box-shaped mesh)
  Q.ensureBoxEdit = function (key) {
    const e = Q.state.edits[key];
    if (e && e.type === 'boxes') return e;
    const cur = Q.current(key);
    let parts = null;
    if (cur.type === 'boxes') parts = cur.parts.map(Q.clonePart);
    else if (cur.type === 'mesh') parts = Q.boxes.fromMesh(cur.mesh);
    if (!parts) return null;
    Q.state.edits[key] = { type: 'boxes', parts };
    return Q.state.edits[key];
  };

  // --- undo / redo: snapshots of one model's edit -------------------------------------------------------------------
  const undo = [], redo = [];
  let pending = null;   // an open coalescing group (a drag, typing in a field)
  Q.snapshot = key => JSON.stringify(Q.state.edits[key] || null);
  // call before changing model `key`; `group` coalesces repeated changes (same group string within 1.5 s)
  Q.beginChange = function (key, group) {
    const now = performance.now();
    if (group && pending && pending.group === group && pending.key === key && now - pending.t < 1500) { pending.t = now; return; }
    undo.push({ key, before: Q.snapshot(key), group: group || null });
    if (undo.length > 300) undo.shift();
    redo.length = 0;
    pending = group ? { group, key, t: now } : null;
  };
  Q.endChange = function (key, why) { Q.bump(key); Q.onStateChanged && Q.onStateChanged(why || 'edit', key); };
  function restore(key, snap) {
    const v = JSON.parse(snap);
    if (v) Q.state.edits[key] = v; else delete Q.state.edits[key];
  }
  Q.undo = function () {
    const u = undo.pop(); if (!u) return null;
    redo.push({ key: u.key, before: Q.snapshot(u.key) });
    restore(u.key, u.before); pending = null; Q.endChange(u.key, 'undo');
    return u.key;
  };
  Q.redo = function () {
    const r = redo.pop(); if (!r) return null;
    undo.push({ key: r.key, before: Q.snapshot(r.key) });
    restore(r.key, r.before); pending = null; Q.endChange(r.key, 'redo');
    return r.key;
  };
  Q.canUndo = () => undo.length > 0; Q.canRedo = () => redo.length > 0;
  Q.clearHistory = () => { undo.length = 0; redo.length = 0; pending = null; };
  Q.closeGroup = () => { pending = null; };

  // --- bytes / base64 ---------------------------------------------------------------------------------------------------
  Q.b64ToBytes = function (s) {
    const bin = atob(s), u = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
    return u;
  };
  Q.bytesToB64 = function (u) {
    let s = '';
    for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000));
    return btoa(s);
  };
  // colours: linear <-> sRGB hex (the picker is sRGB; the game's numbers are linear and may exceed 1)
  Q.toSrgb = v => v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055;
  Q.fromSrgb = v => v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
  Q.linToHex = function (c) {
    const m = Math.max(1, c[0], c[1], c[2]);   // (over 1: the hue at full brightness; the intensity field keeps the rest)
    return '#' + c.map(v => Math.round(Math.max(0, Math.min(1, Q.toSrgb(v / m))) * 255).toString(16).padStart(2, '0')).join('');
  };
  Q.hexToLin = function (hex) {
    const n = parseInt(hex.slice(1), 16);
    return [(n >> 16) & 255, (n >> 8) & 255, n & 255].map(v => Q.fromSrgb(v / 255));
  };
  Q.fmt = (v, d) => { const s = (+v).toFixed(d == null ? 4 : d); return s.indexOf('.') >= 0 ? s.replace(/0+$/, '').replace(/\.$/, '') : s; };
})();
