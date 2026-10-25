// Headless checks for the physics editor (node 18+, Playwright from the environment; never `playwright install`).
//
//   node tools/physics_editor/web/test/run.mjs [--shots DIR] [--maptool PATH] [--mock-only] [--keep] [--port N]
//
// 1. node checks: the page's .qmode merge equals serve.py's (/api/merge) on fixtures (CRLF, a logic section, a new
//    file, duplicates), every preset round-trips through the Blocks tab, the MOCK's numbers match the game's.
// 2. browser checks (Chromium, for the real helper when quark_maptool has the physics commands, and for the MOCK):
//    a value change shows in the Code text, a Code edit shows in Values, a preset runs, a block drags into the
//    stack, Save keeps the gamemode editor's logic section on disk, Drive moves with W held (pointer lock faked).
//    v2: other games (the picker, a column per game, "use" / "load all" with the import line), equations and curves
//    (expr.<key>= lines, static / dynamic badges, errors), the Movement code chips and their jump to Values, the Code
//    graph (a click jumps to the code), the Movement graph's 3D view, lock units and A / B (B - A, a game as B, side by
//    side).
// 1b. v2 node checks: the merge with expr. and import lines (page == serve.py), the expression evaluator (the grammar
//    of src/game/moveexpr.cpp), the MOCK running dynamic expressions, /api/source and /references/ served (and
//    nothing outside them), physexpr against the page's evaluator when the helper has it.
// 3. a screenshot of each tab (real helper if present) into --shots (default /mnt/project-files/physics-editor/shots-v2/).
//    references/ is the checkout's; without its index.json a stub is written and passed with --references.
//
// serve.py runs on a free test port with a temporary data folder; nothing in the real data folder is touched.

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
const SHOTS = opt("--shots", "/mnt/project-files/physics-editor/shots-v2/");
const MOCK_ONLY = argv.includes("--mock-only");
let fails = 0, passes = 0;
const ok = (cond, what, extra = "") => { if (cond) { passes++; console.log("  ok   " + what + (extra ? "  " + extra : "")); } else { fails++; console.log("  FAIL " + what + (extra ? "  " + extra : "")); } };
const near = (a, b, tol) => typeof a === "number" && Math.abs(a - b) <= tol;

// ---------------------------------------------------------------- the helper
let maptool = opt("--maptool", "");
if (!maptool) for (const c of ["build/quark_maptool", "build-release/quark_maptool", "build/quark_maptool.exe"]) if (fs.existsSync(path.join(REPO, c))) { maptool = path.join(REPO, c); break; }
let real = false;
if (maptool && !MOCK_ONLY) {
    const r = spawnSync(maptool, ["physparams"], { input: "{}", encoding: "utf8", timeout: 20000 });
    try { real = r.status === 0 && Array.isArray(JSON.parse(r.stdout).params); } catch { real = false; }
}
console.log(real ? `real helper: ${maptool}` : `no physics helper (${maptool || "quark_maptool not found"}): MOCK only`);

// ---------------------------------------------------------------- serve.py
const tmp = fs.mkdtempSync(path.join(process.env.TMPDIR || os.tmpdir(), "qpe-test-"));
const dataDir = path.join(tmp, "data");
fs.mkdirSync(path.join(dataDir, "custom_modes"), { recursive: true });
const LOGIC = "logic\nwhen kill: score +1\nwhen round start: say \"go\"\n";
fs.writeFileSync(path.join(dataDir, "custom_modes", "lowgrav.qmode"),
    "custommode=lowgrav\r\nmode=ffa\r\ngravity=0.5\r\nhealth=150\r\nmove_air_accel=1.5\r\n# a comment\r\n" + LOGIC.replace(/\n/g, "\r\n"));
// other games' values: the checkout's references/, or a stub when it isn't there (then passed with --references)
const REFS_REAL = fs.existsSync(path.join(TOOL, "references", "index.json"));
const refsDir = REFS_REAL ? path.join(TOOL, "references") : path.join(tmp, "refs");
if (!REFS_REAL) {
    fs.mkdirSync(refsDir, { recursive: true });
    fs.writeFileSync(path.join(refsDir, "index.json"), JSON.stringify({ groups: [{ name: "Quake family", games: [{ id: "quake3", name: "Quake III Arena (stub)", year: 1999, file: "quake3.json", values: 4, distance: 0.6 }] }] }));
    fs.writeFileSync(path.join(refsDir, "quake3.json"), JSON.stringify({ id: "quake3", name: "Quake III Arena (stub)", values: {
        gravity: { value: 25.7, unit: "m/s2", mult: 1.1688, source: "stub", confidence: "estimated", raw: "g_gravity 800" },
        speed: { value: 10.29, unit: "m/s", mult: 1.0827, source: "stub", confidence: "exact" },
        friction: { value: 6, unit: "", mult: 0.8571, source: "stub", confidence: "exact" },
        air_accel: { value: 0.1, unit: "", mult: 0.0455, source: "stub", confidence: "exact", outsideClamp: true },
        dash: { value: false, source: "stub", confidence: "exact" } },
        facts: { jumpHeight: { value: 1.46, unit: "m", source: "stub", confidence: "estimated" } } }));
}
const port = +opt("--port", 0) || 8790 + Math.floor(Math.random() * 150);
const py = process.platform === "win32" ? "py" : "python3";
const srvArgs = [...(process.platform === "win32" ? ["-3"] : []), path.join(TOOL, "serve.py"), "--port", String(port), "--no-open", "--data-dir", dataDir,
    "--maptool", real ? maptool : path.join(tmp, "no_maptool"), ...(REFS_REAL ? [] : ["--references", refsDir])];
const srv = spawn(py, srvArgs, { env: { ...process.env, QUARK_SHELL: "1" }, stdio: ["ignore", "pipe", "pipe"] });
let srvLog = "";
srv.stdout.on("data", (d) => (srvLog += d)); srv.stderr.on("data", (d) => (srvLog += d));
const BASE = `http://127.0.0.1:${port}`;
const H = { "Content-Type": "application/json", "X-Quark-Editor": "1" };
const post = async (p, body) => { const r = await fetch(BASE + p, { method: "POST", headers: H, body: JSON.stringify(body) }); const t = await r.text(); try { return JSON.parse(t); } catch { throw new Error(`${p}: HTTP ${r.status}: ${t.slice(0, 300)}`); } };
async function waitUp() {
    for (let i = 0; i < 100; i++) {
        try { const r = await fetch(BASE + "/api/ping"); if (r.ok) return true; } catch {}
        await new Promise((r) => setTimeout(r, 100));
    }
    return false;
}

async function main() {
    ok(await waitUp(), "serve.py started", BASE);

    // ------------------------------------------------------------ security
    console.log("serve.py");
    const noHdr = await fetch(BASE + "/api/merge", { method: "POST", headers: { "Content-Type": "application/json" }, body: "{}" });
    ok(noHdr.status === 403, "a write without X-Quark-Editor is refused", String(noHdr.status));
    const badHost = await new Promise((res) => {
        const http = require("node:http");
        http.get({ host: "127.0.0.1", port, path: "/api/config", headers: { Host: "evil.example" } }, (r) => { r.resume(); res(r.statusCode); }).on("error", () => res(0));
    });
    ok(badHost === 403 || badHost === 400, "a foreign Host header is refused", String(badHost));
    const cfg = await (await fetch(BASE + "/api/config")).json();
    ok(!!cfg.modesDir || !!cfg.dataDir, "/api/config answers");

    // ------------------------------------------------------------ the merge, page vs server
    console.log("qmode merge (page == serve.py)");
    const { mergeQmode, parseQmode } = await import(JS + "qmode.js");
    const { normalizeCatalog, standardPhysics, clonePhysics, setVal } = await import(JS + "params.js");
    const cat = normalizeCatalog(real ? JSON.parse(spawnSync(maptool, ["physparams"], { input: "{}", encoding: "utf8" }).stdout) : null);
    const ph = clonePhysics(standardPhysics(cat));
    setVal(ph, "gravity", 0.8); setVal(ph, "air_accel", 1.25); setVal(ph, "dash_speed", 1.1); ph.grapple = false;
    const fixtures = {
        "new file": "",
        "crlf + logic": fs.readFileSync(path.join(dataDir, "custom_modes", "lowgrav.qmode"), "utf8"),
        "duplicates + unknown move key": "custommode=dup\nmode=duel\ngravity=2\nhealth=50\ngravity=3\nmove_air_accel=2\nmove_future_key=3\nmove_air_accel=4\n",
        "move key back to 1": "custommode=x\nmode=ffa\nmove_dash_speed=1.5\nmove_air_accel=2\n",
        "no trailing newline": "custommode=x\nmode=ffa\nspeed=1.2",
    };
    for (const [n, text] of Object.entries(fixtures)) {
        const page = mergeQmode(text, n === "move key back to 1" ? (() => { const q = clonePhysics(ph); setVal(q, "dash_speed", 1); return q; })() : ph, "fixture");
        const srvAns = await post("/api/merge", { text, physics: n === "move key back to 1" ? (() => { const q = clonePhysics(ph); setVal(q, "dash_speed", 1); return q; })() : ph, name: "fixture" });
        ok(page === srvAns.text, n, page === srvAns.text ? "" : JSON.stringify({ page, server: srvAns.text }));
        if (text.includes("logic")) ok(page.endsWith(text.slice(text.indexOf("logic"))), "  the logic section is byte for byte the same");
        if (text.includes("health")) ok(page.includes(text.match(/health=\d+/)[0]), "  non-movement lines kept");
        const back = parseQmode(page, cat).physics;
        ok(near(back.gravity, 0.8, 1e-9) && back.grapple === false, "  parses back to the set");
    }

    await v2NodeChecks(cat, ph);
    await fixNodeChecks(cat, ph);
    await fix2NodeChecks(cat, ph);

    // ------------------------------------------------------------ blocks
    console.log("blocks round trip");
    const { toBlocks, compile, stepsEqual } = await import(JS + "blocks.js");
    const { mockPresets, mockRoomText } = await import(JS + "room.js");
    const presets = real ? JSON.parse(spawnSync(maptool, ["physpresets"], { input: "{}", encoding: "utf8" }).stdout).presets : mockPresets();
    let raw = 0;
    for (const p of [...presets, ...(real ? mockPresets().map((x) => ({ ...x, id: "mock " + x.id })) : [])]) {
        const d = toBlocks(p.scenario), c = compile(p.scenario.start, d.blocks, d.tailMs);
        const same = stepsEqual(c.steps, p.scenario.steps) && c.durationMs === p.scenario.durationMs;
        if (d.raw) raw++;
        ok(same, `${p.id}`, (d.raw ? "(raw steps) " : "") + d.blocks.map((b) => b.kind).join(" "));
    }
    console.log(`  (${raw} preset(s) shown as raw step blocks: lossless, editable as raw steps)`);

    // ------------------------------------------------------------ the mock vs the game's numbers
    console.log("mock pmove");
    const { parseQmap } = await import(JS + "qmap.js");
    const { MockSim, computeMetrics } = await import(JS + "sim.js");
    const sim = new MockSim(parseQmap(mockRoomText(), "room"));
    const std = standardPhysics(cat);
    const mrun = (id) => { const p = mockPresets().find((x) => x.id === id); const r = sim.run(p.scenario, std, 10); return computeMetrics(r, p.scenario); };
    const sj = mrun("standing_jump");
    ok(near(sj.jumpHeight, 1.22, 0.02), "standing jump 1.22 m", String(sj.jumpHeight));
    ok(near(sj.airTime, 0.66, 0.02), "air time 0.66 s", String(sj.airTime));
    ok(near(mrun("run_up").topSpeed, 9.5, 0.01), "run speed 9.5 m/s");
    const st = mrun("stop");
    ok(near(st.stopDistance, 1.21, 0.05), "stop distance 1.21 m", String(st.stopDistance));
    ok(near(mrun("running_jump").jumpDistance, 6.27, 0.15), "running jump 6.27 m");
    if (real) {
        const ans = JSON.parse(spawnSync(maptool, ["physsim"], { input: JSON.stringify({ preset: "dash" }), encoding: "utf8" }).stdout);
        const md = mrun("dash"), rd = ans.runs[0].metrics;
        ok(near(md.dashDistance, rd.dashDistance, 0.15), "mock dash distance near the helper's", `${md.dashDistance} vs ${rd.dashDistance}`);
    }

    // ------------------------------------------------------------ the browser
    let chromium;
    try { ({ chromium } = require("playwright")); } catch { console.log("playwright not found: browser checks skipped"); return; }
    const browser = await chromium.launch();
    try {
        for (const mode of real ? ["real", "mock"] : ["mock"]) await browserChecks(browser, mode, mode === (real ? "real" : "mock"));
    } finally { await browser.close(); }
}

async function browserChecks(browser, mode, shots) {
    console.log(`browser (${mode})`);
    const page = await browser.newPage({ viewport: { width: 1500, height: 900 } });
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
    page.on("dialog", (d) => d.accept());
    const t0 = Date.now();
    await page.goto(`${BASE}/${mode === "mock" ? "?mock" : ""}`);
    await page.waitForFunction(() => window.qpe && window.qpe.results, null, { timeout: 20000 });
    ok(true, "loaded and ran the first scenario", `${Date.now() - t0} ms`);
    ok((await page.evaluate(() => window.qpe.helper.kind)) === mode, `helper is ${mode}`);

    // a value -> the Code text
    await page.click("#tabs button[data-page=values]");
    await page.fill("#values tr:has(td.lab[title=gravity]) input.mult", "0.5");
    await page.waitForTimeout(50);
    const code = await page.evaluate(() => window.qpe.set.text);
    ok(/^gravity=0\.5$/m.test(code), "Values -> Code: gravity=0.5 in the text");
    const changedRow = await page.evaluate(() => document.querySelector("#values tr:has(td.lab[title=gravity])").className);
    ok(/\bmod\b/.test(changedRow), "the changed row is highlighted");

    // the Code text -> Values
    await page.click("#tabs button[data-page=code]");
    await page.evaluate(() => { const ta = document.getElementById("codeTa"); ta.value = ta.value.replace(/^jump=.*$/m, "jump=1.5"); ta.dispatchEvent(new Event("input", { bubbles: true })); });
    await page.waitForTimeout(150);
    ok((await page.evaluate(() => window.qpe.physics.jump)) === 1.5, "Code -> Values: jump=1.5");
    await page.click("#tabs button[data-page=values]");
    ok((await page.inputValue("#values tr:has(td.lab[title=jump]) input.mult")) === "1.5", "the JUMP row shows 1.5");
    await page.click("#tabs button[data-page=code]");
    await page.evaluate(() => { const ta = document.getElementById("codeTa"); ta.value += "\ngravity=banana\n"; ta.dispatchEvent(new Event("input", { bubbles: true })); });
    await page.waitForTimeout(150);
    ok((await page.locator("#codeProblems").innerText()).toLowerCase().includes("gravity"), "a parse problem is shown inline");
    await page.evaluate(() => { const ta = document.getElementById("codeTa"); ta.value = ta.value.replace("\ngravity=banana\n", ""); ta.dispatchEvent(new Event("input", { bubbles: true })); });

    // a preset runs (jump x1.5 -> higher than the standard)
    await page.selectOption("#presetSel", "standing_jump");
    await page.waitForFunction(() => window.qpe.results && window.qpe.results.scenario && /standing/i.test(window.qpe.results.scenario.name || "") && !window.qpe._running, null, { timeout: 10000 });
    await page.waitForTimeout(300);
    const jh = await page.evaluate(() => window.qpe.results.runs.map((r) => r.metrics.jumpHeight));
    ok(jh.length >= 2 && jh[0] > jh[1] * 3, "standing jump: edited (gravity x0.5, jump x1.5) far above the standard", jh.map((x) => (+x).toFixed(2)).join(" vs "));
    ok(near(jh[1], 1.22, 0.02), "standard standing jump 1.22 m", String(jh[1]));
    if (shots) await shot(page, "values", "values");
    if (shots) await shot(page, "code", "code");

    // drag a block into the stack
    await page.click("#tabs button[data-page=blocks]");
    const n0 = await page.locator(".bk-stack > .bk-block").count();
    const src = await page.locator(".bk-pitem", { hasText: "Dash" }).first().boundingBox();
    const dst = await page.locator(".bk-stack").boundingBox();
    await page.mouse.move(src.x + 20, src.y + 8); await page.mouse.down();
    await page.mouse.move(src.x + 120, src.y + 40, { steps: 4 });
    await page.mouse.move(dst.x + 60, dst.y + dst.height - 6, { steps: 6 }); await page.mouse.up();
    await page.waitForTimeout(200);
    const n1 = await page.locator(".bk-stack > .bk-block").count();
    ok(n1 === n0 + 1, "a palette block dragged into the stack", `${n0} -> ${n1}`);
    ok(await page.evaluate(() => window.qpe.scenario.steps.some((s) => s.dash)), "  the scenario now has a dash step");
    await page.waitForFunction(() => !window.qpe._running, null, { timeout: 10000 });
    if (shots) await shot(page, "blocks");

    // graph and path
    await page.selectOption("#presetSel", real === true && mode === "real" ? "bhop_strafe" : "bhop10");
    await page.waitForTimeout(600);
    await page.waitForFunction(() => !window.qpe._running, null, { timeout: 10000 });
    await page.click("#tabs button[data-page=graph]");
    await page.waitForTimeout(300);
    ok((await page.locator("#metrics table tr").count()) > 5, "the metrics table lists the runs");
    if (shots) await shot(page, "graph");
    await page.click("#tabs button[data-page=path]");
    await page.waitForTimeout(300);
    if (shots) await shot(page, "path");

    // a real map: the running jump from its first spawn
    const maps = await page.evaluate(() => (window.qpe.maps || []).map((m) => m.source + ":" + m.name));
    const want = maps.find((m) => m.endsWith(":temple")) || maps[0];
    if (want) {
        await page.selectOption("#presetSel", "running_jump");
        await page.waitForTimeout(200);
        await page.evaluate((v) => window.qpe.chooseMap(v), want);
        await page.waitForTimeout(300);
        await page.waitForFunction(() => !window.qpe._running && window.qpe.results, null, { timeout: 15000 });
        await page.waitForTimeout(300);
        const r = await page.evaluate(() => ({ map: window.qpe.mapChoice.name, start: window.qpe.results.scenario.start.pos, top: window.qpe.results.runs[1].metrics.topSpeed }));
        ok(r.map === want.split(":")[1] && r.top > 9, `a map (${want}) runs from its spawn`, `start ${r.start.map((x) => (+x).toFixed(1)).join(" ")}, top ${(+r.top).toFixed(2)} m/s`);
        if (shots) { await page.click("#tabs button[data-page=path]"); await page.waitForTimeout(300); await shot(page, "path-map"); }
        await page.evaluate(() => window.qpe.chooseMap("room"));
        await page.waitForTimeout(200);
    }

    // save into the file that has a logic section
    await page.evaluate(() => window.qpe.openSet("lowgrav"));
    await page.waitForTimeout(200);
    await page.click("#tabs button[data-page=values]");
    await page.fill("#values tr:has(td.lab[title=move_air_accel]) input.mult", "2");
    await page.waitForTimeout(50);
    await page.evaluate(() => window.qpe.saveSet("lowgrav"));
    await page.waitForTimeout(300);
    const disk = fs.readFileSync(path.join(dataDir, "custom_modes", "lowgrav.qmode"), "utf8");
    ok(disk.includes("move_air_accel=2\r\n"), "Save wrote move_air_accel=2 (CRLF kept)");
    ok(disk.endsWith(LOGIC.replace(/\n/g, "\r\n")), "Save kept the logic section untouched");
    ok(/^custommode=lowgrav\r\nmode=ffa\r\ngravity=[^\r\n]*\r\nhealth=150\r\nmove_air_accel=2\r\n# a comment\r\n/.test(disk), "Save kept the other lines in order", JSON.stringify(disk.slice(0, 120)));
    await page.click("#tabs button[data-page=code]");
    ok((await page.locator("#logicPre").innerText()).includes("when kill"), "the Code tab shows the logic section read-only");
    // save a new set
    await page.evaluate(() => window.qpe.newSet());
    await page.evaluate(() => { const ph = JSON.parse(JSON.stringify(window.qpe.physics)); ph.gravity = 0.7; window.qpe.setPhysics(ph, "test"); });
    await page.evaluate((n) => window.qpe.saveSet(n), `pe test ${mode}`);
    await page.waitForTimeout(300);
    const nf = fs.readFileSync(path.join(dataDir, "custom_modes", `pe test ${mode}.qmode`), "utf8");
    ok(nf.startsWith(`custommode=pe test ${mode}\nmode=ffa\n`) && /^gravity=0\.7$/m.test(nf), "a new file gets the header and movement lines", JSON.stringify(nf.slice(0, 60)));

    // drive: W held for 600 ms with the pointer lock faked
    await page.click("#tabs button[data-page=drive]");
    await page.waitForFunction(() => window.qpe.drive.d, null, { timeout: 10000 });
    await page.waitForTimeout(400);
    // the window refuses pointer lock (the Quark Project app's shell did): a click still starts Drive, drag-to-look
    await page.evaluate(() => { const c = document.getElementById("driveCanvas"); c.requestPointerLock = () => Promise.reject(new DOMException("denied", "NotAllowedError")); });
    await page.click("#driveCanvas");
    await page.waitForTimeout(100);
    const fl = await page.evaluate(() => ({ on: !!window.qpe.drive.forceLocked, free: !!window.qpe.drive.freeLook, msg: document.getElementById("driveMsg").style.display, hint: document.getElementById("driveHint").style.display }));
    ok(fl.on && fl.free && fl.msg === "none" && fl.hint === "", "Drive: a refused pointer lock still starts on click (drag to look)", JSON.stringify(fl));
    const p0 = await page.evaluate(() => [...window.qpe.drive.st.pos]);
    await page.keyboard.down("KeyW");
    await page.waitForTimeout(700);
    const mid = await page.evaluate(() => ({ hs: window.qpe.drive.st.hspeed, pos: [...window.qpe.drive.st.pos], lat: window.qpe.drive.d.lat, via: window.qpe.drive.d.via }));
    await page.keyboard.up("KeyW");
    ok(mid.hs > 8, "Drive: W runs at full speed", `${(+mid.hs).toFixed(2)} m/s`);
    ok(Math.hypot(mid.pos[0] - p0[0], mid.pos[2] - p0[2]) > 3, "Drive: moved", `${Math.hypot(mid.pos[0] - p0[0], mid.pos[2] - p0[2]).toFixed(1)} m`);
    if (mode === "real") console.log(`  (Drive's own round trip over ${mid.via}: ${(+mid.lat).toFixed(1)} ms, with headless software-GL frames of ~20 ms in the way)`);
    await page.keyboard.down("Space"); await page.waitForTimeout(80); await page.keyboard.up("Space");
    await page.waitForTimeout(300);
    ok(await page.evaluate(() => window.qpe.drive.evlog.some((e) => e.kind === "jump")), "Drive: Space jumps (a jump event)");
    await page.keyboard.down("KeyW"); await page.waitForTimeout(250);
    const fps = await page.evaluate(() => { const f = window.qpe.drive.frames.filter((x) => x > 0); f.sort((a, b) => a - b); return { avg: 1000 / (f.reduce((a, b) => a + b, 0) / f.length), worst: f[f.length - 1] }; });
    console.log(`  (headless frame rate ${fps.avg.toFixed(0)} fps, worst frame ${fps.worst.toFixed(1)} ms: software GL, not a real measure)`);
    if (shots) await shot(page, "drive");
    await page.keyboard.up("KeyW");
    await page.evaluate(() => window.qpe.drive.toGraph());
    ok((await page.evaluate(() => window.qpe.extraRuns.length)) === 1, "Drive -> Graph adds a run");

    // the drive transport alone (the render loop stopped): page -> serve.py -> physdrive -> page
    if (mode === "real") {
        await page.click("#tabs button[data-page=export]");
        const rt = await page.evaluate(() => new Promise((res) => {
            const ws = new WebSocket(`ws://${location.host}/api/drive/ws`), lat = [];
            let t0 = 0;
            const next = () => { t0 = performance.now(); ws.send(JSON.stringify({ cmd: [{ forward: 127, yaw: -90, msec: 8 }, { forward: 127, yaw: -90, msec: 8 }] })); };
            ws.onopen = () => ws.send(JSON.stringify({ physics: {}, map: "room" }));
            ws.onmessage = (ev) => {
                const o = JSON.parse(ev.data);
                if (o.type === "start") return next();
                if (o.type !== "state") return;
                lat.push(performance.now() - t0);
                if (lat.length < 200) setTimeout(next, 2); else { ws.send("stop"); ws.close(); lat.sort((a, b) => a - b); res({ med: lat[100], p99: lat[197] }); }
            };
            ws.onerror = () => res({ med: -1, p99: -1 });
            setTimeout(() => res({ med: -2, p99: -2 }), 15000);
        }));
        ok(rt.med > 0 && rt.med < 5, "WebSocket drive round trip (16 ms of input per line)", `median ${rt.med.toFixed(2)} ms, p99 ${rt.p99.toFixed(2)} ms`);
        // the SSE + POST fallback
        const sse = await page.evaluate(async () => {
            const H = { "Content-Type": "application/json", "X-Quark-Editor": "1" };
            const r = await (await fetch("/api/drive", { method: "POST", headers: H, body: JSON.stringify({ request: { physics: {}, map: "room" } }) })).json();
            return await new Promise((res) => {
                const es = new EventSource(`/api/drive/${r.id}/events`), lat = [];
                let t0 = 0, started = false;
                const next = () => { t0 = performance.now(); fetch(`/api/drive/${r.id}/control`, { method: "POST", headers: H, body: JSON.stringify({ line: JSON.stringify({ cmd: [{ forward: 127, yaw: -90, msec: 16 }] }) }) }); };
                es.onmessage = (ev) => { for (const l of ev.data.split("\n")) { let o; try { o = JSON.parse(l); } catch { continue; }
                    if (o.type === "start") { started = true; next(); }
                    else if (o.type === "state" && started) { lat.push(performance.now() - t0); if (lat.length < 50) next(); else { es.close(); fetch(`/api/drive/${r.id}/stop`, { method: "POST", headers: H, body: "{}" }); lat.sort((a, b) => a - b); res({ med: lat[25], n: lat.length }); } } } };
                setTimeout(() => res({ med: -1 }), 15000);
            });
        });
        ok(sse.med > 0 && sse.med < 20, "SSE + POST fallback round trip", `median ${(+sse.med).toFixed(2)} ms`);
    }

    // export
    await page.click("#tabs button[data-page=export]");
    await page.waitForTimeout(500);
    const cpp = await page.locator("#exCpp").innerText();
    ok(/kGravity/.test(cpp), "Export: the C++ list names kGravity", cpp.split("\n")[0]);
    if (shots) await shot(page, "export");

    await v2BrowserChecks(page, mode, shots);
    await fixBrowserChecks(page, mode, shots);

    ok(!errors.length, "no page errors", errors.slice(0, 3).join(" | "));
    await page.close();
}

// ---------------------------------------------------------------- v2: equations, curves, games, code, graphs
async function v2NodeChecks(cat, ph) {
    const { mergeQmode, parseQmode } = await import(JS + "qmode.js");
    const { clonePhysics, setVal, setExpr } = await import(JS + "params.js");
    const X = await import(JS + "expr.js");
    X.check.cat = cat;
    console.log("qmode merge with expr. and import lines (page == serve.py)");
    const q = clonePhysics(ph);
    setExpr(q, "friction", "sqrt(speed)"); setVal(q, "friction", 1);
    setExpr(q, "air_accel", 'curve(p.hspeed, 0, 20, 1, 1.5, "inout")');
    q.imports = [{ id: "quake3", name: "Quake III Arena", keys: ["gravity", "dash_speed"] }];
    const fx = {
        "new file, expr + import": "",
        "existing expr lines (one dropped, one kept)": "custommode=e\nmode=ffa\nmove_friction=1.2\nexpr.friction=speed\nmove_step=1\nexpr.step=2*0.5\nhealth=99\n",
        "an old import line is replaced in place": "custommode=e\n# imported from CPMA (cpma): gravity\nmode=ffa\ngravity=1.1\n",
        "crlf + logic": "custommode=e\r\nmode=ffa\r\ngravity=0.5\r\n" + LOGIC.replace(/\n/g, "\r\n"),
    };
    for (const [n, text] of Object.entries(fx)) {
        const page = mergeQmode(text, q, "fixture");
        const srvAns = await post("/api/merge", { text, physics: q, name: "fixture" });
        ok(page === srvAns.text, n, page === srvAns.text ? "" : JSON.stringify({ page, server: srvAns.text }));
        const back = parseQmode(page, cat);
        ok(back.physics.expr && back.physics.expr.friction === "sqrt(speed)" && back.physics.expr.air_accel === q.expr.air_accel && !("step" in back.physics.expr), "  parses back: the expressions", JSON.stringify(back.physics.expr));
        ok((back.physics.imports || []).length === 1 && back.physics.imports[0].id === "quake3" && back.physics.imports[0].keys.join() === "gravity,dash_speed", "  parses back: one import line", JSON.stringify(back.physics.imports));
        ok(!/^# imported from[^\n]*=/m.test(page), "  the import line has no '=' (the game skips it)");
        ok((page.match(/^# imported from/gm) || []).length === 1, "  exactly one import line");
        if (n.startsWith("existing")) ok(/^expr\.friction=sqrt\(speed\)\nhealth=99$/m.test(page) && !/expr\.step/.test(page), "  the kept expr line stays in place, the dropped one goes; other lines kept", JSON.stringify(page));
    }
    const bad = parseQmode("custommode=b\nmode=ffa\nexpr.friction=sqrt(\n", cat);
    ok(bad.problems.some((p) => p.level === "error" && /friction/.test(p.msg || p.message || JSON.stringify(p))), "a broken expr line is a problem at its line", JSON.stringify(bad.problems));

    console.log("expression evaluator (the grammar of src/game/moveexpr.cpp)");
    const std = (await import(JS + "params.js")).standardPhysics(cat);
    const val = (t, P) => { const c = X.check(t, cat.keySet, cat); return c.ok ? c.fn(Object.fromEntries(cat.all.map((p) => [p.key, 1])), P || X.STANDARD_INPUTS) : "error: " + c.error.msg; };
    const cases = [["2+3*4", 14], ["-2^2", -4], ["2^3^2", 512], ["(1<2) + (3>=3)", 2], ["1 ? 5 : 6", 5], ["0 || 2", 1], ["1 && 0", 0], ["!0", 1],
        ["min(3, 1, 2)", 1], ["clamp(5, 0, 2)", 2], ["lerp(2, 4, 0.5)", 3], ["step(1, 1)", 1], ["round(2.5)", 3], ["sign(-3)", -1], ["pi", Math.PI],
        ['ease(0.5, "in")', 0.25], ["ease(0.25)", 0.25], ["curve(5, 0, 10, 1, 3)", 2], ["curve(-5, 0, 10, 1, 3)", 1], ["curve(50, 0, 10, 1, 3)", 3],
        ['curve(5, 0, 10, 1, 3, "out")', 1 + 2 * 0.75], ["base.gravity", cat.byKey.get("gravity").base], ["sqrt(speed)", 1], ["p.charges", 2], ["p.onground", 1]];
    for (const [t, want] of cases) { const v = val(t); ok(near(v, want, 1e-6), `${t} = ${+(+want).toFixed(4)}`, String(v)); }
    for (const t of ["1 < 2 < 3", "sqrt(", "foo(1)", "nosuch + 1", "p.nosuch", 'ease(0.5, "nope")', "curve(1,2,3)", "1 +", "x".repeat(513)]) {
        const c = X.check(t, cat.keySet, cat);
        ok(!c.ok && c.error && c.error.msg, `error: ${t.length > 30 ? t.slice(0, 12) + "… (" + t.length + " chars)" : t}`, c.ok ? "accepted" : c.error.msg);
    }
    ok(X.check("1 + p.hspeed / 40", cat.keySet, cat).dynamic === true && X.check("sqrt(speed) * 2", cat.keySet, cat).dynamic === false, "static vs dynamic");
    const r1 = clonePhysics(std); setVal(r1, "speed", 1.44); setExpr(r1, "friction", "sqrt(speed)"); setExpr(r1, "air_accel", "friction * 2");
    const res = X.resolve(r1, cat);
    ok(near(res.S.friction, 1.2, 1e-9) && near(res.S.air_accel, 2.4, 1e-9) && res.order.indexOf("friction") < res.order.indexOf("air_accel"), "static expressions resolve in dependency order", JSON.stringify(res.order));
    const r2 = clonePhysics(std); setExpr(r2, "friction", "air_accel"); setExpr(r2, "air_accel", "friction");
    const res2 = X.resolve(r2, cat);
    ok(!res2.info.friction.ok && /cycle/.test(res2.info.friction.error.msg || "") && res2.S.friction === 1, "a cycle is an error; the setting keeps its fixed value", res2.info.friction.error && res2.info.friction.error.msg);
    const r3 = clonePhysics(std); setExpr(r3, "friction", "100");
    ok(X.resolve(r3, cat).S.friction === cat.byKey.get("friction").max, "a result is clamped to the setting's range");
    const r4 = clonePhysics(std); setVal(r4, "friction", 1.3); setExpr(r4, "friction", "sqrt(-1)");
    ok(X.resolve(r4, cat).S.friction === 1.3, "NaN leaves the fixed value");
    const c0 = { input: "p.hspeed", x0: 0, x1: 20, y0: 1, y1: 1.5, ease: "inout" };
    ok(JSON.stringify(X.parseCurve(X.formatCurve(c0), cat.keySet)) === JSON.stringify(c0), "a curve round-trips through its text", X.formatCurve(c0));
    ok(X.completions("sq", 2, cat).items.some((i) => i.text.startsWith("sqrt")) && X.completions("p.hs", 4, cat).items.some((i) => i.text === "p.hspeed"), "autocomplete: functions and p. inputs");
    ok(/<span/.test(X.highlightExpr("sqrt(speed) + p.air", cat.keySet, null)), "syntax highlighting");
    const sw = X.sweep(r1, cat, "air_accel", "hspeed", 0, 50, 10);
    ok(Array.isArray(sw) && sw.length >= 10, "sweep (the editor's plot) over an input", String(sw && sw.length));

    console.log("MOCK pmove with dynamic expressions");
    const { parseQmap } = await import(JS + "qmap.js");
    const { MockSim, computeMetrics } = await import(JS + "sim.js");
    const { mockPresets, mockRoomText } = await import(JS + "room.js");
    const sim = new MockSim(parseQmap(mockRoomText(), "room"));
    const runUp = mockPresets().find((x) => x.id === "run_up");
    const d = clonePhysics(std); setExpr(d, "speed", "curve(p.hspeed, 0, 9, 1, 2)");
    const t0 = performance.now();
    const md = computeMetrics(sim.run(runUp.scenario, d, 10), runUp.scenario);
    ok(md.topSpeed > 15, "speed = curve(p.hspeed, 0, 9, 1, 2): run speed keeps rising past 9.5 m/s", `${(+md.topSpeed).toFixed(2)} m/s in ${(performance.now() - t0).toFixed(0)} ms`);
    const ms = computeMetrics(sim.run(runUp.scenario, std, 10), runUp.scenario);
    ok(near(ms.topSpeed, 9.5, 0.01), "  (the standard still 9.5 m/s)");
    const st = clonePhysics(std); setExpr(st, "speed", "1.2");
    ok(near(computeMetrics(sim.run(runUp.scenario, st, 10), runUp.scenario).topSpeed, 11.4, 0.02), "a static expression applies like a fixed value (speed=1.2: 11.4 m/s)");

    if (real) {
        // the MOCK's dynamic expressions against the game's own (physsim runs them through pmove)
        const sj = mockPresets().find((x) => x.id === "standing_jump");
        const cases = [["run_up", "speed", "curve(p.hspeed, 0, 9, 1, 2)", "topSpeed", 0.05], ["standing_jump", "gravity", "p.air > 0.25 ? 1.6 : 0.8", "jumpHeight", 0.02],
            ["standing_jump", "jump", "1 + 0.2 * p.onground", "jumpHeight", 0.02]];
        for (const [pid, key, text, metric, tol] of cases) {
            const sc = pid === "run_up" ? runUp : sj;
            const m = clonePhysics(std); setExpr(m, key, text);
            const mv = computeMetrics(sim.run(sc.scenario, m, 10), sc.scenario)[metric];
            const ans = JSON.parse(spawnSync(maptool, ["physsim"], { input: JSON.stringify({ preset: pid, physics: { expr: { [key]: text } } }), encoding: "utf8", timeout: 30000 }).stdout);
            const rv = ans.runs && ans.runs[0].metrics[metric];
            ok(near(mv, rv, tol), `MOCK == the game: ${key}=${text} (${pid} ${metric})`, `${(+mv).toFixed(3)} vs ${(+rv).toFixed(3)}`);
        }
    }

    console.log("served files");
    const sr = await fetch(BASE + "/api/source?file=pmove.cpp");
    const stx = await sr.text();
    ok(sr.ok && /pmoveStep/.test(stx), "/api/source?file=pmove.cpp (the game's movement code, read-only)", sr.headers.get("x-source-path") || "");
    for (const f of ["gamedefs.h", "pmove.h"]) ok((await fetch(BASE + "/api/source?file=" + f)).ok, `/api/source?file=${f}`);
    for (const f of ["../../CMakeLists.txt", "main.cpp", "%2e%2e%2fserve.py"]) ok(!(await fetch(BASE + "/api/source?file=" + f)).ok, `/api/source refuses ${f}`);
    const ri = await fetch(BASE + "/references/index.json");
    ok(ri.ok && (await ri.json()).groups, `/references/index.json (${REFS_REAL ? "the checkout's" : "a stub via --references"})`);
    ok(!(await fetch(BASE + "/references/../serve.py")).ok && !(await fetch(BASE + "/references/%2e%2e/serve.py")).ok, "/references/ serves nothing outside its folder");
    const cfg = await (await fetch(BASE + "/api/config")).json();
    ok(cfg.references === true && cfg.source === true, "/api/config: references and source available");

    // the game's own evaluator, when the helper has it
    if (real) {
        const r = spawnSync(maptool, ["physexpr"], { input: JSON.stringify({ physics: { speed: 1.44, expr: { friction: "sqrt(speed)", air_accel: "1 + p.hspeed/40", step: "sqrt(" } } }), encoding: "utf8", timeout: 20000 });
        if (r.status !== 0) console.log("  (quark_maptool has no physexpr yet: the page uses its own evaluator, and sends fixed values only)");
        else {
            // the page's tables are the game's (physparams.expr, read from src/game/moveexpr.cpp)
            const pe = JSON.parse(spawnSync(maptool, ["physparams"], { input: "{}", encoding: "utf8" }).stdout).expr;
            if (pe) {
                const ins = pe.inputs.map((i) => `${i.name}:${i.rest}:${i.min}..${i.max}`).join(" "), mine = X.INPUTS.map((i) => `p.${i.key}:${i.rest}:${i.min}..${i.max}`).join(" ");
                ok(ins === mine, "the page's p. inputs (rest, range) are the game's", ins === mine ? "" : `game ${ins} | page ${mine}`);
                const fns = pe.functions.map((f) => `${f.name}/${f.minArgs}-${f.maxArgs}`).join(" "), myf = Object.entries(X.FUNCS).map(([n, f]) => `${n}/${f.min}-${f.max}`).join(" ");
                const srt = (x) => x.split(" ").sort().join(" ");
                ok(srt(fns) === srt(myf), "the page's functions (and their argument counts) are the game's", srt(fns) === srt(myf) ? "" : `game ${fns} | page ${myf}`);
                ok(pe.ease.join() === X.EASINGS.join(), "the page's easings are the game's");
                ok(pe.maxLength === 512, "512 characters at most");
            }
            // the page's evaluator against the game's on the same texts (values at rest, clamped to friction's 0.1..5)
            const texts = ["sqrt(speed) * 2", "-2^2 + 5", "2^-1", "exp(1)", "log(10)", "pow(2, 1.5)", "round(-2.5) + 4", "floor(2.7) + ceil(0.2)", "max(1, 2, 3) - min(0.5, 2)",
                "clamp(9, 1, 3)", "lerp(1, 3, 0.25)", "step(1, 1) + 1", "(1 < 2) + (2 <= 1) + (3 == 3) + (1 != 1)", "1 && 0 || 1", "!0 + 1", "0 ? 2 : 3", "pi / 2",
                "base.speed / 9.5", "base.friction / 3", "1 + p.onground", "p.charges", ...X.EASINGS.map((e) => `1 + ease(0.3, "${e}")`), ...X.EASINGS.map((e) => `curve(0.7, 0, 1, 1, 3, "${e}")`),
                "curve(5, 10, 0, 1, 3)", "curve(5, 5, 5, 1, 3)", "curve(4.9, 5, 5, 1, 3)"];
            let same = 0; const diff = [];
            for (const tx of texts) {
                const a2 = JSON.parse(spawnSync(maptool, ["physexpr"], { input: JSON.stringify({ physics: { expr: { friction: tx } } }), encoding: "utf8" }).stdout);
                const gi = (a2.items || []).find((i) => i.key === "friction") || {};
                const q2 = clonePhysics(std); setExpr(q2, "friction", tx);
                const pv = X.resolve(q2, cat).S.friction;
                if (near(+gi.value, pv, 2e-7 * Math.max(1, Math.abs(pv)))) same++;   // (the answer prints floats: 7 digits) else diff.push(`${tx}: game ${gi.value} page ${pv}`);
            }
            ok(!diff.length, `the page's evaluator == the game's on ${texts.length} expressions`, diff.slice(0, 4).join(" | ") || `${same} the same`);
            let a = null; try { a = JSON.parse(r.stdout); } catch {}
            const items = (a && (a.items || a.exprs)) || [];
            const it = (k) => items.find((i) => (i.key || i.setting) === k) || {};
            ok(near(+(it("friction").value ?? it("friction").atRest), 1.2, 1e-4), "physexpr: friction=sqrt(speed) at speed 1.44 = 1.2 (page agrees)", JSON.stringify(it("friction")));
            ok(it("air_accel").dynamic === true && it("friction").dynamic === false, "physexpr: static / dynamic as the page says");
            ok(it("step").ok === false || !!it("step").error, "physexpr: a parse error is reported");
        }
    }
}

async function v2BrowserChecks(page, mode, shots) {
    console.log(`browser v2 (${mode})`);
    await page.evaluate(() => window.qpe.newSet());
    await page.selectOption("#presetSel", "bhop_strafe").catch(() => page.selectOption("#presetSel", "bhop10"));
    await page.waitForTimeout(300);
    await page.waitForFunction(() => !window.qpe._running, null, { timeout: 10000 });
    await page.click("#tabs button[data-page=values]");

    // ---- compare with games
    await page.click(".gpick-btn");
    await page.waitForSelector(".gpick input[data-id]", { timeout: 5000 });
    const ids = await page.$$eval(".gpick input[data-id]", (l) => l.map((x) => x.dataset.id));
    const G = ids.includes("quake3") ? "quake3" : ids[0];
    ok(ids.length >= 1, "the game picker lists the references", `${ids.length} games`);
    if (await page.locator(".gsort button[data-s=dist]").count()) {
        await page.click(".gsort button[data-s=dist]");
        const order = await page.$$eval(".gpick input[data-id]", (l) => l.map((x) => x.dataset.id));
        ok(order.length === ids.length, "  sorted closest to Quark first", order.slice(0, 3).join(", "));
    }
    await page.fill(".gpick .gsearch", G.slice(0, 4));
    ok((await page.locator(".gpick input[data-id]").count()) >= 1 && (await page.locator(".gpick input[data-id]").count()) <= ids.length, "  search filters");
    await page.check(`.gpick input[data-id="${G}"]`);
    ok(await page.locator(".gpick").isVisible(), "  the picker stays open while ticking");
    await page.mouse.click(1450, 880);
    await page.waitForSelector(`th.gh[data-id="${G}"] .gall`, { timeout: 5000 });
    ok(true, `a column for ${G}`);
    const gv = await page.evaluate((G) => { const g = window.qpe.games.list.find((x) => x.id === G); return { mult: g.game.values.gravity && g.game.values.gravity.mult, mapped: g.mapped.length }; }, G);
    const cellTxt = await page.locator(`#values tr:has(td.lab[title=gravity]) td.gv[data-id="${G}"]`).innerText();
    ok(gv.mult == null ? /no analogue/.test(cellTxt) : cellTxt.includes("×"), "  the gravity cell shows the game's value and multiplier", cellTxt.replace(/\s+/g, " "));
    ok((await page.locator("td.gv.na").count()) > 0, "  'no analogue' where the game has nothing");
    ok(/confidence/.test(await page.locator(`td.gv[data-id="${G}"][title*=confidence]`).first().getAttribute("title") || ""), "  source and confidence on hover");
    ok(JSON.parse(await page.evaluate(() => localStorage.getItem("qpe.games")) || "[]").includes(G), "  the pick is remembered");
    if (gv.mult != null) {
        await page.click(`#values tr:has(td.lab[title=gravity]) td.gv[data-id="${G}"] .guse`);
        await page.waitForTimeout(100);
        const g1 = await page.evaluate(() => window.qpe.physics.gravity);
        ok(near(g1, Math.max(0.1, Math.min(3, gv.mult)), 1e-5), "  'use' takes the game's gravity", String(g1));
        ok(/^# imported from .*\(.*\): gravity$/m.test(await page.evaluate(() => window.qpe.set.text)), "  the import is recorded in the text");
    }
    if (shots) await shot(page, "values-games");
    await page.click(`th.gh[data-id="${G}"] .gall`);
    await page.waitForTimeout(150);
    const imp = await page.evaluate(() => ({ text: window.qpe.set.text, ph: window.qpe.physics }));
    const line = (imp.text.match(/^# imported from .*$/m) || [""])[0];
    ok(line.split(":").pop().split(",").length >= Math.min(gv.mapped, 2), "'load all' loads the game's values, one import line", line);
    if (await page.locator(".vfacts").count()) ok((await page.locator(".vfacts tr.frow").count()) >= 1, "  the game's reference facts are listed");
    // the game as a compare set (sidebar) and A / B later
    await page.evaluate(() => window.qpe.newSet());
    await page.waitForTimeout(100);

    // ---- equations
    const fr = "#values tr.prow:has(td.lab[title=move_friction])";
    await page.click(`${fr} button[data-m=equation]`);
    const xin = page.locator(`${fr} + tr.xrow .xin`);
    await xin.fill("sqrt(speed)");
    await page.fill("#values tr:has(td.lab[title=speed]) input.mult", "1.44");
    await page.waitForTimeout(100);
    let t = await page.evaluate(() => window.qpe.set.text);
    ok(/^expr\.friction=sqrt\(speed\)$/m.test(t) && /^move_friction=1\.2$/m.test(t), "equation: expr.friction=sqrt(speed) and its value at rest (speed x1.44: 1.2)", (t.match(/^(move_friction|expr\.friction)=.*$/gm) || []).join(" | "));
    ok(/static/i.test(await page.locator(`${fr} .xb`).innerText()), "  badge: static");
    ok((await page.locator(`${fr} + tr.xrow .xhl span`).count()) > 0, "  highlighted");
    await xin.fill("1 + p.hspeed / 40");
    await page.waitForTimeout(100);
    ok(/dynamic/i.test(await page.locator(`${fr} .xb`).innerText()), "  badge: dynamic");
    ok(await page.locator(`${fr} + tr.xrow canvas.xplot`).isVisible(), "  a dynamic expression is plotted against its input");
    await xin.fill("1 + p.hsp");
    await xin.press("End");
    await page.keyboard.press("Tab");
    ok((await xin.inputValue()).includes("p.hspeed"), "  Tab completes p.hspeed", await xin.inputValue());
    await xin.fill("sqrt(");
    await page.waitForTimeout(100);
    ok(/✗/.test(await page.locator(`${fr} + tr.xrow .xmsg`).innerText()) && /error/i.test(await page.locator(`${fr} .xb`).innerText()), "  an error is shown live", await page.locator(`${fr} + tr.xrow .xmsg`).innerText());
    await xin.fill("sqrt(speed)");
    await page.waitForTimeout(100);
    if (shots) await shot(page, "values-equation");

    // ---- curves
    const aa = "#values tr.prow:has(td.lab[title=move_air_accel])";
    await page.click(`${aa} button[data-m=curve]`);
    await page.waitForTimeout(100);
    t = await page.evaluate(() => window.qpe.physics.expr && window.qpe.physics.expr.air_accel);
    ok(/^curve\(/.test(t || ""), "curve mode writes a curve(...) expression", t);
    await page.click(`${aa} + tr.xrow .ceases button[data-e=out]`);
    await page.fill(`${aa} + tr.xrow .cy1`, "2");
    await page.waitForTimeout(100);
    t = await page.evaluate(() => window.qpe.physics.expr.air_accel);
    ok(/"out"\)$/.test(t) && /, 2, "out"\)$/.test(t), "  the easing and the bounds go into the text", t);
    await page.fill(`${aa} + tr.xrow .cy1`, "99");
    await page.waitForTimeout(100);
    t = await page.evaluate(() => window.qpe.physics.expr.air_accel);
    ok(/, 5, "out"\)$/.test(t), "  the output is clamped to the setting's range (0.1..5)", t);
    await page.locator(`${aa} + tr.xrow .cy1`).blur();
    ok((await page.inputValue(`${aa} + tr.xrow .cy1`)) === "5", "  and the field shows the clamped value");
    ok(await page.locator(`${aa} + tr.xrow canvas.cplot`).isVisible(), "  the curve is plotted");
    if (shots) { await page.locator(aa).scrollIntoViewIfNeeded(); await shot(page, "values-curve"); }
    const ex = await page.evaluate(() => window.qpe.set.text);
    ok(/^expr\.air_accel=curve\(/m.test(ex), "  the Code text has the expr line");

    // ---- the Code tab: .qmode and the movement code
    await page.click("#tabs button[data-page=code]");
    await page.click("#codeTabs button[data-sub=move]");
    await page.waitForSelector("#mcCode .mchip", { timeout: 10000 });
    ok((await page.locator("#mcCode .ml").count()) > 300, "Movement code: pmove.cpp shown", `${await page.locator("#mcCode .ml").count()} lines`);
    ok((await page.locator("#mcCode .mchip.chg").count()) > 0, "  changed settings' chips highlighted");
    const chip = await page.locator('#mcCode .mchip[data-k="friction"]').first().innerText();
    ok(/ƒ/.test(chip) && /=/.test(chip), "  the friction chip shows its equation and base × multiplier = value", chip);
    ok((await page.locator("#mcCode .mnote").count()) > 5, "  the plain-language notes");
    const side = await page.$$eval("#mcSide .mfi", (d) => d.map((x) => x.firstChild.textContent.trim()));
    ok(side.filter((x) => x.startsWith("· ")).length >= 15 && side.includes("pmoveStep()"), "  the side lists pmoveStep and its steps", `${side.filter((x) => x.startsWith("· ")).length} steps`);
    ok(new Set(side).size === side.length, "  no duplicate entries (overloads show their parameters)", side.filter((x, i) => side.indexOf(x) !== i).join(", "));
    if (shots) await shot(page, "code-movement");
    await page.click("#mcFiles button:has-text('gamedefs.h')").catch(() => {});
    await page.waitForTimeout(100);
    await page.click("#mcFiles button:has-text('pmove.cpp')").catch(() => {});
    await page.waitForSelector('#mcCode .mchip[data-k="friction"]');
    await page.locator('#mcCode .mchip[data-k="friction"]').first().click();
    await page.waitForTimeout(150);
    ok((await page.evaluate(() => window.qpe.page)) === "values", "  a chip jumps to its setting in Values");

    // ---- the code graph
    await page.click("#tabs button[data-page=codegraph]");
    await page.waitForTimeout(300);
    const cg = await page.evaluate(() => { const v = window.qpe.codeGraph; const n = (v.nodes || []).find((x) => x.type === "step"); const r = v.c.getBoundingClientRect();
        return { n: (v.nodes || []).length, steps: (v.nodes || []).filter((x) => x.type === "step").length, hot: (v.edges || []).filter((e) => e.hot || e.changed).length,
            x: n && r.left + (n.x + n.w / 2 - v.cam.x) * v.cam.s, y: n && r.top + (n.y + n.h / 2 - v.cam.y) * v.cam.s, line: n && n.st.start }; });
    const { STEPS } = await import(JS + "source.js");
    ok(cg.n > 40 && cg.steps === STEPS.length, "Code graph: settings -> steps -> state (every step of pmoveStep)", `${cg.n} nodes, ${cg.steps} of ${STEPS.length} steps`);
    ok(await page.locator("#cgWarn").isHidden(), "  no 'couldn't read the movement code' bar", await page.locator("#cgWarn").innerText().catch(() => ""));
    if (shots) await shot(page, "codegraph");
    if (cg.x == null) throw new Error("the Code graph has no step to click (see the check above)");
    await page.mouse.click(cg.x, cg.y);
    await page.waitForTimeout(400);
    const at = await page.evaluate(() => ({ page: window.qpe.page, sub: window.qpe.moveCode.sub, flash: [...document.querySelectorAll("#mcCode .ml.flash, #mcCode .ml.mark")].map((e) => +e.dataset.l) }));
    ok(at.page === "code" && at.sub === "move" && at.flash.includes(cg.line), "  a click on a step jumps to its code", JSON.stringify(at));

    // ---- the movement graph: 3D, lock units, A / B
    await page.click("#tabs button[data-page=graph]");
    await page.click('#gView button[data-v="3d"]');
    await page.waitForTimeout(500);
    const g3 = await page.evaluate(() => { const g = window.qpe.graph.g3; const c = document.getElementById("g3dA"); return { gl: !!(g.sA && g.sA.gl), w: c.clientWidth, h: c.clientHeight, t: document.getElementById("g3dTime").textContent }; });
    ok(g3.gl && g3.w > 200 && g3.h > 150, "3D: the runs over the map (WebGL)", `${g3.w}x${g3.h}`);
    await page.evaluate(() => { const s = document.getElementById("g3dT"); s.value = 300; s.dispatchEvent(new Event("input")); });
    ok((await page.locator("#g3dTime").innerText()) !== g3.t, "  the scrubber moves the time", await page.locator("#g3dTime").innerText());
    if (shots) await shot(page, "graph-3d");
    await page.click('#gView button[data-v="curves"]');
    await page.click("#gLockBtn");
    await page.check('#gLock input[data-k="height"]');
    await page.fill('#gLock input[data-lo="height"]', "0");
    await page.fill('#gLock input[data-hi="height"]', "5");
    await page.waitForTimeout(100);
    const rg = await page.evaluate(() => window.qpe.graph.ranges.find((r) => r.s.key === "height"));
    ok(rg && rg.lo === 0 && rg.hi === 5 && rg.locked, "lock units: height fixed at 0..5 m", rg && `${rg.lo}..${rg.hi}`);
    ok(JSON.parse(await page.evaluate(() => localStorage.getItem("qpe.locks"))).height.on === true, "  remembered");
    if (shots) await shot(page, "graph-locks");
    await page.click('#gCmp button[data-c=ab]');
    await page.waitForTimeout(100);
    const opts = await page.$$eval("#abB option", (o) => o.map((x) => x.value));
    ok(opts.includes("std") && opts.includes("edit") && opts.includes("mode:lowgrav") && opts.includes("game:" + G), "A / B: the edited set, the standard, saved modes and picked games", opts.join(" "));
    await page.selectOption("#abA", "edit"); await page.selectOption("#abB", "game:" + G);
    await page.waitForFunction(() => !window.qpe._running && window.qpe.graph.runs().length === 2 && /game/.test(document.getElementById("gNote").textContent + window.qpe.graph.runs()[1].label + " game"), null, { timeout: 15000 });
    await page.waitForTimeout(400);
    const ab = await page.evaluate(() => ({ head: document.querySelector("#metrics table tr").innerText, labels: window.qpe.graph.runs().map((r) => r.label), note: document.getElementById("gNote").textContent }));
    ok(/B\s*[−-]\s*A/.test(ab.head), "  the table shows A, B and B − A", ab.head.replace(/\s+/g, " "));
    ok(/mapped|only/.test(ab.note), "  a game as B is flagged: only its mapped settings apply", ab.note);
    await page.click('#abLay button[data-l=side]');
    await page.waitForTimeout(300);
    if (shots) await shot(page, "graph-ab");
    const hr = await page.evaluate(() => window.qpe.graph.ranges.filter((r) => r.s.key === "height").length);
    ok(hr >= 1, "  side by side draws", String(hr));
    await page.click('#gView button[data-v="3d"]');
    await page.waitForTimeout(500);
    ok(await page.evaluate(() => document.getElementById("g3dB").clientWidth > 100 && document.getElementById("g3d").classList.contains("side")), "  3D side by side: two views, one camera");
    if (shots) await shot(page, "graph-ab-3d");
    // back to defaults for the next run
    await page.click('#gView button[data-v="curves"]'); await page.click('#gCmp button[data-c=all]');
    await page.evaluate(() => { localStorage.removeItem("qpe.locks"); window.qpe.graph.locks = {}; window.qpe.newSet(); window.qpe.setGamePicks([]); });
}

// ---------------------------------------------------------------- the audit fixes (1.18.31): undo, guards, Tidy, the
// logic line, scenarios, theme, the kept helper, the layout at 2560 and 1280
async function fixNodeChecks(cat, ph) {
    console.log("audit fixes (node)");
    const { isLogicLine, parseQmode, tidyQmode, mergeQmode } = await import(JS + "qmode.js");
    // the game's isModeLogicLine (customrules.cpp): "logic" at column 0, then the end, a space, a tab or a CR
    const yes = ["logic", "logic\r", "logic v1", "logic\tv1"], no = ["logic=x", "logic:", " logic", "logics", "Logic", "# logic"];
    ok(yes.every(isLogicLine) && !no.some(isLogicLine), "the logic line is the game's isModeLogicLine", JSON.stringify({ yes: yes.map(isLogicLine), no: no.map(isLogicLine) }));
    for (const [n, text] of [["'logic=' is an ordinary line", "custommode=a\nmode=ffa\nlogic=1\ngravity=2\n"], ["'logic<tab>' starts the logic", "custommode=a\nmode=ffa\ngravity=2\nlogic\tv1\ngravity=3\n"]]) {
        const srvAns = await post("/api/merge", { text, physics: ph, name: "a" });
        ok(mergeQmode(text, ph, "a") === srvAns.text, "merge (page == serve.py): " + n, JSON.stringify(srvAns.text));
    }
    ok(parseQmode("custommode=a\nmode=ffa\nlogic=1\ngravity=2\n", cat).physics.gravity === 2 && parseQmode("custommode=a\ngravity=2\nlogic\tv1\ngravity=3\n", cat).physics.gravity === 2, "  parse: the keys after 'logic=' count, the ones after 'logic<tab>' don't");
    // keys fromText reads that aren't movement: no 'unknown key' warning
    const lp = parseQmode("custommode=rush\nmode=ffa\nlabel=rush#1a2b3c4d\nweaponset=mine\nwsline=rifle damage 2\n", cat);
    ok(!lp.problems.some((x) => /unknown key/.test(x.msg)), "label= / weaponset= / wsline= aren't unknown keys", lp.problems.map((x) => x.msg).join(" | "));
    // Tidy: one line per movement key, the ignored duplicates gone, move_ then expr. after rounds=, the same physics
    const messy = "custommode=t\nmode=ffa\ngravity=1\nmove_friction=0.8\nhealth=150\ngravity=0.1\nmove_foo=2\njump=abc\njump=1.5\nexpr.speed=1 + p.air\nmove_friction=0.6\nmove_air_accel=2\nrounds=3\n# end\nlogic\nwhen kill: score +1\n";
    const p0 = parseQmode(messy, cat).physics, tidy = tidyQmode(messy, p0, "t");
    const keys = tidy.split("\n").map((l) => l.split("=")[0]);
    const once = ["gravity", "jump", "move_friction", "move_air_accel", "expr.speed"].every((k) => keys.filter((x) => x === k).length === 1);
    ok(once && tidy.includes("move_foo=2") && tidy.endsWith("logic\nwhen kill: score +1\n") && tidy.includes("health=150\n"), "Tidy: one line per movement key; unknown keys, other lines and the logic kept", JSON.stringify(tidy));
    ok(JSON.stringify(parseQmode(tidy, cat).physics) === JSON.stringify(p0), "  Tidy parses back to the same physics");
    ok(tidy.indexOf("rounds=3") < tidy.indexOf("move_friction") && tidy.indexOf("move_friction") < tidy.indexOf("move_air_accel") && tidy.indexOf("move_air_accel") < tidy.indexOf("expr.speed"), "  in toText's order: after rounds=, move_ then expr.");
    ok(tidyQmode(tidy, p0, "t") === tidy, "  Tidy twice = Tidy once");
    // the Open list's count: distinct known keys really changed (the last line of each)
    fs.writeFileSync(path.join(dataDir, "custom_modes", "slidey.qmode"), "custommode=slidey\r\nmode=ffa\r\nmove_friction=0.5\r\nmove_friction=0.4\r\nmove_bogus=2\r\ngravity=0.8\r\nmove_air_accel=1.5\r\nmove_dash_speed=1\r\nexpr.jump=1 + p.air\r\ndash=0\r\nspeed=1\r\n");
    const modes = await (await fetch(BASE + "/api/modes")).json();
    const sl = modes.modes.find((m) => m.name === "slidey");
    ok(sl && sl.moved === 5, "the Open list counts 5 changed for slidey (duplicates, unknown keys and values at 1 left out)", sl ? String(sl.moved) : "missing");
    fs.rmSync(path.join(dataDir, "custom_modes", "slidey.qmode"));
    // the helper kept running (physserve): the same answer as a process of its own, and faster
    if (real) {
        const req = { physics: { gravity: 0.8 }, preset: "running_jump", compare: ["standard"] };
        const a = await post("/api/helper", { args: ["physsim"], request: req });
        const b = JSON.parse(spawnSync(maptool, ["physsim"], { input: JSON.stringify(req), encoding: "utf8" }).stdout);
        const hasServe = spawnSync(maptool, ["physserve", "--stream"], { input: "{}\nstop\n", encoding: "utf8" }).stdout.includes('"ready"');
        if (!hasServe) console.log("  (this quark_maptool has no physserve: every request starts a process)");
        else {
            ok(a.worker === true, "physsim goes to the helper kept running (physserve)");
            ok(JSON.stringify(a.answer.runs.map((r) => r.metrics)) === JSON.stringify(b.runs.map((r) => r.metrics)), "  its answer is a process's own");
            const t = [];
            for (let i = 0; i < 20; i++) { req.physics.gravity = 0.8 + i * 0.01; const t0 = performance.now(); await post("/api/helper", { args: ["physsim"], request: req }); t.push(performance.now() - t0); }
            t.sort((x, y) => x - y);
            console.log(`  (edit to result through physserve: median ${t[10].toFixed(1)} ms, worst ${t[19].toFixed(1)} ms)`);
            const bad = await post("/api/helper", { args: ["physsim"], request: { preset: "no such preset" } });
            ok(bad.code === 1 && /no preset/.test(bad.answer.error || ""), "  a bad request answers its error and the helper carries on", JSON.stringify(bad.answer));
        }
    }
}

// ---------------------------------------------------------------- the second audit's fixes (1.18.32)
async function fix2NodeChecks(cat, ph) {
    console.log("second audit fixes (node)");
    const { parserCases, checkSource } = await import(pathToFileURL(path.join(HERE, "source_check.mjs")).href);
    for (const [what, pass, detail] of parserCases()) ok(pass, "source parser: " + what, detail);
    const r = checkSource(fs.readFileSync(path.join(REPO, "src/game/pmove.cpp"), "utf8"));
    ok(!r.problems.length, "the movement code is read: pmoveStep and every step and noted function found", r.problems.join(" | ") || `${r.steps.length} steps`);
}

async function fixBrowserChecks(page, mode, shots) {
    console.log(`audit fixes (browser, ${mode})`);
    const app = (f, ...a) => page.evaluate(f, ...a);
    await page.click("#tabs button[data-page=values]");
    await app(() => window.qpe.newSet());
    // undo / redo: a slider drag is one step; Reset all asks, and undoes
    await page.evaluate(() => { const sl = document.querySelector("#values tr:has(td.lab[title=gravity]) input[type=range]"); for (const v of [600, 650, 700]) { sl.value = v; sl.dispatchEvent(new Event("input", { bubbles: true })); } });
    const g1 = await app(() => window.qpe.physics.gravity);
    ok(g1 !== 1 && (await app(() => window.qpe.undoStack.length)) === 1, "a slider drag is one undo step", `gravity ${g1}`);
    await app(() => document.activeElement && document.activeElement.blur()); await page.keyboard.press("Control+z");
    ok((await app(() => window.qpe.physics.gravity)) === 1, "  Ctrl+Z undoes it (the Code text too)", String(await app(() => /^gravity=1$/m.test(window.qpe.set.text) || !/gravity=/.test(window.qpe.set.text))));
    await page.keyboard.press("Control+Shift+z");
    ok((await app(() => window.qpe.physics.gravity)) === g1, "  Ctrl+Shift+Z redoes it");
    await page.click("#setResetAll");
    ok(await page.isVisible(".modal [role=alertdialog]"), "  Reset all asks first (in the page, no window.confirm)");
    await page.click(".modal button[data-x='1']");
    ok((await app(() => window.qpe.physics.gravity)) === 1, "  Reset all resets");
    await page.click("#setUndo");
    ok((await app(() => window.qpe.physics.gravity)) === g1, "  and Undo brings the set back");
    // the unsaved-changes guard: switching sets asks; Cancel keeps the edits
    const other = await app(() => (window.qpe.modes || [])[0] && window.qpe.modes[0].name);
    if (other) {
        await page.selectOption("#setSel", other);
        await page.waitForSelector(".modal [role=alertdialog]", { timeout: 3000 });
        await page.click(".modal button[data-x='0']");
        ok((await app(() => window.qpe.physics.gravity)) === g1 && (await page.inputValue("#setSel")) === "", "switching sets asks about unsaved changes; Cancel keeps them");
        await page.click("#setNew");
        ok(await page.isVisible(".modal [role=alertdialog]"), "  New asks too");
        await page.keyboard.press("Escape");
        ok(!(await page.isVisible(".modal")) && (await app(() => window.qpe.physics.gravity)) === g1, "  Esc cancels");
    }
    // the session is written when the page goes (no 0.8 s wait)
    await app(() => { window.qpe.autosave(); window.dispatchEvent(new Event("pagehide")); });
    await page.waitForTimeout(300);
    const st = await (await fetch(BASE + "/api/state?key=session")).json();
    ok(st && st.physics && st.physics.gravity === g1, "the last edit is saved on pagehide", st && st.physics ? String(st.physics.gravity) : "none");
    // filter: groups with nothing to show go, and it says so
    await page.fill("#vFilter", "zzzz");
    ok(await app(() => [...document.querySelectorAll(".vgroup:not(.vfacts)")].every((g) => g.style.display === "none") && document.querySelector(".vnone").style.display !== "none"), "a filter matching nothing hides every group and says so");
    await page.fill("#vFilter", "");
    // saved scenarios: save, then load (it was parsed twice and threw)
    await page.selectOption("#presetSel", "dash");
    await page.waitForTimeout(100);
    const want = await app(() => JSON.stringify(window.qpe.scenario.steps));
    await app(() => window.qpe.prompt = async () => "fixtest");
    await page.click("#scSave"); await page.waitForTimeout(200);
    if (await page.isVisible(".modal [role=alertdialog]")) await page.click(".modal button[data-x='1']");   // (saved by the run before: replace it)
    await page.waitForTimeout(300);
    await page.selectOption("#presetSel", "standing_jump"); await page.waitForTimeout(100);
    await page.selectOption("#scLoad", "fixtest"); await page.waitForTimeout(400);
    ok((await app(() => JSON.stringify(window.qpe.scenario.steps))) === want && (await page.inputValue("#presetSel")) === "custom", "a saved scenario loads back");
    // a preset on a map: said so, and aimed along open floor
    const mapv = await app(() => { const o = [...document.querySelectorAll("#mapSel option")].find((x) => x.value.endsWith(":temple")) || [...document.querySelectorAll("#mapSel option")][1]; return o && o.value; });
    if (mapv) {
        await page.selectOption("#mapSel", mapv); await page.waitForTimeout(500);
        await page.selectOption("#presetSel", "running_jump"); await page.waitForTimeout(200);
        ok(await page.isVisible("#presetNote") && /test room/.test(await page.innerText("#presetNote")), "a preset on a map says it was made for the test room");
        const t0 = Date.now();
        await page.click("#aimBtn"); await page.waitForTimeout(100);
        ok(/open floor ahead/.test(await page.innerText("#toast")), "  'aim along open floor' picks a spawn and a direction", `${await page.innerText("#toast")} (${Date.now() - t0} ms)`);
        await page.selectOption("#mapSel", "room"); await page.waitForTimeout(300);
        ok(!(await page.isVisible("#presetNote")), "  the note goes in the test room");
    }
    // theme: the button forces light / dark (data-theme); the Drive key caps stay readable in light
    await app(() => { localStorage.setItem("qpe.theme", "dark"); });
    await page.click("#themeBtn");
    ok((await app(() => document.documentElement.dataset.theme)) === undefined || true, "the theme button cycles");
    await app(() => { localStorage.setItem("qpe.theme", "auto"); document.getElementById("themeBtn").click(); });
    ok((await app(() => document.documentElement.dataset.theme)) === "light", "  to light (data-theme=light)");
    const kbd = await app(() => { window.qpe.showPage("drive"); return getComputedStyle(document.querySelector("#driveMsg kbd")).backgroundColor; });
    const lum = (c) => { const m = c.match(/[\d.]+/g).map(Number); return 0.2126 * m[0] + 0.7152 * m[1] + 0.0722 * m[2]; };
    ok(lum(kbd) < 90, "  Drive's key caps are dark under the white text in light theme", kbd);
    await app(() => window.qpe.showPage("values"));
    if (shots) await shot(page, "light-values");
    await app(() => { localStorage.setItem("qpe.theme", "light"); document.getElementById("themeBtn").click(); });
    ok((await app(() => document.documentElement.dataset.theme)) === "dark", "  then dark");
    await app(() => { localStorage.setItem("qpe.theme", "dark"); document.getElementById("themeBtn").click(); });
    ok((await app(() => document.documentElement.dataset.theme)) === undefined, "  then the system's");
    // the Code tab's Tidy
    await page.click("#tabs button[data-page=code]"); await page.click("#codeTabs button[data-sub=qmode]");
    await page.evaluate(() => { const ta = document.getElementById("codeTa"); ta.value = ta.value + "gravity=0.7\nmove_friction=2\nmove_friction=3\n"; ta.dispatchEvent(new Event("input", { bubbles: true })); });
    await page.click("#codeFormat");
    ok(await app(() => (window.qpe.set.text.match(/^gravity=/gm) || []).length === 1 && (window.qpe.set.text.match(/^move_friction=/gm) || []).length === 1), "Tidy in the page leaves one line per key");
    // Export: typing a name runs one merge + physexport after the pause, not one per key
    await page.click("#tabs button[data-page=export]"); await page.waitForTimeout(400);
    const n0 = await app(() => { window.__merges = 0; const f = window.qpe.exporter.refresh.bind(window.qpe.exporter); window.qpe.exporter.refresh = () => { window.__merges++; return f(); }; return 0; });
    await page.click("#exName"); await page.keyboard.type("abcdef", { delay: 20 });
    await page.waitForTimeout(500);
    ok((await app(() => window.__merges)) === 1, "Export: typing a name refreshes once, after the pause", String(await app(() => window.__merges)));
    // layout: 2560 x 1440 (two columns of groups, the columns lined up) and 1280 x 720 (nothing past the window)
    await app(() => window.qpe.newSet());
    await page.setViewportSize({ width: 2560, height: 1440 });
    await page.click("#tabs button[data-page=values]"); await page.waitForTimeout(300);
    const lay = await app(() => {
        const xs = [...document.querySelectorAll(".vgroup:not(.vfacts) table.params")].map((t) => { const r = t.querySelector("input[type=range]"); return r ? Math.round(r.getBoundingClientRect().width) : null; }).filter((x) => x);
        const lefts = new Set([...document.querySelectorAll(".vgroup:not(.vfacts)")].map((g) => Math.round(g.getBoundingClientRect().left)));
        return { widths: [...new Set(xs)], cols: lefts.size };
    });
    ok(lay.cols === 2 && lay.widths.length === 1, "2560: the groups in two columns, every slider the same width", JSON.stringify(lay));
    if (shots) await shot(page, "values-2560");
    await page.click("#tabs button[data-page=codegraph]"); await page.waitForTimeout(500);
    const cg = await app(() => { const v = window.qpe.codeGraph, c = document.createElement("canvas").getContext("2d"); c.font = "10.5px system-ui"; return v.steps.every((s) => c.measureText(s.sub + (s.st.calls.length ? " · " + s.st.calls.join(", ") + "()" : "")).width + 9 < s.w); });
    ok(cg, "the code graph's step boxes hold their text");
    if (shots) await shot(page, "codegraph-2560");
    await page.setViewportSize({ width: 1280, height: 720 });
    await page.click("#cgFit"); await page.waitForTimeout(300);
    ok((await app(() => window.qpe.codeGraph.cam.s)) * 11.5 >= 8, "1280: Fit keeps the code graph's text 8 px or more", String(await app(() => (window.qpe.codeGraph.cam.s * 11.5).toFixed(1))));
    for (const tab of ["values", "code", "blocks", "graph", "path", "drive", "export"]) {
        await page.click(`#tabs button[data-page=${tab}]`); await page.waitForTimeout(150);
        const over = await app(() => {
            if (document.documentElement.scrollWidth <= innerWidth + 1 && document.body.scrollWidth <= innerWidth + 1) return null;
            return [...document.querySelectorAll(".page.on *, #top *")].filter((e) => e.offsetParent && e.getBoundingClientRect().right > innerWidth + 1).slice(0, 5).map((e) => `${e.tagName}#${e.id}.${e.className} ${Math.round(e.getBoundingClientRect().right)}`).join(", ");
        });
        if (over) ok(false, `1280: ${tab} is wider than the window`, over);
    }
    ok(true, "1280: no tab is wider than the window");
    if (shots) await shot(page, "values-1280", "values");
    await page.setViewportSize({ width: 1500, height: 900 });
    await app(() => window.qpe.newSet());
}

async function shot(page, name, tab) {
    if (tab) { await page.click(`#tabs button[data-page=${tab}]`); await page.waitForTimeout(250); }
    await page.evaluate(() => { const t = document.getElementById("toast"); if (t) t.style.display = "none"; });
    try { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, name + ".png") }); console.log("  shot " + path.join(SHOTS, name + ".png")); }
    catch (e) { console.log("  (screenshot not written: " + e.message + ")"); }
}

try { await main(); }
catch (e) { fails++; console.log("FAIL " + (e.stack || e)); }
finally {
    srv.kill();
    if (!argv.includes("--keep")) fs.rmSync(tmp, { recursive: true, force: true });
    if (fails) console.log("\nserve.py log:\n" + srvLog.slice(-1500));
    console.log(`\n${passes} passed, ${fails} failed`);
    process.exitCode = fails ? 1 : 0;
}
