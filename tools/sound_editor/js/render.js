/* ===== RENDER =======================================================================================
   Canvas drawing: waveforms (min / max per pixel, zoomable), spectrograms, mini waveforms for the matrix. */
(function () {
  'use strict';
  const Q = window.Q, R = 48000;

  // (size: {w, h} already read, so a batch of canvases reads the layout once instead of once per canvas)
  Q.fitCanvas = function (cv, size) {
    const dpr = window.devicePixelRatio || 1, w = Math.max(1, size ? size.w : cv.clientWidth), h = Math.max(1, size ? size.h : cv.clientHeight);
    if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); }
    const g = cv.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { g, w, h };
  };
  const css = name => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  Q.css = css;

  /* opts: {t0, t1: the view in seconds, offset: where the data's t = 0 sits on the axis, color, dim: [a, b] a range
     drawn dimmed (outside the trim), regions: [{a, b, fill}], markers: [{t, color, label, dash}], playhead, cursor,
     fades: {a, b, fi, fo}, grid: true} */
  Q.drawWave = function (cv, x, o) {
    const { g, w, h } = Q.fitCanvas(cv), t0 = o.t0, t1 = o.t1, span = Math.max(1e-6, t1 - t0), off = o.offset || 0;
    const X = t => (t - t0) / span * w, mid = h / 2;
    g.fillStyle = css('--wave-bg'); g.fillRect(0, 0, w, h);
    // grid: seconds / tenths
    if (o.grid !== false) {
      const step = span > 4 ? 1 : span > 1.5 ? 0.25 : span > 0.4 ? 0.1 : span > 0.1 ? 0.02 : 0.005;
      g.font = '10px ui-monospace, Consolas, monospace'; g.textBaseline = 'top';
      for (let t = Math.ceil(t0 / step) * step; t <= t1; t += step) {
        const px = Math.round(X(t)) + 0.5;
        g.fillStyle = css('--grid'); g.fillRect(px, 0, 1, h);
        if (o.labels !== false) { g.fillStyle = css('--text-faint'); g.fillText((step < 0.01 ? (t * 1000).toFixed(0) + 'ms' : t.toFixed(step < 0.1 ? 2 : 1) + 's'), px + 3, 2); }
      }
    }
    g.fillStyle = css('--grid-strong'); g.fillRect(0, Math.round(mid), w, 1);
    for (const r of o.regions || []) { g.fillStyle = r.fill; g.fillRect(X(r.a), 0, X(r.b) - X(r.a), h); }
    if (x && x.length) {
      const color = o.color || css('--wave'), dimColor = css('--wave-dim');
      const spp = span * R / w, amp = (h / 2 - 2) * (o.gain || 1);
      if (spp >= 1) {
        for (let px = 0; px < w; px++) {
          const ta = t0 + px / w * span - off, tb = t0 + (px + 1) / w * span - off;
          let a = Math.floor(ta * R), b = Math.ceil(tb * R);
          if (b < 0 || a >= x.length) continue;
          a = Math.max(0, a); b = Math.min(x.length, Math.max(b, a + 1));
          let mn = 1e9, mx = -1e9;
          for (let i = a; i < b; i++) { const v = x[i]; if (v < mn) mn = v; if (v > mx) mx = v; }
          const tm = (a / R) + off, dim = o.dim && (tm < o.dim[0] || tm > o.dim[1]);
          g.fillStyle = dim ? dimColor : color;
          const y0 = mid - Math.min(1.2, mx) * amp, y1 = mid - Math.max(-1.2, mn) * amp;
          g.fillRect(px, y0, 1, Math.max(1, y1 - y0));
        }
      } else {
        g.strokeStyle = color; g.lineWidth = 1.25; g.beginPath();
        const a = Math.max(0, Math.floor((t0 - off) * R) - 1), b = Math.min(x.length, Math.ceil((t1 - off) * R) + 2);
        for (let i = a; i < b; i++) { const px = X(i / R + off), py = mid - x[i] * amp; i === a ? g.moveTo(px, py) : g.lineTo(px, py); }
        g.stroke();
        if (spp < 0.15) { g.fillStyle = color; for (let i = a; i < b; i++) g.fillRect(X(i / R + off) - 1.5, mid - x[i] * amp - 1.5, 3, 3); }
      }
    }
    if (o.fades) {   // fade curves over the kept region
      const f = o.fades; g.strokeStyle = css('--fade'); g.lineWidth = 1.5; g.beginPath();
      g.moveTo(X(f.a), h - 2); g.lineTo(X(f.a + f.fi), 2); g.lineTo(X(f.b - f.fo), 2); g.lineTo(X(f.b), h - 2); g.stroke();
    }
    for (const m of o.markers || []) {
      const px = Math.round(X(m.t)) + 0.5; if (px < -40 || px > w + 40) continue;
      g.strokeStyle = m.color; g.lineWidth = m.width || 1; g.setLineDash(m.dash || []);
      g.beginPath(); g.moveTo(px, 0); g.lineTo(px, h); g.stroke(); g.setLineDash([]);
      if (m.label) { g.font = '10px ui-monospace, Consolas, monospace'; g.fillStyle = m.color; g.textBaseline = 'bottom';
        // (a label that would run off the right edge goes on the marker's left)
        const tw = g.measureText(m.label).width, left = m.right || px + 3 + tw > w; g.fillText(m.label, left ? px - tw - 3 : px + 3, h - 2 - (m.row || 0) * 11); }
    }
    if (o.cursor != null) { g.fillStyle = css('--cursor'); g.fillRect(Math.round(X(o.cursor)), 0, 1, h); }
    if (o.playhead != null) { g.fillStyle = css('--playhead'); g.fillRect(Math.round(X(o.playhead)) - 0.5, 0, 2, h); }
  };

  // --- spectrogram: STFT 1024 / hop 128, cached as an image per buffer ----------------------------------------------
  function fft(re, im) {
    const n = re.length;
    for (let i = 1, j = 0; i < n; i++) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
    for (let len = 2; len <= n; len <<= 1) {
      const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
      for (let i = 0; i < n; i += len) {
        let cr = 1, ci = 0;
        for (let j = 0; j < len / 2; j++) {
          const ar = re[i + j], ai = im[i + j], br = re[i + j + len / 2] * cr - im[i + j + len / 2] * ci, bi = re[i + j + len / 2] * ci + im[i + j + len / 2] * cr;
          re[i + j] = ar + br; im[i + j] = ai + bi; re[i + j + len / 2] = ar - br; im[i + j + len / 2] = ai - bi;
          const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
        }
      }
    }
  }
  const specCache = new WeakMap();
  function palette(v) {   // 0..1 -> dark navy, violet, amber, white
    const s = [[11, 13, 20], [40, 30, 90], [140, 50, 120], [230, 110, 40], [250, 210, 120], [255, 255, 235]];
    const f = Math.max(0, Math.min(0.9999, v)) * (s.length - 1), i = Math.floor(f), t = f - i, a = s[i], b = s[i + 1];
    return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
  }
  function specImage(x) {
    let c = specCache.get(x); if (c) return c;
    const N = 1024, hop = 128, cols = Math.max(1, Math.ceil(x.length / hop)), rows = 256, win = new Float32Array(N);
    for (let i = 0; i < N; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1));
    const cv = document.createElement('canvas'); cv.width = cols; cv.height = rows;
    const g = cv.getContext('2d'), img = g.createImageData(cols, rows), re = new Float32Array(N), im = new Float32Array(N);
    // rows on a log frequency axis 30 Hz .. 24 kHz
    const binOf = new Int32Array(rows);
    for (let r = 0; r < rows; r++) { const f = 30 * Math.pow(24000 / 30, 1 - r / (rows - 1)); binOf[r] = Math.min(N / 2 - 1, Math.round(f / R * N)); }
    for (let col = 0; col < cols; col++) {
      const s = col * hop - N / 2;
      for (let i = 0; i < N; i++) { const j = s + i; re[i] = (j >= 0 && j < x.length ? x[j] : 0) * win[i]; im[i] = 0; }
      fft(re, im);
      for (let r = 0; r < rows; r++) {
        const b = binOf[r], m = Math.hypot(re[b], im[b]) / (N / 4), db = 20 * Math.log10(m + 1e-9), v = (db + 100) / 100;
        const [cr, cg, cb] = palette(v), o = (r * cols + col) * 4;
        img.data[o] = cr; img.data[o + 1] = cg; img.data[o + 2] = cb; img.data[o + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
    c = { cv, hop };
    specCache.set(x, c);
    return c;
  }
  Q.drawSpectrogram = function (cv, x, o) {
    const { g, w, h } = Q.fitCanvas(cv);
    g.fillStyle = '#0b0d14'; g.fillRect(0, 0, w, h);
    if (!x || !x.length) return;
    const im = specImage(x), span = o.t1 - o.t0, off = o.offset || 0;
    const sx = (o.t0 - off) * R / im.hop, sw = span * R / im.hop;
    g.imageSmoothingEnabled = sw < w;
    g.drawImage(im.cv, sx, 0, sw, im.cv.height, 0, 0, w, h);
    g.fillStyle = 'rgba(255,255,255,.45)'; g.font = '10px ui-monospace, Consolas, monospace'; g.textBaseline = 'middle';
    for (const f of [100, 1000, 10000]) { const y = (1 - Math.log(f / 30) / Math.log(24000 / 30)) * h; g.fillRect(0, y, 4, 1); g.fillText(f >= 1000 ? f / 1000 + 'k' : f, 6, y); }
    if (o.playhead != null) { g.fillStyle = Q.css('--playhead'); g.fillRect((o.playhead - o.t0) / span * w, 0, 2, h); }
  };

  // mini waveform (matrix cells): peak envelope, fixed 0..maxT seconds so lengths compare at a glance
  Q.drawMini = function (cv, x, maxT, color, size) {
    const { g, w, h } = Q.fitCanvas(cv, size);
    g.clearRect(0, 0, w, h);
    if (!x || !x.length) return;
    const n = x.length, per = maxT * R / w, mid = h / 2;
    g.fillStyle = color;
    for (let px = 0; px < w; px++) {
      const a = Math.floor(px * per); if (a >= n) break;
      const b = Math.min(n, Math.floor((px + 1) * per));
      let m = 0; for (let i = a; i < b; i += 4) { const v = Math.abs(x[i]); if (v > m) m = v; }
      const hh = Math.max(1, Math.sqrt(m) * (h - 2));   // (sqrt: quiet tails stay visible)
      g.fillRect(px, mid - hh / 2, 1, hh);
    }
  };
})();
