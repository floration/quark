// Bot editor tests: node tools/bot_editor/web/test/run.mjs
// DSL round trips on the default brain fixture, blocks / graph back to the same AST, validation, map reading, mock.
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { parse, print, printExpr, BrainError } from "../js/dsl.js";
import { validate, deepEqual, firstDiff, stripIds, numberIds, carryIds, clone, slotKey, diffBrains, declAreas, decompose, compose } from "../js/ast.js";
import { brainToBlocks, blocksToBrain, toBlocks, fromBlocks } from "../js/blocks.js";
import { toGraph, fromGraph, overviewGraph } from "../js/graph.js";
import { parseQmap, mapObjectives } from "../js/qmap.js";
import { MockSim } from "../js/mocksim.js";
import { normalizeLine } from "../js/helper.js";

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

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
