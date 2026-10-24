// Thick lines in world space (WebGL draws 1 px lines only): one instanced thin box per segment, coloured per segment.
import * as THREE from "../vendor/three.module.min.js";

const unit = new THREE.BoxGeometry(1, 1, 1).translate(0, 0, 0.5);   // (along +z from the origin, length 1)
const tmpM = new THREE.Matrix4(), tmpQ = new THREE.Quaternion(), tmpS = new THREE.Vector3(), tmpA = new THREE.Vector3(), tmpB = new THREE.Vector3();
const zAxis = new THREE.Vector3(0, 0, 1), tmpD = new THREE.Vector3(), tmpC = new THREE.Color();

// segs: [[a, b, color(THREE.Color | [r,g,b] | hex)], ...] -> one InstancedMesh (opts: width, opacity, depthTest)
export function segmentMesh(segs, opts = {}) {
  const w = opts.width ?? 0.08;
  const mat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: (opts.opacity ?? 1) < 1, opacity: opts.opacity ?? 1, depthTest: opts.depthTest ?? true, depthWrite: (opts.opacity ?? 1) >= 1 });
  const mesh = new THREE.InstancedMesh(unit, mat, Math.max(1, segs.length));
  mesh.count = segs.length;
  mesh.frustumCulled = false;
  if (opts.renderOrder) mesh.renderOrder = opts.renderOrder;
  segs.forEach(([a, b, c], i) => {
    tmpA.fromArray(a); tmpB.fromArray(b);
    tmpD.subVectors(tmpB, tmpA);
    const len = tmpD.length();
    if (len < 1e-6) tmpQ.identity(); else tmpQ.setFromUnitVectors(zAxis, tmpD.multiplyScalar(1 / len));
    tmpS.set(w, w, Math.max(len, 1e-4));
    tmpM.compose(tmpA, tmpQ, tmpS);
    mesh.setMatrixAt(i, tmpM);
    mesh.setColorAt(i, c instanceof THREE.Color ? c : Array.isArray(c) ? tmpC.setRGB(c[0], c[1], c[2]) : tmpC.set(c ?? 0xffffff));
  });
  mesh.instanceMatrix.needsUpdate = true;
  if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
  return mesh;
}

// a polyline's segments, coloured by a function of the fraction along it (0..1) or one colour
export function polylineSegs(pts, color, out = []) {
  for (let i = 0; i + 1 < pts.length; i++) out.push([pts[i], pts[i + 1], typeof color === "function" ? color(i / Math.max(1, pts.length - 2), i) : color]);
  return out;
}

// green (fast) -> yellow -> red (slow)
export function heat(f) {
  f = Math.max(0, Math.min(1, f));
  return f < 0.5 ? new THREE.Color().setRGB(0.2 + 1.6 * f, 0.85, 0.25) : new THREE.Color().setRGB(1.0, 0.85 - 1.3 * (f - 0.5), 0.25 - 0.3 * (f - 0.5));
}

export function dispose(obj) {
  obj.traverse(o => { if (o.geometry && o.geometry !== unit && !o.geometry.userData.shared) o.geometry.dispose(); if (o.material) [].concat(o.material).forEach(m => { if (m.userData.shared) return; if (m.map && !m.userData.sharedMap) m.map.dispose(); m.dispose(); }); });
}

const labelCache = new Map();
// a text sprite (cached per text + colour), the same size on screen at any distance (size 1: about 22 px), drawn
// over everything when onTop
export function label(text, color = "#ffffff", size = 0.8, onTop = true) {
  const key = text + "|" + color;
  let tex = labelCache.get(key);
  if (!tex) {
    const c = document.createElement("canvas"), g = c.getContext("2d");
    g.font = "bold 44px system-ui, sans-serif";
    const w = Math.ceil(g.measureText(text).width) + 20;
    c.width = w; c.height = 60;
    g.font = "bold 44px system-ui, sans-serif";
    g.fillStyle = "#000000b0"; g.beginPath(); g.roundRect(0, 0, w, 60, 10); g.fill();
    g.fillStyle = color; g.textBaseline = "middle"; g.fillText(text, 10, 32);
    tex = new THREE.CanvasTexture(c); tex.userData.aspect = w / 60; tex.colorSpace = THREE.LinearSRGBColorSpace;
    labelCache.set(key, tex);
    if (labelCache.size > 600) { const [k0, t0] = labelCache.entries().next().value; labelCache.delete(k0); t0.dispose(); }
  }
  const m = new THREE.SpriteMaterial({ map: tex, depthTest: !onTop, transparent: true, sizeAttenuation: false });
  m.userData.sharedMap = true;
  const s = new THREE.Sprite(m);
  const k = 0.042 * size;   // (sizeAttenuation off: a fraction of the view height at 75 degrees)
  s.scale.set(k * tex.userData.aspect, k, 1);
  s.userData.base = [k * tex.userData.aspect, k];
  s.renderOrder = 10;
  s.userData.label = true;
  return s;
}
