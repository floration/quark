// Quark UI customizer - draws a page the way the game does (src/client/ui.cpp, renderer.cpp): panels with an accent top
// bar, buttons, tabs, toggles ("LABEL: ON"), cycles (< value >), sliders (track, fill, "LABEL: number"), text fields,
// colour pickers, dropdowns and their open popups, labels and hints (fitted: closed up, then smaller, then cut), the
// title logo. All coordinates are game pixels at 1280x720; the caller sets the canvas transform.
'use strict';

const QRender = (() => {
  const FONT = 'QuarkUI, Rubik, "Work Sans", "Segoe UI", Arial, sans-serif';
  const MIN_TS = 1.3;          // Renderer::kMinTextScale
  const EM = 7.5;              // font units: em size (a line is 8 units tall)
  let BASE = 6.0;              // the baseline below the line top: cap tops 0.75 + the game font's cap height (Rubik 5.25)
  let style = {};
  let accent = [0.25, 0.85, 1, 1];
  let uiTs = 2;
  let hintTs = 1.5;
  // Font trial (QFonts entry + options): family, weight, width, size correction (cap height matched to the game's font),
  // letter spacing (em), uppercase forced; text size multiplier and minimum text scale for every text.
  let fc = { family: FONT, weight: 700, stretch: '', sizeK: 1, ls: 0, upper: false };
  let mult = 1, minTs = 0;
  function setFontCfg(c) {
    c = c || {};
    fc = { family: c.family ? `${c.family}, ${FONT}` : FONT, weight: Number(c.weight) || 700, stretch: c.stretch || '',
      sizeK: Number(c.sizeK) > 0 ? Number(c.sizeK) : 1, ls: Number(c.ls) || 0, upper: !!c.upper };
  }
  function setTextOpts(o) {
    o = o || {};
    mult = Number(o.mult) > 0 ? Number(o.mult) : 1;
    minTs = Number(o.minTs) > 0 ? Number(o.minTs) : 0;
  }
  const getFontCfg = () => Object.assign({ mult, minTs }, fc);
  // a text scale as the game asks for it -> as drawn with the trial's minimum and multiplier
  const want = (ts) => Math.max(Number(ts) || uiTs, minTs) * mult;
  const up = (t) => (fc.upper ? String(t).toUpperCase() : t);

  function setStyle(s) {
    style = s || {};
    accent = Array.isArray(style.accent) ? style.accent : [0.25, 0.85, 1, 1];
    uiTs = Number(style.textScale) || 2;
    hintTs = Number(style.hintScale) || uiTs * 0.75;
    const f = style.font || {};
    BASE = Number(f.baseline) > 0 ? Number(f.baseline) : 6.0;
  }
  const btn = () => style.button || {};
  const col = (c, fb) => (Array.isArray(c) && c.length >= 3 ? c : fb);
  function rgba(c) {
    const a = c.length > 3 ? c[3] : 1;
    const v = (x) => Math.round(Math.max(0, Math.min(1, x)) * 255);
    return `rgba(${v(c[0])},${v(c[1])},${v(c[2])},${a})`;
  }
  const mul = (c, k, a) => [c[0] * k, c[1] * k, c[2] * k, a];
  const WHITE = [1, 1, 1, 1], GREY = [0.5, 0.5, 0.5, 1], HINT = [0.7, 0.72, 0.75, 1];

  function rect(ctx, x, y, w, h, c) { ctx.fillStyle = rgba(c); ctx.fillRect(x, y, w, h); }
  // Renderer::rectGradient: corners tl tr br bl. The game's uses blend along one axis (the colour picker's ramps).
  function rectGradient(ctx, x, y, w, h, g) {
    const [tl, tr, br, bl] = g;
    const same = (a, b) => a.every((v, i) => Math.abs(v - b[i]) < 1e-4);
    let lg;
    if (same(tl, tr) && same(bl, br)) { lg = ctx.createLinearGradient(x, y, x, y + h); lg.addColorStop(0, rgba(tl)); lg.addColorStop(1, rgba(bl)); }
    else { lg = ctx.createLinearGradient(x, y, x + w, y); lg.addColorStop(0, rgba(tl)); lg.addColorStop(1, rgba(tr)); }
    ctx.fillStyle = lg;
    ctx.fillRect(x, y, w, h);
  }

  // ---- text (Renderer::text / textWidth / Ui::fit, textfit.h) ----
  const tsOf = (ts) => Math.max(MIN_TS, ts);
  const pxOf = (ts) => EM * tsOf(ts) * fc.sizeK;
  // tk: how far the letters are closed up, in px per gap (the game's fit closes each pair up as far as its ink allows)
  function setFont(ctx, ts, tk) {
    const px = pxOf(ts);
    ctx.font = `${fc.weight} ${px.toFixed(2)}px ${fc.family}`;
    if ('fontStretch' in ctx) ctx.fontStretch = fc.stretch || 'normal';
    if ('letterSpacing' in ctx) ctx.letterSpacing = (fc.ls * px - (tk || 0)).toFixed(2) + 'px';
  }
  function textWidth(ctx, t, ts, tk) {
    setFont(ctx, ts, tk);
    t = up(String(t));
    // (canvas letter spacing also follows the last letter: not part of the text's width)
    const n = [...t].length;
    return ctx.measureText(t).width - (n ? fc.ls * pxOf(ts) - (tk || 0) : 0);
  }
  const textHeight = (ts) => 8 * tsOf(ts);
  function text(ctx, x, y, ts, t, c, tk) {
    if (!t) return;
    setFont(ctx, ts, tk);
    t = up(String(t));
    ctx.textBaseline = 'alphabetic';
    ctx.textAlign = 'left';
    const by = Math.round(y) + BASE * tsOf(ts);
    ctx.lineJoin = 'round';
    ctx.lineWidth = 2 * Math.max(1, 0.15 * tsOf(ts));   // the SDF shader's black outline, max(1, 0.15*scale) px outside
    ctx.strokeStyle = `rgba(0,0,0,${c[3] == null ? 1 : c[3]})`;
    ctx.strokeText(t, x, by);
    ctx.fillStyle = rgba(c);
    ctx.fillText(t, x, by);
  }
  // The most a fit closes letters up (px per gap at scale ts): textfit.h lets a pair close up until its inks are one
  // outline apart, and never by more than 15% of the first letter's advance; for bold capitals that is about 0.4 units.
  const MAX_TK = 0.4;
  const maxTk = (ts) => MAX_TK * tsOf(ts) * fc.sizeK;
  function clipText(ctx, t, ts, maxW) {
    if (textWidth(ctx, t, ts) <= maxW) return t;
    const ell = textWidth(ctx, '...', ts);
    let n = t.length;
    while (n > 0 && textWidth(ctx, t.slice(0, n), ts) + ell > maxW) n--;
    return t.slice(0, n) + '...';
  }
  // Renderer::fitText: t at ts in maxW: as it is, else closed up (one level for every gap), else smaller (closed up all
  // the way), and only below the smallest scale cut with "...". {t, ts, tk}
  function fitAt(ctx, t, ts, maxW) {
    t = String(t == null ? '' : t);
    if (!t || !(maxW > 0) || textWidth(ctx, t, ts) <= maxW) return { t, ts, tk: 0 };
    const gaps = Math.max(1, [...t].length - 1);
    const w0 = textWidth(ctx, t, ts);
    if (w0 - gaps * maxTk(ts) <= maxW) return { t, ts, tk: (w0 - maxW) / gaps + 0.001 };
    // smaller, closed up all the way: width is about proportional to the scale
    const wk = (s) => textWidth(ctx, t, s, maxTk(s));
    let s2 = ts * maxW / wk(ts);
    if (s2 >= MIN_TS) {
      for (let i = 0; i < 3 && wk(s2) > maxW; i++) s2 *= maxW / wk(s2);
      return { t, ts: s2, tk: maxTk(s2) };
    }
    return { t: clipText(ctx, t, MIN_TS, maxW), ts: MIN_TS, tk: 0 };
  }
  // the UI's text size, fitted
  const fit = (ctx, t, maxW) => fitAt(ctx, t, want(uiTs), maxW);
  function centered(ctx, x, y, w, h, t, c) {
    const f = fit(ctx, t, w - 8);
    text(ctx, x + (w - textWidth(ctx, f.t, f.ts, f.tk)) / 2, y + (h - textHeight(f.ts)) / 2, f.ts, f.t, c, f.tk);
  }

  // ---- the title logo (Renderer::logo, label style "logo") ----
  // In Rubik the game draws a baked texture (tools/make_logo.py, from tools/make_icon.py's Q): Rubik wght 800, a SQUARE
  // dilation outline of 18/844 em, the Q split by a line tilted 0.16 (9.09 deg off vertical, top leaning right) through
  // (pen origin + advance / 2, the ink box's centre) with a 12/844 em black seam: left of it and UARK in the friendly
  // colour, right of it in the enemy colour (the player's team colours). Size: the em of the text at that scale with
  // Rubik's cap height (0.70 em) matched to the old UI font's (0.66 em); its baseline 5.7 units below the line top.
  const LOGO = { weight: 800, outline: 18 / 844, seam: 12 / 844, tilt: 0.16, sizeK: 0.66 / 0.70, base: 5.7,
    cyan: [64 / 255, 217 / 255, 1, 1], red: [1, 77 / 255, 64 / 255, 1] };
  let logoFamily = `Rubik, ${FONT}`;
  const logoCache = new Map();
  // the logo's font family (the embedded Rubik once it is loaded); clears the bitmaps drawn with the fallback
  function setLogoFont(family) { logoFamily = family ? `${family}, Rubik, ${FONT}` : `Rubik, ${FONT}`; logoCache.clear(); }
  const logoPx = (ts) => EM * tsOf(ts) * LOGO.sizeK;
  const logoFont = (px) => `${LOGO.weight} ${px.toFixed(2)}px ${logoFamily}`;
  function logoWidth(ctx, t, ts) {
    ctx.font = logoFont(logoPx(ts));
    if ('letterSpacing' in ctx) ctx.letterSpacing = '0px';
    if ('fontStretch' in ctx) ctx.fontStretch = 'normal';
    return ctx.measureText(String(t)).width;
  }
  const mk = (w, h) => { const c = document.createElement('canvas'); c.width = Math.max(1, w); c.height = Math.max(1, h); return c; };
  // The logo at P device px per em, colour c (friendly) for the Q's left and the letters after it, e (enemy) for the
  // Q's right: {canvas, ox, oy} (the text origin, on the baseline, is at (ox, oy) in the canvas).
  function logoBitmap(t, P, c, e) {
    const key = [t, P.toFixed(2), c.slice(0, 3).join(','), e.slice(0, 3).join(','), logoFamily].join('|');
    if (logoCache.has(key)) return logoCache.get(key);
    const R = LOGO.outline * P;
    const m0 = mk(4, 4).getContext('2d');
    m0.font = logoFont(P);
    const m = m0.measureText(t);
    const pad = Math.ceil(R) + 3;
    // (whole-pixel origin: the bitmap lands on the device pixel grid with the text origin where it should be)
    const ox = pad + Math.ceil(m.actualBoundingBoxLeft), oy = pad + Math.ceil(m.actualBoundingBoxAscent);
    const W = ox + Math.ceil(m.actualBoundingBoxRight) + pad + 1, H = oy + Math.ceil(m.actualBoundingBoxDescent) + pad + 1;
    const prep = (g) => { g.font = logoFont(P); g.textBaseline = 'alphabetic'; g.textAlign = 'left'; return g; };
    // outline: the glyph mask (white on black), max-filtered along x then y (lighten = per-pixel max), as alpha
    const M = mk(W, H), mg = prep(M.getContext('2d'));
    mg.fillStyle = '#000'; mg.fillRect(0, 0, W, H);
    mg.fillStyle = '#fff'; mg.fillText(t, ox, oy);
    const pass = (src, horiz) => {
      const D = mk(W, H), g = D.getContext('2d');
      g.fillStyle = '#000'; g.fillRect(0, 0, W, H);
      g.globalCompositeOperation = 'lighten';
      const n = Math.max(2, Math.ceil(2 * R / 0.5));
      for (let i = 0; i <= n; i++) { const d = -R + (2 * R * i) / n; g.drawImage(src, horiz ? d : 0, horiz ? 0 : d); }
      return D;
    };
    const dil = pass(pass(M, true), false);
    const F = mk(W, H), fg = prep(F.getContext('2d'));
    const img = dil.getContext('2d').getImageData(0, 0, W, H), px = img.data;
    for (let i = 0; i < px.length; i += 4) { px[i + 3] = px[i]; px[i] = px[i + 1] = px[i + 2] = 0; }
    fg.putImageData(img, 0, 0);
    // the letters after the first, in the friendly colour (offset keeps the pair kerning of the whole word)
    const first = Array.from(t)[0] || '', rest = t.slice(first.length);
    if (rest) { fg.fillStyle = rgba([c[0], c[1], c[2], 1]); fg.fillText(rest, ox + m.width - fg.measureText(rest).width, oy); }
    // the first letter: friendly, enemy right of the slanted line through its ink-box centre, a black seam on the line
    if (first) {
      const Q = mk(W, H), qg = prep(Q.getContext('2d'));
      const qm = qg.measureText(first);
      const qcx = ox + qm.width / 2;   // (make_icon.py centres Pillow's textbbox: the advance across, the ink down)
      const qcy = oy + (qm.actualBoundingBoxDescent - qm.actualBoundingBoxAscent) / 2;
      const L = 2 * H, top = [qcx + LOGO.tilt * L, qcy - L], bot = [qcx - LOGO.tilt * L, qcy + L];
      qg.fillStyle = rgba([c[0], c[1], c[2], 1]); qg.fillText(first, ox, oy);
      qg.save();
      qg.beginPath(); qg.moveTo(top[0], top[1]); qg.lineTo(top[0] + 4 * W, top[1]); qg.lineTo(bot[0] + 4 * W, bot[1]); qg.lineTo(bot[0], bot[1]); qg.closePath();
      qg.clip();
      qg.fillStyle = rgba([e[0], e[1], e[2], 1]); qg.fillText(first, ox, oy);
      qg.restore();
      qg.globalCompositeOperation = 'source-atop';
      qg.strokeStyle = '#000'; qg.lineWidth = LOGO.seam * P; qg.lineCap = 'butt';
      qg.beginPath(); qg.moveTo(top[0], top[1]); qg.lineTo(bot[0], bot[1]); qg.stroke();
      fg.drawImage(Q, 0, 0);
    }
    const out = { canvas: F, ox, oy };
    if (logoCache.size > 64) logoCache.clear();
    logoCache.set(key, out);
    return out;
  }
  // Draws t as the logo: x = left edge of the advance, y = line top (as text() takes them); e: the enemy colour.
  function logoText(ctx, x, y, ts, t, c, e) {
    if (!t) return;
    const T = ctx.getTransform ? ctx.getTransform() : { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0 };
    const k = Math.hypot(T.a, T.b) || 1;
    const b = logoBitmap(String(t), logoPx(ts) * k, c, col(e, LOGO.red));
    const by = Math.round(y) + LOGO.base * tsOf(ts);
    const dx = T.a * x + T.c * by + T.e, dy = T.b * x + T.d * by + T.f;
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.globalAlpha *= c[3] == null ? 1 : c[3];
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(b.canvas, Math.round(dx - b.ox), Math.round(dy - b.oy));
    ctx.restore();
  }

  // printf-ish for slider numbers: %.2f, %d, %g, %%
  function cfmt(fmt, v) {
    fmt = fmt || '%.0f';
    v = Number(v);
    if (!Number.isFinite(v)) v = 0;
    return String(fmt).replace(/%([-+ 0#]*)(\d*)(?:\.(\d+))?([difgs%])/g, (m, fl, wd, pr, ty) => {
      let s;
      if (ty === '%') return '%';
      if (ty === 'd' || ty === 'i') s = String(Math.trunc(v));
      else if (ty === 'f') s = v.toFixed(pr === undefined ? 6 : +pr);
      else if (ty === 'g') s = String(+v.toPrecision(pr ? +pr : 6));
      else s = String(v);
      if (fl.includes('+') && v >= 0) s = '+' + s;
      if (wd) s = s.padStart(+wd, fl.includes('0') ? '0' : ' ');
      return s;
    });
  }
  const on = (v) => v === true || v === 1 || v === '1' || String(v).toLowerCase() === 'true' || String(v).toLowerCase() === 'on';
  function optionText(e) {
    const opts = Array.isArray(e.options) ? e.options : [];
    const i = parseInt(e.value, 10);
    if (opts.length && i >= 0 && i < opts.length) return String(opts[i]);
    if (opts.length && e.value != null && opts.includes(e.value)) return String(e.value);
    if (opts.length) return String(opts[0]);
    return e.value != null && e.value !== '' ? String(e.value) : '?';
  }
  // "#RRGGBB" (or an RGBA array) -> RGBA 0..1
  function colorOf(v, fb) {
    if (Array.isArray(v) && v.length >= 3) return v;
    const m = /^#?([0-9a-f]{6})$/i.exec(String(v || ''));
    if (!m) return fb;
    const n = parseInt(m[1], 16);
    return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255, 1];
  }
  const hexOf = (c) => '#' + c.slice(0, 3).map((v) => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0')).join('').toUpperCase();
  function hsvToRgb(h, s, v) {
    const k = [5, 3, 1];
    return k.map((kk) => { const t = (kk + h * 6) % 6; return v - v * s * Math.max(0, Math.min(t, 4 - t, 1)); }).concat([1]);
  }

  // ---- widgets ----
  const IDLE = () => col(btn().bg, [0.14, 0.16, 0.2, 0.9]);
  const HOVER = () => col(btn().hover, mul(accent, 0.5, 0.95));
  function buttonLook(ctx, x, y, w, h, label, enabled, hovered) {
    const bg = !enabled ? col(btn().disabled, [0.12, 0.12, 0.14, 0.8]) : hovered ? HOVER() : IDLE();
    rect(ctx, x, y, w, h, bg);
    if (hovered && enabled) rect(ctx, x, y + h - 2, w, 2, accent);
    centered(ctx, x, y, w, h, label, enabled ? col(btn().text, WHITE) : GREY);
  }
  const FIELD = [0.08, 0.09, 0.11, 0.95];

  // Box of an element on the canvas (labels without a size get one from their text); null when it has none (keys).
  function box(ctx, e) {
    if (e.kind === 'label' && (!e.w || !e.h)) {
      const ts = want(e.textScale);
      // (x is the text's left edge even for centred labels: the dump records where it was drawn)
      const tw = e.style === 'logo' ? logoWidth(ctx, e.label || ' ', ts) : textWidth(ctx, e.label || ' ', ts);
      return { x: e.x, y: e.y, w: tw, h: textHeight(ts) };
    }
    if (!(e.w > 0 && e.h > 0)) return null;
    return { x: e.x, y: e.y, w: e.w, h: e.h };
  }

  function drawLabel(ctx, e) {
    const { x, y, w } = e;
    const label = e.label || '';
    const ts = want(e.textScale || (e.hint ? hintTs : uiTs));
    const c = col(e.color, e.hint ? HINT : WHITE);
    if (e.style === 'logo') {   // the title logo (Renderer::logo)
      let ts3 = ts;
      if (e.fit && e.w > 0 && logoWidth(ctx, label, ts) > e.w) ts3 = Math.max(MIN_TS, e.w / (logoWidth(ctx, label, 10) / 10));
      const lw = logoWidth(ctx, label, ts3);
      let lx = x;
      if (e.w > 0) { if (e.align === 'center') lx = x + (w - lw) / 2; else if (e.align === 'right') lx = x + w - lw; }
      logoText(ctx, lx, y + (textHeight(ts) - textHeight(ts3)) / 2, ts3, up(label), c, e.enemy);
      return;
    }
    // the width it is fitted to: maxW (Ui's box or the caller's), else (an older dump's labelFit) its own width
    const limit = Number(e.maxW) > 0 ? Number(e.maxW) : e.fit && e.w > 0 ? e.w : 0;
    let f;
    if (Number(e.track) > 0 && e.w > 0 && !limit) {   // (a raw text call the game closed up itself: its drawn width)
      const nat = textWidth(ctx, label, ts), gaps = Math.max(1, [...label].length - 1);
      f = { t: label, ts, tk: nat > e.w ? Math.min(maxTk(ts), (nat - e.w) / gaps) : 0 };
    } else f = fitAt(ctx, label, ts, limit);
    const tw = textWidth(ctx, f.t, f.ts, f.tk);
    let tx = x;
    if (e.w > 0) {   // aligned in its box (the box is the text's width in the dump; this keeps an edited label centred)
      if (e.align === 'center') tx = x + (w - tw) / 2;
      else if (e.align === 'right') tx = x + w - tw;
    }
    text(ctx, tx, y + (textHeight(ts) - textHeight(f.ts)) / 2, f.ts, f.t, c, f.tk);
  }

  function drawPopup(ctx, e) {
    const { x, y, w, h } = e;
    rect(ctx, x, y, w, h, [0.05, 0.06, 0.08, 0.98]);
    if (e.popup === 'color') {   // Ui::popups: the saturation / value square, the hue bar, the hex field, swatch and R G B
      rect(ctx, x, y, w, 2, accent);
      const [hh, ss, vv] = Array.isArray(e.hsv) ? e.hsv : [0, 0, 1];
      const sv = Array.isArray(e.sv) ? e.sv : [x + 12, y + 12, 168, 120], hb = Array.isArray(e.hue) ? e.hue : [sv[0] + sv[2] + 8, sv[1], 20, sv[3]];
      const hx = Array.isArray(e.hex) ? e.hex : [sv[0], sv[1] + sv[3] + 8, 96, 28];
      const hc = hsvToRgb(hh, 1, 1), cur = colorOf(e.value, hsvToRgb(hh, ss, vv));
      rectGradient(ctx, sv[0], sv[1], sv[2], sv[3], [WHITE, hc, hc, WHITE]);
      rectGradient(ctx, sv[0], sv[1], sv[2], sv[3], [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 1], [0, 0, 0, 1]]);
      for (let i = 0; i < 6; i++) {
        const y0 = hb[1] + hb[3] * i / 6, y1 = hb[1] + hb[3] * (i + 1) / 6;
        const c0 = hsvToRgb(i / 6, 1, 1), c1 = hsvToRgb(((i + 1) % 6) / 6, 1, 1);
        rectGradient(ctx, hb[0], y0, hb[2], y1 - y0, [c0, c0, c1, c1]);
      }
      const cx = sv[0] + ss * sv[2], cy = sv[1] + (1 - vv) * sv[3], rr = 5;
      rect(ctx, cx - rr - 1, cy - rr - 1, 2 * rr + 2, 2 * rr + 2, [0, 0, 0, 1]);
      rect(ctx, cx - rr, cy - rr, 2 * rr, 2 * rr, WHITE);
      rect(ctx, cx - rr + 1, cy - rr + 1, 2 * rr - 2, 2 * rr - 2, cur);
      const yy = hb[1] + hh * hb[3];
      rect(ctx, hb[0] - 2, yy - 2, hb[2] + 4, 4, [0, 0, 0, 1]);
      rect(ctx, hb[0] - 1, yy - 1, hb[2] + 2, 2, WHITE);
      rect(ctx, hx[0], hx[1], hx[2], hx[3], FIELD);
      rect(ctx, hx[0], hx[1] + hx[3] - 2, hx[2], 2, [0.3, 0.3, 0.35, 1]);
      const fts = want(uiTs);
      text(ctx, hx[0] + 6, hx[1] + (hx[3] - textHeight(fts)) / 2, fts, hexOf(cur), WHITE);
      const swx = hx[0] + hx[2] + 8;
      rect(ctx, swx, hx[1], 28, hx[3], cur);
      const v = cur.slice(0, 3).map((q) => Math.round(q * 255)).join(' ');
      const hs = want(hintTs);
      text(ctx, swx + 36, hx[1] + (hx[3] - textHeight(hs)) / 2, hs, v, HINT);
      return;
    }
    // the dropdown's list: an entry a row (rowH = the field's height), the picked one in the accent colour
    const opts = Array.isArray(e.options) ? e.options : [];
    const pad = 8, gap = 4, rowH = Number(e.rowH) > 0 ? Number(e.rowH) : Math.max(16, (h - 2 * pad - (opts.length - 1) * gap) / Math.max(1, opts.length));
    const sel = parseInt(e.value, 10);
    opts.forEach((o, i) => {
      const ex = x + pad, ey = y + pad + i * (rowH + gap), ew = w - 2 * pad;
      rect(ctx, ex, ey, ew, rowH, i === sel ? accent : IDLE());
      centered(ctx, ex, ey, ew, rowH, String(o), WHITE);
    });
  }

  function drawElement(ctx, e, hovered) {
    const { x, y, w, h } = e;
    const enabled = e.enabled !== false;
    const label = e.label || '';
    switch (e.kind) {
      case 'panel': {
        const c = col(e.color, col(style.panelColor, [0.05, 0.06, 0.08, 0.75]));
        rect(ctx, x, y, w, h, c);
        rect(ctx, x, y, w, Number(e.topLineH) > 0 ? Number(e.topLineH) : 2, accent);
        if (label) text(ctx, x + 8, y + 8, want(uiTs), label, [1, 1, 1, 0.9]);
        break;
      }
      case 'rect':
        if (Array.isArray(e.gradient) && e.gradient.length === 4) rectGradient(ctx, x, y, w, h, e.gradient);
        else rect(ctx, x, y, w, h, col(e.color, [1, 1, 1, 0.15]));
        break;
      case 'label': drawLabel(ctx, e); break;
      case 'button':
        buttonLook(ctx, x, y, w, h, label, enabled, hovered);
        break;
      case 'custom':   // a hand-drawn button: the game's own rects and labels around it are its look, this is its hit area
        if (!e.origId) buttonLook(ctx, x, y, w, h, label, enabled, hovered);   // (one added in the customizer: a button)
        break;
      case 'tab': {   // Ui::tab: the current one filled (a page tab accent * 0.6, a sub-page tab accent * 0.35 + an underline)
        if (!on(e.current)) { buttonLook(ctx, x, y, w, h, label, enabled, hovered); break; }
        const k = e.sub ? 0.35 : 0.6;
        rect(ctx, x, y, w, h, mul(accent, k, e.sub ? 0.95 : 1));
        if (e.sub) rect(ctx, x, y + h - 2, w, 2, accent);
        centered(ctx, x, y, w, h, label, WHITE);
        break;
      }
      case 'toggle':
        buttonLook(ctx, x, y, w, h, label + ': ' + (on(e.value) ? 'ON' : 'OFF'), enabled, hovered);
        break;
      case 'cycle': {
        const bw = Math.min(h, w / 3);
        buttonLook(ctx, x, y, bw, h, '<', enabled, false);
        buttonLook(ctx, x + w - bw, y, bw, h, '>', enabled, false);
        rect(ctx, x + bw, y, w - 2 * bw, h, FIELD);
        const f = fit(ctx, optionText(e), w - 2 * bw - 8);
        text(ctx, x + (w - textWidth(ctx, f.t, f.ts, f.tk)) / 2, y + (h - textHeight(f.ts)) / 2, f.ts, f.t, enabled ? WHITE : GREY, f.tk);
        break;
      }
      case 'slider': {
        const lo = Number(e.min), hi = Number(e.max);
        let v = Number(e.value);
        if (!Number.isFinite(v)) v = Number.isFinite(Number(e.def)) ? Number(e.def) : lo || 0;
        const t = Number.isFinite(lo) && Number.isFinite(hi) && hi !== lo ? Math.max(0, Math.min(1, (v - lo) / (hi - lo))) : 0.5;
        rect(ctx, x, y, w, h, FIELD);
        rect(ctx, x, y, w * t, h, mul(accent, 0.45, 0.95));
        const nstr = cfmt(e.fmt, v);
        const maxW = w - 8;
        let lab = label, ts = want(uiTs);
        if (textWidth(ctx, lab + ': ' + nstr, ts) > maxW) {   // (the game's slider shrinks, then cuts the LABEL: no closing up)
          ts = Math.max(MIN_TS, maxW / (textWidth(ctx, lab + ': ' + nstr, 10) / 10));
          if (textWidth(ctx, lab + ': ' + nstr, ts) > maxW + 0.01) lab = clipText(ctx, lab, ts, Math.max(0, maxW - textWidth(ctx, ': ' + nstr, ts)));
        }
        const s = lab + ': ' + nstr;
        const sx = x + (w - textWidth(ctx, s, ts)) / 2, sy = y + (h - textHeight(ts)) / 2;
        text(ctx, sx, sy, ts, s, WHITE);
        const bx = sx + textWidth(ctx, lab + ': ', ts), bw = textWidth(ctx, nstr, ts);
        rect(ctx, bx - 3, y + 3, bw + 6, h - 6, [1, 1, 1, hovered ? 0.14 : 0.05]);
        break;
      }
      case 'text': {
        rect(ctx, x, y, w, h, e.focused ? [0.1, 0.12, 0.16, 1] : FIELD);
        rect(ctx, x, y + h - 2, w, 2, e.focused ? accent : [0.3, 0.3, 0.35, 1]);
        let shown = e.value == null ? '' : String(e.value);
        const fts = want(uiTs);
        const maxW = w - 12 - Math.max(1, Math.round(fts * 0.6)) - 2;
        while (shown && textWidth(ctx, shown, fts) > maxW) shown = shown.slice(1);
        text(ctx, x + 6, y + (h - textHeight(fts)) / 2, fts, shown, WHITE);
        break;
      }
      case 'key': {   // a keybind slot: action name on the left, the key in a field on the right
        const kw = Math.min(w * 0.45, Math.max(60, w * 0.35));
        const f = fit(ctx, label, w - kw - 16);
        text(ctx, x + 4, y + (h - textHeight(f.ts)) / 2, f.ts, f.t, WHITE, f.tk);
        rect(ctx, x + w - kw, y, kw, h, FIELD);
        rect(ctx, x + w - kw, y + h - 2, kw, 2, accent);
        centered(ctx, x + w - kw, y, kw, h, String(e.value || '?'), WHITE);
        break;
      }
      case 'colorpicker': {   // Ui::colorPicker: a button, its label on the left, a 36 x (h - 8, at most 22) swatch on the right
        const open = on(e.open);
        rect(ctx, x, y, w, h, hovered ? HOVER() : IDLE());
        if (hovered || open) rect(ctx, x, y + h - 2, w, 2, accent);
        const sw = 36, sh = Math.min(h - 8, 22), sx = x + w - sw - 8, sy = y + (h - sh) / 2;
        rect(ctx, sx - 1, sy - 1, sw + 2, sh + 2, [0, 0, 0, 1]);
        rect(ctx, sx, sy, sw, sh, colorOf(e.color, colorOf(e.value, WHITE)));
        const f = fit(ctx, label, sx - x - 16);
        text(ctx, x + 8, y + (h - textHeight(f.ts)) / 2, f.ts, f.t, WHITE, f.tk);
        break;
      }
      case 'dropdown': {   // Ui::dropdown: "LABEL: VALUE  V" ("^" while open), centred like a button
        const open = on(e.open);
        rect(ctx, x, y, w, h, hovered ? HOVER() : IDLE());
        if (hovered || open) rect(ctx, x, y + h - 2, w, 2, accent);
        centered(ctx, x, y, w, h, (label ? label + ': ' : '') + optionText(e) + (open ? '  ^' : '  V'), WHITE);
        break;
      }
      case 'popup': drawPopup(ctx, e); break;
      case 'handle': {
        ctx.save();
        ctx.setLineDash([4, 3]);
        ctx.strokeStyle = rgba(mul(accent, 1, 0.9));
        ctx.lineWidth = 1.5;
        ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
        ctx.restore();
        rect(ctx, x, y, w, h, mul(accent, 1, 0.12));
        break;
      }
      case 'view': {
        rect(ctx, x, y, w, h, [0.2, 0.3, 0.35, 0.18]);
        ctx.save();
        ctx.setLineDash([2, 4]);
        ctx.strokeStyle = 'rgba(255,255,255,0.35)';
        ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
        ctx.restore();
        const f = fit(ctx, '3D VIEW: ' + label, w - 8);
        text(ctx, x + 6, y + 6, f.ts, f.t, [1, 1, 1, 0.45], f.tk);
        break;
      }
      default:
        buttonLook(ctx, x, y, w, h, label || e.kind.toUpperCase(), enabled, hovered);
    }
  }

  // The page's backdrop: the menu backdrop (a dark blue night), or the game world dimmed.
  // How a widget's own text fits, as drawElement fits it (the layout check): {t, room, want, ts, cut} for the kinds
  // that fit one line of text into their box (null for the others, and for empty text). ts < want: shrunk; cut: "...".
  function textFitOf(ctx, e) {
    const { w, h } = e;
    if (!(w > 0 && h > 0)) return null;
    const label = e.label || '';
    let t = '', room = 0;
    switch (e.kind) {
      case 'button': case 'tab': t = label; room = w - 8; break;
      case 'custom': if (e.origId) return null; t = label; room = w - 8; break;
      case 'toggle': t = label + ': ' + (on(e.value) ? 'ON' : 'OFF'); room = w - 8; break;
      case 'dropdown': t = (label ? label + ': ' : '') + optionText(e) + '  V'; room = w - 8; break;
      case 'cycle': { const bw = Math.min(h, w / 3); t = optionText(e); room = w - 2 * bw - 8; break; }
      case 'key': t = label; room = w - Math.min(w * 0.45, Math.max(60, w * 0.35)) - 16; break;
      case 'colorpicker': t = label; room = w - 36 - 8 - 16; break;
      case 'slider': {   // (shrinks, then cuts the label: no closing up)
        t = label + ': ' + cfmt(e.fmt, Number.isFinite(Number(e.value)) ? Number(e.value) : Number(e.def) || 0);
        room = w - 8;
        const ws = want(uiTs), tw = textWidth(ctx, t, ws);
        const ts = tw <= room ? ws : Math.max(MIN_TS, room / (textWidth(ctx, t, 10) / 10));
        return { t, room, want: ws, ts, cut: textWidth(ctx, t, ts) > room + 0.01 };
      }
      default: return null;
    }
    if (!t) return null;
    const f = fitAt(ctx, t, want(uiTs), room);
    return { t, room, want: want(uiTs), ts: f.ts, cut: f.t !== t };
  }

  function backdrop(ctx, page, W, H) {
    const g = ctx.createLinearGradient(0, 0, 0, H);
    if (page.editor) { g.addColorStop(0, '#2c3440'); g.addColorStop(1, '#1a1f26'); } else if (page.inGame) { g.addColorStop(0, '#36404a'); g.addColorStop(0.55, '#4a4a40'); g.addColorStop(1, '#2b2a24'); }
    else { g.addColorStop(0, '#0b1420'); g.addColorStop(0.6, '#122233'); g.addColorStop(1, '#070b10'); }
    ctx.fillStyle = g;
    ctx.fillRect(0, 0, W, H);
    if (page.inGame) {   // a horizon and a dim layer (menus over the world darken it)
      ctx.fillStyle = 'rgba(80,90,70,0.5)';
      ctx.fillRect(0, H * 0.58, W, H * 0.42);
      ctx.fillStyle = 'rgba(0,0,0,0.45)';
      ctx.fillRect(0, 0, W, H);
    } else {
      const r = ctx.createRadialGradient(W * 0.5, H * 0.35, 20, W * 0.5, H * 0.35, W * 0.6);
      r.addColorStop(0, 'rgba(64,217,255,0.10)');
      r.addColorStop(1, 'rgba(64,217,255,0)');
      ctx.fillStyle = r;
      ctx.fillRect(0, 0, W, H);
    }
  }

  function drawPage(ctx, page, W, H, hoverUid) {
    const keep = accent;
    if (Array.isArray(page.accent) && page.accent.length >= 3) accent = page.accent;   // (the player's team colour on that capture)
    backdrop(ctx, page, W, H);
    for (const e of page.elements) {
      if (!box(ctx, e)) continue;
      ctx.save();
      drawElement(ctx, e, e.uid === hoverUid);
      ctx.restore();
    }
    accent = keep;
  }

  return { setStyle, setFontCfg, setTextOpts, getFontCfg, drawPage, drawElement, backdrop, box, textWidth, text, rect, cfmt, rgba, MIN_TS,
    setLogoFont, logoWidth, logoText, LOGO, fitAt, textFitOf };
})();

if (typeof module !== 'undefined') module.exports = QRender;
