// The brain's JSON AST (tools/bots/GRAMMAR.md section 4): the catalog of slots / sensors / builtins, validation,
// tree helpers, an evaluator (curves and decision odds in the Values panel), and the provenance areas.
// The catalog here is GRAMMAR.md section 5; `setCatalog` replaces it with `quark_tests exportbrain`'s when there.

export const WEAPONS = ["rifle", "rocket", "knife", "sniper", "laser", "shotgun", "sticky", "crossbow", "bubble", "syringe", "locator"];
export const BRAIN_MODES = ["vs", "bomb", "race", "koth", "payload", "ffa", "npayload", "duel", "arena", "tourney", "hide", "tag", "points"];
export const MAP_TO_BRAIN_MODE = { "1v1": "arena" };
export const POINTS = ["me.pos", "checkpoint.next", "hill.floor", "cart.goal", "point.floor", "bomb.pos", "carrier.pos"];
export const ACTIONS = ["goto", "fallback", "seek", "evade", "pass"];

const COMMON = { now: "f64", dt: "f32", mode: "mode", level: "int", myId: "int", health: "f32", itMode: "bool" };
const OBJ_SENSORS = { checkpointsLeft: "bool", hasHill: "bool", hasCart: "bool", pointActive: "bool", bombPlanted: "bool", bombDropped: "bool",
    bombCarried: "bool", attacker: "bool", carrierIsMe: "bool", carrierAlive: "bool", navReady: "bool", it: "bool", siteCount: "int" };

export let CATALOG = {
    common: COMMON,
    slots: {
        react: { when: "a threat comes into view", sensors: { off: "f32", dist: "f32", sinceSeen: "f64" }, outputs: { r: "f32" }, area: "perception" },
        hearing: { when: "each look (20 Hz), per threat not in view", sensors: { dist: "f32", gunAge: "f64", onGround: "bool", groundSpeed: "f32", dashing: "bool" }, outputs: { heard: "bool" }, area: "perception" },
        targetScore: { when: "each look, per enemy in view", sensors: { dist: "f32", angleOff: "f32", current: "bool" }, outputs: { score: "f32" }, area: "targeting" },
        objective: { when: "each tick not fighting (and urgent)", sensors: OBJ_SENSORS, outputs: {}, area: "mode" },
        urgent: { when: "each tick", sensors: OBJ_SENSORS, outputs: { urgent: "bool" }, area: "mode/bomb" },
        pickWeapon: { when: "each fight tick", sensors: { dist: "f32" }, outputs: { pick: "weapon" }, area: "weapons" },
        throwKnife: { when: "each fight tick", sensors: { weapon: "weapon", dist: "f32", weaponRange: "f32" }, outputs: { throw: "bool" }, area: "weapons" },
        idealRange: { when: "each fight tick", sensors: { weapon: "weapon" }, outputs: { range: "f32" }, area: "weapons" },
        rejudge: { when: "when the last next is due (and each new target)", sensors: {}, outputs: { lead: "f32", drop: "f32", speed: "f32", next: "f64" }, area: "aim" },
        fire: { when: "each fight tick, target in view", sensors: { err: "f32", dist: "f32", weapon: "weapon", throwKnife: "bool", weaponRange: "f32", ownBlast: "bool" }, outputs: { fire: "bool", alt: "bool" }, area: "aim" },
        fightHook: { when: "fight, target in view, grapple ready", sensors: { weapon: "weapon", dist: "f32", hookRange: "f32" }, outputs: { try: "bool", retry: "f64", go: "bool", hold: "f64" }, area: "fight-movement" },
        strafe: { when: "fight, when the last next is due", sensors: {}, outputs: { dir: "f32", next: "f64" }, area: "fight-movement" },
        pulledDash: { when: "fight, pulled by an enemy grapple, dash ready, no drop near", sensors: { ideal: "f32" }, outputs: { dash: "bool" }, area: "fight-movement" },
        fightJump: { when: "fight, on the ground, no drop near", sensors: {}, outputs: { jump: "bool" }, area: "fight-movement" },
        fightDash: { when: "fight, on the ground, no drop near, dash ready", sensors: { dist: "f32" }, outputs: { dash: "bool", closeIn: "bool" }, area: "fight-movement" },
        glance: { when: "not fighting, nothing heard, when the last next is due", sensors: { idle: "bool" }, outputs: { yaw: "f32", until: "f64", next: "f64" }, area: "perception" },
        hookRetry: { when: "route grapple, when the last try is due", sensors: { grappleOnly: "bool" }, outputs: { next: "f64" }, area: "roam" },
        routeHook: { when: "route grapple, per candidate anchor", sensors: { rise: "f32", flat: "f32", grappleOnly: "bool" }, outputs: { want: "bool" }, area: "roam" },
        bhop: { when: "moving", sensors: { fighting: "bool", wpDist: "f32" }, outputs: { hop: "bool" }, area: "roam" },
        runDash: { when: "not fighting, on the ground, dash ready", sensors: { wpDist: "f32", straight: "f32" }, outputs: { dash: "bool" }, area: "roam" },
        dashJump: { when: "a run dash was chosen", sensors: { hopping: "bool", wpDist: "f32" }, outputs: { jump: "bool" }, area: "roam" },
        unstick: { when: "when the last next is due", sensors: { moving: "bool", moved: "f32", hasTarget: "bool" }, outputs: { stuck: "bool", side: "f32", until: "f64", next: "f64" }, area: "roam" },
    },
    builtins: {
        roll: { args: 0, type: "f32", doc: "uniform random [0, 1): one draw", draws: 1 },
        gauss: { args: 0, type: "f32", doc: "normal random (mean 0, sd 1): two draws", draws: 2 },
        chance: { args: 1, type: "bool", doc: "an event `rate` times a second, this tick (one draw when rate > 0)", draws: 1 },
        min: { args: 2, type: "common" }, max: { args: 2, type: "common" }, clamp: { args: 3, type: "common" },
        abs: { args: 1, type: "arg" }, sqrt: { args: 1, type: "arg" }, exp: { args: 1, type: "arg" },
        f32: { args: 1, type: "f32" }, f64: { args: 1, type: "f64" },
        allowed: { args: 1, type: "bool", doc: "the server allows weapon w" },
        firstAllowed: { args: 2, type: "weapon", doc: "the first allowed weapon of the list, else w" },
        site: { args: 1, type: "point", doc: "bomb site i (modulo the number of sites)" },
    },
    params: null,   // from exportbrain: {name: {type, unit, doc, min, max}}
};
// `quark_tests exportbrain`'s catalog (lists of {name, ...}) -> the shape above. Slot areas stay the page's (the
// provenance areas); everything else (sensors, outputs, types, docs, builtins, params with unit / range / default)
// is the game's.
export function catalogFromExport(c) {
    const sens = new Map((c.sensors || []).map((x) => [x.name, x]));
    const common = {};
    for (const x of c.sensors || []) if (x.common) common[x.name] = x.type;
    const slots = {};
    for (const sl of c.slots || []) {
        const old = CATALOG.slots[sl.name] || {};
        const sensors = {};
        for (const n of sl.sensors || []) { const x = typeof n === "string" ? sens.get(n) : n; if (x) sensors[x.name] = x.type; }
        const outputs = {};
        for (const o of sl.outputs || []) outputs[o.name] = o.type;
        slots[sl.name] = { when: sl.when || old.when || "", doc: sl.doc || "", sensors, outputs, rolls: sl.rolls || 0, area: old.area || (sl.name === "objective" ? "mode" : "brain") };
    }
    const builtins = {};
    for (const bi of c.builtins || []) {
        const m = /^(\w+)\(([^)]*)\)\s*(?:->\s*(\w+))?/.exec(bi.sig || bi.name);
        if (!m) continue;
        const args = m[2].trim() ? m[2].split(",").length : 0;
        const type = m[3] || (["min", "max", "clamp"].includes(m[1]) ? "common" : "arg");
        builtins[m[1]] = { args, type, doc: bi.doc || "", sig: bi.sig, ...(bi.draws ? { draws: bi.draws } : {}) };
    }
    const params = {};
    for (const p of c.params || []) params[p.name] = { type: p.type, unit: p.unit || "", doc: p.doc || "", min: p.min, max: p.max, default: p.default };
    return { slots, builtins, common, params, fromGame: true };
}

export function setCatalog(c) {
    if (!c) return;
    if (Array.isArray(c.slots)) c = catalogFromExport(c);
    if (c.fromGame) { CATALOG.slots = c.slots; CATALOG.builtins = c.builtins; CATALOG.common = c.common; CATALOG.params = c.params; CATALOG.fromGame = true; return; }
    if (c.slots) CATALOG.slots = Object.assign({}, CATALOG.slots, c.slots);
    if (c.builtins) CATALOG.builtins = Object.assign({}, CATALOG.builtins, c.builtins);
    if (c.common) CATALOG.common = c.common;
    if (c.params) CATALOG.params = c.params;
}

export function slotKey(d) { return d.name === "objective" ? "objective." + d.mode : d.name; }
export function slotInfo(d) { return CATALOG.slots[d.name]; }

// ---------------------------------------------------------------- tree helpers
export const clone = (x) => JSON.parse(JSON.stringify(x));
const curveTypes = new WeakMap();   // curve decl -> its result type (a cache kept off the AST so it never shows in a compare)

// keys starting with "_" are page-side caches, never part of the brain
export function deepEqual(a, b, ignore = null) {
    if (a === b) return true;
    if (typeof a !== typeof b || a === null || b === null || typeof a !== "object") return false;
    if (Array.isArray(a) !== Array.isArray(b)) return false;
    if (Array.isArray(a)) return a.length === b.length && a.every((x, i) => deepEqual(x, b[i], ignore));
    const keep = (o) => (k) => o[k] !== undefined && k[0] !== "_" && !(ignore && ignore.has(k));
    const ka = Object.keys(a).filter(keep(a)), kb = Object.keys(b).filter(keep(b));
    if (ka.length !== kb.length) return false;
    return ka.every((k) => Object.prototype.hasOwnProperty.call(b, k) && deepEqual(a[k], b[k], ignore));
}
// first difference (path), for test messages
export function firstDiff(a, b, path = "") {
    if (deepEqual(a, b)) return null;
    if (typeof a !== "object" || typeof b !== "object" || !a || !b) return `${path}: ${JSON.stringify(a)} != ${JSON.stringify(b)}`;
    const keys = new Set([...Object.keys(a), ...Object.keys(b)]);
    for (const k of keys) { const d = firstDiff(a[k], b[k], path + "/" + k); if (d) return d; }
    return path + ": differ";
}
export function stripIds(x) {
    if (Array.isArray(x)) return x.map(stripIds);
    if (x && typeof x === "object") { const o = {}; for (const k in x) if (k !== "id" && k !== "ref" && k[0] !== "_") o[k] = stripIds(x[k]); return o; }
    return x;
}

// child expressions of an expression, as [key, node] (index for args)
export function exprChildren(e) {
    switch (e.op) {
        case "num": case "bool": case "name": case "hole": return [];
        case "call": return e.args.map((a, i) => ["args." + i, a]);
        case "index": return [["index", e.index]];
        case "neg": case "not": return [["a", e.a]];
        case "in": return [["a", e.a]];
        case "?:": return [["c", e.c], ["a", e.a], ["b", e.b]];
        default: return [["a", e.a], ["b", e.b]];
    }
}
export function walkExpr(e, f) { f(e); for (const [, c] of exprChildren(e)) walkExpr(c, f); }
export function stmtExprs(s) {
    switch (s.op) {
        case "set": return [s.value];
        case "return": return s.values;
        case "goto": return [s.point];
        case "if": return s.arms.map((a) => a.cond);
        default: return [];
    }
}
export function walkStmts(body, f) {
    for (const s of body) {
        f(s);
        if (s.op === "if") { for (const a of s.arms) walkStmts(a.body, f); if (s.else) walkStmts(s.else.body, f); }
    }
}
export function walkBrainExprs(brain, f) {
    for (const d of brain.decls) {
        if (d.kind === "curve" && d.form === "expr") walkExpr(d.body, (e) => f(e, d));
        if (d.kind === "slot") walkStmts(d.body, (s) => stmtExprs(s).forEach((x) => walkExpr(x, (e) => f(e, d))));
    }
}

// ids: number nodes n1.. in document order (as exportbrain), keeping ids already there
export function numberIds(brain) {
    let max = 0;
    const all = [];
    const visit = (n) => { all.push(n); const m = /^n(\d+)$/.exec(n.id || ""); if (m) max = Math.max(max, +m[1]); };
    for (const d of brain.decls) {
        visit(d);
        if (d.kind === "curve" && d.form === "expr") walkExpr(d.body, visit);
        if (d.kind === "slot") walkStmts(d.body, (s) => { visit(s); stmtExprs(s).forEach((x) => walkExpr(x, visit)); });
    }
    for (const n of all) if (!n.id) n.id = "n" + ++max;
    return brain;
}
// after a text edit: keep the ids of nodes at the same place with the same op (graph positions survive small edits)
export function carryIds(oldB, newB) {
    if (!oldB) return numberIds(newB);
    const key = (d) => d.kind + ":" + (d.kind === "slot" ? slotKey(d) : d.name);
    const olds = new Map(oldB.decls.map((d) => [key(d), d]));
    const pair = (a, b) => { if (a && b && a.op === b.op && a.id && !b.id) b.id = a.id; };
    const pairExpr = (a, b) => {
        if (!a || !b || a.op !== b.op) return;
        pair(a, b);
        const ca = exprChildren(a), cb = exprChildren(b);
        for (let i = 0; i < Math.min(ca.length, cb.length); i++) if (ca[i][0] === cb[i][0]) pairExpr(ca[i][1], cb[i][1]);
    };
    const pairBody = (A, B) => {
        for (let i = 0; i < Math.min(A.length, B.length); i++) {
            const a = A[i], b = B[i];
            if (a.op !== b.op) continue;
            pair(a, b);
            const ea = stmtExprs(a), eb = stmtExprs(b);
            for (let k = 0; k < Math.min(ea.length, eb.length); k++) pairExpr(ea[k], eb[k]);
            if (a.op === "if") {
                for (let k = 0; k < Math.min(a.arms.length, b.arms.length); k++) pairBody(a.arms[k].body, b.arms[k].body);
                if (a.else && b.else) pairBody(a.else.body, b.else.body);
            }
        }
    };
    for (const d of newB.decls) {
        const o = olds.get(key(d));
        if (!o) continue;
        if (o.id) d.id = o.id;
        if (d.kind === "curve" && d.form === "expr" && o.form === "expr") pairExpr(o.body, d.body);
        if (d.kind === "slot") pairBody(o.body, d.body);
    }
    // ids must stay unique: drop any duplicate the pairing made, then number the rest
    const seen = new Set();
    const dedupe = (n) => { if (n.id) { if (seen.has(n.id)) delete n.id; else seen.add(n.id); } };
    for (const d of newB.decls) {
        dedupe(d);
        if (d.kind === "curve" && d.form === "expr") walkExpr(d.body, dedupe);
        if (d.kind === "slot") walkStmts(d.body, (s) => { dedupe(s); stmtExprs(s).forEach((x) => walkExpr(x, dedupe)); });
    }
    return numberIds(newB);
}

// ---------------------------------------------------------------- symbols
export function symbols(brain) {
    const S = { params: new Map(), curves: new Map(), lists: new Map(), tables: new Map(), slots: new Map() };
    for (const d of brain.decls) {
        if (d.kind === "param") S.params.set(d.name, d);
        else if (d.kind === "curve") S.curves.set(d.name, d);
        else if (d.kind === "list") S.lists.set(d.name, d);
        else if (d.kind === "table") S.tables.set(d.name, d);
        else if (d.kind === "slot") S.slots.set(slotKey(d), d);
    }
    return S;
}
export function highlightSets(brain) {
    const S = symbols(brain);
    const sensors = new Set(Object.keys(CATALOG.common));
    for (const s of Object.values(CATALOG.slots)) { for (const k in s.sensors) sensors.add(k); for (const k in s.outputs) sensors.add(k); }
    for (const p of POINTS) sensors.add(p);
    return { params: new Set(S.params.keys()), curves: new Set(S.curves.keys()), lists: new Set([...S.lists.keys(), ...S.tables.keys()]),
        weapons: new Set([...WEAPONS, "none", ...BRAIN_MODES]), builtins: new Set(Object.keys(CATALOG.builtins)), sensors };
}

// ---------------------------------------------------------------- validation (the editor's checks; checkbrain is authoritative)
// returns [{severity: "error"|"warning", msg, line, col, where}] and per-slot info {draws: "roll()/gauss()/chance() count"}
export function validate(brain) {
    const out = [];
    const S = symbols(brain);
    const L = (n) => (n && n._loc) || {};
    const err = (msg, n, where, sev = "error") => out.push({ severity: sev, msg, line: L(n).line, col: L(n).col, where });
    const names = new Map();
    for (const d of brain.decls) {
        if (d.kind === "slot") {
            const key = slotKey(d);
            if (names.has("slot:" + key)) err(`slot ${key} is written twice`, d, key);
            names.set("slot:" + key, d);
            if (!CATALOG.slots[d.name]) err(`no slot called '${d.name}' (the game calls: ${Object.keys(CATALOG.slots).join(", ")})`, d, key);
            if (d.name === "objective" && !BRAIN_MODES.includes(d.mode) && d.mode !== "default") err(`'${d.mode}' isn't a mode (${BRAIN_MODES.join(" ")} default)`, d, key);
            continue;
        }
        if (names.has(d.name)) err(`'${d.name}' is declared twice`, d, d.name);
        names.set(d.name, d);
        if (WEAPONS.includes(d.name) || BRAIN_MODES.includes(d.name) || CATALOG.builtins[d.name]) err(`'${d.name}' is a weapon, mode or builtin name`, d, d.name);
        if (d.kind === "param") {
            const cp = CATALOG.params && CATALOG.params[d.name];
            if (cp && cp.type && cp.type !== d.type) err(`${d.name} is ${cp.type} (the game reads it as that)`, d, d.name);
            if (cp && typeof cp.min === "number") d.values.forEach((v) => { if (v < cp.min || v > cp.max) err(`${d.name}: ${v} is outside ${cp.min}..${cp.max}`, d, d.name, "warning"); });
        }
        if (d.kind === "list") {
            const seen = new Set();
            for (const w of d.items) {
                if (!WEAPONS.includes(w)) err(`'${w}' isn't a weapon`, d, d.name);
                if (seen.has(w)) err(`'${w}' is in the list twice`, d, d.name);
                seen.add(w);
            }
            if (d.items.length > 11) err("a list holds 1 to 11 weapons", d, d.name);
        }
        if (d.kind === "table") for (const [w] of d.entries) if (!WEAPONS.includes(w)) err(`'${w}' isn't a weapon`, d, d.name);
        if (d.kind === "curve" && d.form === "expr") {
            const sc = { kind: "curve", arg: d.arg, argType: d.argType, d };
            typeOf(d.body, sc, S, (m, n) => err(m, n, d.name));
        }
    }
    const info = {};
    for (const d of brain.decls) {
        if (d.kind !== "slot" || !CATALOG.slots[d.name]) continue;
        const key = slotKey(d), cat = CATALOG.slots[d.name];
        const sc = { kind: "slot", d, cat, locals: new Map() };
        let draws = 0;
        const e = (m, n) => err(m, n, key);
        const checkBody = (body, assigned) => {
            for (const s of body) {
                for (const x of stmtExprs(s)) {
                    walkExpr(x, (n) => { if (n.op === "call" && CATALOG.builtins[n.fn] && CATALOG.builtins[n.fn].draws) draws += CATALOG.builtins[n.fn].draws; });
                }
                if (s.op === "set") {
                    const t = typeOf(s.value, sc, S, e);
                    if (cat.outputs[s.name]) { if (t && !assignable(t, cat.outputs[s.name])) e(`${s.name} is ${cat.outputs[s.name]}, this is ${t}`, s); }
                    else if (cat.sensors[s.name] || CATALOG.common[s.name]) e(`${s.name} is a sensor (read-only)`, s);
                    else if (S.params.has(s.name)) e(`${s.name} is a param (read-only in a slot)`, s);
                    else if (!sc.locals.has(s.name)) sc.locals.set(s.name, t);
                    assigned.add(s.name);
                } else if (s.op === "if") {
                    for (const a of s.arms) { const t = typeOf(a.cond, sc, S, e); if (t && t !== "bool") e(`an if condition is bool, not ${t}`, a.cond); }
                    for (const a of s.arms) checkBody(a.body, new Set(assigned));
                    if (s.else) checkBody(s.else.body, new Set(assigned));
                } else if (s.op === "return") {
                    const outs = Object.entries(cat.outputs);
                    if (s.values.length > outs.length) e(`${key} has ${outs.length} output${outs.length === 1 ? "" : "s"} (${outs.map((o) => o[0]).join(", ")})`, s);
                    s.values.forEach((v, i) => { const t = typeOf(v, sc, S, e); if (outs[i] && t && !assignable(t, outs[i][1])) e(`${outs[i][0]} is ${outs[i][1]}, this is ${t}`, v); });
                } else if (s.op === "goto") {
                    if (d.name !== "objective") e("goto is for objective slots", s);
                    const t = typeOf(s.point, sc, S, e);
                    if (t && t !== "point") e(`goto takes a point (${POINTS.join(", ")}, site(i)), not ${t}`, s.point);
                } else if (["fallback", "seek", "evade"].includes(s.op) && d.name !== "objective") e(`${s.op} is for objective slots`, s);
            }
        };
        checkBody(d.body, new Set());
        if (d.name === "objective" && !endsInAction(d.body)) err(`objective ${d.mode} must end in an action (goto, fallback, seek, evade) on every path`, d, key);
        info[key] = { draws };
    }
    return { problems: out, info, errors: out.filter((p) => p.severity === "error") };
}
function endsInAction(body) {
    const last = body[body.length - 1];
    if (!last) return false;
    if (["goto", "fallback", "seek", "evade"].includes(last.op)) return true;
    if (last.op === "if") return !!last.else && last.arms.every((a) => endsInAction(a.body)) && endsInAction(last.else.body);
    // an action earlier on a path that always runs
    return body.some((s) => ["goto", "fallback", "seek", "evade"].includes(s.op));
}
const NUM = new Set(["int", "f32", "f64"]);
function assignable(t, want) {
    if (t === want) return true;
    if (want === "f64" && (t === "f32" || t === "int")) return true;
    if (want === "f32" && t === "int") return true;
    return false;
}
function common(a, b) { if (a === "f64" || b === "f64") return "f64"; if (a === "f32" || b === "f32") return "f32"; return "int"; }
export function typeOf(e, sc, S, err) {
    const t = (x) => typeOf(x, sc, S, err);
    switch (e.op) {
        case "num": return e.type;
        case "bool": return "bool";
        case "hole": err("an empty input", e); return null;
        case "name": {
            const n = e.name;
            if (sc.kind === "curve" && n === sc.arg) return sc.argType;
            if (sc.kind === "slot") {
                if (sc.locals.has(n)) return sc.locals.get(n);
                if (sc.cat.outputs[n]) return sc.cat.outputs[n];
                if (sc.cat.sensors[n]) return sc.cat.sensors[n];
                if (CATALOG.common[n]) return CATALOG.common[n];
                if (POINTS.includes(n)) return "point";
            }
            if (S.params.has(n)) { const p = S.params.get(n); return p.type; }
            if (S.lists.has(n)) return "list";
            if (WEAPONS.includes(n) || n === "none") return "weapon";
            if (BRAIN_MODES.includes(n)) return "mode";
            err(sc.kind === "curve" ? `'${n}' isn't the argument or a param (curves use no sensors)` : `unknown name '${n}' (not a sensor of this slot, a local set earlier, a param, a weapon or a mode)`, e);
            return null;
        }
        case "call": {
            const b = CATALOG.builtins[e.fn];
            const args = e.args.map(t);
            if (b) {
                if (b.args !== undefined && e.args.length !== b.args) err(`${e.fn} takes ${b.args} argument${b.args === 1 ? "" : "s"}`, e);
                if (b.type === "common") return args.reduce((a, x) => (a && x && NUM.has(a) && NUM.has(x) ? common(a, x) : a || x), null);
                if (b.type === "arg") return args[0];
                if (e.fn === "firstAllowed" && e.args[0] && args[0] !== "list") err("firstAllowed(list, weapon)", e);
                return b.type;
            }
            const c = S.curves.get(e.fn);
            if (c) {
                if (e.args.length !== 1) err(`curve ${e.fn} takes one argument`, e);
                if (sc.kind === "curve" && sc.d === c) err("a curve can't call itself", e);
                if (c.form !== "expr") return "f32";
                if (!curveTypes.has(c)) curveTypes.set(c, typeOf(c.body, { kind: "curve", arg: c.arg, argType: c.argType, d: c }, S, () => {}));
                return curveTypes.get(c) || "f32";
            }
            err(`unknown function '${e.fn}'`, e);
            return null;
        }
        case "index": {
            if (!S.tables.has(e.table)) err(`no table called '${e.table}'`, e);
            const it = t(e.index);
            if (it && it !== "weapon") err("a table is indexed by a weapon", e.index);
            return "f32";
        }
        case "neg": { const a = t(e.a); if (a && !NUM.has(a)) err(`- of ${a}`, e); return a; }
        case "not": { const a = t(e.a); if (a && a !== "bool") err(`not of ${a} (bool only)`, e); return "bool"; }
        case "and": case "or": { const a = t(e.a), b = t(e.b); if ((a && a !== "bool") || (b && b !== "bool")) err(`${e.op} takes bools`, e); return "bool"; }
        case "?:": { const c = t(e.c), a = t(e.a), b = t(e.b); if (c && c !== "bool") err("?: condition is bool", e.c); return a && b && NUM.has(a) && NUM.has(b) ? common(a, b) : a || b; }
        case "in": { t(e.a); if (!S.lists.has(e.list)) err(`no list called '${e.list}'`, e); return "bool"; }
        case "==": case "!=": t(e.a); t(e.b); return "bool";
        case "<": case "<=": case ">": case ">=": {
            const a = t(e.a), b = t(e.b);
            if ((a && !NUM.has(a)) || (b && !NUM.has(b))) err(`${e.op} compares numbers`, e);
            return "bool";
        }
        default: {   // + - * /
            const a = t(e.a), b = t(e.b);
            if ((a && !NUM.has(a)) || (b && !NUM.has(b))) { err(`${e.op} takes numbers, not ${!NUM.has(a) ? a : b}`, e); return null; }
            if (e.op === "/" && a === "int" && b === "int") err("int / int is an error: write 1.0 / 2", e);
            return a && b ? common(a, b) : a || b;
        }
    }
}

// ---------------------------------------------------------------- evaluation (Values panel: curve plots, odds)
// env: {level, args: {name: value}, sensors: {name: value}}; random draws return their mean (roll 0.5, gauss 0)
// unless env.rnd is given. Returns a number / bool / string (weapon) or undefined when it can't tell.
export function evalExpr(e, brain, env, S = symbols(brain)) {
    const ev = (x) => evalExpr(x, brain, env, S);
    const f = (x, t) => (t === "f32" ? Math.fround(x) : x);
    switch (e.op) {
        case "num": return e.v;
        case "bool": return e.v;
        case "hole": return undefined;
        case "name": {
            const n = e.name;
            if (env.args && n in env.args) return env.args[n];
            if (env.sensors && n in env.sensors) return env.sensors[n];
            const p = S.params.get(n);
            if (p) return paramValue(p, env.level);
            if (WEAPONS.includes(n) || BRAIN_MODES.includes(n)) return n;
            return undefined;
        }
        case "call": {
            const a = e.args.map(ev);
            switch (e.fn) {
                case "roll": return env.rnd ? env.rnd() : 0.5;
                case "gauss": return env.gauss ? env.gauss() : 0;
                case "chance": return a[0] > 0;
                case "min": return Math.min(a[0], a[1]);
                case "max": return Math.max(a[0], a[1]);
                case "clamp": return Math.min(Math.max(a[0], a[1]), a[2]);
                case "abs": return Math.abs(a[0]);
                case "sqrt": return Math.sqrt(a[0]);
                case "exp": return Math.exp(a[0]);
                case "f32": return Math.fround(a[0]);
                case "f64": return a[0];
            }
            const c = S.curves.get(e.fn);
            if (c) return evalCurve(c, a[0], brain, env.level, S);
            return undefined;
        }
        case "index": { const t = S.tables.get(e.table), w = ev(e.index); if (!t) return undefined; const en = t.entries.find((x) => x[0] === w); return en ? en[1] : (t.else ?? 0); }
        case "neg": return -ev(e.a);
        case "not": return !ev(e.a);
        case "and": return ev(e.a) && ev(e.b);
        case "or": return ev(e.a) || ev(e.b);
        case "?:": return ev(e.c) ? ev(e.a) : ev(e.b);
        case "in": { const l = S.lists.get(e.list); return l ? l.items.includes(ev(e.a)) : undefined; }
        case "==": return ev(e.a) === ev(e.b);
        case "!=": return ev(e.a) !== ev(e.b);
        case "<": return ev(e.a) < ev(e.b);
        case "<=": return ev(e.a) <= ev(e.b);
        case ">": return ev(e.a) > ev(e.b);
        case ">=": return ev(e.a) >= ev(e.b);
        case "+": return ev(e.a) + ev(e.b);
        case "-": return ev(e.a) - ev(e.b);
        case "*": return ev(e.a) * ev(e.b);
        case "/": return ev(e.a) / ev(e.b);
    }
    return undefined;
}
export function paramValue(p, level) { return p.values.length === 4 ? p.values[level] : p.values[0]; }
export function evalCurve(c, x, brain, level, S = symbols(brain)) {
    if (c.form === "steps") { let y = c.base; for (const [px, py] of c.points) if (x > px) y = py; return y; }
    if (c.form === "points") {
        const P = c.points;
        if (x <= P[0][0]) return P[0][1];
        for (let i = 0; i + 1 < P.length; i++) if (x <= P[i + 1][0]) return P[i][1] + (P[i + 1][1] - P[i][1]) * ((x - P[i][0]) / (P[i + 1][0] - P[i][0]));
        return P[P.length - 1][1];
    }
    return evalExpr(c.body, brain, { level, args: { [c.arg]: x } }, S);
}

// "share of / chance / per second" numbers shown as %: the decision points that roll the dice
// returns [{slot, text, kind: "roll"|"chance", expr}] for every `roll() < X` / `X > roll()` and `chance(X)`
export function decisionOdds(brain) {
    const out = [];
    for (const d of brain.decls) {
        if (d.kind !== "slot") continue;
        const key = slotKey(d);
        walkStmts(d.body, (s) => stmtExprs(s).forEach((x) => walkExpr(x, (e) => {
            const isRoll = (n) => n.op === "call" && n.fn === "roll";
            if ((e.op === "<" || e.op === "<=") && isRoll(e.a)) out.push({ slot: key, kind: "roll", expr: e.b, stmt: s });
            else if ((e.op === ">" || e.op === ">=") && isRoll(e.b)) out.push({ slot: key, kind: "roll", expr: e.a, stmt: s });
            else if (e.op === "call" && e.fn === "chance") out.push({ slot: key, kind: "chance", expr: e.args[0], stmt: s });
        })));
    }
    return out;
}

// ---------------------------------------------------------------- provenance areas (serve.py provenance.json)
const AREA_OF_PARAM = {
    targetAimHeight: "targeting", targetAngleWeight: "targeting", targetKeepBonus: "targeting",
    canLead: "weapons", weapons: "weapons", weaponClose: "weapons", weaponMid: "weapons", weaponFar: "weapons", knifeThrowMax: "weapons", rifleScopeDist: "weapons", rocketFeetHeight: "weapons",
};
const SECTION_AREA = [[/perception/i, "perception"], [/^#\s*aim/i, "aim"], [/fight/i, "fight-movement"], [/route|look/i, "roam"], [/objective|hunt|roam/i, "roam"], [/weapon/i, "weapons"]];
// the provenance area of each declaration: { declKey -> area }
export function declAreas(brain) {
    const res = new Map();
    let section = "perception";
    for (const d of brain.decls) {
        for (const c of d.comments || []) for (const [re, a] of SECTION_AREA) if (re.test(c)) { section = a; break; }
        let area = section;
        if (d.kind === "param") area = AREA_OF_PARAM[d.name] || section;
        else if (d.kind === "curve") area = /react|hear|sound|see/i.test(d.name) ? "perception" : section;
        else if (d.kind === "list" || d.kind === "table") area = "weapons";
        else if (d.kind === "slot") {
            const c = CATALOG.slots[d.name];
            area = d.name === "objective" ? "mode/" + d.mode : (c && c.area) || section;
        }
        res.set(declKey(d), area);
    }
    return res;
}
export const declKey = (d) => d.kind + ":" + (d.kind === "slot" ? slotKey(d) : d.name);

// what differs from the default brain, per declaration: [{key, kind, name, change: "changed"|"added"|"removed"|"default", area}]
// A user brain only overrides what it writes, so a declaration it leaves out is the default one ("default").
export function diffBrains(brain, def) {
    const strip = (d) => { const c = stripIds(d); delete c.comments; delete c.trail; return c; };
    const mine = new Map(brain.decls.map((d) => [declKey(d), d])), theirs = new Map(def.decls.map((d) => [declKey(d), d]));
    const areasMine = declAreas(brain), areasDef = declAreas(def);
    const out = [];
    for (const [k, d] of mine) {
        const o = theirs.get(k);
        if (!o) out.push({ key: k, decl: d, change: "added", area: areasMine.get(k) });
        else if (!deepEqual(strip(d), strip(o))) out.push({ key: k, decl: d, def: o, change: "changed", area: areasDef.get(k) || areasMine.get(k) });
    }
    for (const [k, o] of theirs) if (!mine.has(k)) out.push({ key: k, def: o, change: "default", area: areasDef.get(k) });
    return out;
}

// ---------------------------------------------------------------- generic node parts (blocks and graph build on these)
// decompose(node) -> {cat: "decl"|"stmt"|"expr", op, fields, inputs: [[path, expr]], stacks: [[path, [stmt]]]}
// compose(parts) -> the node again; compose(decompose(n)) deep-equals n. Paths are dotted ("arms.0.cond", "args.1").
const EXPR_OPS = new Set(["num", "bool", "name", "hole", "call", "index", "neg", "not", "in", "?:", "+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!=", "and", "or"]);
export const isExpr = (n) => !!n && typeof n === "object" && EXPR_OPS.has(n.op) && !n.kind;
export function decompose(n) {
    const fields = clone(n), inputs = [], stacks = [];
    let cat, op;
    if (n.kind) {
        cat = "decl"; op = n.kind;
        if (n.kind === "slot") { stacks.push(["body", n.body]); delete fields.body; }
        if (n.kind === "curve" && n.form === "expr") { inputs.push(["body", n.body]); delete fields.body; }
    } else if (isExpr(n)) {
        cat = "expr"; op = n.op;
        for (const [k, c] of exprChildren(n)) inputs.push([k, c]);
        delete fields.a; delete fields.b; delete fields.c; delete fields.index;
        if (n.op === "call") fields.args = n.args.length;
    } else {
        cat = "stmt"; op = n.op;
        if (n.op === "set") { inputs.push(["value", n.value]); delete fields.value; }
        else if (n.op === "goto") { inputs.push(["point", n.point]); delete fields.point; }
        else if (n.op === "return") { n.values.forEach((v, i) => inputs.push(["values." + i, v])); fields.values = n.values.length; }
        else if (n.op === "if") {
            fields.arms = n.arms.map((a, i) => { inputs.push([`arms.${i}.cond`, a.cond]); stacks.push([`arms.${i}.body`, a.body]); const m = clone(a); delete m.cond; delete m.body; return m; });
            if (n.else) { stacks.push(["else.body", n.else.body]); fields.else = clone(n.else); delete fields.else.body; }
        }
    }
    delete fields.op; delete fields.kind;
    return { cat, op, fields, inputs, stacks };
}
export function compose(p) {
    const n = p.cat === "decl" ? { kind: p.op } : { op: p.op };
    const f = clone(p.fields);
    if (p.cat === "expr" && p.op === "call") { const k = f.args; f.args = new Array(k).fill(null).map(() => ({ op: "hole" })); }
    if (p.cat === "stmt" && p.op === "return") { const k = f.values; f.values = new Array(k).fill(null).map(() => ({ op: "hole" })); }
    Object.assign(n, f);
    if (p.cat === "expr") for (const k of ["a", "b", "c", "index"]) if (needsInput(p.op, k)) n[k] = { op: "hole" };
    if (p.cat === "stmt" && p.op === "set") n.value = { op: "hole" };
    if (p.cat === "stmt" && p.op === "goto") n.point = { op: "hole" };
    if (p.cat === "decl" && p.op === "curve" && n.form === "expr") n.body = { op: "hole" };
    if (p.cat === "decl" && p.op === "slot") n.body = [];
    if (p.cat === "stmt" && p.op === "if") { n.arms.forEach((a) => { a.cond = { op: "hole" }; a.body = []; }); if (n.else) n.else.body = []; }
    for (const [path, v] of p.inputs) setPath(n, path, v);
    for (const [path, v] of p.stacks) setPath(n, path, v);
    return n;
}
function needsInput(op, k) {
    if (op === "num" || op === "bool" || op === "name" || op === "hole" || op === "call") return false;
    if (op === "index") return k === "index";
    if (op === "neg" || op === "not" || op === "in") return k === "a";
    if (op === "?:") return k === "a" || k === "b" || k === "c";
    return k === "a" || k === "b";
}
export function setPath(o, path, v) {
    const ks = path.split(".");
    for (let i = 0; i < ks.length - 1; i++) o = o[/^\d+$/.test(ks[i]) ? +ks[i] : ks[i]];
    const last = ks[ks.length - 1];
    o[/^\d+$/.test(last) ? +last : last] = v;
}
export function getPath(o, path) {
    for (const k of path.split(".")) { if (o == null) return undefined; o = o[/^\d+$/.test(k) ? +k : k]; }
    return o;
}

// new nodes for the palettes (blocks and graph)
export function newExpr(op, extra = {}) {
    const H = () => ({ op: "hole" });
    switch (op) {
        case "num": return { op, type: extra.type || "f32", v: extra.v ?? 0 };
        case "bool": return { op, v: extra.v ?? true };
        case "name": return { op, name: extra.name || "x" };
        case "call": return { op, fn: extra.fn, args: new Array(extra.argc ?? (CATALOG.builtins[extra.fn]?.args ?? 1)).fill(0).map(H) };
        case "index": return { op, table: extra.table, index: H() };
        case "neg": case "not": return { op, a: H() };
        case "in": return { op, a: H(), list: extra.list };
        case "?:": return { op, c: H(), a: H(), b: H() };
        default: return { op, a: H(), b: H() };
    }
}
export function newStmt(op, extra = {}) {
    const H = () => ({ op: "hole" });
    switch (op) {
        case "set": return { op, name: extra.name || "x", value: H() };
        case "return": return { op, values: new Array(extra.n ?? 1).fill(0).map(H) };
        case "goto": return { op, point: extra.point ? { op: "name", name: extra.point } : H() };
        case "if": return { op, arms: [{ cond: H(), body: [{ op: "pass" }] }], ...(extra.else ? { else: { body: [{ op: "pass" }] } } : {}) };
        default: return { op };
    }
}
