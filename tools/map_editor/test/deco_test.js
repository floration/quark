#!/usr/bin/env node
// Tests for the decoration engine (tools/map_editor/js/deco*.js). Run from anywhere:
//   node tools/map_editor/test/deco_test.js [--maps DIR] [--maptool EXE] [--quick] [--map NAME]
// Small hand-made maps (each generator, the walk test, keep-outs, symmetry, determinism, apply / remove / regenerate),
// then every real map in maps/ (the generated .qmap files: run the build or tools/make_maps.py first) with every preset:
// deterministic, nothing in a spawn or an objective, apply + remove gives the map back, and when build/quark_maptool
// exists, the game's own checks (every spawn reaches every objective on the walk grid, ladders, stairs...) still pass
// on the decorated map wherever they passed before. Exit code 1 on any failure.
'use strict';
const fs = require('fs'), path = require('path'), cp = require('child_process');
const ROOT = path.resolve(__dirname, '..', '..', '..');
const ED = path.join(ROOT, 'tools', 'map_editor');
require(path.join(ED, 'js', 'deco.js'));
require(path.join(ED, 'js', 'deco_rules.js'));
const D = globalThis.MapDeco;
const { parseQmap } = require('./deco_qmap.js');

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const QUICK = args.includes('--quick');
const MAPS = opt('--maps', path.join(ROOT, 'maps'));
let MAPTOOL = opt('--maptool', path.join(ROOT, 'build', process.platform === 'win32' ? 'quark_maptool.exe' : 'quark_maptool'));
if (!fs.existsSync(MAPTOOL)) MAPTOOL = null;

let fails = 0, passes = 0;
function ok(cond, what) { if (cond) passes++; else { fails++; console.log('  FAIL ' + what); } }
function section(s) { console.log('== ' + s); }
const B = (mat, a, b) => ({ mat, min: a, max: b });
const sorted = (bs) => bs.map(D.boxKey).sort();
const sameList = (a, b) => JSON.stringify(sorted(a)) === JSON.stringify(sorted(b));

// ---------------------------------------------------------------- a hand-made test map
// a 20 x 20 room (walls 2 m thick, 6 m tall, clip on top like the shipped maps), an inner wall across z = 0 with a
// doorway (x -1..1) and a window (x 5..7, sill at 1 m), a 3 m platform in the +x +z corner reached by a stairs group,
// a roof over the -x -z quarter (a low room: 3.5 m), spawns and a hill.
function testMap() {
    const brushes = [
        B(0, [-12, -1, -12], [12, 0, 12]),
        B(0, [-12, 0, -12], [-10, 6, 12]), B(0, [10, 0, -12], [12, 6, 12]), B(0, [-10, 0, -12], [10, 6, -10]), B(0, [-10, 0, 10], [10, 6, 12]),
        B(9, [-12, 6, -12], [12, 40, -10]), B(9, [-12, 6, 10], [12, 40, 12]), B(9, [-12, 6, -10], [-10, 40, 10]), B(9, [10, 6, -10], [12, 40, 10]),
        B(0, [-10, 0, -0.25], [-1, 4, 0.25]), B(0, [1, 0, -0.25], [5, 4, 0.25]), B(0, [7, 0, -0.25], [10, 4, 0.25]),
        B(0, [-1, 2.4, -0.25], [1, 4, 0.25]),                         // lintel over the door
        B(0, [5, 0, -0.25], [7, 1, 0.25]), B(0, [5, 2.5, -0.25], [7, 4, 0.25]),   // window sill and lintel
        B(0, [4, 0, 4], [10, 3, 10]),                                  // platform
        B(0, [-10, 3.5, -10], [-0.25, 3.8, -0.25]),                    // roof over the -x -z quarter
    ];
    return { name: 'test', title: 'Test', formatVersion: 2, brushes, groups: [{ type: 'stairs', mat: 0, dir: 0, min: [1, 0, 5], max: [4, 3, 7] }],
             spawns: [{ pos: [-5, 0, 5], yaw: 0, team: 'a' }, { pos: [5, 0, -5], yaw: 0, team: 'b' }], sites: [], points: [], checkpoints: [], path: [], ladders: [],
             hill: { min: [-7, 0, -7], max: [-3, 3, -3] }, stairs: [], lights: [] };
}
function boxesOverlap(a, b) { return a.min[0] < b.max[0] - 1e-3 && a.max[0] > b.min[0] + 1e-3 && a.min[1] < b.max[1] - 1e-3 && a.max[1] > b.min[1] + 1e-3 && a.min[2] < b.max[2] - 1e-3 && a.max[2] > b.min[2] + 1e-3; }
function spawnHull(s) { return { min: [s.pos[0] - 0.4, s.pos[1], s.pos[2] - 0.4], max: [s.pos[0] + 0.4, s.pos[1] + 1.8, s.pos[2] + 0.4] }; }
function objectiveViolations(map, boxes) {
    const vols = [].concat((map.spawns || []).map((s) => ['spawn', spawnHull(s)]), (map.sites || []).map((v) => ['site', v]), map.hill ? [['hill', map.hill]] : [],
                           (map.points || []).map((v) => ['point', v]), (map.checkpoints || []).map((v) => ['checkpoint', v]));
    const bad = [];
    for (const b of boxes) for (const [why, v] of vols) if (boxesOverlap(b, v)) bad.push(why + ' ' + D.boxKey(b));
    for (const b of boxes) if (D.isSolid(b.mat)) for (const l of map.ladders || []) if (boxesOverlap(b, l)) bad.push('ladder ' + D.boxKey(b));
    return bad;
}
function shuffle(list, seed) {
    const r = D.rng('shuffle', seed), a = list.slice();
    for (let i = a.length - 1; i > 0; i--) { const j = Math.floor(r() * (i + 1)); [a[i], a[j]] = [a[j], a[i]]; }
    return a;
}

// ---------------------------------------------------------------- unit tests
section('intervals and regions');
{
    const { union, sub, and } = D.iv;
    ok(JSON.stringify(union([[0, 1], [0.5, 2], [3, 4]])) === '[[0,2],[3,4]]', 'iv union');
    ok(JSON.stringify(sub([[0, 10]], [[2, 3], [5, 6]])) === '[[0,2],[3,5],[6,10]]', 'iv sub');
    ok(JSON.stringify(and([[0, 5]], [[3, 8]])) === '[[3,5]]', 'iv and');
    const map = testMap(), W = D._internal.buildWorld(map);
    // the inner face of the -x outer wall (x = -10, facing +x): exposed from the floor (y 0) to 6, cut by the inner wall
    const R = D.region.make(W, 0, 1, -10, [[-12, 0, 12, 6]]);
    ok(R.bands.length >= 1 && Math.abs(R.bands[0].v0) < 1e-6, 'wall face starts at the floor');
    const e = D.region.edges(R);
    ok(e.some((x) => x.o === 'v-' && x.kind === 'cover' && Math.abs(x.at) < 1e-6), 'wall meets floor: a cover edge at y 0');
    ok(e.some((x) => x.o[0] === 'u' && x.kind === 'cover' && Math.abs(Math.abs(x.at) - 0.25) < 1e-6), 'the inner wall cuts the face (cover edge at z +-0.25)');
    const op = D._internal.findOpenings(D._internal.makeCtx(map, W));
    ok(op.some((o) => o.kind === 'door' && Math.abs(o.a + 1) < 1e-6 && Math.abs(o.b - 1) < 1e-6 && Math.abs(o.y1 - 2.4) < 1e-6), 'finds the doorway');
    ok(op.some((o) => o.kind === 'window' && Math.abs(o.a - 5) < 1e-6 && Math.abs(o.y0 - 1) < 1e-6 && Math.abs(o.y1 - 2.5) < 1e-6), 'finds the window');
    ok(op.length === 2, 'two openings, got ' + op.length);
    // groupBoxes matches the game's stairs: 6 steps 0.5 m each
    const steps = D.groupBoxes(map.groups[0]);
    ok(steps.length === 6 && Math.abs(steps[5].max[1] - 3) < 1e-9, 'stairs group: 6 steps');
    const room = D.groupBoxes({ type: 'room', mat: 0, dir: 0, min: [0, 0, 0], max: [6, 3, 6] });
    ok(room.length === 2 + 4 * 3, 'room group: floor, ceiling, 4 walls with a door each');
}

section('walk test (solid decorations never cut a passage)');
{
    const corridor = { name: 'c', brushes: [B(0, [-10, -1, -10], [10, 0, 10]), B(0, [-10, 0, -10], [10, 3, -0.8]), B(0, [-10, 0, 0.8], [10, 3, 10])], spawns: [], groups: [] };
    ok(D.cutsRoute(corridor, [B(0, [-0.5, 0, -0.8], [0.5, 1.2, 0])]) === true, 'a crate halving a 1.6 m corridor cuts it');
    ok(D.cutsRoute(corridor, [B(0, [-0.5, 0, -0.8], [0.5, 1.2, -0.65])]) === false, 'a 15 cm ledge leaves 1.45 m: fine');
    ok(D.cutsRoute(corridor, [B(0, [-0.5, 0, -0.8], [0.5, 1.2, -0.05])]) === true, 'leaving 0.85 m (< 0.8 + 2 x 0.1 margin... one side) cuts it');
    ok(D.cutsRoute(corridor, [B(0, [-0.5, 0, -0.8], [0.5, 0.4, 0.8])]) === false, 'a 0.4 m step across it can be stepped over');
    ok(D.cutsRoute(corridor, [B(0, [-0.5, 1.6, -0.8], [0.5, 3, 0.8])]) === true, 'a beam down to 1.6 m cuts it (headroom)');
    ok(D.cutsRoute(corridor, [B(4, [-0.5, 2.9, -0.8], [0.5, 3, 0.8])]) === false, 'a glow strip at 2.9 m does not');
    const open = { name: 'o', brushes: [B(0, [-10, -1, -10], [10, 0, 10])], spawns: [], groups: [] };
    ok(D.cutsRoute(open, [B(0, [-0.6, 0, -0.6], [0.6, 1.2, 0.6])]) === false, 'a crate in an open room cuts nothing');
    const map = testMap();
    ok(D.cutsRoute(map, [B(0, [-0.5, 0, -0.6], [0.5, 1.5, 0.6])]) === true, 'a crate in the doorway cuts it');
    ok(D.cutsRoute(map, [B(1, [1.0, 0, 7.0], [4.0, 1.2, 7.6])]) === false, 'a box beside the stairs (not on them) is fine');
    ok(D.cutsRoute(map, [B(1, [2.0, 1.0, 5.0], [2.6, 2.5, 7.0])]) === true, 'a box across the stairs cuts them');
}

section('generators on the test map');
const ALL_GENS = Object.keys(D.generators);
{
    const map = testMap();
    const want = { edgeTrim: 10, bands: 5, wallStrips: 5, pillars: 2, floorBorder: 5, lightStrip: 1, windowFrames: 2, railings: 1, scatter: 1, kickPlates: 6 };
    for (const g of ALL_GENS) {
        const r = D.newRule(g);
        const res = D.generate(map, [r]);
        const rep = res.report.rules[0];
        console.log('  ' + g.padEnd(13) + String(rep.units).padStart(4) + ' units ' + String(rep.boxes).padStart(4) + ' boxes  rejected ' + JSON.stringify(rep.rejected) + (rep.warnings.length ? ' warnings ' + rep.warnings.join('; ') : ''));
        ok(rep.units >= (want[g] || 1), g + ': at least ' + (want[g] || 1) + ' units (got ' + rep.units + ')');
        ok(rep.warnings.length === 0, g + ': no warnings');
        const bad = objectiveViolations(map, res.brushes);
        ok(bad.length === 0, g + ': nothing in a spawn / objective: ' + bad.slice(0, 3).join(', '));
        const again = D.generate(map, [Object.assign({}, r)]);
        ok(JSON.stringify(again.brushes) === JSON.stringify(res.brushes), g + ': deterministic');
        const shuffled = Object.assign({}, map, { brushes: shuffle(map.brushes, g) });
        ok(sameList(D.generate(shuffled, [r]).brushes, res.brushes), g + ': same output with the brushes reordered');
        // every solid box from the generator: the walk test, independently, on the map plus everything else made
        for (const b of res.brushes.filter((x) => D.isSolid(x.mat))) {
            const others = res.brushes.filter((x) => x !== b);
            if (D.cutsRoute(Object.assign({}, map, { brushes: map.brushes.concat(others) }), [b]) && D.cutsRoute(map, [b])) { ok(false, g + ': solid box cuts a passage ' + D.boxKey(b)); break; }
        }
    }
    // specifics
    const trims = D.generate(map, [D.newRule('edgeTrim')]).brushes;
    ok(trims.every((b) => b.min[0] > -12 + 1e-6 && b.max[0] < 12 - 1e-6 && b.min[2] > -12 + 1e-6 && b.max[2] < 12 - 1e-6), 'skirting: never on the outside of the outer walls');
    ok(trims.every((b) => b.min[1] >= -1e-6), 'skirting: never under the floor');
    const frames = D.generate(map, [D.newRule('windowFrames')]).brushes;
    ok(frames.every((b) => !boxesOverlap(b, { min: [-1, 0, -0.25], max: [1, 2.4, 0.25] }) && !boxesOverlap(b, { min: [5, 1, -0.25], max: [7, 2.5, 0.25] })), 'frames: never inside the openings');
    const rails = D.generate(map, [D.newRule('railings')]).brushes;
    ok(rails.length > 0 && rails.every((b) => b.min[1] >= 3 - 1e-6), 'railings: on the platform only');
    ok(rails.every((b) => !boxesOverlap(b, { min: [3.9, 3, 5], max: [4.2, 4.5, 7] })), 'railings: leave the top of the stairs open');
    const solidRail = D.newRule('railings'); solidRail.params.mat = 1;
    const sr = D.generate(map, [solidRail]).report.rules[0];
    ok(sr.units === 0 && sr.rejected['blocks a drop'] > 0, 'a solid railing is refused (blocks a drop) unless allowed');
    solidRail.constraints.allowBlockDrops = true;
    ok(D.generate(map, [solidRail]).report.rules[0].units > 0, '... and placed when allowed');
    const lights = D.generate(map, [D.newRule('lightStrip')]).brushes;
    ok(lights.length > 0 && lights.every((b) => Math.abs(b.max[1] - 3.5) < 1e-6), 'light strips: under the low roof');
    const crateRule = D.newRule('scatter'); crateRule.params.step = 2; crateRule.params.chance = 1; crateRule.params.clear = 0.3;
    const crates = D.generate(map, [crateRule]);
    ok(crates.report.rules[0].rejected['blocks a passage'] > 0, 'dense crates: some refused by the walk test');
    ok(objectiveViolations(map, crates.brushes).length === 0, 'dense crates: none in the spawns or on the hill');
    const capped = Object.assign({}, crateRule, { constraints: Object.assign({}, crateRule.constraints, { maxCount: 3 }) });
    ok(D.generate(map, [capped]).report.rules[0].units === 3, 'maxCount caps the units');
    const seed2 = Object.assign({}, crateRule, { seed: 2 });
    ok(!sameList(D.generate(map, [seed2]).brushes, crates.brushes), 'another seed, another layout');
}

section('symmetry');
{
    // a point-symmetric map: the test map's room with symmetric crates
    const map = { name: 's', brushes: [B(0, [-12, -1, -12], [12, 0, 12]), B(0, [-12, 0, -12], [-10, 6, 12]), B(0, [10, 0, -12], [12, 6, 12]), B(0, [-10, 0, -12], [10, 6, -10]), B(0, [-10, 0, 10], [10, 6, 12]),
        B(0, [2, 0, 2], [4, 2.5, 6]), B(0, [-4, 0, -6], [-2, 2.5, -2])], spawns: [{ pos: [-8, 0, 8], yaw: 0, team: 'a' }, { pos: [8, 0, -8], yaw: 0, team: 'b' }], groups: [] };
    for (const g of ['scatter', 'edgeTrim', 'pillars']) {
        const r = D.newRule(g); r.constraints.symmetry = 'point';
        if (g === 'scatter') { r.params.chance = 0.8; r.params.step = 3; }
        if (g === 'pillars') r.params.placement = 'outer';
        if (g === 'edgeTrim') r.target = { kind: 'all' };
        const out = D.generate(map, [r]).brushes, keys = new Set(out.map(D.boxKey));
        ok(out.length > 0 && out.every((b) => keys.has(D.boxKey(D._internal.makeCtx(map, D._internal.buildWorld(map)).mirrorBox(b, 'point')))), g + ': every box has its mirror (' + out.length + ' boxes)');
    }
}

section('targets, tags and the sidecar (apply / remove / regenerate)');
{
    const map = testMap();
    let deco = D.emptyDeco();
    const skirting = D.newRule('edgeTrim'), crates = D.newRule('scatter'), plates = D.newRule('kickPlates');
    deco.rules.push(skirting, crates, plates);
    let res = D.apply(map, deco, null);
    const n = res.added.length;
    ok(n > 0 && res.map.brushes.length === map.brushes.length + n, 'apply adds the boxes at the end of map.brushes');
    ok(res.deco.rules.every((r) => r.generated.length > 0), 'every rule records its boxes');
    ok(res.deco.rules.reduce((s, r) => s + r.generated.length, 0) === n, 'the records add up');
    // reorder the brushes (as an editor would), then regenerate: same boxes, nothing doubled
    const reordered = Object.assign({}, res.map, { brushes: shuffle(res.map.brushes, 'x') });
    const again = D.apply(reordered, res.deco, null);
    ok(again.missing.length === 0 && again.removed.length === n, 'regenerate finds every old box after reordering');
    ok(again.map.brushes.length === res.map.brushes.length && sameList(again.map.brushes, res.map.brushes), 'regenerate gives the same map');
    // remove one rule, then all
    const r1 = D.remove(again.map, again.deco, [crates.id]);
    ok(r1.removed.length === again.deco.rules.find((r) => r.id === crates.id).generated.length || r1.removed.length > 0, 'remove one rule');
    ok(r1.map.brushes.length === again.map.brushes.length - r1.removed.length, 'remove: the count');
    const r2 = D.remove(r1.map, r1.deco, null);
    ok(sameList(r2.map.brushes, map.brushes), 'remove all gives the original map back');
    // a box the user moved is no longer ours: remove reports it missing and leaves it
    const moved = clone(res.map), mine = res.deco.rules[0].generated[0];
    const k = moved.brushes.findIndex((b) => D.sameBox(b, mine));
    moved.brushes[k].min[1] += 0.5; moved.brushes[k].max[1] += 0.5;
    const r3 = D.remove(moved, res.deco, [skirting.id]);
    ok(r3.missing.length === 1 && r3.map.brushes.length === moved.brushes.length - (res.deco.rules[0].generated.length - 1), 'a moved box is reported missing and kept');
    // targets: a tag (saved selection), a group, a live selection, a filter
    deco = D.emptyDeco();
    deco.tags.inner = D.tagFromSelection(map, { brushes: [9, 10, 11, 12] });
    const tagged = D.newRule('edgeTrim', { target: { kind: 'tag', name: 'inner' } });
    const rt = D.generate(map, [tagged], null, { deco }).brushes;
    ok(rt.length > 0 && rt.every((b) => Math.abs(b.min[2]) < 0.4 && Math.abs(b.max[2]) < 0.4), 'tag target: only the inner wall gets trim');
    const shuffled = Object.assign({}, map, { brushes: shuffle(map.brushes, 'tag') });
    ok(sameList(D.generate(shuffled, [tagged], null, { deco }).brushes, rt), 'tag target survives reordering');
    const sel = D.selectionOf(shuffled, deco, { kind: 'tag', name: 'inner' });
    ok(sel.brushes.length === 4 && sel.brushes.every((i) => Math.abs(shuffled.brushes[i].min[2] + 0.25) < 1e-6), 'selectionOf: the tag\'s brushes in the reordered map');
    const grp = D.generate(map, [D.newRule('kickPlates', { target: { kind: 'group', index: 0 } })]).brushes;
    ok(grp.length > 0, 'group target');
    const live = D.generate(map, [D.newRule('edgeTrim')], { brushes: [15] }).brushes;   // the platform, as a live selection
    ok(live.length > 0 && live.every((b) => b.min[0] >= 4 - 0.1 && b.min[2] >= 4 - 0.1), 'live selection override');
    const filt = D.generate(map, [D.newRule('edgeTrim', { target: { kind: 'filter', filter: { role: 'wall', minHeight: 5 } } })]).brushes;
    ok(filt.length > 0 && !filt.some((b) => b.max[2] - b.min[2] < 0.1 && Math.abs(Math.abs(b.min[2] + b.max[2]) / 2 - 0.27) < 0.05), 'filter: walls taller than 5 m: no trim on the 4 m inner wall\'s faces');
    // other rules' boxes are never targets
    deco = D.emptyDeco(); deco.rules.push(D.newRule('scatter', { params: Object.assign(D.newRule('scatter').params, { chance: 1, step: 3 }) }), D.newRule('edgeTrim', { target: { kind: 'all' } }));
    const two = D.apply(map, deco, null);
    const crateBoxes = two.deco.rules[0].generated;
    const trimAll = two.deco.rules[1].generated;
    ok(crateBoxes.length > 0 && !trimAll.some((t) => crateBoxes.some((c) => Math.abs(t.min[1] - c.min[1]) < 1e-6 && boxesOverlap({ min: [t.min[0] - 0.1, t.min[1], t.min[2] - 0.1], max: [t.max[0] + 0.1, t.max[1], t.max[2] + 0.1] }, c) && t.max[1] - t.min[1] < 0.3)), 'trim rule after the crates does not trim the crates');
    // preview: what apply would do, nothing changed
    const pv = D.preview(map, two.deco, [two.deco.rules[0].id]);
    ok(pv.remove.length === 0 && pv.add.length > 0, 'preview on the plain map: adds, removes nothing');
    const pv2 = D.preview(two.map, two.deco, [two.deco.rules[0].id]);
    ok(pv2.remove.length === crateBoxes.length && sameList(pv2.add, crateBoxes), 'preview on the decorated map: removes the old, adds the same');
    // presets load and run
    const idx = JSON.parse(fs.readFileSync(path.join(ED, 'deco_presets', 'index.json'), 'utf8'));
    for (const f of idx) {
        const p = JSON.parse(fs.readFileSync(path.join(ED, 'deco_presets', f), 'utf8'));
        ok(p.name && Array.isArray(p.rules) && p.rules.every((r) => D.generators[r.gen]), 'preset ' + f + ' is well formed');
        const d = D.emptyDeco(); d.rules = p.rules.map(D.ruleFromPreset);
        const a = D.apply(map, d, null);
        ok(a.report.rules.every((r) => r.warnings.length === 0), 'preset ' + f + ' runs clean on the test map');
    }
}
function clone(o) { return JSON.parse(JSON.stringify(o)); }

// ---------------------------------------------------------------- the real maps
function maptool(cmd, body) {
    const out = cp.execFileSync(MAPTOOL, [cmd], { input: JSON.stringify(body), maxBuffer: 1 << 28 });
    return JSON.parse(out.toString());
}
function loadMap(file) {
    if (MAPTOOL) { const m = maptool('parse', { file }); if (!m.error) return m; }
    return parseQmap(fs.readFileSync(file, 'utf8'), path.basename(file, '.qmap'));
}
section('real maps (' + MAPS + (MAPTOOL ? ', checks by ' + path.relative(ROOT, MAPTOOL) : ', no quark_maptool: geometry checks only') + ')');
{
    let files = fs.existsSync(MAPS) ? fs.readdirSync(MAPS).filter((f) => f.endsWith('.qmap')).sort() : [];
    const only = opt('--map', null);
    if (only) files = files.filter((f) => f === only + '.qmap');
    if (QUICK) files = files.filter((f, i) => i % 5 === 0);
    if (!files.length) console.log('  (no maps: run the build, or python3 tools/make_maps.py --out <dir> and pass --maps <dir>)');
    const presets = JSON.parse(fs.readFileSync(path.join(ED, 'deco_presets', 'index.json'), 'utf8'))
        .map((f) => JSON.parse(fs.readFileSync(path.join(ED, 'deco_presets', f), 'utf8')));
    const allGens = { name: 'every generator (defaults)', rules: ALL_GENS.map((g) => ({ gen: g })) };
    for (const f of files) {
        const map = loadMap(path.join(MAPS, f));
        const name = path.basename(f, '.qmap');
        let before = null;
        if (MAPTOOL) before = maptool('check', { map }).checks;
        for (const p of presets.concat([allGens])) {
            const deco = D.emptyDeco(); deco.rules = p.rules.map(D.ruleFromPreset);
            const t0 = Date.now();
            const a = D.apply(map, deco, null);
            const ms = Date.now() - t0;
            const tag = name + ' / ' + p.name;
            const b2 = D.apply(map, D.normDeco(deco), null);
            ok(JSON.stringify(a.added) === JSON.stringify(b2.added), tag + ': deterministic');
            const bad = objectiveViolations(map, a.added);
            ok(bad.length === 0, tag + ': nothing in a spawn / objective: ' + bad.slice(0, 3).join(', '));
            const rm = D.remove(a.map, a.deco, null);
            ok(rm.missing.length === 0 && sameList(rm.map.brushes, map.brushes), tag + ': remove gives the map back');
            let checks = '';
            if (MAPTOOL && a.added.length) {
                const after = maptool('check', { map: a.map }).checks;
                const broke = before.filter((c) => c.pass && after.some((x) => x.id === c.id && !x.pass));
                ok(broke.length === 0, tag + ': the game\'s checks still pass: ' + broke.map((c) => c.id + ' (' + (after.find((x) => x.id === c.id).detail) + ')').join(', '));
                checks = broke.length ? ' CHECKS BROKEN' : ' checks ok';
            }
            const rej = {};
            for (const r of a.report.rules) for (const [k, v] of Object.entries(r.rejected)) rej[k] = (rej[k] || 0) + v;
            console.log('  ' + tag.padEnd(48) + String(a.added.length).padStart(6) + ' boxes ' + String(ms).padStart(6) + ' ms' + checks + '  rejected ' + JSON.stringify(rej));
        }
    }
}

console.log((fails ? 'FAILED' : 'ok') + ': ' + passes + ' passed, ' + fails + ' failed');
process.exit(fails ? 1 : 0);
