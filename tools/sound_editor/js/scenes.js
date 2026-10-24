/* ===== SCENES =======================================================================================
   Scripted gameplay moments played with the game's volumes, pitches, refires and 3D gains (the audit, section A).
   The listener stands at the origin looking down -Z (right = +X). Each event:
     {t, snd, who: 'me' | 'enemy' | 'ann', vol, pitch: number | 'j97' | 'j90', pos: [x, y, z] | null (2D),
      kind: 'sound' | 'voice', missing: true = a sound the game doesn't have yet (plays only once designed)}
   'me' uses YOUR kit, 'enemy' the enemy's kit, 'ann' the announcer voice. Edits are heard in place. */
(function () {
  'use strict';
  const Q = window.Q, RF = () => Q.REFIRE, STEP = () => Q.GAME.stepEvery;

  function S() {   // a little scene builder
    const ev = [];
    const api = {
      ev,
      at(t, snd, o) { ev.push(Object.assign({ t, snd, who: 'me', vol: 1, pitch: 1, pos: null, kind: 'sound' }, o || {})); return api; },
      me(t, snd, vol, pitch) { return api.at(t, snd, { vol, pitch: pitch || 1 }); },
      them(t, snd, pos, vol, pitch) { return api.at(t, snd, { who: 'enemy', pos, vol, pitch: pitch || 1 }); },
      mine3d(t, snd, pos, vol, pitch) { return api.at(t, snd, { pos, vol, pitch: pitch || 1 }); },
      say(t, line) { return api.at(t, line, { who: 'ann', kind: 'voice', vol: 1 }); },
      miss(t, snd, vol, pos) { return api.at(t, snd, { vol: vol || 0.8, pos: pos || null, missing: true }); },
      steps(t0, t1, every) { for (let t = t0; t < t1 - 1e-6; t += every || STEP()) api.me(t, 'STEP', 0.55, 'j90'); return api; },
      theirSteps(t0, t1, pos, every) { for (let t = t0; t < t1 - 1e-6; t += every || STEP()) api.them(t, 'STEP', pos, 0.8, 'j90'); return api; },
      fire(t, w, n, opts) {   // n shots of your weapon at its refire; opts.hit(i) -> damage or 0
        opts = opts || {};
        const r = opts.refire || RF()[w];
        for (let i = 0; i < n; i++) {
          const ti = t + i * r;
          api.me(ti, w, w === 'LASER' ? 0.6 : 0.8, 'j97');
          const d = opts.hit ? opts.hit(i) : 0;
          if (d) api.hit(ti + (opts.delay || 0.03), d, opts.head && opts.head(i));
        }
        return t + (n - 1) * r;
      },
      theirFire(t, w, n, pos, refire) { const r = refire || RF()[w]; for (let i = 0; i < n; i++) api.them(t + i * r, w, [pos[0], pos[1] + 1.5, pos[2]], w === 'LASER' ? 0.7 : 1.0, w === 'LASER' ? 1 : 'j97'); return t + (n - 1) * r; },
      hit(t, dmg, head) {   // the hitsound tier by damage (hitSoundFor), 2D 0.9
        const snd = dmg < 10 ? 'HIT' : dmg < 25 ? 'HIT_MED' : dmg <= 50 ? 'HIT_HEAVY' : 'HIT_BRUTAL';
        api.me(t, snd, 0.9);
        if (head) api.miss(t, 'HEADSHOT', 0.9);
        return api;
      },
      kill(t) { return api.me(t, 'KILL', 0.9); },
      hurt(t) { return api.me(t, 'HURT', 0.7); },
    };
    return api;
  }
  const dist = p => Math.hypot(p[0], p[1], p[2]);

  Q.SCENES = [
    { id: 'rifle', name: 'Rifle duel', desc: 'You strafe and trade rifle fire with an enemy 20 m ahead-right; body and head hits, a headshot kill.', len: 6.5, build() {
      const s = S(), E = [9, 0, -18];
      s.steps(0, 2.2).theirSteps(0, 1.2, E);
      s.theirFire(0.55, 'RIFLE', 9, E);
      for (let i = 0; i < 4; i++) s.hurt(0.6 + i * 0.3);
      s.fire(0.9, 'RIFLE', 10, { hit: i => i % 3 === 2 ? 0 : (i % 4 === 0 ? 7.5 : 4.95), head: i => i % 4 === 0 });
      s.steps(2.6, 4.2);
      s.theirFire(2.9, 'RIFLE', 5, E);
      const end = s.fire(3.4, 'RIFLE', 8, { hit: i => i === 7 ? 7.5 : 4.95, head: i => i === 7 });
      s.kill(end + 0.03).say(end + 0.12, 'HEADSHOT');
      s.steps(end + 0.5, 6.3);
      return s.ev;
    } },
    { id: 'rockets', name: 'Rockets and jumps', desc: 'Rocket jumps, a direct hit, enemy rockets landing close, an air-rocket kill.', len: 7, build() {
      const s = S(), E = [-10, 0, -22], T = [6, 0, -20];
      s.steps(0, 0.6).me(0.6, 'JUMP', 0.5);
      s.me(0.75, 'ROCKET', 0.8, 'j97');
      s.mine3d(0.75 + dist(T) / 48, 'EXPLOSION', T, 1.25).hit(0.75 + dist(T) / 48 + 0.03, 34);
      s.me(1.5, 'LAND', 0.37).steps(1.6, 2.6);
      s.theirSteps(0, 2.4, E).them(1.9, 'ROCKET', [E[0], 1.5, E[2]], 1.0, 'j97');
      const near = [2.5, 0, -3];
      s.at(1.9 + 22 / 48, 'EXPLOSION', { who: 'enemy', pos: near, vol: 1.25 }).hurt(1.9 + 22 / 48 + 0.02);
      s.me(2.95, 'JUMP', 0.5).me(3.1, 'ROCKET', 0.8, 'j97');
      s.mine3d(3.1 + dist(T) / 48, 'EXPLOSION', T, 1.25).hit(3.1 + dist(T) / 48 + 0.03, 34);
      s.me(4.3, 'ROCKET', 0.8, 'j97');
      const t2 = 4.3 + dist(T) / 48;
      s.mine3d(t2, 'EXPLOSION', T, 1.25).hit(t2 + 0.03, 58).kill(t2 + 0.06).say(t2 + 0.15, 'AIR_ROCKET');
      s.me(5.2, 'LAND', 0.6).steps(5.3, 6.8);
      return s.ev;
    } },
    { id: 'laser', name: 'Laser, sniper, headshot', desc: 'A laser fight (20 sounds and 20 light hits a second, your hurt with no rate limit), then sniper body and head shots.', len: 7, build() {
      const s = S(), E = [-7, 0, -12];
      s.fire(0.2, 'LASER', 30, { refire: 0.05, hit: () => 2, delay: 0.02 });
      s.theirFire(0.5, 'LASER', 20, E, 0.05);
      for (let t = 0.55; t < 1.5; t += 0.025) s.hurt(t);   // ~40/s: the known pile-up
      s.steps(1.9, 3.0);
      s.me(3.2, 'SNIPER', 0.8, 'j97').hit(3.23, 40);
      s.theirSteps(3.0, 4.6, [-12, 0, -30]);
      s.me(4.7, 'SNIPER', 0.8, 'j97').hit(4.73, 50, true).kill(4.76).say(4.85, 'HEADSHOT');
      return s.ev;
    } },
    { id: 'movement', name: 'Movement tech', desc: 'Run, dash, super (dash jump), hyper (super dash), grapple to an anchor 20 m up ahead; an enemy grapples too.', len: 7, build() {
      const s = S();
      s.steps(0, 0.6).me(0.6, 'DASH', 0.8).steps(0.62, 0.74, Q.GAME.dashStepEvery);
      s.miss(1.8, 'DASH_READY', 0.6);
      s.steps(0.8, 1.3).me(1.3, 'DASH', 0.8).me(1.38, 'SUPER', 0.9).me(1.38, 'JUMP', 0.5).me(2.1, 'LAND', 0.5);
      s.steps(2.15, 2.6).me(2.6, 'DASH', 0.8).me(2.66, 'HYPER', 0.9).me(2.66, 'JUMP', 0.5).me(3.2, 'LAND', 0.45);
      s.me(3.6, 'GRAPPLE_FIRE', 0.7).mine3d(3.6, 'GRAPPLE_HIT', [0, 10, -20], 0.9);
      s.me(4.9, 'LAND', 0.8).steps(5.0, 6.0);
      s.them(5.4, 'GRAPPLE_HIT', [-14, 6, -16], 0.9).theirSteps(5.6, 6.9, [-8, 0, -6]);
      return s.ev;
    } },
    { id: 'bomb', name: 'Bomb round', desc: 'Freeze countdown, the plant, a shotgun fight, the defuse and the round win (bomb tick / plant / defuse play if you design them).', len: 12, build() {
      const s = S(), E = [7, 0, -9];
      s.say(0, 'THREE').say(1, 'TWO').say(2, 'ONE').say(3, 'GO');
      s.steps(3.05, 4.8).miss(4.9, 'PLANT', 0.8, [0, 0, -2]).miss(5.4, 'PLANT', 0.8, [0, 0, -2]);
      s.say(5.9, 'BOMB_PLANTED');
      for (let t = 6.2; t < 11; t += 0.5) s.miss(t, 'BOMB_TICK', 0.7, [0, 0, -4]);
      s.say(7.0, 'THIRTY_SECONDS').theirSteps(6.0, 7.5, E);
      s.them(7.5, 'SHOTGUN', [E[0], 1.5, E[2]], 1.0, 'j97').hurt(7.53);
      s.fire(7.8, 'SHOTGUN', 3, { hit: i => i < 2 ? 34 : 0 });
      s.kill(8.6);
      s.steps(8.9, 9.6);
      for (let t = 9.7; t < 10.9; t += 0.5) s.miss(t, 'DEFUSE', 0.8, [0, 0, -2]);
      s.say(10.9, 'BOMB_DEFUSED').say(11.6, 'ROUND_WON');
      return s.ev;
    } },
    { id: 'race', name: 'Race', desc: 'GO, run and dash through three checkpoints to the finish; FINISH, ROUND_OVER and HIGH_SCORE overlap as in the game.', len: 10, build() {
      const s = S();
      s.say(0, 'THREE').say(1, 'TWO').say(2, 'ONE').say(3, 'GO');
      s.steps(3.05, 9.0);
      s.me(4.0, 'DASH', 0.8).me(4.6, 'CHECKPOINT', 0.9).me(5.6, 'JUMP', 0.5).me(6.1, 'LAND', 0.5).me(6.6, 'CHECKPOINT', 0.9);
      s.me(7.2, 'DASH', 0.8).me(7.9, 'CHECKPOINT', 0.9);
      s.me(9.0, 'FINISH', 0.9).say(9.02, 'ROUND_OVER').say(9.05, 'HIGH_SCORE');
      return s.ev;
    } },
    { id: 'streak', name: 'Killstreak', desc: 'Ten quick kills: SPREE, RAMPAGE, UNSTOPPABLE, GODLIKE; someone else\'s streak sting; then your death.', len: 14, build() {
      const s = S(), lines = { 3: 'SPREE', 5: 'RAMPAGE', 7: 'UNSTOPPABLE', 10: 'GODLIKE' };
      let t = 0.2;
      for (let k = 1; k <= 10; k++) {
        const w = k % 2 ? 'RIFLE' : 'SHOTGUN';
        const end = w === 'RIFLE' ? s.fire(t, 'RIFLE', 4, { hit: () => 4.95 }) : s.fire(t, 'SHOTGUN', 1, { hit: () => 34 });
        s.kill(end + 0.04);
        if (lines[k]) s.say(end + 0.12, lines[k]);
        if (k === 4) s.me(end + 0.6, 'STREAK', 0.7);
        t = end + 1.1;
      }
      s.me(t + 0.3, 'DEATH', 0.8).say(t + 0.35, 'YOU_DIED');
      return s.ev;
    } },
    { id: 'ladder', name: 'Hitsound ladder', desc: 'HIT, HIT_MED, HIT_HEAVY, HIT_BRUTAL three times each, up then down: the four must stay in order.', len: 7, build() {
      const s = S(), d = [5, 15, 40, 60];
      let t = 0.2;
      for (const k of [0, 1, 2, 3, 3, 2, 1, 0]) { for (let i = 0; i < 3; i++) { s.hit(t, d[k]); t += 0.22; } t += 0.15; }
      return s.ev;
    } },
    { id: 'chaos', name: 'Pile-up', desc: 'Six stickies at once, crossbow bolts, a 10-bubble stream popping (the fire sound at pitch 1.7), an enemy sticky on you.', len: 8, build() {
      const s = S(), W = [3, 0, -14];
      for (let i = 0; i < 6; i++) { s.me(0.2 + i * 0.75, 'STICKY', 0.8, 'j97'); s.mine3d(0.55 + i * 0.75, 'STICKY_ATTACH', [W[0] + i * 0.3, 0, W[2]], 0.8); }
      s.miss(4.6, 'DETONATE', 0.8);
      for (let i = 0; i < 6; i++) s.mine3d(4.65, 'EXPLOSION', [W[0] + i * 0.3, 0.5, W[2]], 1.25);
      s.hit(4.68, 60).kill(4.7);
      s.them(5.2, 'CROSSBOW', [-6, 1.5, -18], 1.0, 'j97').at(5.45, 'BOLT_HIT', { who: 'enemy', pos: [-1, 0, -2], vol: 0.8 });
      s.me(5.6, 'BUBBLE', 0.8, 'j97');
      for (let i = 0; i < 10; i++) s.mine3d(6.3 + (i % 3) * 0.01, 'BUBBLE', [2 + i * 0.4, 1, -9 - i * 0.3], 0.7, 1.7);
      s.at(6.9, 'STICKY_ATTACH', { who: 'enemy', vol: 1.0, pitch: 1.6 });
      s.miss(7.0, 'STICKY_BEEP', 0.8).miss(7.5, 'STICKY_BEEP', 0.8);
      return s.ev;
    } },
    { id: 'reuses', name: 'Reused sounds', desc: 'Every place a sound stands in for another: locator found (GRAPPLE_HIT x1.4), healed (SYRINGE x1.3), queue filled (STREAK), rock-paper-scissors cues, typing.', len: 9, build() {
      const s = S();
      s.me(0.2, 'LOCATOR', 0.8).me(1.2, 'GRAPPLE_HIT', 0.6, 1.4);
      for (let i = 0; i < 6; i++) s.them(2.0 + i * 0.25, 'TYPING', [4, 1.5, -6], 0.6, 'j90');
      s.me(3.8, 'SYRINGE', 0.8, 1.3);
      s.me(4.8, 'STREAK', 1.0);
      s.me(6.0, 'TYPING', 0.9, 0.8).me(6.6, 'BOLT_HIT', 0.9, 0.9).me(7.2, 'CHECKPOINT', 0.7, 0.8).me(7.8, 'FINISH', 0.6, 1.2).me(8.4, 'HURT', 0.8);
      return s.ev;
    } },
  ];

  // HitSoundGate (audio.h): within 40 ms of the last hitsound, only a heavier tier plays
  function gateHits(ev) {
    let at = -1e9, tier = -1;
    return ev.filter(e => {
      const t = e.kind === 'sound' && !e.missing ? Q.hitTier(e.snd) : -1;
      if (t < 0) return true;
      if (e.t - at < 0.04 && t <= tier) return false;
      at = e.t; tier = t; return true;
    });
  }
  Q.sceneEvents = function (scene) {
    const ev = scene.build().sort((a, b) => a.t - b.t);
    return gateHits(ev);
  };

  // the asset an event plays, given the scene's pack choices
  Q.eventKey = function (e, opt) {
    if (e.kind === 'voice') return Q.key('voice', opt.voice, e.snd);
    return Q.key('sound', e.who === 'enemy' ? opt.enemyKit : opt.myKit, e.snd);
  };
  Q.eventAudible = function (e, opt) {
    const key = Q.eventKey(e, opt), a = Q.assets[key];
    if (!a) return false;
    if (e.missing && !Q.state.edits[key]) return false;
    if (e.missing && !opt.missing) return false;
    return Q.audible(a.kind, a.pack, a.name);
  };

  /* Schedule one pass of a scene into (c, dest) starting at `start`. Buffers come from Q.getGame (the game-prepared
     edits). Returns the scheduled voices (for live mute / stop). */
  Q.prepareScene = async function (scene, opt) {
    const ev = Q.sceneEvents(scene), keys = new Set();
    if (!Q.loaded['voice:' + opt.voice]) await Q.loadPack('voice', opt.voice);
    for (const e of ev) { const k = Q.eventKey(e, opt); if (Q.assets[k]) keys.add(k); }
    const bufs = {};
    await Promise.all([...keys].map(async k => { bufs[k] = (await Q.getGame(k)).v; }));
    return { ev, bufs };
  };
  Q.scheduleScene = function (c, dest, prep, opt, start) {
    const voices = [];
    for (const e of prep.ev) {
      const key = Q.eventKey(e, opt), x = prep.bufs[key];
      if (!x || !x.length) continue;
      if (e.missing && (!opt.missing || !Q.state.edits[key])) continue;
      const v = Q.scheduleVoice(c, dest, x, start + e.t, Q.voiceGains(e.vol, e.pos), Q.pitchOf(e.pitch, opt.jitter));
      if (!v) continue;
      v.e = e; v.key = key;
      v.gate.gain.value = Q.eventAudible(e, opt) ? 1 : 0;
      voices.push(v);
    }
    return voices;
  };

  // offline render of one pass: a stereo AudioBuffer of the scene as the game would mix it
  Q.renderSceneOffline = async function (scene, opt) {
    const prep = await Q.prepareScene(scene, opt);
    let end = scene.len;
    for (const e of prep.ev) { const x = prep.bufs[Q.eventKey(e, opt)]; if (x) end = Math.max(end, e.t + x.length / 48000 / 0.9); }
    const c = new OfflineAudioContext(2, Math.ceil((end + 0.1) * 48000), 48000);
    const bus = Q.makeGameBus(c, opt.master, opt.limiter);
    bus.output.connect(c.destination);
    const voices = Q.scheduleScene(c, bus.input, prep, opt, 0);
    const buf = await c.startRendering();
    return { buf, events: prep.ev.length, voices: voices.length };
  };
})();
