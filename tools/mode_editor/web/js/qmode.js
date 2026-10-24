// Adapted from tools/bot_editor/web/js/dsl.js at c7aae27 (the brain language's parser / printer), for the mode
// logic block of a .qmode (design section 2.2): header `logic N` / `base <mode>`, the declarations param (one
// value), curve, use, state, timer, proc and slot (and `slot timer <name>`), the statements for / break / actions /
// `name[i] = v`, aggregates (count / any / first / random / best over a player set) and string arguments.
// Function names are kept from dsl.js so a later shared module (tools/editor_common/qlang/) is an easy merge.
//
// print(parse(t)) === t for canonical t, and parse(print(ast)) deep-equals ast. Parse errors carry line:col
// (1-based, counted in the logic text: its first line is `logic N`). Nodes get a non-enumerable `_loc`.

export const KEYWORDS = new Set(["logic", "base", "param", "curve", "use", "state", "match", "round", "timer", "proc", "slot", "if", "elif", "else",
    "return", "pass", "for", "in", "where", "by", "break", "and", "or", "not", "yes", "no", "true", "false", "none", "A", "B"]);
export const PARAM_TYPES = ["f32", "f64", "int", "bool"];
export const STATE_TYPES = ["f32", "f64", "int", "bool", "team", "player"];
export const AGGREGATES = new Set(["count", "any", "first", "random", "best"]);
export const LOGIC_VERSION = 1;
const CMP = new Set(["<", "<=", ">", ">=", "==", "!="]);
const CONST_KW = new Set(["none", "A", "B"]);

// Readability parentheses around an `and` inside an `or` (as the brain grammar, section 3)
export const opts = { andInOrParens: true };

export class ModeError extends Error {
    constructor(msg, line, col) { super(`${line}:${col}: ${msg}`); this.msg = msg; this.line = line; this.col = col; }
}
const loc = (node, line, col) => { Object.defineProperty(node, "_loc", { value: { line, col }, enumerable: false, writable: true }); return node; };

// ---------------------------------------------------------------- numbers (unchanged from dsl.js)
export function f32(v) { return Math.fround(v); }
export function canonNum(v, type) {
    if (type === "int") return String(Math.trunc(v));
    if (type === "f32") {
        const t = Math.fround(v);
        if (!Number.isFinite(t)) return String(t);
        for (let p = 1; p <= 9; p++) {
            const s = +t.toPrecision(p);
            if (Math.fround(s) === t) return String(s);
        }
        return String(t);
    }
    return String(v);
}
export function roundTo(v, type) {
    if (type === "int") return Math.trunc(v);
    if (type === "f32") return +canonNum(v, "f32");
    return v;
}
export function exprNumText(v, type) {
    let s = canonNum(v, type);
    if (type === "int") return s;
    if (!/[.e]/.test(s)) s += ".0";
    return type === "f32" ? s + "f" : s;
}

// ---------------------------------------------------------------- tokenizer (one line at a time)
const NAME_RE = /[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*/y;
const NUM_RE = /[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?f?/y;
const PUNCT = ["==", "!=", "<=", ">=", "(", ")", "[", "]", ",", ":", "?", "=", "<", ">", "+", "-", "*", "/"];

function tokenize(text, line, col0) {
    const toks = [];
    let i = 0;
    while (i < text.length) {
        const c = text[i];
        if (c === " ") { i++; continue; }
        if (c === "\t") throw new ModeError("tab: indent and space with spaces", line, col0 + i);
        const col = col0 + i;
        NAME_RE.lastIndex = i;
        let m = NAME_RE.exec(text);
        if (m) { toks.push({ t: KEYWORDS.has(m[0]) ? "kw" : "name", v: m[0], line, col }); i += m[0].length; continue; }
        NUM_RE.lastIndex = i;
        m = NUM_RE.exec(text);
        if (m) {
            const s = m[0];
            if (/[A-Za-z_.]/.test(text[i + s.length] || "")) throw new ModeError(`bad number '${text.slice(i).split(/[\s,)]/)[0]}'`, line, col);
            const isF = s.endsWith("f"), body = isF ? s.slice(0, -1) : s;
            toks.push({ t: "num", v: +body, type: isF ? "f32" : /[.eE]/.test(body) ? "f64" : "int", text: s, f: isF, line, col });
            i += s.length; continue;
        }
        if (c === '"') {
            const j = text.indexOf('"', i + 1);
            if (j < 0) throw new ModeError("unterminated string", line, col);
            const s = text.slice(i + 1, j);
            if (s.includes("\\")) throw new ModeError("no \\ in strings", line, col);
            if (new TextEncoder().encode(s).length > 120) throw new ModeError("a string is at most 120 bytes", line, col);
            toks.push({ t: "str", v: s, line, col }); i = j + 1; continue;
        }
        const p = PUNCT.find((q) => text.startsWith(q, i));
        if (p) { toks.push({ t: "p", v: p, line, col }); i += p.length; continue; }
        throw new ModeError(`unexpected '${c}'`, line, col);
    }
    return toks;
}

class Toks {
    constructor(toks, line, endCol) { this.a = toks; this.i = 0; this.line = line; this.endCol = endCol; }
    peek(k = 0) { return this.a[this.i + k]; }
    at(t, v) { const x = this.a[this.i]; return !!x && x.t === t && (v === undefined || x.v === v); }
    atP(v) { return this.at("p", v); }
    atKw(v) { return this.at("kw", v); }
    next() { return this.a[this.i++]; }
    done() { return this.i >= this.a.length; }
    where() { const x = this.a[this.i]; return x ? [x.line, x.col] : [this.line, this.endCol]; }
    fail(msg) { const [l, c] = this.where(); throw new ModeError(msg, l, c); }
    expectP(v) { if (!this.atP(v)) this.fail(`expected '${v}'${this.peek() ? ` but found '${this.peek().v}'` : " at the end of the line"}`); return this.next(); }
    expectKw(v) { if (!this.atKw(v)) this.fail(`expected '${v}'${this.peek() ? ` but found '${this.peek().v}'` : ""}`); return this.next(); }
    expectName(what = "a name") { if (!this.at("name")) this.fail(`expected ${what}${this.peek() ? ` but found '${this.peek().v}'` : ""}`); return this.next(); }
    end() { if (!this.done()) this.fail(`unexpected '${this.peek().v}'`); }
}

// ---------------------------------------------------------------- expressions
function parseExpr(T) { return parseTernary(T); }
function parseTernary(T) {
    const c = parseOr(T);
    if (T.atP("?")) {
        const q = T.next();
        const a = parseTernary(T);
        T.expectP(":");
        const b = parseTernary(T);
        return loc({ op: "?:", c, a, b }, q.line, q.col);
    }
    return c;
}
function parseOr(T) {
    let l = parseAnd(T);
    while (T.atKw("or")) { const o = T.next(); l = loc({ op: "or", a: l, b: parseAnd(T) }, o.line, o.col); }
    return l;
}
function parseAnd(T) {
    let l = parseNot(T);
    while (T.atKw("and")) { const o = T.next(); l = loc({ op: "and", a: l, b: parseNot(T) }, o.line, o.col); }
    return l;
}
function parseNot(T) {
    if (T.atKw("not")) { const o = T.next(); return loc({ op: "not", a: parseNot(T) }, o.line, o.col); }
    return parseCmp(T);
}
function parseCmp(T) {
    const l = parseAdd(T);
    const x = T.peek();
    if (x && x.t === "p" && CMP.has(x.v)) {
        T.next();
        const r = parseAdd(T);
        const y = T.peek();
        if (y && y.t === "p" && CMP.has(y.v)) throw new ModeError("comparisons don't chain: add parentheses", y.line, y.col);
        return loc({ op: x.v, a: l, b: r }, x.line, x.col);
    }
    return l;
}
function parseAdd(T) {
    let l = parseMul(T);
    while (T.atP("+") || T.atP("-")) { const o = T.next(); l = loc({ op: o.v, a: l, b: parseMul(T) }, o.line, o.col); }
    return l;
}
function parseMul(T) {
    let l = parseUnary(T);
    while (T.atP("*") || T.atP("/")) { const o = T.next(); l = loc({ op: o.v, a: l, b: parseUnary(T) }, o.line, o.col); }
    return l;
}
function parseUnary(T) {
    if (T.atP("-")) {
        const o = T.next();
        const n = T.peek();
        if (n && n.t === "num") { T.next(); return loc({ op: "num", type: n.type, v: -roundTo(n.v, n.type) }, o.line, o.col); }
        return loc({ op: "neg", a: parseUnary(T) }, o.line, o.col);
    }
    return parsePostfix(T);
}
// `everyone` | `playing` | `team(<expr>)` (only after `in`)
function parseSet(T) {
    const s = T.expectName("a player set (everyone, playing, team(t))");
    if (s.v === "everyone" || s.v === "playing") return { set: s.v };
    if (s.v === "team") { T.expectP("("); const team = parseExpr(T); T.expectP(")"); return { set: "team", team }; }
    throw new ModeError(`'${s.v}' isn't a player set (everyone, playing, team(t))`, s.line, s.col);
}
function parseAgg(T, x) {
    // fn ( var in SET [where cond] [by expr] )
    const v = T.expectName("the player variable");
    T.expectKw("in");
    const node = loc({ op: "agg", fn: x.v, var: v.v, set: parseSet(T) }, x.line, x.col);
    if (T.atKw("where")) { T.next(); node.where = parseExpr(T); }
    if (T.atKw("by")) { T.next(); node.by = parseExpr(T); }
    T.expectP(")");
    return node;
}
function parsePostfix(T) {
    const x = T.peek();
    if (!x) T.fail("expected a value");
    if (x.t === "num") { T.next(); return loc({ op: "num", type: x.type, v: roundTo(x.v, x.type) }, x.line, x.col); }
    if (x.t === "str") { T.next(); return loc({ op: "str", v: x.v }, x.line, x.col); }
    if (x.t === "kw" && (x.v === "yes" || x.v === "true")) { T.next(); return loc({ op: "bool", v: true }, x.line, x.col); }
    if (x.t === "kw" && (x.v === "no" || x.v === "false")) { T.next(); return loc({ op: "bool", v: false }, x.line, x.col); }
    if (x.t === "kw" && CONST_KW.has(x.v)) { T.next(); return loc({ op: "name", name: x.v }, x.line, x.col); }
    if (x.t === "p" && x.v === "(") {
        T.next();
        const e = parseExpr(T);
        T.expectP(")");
        return e;
    }
    if (x.t === "name") {
        T.next();
        if (T.atP("(")) {
            T.next();
            if (AGGREGATES.has(x.v) && T.at("name") && T.peek(1) && T.peek(1).t === "kw" && T.peek(1).v === "in") return parseAgg(T, x);
            const args = [];
            if (!T.atP(")")) {
                for (;;) { args.push(parseExpr(T)); if (T.atP(",")) { T.next(); continue; } break; }
            }
            T.expectP(")");
            return loc({ op: "call", fn: x.v, args }, x.line, x.col);
        }
        if (T.atP("[")) {
            T.next();
            const index = parseExpr(T);
            T.expectP("]");
            return loc({ op: "index", array: x.v, index }, x.line, x.col);
        }
        if (x.v === "_") return loc({ op: "hole" }, x.line, x.col);   // an empty input (blocks / graph): an error when checked
        return loc({ op: "name", name: x.v }, x.line, x.col);
    }
    T.fail(`unexpected '${x.v}'`);
}

// ---------------------------------------------------------------- lines
// the comment starts at the first # outside a string
function commentAt(s) {
    let q = false;
    for (let i = 0; i < s.length; i++) { const c = s[i]; if (c === '"') q = !q; else if (c === "#" && !q) return i; }
    return -1;
}
function splitLines(text, line0 = 1) {
    const raw = text.replace(/\r\n/g, "\n").split("\n");
    if (raw.length && raw[raw.length - 1] === "") raw.pop();
    return raw.map((s, i) => {
        const line = i + line0;
        let code = s, comment = null;
        const h = commentAt(s);
        if (h >= 0) { code = s.slice(0, h); comment = s.slice(h).replace(/\s+$/, ""); }
        const tab = code.indexOf("\t");
        if (tab >= 0) throw new ModeError("tab: indent with spaces", line, tab + 1);
        const indent = code.length - code.replace(/^ +/, "").length;
        code = code.replace(/\s+$/, "");
        return { line, indent, code: code.slice(indent), col0: indent + 1, comment, blank: code.trim() === "" && comment === null };
    });
}

class LineReader {
    constructor(lines) { this.L = lines; this.i = 0; this.pending = []; }
    skip() {
        while (this.i < this.L.length) {
            const l = this.L[this.i];
            if (l.blank) { if (this.pending[this.pending.length - 1] !== "") this.pending.push(""); this.i++; continue; }
            if (l.code === "") { this.pending.push(l.comment); this.i++; continue; }
            break;
        }
    }
    peek() { this.skip(); return this.L[this.i]; }
    take() { this.skip(); return this.L[this.i++]; }
    comments() { const c = this.pending; this.pending = []; return c; }
}

function attach(node, comments, trail) {
    if (comments && comments.length) node.comments = comments;
    if (trail) node.trail = trail;
    return node;
}

// ---------------------------------------------------------------- parse
// text: the logic block (from its `logic N` line). line0: the file line of that first line (errors are counted
// from it; 1 = the logic text on its own).
export function parse(text, line0 = 1) {
    const R = new LineReader(splitLines(text, line0));
    const ast = { format: "qmode-logic-ast", version: LOGIC_VERSION, base: null, decls: [], tail: [] };
    let l = R.take();
    if (!l) throw new ModeError("empty: the logic starts with 'logic 1'", line0, 1);
    if (R.pending.some((c) => c !== "")) throw new ModeError("comments go after the 'logic 1' line", l.line, 1);
    R.comments();
    if (l.indent) throw new ModeError("unexpected indent", l.line, 1);
    let T = lineToks(l);
    if (!T.atKw("logic")) T.fail("the logic starts with 'logic 1'");
    T.next();
    const v = T.next();
    if (!v || v.t !== "num" || v.type !== "int") T.fail("expected the logic version number");
    if (v.v > LOGIC_VERSION) throw new ModeError(`logic ${v.v} is newer than this editor knows (${LOGIC_VERSION})`, v.line, v.col);
    if (v.v < 1) throw new ModeError("bad logic version", v.line, v.col);
    ast.version = v.v;
    T.end(); noTrail(l);
    const p = R.peek();
    if (p && !p.indent && /^base(\s|$)/.test(p.code)) {
        if (R.pending.length) throw new ModeError("the header lines carry no comments", p.line, 1);
        R.take();
        const m = /^base\s+([A-Za-z0-9_]+)$/.exec(p.code);
        if (!m) throw new ModeError("expected 'base <mode>'", p.line, p.col0);
        ast.base = m[1];
        noTrail(p);
    }
    for (;;) {
        const d = R.peek();
        if (!d) break;
        if (d.indent) throw new ModeError("unexpected indent", d.line, 1);
        R.take();
        const comments = R.comments();
        ast.decls.push(attach(parseDecl(d, R), comments, d.comment));
    }
    ast.tail = R.comments();
    while (ast.tail.length && ast.tail[ast.tail.length - 1] === "") ast.tail.pop();
    return ast;
}

function noTrail(l) { if (l.comment) throw new ModeError("the header lines carry no comments", l.line, l.col0 + l.code.length + 1); }
function lineToks(l) { return new Toks(tokenize(l.code, l.line, l.col0), l.line, l.col0 + l.code.length); }

function dataNum(T, type, what) {
    let neg = false;
    if (T.atP("-")) { T.next(); neg = true; }
    const x = T.next();
    if (!x || x.t !== "num") { T.i--; T.fail(`expected ${what}`); }
    if (x.f) throw new ModeError("data numbers take the declared type: no 'f'", x.line, x.col);
    if (type === "int" && x.type !== "int") throw new ModeError("an int value", x.line, x.col);
    return roundTo(neg ? -x.v : x.v, type);
}
function dataValue(T, type) {
    if (type === "bool") {
        const b = T.next();
        if (!b || b.t !== "kw" || !["yes", "no", "true", "false"].includes(b.v)) { if (b) T.i--; T.fail("expected yes or no"); }
        return b.v === "yes" || b.v === "true";
    }
    if (type === "team" || type === "player") {
        const b = T.next();
        if (!b || b.t !== "kw" || !CONST_KW.has(b.v) || (type === "player" && b.v !== "none")) { if (b) T.i--; T.fail(type === "team" ? "expected A, B or none" : "expected none"); }
        return b.v;
    }
    return dataNum(T, type, "a number");
}

function parseDecl(l, R) {
    const T = lineToks(l);
    const k = T.next();
    if (!k || k.t !== "kw") T.i--, T.fail("expected param, curve, use, state, timer, proc or slot");
    const at = (n) => loc(n, k.line, k.col);
    if (k.v === "param") {
        const name = T.expectName("the param's name").v;
        const tt = T.expectName("a type (f32 f64 int bool)");
        if (!PARAM_TYPES.includes(tt.v)) throw new ModeError(`unknown type '${tt.v}' (f32 f64 int bool)`, tt.line, tt.col);
        T.expectP("=");
        const value = dataValue(T, tt.v);
        if (!T.done()) T.fail("a mode param has one value (modes have no skill levels)");
        return at({ kind: "param", name, type: tt.v, value });
    }
    if (k.v === "curve") {
        const name = T.expectName("the curve's name").v;
        T.expectP("(");
        const arg = T.expectName("the argument's name").v;
        const at2 = T.expectName("the argument's type (f32 or f64)");
        if (at2.v !== "f32" && at2.v !== "f64") throw new ModeError("a curve's argument is f32 or f64", at2.line, at2.col);
        T.expectP(")");
        T.expectP("=");
        const form = T.peek();
        if (form && form.t === "name" && (form.v === "steps" || form.v === "points")) {
            T.next();
            if (at2.v !== "f32") throw new ModeError(`${form.v} curves take an f32`, at2.line, at2.col);
            const pts = [];
            let base;
            if (form.v === "steps") { base = dataNum(T, "f32", "the base value"); }
            while (!T.done()) {
                if (form.v === "steps" || pts.length) T.expectP(",");
                const x = dataNum(T, "f32", "x");
                T.expectP(":");
                pts.push([x, dataNum(T, "f32", "y")]);
            }
            for (let i = 1; i < pts.length; i++) if (form.v === "points" ? pts[i][0] <= pts[i - 1][0] : pts[i][0] < pts[i - 1][0]) throw new ModeError(`x must ascend${form.v === "points" ? " strictly" : ""}`, l.line, l.col0);
            if (form.v === "points" && (pts.length < 1 || pts.length > 16)) throw new ModeError("1 to 16 points", l.line, l.col0);
            return form.v === "steps" ? at({ kind: "curve", name, arg, argType: at2.v, form: "steps", base, points: pts })
                : at({ kind: "curve", name, arg, argType: at2.v, form: "points", points: pts });
        }
        const body = parseExpr(T);
        T.end();
        return at({ kind: "curve", name, arg, argType: at2.v, form: "expr", body });
    }
    if (k.v === "use") {
        const o = T.expectName("an object (bomb cart ncart points race drain it tiebreak bracket duelmods)");
        T.end();
        return at({ kind: "use", object: o.v });
    }
    if (k.v === "state") {
        const life = T.next();
        if (!life || life.t !== "kw" || (life.v !== "match" && life.v !== "round")) { if (life) T.i--; T.fail("expected 'match' or 'round' (when the value resets)"); }
        const name = T.expectName("the state's name").v;
        let per = null;
        if (T.atP("[")) {
            T.next();
            const pp = T.expectName("team or player");
            if (pp.v !== "team" && pp.v !== "player") throw new ModeError("a state array is per team or per player", pp.line, pp.col);
            per = pp.v;
            T.expectP("]");
        }
        const tt = T.expectName("a type (f32 f64 int bool team player)");
        if (!STATE_TYPES.includes(tt.v)) throw new ModeError(`unknown type '${tt.v}' (${STATE_TYPES.join(" ")})`, tt.line, tt.col);
        T.expectP("=");
        const value = dataValue(T, tt.v);
        T.end();
        return at({ kind: "state", life: life.v, name, per, type: tt.v, value });
    }
    if (k.v === "timer") {
        const name = T.expectName("the timer's name").v;
        T.end();
        return at({ kind: "timer", name });
    }
    if (k.v === "proc") {
        const name = T.expectName("the proc's name").v;
        T.expectP(":");
        T.end();
        const n = at({ kind: "proc", name });
        n.body = parseBody(R, 0, l);
        return n;
    }
    if (k.v === "slot") {
        let n;
        if (T.atKw("timer")) { T.next(); n = at({ kind: "slot", name: "timer", timer: T.expectName("the timer's name").v }); }
        else n = at({ kind: "slot", name: T.expectName("the slot's name").v });
        T.expectP(":");
        T.end();
        n.body = parseBody(R, 0, l);
        return n;
    }
    T.i--; T.fail(`expected param, curve, use, state, timer, proc or slot, not '${k.v}'`);
}

function parseBody(R, parentIndent, head) {
    const first = R.peek();
    if (!first || first.indent <= parentIndent) throw new ModeError("expected an indented block", head.line, head.col0 + head.code.length);
    const ind = first.indent;
    const out = [];
    for (;;) {
        const l = R.peek();
        if (!l || l.indent < ind) break;
        if (l.indent > ind) throw new ModeError("unexpected indent", l.line, l.indent + 1);
        if (/^(elif|else)\b/.test(l.code)) throw new ModeError(`'${l.code.split(/\W/)[0]}' without an 'if'`, l.line, l.col0);
        R.take();
        const comments = R.comments();
        out.push(parseStmtLine(l, R, ind, comments));
    }
    return out;
}

function parseStmtLine(l, R, ind, comments) {
    const T = lineToks(l);
    if (T.atKw("if")) {
        const k = T.next();
        const node = loc({ op: "if", arms: [] }, k.line, k.col);
        attach(node, comments);
        node.arms.push(parseArm(T, l, R, ind, true));
        for (;;) {
            const n = R.peek();
            if (!n || n.indent !== ind || !/^(elif|else)\b/.test(n.code)) break;
            R.take();
            const cm = R.comments();
            const T2 = lineToks(n);
            const kw = T2.next();
            if (kw.v === "elif") { const arm = parseArm(T2, n, R, ind, true); attach(arm, cm, arm.trail); node.arms.push(arm); }
            else { const arm = parseArm(T2, n, R, ind, false); node.else = attach({ body: arm.body }, cm, arm.trail); break; }
        }
        return node;
    }
    if (T.atKw("for")) {
        const k = T.next();
        const v = T.expectName("the player variable");
        T.expectKw("in");
        const node = loc({ op: "for", var: v.v, set: parseSet(T) }, k.line, k.col);
        if (T.atKw("where")) { T.next(); node.where = parseExpr(T); }
        T.expectP(":");
        if (!T.done()) {
            if (T.atKw("if") || T.atKw("for") || T.atKw("elif") || T.atKw("else")) T.fail("an inline loop holds one simple statement (write it as a block)");
            node.body = [parseSimple(T)];
        } else node.body = parseBody(R, ind, l);
        return attach(node, comments, l.comment);
    }
    const s = parseSimple(T);
    return attach(s, comments, l.comment);
}

function parseArm(T, l, R, ind, hasCond) {
    const arm = {};
    if (hasCond) arm.cond = parseExpr(T);
    T.expectP(":");
    if (!T.done()) {
        if (T.atKw("if") || T.atKw("elif") || T.atKw("else") || T.atKw("for")) T.fail("an inline arm holds one simple statement (write it as a block)");
        arm.body = [parseSimple(T)];
    } else arm.body = parseBody(R, ind, l);
    if (l.comment) arm.trail = l.comment;
    return arm;
}

function parseSimple(T) {
    const x = T.peek();
    if (!x) T.fail("expected a statement");
    const at = (n) => loc(n, x.line, x.col);
    if (x.t === "kw") {
        T.next();
        if (x.v === "return") {
            const values = [];
            if (!T.done()) for (;;) { values.push(parseExpr(T)); if (T.atP(",")) { T.next(); continue; } break; }
            T.end();
            return at({ op: "return", values });
        }
        if (x.v === "pass" || x.v === "break") { T.end(); return at({ op: x.v }); }
        T.i--; T.fail(`unexpected '${x.v}'`);
    }
    if (x.t === "name") {
        const n1 = T.peek(1);
        if (n1 && n1.t === "p" && n1.v === "=") {
            T.next(); T.next();
            const value = parseExpr(T);
            T.end();
            return at({ op: "set", name: x.v, value });
        }
        if (n1 && n1.t === "p" && n1.v === "[") {
            T.next(); T.next();
            const index = parseExpr(T);
            T.expectP("]");
            T.expectP("=");
            const value = parseExpr(T);
            T.end();
            return at({ op: "set", name: x.v, index, value });
        }
        if (n1 && n1.t === "p" && n1.v === "(") {
            const e = parsePostfix(T);
            T.end();
            if (e.op !== "call") throw new ModeError(`${e.fn || x.v}(...) has a value: use it in an expression`, x.line, x.col);
            return at({ op: "do", fn: e.fn, args: e.args });
        }
    }
    T.fail(x.t === "name" ? `expected '=' or '(' after '${x.v}' (or a statement: return, pass, break, for, if)` : `unexpected '${x.v}'`);
}

// ---------------------------------------------------------------- print
const PREC = { "?:": 1, or: 2, and: 3, not: 4, "<": 5, "<=": 5, ">": 5, ">=": 5, "==": 5, "!=": 5, "+": 6, "-": 6, "*": 7, "/": 7, neg: 8 };
const prec = (e) => PREC[e.op] ?? 9;

export function printSet(s) { return s.set === "team" ? `team(${printExpr(s.team)})` : s.set; }
export function printExpr(e) {
    switch (e.op) {
        case "num": return exprNumText(e.v, e.type);
        case "bool": return e.v ? "yes" : "no";
        case "name": return e.name;
        case "hole": return "_";
        case "str": return `"${e.v}"`;
        case "call": return `${e.fn}(${e.args.map(printExpr).join(", ")})`;
        case "index": return `${e.array}[${printExpr(e.index)}]`;
        case "agg": return `${e.fn}(${e.var} in ${printSet(e.set)}${e.where ? " where " + printExpr(e.where) : ""}${e.by ? " by " + printExpr(e.by) : ""})`;
        case "neg": return "-" + wrap(e.a, prec(e.a) < 8 || (e.a.op === "num" && e.a.v < 0) || e.a.op === "neg");
        case "not": return "not " + wrap(e.a, prec(e.a) < 4 || prec(e.a) === 5);
        case "?:": return `${wrap(e.c, e.c.op === "?:")} ? ${wrap(e.a, e.a.op === "?:")} : ${printExpr(e.b)}`;
        default: {
            const p = PREC[e.op];
            if (p === undefined) throw new Error("unknown expression op " + e.op);
            let la = prec(e.a) < p, rb = prec(e.b) <= p;
            if (p === 5) { la = prec(e.a) <= 5; rb = prec(e.b) <= 5; }
            if (opts.andInOrParens && e.op === "or") { la = la || e.a.op === "and"; rb = rb || e.b.op === "and"; }
            return `${wrap(e.a, la)} ${e.op} ${wrap(e.b, rb)}`;
        }
    }
}
const wrap = (e, paren) => (paren ? `(${printExpr(e)})` : printExpr(e));

export function dataText(v, type) {
    if (type === "bool") return v ? "yes" : "no";
    if (type === "team" || type === "player") return String(v);
    return canonNum(v, type);
}

export function printDeclHead(d) {
    switch (d.kind) {
        case "param": return `param ${d.name} ${d.type} = ${dataText(d.value, d.type)}`;
        case "curve": {
            const head = `curve ${d.name}(${d.arg} ${d.argType}) = `;
            if (d.form === "steps") return head + "steps " + [canonNum(d.base, "f32"), ...d.points.map(([x, y]) => `${canonNum(x, "f32")}: ${canonNum(y, "f32")}`)].join(", ");
            if (d.form === "points") return head + "points " + d.points.map(([x, y]) => `${canonNum(x, "f32")}: ${canonNum(y, "f32")}`).join(", ");
            return head + printExpr(d.body);
        }
        case "use": return `use ${d.object}`;
        case "state": return `state ${d.life} ${d.name}${d.per ? `[${d.per}]` : ""} ${d.type} = ${dataText(d.value, d.type)}`;
        case "timer": return `timer ${d.name}`;
        case "proc": return `proc ${d.name}:`;
        case "slot": return d.name === "timer" ? `slot timer ${d.timer}:` : `slot ${d.name}:`;
    }
    throw new Error("unknown declaration kind " + d.kind);
}

export function printSimple(s) {
    switch (s.op) {
        case "set": return `${s.name}${s.index ? `[${printExpr(s.index)}]` : ""} = ${printExpr(s.value)}`;
        case "return": return s.values.length ? "return " + s.values.map(printExpr).join(", ") : "return";
        case "do": return `${s.fn}(${s.args.map(printExpr).join(", ")})`;
        case "pass": case "break": return s.op;
    }
    throw new Error("unknown statement op " + s.op);
}

const inlineable = (body) => body.length === 1 && body[0].op !== "if" && body[0].op !== "for" && !(body[0].comments && body[0].comments.length) && !body[0].trail;

function printStmts(body, ind, out) { for (const s of body) printStmt(s, ind, out); }
function comments(c, ind, out) { if (c) for (const x of c) out.push(x === "" ? "" : ind + x); }
const trail = (t) => (t ? " " + t : "");

function printStmt(s, ind, out) {
    comments(s.comments, ind, out);
    if (s.op === "for") {
        out.push(`${ind}for ${s.var} in ${printSet(s.set)}${s.where ? " where " + printExpr(s.where) : ""}:${trail(s.trail)}`);
        printStmts(s.body, ind + "  ", out);
        return;
    }
    if (s.op !== "if") { out.push(ind + printSimple(s) + trail(s.trail)); return; }
    const arm = (head, a) => {
        if (inlineable(a.body)) out.push(`${ind}${head}: ${printSimple(a.body[0])}${trail(a.trail)}`);
        else { out.push(`${ind}${head}:${trail(a.trail)}`); printStmts(a.body, ind + "  ", out); }
    };
    s.arms.forEach((a, i) => {
        if (i) comments(a.comments, ind, out);
        arm(`${i ? "elif" : "if"} ${printExpr(a.cond)}`, a);
    });
    if (s.else) { comments(s.else.comments, ind, out); arm("else", s.else); }
}

export function printDecl(d, out = []) {
    comments(d.comments, "", out);
    out.push(printDeclHead(d) + trail(d.trail));
    if (d.kind === "slot" || d.kind === "proc") printStmts(d.body, "  ", out);
    return out;
}

export function print(ast) {
    const out = [`logic ${ast.version ?? LOGIC_VERSION}`];
    if (ast.base) out.push(`base ${ast.base}`);
    for (const d of ast.decls) printDecl(d, out);
    if (ast.tail && ast.tail.length) comments(ast.tail, "", out);
    return out.join("\n") + "\n";
}

export function printSlotBody(d) { const out = []; printStmts(d.body, "", out); return out.join("\n"); }

// ---------------------------------------------------------------- syntax highlighting (Code view)
const HL_KW = /^(logic|base|param|curve|use|state|match|round|timer|proc|slot|if|elif|else|return|pass|for|in|where|by|break|and|or|not)$/;
const HL_CONST = /^(yes|no|true|false|none|A|B|steps|points)$/;
const HL_TYPE = /^(f32|f64|int|bool|team|player)$/;
export function highlightLine(s, known = {}) {
    const esc = (t) => t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    const h = commentAt(s);
    const code = h >= 0 ? s.slice(0, h) : s, com = h >= 0 ? s.slice(h) : "";
    let out = "";
    const re = /([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)|([0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?f?)|("[^"]*")|(\s+)|(.)/g;
    let m;
    while ((m = re.exec(code))) {
        if (m[1]) {
            const w = m[1];
            const cls = HL_KW.test(w) ? "k" : HL_CONST.test(w) ? "c" : HL_TYPE.test(w) ? "t" : known.params && known.params.has(w) ? "p"
                : known.curves && known.curves.has(w) ? "u" : known.states && known.states.has(w) ? "l" : known.consts && known.consts.has(w) ? "c"
                : known.actions && known.actions.has(w) ? "a" : known.builtins && known.builtins.has(w) ? "b" : known.sensors && known.sensors.has(w) ? "s" : "";
            out += cls ? `<span class="h${cls}">${esc(w)}</span>` : esc(w);
        } else if (m[2]) out += `<span class="hn">${esc(m[2])}</span>`;
        else if (m[3]) out += `<span class="hstr">${esc(m[3])}</span>`;
        else out += esc(m[0]);
    }
    if (com) out += `<span class="hcm">${esc(com)}</span>`;
    return out;
}
