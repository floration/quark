// Flips and the half turn: mirror objects across x = c or z = c (Flip X / Flip Z), or both at once, which is the half
// turn about the vertical line through c (the point symmetry a ranked map needs: quark_maptool's ranked_mirror check
// wants every object's twin at (-x, -z) with team A and B swapped and the yaw turned 180 degrees). In place, one
// object at a time: edit.js wraps a selection's flips in one undo step.
import { KINDS } from "./doc.js";
import { tidy } from "./util.js";
import { turnDef, capAxis } from "./repeat.js";

const TWO_PI = 2 * Math.PI;
const normYaw = (y) => Math.round((((y % TWO_PI) + TWO_PI) % TWO_PI) * 1e6) / 1e6;
// the yaw after the flip (forward is (-sin yaw, -cos yaw): across x it is -yaw, across z PI - yaw, both: yaw + PI)
export function flipYaw(yaw, fx, fz) { return normYaw(fx && fz ? yaw + Math.PI : fx ? -yaw : fz ? Math.PI - yaw : yaw); }
// a custom object's definition mirrored inside its unit along axis k (along its start / end pieces' axis the pieces
// also swap ends)
export function mirrorDef(def, k) {
  const a = capAxis(def);
  const flip = (boxes, size) => boxes.map(b => { const mn = b.min.slice(), mx = b.max.slice(); mn[k] = tidy(size[k] - b.max[k]); mx[k] = tidy(size[k] - b.min[k]); return { ...b, min: mn, max: mx }; });
  const piece = (p) => p && { len: p.len, boxes: flip(p.boxes, def.unit.map((v, j) => j === a ? p.len : v)) };
  const out = { ...def, boxes: flip(def.boxes, def.unit) };
  const st = piece(def.start), en = piece(def.end);
  delete out.start; delete out.end;
  const [s0, e0] = k === a ? [en, st] : [st, en];
  if (s0) out.start = s0;
  if (e0) out.end = e0;
  return out;
}
// flips object o (of selection kind `kind`) across x = c[0] when fx, across z = c[1] when fz (both: the half turn)
export function flipObj(kind, o, fx, fz, c) {
  const K = KINDS[kind], ax = [fx ? 0 : -1, fz ? 2 : -1].filter(k => k >= 0), cen = [c[0], 0, c[1]];
  const f = (v, k) => tidy(2 * cen[k] - v);
  if (!ax.length) return;
  if (K.shape === "box") {
    for (const k of ax) { const a = f(o.max[k], k), b = f(o.min[k], k); o.min[k] = a; o.max[k] = b; }
    if (kind === "group") {
      if (o.type === "stairs" && ((fx && o.dir < 2) || (fz && o.dir >= 2))) o.dir ^= 1;   // (0 +x, 1 -x, 2 +z, 3 -z)
      for (const p of o.parts || []) for (const k of ax) { const a = tidy(1 - p.max[k]), b = tidy(1 - p.min[k]); p.min[k] = a; p.max[k] = b; }
    }
    if (kind === "object" && o.def) {
      if (fx && fz) o.rot = ((o.rot || 0) + 2) % 4;   // (a half turn: the definition stays the library's)
      else { o.def = mirrorDef(turnDef(o.def, o.rot || 0), ax[0]); o.rot = 0; }   // (a mirror is no turn: baked into the definition)
    }
  } else if (K.shape === "stairs") {
    if (fx) { const a = f(o.x1, 0), b = f(o.x0, 0); o.x0 = Math.min(a, b); o.x1 = Math.max(a, b); }
    if (fz) { const a = f(o.z1, 2), b = f(o.z0, 2); o.z0 = Math.min(a, b); o.z1 = Math.max(a, b); }
    if ((fx && o.axis === "x") || (fz && o.axis === "z")) o.rising = -(o.rising || 1);
  } else {
    const p = K.shape === "arr" ? o : o.pos;
    for (const k of ax) p[k] = f(p[k], k);
    if (typeof o.yaw === "number") o.yaw = flipYaw(o.yaw, fx, fz);
  }
}
// the point-symmetric twin of an object through (c[0], c[1]): the half turn, team A and B swapped
export function twinObj(kind, o, c = [0, 0]) {
  const t = structuredClone(o);
  flipObj(kind, t, true, true, c);
  if (t.team === "a") t.team = "b"; else if (t.team === "b") t.team = "a";
  return t;
}
