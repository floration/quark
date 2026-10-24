// node tools/tests_editor/web/test/patch_test.mjs: the block patches on the real files, with the fixture's model.
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";
import * as P from "../js/patch.js";
import { unified } from "../js/util.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, "../../../..");
const model = JSON.parse(execFileSync("python3", [path.join(here, "fixture_server.py"), "--dump"], { maxBuffer: 1 << 28 }).toString());
const isFn = (n) => !!model.functions[n];
const cpp = readFileSync(path.join(repo, "tests/tests.cpp"), "utf8");
const yml = readFileSync(path.join(repo, ".github/workflows/release.yml"), "utf8");
const qt = model.suites[0], ci = model.suites.find((s) => s.id === "ci:release.yml");
const G = (n) => qt.items.find((i) => i.name === n && i.kind === "group");
const J = (n) => ci.items.find((i) => i.jobId === n);
let fails = 0;
const ok = (c, msg, extra) => { if (!c) { fails++; console.log("FAIL", msg, extra || ""); } else console.log("ok  ", msg); };
const changed = (a, b) => unified(a, b, "f", 0).split("\n").filter((l) => /^[+-][^+-]/.test(l) || /^[+-]$/.test(l));

// group: the sequence round-trips unchanged
for (const g of qt.items.filter((i) => i.kind === "group")) {
    const s = P.entrySequence(cpp, g, isFn);
    ok(s && P.writeSequence(cpp, g, s.seq, isFn) === cpp, "round trip " + g.name);
    ok(s.seq.filter((x) => x.kind === "call").length === g.calls.length, "calls of " + g.name + " = model's", s.seq.filter((x) => x.kind === "call").length + " vs " + g.calls.length);
}
// reorder the weapons group: swap the first two calls
{
    const g = G("weapons"), s = P.entrySequence(cpp, g, isFn);
    const seq = s.seq.slice(); [seq[0], seq[1]] = [seq[1], seq[0]];
    const out = P.writeSequence(cpp, g, seq, isFn);
    const d = changed(cpp, out);
    ok(d.length === 2 && d[1].includes('{"weapons", [] { testWeapons(TP(28530)); testFixedSpread(TP(28555)); testRocketSelfDamage'), "swap two calls in weapons", d.join("\n"));
    const seq2 = s.seq.filter((x) => x.fn !== "testWallbang");
    seq2.push({ kind: "call", fn: "testLadders", args: "" });
    const out2 = P.writeSequence(cpp, g, seq2, isFn);
    ok(changed(cpp, out2)[1].includes("testTimeToKill(); testLadders(); }},"), "remove + add a call", changed(cpp, out2)[1]);
}
// multi-line group (maps): reorder keeps comments and the non-call code
{
    const g = G("maps"), s = P.entrySequence(cpp, g, isFn);
    const seq = s.seq.slice(); const a = seq.shift(); seq.splice(2, 0, a);
    const out = P.writeSequence(cpp, g, seq, isFn);
    ok(out.includes("// the map bar: every map scores") && out.split("\n").length === cpp.split("\n").length, "maps reorder keeps comment and line count");
}
// par -> alone and back
{
    const out = P.moveGroup(cpp, G("hidden"), "alone");
    const v = P.findVectors(out);
    const lines = out.split("\n");
    ok(/\{"hidden"/.test(lines[v.alone.end - 2]), "hidden moved to the end of alone");
    ok(changed(cpp, out).length === 2, "move = one line out, one in");
    const back = P.moveGroup(cpp, G("netcode"), "par", "bots");
    ok(/\{"netcode"/.test(back.split("\n")[P.findVectors(back).par.start]), "netcode moved to the top of par");
}
// shortcut command body
{
    const c = qt.items.find((i) => i.id === "cmd:kick");
    const s = P.entrySequence(cpp, c, isFn);
    ok(s && s.seq.length === 4 && s.seq[3].kind === "code", "kick shortcut: 3 calls + return", JSON.stringify(s && s.seq.map((x) => x.kind)));
}
// yaml: if / needs / runs-on
{
    const j = J("macos");
    let out = P.setJobIf(yml, j, "github.ref == 'refs/heads/testing'");
    let d = changed(yml, out);
    ok(d.length === 2 && d[1] === "+    if: github.ref == 'refs/heads/testing'   # (as windows)", "set if keeps the comment", d.join("\n"));
    out = P.setJobIf(yml, j, "");
    ok(changed(yml, out).length === 1, "remove if");
    out = P.setJobNeeds(yml, j, ["meta"]);
    d = changed(yml, out);
    ok(d[1] === "+    needs: [meta]", "needs list form", d.join("\n"));
    out = P.setJobNeeds(yml, J("meta"), ["lightcache"]);
    d = changed(yml, out);
    ok(d.length === 1 && d[0] === "+    needs: lightcache" && out.split("\n")[48] === "    needs: lightcache", "insert needs after the job line", d.join("\n"));
    out = P.setJobIf(yml, J("meta"), "!cancelled()");
    ok(changed(yml, out)[0] === '+    if: "!cancelled()"', "quote a leading !", changed(yml, out)[0]);
    out = P.setRunsOn(yml, j, "macos-14");
    ok(changed(yml, out)[1] === "+    runs-on: macos-14", "runs-on");
}
// yaml: steps
{
    const j = J("meta");
    const out = P.writeSteps(yml, j, [0, 2, 1, 3]);
    ok(out.split("\n").length === yml.split("\n").length, "reorder steps keeps line count");
    ok(out.indexOf("no keys, tokens") < out.indexOf("which channel this run publishes"), "step 2 now before step 1");
    ok(P.writeSteps(yml, j, [0, 1, 2, 3]) === yml, "steps identity");
    const add = P.writeSteps(yml, j, [0, 1, 2, 3, { name: "say hi", run: "echo hi" }]);
    const d = changed(yml, add);
    ok(d.join("\n") === "+      - name: say hi\n+        run: echo hi", "add a step", d.join("\n"));
    const del = P.writeSteps(yml, j, [0, 1, 3]);
    ok(!del.includes("tools/check_secrets.py\n") && del.includes("tools/provenance.py check"), "delete a step");
    let e = P.editStep(yml, j, 2, { name: "secrets check" });
    ok(changed(yml, e)[1] === "+      - name: secrets check", "rename a step", changed(yml, e).join("\n"));
    e = P.editStep(yml, j, 0, { name: "checkout" });
    ok(changed(yml, e).join("\n") === "-      - uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4.4.0\n+      - name: checkout\n+        uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4.4.0", "name a uses step", changed(yml, e).join("\n"));
    e = P.editStep(yml, j, 2, { run: "python3 -I tools/check_secrets.py --all" });
    ok(changed(yml, e)[1] === "+        run: python3 -I tools/check_secrets.py --all", "edit a one-line run");
    e = P.editStep(yml, j, 2, { run: "a\nb" });
    ok(changed(yml, e).join("\n") === "-        run: python3 -I tools/check_secrets.py\n+        run: |\n+          a\n+          b", "one-line run -> block", changed(yml, e).join("\n"));
    e = P.editStep(yml, j, 2, { if: "github.ref == 'refs/heads/testing'" });
    ok(changed(yml, e).join("\n") === "+        if: github.ref == 'refs/heads/testing'", "add a step if", changed(yml, e).join("\n"));
    const lin = J("linux");
    e = P.editStep(yml, lin, lin.steps.findIndex((s) => s.run && s.run.includes("exportassets")), { run: "echo replaced" });
    ok(!e.includes("exportassets build/assets_export") && e.split("\n").length < yml.split("\n").length, "replace a block run");
}
// a whole draft at once (edit + reorder + keys) = the same as one by one
{
    const j = J("windows");
    const out = P.applyJobDraft(yml, j, { steps: [1, 0, 2, 3, 4, { name: "x", run: "echo x" }], stepEdits: { 2: { name: "renamed", if: "always()" } }, if: "", needs: ["meta", "lightcache"], runsOn: "ubuntu-24.04" });
    const d = changed(yml, out);
    ok(out.includes("    needs: [meta, lightcache]") && !out.split("\n").slice(146, 152).some((l) => l.startsWith("    if:")) && out.includes("      - name: renamed\n        if: always()") && out.includes("      - name: x\n        run: echo x"), "job draft", d.join("\n"));
    ok(out.split("\n").slice(140, 200).join("\n").indexOf("download-artifact") < out.split("\n").slice(140, 200).join("\n").indexOf("actions/checkout"), "job draft reorder");
}
// group lists after the group grew a line
{
    const g = G("maps"), s = P.entrySequence(cpp, g, isFn);
    const grown = P.writeSequence(cpp, g, [...s.seq, { kind: "call", fn: "testLadders", args: "" }], isFn);
    ok(grown.split("\n").length === cpp.split("\n").length + 1, "multi-line group: a new call is a new line");
    const moved = P.moveGroup(grown, g, "alone", "netcode");
    const ent = P.listEntries(moved);
    ok(ent.alone[0].name === "maps" && ent.alone[0].to - ent.alone[0].from === 6 && !ent.par.some((e) => e.name === "maps"), "move the grown group to alone", JSON.stringify(ent.alone[0]));
    ok(P.writeLists(cpp, Object.fromEntries(Object.entries(P.listEntries(cpp)).map(([k, v]) => [k, v.map((e) => e.name)]))) === cpp, "writeLists identity");
}
// rules
{
    const j = J("linux");
    const add = P.writeSteps(yml, j, [...j.steps.map((_, i) => i), { name: "tests", run: "build/quark_tests group weapons" }]);
    ok(P.ruleHits(".github/workflows/release.yml", yml, add, model).some((h) => h.id === "ci-scope"), "ci-scope hit for a gameplay group");
    const re = P.writeSteps(yml, j, [...j.steps.map((_, i) => i)].reverse());
    ok(!P.ruleHits(".github/workflows/release.yml", yml, re, model).length, "reorder: no rule hit");
    const fin = P.setJobIf(yml, j, "contains(github.event.head_commit.message, '[final]')");
    ok(P.ruleHits(".github/workflows/release.yml", yml, fin, model).some((h) => h.id === "final"), "final hit");
}
console.log(fails ? fails + " FAILED" : "all patch tests passed");
process.exit(fails ? 1 : 0);
