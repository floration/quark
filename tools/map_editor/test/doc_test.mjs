#!/usr/bin/env node
// Tests for the open map's undo / redo (js/doc.js: per-object steps), the save checks (js/validate.js) and the
// objective names. Plain node, no browser: node tools/map_editor/test/doc_test.mjs. Exit code 1 on any failure.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

// the page's modules are plain ES modules in js/ (no package.json there): copy the ones under test next to one
const JS = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "js");
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "qme-doc-"));
for (const f of ["doc.js", "util.js", "fallback_parse.js", "validate.js", "groups.js", "repeat.js", "xform.js"]) if (fs.existsSync(path.join(JS, f))) fs.copyFileSync(path.join(JS, f), path.join(tmp, f));
fs.writeFileSync(path.join(tmp, "package.json"), '{"type":"module"}');
const { Doc, objectiveName } = await import(pathToFileURL(path.join(tmp, "doc.js")));
const { validateMap } = await import(pathToFileURL(path.join(tmp, "validate.js")));
const { parseQmap } = await import(pathToFileURL(path.join(tmp, "fallback_parse.js")));
const G = fs.existsSync(path.join(tmp, "groups.js")) ? await import(pathToFileURL(path.join(tmp, "groups.js"))) : null;

let fails = 0, passes = 0;
const ok = (c, what) => { if (c) passes++; else { fails++; console.log("  FAIL " + what); } };
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const section = (s) => console.log("== " + s);

function bigMap(n = 2000) {
  let t = "version 2\nhandmade\ntitle T\nspawn a 0 0 0 0\nspawn b 10 0 0 90\npath 0 0 0\npath 10 0 0\npath 20 0 0\ncartcheck 0.5\n";
  for (let i = 0; i < n; i++) t += `box ${i} 0 0 ${i + 1} 1 1\n`;
  return parseQmap(t, "t");
}
const open = (m) => { const d = new Doc(); d.open(m, "t", "mine", {}, null); return d; };

section("a nudge of one box: one small step, undo / redo exact");
{
  const d = open(bigMap()), b = d.map.brushes[500], before = structuredClone(b);
  d.select([{ k: "brush", i: 500 }]);
  d.edit("move", ["brushes"], () => { b.min[0] += 1; b.max[0] += 1; }, { touch: d.sel });
  const st = d.undoStack[0];
  ok(st && st.parts.brushes.mut.length === 1 && !st.parts.brushes.st, "the step holds one box and no list change");
  ok(JSON.stringify(st.parts).length < 400, "the step is small (" + JSON.stringify(st.parts).length + " bytes as JSON)");
  d.undo();
  ok(eq(d.map.brushes[500], before) && d.map.brushes[500] === b, "undo restores the box in place");
  ok(!d.dirty && !d.mapEdited, "undone: clean");
  d.redo();
  ok(d.map.brushes[500].min[0] === before.min[0] + 1, "redo moves it again");
}

section("an edit that changes nothing makes no step");
{
  const d = open(bigMap(10));
  d.edit("resize", ["brushes"], () => {}, { touch: [{ k: "brush", i: 3 }] });
  d.edit("title", ["title"], (m) => { m.title = "T"; });
  ok(d.undoStack.length === 0 && !d.dirty, "no step, not dirty");
}

section("delete half, undo, redo: same objects, same order");
{
  const d = open(bigMap()), objs = d.map.brushes.slice();
  d.select(objs.map((_, i) => ({ k: "brush", i })).filter((_, i) => i % 2));
  d.deleteSel();
  ok(d.map.brushes.length === 1000 && d.map.brushes.every((b, i) => b === objs[i * 2]), "deleted the odd ones");
  d.undo();
  ok(d.map.brushes.length === 2000 && d.map.brushes.every((b, i) => b === objs[i]), "undo puts every one back where it was");
  d.redo();
  ok(d.map.brushes.length === 1000 && d.map.brushes[1] === objs[2], "redo deletes them again");
}

section("deleting the payload path keeps its checkpoints for undo");
{
  const d = open(bigMap(5));
  ok(eq(d.map.cartChecks, [0.5]), "the map has a cart checkpoint");
  d.select([{ k: "path", i: 0 }, { k: "path", i: 1 }]);
  d.deleteSel();
  ok(d.map.path.length === 1 && d.map.cartChecks.length === 0, "one point left: the checkpoints go");
  d.undo();
  ok(d.map.path.length === 3 && eq(d.map.cartChecks, [0.5]), "undo brings back the points and the checkpoints");
}

section("a point put in the middle of a list, and a duplicate");
{
  const d = open(bigMap(5)), p0 = d.map.path.slice();
  d.edit("add path point", ["path"], (m) => m.path.splice(1, 0, [5, 0, 0]), { touch: [] });
  ok(d.map.path.length === 4 && eq(d.map.path[1], [5, 0, 0]), "inserted");
  d.undo();
  ok(d.map.path.length === 3 && d.map.path.every((p, i) => p === p0[i]), "undo takes it out");
  d.select([{ k: "brush", i: 2 }]);
  d.duplicateSel([0, 5, 0]);
  ok(d.map.brushes.length === 6 && d.map.brushes[5].min[1] === 5 && d.sel[0].i === 5, "duplicated above, the copy selected");
  d.undo();
  ok(d.map.brushes.length === 5, "undo removes the copy");
}

section("replaceMap (the decoration hook) keeps unchanged objects: the step holds only what changed");
{
  const d = open(bigMap()), m2 = structuredClone(d.map);
  m2.brushes.splice(10, 1); m2.brushes.push({ mat: 8, min: [0, 5, 0], max: [1, 6, 1] });
  const keep = d.map.brushes[20];
  d.replaceMap(m2, "decorate");
  const p = d.undoStack[0].parts.brushes;
  ok(d.map.brushes[19] === keep && p.st && p.st.drop.length === 1 && p.st.ins.length === 1, "1 added, 1 taken out, the rest the same objects");
  ok(eq(d.undoStack[0].keys, ["brushes"]), "only brushes changed");
  d.undo();
  ok(d.map.brushes.length === 2000 && d.map.brushes[10].min[0] === 10, "undo restores it");
}

section("validateMap: what the game refuses, and spawns in walls");
{
  const m = bigMap(3);
  ok(validateMap({ ...m, generated: [] }).errors.length === 0, "a good map has no errors");
  const bad = structuredClone(m); bad.brushes[1].max[0] = 9999;
  const v = validateMap(bad);
  ok(v.errors.length === 1 && v.errors[0].ref.k === "brush" && v.errors[0].ref.i === 1, "a coordinate over 4096 m is an error naming the box");
  const flip = structuredClone(m); flip.brushes[0].min[1] = 5;
  ok(validateMap(flip).errors.some(e => /min above its max/.test(e.msg)), "min above max is an error");
  const wall = structuredClone(m); wall.spawns[0].pos = [0.5, 0, 0.5];
  ok(validateMap(wall).warnings.some(w => w.ref && w.ref.k === "spawn" && w.ref.i === 0), "a spawn inside a box is a warning");
  const flat = structuredClone(m); flat.brushes[2].min[0] = flat.brushes[2].max[0];
  ok(validateMap(flat).warnings.some(w => w.ref && w.ref.k === "brush" && w.ref.i === 2 && /no size along x/.test(w.msg)), "a box of no size is a warning");
  const none = structuredClone(m); none.spawns = [];
  ok(validateMap(none).errors.some(e => /no spawn/.test(e.msg)), "no spawn is an error");
}

section("objective names match the game (SITE A, POINT A) and quark_maptool");
ok(objectiveName({}, "site", 0) === "site A" && objectiveName({}, "point", 1) === "point B" && objectiveName({}, "checkpoint", 1) === "checkpoint 2", "names");

if (G) {
  section("groups: copy / paste between maps, repeat objects");
  const d = open(bigMap(5));
  d.map.lights.push({ pos: [2, 3, 0], radius: 8, intensity: 1 });
  d.select([{ k: "brush", i: 1 }, { k: "brush", i: 2 }, { k: "light", i: 0 }, { k: "spawn", i: 0 }]);
  const clip = G.copySelection(d);
  ok(clip && clip.brushes.length === 2 && clip.lights.length === 1 && clip.spawns.length === 1, "copied two boxes, a light, a spawn");
  ok(clip.brushes[0].min[0] === 1.4 && clip.spawns[0].pos[0] === 0.4, "positions relative to the selection's bounds (the spawn's hull starts at x -0.4)");
  const d2 = open(bigMap(2));
  const n0 = d2.map.brushes.length;
  G.pasteGroup(d2, clip, G.pasteBase(clip, [100, 0, 100]));
  ok(d2.map.brushes.length === n0 + 2 && d2.map.lights.length === 1 && d2.map.spawns.length === 3, "pasted into another map");
  ok(d2.sel.length === 4, "the pasted objects are selected");
  ok(d2.undoStack.length === 1, "one undo step");
  const bb = d2.selBounds();
  ok(Math.abs(bb.min[0] - 100) < 1e-6 || Math.abs((bb.min[0] + bb.max[0]) / 2 - 100) < 2, "at the paste point");
  d2.undo();
  ok(d2.map.brushes.length === n0 && d2.map.lights.length === 0 && d2.map.spawns.length === 2, "one undo takes the paste out");
  // every kind copies: bot starts, player figures, path points and the hill too (the hill replaces the map's)
  {
    const d4 = open(bigMap(3));
    d4.sidecar.botStarts.push({ pos: [1, 0, 1], yaw: 0, team: "a" });
    d4.sidecar.figures.push({ pos: [2, 0, 1], yaw: 0, model: "hull" });
    d4.map.hill = { min: [0, 0, 0], max: [4, 3, 4] };
    const kinds = [["brush", 0], ["spawn", 0], ["botstart", 0], ["figure", 0], ["path", 1], ["hill", 0]];
    d4.select(kinds.map(([k, i]) => ({ k, i })));
    const c4 = G.copySelection(d4);
    ok(G.clipCount(c4) === 6 && c4.botStarts.length === 1 && c4.figures.length === 1 && c4.path.length === 1 && c4.hill && !Array.isArray(c4.hill), "copied 6 objects of 6 kinds (the hill as one value)");
    const d5 = open(bigMap(2)); d5.map.hill = null;
    G.pasteGroup(d5, JSON.parse(JSON.stringify(c4)), G.pasteBase(c4, [50, 0, 50]));
    ok(d5.sidecar.botStarts.length === 1 && d5.sidecar.figures.length === 1 && d5.map.path.length === 4 && d5.map.hill && d5.map.hill.min[0] > 40, "pasted bot start, figure, path point and hill");
    ok(d5.sel.length === 6 && d5.sel.some(s => s.k === "hill"), "all six selected");
    d5.undo();
    ok(d5.sidecar.botStarts.length === 0 && d5.sidecar.figures.length === 0 && d5.map.path.length === 3 && d5.map.hill === null, "one undo takes them all out");
    ok(eq(c4.origin, d4.selBounds().min), "the clipboard keeps where it came from (paste in place)");
  }
  // repeat objects: a 1 m unit repeated along x
  const def = { name: "post", unit: [1, 1, 1], spacing: [2, 0, 0], axes: [true, false, false], boxes: [{ mat: 3, min: [0, 0, 0], max: [0.5, 1, 0.5] }] };
  const boxes = G.expandRepeat(def, { min: [0, 0, 0], max: [6, 1, 1] });
  ok(boxes.length === 3 && boxes[2].min[0] === 4, "a 6 m long row of a 2 m spaced post is 3 posts");
  const boxes2 = G.expandRepeat(def, { min: [0, 0, 0], max: [6, 3, 1] });
  ok(boxes2.length === 3 && boxes2[0].max[1] === 1, "an axis that doesn't repeat isn't stretched either");
  const turned = G.expandRepeat(def, { min: [0, 0, 0], max: [1, 1, 6] }, 1);
  ok(turned.length === 3 && turned[2].min[2] === 4, "turned a quarter, it repeats along z");
  // an object in a map: drawn from the sidecar, saved as plain boxes, taken out again when the map opens
  const d3 = open(bigMap(2));
  d3.edit("add post row", ["sc.objects"], (m, sc) => { sc.objects.push({ name: "post", def, rot: 0, min: [10, 0, 0], max: [16, 1, 1] }); }, { touch: [] });
  ok(d3.map.generated.filter(b => b.object === 0).length === 3 && d3.mapEdited, "its 3 posts are drawn; the map counts as edited");
  const saved = structuredClone(d3.map); saved.brushes.push(...d3.map.generated.filter(b => b.object !== undefined).map(b => ({ mat: b.mat, min: b.min, max: b.max })));
  const side = { objects: structuredClone(d3.sidecar.objects) };
  const R = await import(pathToFileURL(path.join(tmp, "repeat.js")));
  ok(R.stripObjects(saved, side).length === 0 && saved.brushes.length === 2 && side.objects.length === 1, "opening it takes the saved posts back out of the boxes");
  const edited = structuredClone(saved); edited.brushes.push({ mat: 3, min: [10, 0, 0], max: [10.5, 1, 0.5] });
  const side2 = { objects: structuredClone(d3.sidecar.objects) };
  ok(R.stripObjects(edited, side2).length === 1 && edited.brushes.length === 3 && side2.objects.length === 0, "posts no longer all there: the object goes, its boxes stay");
}

section("+ object from a box and a group: both are replaced, nothing doubled");
{
  const R = await import(pathToFileURL(path.join(tmp, "repeat.js")));
  for (const withBox of [false, true]) {
    const d = open(bigMap(3));
    d.edit("group", ["groups"], (m) => { m.groups.push({ type: "custom", mat: 0, dir: 0, min: [10, 0, 0], max: [12, 2, 2], parts: [{ mat: 1, min: [0, 0, 0], max: [1, 1, 1] }] }); }, { touch: [] });
    d.select([{ k: "group", i: 0 }, ...(withBox ? [{ k: "brush", i: 1 }] : [])]);
    const boxes = G.selectedBoxes(d), all0 = d.map.brushes.length + d.map.generated.length;
    G.replaceWithObject(d, "o", R.defFromBoxes("o", boxes), boxes);
    ok(d.map.groups.length === 0 && d.map.brushes.length === (withBox ? 2 : 3) && d.sidecar.objects.length === 1, (withBox ? "box + group" : "group only") + ": the group (and box) replaced by the object");
    ok(d.map.brushes.length + d.map.generated.length === all0, "the same number of boxes as before (no doubled geometry)");
    d.undo();
    ok(d.map.groups.length === 1 && d.sidecar.objects.length === 0 && d.map.brushes.length === 3, "one undo puts them back");
  }
}

section("custom objects: start / end pieces, editing a part in place");
{
  const R = await import(pathToFileURL(path.join(tmp, "repeat.js")));
  const rail = { name: "railing", unit: [1.5, 1.1, 0.1], axes: [true, false, false], spacing: [1.5, 1.1, 0.1],
    boxes: [{ mat: 3, min: [0, 0, 0], max: [0.1, 1.1, 0.1] }, { mat: 3, min: [0.1, 1, 0.02], max: [1.5, 1.08, 0.08] }],
    end: { len: 0.1, boxes: [{ mat: 3, min: [0, 0, 0], max: [0.1, 1.1, 0.1] }] } };
  const box = R.fitBox(rail, { min: [0, 0, 0] }, [4, 1, 1]);
  ok(Math.abs(box.max[0] - 6.1) < 1e-6, "4 units of 1.5 m and a 0.1 m closing post: 6.1 m");
  const bx = R.expandRepeat(rail, box);
  ok(bx.length === 9 && bx[8].min[0] === 6 && bx[8].max[0] === 6.1, "the closing post sits after the last rail");
  ok(eq(R.repeatCounts(rail, box), [4, 1, 1]), "the pieces don't count as units");
  // turned a quarter: along z, the post still closes the far end of the run (the boxes are the run's, turned)
  const t = R.expandRepeat(rail, { min: [0, 0, 0], max: [0.1, 1.1, 6.1] }, 1);
  ok(t.length === 9 && t.filter(b => b.max[1] === 1.1).length === 5, "turned: 4 posts + the closing one");
  const zs = t.filter(b => b.max[1] === 1.1).map(b => b.min[2]).sort((a, b) => a - b);
  ok(eq(zs, [0, 1.5, 3, 4.5, 6]), "turned: posts at both ends (" + zs.join(" ") + ")");
  const h = R.expandRepeat(rail, box, 2).filter(b => b.max[1] - b.min[1] > 1).map(b => b.min[0]).sort((a, b) => a - b);
  ok(eq(h, [0, 1.5, 3, 4.5, 6]), "a half turn: the closing post moves to the other end, the posts stay put (" + h.join(" ") + ")");
  // a start piece: before the first unit
  const st = { ...rail, start: { len: 0.5, boxes: [{ mat: 1, min: [0, 0, 0], max: [0.5, 0.2, 0.1] }] } };
  const sb = R.expandRepeat(st, R.fitBox(st, { min: [10, 0, 0] }, [2, 1, 1]));
  ok(sb[0].min[0] === 10 && sb[1].min[0] === 10.5 && sb[sb.length - 1].min[0] === 13.5, "start piece at the min, units after it, end piece last");
  // edit the end piece: it comes out as boxes; a wider post goes back in
  const o = { name: "railing", def: rail, rot: 0, ...box };
  const eb = R.partBoxes(o, "end");
  ok(eb.length === 1 && eb[0].min[0] === 6, "the end piece's boxes in the world");
  const o2 = R.withoutPiece(o, "end");
  ok(!o2.def.end && Math.abs(o2.max[0] - 6) < 1e-6 && R.repeatCounts(o2.def, o2)[0] === 4, "without its end piece: 4 units, 6 m");
  const o3 = R.withPart(o2, "end", [{ mat: 1, min: [6, 0, 0], max: [6.3, 1.2, 0.1] }]);
  ok(o3.def.end.len === 0.3 && Math.abs(o3.max[0] - 6.3) < 1e-6 && R.expandRepeat(o3.def, o3).length === 9, "a 0.3 m post as the new end piece");
  ok(R.withPart(o2, "end", []).def.end === undefined, "no boxes: no end piece");
  // edit the unit: a taller post; the spacing followed the unit (it touched), the end piece stays
  const ub = R.partBoxes(o, "unit"); ub[0].max[1] = 1.5;
  const o4 = R.withPart(o, "unit", ub);
  ok(o4.def.unit[1] === 1.5 && o4.def.end && R.repeatCounts(o4.def, o4)[0] === 4 && o4.def.boxes.length === 2, "a taller unit: same 4 units, the end piece kept");
  ok(R.withPart(o, "unit", []) === null, "a unit can't be empty");
}

section("linked library objects and plurals");
{
  const R = await import(pathToFileURL(path.join(tmp, "repeat.js")));
  const U = await import(pathToFileURL(path.join(tmp, "util.js")));
  ok(U.plural(1, "box") === "1 box" && U.plural(2, "box") === "2 boxes" && U.plural(3, "placed copy") === "3 placed copies" && U.plural(0, "object") === "0 objects", "plural()");
  const def = { name: "p", unit: [1, 1, 1], axes: [true, false, false], spacing: [2, 1, 1], boxes: [{ mat: 0, min: [0, 0, 0], max: [0.5, 1, 0.5] }] };
  const o = { name: "p", def, rot: 1, min: [0, 0, 0], max: [1, 1, 7], lib: "p" };
  ok(R.sameDef(def, { ...def, name: "other" }), "sameDef ignores the name");
  const def2 = { ...def, unit: [1, 2, 1], spacing: [3, 2, 1], boxes: [{ mat: 1, min: [0, 0, 0], max: [1, 2, 1] }] };
  const o2 = R.relink(o, def2);
  ok(o2.rot === 1 && eq(o2.min, [0, 0, 0]) && eq(R.repeatCounts(R.turnDef(def2, 1), o2), [1, 1, 4]) && o2.max[2] === 10 && o2.max[1] === 2 && o2.lib === "p", "relinked: same place, turn and 4 units, the new spacing");
}

section("the paste preview's boxes");
{
  const d = open(bigMap(3));
  d.select([{ k: "brush", i: 1 }, { k: "spawn", i: 0 }]);
  const c = G.copySelection(d), sb = G.stampBoxes({ kind: "group", clip: c });
  ok(sb.length === 2 && eq(sb[0].min, [1.4, 0, 0.4]) && sb[1].max[1] === 1.8, "a box and a spawn's hull, relative to the paste corner");
}

section("library: thumbnails, stats, import formats");
{
  const item = { kind: "object", def: { name: "p", unit: [1, 2, 1], axes: [true, false, false], spacing: [1, 2, 1], boxes: [{ mat: 0, min: [0, 0, 0], max: [1, 2, 1] }] } };
  ok(eq(G.itemStats(item), { size: [1, 2, 1], boxes: 1 }), "an object's size and box count");
  const svg = G.thumbSvg(G.stampBoxes(item), ["#808080"]);
  ok(svg.startsWith("<svg") && (svg.match(/<polygon/g) || []).length === 3, "a thumbnail: one box is three faces");
  const d = open(bigMap(2)); d.select([{ k: "brush", i: 0 }]);
  const c = G.copySelection(d);
  ok(G.importItems({ format: G.LIB_FORMAT, items: [item, { kind: "nope" }] }).length === 1 && G.importItems(item).length === 1 && G.importItems(c, "x")[0].kind === "group" && G.importItems({}).length === 0, "import: a library file, one item, a clipboard group; junk skipped");
}

section("flips and the point-symmetric twin (xform.js)");
{
  const X = await import(pathToFileURL(path.join(tmp, "xform.js")));
  const b = { mat: 1, min: [2, 0, 1], max: [5, 2, 3] };
  X.flipObj("brush", b, true, false, [0, 0]);
  ok(eq(b.min, [-5, 0, 1]) && eq(b.max, [-2, 2, 3]), "a box flipped across x = 0");
  const g = { type: "stairs", dir: 0, mat: 0, min: [0, 0, 0], max: [4, 2, 2], parts: [] };
  X.flipObj("group", g, true, false, [2, 0]);
  ok(g.dir === 1 && eq(g.min, [0, 0, 0]), "stairs rising +x rise -x after a flip across x about their middle");
  const cg = { type: "custom", dir: 0, mat: 0, min: [0, 0, 0], max: [2, 2, 2], parts: [{ mat: 0, min: [0, 0, 0], max: [0.25, 1, 1] }] };
  X.flipObj("group", cg, false, true, [1, 1]);
  X.flipObj("group", cg, true, false, [1, 1]);
  ok(eq(cg.parts[0].min, [0.75, 0, 0]) && eq(cg.parts[0].max, [1, 1, 1]), "a custom group's parts flip with it");
  const sp = { pos: [3, 0, 4], yaw: Math.PI / 2, team: "a" };
  const tw = X.twinObj("spawn", sp);
  ok(eq(tw.pos, [-3, 0, -4]) && tw.team === "b" && Math.abs(tw.yaw - 3 * Math.PI / 2) < 1e-5, "a spawn's twin: (-x, -z), team B, yaw + 180 (as ranked_mirror wants)");
  const sx = { pos: [3, 0, 4], yaw: 0.5, team: "a" }; X.flipObj("spawn", sx, true, false, [0, 0]);
  ok(sx.pos[0] === -3 && Math.abs(sx.yaw - (2 * Math.PI - 0.5)) < 1e-5 && sx.team === "a", "a flip across x mirrors the yaw and keeps the team");
  const st = { x0: 1, z0: 0, x1: 3, z1: 2, base: 0, top: 2, rising: 1, axis: "x", solid: true };
  X.flipObj("stairs", st, true, false, [0, 0]);
  ok(st.x0 === -3 && st.x1 === -1 && st.rising === -1, "a stairs record flipped along its axis rises the other way");
  // a custom object: the half turn turns it (the definition stays the library's); a flip mirrors the definition
  const def = { name: "r", unit: [2, 1, 1], axes: [true, false, false], spacing: [2, 1, 1], boxes: [{ mat: 0, min: [0, 0, 0], max: [0.5, 1, 1] }] };
  const R = await import(pathToFileURL(path.join(tmp, "repeat.js")));
  const ob = { name: "r", def, rot: 0, min: [1, 0, 0], max: [7, 1, 1] };
  const before = R.expandRepeat(ob.def, ob, 0).map(x => [-x.max[0], -x.max[2]].join()).sort();
  const t = X.twinObj("object", ob);
  ok(t.rot === 2 && eq(t.def, ob.def), "the object's twin is turned half way, same definition");
  const after = R.expandRepeat(t.def, t, t.rot).map(x => [x.min[0], x.min[2]].join()).sort();
  ok(eq(before, after), "its boxes are exactly the original's boxes through the centre");
  const fl = structuredClone(ob); X.flipObj("object", fl, true, false, [0, 0]);
  const flb = R.expandRepeat(fl.def, fl, fl.rot).map(x => [x.min[0], x.min[2]].join()).sort();
  ok(eq(flb, R.expandRepeat(ob.def, ob, 0).map(x => [-x.max[0], x.min[2]].join()).sort()) && fl.rot === 0, "a flipped object's boxes are the mirror image");
  // a flip of an object with an end piece: the piece comes to the other end (the boxes are the mirror image)
  const rail = { name: "r", unit: [1.5, 1, 0.1], axes: [true, false, false], spacing: [1.5, 1, 0.1], boxes: [{ mat: 0, min: [0, 0, 0], max: [0.1, 1, 0.1] }, { mat: 0, min: [0.1, 0.9, 0], max: [1.5, 1, 0.1] }],
    end: { len: 0.1, boxes: [{ mat: 0, min: [0, 0, 0], max: [0.1, 1, 0.1] }] } };
  const ro = { name: "r", def: rail, rot: 0, min: [0, 0, 0], max: [4.6, 1, 0.1] };
  const key = (b) => [b.min[0], b.max[0], b.min[1]].join();
  const want = R.expandRepeat(rail, ro).map(b => key({ min: [-b.max[0], b.min[1]], max: [-b.min[0]] })).sort();
  const rf = structuredClone(ro); X.flipObj("object", rf, true, false, [0, 0]);
  ok(rf.def.start && !rf.def.end && eq(R.expandRepeat(rf.def, rf, rf.rot).map(key).sort(), want), "flipped: the end piece became a start piece, boxes mirrored");
  // the Doc: a half map mirrored through the centre has every box's twin
  const d = open(bigMap(4));
  d.select(d.map.brushes.map((_, i) => ({ k: "brush", i })).concat([{ k: "spawn", i: 0 }]));
  const keys = d.keysOf(), added = [];
  d.edit("mirror copy", keys, () => { for (const s of d.sel.slice()) { const arr = d.get(keys.find(k => k === (s.k === "brush" ? "brushes" : "spawns"))); arr.push(X.twinObj(s.k, d.obj(s))); added.push(1); } }, { touch: [] });
  const twin = (a) => d.map.brushes.some(o => o.mat === a.mat && eq(o.min, [-a.max[0], a.min[1], -a.max[2]]) && eq(o.max, [-a.min[0], a.max[1], -a.min[2]]));
  ok(d.map.brushes.length === 8 && d.map.brushes.every(twin), "every box has its mirrored twin");
}

fs.rmSync(tmp, { recursive: true, force: true });
console.log((fails ? "FAILED " + fails : "OK") + " (" + passes + " passed)");
process.exit(fails ? 1 : 0);
