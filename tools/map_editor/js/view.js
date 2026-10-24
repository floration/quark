// The 3D view: three.js scene with every box drawn as ONE instanced mesh (opaque) plus one for see-through
// materials (water, clip), shaded like the game (map palette, sun, floor tint, outlines, 2 m panels, fog); the
// gameplay markers; overlay layers other modules draw into; fly / orbit / top-down cameras. It renders on demand
// (an edit, a camera move) and continuously only while something animates.
import * as THREE from "../vendor/three.module.min.js";
import { segmentMesh, polylineSegs, label, dispose } from "./lines.js";

export const TEAM_COLORS = { a: 0x4f9cf5, b: 0xec5a4f, any: 0xe8e8e8, race: 0xf2d45a };
export function teamColor(team) {
  if (TEAM_COLORS[team] !== undefined) return TEAM_COLORS[team];
  if (/^att\d$/.test(team)) return [0x4f9cf5, 0x6fb4ff, 0x3a7dd0, 0x8cc4ff, 0x2f6ab0][+team[3] % 5];
  if (/^def\d$/.test(team)) return [0xec5a4f, 0xff7a6e, 0xc9443a, 0xff9a8f, 0xa83a30][+team[3] % 5];
  return 0xaaaaaa;
}
export const ZONE_COLORS = { site: 0xff6a3d, hill: 0xffd23f, point: 0xb57aff, checkpoint: 0x3fd8ff, finish: 0x3fff8a, ladder: 0x5fe35f, stairs: 0x9aa3b0 };
const DEFAULT_PAL = { base: [0.62, 0.62, 0.64], accent: [0.40, 0.40, 0.43], metal: [0.30, 0.33, 0.37], glow: [1.0, 0.55, 0.15], sky: [0.62, 0.66, 0.72] };
const TRANSPARENT = new Set([6, 9]);

const VS = `
attribute float aMat; attribute float aSel;
varying vec3 vWorld; varying vec3 vN; varying vec3 vLocal; varying vec3 vSize; varying float vMat; varying float vSel;
void main() {
  vSize = vec3(length(instanceMatrix[0].xyz), length(instanceMatrix[1].xyz), length(instanceMatrix[2].xyz));
  vLocal = position * vSize;
  vN = normal; vMat = aMat; vSel = aSel;
  vec4 w = modelMatrix * instanceMatrix * vec4(position, 1.0);
  vWorld = w.xyz;
  gl_Position = projectionMatrix * viewMatrix * w;
}`;
const FS = `
uniform vec3 uCol[10]; uniform vec3 uSun; uniform vec3 uFloor; uniform vec3 uFog; uniform vec3 uCam; uniform vec3 uSky;
uniform float uFogD; uniform float uAlpha; uniform float uOutline; uniform float uPanels;
varying vec3 vWorld; varying vec3 vN; varying vec3 vLocal; varying vec3 vSize; varying float vMat; varying float vSel;
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main() {
  int m = int(vMat + 0.5);
  vec3 c = uCol[m];
  vec3 an = abs(vN);
  bool unlit = m >= 4 && m < 8;
  if (!unlit) {
    if (uPanels > 0.5) {
      vec2 pp = an.y > 0.5 ? vWorld.xz * 0.25 : an.x > 0.5 ? vWorld.zy * 0.5 : vWorld.xy * 0.5;
      if (m == 3) pp = pp * vec2(2.0, 0.5);
      vec2 cell = floor(pp), f = fract(pp), w = fwidth(pp) * 1.5;
      float seam = min(smoothstep(0.0, w.x + 0.012, min(f.x, 1.0 - f.x)), smoothstep(0.0, w.y + 0.012, min(f.y, 1.0 - f.y)));
      c *= (0.93 + 0.1 * hash2(cell + floor(vWorld.y * 0.01))) * mix(0.86, 1.0, seam);
    }
    if (m == 0 && vN.y > 0.5) c = mix(c, uFloor, 0.55);
    float sun = max(dot(vN, uSun), 0.0);
    c *= 0.50 + 0.12 * (vN.y * 0.5 + 0.5) + 0.55 * sun;
  } else if (m == 5) {
    c = mix(c * (0.55 + 0.25 * vN.y + 0.15 * an.x), uFog, 0.45);
  } else if (m == 6) {
    c = mix(vec3(0.04, 0.16, 0.28), vec3(0.14, 0.40, 0.58), 1.0 - an.y * 0.6);
  }
  // outlines: darken near the box's edges, a constant width in pixels
  vec2 p, s;
  if (an.x > 0.5) { p = vLocal.zy; s = vSize.zy; } else if (an.y > 0.5) { p = vLocal.xz; s = vSize.xz; } else { p = vLocal.xy; s = vSize.xy; }
  vec2 d = min(p, s - p), fw = max(fwidth(p), vec2(1e-5));
  float e = min(smoothstep(0.0, fw.x * uOutline, d.x), smoothstep(0.0, fw.y * uOutline, d.y));
  float a = m == 9 ? uAlpha * 0.2 : uAlpha;
  if (vSel > 0.5) {
    vec3 hc = vSel > 2.5 ? vec3(1.0, 0.25, 0.25) : vSel > 1.5 ? vec3(0.55, 0.85, 1.0) : vec3(1.0, 0.72, 0.2);
    c = mix(c, hc, vSel > 1.5 ? 0.25 : 0.32);
    c = mix(hc, c, e);
    if (a < 1.0) a = max(a, 0.45);
  } else {
    c *= mix(0.42, 1.0, e);
  }
  if (!unlit || m == 5) { float dist = length(vWorld - uCam); c = mix(c, uFog, 1.0 - exp(-dist * uFogD)); }
  gl_FragColor = vec4(c, a);
}`;

export class View {
  constructor(canvas, doc) {
    this.canvas = canvas; this.doc = doc;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: "high-performance" });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
    this.renderer.autoClear = false;
    this.renderer.info.autoReset = false;
    this.scene = new THREE.Scene();
    this.overlay = new THREE.Scene();       // drawn after a depth clear: the gizmo
    this.persp = new THREE.PerspectiveCamera(75, 1, 0.05, 3000);
    this.ortho = new THREE.OrthographicCamera(-50, 50, 50, -50, -5000, 5000);
    this.camMode = "fly";
    this.cam = { pos: new THREE.Vector3(0, 10, 20), yaw: 0, pitch: -0.3, target: new THREE.Vector3(), dist: 30, topH: 60, speed: 12 };
    this.show = { clip: true, far: true, generated: true, zones: true, spawns: true, lights: true, grid: true, stairs: false,
                  panels: true, figuresAtSpawns: false, ladders: true, path: true, labels: true, continuous: false };
    this.layers = {};
    for (const n of ["markers", "selection", "test", "routes", "bots", "figures", "deco", "grid"]) this.layers[n] = this.addLayer(n);
    this.scene.add(new THREE.HemisphereLight(0xdde6ff, 0x404040, 1.6));
    this.sunLight = new THREE.DirectionalLight(0xffffff, 1.6); this.scene.add(this.sunLight); this.scene.add(this.sunLight.target);
    this.uniforms = {
      uCol: { value: Array.from({ length: 10 }, () => new THREE.Color()) }, uSun: { value: new THREE.Vector3(0, 1, 0) },
      uFloor: { value: new THREE.Color() }, uFog: { value: new THREE.Color() }, uSky: { value: new THREE.Color() }, uCam: { value: new THREE.Vector3() },
      uFogD: { value: 0.0025 }, uOutline: { value: 1.3 }, uPanels: { value: 1 },
    };
    const mk = (alpha) => new THREE.ShaderMaterial({ vertexShader: VS, fragmentShader: FS, transparent: alpha < 1, depthWrite: alpha >= 1,
      uniforms: { ...this.uniforms, uAlpha: { value: alpha } }, side: alpha < 1 ? THREE.DoubleSide : THREE.FrontSide, extensions: { derivatives: true } });
    this.matOpaque = mk(1); this.matTrans = mk(0.4);
    this.boxGeo = new THREE.BoxGeometry(1, 1, 1).translate(0.5, 0.5, 0.5);
    this.meshes = { opaque: null, trans: null };
    this.inst = { opaque: [], trans: [] };   // instance -> {k: 'b'|'g', i}
    this.brushInst = [];                      // brush i -> [which, slot]
    this.needs = true; this.anim = new Set(); this.frameTimes = [];
    this.keys = new Set(); this.lastT = performance.now();
    this.hover = null;
    this.resize();
    new ResizeObserver(() => { this.resize(); this.redraw(); }).observe(canvas.parentElement);
    this.loop = this.loop.bind(this);
    requestAnimationFrame(this.loop);
  }

  addLayer(name) { const g = new THREE.Group(); g.name = name; this.scene.add(g); return g; }
  clearLayer(name) { const g = this.layers[name]; for (const c of g.children.slice()) { g.remove(c); dispose(c); } this.redraw(); }
  redraw() { this.needs = true; }
  get camera() { return this.camMode === "top" ? this.ortho : this.persp; }

  resize() {
    const r = this.canvas.parentElement.getBoundingClientRect();
    this.w = Math.max(1, r.width); this.h = Math.max(1, r.height);
    this.renderer.setSize(this.w, this.h, false);
    this.persp.aspect = this.w / this.h; this.persp.updateProjectionMatrix();
    this.updateOrtho();
  }
  updateOrtho() {
    const a = this.w / this.h, h = this.cam.topH;
    Object.assign(this.ortho, { left: -h * a / 2, right: h * a / 2, top: h / 2, bottom: -h / 2 });
    this.ortho.updateProjectionMatrix();
  }

  // ---- the map
  setPalette(map) {
    const p = Object.assign({}, DEFAULT_PAL, map.palette || {});
    const C = this.uniforms.uCol.value;
    const lin = (c, k = 1) => new THREE.Color(c[0] * k, c[1] * k, c[2] * k);
    C[0].copy(lin(p.base)); C[1].copy(lin(p.accent)); C[2].copy(lin(p.base, 0.42)); C[3].copy(lin(p.metal));
    C[4].copy(lin(p.glow, 0.95)); C[5].copy(lin(p.metal, 0.9)); C[6].setRGB(0.1, 0.32, 0.5); C[7].setRGB(1.0, 0.45, 0.1);
    C[8].copy(lin(p.accent)); C[9].setRGB(0.75, 0.35, 1.0);
    this.uniforms.uFloor.value.copy(lin(p.metal.map(v => Math.min(1, v * 1.35))));
    this.uniforms.uFog.value.copy(lin(p.sky)); this.uniforms.uSky.value.copy(lin(p.sky));
    this.renderer.setClearColor(lin(p.sky));
    const s = map.sun || [0.4, 1, 0.25]; const l = Math.hypot(...s) || 1;
    this.uniforms.uSun.value.set(s[0] / l, s[1] / l, s[2] / l);
    this.sunLight.position.set(s[0] * 100, s[1] * 100, s[2] * 100);
    this.matColors = { pal: p, hex: C.map(c => "#" + c.getHexString()) };
  }

  rebuildBrushes() {
    const m = this.doc.map; if (!m) return;
    const sh = this.show;
    const vis = (b) => !(b.mat === 9 && !sh.clip) && !(b.mat === 5 && !sh.far);
    const lists = { opaque: [], trans: [] };
    this.brushInst = new Array(m.brushes.length);
    m.brushes.forEach((b, i) => { if (!vis(b)) return; const w = TRANSPARENT.has(b.mat) ? "trans" : "opaque"; this.brushInst[i] = [w, lists[w].length]; lists[w].push({ k: "b", i, b }); });
    if (sh.generated) m.generated.forEach((b, i) => { if (!vis(b)) return; const w = TRANSPARENT.has(b.mat) ? "trans" : "opaque"; lists[w].push({ k: "g", i, b }); });
    for (const w of ["opaque", "trans"]) {
      const L = lists[w];
      let mesh = this.meshes[w];
      if (!mesh || mesh.userData.cap < L.length) {
        if (mesh) { this.scene.remove(mesh); mesh.dispose(); }
        const cap = Math.max(64, Math.ceil(L.length * 1.5));
        mesh = new THREE.InstancedMesh(this.boxGeo, w === "opaque" ? this.matOpaque : this.matTrans, cap);
        mesh.userData.cap = cap; mesh.frustumCulled = false; mesh.renderOrder = w === "opaque" ? 0 : 5;
        mesh.geometry = this.boxGeo.clone();
        mesh.geometry.setAttribute("aMat", new THREE.InstancedBufferAttribute(new Float32Array(cap), 1));
        mesh.geometry.setAttribute("aSel", new THREE.InstancedBufferAttribute(new Float32Array(cap), 1));
        this.meshes[w] = mesh; this.scene.add(mesh);
      }
      const M = mesh.instanceMatrix.array, A = mesh.geometry.attributes.aMat.array;
      L.forEach((e, j) => this.writeInst(M, j, e.b) || (A[j] = e.b.mat));
      mesh.count = L.length;
      mesh.instanceMatrix.needsUpdate = true; mesh.geometry.attributes.aMat.needsUpdate = true;
      this.inst[w] = L;
    }
    this.updateSelectionTint();
    this.redraw();
  }
  writeInst(M, j, b) {
    const o = j * 16;
    const sx = Math.max(1e-4, b.max[0] - b.min[0]), sy = Math.max(1e-4, b.max[1] - b.min[1]), sz = Math.max(1e-4, b.max[2] - b.min[2]);
    M[o] = sx; M[o + 1] = 0; M[o + 2] = 0; M[o + 3] = 0; M[o + 4] = 0; M[o + 5] = sy; M[o + 6] = 0; M[o + 7] = 0;
    M[o + 8] = 0; M[o + 9] = 0; M[o + 10] = sz; M[o + 11] = 0; M[o + 12] = b.min[0]; M[o + 13] = b.min[1]; M[o + 14] = b.min[2]; M[o + 15] = 1;
  }
  // a live drag of some brushes: only their instances
  updateBrushes(indices) {
    const m = this.doc.map;
    for (const i of indices) {
      const r = this.brushInst[i]; if (!r) continue;
      const mesh = this.meshes[r[0]];
      this.writeInst(mesh.instanceMatrix.array, r[1], m.brushes[i]);
      mesh.instanceMatrix.needsUpdate = true;
    }
    this.redraw();
  }
  // highlight: selected brushes, a selected group's parts (1), test highlights (2: blue, 3: red)
  updateSelectionTint() {
    const d = this.doc;
    const selB = new Set(d.selOf("brush")), selG = new Set(d.selOf("group"));
    const hl = this.highlight || { brushes: new Set(), color: 2 };
    const part = d.sel.find(s => s.k === "group" && s.part !== undefined);
    for (const w of ["opaque", "trans"]) {
      const mesh = this.meshes[w]; if (!mesh) continue;
      const S = mesh.geometry.attributes.aSel.array;
      this.inst[w].forEach((e, j) => {
        let v = 0;
        if (e.k === "b") { if (selB.has(e.i)) v = 1; else if (hl.brushes.has(e.i)) v = hl.color; }
        else if (e.b.group !== undefined && selG.has(e.b.group)) v = part && part.i === e.b.group ? (this.partIndex(e) === part.part ? 1 : 2) : 1;
        S[j] = v;
      });
      mesh.geometry.attributes.aSel.needsUpdate = true;
    }
    this.redraw();
  }
  partIndex(e) {   // which part of its group a generated box is (they are made in order)
    const g = this.doc.map.generated; let n = 0;
    for (let k = e.i - 1; k >= 0 && g[k].group === e.b.group; k--) n++;
    return n;
  }

  rebuildMarkers() {
    const L = this.layers.markers;
    for (const c of L.children.slice()) { L.remove(c); dispose(c); }
    const m = this.doc.map; if (!m) return;
    const sh = this.show, d = this.doc;
    const zone = (b, color, sel, text) => {
      const sz = [0, 1, 2].map(k => Math.max(0.01, b.max[k] - b.min[k]));
      const g = new THREE.BoxGeometry(...sz);
      const mesh = new THREE.Mesh(g, new THREE.MeshBasicMaterial({ color, transparent: true, opacity: sel ? 0.28 : 0.13, depthWrite: false }));
      mesh.position.set((b.min[0] + b.max[0]) / 2, (b.min[1] + b.max[1]) / 2, (b.min[2] + b.max[2]) / 2);
      mesh.renderOrder = 6;
      const edges = new THREE.LineSegments(new THREE.EdgesGeometry(g), new THREE.LineBasicMaterial({ color: sel ? 0xffc040 : color }));
      mesh.add(edges);
      if (text && sh.labels) { const s = label(text, "#" + new THREE.Color(color).getHexString(), 0.9); s.position.set(0, sz[1] / 2 + 0.7, 0); mesh.add(s); }
      L.add(mesh);
    };
    if (sh.zones) {
      m.sites.forEach((b, i) => zone(b, ZONE_COLORS.site, d.isSel("site", i), "SITE " + String.fromCharCode(65 + i)));
      if (m.hill) zone(m.hill, ZONE_COLORS.hill, d.isSel("hill", 0), "HILL");
      m.points.forEach((b, i) => zone(b, ZONE_COLORS.point, d.isSel("point", i), "POINT " + (i + 1)));
      m.checkpoints.forEach((b, i) => zone(b, i === m.checkpoints.length - 1 ? ZONE_COLORS.finish : ZONE_COLORS.checkpoint, d.isSel("checkpoint", i), i === m.checkpoints.length - 1 ? "FINISH" : "CP " + (i + 1)));
    }
    if (sh.ladders) m.ladders.forEach((b, i) => zone(b, ZONE_COLORS.ladder, d.isSel("ladder", i), null));
    if (sh.stairs) m.stairs.forEach((s, i) => zone({ min: [Math.min(s.x0, s.x1), s.base, Math.min(s.z0, s.z1)], max: [Math.max(s.x0, s.x1), s.top, Math.max(s.z0, s.z1)] }, ZONE_COLORS.stairs, d.isSel("stairs", i), null));
    // groups: their box outlined (room / stairs / custom), brighter when selected
    m.groups.forEach((g, i) => {
      const sel = d.isSel("group", i);
      if (!sel && !sh.generated) return;
      const sz = [0, 1, 2].map(k => Math.max(0.01, g.max[k] - g.min[k]));
      const e = new THREE.LineSegments(new THREE.EdgesGeometry(new THREE.BoxGeometry(...sz)), new THREE.LineBasicMaterial({ color: sel ? 0xffc040 : 0x8a93a3, transparent: true, opacity: sel ? 1 : 0.5 }));
      e.position.set((g.min[0] + g.max[0]) / 2, (g.min[1] + g.max[1]) / 2, (g.min[2] + g.max[2]) / 2);
      L.add(e);
    });
    if (sh.path && m.path.length) {
      const pts = m.path.map(p => [p[0], p[1] + 0.15, p[2]]);
      L.add(segmentMesh(polylineSegs(pts, 0xffd23f), { width: 0.12 }));
      m.path.forEach((p, i) => {
        const s = new THREE.Mesh(new THREE.SphereGeometry(d.isSel("path", i) ? 0.35 : 0.25, 12, 8), new THREE.MeshBasicMaterial({ color: d.isSel("path", i) ? 0xffffff : 0xffd23f }));
        s.position.set(p[0], p[1] + 0.15, p[2]); L.add(s);
      });
      // cart checkpoints along the track
      const segL = []; let tot = 0;
      for (let i = 0; i + 1 < m.path.length; i++) { const l = Math.hypot(...[0, 1, 2].map(k => m.path[i + 1][k] - m.path[i][k])); segL.push(l); tot += l; }
      for (const f of m.cartChecks) {
        let left = f * tot, i = 0;
        while (i < segL.length - 1 && left > segL[i]) { left -= segL[i]; i++; }
        const t = segL[i] ? left / segL[i] : 0, a = m.path[i], b = m.path[i + 1] || a;
        const ring = new THREE.Mesh(new THREE.TorusGeometry(0.8, 0.08, 6, 24), new THREE.MeshBasicMaterial({ color: 0x3fff8a }));
        ring.position.set(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t + 1, a[2] + (b[2] - a[2]) * t);
        ring.rotation.y = Math.atan2(b[0] - a[0], b[2] - a[2]);
        L.add(ring);
        if (sh.labels) { const s = label("CHECK " + Math.round(f * 100) + "%", "#3fff8a", 0.6); s.position.copy(ring.position).y += 1.3; L.add(s); }
      }
    }
    if (sh.spawns) m.spawns.forEach((s, i) => L.add(this.spawnMarker(s, d.isSel("spawn", i))));
    if (sh.lights) m.lights.forEach((l, i) => {
      const sel = d.isSel("light", i);
      const s = new THREE.Mesh(new THREE.SphereGeometry(0.25, 12, 8), new THREE.MeshBasicMaterial({ color: sel ? 0xffffff : 0xfff0a0 }));
      s.position.fromArray(l.pos); L.add(s);
      if (sel) {
        const r = new THREE.LineSegments(new THREE.WireframeGeometry(new THREE.SphereGeometry(l.radius, 16, 8)), new THREE.LineBasicMaterial({ color: 0xfff0a0, transparent: true, opacity: 0.25 }));
        r.position.fromArray(l.pos); L.add(r);
      }
    });
    (d.sidecar.botStarts || []).forEach((b, i) => {
      const mk = this.spawnMarker({ pos: b.pos, yaw: b.yaw, team: b.team || "any" }, d.isSel("botstart", i), true);
      if (sh.labels) { const s = label("BOT " + (i + 1), "#9cf", 0.5); s.position.set(0, 2.3, 0); mk.add(s); }
      L.add(mk);
    });
    this.redraw();
  }
  spawnMarker(s, sel, bot = false) {
    const g = new THREE.Group();
    g.position.fromArray(s.pos); g.rotation.y = s.yaw || 0;
    const col = teamColor(s.team);
    const hull = new THREE.Mesh(new THREE.BoxGeometry(0.8, 1.8, 0.8).translate(0, 0.9, 0), new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: sel ? 0.45 : 0.22, depthWrite: false }));
    hull.renderOrder = 6; g.add(hull);
    g.add(new THREE.LineSegments(new THREE.EdgesGeometry(hull.geometry), new THREE.LineBasicMaterial({ color: sel ? 0xffc040 : col })));
    const arrow = new THREE.Mesh(new THREE.ConeGeometry(0.16, 0.5, 10).rotateX(-Math.PI / 2).translate(0, 1.6, -0.65), new THREE.MeshBasicMaterial({ color: sel ? 0xffc040 : col }));
    g.add(arrow);
    if (bot) { const ring = new THREE.Mesh(new THREE.TorusGeometry(0.55, 0.05, 6, 20).rotateX(Math.PI / 2).translate(0, 0.05, 0), new THREE.MeshBasicMaterial({ color: 0x9cf })); g.add(ring); }
    else if (this.show.labels && /^(att|def)\d$/.test(s.team)) { const l = label(s.team.toUpperCase(), "#" + new THREE.Color(col).getHexString(), 0.4); l.position.set(0, 2.15, 0); g.add(l); }
    return g;
  }

  // the grid: 1 m lines (and 8 m stronger ones) at height y, sized to the map
  rebuildGrid(y = 0) {
    this.clearLayer("grid");
    if (!this.show.grid || !this.doc.map) return;
    const bb = this.mapBounds();
    const cx = Math.round((bb.min[0] + bb.max[0]) / 16) * 8, cz = Math.round((bb.min[2] + bb.max[2]) / 16) * 8;
    const half = Math.min(600, Math.ceil(Math.max(bb.max[0] - bb.min[0], bb.max[2] - bb.min[2]) / 16 + 2) * 8);
    const g1 = new THREE.GridHelper(half * 2, half * 2, 0x000000, 0x000000);
    g1.material.transparent = true; g1.material.opacity = 0.13; g1.material.depthWrite = false;
    const g8 = new THREE.GridHelper(half * 2, half / 4, 0x000000, 0x000000);
    g8.material.transparent = true; g8.material.opacity = 0.3; g8.material.depthWrite = false;
    for (const g of [g1, g8]) { g.position.set(cx, y + 0.003, cz); g.renderOrder = 4; this.layers.grid.add(g); }
    this.gridY = y;
  }

  mapBounds() {
    const m = this.doc.map;
    const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (const b of m.brushes) { if (b.mat >= 5 && b.mat !== 8) continue; for (let k = 0; k < 3; k++) { if (b.min[k] < mn[k]) mn[k] = b.min[k]; if (b.max[k] > mx[k]) mx[k] = b.max[k]; } }
    if (!isFinite(mn[0])) return { min: [-20, 0, -20], max: [20, 10, 20] };
    return { min: mn, max: mx };
  }

  // ---- cameras
  fwd() { const { yaw, pitch } = this.cam; return new THREE.Vector3(-Math.sin(yaw) * Math.cos(pitch), Math.sin(pitch), -Math.cos(yaw) * Math.cos(pitch)); }
  setCamMode(mode) {
    if (mode === this.camMode) return;
    const c = this.cam;
    if (mode === "orbit") { c.target.copy(c.pos).addScaledVector(this.fwd(), c.dist); }
    if (this.camMode === "orbit" && mode === "fly") { /* keep the eye where it is */ }
    if (mode === "top") { c.target.set(c.pos.x, 0, c.pos.z); if (this.camMode === "orbit") c.target.copy(c.target); }
    if (this.camMode === "top" && mode !== "top") { c.pos.set(c.target.x, c.pos.y, c.target.z + 20); c.pitch = -0.6; c.yaw = 0; if (mode === "orbit") c.target.set(c.target.x, 0, c.target.z); }
    this.camMode = mode; this.updateOrtho(); this.redraw();
  }
  frame(min, max, keepDir = true) {
    const c = this.cam;
    const ctr = new THREE.Vector3((min[0] + max[0]) / 2, (min[1] + max[1]) / 2, (min[2] + max[2]) / 2);
    const r = Math.max(2, Math.hypot(max[0] - min[0], max[1] - min[1], max[2] - min[2]) / 2);
    if (this.camMode === "top") { c.target.set(ctr.x, 0, ctr.z); c.topH = Math.max(8, r * 2.4); this.updateOrtho(); }
    else {
      if (!keepDir) { c.yaw = Math.PI * 0.25; c.pitch = -0.6; }
      const dist = r / Math.tan(THREE.MathUtils.degToRad(this.persp.fov / 2)) * 1.1;
      c.dist = dist; c.target.copy(ctr);
      c.pos.copy(ctr).addScaledVector(this.fwd(), -dist);
    }
    this.redraw();
  }
  frameMap() { const b = this.mapBounds(); this.frame(b.min, b.max, false); }
  applyCamera() {
    const c = this.cam;
    if (this.camMode === "orbit") c.pos.copy(c.target).addScaledVector(this.fwd(), -c.dist);
    if (this.camMode === "top") {
      this.ortho.position.set(c.target.x, 2000, c.target.z); this.ortho.up.set(0, 0, -1); this.ortho.lookAt(c.target.x, 0, c.target.z);
      this.uniforms.uCam.value.set(c.target.x, 2000, c.target.z);
      this.uniforms.uFogD.value = 0;
    } else {
      this.persp.position.copy(c.pos); this.persp.lookAt(c.pos.clone().add(this.fwd()));
      this.uniforms.uCam.value.copy(c.pos);
      this.uniforms.uFogD.value = this.fogD ?? 0.0025;
    }
  }
  // a screen point -> ray {o, d} (arrays)
  ray(cx, cy) {
    const r = this.canvas.getBoundingClientRect();
    const x = ((cx - r.left) / r.width) * 2 - 1, y = -((cy - r.top) / r.height) * 2 + 1;
    this.applyCamera(); const cam = this.camera; cam.updateMatrixWorld();
    const rc = new THREE.Raycaster(); rc.setFromCamera(new THREE.Vector2(x, y), cam);
    return { o: rc.ray.origin.toArray(), d: rc.ray.direction.toArray() };
  }
  project(p) {
    const v = new THREE.Vector3(...p).project(this.camera);
    return { x: (v.x + 1) / 2 * this.w, y: (1 - v.y) / 2 * this.h, behind: v.z > 1 };
  }
  pxPerMetre(at) {
    if (this.camMode === "top") return this.h / this.cam.topH;
    const d = Math.max(0.5, at ? this.cam.pos.distanceTo(new THREE.Vector3(...at)) : this.cam.dist);
    return (this.h / 2) / (Math.tan(THREE.MathUtils.degToRad(this.persp.fov / 2)) * d);
  }

  // fly keys: WASD + Space / C (E / Q), Shift fast
  step(dt) {
    if (this.camMode !== "fly" || !this.keys.size) return false;
    const k = this.keys, f = this.fwd(), right = new THREE.Vector3(-f.z, 0, f.x).normalize();
    const v = new THREE.Vector3();
    if (k.has("KeyW")) v.add(f); if (k.has("KeyS")) v.sub(f);
    if (k.has("KeyD")) v.add(right); if (k.has("KeyA")) v.sub(right);
    if (k.has("Space") || k.has("KeyE")) v.y += 1; if (k.has("KeyC") || k.has("KeyQ")) v.y -= 1;
    if (v.lengthSq() === 0) return false;
    const sp = this.cam.speed * (k.has("ShiftLeft") || k.has("ShiftRight") ? 3 : 1);
    this.cam.pos.addScaledVector(v.normalize(), sp * dt);
    return true;
  }

  loop(t) {
    const dt = Math.min(0.1, (t - this.lastT) / 1000); this.lastT = t;
    let moving = this.step(dt);
    for (const f of this.anim) if (f(dt, t) !== false) moving = true;
    if (this.needs || moving || this.show.continuous) {
      this.needs = false;
      const t0 = performance.now();
      this.applyCamera();
      this.onBeforeRender && this.onBeforeRender();
      this.scaleLabels();
      const r = this.renderer;
      r.info.reset();
      r.clear();
      r.render(this.scene, this.camera);
      if (this.overlay.children.length) { r.clearDepth(); r.render(this.overlay, this.camera); }
      this.frameTimes.push({ t, ms: performance.now() - t0 });
      this.onAfterRender && this.onAfterRender();
    }
    requestAnimationFrame(this.loop);
  }
  // labels keep their screen size: the perspective camera does it itself (sizeAttenuation off); top-down scales them
  scaleLabels() {
    const k = this.camMode === "top" ? this.cam.topH / 1.535 : 1;
    if (k === 1 && this.labelK === 1) return;
    this.labelK = k;
    this.scene.traverse(o => { if (o.userData.label && o.userData.base) o.scale.set(o.userData.base[0] * k, o.userData.base[1] * k, 1); });
  }
  // frame stats over the last 2 s of drawn frames: average fps, 1 % and 0.1 % lows (the game's F3 measure)
  stats() {
    const now = performance.now();
    this.frameTimes = this.frameTimes.filter(f => now - f.t < 2000);
    const F = this.frameTimes; if (F.length < 3) return null;
    const gaps = []; for (let i = 1; i < F.length; i++) gaps.push(F[i].t - F[i - 1].t);
    const s = gaps.slice().sort((a, b) => b - a);
    const avg = gaps.reduce((a, b) => a + b, 0) / gaps.length;
    const low = (p) => { const n = Math.max(1, Math.floor(s.length * p)); return s.slice(0, n).reduce((a, b) => a + b, 0) / n; };
    const cpu = F.reduce((a, f) => a + f.ms, 0) / F.length;
    return { fps: 1000 / avg, low1: 1000 / low(0.01), low01: 1000 / low(0.001), cpu, calls: this.renderer.info.render.calls, tris: this.renderer.info.render.triangles };
  }
}
