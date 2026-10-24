// Equations and curves for movement settings (INTERFACE.md "v2: equations and curves"): a setting's multiplier can be
// an expression instead of a number, saved as `expr.<key>=<expression>` in the .qmode. This is the page's evaluator for
// that grammar: it checks and highlights what you type, works out the values (static ones once, in dependency order;
// dynamic ones from the player's state, p.*), draws the curves, and the MOCK pmove runs dynamic ones every 1 ms step.
// With a helper that has `physexpr`, the page also asks it (the game's own evaluator) and shows where they differ.
//
// The grammar and its meaning follow the game's own evaluator (src/game/moveexpr.cpp), which the helper runs:
//   numbers, + - * / ^ (power, right to left: -2^2 = -4), unary - + !, one comparison < <= > >= == != (1 or 0),
//   && || (1 or 0), c ? a : b, ( ), pi
//   other settings by key (gravity, speed, jump, friction, air_accel, ...): their multiplier (1 = standard);
//   base.<key>: that setting's standard absolute value (base.speed = 9.5)
//   p.speed p.hspeed p.vspeed (m/s), p.air p.ground (s since leaving / landing), p.onground, p.charges,
//   p.dashing p.hooked (0/1), p.height (m above the floor under you, 50 at most)
//   sqrt abs min max pow exp log floor ceil round sign, clamp(x,lo,hi), lerp(a,b,t), step(edge,x),
//   ease(t, "mode") (t clamped to 0..1; mode defaults to "linear"),
//   curve(x, x0, x1, y0, y1, "mode"): x in [x0,x1] mapped to [y0,y1] with that easing, flat outside
//   A result that is NaN leaves the setting at its fixed value; anything else is clamped to the setting's range.

// (lo..hi: the range the editor plots; rest: standing still on the floor, the state a static value is shown at)
export const INPUTS = [
    { key: "speed", unit: "m/s", min: 0, max: 50, rest: 0, doc: "your speed (3D)" },
    { key: "hspeed", unit: "m/s", min: 0, max: 50, rest: 0, doc: "your horizontal speed" },
    { key: "vspeed", unit: "m/s", min: -40, max: 20, rest: 0, doc: "your vertical speed (up is +)" },
    { key: "air", unit: "s", min: 0, max: 3, rest: 0, doc: "time in the air since leaving the floor (0 on the floor)" },
    { key: "ground", unit: "s", min: 0, max: 3, rest: 0, doc: "time on the floor since landing (0 in the air)" },
    { key: "onground", unit: "", min: 0, max: 1, rest: 1, doc: "1 on the floor, 0 in the air" },
    { key: "charges", unit: "", min: 0, max: 10, rest: 2, doc: "dash charges held" },
    { key: "dashing", unit: "", min: 0, max: 1, rest: 0, doc: "1 while a dash runs" },
    { key: "hooked", unit: "", min: 0, max: 1, rest: 0, doc: "1 while your grapple holds" },
    { key: "height", unit: "m", min: 0, max: 50, rest: 0, doc: "height above the floor below you (0 on it, 50 at most)" },
];
export const INPUT_KEYS = new Set(INPUTS.map((i) => i.key));
export const STANDARD_INPUTS = Object.freeze(Object.fromEntries(INPUTS.map((i) => [i.key, i.rest])));

export const EASINGS = ["linear", "in", "out", "inout", "in3", "out3", "inout3", "smooth", "smoother", "expo", "outexpo", "back", "outback"];
const C1 = 1.70158, C3 = C1 + 1;
export function ease(t, kind) {
    if (t !== t) return t;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const u = 1 - t;
    switch (kind) {
        case "in": return t * t;
        case "out": return 1 - u * u;
        case "inout": return t < 0.5 ? 2 * t * t : 1 - 2 * u * u;
        case "in3": return t * t * t;
        case "out3": return 1 - u * u * u;
        case "inout3": return t < 0.5 ? 4 * t * t * t : 1 - 4 * u * u * u;
        case "smooth": return t * t * (3 - 2 * t);
        case "smoother": return t * t * t * (t * (t * 6 - 15) + 10);
        case "expo": return t === 0 ? 0 : Math.pow(2, 10 * t - 10);
        case "outexpo": return t === 1 ? 1 : 1 - Math.pow(2, -10 * t);
        case "back": return C3 * t * t * t - C1 * t * t;
        case "outback": return 1 - (C3 * u * u * u - C1 * u * u);
        default: return t;   // linear
    }
}
export const EASE_DOC = {
    linear: "t (straight)", in: "t² (slow start)", out: "1 − (1 − t)² (slow end)", inout: "slow both ends (quadratic)",
    in3: "t³ (slower start)", out3: "1 − (1 − t)³ (slower end)", inout3: "slow both ends (cubic)", smooth: "t²(3 − 2t) (smoothstep)",
    smoother: "6t⁵ − 15t⁴ + 10t³ (smootherstep)", expo: "2^(10t − 10) (flat, then steep)", outexpo: "1 − 2^(−10t) (steep, then flat)",
    back: "dips below the start, then up", outback: "overshoots the end, then settles",
};
export function curveFn(x, x0, x1, y0, y1, kind) {
    const t = x1 === x0 ? (x < x0 ? 0 : 1) : (x - x0) / (x1 - x0);
    return y0 + (y1 - y0) * ease(t, kind);
}

const F = (min, max, doc, fn) => ({ min, max, doc, fn });
export const FUNCS = {
    sqrt: F(1, 1, "sqrt(x)", Math.sqrt), abs: F(1, 1, "abs(x)", Math.abs),
    min: F(2, 8, "min(a, b, ...)", Math.min), max: F(2, 8, "max(a, b, ...)", Math.max),
    pow: F(2, 2, "pow(x, y) = x^y", Math.pow), exp: F(1, 1, "exp(x) = e^x", Math.exp), log: F(1, 1, "log(x): natural log", Math.log),
    floor: F(1, 1, "floor(x)", Math.floor), ceil: F(1, 1, "ceil(x)", Math.ceil), sign: F(1, 1, "sign(x): -1, 0 or 1", Math.sign),
    round: F(1, 1, "round(x): to the nearest (halves away from 0)", (x) => (x < 0 ? -Math.round(-x) : Math.round(x))),
    clamp: F(3, 3, "clamp(x, lo, hi)", (x, a, b) => (x < a ? a : x > b ? b : x)),
    lerp: F(3, 3, "lerp(a, b, t) = a + (b - a) t", (a, b, t) => a + (b - a) * t),
    step: F(2, 2, "step(edge, x): 1 when x >= edge, else 0", (e, x) => (x >= e ? 1 : 0)),
    ease: F(1, 2, 'ease(t, "mode"): t clamped to 0..1, eased ("linear" if no mode)', (t) => t),
    curve: F(5, 6, 'curve(x, x0, x1, y0, y1, "mode"): x0..x1 -> y0..y1 eased, flat outside', curveFn),
};
const STR_ARG = { ease: 1, curve: 5 };   // the argument that is an easing name

// ---------------------------------------------------------------- tokens
const isDigit = (c) => c >= "0" && c <= "9";
const isIdStart = (c) => (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c === "_";
const isId = (c) => isIdStart(c) || isDigit(c);
export function tokenize(src) {
    const out = [];
    let i = 0;
    while (i < src.length) {
        const c = src[i];
        if (c === " " || c === "\t") { i++; continue; }
        const s = i;
        if (isDigit(c) || (c === "." && isDigit(src[i + 1] || ""))) {
            while (isDigit(src[i] || "")) i++;
            if (src[i] === ".") { i++; while (isDigit(src[i] || "")) i++; }
            if ((src[i] === "e" || src[i] === "E") && (isDigit(src[i + 1] || "") || ((src[i + 1] === "+" || src[i + 1] === "-") && isDigit(src[i + 2] || "")))) {
                i += 2; while (isDigit(src[i] || "")) i++;
            }
            out.push({ t: "num", v: parseFloat(src.slice(s, i)), s, e: i });
        } else if (isIdStart(c)) {
            while (isId(src[i] || "")) i++;
            let name = src.slice(s, i);
            if ((name === "p" || name === "base") && src[i] === ".") {
                i++; const s2 = i;
                while (isId(src[i] || "")) i++;
                out.push({ t: name === "p" ? "in" : "base", v: src.slice(s2, i), s, e: i });
            } else out.push({ t: "id", v: name, s, e: i });
        } else if (c === '"' || c === "'") {
            i++;
            while (i < src.length && src[i] !== c) i++;
            const closed = src[i] === c;
            if (closed) i++;
            out.push({ t: "str", v: src.slice(s + 1, closed ? i - 1 : i), s, e: i, open: !closed });
        } else {
            const two = src.slice(i, i + 2);
            if (["<=", ">=", "==", "!=", "&&", "||"].includes(two)) { out.push({ t: "op", v: two, s, e: i + 2 }); i += 2; }
            else if ("+-*/^()<>,?:!".includes(c)) { out.push({ t: "op", v: c, s, e: i + 1 }); i++; }
            else { out.push({ t: "bad", v: c, s, e: i + 1 }); i++; }
        }
    }
    return out;
}

// ---------------------------------------------------------------- parse
class ExprError extends Error { constructor(msg, s, e) { super(msg); this.s = s; this.e = e; } }

// parse(text) -> {ast, error: {msg, s, e} | null}
export function parse(src) {
    src = String(src ?? "");
    const toks = tokenize(src);
    let k = 0;
    const peek = () => toks[k];
    const isOp = (v) => toks[k] && toks[k].t === "op" && toks[k].v === v;
    const end = src.length;
    const fail = (msg, t) => { throw new ExprError(msg, t ? t.s : end, t ? t.e : end); };
    const expect = (v, what) => { if (!isOp(v)) fail(`expected '${v}'${what ? " " + what : ""}${peek() ? `, found '${src.slice(peek().s, peek().e)}'` : " at the end"}`, peek()); return toks[k++]; };
    function ternary() {
        const c = orx();
        if (isOp("?")) {
            k++;
            const a = ternary();
            expect(":", "(the other half of ? :)");
            const b = ternary();
            return { t: "tern", c, a, b, s: c.s, e: b.e };
        }
        return c;
    }
    function orx() {
        let a = andx();
        while (isOp("||")) { k++; const b = andx(); a = { t: "bin", op: "||", a, b, s: a.s, e: b.e }; }
        return a;
    }
    function andx() {
        let a = comparison();
        while (isOp("&&")) { k++; const b = comparison(); a = { t: "bin", op: "&&", a, b, s: a.s, e: b.e }; }
        return a;
    }
    function comparison() {   // (one comparison: a < b < c is an error, as in the game)
        const a = additive();
        if (peek() && peek().t === "op" && ["<", "<=", ">", ">=", "==", "!="].includes(peek().v)) {
            const op = toks[k++].v, b = additive();
            return { t: "bin", op, a, b, s: a.s, e: b.e };
        }
        return a;
    }
    function additive() {
        let a = multiplicative();
        while (isOp("+") || isOp("-")) { const op = toks[k++].v, b = multiplicative(); a = { t: "bin", op, a, b, s: a.s, e: b.e }; }
        return a;
    }
    function multiplicative() {
        let a = unary();
        while (isOp("*") || isOp("/")) { const op = toks[k++].v, b = unary(); a = { t: "bin", op, a, b, s: a.s, e: b.e }; }
        return a;
    }
    function unary() {
        if (isOp("-") || isOp("+") || isOp("!")) { const t = toks[k++]; const a = unary(); return t.v === "-" ? { t: "neg", a, s: t.s, e: a.e } : t.v === "!" ? { t: "not", a, s: t.s, e: a.e } : a; }
        return power();
    }
    function power() {
        const a = primary();
        if (isOp("^")) { k++; const b = unary(); return { t: "bin", op: "^", a, b, s: a.s, e: b.e }; }   // (right to left; -x^2 = -(x^2))
        return a;
    }
    function primary() {
        const t = peek();
        if (!t) fail("the expression ends too soon", null);
        if (t.t === "num") { k++; return { t: "num", v: t.v, s: t.s, e: t.e }; }
        if (t.t === "str") { k++; if (t.open) fail("a string without its closing quote", t); return { t: "str", v: t.v, s: t.s, e: t.e }; }
        if (t.t === "in") { k++; return { t: "in", k: t.v, s: t.s, e: t.e }; }
        if (t.t === "base") { k++; return { t: "base", k: t.v, s: t.s, e: t.e }; }
        if (t.t === "id") {
            k++;
            if (isOp("(")) {
                k++;
                const args = [];
                if (!isOp(")")) {
                    for (;;) { args.push(ternary()); if (isOp(",")) { k++; continue; } break; }
                }
                const close = expect(")", `to close ${t.v}(`);
                return { t: "call", f: t.v, args, s: t.s, e: close.e, fs: t.s, fe: t.e };
            }
            if (t.v === "pi") return { t: "num", v: Math.PI, s: t.s, e: t.e };
            return { t: "id", k: t.v, s: t.s, e: t.e };
        }
        if (t.t === "op" && t.v === "(") {
            k++;
            const a = ternary();
            expect(")", "to close '('");
            return a;
        }
        if (t.t === "bad") fail(`'${t.v}' isn't part of an expression`, t);
        fail(`unexpected '${src.slice(t.s, t.e)}'`, t);
    }
    try {
        if (!src.trim()) return { ast: null, error: { msg: "empty: type an expression (e.g. sqrt(speed))", s: 0, e: 0 } };
        const ast = ternary();
        if (k < toks.length) fail(`unexpected '${src.slice(toks[k].s, toks[k].e)}' (an operator missing?)`, toks[k]);
        return { ast, error: null };
    } catch (ex) {
        if (ex instanceof ExprError) return { ast: null, error: { msg: ex.message, s: ex.s, e: ex.e } };
        throw ex;
    }
}

// ---------------------------------------------------------------- check: names, argument counts, easing names
// -> {settings: Set, inputs: Set, errors: [{msg, s, e}]}
export function analyze(ast, settingKeys, cat) {
    const settings = new Set(), inputs = new Set(), errors = [];
    const walk = (n, strOk) => {
        if (!n) return;
        switch (n.t) {
            case "num": return;
            case "str": if (!strOk) errors.push({ msg: `a string ("${n.v}") is only an easing name in ease() / curve()`, s: n.s, e: n.e }); return;
            case "in":
                if (!INPUT_KEYS.has(n.k)) errors.push({ msg: `p.${n.k}: no such input (${INPUTS.map((i) => "p." + i.key).join(", ")})`, s: n.s, e: n.e });
                else inputs.add(n.k);
                return;
            case "base":
                if (!settingKeys.has(n.k)) errors.push({ msg: `base.${n.k}: no such setting${suggest(n.k, [...settingKeys])}`, s: n.s, e: n.e });
                else if (cat) n.v = cat.byKey.get(n.k).base;
                return;
            case "id":
                if (settingKeys.has(n.k)) settings.add(n.k);
                else if (FUNCS[n.k]) errors.push({ msg: `${n.k} is a function: ${FUNCS[n.k].doc}`, s: n.s, e: n.e });
                else if (n.k.startsWith("move_") && settingKeys.has(n.k.slice(5))) errors.push({ msg: `write ${n.k.slice(5)} (the key without move_)`, s: n.s, e: n.e });
                else if (INPUT_KEYS.has(n.k)) errors.push({ msg: `the player's ${n.k} is p.${n.k}`, s: n.s, e: n.e });
                else errors.push({ msg: `unknown name '${n.k}'${suggest(n.k, [...settingKeys])}`, s: n.s, e: n.e });
                return;
            case "neg": case "not": return walk(n.a);
            case "bin": walk(n.a); walk(n.b); return;
            case "tern": walk(n.c); walk(n.a); walk(n.b); return;
            case "call": {
                const f = FUNCS[n.f];
                if (!f) { errors.push({ msg: `unknown function '${n.f}'${suggest(n.f, Object.keys(FUNCS))}`, s: n.fs, e: n.fe }); n.args.forEach((a) => walk(a)); return; }
                if (n.args.length < f.min || n.args.length > f.max) errors.push({ msg: `${f.doc}: ${n.args.length} argument${n.args.length === 1 ? "" : "s"} given`, s: n.s, e: n.e });
                n.args.forEach((a, i) => {
                    if (STR_ARG[n.f] === i) {
                        if (a.t !== "str") errors.push({ msg: `the easing is a name in quotes: ${EASINGS.map((x) => '"' + x + '"').join(" ")}`, s: a.s, e: a.e });
                        else if (!EASINGS.includes(a.v)) errors.push({ msg: `no easing "${a.v}": ${EASINGS.join(", ")}`, s: a.s, e: a.e });
                    } else walk(a);
                });
                return;
            }
        }
    };
    walk(ast);
    return { settings, inputs, errors };
}
function suggest(w, list) {
    let best = null, bd = 3;
    for (const c of list) { const d = lev(w, c); if (d < bd) { bd = d; best = c; } }
    return best ? ` (${best}?)` : "";
}
function lev(a, b) {
    const m = a.length, n = b.length, d = Array.from({ length: m + 1 }, (_, i) => [i, ...Array(n).fill(0)]);
    for (let j = 1; j <= n; j++) d[0][j] = j;
    for (let i = 1; i <= m; i++) for (let j = 1; j <= n; j++) d[i][j] = Math.min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
    return d[m][n];
}

// ---------------------------------------------------------------- compile: a closure tree, (S settings, P inputs) -> number
export function compile(n) {
    switch (n.t) {
        case "num": { const v = n.v; return () => v; }
        case "str": return () => NaN;
        case "in": { const k = n.k; return (S, P) => +P[k] || 0; }
        case "id": { const k = n.k; return (S) => S[k]; }
        case "neg": { const a = compile(n.a); return (S, P) => -a(S, P); }
        case "not": { const a = compile(n.a); return (S, P) => (a(S, P) === 0 ? 1 : 0); }
        case "base": { const v = n.v ?? NaN; return () => v; }
        case "tern": { const c = compile(n.c), a = compile(n.a), b = compile(n.b); return (S, P) => (c(S, P) !== 0 ? a(S, P) : b(S, P)); }
        case "bin": {
            const a = compile(n.a), b = compile(n.b);
            switch (n.op) {
                case "+": return (S, P) => a(S, P) + b(S, P);
                case "-": return (S, P) => a(S, P) - b(S, P);
                case "*": return (S, P) => a(S, P) * b(S, P);
                case "/": return (S, P) => a(S, P) / b(S, P);
                case "^": return (S, P) => Math.pow(a(S, P), b(S, P));
                case "<": return (S, P) => (a(S, P) < b(S, P) ? 1 : 0);
                case "<=": return (S, P) => (a(S, P) <= b(S, P) ? 1 : 0);
                case ">": return (S, P) => (a(S, P) > b(S, P) ? 1 : 0);
                case ">=": return (S, P) => (a(S, P) >= b(S, P) ? 1 : 0);
                case "==": return (S, P) => (a(S, P) === b(S, P) ? 1 : 0);
                case "!=": return (S, P) => (a(S, P) !== b(S, P) ? 1 : 0);
                case "&&": return (S, P) => (a(S, P) !== 0 && b(S, P) !== 0 ? 1 : 0);
                case "||": return (S, P) => (a(S, P) !== 0 || b(S, P) !== 0 ? 1 : 0);
            }
            break;
        }
        case "call": {
            const f = FUNCS[n.f].fn;
            if (n.f === "ease") { const t = compile(n.args[0]), kind = n.args[1] ? n.args[1].v : "linear"; return (S, P) => ease(t(S, P), kind); }
            if (n.f === "curve") {
                const [x, x0, x1, y0, y1] = n.args.slice(0, 5).map(compile), kind = n.args[5] ? n.args[5].v : "linear";
                return (S, P) => curveFn(x(S, P), x0(S, P), x1(S, P), y0(S, P), y1(S, P), kind);
            }
            const as = n.args.map(compile);
            if (n.f === "min" || n.f === "max") {   // (a chain of pairs, NaN as the game's: b < a ? b : a)
                const mn = n.f === "min";
                return (S, P) => { let r = as[0](S, P); for (let i = 1; i < as.length; i++) { const b = as[i](S, P); r = mn ? (b < r ? b : r) : (b > r ? b : r); } return r; };
            }
            if (as.length === 1) { const a = as[0]; return (S, P) => f(a(S, P)); }
            if (as.length === 2) { const [a, b] = as; return (S, P) => f(a(S, P), b(S, P)); }
            if (as.length === 3) { const [a, b, c] = as; return (S, P) => f(a(S, P), b(S, P), c(S, P)); }
            return (S, P) => f(...as.map((g) => g(S, P)));
        }
    }
    return () => NaN;
}

// One expression, checked: {ok, fn, settings, inputs, error: {msg, s, e} | null, dynamic (uses p.* itself)}
const cache = new Map();
export function check(text, settingKeys, cat) {
    const key = text + "\u0000" + settingKeys.size;
    const hit = cache.get(key);
    if (hit && hit.keys === settingKeys) return hit.v;
    const { ast, error } = parse(text);
    let v;
    if (error) v = { ok: false, error, settings: new Set(), inputs: new Set(), ast: null };
    else {
        if (text.length > 512) { const v2 = { ok: false, error: { msg: "too long (512 characters at most: the game ignores a longer line)", s: 512, e: text.length }, settings: new Set(), inputs: new Set() }; return v2; }
        const a = analyze(ast, settingKeys, cat || check.cat);
        v = a.errors.length ? { ok: false, error: a.errors[0], errors: a.errors, settings: a.settings, inputs: a.inputs, ast }
            : { ok: true, error: null, fn: compile(ast), settings: a.settings, inputs: a.inputs, ast, dynamic: a.inputs.size > 0 };
    }
    if (cache.size > 2000) cache.clear();
    cache.set(key, { keys: settingKeys, v });
    return v;
}

// ---------------------------------------------------------------- a whole set
// resolve(physics, cat, P = STANDARD_INPUTS) -> {S: {key: multiplier}, info: {key: {text, ok, error, dynamic, value, deps,
// inputs, clamped, cycle}}, order: [keys with expressions, dependency order], dynamicKeys: [...]}
// Settings are referenced by their multiplier; each result is clamped to the setting's min..max like a fixed value.
// An expression with an error, in a cycle, or not finite leaves the setting at its fixed value.
export function resolve(ph, cat, P = STANDARD_INPUTS) {
    const keys = cat.keySet || (cat.keySet = new Set(cat.all.map((p) => p.key)));
    const S = {};
    for (const p of cat.all) S[p.key] = p.top ? (ph[p.key] ?? 1) : ((ph.move && ph.move[p.key]) ?? 1);
    const ex = ph.expr || {};
    const info = {};
    const list = Object.keys(ex).filter((k) => keys.has(k) && String(ex[k]).trim());
    for (const k of list) {
        check.cat = cat;
        const c = check(String(ex[k]), keys, cat);
        info[k] = { text: String(ex[k]), ok: c.ok, error: c.error, fn: c.fn, deps: [...c.settings], inputs: [...c.inputs], dynamic: false, value: S[k] };
    }
    for (const k of Object.keys(ex)) if (!keys.has(k)) info[k] = { text: String(ex[k]), ok: false, error: { msg: `unknown setting '${k}'`, s: 0, e: 0 }, deps: [], inputs: [] };
    // dependency order (only expression settings depend on anything); cycles are errors
    const order = [], state = {};
    const visit = (k, path) => {
        if (state[k] === 2) return;
        if (state[k] === 1) {
            const cyc = path.slice(path.indexOf(k));
            for (const c of cyc) if (info[c]) { info[c].ok = false; info[c].cycle = true; info[c].error = { msg: `a cycle: ${[...cyc, k].join(" → ")}`, s: 0, e: info[c].text.length }; }
            return;
        }
        state[k] = 1;
        const i = info[k];
        if (i) for (const d of i.deps) if (info[d]) visit(d, [...path, k]);
        state[k] = 2;
        if (i) order.push(k);
    };
    for (const k of list) visit(k, []);
    const dynamicKeys = [];
    for (const k of order) {
        const i = info[k];
        if (!i.ok) continue;
        i.dynamic = i.inputs.length > 0 || i.deps.some((d) => info[d] && info[d].ok && info[d].dynamic);
        if (i.dynamic) dynamicKeys.push(k);
        const p = cat.byKey.get(k);
        let v = i.fn(S, P);
        if (v !== v) { i.nonFinite = true; i.value = S[k]; continue; }   // (NaN: the fixed value; ±infinity is clamped)
        const c = v < p.min ? p.min : v > p.max ? p.max : v;
        i.clamped = c !== v; i.raw = v;
        S[k] = c; i.value = c;
    }
    return { S, info, order: order.filter((k) => info[k].ok), dynamicKeys };
}

// A runtime plan for the dynamic expressions (the MOCK pmove): plan.update(P, set(key, value)) every step.
export function dynamicPlan(ph, cat) {
    const r = resolve(ph, cat);
    if (!r.dynamicKeys.length) return null;
    const S = { ...r.S };
    const steps = r.order.filter((k) => r.info[k].dynamic).map((k) => { const p = cat.byKey.get(k); return { k, fn: r.info[k].fn, min: p.min, max: p.max, fixed: r.S[k] }; });
    return {
        keys: steps.map((s) => s.k), S,
        update(P, set) {
            for (const s of steps) {
                let v = s.fn(S, P);
                if (v !== v) v = s.fixed; else if (v < s.min) v = s.min; else if (v > s.max) v = s.max;
                S[s.k] = v; set(s.k, v);
            }
        },
    };
}

// ---------------------------------------------------------------- curves: curve(input, x0, x1, y0, y1, "ease")
// the curve editor's form of an expression, or null if the text is something else
export function parseCurve(text, settingKeys) {
    const { ast, error } = parse(text);
    if (error || !ast || ast.t !== "call" || ast.f !== "curve" || ast.args.length < 5) return null;
    const a = ast.args, num = (n) => (n.t === "num" ? n.v : n.t === "neg" && n.a.t === "num" ? -n.a.v : null);
    const inp = a[0].t === "in" && INPUT_KEYS.has(a[0].k) ? "p." + a[0].k : a[0].t === "id" && (!settingKeys || settingKeys.has(a[0].k)) ? a[0].k : null;
    const b = [num(a[1]), num(a[2]), num(a[3]), num(a[4])];
    if (!inp || b.some((x) => x == null)) return null;
    const e = a[5] ? (a[5].t === "str" && EASINGS.includes(a[5].v) ? a[5].v : null) : "linear";
    if (!e) return null;
    return { input: inp, x0: b[0], x1: b[1], y0: b[2], y1: b[3], ease: e };
}
const n4 = (v) => { const s = (+(+v).toPrecision(4)).toString(); return s === "-0" ? "0" : s; };
export function formatCurve(c) { return `curve(${c.input}, ${n4(c.x0)}, ${n4(c.x1)}, ${n4(c.y0)}, ${n4(c.y1)}, "${c.ease}")`; }

// ---------------------------------------------------------------- syntax highlighting (HTML spans)
const esc = (s) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
export function highlightExpr(src, settingKeys, err) {
    const toks = tokenize(src);
    let out = "", at = 0;
    for (const t of toks) {
        out += esc(src.slice(at, t.s));
        const txt = esc(src.slice(t.s, t.e));
        let cls = "xo";
        if (t.t === "num") cls = "xn";
        else if (t.t === "str") cls = "xs";
        else if (t.t === "in") cls = INPUT_KEYS.has(t.v) ? "xi" : "xbad";
        else if (t.t === "base") cls = settingKeys && settingKeys.has(t.v) ? "xk" : "xbad";
        else if (t.t === "id") cls = FUNCS[t.v] ? "xf" : t.v === "pi" ? "xn" : settingKeys && settingKeys.has(t.v) ? "xk" : "xbad";
        else if (t.t === "bad") cls = "xbad";
        const inErr = err && t.s < err.e && t.e > err.s;
        out += `<span class="${cls}${inErr ? " xerr" : ""}">${txt}</span>`;
        at = t.e;
    }
    out += esc(src.slice(at));
    if (err && err.s >= src.length) out += `<span class="xerr xend"> </span>`;
    return out;
}

// ---------------------------------------------------------------- autocomplete: the word at the caret and what fits
export function completions(src, caret, cat) {
    let s = caret;
    while (s > 0 && /[A-Za-z0-9_.]/.test(src[s - 1])) s--;
    const word = src.slice(s, caret);
    if (!word || /^\d/.test(word)) return { from: s, to: caret, word, items: [] };
    const items = [];
    const lw = word.toLowerCase();
    if (lw.startsWith("p.")) {
        for (const i of INPUTS) if (("p." + i.key).startsWith(lw)) items.push({ text: "p." + i.key, kind: "input", doc: `${i.doc}${i.unit ? " (" + i.unit + ")" : ""}` });
    } else if (lw.startsWith("base.")) {
        for (const p of cat.all) if (("base." + p.key).startsWith(lw)) items.push({ text: "base." + p.key, kind: "setting", doc: `${p.label}'s standard value: ${p.base} ${p.unit || ""}` });
    } else {
        for (const p of cat.all) if (p.key.startsWith(lw) || p.label.toLowerCase().includes(lw)) items.push({ text: p.key, kind: "setting", doc: p.label + (p.unit ? ` · ${p.base} ${p.unit} standard` : "") });
        for (const [f, d] of Object.entries(FUNCS)) if (f.startsWith(lw)) items.push({ text: f + "(", kind: "function", doc: d.doc });
        if ("p".startsWith(lw)) for (const i of INPUTS) items.push({ text: "p." + i.key, kind: "input", doc: i.doc });
    }
    items.sort((a, b) => (a.text.startsWith(lw) === b.text.startsWith(lw) ? a.text.length - b.text.length : a.text.startsWith(lw) ? -1 : 1));
    return { from: s, to: caret, word, items: items.slice(0, 12) };
}

// an expression's value over one input (the others at the standard state): for plots. -> [[x, y]...]
export function sweep(ph, cat, key, input, x0, x1, n = 120, base = STANDARD_INPUTS) {
    const out = [];
    const P = { ...base };
    const isInput = input.startsWith("p.");
    const ik = isInput ? input.slice(2) : input;
    for (let i = 0; i <= n; i++) {
        const x = x0 + (x1 - x0) * i / n;
        let v;
        if (isInput) { P[ik] = x; v = resolve(ph, cat, P).S[key]; }
        else {
            const q = { ...ph, move: { ...(ph.move || {}) }, expr: { ...(ph.expr || {}) } };
            delete q.expr[ik];
            if (cat.byKey.get(ik) && cat.byKey.get(ik).top) q[ik] = x; else q.move[ik] = x;
            v = resolve(q, cat, P).S[key];
        }
        out.push([x, v]);
    }
    return out;
}
