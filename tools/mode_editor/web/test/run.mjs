// Gamemode editor tests: node tools/mode_editor/web/test/run.mjs
// Adapted from tools/bot_editor/web/test/run.mjs at c7aae27. Text <-> AST <-> blocks <-> graph round trips on every
// built-in text (tools/modes/*.qlogic when this checkout has them, else the fixtures from the design) merged for all
// 13 modes; the .qmode file rule (foreign lines kept verbatim: physics keys, unknown keys, CRLF; owned keys patched in
// place; nothing after `logic`); and serve.py itself (save, the 409 refusal when the file changed on disk, 400 when a
// save would touch a foreign line, the Host / header checks).
import fs from "fs";
import os from "os";
import path from "path";
import http from "http";
import net from "net";
import { spawn } from "child_process";
import { fileURLToPath } from "url";
import { parse, print, printExpr, ModeError } from "../js/qmode.js";
import { CATALOG, validate, deepEqual, firstDiff, stripIds, numberIds, clone, slotKey, declKey, mergeLogic, diffLogic, interpretedSlots, costs } from "../js/lang.js";
import { logicToBlocks, blocksToLogic } from "../js/blocks.js";
import { toGraph, fromGraph, overviewGraph } from "../js/graph.js";
import { splitQmode, patchQmode, foreignLines, newQmodeText, modeKeys, ownerOf, logicKeyClash } from "../js/qfile.js";
import { loadModeText, logicTextFor, fileTextFor } from "../js/modeui.js";
import { phasesModel } from "../js/phases.js";
import { timelineModel } from "../js/sim.js";
import { MODES } from "../js/catalog.js";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const TOOL = path.join(HERE, "..", "..");
const REPO_MODES = path.join(TOOL, "..", "modes");
let pass = 0, fail = 0;
const results = [];
const t = async (name, f) => {
    try { await f(); pass++; console.log("  ok   " + name); }
    catch (e) { fail++; console.log("  FAIL " + name + "\n       " + (e.stack || e).toString().split("\n").slice(0, 5).join("\n       ")); }
};
const eq = (a, b, msg = "") => { if (!deepEqual(a, b)) throw new Error(`${msg} ${firstDiff(a, b)}`); };
const same = (a, b, msg = "") => { if (a !== b) throw new Error(`${msg}: ${JSON.stringify(a)} !== ${JSON.stringify(b)}`); };
const ok = (c, msg) => { if (!c) throw new Error(msg); };
const plain = (x) => JSON.parse(JSON.stringify(x));   // (drops _loc)

// the built-in texts: the repository's when G2's tools/modes/*.qlogic exist, else this page's fixtures
const SRC = fs.existsSync(path.join(REPO_MODES, "common.qlogic")) ? REPO_MODES : path.join(HERE, "..", "fixtures");
const TEXTS = {};
for (const n of ["common", ...MODES]) { const p = path.join(SRC, n + ".qlogic"); if (fs.existsSync(p)) TEXTS[n] = fs.readFileSync(p, "utf8"); }
console.log(`built-in texts from ${path.relative(process.cwd(), SRC) || SRC}: ${Object.keys(TEXTS).join(", ")}`);
const ASTS = Object.fromEntries(Object.entries(TEXTS).map(([n, x]) => [n, plain(parse(x))]));
const defaultFor = (b) => mergeLogic([ASTS.common, ASTS[b]], b);

console.log("qmode text");
await t("print(parse(text)) == text for every built-in text (canonical round trip)", () => {
    for (const [n, text] of Object.entries(TEXTS)) {
        const out = print(parse(text));
        if (out !== text) { const a = text.split("\n"), b = out.split("\n"); const i = a.findIndex((l, k) => l !== b[k]); throw new Error(`${n} line ${i + 1}:\n  file:    ${a[i]}\n  printed: ${b[i]}`); }
    }
});
await t("parse(print(ast)) deep-equals ast (every text, and merged for all 13 modes)", () => {
    for (const [n, a] of Object.entries(ASTS)) eq(plain(parse(print(a))), a, n);
    for (const m of MODES) { const a = defaultFor(m); eq(stripIds(plain(parse(print(a)))), stripIds(plain(a)), m); }
});
await t("printer: precedence, aggregates, for, index, strings, actions", () => {
    const exprs = ["a - (b - c)", "(a + b) * c", "a ? b : c ? d : e", "not (a < b)", "(a and b) or c", "count(p in playing where alive(p))",
        "best(p in team(A) where alive(p) by score(p))", "any(p in everyone)", "held[A]", "held[team(p)] + 1", "int(x)"];
    for (const e of exprs) {
        const src = `logic 1\nbase ffa\nslot tick:\n  r = ${e}\n`;
        same(printExpr(parse(src).decls[0].body[0].value), e, "printing " + e);
    }
    const src = `logic 1\nbase koth\nstate match held[team] f32 = 0\nslot tick:\n  for p in playing where alive(p):\n    if team(p) == A: held[A] = held[A] + dt\n    else: break\n  announce("round over", 3)\n  endRound(A)\n`;
    same(print(parse(src)), src);
});
await t("errors carry line:col", () => {
    const bad = [[`logic 1\nslot tick:\n  r = (1 +\n`, 3], [`logic 9\n`, 1], [`logic 1\nslot tick:\nr = 1\n`, 2]];
    for (const [src, line] of bad) {
        try { parse(src); throw new Error("no error for " + JSON.stringify(src)); }
        catch (e) { if (!(e instanceof ModeError)) throw e; same(e.line, line, e.message); ok(e.col >= 1, "col"); }
    }
});

console.log("checks");
await t("every built-in mode's merged logic passes the editor's checks (no errors)", () => {
    for (const m of MODES) {
        const v = validate(parse(print(defaultFor(m))), { base: m });
        if (v.errors.length) throw new Error(`${m}: ${v.errors.slice(0, 3).map((p) => `${p.line}:${p.col} ${p.msg}`).join("; ")}`);
    }
});
await t("checks catch: unknown slot, duplicate, recursion, break outside for", () => {
    const cases = [
        [`logic 1\nbase ffa\nslot nope:\n  pass\n`, /slot/],
        [`logic 1\nbase ffa\nparam a f32 = 1\nparam a f32 = 2\n`, /twice/],
        [`logic 1\nbase ffa\nproc f:\n  f()\nslot tick:\n  f()\n`, /themselves/],
        [`logic 1\nbase ffa\nslot tick:\n  break\n`, /break/],
    ];
    for (const [src, re] of cases) {
        const v = validate(parse(src), { base: "ffa" });
        ok(v.errors.some((p) => re.test(p.msg)), `expected ${re} for ${JSON.stringify(src)}; got ${v.errors.map((p) => p.msg).join(" | ") || "nothing"}`);
    }
});
await t("cost: loops count x32 in the worst case", () => {
    const a = parse(`logic 1\nbase ffa\nslot tick:\n  for p in playing:\n    kill(p)\n`);
    const c = costs(a).get("slot:tick");
    ok(c.worst > c.insns * 8, JSON.stringify(c));
});

console.log("merge / diff");
await t("a file that only changes a number: one changed decl, no interpreted slot", () => {
    const def = defaultFor("koth");
    const ast = clone(def);
    ast.decls.find((d) => d.name === "target").value = 150;
    const d = diffLogic(ast, def).filter((x) => x.change !== "default");
    same(d.length, 1); same(d[0].key, "param:target");
    same(interpretedSlots(ast, def).size, 0);
    const text = logicTextFor(ast, def, false);
    ok(/param target int = 150/.test(text) && !/slot/.test(text), text);
    same(logicTextFor(def, def, false), "", "no differences -> no logic block");
});
await t("changing a proc makes the slots that call it interpreted", () => {
    const def = defaultFor("ffa");
    const procs = def.decls.filter((d) => d.kind === "proc");
    if (!procs.length) return;   // (the fixtures may have none)
    const ast = clone(def);
    ast.decls.find((d) => d.kind === "proc").body.push({ op: "pass" });
    ok(interpretedSlots(ast, def).size > 0, "no slot interpreted");
});
await t("loadModeText: key lines + logic -> merged AST, and back to the same file", () => {
    const user = `custommode=fast\nmode=koth\ngravity=0.8\nmove_air=2\nfoo=bar\ntimelimit=0\nlogic 1\nbase koth\nparam target int = 50\n`;
    const m = loadModeText(user, defaultFor);
    same(m.base, "koth");
    same(m.ast.decls.find((d) => d.name === "target").value, 50);
    same(fileTextFor(user, {}, m.ast, m.def, false), user);
});

console.log("blocks / graph");
await t("blocks export back to the same AST (all 13 merged defaults)", () => {
    for (const m of MODES) {
        const ast = numberIds(plain(defaultFor(m)));
        eq(blocksToLogic(logicToBlocks(ast)), ast, m);
        same(print(blocksToLogic(logicToBlocks(ast))), print(ast), m);
    }
});
await t("graph exports back to the same AST (every slot, proc and expression curve, all 13 modes)", () => {
    let n = 0;
    for (const m of MODES) {
        const ast = numberIds(plain(defaultFor(m)));
        for (const d of ast.decls) {
            if (d.kind !== "slot" && d.kind !== "proc" && !(d.kind === "curve" && d.form === "expr")) continue;
            eq(fromGraph(toGraph(d)), d, `${m} ${declKey(d)}`);
            n++;
        }
    }
    ok(n >= 13 * 5, "only " + n + " graphs");
});
await t("graph layout: no two nodes overlap in a slot", () => {
    for (const m of ["ffa", "koth", "bomb"]) for (const d of defaultFor(m).decls.filter((x) => x.kind === "slot")) {
        const g = toGraph(d);
        for (let i = 0; i < g.nodes.length; i++) for (let j = i + 1; j < g.nodes.length; j++) {
            const a = g.nodes[i], b = g.nodes[j];
            if (Math.abs(a.x - b.x) < 176 && Math.abs(a.y - b.y) < 24) throw new Error(`${m} ${slotKey(d)}: ${a.op} and ${b.op} overlap`);
        }
    }
});
await t("overview graph links slots to the params / state they read", () => {
    const ov = overviewGraph(defaultFor("koth"));
    ok(ov.edges.some((e) => e.from === "param:respawn" && e.to === "slot:respawnDelay"), "respawn -> respawnDelay");
    ok(ov.edges.some((e) => e.from === "state:held" && e.to === "slot:tick"), "held -> tick");
});

console.log(".qmode file rule");
const PHYS = `custommode=lowgrav\r\nmode=koth\r\nweapons=0\r\ngravity=0.5\r\nmove_airaccel=12\r\nmove.friction=4\r\nspeed=1.2\r\nsomefuturekey=x=y\r\n  # a comment someone wrote\r\nweapon=0,1,1,1\r\ntimelimit=300\r\nlabel=lowgrav#ab12\r\n`;
await t("owners: mode keys, physics keys and prefixes, undecided, unknown", () => {
    same(ownerOf("timelimit"), "mode"); same(ownerOf("gravity"), "physics"); same(ownerOf("move_x"), "physics"); same(ownerOf("move.y"), "physics");
    same(ownerOf("weapon"), "undecided"); same(ownerOf("somefuturekey"), "unknown"); same(ownerOf("label"), "game"); same(ownerOf(null), "none");
});
await t("save keeps every foreign line verbatim and in place (physics keys, unknown keys, comments, CRLF)", () => {
    const def = defaultFor("koth");
    const ast = clone(def);
    ast.decls.find((d) => d.name === "target").value = 30;
    const out = fileTextFor(PHYS, { timelimit: "600", scorelimit: "40" }, ast, def, false);
    eq(foreignLines(out), foreignLines(PHYS));
    const a = PHYS.split("\r\n"), b = out.split("\r\n");
    for (let i = 0; i < a.length - 1; i++) if (!a[i].startsWith("timelimit=")) same(b[i], a[i], "line " + (i + 1));
    same(b[10], "timelimit=600", "patched in place");
    same(b[12], "scorelimit=40", "a missing key goes just above logic");
    same(b[13], "logic 1");
    ok(!/[^\r]\n/.test(out), "every line keeps CRLF");
    same(modeKeys(out).timelimit, "600");
});
await t("save never writes a key line after logic, and refuses keys it doesn't own", () => {
    const sp = splitQmode(PHYS + "logic 1\r\nbase koth\r\n");
    same(sp.lines.length, 12); same(sp.logicLine, 13);
    let threw = false;
    try { patchQmode(PHYS, { gravity: "2" }, ""); } catch { threw = true; }
    ok(threw, "patching gravity must throw");
    ok(logicKeyClash("logic 1\nbase ffa\ngravity=2\n"), "gravity=2 after logic is a clash");
    ok(!logicKeyClash("logic 1\nslot tick:\n  x = 2\n"), "an assignment isn't a key line");
});
await t("a file with no logic: a numbers-only save adds no block; dropping the logic removes it", () => {
    const def = defaultFor("ffa");
    const plainFile = newQmodeText("x", "ffa");
    same(fileTextFor(plainFile, { timelimit: "120" }, clone(def), def, false), plainFile.replace("timelimit=0", "timelimit=120"));
    same(patchQmode(plainFile + "logic 1\nbase ffa\nparam respawn f32 = 1\n", {}, ""), plainFile);
});
await t("unknown lines before logic stay before it; text after logic belongs to logic", () => {
    const f = "mode=ffa\nzzz\nlogic 1\nbase ffa\n";
    const sp = splitQmode(f);
    same(sp.lines.map((l) => l.raw).join("|"), "mode=ffa|zzz");
    same(sp.logicText, "logic 1\nbase ffa\n");
});

console.log("phases / sim models");
await t("phases: built-in slots native, a changed slot interpreted, object events under their object", () => {
    const def = defaultFor("bomb");
    const ast = clone(def);
    const tick = ast.decls.find((d) => d.kind === "slot" && d.name === "tick");
    tick.body.push({ op: "pass" });
    const m = phasesModel(ast, def, validate(ast, { base: "bomb" }));
    const all = m.columns.flatMap((c) => [...c.enter, ...c.during, ...c.objects.flatMap((o) => o.events)]);
    same(all.find((s) => s.name === "tick").status, "interpreted");
    same(all.find((s) => s.name === "respawnDelay").status, "native");
    ok(m.columns.find((c) => c.id === "live").objects.some((o) => o.name === "bomb" && o.on), "bomb object on the round");
    same(all.find((s) => s.name === "timeUp").status === "unwritten" || all.find((s) => s.name === "timeUp").status === "native", true);
});
await t("sim timeline model reads modesim NDJSON without inventing values", () => {
    const lines = [{ type: "start", label: "x", base: "koth", interpreted: ["tick"] },
        { type: "frame", t: 0, phase: "freeze", scoreA: 0, scoreB: 0, players: [] },
        { type: "frame", t: 3, phase: "live", scoreA: 1, scoreB: 0, players: [{ id: 1, name: "a", team: 0, pos: [1, 0, 2], score: 1 }], events: [{ type: "kill", victim: 2 }] },
        { type: "slot", t: 3, slot: "tick", ns: 900 },
        { type: "end", winner: "A", reason: "score", rounds: 1, perf: { ticks: 180, logicNs: { avg: 800, p99: 1200, max: 1500 } } }];
    const m = timelineModel(lines);
    same(m.frames.length, 2); same(m.bands.length, 2); same(m.markers.length, 1); same(m.slots.length, 1);
    same(m.end.winner, "A"); same(m.maxScore, 1); same(m.t1, 3);
    same(timelineModel([]).end, null);
});

console.log("serve.py");
const freePort = () => new Promise((res) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const p = s.address().port; s.close(() => res(p)); }); });
const request = (port, method, url, { body, headers = {} } = {}) => new Promise((res, rej) => {
    const data = body === undefined ? undefined : typeof body === "string" ? body : JSON.stringify(body);
    const r = http.request({ host: "127.0.0.1", port, method, path: url, headers: { ...(data !== undefined ? { "Content-Type": typeof body === "string" ? "text/plain" : "application/json", "Content-Length": Buffer.byteLength(data) } : {}), ...headers } }, (resp) => {
        let s = ""; resp.setEncoding("utf8"); resp.on("data", (c) => (s += c)); resp.on("end", () => { let j = null; try { j = JSON.parse(s); } catch {} res({ status: resp.statusCode, text: s, json: j }); });
    });
    r.on("error", rej);
    if (data !== undefined) r.write(data);
    r.end();
});
const W = { "X-Quark-Editor": "1" };
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "qme-test-"));
const port = await freePort();
const srv = spawn(process.env.PYTHON || "python3", [path.join(TOOL, "serve.py"), "--port", String(port), "--data-dir", tmp, "--no-open",
    "--quark-tests", path.join(tmp, "no_quark_tests"), "--maptool", path.join(tmp, "no_maptool")], { env: { ...process.env, QUARK_SHELL: "1", QME_PROVENANCE: path.join(tmp, "provenance.json") }, stdio: ["ignore", "pipe", "pipe"] });
let srvOut = "";
srv.stdout.on("data", (d) => (srvOut += d)); srv.stderr.on("data", (d) => (srvOut += d));
for (let i = 0; i < 100; i++) { try { const r = await request(port, "GET", "/api/ping"); if (r.status === 200) break; } catch {} await new Promise((r) => setTimeout(r, 100)); }
try {
    const dir = path.join(tmp, "custom_modes");
    fs.mkdirSync(dir, { recursive: true });
    const file = path.join(dir, "lowgrav.qmode");
    fs.writeFileSync(file, PHYS);
    await t("serves the page and its config (no binaries: built-in catalog, fixtures or repo texts, no modesim)", async () => {
        same((await request(port, "GET", "/")).status, 200);
        const c = (await request(port, "GET", "/api/config")).json;
        same(c.modesDir, dir); same(c.quarkTests, null); same(c.modesim, false);
        same((await request(port, "GET", "/api/catalog")).json.source, "builtin");
        const d = (await request(port, "GET", "/api/defaults")).json;
        ok(d.texts.common && ["fixtures", "repo"].includes(d.source), JSON.stringify(d.source));
        const s = (await request(port, "GET", "/api/modesim/status")).json;
        same(s.available, false); ok(/quark_maptool/.test(s.reason), s.reason);
        eq((await request(port, "GET", "/api/modes")).json.user.map((u) => u.name), ["lowgrav"]);
    });
    await t("refuses a foreign Host, and writes without the X-Quark-Editor header", async () => {
        same((await request(port, "GET", "/api/config", { headers: { Host: "evil.example" } })).status, 403);
        same((await request(port, "POST", "/api/state?key=x", { body: {} })).status, 403);
        same((await request(port, "POST", "/api/state?key=x", { body: {}, headers: { ...W, Host: "evil.example:" + port } })).status, 403);
        same((await request(port, "POST", "/api/state?key=x", { body: {}, headers: W })).status, 200);
    });
    let meta;
    await t("save: owned keys patched, logic written, every foreign line kept byte for byte, provenance merged", async () => {
        const g = (await request(port, "GET", "/api/mode?name=lowgrav")).json;
        same(g.text, PHYS);
        const m = loadModeText(g.text, defaultFor);
        m.ast.decls.find((d) => d.name === "target").value = 25;
        const text = fileTextFor(g.text, { timelimit: "450" }, m.ast, m.def, false);
        fs.writeFileSync(path.join(tmp, "provenance.json"), JSON.stringify({ format: 1, tool: "mode-editor", items: { "gamemodes/other": { origin: "human", by: "q" } } }));
        const r = await request(port, "POST", "/api/mode?name=lowgrav", { body: { text, baseSha256: g.sha256, baseMtime: g.mtime, provenance: { human: false } }, headers: W });
        same(r.status, 200, r.text);
        const disk = fs.readFileSync(file, "utf8");
        same(disk, text);
        eq(foreignLines(disk), foreignLines(PHYS));
        ok(disk.includes("timelimit=450\r\n") && disk.includes("param target int = 25 "), "patched + logic");
        const prov = JSON.parse(fs.readFileSync(path.join(tmp, "provenance.json"), "utf8"));
        same(prov.items["gamemodes/lowgrav"].origin, "ai"); same(prov.items["gamemodes/other"].origin, "human", "other items kept");
        ok(fs.existsSync(file + ".bak"), "a .bak of the previous file");
        meta = r.json;
    });
    await t("refuses (409) a save over a file that changed on disk since it was loaded, and writes nothing", async () => {
        const changed = fs.readFileSync(file, "utf8").replace("gravity=0.5", "gravity=0.4");   // (the physics editor saved)
        fs.writeFileSync(file, changed);
        const r = await request(port, "POST", "/api/mode?name=lowgrav", { body: { text: changed.replace("timelimit=450", "timelimit=10"), baseSha256: meta.sha256, baseMtime: meta.mtime }, headers: W });
        same(r.status, 409, r.text); ok(r.json.changed && /changed on disk/.test(r.json.error), r.text);
        same(fs.readFileSync(file, "utf8"), changed, "file untouched");
        fs.unlinkSync(file);
        const r2 = await request(port, "POST", "/api/mode?name=lowgrav", { body: { text: changed, baseSha256: meta.sha256 }, headers: W });
        same(r2.status, 409, "deleted since loaded");
        fs.writeFileSync(file, changed);
    });
    await t("refuses (400) a save that would change a line it doesn't own, or a key line after logic", async () => {
        const g = (await request(port, "GET", "/api/mode?name=lowgrav")).json;
        const r = await request(port, "POST", "/api/mode?name=lowgrav", { body: { text: g.text.replace("move_airaccel=12", "move_airaccel=13"), baseSha256: g.sha256 }, headers: W });
        same(r.status, 400, r.text); ok(/doesn't own/.test(r.json.error), r.text);
        const r2 = await request(port, "POST", "/api/mode?name=lowgrav", { body: { text: g.text + "gravity=3\n", baseSha256: g.sha256 }, headers: W });
        same(r2.status, 400, r2.text);
        const r3 = await request(port, "POST", "/api/mode?name=lowgrav", { body: { text: g.text.replace("somefuturekey=x=y\r\n", ""), baseSha256: g.sha256 }, headers: W });
        same(r3.status, 400, "dropping an unknown key is a change too");
    });
    await t("a new name that exists needs overwrite; built-in names and bad names are refused", async () => {
        const r = await request(port, "POST", "/api/mode?name=lowgrav", { body: { text: newQmodeText("lowgrav", "ffa") }, headers: W });
        same(r.status, 409); ok(r.json.exists, r.text);
        same((await request(port, "POST", "/api/mode?name=koth", { body: { text: newQmodeText("koth", "koth") }, headers: W })).status, 400);
        same((await request(port, "POST", "/api/mode?name=..%2Fx", { body: { text: "mode=ffa\n" }, headers: W })).status, 400);
        same((await request(port, "GET", "/api/mode?name=nope")).status, 404);
        const n = await request(port, "POST", "/api/mode?name=fresh", { body: { text: newQmodeText("fresh", "ffa"), provenance: { human: true } }, headers: W });
        same(n.status, 200, n.text);
        same(JSON.parse(fs.readFileSync(path.join(tmp, "provenance.json"), "utf8")).items["gamemodes/fresh"].origin, "human");
        same(n.json.check.available, false, "no checkmode without quark_tests");
    });
    await t("runs only modesim / modebatch, and only when quark_maptool has them", async () => {
        same((await request(port, "POST", "/api/run", { body: { args: ["modesim"], request: {} }, headers: W })).status, 501);
        same((await request(port, "POST", "/api/helper", { body: { args: ["bots"], request: {} }, headers: W })).status, 501);
    });
} finally {
    srv.kill();
    fs.rmSync(tmp, { recursive: true, force: true });
}

console.log(`\n${pass} passed, ${fail} failed`);
if (fail) { if (srvOut) console.log("serve.py output:\n" + srvOut); process.exit(1); }
