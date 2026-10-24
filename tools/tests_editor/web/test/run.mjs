// Headless page tests (Playwright + Chromium). Two ways to run:
//   node tools/tests_editor/web/test/run.mjs                 starts test/fixture_server.py (saves go to a temp copy)
//   node tools/tests_editor/web/test/run.mjs --url http://127.0.0.1:8775 --repo <the server's repo copy>
//        against a running serve.py: --repo is where the 409 test edits the file "behind the page's back" (never the
//        real working repo: saves write it). Screenshots: --shots DIR (default /mnt/project-files/tests-editor/shots).
import { spawn, execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import { mkdirSync, readFileSync, appendFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
let playwright;
try { playwright = require("playwright"); } catch { playwright = require(path.join(execFileSync("npm", ["root", "-g"]).toString().trim(), "playwright")); }
const here = path.dirname(fileURLToPath(import.meta.url));
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const SHOTS = arg("--shots", "/mnt/project-files/tests-editor/shots");
let BASE = arg("--url", null);
const REPO = arg("--repo", null);
const FIXTURE = !BASE;
let fixture = null;
if (FIXTURE) {
    const net = await import("node:net");
    const port = await new Promise((res) => { const sv = net.createServer().listen(0, "127.0.0.1", () => { const p = sv.address().port; sv.close(() => res(p)); }); });
    BASE = `http://127.0.0.1:${port}`;
    fixture = spawn("python3", [path.join(here, "fixture_server.py"), "--port", String(port)], { stdio: ["ignore", "pipe", "inherit"] });
    await new Promise((res) => fixture.stdout.once("data", res));
}
mkdirSync(SHOTS, { recursive: true });
let fails = 0, passes = 0;
const ok = (c, msg, extra) => { if (c) { passes++; console.log("ok  ", msg); } else { fails++; console.log("FAIL", msg, extra === undefined ? "" : extra); } };
const post = (p, body) => fetch(BASE + p, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body || {}) }).then((r) => r.json().catch(() => ({})));
const getFile = (p) => fetch(BASE + "/api/file?path=" + encodeURIComponent(p)).then((r) => r.json());
const originals = new Map();
async function remember(p) { if (!originals.has(p)) originals.set(p, (await getFile(p)).text); }
async function restore() {
    if (FIXTURE) return post("/__test/reset");
    for (const [p, t] of originals) { const f = await getFile(p); if (f.text !== t) await post("/api/file", { path: p, text: t, baseSha256: f.sha256 }); }
}
async function touchBehind(p) {   // someone else edits the file on disk
    if (FIXTURE) return post("/__test/touch", { path: p });
    appendFileSync(path.join(REPO, p), "\n// touched by someone else\n");
}

const browser = await playwright.chromium.launch();
const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
const errors = [];
page.on("console", (m) => { if (m.type() === "error" && !/Failed to load resource: the server responded with a status of (409|404)/.test(m.text())) errors.push(m.text()); });
page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
const go = async (hash, view) => {
    await page.goto(BASE + "/" + hash);
    await page.waitForSelector("#tree .trow");
    if (view) await page.click(`#viewTabs [data-view=${view}]`);
    await page.waitForTimeout(350);
};
const diffLinesOf = () => page.$$eval(".modal pre.diff span", (s) => s.map((x) => x.textContent));
const shot = (n) => page.screenshot({ path: path.join(SHOTS, n) });
const model = await fetch(BASE + "/api/model").then((r) => r.json());
const qt = model.suites.find((s) => s.id === "quark_tests");
const weapons = qt.items.find((i) => i.id === "group:weapons");
const parCount = qt.items.filter((i) => i.kind === "group" && i.list === "par").length;
await restore();

try {
    // ---- When
    await go("#when");
    ok(await page.isVisible(".when"), "when view shows");
    ok(await page.locator("#lane-commit .gbar.par").count() === parCount, "before-commit lane: one bar per par group", await page.locator("#lane-commit .gbar.par").count());
    ok(await page.locator("#lane-commit .gbar.alone").count() === qt.items.filter((i) => i.kind === "group" && i.list !== "par").length, "before-commit lane: the alone groups in a row");
    ok(/sanitizers/.test(await page.textContent("#lane-final")), "final lane has the sanitizers");
    ok(/release-testing/.test(await page.textContent("#lane-testing")) && !/sanitizers/.test(await page.textContent("#lane-testing")), "testing lane: release-testing, not the sanitizers");
    const tagJobs = await page.$$eval("#lane-tag .wjob b", (b) => b.map((x) => x.textContent));
    ok(tagJobs.includes("release") && tagJobs.includes("pages") && !tagJobs.includes("linux"), "tag lane: release + pages", tagJobs.join());
    ok(/check_secrets|secrets/.test(await page.textContent("#lane-precommit")), "pre-commit lane: the hook");
    const order = await page.$$eval("section.lane", (ls) => ls.map((l) => l.id));
    ok(order.slice(0, 8).join() === "lane-edit,lane-precommit,lane-commit,lane-check,lane-push,lane-testing,lane-final,lane-tag", "lane order", order.join());
    await shot("when.png");

    // ---- a group: Code / Graph / Blocks
    await go("#group/weapons", "code");
    const ta = await page.inputValue(".code-ta");
    ok(ta.split("\n")[weapons.line - 1].includes('{"weapons"'), "code: the file is loaded, the group's line is where the model says");
    ok(await page.isVisible(".code-range"), "code: the group's range is highlighted");
    ok(await page.locator(".code-hl .tfn").count() > 5, "code: test functions coloured");
    const visible = await page.$$eval(".code-gin", (g) => g[0].textContent.split("\n").length);
    ok(visible < 200, "code: only the lines in view are rendered (virtualized)", visible);
    await shot("code.png");
    await page.click("#viewTabs [data-view=graph]");
    await page.waitForTimeout(400);
    ok(await page.locator(".gr-node.c-fn").count() === new Set(weapons.calls.map((c) => c.fn)).size, "graph: one node per called function");
    ok(await page.locator(".gr-barrow").count() === parCount && await page.locator(".gr-barrow.sel").count() === 1, "graph: the par lane, this group marked");
    await shot("group-graph.png");
    await page.click("#viewTabs [data-view=blocks]");
    await page.waitForTimeout(300);
    ok(await page.locator(".bk-stack .bk-stmt.c-call").count() === weapons.calls.length, "blocks: one call block per call");
    await shot("group-blocks.png");

    // ---- edit a block (drag the 2nd call above the 1st), check the patch, save, the model follows
    await remember(weapons.file);
    const blocks = page.locator(".bk-stack .bk-stmt.c-call");
    const b2 = await blocks.nth(1).boundingBox(), b1 = await blocks.nth(0).boundingBox();
    await page.mouse.move(b2.x + 7, b2.y + b2.height / 2);   // (the grip)
    await page.mouse.down();
    await page.mouse.move(b2.x + 7, b2.y - 5, { steps: 4 });
    await page.mouse.move(b1.x + 7, b1.y + 3, { steps: 6 });
    await page.mouse.up();
    await page.waitForTimeout(200);
    ok(await page.isVisible(".bk-draftbar .bk-dirty"), "drag: the draft bar says not saved");
    await page.click(".bk-draftbar button.primary");
    await page.waitForSelector(".modal pre.diff");
    const diff = (await diffLinesOf()).join("\n");
    const want = `{"weapons", [] { ${weapons.calls[1].fn}(${weapons.calls[1].args}); ${weapons.calls[0].fn}(${weapons.calls[0].args});`;
    const adds = diff.split("\n").filter((l) => l.startsWith("+") && !l.startsWith("+++"));
    ok(adds.length === 1 && adds[0].includes(want), "patch: one line changed, the two calls swapped", adds.join(" | "));
    await page.click(".modal button.primary");
    await page.waitForFunction(() => document.querySelector("#toast") && /saved/.test(document.querySelector("#toast").textContent));
    await page.waitForTimeout(300);
    const m2 = await fetch(BASE + "/api/model").then((r) => r.json());
    ok(m2.suites[0].items.find((i) => i.id === "group:weapons").calls[0].fn === weapons.calls[1].fn, "saved: the model has the new order");
    ok((await page.evaluate(() => location.hash)) === "#group/weapons" && await page.locator('#tree .trow.sel[data-key="quark_tests|group:weapons"]').count() === 1, "saved: the selection is kept");
    ok(await page.locator(".bk-stack .bk-stmt.c-call").first().locator(".bk-fn").textContent() === weapons.calls[1].fn, "saved: the blocks show the new order");
    await restore();

    // ---- par -> alone toggle: the patch moves the line
    await go("#group/hidden", "blocks");
    await page.click(".bk-hat .seg button:nth-child(2)");
    await page.click(".bk-draftbar button.primary");
    await page.waitForSelector(".modal pre.diff");
    const d2 = (await diffLinesOf()).filter((l) => /^[+-]\s/.test(l));
    ok(d2.length === 2 && d2[0].startsWith("-") && d2[1].startsWith("+") && d2.every((l) => l.includes('{"hidden"')), "move to alone: the group's line moves", d2.join(" | "));
    await page.click(".modal button:not(.primary):not(.danger)");

    // ---- a CI job
    await go("#ci/release.yml/macos", "graph");
    const jobs = model.suites.find((s) => s.id === "ci:release.yml").items;
    ok(await page.locator(".gr-node.c-job").count() === jobs.length, "ci graph: a node per job");
    ok(await page.locator(".gr-edge.cond").count() > 0 && await page.locator(".gr-elabel").count() > 0, "ci graph: conditional edges, labelled");
    ok(await page.locator(".gr-node.c-job.sel").count() === 1, "ci graph: the job is selected");
    await shot("ci-graph.png");
    await page.click("#viewTabs [data-view=blocks]");
    await page.waitForTimeout(300);
    const macos = jobs.find((j) => j.jobId === "macos");
    ok(await page.locator(".bk-stack .bk-stmt").count() === macos.steps.length, "ci blocks: a block per step");
    await shot("ci-blocks.png");
    await remember(macos.file);
    await page.fill(".bk-hat .bk-line input.bk-text >> nth=0", "github.ref == 'refs/heads/testing'");
    await page.press(".bk-hat .bk-line input.bk-text >> nth=0", "Tab");
    await page.click(".bk-draftbar button.primary");
    await page.waitForSelector(".modal pre.diff");
    const d3 = (await diffLinesOf()).filter((l) => /^[+-]\s/.test(l));
    ok(d3.length === 2 && d3[1].startsWith("+    if: github.ref == 'refs/heads/testing'") && d3[0].startsWith("-    if:"), "job If: one line replaced, the comment kept", d3.join(" | "));
    await page.click(".modal button:not(.primary):not(.danger)");
    await page.click(".bk-draftbar button:not(.primary)");   // discard
    // a rule: a gameplay group in CI warns before saving
    await page.click(".bk-stack .bk-stmt:not(.c-uses) .bk-mini:has-text('✎')");
    await page.fill(".bk-form textarea", "build/quark_tests group weapons");
    await page.press(".bk-form textarea", "Tab");
    ok(/ci-scope|quark_tests/.test(await page.textContent(".bk-draftbar")), "rule: the draft bar warns (ci-scope)");
    await page.click(".bk-draftbar button.primary");
    await page.waitForSelector(".modal .rulehit");
    ok(/ci-scope/.test(await page.textContent(".modal .rulehit")) && await page.locator(".modal button.danger:has-text('Save anyway')").count() === 1, "rule: the confirm panel names the rule, Save anyway");
    await page.click(".modal button:not(.primary):not(.danger)");
    await page.click(".bk-draftbar button:not(.primary)");

    // ---- a script
    const rc = model.suites.find((s) => s.id === "script:release_check.sh");
    await go("#suite/script:release_check.sh", "graph");
    ok(await page.locator(".gr-node.c-step").count() === rc.items.length, "script graph: a node per step");
    await page.click("#viewTabs [data-view=blocks]");
    ok(await page.locator(".bk-stack.ro .bk-stmt").count() === rc.items.length, "script blocks: a block per step (read-only)");
    await page.click("#viewTabs [data-view=code]");
    await page.waitForTimeout(200);
    ok((await page.textContent(".code-path")) === "tools/release_check.sh" && await page.locator(".code-hl .tc").count() > 0, "script code: loaded and coloured");

    // ---- a 409: edit in Code, the file changes on disk, save -> the choice, then overwrite after the diff
    const duel = qt.items.find((i) => i.id === "group:duel");
    await go("#group/duel", "code");
    await remember(duel.file);
    await page.focus(".code-ta");
    await page.evaluate((line) => { const ta = document.querySelector(".code-ta"); const v = ta.value.split("\n"); const at = v.slice(0, line).join("\n").length; ta.setSelectionRange(at, at); }, duel.line);
    await page.keyboard.type("   // mine");
    ok(/unsaved/.test(await page.textContent(".code-dirty")), "code: dirty marker");
    await touchBehind(duel.file);
    await page.keyboard.press("Control+s");
    await page.waitForSelector(".modal h3:has-text('changed on disk')");
    ok(true, "409: 'changed on disk' choice shown");
    await page.click(".modal button:has-text('Show diff')");
    await page.waitForSelector(".modal pre.diff");
    const d4 = (await diffLinesOf()).join("\n");
    ok(/-\/\/ touched by someone else/.test(d4) && /\+.*\/\/ mine/.test(d4), "409: the diff shows theirs (−) and mine (+)");
    await page.click(".modal button:has-text('Overwrite')");
    await page.waitForFunction(() => /saved/.test(document.querySelector("#toast").textContent));
    const after = (await getFile(duel.file)).text;
    ok(after.includes("// mine") && !after.includes("touched by someone else"), "409: overwrite saved mine");
    await restore();
    // and the other choice: reload
    await go("#group/duel", "code");
    await page.focus(".code-ta");
    await page.keyboard.type("x");
    await touchBehind(duel.file);
    await page.keyboard.press("Control+s");
    await page.waitForSelector(".modal h3:has-text('changed on disk')");
    await page.click(".modal button:has-text('Reload')");
    await page.waitForTimeout(400);
    ok((await page.inputValue(".code-ta")).includes("touched by someone else") && !(await page.textContent(".code-dirty")), "409: reload takes the file on disk");
    await restore();

    // ---- routing
    await go("#ci/release.yml/linux%20(netcode)?run=123");
    ok(await page.locator('#tree .trow.sel[data-key="ci:release.yml|job:linux"]').count() === 1, "route: a matrix job name selects the job");
    ok(FIXTURE ? /opened from run 123/.test(await page.textContent("#details")) : true, "route: ?run= shown in the details");
    await go("#fn/testWeapons");
    ok(await page.locator('#tree .trow.sel[data-key="fn:testWeapons"]').count() === 1 && /function testWeapons/.test(await page.textContent("#details")), "route: #fn/ selects the function");
    await go("#suite/ci:release.yml");
    ok(await page.locator('#tree .thead.sel[data-key="suite:ci:release.yml"]').count() === 1, "route: #suite/");
    await page.click('#tree .trow[data-key="quark_tests|group:maps"]');
    ok((await page.evaluate(() => location.hash)) === "#group/maps", "tree click sets the hash");
    await page.goBack(); await page.waitForTimeout(300);
    ok(await page.locator('#tree .thead.sel[data-key="suite:ci:release.yml"]').count() === 1, "back goes to the last selection");
    await page.goto(BASE + "/?embed=1#group/weapons");
    await page.waitForSelector("#tree .trow");
    ok(!(await page.isVisible(".qt-nav")) && !(await page.isVisible("#top .brand")), "?embed=1 hides the header");
    await page.fill("#search", "wallbang");
    await page.waitForTimeout(100);
    const hits = await page.$$eval("#tree .trow:not([hidden])", (r) => r.filter((x) => x.offsetParent).map((x) => x.dataset.key));
    ok(hits.includes("fn:testWallbang") && hits.includes("quark_tests|group:weapons") && hits.length < 10, "search finds the function and the groups that call it", hits.join());
    await page.fill("#search", "zzzznomatch");
    await page.waitForTimeout(100);
    ok(await page.$$eval("#tree .trow, #tree .thead, #tree .tsub", (r) => r.filter((x) => x.offsetParent).length) === 0, "search with no match shows no rows");
    await page.fill("#search", "");

    // ---- last local results in the tree, side panels, the code editor's Esc-then-Tab
    if (FIXTURE) {
        ok(await page.locator('#tree .trow[data-key="quark_tests|group:duel"] .tdot.cc-bad').count() === 1 && await page.locator('#tree .trow[data-key="quark_tests|group:weapons"] .tdot.cc-ok').count() === 1, "tree: each group's last local result");
        await go("#group/duel");
        ok(/Last local run/.test(await page.textContent("#details")) && await page.locator("#details .fline .link:has-text('tests/tests.cpp:120')").count() === 1, "details: the last run's FAIL line links to its CHECK");
    }
    const mw = () => page.$eval("#main", (e) => e.getBoundingClientRect().width);
    const w0 = await mw();
    await page.click("#leftToggle"); await page.waitForTimeout(80);
    const w1 = await mw();
    await page.click("#leftToggle"); await page.click("#rightToggle"); await page.waitForTimeout(80);
    const w2 = await mw();
    await page.click("#rightToggle");
    ok(w1 > w0 + 150 && w2 > w0 + 200 && Math.abs(await mw() - w0) < 2, "panels: hiding the tree / details gives the code their width", [w0, w1, w2].join());
    await go("#group/duel", "code");
    await page.focus(".code-ta");
    await page.keyboard.press("Escape"); await page.keyboard.press("Tab");
    ok(await page.evaluate(() => document.activeElement !== document.querySelector(".code-ta")), "code: Esc then Tab leaves the editor");
    await page.keyboard.press("Escape");

    // ---- a command that takes a NAME: the page asks for it
    if (FIXTURE) {
        await go("#cmd/rules");
        ok(/Without NAME, quark_tests rules runs the group rules/.test(await page.textContent("#details")) && await page.locator("#runArgs").count() === 1, "rules NAME: asked for, with the group warning");
        await page.click("#runBtn");
        ok(/takes 1 argument/.test(await page.textContent("#toast")), "rules NAME: refused without it");
        await page.fill("#runArgs", "overtime");
        await page.click("#runBtn");
        await page.waitForFunction(() => /passed/.test(document.querySelector("#runState").textContent), null, { timeout: 8000 });
        ok((await page.textContent("#runTitle")) === "quark_tests rules overtime", "rules NAME: run with it", await page.textContent("#runTitle"));
    }

    // ---- the whole suite: FAIL lines marked, jump to the next one, its file:line opens the CHECK
    await go("#suite/quark_tests");
    ok(/Run the whole suite/.test(await page.textContent("#runBtn")) && await page.locator("#details .gchip").count() === qt.items.filter((i) => i.kind === "group").length, "suite: whole-suite button and a chip per group");
    if (FIXTURE) {
        ok(/Rerun failed \(1\)/.test(await page.textContent("#details")), "suite: rerun failed counts the failed groups");
        await page.click("#runBtn");
        await page.waitForFunction(() => /failed/.test(document.querySelector("#runState").textContent), null, { timeout: 8000 });
        ok(await page.locator("#runOut .ofail").count() === 3 && await page.locator("#runOut .opass").count() === 0, "run output: the FAIL lines are marked", await page.locator("#runOut .ofail").count());
        await page.click("#runFails");
        ok(await page.locator("#runOut .ofail.cur").count() === 1 && /1\/3/.test(await page.textContent("#runFails")), "run output: jump to the first failure");
        await page.click("#runOut .olink");
        await page.waitForTimeout(300);
        ok((await page.textContent(".code-path")) === "tests/tests.cpp" && /lines 120-120/.test(await page.textContent(".code-bar")), "run output: the FAIL's file:line opens in Code");
        await go("#suite/quark_tests");
        await page.click("details.pick summary");
        await page.click('.gchip[data-g="duel"]');
        await page.click("details.pick .runbtn");
        await page.waitForFunction(() => /duel/.test(document.querySelector("#runTitle").textContent) && /failed/.test(document.querySelector("#runState").textContent), null, { timeout: 8000 });
        ok(/only=duel/.test(await page.textContent("#runOut")) && /quark_tests: duel/.test(await page.textContent("#runTitle")), "suite: a chosen set runs as QUARK_TEST_ONLY");
    }

    // ---- run
    await go("#group/weapons");
    await page.click("#runBtn");
    if (FIXTURE) {
        await page.waitForFunction(() => /passed/.test(document.querySelector("#runState").textContent), null, { timeout: 8000 });
        ok(/all tests passed/.test(await page.textContent("#runOut")), "run: output streamed, passed");
        await page.waitForTimeout(300);
        ok(await page.locator("#details .runh").count() >= 1, "run: history listed");
    } else {
        await page.waitForTimeout(1500);
        const t = (await page.textContent("#toast")) + (await page.textContent("#runState"));
        ok(/no build|running|passed|failed/.test(t), "run: started, or 'no build' said", t);
        if (/running/.test(t)) await page.click("#runStop");
    }
    ok(errors.length === 0, "no console errors", errors.join("\n"));
} catch (e) {
    fails++;
    console.log("FAIL (exception)", e.stack);
    await shot("failure.png").catch(() => {});
} finally {
    await restore().catch(() => {});
    await browser.close();
    if (fixture) fixture.kill();
}
console.log(`${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
