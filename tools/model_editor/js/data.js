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
  Q.SLOT_NAMES = ['body', 'team', 'glow', 'metal', 'base', 'accent'];   // tintSlotName (models.cpp); body = no tint
  Q.SLOTS = Q.SLOT_NAMES.length;
  Q.SLOT_TITLES = ['none (as authored)', 'team / accent colour', 'glow (map)', 'metal (map)', 'base (map)', 'accent (map)'];
  Q.slotFromName = function (name) {                                   // tintSlotFromName: prefix, any case
    const l = String(name || '').toLowerCase();
    for (let s = 1; s < Q.SLOTS; s++) if (l.startsWith(Q.SLOT_NAMES[s])) return s;
    return 0;
  };
  const lin = c => [Math.pow(c[0], 2.2), Math.pow(c[1], 2.2), Math.pow(c[2], 2.2)];
  const f32 = Math.fround;
  Q.lin = lin;
  Q.viewmodelMetal = m => lin(m).map(v => v * 1.6 + 0.05);           // models.cpp viewmodelMetal
  Q.viewmodelBase = b => lin(b).map(v => v * 1.1);                     // models.cpp viewmodelBase
  Q.cartMetal = m => lin(m).map(v => v * 1.6 + 0.04);                 // models.cpp cartMetal
  Q.cartAccent = a => lin(a).map(v => v * 1.4);                        // models.cpp cartAccent
  Q.crownColor = place => place === 1 ? [1.0, 0.78, 0.15] : [0.82, 0.85, 0.9];
  Q.DEFAULT_PALETTE = { id: '', title: 'Default (no map palette)', base: [0.62, 0.62, 0.64], accent: [0.40, 0.40, 0.43], metal: [0.30, 0.33, 0.37],
    glow: [1.00, 0.55, 0.15], sky: [0.62, 0.66, 0.72], sun: norm([0.4, 1.0, 0.25]) };   // renderer.h + map.h sunDir
  Q.TEAM = { friendly: [0.25, 0.85, 1.0], enemy: [1.0, 0.3, 0.25] };   // config.h colorFriendly / colorEnemy
  Q.NEUTRAL_CART = [0.75, 0.75, 0.78]; Q.GATE_GREY = [0.8, 0.8, 0.8];   // game.cpp: a neutral cart, a gate not reached yet
  Q.BOMB_DARK = [0.15, 0.15, 0.15]; Q.BOMB_BLINK = [1, 0.2, 0.2];        // game.cpp: the bomb, and a planted one's blink
  Q.HELD_BASE = [0.85, 0.88, 0.92];                                     // drawHeldWeapon: bright steel
  Q.OUTLINE_COLORS = { black: [0, 0, 0], white: [1, 1, 1], team: null, red: [1, 0.1, 0.1], yellow: [1, 0.9, 0.1], green: [0.1, 1, 0.2], cyan: [0.1, 0.95, 1], magenta: [1, 0.2, 1] };
  Q.SUN_COLOR = [0.78, 0.75, 0.70]; Q.SKY_LIGHT = [0.42, 0.45, 0.52]; Q.FOG = 0.0035;   // renderer.cpp drawScene
  Q.NEAR = 0.05; Q.FAR = 400; Q.FOV = 100;                              // camera; config.h fov
  Q.EYE = 1.6; Q.CROUCH_EYE = 1.05; Q.PLAYER_H = 1.8; Q.CROUCH_H = 1.2; Q.HALF_W = 0.4; Q.HEAD_FRAC = 0.2; Q.HEAD_W = 0.45;
  Q.MAX_TRIS = 20000;                                                  // kMaxOverrideTriangles
  Q.MAX_BOXES = 256;                                                   // kMaxOverrideBoxes
  Q.GUN_ARM = [0.12, 0.12, 0.14];                                      // the arm box colour (models.cpp propParts)
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

  // The colours the export used, per model (referencePalette(kind, idx), models.cpp): a box without `mul` and an
  // override mesh are tinted by (current / reference); a box file's `color` is its colour at these. Team is white
  // (crown: the #1 gold; gate: the unreached grey), the map slots the stock palette, the cart its own metal, the bomb
  // dark. Slot order: body, team, glow, metal, base, accent.
  const refCache = {};
  Q.referencePalette = function (kind, name) {
    const id = kind + '/' + (name || '');
    if (refCache[id]) return refCache[id];
    const p = [[1, 1, 1], [1, 1, 1], [1.0, 0.55, 0.15], Q.viewmodelMetal([0.30, 0.33, 0.37]), Q.viewmodelBase([0.62, 0.62, 0.64]), Q.cartAccent([0.40, 0.40, 0.43])];
    if (kind === 'crown') p[1] = Q.crownColor(1);
    if (kind === 'prop' && name === 'CART') p[3] = Q.cartMetal([0.30, 0.33, 0.37]);
    if (kind === 'prop' && name === 'BOMB') p[2] = Q.BOMB_DARK;
    if (kind === 'prop' && name === 'GATE') p[1] = Q.GATE_GREY;
    return (refCache[id] = p.map(c => c.map(f32)));   // (the game's are floats)
  };
  Q.refOf = key => { const m = Q.models[key]; return Q.referencePalette(m ? m.kind : '', m ? m.name : ''); };
  // The slots a model's drawing gives colours to (drivenSlots, models.cpp); the others stay at the reference (tint 1:
  // drawn as authored). A kind this page doesn't know: team only.
  Q.drivenSlots = function (kind, name) {
    switch (kind) {
      case 'player': case 'crown': return [1];
      case 'projectile': return [1, 2];
      case 'weapon': return [1, 2, 3, 4];
      case 'prop': return name === 'BOMB' ? [2] : name === 'CART' ? [1, 2, 3, 5] : name === 'GATE' ? [1] : [];
    }
    return [1];
  };
  // The palette the game builds where it draws a model (game.cpp), every slot (unset ones white, as ModelPalette).
  //   ctx: { kind, name, team, map (palette), use: 'held' (a weapon on "it"'s hand), vmColor, crownPlace,
  //          cartTeam (null: neutral), gateReached, bombBlink }
  Q.runtimePalette = function (ctx) {
    const W = [1, 1, 1], p = [W, W, W, W, W, W], m = ctx.map || Q.DEFAULT_PALETTE, team = ctx.team || Q.TEAM.enemy;
    switch (ctx.use === 'held' ? 'held' : ctx.kind) {
      case 'player': case 'projectile': p[1] = team; p[2] = m.glow; break;
      case 'weapon': p[1] = ctx.vmColor || Q.TEAM.friendly; p[2] = m.glow; p[3] = Q.viewmodelMetal(m.metal); p[4] = Q.viewmodelBase(m.base); break;
      case 'held': p[1] = team; p[2] = m.glow; p[3] = Q.viewmodelMetal(m.metal); p[4] = Q.HELD_BASE; break;
      case 'crown': p[1] = Q.crownColor(ctx.crownPlace || 1); break;
      case 'prop':
        if (ctx.name === 'BOMB') p[2] = ctx.bombBlink ? Q.BOMB_BLINK : Q.BOMB_DARK;
        else if (ctx.name === 'CART') { p[1] = ctx.cartTeam === null ? Q.NEUTRAL_CART : (ctx.cartTeam || team); p[2] = m.glow; p[3] = Q.cartMetal(m.metal); p[5] = Q.cartAccent(m.accent || Q.DEFAULT_PALETTE.accent); }
        else if (ctx.name === 'GATE') p[1] = ctx.gateReached ? team : Q.GATE_GREY;
        break;
      default: p[1] = team;
    }
    return p;
  };
  // drivenPalette: the slot colours a model is drawn with (uniform uSlot[6]; slot 0 = 1), into out (Float32Array 18)
  Q.slotColors = function (kind, name, pal, out) {
    const ref = Q.referencePalette(kind, name), d = Q.drivenSlots(kind, name);
    out = out || new Float32Array(18);
    out[0] = out[1] = out[2] = 1;
    for (let s = 1; s < Q.SLOTS; s++) { const c = d.indexOf(s) >= 0 ? pal[s] : ref[s]; out[s * 3] = c[0]; out[s * 3 + 1] = c[1]; out[s * 3 + 2] = c[2]; }
    return out;
  };

  // --- box colours (a box model file, docs/ASSETS.md "Box models") --------------------------------------------------------
  // A slot box is drawn slot colour * mul + team colour * teamMul + add; `color` is that at the reference palette.
  const ratio = (a, b) => b > 1e-3 ? a / b : a;
  Q.ratio = ratio;
  // finishBoxModel: a slot box without mul gets colour / reference (how a mesh is tinted), add 0
  Q.finishPart = function (p, ref) {
    if ((p.slot | 0) === 0) { p.mul = p.add = p.teamMul = null; return p; }
    if (!p.mul) { const r = ref[p.slot]; p.mul = p.color.map((c, k) => f32(ratio(c, r[k]))); p.add = [0, 0, 0]; p.teamMul = [0, 0, 0]; }
    if (!p.add) p.add = [0, 0, 0];
    if (!p.teamMul || p.slot === 1) p.teamMul = [0, 0, 0];
    return p;
  };
  // the colour a slot box shows at the reference palette (what `color` holds)
  Q.colorAtRef = function (p, ref) {
    if ((p.slot | 0) === 0) return p.color.slice();
    const s = ref[p.slot], t = ref[1], m = p.mul || [1, 1, 1], a = p.add || [0, 0, 0], tm = p.teamMul || [0, 0, 0];
    return [0, 1, 2].map(k => f32(s[k] * m[k] + t[k] * tm[k] + a[k]));
  };
  // a box recoloured to D (its colour at the reference): mul rewritten so it draws as shown; its add / teamMul kept
  // when they still fit under D (the offset a default grey has), else dropped (then it follows the slot like a mesh)
  Q.recolor = function (p, D, ref) {
    p.color = D.map(v => f32(Math.max(0, v)));
    if ((p.slot | 0) === 0) return p;
    const s = ref[p.slot], t = ref[1], a = p.add || [0, 0, 0], tm = p.slot === 1 ? [0, 0, 0] : (p.teamMul || [0, 0, 0]);
    const rest = [0, 1, 2].map(k => p.color[k] - a[k] - t[k] * tm[k]);
    if (rest.every(v => v >= 0)) { p.mul = rest.map((v, k) => f32(ratio(v, s[k]))); p.add = a.slice(); p.teamMul = tm.slice(); }
    else { p.mul = p.color.map((v, k) => f32(ratio(v, s[k]))); p.add = [0, 0, 0]; p.teamMul = [0, 0, 0]; }
    return p;
  };
  // a box moved to another slot: it keeps the colour it shows at the reference
  Q.setSlot = function (p, slot, ref) {
    const D = Q.colorAtRef(p, ref);
    p.slot = slot | 0; p.mul = p.add = p.teamMul = null;
    p.color = D;
    return Q.finishPart(p, ref);
  };

  // --- the registry ----------------------------------------------------------------------------------------------------
  // Q.models[key] = { key: 'models/player/STANDARD', kind, name, desc, defMesh, defParts | null, shipped: {name, bytes} | null }
  Q.models = {}; Q.order = []; Q.kinds = [];
  Q.KIND_ORDER = ['player', 'weapon', 'projectile', 'crown', 'prop'];
  Q.KIND_TITLES = { player: 'Players', weapon: 'Weapons (first person)', projectile: 'Projectiles', crown: 'Crown', prop: 'Props (arms, bomb, cart, gates)' };
  Q.buildModels = function (man) {
    for (const it of man.models) {
      const m = { key: it.key, kind: it.kind, name: it.name, desc: it.desc || '', defMesh: null, defParts: null, shipped: null, err: null };
      Q.models[m.key] = m;   // (registered first: the reference palette is looked up by key)
      try {
        m.defMesh = Q.gltf.parse(Q.b64ToBytes(it.data), it.file);
        m.defParts = Q.boxes.partsOf(m.defMesh, m.key, true);
        if (m.defParts && m.defParts.error) { m.err = m.defParts.error; m.defParts = null; }
      } catch (e) { m.err = String(e && e.message || e); }
      if (it.shipped) {
        try {
          const b = Q.b64ToBytes(it.shipped.data), mesh = Q.gltf.parse(b, it.shipped.name), parts = Q.boxes.fromExtras(mesh.extras, Q.refOf(m.key));
          m.shipped = { name: it.shipped.name, bytes: b, mesh, parts: parts && !parts.error ? parts : null, err: parts && parts.error ? 'box model: ' + parts.error : null };
        } catch (e) { m.shipped = { name: it.shipped.name, err: String(e.message || e) }; }
      }
      Q.order.push(m.key);
      if (!Q.kinds.includes(m.kind)) Q.kinds.push(m.kind);
    }
    Q.kinds.sort((a, b) => (Q.KIND_ORDER.indexOf(a) + 1 || 99) - (Q.KIND_ORDER.indexOf(b) + 1 || 99) || a.localeCompare(b));
    Q.palettes = [Q.DEFAULT_PALETTE].concat((man.palettes || []).map(p => Object.assign({}, p, { sun: norm(p.sun || Q.DEFAULT_PALETTE.sun) })));
    // the reference palettes above are this page's copy of the game's: check them against the defaults' boxes
    // (each box's colour is its formula at the reference) and say so in the console when the game changed them
    for (const k of Q.order) {
      const m = Q.models[k], ref = Q.refOf(k);
      for (const p of m.defParts || []) {
        const c = Q.colorAtRef(p, ref);
        if (c.some((v, i) => Math.abs(v - p.color[i]) > 1e-4 * Math.max(1, v))) { console.warn('reference palette differs from the export for ' + k + ' (slot ' + Q.SLOT_NAMES[p.slot] + ')'); m.refMismatch = true; break; }
      }
    }
  };
  Q.kindTitle = k => Q.KIND_TITLES[k] || (k.charAt(0).toUpperCase() + k.slice(1));
  Q.findModel = (kind, name) => Q.models['models/' + kind + '/' + name] || null;

  // --- state: the edits (autosaved, saved in project files) -------------------------------------------------------------
  // state.edits[key] = { type: 'boxes', parts: [...] } | { type: 'mesh', src: sourceId }
  // part = { c: [x,y,z], h: [x,y,z], color: [r,g,b] (linear, may be > 1: the colour at the reference palette), slot: 0..5,
  //   mul / add / teamMul: [r,g,b] (slot boxes: drawn = slot * mul + team * teamMul + add; null on slot 0), alpha, shadow,
  //   visOnly, crouch (players: a crouch squashes the half height too), name }
  // Q.sources[id] = { name, bytes } (imported files, kept with the project)
  Q.state = { edits: {}, prov: {} };
  Q.sources = {};
  Q.ver = {};   // key -> change counter (caches: GPU meshes, thumbnails)
  Q.bump = key => { Q.ver[key] = (Q.ver[key] || 0) + 1; };
  Q.newPart = () => ({ c: [0, 0, 0], h: [0.05, 0.05, 0.05], color: [0.5, 0.5, 0.5], slot: 0, mul: null, add: null, teamMul: null, alpha: 1, shadow: true, visOnly: false, crouch: false, name: '' });
  const cp = a => a ? a.slice() : null;
  Q.clonePart = p => ({ c: p.c.slice(), h: p.h.slice(), color: p.color.slice(), slot: p.slot | 0, mul: cp(p.mul), add: cp(p.add), teamMul: cp(p.teamMul),
    alpha: p.alpha == null ? 1 : +p.alpha, shadow: p.shadow !== false, visOnly: !!p.visOnly, crouch: !!p.crouch, name: p.name || '' });
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
    // a shipped override (assets/overrides) is what the game draws in place of the built-in boxes: a box model
    // (extras.quark version 2) through the box path, anything else as a mesh
    if (m.shipped && m.shipped.mesh && !m.shipped.err) {
      if (m.shipped.parts) return { type: 'boxes', parts: m.shipped.parts, edited: false, shipped: true };
      return { type: 'mesh', mesh: m.shipped.mesh, name: m.shipped.name, edited: false, shipped: true };
    }
    if (m.defParts) return { type: 'boxes', parts: m.defParts, edited: false };
    if (m.defMesh) return { type: 'mesh', mesh: m.defMesh, name: m.key, edited: false };
    return { type: 'none' };
  };
  // older project files / autosaves (before box model files): slot boxes get mul / add / teamMul, as the game reads them
  Q.normalizeEdits = function () {
    for (const [k, e] of Object.entries(Q.state.edits)) if (e.type === 'boxes' && Q.models[k]) {
      const ref = Q.refOf(k);
      e.parts = e.parts.map(p => Q.finishPart(Object.assign(Q.newPart(), p, { mul: p.mul || null, add: p.add || null, teamMul: p.teamMul || null }), ref));
    }
  };
  // start editing the boxes (a copy of the default's, or of an imported box-shaped mesh)
  Q.ensureBoxEdit = function (key) {
    const e = Q.state.edits[key];
    if (e && e.type === 'boxes') return e;
    const cur = Q.current(key);
    let parts = null;
    if (cur.type === 'boxes') parts = cur.parts.map(Q.clonePart);
    else if (cur.type === 'mesh') { parts = Q.boxes.partsOf(cur.mesh, key, false); if (parts && parts.error) parts = null; }
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
