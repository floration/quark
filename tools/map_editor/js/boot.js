// Start: three.js, the server's info, the decoration scripts, then the editor's parts wired together.
import * as THREE from "../vendor/three.module.min.js";
import { api } from "./api.js";
import { Doc, KINDS, objectiveName } from "./doc.js";
import { validateMap } from "./validate.js";
import { stripObjects, repeatCounts, turnDef } from "./repeat.js";
import { View } from "./view.js";
import { Editor, GRIDS } from "./edit.js";
import { Panels } from "./panels.js";
import { Player } from "./player.js";
import { Testing } from "./testing.js";
import { Bots } from "./bots.js";
import { Library } from "./groups.js";
import { el, fill, $, $$, Emitter, banner, dialog, prompt, check, validMapName, fmt } from "./util.js";
import { MATERIALS, MODES } from "./fallback_parse.js";

window.THREE = THREE;
const MARKER_KEYS = new Set(["spawns", "lights", "sites", "hill", "points", "checkpoints", "path", "cartChecks", "ladders", "stairs", "groups", "sc.botStarts", "sc.objects", "*"]);
const SIDE_KEYS = ["figures", "botStarts", "tests", "deco", "camera", "bots"];

class App extends Emitter {
  async start() {
    this.api = api;
    try { await api.init(); } catch (e) { banner("The editor's server isn't answering: start it with run.cmd / run.sh (" + e.message + ")", true, 0); throw e; }
    const info = api.info;
    $("#gameVersion").textContent = info.version ? "v" + info.version : "";
    if (EMBED) $("#top").append($("#gameVersion"));   // (the version stays in sight without the brand line)
    if (!info.maptool) banner("quark_maptool wasn't found next to serve.py: maps open and save with the page's own reader, checks, metrics and bots are off. Build it and rebuild the editor.", true, 0);
    for (const n of info.deco || []) {   // the decoration part (js/deco*.js), before the editor starts
      try { await import("./" + n); } catch (e) { console.error("decoration script " + n + " failed:", e); }
    }
    this.doc = new Doc();
    this.view = new View($("#gl"), this.doc);
    this.tip = $("#tip");
    this.editor = new Editor(this);
    this.player = new Player(this);
    this.panels = new Panels(this);
    this.testing = new Testing(this);
    this.bots = new Bots(this);
    this.library = new Library(this);
    this.origin = { origin: "human", note: "" };
    this.wire();
    await this.panels.loadList();
    const want = new URLSearchParams(location.search);
    const L = this.panels.list;
    const first = want.get("map") ? { src: want.get("src") || (L.mine.some(m => m.name === want.get("map")) ? "mine" : "default"), name: want.get("map") }
      : L.mine.length ? { src: "mine", name: L.mine[0].name } : L.defaults.find(m => m.name === "temple") ? { src: "default", name: "temple" } : L.defaults[0] ? { src: "default", name: L.defaults[0].name } : null;
    if (first) await this.openMap(first.src, first.name, true); else this.newMap(true);
    this.mountDeco();
    window.qme = this;   // (for the console and the headless tests)
    this.ready = true;
  }

  wire() {
    const d = this.doc, v = this.view, ed = this.editor;
    d.on("map", (keys, live, change) => {
      if (!keys) { v.setPalette(d.map); v.rebuildBrushes(); v.rebuildMarkers(); v.rebuildGrid(0); ed.pickDirty = true; return; }
      if (keys.some(k => k === "palette" || k === "sun" || k === "*")) { v.setPalette(d.map); }
      const geo = keys.some(k => k === "brushes" || k === "groups" || k === "ladders" || k === "sc.objects" || k === "*");
      if (geo) {
        // boxes changed in place (a drag, a nudge, a material): only their instances and their picking cells
        const inPlace = keys.every(k => k === "brushes" || k.startsWith("sc.")) && change && change.touched.brushes && !change.structural.has("brushes");
        if (!(inPlace && v.updateBrushes(change.touched.brushes))) { v.rebuildBrushes(); ed.pickDirty = true; }
        else ed.pickMoved(change.touched.brushes);
      }
      if (keys.some(k => MARKER_KEYS.has(k))) v.rebuildMarkers();   // (markers aren't in the picking grid: tested one by one)
      v.redraw();
    });
    let figSel = "";
    d.on("selection", () => {
      v.updateSelectionTint(); v.restyleMarkers();
      const fs = d.selOf("figure").join(",");   // (the figures redraw only when their own selection changed)
      if (fs !== figSel) { figSel = fs; this.player.redraw(); }
      const b = d.selBounds();
      const y = b ? Math.round(b.min[1] * 8) / 8 : 0;
      if (y !== v.gridY) v.rebuildGrid(y);
      this.hud();
    });
    d.on("dirty", () => this.mapLabel());
    d.on("open", () => { this.mapLabel(); if (d.map.preferredMode && MODES.includes(d.map.preferredMode)) $("#playMode").value = d.map.preferredMode; });
    this.on("tool", () => { $$("#tools button").forEach(b => b.classList.toggle("on", b.dataset.tool === ed.tool)); $("#toolMat").value = ed.mat; this.hud(); });
    v.onAfterRender = () => this.afterRender();

    // toolbar
    $$("#tools button").forEach(b => b.addEventListener("click", () => this.setTool(b.dataset.tool)));
    $("#toolMat").append(...MATERIALS.map((n, i) => el("option", { value: i }, i + " " + n)));
    $("#toolMat").addEventListener("change", e => { ed.mat = +e.target.value; });
    $("#toolTeam").append(...["a", "b", "any", "race", "att0", "att1", "att2", "def0", "def1", "def2"].map(t => el("option", { value: t }, t)));
    $("#toolTeam").addEventListener("change", e => { ed.team = e.target.value; });
    $("#grid").append(...GRIDS.map((g, i) => el("option", { value: i }, g < 1 ? "1/" + Math.round(1 / g) + " m" : g + " m")));
    $("#grid").value = ed.gridIdx;
    $("#grid").addEventListener("change", e => this.setGrid(+e.target.value));
    $$("#camModes button").forEach(b => b.addEventListener("click", () => this.setCam(b.dataset.cam)));
    const showItems = [["clip", "clip boxes (invisible in play)"], ["far", "far scenery"], ["generated", "group parts and ladder rungs"], ["zones", "objective zones"], ["spawns", "spawns"],
      ["lights", "lights"], ["ladders", "ladder volumes"], ["path", "payload path"], ["stairs", "stairs records"], ["labels", "labels"], ["grid", "1 m grid"], ["panels", "2 m panel seams"], ["continuous", "render every frame (fps test)"]];
    $("#showList").append(...showItems.map(([k, label]) => check(label, v.show[k], val => {
      v.show[k] = val;
      if (["clip", "far", "generated"].includes(k)) { v.rebuildBrushes(); ed.pickDirty = true; }
      if (k === "grid") v.rebuildGrid(v.gridY || 0);
      if (k === "panels") v.uniforms.uPanels.value = val ? 1 : 0;
      v.rebuildMarkers(); v.redraw();
    })));
    $("#showMenu > button").addEventListener("click", () => $("#showMenu").classList.toggle("open"));
    document.addEventListener("mousedown", e => { if (!e.target.closest("#showMenu")) $("#showMenu").classList.remove("open"); });
    $$("#rtabs button").forEach(b => b.addEventListener("click", () => this.rtab(b.dataset.rtab)));
    $("#playMode").append(...MODES.map(m => el("option", { value: m }, m)));

    $("#btnNew").addEventListener("click", () => this.newMap());
    $("#btnSave").addEventListener("click", () => this.save());
    $("#btnSaveAs").addEventListener("click", () => this.saveAs());
    $("#btnUndo").addEventListener("click", () => d.undo());
    $("#btnRedo").addEventListener("click", () => d.redo());
    $("#btnOpenGame").addEventListener("click", () => this.launch("editor"));
    $("#btnPlay").addEventListener("click", () => this.launch("play"));
    $("#btnSettings").addEventListener("click", () => this.settings());
    $("#btnHelp").addEventListener("click", () => this.help());
    window.addEventListener("beforeunload", e => { if (d.dirty) { e.preventDefault(); e.returnValue = ""; } });
    setInterval(() => this.hud(), 500);
  }

  rtab(t) { $$("#rtabs button").forEach(b => b.classList.toggle("on", b.dataset.rtab === t)); $$(".rpanel").forEach(p => p.classList.toggle("on", p.id === "rp-" + t)); this.emit("rtab", t); }
  setTool(t) { this.editor.setTool(t); }
  setGrid(i) { this.editor.gridIdx = i; $("#grid").value = i; this.hud(); }
  setCam(m) { this.view.setCamMode(m); $$("#camModes button").forEach(b => b.classList.toggle("on", b.dataset.cam === m)); this.hud(); }
  frameSel() { const b = this.doc.selBounds(); if (b) this.view.frame(b.min, b.max); }
  describe(ref) {
    const m = this.doc.map, K = KINDS[ref.k];
    if (ref.k === "brush") return "box #" + ref.i + " " + MATERIALS[m.brushes[ref.i].mat];
    if (ref.k === "group") return "group #" + ref.i + " " + m.groups[ref.i].type + (ref.gen !== undefined ? " (" + MATERIALS[m.generated[ref.gen].mat] + " part)" : "");
    if (ref.k === "spawn") return "spawn #" + ref.i + " team " + m.spawns[ref.i].team;
    if (ref.k === "object") { const o = this.doc.sidecar.objects[ref.i]; return "custom object #" + ref.i + " " + (o ? o.name + " (" + repeatCounts(turnDef(o.def, o.rot || 0), o).join(" x ") + ")" : ""); }
    return objectiveName(m, ref.k, ref.i);
  }
  readOnlyNote() { if (!this.roNoted) { this.roNoted = true; banner("This is a default map: your changes are kept when you Save as a map of your own.", false, 6000); } }

  mapLabel() {
    const d = this.doc;
    const L = $("#mapLabel");
    fill(L, el("span", { class: d.dirty ? "dirty" : "" }, (d.map && d.map.title) || d.name || "new map", d.dirty ? " *" : ""),
      el("span", { class: "faint" }, "  " + (d.name ? d.name + ".qmap" : "(not saved)")),
      d.readOnly ? el("span", { class: "ro" }, "default, read-only") : null,
      el("span", { class: "faint" }, "  " + this.origin.origin));
    document.title = (d.dirty ? "* " : "") + ((d.map && d.map.title) || d.name || "new map") + " - Quark Map Editor";
    $("#btnUndo").disabled = !d.undoStack.length; $("#btnRedo").disabled = !d.redoStack.length;
    $("#btnUndo").title = d.undoStack.length ? "Undo " + d.undoStack[d.undoStack.length - 1].label + " (Ctrl+Z)" : "Undo (Ctrl+Z)";
    $("#btnRedo").title = d.redoStack.length ? "Redo " + d.redoStack[d.redoStack.length - 1].label + " (Ctrl+Y)" : "Redo (Ctrl+Y)";
  }
  hud() {
    const d = this.doc, ed = this.editor, v = this.view; if (!d.map) return;
    const c = v.cam, g = GRIDS[ed.gridIdx];
    const lines = [
      "tool " + ed.tool + "   grid " + (g < 1 ? "1/" + Math.round(1 / g) : g) + " m   " + v.camMode + (v.camMode === "fly" ? " speed " + Math.round(c.speed) : ""),
      v.camMode === "top" ? "centre " + fmt(c.target.x, 1) + " " + fmt(c.target.z, 1) : "eye " + [c.pos.x, c.pos.y, c.pos.z].map(x => fmt(x, 1)).join(" "),
    ];
    if (d.sel.length) lines.push(d.sel.length === 1 ? this.describe(d.sel[0]) : d.sel.length + " selected");
    if (this.hudDrag) lines.push(this.hudDrag);
    if (ed.tool === "route" && this.testing.routeA) lines.push("route: click the end point");
    if (ed.tool === "stamp" && ed.stamp) lines.push("click to place " + ed.stamp.name + " (Esc: cancel)");
    $("#hudInfo").textContent = lines.join("\n");
    const s = v.stats();
    $("#hudStats").textContent = (d.map.brushes.length + d.map.generated.length) + " boxes  " + (s ? s.calls + " draws\n" + (v.show.continuous ? Math.round(s.fps) + " fps  1% " + Math.round(s.low1) + "  0.1% " + Math.round(s.low01) + "\n" : "") + "render " + s.cpu.toFixed(1) + " ms cpu" : "");
  }
  afterRender() {   // the scale bar: a round length near 100 px at the screen's centre
    const v = this.view;
    let at = null;
    if (v.camMode !== "top") {
      const r = v.canvas.getBoundingClientRect();
      if (!this.editor.pickDirty) {
        const ray = v.ray(r.left + r.width / 2, r.top + r.height / 2);
        const h = this.editor.picker.cast(ray.o, ray.d, 2000)[0];
        if (h) at = ray.o.map((o, k) => o + ray.d[k] * h.t);
      }
    }
    const ppm = v.pxPerMetre(at);
    const nice = [0.25, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000];
    const len = nice.find(n => n * ppm >= 60) || 1000;
    $("#scalebar .bar").style.width = Math.round(len * ppm) + "px";
    $("#scalebar span").textContent = len + " m";
    if (this.hudDrag !== this.lastHudDrag) { this.lastHudDrag = this.hudDrag; this.hud(); }
  }

  // ---- maps
  confirmDiscard() { return !this.doc.dirty || confirm("Drop your unsaved changes to " + (this.doc.name || "this map") + "?"); }
  async openMap(src, name, quiet) {
    if (!quiet && !this.confirmDiscard()) return;
    try {
      const text = await api.readMap(src, name);
      const map = await api.tool("parse", { text, name });
      if (map.error) throw new Error(map.error);
      let side = {};
      try { side = await api.readSidecar(name); } catch { side = {}; }
      const dropped = stripObjects(map, side);   // (custom objects' boxes are drawn from the objects, not kept twice)
      if (dropped.length) banner("Custom object" + (dropped.length > 1 ? "s " : " ") + dropped.join(", ") + " no longer matched the map (edited elsewhere?): kept as plain boxes", true, 8000);
      if (src === "default") this.origin = { origin: "ai", note: "edited from " + name, base: name };
      else {
        const L = this.panels.list.mine.find(m => m.name === name);
        this.origin = L && L.provenance ? { origin: L.provenance.origin, note: L.provenance.note || "" } : { origin: map.handmade ? "human" : "ai", note: "" };
      }
      this.roNoted = false;
      this.doc.open(map, name, src, side, text);
      if (side.camera && side.camera.pos) { Object.assign(this.view.cam, { yaw: side.camera.yaw, pitch: side.camera.pitch }); this.view.cam.pos.fromArray(side.camera.pos); this.view.redraw(); }
      else this.view.frameMap();
      history.replaceState(null, "", pageUrl({ src, map: name }));
      this.emit("opened");
    } catch (e) { banner("Couldn't open " + name + ": " + e.message, true, 0); console.error(e); }
  }
  async newMap(quiet) {
    if (!quiet && !this.confirmDiscard()) return;
    const text = "version 2\nhandmade\ntitle New Map\nbox -16 -1 -16 16 0 16\nspawn a -8 0 0 270\nspawn b 8 0 0 90\n";
    const map = await api.tool("parse", { text, name: "" });
    this.origin = { origin: "human", note: "made in the map editor" };
    this.doc.open(map, "", "new", {});
    this.view.frameMap();
    history.replaceState(null, "", pageUrl({}));
    this.emit("opened");
  }
  mapForSave() {
    const skip = new Set(["generated", "materials", "player", "warnings", "fallback", "modesSupported", "modesPossible", "payloadLength", "bounds", "ms", "killY", "humanMade"]);
    const m = structuredClone(Object.fromEntries(Object.entries(this.doc.map).filter(([k]) => !skip.has(k))));
    // custom objects (sidecar) go into the .qmap as the plain boxes they are made of, after the map's own
    for (const b of this.doc.map.generated) if (b.object !== undefined) m.brushes.push({ mat: b.mat, min: b.min.slice(), max: b.max.slice() });
    // a map changed here is human-made; an unedited one keeps its own line, so it saves byte for byte as its file
    m.handmade = !!this.doc.map.handmade || this.doc.mapEdited;
    return m;
  }
  sidecarForSave() {
    const s = {}; const sc = this.doc.sidecar;
    for (const [k, v] of Object.entries(sc)) if (v !== undefined && !(Array.isArray(v) && !v.length && (k === "figures" || k === "botStarts" || k === "objects"))) s[k] = v;
    const c = this.view.cam; s.camera = { pos: c.pos.toArray().map(x => Math.round(x * 100) / 100), yaw: c.yaw, pitch: c.pitch };
    return s;
  }
  provenance() {
    const o = this.origin; let note = o.note || "";
    const deco = this.doc.sidecar.deco;
    if (deco && Array.isArray(deco.rulesApplied) && deco.rulesApplied.length) note = (note ? note + "; " : "") + "decorations generated by rules " + deco.rulesApplied.join(", ");
    return { origin: o.origin, by: "q", note };
  }
  // The checks before a save: the page's own (validate.js: the game's limits, spawns in walls) and the helper's
  // (serialize answers valid: false when the game's Map::parse would refuse the text). Errors refuse the save and
  // show where; warnings go with it. -> the text to write, or null (refused)
  async checkedText() {
    const d = this.doc;
    // unedited (or edited and undone): the file's own text, so a copy of a map is that map byte for byte (Map::serialize
    // orders and spaces some lines differently from make_maps.py and keeps no comments); edited: Map::serialize
    if (!d.mapEdited && typeof d.fileText === "string") return { text: d.fileText, warnings: [] };
    const m = this.mapForSave(), v = validateMap({ ...m, generated: d.map.generated });
    if (v.errors.length) { this.refuseSave(v.errors); return null; }
    const ans = await api.tool("serialize", m);
    if (ans.error) throw new Error(ans.error);
    if (ans.valid === false) { this.refuseSave([{ msg: "the game can't load it: " + (ans.parseError || "parse error") }]); return null; }
    return { text: ans.text, warnings: [...v.warnings.map(w => w.msg), ...(ans.warnings || [])] };
  }
  refuseSave(errors) {
    const first = errors.find(e => e.ref && this.doc.exists(e.ref));
    banner("Not saved: " + errors[0].msg + (errors.length > 1 ? " (+" + (errors.length - 1) + " more)" : ""), true, 0);
    dialog([el("h2", {}, "Not saved: the game would refuse this map"), el("ul", {}, errors.slice(0, 12).map(e => el("li", {}, e.msg))),
      el("p", { class: "dim" }, "Fix it and save again (the file on disk is unchanged).")],
      first ? [{ label: "Close", value: "close" }, { label: "Show it", value: "show", primary: true }] : undefined)
      .then(r => { if (r === "show") { this.doc.select([first.ref]); this.frameSel(); } });
  }
  async writeAs(name) {
    const d = this.doc;
    const c = await this.checkedText();
    if (!c) return false;
    const r = await api.writeMap(name, c.text, this.provenance());
    await api.writeSidecar(name, this.sidecarForSave());
    d.markSaved(name, "mine");
    if (d.map.title === undefined) d.map.title = "";
    await this.panels.loadList();
    this.mapLabel();
    history.replaceState(null, "", pageUrl({ src: "mine", map: name }));
    if (c.warnings.length) banner("Saved " + r.path + ", with " + c.warnings.length + " warning" + (c.warnings.length > 1 ? "s" : "") + ": " + c.warnings.slice(0, 3).join("; "), true, 12000);
    else banner("Saved " + r.path, false, 4000);
    this.emit("save", name);
    return true;
  }
  // your own maps: rename (the map, its sidecar, its provenance) and delete (moved to <your maps>/deleted/)
  async renameMine(name) {
    const to = await prompt("Rename " + name, name, "1-40 letters, digits, - and _");
    if (!to || to === name) return;
    if (!validMapName(to)) { banner("A map name is 1-40 letters, digits, - and _ (" + to + ")", true); return; }
    try {
      await api.renameMap(name, to);
      if (this.doc.src === "mine" && this.doc.name === name) { this.doc.name = to; history.replaceState(null, "", pageUrl({ src: "mine", map: to })); this.mapLabel(); this.panels.renderMapSettings(); }
      await this.panels.loadList(); banner("Renamed " + name + " to " + to, false, 3000);
    } catch (e) { banner(e.message, true); }
  }
  async deleteMine(name) {
    if (!confirm("Delete " + name + "? It is moved to the deleted folder in your maps folder (" + api.info.userMaps + "), not erased.")) return;
    try {
      const r = await api.deleteMap(name);
      const open = this.doc.src === "mine" && this.doc.name === name;
      await this.panels.loadList();
      if (open) { this.doc.src = "new"; this.doc.name = ""; this.doc.savedAt = -1; this.doc.emit("dirty"); this.panels.renderMapSettings(); history.replaceState(null, "", pageUrl({})); }
      banner("Deleted " + name + " (kept in " + (r.moved[0] || "deleted/") + ")", false, 5000);
    } catch (e) { banner(e.message, true); }
  }
  async save() {
    const d = this.doc;
    if (d.src !== "mine" || !d.name) return this.saveAs();
    try { await this.writeAs(d.name); } catch (e) { banner("Save failed: " + e.message, true, 0); }
  }
  async saveAs() {
    const d = this.doc, defaults = new Set(this.panels.list.defaults.map(m => m.name));
    let name = d.name && !defaults.has(d.name) ? d.name : (d.name ? d.name + "_mine" : "");
    const inp = el("input", { type: "text", value: name, style: { width: "100%" } });
    const mine = this.origin.origin === "human";
    let laid = mine;
    const note = el("input", { type: "text", value: this.origin.note || "", style: { width: "100%" } });
    const body = [el("h2", {}, "Save as"),
      el("p", { class: "dim" }, "A name of 1-40 letters, digits, - and _. It goes in your maps folder: " + api.info.userMaps),
      inp,
      el("p", {}, check("I laid this map out (provenance: human)", laid, v => { laid = v; }, "Ticked: the map counts as human-made. A default map you only touched up stays ai.")),
      el("p", { class: "dim" }, "note"), note];
    inp.addEventListener("keydown", e => { if (e.key === "Enter") { e.preventDefault(); $("#dlg").close("ok"); } });
    const r = await dialog(body, [{ label: "Cancel", value: "cancel" }, { label: "Save", value: "ok", primary: true }]);
    if (r !== "ok") return;
    name = inp.value.trim();
    if (!validMapName(name)) { banner("A map name is 1-40 letters, digits, - and _ (" + name + ")", true); return; }
    if (defaults.has(name)) { banner(name + " is a default map's name: pick a name of your own", true); return; }
    if (this.panels.list.mine.some(m => m.name === name) && name !== d.name && !confirm(name + " exists in your maps: replace it?")) return;
    this.origin = { ...this.origin, origin: laid ? "human" : "ai", note: note.value.trim() || (laid ? "" : this.origin.note) };
    try { await this.writeAs(name); } catch (e) { banner("Save failed: " + e.message, true, 0); }
  }
  async launch(what) {
    const d = this.doc;
    if (d.src !== "mine" || d.dirty) {
      if (!confirm(d.src !== "mine" ? "The game opens your saved maps: save this one first?" : "Save your changes first, so the game gets them?")) return;
      await this.save(); if (d.src !== "mine" || d.dirty) return;
    }
    try { await api.launch(what, d.name, $("#playMode").value); banner(what === "editor" ? "Opening " + d.name + " in the game's editor" : "Starting the game: " + $("#playMode").value + " on " + d.name, false, 4000); }
    catch (e) { banner(e.message, true, 0); if (/exe/.test(e.message)) this.settings(); }
  }
  async settings() {
    const cfg = await api.config();
    const exe = el("input", { type: "text", value: cfg.gameExe || "", style: { width: "100%" }, placeholder: "C:\\Users\\user\\Desktop\\Quark\\testing\\quark.exe" });
    const maps = el("input", { type: "text", value: cfg.userMaps || "", style: { width: "100%" } });
    const r = await dialog([el("h2", {}, "Settings"), el("p", { class: "dim" }, "The game exe (Open in game, Play test)"), exe, el("p", { class: "dim" }, "Your maps folder (the game's user maps; empty: the default)"), maps],
      [{ label: "Cancel", value: "cancel" }, { label: "Save", value: "ok", primary: true }]);
    if (r !== "ok") return;
    try { await api.setConfig({ gameExe: exe.value.trim(), userMaps: maps.value.trim() }); api.info = await api.init(); await this.panels.loadList(); banner("Settings saved", false, 2500); }
    catch (e) { banner(e.message, true); }
  }
  help() {
    const K = [["Right mouse + WASD", "fly (Space / C up / down, Shift fast, wheel forward, Ctrl+wheel speed)"], ["Alt + drag", "orbit"], ["T", "top-down view"], ["F / Home", "frame the selection / the map"],
      ["Click", "select (Shift adds, again: what is behind, Ctrl: a group's part)"], ["Drag", "a rectangle selects; on a selected object: slide it"], ["Arrows / PgUp PgDn", "move by the grid (Shift / Ctrl: grow / shrink that face)"],
      ["[ ]", "grid 1/8 m .. 4 m"], ["M", "material (spawn: team)"], ["R", "rotate 90 degrees"], ["Q / E", "turn a spawn 45 degrees"], ["Ctrl+D", "duplicate"], ["Del", "delete"],
      ["Ctrl+Z / Ctrl+Y", "undo / redo"], ["Ctrl+C / Ctrl+V", "copy the selection / paste it at the cursor (any map)"], ["Ctrl+S", "save"], ["B", "box tool"], ["Esc", "back to select / clear"]];
    dialog([el("h2", {}, "Keys"), el("div", { class: "keys" }, K.flatMap(([k, t]) => [el("kbd", {}, k), el("span", {}, t)]))]);
  }

  // ---- groups
  makeGroup() {
    const d = this.doc, m = d.map, idx = d.selOf("brush").sort((a, b) => a - b);
    if (!idx.length) return;
    const bb = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
    for (const i of idx) for (let k = 0; k < 3; k++) { bb.min[k] = Math.min(bb.min[k], m.brushes[i].min[k]); bb.max[k] = Math.max(bb.max[k], m.brushes[i].max[k]); }
    if (bb.max[1] - bb.min[1] > 64) { banner("A group is at most 64 m tall", true); return; }
    const sz = [0, 1, 2].map(k => Math.max(1e-6, bb.max[k] - bb.min[k]));
    let gi = -1;
    d.edit("group " + idx.length + " boxes", ["brushes", "groups"], () => {
      const parts = idx.map(i => { const b = m.brushes[i]; return { mat: b.mat, min: [0, 1, 2].map(k => (b.min[k] - bb.min[k]) / sz[k]), max: [0, 1, 2].map(k => (b.max[k] - bb.min[k]) / sz[k]) }; });
      for (const i of idx.slice().reverse()) m.brushes.splice(i, 1);
      m.groups.push({ type: "custom", mat: 0, dir: 0, min: bb.min, max: bb.max, parts }); gi = m.groups.length - 1;
    }, { touch: [] });
    d.select([{ k: "group", i: gi }]);
  }
  ungroup(gi) {
    const d = this.doc, m = d.map;
    const parts = m.generated.filter(b => b.group === gi).map(b => ({ mat: b.mat, min: b.min.map(v => Math.round(v * 1e4) / 1e4), max: b.max.map(v => Math.round(v * 1e4) / 1e4) }));
    const n0 = m.brushes.length;
    d.edit("ungroup", ["brushes", "groups"], () => { m.groups.splice(gi, 1); m.brushes.push(...parts); }, { touch: [] });
    d.select(parts.map((_, j) => ({ k: "brush", i: n0 + j })));
  }

  // ---- the decoration hook (README.md "Decoration hook")
  mountDeco() {
    if (!window.MapDeco || typeof window.MapDeco.mountPanel !== "function") return;
    $('#rtabs button[data-rtab="deco"]').hidden = false;
    const d = this.doc, self = this;
    // A setMap and a sidecar.set in the same task (the panel's apply / remove: save() then setMap, or setMap then
    // save()) are one undo step: the sidecar entry's old value rides on the map's step, so one undo restores both.
    let tick = null;   // {step, before: {key: old value}}, cleared after this task
    const thisTick = () => { if (!tick) { tick = { step: null, before: {} }; queueMicrotask(() => { tick = null; }); } return tick; };
    const ride = (step, k, before) => d.rideOn(step, "sc." + k, before);
    const top = () => d.undoStack[d.undoStack.length - 1];
    const hookApi = {
      getMap: () => structuredClone(d.map),
      setMap: (newMap, label) => {
        const t = thisTick();
        const was = top();
        d.replaceMap(newMap, label);
        t.step = top() !== was ? top() : null;   // (null: the map came back the same, no step)
        if (t.step) for (const [k, b] of Object.entries(t.before)) ride(t.step, k, b);
        t.before = {};
      },
      getSelection: () => ({ brushes: d.selOf("brush"), groups: d.selOf("group") }),
      select: (sel) => d.select([...(sel.brushes || []).map(i => ({ k: "brush", i })), ...(sel.groups || []).map(i => ({ k: "group", i }))]),
      sidecar: {
        get: (k) => structuredClone(d.sidecar[k]),
        set: (k, v) => {
          const t = thisTick(), before = structuredClone(d.sidecar[k]);
          d.sidecar[k] = structuredClone(v);
          if (t.step && t.step === top()) { ride(t.step, k, before); return; }   // (already unsaved by the map step)
          if (!(k in t.before)) t.before[k] = before;   // a setMap later in this task takes it into its step
          d.seq++; d.emit("dirty");
        },
      },
      redraw: () => self.view.redraw(),
      on: (ev, fn) => ev === "map" ? d.on("map", (keys, live) => { if (!live) fn(keys); }) : ev === "selection" ? d.on("selection", fn) : ev === "save" ? self.on("save", fn) : self.on(ev, fn),
      scene: this.view.scene, layer: this.view.layers.deco, THREE,
      maptool: (cmd, body) => api.tool(cmd, body),
      get mapName() { return d.name; },
    };
    try { window.MapDeco.mountPanel($("#decoMount"), hookApi); } catch (e) { console.error("MapDeco.mountPanel failed:", e); }
  }
}

// ?embed=1: the page runs as a tab inside another app's window (the project manager's shell), which has its own
// title bar: the page's brand / title line goes and the toolbar stays. Kept in the URL when a map opens.
const EMBED = new URLSearchParams(location.search).get("embed") === "1";
if (EMBED) document.documentElement.classList.add("embed");
function pageUrl(params) {
  const q = new URLSearchParams();
  for (const [k, v] of Object.entries(params)) q.set(k, v);
  if (EMBED) q.set("embed", "1");
  return "?" + q.toString();
}

const app = new App();
app.start().catch(e => console.error(e));
