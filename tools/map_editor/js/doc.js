// The open map: its JSON (INTERFACE.md's shape), where it came from, its sidecar, the selection and undo / redo.
// An edit names the top-level keys it changes ("brushes", "spawns", ... or "sc.<key>" for a sidecar entry); undo
// keeps a copy of those keys from before, so one step costs what it touched, never the whole map.
import { Emitter } from "./util.js";
import { generateParts } from "./fallback_parse.js";

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
};

const clone = (v) => v === undefined ? undefined : structuredClone(v);

export class Doc extends Emitter {
  constructor() {
    super();
    this.map = null; this.name = ""; this.src = "new"; this.sidecar = {};
    this.sel = []; this.undoStack = []; this.redoStack = []; this.savedAt = 0; this.seq = 0;
    this.maxUndo = 200;
  }
  get readOnly() { return this.src === "default"; }
  get dirty() { return this.seq !== this.savedAt; }

  open(map, name, src, sidecar) {
    this.map = map; this.name = name; this.src = src; this.sidecar = sidecar || {};
    for (const k of ["figures", "botStarts"]) if (!Array.isArray(this.sidecar[k])) this.sidecar[k] = [];
    this.map.generated = generateParts(this.map);
    this.sel = []; this.undoStack = []; this.redoStack = []; this.seq = 0; this.savedAt = 0;
    this.emit("open"); this.emit("map", null); this.emit("selection"); this.emit("dirty");
  }
  markSaved(name, src) { if (name) this.name = name; if (src) this.src = src; this.savedAt = this.seq; this.emit("dirty"); }

  // value of a key ("brushes" or "sc.figures")
  get(key) { return key.startsWith("sc.") ? this.sidecar[key.slice(3)] : this.map[key]; }
  put(key, v) { if (key.startsWith("sc.")) this.sidecar[key.slice(3)] = v; else this.map[key] = v; }

  // One undo step: fn(map, sidecar) changes the named keys in place.
  edit(label, keys, fn, { keepSel = true } = {}) {
    const before = {};
    for (const k of keys) before[k] = clone(this.get(k));
    const prevSel = this.sel.slice();
    fn(this.map, this.sidecar);
    this.push({ label, keys, before, sel: prevSel });
    if (!keepSel) this.sel = [];
    this.changed(keys);
  }
  // A drag: begin() copies the keys, the caller changes them live (calling live()), commit() makes it one step.
  begin(label, keys) {
    const before = {};
    for (const k of keys) before[k] = clone(this.get(k));
    const sel = this.sel.slice();
    return {
      live: () => this.changed(keys, true),
      commit: () => { this.push({ label, keys, before, sel }); this.changed(keys); },
      cancel: () => { for (const k of keys) this.put(k, clone(before[k])); this.changed(keys); },
    };
  }
  push(step) {
    this.undoStack.push(step); if (this.undoStack.length > this.maxUndo) this.undoStack.shift();
    this.redoStack = []; this.seq++; step.seq = this.seq; this.emit("dirty");
  }
  swap(from, to) {
    const s = from.pop(); if (!s) return null;
    const cur = {};
    for (const k of s.keys) { cur[k] = this.get(k); this.put(k, s.before[k]); }
    const sel = this.sel;
    this.sel = (s.sel || []).filter(x => this.exists(x));
    to.push({ label: s.label, keys: s.keys, before: cur, sel, seq: s.seq });
    this.changed(s.keys);
    this.emit("selection");
    return s;
  }
  undo() { const s = this.swap(this.undoStack, this.redoStack); if (s) { this.seq = this.undoStack.length ? this.undoStack[this.undoStack.length - 1].seq : 0; this.emit("dirty"); } return s; }
  redo() { const s = this.swap(this.redoStack, this.undoStack); if (s) { this.seq = s.seq; this.emit("dirty"); } return s; }

  changed(keys, live = false) {
    if (this.sel.length && !live) this.sel = this.sel.filter(x => this.exists(x));
    if (!live && keys.some(k => k === "groups" || k === "ladders" || k === "*")) this.map.generated = generateParts(this.map);
    else if (live && keys.some(k => k === "groups" || k === "ladders")) this.map.generated = generateParts(this.map);
    this.emit("map", keys, live);
  }

  // Replace the whole map (the decoration hook's setMap): one step.
  replaceMap(newMap, label) {
    const keys = Object.keys(this.map).filter(k => !["generated", "materials", "player", "warnings", "fallback"].includes(k));
    this.edit(label || "decoration", keys, (m) => { for (const k of keys) if (k in newMap) m[k] = clone(newMap[k]); });
    this.sel = this.sel.filter(x => this.exists(x)); this.emit("selection");
  }

  // ---- selection
  exists(s) {
    const K = KINDS[s.k]; if (!K) return false;
    const v = this.get(K.key);
    return K.single ? !!v : Array.isArray(v) && s.i >= 0 && s.i < v.length;
  }
  select(list, add = false) {
    const next = add ? this.sel.slice() : [];
    for (const s of list) {
      const at = next.findIndex(x => x.k === s.k && x.i === s.i);
      if (at >= 0) { if (add && list.length === 1) next.splice(at, 1); } else next.push({ k: s.k, i: s.i, part: s.part });
    }
    this.sel = next.filter(x => this.exists(x));
    this.emit("selection");
  }
  isSel(k, i) { return this.sel.some(x => x.k === k && x.i === i); }
  selOf(k) { return this.sel.filter(x => x.k === k).map(x => x.i); }

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
    const sel = this.sel.slice();
    this.edit("delete " + (sel.length > 1 ? sel.length + " objects" : KINDS[sel[0].k].label), this.keysOf(sel), () => {
      const byKey = {};
      for (const s of sel) (byKey[KINDS[s.k].key] = byKey[KINDS[s.k].key] || []).push(s);
      for (const [key, list] of Object.entries(byKey)) {
        const K = KINDS[list[0].k];
        if (K.single) { this.put(key, null); continue; }
        const arr = this.get(key);
        for (const i of list.map(s => s.i).sort((a, b) => b - a)) arr.splice(i, 1);
        if (key === "path") this.map.cartChecks = this.map.cartChecks.filter(() => this.map.path.length >= 2);
      }
    });
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
    });
    this.sel = added; this.emit("selection");
  }
}
