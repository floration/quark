// Groups you copy between maps, and the library. Copy (Ctrl+C) takes the selection (boxes, groups, spawns, lights,
// ladders, objective zones, stairs records, custom objects) with positions relative to its bounds; Paste (Ctrl+V)
// puts it at the cursor, or the view's centre, as one undo step. The clipboard is kept by the server
// (clipboard.json: it survives opening another map and restarts) and is also put on the system clipboard as JSON,
// so it pastes into another editor window too. The library (left panel) keeps named groups and custom objects
// (repeat.js) on disk: library/<name>.json next to serve.py, plus the shipped library_presets/.
import { KINDS } from "./doc.js";
import { el, fill, $, banner, dialog, prompt, check, numInput, validMapName, snap, tidy } from "./util.js";
import { defFromBoxes, expandRepeat, repeatCounts, turnDef } from "./repeat.js";
export { expandRepeat } from "./repeat.js";

export const CLIP_FORMAT = "quark-map-group";
const LISTS = { brush: "brushes", group: "groups", spawn: "spawns", light: "lights", ladder: "ladders", site: "sites", point: "points", checkpoint: "checkpoints", stairs: "stairs", object: "objects" };
const KIND_OF = Object.fromEntries(Object.entries(LISTS).map(([k, l]) => [l, k]));
const keyOf = (list) => KINDS[KIND_OF[list]].key;

// moves one object (a copy) by d, whatever its shape
function shift(shape, o, d) {
  if (shape === "box") { o.min = o.min.map((v, k) => tidy(v + d[k])); o.max = o.max.map((v, k) => tidy(v + d[k])); }
  else if (shape === "stairs") { o.x0 = tidy(o.x0 + d[0]); o.x1 = tidy(o.x1 + d[0]); o.z0 = tidy(o.z0 + d[2]); o.z1 = tidy(o.z1 + d[2]); o.base = tidy(o.base + d[1]); o.top = tidy(o.top + d[1]); }
  else if (shape === "arr") for (let k = 0; k < 3; k++) o[k] = tidy(o[k] + d[k]);
  else o.pos = o.pos.map((v, k) => tidy(v + d[k]));
}
// the selection as a clipboard group: {format, version, from, size, <list>: [objects relative to the bounds' min]}
export function copySelection(d) {
  const sel = d.sel.filter(s => LISTS[s.k]);
  if (!sel.length) return null;
  const bb = d.selBounds(sel), neg = bb.min.map(v => -v);
  const out = { format: CLIP_FORMAT, version: 1, from: d.name || "", size: bb.max.map((v, k) => tidy(v - bb.min[k])) };
  for (const s of sel) {
    const o = structuredClone(d.obj(s));
    shift(KINDS[s.k].shape, o, neg);
    (out[LISTS[s.k]] = out[LISTS[s.k]] || []).push(o);
  }
  return out;
}
export const clipCount = (c) => Object.keys(LISTS).reduce((n, k) => n + ((c && c[LISTS[k]]) || []).length, 0);
export const isClip = (c) => !!c && c.format === CLIP_FORMAT && Array.isArray(c.size) && clipCount(c) > 0;
// pastes a clipboard group with its floor's centre at `at` (snapped to the grid): one undo step, the copies selected
export function pasteGroup(d, clip, at, grid = 0.125) {
  if (!isClip(clip)) return [];
  const base = [snap(at[0] - clip.size[0] / 2, grid), tidy(at[1]), snap(at[2] - clip.size[2] / 2, grid)];
  const lists = Object.values(LISTS).filter(l => Array.isArray(clip[l]) && clip[l].length);
  const added = [];
  d.edit("paste " + clipCount(clip) + " objects", lists.map(keyOf), () => {
    for (const l of lists) {
      const arr = d.get(keyOf(l)), shape = KINDS[KIND_OF[l]].shape;
      for (const o of clip[l]) { const c = structuredClone(o); shift(shape, c, base); arr.push(c); added.push({ k: KIND_OF[l], i: arr.length - 1 }); }
    }
  }, { touch: [] });
  d.select(added);
  return added;
}

export class Library {
  constructor(app) {
    this.app = app; this.doc = app.doc; this.api = app.api;
    this.items = []; this.clip = null;
    this.listEl = $("#library");
    $("#libCopy").addEventListener("click", () => this.copy());
    $("#libPaste").addEventListener("click", () => this.paste());
    $("#libGroup").addEventListener("click", () => this.saveGroup());
    $("#libObject").addEventListener("click", () => this.makeObject());
    // Ctrl+C / Ctrl+V: the copy / paste events (they carry the system clipboard without asking for a permission)
    const typing = (t) => t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT" || t.isContentEditable);
    document.addEventListener("copy", (e) => {
      if (typing(e.target) || document.querySelector("dialog[open]")) return;
      const c = this.copy(true);
      if (c) { e.clipboardData.setData("text/plain", JSON.stringify(c)); e.preventDefault(); }
    });
    document.addEventListener("paste", (e) => {
      if (typing(e.target) || document.querySelector("dialog[open]")) return;
      let c = null;
      try { const o = JSON.parse(e.clipboardData.getData("text/plain")); if (isClip(o)) c = o; } catch { /* not ours: the editor's clipboard */ }
      e.preventDefault();
      this.paste(c);
    });
    this.load();
  }
  async load() {
    try { this.items = (await this.api.library()).items || []; } catch { this.items = []; }
    try { const c = await this.api.clipboard(); this.clip = isClip(c) ? c : null; } catch { this.clip = null; }
    this.render();
  }

  // ---- clipboard
  copy(quiet = false) {
    const c = copySelection(this.doc);
    if (!c) { if (!quiet) banner("Select something to copy", true, 2500); return null; }
    this.clip = c;
    this.api.setClipboard(c).catch(e => banner("The clipboard wasn't kept: " + e.message, true));
    if (!quiet && navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(JSON.stringify(c)).catch(() => {});
    banner("Copied " + clipCount(c) + " objects: Ctrl+V pastes them here or in another map", false, 3000);
    this.render();
    return c;
  }
  async paste(clip = null) {
    let c = clip || this.clip;
    if (!c) { try { const s = await this.api.clipboard(); if (isClip(s)) c = this.clip = s; } catch { /* (no server clipboard) */ } }
    if (!isClip(c)) { banner("Nothing copied yet: select objects and Copy (Ctrl+C)", true, 3000); return; }
    if (this.doc.readOnly) this.app.readOnlyNote();
    pasteGroup(this.doc, c, this.app.editor.pastePoint(), this.app.editor.grid);
    banner("Pasted " + clipCount(c) + " objects" + (c.from ? " from " + c.from : ""), false, 2500);
  }

  // ---- the library
  async put(name, item) {
    try { await this.api.putLibrary(name, item); await this.load(); banner("Saved " + name + " in the library", false, 2500); }
    catch (e) { banner(e.message, true); }
  }
  async askName(title, def) {
    const n = await prompt(title, def, "1-40 letters, digits, - and _ (library/<name>.json next to serve.py)");
    if (n === null) return null;
    if (!validMapName(n)) { banner("A name is 1-40 letters, digits, - and _", true); return null; }
    if (this.items.some(x => x.file === n && !x.builtin) && !confirm(n + " is in the library: replace it?")) return null;
    return n;
  }
  async saveGroup() {
    const c = copySelection(this.doc);
    if (!c) { banner("Select the objects to keep as a group", true, 2500); return; }
    const name = await this.askName("Save the selection as a group", "group");
    if (name) await this.put(name, { kind: "group", name, note: clipCount(c) + " objects from " + (this.doc.name || "a new map"), clip: c });
  }
  // a custom object from the selected boxes (and the parts of selected groups): its unit, the axes it repeats along
  async makeObject() {
    const d = this.doc, m = d.map;
    const boxes = [...d.selOf("brush").map(i => m.brushes[i]), ...m.generated.filter(b => b.group !== undefined && d.isSel("group", b.group))];
    if (!boxes.length) { banner("Select the boxes of one unit (a post and its rail, a pillar...) first", true, 4000); return; }
    const def = defFromBoxes("object", boxes);
    const name = el("input", { type: "text", value: "object", style: { width: "100%" } });
    const axes = [true, false, false], sp = def.spacing.slice();
    let replace = true;
    const r = await dialog([el("h2", {}, "Make a custom object"),
      el("p", { class: "dim" }, "The " + boxes.length + " boxes are one unit (" + def.unit.join(" x ") + " m). Placed in a map, the object grows by whole units along the axes it repeats on: a longer object has more units, never stretched ones."),
      el("div", { class: "form" },
        el("label", {}, "name"), name,
        el("label", {}, "repeats along"), el("div", { class: "chips" }, ["x", "y (up)", "z"].map((a, k) => check(a, axes[k], v => { axes[k] = v; }))),
        el("label", { title: "from one unit's start to the next (the unit's size: units touch)" }, "spacing (m)"), el("div", { class: "v3" }, [0, 1, 2].map(k => numInput(sp[k], v => { sp[k] = Math.max(0.05, Math.min(64, v)); }, { step: 0.25, min: 0.05 }))),
        el("label", {}, ""), check("replace the selected boxes with the object", replace, v => { replace = v; }))],
      [{ label: "Cancel", value: "cancel" }, { label: "Make it", value: "ok", primary: true }]);
    if (r !== "ok") return;
    const n = name.value.trim() || "object";
    if (!validMapName(n)) { banner("A name is 1-40 letters, digits, - and _", true); return; }
    if (!axes.some(Boolean)) { banner("Tick at least one axis to repeat along", true); return; }
    const made = defFromBoxes(n, boxes, axes, sp);
    if (replace && d.selOf("brush").length) {
      const mn = [0, 1, 2].map(k => Math.min(...boxes.map(b => b.min[k]))), gone = new Set(d.selOf("brush"));
      let at = -1;
      d.edit("make custom object " + n, ["brushes", "sc.objects"], (map, sc) => {
        map.brushes = map.brushes.filter((_, i) => !gone.has(i));
        sc.objects.push({ name: n, def: made, rot: 0, min: mn, max: mn.map((v, k) => tidy(v + made.unit[k])) }); at = sc.objects.length - 1;
      }, { touch: [] });
      d.select([{ k: "object", i: at }]);
    }
    await this.put(n, { kind: "object", name: n, def: made });
  }
  saveDef(def) { this.askName("Save this object in the library", def.name || "object").then(n => n && this.put(n, { kind: "object", name: n, def: { ...def, name: n } })); }
  async remove(item) {
    if (!confirm("Delete " + item.name + " from the library?")) return;
    try { await this.api.deleteLibrary(item.file); await this.load(); } catch (e) { banner(e.message, true); }
  }
  // the Stamp tool: the next click places the item (a group pasted there; an object one unit big, sitting on it)
  arm(item) {
    const ed = this.app.editor;
    ed.stamp = item; ed.setTool("stamp");
    banner("Click where " + item.name + " goes (Esc: cancel)", false, 3000);
  }
  stampAt(s) {
    const item = this.app.editor.stamp, d = this.doc, ed = this.app.editor;
    if (!item) return;
    if (item.kind === "group") { pasteGroup(d, item.clip, s.p, ed.grid); return; }
    const def = item.def, p = s.p, g = ed.grid;
    const min = [snap(p[0] - def.unit[0] / 2, g), tidy(p[1]), snap(p[2] - def.unit[2] / 2, g)];
    let at = -1;
    d.edit("add " + item.name, ["sc.objects"], (m, sc) => { sc.objects.push({ name: item.name, def: structuredClone(def), rot: 0, min, max: min.map((v, k) => tidy(v + def.unit[k])) }); at = sc.objects.length - 1; }, { touch: [] });
    d.select([{ k: "object", i: at }]);
  }
  // a custom object back to the plain boxes it is made of
  explode(i) {
    const d = this.doc, o = d.sidecar.objects[i]; if (!o) return;
    const boxes = expandRepeat(o.def, o, o.rot || 0), n0 = d.map.brushes.length;
    d.edit("make plain boxes", ["brushes", "sc.objects"], (m, sc) => { m.brushes.push(...boxes); sc.objects.splice(i, 1); }, { touch: [] });
    d.select(boxes.map((_, j) => ({ k: "brush", i: n0 + j })));
  }

  render() {
    const rows = [];
    const c = this.clip;
    rows.push(el("div", { class: "row faint", title: "Ctrl+C copies the selection, Ctrl+V pastes it (here or in another map)" },
      el("span", {}, c ? "clipboard: " + clipCount(c) + " objects" + (c.from ? " from " + c.from : "") : "clipboard: empty")));
    const groups = this.items.filter(x => x.kind === "group"), objs = this.items.filter(x => x.kind === "object");
    const row = (x, sub) => rows.push(el("div", { class: "row", title: (x.note || "") + (x.builtin ? "\n(shipped with the editor)" : "\nlibrary/" + x.file + ".json"), onclick: () => this.arm(x) },
      el("span", { class: "mname" }, x.name || x.file), el("span", { class: "sub" }, sub),
      x.builtin ? el("span", { class: "tag" }, "shipped") : el("span", { class: "acts" }, el("button", { class: "mini danger", title: "delete from the library", onclick: (e) => { e.stopPropagation(); this.remove(x); } }, "x"))));
    rows.push(el("div", { class: "grp" }, "Custom objects (" + objs.length + ")"));
    for (const x of objs) row(x, ["x", "y", "z"].filter((_, k) => x.def && x.def.axes[k]).join("") + " every " + (x.def ? x.def.spacing.filter((_, k) => x.def.axes[k]).join("/") : "?") + " m");
    rows.push(el("div", { class: "grp" }, "Groups (" + groups.length + ")"));
    for (const x of groups) row(x, clipCount(x.clip) + " objects");
    if (!groups.length) rows.push(el("div", { class: "row faint" }, "none yet: select, then + group"));
    fill(this.listEl, ...rows);
  }
}
export { repeatCounts, turnDef };
