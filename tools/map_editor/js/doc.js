// The open map: its JSON (INTERFACE.md's shape), where it came from, its sidecar, the selection and undo / redo.
// An edit names the top-level keys it changes ("brushes", "spawns", ... or "sc.<key>" for a sidecar entry) and, for the
// object lists (brushes, groups, spawns...), which objects it changes in place (`touch`). Undo keeps, per list, copies
// of those objects and the objects taken out / put in (by reference, with their indices); other keys are copied
// whole (they are small). So a step costs what it touched: one nudged box of Midtown's 11 681 is one box, not all.
import { Emitter } from "./util.js";
import { generateParts } from "./fallback_parse.js";
import { objectParts } from "./repeat.js";

export const KINDS = {   // selection kind -> map key, and what it is (box: {min,max}; point: [x,y,z] or {pos})
  brush: { key: "brushes", shape: "box", label: "box" },
  group: { key: "groups", shape: "box", label: "group" },
  spawn: { key: "spawns", shape: "pos", label: "spawn" },
  light: { key: "lights", shape: "pos", label: "light" },
  site: { key: "sites", shape: "box", label: "bomb site" },
  hill: { key: "hill", shape: "box", label: "hill", single: true },
  point: { key: "points", shape: "box", label: "capture point" },
  checkpoint: { key: "checkpoints", shape: "box", label: "checkpoint" },
  path: { key: "path", shape: "arr", label: "path point" },
  ladder: { key: "ladders", shape: "box", label: "ladder" },
  stairs: { key: "stairs", shape: "stairs", label: "stairs record" },
  figure: { key: "sc.figures", shape: "pos", label: "player figure" },
  botstart: { key: "sc.botStarts", shape: "pos", label: "bot start" },
  object: { key: "sc.objects", shape: "box", label: "custom object" },   // (repeat.js: kept in the sidecar, saved as plain boxes)
};
// an objective's name as the game shows it (SITE A, POINT A) and quark_maptool's tests / metrics read it
export function objectiveName(map, k, i) {
  if (k === "site" || k === "point") return k + " " + String.fromCharCode(65 + i);
  if (k === "checkpoint") return "checkpoint " + (i + 1);
  if (k === "hill") return "hill";
  return KINDS[k].label + (KINDS[k].single ? "" : " #" + i);
}
// the lists whose undo is per object (every other key is copied whole)
const LISTS = new Set(Object.values(KINDS).filter(K => !K.single).map(K => K.key));

const touchesMap = (step) => step.keys.some(k => !k.startsWith("sc.") || k === "sc.objects");   // (custom objects are map boxes once saved)
const clone = (v) => v === undefined ? undefined : structuredClone(v);
const same = (a, b) => a === b || JSON.stringify(a) === JSON.stringify(b);   // (small JSON values)
// o takes src's contents, keeping its identity (later steps and the selection refer to the object itself)
function assignInPlace(o, src) {
  if (Array.isArray(o)) { o.length = src.length; for (let k = 0; k < src.length; k++) o[k] = src[k]; return; }
  for (const k of Object.keys(o)) if (!(k in src)) delete o[k];
  Object.assign(o, src);
}
// what turned list `a` into list `b` (same objects, by identity): null when nothing moved; else {drop, ins} that turns
// b back into a: drop [[index in b, obj]], then ins [[index in the result, obj]] (both ascending); or {full: a} when the
// objects kept changed their order
function listDiff(a, b) {
  if (a.length === b.length) { let k = 0; while (k < a.length && a[k] === b[k]) k++; if (k === a.length) return null; }
  if (b.length > a.length) {   // appended only (add, duplicate): the common case
    let k = 0; while (k < a.length && a[k] === b[k]) k++;
    if (k === a.length) { const drop = []; for (let j = a.length; j < b.length; j++) drop.push([j, b[j]]); return { drop, ins: [] }; }
  }
  const inA = new Set(a), inB = new Set(b), drop = [], ins = [];
  b.forEach((o, j) => { if (!inA.has(o)) drop.push([j, o]); });
  a.forEach((o, i) => { if (!inB.has(o)) ins.push([i, o]); });
  for (let i = 0, j = 0; ; i++, j++) {   // the kept objects must be in the same order in both
    while (i < a.length && !inB.has(a[i])) i++;
    while (j < b.length && !inA.has(b[j])) j++;
    if (i >= a.length || j >= b.length) { if (i < a.length || j < b.length) return { full: a }; break; }
    if (a[i] !== b[j]) return { full: a };
  }
  return { drop, ins };
}
function applyDiff(b, op) {
  if (op.full) return op.full;
  const dropped = op.drop.length ? new Set(op.drop.map(x => x[0])) : null;
  const kept = dropped ? b.filter((_, j) => !dropped.has(j)) : b.slice();
  if (!op.ins.length) return kept;
  const out = new Array(kept.length + op.ins.length);
  for (let t = 0, a = 0, k = 0; t < out.length; t++) out[t] = a < op.ins.length && op.ins[a][0] === t ? op.ins[a++][1] : kept[k++];
  return out;
}

export class Doc extends Emitter {
  constructor() {
    super();
    this.map = null; this.name = ""; this.src = "new"; this.sidecar = {};
    this.sel = []; this.undoStack = []; this.redoStack = []; this.savedAt = 0; this.seq = 0;
    this.maxUndo = 200;
  }
  get sel() { return this._sel; }
  set sel(v) { this._sel = v; this.selIdx = null; }
  get readOnly() { return this.src === "default"; }
  get dirty() { return this.seq !== this.savedAt; }

  // fileText: the file's own text (a map saved unedited is written back as that text, byte for byte)
  open(map, name, src, sidecar, fileText = null) {
    this.map = map; this.name = name; this.src = src; this.sidecar = sidecar || {}; this.fileText = fileText;
    for (const k of ["figures", "botStarts", "objects"]) if (!Array.isArray(this.sidecar[k])) this.sidecar[k] = [];
    this.regen();
    this.sel = []; this.undoStack = []; this.redoStack = []; this.seq = 0; this.savedAt = 0; this.lostEdits = false;
    this.emit("open"); this.emit("map", null); this.emit("selection"); this.emit("dirty");
  }
  markSaved(name, src) { if (name) this.name = name; if (src) this.src = src; this.savedAt = this.seq; this.emit("dirty"); }

  // value of a key ("brushes" or "sc.figures")
  get(key) { return key.startsWith("sc.") ? this.sidecar[key.slice(3)] : this.map[key]; }
  put(key, v) { if (key.startsWith("sc.")) this.sidecar[key.slice(3)] = v; else this.map[key] = v; }

  // One undo step: fn(map, sidecar) changes the named keys. touch: the objects ({k, i} refs) fn changes in place; the
  // lists among keys may also get objects added / taken out. touch left out: every key is copied whole (slow on big
  // lists: pass it). An edit that changed nothing makes no step.
  edit(label, keys, fn, { keepSel = true, touch } = {}) {
    const tx = this.begin(label, keys, touch);
    fn(this.map, this.sidecar);
    tx.commit(() => { if (!keepSel) this.sel = []; });
  }
  // A drag: begin() notes the keys, the caller changes them live (calling live()), commit() makes it one step.
  begin(label, keys, touch) {
    const snap = this.snapshot(keys, touch), sel = this.sel.slice();
    return {
      live: () => this.changed(keys, true, snap.change),
      commit: (then) => {
        const step = this.finish(snap);
        if (step) { step.label = label; step.sel = sel; this.push(step); }
        if (then) then();
        this.changed(keys, false, step ? step.change : snap.change);
        return !!step;
      },
      cancel: () => { this.revert(snap); this.changed(keys, false, snap.change); },
    };
  }
  snapshot(keys, touch) {
    const byKey = {};
    if (touch) for (const r of touch) { const K = KINDS[r.k]; if (K && LISTS.has(K.key)) (byKey[K.key] = byKey[K.key] || []).push(r.i); }
    const parts = {}, touched = {};
    for (const k of keys) {
      const v = this.get(k);
      if (touch && LISTS.has(k) && Array.isArray(v)) {
        const idx = [...new Set(byKey[k] || [])].filter(i => i >= 0 && i < v.length);
        parts[k] = { refs: v.slice(), olds: new Map(idx.map(i => [v[i], clone(v[i])])) };
        touched[k] = idx;
      } else parts[k] = { before: clone(v) };
    }
    return { parts, change: { touched, structural: new Set(keys.filter(k => !touched[k])) } };
  }
  // the step for what changed since the snapshot (null: nothing did)
  finish(snap) {
    const parts = {}, structural = new Set();
    for (const [k, p] of Object.entries(snap.parts)) {
      const now = this.get(k);
      if ("before" in p) { if (!same(now, p.before)) { parts[k] = { before: p.before }; structural.add(k); } continue; }
      const mut = [];
      for (const [o, old] of p.olds) if (!same(o, old)) mut.push([o, old]);
      const st = Array.isArray(now) ? listDiff(p.refs, now) : { full: p.refs };
      if (st) structural.add(k);
      if (mut.length || st) parts[k] = { mut, st };
    }
    const keys = Object.keys(parts);
    if (!keys.length) return null;
    return { keys, parts, change: { touched: snap.change.touched, structural } };
  }
  revert(snap) {
    for (const [k, p] of Object.entries(snap.parts)) {
      if ("before" in p) { this.put(k, p.before); continue; }
      for (const [o, old] of p.olds) assignInPlace(o, old);
      if (listDiff(p.refs, this.get(k))) this.put(k, p.refs);
    }
  }
  // adds key's old value (copied whole) to a step already made: the decoration hook's sidecar entry riding on its map step
  rideOn(step, key, before) {
    if (step.parts[key]) return;
    step.parts[key] = { before }; step.keys = [...step.keys, key];
  }
  // the map itself (not only its sidecar) differs from the file it was opened from
  get mapEdited() { return this.lostEdits || this.undoStack.some(touchesMap); }
  push(step) {
    this.undoStack.push(step);
    if (this.undoStack.length > this.maxUndo && touchesMap(this.undoStack.shift())) this.lostEdits = true;
    this.redoStack = []; this.seq++; step.seq = this.seq; this.emit("dirty");
  }
  // undo / redo: applies a step and leaves its inverse on the other stack
  swap(from, to) {
    const s = from.pop(); if (!s) return null;
    const inv = {}, touched = {}, structural = new Set();
    for (const [k, p] of Object.entries(s.parts)) {
      if ("before" in p) { inv[k] = { before: this.get(k) }; this.put(k, p.before); structural.add(k); continue; }
      const q = { mut: [], st: null };
      if (p.st) {
        const cur = this.get(k);
        this.put(k, applyDiff(cur, p.st));
        q.st = p.st.full ? { full: cur } : { drop: p.st.ins, ins: p.st.drop };
        structural.add(k);
      }
      for (const [o, old] of p.mut) { q.mut.push([o, clone(o)]); assignInPlace(o, old); }
      if (!p.st && p.mut.length) {   // (which instances the view rewrites)
        const arr = this.get(k);
        if (p.mut.length <= 8) touched[k] = p.mut.map(([o]) => arr.indexOf(o)).filter(i => i >= 0);
        else { const at = new Map(arr.map((o, i) => [o, i])); touched[k] = p.mut.map(([o]) => at.get(o)).filter(i => i !== undefined); }
      }
      inv[k] = q;
    }
    const sel = this.sel;
    this.sel = (s.sel || []).filter(x => this.exists(x));
    to.push({ label: s.label, keys: s.keys, parts: inv, sel, seq: s.seq });
    this.changed(s.keys, false, { touched, structural });
    this.emit("selection");
    return s;
  }
  undo() { const s = this.swap(this.undoStack, this.redoStack); if (s) { this.seq = this.undoStack.length ? this.undoStack[this.undoStack.length - 1].seq : 0; this.emit("dirty"); } return s; }
  redo() { const s = this.swap(this.redoStack, this.undoStack); if (s) { this.seq = s.seq; this.emit("dirty"); } return s; }

  // change: {touched: {key: [indices changed in place]}, structural: Set(keys added to / taken from / replaced)}
  changed(keys, live = false, change = null) {
    if (this.sel.length && !live && !this.sel.every(x => this.exists(x))) this.sel = this.sel.filter(x => this.exists(x));
    if (keys.some(k => k === "groups" || k === "ladders" || k === "sc.objects" || k === "*")) this.regen();
    this.emit("map", keys, live, change);
  }
  // the boxes made from other things: groups' parts and ladders' rungs (as the game makes them), custom objects' units
  regen() { this.map.generated = generateParts(this.map).concat(objectParts(this.sidecar.objects)); }

  // Replace the whole map (the decoration hook's setMap): one step. Objects that are the same as before keep their
  // identity, so the step holds only the boxes the decoration added and took out, not two copies of the map.
  replaceMap(newMap, label) {
    const keys = Object.keys(this.map).filter(k => !["generated", "materials", "player", "warnings", "fallback"].includes(k) && k in newMap);
    const changedKeys = keys.filter(k => !same(this.map[k], newMap[k]));
    if (!changedKeys.length) return;
    this.edit(label || "decoration", changedKeys, (m) => {
      for (const k of changedKeys) {
        if (!LISTS.has(k) || !Array.isArray(m[k]) || !Array.isArray(newMap[k])) { m[k] = clone(newMap[k]); continue; }
        const pool = new Map();   // JSON of an object -> the current objects with it
        for (const o of m[k]) { const j = JSON.stringify(o); const l = pool.get(j); if (l) l.push(o); else pool.set(j, [o]); }
        m[k] = newMap[k].map(o => { const l = pool.get(JSON.stringify(o)); return l && l.length ? l.shift() : clone(o); });
      }
    }, { touch: [] });
    this.sel = this.sel.filter(x => this.exists(x)); this.emit("selection");
  }

  // ---- selection (selIdx: kind -> Set of indices, made when asked for, so isSel is O(1) with 11 000 selected)
  exists(s) {
    const K = KINDS[s.k]; if (!K) return false;
    const v = this.get(K.key);
    return K.single ? !!v : Array.isArray(v) && s.i >= 0 && s.i < v.length;
  }
  select(list, add = false) {
    const next = add ? this.sel.slice() : [];
    const at = {};   // kind -> Map(index -> position in next)
    const pos = (k) => at[k] || (at[k] = new Map());
    next.forEach((x, n) => pos(x.k).set(x.i, n));
    let drop = -1;
    for (const s of list) {
      const P = pos(s.k), n = P.get(s.i);
      if (n !== undefined) { if (add && list.length === 1) drop = n; continue; }
      P.set(s.i, next.length); next.push({ k: s.k, i: s.i, part: s.part });
    }
    this.sel = next.filter((x, n) => n !== drop && this.exists(x));
    this.emit("selection");
  }
  get selIndex() {
    if (!this.selIdx) { this.selIdx = {}; for (const x of this.sel) (this.selIdx[x.k] = this.selIdx[x.k] || new Set()).add(x.i); }
    return this.selIdx;
  }
  isSel(k, i) { const S = this.selIndex[k]; return !!S && S.has(i); }
  selOf(k) { const S = this.selIndex[k]; return S ? [...S] : []; }

  // ---- object geometry (for the gizmo, framing, moving)
  obj(s) { const K = KINDS[s.k]; const v = this.get(K.key); return K.single ? v : v[s.i]; }
  bounds(s) {
    const K = KINDS[s.k], o = this.obj(s);
    if (!o) return null;
    if (K.shape === "box") return { min: o.min.slice(), max: o.max.slice() };
    if (K.shape === "stairs") return { min: [Math.min(o.x0, o.x1), o.base, Math.min(o.z0, o.z1)], max: [Math.max(o.x0, o.x1), o.top, Math.max(o.z0, o.z1)] };
    const p = K.shape === "arr" ? o : o.pos;
    if (s.k === "spawn" || s.k === "figure" || s.k === "botstart") return { min: [p[0] - 0.4, p[1], p[2] - 0.4], max: [p[0] + 0.4, p[1] + 1.8, p[2] + 0.4] };
    return { min: [p[0] - 0.25, p[1] - 0.25, p[2] - 0.25], max: [p[0] + 0.25, p[1] + 0.25, p[2] + 0.25] };
  }
  selBounds(sel = this.sel) {
    let mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (const s of sel) { const b = this.bounds(s); if (!b) continue; for (let k = 0; k < 3; k++) { mn[k] = Math.min(mn[k], b.min[k]); mx[k] = Math.max(mx[k], b.max[k]); } }
    return isFinite(mn[0]) ? { min: mn, max: mx } : null;
  }
  // moves object s (from its state `from`, a copy taken at drag start) by d
  moveFrom(s, from, d) {
    const K = KINDS[s.k], o = this.obj(s);
    if (K.shape === "box") for (let k = 0; k < 3; k++) { o.min[k] = from.min[k] + d[k]; o.max[k] = from.max[k] + d[k]; }
    else if (K.shape === "stairs") { o.x0 = from.x0 + d[0]; o.x1 = from.x1 + d[0]; o.z0 = from.z0 + d[2]; o.z1 = from.z1 + d[2]; o.base = from.base + d[1]; o.top = from.top + d[1]; }
    else if (K.shape === "arr") for (let k = 0; k < 3; k++) o[k] = from[k] + d[k];
    else for (let k = 0; k < 3; k++) o.pos[k] = from.pos[k] + d[k];
  }
  keysOf(sel = this.sel) { return [...new Set(sel.map(s => KINDS[s.k].key))]; }

  deleteSel() {
    if (!this.sel.length) return;
    const sel = this.sel.slice(), keys = this.keysOf(sel);
    if (keys.includes("path")) keys.push("cartChecks");   // (fewer than 2 path points: the checkpoints go too, and come back on undo)
    this.edit("delete " + (sel.length > 1 ? sel.length + " objects" : KINDS[sel[0].k].label), keys, () => {
      const byKey = {};
      for (const s of sel) (byKey[KINDS[s.k].key] = byKey[KINDS[s.k].key] || []).push(s.i);
      for (const [key, list] of Object.entries(byKey)) {
        if (KINDS[sel.find(s => KINDS[s.k].key === key).k].single) { this.put(key, null); continue; }
        const gone = new Set(list);
        this.put(key, this.get(key).filter((_, i) => !gone.has(i)));   // (one pass: deleting half of Midtown is not 5 000 splices)
        if (key === "path" && this.map.path.length < 2) this.map.cartChecks = [];
      }
    }, { touch: [] });
    this.sel = []; this.emit("selection");
  }
  duplicateSel(offset) {
    if (!this.sel.length) return;
    const sel = this.sel.filter(s => !KINDS[s.k].single);
    const added = [];
    this.edit("duplicate", this.keysOf(sel), () => {
      for (const s of sel) {
        const K = KINDS[s.k], arr = this.get(K.key), c = clone(arr[s.i]);
        arr.push(c);
        const n = { k: s.k, i: arr.length - 1 };
        this.moveFrom(n, clone(c), offset);
        added.push(n);
      }
    }, { touch: [] });
    this.sel = added; this.emit("selection");
  }
}
