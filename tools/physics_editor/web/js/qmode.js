// .qmode text (CustomRules::toText / fromText, src/game/customrules.cpp): key=value lines. The physics editor reads
// and writes ONLY the movement keys (gravity, speed, jump, dash, grapple, move_*); every other line is kept as it is,
// and from a `logic` line to the end of the file is the gamemode editor's section, never touched. mergeQmode here
// is the same merge serve.py does on save (merge_qmode), so the Code tab shows exactly what a save writes.

import { standardPhysics, SWITCHES } from "./params.js";
import { check as checkExpr, highlightExpr } from "./expr.js";

export const TOP_KEYS = ["gravity", "speed", "jump", "dash", "grapple"];
// moveParamKey (gamedefs.h), in order: the order expr.<key> lines are written in (after gravity, speed, jump)
export const MV_KEYS = ["crouch_speed", "ground_accel", "friction", "stop_speed", "air_accel", "air_cap", "overstrafe", "air_control",
    "step", "dash_speed", "dash_time", "dash_exit_min", "dash_keep", "dash_exit_max", "dash_vertical", "dash_charges",
    "dash_cooldown", "bounce_gain", "bounce_max", "hook_range", "hook_accel", "hook_max", "hook_gravity", "hook_release",
    "hook_cooldown", "ladder_speed", "ladder_side", "dash_jump_window", "super_dash_boost", "super_dash_jump",
    "super_dash_max", "dash_bounce_angle", "air_drift", "strafe_boost"];
const EXPR_KEYS = ["gravity", "speed", "jump", ...MV_KEYS];
const isMoveKey = (k) => TOP_KEYS.includes(k) || k.startsWith("move_") || k.startsWith("expr.");
// "# imported from <game> (<id>): <keys>": the set's record of values taken from another game (Values › Compare with
// games). A comment to the game (no '='); the physics editor writes and reads it like a movement line.
export const IMPORT_PREFIX = "# imported from ";
export const isImportLine = (l) => l.replace(/\r$/, "").startsWith(IMPORT_PREFIX);
const clean = (s) => String(s).replace(/[=\r\n]/g, " ").replace(/[()]/g, "").trim();
export function importLines(ph) {
    return (ph.imports || []).filter((i) => i && i.id && i.keys && i.keys.length)
        .map((i) => `${IMPORT_PREFIX}${clean(i.name || i.id)} (${clean(i.id)}): ${i.keys.map(clean).join(", ")}`);
}
const BOOL_KEYS = new Set(["dash", "grapple"]);
// the non-movement keys fromText knows (kept, not edited here; the gamemode editor's): customrules.cpp, and
// weaponset= / wsline= (readWeaponSetRulesLine, weaponset.cpp)
export const OTHER_KEYS = new Set(["custommode", "mode", "weapons", "knockback", "health", "selfdamage", "instagib", "vampire", "regen",
    "timelimit", "scorelimit", "rounds", "weapon", "maps", "label", "weaponset", "wsline"]);

// customrules.cpp isModeLogicLine, exactly: "logic" at column 0, then the end of the line, a space, a tab or a CR
// (so "logic=x", "logic:" and " logic" are ordinary lines to the game, and so here)
export function isLogicLine(line) {
    return line.startsWith("logic") && (line.length === 5 || line[5] === " " || line[5] === "\t" || line[5] === "\r");
}

// C++ `ostream << float` (6 significant digits, %g)
export function fmtNum(v) {
    v = +v;
    if (!Number.isFinite(v)) return "1";
    let s = (+v.toPrecision(6)).toString();
    if (s.includes("e")) s = v.toFixed(6).replace(/0+$/, "").replace(/\.$/, "");
    return s === "-0" ? "0" : s;
}

// C's sscanf("%f"): the longest number at the start (after spaces), else none
function scanf(v) {
    const m = /^\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?|[+-]?(?:inf(?:inity)?|nan))/i.exec(v);
    if (!m) return null;
    const t = m[1].toLowerCase().replace(/^\+/, "");
    if (t.includes("nan")) return NaN;
    if (t.includes("inf")) return t.startsWith("-") ? -Infinity : Infinity;
    return parseFloat(m[1]);
}

// Parse: the physics the game would read (fromText's clamps), and every problem at its line.
//   {physics, name, mode, problems: [{line, level: "error"|"warning"|"info", msg}], logicLine (1-based or 0), lines}
export function parseQmode(text, cat) {
    const ph = standardPhysics(cat);
    const problems = [];
    const lines = text.split("\n");
    let name = "", mode = "", logicLine = 0;
    const seen = new Map();
    const lim = (key) => {
        const p = cat && cat.byKey.get(key);
        if (p) return [p.min, p.max];
        return key === "gravity" ? [0.1, 3] : key === "speed" || key === "jump" ? [0.25, 3] : [0.1, 5];
    };
    const keySet = cat && (cat.keySet || (cat.keySet = new Set(cat.all.map((p) => p.key))));
    for (let i = 0; i < lines.length; i++) {
        let line = lines[i];
        if (line.endsWith("\r")) line = line.slice(0, -1);
        if (isLogicLine(line)) { logicLine = i + 1; break; }
        if (line.startsWith(IMPORT_PREFIX)) {
            const m = /^# imported from (.*) \(([^()]*)\): ?(.*)$/.exec(line);
            if (m) (ph.imports || (ph.imports = [])).push({ name: m[1], id: m[2], keys: m[3].split(",").map((x) => x.trim()).filter(Boolean) });
            continue;
        }
        const eq = line.indexOf("=");
        if (eq < 0) {
            if (line.trim() && !line.trim().startsWith("#")) problems.push({ line: i + 1, level: "warning", msg: `no '=': the game skips this line` });
            continue;
        }
        const k = line.slice(0, eq), v = line.slice(eq + 1);
        if (k !== k.trim()) problems.push({ line: i + 1, level: "error", msg: `'${k}': spaces around the key, the game won't read it` });
        if (seen.has(k) && isMoveKey(k)) problems.push({ line: i + 1, level: "warning", msg: `${k} again (line ${seen.get(k)} is ignored: the last one counts)` });
        seen.set(k, i + 1);
        if (k === "custommode") { name = v; continue; }
        if (k === "mode") { mode = v; continue; }
        if (k.startsWith("expr.")) {
            const key = k.slice(5);
            if (!EXPR_KEYS.includes(key) || (keySet && !keySet.has(key))) { problems.push({ line: i + 1, level: "warning", msg: `expr.${key}: no such setting, the game ignores it` }); continue; }
            const text = v.trim();
            (ph.expr || (ph.expr = {}))[key] = text;
            if (keySet) {
                const c = checkExpr(text, keySet);
                if (!c.ok) problems.push({ line: i + 1, level: "error", msg: `expr.${key}: ${c.error.msg} (the game keeps the fixed value)` });
            }
            continue;
        }
        const isMove = k.startsWith("move_");
        if (!TOP_KEYS.includes(k) && !isMove) {
            if (!OTHER_KEYS.has(k)) problems.push({ line: i + 1, level: "warning", msg: `unknown key '${k}': the game ignores it` });
            continue;
        }
        if (BOOL_KEYS.has(k)) {
            const n = parseInt(v, 10);
            ph[k] = (Number.isNaN(n) ? 0 : n) !== 0;
            if (!/^\s*[01]\s*$/.test(v)) problems.push({ line: i + 1, level: "warning", msg: `${k}: 1 (on) or 0 (off); read as ${ph[k] ? "on" : "off"}` });
            continue;
        }
        const key = isMove ? k.slice(5) : k;
        // only moveParamKey names count after move_ (customrules.cpp): move_gravity / move_speed... are ignored by the
        // game, so they stay out of physics.move (the helper would refuse the set)
        if (isMove && !(cat ? cat.byKey.has(key) && !cat.byKey.get(key).top : MV_KEYS.includes(key))) {
            const top = TOP_KEYS.includes(key);
            problems.push({ line: i + 1, level: "warning", msg: `unknown movement key '${key}': the game ignores it${top ? ` (the setting is the plain ${key}= line)` : ""}` });
            continue;
        }
        let f = scanf(v);
        if (f === null) { problems.push({ line: i + 1, level: "error", msg: `${k}: '${v.trim()}' is not a number (the game reads 0, then clamps it)` }); f = 0; }
        const [a, b] = lim(key);
        const def = isMove ? ph.move[key] : ph[key];
        let x = !Number.isFinite(f) ? def : f < a ? a : f > b ? b : f;
        if (Number.isFinite(f) && (f < a || f > b)) problems.push({ line: i + 1, level: "warning", msg: `${k}=${fmtNum(f)} is outside ${a}..${b}: the game uses ${fmtNum(x)}` });
        if (!Number.isFinite(f)) problems.push({ line: i + 1, level: "warning", msg: `${k}: not a finite number, the game keeps ${fmtNum(def)}` });
        x = Math.fround(x);
        x = +x.toPrecision(7);
        if (isMove) ph.move[key] = x; else ph[key] = x;
    }
    if (!name.trim()) problems.push({ line: 1, level: "error", msg: "no custommode=<name> line: the game doesn't read the file as a custom mode" });
    return { physics: ph, name, mode, problems, logicLine, lines };
}

function movementLines(ph) {
    const out = [];
    for (const k of TOP_KEYS) out.push([k, BOOL_KEYS.has(k) ? (ph[k] !== false ? "1" : "0") : fmtNum(ph[k] ?? 1)]);
    for (const [k, v] of Object.entries(ph.move || {})) if (/^[a-z_]{1,32}$/.test(k) && +v !== 1 && Number.isFinite(+v)) out.push(["move_" + k, fmtNum(v)]);
    // expr.<key> lines (v2), gravity / speed / jump first, then moveParamKey order, then any others by name
    const ex = ph.expr || {};
    const rank = (k) => { const i = EXPR_KEYS.indexOf(k); return i < 0 ? 1000 : i; };
    const ek = Object.keys(ex).filter((k) => /^[a-z_]{1,32}$/.test(k) && typeof ex[k] === "string" && ex[k].trim())
        .sort((a, b) => rank(a) - rank(b) || (a < b ? -1 : a > b ? 1 : 0));
    for (const k of ek) out.push(["expr." + k, ex[k].replace(/[\r\n]+/g, " ").trim()]);
    return out;
}

// serve.py merge_qmode, line for line (tests check both give the same text)
export function mergeQmode(text, ph, name, knownMove) {
    const want = movementLines(ph);
    const imps = importLines(ph);
    if (!text) return `custommode=${name || "physics"}\nmode=ffa\n` + imps.map((l) => l + "\n").join("") + want.map(([k, v]) => `${k}=${v}\n`).join("");
    let lines = text.split("\n");
    const trailing = lines[lines.length - 1] === "";
    if (trailing) lines = lines.slice(0, -1);
    const cr = (l) => (l.endsWith("\r") ? "\r" : "");
    let cut = lines.findIndex(isLogicLine);
    if (cut < 0) cut = lines.length;
    const head = lines.slice(0, cut), tail = lines.slice(cut);
    const keyof = (l) => (l.includes("=") ? l.slice(0, l.indexOf("=")) : null);
    const wantd = new Map(want);
    const last = new Map();
    head.forEach((l, i) => { const k = keyof(l); if (k !== null) last.set(k, i); });
    const out = [], seen = new Set();
    const known = knownMove || new Set(Object.keys(ph.move || {}));
    let impsPlaced = false;
    head.forEach((l, i) => {
        const k = keyof(l);
        if (isImportLine(l)) {   // (the import record: rewritten where its first line was)
            if (!impsPlaced) { for (const x of imps) out.push(x + cr(l)); impsPlaced = true; }
            return;
        }
        if (k === null || !isMoveKey(k) || last.get(k) !== i) out.push(l);
        else if (wantd.has(k)) { out.push(`${k}=${wantd.get(k)}${cr(l)}`); seen.add(k); }
        else if (k.startsWith("move_") && known.has(k.slice(5))) { /* a move_ key back at 1: dropped */ }
        else if (k.startsWith("expr.") && EXPR_KEYS.includes(k.slice(5))) { /* an expression removed: dropped */ }
        else out.push(l);
    });
    const idxOf = (keys) => {
        for (const kk of keys) for (let j = out.length - 1; j >= 0; j--) if (keyof(out[j]) === kk) return j;
        return null;
    };
    for (const [k, v] of want) {
        if (seen.has(k)) continue;
        if (TOP_KEYS.includes(k) && v === "1") continue;   // (a missing top key is already 1 / on: not added)
        let j = null;
        if (k.startsWith("expr.")) {   // (after the setting's own line, else after the last movement line)
            const sk = k.slice(5);
            j = idxOf([TOP_KEYS.includes(sk) ? sk : "move_" + sk]);
            if (j === null) for (let jj = out.length - 1; jj >= 0; jj--) { const kk = keyof(out[jj]) || ""; if (kk.startsWith("move_") || kk.startsWith("expr.")) { j = jj; break; } }
            if (j === null) j = idxOf(["rounds", "scorelimit", "timelimit", "regen", "grapple", "dash", "jump", "speed", "gravity"]);
        } else if (k.startsWith("move_")) {
            for (let jj = out.length - 1; jj >= 0; jj--) if ((keyof(out[jj]) || "").startsWith("move_")) { j = jj; break; }
            if (j === null) j = idxOf(["rounds", "scorelimit", "timelimit", "regen", "grapple", "dash", "jump", "speed", "gravity"]);
        } else if (k === "gravity" || k === "speed" || k === "jump") {
            const order = { gravity: [], speed: ["gravity"], jump: ["speed", "gravity"] }[k];
            j = idxOf([...order, "weapons", "mode", "custommode"]);
        } else {
            j = idxOf([...(k === "grapple" ? ["dash"] : []), "health", "knockback", "jump", "speed", "gravity", "weapons", "mode", "custommode"]);
        }
        if (j === null) { j = -1; out.forEach((l, jj) => { if (keyof(l) !== null) j = jj; }); }
        out.splice(j + 1, 0, `${k}=${v}${j >= 0 ? cr(out[j]) : cr(text.split("\n", 1)[0])}`);
    }
    if (!impsPlaced && imps.length) {   // (after the mode= line, else the custommode= line, else at the top)
        let j = -1;
        for (const kk of ["mode", "custommode"]) { for (let jj = out.length - 1; jj >= 0; jj--) if (keyof(out[jj]) === kk) { j = jj; break; } if (j >= 0) break; }
        const c = j >= 0 ? cr(out[j]) : cr(text.split("\n", 1)[0]);
        out.splice(j + 1, 0, ...imps.map((x) => x + c));
    }
    return out.concat(tail).join("\n") + (trailing ? "\n" : "");
}

// Tidy (the Code tab): the movement lines the way CustomRules::toText writes them. Each movement key the game knows
// keeps one line (the last one: the one the game reads; the ignored earlier ones go), gravity / speed / jump / dash /
// grapple where their last line was, and the move_ lines then the expr. lines as one block in toText's order after
// rounds= (else the nearest key toText writes before it). Movement keys this build doesn't know (the game keeps them as
// they are), other lines and the logic section stay where they are. Parses back to the same physics.
export function tidyQmode(text, ph, name, knownMove) {
    if (!text) return mergeQmode(text, ph, name, knownMove);
    let lines = text.split("\n");
    const trailing = lines[lines.length - 1] === "";
    if (trailing) lines = lines.slice(0, -1);
    let cut = lines.findIndex(isLogicLine);
    if (cut < 0) cut = lines.length;
    const head = lines.slice(0, cut), tail = lines.slice(cut);
    const keyof = (l) => (l.includes("=") ? l.slice(0, l.indexOf("=")) : null);
    const known = knownMove || new Set(MV_KEYS);
    const isKnown = (k) => TOP_KEYS.includes(k) || (k.startsWith("move_") && known.has(k.slice(5))) || (k.startsWith("expr.") && EXPR_KEYS.includes(k.slice(5)));
    const last = new Map();
    head.forEach((l, i) => { const k = keyof(l); if (k !== null) last.set(k, i); });
    // the top keys' last lines and every line that isn't a known movement line; then the top keys and the import record
    // written by the ordinary merge (the set without its move_ / expr. values: those come next, as a block)
    const kept = head.filter((l, i) => { const k = keyof(l); return k === null || !isKnown(k) || (TOP_KEYS.includes(k) && last.get(k) === i); });
    const topOnly = { ...ph, move: {}, expr: {} };
    const out = mergeQmode(kept.join("\n") + "\n", topOnly, name, known).split("\n").slice(0, -1);
    const rank = (k) => { const i = MV_KEYS.indexOf(k.slice(5)); return i < 0 ? 1000 : i; };
    const block = movementLines(ph).filter(([k]) => !TOP_KEYS.includes(k))
        .sort((a, b) => (a[0].startsWith("expr.") - b[0].startsWith("expr.")) || (a[0].startsWith("move_") ? rank(a[0]) - rank(b[0]) : 0));
    if (block.length) {
        let j = -1;
        for (const kk of ["rounds", "scorelimit", "timelimit", "regen", "vampire", "instagib", "selfdamage", "grapple", "dash", "health", "knockback", "jump", "speed", "gravity", "weapons", "mode", "custommode"]) {
            if (j >= 0) break;
            for (let jj = out.length - 1; jj >= 0; jj--) if (keyof(out[jj]) === kk) { j = jj; break; }
        }
        if (j < 0) out.forEach((l, jj) => { if (keyof(l) !== null) j = jj; });
        const c = j >= 0 ? (out[j].endsWith("\r") ? "\r" : "") : (text.split("\n", 1)[0].endsWith("\r") ? "\r" : "");
        out.splice(j + 1, 0, ...block.map(([k, v]) => `${k}=${v}${c}`));
    }
    return out.concat(tail).join("\n") + (trailing ? "\n" : "");
}

// Which lines of the text are movement lines (editable here), other lines (kept), or the logic section (read only)
export function lineKinds(text) {
    const lines = text.split("\n");
    const out = [];
    let logic = false;
    for (const raw of lines) {
        const l = raw.endsWith("\r") ? raw.slice(0, -1) : raw;
        if (!logic && isLogicLine(l)) logic = true;
        if (logic) { out.push("logic"); continue; }
        if (l.startsWith(IMPORT_PREFIX)) { out.push("import"); continue; }
        const eq = l.indexOf("=");
        const k = eq >= 0 ? l.slice(0, eq) : "";
        out.push(isMoveKey(k) ? (k.startsWith("expr.") ? "expr" : "move") : !l.trim() ? "blank" : l.trim().startsWith("#") ? "comment" : "other");
    }
    return out;
}

export function escapeHtml(s) { return s.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[c]); }

// highlighted HTML for the code view (one span per line part)
export function highlight(text, keySet) {
    const kinds = lineKinds(text);
    return text.split("\n").map((l, i) => {
        const kind = kinds[i];
        if (kind === "logic") return `<span class="hlog">${escapeHtml(l)}</span>`;
        if (kind === "comment") return `<span class="hcm">${escapeHtml(l)}</span>`;
        if (kind === "import") return `<span class="himp">${escapeHtml(l)}</span>`;
        if (kind === "expr") {
            const eq = l.indexOf("="), v = l.slice(eq + 1).replace(/\r$/, "");
            return `<span class="hx">${escapeHtml(l.slice(0, eq))}</span><span class="hp">=</span>${highlightExpr(v, keySet)}${l.endsWith("\r") ? "\r" : ""}`;
        }
        const eq = l.indexOf("=");
        if (eq < 0) return escapeHtml(l);
        const k = l.slice(0, eq), v = l.slice(eq + 1);
        const kc = kind === "move" ? "hk" : "ho";
        return `<span class="${kc}">${escapeHtml(k)}</span><span class="hp">=</span><span class="${kind === "move" ? "hn" : "hov"}">${escapeHtml(v)}</span>`;
    }).join("\n");
}

export const SWITCH_KEYS = SWITCHES.map((s) => s.key);
