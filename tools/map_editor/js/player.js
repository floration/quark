// Player size: life-size figures. The visual model is one of the game's player models (models/player/<NAME>.gltf
// from quark_tests exportassets, about 2 m tall) or, without them, a plain body; the hitbox is the game's fixed hull
// (0.8 x 0.8 x 1.8 m, crouched 1.2 m) with its head box (top 20 % of the height, 45 % of the width) and eye height.
import * as THREE from "../vendor/three.module.min.js";
import { el, fill, check, select } from "./util.js";
import { teamColor } from "./view.js";

const HULL = { w: 0.8, h: 1.8, ch: 1.2, eye: 1.6, ceye: 1.05 };

// A small glTF 2.0 reader for what exportassets writes: one mesh, triangle primitives with POSITION / NORMAL /
// COLOR_0 / indices, one base64 buffer.
export function parseGltf(json) {
  const g = typeof json === "string" ? JSON.parse(json) : json;
  const buf = g.buffers.map(b => {
    const m = /^data:.*?;base64,(.*)$/.exec(b.uri || ""); if (!m) throw new Error("glTF: only embedded buffers");
    const s = atob(m[1]); const u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i); return u.buffer;
  });
  const acc = (i) => {
    const a = g.accessors[i], v = g.bufferViews[a.bufferView];
    const n = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4 }[a.type];
    const T = a.componentType === 5126 ? Float32Array : a.componentType === 5125 ? Uint32Array : a.componentType === 5123 ? Uint16Array : Uint8Array;
    return new T(buf[v.buffer], (v.byteOffset || 0) + (a.byteOffset || 0), a.count * n);
  };
  const group = new THREE.Group();
  for (const mesh of g.meshes) for (const p of mesh.primitives) {
    const geo = new THREE.BufferGeometry();
    geo.setAttribute("position", new THREE.BufferAttribute(acc(p.attributes.POSITION).slice(), 3));
    if (p.attributes.NORMAL !== undefined) geo.setAttribute("normal", new THREE.BufferAttribute(acc(p.attributes.NORMAL).slice(), 3));
    if (p.attributes.COLOR_0 !== undefined) geo.setAttribute("color", new THREE.BufferAttribute(acc(p.attributes.COLOR_0).slice(), 3));
    if (p.indices !== undefined) geo.setIndex(new THREE.BufferAttribute(acc(p.indices).slice(), 1));
    const mat = g.materials && g.materials[p.material];
    const alpha = mat && mat.pbrMetallicRoughness && mat.pbrMetallicRoughness.baseColorFactor ? mat.pbrMetallicRoughness.baseColorFactor[3] : 1;
    group.add(new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ vertexColors: !!geo.attributes.color, transparent: alpha < 1, opacity: alpha })));
  }
  return group;
}

export class Player {
  constructor(app) {
    this.app = app; this.doc = app.doc; this.view = app.view;
    this.models = (app.api.info && app.api.info.playerModels) || [];
    this.cache = {};
    this.opts = { model: this.models.includes("STANDARD") ? "STANDARD" : (this.models[0] || "hull"), crouch: false, hitbox: true, atSpawns: false, eye: true };
    this.panel = document.getElementById("rp-player");
    this.doc.on("map", (keys, live) => { if (!keys || keys.some(k => k === "spawns" || k === "sc.figures" || k === "*")) this.redraw(); });
    this.doc.on("open", () => this.redraw());
    this.doc.on("selection", () => this.redraw());
    this.render();
  }
  async model(name) {
    if (name === "hull" || !this.models.includes(name)) return null;
    if (!this.cache[name]) this.cache[name] = fetch("models/player/" + encodeURIComponent(name) + ".gltf").then(r => r.json()).then(parseGltf).catch(e => { console.warn("model", name, e); return null; });
    return this.cache[name];
  }
  drop(p, yaw) {
    const d = this.doc; let idx = -1;
    d.edit("add figure", ["sc.figures"], (m, sc) => { sc.figures.push({ pos: p.map(v => Math.round(v * 1000) / 1000), yaw: yaw + Math.PI, model: this.opts.model, crouch: this.opts.crouch }); idx = sc.figures.length - 1; });
    d.select([{ k: "figure", i: idx }]);
  }
  render() {
    const o = this.opts;
    fill(this.panel, 
      el("h3", {}, "Player size"),
      el("p", {}, "The Player tool drops a life-size figure where you click. Hitbox: 0.8 x 0.8 x 1.8 m (crouched 1.2 m), eyes at 1.6 m (crouched 1.05 m); the head box is the top 20 % of the height, 45 % of the width. Models are cosmetic (about 2 m tall): every one has the same hitbox."),
      el("div", { class: "form" },
        el("label", {}, "model"), select(["hull", ...this.models], o.model, v => { o.model = v; this.redraw(); }),
        el("label", {}, ""), check("crouched", o.crouch, v => { o.crouch = v; this.redraw(); }),
        el("label", {}, ""), check("hitbox + head box", o.hitbox, v => { o.hitbox = v; this.redraw(); }),
        el("label", {}, ""), check("eye height marker", o.eye, v => { o.eye = v; this.redraw(); }),
        el("label", {}, ""), check("a figure at every spawn", o.atSpawns, v => { o.atSpawns = v; this.redraw(); })),
      el("div", { class: "btnrow" },
        el("button", { onclick: () => this.app.setTool("figure") }, "Player tool"),
        el("button", { onclick: () => { const d = this.doc; if (d.sidecar.figures.length) d.edit("clear figures", ["sc.figures"], (m, sc) => { sc.figures = []; }); } }, "Remove all figures")),
      this.models.length ? null : el("p", { class: "faint" }, "No player models in this build (models/player/*.gltf comes from quark_tests exportassets): figures show the hull."),
      el("h3", {}, "Scale"),
      el("p", {}, "The floor grid is 1 m (8 m lines stronger), the bar in the corner is to scale at the screen's centre. Step-up 0.55 m, a jump about 1.25 m, the in-game editor's grid 1/8 m .. 4 m."));
  }
  async figure(f, selected) {
    const g = new THREE.Group();
    g.position.fromArray(f.pos); g.rotation.y = f.yaw || 0;
    const crouch = !!f.crouch, H = crouch ? HULL.ch : HULL.h, col = teamColor(f.team || "any");
    const mdl = await this.model(f.model || this.opts.model);
    if (mdl) {
      const m = mdl.clone();
      if (crouch) m.scale.y = HULL.ch / HULL.h;
      g.add(m);
    } else {   // a plain body: legs, torso, head, facing -z like the models
      const mat = new THREE.MeshLambertMaterial({ color: 0x8c96a8 });
      const k = crouch ? HULL.ch / HULL.h : 1;
      const part = (w, h, d, y, z = 0) => { const b = new THREE.Mesh(new THREE.BoxGeometry(w, h * k, d), mat); b.position.set(0, y * k, z); g.add(b); };
      part(0.5, 0.85, 0.3, 0.425); part(0.62, 0.6, 0.34, 1.15); part(0.34, 0.34, 0.34, 1.62);
      part(0.26, 0.06, 0.04, 1.64, -0.18);
    }
    if (this.opts.hitbox) {
      const hull = new THREE.LineSegments(new THREE.EdgesGeometry(new THREE.BoxGeometry(HULL.w, H, HULL.w).translate(0, H / 2, 0)), new THREE.LineBasicMaterial({ color: selected ? 0xffc040 : 0x7cf0ff }));
      const hw = HULL.w * 0.45, hh = H * 0.2;
      const head = new THREE.LineSegments(new THREE.EdgesGeometry(new THREE.BoxGeometry(hw, hh, hw).translate(0, H - hh / 2, 0)), new THREE.LineBasicMaterial({ color: 0xff5a5a }));
      g.add(hull, head);
    }
    if (this.opts.eye) {
      const ey = crouch ? HULL.ceye : HULL.eye;
      const eye = new THREE.Mesh(new THREE.BoxGeometry(0.9, 0.02, 0.02).translate(0, ey, -0.41), new THREE.MeshBasicMaterial({ color: 0xffe14d }));
      g.add(eye);
    }
    return g;
  }
  async redraw() {
    const d = this.doc, L = this.view.layers.figures;
    const token = (this.token = (this.token || 0) + 1);
    if (!d.map) return;
    const list = [];
    (d.sidecar.figures || []).forEach((f, i) => list.push(this.figure(f, d.isSel("figure", i))));
    if (this.opts.atSpawns) for (const s of d.map.spawns) list.push(this.figure({ pos: s.pos, yaw: s.yaw, team: s.team, model: this.opts.model, crouch: this.opts.crouch }, false));
    const objs = await Promise.all(list);
    if (token !== this.token) return;
    for (const c of L.children.slice()) L.remove(c);   // (small: the models' geometry is shared with the cache, so nothing is disposed)
    for (const o of objs) L.add(o);
    this.view.redraw();
  }
}
