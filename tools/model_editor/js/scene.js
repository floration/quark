/* ===== SCENES ========================================================================================
   What the viewport shows, placed the way the game places it (src/client/game.cpp):
     orbit     the model alone on a turntable (edit here)
     fp        first person: the weapon as your viewmodel (camera space at 0.2, -0.17, -0.55 + the HUD offsets, scale,
               drawn after a depth clear, no shadows on it), the game's fov and near plane
     tp        third person: a player with the gun arm, or holding a weapon as "it" (hide and seek / tag: the viewmodel
               boxes at 2x on the hand), the crown on the head; hitbox overlay
     flight    a projectile flying with its trail, spun / wobbled as the game does
     lineup    the screenshot lineup (quark --models): 5 players 5 m ahead, 1.5 m apart, facing you, your viewmodel
     track     a payload track: the cart, a checkpoint gate either side (reached / not), the track line
   Draw as (S.drawMode): the game's own boxes, a box model file (extras.quark version 2: the same box path, without the
   projectiles' flicker / blink / wobble) or a mesh (no outline, every triangle casts a shadow, squashed whole). */
(function () {
  'use strict';
  const Q = window.Q, R = Q.R, M = R.mat;
  const S = Q.scene = {};

  const DEFAULT_VIEW = { mode: 'orbit', palette: '', team: 'enemy', crouch: false, play: true, speed: 1, drawAs: 'auto', hitbox: false, grid: true,
    ground: true, shadows: true, sunVis: 1, outline: 'black', outlineW: 1, fov: 100, vmX: 0, vmY: 0, vmZ: 0, vmScale: 1, vmColor: '',
    yaw: 0, pitch: 0, fpWeapon: 'models/weapon/ROCKET', tpPlayer: 'models/player/STANDARD', holdIt: false, crown: false, crownPlace: 1,
    landed: false, lineupTeams: false, bright: 1, fpLineup: true, noViewmodel: false, bombLineup: false, bombBlink: false, gateReached: false, cartNeutral: false };
  S.view = Object.assign({}, DEFAULT_VIEW);
  try { Object.assign(S.view, JSON.parse(localStorage.getItem('qme.view') || '{}')); } catch (e) { }
  S.saveView = () => { try { localStorage.setItem('qme.view', JSON.stringify(S.view)); } catch (e) { } };
  S.resetView = () => { Object.assign(S.view, DEFAULT_VIEW); S.saveView(); };
  S.orbit = { target: [0, 1, 0], dist: 4, yaw: 0.6, pitch: -0.25 };
  S.sel = null;        // the model being edited (key)
  S.part = -1;         // its selected part
  S.dirty = true;
  S.time = 0;

  S.palette = () => Q.palettes.find(p => p.id === S.view.palette) || Q.palettes[0];
  S.teamColor = () => S.view.team === 'friendly' ? Q.TEAM.friendly : Q.TEAM.enemy;
  S.outlineColor = function () {
    const c = Q.OUTLINE_COLORS[S.view.outline];
    return c || S.teamColor();   // "team": the outline in the team colour
  };
  // how a model is drawn (game.cpp): 'builtin' (the game's own boxes: outlines, part animations), 'boxfile' (an
  // override file with its box list, extras.quark version 2: the same box path, outlines, crouch per box, visOnly,
  // shadows, but no flame flicker / core blink / wobble), 'mesh' (any other override: lit, every triangle casts, no
  // outline, squashed whole when crouching)
  const DRAW_AS = { boxes: 'builtin', override: 'mesh' };   // (older saved views)
  if (DRAW_AS[S.view.drawAs]) S.view.drawAs = DRAW_AS[S.view.drawAs];
  S.drawMode = function (key) {
    const cur = Q.current(key);
    if (cur.type !== 'boxes') return 'mesh';
    const d = S.view.drawAs;
    if (d === 'builtin' || d === 'boxfile' || d === 'mesh') return d;
    return cur.edited || cur.shipped ? 'boxfile' : 'builtin';
  };

  // --- GPU meshes per (model, draw mode, crouch), refilled when the model changes -------------------------------------------
  const cache = new Map();
  const HS = Q.CROUCH_H / Q.PLAYER_H;   // kPlayerCrouchHeight / kPlayerHeight
  S.meshFor = function (key, mode, crouch) {
    const id = key + '|' + mode + '|' + (crouch || ''), v = Q.ver[key] || 0;
    let e = cache.get(id);
    if (!e) { e = { mesh: R.createMesh(), ver: -1 }; cache.set(id, e); }
    if (e.ver !== v) { fill(e.mesh, key, mode, null, crouch); e.ver = v; }
    return e.mesh;
  };
  // crouch: null, 'parts' (a player: centres times hs, half heights where the box says crouch), 'centres' (the bomb
  // on a carrier's back); a mesh is squashed by its matrix instead
  function fill(mesh, key, mode, parts, crouch) {
    const cur = Q.current(key), m = Q.models[key], kind = m ? m.kind : '', ref = Q.refOf(key);
    if (cur.type === 'boxes') {
      const p = parts || cur.parts;
      if (mode === 'mesh') R.fillMesh(mesh, Q.boxes.toMesh(p, ref), ref);
      else R.fillBoxes(mesh, p, { outline: true, flatAlpha: kind === 'projectile', flatShaded: false, hs: crouch ? HS : 1, crouch });
    } else if (cur.type === 'mesh') R.fillMesh(mesh, cur.mesh, ref);
    else R.fillBoxes(mesh, [], {});
  }
  // the game's per-part animations: the projectile ones on its own boxes only (a box override is drawn still), the
  // locator's screen pulse on the viewmodel either way
  const scaleCol = (d, s, k) => {
    for (let q = 0; q < 3; q++) { d.color[q] = s.color[q] * k; if (s.mul) d.mul[q] = s.mul[q] * k; if (s.add) d.add[q] = s.add[q] * k; if (s.teamMul) d.teamMul[q] = s.teamMul[q] * k; }
  };
  function partAnim(key, mode, role) {
    const m = Q.models[key];
    if (!m || mode === 'mesh') return null;
    const v = S.view;
    if (m.kind === 'weapon' && m.name === 'LOCATOR' && role === 'viewmodel') return (t, src, dst) => {   // the screen pulses when it's ready
      const g = 0.85 + 0.35 * Math.sin(t * 6);
      for (let i = 0; i < src.length; i++) scaleCol(dst[i], src[i], src[i].slot === 2 ? g : 1);
    };
    if (mode !== 'builtin') return null;
    if (m.kind === 'projectile' && m.name === 'ROCKET') return (t, src, dst) => {   // the flame flickers (0.8 + 0.2 sin(60 t + id))
      const fl = 0.8 + 0.2 * Math.sin(t * 60 + 1);
      for (let i = 0; i < src.length; i++) { const s = src[i], d = dst[i]; const flame = (s.slot | 0) === 0 && Math.max(s.color[0], s.color[1], s.color[2]) > 1.5; for (let k = 0; k < 3; k++) d.h[k] = flame ? s.h[k] * fl : s.h[k]; }
    };
    if (m.kind === 'projectile' && m.name === 'STICKY' && v.landed) return (t, src, dst) => {   // stuck: the core blinks
      const core = 0.6 + 0.4 * ((t * 2.5) % 1 < 0.5 ? 1 : 0);
      for (let i = 0; i < src.length; i++) scaleCol(dst[i], src[i], src[i].slot === 1 ? core : 1);
    };
    if (m.kind === 'projectile' && m.name === 'BUBBLE') return (t, src, dst) => {   // the shell wobbles (0.03 sin(14 t + id))
      const wob = 0.03 * Math.sin(t * 14 + 1);
      for (let i = 0; i < src.length; i++) { const s = src[i], d = dst[i]; const sh = (s.alpha == null ? 1 : s.alpha) < 1; d.h[0] = s.h[0] + (sh ? wob : 0); d.h[1] = s.h[1]; d.h[2] = s.h[2] + (sh ? wob : 0); }
    };
    return null;
  }

  // --- building the scene ----------------------------------------------------------------------------------------------
  const scene = { cam: { pos: [0, 1.6, 0], yaw: 0, pitch: 0, fov: 100 }, pal: Q.DEFAULT_PALETTE, sky: true, ground: { y: 0, grid: true }, shadows: true, bright: 1,
    items: [], trails: { data: new Float32Array(R.FL * 6 * 16), count: 0 }, lines: { data: new Float32Array(7 * 8192), count: 0 }, overlay: { data: new Float32Array(7 * 4096), count: 0 } };
  S.current = scene;
  let pool = [];   // items, reused between rebuilds
  function item(i) {
    if (!pool[i]) pool[i] = { mesh: null, model: new Float32Array(16), tint: new Float32Array(18), outlineW: 0, outline: null, sunVis: 1, view: false, castShadow: true,
      key: null, kind: '', role: '', anim: null, ownMesh: null, src: null, scratch: null, spin: null, highlight: null, hidden: false, crouch: null };
    const it = pool[i];
    it.anim = null; it.spin = null; it.highlight = null; it.hidden = false; it.castShadow = true; it.view = false; it.role = ''; it.key = null; it.trail = null; it.crouch = null;
    return it;
  }
  // a model as an item: its mesh, slot colours, outline. ctx: the runtime palette's context (Q.runtimePalette) plus
  // role, crouch ('parts' / 'centres' when crouching)
  function modelItem(it, key, ctx) {
    const m = Q.models[key], kind = m ? m.kind : (ctx.kind || ''), name = m ? m.name : '';
    const mode = S.drawMode(key);
    it.key = key; it.kind = kind; it.mode = mode; it.role = ctx.role || ''; it.crouch = mode === 'mesh' ? null : (ctx.crouch || null);
    const pal = Q.runtimePalette(Object.assign({ team: S.teamColor(), map: scene.pal }, ctx, { kind, name }));
    Q.slotColors(kind, name, pal, it.tint);
    it.outlineW = mode === 'mesh' ? 0 : S.view.outlineW;
    it.outline = S.outlineColor();
    it.sunVis = S.view.sunVis;
    const anim = S.view.play ? partAnim(key, mode, it.role) : null;
    if (anim) {   // its own mesh, refilled each frame from a scratch copy of the parts (no allocation per frame)
      const src = Q.current(key).parts;
      if (!it.ownMesh) it.ownMesh = R.createMesh();
      if (it.src !== src) { it.src = src; it.scratch = src.map(Q.clonePart); }
      it.anim = anim; it.mesh = it.ownMesh;
      fill(it.mesh, key, mode, it.scratch, it.crouch);
    } else it.mesh = S.meshFor(key, mode, it.crouch);
    return it;
  }
  function boxItem(it, parts) {   // scene boxes that are no model (the crown's letter; an arm when the export has none)
    if (!it.ownMesh) it.ownMesh = R.createMesh();
    it.mesh = it.ownMesh;
    R.fillBoxes(it.mesh, parts, { outline: true, flatAlpha: false });
    it.tint.fill(1); it.outlineW = S.view.outlineW; it.outline = S.outlineColor(); it.sunVis = S.view.sunVis; it.kind = ''; it.mode = 'builtin';
    return it;
  }
  const players = () => Q.order.filter(k => Q.models[k].kind === 'player');
  S.players = players;
  S.weaponKey = function () {
    if (S.sel && Q.models[S.sel] && Q.models[S.sel].kind === 'weapon') return S.sel;
    return Q.models[S.view.fpWeapon] ? S.view.fpWeapon : Q.order.find(k => Q.models[k].kind === 'weapon');
  };
  S.playerKey = function () {
    if (S.sel && Q.models[S.sel] && Q.models[S.sel].kind === 'player') return S.sel;
    return Q.models[S.view.tpPlayer] ? S.view.tpPlayer : players()[0];
  };
  S.crownKey = () => Q.order.find(k => Q.models[k].kind === 'crown');
  const prop = name => Q.models['models/prop/' + name] ? 'models/prop/' + name : null;
  S.prop = prop;
  const selIs = (kind, name) => { const m = S.sel && Q.models[S.sel]; return !!m && m.kind === kind && (!name || m.name === name); };
  S.modesFor = function (key) {   // which views make sense for this model
    const m = Q.models[key], k = m ? m.kind : '';
    if (k === 'player') return ['orbit', 'tp', 'lineup'];
    if (k === 'weapon') return ['orbit', 'fp', 'tp', 'lineup'];
    if (k === 'projectile') return ['orbit', 'flight'];
    if (k === 'crown') return ['orbit', 'tp', 'lineup'];
    if (k === 'prop') {
      if (m.name === 'GUN_ARM' || m.name === 'ARM' || m.name === 'BOMB_PACK') return ['orbit', 'tp', 'lineup'];
      if (m.name === 'BOMB') return ['orbit', 'lineup', 'track'];
      if (m.name === 'CART' || m.name === 'GATE') return ['orbit', 'track'];
    }
    return ['orbit'];
  };
  // what the view shows besides the model (game modes): "it" holding the weapon, the bomb carried
  S.holdIt = () => S.view.holdIt || selIs('prop', 'ARM');
  S.bombMode = () => !S.holdIt() && (S.view.bombLineup || selIs('prop', 'BOMB_PACK') || selIs('prop', 'BOMB'));

  // a player at feet `pos` facing yaw, with its gun arm (or "it": the arm holding the weapon), the bomb on its back,
  // the crown; returns the next item index (game.cpp drawWorld)
  const PM = new Float32Array(16), TMP = new Float32Array(16);
  function addPlayer(n, pkey, px, py, pz, yaw, opts) {
    const crouch = !!S.view.crouch, hs = crouch ? HS : 1, bs = 1;
    const it = modelItem(item(n++), pkey, { kind: 'player', role: 'player', crouch: crouch ? 'parts' : null });
    M.trs(it.model, px, py, pz, yaw, 0, bs, it.mode === 'mesh' ? bs * hs : bs, bs);
    if (opts.highlight) it.highlight = opts.highlight;
    const at = h => py + h * hs * bs;
    const rx = Math.cos(yaw), rz = -Math.sin(yaw);   // right
    const sx = px + rx * 0.3 * bs, sy = at(1.3), sz = pz + rz * 0.3 * bs;   // the shoulder
    const arm = (name, fallback) => {   // the arm prop from the shoulder along the aim (pitch 0 here), at body scale
      const k = prop(name), a = k ? modelItem(item(n++), k, { kind: 'prop', role: 'arm' }) : boxItem(item(n++), fallback);
      a.role = 'arm';
      M.trs(a.model, sx, sy, sz, yaw, 0, bs, bs, bs);
    };
    if (opts.held) {   // "it": the arm and the weapon's own model at 2x on the hand
      arm('ARM', ARM_HELD);
      const w = modelItem(item(n++), opts.held, { kind: 'weapon', use: 'held', role: 'held', team: S.teamColor() });
      M.trs(PM, 0, 0.02 * bs, -0.5 * bs, 0, 0, 2 * bs, 2 * bs, 2 * bs);
      M.trs(TMP, sx, sy, sz, yaw, 0, 1, 1, 1);
      M.mul(w.model, TMP, PM);
    } else {
      arm('GUN_ARM', ARM_GUN);
      const pk = opts.pack && prop('BOMB_PACK');
      if (pk) {   // the bomb on its carrier's back (player space; crouched: its height follows, its size doesn't)
        const b = modelItem(item(n++), pk, { kind: 'prop', role: 'pack', crouch: crouch ? 'centres' : null });
        M.trs(b.model, px, py, pz, yaw, 0, bs, b.mode === 'mesh' ? bs * hs : bs, bs);
      }
    }
    if (opts.crown) {
      const ck = S.crownKey();
      if (ck) {
        const ty = py + Q.PLAYER_H * hs * bs + 0.04 * bs;
        const c = modelItem(item(n++), ck, { kind: 'crown', role: 'crown', crownPlace: S.view.crownPlace });
        c.castShadow = false;
        M.trs(c.model, px, ty, pz, yaw, 0, bs, bs, bs);
        const l = boxItem(item(n++), S.view.crownPlace === 1 ? LETTER_GOLD : LETTER_SILVER); l.role = 'letter'; l.castShadow = false;
        M.trs(l.model, px, ty, pz, yaw, 0, bs, bs, bs);
      }
    }
    return n;
  }
  const ARM_GUN = [{ c: [0, 0, -0.35], h: [0.06, 0.07, 0.35], color: Q.GUN_ARM, slot: 0, alpha: 1, shadow: true }];
  const ARM_HELD = [{ c: [0, 0, -0.22], h: [0.055, 0.06, 0.22], color: Q.GUN_ARM, slot: 0, alpha: 1, shadow: true }];
  // the crown's board letter (game.cpp: a 3x5 pixel font, "Q" for the main board), in crown space
  function letter(dark) {
    const rows = [7, 5, 5, 7, 1], px = 0.022, out = [], x0 = -px;
    for (let r = 0; r < 5; r++) for (let c = 0; c < 3; c++) if (rows[r] >> (2 - c) & 1)
      out.push({ c: [x0 + c * px, 0.06 + (2 - r) * px, -0.175], h: [px * 0.5, px * 0.5, 0.012], color: dark, slot: 0, alpha: 1, shadow: false });
    return out;
  }
  const LETTER_GOLD = letter([0.35, 0.18, 0.02]), LETTER_SILVER = letter([0.2, 0.22, 0.26]);
  // a prop on the ground (drawProp): origin, turned by yaw; sunVis: the bomb and the gates are always lit
  function addProp(n, name, x, y, z, yaw, ctx, lit) {
    const k = prop(name);
    if (!k) return n;
    const it = modelItem(item(n++), k, Object.assign({ kind: 'prop', role: 'prop' }, ctx));
    if (lit) it.sunVis = 1;
    M.trs(it.model, x, y, z, yaw, 0, 1, 1, 1);
    return n;
  }

  // the viewmodel: camera space T(cam) Ry(yaw) Rx(pitch) T(at) S(scale)
  function addViewmodel(n, wkey, cam) {
    const v = S.view;
    const vmColor = v.vmColor ? Q.hexToLin(v.vmColor) : null;
    const it = modelItem(item(n++), wkey, { kind: 'weapon', role: 'viewmodel', vmColor: vmColor || Q.TEAM.friendly });
    it.view = true; it.sunVis = 1; it.castShadow = false;
    const at = [0.2 + v.vmX * 0.0005, -0.17 - v.vmY * 0.0005, -0.55 - v.vmZ * 0.001];
    M.trs(TMP, cam.pos[0], cam.pos[1], cam.pos[2], cam.yaw, cam.pitch, 1, 1, 1);
    M.trs(PM, at[0], at[1], at[2], 0, 0, v.vmScale, v.vmScale, v.vmScale);
    M.mul(it.model, TMP, PM);
    return n;
  }

  // the orbit camera: around a target, the game's camera model (eye, yaw, pitch)
  function orbitCam(cam) {
    const o = S.orbit, f = R.forward(o.yaw, o.pitch);
    cam.pos[0] = o.target[0] - f[0] * o.dist; cam.pos[1] = o.target[1] - f[1] * o.dist; cam.pos[2] = o.target[2] - f[2] * o.dist;
    cam.yaw = o.yaw; cam.pitch = o.pitch;
  }
  S.frame = function (key) {   // aim the orbit camera at a model (or the scene's subject)
    const v = S.view, o = S.orbit, m = Q.models[key];
    let mn = [-0.5, 0, -0.5], mx = [0.5, 2, 0.5];
    if (v.mode === 'orbit' && key) { const b = S.bounds(key); if (b) { mn = b.mn; mx = b.mx; } }
    else if (v.mode === 'flight') { mn = [-0.5, 1, -2]; mx = [0.5, 2, 2]; }
    else if (v.mode === 'track') { mn = [-2, 0, -3]; mx = [2, 2.8, 3]; }
    const c = [(mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2], r = Math.max(0.05, Math.hypot(mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]) / 2);
    o.target = c;
    const cv = document.getElementById('gl'), aspect = cv && cv.clientHeight ? cv.clientWidth / cv.clientHeight : 16 / 9;
    const hf = S.view.fov * Math.PI / 180, vf = 2 * Math.atan(Math.tan(hf / 2) / aspect);
    o.dist = Math.max(0.15, r / Math.sin(Math.min(hf, vf) / 2) * 1.08);
    if (v.mode === 'orbit' && m && m.kind === 'weapon') { o.yaw = 2.2; o.pitch = -0.3; }
    else if (v.mode === 'tp' && m && m.name === 'BOMB_PACK') { o.yaw = 0.55; o.pitch = -0.2; }   // (from behind: the pack)
    else if (v.mode === 'orbit' || v.mode === 'tp') { o.yaw = Math.PI + 0.55; o.pitch = -0.2; }
    else if (v.mode === 'track') { o.yaw = Math.PI * 0.75; o.pitch = -0.3; }
    else { o.yaw = 1.3; o.pitch = -0.15; }
    S.dirty = true;
  };
  S.boundsOf = function (parts) {
    if (!parts || !parts.length) return null;
    const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (const p of parts) for (let k = 0; k < 3; k++) { mn[k] = Math.min(mn[k], p.c[k] - p.h[k]); mx[k] = Math.max(mx[k], p.c[k] + p.h[k]); }
    return { mn, mx };
  };
  // the part animations a box override loses (the locator's pulse it keeps)
  S.hasPartAnim = function (key) {
    const m = Q.models[key];
    return !!m && m.kind === 'projectile' && (m.name === 'ROCKET' || m.name === 'STICKY' || m.name === 'BUBBLE');
  };
  S.bounds = function (key) {
    const cur = Q.current(key);
    if (cur.type === 'boxes') return S.boundsOf(cur.parts);
    if (cur.type === 'mesh') return Q.gltf.bounds(cur.mesh);
    return null;
  };

  // rebuild the scene's items (when the selection, the view or a model changes); animate() moves them each frame
  S.build = function () {
    const v = S.view, key = S.sel, m = key ? Q.models[key] : null, kind = m ? m.kind : '';
    scene.pal = S.palette();
    scene.shadows = v.shadows; scene.bright = v.bright || 1;
    scene.cam.fov = v.fov;
    let n = 0;
    const modes = key ? S.modesFor(key) : ['orbit'];
    if (!modes.includes(v.mode)) v.mode = 'orbit';
    scene.ground = v.ground ? { y: 0, grid: v.grid && (v.mode === 'orbit' || v.mode === 'tp' || v.mode === 'flight' || v.mode === 'track') } : null;
    S.focus = null;   // the item being edited (picking, gizmo)
    S.trackLine = null;
    if (v.mode === 'orbit' && key) {
      const crouch = (kind === 'player' || (m && m.name === 'BOMB_PACK')) && v.crouch;
      const it = modelItem(item(n++), key, { kind, role: 'subject', crouch: crouch ? (kind === 'player' ? 'parts' : 'centres') : null, gateReached: v.gateReached,
        cartTeam: v.cartNeutral ? null : undefined, bombBlink: v.bombBlink });
      M.trs(it.model, 0, 0, 0, 0, 0, 1, crouch && it.mode === 'mesh' ? HS : 1, 1);
      if (kind === 'projectile') it.spin = projSpin(key, false);
      S.focus = it;
      if (scene.ground) {
        const b = S.bounds(key);
        scene.ground.y = kind === 'player' || kind === 'crown' || kind === 'prop' || !b ? (b && kind === 'prop' && (m.name === 'GUN_ARM' || m.name === 'ARM') ? Math.min(0, b.mn[1] - 0.05) : 0) : Math.min(0, b.mn[1] - 0.05);
      }
    } else if (v.mode === 'tp') {
      const pkey = S.playerKey();
      const held = kind === 'weapon' ? key : (S.holdIt() ? S.weaponKey() : null);
      n = addPlayer(n, pkey, 0, 0, 0, 0, { held, crown: kind === 'crown' || v.crown, pack: S.bombMode() });
    } else if (v.mode === 'fp') {
      const cam = scene.cam;
      cam.pos[0] = 0; cam.pos[1] = v.crouch ? Q.CROUCH_EYE : Q.EYE; cam.pos[2] = 0; cam.yaw = v.yaw * Math.PI / 180; cam.pitch = v.pitch * Math.PI / 180;
      if (v.fpLineup) n = lineup(n, cam, false);
      if (!v.noViewmodel) n = addViewmodel(n, S.weaponKey(), cam);
    } else if (v.mode === 'lineup') {
      const cam = scene.cam;
      cam.pos[0] = 0; cam.pos[1] = Q.EYE; cam.pos[2] = 0; cam.yaw = v.yaw * Math.PI / 180; cam.pitch = v.pitch * Math.PI / 180;
      n = lineup(n, cam, true);
      n = addViewmodel(n, S.weaponKey(), cam);
    } else if (v.mode === 'flight' && key) {
      const it = modelItem(item(n++), key, { kind, role: 'subject' });
      it.spin = projSpin(key, true);
      it.trail = Q.PROJ[m.name] && Q.PROJ[m.name].trail || null;
      S.focus = it;
    } else if (v.mode === 'track') {
      // a payload track (game.cpp): the cart at the origin pushed toward -Z, a checkpoint gate ahead (not reached:
      // grey) and one behind (reached: the attackers' colour), the track line; a dropped bomb beside it
      n = addProp(n, 'CART', 0, 0, 0, 0, { cartTeam: v.cartNeutral ? null : undefined }, false);
      n = addProp(n, 'GATE', 0, 0, -7, 0, { gateReached: false }, true);
      n = addProp(n, 'GATE', 0, 0, 7, 0, { gateReached: true }, true);
      if (kind === 'prop' && m.name === 'BOMB') n = addProp(n, 'BOMB', 3, 0, 0, 0, { bombBlink: v.bombBlink }, true);
      S.trackLine = { col: v.cartNeutral ? [1, 1, 1] : S.teamColor() };
    }
    if (!S.focus) for (let i = 0; i < n; i++) if (pool[i].key === key) { S.focus = pool[i]; break; }   // (the model being edited)
    scene.items.length = 0;
    for (let i = 0; i < n; i++) scene.items.push(pool[i]);
    if (v.mode === 'orbit' || v.mode === 'tp' || v.mode === 'flight' || v.mode === 'track') orbitCam(scene.cam);
    if (S.camOverride) { const c = S.camOverride; scene.cam.pos[0] = c.pos[0]; scene.cam.pos[1] = c.pos[1]; scene.cam.pos[2] = c.pos[2]; scene.cam.yaw = c.yaw; scene.cam.pitch = c.pitch; }
    S.animate(S.time, true);
    S.dirty = true;
  };
  // the lineup (game.cpp, --models): the players 5 m ahead, 1.5 m apart, facing you; in a team mode every other one
  // is on team B; "it" modes: each turned aside holding the weapon; bomb mode: turned away carrying the bomb, a
  // dropped bomb 3 m ahead
  function lineup(n, cam, withCrowns) {
    const v = S.view, f = [-Math.sin(cam.yaw), 0, -Math.cos(cam.yaw)], rt = [Math.cos(cam.yaw), 0, -Math.sin(cam.yaw)];
    const list = players();
    const crownOn = withCrowns && (v.crown || selIs('crown'));
    const it = S.holdIt(), bomb = S.bombMode();
    const saveTeam = v.team;
    for (let k = 0; k < list.length; k++) {
      if (v.lineupTeams) v.team = k % 2 ? 'enemy' : 'friendly';
      const x = f[0] * 5 + rt[0] * ((k - 2) * 1.5), z = f[2] * 5 + rt[2] * ((k - 2) * 1.5);
      n = addPlayer(n, list[k], x, 0, z, cam.yaw + Math.PI - (it ? 1.0 : bomb ? 2.3 : 0), { held: it ? S.weaponKey() : null, crown: crownOn, pack: bomb });
      v.team = saveTeam;
    }
    if (bomb) n = addProp(n, 'BOMB', f[0] * 3, 0, f[2] * 3, 0, { bombBlink: v.bombBlink }, true);
    return n;
  }
  function projSpin(key, flying) {
    const m = Q.models[key];
    if (!m || m.kind !== 'projectile') return null;
    const v = S.view;
    if (m.name === 'STICKY') return v.landed ? null : 'stickySpin';
    if (m.name === 'KNIFE') return flying || v.play ? 'knifeSpin' : null;
    return null;
  }

  // per frame: spins, the flight path, part animations, the trail; no allocation
  S.animate = function (t, force) {
    const v = S.view;
    let moved = !!force;
    scene.trails.count = 0;
    if (S.trackLine) addLine(0, 0.05, 20, 0, 0.05, -20, 0.25, S.trackLine.col, 0.35);   // (Renderer::line along the track)
    for (const it of scene.items) {
      if (it.anim && v.play) {
        it.anim(t, Q.current(it.key).parts, it.scratch);
        fill(it.mesh, it.key, it.mode, it.scratch, it.crouch);
        moved = true;
      }
      if (v.mode === 'flight' && it.role === 'subject') {
        const p = Q.PROJ[Q.models[it.key].name] || { speed: 30 }, L = 12;
        const sp = p.speed * (v.speed || 1) * 0.25;   // (a quarter speed at 1x, to see it)
        const z = v.play ? L / 2 - ((t * sp) % L) : 0;
        let my = 0, mp = 0;
        if (it.spin === 'stickySpin' && v.play) my = t * 6;
        if (it.spin === 'knifeSpin' && v.play) mp = t * 25;
        M.trs(it.model, 0, 1.5, z, my, mp, 1, 1, 1);
        if (it.trail) { const tr = it.trail; addLine(0, 1.5, z - tr.from, 0, 1.5, z + tr.len, tr.w, S.teamColor(), tr.a); }
        moved = true;
      } else if (v.mode === 'orbit' && it.role === 'subject' && it.spin && v.play) {
        M.trs(it.model, 0, 0, 0, it.spin === 'stickySpin' ? t * 6 : 0, it.spin === 'knifeSpin' ? t * 25 : 0, 1, 1, 1);
        moved = true;
      }
    }
    if (moved) S.dirty = true;
  };
  // Renderer::line: a camera-facing quad from a to b, w wide (projectile trails, the payload track)
  function trailVertex(x0, y0, z0, col, a) {
    const T = scene.trails, d = T.data, F = R.FL;
    if ((T.count + 1) * F > d.length) return;
    let o = T.count * F;
    d[o++] = x0; d[o++] = y0; d[o++] = z0; d[o++] = col[0]; d[o++] = col[1]; d[o++] = col[2]; d[o++] = a; d[o++] = 0;
    for (let q = 0; q < 6; q++) d[o++] = 0;
    T.count++;
  }
  function addLine(ax, ay, az, bx, by, bz, w, col, a) {
    const cam = scene.cam.pos;
    let dx = bx - ax, dy = by - ay, dz = bz - az; const dl = Math.hypot(dx, dy, dz) || 1; dx /= dl; dy /= dl; dz /= dl;
    let tx = cam[0] - (ax + bx) / 2, ty = cam[1] - (ay + by) / 2, tz = cam[2] - (az + bz) / 2;
    // side = normalize(cross(d, toCam)) * w / 2
    let sx = dy * tz - dz * ty, sy = dz * tx - dx * tz, sz = dx * ty - dy * tx; const sl = Math.hypot(sx, sy, sz) || 1;
    sx = sx / sl * w / 2; sy = sy / sl * w / 2; sz = sz / sl * w / 2;
    // quad a - side, b - side, b + side, a + side as triangles 0 1 2, 0 2 3
    trailVertex(ax - sx, ay - sy, az - sz, col, a); trailVertex(bx - sx, by - sy, bz - sz, col, a); trailVertex(bx + sx, by + sy, bz + sz, col, a);
    trailVertex(ax - sx, ay - sy, az - sz, col, a); trailVertex(bx + sx, by + sy, bz + sz, col, a); trailVertex(ax + sx, ay + sy, az + sz, col, a);
  }

  // --- overlays: hitbox, selection, gizmo ---------------------------------------------------------------------------------
  function line(L, a, b, c) {
    const d = L.data; if ((L.count + 2) * 7 > d.length) return;
    let o = L.count * 7;
    d[o++] = a[0]; d[o++] = a[1]; d[o++] = a[2]; d[o++] = c[0]; d[o++] = c[1]; d[o++] = c[2]; d[o++] = c[3];
    d[o++] = b[0]; d[o++] = b[1]; d[o++] = b[2]; d[o++] = c[0]; d[o++] = c[1]; d[o++] = c[2]; d[o++] = c[3];
    L.count += 2;
  }
  S.xf = function (m, p, o) { o = o || [0, 0, 0]; o[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]; o[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]; o[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]; return o; };
  function boxLines(L, m, c, h, col) {
    const corners = [];
    for (let i = 0; i < 8; i++) corners.push(S.xf(m, [c[0] + (i & 1 ? h[0] : -h[0]), c[1] + (i & 2 ? h[1] : -h[1]), c[2] + (i & 4 ? h[2] : -h[2])]));
    const E = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
    for (const [a, b] of E) line(L, corners[a], corners[b], col);
  }
  S.gizmoAxes = null;
  S.updateOverlay = function () {   // (on selection / view changes, not per frame)
    const v = S.view, L = scene.lines, O = scene.overlay;
    L.count = 0; O.count = 0;
    if (v.hitbox) for (const it of scene.items) {
      if (it.role !== 'player') continue;
      const hs = v.crouch ? Q.CROUCH_H / Q.PLAYER_H : 1, H = Q.PLAYER_H * hs, w = Q.HALF_W;
      // hitbox: an axis-aligned box at the feet (not turned with the model); the head: the top 20 %, 45 % of the width
      const feet = [it.model[12], it.model[13], it.model[14]], I = M.ident(new Float32Array(16));
      boxLines(O, I, [feet[0], feet[1] + H / 2, feet[2]], [w, H / 2, w], [1, 0.85, 0.2, 0.9]);
      boxLines(O, I, [feet[0], feet[1] + H - H * Q.HEAD_FRAC / 2, feet[2]], [w * Q.HEAD_W, H * Q.HEAD_FRAC / 2, w * Q.HEAD_W], [1, 0.3, 0.3, 0.95]);
    }
    S.gizmoAxes = null;
    const f = S.focus;
    if (f && S.part >= 0 && S.editable()) {
      const parts = Q.current(f.key).parts, p0 = parts && parts[S.part], p = p0 && S.geom(p0, f);
      if (p) {
        boxLines(O, f.model, p.c, p.h.map(x => x + 0.0005), [0.3, 0.75, 1, 1]);
        const c = S.xf(f.model, p.c), len = Math.max(0.04, Math.max(p.h[0], p.h[1], p.h[2]) * 1.6 + 0.03);
        const axes = [];
        for (let a = 0; a < 3; a++) {
          const e = [0, 0, 0]; e[a] = 1;
          const tip = S.xf(f.model, [p.c[0] + e[0] * (p.h[0] + len), p.c[1] + e[1] * (p.h[1] + len), p.c[2] + e[2] * (p.h[2] + len)]);
          const face = S.xf(f.model, [p.c[0] + e[0] * p.h[0], p.c[1] + e[1] * p.h[1], p.c[2] + e[2] * p.h[2]]);
          const col = [a === 0 ? 1 : 0.25, a === 1 ? 1 : 0.25, a === 2 ? 1 : 0.25, 1];
          line(O, face, tip, col);
          // the handle: a small cross at the tip
          const s = len * 0.12;
          for (let b = 0; b < 3; b++) { if (b === a) continue; const d = [0, 0, 0]; d[b] = s; line(O, [tip[0] - d[0], tip[1] - d[1], tip[2] - d[2]], [tip[0] + d[0], tip[1] + d[1], tip[2] + d[2]], col); }
          axes.push({ axis: a, origin: c, face, tip });
        }
        S.gizmoAxes = axes;
      }
    }
    S.dirty = true;
  };
  // a box where the item draws it (a crouch moves its centre, squashes its height where it says so); yk: what a
  // model-space y change becomes on screen (the gizmo divides by it)
  S.geom = function (p, it) {
    if (!it || !it.crouch) return p;
    return { c: [p.c[0], p.c[1] * HS, p.c[2]], h: [p.h[0], it.crouch === 'parts' && p.crouch ? p.h[1] * HS : p.h[1], p.h[2]] };
  };
  S.yk = (p, it, size) => !it || !it.crouch ? 1 : size ? (it.crouch === 'parts' && p.crouch ? HS : 1) : HS;
  S.editable = function () {
    const f = S.focus;
    return !!(f && f.key === S.sel && Q.current(f.key).type === 'boxes');
  };

  // --- picking: a screen point to a ray, then the nearest box of the focused model ---------------------------------------
  const inv = new Float32Array(16), invM = new Float32Array(16);
  S.ray = function (x, y, w, h) {   // pixels -> world ray {o, d}
    if (!M.invert(inv, R.viewProj)) return null;
    const nx = x / w * 2 - 1, ny = 1 - y / h * 2;
    const un = (z) => { const v = [nx, ny, z, 1], o = [0, 0, 0, 0]; for (let r = 0; r < 4; r++) o[r] = inv[r] * v[0] + inv[4 + r] * v[1] + inv[8 + r] * v[2] + inv[12 + r] * v[3]; return [o[0] / o[3], o[1] / o[3], o[2] / o[3]]; };
    const a = un(-1), b = un(1), d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], l = Math.hypot(d[0], d[1], d[2]);
    return { o: a, d: [d[0] / l, d[1] / l, d[2] / l] };
  };
  S.pick = function (ray) {
    const f = S.focus;
    if (!f || !ray || !S.editable()) return -1;
    if (!M.invert(invM, f.model)) return -1;
    const o = S.xf(invM, ray.o), e = S.xf(invM, [ray.o[0] + ray.d[0], ray.o[1] + ray.d[1], ray.o[2] + ray.d[2]]), d = [e[0] - o[0], e[1] - o[1], e[2] - o[2]];
    let best = -1, bt = Infinity;
    Q.current(f.key).parts.forEach((p0, i) => {
      const p = S.geom(p0, f);
      let t0 = -Infinity, t1 = Infinity;
      for (let k = 0; k < 3; k++) {
        const lo = p.c[k] - p.h[k], hi = p.c[k] + p.h[k];
        if (Math.abs(d[k]) < 1e-12) { if (o[k] < lo || o[k] > hi) return; continue; }
        let a = (lo - o[k]) / d[k], b = (hi - o[k]) / d[k];
        if (a > b) { const s = a; a = b; b = s; }
        t0 = Math.max(t0, a); t1 = Math.min(t1, b);
      }
      if (t0 <= t1 && t1 > 0 && t0 < bt) { bt = Math.max(t0, 0); best = i; }
    });
    return best;
  };
  // the gizmo handle under a screen point (projected distance to the axis segment), or null
  S.pickGizmo = function (x, y, w, h) {
    if (!S.gizmoAxes) return null;
    const proj = p => { const m = R.viewProj, cx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], cy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13], cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15]; return cw > 0 ? [(cx / cw * 0.5 + 0.5) * w, (0.5 - cy / cw * 0.5) * h] : null; };
    let best = null, bd = 9;
    for (const ax of S.gizmoAxes) {
      const a = proj(ax.face), b = proj(ax.tip);
      if (!a || !b) continue;
      const dx = b[0] - a[0], dy = b[1] - a[1], l2 = dx * dx + dy * dy || 1;
      const t = Math.max(0, Math.min(1, ((x - a[0]) * dx + (y - a[1]) * dy) / l2));
      const dd = Math.hypot(a[0] + dx * t - x, a[1] + dy * t - y);
      if (dd < bd) { bd = dd; best = ax; }
    }
    return best;
  };
  // the point on a world axis line (through origin, direction dir) nearest a ray: its parameter along the axis
  S.axisParam = function (origin, dir, ray) {
    const w0 = [origin[0] - ray.o[0], origin[1] - ray.o[1], origin[2] - ray.o[2]];
    const a = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2], b = dir[0] * ray.d[0] + dir[1] * ray.d[1] + dir[2] * ray.d[2], c = 1;
    const d = dir[0] * w0[0] + dir[1] * w0[1] + dir[2] * w0[2], e = ray.d[0] * w0[0] + ray.d[1] * w0[1] + ray.d[2] * w0[2];
    const den = a * c - b * b;
    if (Math.abs(den) < 1e-9) return null;
    return (b * e - c * d) / den;
  };

  // --- thumbnails ------------------------------------------------------------------------------------------------------
  let thumbTarget = null, thumbCanvas = null;
  const thumbScene = { cam: { pos: [0, 0, 0], yaw: 0, pitch: 0, fov: 40 }, pal: Q.DEFAULT_PALETTE, sky: true, ground: null, shadows: false, bright: 1, items: [], trails: null, lines: null, overlay: null };
  S.thumbnail = function (key, size) {
    size = size || 96;
    if (!thumbTarget) { thumbTarget = R.target(size, size); thumbCanvas = document.createElement('canvas'); thumbCanvas.width = thumbCanvas.height = size; }
    const m = Q.models[key];
    const it = modelItem(item(900), key, { kind: m ? m.kind : '' });
    M.ident(it.model);
    it.anim = null; it.mesh = S.meshFor(key, S.drawMode(key), null);
    thumbScene.items = [it];
    thumbScene.pal = S.palette();
    const b = S.bounds(key) || { mn: [-0.5, -0.5, -0.5], mx: [0.5, 0.5, 0.5] };
    const c = [(b.mn[0] + b.mx[0]) / 2, (b.mn[1] + b.mx[1]) / 2, (b.mn[2] + b.mx[2]) / 2], r = Math.max(0.03, Math.hypot(b.mx[0] - b.mn[0], b.mx[1] - b.mn[1], b.mx[2] - b.mn[2]) / 2);
    const yaw = m && m.kind === 'weapon' ? 2.0 : Math.PI + 0.6, pitch = -0.3, f = R.forward(yaw, pitch), dist = r / Math.tan(20 * Math.PI / 180) * 1.05;
    thumbScene.cam.pos = [c[0] - f[0] * dist, c[1] - f[1] * dist, c[2] - f[2] * dist]; thumbScene.cam.yaw = yaw; thumbScene.cam.pitch = pitch;
    R.draw(thumbScene, thumbTarget);
    R.readTarget(thumbTarget, thumbCanvas.getContext('2d'));
    return thumbCanvas.toDataURL('image/png');
  };
})();
