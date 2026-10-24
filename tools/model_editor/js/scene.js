/* ===== SCENES ========================================================================================
   What the viewport shows, placed the way the game places it (src/client/game.cpp):
     orbit     the model alone on a turntable (edit here)
     fp        first person: the weapon as your viewmodel (camera space at 0.2, -0.17, -0.55 + the HUD offsets, scale,
               drawn after a depth clear, no shadows on it), the game's fov and near plane
     tp        third person: a player with the gun arm, or holding a weapon as "it" (hide and seek / tag: the viewmodel
               boxes at 2x on the hand), the crown on the head; hitbox overlay
     flight    a projectile flying with its trail, spun / wobbled as the game does
     lineup    the screenshot lineup (quark --models): 5 players 5 m ahead, 1.5 m apart, facing you, your viewmodel
   Draw as: "boxes" = the game's built-in box path (1 px outlines, the part animations), "override" = how a .glb in
   the overrides folder is drawn (no outline, every triangle casts a shadow, only whole-model spins). */
(function () {
  'use strict';
  const Q = window.Q, R = Q.R, M = R.mat;
  const S = Q.scene = {};

  const DEFAULT_VIEW = { mode: 'orbit', palette: '', team: 'enemy', crouch: false, play: true, speed: 1, drawAs: 'auto', hitbox: false, grid: true,
    ground: true, shadows: true, sunVis: 1, outline: 'black', outlineW: 1, fov: 100, vmX: 0, vmY: 0, vmZ: 0, vmScale: 1, vmColor: '',
    yaw: 0, pitch: 0, fpWeapon: 'models/weapon/ROCKET', tpPlayer: 'models/player/STANDARD', holdIt: false, crown: false, crownPlace: 1,
    landed: false, lineupTeams: false, bright: 1, fpLineup: true, noViewmodel: false };
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
  // how a model is drawn: 'boxes' (the game's box path) or 'override'
  S.drawMode = function (key) {
    const cur = Q.current(key);
    if (cur.type !== 'boxes') return 'override';
    if (S.view.drawAs === 'boxes' || S.view.drawAs === 'override') return S.view.drawAs;
    return cur.edited || cur.shipped ? 'override' : 'boxes';
  };

  // --- GPU meshes per (model, draw mode), refilled when the model changes -----------------------------------------------
  const cache = new Map();
  S.meshFor = function (key, mode) {
    const id = key + '|' + mode, v = Q.ver[key] || 0;
    let e = cache.get(id);
    if (!e) { e = { mesh: R.createMesh(), ver: -1 }; cache.set(id, e); }
    if (e.ver !== v) { fill(e.mesh, key, mode, null); e.ver = v; }
    return e.mesh;
  };
  function fill(mesh, key, mode, parts) {
    const cur = Q.current(key), m = Q.models[key], kind = m ? m.kind : '';
    if (cur.type === 'boxes') {
      const p = parts || cur.parts;
      if (mode === 'boxes') R.fillBoxes(mesh, p, { outline: true, flatAlpha: kind === 'projectile', flatShaded: false });
      else R.fillBoxes(mesh, p, { outline: false, flatAlpha: true, flatShaded: true, noShadowFlags: true });
    } else if (cur.type === 'mesh') R.fillMesh(mesh, cur.mesh);
    else R.fillBoxes(mesh, [], {});
  }
  // the game's per-part animations (box path only; an override gets none of these)
  function partAnim(key) {
    const m = Q.models[key];
    if (!m || Q.isEdited(key) || !m.defParts || Q.current(key).shipped) return null;
    const v = S.view;
    if (m.kind === 'projectile' && m.name === 'ROCKET') return (t, src, dst) => {   // the flame flickers (0.8 + 0.2 sin(60 t + id))
      const fl = 0.8 + 0.2 * Math.sin(t * 60 + 1);
      for (let i = 0; i < src.length; i++) { const s = src[i], d = dst[i]; const flame = s.slot === 0 && Math.max(s.color[0], s.color[1], s.color[2]) > 1.5; for (let k = 0; k < 3; k++) d.h[k] = flame ? s.h[k] * fl : s.h[k]; }
    };
    if (m.kind === 'projectile' && m.name === 'STICKY' && v.landed) return (t, src, dst) => {   // stuck: the core blinks
      const core = 0.6 + 0.4 * ((t * 2.5) % 1 < 0.5 ? 1 : 0);
      for (let i = 0; i < src.length; i++) for (let k = 0; k < 3; k++) dst[i].color[k] = src[i].slot === 1 ? src[i].color[k] * core : src[i].color[k];
    };
    if (m.kind === 'projectile' && m.name === 'BUBBLE') return (t, src, dst) => {   // the shell wobbles (0.03 sin(14 t + id))
      const wob = 0.03 * Math.sin(t * 14 + 1);
      for (let i = 0; i < src.length; i++) { const s = src[i], d = dst[i]; const sh = (s.alpha == null ? 1 : s.alpha) < 1; d.h[0] = s.h[0] + (sh ? wob : 0); d.h[1] = s.h[1]; d.h[2] = s.h[2] + (sh ? wob : 0); }
    };
    if (m.kind === 'weapon' && m.name === 'LOCATOR') return (t, src, dst) => {   // the screen pulses when it's ready
      const g = 0.85 + 0.35 * Math.sin(t * 6);
      for (let i = 0; i < src.length; i++) for (let k = 0; k < 3; k++) dst[i].color[k] = src[i].slot === 2 ? src[i].color[k] * g : src[i].color[k];
    };
    return null;
  }

  // --- building the scene ----------------------------------------------------------------------------------------------
  const scene = { cam: { pos: [0, 1.6, 0], yaw: 0, pitch: 0, fov: 100 }, pal: Q.DEFAULT_PALETTE, sky: true, ground: { y: 0, grid: true }, shadows: true, bright: 1,
    items: [], trails: { data: new Float32Array(8 * 6 * 8), count: 0 }, lines: { data: new Float32Array(7 * 8192), count: 0 }, overlay: { data: new Float32Array(7 * 4096), count: 0 } };
  S.current = scene;
  let pool = [];   // items, reused between rebuilds
  function item(i) {
    if (!pool[i]) pool[i] = { mesh: null, model: new Float32Array(16), tint: new Float32Array(15), outlineW: 0, outline: null, sunVis: 1, view: false, castShadow: true,
      key: null, kind: '', role: '', anim: null, ownMesh: null, src: null, scratch: null, spin: null, highlight: null, hidden: false };
    const it = pool[i];
    it.anim = null; it.spin = null; it.highlight = null; it.hidden = false; it.castShadow = true; it.view = false; it.role = ''; it.key = null; it.trail = null;
    return it;
  }
  // a model as an item: its mesh, tints, outline
  function modelItem(it, key, ctx) {
    const m = Q.models[key], kind = m ? m.kind : (ctx.kind || '');
    const mode = S.drawMode(key);
    it.key = key; it.kind = kind; it.mode = mode;
    const pal = Q.runtimePalette(Object.assign({ kind, team: S.teamColor(), map: scene.pal }, ctx));
    Q.tints(kind, pal, it.tint);
    it.outlineW = mode === 'boxes' ? S.view.outlineW : 0;
    it.outline = S.outlineColor();
    it.sunVis = S.view.sunVis;
    const anim = mode === 'boxes' && S.view.play ? partAnim(key) : null;
    if (anim) {   // its own mesh, refilled each frame from a scratch copy of the parts (no allocation per frame)
      const src = Q.current(key).parts;
      if (!it.ownMesh) it.ownMesh = R.createMesh();
      if (it.src !== src) { it.src = src; it.scratch = src.map(Q.clonePart); }
      it.anim = anim; it.mesh = it.ownMesh;
      fill(it.mesh, key, mode, it.scratch);
    } else it.mesh = S.meshFor(key, mode);
    return it;
  }
  function boxItem(it, parts, color) {   // scene props drawn with the box path (the gun arm, the crown's letter)
    if (!it.ownMesh) it.ownMesh = R.createMesh();
    it.mesh = it.ownMesh;
    R.fillBoxes(it.mesh, parts, { outline: true, flatAlpha: false });
    it.tint.fill(1); it.outlineW = S.view.outlineW; it.outline = S.outlineColor(); it.sunVis = S.view.sunVis; it.kind = '';
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
  S.modesFor = function (key) {   // which views make sense for this model
    const k = Q.models[key] ? Q.models[key].kind : '';
    if (k === 'player') return ['orbit', 'tp', 'lineup'];
    if (k === 'weapon') return ['orbit', 'fp', 'tp', 'lineup'];
    if (k === 'projectile') return ['orbit', 'flight'];
    if (k === 'crown') return ['orbit', 'tp', 'lineup'];
    return ['orbit'];
  };

  // a player at feet `pos` facing yaw, with its gun arm (or held weapon), crown; returns the next item index
  const PM = new Float32Array(16);
  function addPlayer(n, pkey, px, py, pz, yaw, opts) {
    const hs = S.view.crouch ? Q.CROUCH_H / Q.PLAYER_H : 1, bs = 1;
    const it = modelItem(item(n++), pkey, { kind: 'player' });
    it.role = 'player';
    M.trs(it.model, px, py, pz, yaw, 0, bs, bs * hs, bs);
    if (opts.highlight) it.highlight = opts.highlight;
    const at = h => py + h * hs * bs;
    const rx = Math.cos(yaw), rz = -Math.sin(yaw);   // right
    const sx = px + rx * 0.3 * bs, sy = at(1.3), sz = pz + rz * 0.3 * bs;   // the shoulder
    if (opts.held) {   // "it": the arm and the weapon's own model at 2x on the hand
      const arm = boxItem(item(n++), ARM_HELD, Q.GUN_ARM); arm.role = 'arm';
      M.trs(arm.model, sx, sy, sz, yaw, 0, 1, 1, 1);
      const w = modelItem(item(n++), opts.held, { kind: 'weapon', use: 'held', team: S.teamColor() });
      w.role = 'held';
      M.trs(PM, 0, 0.02 * bs, -0.5 * bs, 0, 0, 2 * bs, 2 * bs, 2 * bs);
      M.trs(TMP, sx, sy, sz, yaw, 0, 1, 1, 1);
      M.mul(w.model, TMP, PM);
    } else {
      const arm = boxItem(item(n++), ARM_GUN, Q.GUN_ARM); arm.role = 'arm';
      M.trs(arm.model, sx, sy, sz, yaw, 0, 1, 1, 1);
    }
    if (opts.crown) {
      const ck = S.crownKey();
      if (ck) {
        const ty = py + Q.PLAYER_H * hs * bs + 0.04 * bs;
        const c = modelItem(item(n++), ck, { kind: 'crown', crownPlace: S.view.crownPlace });
        c.role = 'crown'; c.castShadow = false;
        M.trs(c.model, px, ty, pz, yaw, 0, bs, bs, bs);
        const l = boxItem(item(n++), S.view.crownPlace === 1 ? LETTER_GOLD : LETTER_SILVER); l.role = 'letter'; l.castShadow = false;
        M.trs(l.model, px, ty, pz, yaw, 0, bs, bs, bs);
      }
    }
    return n;
  }
  const TMP = new Float32Array(16);
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

  // the viewmodel: camera space T(cam) Ry(yaw) Rx(pitch) T(at) S(scale)
  function addViewmodel(n, wkey, cam) {
    const v = S.view;
    const vmColor = v.vmColor ? Q.hexToLin(v.vmColor) : null;
    const it = modelItem(item(n++), wkey, { kind: 'weapon', vmColor: vmColor || Q.TEAM.friendly });
    it.role = 'viewmodel'; it.view = true; it.sunVis = 1; it.castShadow = false;
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
    const c = [(mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2], r = Math.max(0.05, Math.hypot(mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]) / 2);
    o.target = c;
    const cv = document.getElementById('gl'), aspect = cv && cv.clientHeight ? cv.clientWidth / cv.clientHeight : 16 / 9;
    const hf = S.view.fov * Math.PI / 180, vf = 2 * Math.atan(Math.tan(hf / 2) / aspect);
    o.dist = Math.max(0.15, r / Math.sin(Math.min(hf, vf) / 2) * 1.08);
    if (v.mode === 'orbit' && m && m.kind === 'weapon') { o.yaw = 2.2; o.pitch = -0.3; }
    else if (v.mode === 'orbit' || v.mode === 'tp') { o.yaw = Math.PI + 0.55; o.pitch = -0.2; }
    else { o.yaw = 1.3; o.pitch = -0.15; }
    S.dirty = true;
  };
  S.boundsOf = function (parts) {
    if (!parts || !parts.length) return null;
    const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (const p of parts) for (let k = 0; k < 3; k++) { mn[k] = Math.min(mn[k], p.c[k] - p.h[k]); mx[k] = Math.max(mx[k], p.c[k] + p.h[k]); }
    return { mn, mx };
  };
  S.hasPartAnim = function (key) {
    const m = Q.models[key];
    return !!m && ((m.kind === 'projectile' && (m.name === 'ROCKET' || m.name === 'STICKY' || m.name === 'BUBBLE')) || (m.kind === 'weapon' && m.name === 'LOCATOR'));
  };
  S.bounds = function (key) {
    const cur = Q.current(key);
    if (cur.type === 'boxes') {
      if (!cur.parts.length) return null;
      const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
      for (const p of cur.parts) for (let k = 0; k < 3; k++) { mn[k] = Math.min(mn[k], p.c[k] - p.h[k]); mx[k] = Math.max(mx[k], p.c[k] + p.h[k]); }
      return { mn, mx };
    }
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
    scene.ground = v.ground ? { y: 0, grid: v.grid && (v.mode === 'orbit' || v.mode === 'tp' || v.mode === 'flight') } : null;
    S.focus = null;   // the item being edited (picking, gizmo)
    if (v.mode === 'orbit' && key) {
      const it = modelItem(item(n++), key, { kind });
      it.role = 'subject';
      const hs = kind === 'player' && v.crouch ? Q.CROUCH_H / Q.PLAYER_H : 1;
      M.trs(it.model, 0, 0, 0, 0, 0, 1, hs, 1);
      if (kind === 'projectile') it.spin = projSpin(key, false);
      S.focus = it;
      if (scene.ground) {
        const b = S.bounds(key);
        scene.ground.y = kind === 'player' || kind === 'crown' || !b ? 0 : Math.min(0, b.mn[1] - 0.05);
      }
    } else if (v.mode === 'tp') {
      const pkey = S.playerKey();
      const held = kind === 'weapon' ? key : (v.holdIt ? S.weaponKey() : null);
      const before = n;
      n = addPlayer(n, pkey, 0, 0, 0, 0, { held, crown: kind === 'crown' || v.crown });
      for (let i = before; i < n; i++) {
        const it = pool[i];
        if (it.key === key && (it.role === 'player' || it.role === 'held' || it.role === 'crown')) S.focus = it;
      }
    } else if (v.mode === 'fp') {
      const cam = scene.cam;
      cam.pos[0] = 0; cam.pos[1] = v.crouch ? Q.CROUCH_EYE : Q.EYE; cam.pos[2] = 0; cam.yaw = v.yaw * Math.PI / 180; cam.pitch = v.pitch * Math.PI / 180;
      if (v.fpLineup) n = lineup(n, cam, false);
      if (!v.noViewmodel) { n = addViewmodel(n, S.weaponKey(), cam); S.focus = pool[n - 1]; }
    } else if (v.mode === 'lineup') {
      const cam = scene.cam;
      cam.pos[0] = 0; cam.pos[1] = Q.EYE; cam.pos[2] = 0; cam.yaw = v.yaw * Math.PI / 180; cam.pitch = v.pitch * Math.PI / 180;
      n = lineup(n, cam, true);
      n = addViewmodel(n, S.weaponKey(), cam);
      for (let i = 0; i < n; i++) if (pool[i].key === key && pool[i].role !== 'crown') { S.focus = pool[i]; break; }
    } else if (v.mode === 'flight' && key) {
      const it = modelItem(item(n++), key, { kind });
      it.role = 'subject';
      it.spin = projSpin(key, true);
      it.trail = Q.PROJ[m.name] && Q.PROJ[m.name].trail || null;
      S.focus = it;
    }
    scene.items.length = 0;
    for (let i = 0; i < n; i++) scene.items.push(pool[i]);
    if (v.mode === 'orbit' || v.mode === 'tp' || v.mode === 'flight') orbitCam(scene.cam);
    S.animate(S.time, true);
    S.dirty = true;
  };
  // the lineup (game.cpp, --models): the players 5 m ahead, 1.5 m apart, facing you; in a team mode every other one
  // is on team B. The model being edited takes its own slot.
  function lineup(n, cam, withCrowns) {
    const v = S.view, f = [-Math.sin(cam.yaw), 0, -Math.cos(cam.yaw)], rt = [Math.cos(cam.yaw), 0, -Math.sin(cam.yaw)];
    const list = players();
    const sel = S.sel && Q.models[S.sel] && Q.models[S.sel].kind === 'player' ? S.sel : null;
    const crownOn = withCrowns && (v.crown || (S.sel && Q.models[S.sel] && Q.models[S.sel].kind === 'crown'));
    const saveTeam = v.team;
    for (let k = 0; k < list.length; k++) {
      if (v.lineupTeams) v.team = k % 2 ? 'enemy' : 'friendly';
      const x = f[0] * 5 + rt[0] * ((k - 2) * 1.5), z = f[2] * 5 + rt[2] * ((k - 2) * 1.5);
      // ("it": turned aside, the weapon side-on, game.cpp)
      n = addPlayer(n, list[k], x, 0, z, cam.yaw + Math.PI - (v.holdIt ? 1.0 : 0), { held: v.holdIt ? S.weaponKey() : null, crown: crownOn,
        highlight: null });
      v.team = saveTeam;
    }
    void sel;
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
    for (const it of scene.items) {
      if (it.anim && v.play) {
        it.anim(t, Q.current(it.key).parts, it.scratch);
        fill(it.mesh, it.key, it.mode, it.scratch);
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
        if (it.trail) addTrail(0, 1.5, z, it.trail);
        moved = true;
      } else if (v.mode === 'orbit' && it.role === 'subject' && it.spin && v.play) {
        M.trs(it.model, 0, 0, 0, it.spin === 'stickySpin' ? t * 6 : 0, it.spin === 'knifeSpin' ? t * 25 : 0, 1, 1, 1);
        moved = true;
      }
    }
    if (moved) S.dirty = true;
  };
  // Renderer::line: a camera-facing quad from the projectile back along its path (direction -Z: the trail is +Z)
  function trailVertex(x0, y0, z0, col, a) {
    const T = scene.trails, d = T.data;
    let o = T.count * 8;
    d[o++] = x0; d[o++] = y0; d[o++] = z0; d[o++] = col[0]; d[o++] = col[1]; d[o++] = col[2]; d[o++] = a; d[o++] = 0;
    T.count++;
  }
  function addTrail(x, y, z, tr) {
    const cam = scene.cam.pos, col = S.teamColor();
    const ax = x, ay = y, az = z - tr.from, bx = x, by = y, bz = z + tr.len;   // a = pos + dir * from (dir = -Z), b = pos - dir * len
    let tx = cam[0] - (ax + bx) / 2, ty = cam[1] - (ay + by) / 2;
    const tz = cam[2] - (az + bz) / 2, tl = Math.hypot(tx, ty, tz) || 1; tx /= tl; ty /= tl;
    // side = normalize(cross(d, toCam)) * w / 2 with d = (0, 0, 1)
    let sx = -ty, sy = tx; const sl = Math.hypot(sx, sy) || 1; sx = sx / sl * tr.w / 2; sy = sy / sl * tr.w / 2;
    // quad a - side, b - side, b + side, a + side as triangles 0 1 2, 0 2 3
    trailVertex(ax - sx, ay - sy, az, col, tr.a); trailVertex(bx - sx, by - sy, bz, col, tr.a); trailVertex(bx + sx, by + sy, bz, col, tr.a);
    trailVertex(ax - sx, ay - sy, az, col, tr.a); trailVertex(bx + sx, by + sy, bz, col, tr.a); trailVertex(ax + sx, ay + sy, az, col, tr.a);
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
      const parts = Q.current(f.key).parts, p = parts && parts[S.part];
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
    Q.current(f.key).parts.forEach((p, i) => {
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
    it.anim = null; it.mesh = S.meshFor(key, S.drawMode(key));
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
