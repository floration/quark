// The 3D view: three.js scene with every box drawn as ONE instanced mesh (opaque) plus one for see-through
// materials (water, clip), shaded like the game (map palette, sun, floor tint, outlines, 2 m panels, fog); the
// gameplay markers; overlay layers other modules draw into; fly / orbit / top-down cameras. It renders on demand
// (an edit, a camera move) and continuously only while something animates.
import * as THREE from "../vendor/three.module.min.js";
import { dispose } from "./lines.js";
import { Markers } from "./markers.js";
export { TEAM_COLORS, teamColor, ZONE_COLORS } from "./markers.js";
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
uniform vec3 uCol[12]; uniform vec3 uSun; uniform vec3 uFloor; uniform vec3 uFog; uniform vec3 uCam; uniform vec3 uSky;
uniform float uFogD; uniform float uAlpha; uniform float uOutline; uniform float uPanels;
varying vec3 vWorld; varying vec3 vN; varying vec3 vLocal; varying vec3 vSize; varying float vMat; varying float vSel;
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main() {
  int m = int(vMat + 0.5);
  vec3 c = uCol[m];
  vec3 an = abs(vN);
  bool unlit = (m >= 4 && m < 8) || m == 11;
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
  } else if (m == 11) {
    c = mix(c * 0.35, c, max(vN.y, 0.0));   // acid: glowing green on top, darker at the sides (as in the game)
  }
  // outlines: darken near the box's edges, a constant width in pixels
  vec2 p, s;
  if (an.x > 0.5) { p = vLocal.zy; s = vSize.zy; } else if (an.y > 0.5) { p = vLocal.xz; s = vSize.xz; } else { p = vLocal.xy; s = vSize.xy; }
  vec2 d = min(p, s - p), fw = max(fwidth(p), vec2(1e-5));
  float e = min(smoothstep(0.0, fw.x * uOutline, d.x), smoothstep(0.0, fw.y * uOutline, d.y));
  // clip boxes only from outside: a map-sized one (Midtown's) would tint the whole view from inside
  if (m == 9 && !gl_FrontFacing) discard;
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
    this.markers = new Markers(this);
    this.scene.add(new THREE.HemisphereLight(0xdde6ff, 0x404040, 1.6));
    this.sunLight = new THREE.DirectionalLight(0xffffff, 1.6); this.scene.add(this.sunLight); this.scene.add(this.sunLight.target);
    this.uniforms = {
      uCol: { value: Array.from({ length: 12 }, () => new THREE.Color()) }, uSun: { value: new THREE.Vector3(0, 1, 0) },
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
    C[8].copy(lin(p.accent)); C[9].setRGB(0.75, 0.35, 1.0); C[10].setRGB(0.3, 0.33, 0.42); C[11].setRGB(0.36, 1.0, 0.23);
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
        if (mesh) { this.scene.remove(mesh); mesh.geometry.dispose(); mesh.dispose(); }
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
    this.updateSelectionTint(true);
    this.redraw();
  }
  writeInst(M, j, b) {
    const o = j * 16;
    const sx = Math.max(1e-4, b.max[0] - b.min[0]), sy = Math.max(1e-4, b.max[1] - b.min[1]), sz = Math.max(1e-4, b.max[2] - b.min[2]);
    M[o] = sx; M[o + 1] = 0; M[o + 2] = 0; M[o + 3] = 0; M[o + 4] = 0; M[o + 5] = sy; M[o + 6] = 0; M[o + 7] = 0;
    M[o + 8] = 0; M[o + 9] = 0; M[o + 10] = sz; M[o + 11] = 0; M[o + 12] = b.min[0]; M[o + 13] = b.min[1]; M[o + 14] = b.min[2]; M[o + 15] = 1;
  }
  // some brushes changed in place (a drag, a nudge, a material): only their instances. false: one of them changed
  // list (opaque / see-through) or visibility, the caller rebuilds
  updateBrushes(indices) {
    const m = this.doc.map, sh = this.show;
    for (const i of indices) {
      const r = this.brushInst[i], b = m.brushes[i]; if (!b) return false;
      const vis = !(b.mat === 9 && !sh.clip) && !(b.mat === 5 && !sh.far);
      if (!vis) { if (r) return false; continue; }
      if (!r || r[0] !== (TRANSPARENT.has(b.mat) ? "trans" : "opaque")) return false;
      const mesh = this.meshes[r[0]];
      this.writeInst(mesh.instanceMatrix.array, r[1], b);
      mesh.instanceMatrix.needsUpdate = true;
      const A = mesh.geometry.attributes.aMat;
      if (A.array[r[1]] !== b.mat) { A.array[r[1]] = b.mat; A.needsUpdate = true; }
    }
    this.redraw();
    return true;
  }
  // highlight: selected brushes, a selected group's parts (1), test highlights (2: blue, 3: red). Only the brushes
  // whose selection changed are rewritten, unless groups or a highlight are involved (then every instance)
  updateSelectionTint(full = false) {
    const d = this.doc;
    const selB = d.selIndex.brush || new Set(), selG = d.selIndex.group || new Set(), selO = d.selIndex.object || new Set();
    const hl = this.highlight || null;
    const prev = this.tinted;
    if (!full && prev && prev.hl === hl && !selG.size && !selO.size && !prev.groups) {
      for (const set of [prev.brushes, selB]) for (const i of set) {
        if (prev.brushes.has(i) === selB.has(i)) continue;
        const r = this.brushInst[i]; if (!r) continue;
        const mesh = this.meshes[r[0]], S = mesh.geometry.attributes.aSel;
        S.array[r[1]] = selB.has(i) ? 1 : hl && hl.brushes.has(i) ? hl.color : 0; S.needsUpdate = true;
      }
      this.tinted = { brushes: new Set(selB), hl, groups: false };
      this.redraw(); return;
    }
    const part = d.sel.find(s => s.k === "group" && s.part !== undefined);
    for (const w of ["opaque", "trans"]) {
      const mesh = this.meshes[w]; if (!mesh) continue;
      const S = mesh.geometry.attributes.aSel.array;
      this.inst[w].forEach((e, j) => {
        let v = 0;
        if (e.k === "b") { if (selB.has(e.i)) v = 1; else if (hl && hl.brushes.has(e.i)) v = hl.color; }
        else if (e.b.group !== undefined && selG.has(e.b.group)) v = part && part.i === e.b.group ? (this.partIndex(e) === part.part ? 1 : 2) : 1;
        else if (e.b.object !== undefined && selO.has(e.b.object)) v = 1;
        S[j] = v;
      });
      mesh.geometry.attributes.aSel.needsUpdate = true;
    }
    this.tinted = { brushes: new Set(selB), hl, groups: selG.size > 0 || selO.size > 0 };
    this.redraw();
  }
  partIndex(e) {   // which part of its group a generated box is (they are made in order)
    const g = this.doc.map.generated; let n = 0;
    for (let k = e.i - 1; k >= 0 && g[k].group === e.b.group; k--) n++;
    return n;
  }

  rebuildMarkers() { this.markers.build(); }
  restyleMarkers() { this.markers.restyle(); }

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
    for (const b of m.brushes) { if (b.mat >= 5 && b.mat !== 8 && b.mat !== 10) continue; for (let k = 0; k < 3; k++) { if (b.min[k] < mn[k]) mn[k] = b.min[k]; if (b.max[k] > mx[k]) mx[k] = b.max[k]; } }
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
      this.declutterLabels();
      const r = this.renderer;
      r.info.reset();
      r.clear();
      r.render(this.scene, this.camera);
      if (this.overlay.children.some(c => c.visible)) { r.clearDepth(); r.render(this.overlay, this.camera); }
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
  // Labels drawn on top keep their screen size, so far ones land on near ones (Midtown's POINT A under POINT B, a stage's
  // ATT / DEF labels in a pile). Each drawn frame, in screen space: objective labels (userData.prio 2, then 1) move up
  // a label's height at a time until they are clear (up to 3 steps); the rest (spawn stages) hide where they would
  // overlap one already placed. Nearer first. A few hundred labels: well under a millisecond.
  declutterLabels() {
    const cam = this.camera, P = cam.projectionMatrix.elements, W = this.w, H = this.h;
    const list = [];
    for (const name of ["markers", "routes"]) this.layers[name].traverse(o => { if (o.isSprite && o.userData.label) list.push(o); });
    if (!list.length || list.length > 1500) return;
    cam.updateMatrixWorld();   // (the camera as this frame draws it, not as the last one did)
    const v = this.tmpLabelV || (this.tmpLabelV = new THREE.Vector3()), inv = cam.matrixWorldInverse, items = [];
    for (const s of list) {
      if (s.userData.c0 === undefined) s.userData.c0 = s.center.y;
      s.center.y = s.userData.c0;
      v.copy(s.position).applyMatrix4(inv);
      const depth = -v.z;
      if (depth <= 0) { s.visible = true; continue; }
      v.copy(s.position).project(cam);
      const w = s.scale.x * P[0] * W / 2, h = s.scale.y * P[5] * H / 2;   // (sizeAttenuation off: the scale is in clip space)
      const x = (v.x + 1) / 2 * W - s.center.x * w, y = (1 - v.y) / 2 * H - (1 - s.center.y) * h;
      items.push({ s, x, y, w, h, depth, prio: s.userData.prio || 0 });
    }
    items.sort((a, b) => b.prio - a.prio || a.depth - b.depth);
    const placed = [], hit = (r) => placed.some(q => r.x < q.x + q.w + 2 && q.x < r.x + r.w + 2 && r.y < q.y + q.h + 1 && q.y < r.y + r.h + 1);
    for (const it of items) {
      let r = it, steps = 0;
      while (hit(r) && it.prio && steps < 3) { steps++; r = { ...it, y: it.y - steps * (it.h + 2) }; }
      const ok = !hit(r);
      it.s.visible = ok || it.prio > 0;
      if (steps && ok) it.s.center.y = it.s.userData.c0 - steps * (1 + 2 / it.h);
      if (ok) placed.push(r);
    }
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
