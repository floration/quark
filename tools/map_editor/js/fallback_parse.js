// A JS reader / writer of .qmap text, for when quark_maptool is missing (viewing a map, saving a simple edit).
// It gives the map JSON of INTERFACE.md. The helper is the real path: it runs the game's own Map::parse / serialize
// (this one doesn't check limits or report every error the game would).
export const MATERIALS = ["box", "accent", "wallbang", "metal", "glow", "far", "water", "lava", "trim", "clip"];
export const MODES = ["vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "1v1", "tourney", "hide", "tag", "points"];
export const PLAYER = { halfWidth: 0.4, height: 1.8, crouchHeight: 1.2, eye: 1.6, crouchEye: 1.05, stepUp: 0.5, jumpHeight: 1.25 };

const D2R = Math.PI / 180;

export function parseQmap(text, name = "") {
  const m = {
    name, title: "", formatVersion: 1, handmade: false, setting: "", preferredMode: null, allowedModes: [], ranked: [],
    sun: [0.4, 1, 0.25], palette: null, void: null, indoor: null, holdfire: 0,
    brushes: [], generated: [], groups: [], spawns: [], sites: [], hill: null, points: [], checkpoints: [], path: [],
    cartChecks: [], ladders: [], stairs: [], lights: [], materials: MATERIALS.slice(), player: { ...PLAYER },
    warnings: [], fallback: true,
  };
  { const l = Math.hypot(...m.sun); m.sun = m.sun.map(v => v / l); }
  const box = (t, i) => ({ min: [+t[i], +t[i + 1], +t[i + 2]], max: [+t[i + 3], +t[i + 4], +t[i + 5]] });
  const lines = text.split("\n");
  for (let ln = 0; ln < lines.length; ln++) {
    let s = lines[ln];
    const h = s.indexOf("#"); if (h >= 0) s = s.slice(0, h);
    const t = s.trim().split(/\s+/); const kw = t[0];
    if (!kw) continue;
    const mi = MATERIALS.indexOf(kw);
    if (mi >= 0) { const b = box(t, 1); b.mat = mi; m.brushes.push(b); continue; }
    switch (kw) {
      case "version": m.formatVersion = +t[1]; break;
      case "handmade": m.handmade = true; break;
      case "setting": m.setting = s.trim().slice(8).trim(); break;
      case "title": m.title = s.trim().slice(6).trim(); break;
      case "for": m.preferredMode = t[1]; break;
      case "modes": m.allowedModes = t.slice(1); break;
      case "ranked": m.ranked = t.slice(1); break;
      case "sun": { const v = [+t[1], +t[2], +t[3]]; const l = Math.hypot(...v) || 1; m.sun = v.map(x => x / l); break; }
      case "void": m.void = +t[1]; break;
      case "indoor": m.indoor = +t[1]; break;
      case "holdfire": m.holdfire = +t[1]; break;
      case "palette": {
        const p = {}; for (let i = 1; i + 3 < t.length + 1; i += 4) if (t[i]) p[t[i]] = [+t[i + 1], +t[i + 2], +t[i + 3]];
        m.palette = p; break;
      }
      case "spawn": m.spawns.push({ team: t[1], pos: [+t[2], +t[3], +t[4]], yaw: (+(t[5] || 0)) * D2R }); break;
      case "site": m.sites.push(box(t, 1)); break;
      case "hill": m.hill = box(t, 1); break;
      case "point": m.points.push(box(t, 1)); break;
      case "checkpoint": m.checkpoints.push(box(t, 1)); break;
      case "path": m.path.push([+t[1], +t[2], +t[3]]); break;
      case "cartcheck": m.cartChecks.push(+t[1]); m.cartChecks.sort((a, b) => a - b); break;
      case "ladder": { const b = box(t, 1); b.auto = t[7] === "auto"; m.ladders.push(b); break; }
      case "light": m.lights.push({ pos: [+t[1], +t[2], +t[3]], radius: +t[4], intensity: t[5] !== undefined ? +t[5] : 1 }); break;
      case "group": { const b = box(t, 4); m.groups.push({ type: t[1], mat: +t[2], dir: +t[3], min: b.min, max: b.max, parts: [] }); break; }
      case "part": { const g = m.groups[m.groups.length - 1]; if (g) { const b = box(t, 2); b.mat = +t[1]; g.parts.push(b); } break; }
      case "stairs": m.stairs.push({ x0: +t[1], z0: +t[2], x1: +t[3], z1: +t[4], base: +t[5], top: +t[6], axis: t[7], rising: +t[8], solid: t[9] === "1" }); break;
      default: m.warnings.push("line " + (ln + 1) + ": unknown '" + kw + "'");
    }
  }
  m.generated = generateParts(m);
  return m;
}

// The boxes made from other objects, as Map::genLadderBrushes / Map::groupBoxes make them (map.cpp): auto ladders'
// rails and rungs (trim), then every group's parts in world space. The page uses this to redraw an edit at once.
export function generateParts(m) {
  const out = [];
  const add = (mat, a, c) => out.push({ mat, min: a, max: c });
  m.ladders.forEach((l, li) => {
    if (!l.auto) return;
    const n0 = out.length;
    const alongX = (l.max[0] - l.min[0]) >= (l.max[2] - l.min[2]);
    const top = Math.max(l.min[1] + 0.5, l.max[1] - 1.0);
    const put = (a0, y0, a1, y1, d0, d1) => alongX ? add(8, [a0, y0, d0], [a1, y1, d1]) : add(8, [d0, y0, a0], [d1, y1, a1]);
    const w0 = alongX ? l.min[0] : l.min[2], w1 = alongX ? l.max[0] : l.max[2];
    const dc = alongX ? (l.min[2] + l.max[2]) * 0.5 : (l.min[0] + l.max[0]) * 0.5;
    put(w0, l.min[1], w0 + 0.08, top, dc - 0.04, dc + 0.04);
    put(w1 - 0.08, l.min[1], w1, top, dc - 0.04, dc + 0.04);
    for (let y = l.min[1] + 0.35; y < top; y += 0.4) put(w0 + 0.08, y, w1 - 0.08, y + 0.06, dc - 0.03, dc + 0.03);
    for (let k = n0; k < out.length; k++) out[k].ladder = li;
  });
  m.groups.forEach((g, gi) => {
    const n0 = out.length;
    groupBoxes(g, out);
    for (let k = n0; k < out.length; k++) out[k].group = gi;
  });
  return out;
}

export function groupBoxes(g, out) {
  const mn = g.min, mx = g.max, sz = [0, 1, 2].map(k => mx[k] - mn[k]);
  const add = (a, c) => { if (c[0] - a[0] > 1e-3 && c[1] - a[1] > 1e-3 && c[2] - a[2] > 1e-3) out.push({ mat: g.mat, min: a, max: c }); };
  if (g.type === "custom") {
    for (const p of g.parts || [])
      out.push({ mat: p.mat, min: [0, 1, 2].map(k => mn[k] + p.min[k] * sz[k]), max: [0, 1, 2].map(k => mn[k] + p.max[k] * sz[k]) });
  } else if (g.type === "room") {
    const t = Math.min(0.25, Math.min(sz[0], sz[1], sz[2]) * 0.25);
    add(mn.slice(), [mx[0], mn[1] + t, mx[2]]);
    add([mn[0], mx[1] - t, mn[2]], mx.slice());
    const y0 = mn[1] + t, y1 = mx[1] - t, dh = Math.min(2.4, (y1 - y0) * 0.85);
    for (let w = 0; w < 4; w++) {
      const alongX = w < 2, a = alongX ? 0 : 2;
      const s0 = alongX ? mn[0] : mn[2] + t, s1 = alongX ? mx[0] : mx[2] - t;
      const d0 = alongX ? (w === 0 ? mn[2] : mx[2] - t) : (w === 2 ? mn[0] : mx[0] - t), d1 = d0 + t;
      const wall = (u0, v0, u1, v1) => { const p = [0, 0, 0], q = [0, 0, 0]; p[a] = u0; q[a] = u1; p[1] = v0; q[1] = v1; p[2 - a] = d0; q[2 - a] = d1; add(p, q); };
      const len = s1 - s0, dw = Math.min(1.6, len - 2 * t - 0.5);
      if (dw < 0.9 || dh < 1.0) { wall(s0, y0, s1, y1); continue; }
      const c = (s0 + s1) * 0.5;
      wall(s0, y0, c - dw * 0.5, y1);
      wall(c + dw * 0.5, y0, s1, y1);
      wall(c - dw * 0.5, y0 + dh, c + dw * 0.5, y1);
    }
  } else {
    const a = g.dir < 2 ? 0 : 2, neg = (g.dir & 1) !== 0;
    const n = Math.min(128, Math.max(1, Math.ceil(sz[1] / 0.5 - 1e-4)));
    const run = sz[a] / n, rise = sz[1] / n;
    for (let k = 0; k < n; k++) {
      const p = mn.slice(), q = mx.slice();
      q[1] = mn[1] + rise * (k + 1);
      if (neg) { q[a] = mx[a] - run * k; p[a] = q[a] - run; } else { p[a] = mn[a] + run * k; q[a] = p[a] + run; }
      add(p, q);
    }
  }
}

// Number formatting like Map::serialize's exactNum (fewest digits that read back as the same float32).
const f32 = new Float32Array(1);
function num(v) {
  f32[0] = v; const x = f32[0];
  for (let d = 6; d <= 9; d++) { const s = (+x.toPrecision(d)).toString(); f32[0] = +s; if (f32[0] === x) return s; }
  return x.toString();
}
function yawDeg(yaw) {
  const deg = yaw / D2R;
  for (let d = 6; d <= 9; d++) { const s = (+deg.toPrecision(d)).toString(); f32[0] = +s * D2R; const a = f32[0]; f32[0] = yaw; if (a === f32[0]) return s; }
  return (+deg.toPrecision(9)).toString();
}

export function serializeQmap(m) {
  let o = "# made with the quark map editor\nversion 2\nhandmade\n";
  const B = (kw, b) => { o += kw + " " + [...b.min, ...b.max].map(num).join(" ") + "\n"; };
  if (m.title) o += "title " + m.title + "\n";
  if (m.setting) o += "setting " + m.setting + "\n";
  if (m.preferredMode) o += "for " + m.preferredMode + "\n";
  if (m.ranked && m.ranked.length) o += "ranked " + m.ranked.join(" ") + "\n";
  if (m.allowedModes && m.allowedModes.length) o += "modes " + m.allowedModes.join(" ") + "\n";
  o += "sun " + m.sun.map(num).join(" ") + "\n\n";
  if (m.void !== null && m.void !== undefined) o += "void " + num(m.void) + "\n";
  if (m.indoor !== null && m.indoor !== undefined) o += "indoor " + num(m.indoor) + "\n";
  if (m.holdfire > 0) o += "holdfire " + num(m.holdfire) + "\n";
  if (m.palette) {
    o += "palette";
    for (const k of ["base", "accent", "metal", "glow", "sky"]) o += " " + k + " " + (m.palette[k] || [0.6, 0.6, 0.6]).map(num).join(" ");
    o += "\n\n";
  }
  for (const b of m.brushes) B(MATERIALS[b.mat], b);
  o += "\n";
  for (const s of m.spawns) o += "spawn " + s.team + " " + s.pos.map(num).join(" ") + " " + yawDeg(s.yaw) + "\n";
  for (const b of m.sites) B("site", b);
  if (m.hill) B("hill", m.hill);
  for (const b of m.points) B("point", b);
  for (const f of m.cartChecks) o += "cartcheck " + num(f) + "\n";
  for (const l of m.ladders) { B("ladder", l); if (l.auto) o = o.slice(0, -1) + " auto\n"; }
  for (const g of m.groups) {
    B("group " + g.type + " " + g.mat + " " + g.dir, g);
    for (const p of g.parts || []) B("part " + p.mat, p);
  }
  for (const s of m.stairs) o += "stairs " + [s.x0, s.z0, s.x1, s.z1, s.base, s.top].map(num).join(" ") + " " + s.axis + " " + num(s.rising) + " " + (s.solid ? 1 : 0) + "\n";
  for (const c of m.checkpoints) B("checkpoint", c);
  for (const p of m.path) o += "path " + p.map(num).join(" ") + "\n";
  for (const l of m.lights) o += "light " + [...l.pos, l.radius, l.intensity].map(num).join(" ") + "\n";
  return o;
}
