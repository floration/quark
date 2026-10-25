/* ===== LOUDNESS =====================================================================================
   ITU-R BS.1770-4 K-weighting, integrated loudness (gated), max momentary, true peak (4x oversampled),
   and ports of the game's own measures (src/client/audio.cpp: loudness(), Audio::heardLoudness()).
   Pure functions on Float32Array mono clips; works in a page or in node (module.exports at the end). */
(function (root) {
  'use strict';
  const L = {};

  // --- K-weighting filter coefficients for any rate (the BS.1770 pre-filter + RLB high-pass) -------------
  function kCoeffs(rate) {
    // stage 1: high shelf (+4 dB above ~1.5 kHz). Analog prototype from BS.1770 / libebur128.
    let f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    let K = Math.tan(Math.PI * f0 / rate);
    const Vh = Math.pow(10, G / 20), Vb = Math.pow(Vh, 0.4996667741545416);
    let a0 = 1 + K / Q + K * K;
    const s1 = {
      b: [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0],
      a: [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0] };
    // stage 2: RLB high-pass (~38 Hz)
    f0 = 38.13547087602444; Q = 0.5003270373238773;
    K = Math.tan(Math.PI * f0 / rate);
    const s2 = { b: [1, -2, 1], a: [1, 2 * (K * K - 1) / (1 + K / Q + K * K), (1 - K / Q + K * K) / (1 + K / Q + K * K)] };
    return [s1, s2];
  }

  function biquad(x, c) {
    const y = new Float64Array(x.length);
    const [b0, b1, b2] = c.b, a1 = c.a[1], a2 = c.a[2];
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < x.length; i++) {
      const xi = x[i], yi = b0 * xi + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1; x1 = xi; y2 = y1; y1 = yi; y[i] = yi;
    }
    return y;
  }

  L.kWeight = function (x, rate) {
    const [s1, s2] = kCoeffs(rate || 48000);
    return biquad(biquad(x, s1), s2);
  };

  const toLufs = ms => ms > 0 ? -0.691 + 10 * Math.log10(ms) : -Infinity;

  /* Integrated loudness (BS.1770-4): 400 ms blocks, 75 % overlap, absolute gate -70 LUFS, relative gate -10 LU.
     A clip shorter than one block has no complete block: its whole-clip K-weighted mean square is used instead
     (short game sounds: the meter's integrated value would otherwise be undefined). */
  L.integrated = function (x, rate, kw) {
    rate = rate || 48000;
    const y = kw || L.kWeight(x, rate), n = y.length;
    if (!n) return -Infinity;
    const block = Math.round(0.4 * rate), step = Math.round(0.1 * rate);
    if (n < block) { let s = 0; for (let i = 0; i < n; i++) s += y[i] * y[i]; return toLufs(s / n); }
    const cum = new Float64Array(n + 1);
    for (let i = 0; i < n; i++) cum[i + 1] = cum[i] + y[i] * y[i];
    const z = [];
    for (let s = 0; s + block <= n; s += step) z.push((cum[s + block] - cum[s]) / block);
    const absGated = z.filter(v => toLufs(v) > -70);
    if (!absGated.length) return -Infinity;
    const mean = absGated.reduce((a, b) => a + b, 0) / absGated.length;
    const relT = toLufs(mean) - 10;
    const rel = absGated.filter(v => toLufs(v) > relT);
    return toLufs(rel.reduce((a, b) => a + b, 0) / rel.length);
  };

  /* Max momentary loudness: the loudest 400 ms window (10 ms hop). A clip shorter than the window is measured as a
     meter would see it: its energy over a full 400 ms window (the rest silence). */
  L.maxMomentary = function (x, rate, kw) {
    rate = rate || 48000;
    const y = kw || L.kWeight(x, rate), n = y.length, w = Math.round(0.4 * rate), hop = Math.round(0.01 * rate);
    if (!n) return -Infinity;
    const cum = new Float64Array(n + 1);
    for (let i = 0; i < n; i++) cum[i + 1] = cum[i] + y[i] * y[i];
    if (n <= w) return toLufs(cum[n] / w);
    let best = 0;
    for (let s = 0; s + w <= n; s += hop) best = Math.max(best, cum[s + w] - cum[s]);
    best = Math.max(best, cum[n] - cum[n - w]);
    return toLufs(best / w);
  };

  // --- true peak: 4x oversampling, 48-tap (12 per phase) windowed-sinc interpolator, as BS.1770-4 Annex 2 -------
  const TP_PHASES = 4, TP_TAPS = 12;
  const tpFilter = (() => {
    const N = TP_PHASES * TP_TAPS, h = new Float64Array(N), c = (N - 1) / 2;
    for (let i = 0; i < N; i++) {
      const t = (i - c) / TP_PHASES;
      const sinc = t === 0 ? 1 : Math.sin(Math.PI * t) / (Math.PI * t);
      const win = 0.42 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1)) + 0.08 * Math.cos(4 * Math.PI * i / (N - 1)); // Blackman
      h[i] = sinc * win;
    }
    const ph = [];
    for (let p = 0; p < TP_PHASES; p++) {
      const f = new Float64Array(TP_TAPS); let s = 0;
      for (let k = 0; k < TP_TAPS; k++) { f[k] = h[k * TP_PHASES + p]; s += f[k]; }
      for (let k = 0; k < TP_TAPS; k++) f[k] /= s;   // each phase passes DC at unity
      ph.push(f);
    }
    return ph;
  })();

  /* The same result as interpolating every point, much faster: an output point is at most TP_GAIN times the largest
     |sample| under its 12 taps, so a stretch whose block maxima can't beat the peak found so far is skipped (quiet
     tails and most of a clip after its loudest hit). Exact, not an estimate. */
  const TP_GAIN = Math.max(...tpFilter.map(f => f.reduce((s, v) => s + Math.abs(v), 0)));
  L.truePeak = function (x) {   // linear (not dB)
    let peak = 0;
    const n = x.length, nb = Math.ceil((n + TP_TAPS) / TP_TAPS) + 1, bmax = new Float64Array(nb);
    for (let i = 0; i < n; i++) { const a = Math.abs(x[i]); if (a > peak) peak = a; const b = (i / TP_TAPS) | 0; if (a > bmax[b]) bmax[b] = a; }
    for (let b = 0; b * TP_TAPS < n + TP_TAPS; b++) {
      // outputs i in block b read x[i-11 .. i]: blocks b-1 and b
      if (Math.max(bmax[b], b ? bmax[b - 1] : 0) * TP_GAIN <= peak) continue;
      const end = Math.min(n + TP_TAPS, (b + 1) * TP_TAPS);
      for (let i = b * TP_TAPS; i < end; i++) {
        const inner = i >= TP_TAPS - 1 && i < n;
        for (let p = 0; p < TP_PHASES; p++) {
          const f = tpFilter[p]; let s = 0;
          if (inner) for (let k = 0; k < TP_TAPS; k++) s += f[k] * x[i - k];
          else for (let k = 0; k < TP_TAPS; k++) { const j = i - k; if (j >= 0 && j < n) s += f[k] * x[j]; }
          const a = Math.abs(s); if (a > peak) peak = a;
        }
      }
    }
    return peak;
  };

  // per input sample: the largest |value| among it and the 4 oversampled points around it (delay-compensated);
  // the limiter's detector
  L.peakPerSample = function (x) {
    const n = x.length, out = new Float32Array(n), delay = Math.round((TP_PHASES * TP_TAPS - 1) / 2 / TP_PHASES);
    for (let i = 0; i < n; i++) out[i] = Math.abs(x[i]);
    for (let i = 0; i < n + TP_TAPS; i++) {
      const at = i - delay; if (at < 0 || at >= n) continue;
      for (let p = 0; p < TP_PHASES; p++) {
        const f = tpFilter[p]; let s = 0;
        for (let k = 0; k < TP_TAPS; k++) { const j = i - k; if (j >= 0 && j < n) s += f[k] * x[j]; }
        const a = Math.abs(s);
        if (a > out[at]) out[at] = a;
        if (at + 1 < n && a > out[at + 1]) out[at + 1] = a;
      }
    }
    return out;
  };

  L.samplePeak = function (x) { let p = 0; for (let i = 0; i < x.length; i++) { const a = Math.abs(x[i]); if (a > p) p = a; } return p; };

  // --- the game's measures (audio.cpp), linear amplitude ----------------------------------------------------
  // loudness(): the loudest 20 ms RMS (so a clip's level doesn't depend on its tail)
  L.gameLoudness = function (v) {
    const w = 48000 / 50 | 0; let best = 0, e = 0;
    for (let i = 0; i < v.length; i++) {
      e += v[i] * v[i];
      if (i >= w) e -= v[i - w] * v[i - w];
      if (e > best) best = e;
    }
    return Math.sqrt(best / w);
  };
  // Audio::heardLoudness(): two one-pole high-passes at 250 Hz, then the loudest 20 ms RMS (float maths as the game)
  const onePole = hz => 1 - Math.exp(-2 * Math.PI * hz / 48000);
  L.onePole = onePole;
  L.heardLoudness = function (v) {
    const w = 48000 / 50 | 0, sq = new Float32Array(w), a = Math.fround(onePole(250));
    let l1 = 0, l2 = 0, best = 0, e = 0;
    for (let i = 0, k = 0; i < v.length; i++, k = (k + 1 === w ? 0 : k + 1)) {
      l1 = Math.fround(l1 + Math.fround((v[i] - l1) * a));
      let x = Math.fround(v[i] - l1);
      l2 = Math.fround(l2 + Math.fround((x - l2) * a));
      x = Math.fround(x - l2);
      const x2 = Math.fround(x * x);
      e += x2 - sq[k]; sq[k] = x2;
      if (e > best) best = e;
    }
    return Math.sqrt(best / w);
  };

  L.db = v => v > 0 ? 20 * Math.log10(v) : -Infinity;

  /* Everything at once (one K-weighting pass). Returns {dur, lufs, mmax, tp, peak, heard, game} with
     lufs / mmax in LUFS, tp in dBTP, peak in dBFS, heard / game in dBFS (RMS of the loudest 20 ms). */
  L.measure = function (x, rate) {
    rate = rate || 48000;
    if (!x || !x.length) return { dur: 0, lufs: -Infinity, mmax: -Infinity, tp: -Infinity, peak: -Infinity, heard: -Infinity, game: -Infinity };
    const kw = L.kWeight(x, rate);
    return {
      dur: x.length / rate,
      lufs: L.integrated(x, rate, kw),
      mmax: L.maxMomentary(x, rate, kw),
      tp: L.db(L.truePeak(x)),
      peak: L.db(L.samplePeak(x)),
      heard: L.db(L.heardLoudness(x)),
      game: L.db(L.gameLoudness(x)),
    };
  };

  root.QLoud = L;
  if (typeof module !== 'undefined') module.exports = L;
})(typeof window !== 'undefined' ? window : globalThis);
