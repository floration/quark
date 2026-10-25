// Groups you copy between maps, and the library. Copy (Ctrl+C) takes the selection (every kind of object: boxes,
// groups, spawns, lights, ladders, objective zones, the hill, payload path points, stairs records, custom objects, bot
// starts, player figures) with positions relative to its bounds; Paste (Ctrl+V) puts it at the cursor, or the view's
// centre, as one undo step. The clipboard is kept by the server (clipboard.json: it survives opening another map and
// restarts) and is also put on the system clipboard as JSON, so it pastes into another editor window too. The library
// (left panel) keeps named groups and custom objects (repeat.js) on disk: library/<name>.json next to serve.py, plus
// the shipped library_presets/.
import { KINDS } from "./doc.js";
import { el, fill, $, banner, dialog, prompt, check, numInput, validMapName, snap, tidy, plural } from "./util.js";
import { defFromBoxes, expandRepeat, fitBox, repeatCounts, turnDef, partBoxes, withoutPiece, withPart, sameDef, relink } from "./repeat.js";
export { expandRepeat } from "./repeat.js";

export const CLIP_FORMAT = "quark-map-group";
// selection kind -> the clipboard's field for it (every kind the editor selects). The hill is one value, not a list:
// a pasted hill replaces the map's (a map has one)
const LISTS = { brush: "brushes", group: "groups", spawn: "spawns", light: "lights", ladder: "ladders", site: "sites", point: "points", checkpoint: "checkpoints",
  stairs: "stairs", object: "objects", path: "path", hill: "hill", botstart: "botStarts", figure: "figures" };
const KIND_OF = Object.fromEntries(Object.entries(LISTS).map(([k, l]) => [l, k]));
const keyOf = (list) => KINDS[KIND_OF[list]].key;
const itemsOf = (c, l) => !c || !c[l] ? [] : KINDS[KIND_OF[l]].single ? [c[l]] : Array.isArray(c[l]) ? c[l] : [];

// moves one object (a copy) by d, whatever its shape
export function shift(shape, o, d) {
  if (shape === "box") { o.min = o.min.map((v, k) => tidy(v + d[k])); o.max = o.max.map((v, k) => tidy(v + d[k])); }
  else if (shape === "stairs") { o.x0 = tidy(o.x0 + d[0]); o.x1 = tidy(o.x1 + d[0]); o.z0 = tidy(o.z0 + d[2]); o.z1 = tidy(o.z1 + d[2]); o.base = tidy(o.base + d[1]); o.top = tidy(o.top + d[1]); }
  else if (shape === "arr") for (let k = 0; k < 3; k++) o[k] = tidy(o[k] + d[k]);
  else o.pos = o.pos.map((v, k) => tidy(v + d[k]));
}
// the selection as a clipboard group: {format, version, from, size, origin, <list>: [objects relative to the bounds'
// min]} (origin: where that min was, for Paste in place)
export function copySelection(d) {
  const sel = d.sel.filter(s => LISTS[s.k]);
  if (!sel.length) return null;
  const bb = d.selBounds(sel), neg = bb.min.map(v => -v);
  const out = { format: CLIP_FORMAT, version: 1, from: d.name || "", size: bb.max.map((v, k) => tidy(v - bb.min[k])), origin: bb.min.map(tidy) };
  for (const s of sel) {
    const o = structuredClone(d.obj(s)), l = LISTS[s.k];
    shift(KINDS[s.k].shape, o, neg);
    if (KINDS[s.k].single) out[l] = o; else (out[l] = out[l] || []).push(o);
  }
  return out;
}
export const clipCount = (c) => Object.values(LISTS).reduce((n, l) => n + itemsOf(c, l).length, 0);
export const isClip = (c) => !!c && c.format === CLIP_FORMAT && Array.isArray(c.size) && clipCount(c) > 0;
// the min corner a paste at `at` puts the group's bounds at: its floor's centre on `at`, snapped to the grid
export function pasteBase(clip, at, grid = 0.125) { return [snap(at[0] - clip.size[0] / 2, grid), tidy(at[1]), snap(at[2] - clip.size[2] / 2, grid)]; }
// pastes a clipboard group with its bounds' min corner at `base` (pasteBase: at the cursor; clip.origin: in place):
// one undo step, the copies selected
export function pasteGroup(d, clip, base) {
  if (!isClip(clip)) return [];
  const lists = Object.values(LISTS).filter(l => itemsOf(clip, l).length);
  const added = [];
  d.edit("paste " + plural(clipCount(clip), "object"), lists.map(keyOf), () => {
    for (const l of lists) {
      const kind = KIND_OF[l], K = KINDS[kind];
      for (const o of itemsOf(clip, l)) {
        const c = structuredClone(o); shift(K.shape, c, base);
        if (K.single) { d.put(K.key, c); added.push({ k: kind, i: 0 }); continue; }
        const arr = d.get(K.key); arr.push(c); added.push({ k: kind, i: arr.length - 1 });
      }
    }
  }, { touch: [] });
  d.select(added);
  return added;
}

// the selected boxes and groups replaced by a custom object of definition `def` made from them (`boxes`: their boxes),
// one unit big, so it is exactly those boxes (lib: the library item it is linked to): one undo step, the object selected
export function replaceWithObject(d, name, def, boxes, lib = undefined) {
  const mn = [0, 1, 2].map(k => Math.min(...boxes.map(b => b.min[k]))), goneB = new Set(d.selOf("brush")), goneG = new Set(d.selOf("group"));
  let at = -1;
  d.edit("make custom object " + name, ["brushes", "groups", "sc.objects"], (map, sc) => {
    if (goneB.size) map.brushes = map.brushes.filter((_, i) => !goneB.has(i));
    if (goneG.size) map.groups = map.groups.filter((_, i) => !goneG.has(i));
    sc.objects.push({ name, def, rot: 0, ...fitBox(def, { min: mn }, [1, 1, 1]), ...(lib ? { lib } : {}) }); at = sc.objects.length - 1;
  }, { touch: [] });
  d.select([{ k: "object", i: at }]);
  return at;
}
// the boxes of the selection a custom object is made from: selected boxes and the parts of selected groups
export const selectedBoxes = (d) => [...d.selOf("brush").map(i => d.map.brushes[i]), ...d.map.generated.filter(b => b.group !== undefined && d.isSel("group", b.group))];

// what a stamp (a clipboard group, a library group or object) looks like, as boxes relative to the corner it is placed
// at (Library.stampBase): the paste preview draws them under the cursor
export function stampBoxes(item) {
  if (item.kind === "object") return expandRepeat(item.def, fitBox(item.def, { min: [0, 0, 0] }, [1, 1, 1]));
  const c = item.clip, out = [];
  for (const l of Object.values(LISTS)) {
    const kind = KIND_OF[l], K = KINDS[kind];
    for (const o of itemsOf(c, l)) {
      if (kind === "object" && o.def) { out.push(...expandRepeat(o.def, o, o.rot || 0)); continue; }
      if (K.shape === "box") out.push({ min: o.min, max: o.max });
      else if (K.shape === "stairs") out.push({ min: [Math.min(o.x0, o.x1), o.base, Math.min(o.z0, o.z1)], max: [Math.max(o.x0, o.x1), o.top, Math.max(o.z0, o.z1)] });
      else {
        const p = K.shape === "arr" ? o : o.pos, r = kind === "light" || kind === "path" ? 0.25 : 0.4, h = r === 0.4 ? 1.8 : 0.25;
        out.push({ min: [p[0] - r, p[1] - (r === 0.4 ? 0 : r), p[2] - r], max: [p[0] + r, p[1] + h, p[2] + r] });
      }
    }
  }
  return out;
}

// a library item's thumbnail: its boxes drawn isometric (seen from +x +y +z, the 400 biggest), as SVG markup.
// colors: material index -> "#rrggbb"
export function thumbSvg(boxes, colors = [], w = 36, h = 26) {
  if (!boxes.length) return "";
  const vol = (b) => (b.max[0] - b.min[0]) * (b.max[1] - b.min[1]) * (b.max[2] - b.min[2]);
  const list = boxes.length > 400 ? boxes.slice().sort((a, b) => vol(b) - vol(a)).slice(0, 400) : boxes.slice();
  list.sort((a, b) => (a.min[0] + a.min[1] + a.min[2]) - (b.min[0] + b.min[1] + b.min[2]));
  const P = (x, y, z) => [(x - z) * 0.866, (x + z) * 0.5 - y];
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  const polys = [];
  const shade = (hex, k) => "#" + [1, 3, 5].map(i => Math.round(parseInt(hex.slice(i, i + 2), 16) * k).toString(16).padStart(2, "0")).join("");
  for (const b of list) {
    const [a, c] = [b.min, b.max], col = colors[b.mat] || "#9aa0a8";
    const faces = [[[a[0], c[1], a[2]], [c[0], c[1], a[2]], [c[0], c[1], c[2]], [a[0], c[1], c[2]], 1],
      [[c[0], a[1], a[2]], [c[0], c[1], a[2]], [c[0], c[1], c[2]], [c[0], a[1], c[2]], 0.75],
      [[a[0], a[1], c[2]], [c[0], a[1], c[2]], [c[0], c[1], c[2]], [a[0], c[1], c[2]], 0.55]];
    for (const f of faces) {
      const pts = f.slice(0, 4).map(q => P(...q));
      for (const [px, py] of pts) { x0 = Math.min(x0, px); x1 = Math.max(x1, px); y0 = Math.min(y0, py); y1 = Math.max(y1, py); }
      polys.push([pts, shade(col, f[4])]);
    }
  }
  const sw = Math.max(x1 - x0, 1e-3), sh = Math.max(y1 - y0, 1e-3), k = Math.min((w - 4) / sw, (h - 4) / sh);
  const ox = (w - sw * k) / 2 - x0 * k, oy = (h - sh * k) / 2 - y0 * k;
  const r = (v) => Math.round(v * 10) / 10;
  return '<svg xmlns="http://www.w3.org/2000/svg" width="' + w + '" height="' + h + '" viewBox="0 0 ' + w + " " + h + '">' +
    polys.map(([pts, col]) => '<polygon points="' + pts.map(([px, py]) => r(px * k + ox) + "," + r(py * k + oy)).join(" ") + '" fill="' + col + '" stroke="#0006" stroke-width="0.3"/>').join("") + "</svg>";
}
// an item's size and box count, for its library row
export function itemStats(item) {
  const bx = stampBoxes(item);
  if (!bx.length) return { size: [0, 0, 0], boxes: 0 };
  const size = [0, 1, 2].map(k => tidy(Math.max(...bx.map(b => b.max[k])) - Math.min(...bx.map(b => b.min[k]))));
  return { size, boxes: bx.length };
}
export const LIB_FORMAT = "quark-map-library";
// the library items in an imported .json: an exported library {format, items}, one item {kind, ...}, or a copied
// clipboard group (made a group named `fallback`)
export function importItems(obj, fallback = "imported") {
  if (obj && obj.format === LIB_FORMAT && Array.isArray(obj.items)) return obj.items.filter(x => x && (x.kind === "group" || x.kind === "object"));
  if (obj && (obj.kind === "group" || obj.kind === "object")) return [obj];
  if (isClip(obj)) return [{ kind: "group", name: fallback, note: plural(clipCount(obj), "object") + (obj.from ? " from " + obj.from : ""), clip: obj }];
  return [];
}
const bareItem = (x) => { const { file, builtin, ...rest } = x; return rest; };

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
    // (the paste event doesn't say whether Shift was down: Ctrl+Shift+V, paste in place, is noted from its keydown)
    let inPlace = false;
    document.addEventListener("keydown", (e) => { if ((e.ctrlKey || e.metaKey) && e.code === "KeyV") inPlace = e.shiftKey; }, true);
    document.addEventListener("paste", (e) => {
      const here = inPlace; inPlace = false;
      if (typing(e.target) || document.querySelector("dialog[open]")) return;
      let c = null;
      try { const o = JSON.parse(e.clipboardData.getData("text/plain")); if (isClip(o)) c = o; } catch { /* not ours: the editor's clipboard */ }
      e.preventDefault();
      this.paste(c, here);
    });
    this.doc.on("open", () => { this.editing = null; this.render(); });
    $("#libFilter").addEventListener("input", () => this.render());
    $("#libExport").addEventListener("click", () => this.exportAll());
    $("#libImport").addEventListener("click", () => $("#libFile").click());
    $("#libFile").addEventListener("change", (e) => { const f = [...e.target.files]; e.target.value = ""; if (f.length) this.importFiles(f); });
    this.doc.on("map", (keys, live) => { if (!live && keys && keys.includes("sc.objects")) this.render(); });   // (the "update N" buttons)
    this.load();
  }
  async load() {
    try { this.items = (await this.api.library()).items || []; } catch { this.items = []; }
    this.thumbs = null;   // (made again on the next render)
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
    banner("Copied " + plural(clipCount(c), "object") + ": Ctrl+V pastes them here or in another map (Ctrl+Shift+V: in place)", false, 3000);
    this.render();
    return c;
  }
  // Paste (Ctrl+V, the paste button): the clipboard follows the cursor as a preview and a click puts it there (Shift
  // + click: and keeps pasting; Esc: cancel). inPlace (Ctrl+Shift+V): at once, at the coordinates it was copied from
  async paste(clip = null, inPlace = false) {
    let c = clip || this.clip;
    if (!c) { try { const s = await this.api.clipboard(); if (isClip(s)) c = this.clip = s; } catch { /* (no server clipboard) */ } }
    if (!isClip(c)) { banner("Nothing copied yet: select objects and Copy (Ctrl+C)", true, 3000); return; }
    if (this.doc.readOnly) this.app.readOnlyNote();
    if (inPlace && Array.isArray(c.origin)) {
      pasteGroup(this.doc, c, c.origin);
      banner("Pasted " + plural(clipCount(c), "object") + " in place" + (c.from ? " (where they were in " + c.from + ")" : ""), false, 2500);
      return;
    }
    this.arm({ kind: "group", name: plural(clipCount(c), "copied object"), clip: c, paste: true });
  }

  // ---- the library
  async put(name, item) {
    try { await this.api.putLibrary(name, item); await this.load(); banner("Saved " + name + " in the library", false, 2500); }
    catch (e) { banner(e.message, true); }
  }
  async askName(title, def, linked = null) {
    const n = await prompt(title, def, "1-40 letters, digits, - and _ (library/<name>.json next to serve.py)");
    if (n === null) return null;
    if (!validMapName(n)) { banner("A name is 1-40 letters, digits, - and _", true); return null; }
    return this.nameFree(n, linked) ? n : null;
  }
  // a library name may be used: asks before hiding a shipped item or replacing one of yours (other than `linked`)
  nameFree(n, linked = null) {
    const had = this.items.find(x => x.file === n);
    if (had && had.builtin) return confirm(n + " is a shipped " + had.kind + ": yours will hide it in the library (delete yours to get it back). Use this name?");
    if (had && n !== linked) return confirm(n + " is in the library: replace it?");
    return true;
  }
  async saveGroup() {
    const c = copySelection(this.doc);
    if (!c) { banner("Select the objects to keep as a group", true, 2500); return; }
    const name = await this.askName("Save the selection as a group", "group");
    if (name) await this.put(name, { kind: "group", name, note: plural(clipCount(c), "object") + " from " + (this.doc.name || "a new map"), clip: c });
  }
  // a custom object from the selected boxes (and the parts of selected groups): its unit, the axes it repeats along
  async makeObject() {
    const d = this.doc;
    const boxes = selectedBoxes(d);
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
        el("label", {}, ""), check("replace the selected boxes and groups with the object", replace, v => { replace = v; }))],
      [{ label: "Cancel", value: "cancel" }, { label: "Make it", value: "ok", primary: true }]);
    if (r !== "ok") return;
    const n = name.value.trim() || "object";
    if (!validMapName(n)) { banner("A name is 1-40 letters, digits, - and _", true); return; }
    if (!axes.some(Boolean)) { banner("Tick at least one axis to repeat along", true); return; }
    const over = [0, 1, 2].filter(k => axes[k] && sp[k] < def.unit[k] - 1e-6);
    if (over.length && !confirm("The spacing along " + over.map(k => "xyz"[k]).join(", ") + " is under the unit's size (" + over.map(k => def.unit[k]).join(", ") + " m): the units will overlap. Make it anyway?")) return;
    if (!this.nameFree(n)) return;
    const made = defFromBoxes(n, boxes, axes, sp);
    if (replace) replaceWithObject(d, n, made, boxes, n);
    await this.put(n, { kind: "object", name: n, def: made });
  }
  // ---- linked objects: object.lib names the library item it was placed from (or saved as)
  item(file) { return this.items.find(x => x.file === file && x.kind === "object"); }
  // the placed objects of library item `file` in this map whose definition is not the library's
  stale(file) { const it = this.item(file); return it ? (this.doc.sidecar.objects || []).map((o, i) => ({ o, i })).filter(x => x.o.lib === file && !sameDef(x.o.def, it.def)) : []; }
  copiesOf(file) { return (this.doc.sidecar.objects || []).filter(o => o.lib === file).length; }
  // every placed copy of `file` in this map takes the library's definition (same place, turn and units): one step
  updateFromLibrary(file) {
    const it = this.item(file), list = this.stale(file); if (!it || !list.length) return 0;
    this.doc.edit("update " + plural(list.length, it.name || file) + " from the library", ["sc.objects"], (m, sc) => { for (const x of list) sc.objects[x.i] = relink(x.o, { ...it.def, name: it.name || file }); }, { touch: [] });
    banner("Updated " + plural(list.length, "placed copy") + " of " + (it.name || file) + " from the library", false, 3000);
    return list.length;
  }
  // object i's definition saved in the library (under its linked name, or a new one) and, asked, to every other copy
  async saveDef(i) {
    const d = this.doc, o = d.sidecar.objects[i]; if (!o) return;
    const n = await this.askName("Save this object in the library", o.lib || o.def.name || o.name || "object", o.lib);
    if (!n) return;
    const def = { ...o.def, name: n };
    await this.put(n, { kind: "object", name: n, def });
    const others = (d.sidecar.objects || []).filter((x, j) => j !== i && x.lib === n && !sameDef(x.def, def)).length;
    const relinkAll = others && confirm("Update the " + plural(others, "other placed copy") + " of " + n + " in this map too?");
    d.edit("link to library " + n, ["sc.objects"], (m, sc) => {
      sc.objects.forEach((x, j) => { if (j === i) sc.objects[j] = { ...x, lib: n, name: n, def }; else if (relinkAll && x.lib === n) sc.objects[j] = relink(x, def); });
    }, { touch: [] });
    d.select([{ k: "object", i }]);
  }
  async remove(item) {
    if (!confirm("Delete " + item.name + " from the library?")) return;
    try { await this.api.deleteLibrary(item.file); await this.load(); } catch (e) { banner(e.message, true); }
  }
  // the Stamp tool: a preview of the item follows the cursor and a click places it (a group pasted there; an object one
  // unit big, sitting on it). Library items keep stamping until Esc; a paste places once (Shift + click: again)
  arm(item) {
    const ed = this.app.editor;
    ed.stamp = item; ed.setTool("stamp"); ed.setGhost(item);
    banner("Click to place " + item.name + (item.paste ? " (Shift + click: and paste again; Esc: cancel)" : " (Esc: stop)"), false, 3000);
  }
  // the min corner the item goes at when stamped on point p (the preview is drawn there too)
  stampBase(item, p) {
    const g = this.app.editor.grid;
    if (item.kind === "group") return pasteBase(item.clip, p, g);
    const size = fitBox(item.def, { min: [0, 0, 0] }, [1, 1, 1]).max;
    return [snap(p[0] - size[0] / 2, g), tidy(p[1]), snap(p[2] - size[2] / 2, g)];
  }
  stampAt(s, again = false) {
    const ed = this.app.editor, item = ed.stamp, d = this.doc;
    if (!item) return;
    const min = this.stampBase(item, s.p);
    if (item.kind === "group") {
      pasteGroup(d, item.clip, min);
      if (item.paste && !again) { ed.stamp = null; ed.setTool("select"); }
      return;
    }
    const def = item.def;
    let at = -1;
    d.edit("add " + item.name, ["sc.objects"], (m, sc) => { sc.objects.push({ name: item.name, def: structuredClone(def), rot: 0, ...fitBox(def, { min }, [1, 1, 1]), lib: item.file }); at = sc.objects.length - 1; }, { touch: [] });
    d.select([{ k: "object", i: at }]);
  }
  // a custom object back to the plain boxes it is made of
  explode(i) {
    const d = this.doc, o = d.sidecar.objects[i]; if (!o) return;
    const boxes = expandRepeat(o.def, o, o.rot || 0), n0 = d.map.brushes.length;
    d.edit("make plain boxes", ["brushes", "sc.objects"], (m, sc) => { m.brushes.push(...boxes); sc.objects.splice(i, 1); }, { touch: [] });
    d.select(boxes.map((_, j) => ({ k: "brush", i: n0 + j })));
  }

  // Editing a custom object's unit or start / end piece in place: the part goes out as plain boxes (selected; for a
  // piece the object stays, without it, so you see where it joins), you move, resize, add or delete boxes, and Done
  // takes the selected boxes back in as that part (an empty selection removes a piece). Each of start, Done and
  // Cancel is one undo step. this.editing: {part, name, obj (the object as it is while edited), orig, at, added}
  editPart(i, part) {
    const d = this.doc, o = d.sidecar.objects[i]; if (!o) return;
    if (this.editing) this.finishEdit(false);
    const boxes = partBoxes(o, part), n0 = d.map.brushes.length, orig = structuredClone(o);
    const obj = part === "unit" ? null : withoutPiece(o, part);
    d.edit("edit the " + part + " of " + o.name, ["brushes", "sc.objects"], (m, sc) => {
      m.brushes.push(...boxes);
      if (obj) sc.objects[i] = obj; else sc.objects.splice(i, 1);
    }, { touch: [] });
    this.editing = { part, name: o.name, obj, orig, at: i, added: d.map.brushes.slice(n0) };
    d.select(boxes.map((_, j) => ({ k: "brush", i: n0 + j })));
    banner("Editing the " + part + " of " + o.name + ": change its boxes, then Done in Properties (the selected boxes become the " + part + ")", false, 6000);
  }
  // ok: the selected boxes become the part; else the object goes back as it was and the boxes put out are removed
  finishEdit(ok) {
    const d = this.doc, e = this.editing; if (!e) return;
    this.editing = null;
    const objs = d.sidecar.objects, at = e.obj ? objs.indexOf(e.obj) : -1;
    if (e.obj && at < 0) { banner("The object being edited is gone (undone?): editing stopped", true); this.app.panels.renderProps(); return; }
    const base = e.obj || e.orig;
    const sel = d.selOf("brush"), boxes = ok ? sel.map(i => d.map.brushes[i]) : [];
    const made = ok ? withPart(base, e.part, boxes.map(b => ({ mat: b.mat, min: b.min.slice(), max: b.max.slice() }))) : e.orig;
    if (!made) { this.editing = e; banner("Select the boxes of the unit first (a unit can't be empty)", true, 3000); return; }
    const gone = new Set(ok ? sel.map(i => d.map.brushes[i]) : e.added);
    let idx = -1;
    d.edit((ok ? "done editing the " : "cancel editing the ") + e.part + " of " + e.name, ["brushes", "sc.objects"], (m, sc) => {
      m.brushes = m.brushes.filter(b => !gone.has(b));
      if (at >= 0) { sc.objects[at] = made; idx = at; }
      else { idx = Math.min(e.at, sc.objects.length); sc.objects.splice(idx, 0, made); }
    }, { touch: [] });
    d.select([{ k: "object", i: idx }]);
    banner(ok ? "The " + e.part + " of " + e.name + " is now " + plural(boxes.length, "box") + (this.items.some(x => x.file === made.lib) ? " (the library's " + made.lib + " is unchanged: save to library to keep it there)" : "") : "Editing cancelled: " + e.name + " is as it was", false, 4000);
  }

  // ---- library management: edit (name, category, note), export, import
  async editItem(x) {
    const name = el("input", { type: "text", value: x.file, style: { width: "100%" } });
    const cat = el("input", { type: "text", value: x.category || "", placeholder: "e.g. railings, cover, props", style: { width: "100%" }, list: "libCats" });
    const note = el("input", { type: "text", value: x.note || "", style: { width: "100%" } });
    const cats = el("datalist", { id: "libCats" }, [...new Set(this.items.map(i => i.category).filter(Boolean))].sort().map(c => el("option", { value: c })));
    const r = await dialog([el("h2", {}, "Library item " + x.file), el("div", { class: "form" }, el("label", {}, "name"), name, el("label", {}, "category"), cat, el("label", {}, "note"), note), cats],
      [{ label: "Cancel", value: "cancel" }, { label: "Save", value: "ok", primary: true }]);
    if (r !== "ok") return;
    const n = name.value.trim();
    if (!validMapName(n)) { banner("A name is 1-40 letters, digits, - and _", true); return; }
    if (n !== x.file && n.toLowerCase() !== x.file.toLowerCase() && this.items.some(i => i.file === n)) { banner(n + " is in the library already", true); return; }
    try {
      if (n !== x.file) await this.api.renameLibrary(x.file, n);
      const item = { ...bareItem(x), name: n, category: cat.value.trim() || undefined, note: note.value.trim() || undefined };
      if (item.def) item.def = { ...item.def, name: n };
      await this.api.putLibrary(n, item);
      // the open map's objects linked to the old name follow it (copies in other maps keep the old link)
      const d = this.doc, linked = (d.sidecar.objects || []).some(o => o.lib === x.file);
      if (n !== x.file && linked) d.edit("rename library link " + x.file + " -> " + n, ["sc.objects"], (m, sc) => { sc.objects = sc.objects.map(o => o.lib === x.file ? { ...o, lib: n } : o); }, { touch: [] });
      await this.load();
      banner("Saved " + n, false, 2000);
    } catch (e) { banner(e.message, true); }
  }
  download(fname, obj) {
    const a = el("a", { href: URL.createObjectURL(new Blob([JSON.stringify(obj, null, 1)], { type: "application/json" })), download: fname });
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }
  exportAll() {
    const mine = this.items.filter(x => !x.builtin);
    if (!mine.length) { banner("Your library is empty (the shipped items come with every editor)", true, 3000); return; }
    this.download("quark-library.json", { format: LIB_FORMAT, version: 1, items: mine.map(x => ({ ...bareItem(x), file: x.file })) });
    banner("Exported " + plural(mine.length, "library item") + " to quark-library.json", false, 3000);
  }
  async importFiles(files) {
    let added = 0;
    for (const f of files) {
      let obj;
      try { obj = JSON.parse(await f.text()); } catch { banner(f.name + " isn't JSON", true); continue; }
      const items = importItems(obj, f.name.replace(/\.json$/i, "").replace(/[^A-Za-z0-9_-]/g, "_").slice(0, 40) || "imported");
      if (!items.length) { banner(f.name + " has no library items", true); continue; }
      for (const it of items) {
        let n = String(it.file || it.name || "imported").replace(/[^A-Za-z0-9_-]/g, "_").slice(0, 40);
        if (!validMapName(n)) n = "imported";
        if (!this.nameFree(n)) continue;
        try { await this.api.putLibrary(n, { ...bareItem(it), name: n }); added++; } catch (e) { banner(e.message, true); }
      }
    }
    await this.load();
    if (added) banner("Imported " + plural(added, "library item"), false, 3000);
  }

  render() {
    const rows = [], q = (($("#libFilter") || {}).value || "").trim().toLowerCase();
    const c = this.clip;
    rows.push(el("div", { class: "row faint", title: "Ctrl+C copies the selection, Ctrl+V pastes it (here or in another map)" },
      el("span", {}, c ? "clipboard: " + plural(clipCount(c), "object") + (c.from ? " from " + c.from : "") : "clipboard: empty")));
    const shown = (x) => !q || [x.file, x.name, x.note, x.category].some(v => v && String(v).toLowerCase().includes(q));
    const colors = (this.app.view && this.app.view.matColors && this.app.view.matColors.hex) || [];
    const thumbs = this.thumbs || (this.thumbs = new Map());
    const row = (x) => {
      const key = x.file + "|" + (x.builtin ? 1 : 0);
      let t = thumbs.get(key);
      if (!t) { const st = itemStats(x); t = { st, svg: thumbSvg(stampBoxes(x), colors) }; thumbs.set(key, t); }
      const rep = x.kind === "object" && x.def ? ["x", "y", "z"].filter((_, k) => x.def.axes[k]).join("") + " every " + x.def.spacing.filter((_, k) => x.def.axes[k]).join("/") + " m" + (x.def.start || x.def.end ? ", with " + [x.def.start ? "start" : "", x.def.end ? "end" : ""].filter(Boolean).join(" + ") + " piece" : "") : plural(clipCount(x.clip), "object");
      const stale = x.kind === "object" ? this.stale(x.file).length : 0, copies = x.kind === "object" ? this.copiesOf(x.file) : 0;
      const thumb = el("span", { class: "thumb", html: t.svg });
      rows.push(el("div", { class: "row", title: (x.note || "") + (x.category ? "\ncategory: " + x.category : "") + (copies ? "\n" + plural(copies, "placed copy") + " in this map" : "") + (x.builtin ? "\n(shipped with the editor)" : "\nlibrary/" + x.file + ".json"), onclick: () => this.arm(x) },
        thumb,
        el("span", { class: "ltxt" }, el("span", { class: "mname" }, x.name || x.file), el("span", { class: "sub" }, t.st.size.join(" x ") + " m, " + plural(t.st.boxes, "box") + " | " + rep)),
        stale ? el("button", { class: "mini warn", title: "update the " + plural(stale, "placed copy") + " in this map that differ from the library", onclick: (e) => { e.stopPropagation(); this.updateFromLibrary(x.file); } }, "update " + stale) : null,
        x.builtin ? el("span", { class: "tag" }, "shipped") : null,
        el("span", { class: "acts" },
          x.builtin ? null : el("button", { class: "mini", title: "name, category, note", onclick: (e) => { e.stopPropagation(); this.editItem(x); } }, "edit"),
          el("button", { class: "mini", title: "save it as a .json file", onclick: (e) => { e.stopPropagation(); this.download(x.file + ".json", { format: LIB_FORMAT, version: 1, items: [{ ...bareItem(x), file: x.file }] }); } }, "export"),
          x.builtin ? null : el("button", { class: "mini danger", title: "delete from the library", onclick: (e) => { e.stopPropagation(); this.remove(x); } }, "x"))));
    };
    const section = (title, list, empty) => {
      const vis = list.filter(shown).sort((a, b) => (a.category || "").localeCompare(b.category || "") || (a.name || a.file).localeCompare(b.name || b.file));
      rows.push(el("div", { class: "grp" }, title + " (" + (q && list.length ? vis.length + " of " + list.length : list.length) + ")"));
      let cat = null;
      for (const x of vis) { if ((x.category || "") !== cat) { cat = x.category || ""; if (cat || vis.some(v => v.category)) rows.push(el("div", { class: "cat" }, cat || "(no category)")); } row(x); }
      if (!list.length) rows.push(el("div", { class: "row faint" }, empty));
      else if (!vis.length) rows.push(el("div", { class: "row faint" }, "nothing matches " + q));
    };
    section("Custom objects", this.items.filter(x => x.kind === "object"), "none yet: select boxes, then + object");
    section("Groups", this.items.filter(x => x.kind === "group"), "none yet: select, then + group");
    fill(this.listEl, ...rows);
  }
}
export { repeatCounts, turnDef };
