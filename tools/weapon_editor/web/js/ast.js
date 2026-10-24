// The weapon set's JSON AST: symbols, validation against the catalog (the editor's checks; wpncheck /
// checkweapons are authoritative), tree helpers, an evaluator (curves, the "try a hit" panel, the MOCK range runs the
// hit / kill slots with it), the field values a set gives, and editing a field in the AST.
// The generic parts (decompose / compose / ids) are the bot editor's ast.js.

import { WEAPONS } from "./catalog.js";
import { parse, print, canonNum } from "./dsl.js";

let CAT = null;   // the normalized catalog (catalog.js), set by the app
export function setCatalog(c) { CAT = c; }
export function catalog() { return CAT; }

export const clone = (x) => JSON.parse(JSON.stringify(x));
export function slotKey(d) { return d.weapon ? d.name + " " + d.weapon : d.name; }
export const declKey = (d) => d.kind + ":" + (d.kind === "slot" ? slotKey(d) : d.kind === "label" ? d.weapon : d.name);
const isField = (n) => !!(CAT && CAT.byKey.has(n));

// ---------------------------------------------------------------- tree helpers
export function deepEqual(a, b) {
    if (a === b) return true;
    if (typeof a !== typeof b || a === null || b === null || typeof a !== "object") return false;
    if (Array.isArray(a) !== Array.isArray(b)) return false;
    if (Array.isArray(a)) return a.length === b.length && a.every((x, i) => deepEqual(x, b[i]));
    const keep = (o) => (k) => o[k] !== undefined && k[0] !== "_";
    const ka = Object.keys(a).filter(keep(a)), kb = Object.keys(b).filter(keep(b));
    if (ka.length !== kb.length) return false;
    return ka.every((k) => Object.prototype.hasOwnProperty.call(b, k) && deepEqual(a[k], b[k]));
}
export function firstDiff(a, b, path = "") {
    if (deepEqual(a, b)) return null;
    if (typeof a !== "object" || typeof b !== "object" || !a || !b) return `${path}: ${JSON.stringify(a)} != ${JSON.stringify(b)}`;
    for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) { const d = firstDiff(a[k], b[k], path + "/" + k); if (d) return d; }
    return path + ": differ";
}
export function stripIds(x) {
    if (Array.isArray(x)) return x.map(stripIds);
    if (x && typeof x === "object") { const o = {}; for (const k in x) if (k !== "id" && k[0] !== "_") o[k] = stripIds(x[k]); return o; }
    return x;
}
export function exprChildren(e) {
    switch (e.op) {
        case "num": case "bool": case "name": case "hole": return [];
        case "call": return e.args.map((a, i) => ["args." + i, a]);
        case "neg": case "not": return [["a", e.a]];
        case "?:": return [["c", e.c], ["a", e.a], ["b", e.b]];
        default: return [["a", e.a], ["b", e.b]];
    }
}
export function walkExpr(e, f) { f(e); for (const [, c] of exprChildren(e)) walkExpr(c, f); }
export function stmtExprs(s) {
    switch (s.op) {
        case "set": return [s.value];
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
export function numberIds(set) {
    let max = 0;
    const all = [];
    const visit = (n) => { all.push(n); const m = /^n(\d+)$/.exec(n.id || ""); if (m) max = Math.max(max, +m[1]); };
    for (const d of set.decls) {
        visit(d);
        if (d.kind === "curve" && d.form === "expr") walkExpr(d.body, visit);
        if (d.kind === "slot") walkStmts(d.body, (s) => { visit(s); stmtExprs(s).forEach((x) => walkExpr(x, visit)); });
    }
    for (const n of all) if (!n.id) n.id = "n" + ++max;
    return set;
}
// after a text edit: keep the ids of nodes at the same place with the same op (graph positions survive small edits)
export function carryIds(oldS, newS) {
    if (!oldS) return numberIds(newS);
    const olds = new Map(oldS.decls.map((d) => [declKey(d), d]));
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
    for (const d of newS.decls) {
        const o = olds.get(declKey(d));
        if (!o) continue;
        if (o.id) d.id = o.id;
        if (d.kind === "curve" && d.form === "expr" && o.form === "expr") pairExpr(o.body, d.body);
        if (d.kind === "slot") pairBody(o.body, d.body);
    }
    const seen = new Set();
    const dedupe = (n) => { if (n.id) { if (seen.has(n.id)) delete n.id; else seen.add(n.id); } };
    for (const d of newS.decls) {
        dedupe(d);
        if (d.kind === "curve" && d.form === "expr") walkExpr(d.body, dedupe);
        if (d.kind === "slot") walkStmts(d.body, (s) => { dedupe(s); stmtExprs(s).forEach((x) => walkExpr(x, dedupe)); });
    }
    return numberIds(newS);
}

// ---------------------------------------------------------------- symbols
export function symbols(set) {
    const S = { params: new Map(), curves: new Map(), labels: new Map(), slots: new Map() };
    for (const d of set.decls) {
        if (d.kind === "param") S.params.set(d.name, d);
        else if (d.kind === "curve") S.curves.set(d.name, d);
        else if (d.kind === "label") S.labels.set(d.weapon, d);
        else if (d.kind === "slot") S.slots.set(slotKey(d), d);
    }
    return S;
}
export function slotCat(name) { return CAT && CAT.slotByName.get(name); }
export function sensorsOf(name) { const c = slotCat(name); const o = {}; if (c) for (const s of c.sensors) o[s.name] = s.type; return o; }
export function outputsOf(name) { const c = slotCat(name); const o = {}; if (c) for (const s of c.outputs) o[s.name] = s.type; return o; }
export function highlightSets(set) {
    const S = symbols(set);
    const sensors = new Set();
    for (const s of (CAT && CAT.slots) || []) { for (const x of s.sensors) sensors.add(x.name); for (const x of s.outputs) sensors.add(x.name); }
    return { fields: new Set(CAT ? CAT.byKey.keys() : []), params: new Set(S.params.keys()), curves: new Set(S.curves.keys()),
        weapons: new Set(WEAPONS), builtins: new Set(Object.keys((CAT && CAT.builtinShape) || {})), sensors };
}

// ---------------------------------------------------------------- validation
// [{severity: "error"|"warning", msg, line, col, where}]
export function validate(set) {
    const out = [];
    const S = symbols(set);
    const L = (n) => (n && n._loc) || {};
    const err = (msg, n, where, sev = "error") => out.push({ severity: sev, msg, line: L(n).line, col: L(n).col, where });
    const names = new Set();
    const builtins = (CAT && CAT.builtinShape) || {};
    for (const d of set.decls) {
        const key = declKey(d);
        if (names.has(key)) err(d.kind === "slot" ? `slot ${slotKey(d)} is written twice` : d.kind === "label" ? `${d.weapon} has two labels` : `'${d.name}' is declared twice`, d, key);
        names.add(key);
        if (d.kind === "label") {
            if (!WEAPONS.includes(d.weapon)) err(`'${d.weapon}' isn't a weapon (${WEAPONS.join(" ")})`, d, key);
            if (d.text.length > 24) err("a label is at most 24 characters", d, key);
            if (!/^[A-Z0-9 ]*$/.test(d.text)) err("a label holds A-Z, 0-9 and spaces only", d, key);
        } else if (d.kind === "param") {
            const f = CAT && CAT.byKey.get(d.name);
            const dot = d.name.indexOf(".");
            if (f) {
                if (f.readOnly) err(`${d.name} is the weapon's kind: it can't be changed`, d, key);
                if (f.type !== d.type) err(`${d.name} is ${f.type} (the game reads it as that)`, d, key);
                else if (f.type !== "bool" && ((typeof f.min === "number" && d.value < f.min) || (typeof f.max === "number" && d.value > f.max)))
                    err(`${d.name}: ${d.value} is outside ${f.min}..${f.max}: the game clamps it to ${d.value < f.min ? f.min : f.max}`, d, key, "warning");
            } else if (dot > 0 && WEAPONS.includes(d.name.slice(0, dot))) err(`${d.name}: ${d.name.slice(0, dot)} has no field '${d.name.slice(dot + 1)}'`, d, key);
            else if (dot > 0) err(`'${d.name}': a dotted name is a weapon's field; the set's own constants are plain names`, d, key, "warning");
            if (WEAPONS.includes(d.name) || builtins[d.name] || ["hit", "kill"].includes(d.name)) err(`'${d.name}' is a weapon, slot or builtin name`, d, key);
        } else if (d.kind === "curve") {
            const dot = d.name.indexOf(".");
            if (dot > 0 && !(WEAPONS.includes(d.name.slice(0, dot)) && d.name.slice(dot + 1) === "falloff")) err(`'${d.name}': the only per-weapon curve is <weapon>.falloff`, d, key, "warning");
            if (dot > 0 && d.form === "expr" && d.name.slice(dot + 1) === "falloff") err("a weapon's falloff curve is data: points x: y, ... or steps base, x: y, ...", d, key);
            else if (d.form === "expr") typeOf(d.body, { kind: "curve", arg: d.arg, argType: d.argType, d }, S, (m, n) => err(m, n, key));
        } else if (d.kind === "slot") {
            if (!slotCat(d.name)) { err(`no slot called '${d.name}' (hit, kill)`, d, key); continue; }
            if (d.weapon && !WEAPONS.includes(d.weapon)) { err(`'${d.weapon}' isn't a weapon (${WEAPONS.join(" ")})`, d, key); continue; }
            const sc = { kind: "slot", d, sensors: sensorsOf(d.name), outputs: outputsOf(d.name), locals: new Map() };
            const e = (m, n) => err(m, n, key);
            const checkBody = (body) => {
                for (const s of body) {
                    if (s.op === "set") {
                        const t = typeOf(s.value, sc, S, e);
                        if (sc.outputs[s.name]) { if (t && !assignable(t, sc.outputs[s.name])) e(`'${s.name}' is ${sc.outputs[s.name]}, not ${t}${t === "f64" && sc.outputs[s.name] === "f32" ? " (write f32(...))" : ""}`, s); }
                        else if (sc.sensors[s.name]) e(`${s.name} is a sensor (read-only)`, s);
                        else if (S.params.has(s.name) || isField(s.name)) e(`${s.name} is a param (read-only in a slot)`, s);
                        else if (!sc.locals.has(s.name)) sc.locals.set(s.name, t);
                    } else if (s.op === "if") {
                        for (const a of s.arms) { const t = typeOf(a.cond, sc, S, e); if (t && t !== "bool") e(`an if condition is bool, not ${t}`, a.cond); }
                        for (const a of s.arms) checkBody(a.body);
                        if (s.else) checkBody(s.else.body);
                    }
                }
            };
            checkBody(d.body);
        }
    }
    return { problems: out, errors: out.filter((p) => p.severity === "error") };
}
const NUM = new Set(["int", "f32", "f64"]);
const CURVE_TYPING = new Set();
function assignable(t, want) {
    if (t === want) return true;
    if (want === "f64" && (t === "f32" || t === "int")) return true;
    if (want === "f32" && t === "int") return true;   // (as the game: f64 never narrows by itself, write f32(...))
    return false;
}
function common(a, b) { if (a === "f64" || b === "f64") return "f64"; if (a === "f32" || b === "f32") return "f32"; return "int"; }
export function typeOf(e, sc, S, err) {
    const t = (x) => typeOf(x, sc, S, err);
    const builtins = (CAT && CAT.builtinShape) || {};
    switch (e.op) {
        case "num": return e.type;
        case "bool": return "bool";
        case "hole": err("an empty input", e); return null;
        case "name": {
            const n = e.name;
            if (sc.kind === "curve" && n === sc.arg) return sc.argType;
            if (sc.kind === "slot") {
                if (sc.locals.has(n)) return sc.locals.get(n);
                if (sc.outputs[n]) return sc.outputs[n];
                if (sc.sensors[n]) return sc.sensors[n];
            }
            if (S.params.has(n)) return S.params.get(n).type;
            if (isField(n)) return CAT.byKey.get(n).type;
            if (WEAPONS.includes(n)) return "weapon";
            err(sc.kind === "curve" ? `'${n}' isn't the argument or a param (curves read no sensors)` : `unknown name '${n}' (not a sensor or output of this slot, a local set earlier, a param or a weapon)`, e);
            return null;
        }
        case "call": {
            const b = builtins[e.fn];
            const args = e.args.map(t);
            if (b) {
                if (b.args !== undefined && e.args.length !== b.args) err(`${e.fn} takes ${b.args} argument${b.args === 1 ? "" : "s"}`, e);
                if (b.type === "common") return args.reduce((a, x) => (a && x && NUM.has(a) && NUM.has(x) ? common(a, x) : a || x), null);
                if (b.type === "arg") return args[0];
                return b.type;
            }
            const c = S.curves.get(e.fn);
            if (c) {
                if (e.args.length !== 1) err(`curve ${e.fn} takes one argument`, e);
                if (sc.kind === "curve" && sc.d === c) { err("a curve can't call itself", e); return "f32"; }
                if (args[0] && (!NUM.has(args[0]) || !assignable(args[0], c.argType || "f32"))) err(`the curve takes an ${c.argType || "f32"}, not ${args[0]}${args[0] === "f64" ? " (write f32(...))" : ""}`, e.args[0] || e);
                if (c.form !== "expr") return "f32";
                if (CURVE_TYPING.has(c)) return "f32";   // (a cycle between curves: the game reports it on its own)
                CURVE_TYPING.add(c);
                try { return typeOf(c.body, { kind: "curve", arg: c.arg, argType: c.argType, d: c }, S, () => {}) || "f32"; } finally { CURVE_TYPING.delete(c); }
            }
            err(`unknown function '${e.fn}'`, e);
            return null;
        }
        case "neg": { const a = t(e.a); if (a && !NUM.has(a)) err(`- of ${a}`, e); return a; }
        case "not": { const a = t(e.a); if (a && a !== "bool") err(`not of ${a} (bool only)`, e); return "bool"; }
        case "and": case "or": { const a = t(e.a), b = t(e.b); if ((a && a !== "bool") || (b && b !== "bool")) err(`${e.op} takes bools`, e); return "bool"; }
        case "?:": { const c = t(e.c), a = t(e.a), b = t(e.b); if (c && c !== "bool") err("?: condition is bool", e.c); return a && b && NUM.has(a) && NUM.has(b) ? common(a, b) : a || b; }
        case "==": case "!=": t(e.a); t(e.b); return "bool";
        case "<": case "<=": case ">": case ">=": {
            const a = t(e.a), b = t(e.b);
            if ((a && !NUM.has(a)) || (b && !NUM.has(b))) err(`${e.op} compares numbers`, e);
            return "bool";
        }
        default: {
            const a = t(e.a), b = t(e.b);
            if ((a && !NUM.has(a)) || (b && !NUM.has(b))) { err(`${e.op} takes numbers, not ${!NUM.has(a) ? a : b}`, e); return null; }
            if (e.op === "/" && a === "int" && b === "int") err("int / int is an error: write 1.0 / 2", e);
            return a && b ? common(a, b) : a || b;
        }
    }
}

// ---------------------------------------------------------------- evaluation
// env: {args, vars (sensors / outputs / locals, written by set)}; params and fields read from the set
export function evalExpr(e, set, env, S = symbols(set), values = null) {
    const ev = (x) => evalExpr(x, set, env, S, values);
    switch (e.op) {
        case "num": return e.v;
        case "bool": return e.v;
        case "hole": return undefined;
        case "name": {
            const n = e.name;
            if (env.args && n in env.args) return env.args[n];
            if (env.vars && n in env.vars) return env.vars[n];
            const p = S.params.get(n);
            if (p) return p.value;
            if (values && values.has(n)) return values.get(n);
            if (CAT && CAT.byKey.has(n)) return CAT.byKey.get(n).default;
            if (WEAPONS.includes(n)) return n;
            return undefined;
        }
        case "call": {
            const a = e.args.map(ev);
            switch (e.fn) {
                case "min": return Math.min(a[0], a[1]);
                case "max": return Math.max(a[0], a[1]);
                case "clamp": return Math.min(Math.max(a[0], a[1]), a[2]);
                case "abs": return Math.abs(a[0]);
                case "sqrt": return Math.sqrt(a[0]);
                case "floor": return Math.floor(a[0]);
                case "ceil": return Math.ceil(a[0]);
                case "pow": return Math.pow(a[0], a[1]);
                case "lerp": return a[0] + (a[1] - a[0]) * a[2];
                case "f32": return Math.fround(a[0]);
                case "f64": return a[0];
                case "int": return Math.trunc(a[0]);
            }
            const c = S.curves.get(e.fn);
            if (c) return evalCurve(c, a[0], set, S, values);
            return undefined;
        }
        case "neg": return -ev(e.a);
        case "not": return !ev(e.a);
        case "and": return ev(e.a) && ev(e.b);
        case "or": return ev(e.a) || ev(e.b);
        case "?:": return ev(e.c) ? ev(e.a) : ev(e.b);
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
export function evalCurve(c, x, set, S = symbols(set), values = null) {
    if (c.form === "steps") { let y = c.base; for (const [px, py] of c.points) if (x > px) y = py; return y; }
    if (c.form === "points") {
        const P = c.points;
        if (x <= P[0][0]) return P[0][1];
        for (let i = 0; i + 1 < P.length; i++) if (x <= P[i + 1][0]) return P[i][1] + (P[i + 1][1] - P[i][1]) * ((x - P[i][0]) / (P[i + 1][0] - P[i][0]));
        return P[P.length - 1][1];
    }
    return evalExpr(c.body, set, { args: { [c.arg]: x } }, S, values);
}
// run a hit / kill slot: the weapon's own slot if the set has one, else the general one. sensors: {name: value};
// outputs start at their defaults (hit: damage = the game's damage, knock 1, heal 0; kill: heal 0, refund no).
// Returns {outputs, slot: key | null, trace: [lines run]}.
export function runSlot(set, name, weapon, sensors, S = symbols(set), values = null) {
    const d = S.slots.get(name + " " + weapon) || S.slots.get(name);
    const outputs = name === "hit" ? { damage: sensors.damage ?? 0, knock: 1, heal: 0 } : { heal: 0, refund: false };
    if (!d) return { outputs, slot: null, trace: [] };
    const vars = { ...sensors, ...outputs, weapon };
    const env = { vars };
    const trace = [];
    let steps = 0;
    const run = (body) => {
        for (const s of body) {
            if (++steps > 10000) return true;
            if (s._loc) trace.push(s._loc.line);
            if (s.op === "return") return true;
            if (s.op === "set") vars[s.name] = evalExpr(s.value, set, env, S, values);
            else if (s.op === "if") {
                let done = false;
                for (const a of s.arms) if (evalExpr(a.cond, set, env, S, values)) { if (run(a.body)) return true; done = true; break; }
                if (!done && s.else && run(s.else.body)) return true;
            }
        }
        return false;
    };
    run(d.body);
    for (const k in outputs) if (k in vars) outputs[k] = vars[k];
    if (name === "hit") { outputs.damage = Math.max(0, +outputs.damage || 0); outputs.knock = +outputs.knock || 0; outputs.heal = Math.max(0, +outputs.heal || 0); }
    else { outputs.heal = Math.max(0, +outputs.heal || 0); outputs.refund = !!outputs.refund; }
    return { outputs, slot: slotKey(d), trace };
}

// ---------------------------------------------------------------- the field values a set gives
// Map key -> value for every catalog field: the default, or the set's param (clamped as the game clamps it)
export function fieldValues(set) {
    const v = new Map(CAT.defaults);
    const clamped = [];
    for (const d of set ? set.decls : []) {
        if (d.kind !== "param") continue;
        const f = CAT.byKey.get(d.name);
        if (!f || f.readOnly || f.type !== d.type) continue;
        let x = d.value;
        if (f.type !== "bool") {
            if (typeof f.min === "number" && x < f.min) { x = f.min; clamped.push(d.name); }
            if (typeof f.max === "number" && x > f.max) { x = f.max; clamped.push(d.name); }
        }
        v.set(d.name, x);
    }
    v.clamped = clamped;
    return v;
}
export function labelsOf(set) { const m = new Map(); for (const d of set ? set.decls : []) if (d.kind === "label") m.set(d.weapon, d.text); return m; }
export function curvesOf(set) { const m = new Map(); for (const d of set ? set.decls : []) if (d.kind === "curve") m.set(d.name, d); return m; }
export function changedFields(values) { return CAT.fields.filter((f) => !sameVal(values.get(f.key), f.default, f.type)).map((f) => f.key); }
export function sameVal(a, b, type) { if (type === "bool") return !!a === !!b; return Math.abs(+a - +b) <= 1e-6 * Math.max(1, Math.abs(+b)); }

// a new AST with field `key` = v: the param changed in place; back at the default it is removed (a set writes only
// what it changes, unless the line carries a comment); a new one goes after the last field param of a weapon at or
// before this one in the catalog order (else after the labels, else first)
export function withField(set, key, v) {
    const f = CAT.byKey.get(key);
    const s = { ...set, decls: set.decls.slice() };
    const i = s.decls.findIndex((d) => d.kind === "param" && d.name === key);
    const isDef = sameVal(v, f.default, f.type);
    const value = f.type === "bool" ? !!v : f.type === "int" ? Math.trunc(v) : +canonNum(v, "f32");
    if (i >= 0) {
        if (isDef && !(s.decls[i].comments && s.decls[i].comments.some((c) => c)) && !s.decls[i].trail) s.decls.splice(i, 1);
        else s.decls[i] = { ...s.decls[i], type: f.type, value, ...(s.decls[i]._loc ? {} : {}) };
        return s;
    }
    if (isDef) return s;
    const order = new Map(CAT.fields.map((x, k) => [x.key, k]));
    const me = order.get(key);
    let at = -1;
    s.decls.forEach((d, k) => { if (d.kind === "param" && order.has(d.name) && order.get(d.name) < me) at = k; });
    if (at < 0) s.decls.forEach((d, k) => { if (d.kind === "label") at = k; });
    if (at < 0) { const firstOwn = s.decls.findIndex((d) => d.kind === "param" && order.has(d.name) && order.get(d.name) > me); at = firstOwn >= 0 ? firstOwn - 1 : -1; }
    s.decls.splice(at + 1, 0, { kind: "param", name: key, type: f.type, value });
    return s;
}
export function withLabel(set, weapon, text) {
    const s = { ...set, decls: set.decls.slice() };
    const i = s.decls.findIndex((d) => d.kind === "label" && d.weapon === weapon);
    if (!text) { if (i >= 0) s.decls.splice(i, 1); return s; }
    if (i >= 0) { s.decls[i] = { ...s.decls[i], text }; return s; }
    const order = new Map(WEAPONS.map((w, k) => [w, k]));
    let at = -1;
    s.decls.forEach((d, k) => { if (d.kind === "label" && order.get(d.weapon) < order.get(weapon)) at = k; });
    s.decls.splice(at + 1, 0, { kind: "label", weapon, text });
    return s;
}
export function emptySet(name) { return { format: "qweapons-ast", version: 1, name: name || "my weapons", decls: [], tail: [] }; }
export function reparse(set) { return parse(print(set)); }

// ---------------------------------------------------------------- the whole-set views (blocks and graph)
// groups: "general" (the set's own constants, curves and the slots for every weapon), then each weapon with what it
// changes (its fields, label, falloff curve, own slots). Every declaration lands in exactly one group, file order inside.
export function weaponOfDecl(d) {
    if (d.kind === "label") return WEAPONS.includes(d.weapon) ? d.weapon : "general";
    if (d.kind === "slot") return d.weapon && WEAPONS.includes(d.weapon) ? d.weapon : "general";
    const dot = d.name.indexOf(".");
    if (dot > 0 && WEAPONS.includes(d.name.slice(0, dot))) return d.name.slice(0, dot);
    return "general";
}
export function wholeGroups(set) {
    const by = new Map([["general", { area: "general", label: "Every weapon", decls: [] }], ...WEAPONS.map((w) => [w, { area: w, label: (CAT && CAT.byId.get(w) ? CAT.byId.get(w).title : w), decls: [] }])]);
    set.decls.forEach((d, i) => by.get(weaponOfDecl(d)).decls.push(i));
    return [...by.values()].filter((g) => g.decls.length);
}

// ---------------------------------------------------------------- generic node parts (blocks and graph build on these)
const EXPR_OPS = new Set(["num", "bool", "name", "hole", "call", "neg", "not", "?:", "+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!=", "and", "or"]);
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
        delete fields.a; delete fields.b; delete fields.c;
        if (n.op === "call") fields.args = n.args.length;
    } else {
        cat = "stmt"; op = n.op;
        if (n.op === "set") { inputs.push(["value", n.value]); delete fields.value; }
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
    Object.assign(n, f);
    if (p.cat === "expr") for (const k of ["a", "b", "c"]) if (needsInput(p.op, k)) n[k] = { op: "hole" };
    if (p.cat === "stmt" && p.op === "set") n.value = { op: "hole" };
    if (p.cat === "decl" && p.op === "curve" && n.form === "expr") n.body = { op: "hole" };
    if (p.cat === "decl" && p.op === "slot") n.body = [];
    if (p.cat === "stmt" && p.op === "if") { n.arms.forEach((a) => { a.cond = { op: "hole" }; a.body = []; }); if (n.else) n.else.body = []; }
    for (const [path, v] of p.inputs) setPath(n, path, v);
    for (const [path, v] of p.stacks) setPath(n, path, v);
    return n;
}
function needsInput(op, k) {
    if (op === "num" || op === "bool" || op === "name" || op === "hole" || op === "call") return false;
    if (op === "neg" || op === "not") return k === "a";
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
export function newExpr(op, extra = {}) {
    const H = () => ({ op: "hole" });
    switch (op) {
        case "num": return { op, type: extra.type || "f32", v: extra.v ?? 0 };
        case "bool": return { op, v: extra.v ?? true };
        case "name": return { op, name: extra.name || "x" };
        case "call": return { op, fn: extra.fn, args: new Array(extra.argc ?? (((CAT && CAT.builtinShape[extra.fn]) || {}).args ?? 1)).fill(0).map(H) };
        case "neg": case "not": return { op, a: H() };
        case "?:": return { op, c: H(), a: H(), b: H() };
        default: return { op, a: H(), b: H() };
    }
}
export function newStmt(op, extra = {}) {
    const H = () => ({ op: "hole" });
    switch (op) {
        case "set": return { op, name: extra.name || "x", value: H() };
        case "if": return { op, arms: [{ cond: H(), body: [{ op: "pass" }] }], ...(extra.else ? { else: { body: [{ op: "pass" }] } } : {}) };
        default: return { op };
    }
}
