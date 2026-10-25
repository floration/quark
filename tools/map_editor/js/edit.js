// Editing: the mouse and keys in the 3D view. Cameras (fly: right mouse looks, WASD; orbit; top-down), picking and
// selection (click, Shift adds, drag a rectangle, click again cycles through what is under the cursor), the gizmo
// (arrows move, face handles resize, dragging a selected object slides it on the floor plane), the add tools, and
// the in-game editor's keys (arrows move by the grid, Shift / Ctrl + arrow grow / shrink, [ ] the grid, M material).
import * as THREE from "../vendor/three.module.min.js";
import { Picker, rayBox, normalOf } from "./pick.js";
import { KINDS } from "./doc.js";
import { snap, tidy } from "./util.js";
import { repeatCounts, repeatLen, turnDef } from "./repeat.js";
import { flipObj, twinObj } from "./xform.js";

export const GRIDS = [0.125, 0.25, 0.5, 1, 2, 4];
const AX = [new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, 0, 1)];
const AX_COL = [0xff5a5a, 0x5aff6a, 0x5a8cff];

export class Editor {
  constructor(app) {
    this.app = app; this.doc = app.doc; this.view = app.view;
    this.tool = "select"; this.gridIdx = 3; this.mat = 0; this.team = "a";
    this.picker = new Picker(); this.pickDirty = true;
    this.gizmo = new THREE.Group(); this.view.overlay.add(this.gizmo);
    this.drag = null; this.routeA = null; this.cycle = null;
    const c = this.view.canvas;
    c.tabIndex = 0;
    c.addEventListener("contextmenu", e => e.preventDefault());
    c.addEventListener("mousedown", e => this.down(e));
    window.addEventListener("mousemove", e => this.move(e));
    window.addEventListener("mouseup", e => this.up(e));
    c.addEventListener("wheel", e => this.wheel(e), { passive: false });
    c.addEventListener("dblclick", e => this.dblclick(e));
    window.addEventListener("keydown", e => this.key(e, true));
    window.addEventListener("keyup", e => this.key(e, false));
    window.addEventListener("blur", () => this.view.keys.clear());
    this.view.onBeforeRender = () => this.updateGizmo();
  }
  get grid() { return GRIDS[this.gridIdx]; }
  objDef(o) { return o && o.def ? turnDef(o.def, o.rot || 0) : null; }   // a custom object's definition as it is turned
  setTool(t) { this.tool = t; this.routeA = null; this.app.emit("tool", t); this.view.redraw(); }
  // where a paste goes: the surface under the mouse when it is over the view, else under the view's centre
  pastePoint() {
    const r = this.canvas().getBoundingClientRect();
    const at = this.lastMouse && this.canvas().matches(":hover") ? this.lastMouse : { clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 };
    const s = this.surface(at);
    return s ? s.p : this.view.cam.target.toArray();
  }
  pickPoint(cb) { this.pickCb = cb; this.setTool("pickpoint"); }   // the next click in the view gives a point

  // ---- picking
  rebuildPick() {
    const d = this.doc, m = d.map, items = [];
    const sh = this.view.show;
    this.brushItem = new Array(m.brushes.length);
    m.brushes.forEach((b, i) => { if ((b.mat === 9 && !sh.clip) || (b.mat === 5 && !sh.far)) return; this.brushItem[i] = items.length; items.push({ min: b.min, max: b.max, ref: { k: "brush", i } }); });
    m.generated.forEach((b, i) => {
      if (!sh.generated && b.object === undefined) return;   // (custom objects are the map's own boxes: always pickable)
      if (b.group !== undefined) items.push({ min: b.min, max: b.max, ref: { k: "group", i: b.group, gen: i } });
      else if (b.ladder !== undefined) items.push({ min: b.min, max: b.max, ref: { k: "ladder", i: b.ladder } });
      else if (b.object !== undefined) items.push({ min: b.min, max: b.max, ref: { k: "object", i: b.object } });
    });
    this.picker.build(items); this.pickDirty = false;
  }
  // brushes changed in place: move their picking entries (a hidden one now shown, or the other way: rebuild)
  pickMoved(indices) {
    if (this.pickDirty) return;
    const m = this.doc.map, sh = this.view.show;
    for (const i of indices) {
      const b = m.brushes[i], item = this.brushItem[i], vis = b && !((b.mat === 9 && !sh.clip) || (b.mat === 5 && !sh.far));
      if (!b || (item === undefined) === vis) { this.pickDirty = true; return; }
      if (item !== undefined) this.picker.move(item, b.min, b.max);
    }
  }
  markers() {   // [{min, max, ref}] of the small things (tested one by one)
    const d = this.doc, m = d.map, out = [], sh = this.view.show;
    const hull = (p) => ({ min: [p[0] - 0.4, p[1], p[2] - 0.4], max: [p[0] + 0.4, p[1] + 1.8, p[2] + 0.4] });
    const ball = (p, r = 0.35) => ({ min: [p[0] - r, p[1] - r, p[2] - r], max: [p[0] + r, p[1] + r, p[2] + r] });
    if (sh.spawns) m.spawns.forEach((s, i) => out.push({ ...hull(s.pos), ref: { k: "spawn", i } }));
    if (sh.lights) m.lights.forEach((l, i) => out.push({ ...ball(l.pos), ref: { k: "light", i } }));
    if (sh.path) m.path.forEach((p, i) => out.push({ ...ball([p[0], p[1] + 0.15, p[2]], 0.4), ref: { k: "path", i } }));
    (d.sidecar.figures || []).forEach((f, i) => out.push({ ...hull(f.pos), ref: { k: "figure", i } }));
    (d.sidecar.botStarts || []).forEach((f, i) => out.push({ ...hull(f.pos), ref: { k: "botstart", i } }));
    if (sh.ladders) m.ladders.forEach((b, i) => out.push({ min: b.min, max: b.max, ref: { k: "ladder", i } }));
    if (sh.zones) {
      m.sites.forEach((b, i) => out.push({ min: b.min, max: b.max, ref: { k: "site", i }, zone: true }));
      if (m.hill) out.push({ min: m.hill.min, max: m.hill.max, ref: { k: "hill", i: 0 }, zone: true });
      m.points.forEach((b, i) => out.push({ min: b.min, max: b.max, ref: { k: "point", i }, zone: true }));
      m.checkpoints.forEach((b, i) => out.push({ min: b.min, max: b.max, ref: { k: "checkpoint", i }, zone: true }));
    }
    if (sh.stairs) m.stairs.forEach((s, i) => out.push({ min: [Math.min(s.x0, s.x1), s.base, Math.min(s.z0, s.z1)], max: [Math.max(s.x0, s.x1), s.top, Math.max(s.z0, s.z1)], ref: { k: "stairs", i }, zone: true }));
    return out;
  }
  // all hits under a screen point, nearest first; zones only count where the ray enters them from outside
  pickAll(e, surfacesOnly = false) {
    if (this.pickDirty) this.rebuildPick();
    const r = this.view.ray(e.clientX, e.clientY);
    const hits = this.picker.cast(r.o, r.d, 3000, null, !surfacesOnly);
    if (!surfacesOnly) for (const mk of this.markers()) {
      const h = rayBox(r.o, r.d, mk.min, mk.max);
      if (!h || (mk.zone && h[2])) continue;
      hits.push({ t: h[0] - (mk.zone ? 0 : 0.05), ax: h[1], ref: mk.ref, marker: true, zone: mk.zone });
    }
    hits.sort((a, b) => a.t - b.t);
    for (const h of hits) h.p = [0, 1, 2].map(k => r.o[k] + r.d[k] * h.t);
    return { hits, ray: r };
  }
  surface(e) {   // the first solid surface (or the grid plane y = 0) under the cursor: {p, n}
    const { hits, ray } = this.pickAll(e, true);
    const h = hits.find(x => !x.inside);
    if (h) return { p: h.p, n: normalOf(h.ax), hit: h };
    const y = this.view.gridY || 0;
    if (Math.abs(ray.d[1]) > 1e-6) { const t = (y - ray.o[1]) / ray.d[1]; if (t > 0) return { p: [ray.o[0] + ray.d[0] * t, y, ray.o[2] + ray.d[2] * t], n: [0, 1, 0] }; }
    return null;
  }

  // ---- the gizmo
  updateGizmo() {
    const g = this.gizmo;
    for (const c of g.children.slice()) { g.remove(c); c.geometry && c.geometry.dispose(); }
    const d = this.doc;
    if (!d.map || this.tool !== "select" || !d.sel.length || this.app.bots?.playing) return;
    const bb = (this.drag && this.drag.bb) || d.selBounds(); if (!bb) return;
    const ctr = [0, 1, 2].map(k => (bb.min[k] + bb.max[k]) / 2);
    const ppm = this.view.pxPerMetre(ctr), L = 70 / ppm, R = 3 / ppm;
    const mat = (c, a) => new THREE.MeshBasicMaterial({ color: c, transparent: true, opacity: a ?? 0.95, depthTest: false });
    const top = this.view.camMode === "top";
    for (let a = 0; a < 3; a++) {
      if (top && a === 1) continue;
      for (const sgn of [1, -1]) {
        const dir = AX[a].clone().multiplyScalar(sgn);
        const shaft = new THREE.Mesh(new THREE.CylinderGeometry(R, R, L, 6).translate(0, L / 2, 0), mat(AX_COL[a], sgn > 0 ? 0.95 : 0.4));
        shaft.quaternion.setFromUnitVectors(AX[1], dir); shaft.position.fromArray(ctr);
        const head = new THREE.Mesh(new THREE.ConeGeometry(R * 3, R * 8, 10).translate(0, L + R * 4, 0), mat(AX_COL[a], sgn > 0 ? 0.95 : 0.4));
        head.quaternion.copy(shaft.quaternion); head.position.fromArray(ctr);
        shaft.userData = head.userData = { handle: "move", axis: a };
        g.add(shaft, head);
      }
    }
    // face handles: one object with a box (resize)
    if (d.sel.length === 1 && ["box", "stairs"].includes(KINDS[d.sel[0].k].shape)) {
      const rep = d.sel[0].k === "object" ? this.objDef(d.obj(d.sel[0])) : null;
      for (let a = 0; a < 3; a++) for (const side of [0, 1]) {
        if (top && a === 1) continue;
        if (rep && !rep.axes[a]) continue;   // (a custom object only grows where it repeats)
        const p = ctr.slice(); p[a] = side ? bb.max[a] : bb.min[a];
        const s = 6 / ppm;
        const h = new THREE.Mesh(new THREE.BoxGeometry(s, s, s), mat(0xffffff, 0.9));
        h.position.fromArray(p); h.userData = { handle: "size", axis: a, side };
        const ring = new THREE.Mesh(new THREE.BoxGeometry(s * 1.5, s * 1.5, s * 1.5), mat(AX_COL[a], 0.5));
        ring.position.fromArray(p); ring.userData = h.userData;
        g.add(ring, h);
      }
    }
  }
  gizmoHit(e) {
    this.view.applyCamera(); this.view.camera.updateMatrixWorld();
    this.updateGizmo(); this.gizmo.updateMatrixWorld(true);   // (as it is now, even before the next frame draws it)
    if (!this.gizmo.children.length) return null;
    const r = this.canvas().getBoundingClientRect();
    const rc = new THREE.Raycaster();
    rc.setFromCamera(new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1), this.view.camera);
    const hit = rc.intersectObjects(this.gizmo.children, false)[0];
    return hit ? hit.object.userData : null;
  }
  canvas() { return this.view.canvas; }

  // ---- mouse
  down(e) {
    const v = this.view;
    this.canvas().focus();
    this.downAt = { x: e.clientX, y: e.clientY, button: e.button, moved: false, e };
    if (e.button === 2 || e.button === 1) {   // look / pan
      this.look = { x: e.clientX, y: e.clientY, mode: v.camMode === "fly" && e.button === 2 ? "look" : v.camMode === "orbit" && e.button === 2 ? "pan" : v.camMode === "top" ? "pan" : "pan" };
      if (v.camMode === "fly" && e.button === 2) v.keys.add("RMB");
      return;
    }
    if (e.button !== 0) return;
    if (e.altKey && v.camMode !== "top") { this.look = { x: e.clientX, y: e.clientY, mode: "orbit" }; if (v.camMode === "fly") v.setCamMode("orbit"); return; }
    if (this.tool !== "select") return;   // (tools act on the click: up())
    // a gizmo handle drags once the mouse moves; a click on one without moving selects what is under it (so the
    // handles over a small box don't stop "click again for what is behind")
    const gh = this.gizmoHit(e);
    if (gh) { this.pendingHandle = gh; return; }
    // on a selected object: slide it on the floor plane once the mouse moves
    const { hits } = this.pickAll(e);
    const first = hits[0];
    if (first && this.doc.isSel(first.ref.k, first.ref.i) && !e.shiftKey) this.pendingSlide = { hit: first };
  }
  move(e) {
    const v = this.view;
    if (this.look) {
      const dx = e.clientX - this.look.x, dy = e.clientY - this.look.y;
      this.look.x = e.clientX; this.look.y = e.clientY;
      const c = v.cam;
      if (this.look.mode === "look" || this.look.mode === "orbit") {
        c.yaw -= dx * 0.004; c.pitch = Math.max(-1.55, Math.min(1.55, c.pitch - dy * 0.004));
      } else if (this.look.mode === "pan") {
        const ppm = v.pxPerMetre(v.camMode === "top" ? null : c.target.toArray());
        if (v.camMode === "top") { c.target.x -= dx / ppm; c.target.z -= dy / ppm; }
        else {
          const f = v.fwd(), right = new THREE.Vector3(-f.z, 0, f.x).normalize(), up = new THREE.Vector3().crossVectors(right, f);
          const delta = right.multiplyScalar(-dx / ppm).add(up.multiplyScalar(dy / ppm));
          c.target.add(delta); if (v.camMode === "fly") c.pos.add(delta);
        }
      }
      v.redraw(); return;
    }
    const da = this.downAt;
    if (da && !da.moved && Math.hypot(e.clientX - da.x, e.clientY - da.y) > 4) {
      da.moved = true;
      if (this.pendingHandle) { this.startHandleDrag(this.pendingHandle, da.e); this.pendingHandle = null; }
      else if (this.pendingSlide) { this.startSlide(this.pendingSlide.hit, da.e); this.pendingSlide = null; }
      else if (!this.drag && da.button === 0 && this.tool === "select") {
        if (v.camMode === "orbit" && !e.shiftKey) this.look = { x: e.clientX, y: e.clientY, mode: "orbit" };
        else this.marquee = { x0: da.x, y0: da.y };
      }
    }
    if (this.drag) { this.dragMove(e); return; }
    if (this.marquee) { this.drawMarquee(e); return; }
    if (e.target === this.canvas()) { this.lastMouse = { clientX: e.clientX, clientY: e.clientY }; this.hoverAt(e); }
  }
  up(e) {
    const v = this.view;
    if (this.look) { this.look = null; v.keys.delete("RMB"); this.downAt = null; return; }
    const da = this.downAt; this.downAt = null; this.pendingSlide = null; this.pendingHandle = null;
    if (this.drag) { this.endDrag(); return; }
    if (this.marquee) { this.endMarquee(e); return; }
    if (!da || da.button !== 0 || da.moved || e.target !== this.canvas()) return;
    if (this.tool === "select") this.clickSelect(e);
    else this.place(e);
  }
  wheel(e) {
    e.preventDefault();
    const v = this.view, c = v.cam, k = Math.exp(Math.sign(e.deltaY) * 0.15);
    if (v.camMode === "top") {
      const r = this.canvas().getBoundingClientRect();
      const mx = (e.clientX - r.left) / r.width - 0.5, my = (e.clientY - r.top) / r.height - 0.5;
      const a = v.w / v.h, before = c.topH;
      c.topH = Math.max(2, Math.min(4000, c.topH * k));
      c.target.x += mx * a * (before - c.topH); c.target.z += my * (before - c.topH);
      v.updateOrtho();
    } else if (v.camMode === "orbit") c.dist = Math.max(0.5, Math.min(3000, c.dist * k));
    else if (e.ctrlKey || e.altKey) c.speed = Math.max(1, Math.min(200, c.speed / k));
    else c.pos.addScaledVector(v.fwd(), -Math.sign(e.deltaY) * Math.max(1, c.speed * 0.25));
    v.redraw();
  }
  dblclick(e) {   // frame what you double-clicked
    const { hits } = this.pickAll(e);
    if (hits[0]) { const b = this.doc.bounds(hits[0].ref); if (b) this.view.frame(b.min, b.max); }
  }
  clickSelect(e) {
    const { hits } = this.pickAll(e);
    const d = this.doc;
    if (!hits.length) { if (!e.shiftKey) d.select([]); return; }
    // the same spot again: the next thing under the cursor (cycling)
    let pick = hits[0];
    const sameSpot = this.cycle && Math.hypot(e.clientX - this.cycle.x, e.clientY - this.cycle.y) < 4;
    if (sameSpot && !e.shiftKey) {
      const uniq = []; for (const h of hits) if (!uniq.some(u => u.ref.k === h.ref.k && u.ref.i === h.ref.i)) uniq.push(h);
      this.cycle.n = (this.cycle.n + 1) % uniq.length; pick = uniq[this.cycle.n];
    } else this.cycle = { x: e.clientX, y: e.clientY, n: 0 };
    const ref = { k: pick.ref.k, i: pick.ref.i };
    if (ref.k === "group" && (e.ctrlKey || e.metaKey) && pick.ref.gen !== undefined) {   // Ctrl: that part of the group
      const g = d.map.generated; let n = 0; for (let k = pick.ref.gen - 1; k >= 0 && g[k].group === ref.i; k--) n++;
      ref.part = n;
    }
    d.select([ref], e.shiftKey);
  }
  hoverAt(e) {
    if (this.hoverPending) { this.hoverPending = e; return; }
    this.hoverPending = e;
    requestAnimationFrame(() => {
      const ev = this.hoverPending; this.hoverPending = null;
      if (!this.doc.map || this.look || this.drag) return;
      const tip = this.app.tip;
      const { hits } = this.pickAll(ev);
      const h = hits[0];
      if (!h) { tip.hidden = true; return; }
      const r = this.canvas().parentElement.getBoundingClientRect();
      tip.hidden = false;
      tip.style.left = (ev.clientX - r.left + 14) + "px"; tip.style.top = (ev.clientY - r.top + 14) + "px";
      tip.textContent = this.app.describe(h.ref) + "\n" + h.p.map(x => x.toFixed(2)).join(" ");
    });
  }

  // ---- drags
  dragKeys() { return this.doc.keysOf(); }
  startHandleDrag(gh, e) {
    const d = this.doc;
    const bb = d.selBounds();
    const ctr = [0, 1, 2].map(k => (bb.min[k] + bb.max[k]) / 2);
    const P0 = gh.handle === "size" ? ctr.map((v, k) => k === gh.axis ? (gh.side ? bb.max[k] : bb.min[k]) : v) : ctr;
    this.drag = { kind: gh.handle, axis: gh.axis, side: gh.side, P0, s0: this.axisParam(e, P0, gh.axis), from: d.sel.map(s => structuredClone(d.obj(s))), sel: d.sel.slice(), tx: d.begin(gh.handle === "move" ? "move" : "resize", this.dragKeys(), d.sel), bb0: bb, bb: null };
  }
  startSlide(hit, e) {
    const d = this.doc;
    const y = hit.p[1];
    this.drag = { kind: "slide", y, p0: this.planePoint(e, y) || hit.p, from: d.sel.map(s => structuredClone(d.obj(s))), sel: d.sel.slice(), tx: d.begin("move", this.dragKeys(), d.sel) };
  }
  axisParam(e, P0, a) {   // the point on the axis through P0 nearest the mouse ray: its parameter
    const r = this.view.ray(e.clientX, e.clientY);
    const A = AX[a].toArray(), w0 = [0, 1, 2].map(k => P0[k] - r.o[k]);
    const b = A[0] * r.d[0] + A[1] * r.d[1] + A[2] * r.d[2], dd = A[0] * w0[0] + A[1] * w0[1] + A[2] * w0[2], ee = r.d[0] * w0[0] + r.d[1] * w0[1] + r.d[2] * w0[2];
    const den = 1 - b * b;
    if (Math.abs(den) < 1e-6) return 0;
    return (b * ee - dd) / den;
  }
  planePoint(e, y) {
    const r = this.view.ray(e.clientX, e.clientY);
    if (Math.abs(r.d[1]) < 1e-6) return null;
    const t = (y - r.o[1]) / r.d[1]; if (t < 0) return null;
    return [r.o[0] + r.d[0] * t, y, r.o[2] + r.d[2] * t];
  }
  dragMove(e) {
    const dr = this.drag, d = this.doc, g = this.grid;
    if (dr.kind === "move" || dr.kind === "slide") {
      let delta = [0, 0, 0];
      if (dr.kind === "move") delta[dr.axis] = snap(this.axisParam(e, dr.P0, dr.axis) - dr.s0, g);
      else { const p = this.planePoint(e, dr.y); if (!p) return; delta = [snap(p[0] - dr.p0[0], g), 0, snap(p[2] - dr.p0[2], g)]; }
      delta = delta.map(tidy);
      if (dr.last && dr.last.every((v, k) => v === delta[k])) return;
      dr.last = delta;
      dr.sel.forEach((s, j) => d.moveFrom(s, dr.from[j], delta));
      dr.bb = null;
      this.app.hudDrag = "move " + delta.map(v => (v >= 0 ? "+" : "") + v).join(" ");
    } else {
      const s = dr.sel[0], o = d.obj(s), f = dr.from[0], a = dr.axis;
      const t = snap(this.axisParam(e, dr.P0, a) - dr.s0, g);
      const box = KINDS[s.k].shape === "stairs" ? stairsBox(f) : { min: f.min.slice(), max: f.max.slice() };
      const rep = s.k === "object" ? this.objDef(o) : null;
      if (rep) {   // a custom object: whole units (the spacing), at least one; the far side stays put
        const sp = Math.max(0.05, rep.spacing[a]), n0 = repeatCounts(rep, f)[a];
        const moved = (this.axisParam(e, dr.P0, a) - dr.s0) * (dr.side ? 1 : -1);   // (+: longer)
        const n = Math.max(1, n0 + Math.round(moved / sp));
        const len = repeatLen(rep, a, n);
        if (dr.side) box.max[a] = tidy(box.min[a] + len); else box.min[a] = tidy(box.max[a] - len);
        o.min = box.min; o.max = box.max;
        this.app.hudDrag = s.k + " " + repeatCounts(rep, box).join(" x ") + " units";
        dr.tx.live(); return;
      }
      const minSize = Math.min(g, box.max[a] - box.min[a]);
      if (dr.side) box.max[a] = tidy(Math.max(box.min[a] + minSize, box.max[a] + t)); else box.min[a] = tidy(Math.min(box.max[a] - minSize, box.min[a] + t));
      if (KINDS[s.k].shape === "stairs") setStairsBox(o, box); else { o.min = box.min; o.max = box.max; }
      this.app.hudDrag = "size " + [0, 1, 2].map(k => tidy(box.max[k] - box.min[k])).join(" x ") + " m";
    }
    dr.tx.live();
  }
  endDrag() {
    const dr = this.drag; this.drag = null; this.app.hudDrag = "";
    dr.tx.commit();   // (no step when the objects came back where they were)
  }
  drawMarquee(e) {
    const r = this.canvas().parentElement.getBoundingClientRect();
    let m = this.app.marqueeEl;
    if (!m) { m = this.app.marqueeEl = document.createElement("div"); Object.assign(m.style, { position: "absolute", border: "1px dashed #fff", background: "#4f9cf522", pointerEvents: "none" }); this.canvas().parentElement.append(m); }
    const x0 = Math.min(this.marquee.x0, e.clientX) - r.left, y0 = Math.min(this.marquee.y0, e.clientY) - r.top;
    Object.assign(m.style, { left: x0 + "px", top: y0 + "px", width: Math.abs(e.clientX - this.marquee.x0) + "px", height: Math.abs(e.clientY - this.marquee.y0) + "px", display: "block" });
  }
  endMarquee(e) {
    const r = this.canvas().getBoundingClientRect();
    const x0 = Math.min(this.marquee.x0, e.clientX) - r.left, x1 = Math.max(this.marquee.x0, e.clientX) - r.left;
    const y0 = Math.min(this.marquee.y0, e.clientY) - r.top, y1 = Math.max(this.marquee.y0, e.clientY) - r.top;
    this.marquee = null; if (this.app.marqueeEl) this.app.marqueeEl.style.display = "none";
    const v = this.view, d = this.doc, m = d.map, out = [];
    v.applyCamera(); v.camera.updateMatrixWorld();
    const vp = new THREE.Matrix4().multiplyMatrices(v.camera.projectionMatrix, v.camera.matrixWorldInverse).elements;
    const inside = (x, y, z) => {
      const cw = vp[3] * x + vp[7] * y + vp[11] * z + vp[15]; if (cw <= 0) return false;
      const sx = ((vp[0] * x + vp[4] * y + vp[8] * z + vp[12]) / cw + 1) / 2 * v.w, sy = (1 - (vp[1] * x + vp[5] * y + vp[9] * z + vp[13]) / cw) / 2 * v.h;
      return sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
    };
    const ctr = (b) => [(b.min[0] + b.max[0]) / 2, (b.min[1] + b.max[1]) / 2, (b.min[2] + b.max[2]) / 2];
    m.brushes.forEach((b, i) => { if ((b.mat === 9 && !v.show.clip) || (b.mat === 5)) return; if (inside(...ctr(b))) out.push({ k: "brush", i }); });
    m.groups.forEach((b, i) => { if (inside(...ctr(b))) out.push({ k: "group", i }); });
    for (const mk of this.markers()) if (!mk.zone && inside(...ctr(mk))) out.push(mk.ref);
    d.select(out, e.shiftKey);
  }

  // ---- add tools
  place(e) {
    const d = this.doc, g = this.grid, s = this.surface(e);
    if (!s) return;
    if (this.tool === "route") { this.app.testing.routeClick(s.p); return; }
    if (this.tool === "pickpoint") { const cb = this.pickCb; this.pickCb = null; this.setTool("select"); if (cb) cb(s.p); return; }
    if (d.readOnly && !["figure", "botstart"].includes(this.tool)) { this.app.readOnlyNote(); }
    const p = s.p, n = s.n, yaw = this.view.cam.yaw;
    const S = (v) => tidy(snap(v, g));
    const onSurface = (size) => {   // a box of `size` sitting on the surface (its face on it), snapped across
      const min = [0, 0, 0], max = [0, 0, 0];
      for (let k = 0; k < 3; k++) {
        if (n[k] > 0) { min[k] = tidy(p[k]); max[k] = tidy(p[k] + size[k]); }
        else if (n[k] < 0) { max[k] = tidy(p[k]); min[k] = tidy(p[k] - size[k]); }
        else { min[k] = S(p[k] - size[k] / 2); max[k] = tidy(min[k] + size[k]); }
      }
      return { min, max };
    };
    const across = Math.abs(Math.sin(yaw)) > Math.abs(Math.cos(yaw)) ? 2 : 0;   // the axis across your view
    const add = (label, key, obj, k) => {
      let idx = -1;
      d.edit("add " + label, [key], () => { const arr = d.get(key); arr.push(obj); idx = arr.length - 1; }, { touch: [] });
      d.select([{ k, i: idx }]);
    };
    switch (this.tool) {
      case "box": { const b = onSurface([2, 2, 2]); b.mat = this.mat; add("box", "brushes", b, "brush"); break; }
      case "room": { const b = onSurface([8, 4, 8]); add("room", "groups", { type: "room", mat: this.mat, dir: 0, min: b.min, max: b.max, parts: [] }, "group"); break; }
      case "stairs": {
        const f = [-Math.sin(yaw), -Math.cos(yaw)];
        const dir = Math.abs(f[0]) > Math.abs(f[1]) ? (f[0] > 0 ? 0 : 1) : (f[1] > 0 ? 2 : 3);
        const size = dir < 2 ? [4, 2, 3] : [3, 2, 4];
        const b = onSurface(size); add("stairs", "groups", { type: "stairs", mat: this.mat, dir, min: b.min, max: b.max, parts: [] }, "group"); break;
      }
      case "spawn": add("spawn", "spawns", { pos: [S(p[0]), tidy(p[1] + (n[1] > 0 ? 0 : 0.01)), S(p[2])], yaw: snapYaw(yaw), team: this.team }, "spawn"); break;
      case "botstart": add("bot start", "sc.botStarts", { pos: [S(p[0]), tidy(p[1]), S(p[2])], yaw: snapYaw(yaw), team: this.team }, "botstart"); break;
      case "light": add("light", "lights", { pos: [S(p[0] + n[0] * 2), S(p[1] + n[1] * 2 + (n[1] ? 0 : 1)), S(p[2] + n[2] * 2)], radius: 10, intensity: 1 }, "light"); break;
      case "site": add("bomb site", "sites", onSurface([8, 3, 8]), "site"); break;
      case "point": add("capture point", "points", onSurface([8, 3, 8]), "point"); break;
      case "hill": d.edit("set hill", ["hill"], (m) => { m.hill = onSurface([10, 3, 10]); }); d.select([{ k: "hill", i: 0 }]); break;
      case "checkpoint": { const sz = across === 0 ? [8, 4, 1] : [1, 4, 8]; add("checkpoint", "checkpoints", onSurface(sz), "checkpoint"); break; }
      case "path": {
        const sel = d.selOf("path"); const at = sel.length ? Math.max(...sel) + 1 : d.map.path.length;
        d.edit("add path point", ["path"], (m) => m.path.splice(at, 0, [S(p[0]), tidy(p[1]), S(p[2])]), { touch: [] });
        d.select([{ k: "path", i: at }]); break;
      }
      case "ladder": {
        let b;
        if (n[1] === 0) {   // against the wall you clicked: 1 m wide, 0.5 m out, 4 m up from the click's grid step
          const wa = n[0] !== 0 ? 2 : 0, out = n[0] !== 0 ? 0 : 2;
          b = { min: [0, 0, 0], max: [0, 0, 0] };
          b.min[wa] = S(p[wa] - 0.5); b.max[wa] = tidy(b.min[wa] + 1);
          if (n[out] > 0) { b.min[out] = tidy(p[out]); b.max[out] = tidy(p[out] + 0.5); } else { b.max[out] = tidy(p[out]); b.min[out] = tidy(p[out] - 0.5); }
          b.min[1] = S(p[1] - 2); b.max[1] = tidy(b.min[1] + 4);
        } else { b = onSurface(across === 0 ? [1, 4, 0.5] : [0.5, 4, 1]); }
        b.auto = true; add("ladder", "ladders", b, "ladder"); break;
      }
      case "figure": this.app.player.drop(p, yaw); break;
      case "stamp": this.app.library.stampAt(s); break;
    }
    this.pickDirty = true;
  }

  // ---- keys
  key(e, down) {
    const t = e.target;
    const typing = t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT" || t.isContentEditable);
    const v = this.view, d = this.doc;
    const flyKeys = ["KeyW", "KeyA", "KeyS", "KeyD", "Space", "KeyC", "ShiftLeft", "ShiftRight"];
    if (!down) { v.keys.delete(e.code); if (e.code === "KeyQ" || e.code === "KeyE") v.keys.delete(e.code); return; }
    if (typing || document.querySelector("dialog[open]")) return;
    const ctrl = e.ctrlKey || e.metaKey;
    if (ctrl) {
      const k = e.key.toLowerCase();
      if (k === "z" && !e.shiftKey) { e.preventDefault(); d.undo(); return; }
      if (k === "y" || (k === "z" && e.shiftKey)) { e.preventDefault(); d.redo(); return; }
      if (k === "s") { e.preventDefault(); e.shiftKey ? this.app.saveAs() : this.app.save(); return; }
      if (k === "d") { e.preventDefault(); this.duplicate(); return; }
      if (k === "a") { e.preventDefault(); d.select(d.map.brushes.map((_, i) => ({ k: "brush", i }))); return; }
    }
    const inView = this.canvas().matches(":hover") || document.activeElement === this.canvas();
    if (!ctrl && flyKeys.includes(e.code) && inView && v.camMode === "fly") { v.keys.add(e.code); if (e.code === "Space") e.preventDefault(); return; }
    if ((e.code === "KeyQ" || e.code === "KeyE") && v.keys.has("RMB")) { v.keys.add(e.code); return; }
    if (!inView && !["Escape", "Delete"].includes(e.key)) return;
    switch (e.key) {
      case "Escape": if (this.tool !== "select") { this.stamp = null; this.setTool("select"); } else d.select([]); return;
      case "Delete": case "Backspace": d.deleteSel(); this.pickDirty = true; e.preventDefault(); return;
      case "[": this.app.setGrid(Math.max(0, this.gridIdx - 1)); return;
      case "]": this.app.setGrid(Math.min(GRIDS.length - 1, this.gridIdx + 1)); return;
      case "f": case "F": { const b = d.selBounds(); if (b) v.frame(b.min, b.max); else v.frameMap(); return; }
      case "Home": v.frameMap(); return;
      case "t": case "T": this.app.setCam(v.camMode === "top" ? "fly" : "top"); return;
      case "b": case "B": this.setTool("box"); return;
      case "Insert": this.setTool("box"); return;
      case "m": case "M": this.cycleMaterial(); return;
      case "r": case "R": this.rotateSel(); return;
      case "x": case "X": this.flipSel(true, false); return;
      case "z": case "Z": this.flipSel(false, true); return;
      case "p": case "P": this.app.mirrorCopy(); return;
      case "q": case "Q": case "e": case "E": this.turnSel(e.key.toLowerCase() === "q" ? 1 : -1); return;
      case "?": this.app.help(); return;
    }
    const arrows = { ArrowUp: 1, ArrowDown: 1, ArrowLeft: 1, ArrowRight: 1, PageUp: 1, PageDown: 1 };
    if (arrows[e.key] && d.sel.length) { e.preventDefault(); this.nudge(e.key, e.shiftKey, ctrl); }
  }
  nudge(key, grow, shrink) {   // the in-game editor's arrows: move along the world axis nearest your view
    const d = this.doc, g = this.grid, v = this.view;
    const f = [-Math.sin(v.cam.yaw), 0, -Math.cos(v.cam.yaw)];
    const fa = Math.abs(f[0]) > Math.abs(f[2]) ? 0 : 2, ra = 2 - fa;
    const fs = f[fa] > 0 ? 1 : -1;
    const rv = [-f[2], 0, f[0]]; const rs = rv[ra] > 0 ? 1 : -1;
    let axis = -1, sign = 0;
    if (v.camMode === "top") { if (key === "ArrowUp") { axis = 2; sign = -1; } else if (key === "ArrowDown") { axis = 2; sign = 1; } else if (key === "ArrowLeft") { axis = 0; sign = -1; } else if (key === "ArrowRight") { axis = 0; sign = 1; } }
    else if (key === "ArrowUp") { axis = fa; sign = fs; } else if (key === "ArrowDown") { axis = fa; sign = -fs; }
    else if (key === "ArrowRight") { axis = ra; sign = rs; } else if (key === "ArrowLeft") { axis = ra; sign = -rs; }
    if (key === "PageUp") { axis = 1; sign = 1; } else if (key === "PageDown") { axis = 1; sign = -1; }
    if (axis < 0) return;
    d.edit(grow || shrink ? "resize" : "move", d.keysOf(), () => {
      for (const s of d.sel) {
        const K = KINDS[s.k], o = d.obj(s);
        if (grow || shrink) {
          if (s.k === "light") { o.radius = Math.max(1, Math.min(60, o.radius + (grow ? 1 : -1))); continue; }
          if (s.k === "object") {   // one unit more / less where it repeats
            const rep = this.objDef(o); if (!rep.axes[axis]) continue;
            const n = Math.max(1, repeatCounts(rep, o)[axis] + (grow ? 1 : -1)), len = repeatLen(rep, axis, n);
            if (sign > 0) o.max[axis] = tidy(o.min[axis] + len); else o.min[axis] = tidy(o.max[axis] - len);
            continue;
          }
          if (K.shape !== "box") continue;
          const amount = grow ? g : -g, minSize = Math.min(g, o.max[axis] - o.min[axis]);
          if (sign > 0) o.max[axis] = tidy(Math.max(o.min[axis] + minSize, o.max[axis] + amount));
          else o.min[axis] = tidy(Math.min(o.max[axis] - minSize, o.min[axis] - amount));
        } else {
          const dd = [0, 0, 0]; dd[axis] = sign * g;
          d.moveFrom(s, structuredClone(o), dd);
          roundObj(o);
        }
      }
    }, { touch: d.sel });
  }
  cycleMaterial() {
    const d = this.doc;
    const targets = d.sel.filter(s => s.k === "brush" || (s.k === "group" && d.map.groups[s.i].type !== "custom") || s.k === "spawn");
    if (!targets.length) return;
    const teams = ["a", "b", "any", "race"];
    d.edit("material", d.keysOf(targets), () => {
      for (const s of targets) {
        const o = d.obj(s);
        if (s.k === "spawn") { o.team = teams[(teams.indexOf(o.team) + 1) % teams.length]; continue; }
        o.mat = (o.mat + 1) % 10; this.mat = o.mat;
      }
    }, { touch: targets });
    this.app.emit("tool", this.tool);
  }
  turnSel(dir) {
    const d = this.doc;
    const targets = d.sel.filter(s => ["spawn", "figure", "botstart"].includes(s.k));
    if (!targets.length) return;
    d.edit("turn", d.keysOf(targets), () => { for (const s of targets) { const o = d.obj(s); o.yaw = snapYaw(o.yaw + dir * Math.PI / 4); } }, { touch: targets });
  }
  duplicate() {
    const d = this.doc, b = d.selBounds(); if (!b) return;
    const v = this.view, f = [-Math.sin(v.cam.yaw), -Math.cos(v.cam.yaw)];
    const ra = Math.abs(f[0]) > Math.abs(f[1]) ? 2 : 0;   // beside it, across your view
    const off = [0, 0, 0]; off[ra] = tidy(Math.max(this.grid, snap(b.max[ra] - b.min[ra], this.grid) || this.grid));
    d.duplicateSel(off); this.pickDirty = true;
  }
  // 90 degrees about Y around the selection's centre (snapped): boxes swap their x / z sizes, stairs turn, spawns turn
  rotateSel() {
    const d = this.doc, bb = d.selBounds(); if (!bb) return;
    const cx = snap((bb.min[0] + bb.max[0]) / 2, this.grid / 2), cz = snap((bb.min[2] + bb.max[2]) / 2, this.grid / 2);
    const rot = (x, z) => [tidy(cx - (z - cz)), tidy(cz + (x - cx))];
    const DIR = [2, 3, 1, 0];
    d.edit("rotate", d.keysOf(), () => {
      for (const s of d.sel) {
        const K = KINDS[s.k], o = d.obj(s);
        if (K.shape === "box") {
          const [ax, az] = rot(o.min[0], o.min[2]), [bx, bz] = rot(o.max[0], o.max[2]);
          o.min = [Math.min(ax, bx), o.min[1], Math.min(az, bz)]; o.max = [Math.max(ax, bx), o.max[1], Math.max(az, bz)];
          if (s.k === "object") o.rot = ((o.rot || 0) + 1) % 4;
          if (s.k === "group") {
            if (o.type === "stairs") o.dir = DIR[o.dir];
            if (o.type === "custom") for (const p of o.parts) { const x0 = p.min[0], x1 = p.max[0]; p.min[0] = tidy(1 - p.max[2]); p.max[0] = tidy(1 - p.min[2]); p.min[2] = x0; p.max[2] = x1; }
          }
        } else if (K.shape === "stairs") {
          const [ax, az] = rot(o.x0, o.z0), [bx, bz] = rot(o.x1, o.z1);
          o.x0 = Math.min(ax, bx); o.x1 = Math.max(ax, bx); o.z0 = Math.min(az, bz); o.z1 = Math.max(az, bz);
          if (o.axis === "z") o.rising = -(o.rising || 1);   // (+z turns to -x; +x turns to +z)
          o.axis = o.axis === "x" ? "z" : "x";
        } else {
          const p = K.shape === "arr" ? o : o.pos; const [x, z] = rot(p[0], p[2]); p[0] = x; p[2] = z;
          if (o.yaw !== undefined) o.yaw = snapYaw(o.yaw - Math.PI / 2);
        }
      }
    }, { touch: d.sel });
  }
  // mirror the selection about its centre (snapped): across x (X, left / right) or z (Z, front / back). Boxes, groups
  // (stairs turn round, custom parts flip), custom objects, spawns (yaw mirrored), stairs records, points
  flipSel(fx, fz) {
    const d = this.doc, bb = d.selBounds(); if (!bb) return;
    const c = [snap((bb.min[0] + bb.max[0]) / 2, this.grid / 2), snap((bb.min[2] + bb.max[2]) / 2, this.grid / 2)];
    d.edit(fx ? "flip x" : "flip z", d.keysOf(), () => { for (const s of d.sel) flipObj(s.k, d.obj(s), fx, fz, c); }, { touch: d.sel });
    this.pickDirty = true;
  }
  // the point-symmetric twin of the selection through the map's centre (0, 0), as copies: what quark_maptool's
  // ranked_mirror check looks for (x, z -> -x, -z; team A <-> B; yaw + 180). Objects that are their own twin (centred)
  // and the hill (a map has one) are left out. One undo step; the copies are selected.
  mirrorCopy() {
    const d = this.doc, sel = d.sel.filter(s => !KINDS[s.k].single), hill = sel.length < d.sel.length;
    if (!sel.length) return { added: 0, same: 0, hill };
    const added = [];
    let same = 0;
    d.edit("mirror copy", d.keysOf(sel), () => {
      for (const s of sel) {
        const o = d.obj(s), t = twinObj(s.k, o);
        if (JSON.stringify(t) === JSON.stringify(o)) { same++; continue; }
        const arr = d.get(KINDS[s.k].key); arr.push(t); added.push({ k: s.k, i: arr.length - 1 });
      }
    }, { touch: [] });
    d.select(added); this.pickDirty = true;
    return { added: added.length, same, hill };
  }
}

function snapYaw(y) { const s = Math.PI / 8; let v = Math.round(y / s) * s; v = ((v % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI); return Math.round(v * 1e6) / 1e6; }
function roundObj(o) {
  if (o.min) { o.min = o.min.map(tidy); o.max = o.max.map(tidy); }
  if (o.pos) o.pos = o.pos.map(tidy);
  if (Array.isArray(o)) for (let k = 0; k < 3; k++) o[k] = tidy(o[k]);
}
function stairsBox(s) { return { min: [Math.min(s.x0, s.x1), s.base, Math.min(s.z0, s.z1)], max: [Math.max(s.x0, s.x1), s.top, Math.max(s.z0, s.z1)] }; }
function setStairsBox(s, b) { s.x0 = b.min[0]; s.x1 = b.max[0]; s.z0 = b.min[2]; s.z1 = b.max[2]; s.base = b.min[1]; s.top = b.max[1]; }
