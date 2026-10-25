// The side panels: map list (defaults + yours, with provenance), outliner, properties of the selection, map settings.
import { el, fill, $, numInput, select, check, fmt, tidy, rgbHex, hexRgb, plural } from "./util.js";
import { KINDS, objectiveName } from "./doc.js";
import { LIMITS } from "./validate.js";
import { repeatCounts, repeatLen, capAxis, sameDef } from "./repeat.js";
import { MATERIALS, MODES } from "./fallback_parse.js";

export const TEAMS = ["a", "b", "any", "race", ...Array.from({ length: 10 }, (_, i) => "att" + i), ...Array.from({ length: 10 }, (_, i) => "def" + i)];
const TEAM_NAMES = { a: "team A", b: "team B", any: "any", race: "race" };
export const RANKED = ["vanilla", "vampire", "classic", "quirky", "hitscan", "instagib"];
const MAT_NOTES = ["solid", "solid (accent colour)", "solid, hitscan passes for half damage", "solid", "solid, glows", "scenery outside the walls: not solid",
  "water: not solid", "lava: kills", "decoration: not solid", "invisible: blocks players only"];
const D2R = Math.PI / 180;
const X = LIMITS.extent, clampX = (v) => Math.max(-X, Math.min(X, v));   // (every coordinate the game loads: within 4096 m)
// the keys the Map tab shows: it redraws only when one of them changes
const MAPSET_KEYS = new Set(["title", "setting", "preferredMode", "holdfire", "void", "indoor", "sun", "allowedModes", "ranked", "palette", "cartChecks", "warnings", "*"]);

export class Panels {
  constructor(app) {
    this.app = app; this.doc = app.doc;
    this.mapsEl = $("#mapList"); this.outEl = $("#outliner"); this.propsEl = $("#rp-props"); this.mapsetEl = $("#rp-mapset");
    $("#mapFilter").addEventListener("input", () => this.renderMaps());
    this.doc.on("selection", () => { this.renderProps(); this.renderOutliner(); });
    this.doc.on("map", (keys, live, change) => {
      if (live) return;
      this.renderProps();
      if (!keys || !change || change.structural.size || keys.includes("groups")) this.renderOutliner();   // (counts, the group list)
      if (!keys || keys.some(k => MAPSET_KEYS.has(k))) this.renderMapSettings();
    });
    this.doc.on("open", () => { this.renderMaps(); this.renderMapSettings(); });
    app.on("save", () => this.renderMapSettings());
  }

  // ---- maps
  async loadList() { this.list = await this.app.api.maps(); this.renderMaps(); }
  renderMaps() {
    const L = this.list || { defaults: [], mine: [] }, f = $("#mapFilter").value.toLowerCase();
    const d = this.doc, rows = [];
    const shown = (m) => !f || (m.name + " " + m.title + " " + m.for).toLowerCase().includes(f);
    const row = (m, src) => {
      const on = d.name === m.name && d.src === src;
      const prov = m.provenance ? m.provenance.origin : (src === "default" ? "ai" : null);
      const mine = src === "mine";
      rows.push(el("div", { class: "row" + (on ? " on" : ""), title: m.name + ".qmap" + ((m.provenance && m.provenance.note) ? "\n" + m.provenance.note : ""), onclick: () => this.app.openMap(src, m.name) },
        el("span", { class: "mname" }, m.title || m.name, m.title && m.title !== m.name ? el("span", { class: "fname" }, m.name) : null),
        prov ? el("span", { class: "tag", title: "provenance: " + prov + (m.provenance && m.provenance.note ? " (" + m.provenance.note + ")" : "") }, prov) : null,
        el("span", { class: "sub" }, m.for || ""),
        mine ? el("span", { class: "acts" },
          el("button", { class: "mini", title: "rename " + m.name + ".qmap", onclick: (e) => { e.stopPropagation(); this.app.renameMine(m.name); } }, "rename"),
          el("button", { class: "mini danger", title: "delete " + m.name + ".qmap (moved to deleted/ in your maps folder)", onclick: (e) => { e.stopPropagation(); this.app.deleteMine(m.name); } }, "x")) : null));
    };
    const mine = L.mine.filter(shown), defs = L.defaults.filter(shown);
    rows.push(el("div", { class: "grp" }, "Your maps (" + (f && L.mine.length ? mine.length + " of " + L.mine.length : L.mine.length) + ")"));
    mine.forEach(m => row(m, "mine"));
    if (!L.mine.length) rows.push(el("div", { class: "row faint" }, "none yet: New, or Save as"));
    rows.push(el("div", { class: "grp" }, "Default maps (read-only)" + (f ? " (" + defs.length + " of " + L.defaults.length + ")" : "")));
    defs.forEach(m => row(m, "default"));
    if (f && !mine.length && !defs.length) rows.push(el("div", { class: "row faint" }, "no map matches \u201c" + f + "\u201d"));
    fill(this.mapsEl, ...rows);
  }

  // ---- outliner
  renderOutliner() {
    const d = this.doc, m = d.map; if (!m) return;
    const rows = [];
    const kind = (k, label, n) => {
      if (!n) return;
      rows.push(el("div", { class: "row", title: "select all", onclick: () => { d.select(Array.from({ length: n }, (_, i) => ({ k, i }))); } },
        el("span", {}, label), el("span", { class: "sub" }, String(n))));
    };
    const objs = d.sidecar.objects || [], inObjects = m.generated.reduce((n, b) => n + (b.object !== undefined), 0);
    kind("brush", "Boxes", m.brushes.length);
    if (m.generated.length > inObjects) rows.push(el("div", { class: "row faint" }, el("span", {}, "made from groups / ladders"), el("span", { class: "sub" }, String(m.generated.length - inObjects))));
    kind("object", "Custom objects", objs.length);
    if (inObjects) rows.push(el("div", { class: "row faint" }, el("span", {}, "boxes in custom objects"), el("span", { class: "sub" }, String(inObjects))));
    kind("spawn", "Spawns", m.spawns.length);
    kind("light", "Lights", m.lights.length);
    kind("ladder", "Ladders", m.ladders.length);
    kind("site", "Bomb sites", m.sites.length);
    if (m.hill) kind("hill", "Hill", 1);
    kind("point", "Capture points", m.points.length);
    kind("checkpoint", "Checkpoints", m.checkpoints.length);
    kind("path", "Path points", m.path.length);
    kind("stairs", "Stairs records", m.stairs.length);
    kind("figure", "Player figures", (d.sidecar.figures || []).length);
    kind("botstart", "Bot starts", (d.sidecar.botStarts || []).length);
    if (m.groups.length) {
      rows.push(el("div", { class: "grp" }, "Groups"));
      m.groups.forEach((g, i) => rows.push(el("div", { class: "row" + (d.isSel("group", i) ? " on" : ""), onclick: (e) => { d.select([{ k: "group", i }], e.shiftKey); this.app.frameSel(); } },
        el("span", {}, "#" + i + " " + g.type), el("span", { class: "sub" }, g.type === "custom" ? plural(g.parts.length, "part") : MATERIALS[g.mat]))));
    }
    if (objs.length) {
      rows.push(el("div", { class: "grp" }, "Custom objects"));
      objs.forEach((o, i) => rows.push(el("div", { class: "row" + (d.isSel("object", i) ? " on" : ""), title: o.lib ? "placed from the library's " + o.lib : "", onclick: (e) => { d.select([{ k: "object", i }], e.shiftKey); this.app.frameSel(); } },
        el("span", {}, "#" + i + " " + o.name), el("span", { class: "sub" }, repeatCounts(this.app.editor.objDef(o), o).join(" x ") + " units"))));
    }
    fill(this.outEl, ...rows);
  }

  // ---- properties
  field(label, input) { return [el("label", {}, label), input]; }
  v3(arr, onSet, step) { return el("div", { class: "v3" }, [0, 1, 2].map(k => numInput(arr[k], v => onSet(k, clampX(v)), { step: step ?? this.app.editor.grid, min: -X, max: X }))); }
  set(label, s, fn) { const d = this.doc; d.edit(label, [KINDS[s.k].key], () => fn(d.obj(s)), { touch: [s] }); }

  renderProps() {
    const d = this.doc, P = this.propsEl, m = d.map;
    if (!m) { fill(P); return; }
    const sel = d.sel;
    const out = [];
    const ed = this.app.library && this.app.library.editing;
    if (ed) out.push(el("div", { class: "editbar" }, el("span", {}, "Editing the " + ed.part + " of " + ed.name + ": the selected boxes (" + d.selOf("brush").length + ") become it" + (ed.part === "unit" ? "" : "; none selected: no " + ed.part + " piece")),
      el("div", { class: "btnrow" }, el("button", { class: "primary", onclick: () => this.app.library.finishEdit(true) }, "Done"), el("button", { onclick: () => this.app.library.finishEdit(false) }, "Cancel"))));
    if (!sel.length) {
      out.push(el("h3", {}, "Nothing selected"));
      out.push(el("p", {}, "Click to select (Shift adds, drag a rectangle, click again for what is behind). Pick a tool above to add things. ? lists the keys."));
      out.push(el("p", { class: "faint" }, (() => { const gen = m.generated.length - m.generated.reduce((n, b) => n + (b.object !== undefined), 0), no = (d.sidecar.objects || []).length;
        return [plural(m.brushes.length, "box"), gen ? plural(gen, "box") + " made from groups and ladders" : "", no ? plural(no, "custom object") : "", plural(m.spawns.length, "spawn"), plural(m.lights.length, "light")].filter(Boolean).join(", ") + "."; })()));
      fill(P, ...out); return;
    }
    if (sel.length > 1) {
      const counts = {};
      for (const s of sel) counts[s.k] = (counts[s.k] || 0) + 1;
      out.push(el("h3", {}, sel.length + " selected"));
      out.push(el("p", {}, Object.entries(counts).map(([k, n]) => plural(n, KINDS[k].label)).join(", ")));
      const bb = d.selBounds();
      out.push(el("p", { class: "faint num" }, "bounds " + bb.min.map(v => fmt(v)).join(" ") + "  ..  " + bb.max.map(v => fmt(v)).join(" ")));
      if (counts.brush) {
        out.push(el("div", { class: "form" }, this.field("material", select([["", "(mixed)"], ...MATERIALS.map((n, i) => [i, i + " " + n])], "", (v) => {
          if (v === "") return;
          d.edit("material", ["brushes"], () => { for (const i of d.selOf("brush")) m.brushes[i].mat = +v; }, { touch: d.sel.filter(x => x.k === "brush") });
        }))));
      }
      out.push(this.actions());
      fill(P, ...out); return;
    }
    const s = sel[0], o = d.obj(s), K = KINDS[s.k];
    if (!o) { fill(P); return; }
    out.push(el("h3", {}, objectiveName(m, s.k, s.i), el("span", { class: "spacer" }), el("button", { class: "mini", onclick: () => this.app.frameSel() }, "frame")));
    const f = el("div", { class: "form" });
    const add = (...x) => f.append(...x.flat());
    const boxFields = () => {
      // (a min never goes past its max, nor a max past its min: the value stops one grid step from the other side, or
      // at the box's size when it is thinner, as a resize drag does; it never makes a box of no size)
      const keep = (x, k) => Math.min(this.app.editor.grid, x.max[k] - x.min[k]);
      add(this.field("min", this.v3(o.min, (k, v) => this.set("edit", s, (x) => { x.min[k] = tidy(Math.min(v, x.max[k] - keep(x, k))); }))));
      add(this.field("max", this.v3(o.max, (k, v) => this.set("edit", s, (x) => { x.max[k] = tidy(Math.max(v, x.min[k] + keep(x, k))); }))));
      add(this.field("size", el("span", { class: "num dim" }, [0, 1, 2].map(k => fmt(o.max[k] - o.min[k])).join(" x ") + " m")));
    };
    const posFields = (p, yaw) => {
      add(this.field("position", this.v3(p, (k, v) => this.set("move", s, (x) => { (x.pos || x)[k] = tidy(v); }))));
      if (yaw) add(this.field("yaw (deg)", numInput(Math.round(o.yaw / D2R * 100) / 100, v => this.set("turn", s, (x) => { x.yaw = v * D2R; }), { step: 45 })));
    };
    switch (s.k) {
      case "brush":
        add(this.field("material", select(MATERIALS.map((n, i) => [i, i + " " + n]), o.mat, v => { this.set("material", s, x => { x.mat = +v; }); this.app.editor.mat = +v; })));
        add(this.field("", el("span", { class: "faint" }, MAT_NOTES[o.mat])));
        boxFields(); break;
      case "group": {
        add(this.field("type", select(["custom", "room", "stairs"], o.type, v => this.set("group type", s, x => { if (v !== "custom" && x.parts.length && !confirm("Drop this group's " + x.parts.length + " parts?")) return; x.type = v; if (v !== "custom") x.parts = []; }))));
        if (o.type !== "custom") add(this.field("material", select(MATERIALS.map((n, i) => [i, i + " " + n]), o.mat, v => this.set("material", s, x => { x.mat = +v; }))));
        if (o.type === "stairs") add(this.field("rises to", select([[0, "+x"], [1, "-x"], [2, "+z"], [3, "-z"]], o.dir, v => this.set("stairs direction", s, x => { x.dir = +v; }))));
        boxFields();
        break;
      }
      case "spawn":
        add(this.field("team", select(TEAMS.map(t => [t, TEAM_NAMES[t] || (t.startsWith("att") ? "attackers, stage " + t[3] : "defenders, stage " + t[3])]), o.team, v => this.set("spawn team", s, x => { x.team = v; }))));
        posFields(o.pos, true); break;
      case "botstart":
        add(this.field("team", select(TEAMS, o.team || "any", v => this.set("bot team", s, x => { x.team = v; }))));
        posFields(o.pos, true); break;
      case "figure":
        add(this.field("model", select(["hull", ...this.app.player.models], o.model || "hull", v => this.set("figure model", s, x => { x.model = v; }))));
        add(this.field("", check("crouched", o.crouch, v => this.set("figure crouch", s, x => { x.crouch = v; }))));
        posFields(o.pos, true); break;
      case "light":
        posFields(o.pos, false);
        add(this.field("radius", numInput(o.radius, v => this.set("light radius", s, x => { x.radius = Math.max(0.5, Math.min(100, v)); }), { step: 1 })));
        add(this.field("intensity", numInput(o.intensity, v => this.set("light intensity", s, x => { x.intensity = Math.max(0.01, Math.min(10, v)); }), { step: 0.1 })));
        break;
      case "path": posFields(o, false); add(this.field("", el("span", { class: "faint" }, "point " + (s.i + 1) + " of " + m.path.length + ": the Path tool adds after the selected point"))); break;
      case "object": {
        const rep = this.app.editor.objDef(o), n = repeatCounts(rep, o);
        add(this.field("object", el("span", {}, o.name)));
        add(this.field("", el("span", { class: "faint" }, "a custom object: it grows by whole units where it repeats (drag a face handle, or Shift / Ctrl + arrows); saved into the map as plain boxes")));
        add(this.field("units", el("div", { class: "v3" }, [0, 1, 2].map(k => rep.axes[k]
          ? numInput(n[k], v => this.set("units", s, x => { x.max[k] = tidy(x.min[k] + repeatLen(rep, k, Math.max(1, Math.min(500, Math.round(v))))); }), { step: 1, min: 1 })
          : el("span", { class: "faint num" }, "x1")))));
        add(this.field("spacing", el("div", { class: "v3" }, [0, 1, 2].map(k => rep.axes[k]
          ? numInput(rep.spacing[k], v => this.set("spacing", s, x => { const kk = (x.rot || 0) % 2 && k !== 1 ? 2 - k : k; x.def = { ...x.def, spacing: x.def.spacing.map((q, j) => j === kk ? Math.max(0.05, Math.min(64, v)) : q) }; x.max[k] = tidy(x.min[k] + repeatLen({ ...rep, spacing: rep.spacing.map((q, j) => j === k ? Math.max(0.05, Math.min(64, v)) : q) }, k, n[k])); }), { step: 0.25, min: 0.05 })
          : el("span", { class: "faint num" }, "-")))));
        const over = [0, 1, 2].filter(k => rep.axes[k] && rep.spacing[k] < rep.unit[k] - 1e-6);   // (world axes: rep is turned)
        if (over.length) add(this.field("", el("div", { class: "btnrow" }, el("span", { class: "warn" }, "spacing under the unit's size along " + over.map(k => "xyz"[k]).join(", ") + ": units overlap (overlapping boxes, flicker)"),
          el("button", { class: "mini", title: "spacing = the unit's size: units touch", onclick: () => this.set("spacing", s, x => { const t = (x.rot || 0) % 2; x.def = { ...x.def, spacing: x.def.spacing.map((q, j) => over.includes(t && j !== 1 ? 2 - j : j) ? x.def.unit[j] : q) }; x.max = [0, 1, 2].map(k => tidy(x.min[k] + repeatLen({ ...rep, spacing: rep.spacing.map((q, j) => over.includes(j) ? rep.unit[j] : q) }, k, n[k]))); }) }, "touching"))));
        add(this.field("position", this.v3(o.min, (k, v) => this.set("move", s, (x) => { const d0 = tidy(v) - x.min[k]; x.min[k] = tidy(v); x.max[k] = tidy(x.max[k] + d0); }))));
        const ca = capAxis(rep), pieceTxt = (p) => p ? plural(p.boxes.length, "box") + ", " + fmt(p.len) + " m" : "none";
        const L = this.app.library, noCap = ca < 0 ? "start / end pieces need an object that repeats along one axis" : "";
        add(this.field("unit", el("span", { class: "num dim" }, plural(rep.boxes.length, "box") + ", " + rep.unit.map(v => fmt(v)).join(" x ") + " m")));
        add(this.field("start piece", el("span", { class: "num dim", title: noCap }, ca < 0 ? "-" : pieceTxt(rep.start))));
        add(this.field("end piece", el("span", { class: "num dim", title: noCap }, ca < 0 ? "-" : pieceTxt(rep.end))));
        add(this.field("edit", el("div", { class: "btnrow" },
          el("button", { class: "mini", title: "the first unit becomes plain boxes; change them, then Done", onclick: () => L.editPart(s.i, "unit") }, "unit"),
          el("button", { class: "mini", disabled: !!noCap, title: noCap || "the piece before the first unit (build or change it as plain boxes, then Done)", onclick: () => L.editPart(s.i, "start") }, "start piece"),
          el("button", { class: "mini", disabled: !!noCap, title: noCap || "the piece after the last unit, e.g. a railing's closing post (build or change it as plain boxes, then Done)", onclick: () => L.editPart(s.i, "end") }, "end piece"))));
        add(this.field("", el("div", { class: "btnrow" },
          el("button", { class: "mini", title: "turn it into plain boxes (no longer a custom object)", onclick: () => this.app.library.explode(s.i) }, "make plain boxes"),
          el("button", { class: "mini", title: "save its definition (with this spacing and pieces) in the library" + (o.lib ? " as " + o.lib + " (and, asked, to every placed copy)" : ""), onclick: () => L.saveDef(s.i) }, "save to library"))));
        if (o.lib) {
          const it = L.item(o.lib), same = it && sameDef(o.def, it.def);
          add(this.field("library", el("div", { class: "btnrow" }, el("span", { class: it ? (same ? "dim" : "warn") : "faint" }, o.lib + (it ? (same ? " (the same)" : " (differs from the library)") : " (not in the library)")),
            it && !same ? el("button", { class: "mini", title: "every placed copy of " + o.lib + " in this map takes the library's definition (same place, turn and units)", onclick: () => L.updateFromLibrary(o.lib) }, "update from library") : null)));
        }
        break;
      }
      case "ladder": add(this.field("", check("draws its rails and rungs (auto)", o.auto, v => this.set("ladder auto", s, x => { x.auto = v; })))); boxFields(); break;
      case "stairs":
        add(this.field("", el("span", { class: "faint" }, "a staircase record (quark_tests walks it); it doesn't make boxes and doesn't move with them")));
        for (const kk of ["x0", "z0", "x1", "z1", "base", "top", "rising"]) add(this.field(kk, numInput(o[kk], v => this.set("stairs record", s, x => { x[kk] = clampX(v); }))));
        add(this.field("axis", select(["x", "z"], o.axis, v => this.set("stairs record", s, x => { x.axis = v; }))));
        add(this.field("", check("solid", o.solid, v => this.set("stairs record", s, x => { x.solid = v; }))));
        break;
      default: boxFields();
    }
    out.push(f);
    if (s.k === "group") out.push(this.groupParts(s, o));
    out.push(this.actions());
    fill(P, ...out);
  }
  groupParts(s, g) {
    const d = this.doc, m = d.map, touch = { touch: [{ k: "group", i: s.i }] };
    const wrap = el("div", {});
    const parts = m.generated.map((b, i) => ({ b, i })).filter(x => x.b.group === s.i);
    wrap.append(el("h3", {}, "Parts (" + parts.length + ")", el("span", { class: "spacer" }),
      el("button", { class: "mini", title: "Turn this group back into separate boxes", onclick: () => this.app.ungroup(s.i) }, "ungroup")));
    if (g.type !== "custom") wrap.append(el("p", { class: "faint" }, "made from the group's box (" + g.type + "): resize the box and they follow"));
    const tbl = el("table", { class: "t" }, el("tr", {}, el("th", {}, "#"), el("th", {}, "mat"), el("th", { class: "n" }, "size"), g.type === "custom" ? el("th", {}, "") : null));
    parts.slice(0, 400).forEach((x, n) => {
      const on = s.part === n;
      const tr = el("tr", { class: "click" + (on ? " on" : ""), onclick: () => d.select([{ k: "group", i: s.i, part: on ? undefined : n }]) },
        el("td", {}, String(n)), el("td", {}, MATERIALS[x.b.mat]), el("td", { class: "n" }, [0, 1, 2].map(k => fmt(x.b.max[k] - x.b.min[k], 2)).join("x")),
        g.type === "custom" ? el("td", {}, el("button", { class: "mini", title: "remove this part", onclick: (e) => { e.stopPropagation(); d.edit("remove part", ["groups"], () => g.parts.splice(n, 1), touch); } }, "x")) : null);
      tbl.append(tr);
    });
    wrap.append(tbl);
    if (s.part !== undefined && g.type === "custom" && g.parts[s.part]) {
      const p = g.parts[s.part];
      wrap.append(el("h3", {}, "Part " + s.part + " (fractions of the group's box)"));
      const f = el("div", { class: "form" });
      f.append(...this.field("material", select(MATERIALS.map((n, i) => [i, i + " " + n]), p.mat, v => d.edit("part material", ["groups"], () => { p.mat = +v; }, touch))));
      f.append(...this.field("min", el("div", { class: "v3" }, [0, 1, 2].map(k => numInput(p.min[k], v => d.edit("part", ["groups"], () => { p.min[k] = Math.min(Math.max(0, Math.min(1, v)), p.max[k]); }, touch), { step: 0.05, min: 0, max: 1 })))));
      f.append(...this.field("max", el("div", { class: "v3" }, [0, 1, 2].map(k => numInput(p.max[k], v => d.edit("part", ["groups"], () => { p.max[k] = Math.max(Math.max(0, Math.min(1, v)), p.min[k]); }, touch), { step: 0.05, min: 0, max: 1 })))));
      wrap.append(f);
    }
    return wrap;
  }
  actions() {
    const d = this.doc, a = this.app;
    const nb = d.selOf("brush").length;
    return el("div", { class: "btnrow" },
      el("button", { onclick: () => a.editor.duplicate(), title: "Ctrl+D" }, "Duplicate"),
      el("button", { class: "danger", onclick: () => { d.deleteSel(); a.editor.pickDirty = true; }, title: "Del" }, "Delete"),
      el("button", { onclick: () => a.editor.rotateSel(), title: "R: 90 degrees about Y" }, "Rotate"),
      el("button", { onclick: () => a.editor.flipSel(true, false), title: "X: mirror left / right (across x, about the selection's centre)" }, "Flip X"),
      el("button", { onclick: () => a.editor.flipSel(false, true), title: "Z: mirror front / back (across z, about the selection's centre)" }, "Flip Z"),
      el("button", { onclick: () => a.mirrorCopy(), title: "P: add the selection's point-symmetric twin through the map's centre (0, 0), team A and B swapped: what a ranked map needs" }, "Mirror copy"),
      nb >= 1 ? el("button", { onclick: () => a.makeGroup(), title: "One custom group from the selected boxes (move / stretch them as one)" }, "Group boxes") : null);
  }

  // ---- map settings
  renderMapSettings() {
    const d = this.doc, m = d.map, P = this.mapsetEl;
    if (!m) { fill(P); return; }
    const E = (label, keys, fn) => d.edit(label, keys, fn);
    const out = [el("h3", {}, "Map")];
    const f = el("div", { class: "form" });
    const add = (l, i) => f.append(el("label", {}, l), i);
    const txt = (v, on, ph) => { const i = el("input", { type: "text", value: v || "", placeholder: ph || "" }); i.addEventListener("change", () => on(i.value.trim())); return i; };
    add("file name", el("span", { class: "num" }, d.name || "(new)", d.readOnly ? el("span", { class: "warn" }, "  default: save a copy") : ""));
    add("title", txt(m.title, v => E("title", ["title"], () => { m.title = v.replace(/#/g, "").slice(0, 40); })));
    add("setting", txt(m.setting, v => E("setting", ["setting"], () => { m.setting = v.slice(0, 80); }), "the place (a city block, a space station...)"));
    add("built for", select([["", "(no mode)"], ...MODES], m.preferredMode || "", v => E("built for", ["preferredMode"], () => { m.preferredMode = v || null; })));
    add("hold fire (s)", numInput(m.holdfire || 0, v => E("hold fire", ["holdfire"], () => { m.holdfire = Math.max(0, Math.min(30, v)); }), { step: 1 }));
    const opt = (label, key, def) => {
      const on = m[key] !== null && m[key] !== undefined;
      add(label, el("div", { class: "v3" }, check("", on, v => E(label, [key], () => { m[key] = v ? def : null; })), on ? numInput(m[key], v => E(label, [key], () => { m[key] = clampX(v); }), { step: 1 }) : el("span", { class: "faint" }, "off")));
    };
    opt("void y", "void", -10);
    opt("indoor (roof y)", "indoor", 10);
    add("sun", el("div", { class: "v3" }, [0, 1, 2].map(k => numInput(m.sun[k], v => E("sun", ["sun"], () => { const s = m.sun.slice(); s[k] = v; const l = Math.hypot(...s) || 1; m.sun = s.map(x => x / l); }), { step: 0.1 }))));
    out.push(f);
    out.push(el("h3", {}, "Modes it is offered for", el("span", { class: "faint" }, "(none ticked: every mode it has the pieces for)")));
    out.push(el("div", { class: "chips" }, MODES.map(md => check(md, m.allowedModes.includes(md), v => E("modes", ["allowedModes"], () => { m.allowedModes = v ? [...m.allowedModes, md].sort((a, b) => MODES.indexOf(a) - MODES.indexOf(b)) : m.allowedModes.filter(x => x !== md); })))));
    out.push(el("h3", {}, "Ranked ladders"));
    out.push(el("div", { class: "chips" }, RANKED.map(r => check(r, m.ranked.includes(r), v => E("ranked", ["ranked"], () => { m.ranked = v ? [...m.ranked, r] : m.ranked.filter(x => x !== r); })))));
    out.push(el("h3", {}, "Palette", check("own colours", !!m.palette, v => E("palette", ["palette"], () => { m.palette = v ? { base: [0.62, 0.62, 0.64], accent: [0.4, 0.4, 0.43], metal: [0.3, 0.33, 0.37], glow: [1, 0.55, 0.15], sky: [0.62, 0.66, 0.72] } : null; }))));
    if (m.palette) {
      const pf = el("div", { class: "chips" });
      for (const k of ["base", "accent", "metal", "glow", "sky"]) {
        const c = el("input", { type: "color", value: rgbHex(m.palette[k] || [0.6, 0.6, 0.6]) });
        c.addEventListener("change", () => E("palette " + k, ["palette"], () => { m.palette = { ...m.palette, [k]: hexRgb(c.value) }; }));
        pf.append(el("label", {}, c, k));
      }
      out.push(pf);
    }
    out.push(el("h3", {}, "Payload checkpoints", el("span", { class: "faint" }, "(fractions of the track)")));
    out.push(txt(m.cartChecks.join(" "), v => E("cart checkpoints", ["cartChecks"], () => { m.cartChecks = v.split(/[\s,]+/).map(Number).filter(x => x > 0 && x < 1).sort((a, b) => a - b); }), "0.33 0.66"));
    out.push(el("h3", {}, "Materials"));
    const cols = this.app.view.matColors ? this.app.view.matColors.hex : [];
    out.push(el("table", { class: "t" }, MATERIALS.map((n, i) => el("tr", {}, el("td", {}, el("span", { class: "swatch", style: { background: cols[i] || "#888" } })), el("td", {}, i + " " + n), el("td", { class: "faint" }, MAT_NOTES[i])))));
    if (m.warnings && m.warnings.length) { out.push(el("h3", {}, "Warnings")); out.push(el("p", { class: "warn" }, m.warnings.join("\n"))); }
    fill(P, ...out);
  }
}
