// Custom objects that repeat (a railing, a fence, a pillar row, floor tiles): a definition is a unit (a few boxes in a
// box of its own) and, per axis, whether it repeats and the spacing. An object placed in a map is that definition and
// a box: making the box longer along a repeating axis adds units (spacing apart), it never stretches them; the other
// axes keep the unit's size. Objects live in the sidecar ("objects") so they stay editable; a save writes their boxes
// into the .qmap as plain boxes the game loads (App.mapForSave), and opening the map takes those boxes out again.
//
// def: {name, unit: [x, y, z] (the unit's size), axes: [bool, bool, bool] (repeats along x / y / z), spacing: [x, y, z]
//       (unit start to next unit start, >= 0.05), boxes: [{mat, min, max}] (relative to the unit's min corner)}
// object (sidecar.objects[i]): {name, def, rot (quarter turns about y, 0-3), min, max}
export const MAX_REPEAT_BOXES = 20000;   // (one object; the map's own limit is 60 000 boxes)

const r5 = (v) => Math.round(v * 1e5) / 1e5;

// the definition turned q quarter turns about y (inside its unit: x' = uz - z, z' = x)
export function turnDef(def, q) {
  q = ((q % 4) + 4) % 4;
  let d = def;
  for (let n = 0; n < q; n++) {
    const u = d.unit;
    d = { ...d, unit: [u[2], u[1], u[0]], axes: [d.axes[2], d.axes[1], d.axes[0]], spacing: [d.spacing[2], d.spacing[1], d.spacing[0]],
      boxes: d.boxes.map(b => ({ mat: b.mat, min: [r5(u[2] - b.max[2]), b.min[1], b.min[0]], max: [r5(u[2] - b.min[2]), b.max[1], b.max[0]] })) };
  }
  return d;
}
// how many units along each axis an object of box `b` holds
export function repeatCounts(def, b) {
  const n = [0, 1, 2].map(k => {
    if (!def.axes[k]) return 1;
    const L = b.max[k] - b.min[k], s = Math.max(0.05, def.spacing[k]);
    return Math.max(1, Math.floor((L - def.unit[k]) / s + 1e-6) + 1);
  });
  const per = Math.max(1, def.boxes.length);
  while (n[0] * n[1] * n[2] * per > MAX_REPEAT_BOXES) { const k = n.indexOf(Math.max(...n)); n[k] = Math.max(1, Math.floor(n[k] / 2)); }
  return n;
}
// the box that exactly holds n units from b's min corner (what a resize snaps to)
export function fitBox(def, b, n = repeatCounts(def, b)) {
  const min = b.min.slice(), max = [0, 1, 2].map(k => r5(min[k] + (n[k] - 1) * (def.axes[k] ? Math.max(0.05, def.spacing[k]) : 0) + def.unit[k]));
  return { min, max };
}
// the plain boxes an object is made of, in world space
export function expandRepeat(def, b, rot = 0) {
  const d = turnDef(def, rot), n = repeatCounts(d, b), out = [];
  const s = [0, 1, 2].map(k => Math.max(0.05, d.spacing[k]));
  for (let i = 0; i < n[0]; i++) for (let j = 0; j < n[1]; j++) for (let k = 0; k < n[2]; k++) {
    const o = [b.min[0] + i * s[0], b.min[1] + j * s[1], b.min[2] + k * s[2]];
    for (const u of d.boxes) out.push({ mat: u.mat, min: [0, 1, 2].map(a => r5(o[a] + u.min[a])), max: [0, 1, 2].map(a => r5(o[a] + u.max[a])) });
  }
  return out;
}
// every object's boxes, tagged with the object's index (they join map.generated: drawn, picked and tinted like a
// group's parts)
export function objectParts(objects) {
  const out = [];
  (objects || []).forEach((o, i) => { if (o && o.def) for (const b of expandRepeat(o.def, o, o.rot || 0)) { b.object = i; out.push(b); } });
  return out;
}
const boxKey = (b) => b.mat + "|" + b.min.map(r5).join(",") + "|" + b.max.map(r5).join(",");
// Opening a map: takes the boxes the sidecar's objects wrote into the .qmap back out of map.brushes (they are drawn
// from the objects). An object whose boxes aren't all there any more (the map was edited elsewhere) is dropped from
// the sidecar and its boxes stay plain boxes. -> the names of the objects dropped
export function stripObjects(map, sidecar) {
  const objs = Array.isArray(sidecar.objects) ? sidecar.objects : [];
  if (!objs.length) return [];
  const at = new Map();   // key -> indices of map.brushes with it (last first: a save appends the objects' boxes)
  map.brushes.forEach((b, i) => { const k = boxKey(b); const l = at.get(k); if (l) l.push(i); else at.set(k, [i]); });
  const gone = new Set(), dropped = [], kept = [];
  for (const o of objs) {
    const want = o && o.def ? expandRepeat(o.def, o, o.rot || 0) : null, take = [];
    let ok = !!want;
    if (want) for (const b of want) {
      const l = at.get(boxKey(b)); let i;
      while (l && l.length && gone.has(i = l[l.length - 1])) l.pop();
      if (!l || !l.length) { ok = false; break; }
      take.push(l.pop());
      gone.add(take[take.length - 1]);
    }
    if (ok) kept.push(o);
    else { for (const i of take) gone.delete(i); dropped.push((o && o.name) || "object"); }
  }
  if (gone.size) map.brushes = map.brushes.filter((_, i) => !gone.has(i));
  sidecar.objects = kept;
  return dropped;
}
// a definition from some boxes (the selection): its unit is their bounds, spacing the unit's size on the axes asked
export function defFromBoxes(name, boxes, axes = [true, false, false], spacing = null) {
  const mn = [0, 1, 2].map(k => Math.min(...boxes.map(b => b.min[k]))), mx = [0, 1, 2].map(k => Math.max(...boxes.map(b => b.max[k])));
  const unit = [0, 1, 2].map(k => r5(mx[k] - mn[k]));
  return { name, unit, axes: axes.slice(0, 3).map(Boolean), spacing: [0, 1, 2].map(k => r5(spacing && spacing[k] > 0 ? spacing[k] : Math.max(0.05, unit[k]))),
    boxes: boxes.map(b => ({ mat: b.mat, min: [0, 1, 2].map(k => r5(b.min[k] - mn[k])), max: [0, 1, 2].map(k => r5(b.max[k] - mn[k])) })) };
}
