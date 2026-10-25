// Does the physics editor still read the game's movement code? (node 18+, nothing else needed)
//
//   node tools/physics_editor/web/test/source_check.mjs [path/to/pmove.cpp]
//
// The Code tab's Movement code view and the Code graph find pmoveStep and its steps in src/game/pmove.cpp by brace
// matching and anchors (web/js/source.js). A game-code change can move an anchor; this check fails (exit 1, one line
// per problem) when pmoveStep, a step of it or a function with a note can't be found, so the change is caught before
// a release instead of showing an empty graph. It also runs the parser's own cases (a body far longer than pmoveStep,
// braces in strings and comments, overloads). tools/physics_editor/web/test/run.mjs runs it too.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const TOOL = path.resolve(HERE, "../..");
const S = await import(pathToFileURL(path.join(TOOL, "web/js/source.js")).href);

// the parser's own cases: [what, passed, detail]
export function parserCases() {
    const out = [];
    // a function far past any old line limit (pmoveStep was cut off at 400 lines in 1.18.32), with braces in a string,
    // a char literal and a block comment inside it
    const body = Array.from({ length: 5000 }, (_, i) => `    x += ${i};`).join("\n");
    const big = `static int helper(int a) { return a; }\nvoid pmoveStep(MoveState& s) {\n    const char* t = "{{ not a brace";\n    char c = '}';\n    /* a commented-out block {\n    } */\n${body}\n    if (s.x) { s.y = 1; }\n}\nint after() { return 1; }\n`;
    const fns = S.findFunctions(big), f = fns.find((x) => x.name === "pmoveStep");
    const lines = big.split("\n").length;
    out.push(["a 5000-line function is found to its closing brace", !!f && f.end === lines - 2, f ? `${f.start}-${f.end} of ${lines}` : "not found"]);
    out.push(["the functions after it are found too", fns.some((x) => x.name === "after" && x.start === lines - 1), fns.map((x) => x.name).join(" ")]);
    // overloads get their parameter names
    const ov = S.findFunctions("int dash(uint8_t mods) { return 1; }\nint dash(const MoveState& s, const Map* map) { return 2; }\nint one() { return 3; }\n");
    out.push(["overloads are told apart", ov.map((x) => x.title).join(" ") === "dash(mods) dash(s, map) one()", ov.map((x) => x.title).join(" ")]);
    // a missing step and a missing pmoveStep are problems, not silence
    out.push(["no pmoveStep is a problem", S.sourceProblems([], []).some((p) => /pmoveStep/.test(p)), ""]);
    return out;
}

// what the parser can't find in this pmove.cpp text: [] when everything is there
export function checkSource(text) {
    const fns = S.findFunctions(text), steps = S.findSteps(text, fns);
    return { fns, steps, problems: S.sourceProblems(fns, steps) };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const file = process.argv[2] || [path.resolve(TOOL, "../../src/game/pmove.cpp"), path.join(TOOL, "source/pmove.cpp")].find((p) => fs.existsSync(p));
    let bad = 0;
    for (const [what, pass, detail] of parserCases()) { if (!pass) bad++; console.log(`${pass ? "  ok  " : "  FAIL"} ${what}${detail && !pass ? "  " + detail : ""}`); }
    if (!file || !fs.existsSync(file)) { console.log("  FAIL pmove.cpp not found (pass its path)"); process.exit(1); }
    const r = checkSource(fs.readFileSync(file, "utf8"));
    for (const p of r.problems) { bad++; console.log("  FAIL " + p); }
    console.log(`${file}: ${r.fns.length} functions, pmoveStep ${(r.fns.find((f) => f.name === "pmoveStep") || {}).start ? "found" : "NOT found"}, ${r.steps.length} of ${S.STEPS.length} steps`);
    console.log(bad ? `physics editor source check: ${bad} problem(s): update tools/physics_editor/web/js/source.js` : "physics editor source check: ok");
    process.exit(bad ? 1 : 0);
}
