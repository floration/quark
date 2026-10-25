// Picking: a uniform XZ grid (4 m cells, like Map::kCell) over every drawn box, walked along the ray (2D DDA) so a
// click tests the few boxes in the cells it crosses, not all 12 000 of Midtown. Markers (spawns, lights, zones...)
// are few and tested one by one.
const CELL = 4;

export function rayBox(o, d, mn, mx) {   // -> [tEnter, axis*2+side] or null
  let t0 = -Infinity, t1 = Infinity, ax = -1;
  for (let k = 0; k < 3; k++) {
    if (Math.abs(d[k]) < 1e-12) { if (o[k] < mn[k] || o[k] > mx[k]) return null; continue; }
    const inv = 1 / d[k];
    let a = (mn[k] - o[k]) * inv, b = (mx[k] - o[k]) * inv;
    let side = inv < 0 ? 1 : 0;
    if (a > b) { const t = a; a = b; b = t; }
    if (a > t0) { t0 = a; ax = k * 2 + side; }
    if (b < t1) t1 = b;
    if (t0 > t1) return null;
  }
  if (t1 < 0) return null;
  return [Math.max(t0, 0), ax, t0 < 0];   // (inside: t 0)
}
export function normalOf(ax) { const n = [0, 0, 0]; if (ax >= 0) n[ax >> 1] = (ax & 1) ? 1 : -1; return n; }

export class Picker {
  constructor() { this.boxes = []; this.cells = new Map(); this.big = []; this.at = []; }
  // items: [{min, max, ref}]
  build(items) {
    this.boxes = items; this.cells = new Map(); this.big = []; this.at = new Array(items.length);
    items.forEach((b, i) => this.insert(i));
  }
  insert(i) {
    const b = this.boxes[i];
    const x0 = Math.floor(b.min[0] / CELL), x1 = Math.floor(b.max[0] / CELL), z0 = Math.floor(b.min[2] / CELL), z1 = Math.floor(b.max[2] / CELL);
    if ((x1 - x0 + 1) * (z1 - z0 + 1) > 400) { this.big.push(i); this.at[i] = null; return; }   // (huge floors / far scenery: tested always)
    this.at[i] = [x0, x1, z0, z1];
    for (let x = x0; x <= x1; x++) for (let z = z0; z <= z1; z++) {
      const k = x * 100003 + z; let c = this.cells.get(k); if (!c) this.cells.set(k, c = []); c.push(i);
    }
  }
  // item i's box moved or resized (a drag, a nudge): only its cells change, not the whole grid
  move(i, min, max) {
    const a = this.at[i];
    if (a === null) this.big = this.big.filter(j => j !== i);
    else if (a) for (let x = a[0]; x <= a[1]; x++) for (let z = a[2]; z <= a[3]; z++) {
      const c = this.cells.get(x * 100003 + z); if (!c) continue;
      const n = c.indexOf(i); if (n >= 0) c.splice(n, 1);
    }
    this.boxes[i].min = min; this.boxes[i].max = max;
    this.insert(i);
  }
  // every hit along the ray up to maxT, nearest first: [{t, ax, ref, inside}]
  cast(o, d, maxT = 3000, filter, all = false) {
    const hits = [], seen = new Set();
    const test = (i) => {
      if (seen.has(i)) return; seen.add(i);
      const b = this.boxes[i]; if (filter && !filter(b.ref)) return;
      const r = rayBox(o, d, b.min, b.max);
      if (r && r[0] <= maxT) hits.push({ t: r[0], ax: r[1], inside: r[2], ref: b.ref, box: b });
    };
    for (const i of this.big) test(i);
    // 2D DDA over the XZ cells
    let cx = Math.floor(o[0] / CELL), cz = Math.floor(o[2] / CELL);
    const sx = d[0] > 0 ? 1 : -1, sz = d[2] > 0 ? 1 : -1;
    const dx = Math.abs(d[0]) > 1e-12 ? Math.abs(CELL / d[0]) : Infinity, dz = Math.abs(d[2]) > 1e-12 ? Math.abs(CELL / d[2]) : Infinity;
    let tx = Math.abs(d[0]) > 1e-12 ? ((sx > 0 ? (cx + 1) * CELL - o[0] : o[0] - cx * CELL) / Math.abs(d[0])) : Infinity;
    let tz = Math.abs(d[2]) > 1e-12 ? ((sz > 0 ? (cz + 1) * CELL - o[2] : o[2] - cz * CELL) / Math.abs(d[2])) : Infinity;
    let t = 0, best = Infinity, steps = 0;
    while (t <= maxT && steps++ < 4000) {
      const c = this.cells.get(cx * 100003 + cz);
      if (c) for (const i of c) test(i);
      for (const h of hits) if (h.t < best) best = h.t;
      if (!all && best < t) break;   // (nothing in a later cell can be nearer)
      if (tx < tz) { t = tx; tx += dx; cx += sx; } else { t = tz; tz += dz; cz += sz; }
      if (!isFinite(t)) break;
    }
    hits.sort((a, b) => a.t - b.t);
    return hits;
  }
}
