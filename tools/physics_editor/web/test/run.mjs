// Headless checks for the physics editor (node 18+, Playwright from the environment; never `playwright install`).
//
//   node tools/physics_editor/web/test/run.mjs [--shots DIR] [--maptool PATH] [--mock-only] [--keep]
//
// 1. node checks: the page's .qmode merge equals serve.py's (/api/merge) on fixtures (CRLF, a logic section, a new
//    file, duplicates), every preset round-trips through the Blocks tab, the MOCK's numbers match the game's.
// 2. browser checks (Chromium, for the real helper when quark_maptool has the physics commands, and for the MOCK):
//    a value change shows in the Code text, a Code edit shows in Values, a preset runs, a block drags into the
//    stack, Save keeps the gamemode editor's logic section on disk, Drive moves with W held (pointer lock faked).
// 3. a screenshot of each tab (real helper if present) into --shots (default /mnt/project-files/physics-editor/shots/).
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
const SHOTS = opt("--shots", "/mnt/project-files/physics-editor/shots/");
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
const port = 8790 + Math.floor(Math.random() * 150);
const py = process.platform === "win32" ? "py" : "python3";
const srvArgs = [...(process.platform === "win32" ? ["-3"] : []), path.join(TOOL, "serve.py"), "--port", String(port), "--no-open", "--data-dir", dataDir,
    "--maptool", real ? maptool : path.join(tmp, "no_maptool")];
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
    await page.evaluate(() => { window.qpe.drive.forceLocked = true; });
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

    ok(!errors.length, "no page errors", errors.slice(0, 3).join(" | "));
    await page.close();
}

async function shot(page, name, tab) {
    if (tab) { await page.click(`#tabs button[data-page=${tab}]`); await page.waitForTimeout(250); }
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
