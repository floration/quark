// The .qmode file as a whole (design 2.1): the game's key=value lines (CustomRules::toText, customrules.cpp),
// then the logic block from the first line that is exactly `logic <N>` to the end.
//
// The shared file rule (agreed with the physics editor): each editor rewrites only the lines it owns and keeps every
// other line exactly as written, in place. This editor owns the mode keys (custommode, mode, timelimit, scorelimit,
// rounds, maps) and the whole logic block; the physics editor owns the movement keys. On save an owned key's value
// is replaced where it is, a missing one is added at the end of the key=value section (just above `logic`), nothing
// is reordered and no key=value line is ever written after `logic`. serve.py re-checks all of this, and refuses a
// save over a file that changed on disk since it was loaded.

export const MODE_KEYS = ["custommode", "mode", "timelimit", "scorelimit", "rounds", "maps"];
export const PHYSICS_KEYS = ["gravity", "speed", "jump", "knockback", "dash", "grapple", "selfdamage"];
export const PHYSICS_PREFIXES = ["move_", "move."];
export const UNDECIDED_KEYS = ["weapons", "weapon", "health", "instagib", "vampire", "regen"];   // design 7: ask the owner
export const GAME_KEYS = new Set([...MODE_KEYS, ...PHYSICS_KEYS, ...UNDECIDED_KEYS, "label"]);
export const PHYSICS_EDITOR_URL = "http://127.0.0.1:8772/";
const LOGIC_LINE = /^logic [0-9]+[ \t]*$/;

export function ownerOf(key) {
    if (key == null) return "none";
    if (MODE_KEYS.includes(key)) return "mode";
    if (PHYSICS_KEYS.includes(key) || PHYSICS_PREFIXES.some((p) => key.startsWith(p))) return "physics";
    if (UNDECIDED_KEYS.includes(key)) return "undecided";
    if (key === "label") return "game";
    return "unknown";
}
export const OWNER_TEXT = {
    mode: "this editor", physics: "the physics editor", undecided: "owner not decided yet (kept as written)",
    game: "written by the game (network label)", unknown: "not a key the game knows (kept as written)", none: "not a key line (kept as written)",
};

// A line as the game reads it (fromText): the key is everything before the first '=', a trailing \r is dropped.
function keyOf(raw) {
    const line = raw.endsWith("\r") ? raw.slice(0, -1) : raw;
    const eq = line.indexOf("=");
    if (eq < 0) return { key: null, value: null };
    return { key: line.slice(0, eq), value: line.slice(eq + 1) };
}

// -> {lines: [{raw, key, value, owner, n (1-based line)}], logicText (from the logic line, "" if none), logicLine
//     (1-based line of `logic N` or 0), eol, finalNewline}
export function splitQmode(text) {
    const eol = /\r\n/.test(text) ? "\r\n" : "\n";
    const finalNewline = text === "" || text.endsWith("\n");
    const raws = text.split("\n");
    if (finalNewline) raws.pop();
    const lines = [];
    let logicIx = -1;
    for (let i = 0; i < raws.length; i++) {
        const r = raws[i];
        const bare = r.endsWith("\r") ? r.slice(0, -1) : r;
        if (LOGIC_LINE.test(bare)) { logicIx = i; break; }
        const { key, value } = keyOf(r);
        lines.push({ raw: r, key, value, owner: ownerOf(key), n: i + 1 });
    }
    const logicText = logicIx < 0 ? "" : raws.slice(logicIx).map((r) => (r.endsWith("\r") ? r.slice(0, -1) : r)).join("\n") + "\n";
    return { lines, logicText, logicLine: logicIx < 0 ? 0 : logicIx + 1, eol, finalNewline };
}

// the game's reading of the keys (the last line of a key wins, like fromText)
export function keysOf(text) {
    const m = new Map();
    for (const l of splitQmode(text).lines) if (l.key !== null) m.set(l.key, l.value);
    return m;
}
// the lines this editor must never change, in order (also checked by serve.py)
export function foreignLines(text) {
    return splitQmode(text).lines.filter((l) => l.owner !== "mode").map((l) => l.raw.replace(/\r$/, ""));
}

// The text a new mode starts from: CustomRules() written by toText (customrules.cpp:66-79), every key in its order.
export function newQmodeText(name, base) {
    const L = [`custommode=${name}`, `mode=${base}`, "weapons=0", "gravity=1", "speed=1", "jump=1", "knockback=1", "health=100", "dash=1",
        "grapple=1", "selfdamage=1", "instagib=0", "vampire=0", "regen=0", "timelimit=0", "scorelimit=0", "rounds=0"];
    for (let w = 0; w < 11; w++) L.push(`weapon=${w},1,1,1`);
    L.push("maps=");
    return L.join("\n") + "\n";
}

// A logic line the OLD game would read as a key (it parses every line): refuse to write it.
export function logicKeyClash(logicText) {
    for (const [i, l] of logicText.split("\n").entries()) {
        const { key } = keyOf(l);
        if (key !== null && (GAME_KEYS.has(key) || PHYSICS_PREFIXES.some((p) => key.startsWith(p)))) return { line: i + 1, key };
    }
    return null;
}

// The new file text: `original` with the owned keys in `set` (key -> value string; null = leave as is) patched in
// place, missing ones added just above `logic`, and the logic block replaced by `logicText` ("" = no logic block).
export function patchQmode(original, set, logicText) {
    const sp = splitQmode(original);
    const eol = sp.eol;
    for (const k of Object.keys(set)) if (ownerOf(k) !== "mode") throw new Error(`'${k}' isn't a key this editor owns`);
    const out = [];
    const seen = new Set();
    for (const l of sp.lines) {
        if (l.owner === "mode" && set[l.key] != null && String(set[l.key]) !== l.value) {
            const cr = l.raw.endsWith("\r") ? "\r" : "";
            out.push(`${l.key}=${set[l.key]}${cr}`);
        } else out.push(l.raw);
        if (l.key !== null) seen.add(l.key);
    }
    for (const k of MODE_KEYS) if (set[k] != null && !seen.has(k)) out.push(`${k}=${set[k]}${eol === "\r\n" ? "\r" : ""}`);
    let text = out.join("\n");
    if (out.length) text += "\n";
    if (logicText) {
        const clash = logicKeyClash(logicText);
        if (clash) throw new Error(`logic line ${clash.line} would read as the key '${clash.key}' in older games`);
        const body = logicText.endsWith("\n") ? logicText : logicText + "\n";
        text += eol === "\r\n" ? body.replace(/\n/g, "\r\n") : body;
    }
    return text;
}

// the mode keys as an object (missing = undefined)
export function modeKeys(text) {
    const k = keysOf(text);
    const o = {};
    for (const n of MODE_KEYS) if (k.has(n)) o[n] = k.get(n);
    return o;
}
// the game's modeFromName (case-insensitive) -> the canonical name, or null
export function modeFromName(v, modes) {
    const s = String(v || "").trim().toLowerCase();
    return modes.find((m) => m === s) || null;
}
