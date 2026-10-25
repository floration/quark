// The game's movement code (src/game/pmove.cpp, pmove.h, gamedefs.h, served read-only by serve.py at /api/source),
// read the way the Code tab and the Code graph need it: every constant and its value, every place a custom mode's
// multiplier is read (mt.m[MV_*], mt.gravity / speed / jump / dash / grapple) with the constant it scales, the
// functions, and the steps of pmoveStep in the order a 1 ms step runs them, each with the settings it reads and the
// state it changes. All of it is found in the text as it is now (the file can change under the page), with the
// plain-language notes written for each function and step matched by anchors (a step whose anchor is gone is left out).

export const FILES = ["pmove.cpp", "pmove.h", "gamedefs.h"];
const cache = new Map();
export async function loadSource(name, force) {
    if (!cache.has(name) || force) {
        cache.set(name, fetch(`/api/source?file=${encodeURIComponent(name)}`, { cache: "no-store" }).then(async (r) => {
            if (!r.ok) throw new Error(`${name}: ${r.status}`);
            return { name, text: await r.text(), path: r.headers.get("X-Source-Path") || name };
        }));
        cache.get(name).catch(() => cache.delete(name));
    }
    return cache.get(name);
}

// ---------------------------------------------------------------- C++ tokens -> highlighted HTML, one string per line
const KW = new Set(("alignas alignof auto bool break case char const constexpr continue default delete do double else enum explicit extern false float for " +
    "goto if inline int long namespace new noexcept nullptr operator private protected public return short signed sizeof static static_assert " +
    "struct switch template this thread_local true typedef typename uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t union unsigned using virtual void volatile while").split(" "));
const TYPES = new Set(["Vec3", "AABB", "MoveState", "UserCmd", "Map", "HookProbe", "MoveTuning", "CmdCtx", "std"]);
const esc = (s) => s.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[c]);
export function highlightCpp(text) {
    const out = [];
    let inBlock = false;
    for (const line of text.split("\n")) {
        let h = "", i = 0;
        const L = line.replace(/\r$/, "");
        while (i < L.length) {
            if (inBlock) {
                const e = L.indexOf("*/", i);
                const j = e < 0 ? L.length : e + 2;
                h += `<span class="cc">${esc(L.slice(i, j))}</span>`; i = j;
                if (e >= 0) inBlock = false;
                continue;
            }
            const c = L[i];
            if (c === "/" && L[i + 1] === "/") { h += `<span class="cc">${esc(L.slice(i))}</span>`; break; }
            if (c === "/" && L[i + 1] === "*") { inBlock = true; continue; }
            if (c === "#" && !L.slice(0, i).trim()) { h += `<span class="cpp">${esc(L.slice(i))}</span>`; break; }
            if (c === '"' || c === "'") {
                let j = i + 1;
                while (j < L.length && L[j] !== c) j += L[j] === "\\" ? 2 : 1;
                h += `<span class="cs">${esc(L.slice(i, j + 1))}</span>`; i = j + 1; continue;
            }
            if (/[0-9]/.test(c) || (c === "." && /[0-9]/.test(L[i + 1] || ""))) {
                const m = /^(0x[0-9a-fA-F]+|[0-9]*\.?[0-9]+(?:[eE][+-]?[0-9]+)?)[fFuUlL]*/.exec(L.slice(i));
                h += `<span class="cn">${esc(m[0])}</span>`; i += m[0].length; continue;
            }
            if (/[A-Za-z_]/.test(c)) {
                const m = /^[A-Za-z_][A-Za-z0-9_]*/.exec(L.slice(i))[0];
                const cls = KW.has(m) ? "ck" : /^k[A-Z]/.test(m) ? "cK" : /^MV_[A-Z_]+$/.test(m) ? "cM" : /^(MF|BTN|MOVE)_[A-Z_]+$/.test(m) ? "cF" : TYPES.has(m) ? "ct" : L[i + m.length] === "(" ? "cfn" : "";
                h += cls ? `<span class="${cls}">${m}</span>` : m; i += m.length; continue;
            }
            h += esc(c); i++;
        }
        out.push(h);
    }
    return out;
}

// ---------------------------------------------------------------- constants: constexpr float kName = 22.0f;
export function parseConstants(text, file) {
    const out = new Map();
    const re = /constexpr\s+(?:float|int|double|uint\d+_t)\s+(k[A-Za-z0-9_]+)\s*=\s*([^;]+);/g;
    let m;
    const lines = lineStarts(text);
    while ((m = re.exec(text))) {
        const v = numExpr(m[2]);
        out.set(m[1], { name: m[1], value: v, expr: m[2].trim(), file, line: lineOf(lines, m.index) });
    }
    return out;
}
function numExpr(s) {   // a constant's initialiser: numbers (1.0f), + - * / ( ), (float) casts, other constants left as NaN
    const t = s.replace(/\(float\)|\(int\)|static_cast<\w+>/g, "").replace(/([0-9.])[fF]\b/g, "$1");
    if (!/^[\s0-9.eE+\-*/()]+$/.test(t)) return NaN;
    try { const v = Function(`"use strict"; return (${t});`)(); return Number.isFinite(v) ? v : NaN; } catch { return NaN; }
}
function lineStarts(t) { const a = [0]; for (let i = 0; i < t.length; i++) if (t[i] === "\n") a.push(i + 1); return a; }
function lineOf(starts, off) { let lo = 0, hi = starts.length - 1; while (lo < hi) { const m = (lo + hi + 1) >> 1; if (starts[m] <= off) lo = m; else hi = m - 1; } return lo + 1; }

// MV_* -> setting key: moveParamKey's table in gamedefs.h when it is there, else the lower-case name
export function mvKeys(gamedefs) {
    const map = new Map();
    const en = /enum\s+MoveParam\s*\{([^}]*)\}/.exec(gamedefs || "");
    const ks = /moveParamKey\s*\(int i\)\s*\{[^{]*?\{([^}]*)\}/.exec(gamedefs || "");
    if (en && ks) {
        const names = en[1].split(",").map((x) => x.trim()).filter((x) => /^MV_/.test(x) && x !== "MV_COUNT");
        const keys = [...ks[1].matchAll(/"([a-z_]+)"/g)].map((x) => x[1]);
        names.forEach((n, i) => { if (keys[i]) map.set(n, keys[i]); });
    }
    return (name) => map.get(name) || name.replace(/^MV_/, "").toLowerCase();
}

// ---------------------------------------------------------------- uses: where each setting is read
// [{line, col, key, constant, kind: "mult"|"switch"}]
export function findUses(text, mvKey) {
    const out = [];
    const lines = text.split("\n");
    lines.forEach((raw, li) => {
        const code = stripComment(raw);
        const re = /(?:\bmt|moveTuning\(\))\.(?:m\[(MV_[A-Z_]+)\]|(gravity|speed|jump|dash|grapple)\b)/g;
        let m;
        while ((m = re.exec(code))) {
            const key = m[1] ? mvKey(m[1]) : m[2];
            const before = code.slice(0, m.index);
            const k = /(k[A-Z][A-Za-z0-9_]*)\s*\)?\s*\*\s*\(?\s*$/.exec(before) || /^\s*\*\s*(k[A-Z][A-Za-z0-9_]*)/.exec(code.slice(m.index + m[0].length));
            out.push({ line: li + 1, col: m.index, len: m[0].length, key, constant: k ? k[1] : null, kind: m[2] === "dash" || m[2] === "grapple" ? "switch" : "mult" });
        }
    });
    return out;
}
export function stripComment(l) {
    let out = "", inS = null;
    for (let i = 0; i < l.length; i++) {
        const c = l[i];
        if (inS) { out += c; if (c === "\\") { out += l[++i] || ""; } else if (c === inS) inS = null; continue; }
        if (c === '"' || c === "'") { inS = c; out += c; continue; }
        if (c === "/" && l[i + 1] === "/") break;
        out += c;
    }
    return out;
}

// ---------------------------------------------------------------- functions (top level, brace matched)
// Each line's code for brace matching: comments (// and /* */, across lines) gone and string / char literals blanked,
// so a '{' in a string or a commented-out block can't throw the matching off.
export function codeLines(text) {
    const out = [];
    let inBlock = false;
    for (const l of text.split("\n")) {
        let o = "", inS = null;
        for (let i = 0; i < l.length; i++) {
            const c = l[i];
            if (inBlock) { if (c === "*" && l[i + 1] === "/") { inBlock = false; i++; } continue; }
            if (inS) { if (c === "\\") i++; else if (c === inS) { inS = null; o += c; } continue; }
            if (c === '"' || c === "'") { inS = c; o += c; continue; }
            if (c === "/" && l[i + 1] === "/") break;
            if (c === "/" && l[i + 1] === "*") { inBlock = true; i++; continue; }
            o += c;
        }
        out.push(o);
    }
    return out;
}
// [{name, start, end, params}]: every top-level function with a body. A body ends where its braces match, however
// long it is (pmoveStep keeps growing); only the head, before the first '{', may not run on past HEAD_LINES lines.
const HEAD_LINES = 12;
export function findFunctions(text) {
    const code = codeLines(text), out = [];
    const head = /^(?:static\s+|inline\s+|\[\[[^\]]*\]\]\s*)*(?:const\s+)?[A-Za-z_][A-Za-z0-9_:<>*&\s]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)?/;
    let depth = 0;
    for (let i = 0; i < code.length; i++) {
        const c = code[i];
        if (depth === 0) {
            const m = head.exec(c);
            if (m && !/^(if|for|while|switch|return|namespace|struct|enum|class)$/.test(m[1]) && !/;\s*$/.test(c) && !/^\s*(namespace|struct|enum|class)\b/.test(c)) {
                // the body: from the first '{' at or after this line to its match
                let d = 0, j = i, started = false, end = -1;
                for (; j < code.length && (started || j < i + HEAD_LINES); j++) {
                    for (const ch of code[j]) { if (ch === "{") { d++; started = true; } else if (ch === "}") { d--; if (started && d === 0) { end = j; break; } } }
                    if (end >= 0) break;
                    if (!started && /;\s*$/.test(code[j])) break;
                }
                if (end >= 0) { out.push({ name: m[1], start: i + 1, end: end + 1, params: (m[2] || "").trim() }); i = end; continue; }
            }
        }
        for (const ch of c) { if (ch === "{") depth++; else if (ch === "}") depth = Math.max(0, depth - 1); }
    }
    // overloads (dashMaxCharges(mods) / dashMaxCharges(s, map)) are told apart by their parameter names
    const count = new Map();
    for (const f of out) count.set(f.name, (count.get(f.name) || 0) + 1);
    for (const f of out) f.title = f.name + (count.get(f.name) > 1 ? `(${paramNames(f.params)})` : "()");
    return out;
}
// "const MoveState& s, const Map* map" -> "s, map"
export function paramNames(params) {
    return params.split(",").map((p) => { const m = /([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*(?:=.*)?$/.exec(p.trim()); return m ? m[1] : p.trim(); }).filter(Boolean).join(", ");
}

// ---------------------------------------------------------------- the notes: functions and the steps of pmoveStep
// (anchor: the first line matching it, inside the function for steps)
export const FUNCTION_NOTES = {
    playerBox: "The player's hull: 0.8 m wide (kPlayerHalfWidth 0.4 each side) and 1.8 m tall, standing on the feet position, scaled by the size modifiers.",
    airAccelerate: "Air strafing, Quake style. Speed is only added along the wish direction until the speed along it reaches the wish speed (the AIR STRAFE CAP, 2.3 m/s; 2.53 with A/D held), so you gain speed by turning into the strafe: the faster you go, the narrower the window of angles that still adds speed. Each ms adds up to accel × wish speed × dt, accel being AIR ACCELERATION × 10 (22). With A/D held the push is × 1.15 and, above `cross` (12.65 m/s), STRAFE BOOST ramps in up to × 1.1 at 15.8 m/s. A push against your momentum (overstrafing) brakes by only the OVERSTRAFE BRAKE share (0.35).",
    accelerate: "Ground acceleration: speed toward the wish direction is added until the speed along it reaches the wish speed (run speed). Each ms adds at most accel × wish speed × dt: GROUND ACCELERATION 16 × 9.5 m/s = 152 m/s², so you are at full speed in about 60 to 80 ms.",
    slideMove: "Moves the hull through the map one axis at a time (up/down first, then x, then z) and says which axes were blocked; the step code then zeroes the velocity on those axes.",
    maxChargesFor: "How many dashes you hold: 2 × DASH CHARGES, rounded, from 1 to 10.",
    cooldownMsFor: "The wait between dashes, counted from the end of the last one: 0.6 s × DASH COOLDOWN (a third of that with the quick dash modifier), in whole ms.",
    rechargeMsFor: "How long a spent charge takes to come back, from the end of the dash that spent it: 1.2 s × DASH COOLDOWN (the same multiplier scales both).",
    dashBurst: "The speed while a dash runs: DASH SPEED (24 m/s) or the exit speed if that is higher (dashing out of a fast strafe); after a wall bounce, the bounce's speed.",
    refillCharges: "Gives charges back, up to a target, but only ones that have finished recharging.",
    setCharges: "Leaves exactly n charges (super dash and wall bounce leave one): takes extra away, or refills up to n.",
    pmoveStep: "One 1 ms step of movement, the same code on the server and in the client's prediction. In order: the wish direction from the keys; jump (and bunny hop); the dash timers; a new dash; dash jump / super dash; the dash burst; the grapple (fire, pull); then ladder, ground (friction and acceleration) or air (strafing, drift, air control, gravity); the move through the map; stepping up ledges; the dash wall bounce; landing (and the bhop bounce); the dash cooldown; charges back.",
    pmoveExpr: "Equations (v2): before each 1 ms step the player's state goes in (p.speed, p.air, ...) and every dynamic setting is worked out into this command's own copy of the tuning, then the normal step runs. Only custom modes with dynamic equations take this path.",
    pmove: "Runs one command as 1 ms steps (1 to 8 of them, msec), so movement is the same at every server tick rate. The custom mode's tuning (moveTuning(): every multiplier) is fetched once per command.",
    moveParamBase: "The standard value of each MV_ setting: what a custom mode's multiplier scales (the Values tab's standard column).",
    moveParamUnit: "The unit of each MV_ setting (m/s, s, m...).",
    dashReady: "Would a dash press in the next command start a dash: a charge left, no dash running, the cooldown over (bots and the HUD use this).",
    dashAimedDown: "Is the running dash aimed down far enough (DASH VERTICAL × 0.25, about 14.5° down) that a jump makes it a super dash.",
    dashRefillAll: "Every charge back at once (a teammate's syringe).",
};
export const STEPS = [
    { id: "wish", label: "wish direction", anchor: /Vec3 wish = fwd/, note: "The keys (forward / side, −127..127) become a direction and a wish speed: run speed (9.5 m/s × MOVE SPEED) times how far the keys are pressed." },
    { id: "jump", label: "jump / bunny hop", anchor: /if \(onGround && \(?jumpHeld\b.*!dashActive\)/, note: "On the floor with jump held: up at JUMP (7.34 m/s × JUMP). Holding jump jumps again on the first grounded ms, before friction can slow you: bunny hopping keeps your speed. A fresh press is buffered (DASH JUMP WINDOW, 0.1 s): a tap let go up to 0.1 s before landing jumps on landing too, and the dash techs use it." },
    { id: "dashTimers", label: "dash timers", anchor: /s\.hookCooldown = std::max\(0\.0f, s\.hookCooldown - dt\)/, note: "Cooldowns count down 1 ms: the grapple's, the dash cooldown (only while no dash runs) and each spent charge's recharge. The charge count is held to the maximum (DASH CHARGES)." },
    { id: "dashStart", label: "dash start", anchor: /if \(dashStart\) \{/, note: "A dash press with a charge left and the cooldown over starts a dash: along the keys (or forward), DASH LENGTH (0.12 s) long, tilted up/down by the look pitch (DASH VERTICAL). It leaves with your speed + DASH SPEED KEPT (1 m/s), at least DASH EXIT SPEED (MIN) 11 and at most (MAX) 32 m/s. It spends a charge, which starts recharging when the dash ends." },
    { id: "dashTech", label: "dash jump / super dash", anchor: /if \(dashing && !frozen\) \{/, note: "Jump during a dash on the floor: the dash ends at once and you leave at its full burst speed plus a normal jump (all charges back). Aimed down into the floor it is a super dash: the speed you carried in + SUPER DASH BOOST (5 m/s), at most SUPER DASH SPEED (MAX) 36, and a hop from half a jump (just past 14.5° down) to a full one (straight down) × SUPER DASH JUMP; it leaves one charge." },
    { id: "dashBurst", label: "dash burst", anchor: /^\s*if \(dashing\) \{/, note: "While the dash runs: a fixed velocity along its direction at the burst speed (DASH SPEED 24 m/s), no gravity, no steering; when its time runs out the speed settles to the exit speed." },
    { id: "grapple", label: "grapple", anchor: /bool hookHeld = /, note: "Hold the grapple: it fires along your look up to GRAPPLE RANGE (45 m) and keeps trying until it catches; once caught it pulls toward the anchor at GRAPPLE PULL (38 m/s²), adding speed up to GRAPPLE TOP SPEED (24 m/s) but never braking you. It lets go within GRAPPLE RELEASE DISTANCE (1.2 m); then GRAPPLE COOLDOWN (1 s)." },
    { id: "pulled", label: "pulled by a grapple", anchor: /bool pulled = /, note: "Held by another player's grapple: pulled toward them the same way (the same pull and top speed, half gravity)." },
    { id: "ladder", label: "ladder", anchor: /bool onLadder = /, note: "Inside a ladder volume: moving climbs at LADDER CLIMB SPEED (6.75 m/s; look down to climb down), sideways at LADDER SIDE SPEED (4.5 m/s), no gravity; a fresh jump kicks you off backwards." },
    { id: "ground", label: "ground: friction + accelerate", anchor: /\} else if \(onGround\) \{/, note: "On the floor: friction first, removing max(speed, STOPPING SPEED 2 m/s) × GROUND FRICTION (7) × dt each ms: speed falls ~0.7% per ms above 2 m/s, then by a fixed 14 m/s² (a stop from 9.5 m/s takes 0.37 s and 1.21 m). Then accelerate() toward the keys (GROUND ACCELERATION)." },
    { id: "air", label: "air: strafe, drift, control, gravity", anchor: /if \(!dashing && !onLadder\) \{/, note: "In the air: Quake strafing (airAccelerate), drift below run speed (AIR DRIFT 8 m/s² toward the keys, never past run speed), W-only air control bending your momentum toward where you look without changing speed (AIR CONTROL × 0.9, falling off as 1 / speed above 9.5 m/s × MOVE SPEED; A/D turns it off), then gravity: 22 m/s² × GRAVITY (× GRAPPLE GRAVITY 0.5 while hooked, × 0.4 with low gravity)." },
    { id: "move", label: "move through the map", anchor: /Vec3 delta = s\.vel \* dt;/, note: "velocity × 1 ms is moved through the map (slideMove), one axis at a time; blocked axes are remembered." },
    { id: "stepUp", label: "step up ledges", anchor: /\/\/ Step up small ledges/, note: "Blocked sideways: try again raised by STEP HEIGHT (0.55 m), then back down; taken if it gets further. Stairs and lips carry you over, walking or in the air, keeping your speed." },
    { id: "wallBounce", label: "dash wall bounce", anchor: /\/\/ Dash wall bounce/, note: "A dash into a wall closer than DASH WALL BOUNCE ANGLE (45°) to head-on reflects off it at 16 m/s × DASH SPEED (twice per dash at most) and leaves one charge; a glancing hit slides along the wall." },
    { id: "blocked", label: "stop on walls", anchor: /if \(blocked & 1\) s\.vel\.x = 0;/, note: "Velocity on a blocked axis is zeroed (a wall stops that part of your speed; a ceiling stops rising)." },
    { id: "land", label: "landing / bhop bounce", anchor: /\/\/ Ground check/, note: "A probe 5 cm down: on the floor again. Landing harder than a jump with jump held bounces you higher: jump + min((impact − jump) × BHOP BOUNCE 0.5, BHOP BOUNCE (MAX) 8 m/s). Walking off a step down (STEP HEIGHT) keeps you on the floor." },
    { id: "cooldown", label: "dash cooldown starts", anchor: /\/\/ the dash cooldown starts when a dash ends/, note: "The step a dash ends (its time ran out or a dash jump ended it) starts the cooldown: 0.6 s × DASH COOLDOWN." },
    { id: "refill", label: "charges back", anchor: /\/\/ Dash charges back/, note: "At the end of the step on the floor: every recharged charge comes back; while holding jump (bhopping) only up to one; not during a dash, on a ladder or off a grapple." },
];

// the steps of pmoveStep in the text: [{...step, start, end}] (end: the line before the next step, or the function's end)
export function findSteps(text, fns) {
    const lines = text.split("\n");
    const f = fns.find((x) => x.name === "pmoveStep");
    if (!f) return [];
    const found = [];
    for (const st of STEPS) {
        for (let i = f.start; i < f.end; i++) if (st.anchor.test(lines[i - 1])) { found.push({ ...st, start: i }); break; }
    }
    found.sort((a, b) => a.start - b.start);
    found.forEach((s, i) => (s.end = i + 1 < found.length ? found[i + 1].start - 1 : f.end - 1));
    return found;
}

// What this parser couldn't find in the code as it is: [] when every function note and every step of pmoveStep was
// found. A game-code change that moves an anchor shows here (the Code tab and the Code graph say so) and fails
// web/test/source_check.mjs, instead of a step silently dropping out of the graph.
export function sourceProblems(fns, steps) {
    const out = [];
    if (!fns.some((f) => f.name === "pmoveStep")) out.push("couldn't find pmoveStep() in pmove.cpp, so none of its steps: the parser (tools/physics_editor/web/js/source.js findFunctions) needs updating");
    else for (const st of STEPS) if (!steps.some((x) => x.id === st.id)) out.push(`couldn't find the step "${st.label}" in pmoveStep (its anchor ${st.anchor} matches no line): update STEPS in source.js`);
    for (const name of Object.keys(FUNCTION_NOTES)) if (!fns.some((f) => f.name === name)) out.push(`couldn't find ${name}() in pmove.cpp (it has a note in source.js FUNCTION_NOTES)`);
    return out;
}

// ---------------------------------------------------------------- what a range of code changes
export const OUTPUTS = [
    { id: "hvel", label: "horizontal velocity", re: [/\bs\.vel\s*(=|\+=|-=|\*=)/, /\bs\.vel\.[xz]\s*(=|\+=|-=|\*=)/, /\b(airAccelerate|accelerate)\s*\([^)]*\bs\.vel\b/, /\bvel\s*\+=/, /\bvel\.[xz]\s*[+\-*]?=/] },
    { id: "vvel", label: "vertical velocity", re: [/\bs\.vel\s*(=|\*=)/, /\bs\.vel\.y\s*(=|\+=|-=|\*=)/] },
    { id: "pos", label: "position", re: [/\bs\.pos(\.[xyz]|\[[^\]]+\])?\s*(=|\+=|-=)/, /\bslideMove\s*\(\s*s\b/] },
    { id: "ground", label: "on the floor", re: [/\bonGround\s*=/, /\bnowGround\s*=/, /MF_ONGROUND/] },
    { id: "charges", label: "dash charges", re: [/\bdashCharges\s*(=|--|\+\+|-=|\+=)/, /\bs\.dashCharges--/, /\b(refillCharges|setCharges)\s*\(\s*s\b/] },
    { id: "dash", label: "dash state (timer, direction)", re: [/\bs\.dash(Timer|DirX|DirZ|Up|ExitSpeed|FromSpeed|Bounces)\s*(=|-=|\+\+|--)/, /\bs\.dashBounces\+\+/] },
    { id: "cooldown", label: "dash cooldown / recharge", re: [/\bdashCooldownMs\s*=/, /\bdashRechargeMs\s*\[[^\]]*\]\s*=/, /\bdashRecharging\s*=/] },
    { id: "hook", label: "grapple (anchor, hooked)", re: [/\bs\.hookPoint\s*=/, /\bs\.hookTarget\s*=/, /MF_HOOKED/] },
    { id: "hookcd", label: "grapple cooldown", re: [/\bs\.hookCooldown\s*=/] },
    { id: "jumpbuf", label: "jump buffer", re: [/\bs\.jump(Buffer|Grace)\s*=/] },
    { id: "flags", label: "held buttons (flags)", re: [/MF_(JUMPHELD|DASHHELD|HOOKHELD|CROUCHED)/] },
];
export function outputsOf(lines, a, b) {
    const hit = new Set();
    for (let i = a; i <= b && i <= lines.length; i++) {
        const c = stripComment(lines[i - 1]);
        if (!c.trim()) continue;
        for (const o of OUTPUTS) if (!hit.has(o.id) && o.re.some((r) => r.test(c))) hit.add(o.id);
    }
    return hit;
}
export function callsIn(lines, a, b, fns) {
    const names = new Set(), res = fns.map((f) => new RegExp(`\\b${f.name}\\s*\\(`));   // (one RegExp per function, not per line)
    for (let i = a; i <= b && i <= lines.length; i++) {
        const c = stripComment(lines[i - 1]);
        fns.forEach((f, k) => { if (!names.has(f.name) && !(f.start <= i && i <= f.end) && res[k].test(c)) names.add(f.name); });
    }
    return [...names];
}

// everything at once: {files: {name: {text, lines, html}}, constants: Map, uses (pmove.cpp), fns, steps, mvKey, problems}
export async function analyzeSource() {
    const [cpp, h, defs] = await Promise.all(FILES.map((f) => loadSource(f).catch(() => null)));
    if (!cpp) throw new Error("the movement code isn't available (serve.py /api/source)");
    const mvKey = mvKeys(defs && defs.text);
    const constants = new Map();
    for (const f of [defs, h, cpp]) if (f) for (const [k, v] of parseConstants(f.text, f.name)) constants.set(k, v);
    const lines = cpp.text.split("\n");
    const fns = findFunctions(cpp.text);
    const uses = findUses(cpp.text, mvKey);
    const steps = findSteps(cpp.text, fns);
    for (const st of steps) {
        st.uses = uses.filter((u) => u.line >= st.start && u.line <= st.end);
        st.calls = callsIn(lines, st.start, st.end, fns.filter((f) => f.name !== "pmoveStep" && f.name !== "pmove"));
        st.outputs = outputsOf(lines, st.start, st.end);
        for (const c of st.calls) for (const f of fns.filter((x) => x.name === c)) {   // (every overload of it)
            st.viaUses = (st.viaUses || []).concat(uses.filter((u) => u.line >= f.start && u.line <= f.end).map((u) => ({ ...u, via: c })));
            for (const o of outputsOf(lines, f.start, f.end)) if (o !== "pos" || c === "slideMove") st.outputs.add(o);
        }
    }
    const files = {};
    for (const f of [cpp, h, defs]) if (f) files[f.name] = { text: f.text, path: f.path, lines: f.text.split("\n"), html: null };
    return { files, constants, uses, fns, steps, mvKey, problems: sourceProblems(fns, steps) };
}
