// Adapted from tools/bot_editor/web/js/ast.js at c7aae27 (the brain's JSON AST helpers), for the mode logic:
// the catalog (catalog.js until `quark_tests exportmode` gives the game's), tree helpers, the editor's checks
// (types team / player / zone / phase / ..., procs inlined with no recursion, one loop level, limits), the static
// cost of each slot, the merge with the built-in texts (common, then the base mode, then the file) and the diff
// against them. Function names follow ast.js so a later shared module is an easy merge.

import { builtinCatalog, MODES, WEAPONS } from "./catalog.js";

export { MODES, WEAPONS };
export let CATALOG = builtinCatalog();

// `quark_tests exportmode`'s catalog -> the shape above. Lists of {name, ...} or maps are both read; whatever the
// export leaves out stays the built-in one (so a partial export still works).
export function catalogFromExport(c) {
    const out = builtinCatalog();
    const asMap = (x) => (Array.isArray(x) ? Object.fromEntries(x.filter((e) => e && e.name).map((e) => [e.name, e])) : x && typeof x === "object" ? x : null);
    const slots = asMap(c.slots);
    if (slots) for (const [n, s] of Object.entries(slots)) {
        const old = out.slots[n] || {};
        let args = s.args ?? s.sensors ?? old.args ?? {};
        if (Array.isArray(args)) args = Object.fromEntries(args.map((a) => (typeof a === "string" ? [a, "any"] : [a.name, a.type])));
        let o = s.out ?? s.output ?? old.out;
        if (o && !Array.isArray(o)) o = [o.name, o.type];
        out.slots[n] = { ...old, kind: s.kind || (o ? "query" : "event"), when: s.when || old.when || "", doc: s.doc || old.doc || "", args, ...(o ? { out: o } : {}), phase: s.phase || old.phase || "live", object: s.object || old.object };
    }
    for (const k of ["fields", "arrays", "functions", "actions", "objects", "params", "modeInfo", "constants"]) {
        const m = asMap(c[k]);
        if (m) out[k] = { ...out[k], ...m };
    }
    // a param's default may come as one number per mode
    for (const p of Object.values(out.params)) if (p && typeof p.default === "number") p.default = { "*": p.default };
    if (c.limits) out.limits = { ...out.limits, ...c.limits };
    if (Array.isArray(c.keywords)) out.keywords = c.keywords;
    out.fromGame = true;
    out.label = "the game's catalog (quark_tests exportmode)";
    return out;
}
export function setCatalog(c) {
    if (!c) return;
    const n = c.fromGame !== undefined && c.slots && !Array.isArray(c.slots) && c.label ? c : catalogFromExport(c);
    for (const k of Object.keys(CATALOG)) delete CATALOG[k];
    Object.assign(CATALOG, n);
}
export function resetCatalog() { const n = builtinCatalog(); for (const k of Object.keys(CATALOG)) delete CATALOG[k]; Object.assign(CATALOG, n); }

export const slotKey = (d) => (d.name === "timer" ? "timer." + d.timer : d.name);
export const declKey = (d) => d.kind + ":" + (d.kind === "slot" ? slotKey(d) : d.kind === "use" ? d.object : d.name);
export function slotInfo(d) { return CATALOG.slots[d.name]; }
export function paramDefault(name, mode) {
    const p = CATALOG.params[name];
    if (!p || !p.default) return undefined;
    return p.default[mode] ?? p.default["*"];
}

// ---------------------------------------------------------------- tree helpers
export const clone = (x) => JSON.parse(JSON.stringify(x));

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
export function firstDiff(a, b, path = "") {
    if (deepEqual(a, b)) return null;
    if (typeof a !== "object" || typeof b !== "object" || !a || !b) return `${path}: ${JSON.stringify(a)} != ${JSON.stringify(b)}`;
    const keys = new Set([...Object.keys(a), ...Object.keys(b)]);
    for (const k of keys) { const d = firstDiff(a[k], b[k], path + "/" + k); if (d) return d; }
    return path + ": differ";
}
export function stripIds(x) {
    if (Array.isArray(x)) return x.map(stripIds);
    if (x && typeof x === "object") { const o = {}; for (const k in x) if (k !== "id" && k[0] !== "_") o[k] = stripIds(x[k]); return o; }
    return x;
}

export function exprChildren(e) {
    switch (e.op) {
        case "num": case "bool": case "name": case "hole": case "str": return [];
        case "call": return e.args.map((a, i) => ["args." + i, a]);
        case "index": return [["index", e.index]];
        case "agg": { const r = []; if (e.set && e.set.set === "team") r.push(["set.team", e.set.team]); if (e.where) r.push(["where", e.where]); if (e.by) r.push(["by", e.by]); return r; }
        case "neg": case "not": return [["a", e.a]];
        case "?:": return [["c", e.c], ["a", e.a], ["b", e.b]];
        default: return [["a", e.a], ["b", e.b]];
    }
}
export function walkExpr(e, f) { f(e); for (const [, c] of exprChildren(e)) walkExpr(c, f); }
export function stmtExprs(s) {
    switch (s.op) {
        case "set": return s.index ? [s.index, s.value] : [s.value];
        case "return": return s.values;
        case "do": return s.args;
        case "if": return s.arms.map((a) => a.cond);
        case "for": { const r = []; if (s.set && s.set.set === "team") r.push(s.set.team); if (s.where) r.push(s.where); return r; }
        default: return [];
    }
}
export function walkStmts(body, f) {
    for (const s of body) {
        f(s);
        if (s.op === "if") { for (const a of s.arms) walkStmts(a.body, f); if (s.else) walkStmts(s.else.body, f); }
        if (s.op === "for") walkStmts(s.body, f);
    }
}
const hasBody = (d) => d.kind === "slot" || d.kind === "proc";
export function walkAstExprs(ast, f) {
    for (const d of ast.decls) {
        if (d.kind === "curve" && d.form === "expr") walkExpr(d.body, (e) => f(e, d));
        if (hasBody(d)) walkStmts(d.body, (s) => stmtExprs(s).forEach((x) => walkExpr(x, (e) => f(e, d))));
    }
}

export function numberIds(ast) {
    let max = 0;
    const all = [];
    const visit = (n) => { all.push(n); const m = /^n(\d+)$/.exec(n.id || ""); if (m) max = Math.max(max, +m[1]); };
    for (const d of ast.decls) {
        visit(d);
        if (d.kind === "curve" && d.form === "expr") walkExpr(d.body, visit);
        if (hasBody(d)) walkStmts(d.body, (s) => { visit(s); stmtExprs(s).forEach((x) => walkExpr(x, visit)); });
    }
    for (const n of all) if (!n.id) n.id = "n" + ++max;
    return ast;
}
export function carryIds(oldB, newB) {
    if (!oldB) return numberIds(newB);
    const olds = new Map(oldB.decls.map((d) => [declKey(d), d]));
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
            if (a.op === "for") pairBody(a.body, b.body);
        }
    };
    for (const d of newB.decls) {
        const o = olds.get(declKey(d));
        if (!o) continue;
        if (o.id) d.id = o.id;
        if (d.kind === "curve" && d.form === "expr" && o.form === "expr") pairExpr(o.body, d.body);
        if (hasBody(d) && hasBody(o)) pairBody(o.body, d.body);
    }
    const seen = new Set();
    const dedupe = (n) => { if (n.id) { if (seen.has(n.id)) delete n.id; else seen.add(n.id); } };
    for (const d of newB.decls) {
        dedupe(d);
        if (d.kind === "curve" && d.form === "expr") walkExpr(d.body, dedupe);
        if (hasBody(d)) walkStmts(d.body, (s) => { dedupe(s); stmtExprs(s).forEach((x) => walkExpr(x, dedupe)); });
    }
    return numberIds(newB);
}

// ---------------------------------------------------------------- symbols
export function symbols(ast) {
    const S = { params: new Map(), curves: new Map(), states: new Map(), timers: new Map(), procs: new Map(), uses: new Map(), slots: new Map() };
    for (const d of ast.decls) {
        if (d.kind === "param") S.params.set(d.name, d);
        else if (d.kind === "curve") S.curves.set(d.name, d);
        else if (d.kind === "state") S.states.set(d.name, d);
        else if (d.kind === "timer") S.timers.set(d.name, d);
        else if (d.kind === "proc") S.procs.set(d.name, d);
        else if (d.kind === "use") S.uses.set(d.object, d);
        else if (d.kind === "slot") S.slots.set(slotKey(d), d);
    }
    return S;
}
export function highlightSets(ast) {
    const S = symbols(ast);
    const sensors = new Set([...Object.keys(CATALOG.fields), ...Object.keys(CATALOG.arrays)]);
    for (const s of Object.values(CATALOG.slots)) for (const k in s.args) sensors.add(k);
    return {
        params: new Set(S.params.keys()), curves: new Set(S.curves.keys()), states: new Set([...S.states.keys(), ...S.timers.keys()]),
        consts: new Set([...Object.keys(CATALOG.constants), ...WEAPONS, "everyone", "playing"]),
        actions: new Set([...Object.keys(CATALOG.actions), ...S.procs.keys()]),
        builtins: new Set([...Object.keys(CATALOG.functions), ...Object.keys(CATALOG.aggregates)]), sensors,
    };
}

// ---------------------------------------------------------------- types
const NUM = new Set(["int", "f32", "f64"]);
const TEXTABLE = new Set(["text", "player", "team", "int", "none", "str"]);
function assignable(t, want) {
    if (!t || !want || want === "any") return true;
    if (t === want) return true;
    if (want === "f64" && (t === "f32" || t === "int")) return true;
    if (want === "f32" && t === "int") return true;
    if (want === "num") return NUM.has(t);
    if ((want === "team" || want === "player") && t === "none") return true;
    if (want === "winner") return t === "team" || t === "player" || t === "none";
    return false;
}
function common(a, b) { if (a === "f64" || b === "f64") return "f64"; if (a === "f32" || b === "f32") return "f32"; return "int"; }
function sameKind(a, b) {
    if (!a || !b) return true;
    if (NUM.has(a) && NUM.has(b)) return true;
    if (a === "none") return b === "team" || b === "player" || b === "none";
    if (b === "none") return a === "team" || a === "player";
    return a === b;
}
const LOOP_N = 32;

// ---------------------------------------------------------------- validation (the editor's checks; checkmode is the judge)
// returns {problems: [{severity, msg, line, col, where}], info: {slotKey: {insns, worst, interpreted?}}, errors}
export function validate(ast, opts = {}) {
    const out = [];
    const S = symbols(ast);
    const L = (n) => (n && n._loc) || {};
    const err = (msg, n, where, sev = "error") => out.push({ severity: sev, msg, line: L(n).line, col: L(n).col, where });
    const base = opts.base || ast.base;
    const mi = CATALOG.modeInfo[base];
    const names = new Map();
    // (functions and actions are called with '(': a param may share a name with one, as `respawn` does)
    const reserved = (n) => CATALOG.fields[n] || CATALOG.arrays[n] || CATALOG.constants[n] !== undefined || WEAPONS.includes(n) || n === "everyone" || n === "playing";
    const lim = CATALOG.limits;
    let nParams = 0, nStates = 0, nTimers = 0, nProcs = 0, stateValues = 0;
    if (ast.base && opts.base && ast.base !== opts.base) err(`base ${ast.base} doesn't match the file's mode=${opts.base} (the game refuses that)`, ast.decls[0] || null, "base");
    if (base && base !== "common" && !CATALOG.modes.includes(base)) err(`'${base}' isn't a mode (${CATALOG.modes.join(" ")})`, null, "base");
    for (const d of ast.decls) {
        const key = declKey(d);
        if (names.has(key)) { err(`${key.replace(":", " ")} is written twice`, d, key); continue; }
        names.set(key, d);
        if (d.kind === "slot") {
            const c = CATALOG.slots[d.name];
            if (!c) err(`no slot called '${d.name}' (the game calls: ${Object.keys(CATALOG.slots).join(", ")})`, d, key);
            else if (d.name === "timer" && !S.timers.has(d.timer)) err(`no timer called '${d.timer}' (declare it: timer ${d.timer})`, d, key);
            else if (c.object && !S.uses.has(c.object)) err(`slot ${d.name} only fires with 'use ${c.object}'`, d, key, "warning");
            continue;
        }
        if (d.kind === "use") {
            const o = CATALOG.objects[d.object];
            if (!o) err(`no object called '${d.object}' (${Object.keys(CATALOG.objects).join(" ")})`, d, key);
            else if (mi && !mi.uses.includes(d.object)) err(`${d.object} isn't part of ${base}: the server runs it, but the clients' HUD won't show it and bots won't play it`, d, key, "warning");
            continue;
        }
        const plain = d.name;
        const clash = [...names.entries()].find(([k, x]) => k !== key && x.kind !== "slot" && x.kind !== "use" && x.name === plain);
        if (clash) err(`'${plain}' is declared twice (as ${clash[1].kind} and ${d.kind})`, d, key);
        if (reserved(plain)) err(`'${plain}' is a name the game already uses (a sensor, an array or a constant)`, d, key);
        if (d.kind === "param") {
            nParams++;
            const cp = CATALOG.params[d.name];
            if (cp && cp.type && cp.type !== d.type) err(`${d.name} is ${cp.type} (the game reads it as that)`, d, key);
            if (cp && typeof cp.min === "number" && typeof d.value === "number" && (d.value < cp.min || d.value > cp.max)) err(`${d.name}: ${d.value} is outside ${cp.min}..${cp.max}${cp.guess === false || CATALOG.fromGame ? "" : " (the editor's suggested range)"}`, d, key, "warning");
        }
        if (d.kind === "state") { nStates++; stateValues += d.per === "player" ? 32 : d.per === "team" ? 2 : 1; }
        if (d.kind === "timer") nTimers++;
        if (d.kind === "proc") nProcs++;
        if (d.kind === "curve" && d.form === "expr") typeOf(d.body, { kind: "curve", arg: d.arg, argType: d.argType, d, uses: S.uses }, S, (m, n) => err(m, n, key));
    }
    if (nParams > lim.params) err(`${nParams} params: at most ${lim.params}`, null, "limits");
    if (nStates > lim.states) err(`${nStates} state declarations: at most ${lim.states}`, null, "limits");
    if (stateValues > lim.stateValues) err(`state holds ${stateValues} values: at most ${lim.stateValues} (a per-player array is 32, per-team 2)`, null, "limits");
    if (nTimers > lim.timers) err(`${nTimers} timers: at most ${lim.timers}`, null, "limits");
    if (nProcs > lim.procs) err(`${nProcs} procs: at most ${lim.procs}`, null, "limits");

    // procs: who calls whom (no recursion), and which slots call each (their arguments are the proc's)
    const calls = new Map();
    for (const d of ast.decls) if (hasBody(d)) {
        const set = new Set();
        walkStmts(d.body, (s) => { if (s.op === "do" && S.procs.has(s.fn)) set.add(s.fn); });
        calls.set(declKey(d), set);
    }
    const procCycle = (name, stack = []) => {
        if (stack.includes(name)) return [...stack, name];
        for (const c of calls.get("proc:" + name) || []) { const r = procCycle(c, [...stack, name]); if (r) return r; }
        return null;
    };
    for (const [n, d] of S.procs) { const cyc = procCycle(n); if (cyc && cyc[0] === n) err(`procs can't call themselves: ${cyc.join(" -> ")}`, d, "proc:" + n); }
    const callers = new Map();   // proc -> [slot decls] (directly or through other procs)
    for (const d of ast.decls) if (d.kind === "slot") {
        const seen = new Set();
        const go = (k) => { for (const c of calls.get(k) || []) if (!seen.has(c)) { seen.add(c); go("proc:" + c); } };
        go(declKey(d));
        for (const p of seen) { if (!callers.has(p)) callers.set(p, []); callers.get(p).push(d); }
    }

    const info = {};
    const cost = costs(ast, S);
    for (const d of ast.decls) {
        if (!hasBody(d)) continue;
        const key = declKey(d);
        const cat = d.kind === "slot" ? CATALOG.slots[d.name] : null;
        if (d.kind === "slot" && !cat) continue;
        let args = cat ? { ...cat.args } : {};
        if (d.kind === "proc") {
            // a proc is inlined into its callers: it may read an argument every calling slot has
            const cs = callers.get(d.name) || [];
            args = {};
            if (cs.length) {
                const first = CATALOG.slots[cs[0].name]?.args || {};
                for (const [k, t] of Object.entries(first)) if (cs.every((c) => (CATALOG.slots[c.name]?.args || {})[k] === t)) args[k] = t;
            }
        }
        const sc = { kind: d.kind, d, cat, args, locals: new Map(), vars: new Map(), inFor: false, inAgg: false, uses: S.uses };
        const e = (m, n, sev) => err(m, n, key, sev);
        const checkBody = (body) => {
            for (const s of body) {
                if (s.op === "set") checkSet(s, sc, S, e);
                else if (s.op === "if") {
                    for (const a of s.arms) { const t = typeOf(a.cond, sc, S, e); if (t && t !== "bool") e(`an if condition is bool, not ${t}`, a.cond); }
                    for (const a of s.arms) checkBody(a.body);
                    if (s.else) checkBody(s.else.body);
                } else if (s.op === "for") {
                    if (sc.inFor) e("loops can't nest (one 'for' level: 32 x 32 at worst with an aggregate inside)", s);
                    if (s.set.set === "team") { const t = typeOf(s.set.team, sc, S, e); if (t && !assignable(t, "team")) e(`team(...) takes a team, not ${t}`, s.set.team); }
                    const was = sc.inFor, old = sc.vars.get(s.var);
                    if (sc.args[s.var] || sc.locals.has(s.var)) e(`'${s.var}' is already a name here: pick another loop variable`, s);
                    sc.inFor = true; sc.vars.set(s.var, "player");
                    if (s.where) { const t = typeOf(s.where, sc, S, e); if (t && t !== "bool") e(`where takes a bool, not ${t}`, s.where); }
                    checkBody(s.body);
                    sc.inFor = was; if (old) sc.vars.set(s.var, old); else sc.vars.delete(s.var);
                } else if (s.op === "break") { if (!sc.inFor) e("break is only inside a for", s); }
                else if (s.op === "return") {
                    if (cat && cat.kind === "query") {
                        if (s.values.length !== 1) e(`${d.name} returns one value: ${cat.out[0]} (${cat.out[1]})`, s);
                        else { const t = typeOf(s.values[0], sc, S, e); if (t && !assignable(t, cat.out[1])) e(`${d.name} returns ${cat.out[0]}: ${cat.out[1]}, this is ${t}`, s.values[0]); }
                    } else if (s.values.length) e(d.kind === "proc" ? "a proc returns nothing (return ends it)" : `${d.name} is an event: return takes no value`, s);
                } else if (s.op === "do") checkDo(s, sc, S, e);
            }
        };
        checkBody(d.body);
        if (cat && cat.kind === "query" && !alwaysReturns(d.body)) err(`not every path of ${d.name} returns a value (the end of the slot gives the native default)`, d, key, "warning");
        const c = cost.get(key) || { insns: 0, worst: 0 };
        if (c.insns > lim.insnPerSlot) err(`${c.insns} instructions (procs inlined): at most ${lim.insnPerSlot} per slot`, d, key);
        if (c.worst > lim.worstPerCall) err(`worst case ${c.worst} instructions a call (loops x${LOOP_N}): at most ${lim.worstPerCall}`, d, key);
        info[key] = { insns: c.insns, worst: c.worst };
    }
    return { problems: out, info, errors: out.filter((p) => p.severity === "error") };
}
function alwaysReturns(body) {
    for (const s of body) {
        if (s.op === "return") return true;
        if (s.op === "if" && s.else && s.arms.every((a) => alwaysReturns(a.body)) && alwaysReturns(s.else.body)) return true;
    }
    return false;
}
function objectNeed(obj, sc, n, what, err, sev = "error") {
    if (obj && sc.uses && !sc.uses.has(obj)) err(`${what} needs 'use ${obj}'`, n, sev);
}
function checkSet(s, sc, S, e) {
    const vt = typeOf(s.value, sc, S, e);
    const n = s.name;
    if (s.index) {
        const it = typeOf(s.index, sc, S, e);
        const arr = CATALOG.arrays[n], st = S.states.get(n);
        if (arr) {
            if (!arr.rw) e(`${n}[...] is read-only`, s);
            objectNeed(arr.object, sc, s, n, e, "warning");
            if (it && !assignable(it, arr.index)) e(`${n} is indexed by a ${arr.index}, not ${it}`, s.index);
            if (vt && !assignable(vt, arr.type)) e(`${n}[...] is ${arr.type}, this is ${vt}`, s.value);
        } else if (st) {
            if (!st.per) e(`${n} is one value, not an array: ${n} = ...`, s);
            else if (it && !assignable(it, st.per)) e(`${n} is per ${st.per}, not ${it}`, s.index);
            if (vt && !assignable(vt, st.type)) e(`${n} is ${st.type}, this is ${vt}`, s.value);
        } else e(`no array called '${n}' (state arrays and score, objTime, it, out, found, sought, teamScore...)`, s);
        return;
    }
    if (sc.vars.has(n)) return e(`${n} is the loop's player: read-only`, s);
    if (sc.args[n]) return e(`${n} is the slot's argument: read-only`, s);
    const f = CATALOG.fields[n];
    if (f) {
        if (!f.rw) return e(`${n} is read-only`, s);
        if (vt && !assignable(vt, f.type)) e(`${n} is ${f.type}, this is ${vt}`, s.value);
        return;
    }
    if (CATALOG.arrays[n]) return e(`${n} is an array: ${n}[i] = ...`, s);
    if (S.params.has(n)) return e(`${n} is a param (read-only in a slot)`, s);
    const st = S.states.get(n);
    if (st) {
        if (st.per) return e(`${n} is per ${st.per}: ${n}[...] = ...`, s);
        if (vt && !assignable(vt, st.type)) e(`${n} is ${st.type}, this is ${vt}`, s.value);
        return;
    }
    if (CATALOG.constants[n] !== undefined || CATALOG.functions[n] || CATALOG.actions[n] || S.timers.has(n) || S.curves.has(n) || S.procs.has(n)) return e(`${n} is a name the game or this file already uses`, s);
    if (sc.locals.has(n)) { const lt = sc.locals.get(n); if (lt && vt && !assignable(vt, lt)) e(`${n} is ${lt} (its first assignment), this is ${vt}`, s.value); }
    else sc.locals.set(n, vt === "none" ? "player" : vt);
}
function checkArgs(fn, want, args, sc, S, e, n, textAfter = false) {
    if (!textAfter && args.length !== want.length) e(`${fn} takes ${want.length} argument${want.length === 1 ? "" : "s"}`, n);
    if (textAfter && args.length < want.length) e(`${fn} takes ${want.length} argument${want.length === 1 ? "" : "s"} before its text`, n);
    if (textAfter && args.length - want.length > CATALOG.limits.textArgs) e(`at most ${CATALOG.limits.textArgs} text arguments`, n);
    args.forEach((a, i) => {
        if (i < want.length) {
            const w = want[i];
            if (w === "timer") { if (a.op !== "name" || !S.timers.has(a.name)) e(`${fn}: argument ${i + 1} is a timer declared with 'timer <name>'`, a); return; }
            if (a.op === "str") { e(`${fn}: argument ${i + 1} isn't text`, a); return; }
            const t = typeOf(a, sc, S, e);
            if (t && !assignable(t, w)) e(`${fn}: argument ${i + 1} is ${w}, this is ${t}`, a);
        } else {
            if (a.op === "str") return;
            const t = typeOf(a, sc, S, e);
            if (t && !TEXTABLE.has(t)) e(t === "f32" || t === "f64" ? `${t} in text: write int(x) (no float formatting)` : `${t} can't be printed`, a);
        }
    });
}
function checkDo(s, sc, S, e) {
    const a = CATALOG.actions[s.fn];
    if (a) {
        if (a.future) e(`${s.fn}() needs engine code that isn't written yet (design 2.5, phase 4)`, s, "warning");
        objectNeed(a.object, sc, s, s.fn + "()", e);
        checkArgs(s.fn, a.args, s.args, sc, S, e, s, !!a.text);
        if (sc.inAgg) e("an action can't run inside an aggregate", s);
        return;
    }
    if (S.procs.has(s.fn)) { if (s.args.length) e(`proc ${s.fn} takes no arguments`, s); return; }
    if (CATALOG.functions[s.fn] || S.curves.has(s.fn)) return e(`${s.fn}(...) has a value: assign it or use it in a condition`, s);
    e(`unknown action '${s.fn}'`, s);
}
export function typeOf(e, sc, S, err) {
    const t = (x) => typeOf(x, sc, S, err);
    switch (e.op) {
        case "num": return e.type;
        case "bool": return "bool";
        case "str": err("text only goes in a text action's arguments (announce, log, endRound...)", e); return "str";
        case "hole": err("an empty input", e); return null;
        case "name": {
            const n = e.name;
            if (sc.kind === "curve") {
                if (n === sc.arg) return sc.argType;
                if (S.params.has(n)) return S.params.get(n).type;
                err(`'${n}' isn't the argument or a param (curves read nothing else)`, e); return null;
            }
            if (sc.vars.has(n)) return sc.vars.get(n);
            if (sc.locals.has(n)) return sc.locals.get(n);
            if (sc.args[n]) return sc.args[n];
            const f = CATALOG.fields[n];
            if (f) { objectNeed(f.object, sc, e, n, err, "warning"); return f.type; }
            if (S.params.has(n)) return S.params.get(n).type;
            const st = S.states.get(n);
            if (st) { if (st.per) err(`${n} is per ${st.per}: ${n}[...]`, e); return st.type; }
            if (CATALOG.constants[n] !== undefined) return CATALOG.constants[n];
            if (WEAPONS.includes(n)) return "weapon";
            if (S.timers.has(n)) { err(`${n} is a timer: timerLeft(${n})`, e); return null; }
            if (CATALOG.arrays[n]) { err(`${n} is an array: ${n}[i]`, e); return null; }
            if (n === "everyone" || n === "playing") { err(`${n} is a player set: it goes after 'in' (for p in ${n}, count(p in ${n} where ...))`, e); return null; }
            err(`unknown name '${n}' (not a sensor, an argument of this slot, a local set earlier, a param, a state or a constant)`, e);
            return null;
        }
        case "index": {
            const it = t(e.index);
            const arr = CATALOG.arrays[e.array], st = S.states.get(e.array);
            if (arr) {
                objectNeed(arr.object, sc, e, e.array, err, "warning");
                if (it && !assignable(it, arr.index)) err(`${e.array} is indexed by a ${arr.index}, not ${it}`, e.index);
                return arr.type;
            }
            if (st) {
                if (!st.per) err(`${e.array} is one value, not an array`, e);
                else if (it && !assignable(it, st.per)) err(`${e.array} is per ${st.per}, not ${it}`, e.index);
                return st.type;
            }
            err(`no array called '${e.array}'`, e);
            return null;
        }
        case "agg": {
            const rt = CATALOG.aggregates[e.fn];
            if (sc.inAgg) err("an aggregate can't sit inside another aggregate", e);
            if (e.set.set === "team") { const tt = t(e.set.team); if (tt && !assignable(tt, "team")) err(`team(...) takes a team, not ${tt}`, e.set.team); }
            if (e.fn === "best" && !e.by) err("best(p in <set> by <number>) needs 'by'", e);
            if (e.fn !== "best" && e.by) err(`${e.fn} takes no 'by' (best does)`, e);
            const was = sc.inAgg, old = sc.vars.get(e.var);
            sc.inAgg = true; sc.vars.set(e.var, "player");
            if (e.where) { const w = t(e.where); if (w && w !== "bool") err(`where takes a bool, not ${w}`, e.where); }
            if (e.by) { const b = t(e.by); if (b && !NUM.has(b)) err(`by takes a number, not ${b}`, e.by); }
            sc.inAgg = was; if (old) sc.vars.set(e.var, old); else sc.vars.delete(e.var);
            return rt || null;
        }
        case "call": {
            const f = CATALOG.functions[e.fn];
            const args = () => e.args.map((a) => (a.op === "str" ? (err(`${e.fn}: no text here`, a), null) : t(a)));
            if (f) {
                if (e.fn === "timerLeft") { if (e.args.length !== 1 || e.args[0].op !== "name" || !S.timers.has(e.args[0].name)) err("timerLeft(<a timer declared with 'timer <name>'>)", e); return "f32"; }
                const at = args();
                if (e.args.length !== f.args.length) err(`${e.fn} takes ${f.args.length} argument${f.args.length === 1 ? "" : "s"}`, e);
                at.forEach((x, i) => { if (x && f.args[i] && !assignable(x, f.args[i])) err(`${e.fn}: argument ${i + 1} is ${f.args[i]}, this is ${x}`, e.args[i]); });
                objectNeed(f.object, sc, e, e.fn + "()", err, "warning");
                if (f.type === "common") return at.reduce((a, x) => (a && x && NUM.has(a) && NUM.has(x) ? common(a, x) : a || x), null);
                if (f.type === "arg") return at[0];
                return f.type;
            }
            const a = CATALOG.actions[e.fn];
            if (a) {
                if (!a.value) { err(`${e.fn}() is an action with no value: write it as a statement`, e); return null; }
                if (sc.inAgg) err("an action can't run inside an aggregate", e);
                objectNeed(a.object, sc, e, e.fn + "()", err);
                checkArgs(e.fn, a.args, e.args, sc, S, err, e);
                return a.value;
            }
            const c = S.curves.get(e.fn);
            if (c) {
                const at = args();
                if (e.args.length !== 1) err(`curve ${e.fn} takes one argument`, e);
                else if (at[0] && !NUM.has(at[0])) err(`curve ${e.fn} takes a number`, e.args[0]);
                if (sc.kind === "curve" && sc.d === c) err("a curve can't call itself", e);
                return c.form === "expr" ? (c.argType === "f64" ? "f64" : "f32") : "f32";
            }
            if (S.procs.has(e.fn)) { err(`proc ${e.fn} has no value: call it as a statement`, e); return null; }
            if (CATALOG.aggregates[e.fn]) { err(`${e.fn}(p in <set> where <cond>)`, e); return null; }
            err(`unknown function '${e.fn}'`, e);
            return null;
        }
        case "neg": { const a = t(e.a); if (a && !NUM.has(a)) err(`- of ${a}`, e); return a; }
        case "not": { const a = t(e.a); if (a && a !== "bool") err(`not of ${a} (bool only)`, e); return "bool"; }
        case "and": case "or": { const a = t(e.a), b = t(e.b); if ((a && a !== "bool") || (b && b !== "bool")) err(`${e.op} takes bools`, e); return "bool"; }
        case "?:": {
            const c = t(e.c), a = t(e.a), b = t(e.b);
            if (c && c !== "bool") err("?: condition is bool", e.c);
            if (a && b && NUM.has(a) && NUM.has(b)) return common(a, b);
            if (a && b && !sameKind(a, b)) err(`?: gives ${a} or ${b}: the two must be the same kind`, e);
            return a === "none" ? b : a || b;
        }
        case "==": case "!=": { const a = t(e.a), b = t(e.b); if (!sameKind(a, b)) err(`${e.op} compares two of the same kind, not ${a} and ${b}`, e); return "bool"; }
        case "<": case "<=": case ">": case ">=": {
            const a = t(e.a), b = t(e.b);
            if ((a && !NUM.has(a)) || (b && !NUM.has(b))) err(`${e.op} compares numbers`, e);
            return "bool";
        }
        default: {
            const a = t(e.a), b = t(e.b);
            if ((a && !NUM.has(a)) || (b && !NUM.has(b))) { err(`${e.op} takes numbers, not ${a && !NUM.has(a) ? a : b}`, e); return null; }
            if (e.op === "/" && a === "int" && b === "int") err("int / int is an error: write 1.0 / 2 (or f32(a) / f32(b))", e);
            return a && b ? common(a, b) : a || b;
        }
    }
}

// ---------------------------------------------------------------- static cost (the editor's estimate; checkmode's is the real one)
// insns: the slot's code size with procs inlined; worst: the most a call can run (each loop / aggregate x32, an
// if's longest arm)
export function costs(ast, S = symbols(ast)) {
    const res = new Map();
    const procMemo = new Map();
    const exprCost = (e) => {
        if (e.op === "agg") {
            let s = 2, w = 2;
            if (e.set.set === "team") { const c = exprCost(e.set.team); s += c.s; w += c.w; }
            for (const k of ["where", "by"]) if (e[k]) { const c = exprCost(e[k]); s += c.s + 1; w += (c.w + 1) * LOOP_N; }
            w += LOOP_N;
            return { s, w };
        }
        let s = 1, w = 1;
        for (const [, c] of exprChildren(e)) { const x = exprCost(c); s += x.s; w += x.w; }
        return { s, w };
    };
    const procCost = (name, stack) => {
        if (procMemo.has(name)) return procMemo.get(name);
        if (stack.includes(name)) return { s: 0, w: 0 };
        const p = S.procs.get(name);
        const c = p ? bodyCost(p.body, [...stack, name]) : { s: 0, w: 0 };
        procMemo.set(name, c);
        return c;
    };
    const bodyCost = (body, stack) => {
        let s = 0, w = 0;
        for (const st of body) {
            const ex = stmtExprs(st).map(exprCost);
            const es = ex.reduce((a, x) => a + x.s, 0), ew = ex.reduce((a, x) => a + x.w, 0);
            if (st.op === "if") {
                const arms = [...st.arms.map((a) => bodyCost(a.body, stack)), ...(st.else ? [bodyCost(st.else.body, stack)] : [])];
                s += 1 + es + arms.reduce((a, x) => a + x.s + 1, 0);
                w += 1 + ew + Math.max(0, ...arms.map((x) => x.w + 1));
            } else if (st.op === "for") {
                const b = bodyCost(st.body, stack);
                s += 3 + es + b.s;
                w += 3 + (ew + b.w + 2) * LOOP_N;
            } else if (st.op === "do" && S.procs.has(st.fn)) {
                const p = procCost(st.fn, stack);
                s += p.s; w += p.w;
            } else { s += 1 + es; w += 1 + ew; }
        }
        return { s, w };
    };
    for (const d of ast.decls) if (d.kind === "slot" || d.kind === "proc") { const c = bodyCost(d.body, d.kind === "proc" ? [d.name] : []); res.set(declKey(d), { insns: c.s, worst: c.w }); }
    return res;
}

// ---------------------------------------------------------------- evaluation (curve plots in Values)
export function evalExpr(e, ast, env, S = symbols(ast)) {
    const ev = (x) => evalExpr(x, ast, env, S);
    switch (e.op) {
        case "num": return e.v;
        case "bool": return e.v;
        case "name": {
            if (env.args && e.name in env.args) return env.args[e.name];
            const p = S.params.get(e.name);
            return p ? p.value : undefined;
        }
        case "call": {
            const a = e.args.map(ev);
            switch (e.fn) {
                case "min": return Math.min(a[0], a[1]);
                case "max": return Math.max(a[0], a[1]);
                case "clamp": return Math.min(Math.max(a[0], a[1]), a[2]);
                case "abs": return Math.abs(a[0]);
                case "sqrt": return Math.sqrt(a[0]);
                case "f32": return Math.fround(a[0]);
                case "f64": return a[0];
                case "int": return Math.trunc(a[0]);
            }
            const c = S.curves.get(e.fn);
            if (c) return evalCurve(c, a[0], ast, S);
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
export function evalCurve(c, x, ast, S = symbols(ast)) {
    if (c.form === "steps") { let y = c.base; for (const [px, py] of c.points) if (x > px) y = py; return y; }
    if (c.form === "points") {
        const P = c.points;
        if (x <= P[0][0]) return P[0][1];
        for (let i = 0; i + 1 < P.length; i++) if (x <= P[i + 1][0]) return P[i][1] + (P[i + 1][1] - P[i][1]) * ((x - P[i][0]) / (P[i + 1][0] - P[i][0]));
        return P[P.length - 1][1];
    }
    return evalExpr(c.body, ast, { args: { [c.arg]: x } }, S);
}

// ---------------------------------------------------------------- built-in texts: merge and diff
// Merge order (design 2.1): common, then the base mode's file, then the user's. A declaration a later file writes
// replaces the earlier one in place; a new one goes at the end.
export function mergeLogic(parts, base) {
    const decls = [];
    const at = new Map();
    let tail = [];
    for (const p of parts) {
        if (!p) continue;
        for (const d of p.decls) {
            const k = declKey(d);
            // (a replacement with no comments of its own keeps a blank line where the built-in had a comment block)
            if (at.has(k)) { const old = decls[at.get(k)], nd = clone(d); if (!(nd.comments && nd.comments.length) && old.comments && old.comments.length) nd.comments = [""]; decls[at.get(k)] = nd; }
            else { at.set(k, decls.length); decls.push(clone(d)); }
        }
        if (p.tail && p.tail.length) tail = clone(p.tail);
    }
    return { format: "qmode-logic-ast", version: 1, base: base || null, decls, tail };
}
const stripMeta = (d) => { const c = stripIds(d); delete c.comments; delete c.trail; return c; };
export const sameDecl = (a, b) => deepEqual(stripMeta(a), stripMeta(b));
// what differs from the built-in: [{key, decl, def, change: "changed"|"added"|"default"}]. A file only overrides what
// it writes, so a declaration it leaves out is the built-in one ("default").
export function diffLogic(ast, def) {
    const mine = new Map(ast.decls.map((d) => [declKey(d), d])), theirs = new Map(def.decls.map((d) => [declKey(d), d]));
    const out = [];
    for (const [k, d] of mine) {
        const o = theirs.get(k);
        if (!o) out.push({ key: k, decl: d, change: "added" });
        else if (!sameDecl(d, o)) out.push({ key: k, decl: d, def: o, change: "changed" });
    }
    for (const [k, o] of theirs) if (!mine.has(k)) out.push({ key: k, def: o, change: "default" });
    return out;
}
// the slots that leave the native code: changed / added slots, and slots that call a changed / added proc
export function interpretedSlots(ast, def) {
    const changed = new Set(diffLogic(ast, def).filter((x) => x.change !== "default").map((x) => x.key));
    const S = symbols(ast);
    const procChanged = new Map();
    const pc = (n, stack = []) => {
        if (procChanged.has(n)) return procChanged.get(n);
        if (stack.includes(n)) return false;
        const p = S.procs.get(n);
        let r = changed.has("proc:" + n);
        if (p && !r) walkStmts(p.body, (s) => { if (!r && s.op === "do" && S.procs.has(s.fn) && pc(s.fn, [...stack, n])) r = true; });
        procChanged.set(n, r);
        return r;
    };
    const out = new Set();
    for (const d of ast.decls) if (d.kind === "slot") {
        let r = changed.has(declKey(d));
        if (!r) walkStmts(d.body, (s) => { if (!r && s.op === "do" && S.procs.has(s.fn) && pc(s.fn)) r = true; });
        if (r) out.add(slotKey(d));
    }
    return out;
}

// ---------------------------------------------------------------- generic node parts (blocks and graph build on these)
// decompose(node) -> {cat, op, fields, inputs: [[path, expr]], stacks: [[path, [stmt]]]}; compose is its inverse.
// Optional inputs (an aggregate's where / by, team(t), an indexed set) leave `true` in fields where the child was.
const EXPR_OPS = new Set(["num", "bool", "name", "hole", "str", "call", "index", "agg", "neg", "not", "?:", "+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!=", "and", "or"]);
export const isExpr = (n) => !!n && typeof n === "object" && EXPR_OPS.has(n.op) && !n.kind;
export function decompose(n) {
    const fields = clone(n), inputs = [], stacks = [];
    let cat, op;
    if (n.kind) {
        cat = "decl"; op = n.kind;
        if (n.kind === "slot" || n.kind === "proc") { stacks.push(["body", n.body]); delete fields.body; }
        if (n.kind === "curve" && n.form === "expr") { inputs.push(["body", n.body]); delete fields.body; }
    } else if (isExpr(n)) {
        cat = "expr"; op = n.op;
        for (const [k, c] of exprChildren(n)) inputs.push([k, c]);
        delete fields.a; delete fields.b; delete fields.c; delete fields.index;
        if (n.op === "call") fields.args = n.args.length;
        if (n.op === "agg") { if (n.set.set === "team") fields.set.team = true; if (n.where) fields.where = true; if (n.by) fields.by = true; }
    } else {
        cat = "stmt"; op = n.op;
        if (n.op === "set") { if (n.index) { inputs.push(["index", n.index]); fields.index = true; } inputs.push(["value", n.value]); delete fields.value; }
        else if (n.op === "return") { n.values.forEach((v, i) => inputs.push(["values." + i, v])); fields.values = n.values.length; }
        else if (n.op === "do") { n.args.forEach((v, i) => inputs.push(["args." + i, v])); fields.args = n.args.length; }
        else if (n.op === "for") {
            if (n.set.set === "team") { inputs.push(["set.team", n.set.team]); fields.set.team = true; }
            if (n.where) { inputs.push(["where", n.where]); fields.where = true; }
            stacks.push(["body", n.body]); delete fields.body;
        } else if (n.op === "if") {
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
    const H = () => ({ op: "hole" });
    if (p.cat === "expr" && p.op === "call") f.args = new Array(f.args).fill(null).map(H);
    if (p.cat === "stmt" && (p.op === "return")) f.values = new Array(f.values).fill(null).map(H);
    if (p.cat === "stmt" && (p.op === "do")) f.args = new Array(f.args).fill(null).map(H);
    for (const k of ["where", "by", "index"]) if (f[k] === true) f[k] = H();
    if (f.set && f.set.team === true) f.set.team = H();
    Object.assign(n, f);
    if (p.cat === "expr") for (const k of ["a", "b", "c", "index"]) if (needsInput(p.op, k) && !n[k]) n[k] = H();
    if (p.cat === "stmt" && p.op === "set") n.value = H();
    if (p.cat === "decl" && p.op === "curve" && n.form === "expr") n.body = H();
    if (p.cat === "decl" && (p.op === "slot" || p.op === "proc")) n.body = [];
    if (p.cat === "stmt" && p.op === "for") n.body = [];
    if (p.cat === "stmt" && p.op === "if") { n.arms.forEach((a) => { a.cond = H(); a.body = []; }); if (n.else) n.else.body = []; }
    for (const [path, v] of p.inputs) setPath(n, path, v);
    for (const [path, v] of p.stacks) setPath(n, path, v);
    // keep the key order of a parsed node (set: name, index, value) so a compose deep-equals and prints the same
    if (p.cat === "stmt" && p.op === "set" && n.index) { const v = n.value; delete n.value; n.value = v; }
    return n;
}
function needsInput(op, k) {
    if (["num", "bool", "name", "hole", "str", "call", "agg"].includes(op)) return false;
    if (op === "index") return k === "index";
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

// new nodes for the palettes (blocks and graph)
export function newExpr(op, extra = {}) {
    const H = () => ({ op: "hole" });
    switch (op) {
        case "num": return { op, type: extra.type || "f32", v: extra.v ?? 0 };
        case "bool": return { op, v: extra.v ?? true };
        case "name": return { op, name: extra.name || "x" };
        case "str": return { op, v: extra.v ?? "text" };
        case "call": {
            const f = CATALOG.functions[extra.fn] || CATALOG.actions[extra.fn];
            return { op, fn: extra.fn, args: new Array(extra.argc ?? (f ? f.args.length : 1)).fill(0).map(H) };
        }
        case "index": return { op, array: extra.array || "score", index: H() };
        case "agg": {
            const n = { op, fn: extra.fn || "count", var: extra.var || "p", set: extra.set || { set: "playing" } };
            if (n.fn !== "best" || extra.where) n.where = H();
            if (n.fn === "best") n.by = H();
            return n;
        }
        case "neg": case "not": return { op, a: H() };
        case "?:": return { op, c: H(), a: H(), b: H() };
        default: return { op, a: H(), b: H() };
    }
}
export function newStmt(op, extra = {}) {
    const H = () => ({ op: "hole" });
    switch (op) {
        case "set": return extra.indexed ? { op, name: extra.name || "score", index: H(), value: H() } : { op, name: extra.name || "x", value: H() };
        case "return": return { op, values: new Array(extra.n ?? 1).fill(0).map(H) };
        case "do": { const a = CATALOG.actions[extra.fn]; return { op, fn: extra.fn, args: new Array(extra.argc ?? (a ? a.args.length + (a.text ? 1 : 0) : 0)).fill(0).map(() => (a && a.text ? { op: "str", v: "" } : H())).map((x, i) => (a && i < a.args.length ? H() : x)) }; }
        case "for": return { op, var: extra.var || "p", set: extra.set || { set: "playing" }, ...(extra.where ? { where: H() } : {}), body: [{ op: "pass" }] };
        case "if": return { op, arms: [{ cond: H(), body: [{ op: "pass" }] }], ...(extra.else ? { else: { body: [{ op: "pass" }] } } : {}) };
        default: return { op };
    }
}
