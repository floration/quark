/* ===== DSP ==========================================================================================
   The non-destructive edit: trim -> fades -> chain steps (each with bypass) -> output. Reverb runs in an
   OfflineAudioContext (a convolver); everything else (filters, gain, pitch resample, compressor, drive, limiter...) in
   plain JS, and each step's result is cached by its prefix (Q.renderEditData). Also ports of the
   game's own clip preparation (audio.cpp prepareClip / levelHit / voice()), so the editor plays what the game will. */
(function () {
  'use strict';
  const Q = window.Q, L = window.QLoud, R = 48000;
  const dbToLin = db => Math.pow(10, db / 20);
  Q.dbToLin = dbToLin;

  // --- step definitions: label, default params, param specs (for the UI) -------------------------------------
  Q.STEPS = {
    gain: { label: 'Gain', p: { db: 0 }, spec: [['db', 'dB', -40, 24, 0.1]] },
    highpass: { label: 'High-pass', p: { freq: 80, q: 0.707 }, spec: [['freq', 'Hz', 10, 20000, 1, 'log'], ['q', 'Q', 0.1, 18, 0.01]] },
    lowpass: { label: 'Low-pass', p: { freq: 12000, q: 0.707 }, spec: [['freq', 'Hz', 10, 20000, 1, 'log'], ['q', 'Q', 0.1, 18, 0.01]] },
    lowshelf: { label: 'Low shelf', p: { freq: 200, db: 0 }, spec: [['freq', 'Hz', 20, 2000, 1, 'log'], ['db', 'dB', -24, 24, 0.1]] },
    peaking: { label: 'Peak EQ', p: { freq: 1000, db: 0, q: 1 }, spec: [['freq', 'Hz', 20, 20000, 1, 'log'], ['db', 'dB', -24, 24, 0.1], ['q', 'Q', 0.1, 18, 0.01]] },
    highshelf: { label: 'High shelf', p: { freq: 6000, db: 0 }, spec: [['freq', 'Hz', 500, 20000, 1, 'log'], ['db', 'dB', -24, 24, 0.1]] },
    pitch: { label: 'Pitch / speed', p: { semis: 0 }, spec: [['semis', 'st', -24, 24, 0.1]] },
    compressor: { label: 'Compressor', p: { threshold: -18, ratio: 4, knee: 6, attack: 3, release: 80, makeup: 0 },
      spec: [['threshold', 'dB', -60, 0, 0.5], ['ratio', ':1', 1, 20, 0.1], ['knee', 'dB knee', 0, 24, 0.5], ['attack', 'ms att', 0.1, 100, 0.1], ['release', 'ms rel', 5, 1000, 1], ['makeup', 'dB makeup', 0, 24, 0.1]] },
    drive: { label: 'Drive / soft clip', p: { amount: 6, mix: 100 }, spec: [['amount', 'dB drive', 0, 36, 0.1], ['mix', '% mix', 0, 100, 1]] },
    reverse: { label: 'Reverse', p: {}, spec: [] },
    reverb: { label: 'Reverb', p: { size: 0.8, predelay: 10, damp: 4000, mix: 20 },
      spec: [['size', 's', 0.05, 5, 0.01], ['predelay', 'ms pre', 0, 200, 1], ['damp', 'Hz damp', 500, 18000, 10, 'log'], ['mix', '% wet', 0, 100, 1]] },
    normalize: { label: 'Normalize (peak)', p: { db: -1 }, spec: [['db', 'dBFS', -30, 0, 0.1]] },
    limiter: { label: 'Limiter (true peak)', p: { ceiling: -1 }, spec: [['ceiling', 'dBTP', -20, 0, 0.1]] },
  };
  Q.newStep = (type, p) => ({ id: Q.uid(), type, on: true, p: Object.assign({}, Q.STEPS[type].p, p || {}) });

  // --- offline graph helper: x through a node chain made by build(ctx) -----------------------------------------
  async function offline(x, outLen, build) {
    const n = Math.max(1, outLen);
    const ctx = new OfflineAudioContext(1, n, R);
    const buf = ctx.createBuffer(1, Math.max(1, x.length), R);
    buf.copyToChannel(x.length ? x : new Float32Array(1), 0);
    const src = ctx.createBufferSource(); src.buffer = buf;
    const last = build(ctx, src);
    last.connect(ctx.destination);
    src.start(0);
    const out = await ctx.startRendering();
    return out.getChannelData(0).slice(0, outLen);
  }

  /* The Web Audio BiquadFilterNode in plain JS (its spec's coefficients, so a chain sounds exactly as it did when it
     rendered in an OfflineAudioContext): no context per step, about 1 ms for a second of sound instead of 20-60 ms.
     As the node does, low-pass / high-pass read Q in dB, the shelves ignore it (S = 1), peaking reads it linearly. */
  Q.biquadCoefs = function (type, p) {
    const f0 = Math.max(0, Math.min(p.freq, R / 2 - 10)), w0 = 2 * Math.PI * f0 / R, cw = Math.cos(w0), sw = Math.sin(w0);
    const A = Math.pow(10, (p.db || 0) / 40), q = p.q == null ? 1 : p.q;
    let b0, b1, b2, a0, a1, a2;
    if (type === 'lowpass' || type === 'highpass') {
      const al = sw / (2 * Math.pow(10, q / 20)), lp = type === 'lowpass';
      b0 = lp ? (1 - cw) / 2 : (1 + cw) / 2; b1 = lp ? 1 - cw : -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
    } else if (type === 'peaking') {
      const al = sw / (2 * Math.max(q, 1e-4));
      b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A;
    } else {   // lowshelf / highshelf, S = 1
      const al = sw / 2 * Math.SQRT2, k = 2 * al * Math.sqrt(A), s = type === 'lowshelf' ? 1 : -1;
      b0 = A * ((A + 1) - s * (A - 1) * cw + k); b1 = s * 2 * A * ((A - 1) - s * (A + 1) * cw); b2 = A * ((A + 1) - s * (A - 1) * cw - k);
      a0 = (A + 1) + s * (A - 1) * cw + k; a1 = -s * 2 * ((A - 1) + s * (A + 1) * cw); a2 = (A + 1) + s * (A - 1) * cw - k;
    }
    return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
  };
  function biquadStep(x, type, p) {   // direct form I, double precision state (like Chrome's Biquad)
    const [b0, b1, b2, a1, a2] = Q.biquadCoefs(type, p), y = new Float32Array(x.length);
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < x.length; i++) {
      const v = x[i], o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1; x1 = v; y2 = y1; y1 = o; y[i] = o;
    }
    return y;
  }
  Q.biquadOffline = (x, type, p) => offline(x, x.length, (ctx, src) => {   // (the node itself: tests compare the two)
    const f = ctx.createBiquadFilter();
    f.type = type; f.frequency.value = Math.min(p.freq, R / 2 - 10);
    if (p.q != null) f.Q.value = p.q;
    if (p.db != null) f.gain.value = p.db;
    src.connect(f); return f;
  });

  // a deterministic room: noise decaying 60 dB over `size`, darker with time (the damping)
  function impulse(p) {
    const n = Math.max(32, Math.round(p.size * R)), pre = Math.round(p.predelay / 1000 * R), h = new Float32Array(n + pre);
    let s = 0x2545f491, lp = 0, e = 0;
    for (let i = 0; i < n; i++) {
      s ^= s << 13; s ^= s >>> 17; s ^= s << 5;
      const noise = ((s >>> 0) / 4294967296) * 2 - 1, t = i / n;
      const cut = 16000 * Math.pow(p.damp / 16000, t);       // the tail loses its top
      const a = 1 - Math.exp(-2 * Math.PI * cut / R);
      lp += (noise - lp) * a;
      h[pre + i] = lp * Math.exp(-6.9 * t);
    }
    for (let i = 0; i < h.length; i++) e += h[i] * h[i];
    const g = 1 / Math.sqrt(e || 1);
    for (let i = 0; i < h.length; i++) h[i] *= g;
    return h;
  }
  async function reverbStep(x, p) {
    const h = impulse(p), outLen = x.length + h.length;
    const wet = await offline(x, outLen, (ctx, src) => {
      const conv = ctx.createConvolver(); conv.normalize = false;
      const hb = ctx.createBuffer(1, h.length, R); hb.copyToChannel(h, 0); conv.buffer = hb;
      src.connect(conv); return conv;
    });
    const m = p.mix / 100, out = new Float32Array(outLen);
    for (let i = 0; i < outLen; i++) out[i] = (i < x.length ? x[i] * (1 - m) : 0) + wet[i] * m;
    return out;
  }

  // Hermite (4-point) resample: speed > 1 = higher and shorter, as the game's pitch (playback rate)
  Q.resample = function (x, speed) {
    if (Math.abs(speed - 1) < 1e-9) return x.slice();
    const n = Math.max(1, Math.floor((x.length - 1) / speed) + 1), y = new Float32Array(n), last = x.length - 1;
    const at = i => x[i < 0 ? 0 : i > last ? last : i];
    for (let i = 0; i < n; i++) {
      const t = i * speed, k = Math.floor(t), f = t - k;
      const xm1 = at(k - 1), x0 = at(k), x1 = at(k + 1), x2 = at(k + 2);
      const c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
      y[i] = ((c3 * f + c2) * f + c1) * f + x0;
    }
    return y;
  };

  function compressor(x, p) {
    const y = new Float32Array(x.length), at = Math.exp(-1 / (p.attack / 1000 * R)), rt = Math.exp(-1 / (p.release / 1000 * R));
    const T = p.threshold, Rr = Math.max(1, p.ratio), W = p.knee, mk = dbToLin(p.makeup);
    let env = 0;
    for (let i = 0; i < x.length; i++) {
      const a = Math.abs(x[i]);
      env = a > env ? at * env + (1 - at) * a : rt * env + (1 - rt) * a;
      const lv = env > 1e-9 ? 20 * Math.log10(env) : -180;
      let out;
      if (2 * (lv - T) < -W) out = lv;
      else if (2 * Math.abs(lv - T) <= W) out = lv + (1 / Rr - 1) * Math.pow(lv - T + W / 2, 2) / (2 * W || 1);
      else out = T + (lv - T) / Rr;
      y[i] = x[i] * dbToLin(out - lv) * mk;
    }
    return y;
  }

  function drive(x, p) {
    const k = dbToLin(p.amount), norm = Math.tanh(k), m = p.mix / 100, y = new Float32Array(x.length);
    for (let i = 0; i < x.length; i++) y[i] = (1 - m) * x[i] + m * Math.tanh(k * x[i]) / norm;
    return y;
  }

  // lookahead true-peak limiter: instant attack across a 1.5 ms lookahead, 60 ms release, no added delay
  Q.limit = function (x, ceilingDb) {
    const c = dbToLin(ceilingDb), n = x.length, la = Math.round(0.0015 * R), rel = 1 - Math.exp(-1 / (0.06 * R));
    if (!n) return x.slice();
    const pk = L.peakPerSample(x), req = new Float32Array(n);
    for (let i = 0; i < n; i++) req[i] = pk[i] > c ? c / pk[i] : 1;
    // forward-looking minimum over [i, i + la]
    const gmin = new Float32Array(n), dq = [];
    for (let i = n - 1; i >= 0; i--) {
      while (dq.length && req[dq[dq.length - 1]] >= req[i]) dq.pop();
      dq.push(i);
      while (dq[0] > i + la) dq.shift();
      gmin[i] = req[dq[0]];
    }
    // instant attack, exponential release
    const r = new Float32Array(n); let g = 1;
    for (let i = 0; i < n; i++) { g = gmin[i] < g ? gmin[i] : g + (1 - g) * rel; if (g > gmin[i]) g = gmin[i]; r[i] = g; }
    // average over the trailing lookahead window: the gain reaches each peak's need by the time it arrives
    const y = new Float32Array(n); let s = 0;
    for (let i = 0; i < n; i++) {
      s += r[i]; if (i >= la) s -= r[i - la];
      const avg = s / Math.min(i + 1, la);
      y[i] = x[i] * Math.min(avg, r[i]);
    }
    // what smoothing still lets through (rare): a last gentle pass
    const tp = L.truePeak(y);
    if (tp > c * 1.0005) { const k = c / tp; for (let i = 0; i < n; i++) y[i] *= k; }
    return y;
  };

  Q.applyStep = async function (x, st) {
    const p = st.p;
    switch (st.type) {
      case 'gain': { const g = dbToLin(p.db), y = new Float32Array(x.length); for (let i = 0; i < x.length; i++) y[i] = x[i] * g; return y; }
      case 'highpass': case 'lowpass': case 'lowshelf': case 'peaking': case 'highshelf':
        return x.length ? biquadStep(x, st.type, p) : x;
      case 'pitch': return Q.resample(x, Math.pow(2, p.semis / 12));
      case 'compressor': return compressor(x, p);
      case 'drive': return drive(x, p);
      case 'reverse': return x.slice().reverse();
      case 'reverb': return x.length ? reverbStep(x, p) : x;
      case 'normalize': { const pk = L.samplePeak(x); if (pk < 1e-9) return x.slice(); const g = dbToLin(p.db) / pk, y = new Float32Array(x.length); for (let i = 0; i < x.length; i++) y[i] = x[i] * g; return y; }
      case 'limiter': return Q.limit(x, p.ceiling);
    }
    return x;
  };

  // trim (seconds in the source) + fades
  Q.trimFade = function (src, e) {
    const a = Math.max(0, Math.min(src.length, Math.round((e.trim[0] || 0) * R)));
    const b = e.trim[1] == null ? src.length : Math.max(a, Math.min(src.length, Math.round(e.trim[1] * R)));
    const x = src.slice(a, b), n = x.length;
    const fi = Math.min(n, Math.round((e.fadeIn || 0) * R)), fo = Math.min(n, Math.round((e.fadeOut || 0) * R));
    for (let i = 0; i < fi; i++) x[i] *= Math.sin(0.5 * Math.PI * i / fi) ** 2;
    for (let i = 0; i < fo; i++) x[n - 1 - i] *= Math.sin(0.5 * Math.PI * i / fo) ** 2;
    return x;
  };

  /* Rendering an edit, step by step, with every intermediate result cached by its prefix signature (the source, trim
     and fades, and each step so far): moving a slider on step k re-renders only steps k.. (and levelling, which adds
     its gain at the end, renders one step). Results are never changed in place, so they can be shared. Budget: 96 MB
     of samples, least recently used out first. */
  const stepCache = new Map(); let stepBytes = 0;
  const STEP_BUDGET = 96 * 1048576;
  const srcIds = new WeakMap(); let srcSeq = 0;
  const srcId = x => { let id = srcIds.get(x); if (!id) srcIds.set(x, id = 'x' + (++srcSeq)); return id; };
  function cacheGet(sig) { const v = stepCache.get(sig); if (v) { stepCache.delete(sig); stepCache.set(sig, v); } return v; }
  function cachePut(sig, v) {
    if (stepCache.has(sig)) return;
    stepCache.set(sig, v); stepBytes += v.byteLength;
    for (const [k, old] of stepCache) { if (stepBytes <= STEP_BUDGET) break; stepCache.delete(k); stepBytes -= old.byteLength; }
  }
  Q.stepCacheInfo = () => ({ entries: stepCache.size, mb: stepBytes / 1048576 });
  Q.renderEditData = async function (src, e, uptoStep) {
    const chain = (uptoStep == null ? e.chain : e.chain.slice(0, uptoStep)).filter(st => st.on);
    const sigs = [srcId(src) + '|' + (e.trim[0] || 0) + ':' + e.trim[1] + ':' + (e.fadeIn || 0) + ':' + (e.fadeOut || 0)];
    for (const st of chain) sigs.push(sigs[sigs.length - 1] + '|' + st.type + JSON.stringify(st.p));
    let i = sigs.length - 1, x;
    while (i >= 0 && !(x = cacheGet(sigs[i]))) i--;   // the longest prefix already rendered
    if (!x) { x = Q.trimFade(src, e); cachePut(sigs[0], x); i = 0; }
    for (let k = i; k < chain.length; k++) { x = await Q.applyStep(x, chain[k]); cachePut(sigs[k + 1], x); }
    return x;
  };

  // --- the game's clip preparation (audio.cpp) -----------------------------------------------------------------
  function cutRumble(v) {
    const a = Math.fround(L.onePole(25)); let l1 = 0, l2 = 0;
    for (let i = 0; i < v.length; i++) { const s = v[i]; l1 += (s - l1) * a; const x = s - l1; l2 += (x - l2) * a; v[i] = x - l2; }
  }
  function levelHit(v, match) {
    cutRumble(v);
    const want = L.heardLoudness(match);
    for (let pass = 0; pass < 4; pass++) {
      const have = L.heardLoudness(v); let peak = 1e-6;
      if (want < 1e-6 || have < 1e-6) return;
      const g = want / have;
      for (let i = 0; i < v.length; i++) { v[i] *= g; peak = Math.max(peak, Math.abs(v[i])); }
      if (peak <= 0.95) return;
      if (pass === 3) { for (let i = 0; i < v.length; i++) v[i] *= 0.95 / peak; return; }
      for (let i = 0; i < v.length; i++) v[i] = 0.95 * Math.tanh(v[i] / 0.95);
    }
  }
  // the game's leading-silence rule: samples below 3 % of the clip's peak are dropped from the start
  Q.leadingSilence = function (x, frac) {
    let peak = 1e-6; for (let i = 0; i < x.length; i++) peak = Math.max(peak, Math.abs(x[i]));
    const t = peak * (frac == null ? 0.03 : frac); let on = 0;
    while (on < x.length && Math.abs(x[on]) < t) on++;
    return on;
  };
  /* prepareClip: leading silence off, cut at cap with a 10 ms fade, as loud as `match` (keepLevel: the clip is in
     sound-levels.txt, so the game keeps its own level). Returns {v, gainDb, cutStart, capped}. */
  Q.prepareGame = function (x, cap, match, heard, keepLevel) {
    const on = Q.leadingSilence(x), n = Math.min(x.length - on, Math.round(R * cap));
    const v = x.slice(on, on + Math.max(0, n));
    const fade = Math.min(v.length, R / 100);
    for (let i = 0; i < fade; i++) v[v.length - 1 - i] *= i / fade;
    const before = L.gameLoudness(v), info = { cutStart: on / R, capped: x.length - on > R * cap, gainDb: 0, mode: 'kept' };
    if (keepLevel || !v.length) { info.v = v; return info; }
    if (heard && match && L.heardLoudness(match) > 1e-6) {
      const h0 = L.heardLoudness(v); levelHit(v, match);
      info.gainDb = L.db(L.heardLoudness(v)) - L.db(h0); info.mode = 'heard'; info.v = v; return info;
    }
    const want = match ? L.gameLoudness(match) : 0, have = before;
    if (want > 1e-6 && have > 1e-6) {
      const g = want / have;
      for (let i = 0; i < v.length; i++) v[i] = Math.max(-1, Math.min(1, v[i] * g));
      info.gainDb = L.db(g); info.mode = 'matched';
    } else {
      let m = 1e-6; for (let i = 0; i < v.length; i++) m = Math.max(m, Math.abs(v[i]));
      for (let i = 0; i < v.length; i++) v[i] *= 0.89 / m;
      info.gainDb = L.db(0.89 / m); info.mode = 'peak 0.89';
    }
    info.v = v;
    return info;
  };
  // RETRO: DEFAULT x1.1 pitch, 4-bit levels held 4 samples, loudness-matched (audio.cpp voice())
  Q.chiptune = function (b) {
    if (!b.length) return b;
    const before = L.gameLoudness(b), pitch = 1.1, o = new Float32Array(Math.floor(b.length / pitch) + 1);
    for (let i = 0; i < o.length; i++) {
      const x = i * pitch, k = Math.floor(x), f = x - k;
      o[i] = k + 1 < b.length ? b[k] + (b[k + 1] - b[k]) * f : (k < b.length ? b[k] : 0);
    }
    let held = 0;
    for (let i = 0; i < o.length; i++) { if (i % 4 === 0) held = Math.round(o[i] * 8) / 8; o[i] = held; }
    const after = L.gameLoudness(o);
    if (after > 1e-6) { const g = before / after; for (let i = 0; i < o.length; i++) o[i] = Math.max(-1, Math.min(1, o[i] * g)); }
    return o;
  };

  // --- a small generator, to design a sound from nothing (the game's synthesis building blocks) ---------------------
  Q.generate = function (g) {
    const n = Math.round(g.len * R), x = new Float32Array(n);
    let s = 0x12345678 + (g.seed || 0), lp = 0, hp = 0, ph = 0;
    const aL = 1 - Math.exp(-2 * Math.PI * g.noiseHi / R), aH = 1 - Math.exp(-2 * Math.PI * g.noiseLo / R);
    for (let i = 0; i < n; i++) {
      const t = i / R;
      const env = (t < g.attack ? Math.sin(0.5 * Math.PI * t / g.attack) : 1) * Math.exp(-t / Math.max(0.001, g.decay));
      const f = g.f1 + (g.f0 - g.f1) * Math.exp(-t / Math.max(0.001, g.sweep));
      ph += 2 * Math.PI * f / R;
      s = (s * 1664525 + 1013904223) >>> 0;
      const nz = ((s >>> 9) & 0xffff) / 32768 - 1;
      lp += (nz - lp) * aL; hp += (lp - hp) * aH;
      x[i] = env * (g.tone * Math.sin(ph) + g.noise * (lp - hp) * 2.5);
    }
    const pk = L.samplePeak(x); if (pk > 0) for (let i = 0; i < n; i++) x[i] *= 0.8 / pk;
    return x;
  };

  // --- WAV (48 kHz 16-bit mono PCM) --------------------------------------------------------------------------------
  Q.encodeWav = function (x, rate) {
    rate = rate || R;
    const n = x.length, buf = new ArrayBuffer(44 + n * 2), d = new DataView(buf);
    const w = (o, s) => { for (let i = 0; i < s.length; i++) d.setUint8(o + i, s.charCodeAt(i)); };
    w(0, 'RIFF'); d.setUint32(4, 36 + n * 2, true); w(8, 'WAVE'); w(12, 'fmt ');
    d.setUint32(16, 16, true); d.setUint16(20, 1, true); d.setUint16(22, 1, true); d.setUint32(24, rate, true);
    d.setUint32(28, rate * 2, true); d.setUint16(32, 2, true); d.setUint16(34, 16, true); w(36, 'data'); d.setUint32(40, n * 2, true);
    for (let i = 0; i < n; i++) { const v = Math.max(-1, Math.min(1, x[i])); d.setInt16(44 + i * 2, Math.round(v * 32767), true); }
    return new Uint8Array(buf);
  };
})();
