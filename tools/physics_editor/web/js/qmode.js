// .qmode text (CustomRules::toText / fromText, src/game/customrules.cpp): key=value lines. The physics editor reads
// and writes ONLY the movement keys (gravity, speed, jump, dash, grapple, move_*); every other line is kept as it is,
// and from a `logic` line to the end of the file is the gamemode editor's section, never touched. mergeQmode here
// is the same merge serve.py does on save (merge_qmode), so the Code tab shows exactly what a save writes.

import { standardPhysics, SWITCHES } from "./params.js";

export const TOP_KEYS = ["gravity", "speed", "jump", "dash", "grapple"];
const BOOL_KEYS = new Set(["dash", "grapple"]);
// the non-movement keys fromText knows (kept, not edited here; the gamemode editor's)
export const OTHER_KEYS = new Set(["custommode", "mode", "weapons", "knockback", "health", "selfdamage", "instagib", "vampire", "regen",
    "timelimit", "scorelimit", "rounds", "weapon", "maps"]);

export function isLogicLine(line) {
    const s = line.trim();
    return s === "logic" || s.startsWith("logic=") || s.startsWith("logic ") || s.startsWith("logic:");
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
    for (let i = 0; i < lines.length; i++) {
        let line = lines[i];
        if (line.endsWith("\r")) line = line.slice(0, -1);
        if (isLogicLine(line)) { logicLine = i + 1; break; }
        const eq = line.indexOf("=");
        if (eq < 0) {
            if (line.trim() && !line.trim().startsWith("#")) problems.push({ line: i + 1, level: "warning", msg: `no '=': the game skips this line` });
            continue;
        }
        const k = line.slice(0, eq), v = line.slice(eq + 1);
        if (k !== k.trim()) problems.push({ line: i + 1, level: "error", msg: `'${k}': spaces around the key, the game won't read it` });
        if (seen.has(k) && (TOP_KEYS.includes(k) || k.startsWith("move_"))) problems.push({ line: i + 1, level: "warning", msg: `${k} again (line ${seen.get(k)} is ignored: the last one counts)` });
        seen.set(k, i + 1);
        if (k === "custommode") { name = v; continue; }
        if (k === "mode") { mode = v; continue; }
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
        if (isMove && cat && !cat.byKey.has(key)) { problems.push({ line: i + 1, level: "warning", msg: `unknown movement key '${key}': the game ignores it` }); continue; }
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
    return out;
}

// serve.py merge_qmode, line for line (tests check both give the same text)
export function mergeQmode(text, ph, name, knownMove) {
    const want = movementLines(ph);
    if (!text) return `custommode=${name || "physics"}\nmode=ffa\n` + want.map(([k, v]) => `${k}=${v}\n`).join("");
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
    head.forEach((l, i) => {
        const k = keyof(l);
        if (k === null || !(TOP_KEYS.includes(k) || k.startsWith("move_")) || last.get(k) !== i) out.push(l);
        else if (wantd.has(k)) { out.push(`${k}=${wantd.get(k)}${cr(l)}`); seen.add(k); }
        else if (known.has(k.slice(5))) { /* a move_ key back at 1: dropped */ }
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
        if (k.startsWith("move_")) {
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
        const eq = l.indexOf("=");
        const k = eq >= 0 ? l.slice(0, eq) : "";
        out.push(TOP_KEYS.includes(k) || k.startsWith("move_") ? "move" : !l.trim() ? "blank" : l.trim().startsWith("#") ? "comment" : "other");
    }
    return out;
}

export function escapeHtml(s) { return s.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[c]); }

// highlighted HTML for the code view (one span per line part)
export function highlight(text) {
    const kinds = lineKinds(text);
    return text.split("\n").map((l, i) => {
        const kind = kinds[i];
        if (kind === "logic") return `<span class="hlog">${escapeHtml(l)}</span>`;
        if (kind === "comment") return `<span class="hcm">${escapeHtml(l)}</span>`;
        const eq = l.indexOf("=");
        if (eq < 0) return escapeHtml(l);
        const k = l.slice(0, eq), v = l.slice(eq + 1);
        const kc = kind === "move" ? "hk" : "ho";
        return `<span class="${kc}">${escapeHtml(k)}</span><span class="hp">=</span><span class="${kind === "move" ? "hn" : "hov"}">${escapeHtml(v)}</span>`;
    }).join("\n");
}

export const SWITCH_KEYS = SWITCHES.map((s) => s.key);
