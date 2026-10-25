// The movement parameters: the page's own copy of `quark_maptool physparams` (INTERFACE.md), used by the MOCK and
// until the helper answers. Keys, labels, bases and units are the game's (src/game/gamedefs.h moveParamKey /
// moveParamLabel, src/game/pmove.cpp moveParamBase / moveParamUnit); min / max are the clamps CustomRules::fromText
// applies (customrules.cpp), so the page never offers a value the game would clamp. The helper's answer replaces
// this whole catalog when it is there.

const P = (key, label, base, unit, group, cpp, doc) => ({ key, label, base, unit, group, min: 0.1, max: 5, step: 0.01, cpp, doc });

export const FALLBACK = {
    version: "(page copy)",
    moveStep: 0.001,
    top: [
        { key: "gravity", label: "GRAVITY", unit: "m/s2", base: 22, min: 0.1, max: 3, step: 0.01, group: "general", cpp: "kGravity", doc: "pull down in the air" },
        { key: "speed", label: "MOVE SPEED", unit: "m/s", base: 9.5, min: 0.25, max: 3, step: 0.01, group: "general", cpp: "kGroundSpeed", doc: "run speed (and crouch speed, air drift's limit, W-only air control's fall-off)" },
        { key: "jump", label: "JUMP", unit: "m/s", base: 7.34, min: 0.25, max: 3, step: 0.01, group: "general", cpp: "kJumpSpeed", doc: "jump take-off speed (a 1.22 m standing jump at the standard gravity)" },
    ],
    params: [
        P("crouch_speed", "CROUCH SPEED", 4, "m/s", "ground", "kCrouchSpeed", "crouched run speed (the game ignores crouch since 1.17: BTN_CROUCH does nothing)"),
        P("ground_accel", "GROUND ACCELERATION", 16, "", "ground", "kGroundAccel", "how fast you reach run speed (x run speed per second)"),
        P("friction", "GROUND FRICTION", 7, "", "ground", "kFriction", "how fast you slow down on the floor"),
        P("stop_speed", "STOPPING SPEED", 2, "m/s", "ground", "kStopSpeed", "below this speed friction acts as if you moved this fast (a quick stop)"),
        P("step", "STEP HEIGHT", 0.55, "m", "ground", "kStepHeight", "the highest ledge you walk (or fly) over without jumping"),
        P("air_accel", "AIR ACCELERATION", 2.2, "", "air", "kAirAccel", "air strafing acceleration (x10 inside pmove)"),
        P("air_cap", "AIR STRAFE CAP", 2.3, "m/s", "air", "kAirSpeedCap", "the quake-style wish speed cap in the air (A/D: x 1.1)"),
        P("overstrafe", "OVERSTRAFE BRAKE", 0.35, "", "air", "kOverstrafeBrake", "turning past the window: the share of the braking push that still applies"),
        P("air_control", "AIR CONTROL", 4, "", "air", "kAirControl", "how fast W-only bends your momentum in the air"),
        P("air_drift", "AIR DRIFT (UNDER RUN SPEED)", 8, "m/s2", "air", "kAirDriftAccel", "below run speed, held direction pushes you this hard in the air"),
        P("strafe_boost", "STRAFE BOOST (AT SPEED)", 0.1, "", "air", "kStrafeBoost", "A/D strafing's extra acceleration at speed (ramps in from 12.65 m/s)"),
        P("bounce_gain", "BHOP BOUNCE", 0.5, "", "air", "kBounceGain", "landing faster than a jump adds this share of the extra to the next jump"),
        P("bounce_max", "BHOP BOUNCE (MAX)", 8, "m/s", "air", "kBounceMax", "the most a bhop bounce adds"),
        P("dash_speed", "DASH SPEED", 24, "m/s", "dash", "kDashSpeed", "the burst's speed (also scales the wall bounce's 16 m/s)"),
        P("dash_time", "DASH LENGTH", 0.12, "s", "dash", "kDashTime", "how long the burst lasts"),
        P("dash_exit_min", "DASH EXIT SPEED (MIN)", 11, "m/s", "dash", "kDashExitMin", "the slowest you leave a dash"),
        P("dash_keep", "DASH SPEED KEPT", 1, "m/s", "dash", "kDashKeep", "you leave a dash with your old speed plus this"),
        P("dash_exit_max", "DASH EXIT SPEED (MAX)", 32, "m/s", "dash", "kDashExitMax", "the fastest you leave a dash"),
        P("dash_vertical", "DASH VERTICAL", 0.9, "", "dash", "kDashVerticalDown", "looking straight down makes the dash this vertical (sine); straight up 0.4 (kDashVerticalUp), scaled the same"),
        P("dash_charges", "DASH CHARGES", 2, "", "dash", "kDashCharges", "dashes you hold (rounded, 1 to 10)"),
        P("dash_cooldown", "DASH COOLDOWN", 0.6, "s", "dash", "kDashCooldown", "between dashes (and the 1.2 s recharge, scaled the same)"),
        P("dash_jump_window", "DASH JUMP WINDOW", 0.1, "s", "dash", "kDashJumpWindow", "a jump this early still makes a dash jump / super dash"),
        P("dash_bounce_angle", "DASH WALL BOUNCE ANGLE", 45, "deg", "dash", "kDashBounceAngle", "a dash this head-on into a wall bounces off it"),
        P("super_dash_boost", "SUPER DASH BOOST", 5, "m/s", "super dash", "kSuperDashBoost", "a super dash leaves with the speed you carried in + this"),
        P("super_dash_jump", "SUPER DASH JUMP", 0.9, "", "super dash", "kSuperDashJump", "the most of its speed that goes up (it leaves like the dash bounced off the floor: the dash's down share goes up)"),
        P("super_dash_max", "SUPER DASH SPEED (MAX)", 36, "m/s", "super dash", "kSuperDashMax", "the fastest a super dash leaves"),
        P("hook_range", "GRAPPLE RANGE", 45, "m", "grapple", "kGrappleRange", "how far the hook reaches"),
        P("hook_accel", "GRAPPLE PULL", 38, "m/s2", "grapple", "kHookAccel", "pull toward the anchor"),
        P("hook_max", "GRAPPLE TOP SPEED", 24, "m/s", "grapple", "kHookMaxSpeed", "the pull adds speed up to this (never brakes)"),
        P("hook_gravity", "GRAPPLE GRAVITY", 0.5, "x", "grapple", "kHookGravityScale", "gravity while hooked, as a share"),
        P("hook_release", "GRAPPLE RELEASE DISTANCE", 1.2, "m", "grapple", "kHookRelease", "lets go this close to the anchor"),
        P("hook_cooldown", "GRAPPLE COOLDOWN", 1, "s", "grapple", "kHookCooldown", "after the hook is released"),
        P("ladder_speed", "LADDER CLIMB SPEED", 6.75, "m/s", "ladder", "kLadderSpeed", "climbing speed"),
        P("ladder_side", "LADDER SIDE SPEED", 4.5, "m/s", "ladder", "kLadderSide", "sideways speed on a ladder"),
    ],
    player: { halfWidth: 0.4, height: 1.8, crouchHeight: 1.2, eye: 1.6, stepUp: 0.55 },
    constants: {
        "dash cooldown after a dash ends": "0.6 s", "dash recharge": "1.2 s", "dash wall bounce speed": "16 m/s",
        "dash max wall bounces": 2, "super dash threshold": "14.5 deg down", "A/D strafe cap": "x 1.1", "A/D strafe accel": "x 1.15",
        "W-only air control": "x 0.9 (falls off above 9.5 m/s)", "movement step": "1 ms",
    },
};

export const GROUPS = ["general", "ground", "air", "dash", "super dash", "grapple", "ladder"];
export const SWITCHES = [
    { key: "dash", label: "DASH", doc: "dashing allowed" },
    { key: "grapple", label: "GRAPPLE", doc: "the grapple allowed" },
];

// The catalog the page uses: the helper's physparams, or the fallback. Normalised: every entry has key, label, unit,
// base, group, min, max, step; top entries are marked top: true.
export function normalizeCatalog(pp) {
    const src = pp && Array.isArray(pp.params) ? pp : FALLBACK;
    const fb = new Map([...FALLBACK.top, ...FALLBACK.params].map((p) => [p.key, p]));
    const fix = (p, top) => {
        const f = fb.get(p.key) || {};
        return { ...f, ...p, top, group: p.group || f.group || (top ? "general" : "general"),
            min: p.min ?? f.min ?? 0.1, max: p.max ?? f.max ?? 5, step: p.step ?? f.step ?? 0.01, unit: p.unit ?? f.unit ?? "",
            base: p.base ?? f.base ?? 1, label: p.label || f.label || p.key, doc: p.doc || f.doc || "", cpp: p.cpp || f.cpp || "" };
    };
    const top = (src.top && src.top.length ? src.top : FALLBACK.top).map((p) => fix(p, true));
    const params = src.params.map((p) => fix(p, false));
    return { version: src.version || "", top, params, all: [...top, ...params], byKey: new Map([...top, ...params].map((p) => [p.key, p])),
        player: { ...FALLBACK.player, ...(src.player || {}) }, constants: src.constants || FALLBACK.constants, fromHelper: src !== FALLBACK };
}

// A physics set (INTERFACE.md): {gravity, speed, jump, dash, grapple, move: {key: x}} with every key filled in.
export function standardPhysics(cat) {
    const move = {};
    for (const p of (cat || normalizeCatalog()).params) move[p.key] = 1;
    return { gravity: 1, speed: 1, jump: 1, dash: true, grapple: true, move };
}
export function clonePhysics(p) {
    const o = { gravity: p.gravity, speed: p.speed, jump: p.jump, dash: p.dash, grapple: p.grapple, move: { ...p.move } };
    if (p.expr && Object.keys(p.expr).length) o.expr = { ...p.expr };
    if (p.imports && p.imports.length) o.imports = p.imports.map((i) => ({ ...i, keys: [...i.keys] }));
    return o;
}
export function getVal(ph, key) { return key === "gravity" || key === "speed" || key === "jump" ? ph[key] : (ph.move[key] ?? 1); }
export function setVal(ph, key, v) { if (key === "gravity" || key === "speed" || key === "jump") ph[key] = v; else ph.move[key] = v; }
export function changedKeys(ph, cat) {
    const out = [];
    for (const p of cat.all) if (Math.abs(getVal(ph, p.key) - 1) > 1e-9 || hasExpr(ph, p.key)) out.push(p.key);
    for (const s of SWITCHES) if (ph[s.key] === false) out.push(s.key);
    return out;
}
export const hasExpr = (ph, key) => !!(ph.expr && typeof ph.expr[key] === "string" && ph.expr[key].trim());
export function setExpr(ph, key, text) {
    if (text == null || !String(text).trim()) { if (ph.expr) { delete ph.expr[key]; if (!Object.keys(ph.expr).length) delete ph.expr; } }
    else (ph.expr || (ph.expr = {}))[key] = String(text).replace(/[\r\n]+/g, " ").trim();
}
// the compact form sent to the helper (missing keys = 1 / true); `expr` (v2) only when the helper evaluates expressions
export function compactPhysics(ph, withExpr = true) {
    const move = {};
    for (const [k, v] of Object.entries(ph.move || {})) if (v !== 1) move[k] = v;
    const o = { gravity: ph.gravity, speed: ph.speed, jump: ph.jump, dash: ph.dash !== false, grapple: ph.grapple !== false, move };
    if (withExpr && ph.expr && Object.keys(ph.expr).length) o.expr = { ...ph.expr };
    return o;
}

// The C++ constants list (physexport's `cpp`, made in the page for the MOCK): each changed constant at base x mult.
export function cppList(ph, cat) {
    const lines = [];
    const n = (v) => { const s = (+v.toPrecision(6)).toString(); return s.includes(".") || s.includes("e") ? s + "f" : s + ".0f"; };
    for (const p of cat.all) {
        const m = getVal(ph, p.key);
        if (Math.abs(m - 1) < 1e-9) continue;
        const name = p.cpp || p.key;
        const file = p.key === "dash_charges" || p.key === "dash_cooldown" || p.key === "hook_range" || p.key === "hook_cooldown" ? "pmove.h" : "pmove.cpp";
        const isInt = p.key === "dash_charges";
        const v = isInt ? Math.min(10, Math.max(1, Math.round(p.base * m))) : p.base * m;
        lines.push(`${file}: ${name} = ${isInt ? v : n(v)};   // was ${isInt ? p.base : n(p.base)} (x ${+m.toPrecision(6)}${p.unit ? ", " + p.unit : ""})`);
        if (p.key === "dash_speed") lines.push(`pmove.cpp: kDashBounceSpeed = ${n(16 * m)};   // was 16.0f (the dash speed multiplier scales the wall bounce too)`);
        if (p.key === "dash_cooldown") lines.push(`pmove.h: kDashRecharge = ${n(1.2 * m)};   // was 1.2f (the cooldown multiplier scales the recharge too)`);
    }
    for (const [k, t] of Object.entries(ph.expr || {})) lines.push(`(${k}: an equation, expr.${k}=${t}: the line above is its value standing still; the equation itself needs v2's moveexpr, no constant)`);
    if (ph.dash === false) lines.push("(dash off: a custom mode switch, no constant)");
    if (ph.grapple === false) lines.push("(grapple off: a custom mode switch, no constant)");
    return lines.length ? lines.join("\n") + "\n" : "// no change from the standard physics\n";
}
