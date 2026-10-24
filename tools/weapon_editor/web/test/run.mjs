// Headless checks for the weapons editor (node 18+, Playwright from the environment; never `playwright install`).
//
//   node tools/weapon_editor/web/test/run.mjs [--shots DIR] [--port N] [--maptool PATH] [--quark-tests PATH] [--mock-only] [--no-browser]
//
// 1. node checks: .qweapons text round-trips (the built-in standard set, the game's default text from quark_tests
//    exportweapons when there is one, a set with labels / curves / slots), the page's checks, Blocks and Graph give
//    back the identical AST, the MOCK's numbers (shots / time to kill / DPS of the standard weapons), "try a hit"
//    (runSlot), the MOCK range (deterministic, kills at every lane for the rifle).
// 2. serve.py on a free port with a temporary data folder: writes need X-Quark-Editor, save / list / read / 409 /
//    force / delete of a set, Play in game writes custom_modes/wpn-<set>.qmode keeping every other line, the game
//    catalog and check through quark_tests (when present).
// 3. browser (Chromium): the page loads with no errors, a Weapon tab edit shows in the Code text, a Code edit shows
//    in Values, Save writes the file, and a screenshot of each tab at 2560x1440 into --shots
//    (default /mnt/project-files/weapons-editor/shots/).
//
// Nothing in the real data folder is touched.

import { spawn, spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath, pathToFileURL } from "node:url";

const require = createRequire(import.meta.url);
const HERE = path.dirname(fileURLToPath(import.meta.url));
const TOOL = path.resolve(HERE, "../..");
const REPO = path.resolve(TOOL, "../..");
const JS = pathToFileURL(path.join(TOOL, "web/js") + "/").href;
const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const SHOTS = opt("--shots", "/mnt/project-files/weapons-editor/shots/");
const MOCK_ONLY = argv.includes("--mock-only");
const NO_BROWSER = argv.includes("--no-browser");
let fails = 0, passes = 0;
const ok = (cond, what, extra = "") => { if (cond) { passes++; console.log("  ok   " + what + (extra ? "  " + extra : "")); } else { fails++; console.log("  FAIL " + what + (extra ? "  " + extra : "")); } };
const near = (a, b, tol = 0.01) => typeof a === "number" && Math.abs(a - b) <= tol;
// fn returns true / undefined (pass), {info} (pass, with a note), or anything else (fail, shown)
const tryOk = (what, fn) => { try { const r = fn(); const pass = r === true || r === undefined || (r && typeof r === "object" && "info" in r); ok(pass, what, pass ? (r && r.info) || "" : typeof r === "string" ? r : JSON.stringify(r)); } catch (e) { ok(false, what, String(e && e.stack || e).split("\n").slice(0, 3).join(" | ")); } };

const findBin = (flag, names) => {
    const given = opt(flag, "");
    if (given) return given;
    for (const c of names) if (fs.existsSync(path.join(REPO, c))) return path.join(REPO, c);
    return "";
};
const maptool = MOCK_ONLY ? "" : findBin("--maptool", ["build/quark_maptool", "build-release/quark_maptool", "build/quark_maptool.exe"]);
const quarkTests = MOCK_ONLY ? "" : findBin("--quark-tests", ["build/quark_tests", "build-release/quark_tests", "build/quark_tests.exe"]);
let wpnHelper = false, gameCat = null;
if (maptool) { const r = spawnSync(maptool, ["wpnparams"], { input: "{}", encoding: "utf8", timeout: 20000 }); try { wpnHelper = r.status === 0 && !!JSON.parse(r.stdout).fields; } catch {} }
if (quarkTests) { const r = spawnSync(quarkTests, ["exportweapons"], { encoding: "utf8", timeout: 60000, maxBuffer: 64 << 20 }); try { gameCat = JSON.parse(r.stdout); } catch {} }
console.log(`helper: ${wpnHelper ? maptool + " (wpn* commands)" : "none with wpn* commands: MOCK"} · game catalog: ${gameCat ? quarkTests : "none (built-in catalog)"}`);

const DEMO_TAIL = `
param bonus f32 = 1.5
curve rocket.falloff(dist f32) = points 0: 1, 20: 0.6
curve ramp(dist f32) = clamp(1 - dist / 200, 0.5f, 1)
slot hit:
  if head and dist > 30: damage = damage * bonus
slot hit sniper:
  if airborne: damage = damage * 2
  else: knock = 0.5f
  heal = f32(time) * 0 + ramp(dist)
slot kill:
  heal = 10
  if kills > 2: refund = yes
`;

async function nodeChecks() {
    const { builtinCatalog, normalizeCatalog, standardText } = await import(JS + "catalog.js");
    const { setCatalog, fieldValues, validate, numberIds, runSlot, symbols, withField, deepEqual, firstDiff, stripIds } = await import(JS + "ast.js");
    const { parse, print } = await import(JS + "dsl.js");
    const { computeStats } = await import(JS + "stats.js");
    const { mockRange, rangeTargets } = await import(JS + "mockrange.js");
    const { brainToBlocks, blocksToBrain } = await import(JS + "blocks.js");
    const { toGraph, fromGraph, wholeGraph, wholeGraphToBrain } = await import(JS + "graph.js");
    const plain = (x) => JSON.parse(JSON.stringify(x));

    console.log("text");
    const cat = builtinCatalog(); setCatalog(cat);
    const STD = standardText(cat);
    tryOk("the standard set prints back to the same text", () => print(parse(STD)) === STD);
    tryOk("the standard set has no problems", () => { const p = validate(parse(STD)).problems; return p.filter((x) => x.severity === "error").length === 0 || JSON.stringify(p.slice(0, 3)); });
    const DEMO = STD.replace(/weapons "standard"/, 'weapons "demo"\n\nlabel rocket "HOT ROCKET"').replace("param rocket.damage f32 = 34", "param rocket.damage f32 = 40") + DEMO_TAIL;
    tryOk("a set with labels, curves and slots round-trips", () => print(parse(DEMO)) === DEMO);
    tryOk("it checks clean", () => { const p = validate(parse(DEMO)).problems; return p.length === 0 || JSON.stringify(p.slice(0, 3)); });
    tryOk("errors carry a line", () => { try { parse(STD + "\nslot hit nosuchweapon:\n  damage = 1\n"); const p = validate(parse(STD + "\nslot hit nosuchweapon:\n  damage = 1\n")).problems; return p.some((x) => x.line > 0) || "no line"; } catch (e) { return e.line > 0 || "no line on " + e.msg; } });
    const probs = (t) => validate(parse(STD + t)).problems.map((p) => p.msg).join(" | ");
    tryOk("as the game: an f64 doesn't narrow to an f32 output", () => /write f32/.test(probs("\nslot hit:\n  knock = 0.5\n")) || probs("\nslot hit:\n  knock = 0.5\n"));
    tryOk("as the game: a weapon's falloff curve is data", () => /falloff curve is data/.test(probs("\ncurve rifle.falloff(dist f32) = 1 - dist / 100\n")));
    tryOk("as the game: a curve's argument doesn't narrow", () => /curve takes an f32/.test(probs("\ncurve c(x f32) = x\nslot hit:\n  damage = c(time)\n")) || probs("\ncurve c(x f32) = x\nslot hit:\n  damage = c(time)\n"));
    tryOk("a field edit rewrites one line", () => { const t = print(withField(parse(STD), "rifle.damage", 6)); const a = STD.split("\n"), b = t.split("\n"); const d = a.filter((l, i) => l !== b[i]); return (d.length === 1 && t.includes("param rifle.damage f32 = 6")) || JSON.stringify(d); });
    if (gameCat) {
        const gc = normalizeCatalog(gameCat);
        setCatalog(gc);
        tryOk("game catalog: " + gameCat.fields.length + " fields, " + gameCat.weapons.length + " weapons", () => gc.fields.length > 100 && gc.weapons.length >= 11);
        tryOk("the built-in catalog (catalog_data.js) is the game's: every field, range, default; weapons; default text", () => {
            const bad = [];
            const gf = new Map(gameCat.fields.map((f) => [f.key, f]));
            if (cat.fields.length !== gameCat.fields.length) bad.push(`fields: page ${cat.fields.length}, game ${gameCat.fields.length}`);
            for (const f of cat.fields) { const g = gf.get(f.key); if (!g) { bad.push("no field " + f.key + " in the game"); continue; } for (const k of ["type", "min", "max", "default"]) if (g[k] !== f[k]) bad.push(`${f.key}.${k}: page ${f[k]}, game ${g[k]}`); }
            if (cat.defaultText !== gameCat.defaultText) bad.push("defaultText differs");
            if (cat.weapons.map((w) => w.id + ":" + w.kind).join(" ") !== gameCat.weapons.map((w) => w.name + ":" + w.kind).join(" ")) bad.push("weapons differ");
            return bad.length === 0 || bad.slice(0, 6).join("; ") + " (run python3 tools/weapon_editor/build_catalog.py)";
        });
        tryOk("the built-in catalog's slots match the game's", () => {
            const bad = [];
            for (const gs of gameCat.slots) { const bs = cat.slots.find((x) => x.name === gs.name); if (!bs) { bad.push("no slot " + gs.name); continue; }
                const a = gs.sensors.map((x) => x.name + ":" + x.type).sort().join(" "), b = bs.sensors.map((x) => x.name + ":" + x.type).sort().join(" "); if (a !== b) bad.push(gs.name + " sensors: game " + a + " / page " + b);
                const c = gs.outputs.map((x) => x.name + ":" + x.type).sort().join(" "), d = bs.outputs.map((x) => x.name + ":" + x.type).sort().join(" "); if (c !== d) bad.push(gs.name + " outputs: game " + c + " / page " + d); }
            return bad.length === 0 || bad.join("; ");
        });
        tryOk("the game's default text round-trips", () => print(parse(gameCat.defaultText)) === gameCat.defaultText);
        tryOk("the game's default text checks clean in the page", () => { const p = validate(parse(gameCat.defaultText)).problems.filter((x) => x.severity === "error"); return p.length === 0 || JSON.stringify(p.slice(0, 3)); });
        tryOk("the page's standard values equal the game's defaults", () => {
            const v = fieldValues(parse(gameCat.defaultText)), bad = [];
            for (const f of gameCat.fields) if (!f.readOnly && typeof f.default === "number" && v[f.key] !== undefined && Math.abs(v[f.key] - f.default) > 1e-4) bad.push(f.key);
            return bad.length === 0 || bad.slice(0, 6).join(" ");
        });
        setCatalog(cat);
    }

    console.log("blocks / graph");
    const ast = numberIds(plain(parse(DEMO)));
    tryOk("blocks export back to the same AST", () => { const b = blocksToBrain(brainToBlocks(ast)); return deepEqual(stripIds(b), stripIds(ast)) || JSON.stringify(firstDiff(stripIds(b), stripIds(ast))); });
    tryOk("blocks print back to the same text", () => print(blocksToBrain(brainToBlocks(ast))) === DEMO);
    tryOk("every slot / expression curve graph exports back", () => {
        let n = 0;
        for (const d of ast.decls) {
            if (d.kind !== "slot" && !(d.kind === "curve" && d.form === "expr")) continue;
            const back = fromGraph(toGraph(d));
            if (!deepEqual(stripIds(back), stripIds(d))) return "differs: " + JSON.stringify(firstDiff(stripIds(back), stripIds(d)));
            n++;
        }
        return n === 4 || "graphs: " + n;
    });
    tryOk("the whole-set graph exports back (shared inputs off and on)", () => {
        for (const shared of [false, true]) { const back = wholeGraphToBrain(ast, wholeGraph(ast, { shared })); if (print(back) !== DEMO) return "shared " + shared; }
    });

    console.log("numbers (MOCK, standard set, 100 hp)");
    const sset = parse(STD), vals = fieldValues(sset);
    const rows = computeStats(vals, { set: sset, health: 100 }).rows;
    const row = (w, m = "primary") => rows.find((r) => r.weapon === w && r.mode === m);
    const want = [["rifle", 20, 14, 2.88, 1.97], ["sniper", 3, 2, 3, 1.5], ["shotgun", 3, 2, 1.5, 0.75], ["crossbow", 2, 2, 2, 2]];
    for (const [w, sb, sh, tb, th] of want) tryOk(`${w}: ${sb}/${sh} shots, ${tb}/${th} s`, () => { const r = row(w); return (r.stkBody === sb && r.stkHead === sh && near(r.ttkBody, tb) && near(r.ttkHead, th)) || `${r.stkBody}/${r.stkHead} ${r.ttkBody}/${r.ttkHead}`; });
    tryOk("rocket direct hit: 2 shots, 1.2 s", () => { const r = rows.find((r) => r.weapon === "rocket" && /direct/.test(r.label) && !/lob/.test(r.label)); return (r && r.stkBody === 2 && near(r.ttkBody, 1.2)) || JSON.stringify(r && [r.stkBody, r.ttkBody]); });
    tryOk("laser: 40 hp/s, 2.5 s", () => { const r = row("laser"); return (near(r.dpsBody, 40, 0.05) && near(r.ttkBody, 2.5)) || `${r.dpsBody} ${r.ttkBody}`; });
    const dset = parse(DEMO), dvals = fieldValues(dset);
    tryOk("a curve replaces the falloff fields (rocket x0.6 from 20 m)", () => { const r = computeStats(dvals, { set: dset, health: 100 }).rows.find((r) => r.weapon === "rocket"); return near(r.per(40, false) / r.per(0, false), 0.6, 0.02) || String(r.per(40, false) / r.per(0, false)); });

    console.log("try a hit");
    const S = symbols(dset);
    tryOk("slot hit: head at 40 m x1.5", () => near(runSlot(dset, "hit", "rifle", { weapon: "rifle", damage: 10, head: true, dist: 40 }, S, dvals).outputs.damage, 15));
    tryOk("slot hit: body at 40 m unchanged", () => near(runSlot(dset, "hit", "rifle", { weapon: "rifle", damage: 10, head: false, dist: 40 }, S, dvals).outputs.damage, 10));
    tryOk("slot hit sniper runs instead of the general one", () => { const r = runSlot(dset, "hit", "sniper", { weapon: "sniper", damage: 10, head: true, dist: 40, airborne: true }, S, dvals); return (near(r.outputs.damage, 20) && r.trace.length > 0) || JSON.stringify(r.outputs); });
    tryOk("slot kill: heal 10, refund on a streak", () => { const r = runSlot(dset, "kill", "rifle", { weapon: "rifle", kills: 3 }, S, dvals).outputs; return (r.heal === 10 && r.refund === true) || JSON.stringify(r); });

    console.log("range (MOCK)");
    const req = { weapon: "rifle", targets: rangeTargets("lanes"), shooter: { pos: [0, 0, 0], aim: "perfect" }, seconds: 20, seed: 1 };
    let a, b;
    tryOk("rifle on the lanes kills every target", () => { a = mockRange(req, vals, sset); const k = a.targets.map((t) => t.kills).join(" "); return a.targets.every((t) => t.kills >= 1) ? { info: "kills " + k } : "kills " + k; });
    tryOk("the same seed gives the same answer", () => { b = mockRange(req, vals, sset); return JSON.stringify(a.totals) === JSON.stringify(b.totals); });
    tryOk("frames every 20 ms with the shooter in them", () => a.frames.length >= 900 && a.frames[0].players.some((p) => p.shooter));
    for (const w of ["rocket", "laser", "shotgun", "sticky", "bubble", "crossbow", "knife", "sniper"]) tryOk(`${w} fires`, () => { const r = mockRange({ ...req, weapon: w, seconds: 6 }, vals, sset); return r.totals.shots > 0 && (w === "knife" || r.totals.hits > 0) ? { info: `${r.totals.hits} hits / ${r.totals.shots} shots, ${r.totals.kills} kills` } : JSON.stringify(r.totals); });
    return { STD, DEMO };
}

// ---------------------------------------------------------------- serve.py
const tmp = fs.mkdtempSync(path.join(process.env.TMPDIR || os.tmpdir(), "qwe-test-"));
const dataDir = path.join(tmp, "data");
fs.mkdirSync(path.join(dataDir, "custom_modes"), { recursive: true });
fs.writeFileSync(path.join(dataDir, "custom_modes", "wpn-keep.qmode"), "custommode=wpn-keep\r\nmode=duel\r\nhealth=150\r\nweaponset=old\r\n# a comment\r\nlogic\r\nwhen kill: score +1\r\n");
const port = +opt("--port", 0) || 8800 + Math.floor(Math.random() * 150);
const py = process.platform === "win32" ? "py" : "python3";
const srvArgs = [...(process.platform === "win32" ? ["-3"] : []), path.join(TOOL, "serve.py"), "--port", String(port), "--no-open", "--data-dir", dataDir,
    "--maptool", maptool || path.join(tmp, "no_maptool"), "--quark-tests", quarkTests || path.join(tmp, "no_quark_tests")];
const srv = spawn(py, srvArgs, { env: { ...process.env, QUARK_SHELL: "1" }, stdio: ["ignore", "pipe", "pipe"] });
let srvLog = "";
srv.stdout.on("data", (d) => (srvLog += d)); srv.stderr.on("data", (d) => (srvLog += d));
const BASE = `http://127.0.0.1:${port}`;
const H = { "Content-Type": "application/json", "X-Quark-Editor": "1" };
const post = async (p, body, hdr = H) => { const r = await fetch(BASE + p, { method: "POST", headers: hdr, body: JSON.stringify(body) }); const t = await r.text(); let j = null; try { j = JSON.parse(t); } catch {} return { status: r.status, json: j, text: t }; };
async function waitUp() {
    for (let i = 0; i < 100; i++) { try { const r = await fetch(BASE + "/api/ping"); if (r.ok) return true; } catch {} await new Promise((r) => setTimeout(r, 100)); }
    return false;
}

async function serverChecks({ STD, DEMO }) {
    console.log("serve.py");
    ok(await waitUp(), "started", BASE);
    const page = await fetch(BASE + "/"); const html = await page.text();
    ok(page.ok && html.includes("weapons editor"), "GET / serves the page");
    ok((await fetch(BASE + "/web/js/app.js")).ok, "GET /web/js/app.js");
    ok((await fetch(BASE + "/web/data/games.json")).ok, "GET /web/data/games.json");
    const ping = await (await fetch(BASE + "/api/ping")).json();
    ok(ping.tool === "weapons-editor", "ping names the tool");
    ok((await post("/api/set?name=x", { text: STD }, { "Content-Type": "application/json" })).status === 403, "a write without X-Quark-Editor is refused");
    ok((await post("/api/set?name=../x", { text: STD })).status === 400, "a bad set name is refused");
    const s1 = await post("/api/set?name=demo", { text: DEMO });
    ok(s1.status === 200 && s1.json.created === true, "save a new set", s1.json && s1.json.path);
    ok(fs.readFileSync(path.join(dataDir, "custom_weapons", "demo.qweapons"), "utf8") === DEMO, "the file holds the text byte for byte");
    const list = await (await fetch(BASE + "/api/sets")).json();
    ok(list.sets.some((s) => s.name === "demo"), "listed", JSON.stringify(list.sets.map((s) => s.name)));
    ok((await (await fetch(BASE + "/api/set?name=demo")).text()) === DEMO, "read back");
    const c = await post("/api/set?name=demo", { text: DEMO + "\n", expect: "something else" });
    ok(c.status === 409 && c.json.text === DEMO, "a changed-on-disk save is refused with the disk text (409)");
    ok((await post("/api/set?name=demo", { text: DEMO + "\n", expect: "something else", force: true })).status === 200, "force overwrites");
    ok(fs.existsSync(path.join(dataDir, "custom_weapons", "demo.qweapons.bak")), "the old file is kept as .bak");
    const pm = await post("/api/playmode", { set: "demo", base: "koth" });
    ok(pm.status === 200 && pm.json.text === "custommode=wpn-demo\nmode=koth\nweaponset=demo\n", "Play in game writes a new wpn-demo.qmode", JSON.stringify(pm.json && pm.json.text));
    const pk = await post("/api/playmode", { set: "keep", base: "ffa" });
    ok(pk.status === 200 && pk.json.text === "custommode=wpn-keep\r\nmode=duel\r\nhealth=150\r\nweaponset=keep\r\n# a comment\r\nlogic\r\nwhen kill: score +1\r\n", "an existing mode keeps every other line (CRLF, logic)", JSON.stringify(pk.json && pk.json.text));
    const mm = await post("/api/merge-mode", { text: "custommode=a\nmode=ffa\ngravity=0.5\n", set: "s", name: "a" });
    ok(mm.json.text === "custommode=a\nmode=ffa\nweaponset=s\ngravity=0.5\n", "weaponset= goes after mode=", JSON.stringify(mm.json.text));
    ok((await post("/api/playmode", { set: "demo", base: "nosuchmode" })).status === 400, "a bad base mode is refused");
    const st = await post("/api/state?key=ui", { a: 1 });
    ok(st.status === 200 && (await (await fetch(BASE + "/api/state?key=ui")).json()).a === 1, "state round-trips");
    const del = await post("/api/set/delete?name=demo", {});
    ok(del.status === 200 && !fs.existsSync(path.join(dataDir, "custom_weapons", "demo.qweapons")), "delete (kept as .bak)");
    const cat = await (await fetch(BASE + "/api/catalog")).json();
    if (quarkTests && gameCat) ok(cat.catalog && cat.catalog.format === "qweapons-catalog", "/api/catalog: the game's catalog via quark_tests");
    else ok(cat.catalog === null || cat.catalog === undefined, "/api/catalog without quark_tests says why", cat.why);
    const ck = await post("/api/check", { text: DEMO.replace("param rocket.damage f32 = 40", "param rocket.damage f32 = nope") });
    if (quarkTests && cat.catalog) {
        const ans = ck.json.answer;
        ok(ans && ans.ok === false && ans.errors.length > 0 && ans.errors[0].line > 0, "/api/check finds an error with its line", JSON.stringify(ans && ans.errors[0]));
        const good = (await post("/api/check", { text: DEMO })).json.answer;
        ok(good && good.ok === true, "/api/check passes the demo set", JSON.stringify(good && (good.errors || []).slice(0, 2)));
    } else ok(ck.status === 200 && !ck.json.answer, "/api/check without quark_tests answers null");
    const hp = await post("/api/helper", { args: ["simcheck"], request: {} });
    ok(maptool ? hp.status === 200 && typeof hp.json.code === "number" : hp.status === 501, "/api/helper", maptool ? "code " + (hp.json && hp.json.code) : "501 without quark_maptool");
    ok((await post("/api/run", { args: ["rm", "-rf"] })).status === 400, "/api/run only runs the streaming commands");
}

async function browserChecks() {
    let chromium;
    try { ({ chromium } = require("playwright")); } catch { try { ({ chromium } = require(path.join(execRoot(), "playwright"))); } catch { console.log("browser: playwright not found, skipped"); return; } }
    console.log("browser");
    const b = await chromium.launch();
    try {
        for (const mock of [true, false]) {
            const p = await b.newPage({ viewport: { width: 2560, height: 1440 } });
            const errs = [];
            p.on("pageerror", (e) => errs.push("pageerror: " + e.message));
            p.on("dialog", (d) => { errs.push("dialog: " + d.message()); d.accept(); });
            p.on("console", (m) => { if (m.type() === "error") errs.push("console: " + m.text()); });
            await p.goto(BASE + "/" + (mock ? "?mock" : ""));
            await p.waitForFunction(() => window.weaponsApp && window.weaponsApp.stats, null, { timeout: 15000 });
            const badge = await p.textContent("#helperBadge").catch(() => "");
            ok(true, `page loads (${mock ? "?mock" : "default"})`, "badge " + (badge || "").trim());
            if (mock) {
                await p.evaluate(() => window.weaponsApp.setField("rifle.damage", 7, "test"));
                await p.click('#tabs button[data-page="code"]'); await p.waitForTimeout(300);
                ok((await p.inputValue("#codeTa")).includes("param rifle.damage f32 = 7"), "a Weapon edit shows in the Code text");
                await p.evaluate(() => { const ta = document.getElementById("codeTa"); ta.value = ta.value.trimEnd() + "\nparam rocket.refire f32 = 0.6\n"; ta.dispatchEvent(new Event("input")); });
                await p.click('#tabs button[data-page="values"]'); await p.waitForTimeout(300);
                const r = await p.evaluate(() => window.weaponsApp.stats.rows.find((r) => r.weapon === "rocket").refire);
                ok(near(r, 0.6), "a Code edit shows in Values", String(r));
                await p.evaluate(() => window.weaponsApp.save("browsertest"));
                await p.waitForTimeout(500);
                const f = path.join(dataDir, "custom_weapons", "browsertest.qweapons");
                ok(fs.existsSync(f) && fs.readFileSync(f, "utf8").includes("param rocket.refire f32 = 0.6"), "Save writes custom_weapons/<name>.qweapons");
            } else {
                fs.mkdirSync(path.join(dataDir, "custom_weapons"), { recursive: true });
                const demoText = fs.readFileSync(path.join(dataDir, "custom_weapons", "demo.qweapons.bak"), "utf8");
                await post("/api/set?name=demo", { text: demoText, force: true });
                await p.evaluate(async () => { await window.weaponsApp.refreshSets(); await window.weaponsApp.openSet("demo"); window.weaponsApp.selectWeapon("rocket"); });
                await p.waitForTimeout(800);
                fs.mkdirSync(SHOTS, { recursive: true });
                for (const t of ["weapon", "values", "games", "code", "blocks", "graph", "range", "fights"]) {
                    await p.click(`#tabs button[data-page="${t}"]`);
                    await p.waitForTimeout(t === "range" ? 3000 : 1200);
                    await p.screenshot({ path: path.join(SHOTS, t + ".png") });
                }
                ok(true, "screenshots", SHOTS);
            }
            ok(errs.length === 0, `no page errors (${mock ? "?mock" : "default"})`, errs.slice(0, 3).join(" | "));
            await p.close();
        }
    } finally { await b.close(); }
}
function execRoot() { return spawnSync("npm", ["root", "-g"], { encoding: "utf8" }).stdout.trim(); }

let code = 1;
try {
    const t = await nodeChecks();
    await serverChecks(t);
    if (!NO_BROWSER) await browserChecks();
    code = fails ? 1 : 0;
} catch (e) {
    console.log("  FAIL crashed: " + (e && e.stack || e));
    if (srvLog) console.log(srvLog.slice(-2000));
} finally {
    srv.kill();
    fs.rmSync(tmp, { recursive: true, force: true });
}
console.log(`${passes} passed, ${fails} failed`);
process.exit(code);
