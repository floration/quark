// Bot editor tests: node tools/bot_editor/web/test/run.mjs
// DSL round trips on the default brain fixture, blocks / graph back to the same AST, validation, map reading, mock.
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { parse, print, printExpr, BrainError } from "../js/dsl.js";
import { setCatalog, CATALOG, validate, deepEqual, firstDiff, stripIds, numberIds, carryIds, clone, slotKey, declKey, diffBrains, declAreas, decompose, compose, wholeGroups } from "../js/ast.js";
import { brainToBlocks, blocksToBrain, toBlocks, fromBlocks } from "../js/blocks.js";
import { toGraph, fromGraph, overviewGraph, wholeGraph, wholeGraphToBrain, sharedInputs, refitFrame, keepMoved } from "../js/graph.js";
import { parseQmap, mapObjectives, mapSupports, applyHelperParse, mapLabel, mapTitle } from "../js/qmap.js";
import { MockSim } from "../js/mocksim.js";
import { normalizeLine, toSimRequest, toSimObjective } from "../js/helper.js";
import { pctKind } from "../js/values.js";
import { wilson, rateDiff, rateText, summarize } from "../js/batch.js";
import { freeName } from "../js/brainui.js";

const HERE = path.dirname(fileURLToPath(import.meta.url));
let pass = 0, fail = 0;
const t = (name, f) => {
    try { f(); pass++; console.log("  ok   " + name); }
    catch (e) { fail++; console.log("  FAIL " + name + "\n       " + (e.stack || e).toString().split("\n").slice(0, 4).join("\n       ")); }
};
const eq = (a, b, msg = "") => { if (!deepEqual(a, b)) throw new Error(`${msg} ${firstDiff(a, b)}`); };
const same = (a, b, msg = "") => { if (a !== b) throw new Error(`${msg}: ${JSON.stringify(a)} !== ${JSON.stringify(b)}`); };
const plain = (x) => JSON.parse(JSON.stringify(x));   // (drops _loc)

const fixture = (n) => fs.readFileSync(path.join(HERE, "..", "fixtures", n), "utf8");
const DEFAULT = fixture("default.qbrain");
// the authoritative copy, when this checkout has it (tools/bots/default.qbrain): the fixture must match it
const repoDefault = path.join(HERE, "..", "..", "..", "bots", "default.qbrain");

console.log("dsl");
t("print(parse(default)) == default (canonical text round trip)", () => {
    const out = print(parse(DEFAULT));
    if (out !== DEFAULT) {
        const a = DEFAULT.split("\n"), b = out.split("\n");
        const i = a.findIndex((l, k) => l !== b[k]);
        throw new Error(`line ${i + 1}:\n  file:    ${a[i]}\n  printed: ${b[i]}`);
    }
});
t("fixture matches tools/bots/default.qbrain (when present)", () => {
    if (fs.existsSync(repoDefault)) same(fs.readFileSync(repoDefault, "utf8"), DEFAULT, "copy tools/bots/default.qbrain into web/fixtures/");
});
t("parse(print(ast)) deep-equals ast", () => {
    const ast = plain(parse(DEFAULT));
    eq(plain(parse(print(ast))), ast);
    const withIds = numberIds(clone(ast));
    eq(stripIds(plain(parse(print(withIds)))), stripIds(withIds));
});
t("printer: parentheses, literals, inline / block arms", () => {
    const cases = [
        "a - (b - c)", "a - b - c", "(a + b) * c", "a * b / c", "a / (b * c)", "-1.0f * x", "a - -1.0f", "-(a + b)",
        "a ? b : c ? d : e", "(a ? b : c) ? d : e", "a ? (b ? c : d) : e", "not (a < b)", "not (w in hookWeapons)", "not a and b",
        "(a and b) or c", "a and (b or c)", "x == (a < b)", "f(a, b ? c : d)", "t[w]", "1e-7f", "0.35f", "1.0", "3", "1.5e+21", "_",
    ];
    for (const c of cases) {
        const src = `version 1\nbrain "t"\nlevels BEGINNER INTERMEDIATE ADVANCED EXPERT\nslot react:\n  r = ${c}\n`;
        const b = parse(src);
        same(printExpr(b.decls[0].body[0].value), c, "printing " + c);
    }
    const src = `version 1\nbrain "t"\nlevels BEGINNER INTERMEDIATE ADVANCED EXPERT\n\n# c1\nslot fire:\n  # inside\n  if a: fire = yes # t1\n  elif b:\n    alt = yes # t2\n  else: return\n  if c:\n    if d: pass\n# tail\n`;
    same(print(parse(src)), src);
    same(print(parse(src.replace("  else: return", "  else:\n    return"))), src, "block arm with one statement prints inline");
});
t("param values collapse, f32 shortest, data types", () => {
    const b = parse(`version 1\nbrain "t"\nparam p f32 = 0.1 0.1 0.1 0.1\nparam q f32 = 1.5499999523 2 3 4\nparam r bool = yes no true false\nparam s int = -3\n`);
    same(print(b), `version 1\nbrain "t"\nlevels BEGINNER INTERMEDIATE ADVANCED EXPERT\nparam p f32 = 0.1\nparam q f32 = 1.55 2 3 4\nparam r bool = yes no yes no\nparam s int = -3\n`);
});
t("errors carry line:col", () => {
    const bad = [
        [`version 1\nbrain "t"\nslot react:\n  r = (1.0 +\n`, 4, 13],
        [`version 1\nbrain "t"\nslot react:\n  r = a < b < c\n`, 4, 13],
        [`version 1\nbrain "t"\nparam p f32 = 1 2\n`, 3, 18],
        [`version 1\nbrain "t"\nslot react:\nr = 1\n`, 3, 12],
        [`version 2\nbrain "t"\n`, 1, 9],
    ];
    for (const [src, line, col] of bad) {
        try { parse(src); throw new Error("no error for " + JSON.stringify(src)); }
        catch (e) { if (!(e instanceof BrainError)) throw e; same(`${e.line}:${e.col}`, `${line}:${col}`, e.message); }
    }
});

console.log("ast");
t("the default brain validates clean", () => {
    const v = validate(parse(DEFAULT));
    if (v.problems.length) throw new Error(v.problems.slice(0, 5).map((p) => `${p.line}:${p.col} ${p.msg}`).join("\n"));
});
t("validation finds the usual mistakes", () => {
    const v = validate(parse(`version 1\nbrain "t"\nparam p f32 = 1\nslot react:\n  r = nope + 1\n  fov = 2\nslot objective koth:\n  if hasHill: goto hill.floor\nslot pickWeapon:\n  pick = 1 / 2\n`));
    const msgs = v.problems.map((p) => p.msg).join(" | ");
    for (const want of ["unknown name 'nope'", "must end in an action", "int / int"]) if (!msgs.includes(want)) throw new Error("missing: " + want + " in " + msgs);
});
t("decompose / compose are inverse on every node", () => {
    const ast = numberIds(plain(parse(DEFAULT)));
    const check = (n) => eq(compose(decompose(n)), n);
    for (const d of ast.decls) check(d);
});
t("carryIds keeps ids over a text edit", () => {
    const a = numberIds(plain(parse(DEFAULT)));
    const b = carryIds(a, plain(parse(DEFAULT.replace("return dist + angleOff", "return dist * 2.0f + angleOff"))));
    const ids = new Set();
    const walk = (x) => { if (Array.isArray(x)) x.forEach(walk); else if (x && typeof x === "object") { if (x.id) { if (ids.has(x.id)) throw new Error("duplicate id " + x.id); ids.add(x.id); } Object.values(x).forEach(walk); } };
    walk(b);
    same(b.decls[0].id, a.decls[0].id);
});
t("checking a brain leaves it equal to a fresh parse (no caches on the AST)", () => {
    const a = numberIds(plain(parse(DEFAULT))), b = numberIds(plain(parse(DEFAULT)));
    validate(a);
    same(diffBrains(a, b).filter((x) => x.change !== "default").length, 0, "validate made the brain differ");
    eq(stripIds(a), stripIds(b), "AST changed by validate");
    same(JSON.stringify(a), JSON.stringify(b), "validate wrote fields onto the AST");
});
t("diff vs default and provenance areas", () => {
    const def = plain(parse(DEFAULT));
    const mine = plain(parse(DEFAULT.replace("param fov f32 = 1.55 1.7 1.85 2", "param fov f32 = 1.6 1.7 1.85 2").replace("if hasHill: goto hill.floor", "if hasHill: goto me.pos")));
    const d = diffBrains(mine, def).filter((x) => x.change !== "default");
    same(d.map((x) => x.key + "@" + x.area).join(" "), "param:fov@perception slot:objective.koth@mode/koth");
    const areas = new Set(declAreas(def).values());
    for (const a of ["perception", "aim", "weapons", "fight-movement", "roam", "targeting", "mode/bomb"]) if (!areas.has(a)) throw new Error("no decl in area " + a);
});

console.log("blocks / graph");
t("blocks export back to the same AST (whole brain)", () => {
    const ast = numberIds(plain(parse(DEFAULT)));
    eq(blocksToBrain(brainToBlocks(ast)), ast);
    same(print(blocksToBrain(brainToBlocks(ast))), DEFAULT);
});
t("graph exports back to the same AST (every slot and expression curve)", () => {
    const ast = numberIds(plain(parse(DEFAULT)));
    let n = 0;
    for (const d of ast.decls) {
        if (d.kind !== "slot" && !(d.kind === "curve" && d.form === "expr")) continue;
        const g = toGraph(d);
        eq(fromGraph(g), d, slotKey(d));
        n++;
    }
    if (n < 25) throw new Error("only " + n + " graphs");
});
t("graph merges a name used twice into one source node", () => {
    const ast = plain(parse(`version 1\nbrain "t"\nparam p f32 = 1\nslot react:\n  r = p * p + p\n`));
    const g = toGraph(ast.decls[1]);
    same(g.nodes.filter((n) => n.kind === "source").length, 1);
    same(g.edges.filter((e) => e.kind === "data" && g.nodes.find((n) => n.gid === e.from).kind === "source").length, 3);
    eq(fromGraph(g), ast.decls[1]);
});
t("graph layout: no two nodes overlap in a slot", () => {
    const ast = plain(parse(DEFAULT));
    for (const d of ast.decls.filter((x) => x.kind === "slot")) {
        const g = toGraph(d);
        for (let i = 0; i < g.nodes.length; i++) for (let j = i + 1; j < g.nodes.length; j++) {
            const a = g.nodes[i], b = g.nodes[j];
            if (Math.abs(a.x - b.x) < 176 && Math.abs(a.y - b.y) < 24) throw new Error(`${slotKey(d)}: ${a.op} and ${b.op} overlap at ${a.x},${a.y}`);
        }
    }
});
t("overview graph links slots to what they read", () => {
    const ov = overviewGraph(plain(parse(DEFAULT)));
    const has = (a, b) => ov.edges.some((e) => e.from === a && e.to === b);
    if (!has("param:reactMs", "slot:react") || !has("curve:reactDist", "slot:react") || !has("table:idealRange", "slot:idealRange")) throw new Error("missing edges");
});

console.log("whole brain (blocks / graph)");
t("whole-brain groups hold every declaration once, by area, file order inside", () => {
    const ast = plain(parse(DEFAULT));
    const groups = wholeGroups(ast);
    const all = groups.flatMap((g) => g.decls);
    same(all.length, ast.decls.length, "count");
    same(new Set(all).size, ast.decls.length, "each once");
    for (const g of groups) for (let i = 1; i < g.decls.length; i++) if (g.decls[i] < g.decls[i - 1]) throw new Error(g.area + " out of order");
    const areaOf = new Map(groups.flatMap((g) => g.decls.map((i) => [declKey(ast.decls[i]), g.area])));
    same(areaOf.get("slot:react"), "perception"); same(areaOf.get("slot:fire"), "aim"); same(areaOf.get("slot:objective.bomb"), "mode"); same(areaOf.get("slot:urgent"), "mode");
    same(areaOf.get("param:knifeThrowMax"), "weapons");
});
t("whole-brain blocks export back to the identical AST (and one card's edit changes one declaration)", () => {
    const ast = numberIds(plain(parse(DEFAULT)));
    const m = brainToBlocks(ast);
    // the whole canvas renders the model's declarations by group; exporting walks the model in file order
    const shown = wholeGroups(ast).flatMap((g) => g.decls.map((i) => m.decls[i]));
    same(shown.length, m.decls.length);
    eq(blocksToBrain(m), ast);
    same(print(blocksToBrain(m)), DEFAULT);
    const ix = ast.decls.findIndex((d) => d.kind === "param" && d.name === "fov");
    const old = m.decls[ix].fields.values[0];
    m.decls[ix].fields.values[0] = 1.25;
    const edited = blocksToBrain(m);
    const changed = edited.decls.map((d, i) => (deepEqual(d, ast.decls[i]) ? null : i)).filter((x) => x !== null);
    eq(changed, [ix]);
    m.decls[ix].fields.values[0] = old;
    eq(blocksToBrain(m), ast);
});
t("whole-brain graph exports back to the identical AST (shared inputs off and on)", () => {
    const ast = numberIds(plain(parse(DEFAULT)));
    for (const shared of [false, true]) {
        const wg = wholeGraph(ast, { shared });
        const want = ast.decls.filter((d) => d.kind === "slot" || (d.kind === "curve" && d.form === "expr")).length;
        same(wg.frames.length, want, "one frame per slot / expression curve");
        same(new Set(wg.frames.map((f) => f.key)).size, want);
        const back = wholeGraphToBrain(ast, wg);
        eq(back, ast, "shared " + shared);
        same(print(back), DEFAULT);
    }
});
t("whole-brain graph layout: frames don't overlap, nodes sit inside their frame, clusters by area", () => {
    const wg = wholeGraph(plain(parse(DEFAULT)), { shared: true });
    const F = wg.frames;
    for (let i = 0; i < F.length; i++) for (let j = i + 1; j < F.length; j++) {
        const a = F[i], b = F[j];
        if (a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h) throw new Error(`${a.key} overlaps ${b.key}`);
    }
    for (const f of F) for (const n of f.g.nodes) {
        const x = f.x + f.dx + n.x, y = f.y + f.dy + n.y;
        if (x < f.x || y < f.y || x + 176 > f.x + f.w + 0.5 || y > f.y + f.h) throw new Error(`${f.key}: node ${n.op} outside its frame`);
    }
    for (const c of wg.clusters) for (const f of c.frames) if (f.x < c.x || f.y < c.y || f.x + f.w > c.x + c.w + 0.5 || f.y + f.h > c.y + c.h + 0.5) throw new Error(`${f.key} outside cluster ${c.area}`);
    same(wg.clusters.map((c) => c.area).join(","), "perception,targeting,aim,weapons,fight-movement,roam,mode");
});
t("shared inputs: one node per param / curve / list / table read, sensors read by 2+ slots, every use wired", () => {
    const ast = plain(parse(DEFAULT));
    const wg = wholeGraph(ast, { shared: true });
    const by = new Map(wg.shared.nodes.map((n) => [n.id, n]));
    for (const id of ["param:reactMs", "curve:reactDist", "list:close", "table:idealRange"]) if (!by.has(id)) throw new Error("missing " + id);
    for (const n of wg.shared.nodes) {
        if (n.kind === "sensor" && n.frames.length < 2) throw new Error("sensor " + n.name + " in one slot");
        for (const u of n.uses) if (!wg.frames[u.f].g.nodes.some((x) => x.gid === u.gid)) throw new Error("dangling use of " + n.id);
    }
    const react = wg.frames.findIndex((f) => f.key === "slot:react");
    if (!by.get("curve:reactDist").uses.some((u) => u.f === react)) throw new Error("reactDist not wired to react");
});
t("whole-brain graph: moved nodes are kept and a frame refits without moving them", () => {
    const ast = plain(parse(DEFAULT));
    const wg = wholeGraph(ast);
    const f = wg.frames.find((x) => x.key === "slot:fire");
    const n = f.g.nodes.find((x) => x.kind === "stmt");
    const wx = f.x + f.dx + n.x - 300, wy = f.y + f.dy + n.y;
    n.x -= 300; n.moved = true;
    refitFrame(f);
    same(f.x + f.dx + n.x, wx); same(f.y + f.dy + n.y, wy);
    const again = wholeGraph(ast, { keep: new Map([[f.key, f.g]]) });
    const n2 = again.frames.find((x) => x.key === "slot:fire").g.nodes.find((x) => x.fields.id === n.fields.id && x.op === n.op && x.moved);
    if (!n2 || n2.x !== n.x) throw new Error("moved position lost");
    eq(wholeGraphToBrain(ast, again), ast);
});

console.log("maps / simulation");
const MAP = `version 2\ntitle Test\nfor koth\nbox -10 -1 -10 10 0 10\nbox -2 0 -2 2 2 2\nhill -3 0 -3 3 3 3\nspawn a -8 0 8 0\nspawn b 8 0 -8 180\nladder 4 0 4 5 3 4.5 auto\ngroup room 0 0 -9 0 -9 -5 3 -5\ngroup stairs 1 2 5 0 -9 9 2 -7\nsite -9 0 -9 -7 1 -7\n`;
t("qmap: boxes, generated boxes, markers", () => {
    const m = parseQmap(MAP, "test");
    same(m.title, "Test"); same(m.forMode, "koth"); same(m.spawns.length, 2); same(m.sites.length, 1); same(!!m.hill, true);
    if (m.nBoxes < 2 + 6 + 4) throw new Error("expected ladder rungs, room walls and stairs, got " + m.nBoxes + " boxes");
    same(m.warnings.length, 0);
    same(mapObjectives(m).map((o) => o.kind).join(" "), "site hill");
});
t("mock simulation is deterministic and writes the stream schema", () => {
    const m = parseQmap(MAP, "test");
    const req = { mode: "koth", secs: 20, seed: 7, bots: [{ name: "A", team: "a", skill: "EXPERT" }, { name: "B", team: "b", skill: "BEGINNER" }] };
    const run = () => { const s = new MockSim(m, req); let f, last; while ((f = s.step())) last = f; return [last, s.endLine()]; };
    const [f1, e1] = run(), [f2, e2] = run();
    eq(e1, e2); eq(f1, f2);
    for (const k of ["id", "pos", "yaw", "health", "alive", "target", "path", "state", "decision"]) if (!(k in f1.bots[0])) throw new Error("frame bot lacks " + k);
    eq(normalizeLine(JSON.parse(JSON.stringify(f1))).bots[0].pos, f1.bots[0].pos);
});
t("helper adapter reads other spellings", () => {
    const n = normalizeLine({ type: "state", time: 1.5, players: [{ slot: 2, origin: { x: 1, y: 2, z: 3 }, yawDeg: 90, hp: 50, debug: { goal: { x: 4, y: 5, z: 6 }, label: "go" } }] });
    same(n.type, "frame"); same(n.t, 1.5); eq(n.bots[0].pos, [1, 2, 3]); same(Math.round(n.bots[0].yaw * 1000), 1571); eq(n.bots[0].goal, [4, 5, 6]); same(n.bots[0].decision, "go");
});

console.log("maps (modes, helper geometry)");
t("mode support follows Map::canSupportMode", () => {
    const m = parseQmap("version 6\nbox 0 -1 0 10 0 10\nspawn att0 1 0 1 0\nspawn def0 9 0 9 0\npoint 2 0 2 4 2 4\npoint 6 0 6 8 2 8\n", "x");
    same(mapSupports(m, "points"), true, "points"); same(mapSupports(m, "vs"), false, "staged spawns are not teams a / b");
    same(mapSupports(m, "ffa"), true, "ffa"); same(mapSupports(m, "race"), false, "race");
    const v = parseQmap("version 6\nbox 0 -1 0 10 0 10\nspawn a 1 0 1\nspawn b 9 0 9\nhill 4 0 4 6 2 6\n", "y");
    same(mapSupports(v, "koth"), true, "koth"); same(mapSupports(v, "duel"), true, "duel"); same(mapSupports(v, "bomb"), false, "bomb");
});
t("the helper's parse replaces the geometry and the mode answer", () => {
    const m = parseQmap("version 6\nbox 0 -1 0 10 0 10\nspawn a 1 0 1\n", "z");
    same(applyHelperParse(m, { brushes: [{ mat: 0, min: [0, -1, 0], max: [10, 0, 10] }], generated: [{ mat: 8, min: [1, 0, 1], max: [2, 3, 1.1] }], ladders: [], modesPossible: ["ffa"], warnings: [] }), true);
    same(m.nBoxes, 2); same(m.mats[1], 8); same(mapSupports(m, "ffa"), true); same(mapSupports(m, "hide"), false);
    same(m.bounds.x1, 10, "bounds skip trim");
    same(applyHelperParse(m, { error: "x" }), false, "a failed answer keeps the page's reading");
});

console.log("sim (quark_maptool sim, SIM.md)");
t("page objectives -> sim overrides", () => {
    same(toSimObjective({ kind: "default" }), null);
    eq(toSimObjective({ kind: "goto", pos: [1, 0, 2] }), { type: "goto", pos: [1, 0, 2] });
    same(toSimObjective({ kind: "goto" }), null, "no point yet: none");
    eq(toSimObjective({ kind: "hold", pos: [1, 0, 2], radius: 6, chase: false }), { type: "hold", pos: [1, 0, 2], radius: 6, chase: false });
    eq(toSimObjective({ kind: "follow", points: [[0, 0, 0], [5, 0, 5]] }), { type: "route", points: [[0, 0, 0], [5, 0, 5]], loop: true });
    eq(toSimObjective({ kind: "objective", role: "defend", target: { kind: "site", index: 1 } }), { type: "defend", objectiveIndex: 1 });
});
t("the run request -> sim's", () => {
    const r = toSimRequest({ map: { name: "depot", source: "default", text: "x" }, mode: "bomb", seed: 3, secs: 60, speed: "max",
        bots: [{ name: "A", team: "a", skill: "expert", brain: "(editor)", pos: [1, 0, 2], yaw: 90, objective: { kind: "hold", pos: [3, 0, 4] } }, { name: "B", team: "any", brain: "default" }],
        brains: { "(editor)": "version 1\n" } });
    same(r.realtime, false); same(r.speed, 0); same(r.seconds, 60); same(r.bots[0].skill, "EXPERT"); same(r.bots[0].brain, "(editor)");
    same(Math.round(r.bots[0].yaw * 1000), 1571); same(r.bots[0].objective.type, "hold"); same("team" in r.bots[1], false); same("objective" in r.bots[1], false);
    same(r.brains["(editor)"], "version 1\n");
});
t("sim lines: paths carried, the end's winner and scores", () => {
    normalizeLine({ type: "start", mode: "vs", teams: true, bots: [{ id: 0, name: "A", team: "a", brain: "x", brainStatus: "unsupported" }] });
    const f1 = normalizeLine({ type: "frame", t: 0.1, bots: [{ id: 0, alive: true, pos: [0, 0, 0], yaw: 0, health: 100, weapon: "RIFLE", state: "move", decision: "override:hold", target: -1, goal: [1, 0, 1], path: [[1, 0, 1]], kills: 0, deaths: 0, damage: 0 }], events: [{ t: 0.1, kind: "kill", who: 0, victim: 1, detail: "rifle" }] });
    const f2 = normalizeLine({ type: "frame", t: 0.2, bots: [{ id: 0, alive: true, pos: [0, 0, 0], yaw: 0, health: 100, weapon: "RIFLE", state: "move", decision: "override:hold (there)", target: -1, goal: [1, 0, 1], kills: 1 }], events: [] });
    eq(f2.bots[0].path, [[1, 0, 1]], "a frame without path keeps the last"); same(f2.bots[0].decision, "override:hold (there)"); same(f1.bots[0].weapon, "rifle");
    same(f1.events[0].type, "kill"); same(f1.events[0].killer, 0);
    const e = normalizeLine({ type: "end", t: 30, reason: "matchEnd", winner: { team: "a", player: null, text: "TEAM A wins the match" }, bots: [{ id: 0, name: "A", kills: 3 }], result: {}, perf: { speedup: 400 } });
    same(e.winner, "a"); same(e.winnerText, "TEAM A wins the match"); same(e.scores[0].kills, 3);
    normalizeLine({ type: "start", mode: "ffa", teams: false, bots: [] });
    same(normalizeLine({ type: "end", reason: "matchEnd", winner: { team: null, player: 2, text: "x" }, bots: [], perf: {} }).winner, 2);
    same(normalizeLine({ type: "end", reason: "time", winner: null, leader: { team: "b" }, bots: [], perf: {} }).winner, null);
});

console.log("values");
t("% labels only for shares and chances: speeds and gains (m/s, rad/s, 1/s) get none", () => {
    const d = Object.fromEntries(parse(DEFAULT).decls.filter((x) => x.kind === "param").map((x) => [x.name, x]));
    for (const n of ["hearStepSpeed", "turnRate", "turnAccel", "trackGain", "lookGain", "trackPerRad"]) same(pctKind(d[n]), null, n);
    for (const n of ["jukeRate", "jumpRate", "dashRate", "pulledDashRate"]) same(pctKind(d[n]), "rate", n);
    for (const n of ["strafe", "fightHookChance", "soundErr", "airStrafe"]) same(pctKind(d[n]), "share", n);
});

console.log("batch statistics");
t("win rates: Wilson interval, the difference of two runs, per brain counted per match", () => {
    const c = wilson(31, 50);
    same(Math.round(c.lo * 1000), 482, "lo"); same(Math.round(c.hi * 1000), 741, "hi");
    same(wilson(0, 10).lo, 0); same(wilson(10, 10).hi, 1); same(wilson(0, 0).hi, 1, "no matches: anything");
    same(rateText(31, 50).text, "62% ±13");
    same(rateDiff(30, 60, 45, 60).sure, true, "50% -> 75% over 60 each is more than chance");
    same(rateDiff(30, 60, 33, 60).sure, false, "50% -> 55% isn't");
    // a 2 v 2: brain A on team a (2 bots), B on team b; A's team wins 2 of 3 matches, one ran out of time
    const bot = (id, team, brain, won, kills) => ({ id, name: "N" + id, team, brain, brainLabel: brain + "#1", kills, deaths: 1, won });
    const ms = [1, 2, 3].map((k) => ({ map: "m", seed: k, t: 60, winner: k < 3 ? { team: "a", text: "TEAM A wins" } : null,
        bots: [bot(0, "a", "A", k < 3, 2), bot(1, "a", "A", k < 3, 1), bot(2, "b", "B", false, 0), bot(3, "b", "B", false, 1)] }));
    ms.push({ map: "m", seed: 4, error: "boom" });
    const S = summarize(ms), A = S.brains.find((b) => b.name === "A"), B = S.brains.find((b) => b.name === "B");
    same(A.n, 3, "two bots of one brain are one match"); same(A.w, 2); same(A.botMatches, 6); same(A.kills, 9); same(B.w, 0);
    same(S.maps[0].a, 2); same(S.maps[0].timeouts, 1); same(S.maps[0].errors, 1); same(S.bots.length, 4);
});

t("batch: a map of yours named like a shipped one is its own row", () => {
    const m = (mapKey, mapLabel, seed) => ({ map: "temple", mapKey, mapLabel, seed, t: 30, winner: { team: "a" }, bots: [{ id: 0, name: "A", team: "a", brain: "default", won: true }] });
    const S = summarize([m("default/temple", "Temple", 1), m("user/temple", "Temple (yours)", 1), m("user/temple", "Temple (yours)", 2)]);
    same(S.maps.length, 2); same(S.maps.find((x) => x.key === "user/temple").matches, 2); same(S.maps.find((x) => x.key === "default/temple").map, "Temple");
    same(mapLabel({ name: "mybox", source: "user", title: "Harbor" }), "Harbor · mybox.qmap (yours)");
    same(mapLabel({ name: "temple", source: "default", title: "Temple" }), "Temple");
    same(mapTitle({ name: "temple", source: "user", title: "Temple" }), "Temple");
});

console.log("files");
t("an opened file named like a saved brain gets a free name", () => {
    same(freeName("mine", ["default", "other"]), "mine");
    same(freeName("audit1", ["default", "audit1", "audit1-2"]), "audit1-3");
    same(freeName("default", ["default"]), "default-2");
});

console.log("catalog (quark_tests exportbrain)");
t("the game's catalog replaces the page's", () => {
    const keep = JSON.parse(JSON.stringify({ slots: CATALOG.slots, builtins: CATALOG.builtins, common: CATALOG.common, params: CATALOG.params }));
    try {
        setCatalog({ sensors: [{ name: "now", type: "f64", common: true }, { name: "off", type: "f32" }, { name: "dist", type: "f32" }, { name: "sinceSeen", type: "f64" }],
            slots: [{ name: "react", when: "a threat comes into view", sensors: ["off", "dist", "sinceSeen"], outputs: [{ name: "r", type: "f32" }], rolls: 0 }],
            builtins: [{ name: "roll", sig: "roll() -> f32", draws: 1 }, { name: "min", sig: "min(a, b)" }, { name: "site", sig: "site(i int) -> point" }],
            params: [{ name: "reactMs", type: "f32", unit: "ms", min: 0, max: 1800, doc: "reaction time", default: [450, 300, 200, 150] }] });
        same(CATALOG.slots.react.sensors.off, "f32"); same(CATALOG.slots.react.outputs.r, "f32"); same(CATALOG.slots.react.area, "perception", "the page's area kept");
        same(CATALOG.builtins.roll.draws, 1); same(CATALOG.builtins.min.type, "common"); same(CATALOG.builtins.site.args, 1); same(CATALOG.builtins.site.type, "point");
        same(CATALOG.common.now, "f64"); same(CATALOG.params.reactMs.max, 1800);
        const v = validate(parse('version 1\nbrain "x"\nparam reactMs f32 = 2000\n'));
        same(v.problems.some((p) => /outside 0..1800/.test(p.msg)), true, "range from the game");
    } finally { Object.assign(CATALOG, keep); CATALOG.fromGame = false; }
});

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
