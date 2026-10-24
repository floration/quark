/* ===== ENGINE =======================================================================================
   Render cache (each asset's edited output and the game-prepared version), metrics cache, the mixer's
   solo / mute state, and playback through Web Audio with the game's 2D / 3D gains. */
(function () {
  'use strict';
  const Q = window.Q, L = window.QLoud, R = 48000;

  // --- a small concurrency limit for renders (group levelling renders hundreds) -----------------------------
  let running = 0; const waiting = [];
  async function limited(fn) {
    if (running >= 4) await new Promise(r => waiting.push(r));
    running++;
    try { return await fn(); } finally { running--; const w = waiting.shift(); if (w) w(); }
  }

  // --- edited output (what is exported): cached by the edit's signature ------------------------------------
  const outCache = {};   // key -> {sig, v, p (pending promise)}
  Q.editSig = key => { const e = Q.state.edits[key]; return e ? JSON.stringify(e) : 'orig'; };
  Q.getOut = function (key) {
    const sig = Q.editSig(key), c = outCache[key];
    if (c && c.sig === sig) return c.v ? Promise.resolve(c.v) : c.p;
    const e = Q.state.edits[key];
    if (!e) { const v = Q.assets[key] && Q.assets[key].orig || new Float32Array(0); outCache[key] = { sig, v }; return Promise.resolve(v); }
    const p = limited(() => Q.renderEditData(Q.sourceOf(key), e)).then(v => {
      if (outCache[key] && outCache[key].sig === sig) outCache[key].v = v;
      return v;
    });
    outCache[key] = { sig, p };
    return p;
  };
  Q.outSync = key => { const c = outCache[key]; return c && c.sig === Q.editSig(key) && c.v ? c.v : null; };

  /* What the game will play for this asset (a project-wide view: inherited DEFAULT edits, RETRO's chiptune, the
     game's own trim / cap / level match unless the level is kept in sound-levels.txt). */
  Q.inheritsFrom = function (key) {
    const a = Q.assets[key];
    if (!a || a.kind !== 'sound' || a.pack === 'default' || Q.state.edits[key]) return null;
    const dk = Q.key('sound', 'default', a.name);
    if (!Q.state.edits[dk]) return null;
    if (a.derived === 'retro') return { key: dk, retro: true };
    if (a.sameAsDefault) return { key: dk, retro: false };
    return null;
  };
  Q.matchFor = function (key) {   // the clip the game levels a replacement to
    const a = Q.assets[key];
    if (a.kind === 'voice') return a.orig;
    const d = Q.assets[Q.key('sound', 'default', a.name)];
    return d ? d.orig : null;
  };
  const gameCache = {};
  Q.gameSig = function (key) {
    const inh = Q.inheritsFrom(key);
    if (inh) return 'inh:' + inh.retro + ':' + Q.gameSig(inh.key);
    const a = Q.assets[key], dk = a && a.kind === 'sound' && a.pack !== 'default' ? Q.key('sound', 'default', a.name) : null;
    return Q.editSig(key) + (dk && Q.state.edits[key] && Q.state.edits[dk] ? '|match:' + Q.gameSig(dk) : '');
  };
  Q.getGame = function (key) {
    const sig = Q.gameSig(key), c = gameCache[key];
    if (c && c.sig === sig) return c.v ? Promise.resolve(c.v) : c.p;
    const a = Q.assets[key];
    let p;
    const inh = Q.inheritsFrom(key);
    if (inh) p = Q.getGame(inh.key).then(g => inh.retro ? { v: Q.chiptune(g.v), info: Object.assign({}, g.info, { inherited: inh.key, retro: true }) }
      : { v: g.v, info: Object.assign({}, g.info, { inherited: inh.key }) });
    else if (!Q.state.edits[key]) p = Promise.resolve({ v: a.orig || new Float32Array(0), info: { mode: 'shipped', gainDb: 0, cutStart: 0 } });
    else p = (async () => {
      const v = await Q.getOut(key), e = Q.state.edits[key];
      // a kit's replacement is matched to DEFAULT's sound as the game has it then: DEFAULT's own override when it has one
      // (loadOverrideSounds does DEFAULT first)
      const dk = a.kind === 'sound' && a.pack !== 'default' ? Q.key('sound', 'default', a.name) : null;
      const match = dk && Q.state.edits[dk] ? (await Q.getGame(dk)).v : Q.matchFor(key);
      const r = Q.prepareGame(v, a.cap, match, a.kind === 'sound' && Q.hitTier(a.name) >= 0, e && e.levelSet);
      return { v: r.v, info: r };
    })();
    p = p.then(g => { if (gameCache[key] && gameCache[key].sig === sig) gameCache[key].v = g; return g; });
    gameCache[key] = { sig, p };
    return p;
  };
  Q.gameSync = key => { const c = gameCache[key]; return c && c.sig === Q.gameSig(key) && c.v ? c.v : null; };

  // --- metrics, cached per buffer identity -----------------------------------------------------------------
  const metCache = new WeakMap();
  Q.measure = function (x) {
    if (!x) return null;
    let m = metCache.get(x);
    if (!m) { m = L.measure(x, R); metCache.set(x, m); }
    return m;
  };
  // which: 'game' (as heard in the game), 'out' (the file), 'orig' (the shipped one)
  Q.getMetrics = async function (key, which) {
    if (which === 'orig') return Q.measure(Q.assets[key].orig);
    if (which === 'out') return Q.measure(await Q.getOut(key));
    return Q.measure((await Q.getGame(key)).v);
  };
  Q.metricsSync = function (key, which) {
    const x = which === 'orig' ? Q.assets[key].orig : which === 'out' ? Q.outSync(key) : (Q.gameSync(key) || {}).v;
    return x && metCache.get(x) || null;
  };

  // --- mixer: solo / mute per sound and per pack ---------------------------------------------------------------
  Q.mix = { soloSound: new Set(), muteSound: new Set(), soloPack: new Set(), mutePack: new Set() };
  Q.soundId = (kind, name) => (kind === 'voice' ? 'line:' : 'sound:') + name;
  Q.packId = (kind, dir) => (kind === 'voice' ? 'voice:' : 'kit:') + dir;
  Q.audible = function (kind, pack, name) {
    const m = Q.mix, s = Q.soundId(kind, name), p = Q.packId(kind, pack);
    if (m.muteSound.has(s) || m.mutePack.has(p)) return false;
    if (m.soloPack.size && !m.soloPack.has(p)) return false;
    if (m.soloSound.size && !m.soloSound.has(s)) return false;
    return true;
  };
  Q.toggleMix = function (set, id) { const s = Q.mix[set]; s.has(id) ? s.delete(id) : s.add(id); Q.onMixChanged && Q.onMixChanged(); };

  // --- Web Audio output --------------------------------------------------------------------------------------------
  let ctx = null;
  Q.ctx = function () {
    if (!ctx) {
      ctx = new AudioContext({ sampleRate: R, latencyHint: 'interactive' });
      Q.monitor = ctx.createGain(); Q.monitor.gain.value = Q.monitorLevel == null ? 1 : Q.monitorLevel;
      Q.monitor.connect(ctx.destination);
      Q.previewBus = ctx.createGain(); Q.previewBus.connect(Q.monitor);
      Q.analyser = ctx.createAnalyser(); Q.analyser.fftSize = 2048; Q.monitor.connect(Q.analyser);
    }
    if (ctx.state === 'suspended') ctx.resume();
    return ctx;
  };
  // the game's mixer output stage: master x sum, then x / (1 + 0.3|x|)
  Q.makeGameBus = function (c, master, limiter) {
    const g = c.createGain(); g.gain.value = master;
    if (!limiter) return { input: g, output: g };
    const pre = c.createGain(), ws = c.createWaveShaper(), post = c.createGain(), N = 4097, range = 8;
    const curve = new Float32Array(N);
    for (let i = 0; i < N; i++) { const x = (i / (N - 1) * 2 - 1) * range; curve[i] = x / (1 + 0.3 * Math.abs(x)) / range; }
    ws.curve = curve; pre.gain.value = 1 / range; post.gain.value = range;
    g.connect(pre); pre.connect(ws); ws.connect(post);
    return { input: g, output: post };
  };
  // the game's per-voice gains (Audio::voiceGains): 2D = volume both sides; 3D = distance falloff + pan
  Q.voiceGains = function (vol, pos) {
    if (!pos) return [vol, vol];
    const [x, y, z] = pos, dist = Math.hypot(x, y, z);
    let g = vol / (1 + dist / 10);
    if (dist > 90) g = 0;
    const pan = dist > 0.01 ? Math.max(-1, Math.min(1, x / dist)) : 0;   // listener at the origin, yaw 0: right = +X
    return [g * Math.sqrt(0.5 * (1 - pan * 0.8)), g * Math.sqrt(0.5 * (1 + pan * 0.8))];
  };
  // one voice: buffer -> L/R gains -> dest. Returns {src, gate (a gain for live mute), end}
  Q.scheduleVoice = function (c, dest, x, when, gains, pitch) {
    if (!x || !x.length) return null;
    const b = c.createBuffer(1, x.length, R); b.copyToChannel(x, 0);
    const src = c.createBufferSource(); src.buffer = b; src.playbackRate.value = pitch || 1;
    const gate = c.createGain(), gl = c.createGain(), gr = c.createGain(), m = c.createChannelMerger(2);
    gl.gain.value = gains[0]; gr.gain.value = gains[1];
    src.connect(gate); gate.connect(gl); gate.connect(gr); gl.connect(m, 0, 0); gr.connect(m, 0, 1); m.connect(dest);
    src.start(Math.max(when, c.currentTime || 0));
    return { src, gate, end: when + x.length / R / (pitch || 1) };
  };

  // preview playback (one at a time): x at unity, mono to both sides
  let preview = null;
  Q.stopPreview = function () { if (preview) { try { preview.src.stop(); } catch (e) { } preview = null; Q.onPreviewChanged && Q.onPreviewChanged(); } };
  Q.playPreview = function (x, opts) {
    opts = opts || {};
    const c = Q.ctx(); Q.stopPreview();
    if (!x || !x.length) return null;
    const off = Math.max(0, Math.min(x.length - 1, Math.round((opts.from || 0) * R)));
    const to = opts.to != null ? Math.max(off + 1, Math.min(x.length, Math.round(opts.to * R))) : x.length;
    const seg = x.subarray(off, to);
    const v = Q.scheduleVoice(c, Q.previewBus, seg, c.currentTime + 0.005, [opts.vol || 1, opts.vol || 1], opts.pitch || 1);
    preview = { src: v.src, start: c.currentTime + 0.005, from: off / R, dur: seg.length / R / (opts.pitch || 1), pitch: opts.pitch || 1, key: opts.key, tag: opts.tag };
    v.src.onended = () => { if (preview && preview.src === v.src) { preview = null; Q.onPreviewChanged && Q.onPreviewChanged(); } };
    Q.onPreviewChanged && Q.onPreviewChanged();
    return preview;
  };
  Q.previewState = function () {
    if (!preview || !ctx) return null;
    const t = (ctx.currentTime - preview.start) * preview.pitch;
    return Object.assign({}, preview, { pos: preview.from + Math.max(0, t) });
  };
  // game-style random pitch: 'j97' = 0.97 + (rand % 7) * 0.01, 'j90' = 0.9 + (rand % 20) * 0.01
  Q.pitchOf = function (p, jitter) {
    if (p === 'j97') return jitter ? 0.97 + Math.floor(Math.random() * 7) * 0.01 : 1;
    if (p === 'j90') return jitter ? 0.9 + Math.floor(Math.random() * 20) * 0.01 : 1;
    return p || 1;
  };
})();
