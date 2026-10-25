// The gameplay markers in the 3D view: spawns and bot starts (hull, outline, facing arrow), lights, the payload path,
// objective zones, ladder / stairs volumes and group outlines. build() makes them when the map changes: every spawn,
// light and path point is one instance of a few InstancedMeshes, every outline one vertex range of a merged line
// set, so Midtown's markers are a few dozen draw calls, not 500 objects. restyle() follows the selection by
// rewriting instance colours / matrices and line colours in place: a click creates nothing (only a selected light's
// radius sphere, drawn for the selected lights alone).
import * as THREE from "../vendor/three.module.min.js";
import { segmentMesh, polylineSegs, label, dispose } from "./lines.js";
import { objectiveName } from "./doc.js";

export const TEAM_COLORS = { a: 0x4f9cf5, b: 0xec5a4f, any: 0xe8e8e8, race: 0xf2d45a };
export function teamColor(team) {
  if (TEAM_COLORS[team] !== undefined) return TEAM_COLORS[team];
  if (/^att\d$/.test(team)) return [0x4f9cf5, 0x6fb4ff, 0x3a7dd0, 0x8cc4ff, 0x2f6ab0][+team[3] % 5];
  if (/^def\d$/.test(team)) return [0xec5a4f, 0xff7a6e, 0xc9443a, 0xff9a8f, 0xa83a30][+team[3] % 5];
  return 0xaaaaaa;
}
export const ZONE_COLORS = { site: 0xff6a3d, hill: 0xffd23f, point: 0xb57aff, checkpoint: 0x3fd8ff, finish: 0x3fff8a, ladder: 0x5fe35f, stairs: 0x9aa3b0 };
const SEL = new THREE.Color(0xffc040), WHITE = new THREE.Color(0xffffff), PATH = new THREE.Color(0xffd23f), LIGHT = new THREE.Color(0xfff0a0), BOT_RING = new THREE.Color(0x99ccff);

// shared geometry (never disposed: userData.shared)
const shared = (g) => { g.userData.shared = true; return g; };
const HULL = shared(new THREE.BoxGeometry(0.8, 1.8, 0.8).translate(0, 0.9, 0));
const ARROW = shared(new THREE.ConeGeometry(0.16, 0.5, 10).rotateX(-Math.PI / 2).translate(0, 1.6, -0.65));
const RING = shared(new THREE.TorusGeometry(0.55, 0.05, 6, 20).rotateX(Math.PI / 2).translate(0, 0.05, 0));
const BALL = shared(new THREE.SphereGeometry(1, 12, 8));
const UNIT = shared(new THREE.BoxGeometry(1, 1, 1));
const UNIT_EDGES = shared(new THREE.EdgesGeometry(UNIT));
const WIRE_BALL = shared(new THREE.WireframeGeometry(new THREE.SphereGeometry(1, 16, 8)));
const CART_RING = shared(new THREE.TorusGeometry(0.8, 0.08, 6, 24));
const HULL_EDGES = new THREE.EdgesGeometry(HULL).attributes.position.array;   // (24 points: 12 segments)
const BOX_EDGES = UNIT_EDGES.attributes.position.array;

const tmpM = new THREE.Matrix4(), tmpV = new THREE.Vector3(), tmpQ = new THREE.Quaternion(), tmpS = new THREE.Vector3(), tmpC = new THREE.Color(), Y = new THREE.Vector3(0, 1, 0);

function instanced(geo, mat, n) {
  const m = new THREE.InstancedMesh(geo, mat, Math.max(1, n));
  m.count = n; m.frustumCulled = false;
  if (n) m.setColorAt(0, WHITE);   // (instanceColor exists before the first draw: the shader is made with it)
  return m;
}
// one LineSegments for many outlines: n * 24 points, colours (RGBA when alpha) written by restyle
function lineSet(n, alpha) {
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(new Float32Array(n * 72), 3));
  g.setAttribute("color", new THREE.BufferAttribute(new Float32Array(n * 24 * (alpha ? 4 : 3)), alpha ? 4 : 3));
  return new THREE.LineSegments(g, new THREE.LineBasicMaterial({ vertexColors: true, transparent: alpha, depthWrite: !alpha }));
}
function colorRange(line, j, c, a) {   // the 24 points of outline j
  const A = line.geometry.attributes.color, n = A.itemSize, arr = A.array;
  for (let v = j * 24; v < j * 24 + 24; v++) { arr[v * n] = c.r; arr[v * n + 1] = c.g; arr[v * n + 2] = c.b; if (n === 4) arr[v * n + 3] = a; }
  A.needsUpdate = true;
}

export class Markers {
  constructor(view) { this.view = view; this.layer = view.layers.markers; this.extra = new THREE.Group(); this.layer.add(this.extra); }

  clear() {
    for (const c of this.layer.children.slice()) { if (c === this.extra) continue; this.layer.remove(c); dispose(c); }
    this.clearExtra();
    this.zones = []; this.pawns = []; this.lightsM = null; this.pathM = null; this.groupLines = null; this.objLines = null;
  }
  clearExtra() { for (const c of this.extra.children.slice()) { this.extra.remove(c); dispose(c); } }

  build() {
    this.clear();
    const v = this.view, m = v.doc.map, sh = v.show, d = v.doc, L = this.layer;
    if (!m) return;
    const add = (o) => { L.add(o); return o; };
    // zones: a unit box scaled to each, its outline, its label (a sibling, so it isn't scaled)
    const zone = (k, i, b, color, text) => {
      const sz = [0, 1, 2].map(n => Math.max(0.01, b.max[n] - b.min[n])), ctr = [0, 1, 2].map(n => (b.min[n] + b.max[n]) / 2);
      const mesh = add(new THREE.Mesh(UNIT, new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.13, depthWrite: false })));
      mesh.position.fromArray(ctr); mesh.scale.fromArray(sz); mesh.renderOrder = 6;
      const edges = new THREE.LineSegments(UNIT_EDGES, new THREE.LineBasicMaterial({ color })); mesh.add(edges);
      if (text && sh.labels) { const s = add(label(text, "#" + new THREE.Color(color).getHexString(), 0.9)); s.position.set(ctr[0], b.max[1] + 0.7, ctr[2]); s.userData.prio = 2; }
      this.zones.push({ k, i, mesh, edges, color });
    };
    if (sh.zones) {
      m.sites.forEach((b, i) => zone("site", i, b, ZONE_COLORS.site, objectiveName(m, "site", i).toUpperCase()));
      if (m.hill) zone("hill", 0, m.hill, ZONE_COLORS.hill, "HILL");
      m.points.forEach((b, i) => zone("point", i, b, ZONE_COLORS.point, objectiveName(m, "point", i).toUpperCase()));
      m.checkpoints.forEach((b, i) => { const fin = i === m.checkpoints.length - 1; zone("checkpoint", i, b, fin ? ZONE_COLORS.finish : ZONE_COLORS.checkpoint, objectiveName(m, "checkpoint", i).toUpperCase() + (fin ? " FINISH" : "")); });
    }
    if (sh.ladders) m.ladders.forEach((b, i) => zone("ladder", i, b, ZONE_COLORS.ladder, null));
    if (sh.stairs) m.stairs.forEach((s, i) => zone("stairs", i, { min: [Math.min(s.x0, s.x1), s.base, Math.min(s.z0, s.z1)], max: [Math.max(s.x0, s.x1), s.top, Math.max(s.z0, s.z1)] }, ZONE_COLORS.stairs, null));
    // group outlines (every group's box; unselected ones only while group parts are shown)
    if (m.groups.length) {
      const gl = this.groupLines = add(lineSet(m.groups.length, true)), P = gl.geometry.attributes.position.array;
      m.groups.forEach((g, j) => { for (let p = 0; p < 24; p++) for (let n = 0; n < 3; n++) P[(j * 24 + p) * 3 + n] = g.min[n] + (BOX_EDGES[p * 3 + n] + 0.5) * (g.max[n] - g.min[n]); });
      gl.renderOrder = 6;
    }
    // custom objects: their box outlined (so a row of posts reads as one object)
    const objs = d.sidecar.objects || [];
    if (objs.length) {
      const ol = this.objLines = add(lineSet(objs.length, true)), P = ol.geometry.attributes.position.array;
      objs.forEach((o, j) => { for (let p = 0; p < 24; p++) for (let n = 0; n < 3; n++) P[(j * 24 + p) * 3 + n] = o.min[n] + (BOX_EDGES[p * 3 + n] + 0.5) * (o.max[n] - o.min[n]); });
      ol.renderOrder = 6;
    }
    // the payload path: the track, a ball per point, the cart checkpoints
    if (sh.path && m.path.length) {
      add(segmentMesh(polylineSegs(m.path.map(p => [p[0], p[1] + 0.15, p[2]]), 0xffd23f), { width: 0.12 }));
      this.pathM = add(instanced(BALL, new THREE.MeshBasicMaterial(), m.path.length));
      const segL = []; let tot = 0;
      for (let i = 0; i + 1 < m.path.length; i++) { const l = Math.hypot(...[0, 1, 2].map(k => m.path[i + 1][k] - m.path[i][k])); segL.push(l); tot += l; }
      for (const f of m.cartChecks) {
        let left = f * tot, i = 0;
        while (i < segL.length - 1 && left > segL[i]) { left -= segL[i]; i++; }
        const t = segL[i] ? left / segL[i] : 0, a = m.path[i], b = m.path[i + 1] || a;
        const ring = add(new THREE.Mesh(CART_RING, new THREE.MeshBasicMaterial({ color: 0x3fff8a })));
        ring.position.set(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t + 1, a[2] + (b[2] - a[2]) * t);
        ring.rotation.y = Math.atan2(b[0] - a[0], b[2] - a[2]);
        if (sh.labels) { const s = add(label("CHECK " + Math.round(f * 100) + "%", "#3fff8a", 0.6)); s.position.copy(ring.position).y += 1.3; }
      }
    }
    // spawns and bot starts ("pawns"): two hull meshes (unselected / selected opacity), outlines, arrows, bot rings
    if (sh.spawns) m.spawns.forEach((s, i) => this.pawns.push({ k: "spawn", i, pos: s.pos, yaw: s.yaw || 0, team: s.team }));
    (d.sidecar.botStarts || []).forEach((b, i) => this.pawns.push({ k: "botstart", i, pos: b.pos, yaw: b.yaw || 0, team: b.team || "any", bot: true }));
    const n = this.pawns.length;
    if (n) {
      const hm = (op) => instanced(HULL, new THREE.MeshBasicMaterial({ transparent: true, opacity: op, depthWrite: false }), n);
      this.hullN = add(hm(0.22)); this.hullS = add(hm(0.45)); this.hullN.renderOrder = this.hullS.renderOrder = 6;
      this.arrows = add(instanced(ARROW, new THREE.MeshBasicMaterial(), n));
      this.pawnLines = add(lineSet(n, false));
      const P = this.pawnLines.geometry.attributes.position.array, bots = this.pawns.filter(p => p.bot);
      if (bots.length) this.rings = add(instanced(RING, new THREE.MeshBasicMaterial(), bots.length));
      const placed = [];   // (staged spawns' labels: one per spot naming every team there, so stacked spawns don't print over each other)
      this.pawns.forEach((p, j) => {
        p.mat = new THREE.Matrix4().compose(tmpV.fromArray(p.pos), tmpQ.setFromAxisAngle(Y, p.yaw), tmpS.set(1, 1, 1));
        p.color = new THREE.Color(teamColor(p.team));
        this.arrows.setMatrixAt(j, p.mat);
        for (let q = 0; q < 24; q++) { tmpV.fromArray(HULL_EDGES, q * 3).applyMatrix4(p.mat); tmpV.toArray(P, (j * 24 + q) * 3); }
        if (p.bot) { const r = bots.indexOf(p); this.rings.setMatrixAt(r, p.mat); this.rings.setColorAt(r, BOT_RING); }
        if (!sh.labels) return;
        if (p.bot) { const s = add(label("BOT " + (p.i + 1), "#9cf", 0.5)); s.position.set(p.pos[0], p.pos[1] + 2.3, p.pos[2]); return; }
        if (!/^(att|def)\d$/.test(p.team)) return;
        const near = placed.find(o => Math.hypot(o.pos[0] - p.pos[0], o.pos[2] - p.pos[2]) < 3 && Math.abs(o.pos[1] - p.pos[1]) < 2);
        if (near) { if (!near.teams.includes(p.team)) near.teams.push(p.team); } else placed.push({ teams: [p.team], pos: p.pos, color: p.color });
      });
      for (const o of placed) {
        const s = add(label(o.teams.sort().map(t => t.toUpperCase()).join(" "), "#" + o.color.getHexString(), 0.4));
        s.position.set(o.pos[0], o.pos[1] + 2.15, o.pos[2]);
      }
      if (this.rings) this.rings.instanceMatrix.needsUpdate = true;
      this.arrows.instanceMatrix.needsUpdate = true;
    }
    if (sh.lights && m.lights.length) {
      this.lightsM = add(instanced(BALL, new THREE.MeshBasicMaterial(), m.lights.length));
      m.lights.forEach((l, j) => this.lightsM.setMatrixAt(j, tmpM.compose(tmpV.fromArray(l.pos), tmpQ.identity(), tmpS.setScalar(0.25))));
      this.lightsM.instanceMatrix.needsUpdate = true;
    }
    this.restyle();
  }

  // the selection's look: writes colours / matrices in place (O(markers), no objects made)
  restyle() {
    const v = this.view, d = v.doc, m = d.map, sh = v.show;
    if (!m) return;
    for (const z of this.zones) {
      const s = d.isSel(z.k, z.i);
      z.mesh.material.opacity = s ? 0.28 : 0.13;
      z.edges.material.color.set(s ? SEL : z.color);
    }
    if (this.groupLines) {
      const gray = tmpC.setHex(0x8a93a3);
      m.groups.forEach((g, j) => { const s = d.isSel("group", j); colorRange(this.groupLines, j, s ? SEL : gray, s ? 1 : sh.generated ? 0.5 : 0); });
    }
    if (this.objLines) (d.sidecar.objects || []).forEach((o, j) => { const s = d.isSel("object", j); colorRange(this.objLines, j, s ? SEL : tmpC.setHex(0x5fd0c0), s ? 1 : 0.55); });
    if (this.pathM) {
      m.path.forEach((p, j) => {
        const s = d.isSel("path", j);
        this.pathM.setMatrixAt(j, tmpM.compose(tmpV.set(p[0], p[1] + 0.15, p[2]), tmpQ.identity(), tmpS.setScalar(s ? 0.35 : 0.25)));
        this.pathM.setColorAt(j, s ? WHITE : PATH);
      });
      this.pathM.instanceMatrix.needsUpdate = true; this.pathM.instanceColor.needsUpdate = true;
    }
    if (this.pawns.length) {
      let nN = 0, nS = 0;
      this.pawns.forEach((p, j) => {
        const s = d.isSel(p.k, p.i), mesh = s ? this.hullS : this.hullN, at = s ? nS++ : nN++;
        mesh.setMatrixAt(at, p.mat); mesh.setColorAt(at, p.color);
        this.arrows.setColorAt(j, s ? SEL : p.color);
        colorRange(this.pawnLines, j, s ? SEL : p.color);
      });
      for (const [mesh, cnt] of [[this.hullN, nN], [this.hullS, nS]]) { mesh.count = cnt; mesh.instanceMatrix.needsUpdate = true; if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true; }
      this.arrows.instanceColor.needsUpdate = true;
    }
    if (this.lightsM) {
      m.lights.forEach((l, j) => this.lightsM.setColorAt(j, d.isSel("light", j) ? WHITE : LIGHT));
      this.lightsM.instanceColor.needsUpdate = true;
    }
    // the selected lights' reach (few: made per selection)
    this.clearExtra();
    if (sh.lights) for (const i of d.selOf("light")) {
      const l = m.lights[i]; if (!l) continue;
      const r = new THREE.LineSegments(WIRE_BALL, new THREE.LineBasicMaterial({ color: 0xfff0a0, transparent: true, opacity: 0.25 }));
      r.position.fromArray(l.pos); r.scale.setScalar(l.radius); this.extra.add(r);
    }
    v.redraw();
  }
}
