/* ===== UI: scenes ===================================================================================
   Pick a scene, your kit / the enemies' kit / the announcer, play it (looped) through the game's mixer, see every
   event on a timeline with its sound, solo / mute lanes and packs live, render it offline (mix loudness + WAV). */
(function () {
  'use strict';
  const Q = window.Q, U = Q.ui, h = U.h, fmt = U.fmt, R = 48000;
  const S = U.scenes = { scene: Q.SCENES[0], playing: null };
  const HEAD = 22;
  // lanes share the timeline's height (20 px at least, 72 at most): at 2560 x 1440 a scene's 9 lanes fill the view
  // instead of its top fifth
  let LANE = 20;
  function fitLanes() { const wrap = U.$('#tlLabels').parentNode, avail = wrap.clientHeight - HEAD - 4; LANE = Math.max(20, Math.min(72, Math.floor(avail / Math.max(1, S.lanes.length)))); }
  const WHO = { me: { label: 'you', color: '#4f9cf5' }, enemy: { label: 'enemy', color: '#ec835a' }, ann: { label: 'announcer', color: '#b48cf0' } };

  // the game's VOLUME: a typed value is held to 0..1, an empty or unreadable field plays (and shows) the default 0.6
  S.master = function () { const t = U.$('#scMaster').value.trim(), v = parseFloat(t); return t === '' || !isFinite(v) ? 0.6 : Math.max(0, Math.min(1, v)); };
  S.opt = function () {
    return { myKit: U.$('#scMyKit').value, enemyKit: U.$('#scEnemyKit').value, voice: U.$('#scVoice').value, master: S.master(),
      limiter: U.$('#scLimiter').checked, jitter: U.$('#scJitter').checked, missing: U.$('#scMissing').checked, loop: U.$('#scLoop').checked };
  };

  function lanesOf(ev) {
    const lanes = [], idx = {};
    for (const e of ev) {
      const id = e.who + ':' + e.snd + (typeof e.pitch === 'number' && e.pitch !== 1 ? '@' + e.pitch : '');
      if (!(id in idx)) { idx[id] = lanes.length; lanes.push({ id, who: e.who, snd: e.snd, kind: e.kind, pitch: e.pitch, missing: e.missing, n: 0, d3: false }); }
      const l = lanes[idx[id]]; l.n++; if (e.pos) l.d3 = true; e.lane = idx[id];
    }
    return lanes;
  }

  S.render = function () {
    const list = U.$('#sceneList'); list.innerHTML = '';
    for (const sc of Q.SCENES) list.append(h('div.list-item' + (sc === S.scene ? '.on' : ''), { onclick: () => { S.scene = sc; const was = !!S.playing; S.stop(); S.render(); if (was) S.play(); } },
      h('div.t', sc.name), h('div.d', sc.len + ' s')));
    U.$('#sceneDesc').textContent = S.scene.desc;
    S.ev = Q.sceneEvents(S.scene);
    S.lanes = lanesOf(S.ev);
    renderMixer();
    fitLanes();
    renderLanes();
    drawTimeline();
    U.$('#scPlay').textContent = S.playing ? '■ Stop' : '▶ Play';
  };
  U.views.scenes = { render: S.render };

  function renderMixer() {
    const o = S.opt(), m = U.$('#sceneMixer'); m.innerHTML = '';
    const strip = (label, kind, dir, color) => h('div.strip', h('span.sw', { style: { background: color } }), h('b', label), h('span.dim', Q.packName(kind, dir)), U.smButtons('Pack', Q.packId(kind, dir), Q.packId(kind, dir)));
    m.append(strip('You', 'sound', o.myKit, WHO.me.color), strip('Enemies', 'sound', o.enemyKit, WHO.enemy.color), strip('Announcer', 'voice', o.voice, WHO.ann.color));
    if (o.myKit === o.enemyKit) m.append(h('span.faint', 'same kit for you and the enemies: a kit mute / solo affects both'));
    const anySolo = Q.mix.soloSound.size + Q.mix.soloPack.size, anyMute = Q.mix.muteSound.size + Q.mix.mutePack.size;
    if (anySolo || anyMute) m.append(h('button.mini', { type: 'button', onclick: () => { for (const k of ['soloSound', 'muteSound', 'soloPack', 'mutePack']) Q.mix[k].clear(); Q.onMixChanged(); } }, 'clear solo / mute'));
  }

  function renderLanes() {
    const o = S.opt(), box = U.$('#tlLabels'); box.innerHTML = '';
    box.append(h('div.ln.head', 'sound (who, 2D / 3D)'));
    for (const l of S.lanes) {
      const ev = S.ev.find(e => e.lane === S.lanes.indexOf(l)), key = Q.eventKey(ev, o), a = Q.assets[key];
      const designed = !l.missing || Q.state.edits[key];
      box.append(h('div.ln', { title: (a ? a.key : key) + (l.missing ? (designed ? ' (designed by you: needs a game hook)' : ' (missing: design it in the Missing tab)') : '') + ', click to edit',
        style: { height: LANE + 'px', opacity: designed ? '' : 0.45 }, onclick: () => a && U.select(key) },
        h('span.who', { style: { background: WHO[l.who].color } }),
        h('span.n', l.snd + (typeof l.pitch === 'number' && l.pitch !== 1 ? ' x' + l.pitch : '') + (l.missing ? ' *' : '')),
        h('span.faint', l.d3 ? '3D' : '2D'),
        U.smButtons('Sound', Q.soundId(l.kind, l.snd), Q.soundId(l.kind, l.snd))));
    }
  }

  function drawTimeline() {
    const cv = U.$('#tlCanvas'), o = S.opt();
    cv.style.height = (HEAD + S.lanes.length * LANE) + 'px';
    const { g, w, h: H } = Q.fitCanvas(cv), len = S.scene.len + 0.5, X = t => 8 + t / len * (w - 16);
    g.fillStyle = Q.css('--wave-bg'); g.fillRect(0, 0, w, H);
    g.font = '10px ui-monospace, Consolas, monospace'; g.textBaseline = 'middle';
    for (let t = 0; t <= len; t += 0.5) {
      const x = Math.round(X(t)) + 0.5;
      g.fillStyle = t % 1 === 0 ? '#232933' : '#1a1e25'; g.fillRect(x, HEAD - 4, 1, H);
      if (t % 1 === 0) { g.fillStyle = Q.css('--text-faint'); g.fillText(t + 's', x + 3, 10); }
    }
    S.lanes.forEach((l, i) => { g.fillStyle = i % 2 ? '#12151a' : '#0f1216'; g.fillRect(0, HEAD + i * LANE, w, LANE - 1); });
    for (const e of S.ev) {
      const key = Q.eventKey(e, o), a = Q.assets[key], x = (Q.gameSync(key) || {}).v;
      const on = Q.eventAudible(e, o) && (!e.missing || Q.state.edits[key]);
      const y = HEAD + e.lane * LANE, dur = x ? x.length / R / (typeof e.pitch === 'number' ? e.pitch : 1) : 0.05;
      const [gl, gr] = Q.voiceGains(e.vol, e.pos), amp = Math.min(1, Math.max(gl, gr) / 0.9);
      g.globalAlpha = on ? 0.25 + 0.75 * amp : 0.12;
      g.fillStyle = WHO[e.who].color;
      g.fillRect(X(e.t), y + 3, Math.max(2, X(e.t + dur) - X(e.t)), LANE - 7);
      g.globalAlpha = on ? 1 : 0.25; g.fillRect(X(e.t), y + 2, 2, LANE - 5);
      g.globalAlpha = 1;
      if (a && !x) Q.getGame(key).then(() => { if (Q.tab === 'scenes') drawTimelineSoon(); }).catch(() => { });
    }
    if (S.playing) {
      const c = Q.ctx(), t = ((c.currentTime - S.playing.t0) % S.playing.len + S.playing.len) % S.playing.len;
      if (c.currentTime >= S.playing.t0) { g.fillStyle = '#fff'; g.fillRect(X(t), 0, 2, H); }
    }
  }
  let dts = 0;
  function drawTimelineSoon() { clearTimeout(dts); dts = setTimeout(drawTimeline, 60); }
  (function anim() { if (S.playing && Q.tab === 'scenes') drawTimeline(); requestAnimationFrame(anim); })();

  // --- realtime playback (looped: the next pass is scheduled just before this one ends, with the edits as they are then)
  S.play = async function () {
    S.stop();
    const o = S.opt(), c = Q.ctx(), scene = S.scene;
    const tok = S.playing = { voices: [], len: scene.len + 0.4, t0: c.currentTime + 0.15, scene };
    const bus = Q.makeGameBus(c, o.master, o.limiter); bus.output.connect(Q.monitor);
    tok.bus = bus;
    U.$('#scPlay').textContent = '■ Stop';
    const pass = async (start) => {
      if (S.playing !== tok) return;
      let prep;
      try { prep = await Q.prepareScene(scene, S.opt()); } catch (e) { U.fail('scene', e); S.stop(); return; }
      if (S.playing !== tok) return;
      tok.voices = tok.voices.filter(v => v.end > c.currentTime).concat(Q.scheduleScene(c, bus.input, prep, S.opt(), Math.max(start, c.currentTime + 0.02)));
      tok.passes = (tok.passes || 0) + 1;
      const next = start + tok.len;
      tok.timer = setTimeout(() => { if (S.playing === tok && S.opt().loop) pass(next); else if (S.playing === tok) tok.timer = setTimeout(() => S.playing === tok && S.stop(), 600); }, Math.max(0, (next - c.currentTime - 0.6) * 1000));
    };
    await pass(tok.t0);
  };
  S.stop = function () {
    const p = S.playing; if (!p) return;
    S.playing = null; clearTimeout(p.timer);
    for (const v of p.voices) { try { v.src.stop(); } catch (e) { } }
    try { p.bus.output.disconnect(); } catch (e) { }
    U.$('#scPlay').textContent = '▶ Play';
    drawTimeline();
  };
  S.toggle = () => S.playing ? S.stop() : S.play();
  S.applyMix = function () {   // solo / mute while playing: every scheduled voice's gate follows
    if (!S.playing) return;
    const o = S.opt();
    for (const v of S.playing.voices) v.gate.gain.value = Q.eventAudible(v.e, o) ? 1 : 0;
  };

  // --- offline render: mix loudness + a WAV -------------------------------------------------------------------------
  S.renderMix = async function () {
    const box = U.$('#sceneRender'); box.innerHTML = 'rendering...';
    try {
      const r = await Q.renderSceneOffline(S.scene, S.opt());
      const l = r.buf.getChannelData(0), rr = r.buf.getChannelData(1), mono = new Float32Array(l.length);
      for (let i = 0; i < l.length; i++) mono[i] = 0.5 * (l[i] + rr[i]);
      const mL = Q.QLoudStereo(r.buf), cv = h('canvas');
      box.innerHTML = '';
      box.append(h('b', S.scene.name + ' mix'), h('span.num', fmt(r.buf.duration, 2) + ' s, ' + r.voices + ' voices'),
        h('span.num', 'integrated ' + fmt(mL.lufs) + ' LUFS'), h('span.num', 'max momentary ' + fmt(mL.mmax) + ' LUFS'), h('span.num', 'true peak ' + fmt(mL.tp) + ' dBTP'), cv,
        h('button', { type: 'button', onclick: () => Q.download(new Blob([Q.encodeWavStereo(r.buf)], { type: 'audio/wav' }), 'quark-scene-' + S.scene.id + '.wav') }, 'Download WAV'),
        h('button', { type: 'button', onclick: () => { Q.playPreview(mono, { tag: 'scene' }); } }, '▶ play render (mono)'));
      requestAnimationFrame(() => Q.drawWave(cv, mono, { t0: 0, t1: r.buf.duration, labels: false }));
      S.lastRender = { lufs: mL.lufs, tp: mL.tp, dur: r.buf.duration, voices: r.voices, events: r.events, peak: Math.max(...[l, rr].map(c => QLoud.samplePeak(c))) };
      return S.lastRender;
    } catch (e) { box.innerHTML = ''; U.fail('render mix', e); return null; }
  };
  // stereo BS.1770 (two channels, weight 1 each)
  Q.QLoudStereo = function (buf) {
    const L = window.QLoud, a = buf.getChannelData(0), b = buf.getChannelData(1), ka = L.kWeight(a, R), kb = L.kWeight(b, R), n = a.length;
    const blk = Math.round(0.4 * R), hop = Math.round(0.1 * R), cum = new Float64Array(n + 1);
    for (let i = 0; i < n; i++) cum[i + 1] = cum[i] + ka[i] * ka[i] + kb[i] * kb[i];
    const z = []; for (let s = 0; s + blk <= n; s += hop) z.push((cum[s + blk] - cum[s]) / blk);
    const lu = v => v > 0 ? -0.691 + 10 * Math.log10(v) : -Infinity;
    const g1 = z.filter(v => lu(v) > -70), m1 = g1.reduce((s, v) => s + v, 0) / Math.max(1, g1.length);
    const g2 = g1.filter(v => lu(v) > lu(m1) - 10);
    return { lufs: g2.length ? lu(g2.reduce((s, v) => s + v, 0) / g2.length) : -Infinity, mmax: z.length ? lu(Math.max(...z)) : -Infinity, tp: L.db(Math.max(L.truePeak(a), L.truePeak(b))) };
  };

  S.init = function () {
    const fill = (sel, list, val) => { const s = U.$(sel); s.innerHTML = ''; for (const p of list) s.append(h('option', { value: p.dir }, p.name)); s.value = val; };
    fill('#scMyKit', Q.KITS, 'default'); fill('#scEnemyKit', Q.KITS, 'heavy_metal'); fill('#scVoice', Q.VOICES, 'male');
    U.$('#scPlay').onclick = () => S.toggle();
    U.$('#scRender').onclick = () => S.renderMix();
    for (const id of ['#scMyKit', '#scEnemyKit', '#scVoice', '#scMissing']) U.$(id).addEventListener('change', () => { S.render(); S.applyMix(); });
    // (the field shows the volume that plays: typed values outside 0..1 snap to it)
    U.$('#scMaster').addEventListener('change', ev => { const v = S.master(); if (ev.target.value.trim() === '' || +ev.target.value !== v) ev.target.value = v; });
    for (const id of ['#scMaster', '#scLimiter']) U.$(id).addEventListener('change', () => { if (S.playing) S.play(); });
  };
})();
