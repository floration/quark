// Testing: the map checks, time-to-point metrics (routes drawn in 3D, coloured by time), the two-click route tool and
// custom test sets (kept in the sidecar). Everything runs in quark_maptool on the map as it is now (serialized first).
import * as THREE from "../vendor/three.module.min.js";
import { el, fill, $, select, check, numInput, banner, fmtS, fmt } from "./util.js";
import { segmentMesh, polylineSegs, heat, label, dispose } from "./lines.js";
import { MODES } from "./fallback_parse.js";
import { TEAMS } from "./panels.js";
import { objectiveName } from "./doc.js";

export class Testing {
  constructor(app) {
    this.app = app; this.doc = app.doc; this.view = app.view; this.api = app.api;
    this.panel = $("#rp-test");
    this.checks = null; this.metrics = null; this.route = null; this.customResults = null; this.activeWhere = null;
    this.mode = "ffa"; this.routeA = null; this.kinds = null; this.showRoutes = true; this.onlyObjective = null;
    this.doc.on("open", () => { this.checks = this.metrics = this.route = this.customResults = null; this.mode = this.doc.map.preferredMode || "ffa"; this.clearDrawn(); this.render(); });
    this.render();
    if (this.api.hasTool) this.api.tool("help", {}).then(h => { this.help = h; this.kinds = Array.isArray(h.customKinds) && h.customKinds.length ? h.customKinds : null; this.render(); }).catch(() => {});
  }
  async text() {
    const ans = await this.api.tool("serialize", this.app.mapForSave());
    if (ans.error) throw new Error(ans.error);
    return ans.text;
  }
  async run(cmd, extra, label) {
    if (!this.api.hasTool) { banner("quark_maptool isn't there: build it and rebuild the editor", true); return null; }
    const t0 = performance.now();
    this.busy = label; this.render();
    try {
      const text = await this.text();
      const r = await this.api.tool(cmd, { text, name: this.doc.name || "untitled", ...extra });
      r._ms = performance.now() - t0;
      return r;
    } catch (e) { banner(label + " failed: " + e.message, true, 0); return null; }
    finally { this.busy = null; this.render(); }
  }
  clearDrawn() { this.view.clearLayer("test"); this.view.clearLayer("routes"); this.view.highlight = null; this.view.updateSelectionTint(); }

  // where points: a beam and a ball at each, framed
  showWhere(where, pass, frame = true, paths = null) {
    this.view.clearLayer("test");
    const L = this.view.layers.test, col = pass ? 0x3fff8a : 0xff4a4a;
    let pts = (where || []).map(w => Array.isArray(w[0]) ? w : [w]).flat();   // ([x,y,z] or [[min],[max]] boxes)
    if (paths && paths.length) {   // a custom test's routes, coloured by time like the metrics' routes
      const maxS = Math.max(1e-3, ...paths.map(q => q.seconds || 0)), segs = [];
      for (const q of paths) {
        const P = q.path || []; if (P.length < 2) continue;
        for (let i = 0; i + 1 < P.length; i++) segs.push([[P[i][0], P[i][1] + 0.25, P[i][2]], [P[i + 1][0], P[i + 1][1] + 0.25, P[i + 1][2]], heat(((q.seconds || 0) * (i + 1) / (P.length - 1)) / maxS)]);
        pts = pts.concat([P[0], P[P.length - 1]]);
      }
      if (segs.length) L.add(segmentMesh(segs, { width: 0.14 }));
    }
    if (!pts.length) { this.view.redraw(); return; }
    for (const p of pts) {
      const s = new THREE.Mesh(new THREE.SphereGeometry(0.35, 14, 10), new THREE.MeshBasicMaterial({ color: col, depthTest: false, transparent: true, opacity: 0.9 }));
      s.position.fromArray(p); s.renderOrder = 12; L.add(s);
      const beam = new THREE.Mesh(new THREE.CylinderGeometry(0.05, 0.05, 30, 6).translate(0, 15, 0), new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: 0.5, depthWrite: false }));
      beam.position.fromArray(p); L.add(beam);
    }
    if (pts.length > 1 && pts.length < 400) L.add(segmentMesh(polylineSegs(pts, col), { width: 0.06, opacity: 0.6 }));
    if (frame) {
      const mn = [0, 1, 2].map(k => Math.min(...pts.map(p => p[k])) - 3), mx = [0, 1, 2].map(k => Math.max(...pts.map(p => p[k])) + 3);
      this.view.frame(mn, mx);
    }
    this.view.redraw();
  }
  drawRoutes() {
    this.view.clearLayer("routes");
    const segs = [], L = this.view.layers.routes;
    const add = (path, seconds, maxS, objective) => {
      if (!path || path.length < 2) return;
      // colour along the route by the time spent so far (green fast .. red slow, against the slowest route)
      let tot = 0; const d = [0];
      for (let i = 1; i < path.length; i++) { tot += Math.hypot(...[0, 1, 2].map(k => path[i][k] - path[i - 1][k])); d.push(tot); }
      for (let i = 0; i + 1 < path.length; i++) {
        const f = (seconds * (d[i] / (tot || 1))) / (maxS || 1);
        segs.push([[path[i][0], path[i][1] + 0.2, path[i][2]], [path[i + 1][0], path[i + 1][1] + 0.2, path[i + 1][2]], heat(f)]);
      }
    };
    if (this.metrics && this.showRoutes) {
      const R = (this.metrics.routes || []).filter(r => !this.onlyObjective || r.objective === this.onlyObjective || r.to === this.onlyObjective || (r.toSpawn !== undefined && /spawn/.test(this.onlyObjective)));
      const maxS = Math.max(1, ...R.map(r => r.seconds || 0));
      for (const r of R) add(r.path, r.seconds || 0, maxS);
      for (const o of this.metrics.objectives || []) {
        if (!o.pos || (this.onlyObjective && o.name !== this.onlyObjective)) continue;
        // over the zone's own label (POINT A ...) when it has one: same spot, drawn one label higher on screen
        const s = label(o.name + "  A " + fmtS(o.attackers) + " / D " + fmtS(o.defenders), "#ffffff", 0.7), z = this.zoneOf(o.name);
        if (z) { s.position.set((z.min[0] + z.max[0]) / 2, z.max[1] + 0.7, (z.min[2] + z.max[2]) / 2); s.center.set(0.5, -0.9); }
        else s.position.set(o.pos[0], o.pos[1] + 5, o.pos[2]);
        L.add(s);
      }
    }
    if (this.route && this.route.path) add(this.route.path, this.route.seconds || 1, this.route.seconds || 1);
    if (segs.length) L.add(segmentMesh(segs, { width: 0.22 }));
    if (this.routeMarks) for (const p of this.routeMarks) { const s = new THREE.Mesh(new THREE.SphereGeometry(0.3, 12, 8), new THREE.MeshBasicMaterial({ color: 0x9cf })); s.position.fromArray(p); L.add(s); }
    this.view.redraw();
  }

  zoneOf(name) {   // "site A" / "point B" / "hill" / "checkpoint 2" -> that box
    const m = this.doc.map, r = /^(site|point|checkpoint) (\w+)$/.exec(name || "");
    if (name === "hill") return m.hill;
    if (!r) return null;
    const list = { site: m.sites, point: m.points, checkpoint: m.checkpoints }[r[1]];
    const i = /^\d+$/.test(r[2]) ? +r[2] - 1 : r[2].charCodeAt(0) - 65;
    return list[i] || null;
  }

  // ---- the route tool (two clicks)
  async routeClick(p) {
    if (!this.routeA) { this.routeA = p; this.routeMarks = [p]; this.route = null; this.drawRoutes(); this.app.hud(); return; }
    const a = this.routeA; this.routeA = null; this.routeMarks = [a, p];
    const r = await this.run("route", { from: a, to: p }, "route");
    if (r) { this.route = r; if (!r.reachable) banner("No walkable route between those points", true, 4000); }
    this.drawRoutes(); this.render(); this.app.hud();
  }

  // ---- custom tests (sidecar.tests: [{kind, name, ...fields}])
  // the fields a test can't run without (a place or a point left unset), by name
  missing(t) {
    const K = this.kinds && this.kinds.find(k => k.kind === t.kind);
    return K ? (K.fields || []).filter(fd => (fd.type === "place" || fd.type === "point") && (t[fd.name] === null || t[fd.name] === undefined)).map(fd => fd.name) : [];
  }
  // runs the complete tests; the incomplete ones are listed as skipped, never sent (they ran with null and said so)
  async runTests() {
    const all = this.tests, ready = all.filter(t => !this.missing(t).length);
    const r = ready.length ? await this.run("custom", { tests: ready }, "custom tests") : { results: [] };
    if (!r) return;
    const res = (r.results || []).slice(), out = [];
    for (const t of all) {
      const miss = this.missing(t);
      out.push(miss.length ? { name: t.name || t.kind, skipped: true, detail: "not run: set " + miss.join(", ") } : res.shift());
    }
    this.customResults = { ...r, results: out.filter(Boolean) };
    this.render();
  }
  get tests() { const sc = this.doc.sidecar; if (!Array.isArray(sc.tests)) sc.tests = []; return sc.tests; }
  editTests(label, fn) { this.doc.edit(label, ["sc.tests"], (m, sc) => { if (!Array.isArray(sc.tests)) sc.tests = []; fn(sc.tests); }); this.render(); }
  testForm(t, i) {
    const K = this.kinds && this.kinds.find(k => k.kind === t.kind);
    const box = el("div", { class: "result" + (this.editing === i ? " on" : "") });
    const head = el("div", { class: "btnrow" },
      el("b", {}, t.name || t.kind || "test"), el("span", { class: "faint" }, t.kind || ""),
      this.missing(t).length ? el("span", { class: "warn", title: "it is skipped until these are set" }, "set " + this.missing(t).join(", ")) : null, el("span", { class: "spacer" }),
      el("button", { class: "mini", onclick: () => { this.editing = this.editing === i ? null : i; this.render(); } }, this.editing === i ? "done" : "edit"),
      el("button", { class: "mini", onclick: () => this.editTests("copy test", T => T.splice(i + 1, 0, structuredClone(t))) }, "copy"),
      el("button", { class: "mini", onclick: () => this.editTests("remove test", T => T.splice(i, 1)) }, "x"));
    box.append(head);
    if (this.editing !== i) return box;
    const set = (k, v) => this.editTests("edit test", T => { T[i][k] = v; });
    const f = el("div", { class: "form" });
    const txt = (v, on) => { const x = el("input", { type: "text", value: v ?? "" }); x.addEventListener("change", () => on(x.value)); return x; };
    f.append(el("label", {}, "name"), txt(t.name, v => set("name", v)));
    if (this.kinds) f.append(el("label", {}, "kind"), select(this.kinds.map(k => [k.kind, k.title || k.kind]), t.kind, v => this.editTests("test kind", T => { const nk = this.kinds.find(k => k.kind === v); const n = { kind: v, name: T[i].name }; for (const fd of nk.fields || []) n[fd.name] = fd.default ?? null; T[i] = n; })));
    if (K) {
      if (K.desc) box.append(el("p", { class: "faint" }, K.desc));
      for (const fd of K.fields || []) {
        const v = t[fd.name];
        let input;
        switch (fd.type) {
          case "point": input = el("div", { class: "v3" }, [0, 1, 2].map(k => numInput((v || [0, 0, 0])[k], x => { const p = (v || [0, 0, 0]).slice(); p[k] = x; set(fd.name, p); }, { step: 0.5 })),
            el("button", { class: "mini", title: "click a point in the view", onclick: () => this.app.editor.pickPoint(p => set(fd.name, p.map(x => Math.round(x * 100) / 100))) }, "pick")); break;
          case "box": input = el("div", {}, ["min", "max"].map(mm => el("div", { class: "v3" }, [0, 1, 2].map(k => numInput(((v || {})[mm] || [0, 0, 0])[k], x => { const b = structuredClone(v || { min: [0, 0, 0], max: [1, 1, 1] }); b[mm][k] = x; set(fd.name, b); }, { step: 0.5 }))))); break;
          case "number": case "seconds": input = this.optNum(v, x => set(fd.name, x), fd.type === "seconds" ? 0.5 : "any"); break;
          case "place": input = this.placeInput(v, x => set(fd.name, x)); break;
          case "bool": input = check("", v, x => set(fd.name, x)); break;
          case "mode": input = select([["", "(map's mode)"], ...MODES], v || "", x => set(fd.name, x || null)); break;
          case "team": input = select(TEAMS, v || "a", x => set(fd.name, x)); break;
          case "spawn": input = select([["", "(any)"], ...this.doc.map.spawns.map((s, j) => [j, "#" + j + " " + s.team])], v ?? "", x => set(fd.name, x === "" ? null : +x)); break;
          case "objective": input = this.objectiveInput(v, x => set(fd.name, x || null), "(every objective)"); break;
          default:
            if (fd.name === "via") input = select(["walk", "nav"], v || fd.default || "walk", x => set(fd.name, x));
            else if (fd.name === "check" && this.checks) input = select(this.checks.checks.map(c => [c.id, c.id]), v || fd.default, x => set(fd.name, x));
            else input = txt(v, x => set(fd.name, x));
        }
        f.append(el("label", { title: fd.desc || "" }, fd.name), input);
      }
      box.append(f);
    } else {
      box.append(f);
      const ta = el("textarea", { rows: 6 }, JSON.stringify(t, null, 1));
      ta.addEventListener("change", () => { try { const o = JSON.parse(ta.value); this.editTests("edit test", T => { T[i] = o; }); } catch { banner("not valid JSON", true); } });
      box.append(el("p", { class: "faint" }, "This helper gives no test kinds (quark_maptool help): edit the test as JSON (native/README.md has the shape)."), ta);
    }
    return box;
  }

  // a number that may be left empty (null: the test's default)
  optNum(v, on, step) {
    const i = el("input", { type: "number", step: step ?? "any", value: v === null || v === undefined ? "" : String(v), placeholder: "default" });
    i.addEventListener("change", () => { const t = i.value.trim(); if (t === "") on(null); else { const x = parseFloat(t); if (isFinite(x)) on(x); } });
    return i;
  }
  objectiveNames() {
    const m = this.doc.map, n = [];
    m.sites.forEach((_, i) => n.push(objectiveName(m, "site", i)));
    if (m.hill) n.push("hill");
    m.points.forEach((_, i) => n.push(objectiveName(m, "point", i)));
    m.checkpoints.forEach((_, i) => n.push(objectiveName(m, "checkpoint", i)));
    if (m.path.length) n.push("cart", "cart start", "cart end");
    for (const o of (this.metrics && this.metrics.objectives) || []) if (!n.includes(o.name)) n.push(o.name);
    return n;
  }
  objectiveInput(v, on, none) {
    return select([["", none || "(none)"], ...this.objectiveNames()], v || "", on);
  }
  // a place: [x,y,z], {spawn:i}, {team}, {objective}, {box:{min,max}}
  placeInput(v, on) {
    const kind = v === null || v === undefined ? "" : Array.isArray(v) ? "point" : v.spawn !== undefined ? "spawn" : v.team !== undefined ? "team" : v.objective !== undefined ? "objective" : v.box ? "box" : "";
    const m = this.doc.map, wrap = el("div", {});
    const defs = { "": null, point: [0, 0, 0], spawn: { spawn: 0 }, team: { team: "a" }, objective: { objective: this.objectiveNames()[0] || "hill" }, box: { box: { min: [0, 0, 0], max: [4, 3, 4] } } };
    wrap.append(select([["", "(none)"], ["point", "a point"], ["spawn", "a spawn"], ["team", "a team's spawns"], ["objective", "an objective"], ["box", "a box"]], kind, k => on(structuredClone(defs[k]))));
    const pick = (cb) => el("button", { class: "mini", title: "click a point in the view", onclick: () => this.app.editor.pickPoint(p => cb(p.map(x => Math.round(x * 100) / 100))) }, "pick");
    if (kind === "point") wrap.append(el("div", { class: "v3" }, [0, 1, 2].map(k => numInput(v[k], x => { const p = v.slice(); p[k] = x; on(p); }, { step: 0.5 })), pick(on)));
    if (kind === "spawn") wrap.append(select(m.spawns.map((s, j) => [j, "#" + j + " " + s.team]), v.spawn, x => on({ spawn: +x })));
    if (kind === "team") wrap.append(select(TEAMS, v.team, x => on({ team: x })));
    if (kind === "objective") wrap.append(this.objectiveInput(v.objective, x => on({ objective: x })));
    if (kind === "box") for (const mm of ["min", "max"]) wrap.append(el("div", { class: "v3" }, [0, 1, 2].map(k => numInput(v.box[mm][k], x => { const b = structuredClone(v); b.box[mm][k] = x; on(b); }, { step: 0.5 })),
      pick(p => { const b = structuredClone(v); b.box[mm] = p; on(b); })));
    return wrap;
  }

  render() {
    const P = this.panel, d = this.doc, tool = this.api.hasTool, out = [];
    if (!tool) out.push(el("p", { class: "warn" }, "quark_maptool isn't next to serve.py: build it (cmake --build build --target quark_maptool) and rebuild the editor."));
    if (this.busy) out.push(el("p", { class: "warn" }, "running " + this.busy + "..."));
    // checks
    out.push(el("h3", {}, "Checks", el("span", { class: "spacer" }), el("button", { disabled: !tool || !!this.busy, onclick: async () => { const r = await this.run("check", {}, "checks"); if (r) { this.checks = r; this.activeCheck = null; this.render(); } } }, "Run checks")));
    if (this.checks) {
      const C = this.checks.checks || [];
      const fails = C.filter(c => !c.pass).length;
      out.push(el("p", { class: fails ? "bad" : "good" }, fails ? fails + " of " + C.length + " failed" : "all " + C.length + " passed", el("span", { class: "faint" }, "  (" + Math.round(this.checks._ms) + " ms)")));
      C.forEach((c, i) => out.push(el("div", { class: "result " + (c.pass ? "pass" : "fail") + (this.activeCheck === i ? " on" : ""), onclick: () => { this.activeCheck = i; this.showWhere(c.where, c.pass); this.render(); } },
        el("div", {}, c.name || c.id, c.where && c.where.length ? el("span", { class: "faint" }, "  " + c.where.length + " place" + (c.where.length > 1 ? "s" : "")) : null),
        c.detail ? el("div", { class: "d" }, c.detail) : null)));
    }
    // metrics
    out.push(el("h3", {}, "Metrics"));
    out.push(el("div", { class: "btnrow" }, "mode", select(MODES, this.mode, v => { this.mode = v; }),
      el("button", { disabled: !tool || !!this.busy, onclick: async () => { const r = await this.run("metrics", { mode: this.mode }, "metrics"); if (r) { this.metrics = r; this.onlyObjective = null; this.drawRoutes(); this.render(); } } }, "Run metrics"),
      check("routes", this.showRoutes, v => { this.showRoutes = v; this.drawRoutes(); })));
    if (this.metrics) {
      const O = this.metrics.objectives || [];
      const tbl = el("table", { class: "t" }, el("tr", {}, el("th", {}, "objective"), el("th", { class: "n" }, "att"), el("th", { class: "n" }, "def"), el("th", { class: "n" }, "diff")));
      for (const o of O) {
        const bad = o.diff !== null && o.diff !== undefined && Math.abs(o.diff) > 1;
        tbl.append(el("tr", { class: "click" + (this.onlyObjective === o.name ? " on" : ""), title: "show only the routes to it", onclick: () => { this.onlyObjective = this.onlyObjective === o.name ? null : o.name; this.drawRoutes(); this.render(); if (o.pos && this.onlyObjective) this.view.frame(o.pos.map(v => v - 15), o.pos.map(v => v + 15)); } },
          el("td", {}, o.name), el("td", { class: "n" }, fmtS(o.attackers)), el("td", { class: "n" }, fmtS(o.defenders)), el("td", { class: "n " + (bad ? "bad" : "") }, o.diff === null || o.diff === undefined ? "-" : (o.diff > 0 ? "+" : "") + o.diff.toFixed(1))));
      }
      out.push(tbl);
      out.push(el("p", { class: "faint" }, "diff = attackers - defenders arrival (the map report's bar: within -1 .. +1 s). Routes: green fast .. red slow."));
      const N = this.metrics.numbers || {};
      if (Object.keys(N).length) out.push(el("table", { class: "t" }, Object.entries(N).filter(([k, v]) => v === null || typeof v !== "object").map(([k, v]) => el("tr", {}, el("td", {}, k), el("td", { class: "n" }, typeof v === "number" ? fmt(v, 2) : typeof v === "string" ? v : JSON.stringify(v))))));
    }
    // route
    out.push(el("h3", {}, "Route", el("span", { class: "spacer" }), el("button", { disabled: !tool, onclick: () => { this.app.setTool("route"); this.routeA = null; } }, "Pick two points")));
    if (this.route) out.push(el("p", {}, this.route.reachable === false ? el("span", { class: "bad" }, "not reachable") : "walk " + fmtS(this.route.seconds) + ", " + (this.route.path || []).length + " points",
      el("button", { class: "mini", onclick: () => { this.route = null; this.routeMarks = null; this.drawRoutes(); this.render(); } }, "clear")));
    // custom
    out.push(el("h3", {}, "Custom tests", el("span", { class: "faint" }, "(saved with the map)"), el("span", { class: "spacer" }),
      el("button", { onclick: () => this.editTests("add test", T => { const k = this.kinds ? this.kinds[0] : null; const t = { kind: k ? k.kind : "reach", name: "test " + (T.length + 1) }; if (k) for (const fd of k.fields || []) t[fd.name] = fd.default ?? null; T.push(t); this.editing = T.length - 1; }) }, "Add"),
      el("button", { disabled: !tool || !!this.busy || !this.tests.some(t => !this.missing(t).length), title: "runs the tests whose places are set", onclick: () => this.runTests() }, "Run")));
    this.tests.forEach((t, i) => out.push(this.testForm(t, i)));
    if (this.customResults) {
      const R = this.customResults.results || [], ran = R.filter(r => !r.skipped), skipped = R.length - ran.length;
      out.push(el("p", {}, ran.filter(r => r.pass).length + " of " + ran.length + " passed" + (skipped ? ", " + skipped + " not run (incomplete)" : "")));
      R.forEach((r, i) => out.push(el("div", { class: "result " + (r.skipped ? "" : r.pass ? "pass" : "fail"), onclick: () => !r.skipped && this.showWhere(r.where, r.pass, true, r.paths) },
        el("div", {}, r.name, r.value !== undefined && r.value !== null ? el("span", { class: "faint num" }, "  " + (typeof r.value === "number" ? fmt(r.value, 2) : JSON.stringify(r.value))) : null), r.detail ? el("div", { class: r.skipped ? "d warn" : "d" }, r.detail) : null)));
    }
    fill(P, ...out);
  }
}
