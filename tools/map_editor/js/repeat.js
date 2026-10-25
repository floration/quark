// Custom objects that repeat (a railing, a fence, a pillar row, floor tiles): a definition is a unit (a few boxes in a
// box of its own) and, per axis, whether it repeats and the spacing. An object placed in a map is that definition and
// a box: making the box longer along a repeating axis adds units (spacing apart), it never stretches them; the other
// axes keep the unit's size. A definition that repeats along one axis may have a start piece (before the first unit)
// and an end piece (after the last: a railing's closing post, a pillar row's end cap), each once. Objects live in the
// sidecar ("objects") so they stay editable; a save writes their boxes into the .qmap as plain boxes the game loads
// (App.mapForSave), and opening the map takes those boxes out again.
//
// def: {name, unit: [x, y, z] (the unit's size), axes: [bool, bool, bool] (repeats along x / y / z), spacing: [x, y, z]
//       (unit start to next unit start, >= 0.05), boxes: [{mat, min, max}] (relative to the unit's min corner),
//       start / end (optional, one repeating axis only): {len, boxes} (the piece's length along that axis; its boxes
//       relative to the piece's own min corner, the unit's frame across the other axes)}
// object (sidecar.objects[i]): {name, def, rot (quarter turns about y, 0-3), min, max, lib (optional: the library item
//       it was placed from, for "update from the library")}
export const MAX_REPEAT_BOXES = 20000;   // (one object; the map's own limit is 60 000 boxes)

const r5 = (v) => Math.round(v * 1e5) / 1e5;
const turnBoxes = (boxes, u) => boxes.map(b => ({ mat: b.mat, min: [r5(u[2] - b.max[2]), b.min[1], b.min[0]], max: [r5(u[2] - b.min[2]), b.max[1], b.max[0]] }));
// the axis start / end pieces go along: the one repeating axis (-1: none or several repeat, so no pieces)
export function capAxis(def) { const a = [0, 1, 2].filter(k => def.axes[k]); return a.length === 1 ? a[0] : -1; }
const pieceLen = (def, k) => k >= 0 && k === capAxis(def) ? ((def.start && def.start.len) || 0) + ((def.end && def.end.len) || 0) : 0;
// a piece's frame: the unit's size with its length along the cap axis
const pieceSize = (def, p, a) => def.unit.map((v, k) => k === a ? p.len : v);

// the definition turned q quarter turns about y (inside its unit: x' = uz - z, z' = x). +z turns to -x, so a piece
// along z swaps ends (the start piece is at the high-x end after the turn)
export function turnDef(def, q) {
  q = ((q % 4) + 4) % 4;
  let d = def;
  for (let n = 0; n < q; n++) {
    const u = d.unit, a = capAxis(d);
    const turnPiece = (p) => p && { len: p.len, boxes: turnBoxes(p.boxes, pieceSize(d, p, a)) };
    const st = turnPiece(d.start), en = turnPiece(d.end);
    d = { ...d, unit: [u[2], u[1], u[0]], axes: [d.axes[2], d.axes[1], d.axes[0]], spacing: [d.spacing[2], d.spacing[1], d.spacing[0]], boxes: turnBoxes(d.boxes, u) };
    if (st || en) { if (a === 2) { d.start = en; d.end = st; } else { d.start = st; d.end = en; } if (!d.start) delete d.start; if (!d.end) delete d.end; }
  }
  return d;
}
// the object's length along axis k holding n units (with its start / end pieces)
export function repeatLen(def, k, n) { return r5((def.axes[k] ? (n - 1) * Math.max(0.05, def.spacing[k]) : 0) + def.unit[k] + pieceLen(def, k)); }
// how many units along each axis an object of box `b` holds
export function repeatCounts(def, b) {
  const n = [0, 1, 2].map(k => {
    if (!def.axes[k]) return 1;
    const L = b.max[k] - b.min[k] - pieceLen(def, k), s = Math.max(0.05, def.spacing[k]);
    return Math.max(1, Math.floor((L - def.unit[k]) / s + 1e-6) + 1);
  });
  const per = Math.max(1, def.boxes.length);
  while (n[0] * n[1] * n[2] * per > MAX_REPEAT_BOXES) { const k = n.indexOf(Math.max(...n)); n[k] = Math.max(1, Math.floor(n[k] / 2)); }
  return n;
}
// the box that exactly holds n units from b's min corner (what a resize snaps to)
export function fitBox(def, b, n = repeatCounts(def, b)) {
  const min = b.min.slice(), max = [0, 1, 2].map(k => r5(min[k] + repeatLen(def, k, n[k])));
  return { min, max };
}
// the plain boxes an object is made of, in world space
export function expandRepeat(def, b, rot = 0) {
  const d = turnDef(def, rot), n = repeatCounts(d, b), out = [], a = capAxis(d);
  const s = [0, 1, 2].map(k => Math.max(0.05, d.spacing[k]));
  const at = (o, boxes) => { for (const u of boxes) out.push({ mat: u.mat, min: [0, 1, 2].map(k => r5(o[k] + u.min[k])), max: [0, 1, 2].map(k => r5(o[k] + u.max[k])) }); };
  const st = a >= 0 && d.start ? d.start : null, en = a >= 0 && d.end ? d.end : null;
  const first = b.min.slice(); if (st) { at(b.min, st.boxes); first[a] += st.len; }
  for (let i = 0; i < n[0]; i++) for (let j = 0; j < n[1]; j++) for (let k = 0; k < n[2]; k++) at([first[0] + i * s[0], first[1] + j * s[1], first[2] + k * s[2]], d.boxes);
  if (en) { const o = first.slice(); o[a] += (n[a] - 1) * s[a] + d.unit[a]; at(o, en.boxes); }
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

// ---- editing an object's unit or start / end piece in place (Library.editPart): the part goes out as plain boxes,
// you change them, and they go back in as that part
const unitStart = (d, o) => { const a = capAxis(d), p = o.min.slice(); if (a >= 0 && d.start) p[a] = r5(p[a] + d.start.len); return p; };
// the world boxes of an object's part ("unit": its first unit; "start" / "end": that piece, [] when it has none)
export function partBoxes(o, part) {
  const d = turnDef(o.def, o.rot || 0), a = capAxis(d), first = unitStart(d, o), n = repeatCounts(d, o);
  const place = (org, boxes) => boxes.map(u => ({ mat: u.mat, min: [0, 1, 2].map(k => r5(org[k] + u.min[k])), max: [0, 1, 2].map(k => r5(org[k] + u.max[k])) }));
  if (part === "unit") return place(first, d.boxes);
  const p = a >= 0 && d[part]; if (!p) return [];
  if (part === "start") return place(o.min, p.boxes);
  const org = first.slice(); org[a] = r5(org[a] + (n[a] - 1) * Math.max(0.05, d.spacing[a]) + d.unit[a]);
  return place(org, p.boxes);
}
// the object without its start / end piece (turned into its definition: rot 0), the same number of units
export function withoutPiece(o, part) {
  const d = turnDef(o.def, o.rot || 0), n = repeatCounts(d, o), first = unitStart(d, o);
  const def = { ...d }; delete def[part];
  const min = part === "start" ? first : o.min.slice();
  return { ...o, def, rot: 0, ...fitBox(def, { min }, n) };
}
// the object with its part made from `boxes` (world space; none: a piece is removed, a unit can't be empty -> null)
export function withPart(o, part, boxes) {
  const d = turnDef(o.def, o.rot || 0), n = repeatCounts(d, o), a = capAxis(d), first = unitStart(d, o);
  if (part === "unit") {
    if (!boxes.length) return null;
    const u = defFromBoxes(d.name, boxes, d.axes, null);
    // spacing: kept where it was set apart from the unit's size, else the new unit's size (units touch)
    const def = { ...d, unit: u.unit, boxes: u.boxes, spacing: d.spacing.map((s, k) => Math.abs(s - d.unit[k]) < 1e-4 ? u.spacing[k] : s) };
    const mn = [0, 1, 2].map(k => Math.min(...boxes.map(b => b.min[k])));
    if (a >= 0 && d.start) mn[a] = r5(mn[a] - d.start.len);
    return { ...o, def, rot: 0, ...fitBox(def, { min: mn }, n) };
  }
  if (a < 0) return null;
  const def = { ...d }; delete def[part];
  if (!boxes.length) return { ...o, def, rot: 0, ...fitBox(def, { min: part === "start" ? first : o.min.slice() }, n) };
  const lo = Math.min(...boxes.map(b => b.min[a])), hi = Math.max(...boxes.map(b => b.max[a]));
  let org, len;
  if (part === "start") { len = r5(Math.max(0, first[a] - lo)); org = o.min.slice(); org[a] = r5(first[a] - len); }
  else {
    const end = r5(first[a] + (n[a] - 1) * Math.max(0.05, d.spacing[a]) + d.unit[a]);
    len = r5(Math.max(0, hi - end)); org = o.min.slice(); org[a] = end;
  }
  def[part] = { len, boxes: boxes.map(b => ({ mat: b.mat, min: [0, 1, 2].map(k => r5(b.min[k] - org[k])), max: [0, 1, 2].map(k => r5(b.max[k] - org[k])) })) };
  const min = o.min.slice(); if (part === "start") min[a] = org[a];
  return { ...o, def, rot: 0, ...fitBox(def, { min }, n) };
}
