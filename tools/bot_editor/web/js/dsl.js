// .qbrain text <-> JSON AST (tools/bots/GRAMMAR.md, version 1). The parser takes the grammar; the printer writes
// canonical text, so print(parse(t)) === t for canonical t and parse(print(ast)) deep-equals ast.
// Parse errors carry line:col (1-based). Nodes get a non-enumerable `_loc` {line, col} (not part of the JSON).

export const KEYWORDS = new Set(["version", "brain", "levels", "param", "curve", "list", "table", "slot", "if", "elif", "else",
    "return", "goto", "fallback", "seek", "evade", "pass", "and", "or", "not", "in", "yes", "no", "true", "false"]);
export const LEVELS = ["BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"];
export const PARAM_TYPES = ["f32", "f64", "int", "bool"];
const CMP = new Set(["<", "<=", ">", ">=", "==", "!="]);

// Readability parentheses around an `and` inside an `or` (GRAMMAR.md section 3). The grammar asks for them; set
// false if the C++ printer ends up not writing them (see the report: default.qbrain is written without them).
export const opts = { andInOrParens: true };

export class BrainError extends Error {
    constructor(msg, line, col) { super(`${line}:${col}: ${msg}`); this.msg = msg; this.line = line; this.col = col; }
}
const loc = (node, line, col) => { Object.defineProperty(node, "_loc", { value: { line, col }, enumerable: false, writable: true }); return node; };

// ---------------------------------------------------------------- numbers
export function f32(v) { return Math.fround(v); }
// The shortest decimal (JS toString style) that reads back as the same value of its type.
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
// value of a number of the type, as the canonical decimal (an f32 1.55 is 1.55, not 1.5499999523)
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
        if (c === "\t") throw new BrainError("tab: indent and space with spaces", line, col0 + i);
        const col = col0 + i;
        NAME_RE.lastIndex = i;
        let m = NAME_RE.exec(text);
        if (m) { toks.push({ t: KEYWORDS.has(m[0]) ? "kw" : "name", v: m[0], line, col }); i += m[0].length; continue; }
        NUM_RE.lastIndex = i;
        m = NUM_RE.exec(text);
        if (m) {
            const s = m[0];
            if (/[A-Za-z_.]/.test(text[i + s.length] || "")) throw new BrainError(`bad number '${text.slice(i).split(/[\s,)]/)[0]}'`, line, col);
            const isF = s.endsWith("f"), body = isF ? s.slice(0, -1) : s;
            toks.push({ t: "num", v: +body, type: isF ? "f32" : /[.eE]/.test(body) ? "f64" : "int", text: s, f: isF, line, col });
            i += s.length; continue;
        }
        if (c === '"') {
            const j = text.indexOf('"', i + 1);
            if (j < 0) throw new BrainError("unterminated string", line, col);
            const s = text.slice(i + 1, j);
            if (s.includes("\\")) throw new BrainError("no \\ in strings", line, col);
            toks.push({ t: "str", v: s, line, col }); i = j + 1; continue;
        }
        const p = PUNCT.find((q) => text.startsWith(q, i));
        if (p) { toks.push({ t: "p", v: p, line, col }); i += p.length; continue; }
        throw new BrainError(`unexpected '${c}'`, line, col);
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
    fail(msg) { const [l, c] = this.where(); throw new BrainError(msg, l, c); }
    expectP(v) { if (!this.atP(v)) this.fail(`expected '${v}'${this.peek() ? ` but found '${this.peek().v}'` : " at the end of the line"}`); return this.next(); }
    expectKw(v) { if (!this.atKw(v)) this.fail(`expected '${v}'`); return this.next(); }
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
        if (y && ((y.t === "p" && CMP.has(y.v)) || (y.t === "kw" && y.v === "in"))) throw new BrainError("comparisons don't chain: add parentheses", y.line, y.col);
        return loc({ op: x.v, a: l, b: r }, x.line, x.col);
    }
    if (x && x.t === "kw" && x.v === "in") {
        T.next();
        const n = T.expectName("a list name after 'in'");
        const y = T.peek();
        if (y && ((y.t === "p" && CMP.has(y.v)) || (y.t === "kw" && y.v === "in"))) throw new BrainError("comparisons don't chain: add parentheses", y.line, y.col);
        return loc({ op: "in", a: l, list: n.v }, x.line, x.col);
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
function parsePostfix(T) {
    const x = T.peek();
    if (!x) T.fail("expected a value");
    if (x.t === "num") { T.next(); return loc({ op: "num", type: x.type, v: roundTo(x.v, x.type) }, x.line, x.col); }
    if (x.t === "kw" && (x.v === "yes" || x.v === "true")) { T.next(); return loc({ op: "bool", v: true }, x.line, x.col); }
    if (x.t === "kw" && (x.v === "no" || x.v === "false")) { T.next(); return loc({ op: "bool", v: false }, x.line, x.col); }
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
            return loc({ op: "index", table: x.v, index }, x.line, x.col);
        }
        if (x.v === "_") return loc({ op: "hole" }, x.line, x.col);   // an empty input (blocks / graph): an error when checked
        return loc({ op: "name", name: x.v }, x.line, x.col);
    }
    T.fail(`unexpected '${x.v}'`);
}

// ---------------------------------------------------------------- lines
function splitLines(text) {
    const raw = text.replace(/\r\n/g, "\n").split("\n");
    if (raw.length && raw[raw.length - 1] === "") raw.pop();
    return raw.map((s, i) => {
        const line = i + 1;
        let code = s, comment = null;
        const h = s.indexOf("#");
        if (h >= 0) { code = s.slice(0, h); comment = s.slice(h).replace(/\s+$/, ""); }
        const tab = code.indexOf("\t");
        if (tab >= 0) throw new BrainError("tab: indent with spaces", line, tab + 1);
        const indent = code.length - code.replace(/^ +/, "").length;
        code = code.replace(/\s+$/, "");
        return { line, indent, code: code.slice(indent), col0: indent + 1, comment, blank: code.trim() === "" && comment === null };
    });
}

class LineReader {
    constructor(lines) { this.L = lines; this.i = 0; this.pending = []; }
    // skip blank / comment-only lines into pending comments
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
export function parse(text) {
    const R = new LineReader(splitLines(text));
    const brain = { format: "qbrain-ast", version: 1, name: "", levels: LEVELS.slice(), decls: [], tail: [] };
    // header
    let l = R.take();
    if (!l) throw new BrainError("empty file: it starts with 'version 1'", 1, 1);
    if (R.pending.some((c) => c !== "")) throw new BrainError("the header lines carry no comments", l.line, 1);
    R.comments();
    let T = lineToks(l);
    if (!T.atKw("version")) T.fail("a brain starts with 'version 1'");
    T.next();
    const v = T.next();
    if (!v || v.t !== "num" || v.type !== "int") T.fail("expected the version number");
    if (v.v > 1) throw new BrainError(`version ${v.v} is newer than this editor knows (1)`, v.line, v.col);
    if (v.v < 1) throw new BrainError("bad version", v.line, v.col);
    brain.version = v.v;
    T.end(); noTrail(l);
    l = R.take();
    if (R.pending.length) throw new BrainError("the header lines carry no comments", l ? l.line : 1, 1);
    T = l && lineToks(l);
    if (!T || !T.atKw("brain")) (T || new Toks([], l ? l.line : 1, 1)).fail("expected 'brain \"<name>\"' on the second line");
    T.next();
    const nm = T.next();
    if (!nm || nm.t !== "str") T.fail("expected the brain's name in quotes");
    if (new TextEncoder().encode(nm.v).length > 64) throw new BrainError("the name is at most 64 bytes", nm.line, nm.col);
    brain.name = nm.v;
    T.end(); noTrail(l);
    const p = R.peek();
    if (p && /^levels\b/.test(p.code) && !R.pending.length) {
        R.take();
        T = lineToks(p); T.next();
        const names = [];
        while (!T.done()) names.push(T.expectName("a level name").v);
        if (names.join(" ") !== LEVELS.join(" ")) throw new BrainError("levels: exactly " + LEVELS.join(" "), p.line, p.col0);
        noTrail(p);
    }
    // declarations
    for (;;) {
        const d = R.peek();
        if (!d) break;
        if (d.indent) throw new BrainError("unexpected indent", d.line, 1);
        R.take();
        const comments = R.comments();
        brain.decls.push(attach(parseDecl(d, R), comments, d.comment));
    }
    brain.tail = R.comments();
    while (brain.tail.length && brain.tail[brain.tail.length - 1] === "") brain.tail.pop();
    return brain;
}

function noTrail(l) { if (l.comment) throw new BrainError("the header lines carry no comments", l.line, l.col0 + l.code.length + 1); }
function lineToks(l) { return new Toks(tokenize(l.code, l.line, l.col0), l.line, l.col0 + l.code.length); }

function dataNum(T, type, what) {
    let neg = false;
    if (T.atP("-")) { T.next(); neg = true; }
    const x = T.next();
    if (!x || x.t !== "num") { T.i--; T.fail(`expected ${what}`); }
    if (x.f) throw new BrainError("data numbers take the declared type: no 'f'", x.line, x.col);
    if (type === "int" && x.type !== "int") throw new BrainError("an int value", x.line, x.col);
    return roundTo(neg ? -x.v : x.v, type);
}

function parseDecl(l, R) {
    const T = lineToks(l);
    const k = T.next();
    if (!k || k.t !== "kw") T.i--, T.fail("expected param, curve, list, table or slot");
    const at = (n) => loc(n, k.line, k.col);
    if (k.v === "param") {
        const name = T.expectName("the param's name").v;
        const tt = T.expectName("a type (f32 f64 int bool)");
        if (!PARAM_TYPES.includes(tt.v)) throw new BrainError(`unknown type '${tt.v}' (f32 f64 int bool)`, tt.line, tt.col);
        T.expectP("=");
        const values = [];
        while (!T.done()) {
            if (tt.v === "bool") {
                const b = T.next();
                if (b.t !== "kw" || !["yes", "no", "true", "false"].includes(b.v)) throw new BrainError("expected yes or no", b.line, b.col);
                values.push(b.v === "yes" || b.v === "true");
            } else values.push(dataNum(T, tt.v, "a number"));
        }
        if (values.length !== 1 && values.length !== 4) T.fail(`one value (every level) or four (${LEVELS.join(" ")}), not ${values.length}`);
        return at({ kind: "param", name, type: tt.v, values: values.length === 4 && values.every((x) => x === values[0]) ? [values[0]] : values });
    }
    if (k.v === "curve") {
        const name = T.expectName("the curve's name").v;
        T.expectP("(");
        const arg = T.expectName("the argument's name").v;
        const at2 = T.expectName("the argument's type (f32 or f64)");
        if (at2.v !== "f32" && at2.v !== "f64") throw new BrainError("a curve's argument is f32 or f64", at2.line, at2.col);
        T.expectP(")");
        T.expectP("=");
        const form = T.peek();
        if (form && form.t === "name" && (form.v === "steps" || form.v === "points")) {
            T.next();
            if (at2.v !== "f32") throw new BrainError(`${form.v} curves take an f32`, at2.line, at2.col);
            const pts = [];
            let base;
            if (form.v === "steps") { base = dataNum(T, "f32", "the base value"); }
            while (!T.done()) {
                if (form.v === "steps" || pts.length) T.expectP(",");
                const x = dataNum(T, "f32", "x");
                T.expectP(":");
                pts.push([x, dataNum(T, "f32", "y")]);
            }
            for (let i = 1; i < pts.length; i++) if (form.v === "points" ? pts[i][0] <= pts[i - 1][0] : pts[i][0] < pts[i - 1][0]) throw new BrainError(`x must ascend${form.v === "points" ? " strictly" : ""}`, l.line, l.col0);
            if (form.v === "points" && (pts.length < 1 || pts.length > 16)) throw new BrainError("1 to 16 points", l.line, l.col0);
            return form.v === "steps" ? at({ kind: "curve", name, arg, argType: at2.v, form: "steps", base, points: pts })
                : at({ kind: "curve", name, arg, argType: at2.v, form: "points", points: pts });
        }
        const body = parseExpr(T);
        T.end();
        return at({ kind: "curve", name, arg, argType: at2.v, form: "expr", body });
    }
    if (k.v === "list") {
        const name = T.expectName("the list's name").v;
        T.expectP("=");
        const items = [];
        while (!T.done()) items.push(T.expectName("a weapon").v);
        if (!items.length) T.fail("a list holds 1 to 11 weapons");
        return at({ kind: "list", name, items });
    }
    if (k.v === "table") {
        const name = T.expectName("the table's name").v;
        T.expectP("=");
        const entries = [];
        let els = null;
        for (;;) {
            if (T.atKw("else")) { T.next(); els = dataNum(T, "f32", "a number"); T.end(); break; }
            const w = T.expectName("a weapon").v;
            entries.push([w, dataNum(T, "f32", "a number")]);
            if (T.done()) break;
            T.expectP(",");
        }
        const n = at({ kind: "table", name, entries });
        if (els !== null) n.else = els;
        return n;
    }
    if (k.v === "slot") {
        const name = T.expectName("the slot's name").v;
        const n = at({ kind: "slot", name });
        if (name === "objective") {
            const m = T.next();
            if (!m || (m.t !== "name" && m.t !== "kw")) T.i--, T.fail("expected the mode (slot objective <mode>:)");
            n.mode = m.v;
        }
        T.expectP(":");
        T.end();
        n.body = parseBody(R, 0, l);
        return n;
    }
    T.i--; T.fail(`expected param, curve, list, table or slot, not '${k.v}'`);
}

// statements of a block deeper than `parentIndent`
function parseBody(R, parentIndent, head) {
    const first = R.peek();
    if (!first || first.indent <= parentIndent) throw new BrainError("expected an indented block", head.line, head.col0 + head.code.length);
    const ind = first.indent;
    const out = [];
    for (;;) {
        const l = R.peek();
        if (!l || l.indent < ind) break;
        if (l.indent > ind) throw new BrainError("unexpected indent", l.line, l.indent + 1);
        if (/^(elif|else)\b/.test(l.code)) throw new BrainError(`'${l.code.split(/\W/)[0]}' without an 'if'`, l.line, l.col0);
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
    const s = parseSimple(T);
    return attach(s, comments, l.comment);
}

function parseArm(T, l, R, ind, hasCond) {
    const arm = {};
    if (hasCond) arm.cond = parseExpr(T);
    T.expectP(":");
    if (!T.done()) {
        if (T.atKw("if") || T.atKw("elif") || T.atKw("else")) T.fail("an inline arm holds one simple statement (write the 'if' as a block)");
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
        if (x.v === "goto") { const point = parseExpr(T); T.end(); return at({ op: "goto", point }); }
        if (x.v === "fallback" || x.v === "seek" || x.v === "evade" || x.v === "pass") { T.end(); return at({ op: x.v }); }
        T.i--; T.fail(`unexpected '${x.v}'`);
    }
    if (x.t === "name" && T.peek(1) && T.peek(1).t === "p" && T.peek(1).v === "=") {
        T.next(); T.next();
        const value = parseExpr(T);
        T.end();
        return at({ op: "set", name: x.v, value });
    }
    T.fail(x.t === "name" ? `expected '=' after '${x.v}' (or a statement: goto, return, fallback, seek, evade, pass)` : `unexpected '${x.v}'`);
}

// ---------------------------------------------------------------- print
const PREC = { "?:": 1, or: 2, and: 3, not: 4, "<": 5, "<=": 5, ">": 5, ">=": 5, "==": 5, "!=": 5, in: 5, "+": 6, "-": 6, "*": 7, "/": 7, neg: 8 };
const prec = (e) => PREC[e.op] ?? 9;

export function printExpr(e) {
    switch (e.op) {
        case "num": return exprNumText(e.v, e.type);
        case "bool": return e.v ? "yes" : "no";
        case "name": return e.name;
        case "hole": return "_";
        case "call": return `${e.fn}(${e.args.map(printExpr).join(", ")})`;
        case "index": return `${e.table}[${printExpr(e.index)}]`;
        case "neg": return "-" + wrap(e.a, prec(e.a) < 8 || (e.a.op === "num" && e.a.v < 0) || e.a.op === "neg");
        case "not": return "not " + wrap(e.a, prec(e.a) < 4 || prec(e.a) === 5);
        case "?:": return `${wrap(e.c, e.c.op === "?:")} ? ${wrap(e.a, e.a.op === "?:")} : ${printExpr(e.b)}`;
        case "in": return `${wrap(e.a, prec(e.a) <= 5)} in ${e.list}`;
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

function dataText(v, type) { return type === "bool" ? (v ? "yes" : "no") : canonNum(v, type); }

export function printDeclHead(d) {
    switch (d.kind) {
        case "param": {
            const vals = d.values.length === 4 && d.values.every((x) => x === d.values[0]) ? [d.values[0]] : d.values;
            return `param ${d.name} ${d.type} = ${vals.map((v) => dataText(v, d.type)).join(" ")}`;
        }
        case "curve": {
            const head = `curve ${d.name}(${d.arg} ${d.argType}) = `;
            if (d.form === "steps") return head + "steps " + [canonNum(d.base, "f32"), ...d.points.map(([x, y]) => `${canonNum(x, "f32")}: ${canonNum(y, "f32")}`)].join(", ");
            if (d.form === "points") return head + "points " + d.points.map(([x, y]) => `${canonNum(x, "f32")}: ${canonNum(y, "f32")}`).join(", ");
            return head + printExpr(d.body);
        }
        case "list": return `list ${d.name} = ${d.items.join(" ")}`;
        case "table": return `table ${d.name} = ${[...d.entries.map(([w, v]) => `${w} ${canonNum(v, "f32")}`), ...(d.else !== undefined && d.else !== null ? [`else ${canonNum(d.else, "f32")}`] : [])].join(", ")}`;
        case "slot": return `slot ${d.name}${d.name === "objective" ? " " + d.mode : ""}:`;
    }
    throw new Error("unknown declaration kind " + d.kind);
}

export function printSimple(s) {
    switch (s.op) {
        case "set": return `${s.name} = ${printExpr(s.value)}`;
        case "return": return s.values.length ? "return " + s.values.map(printExpr).join(", ") : "return";
        case "goto": return "goto " + printExpr(s.point);
        case "fallback": case "seek": case "evade": case "pass": return s.op;
    }
    throw new Error("unknown statement op " + s.op);
}

const inlineable = (body) => body.length === 1 && body[0].op !== "if" && !(body[0].comments && body[0].comments.length) && !body[0].trail;

function printStmts(body, ind, out) {
    for (const s of body) printStmt(s, ind, out);
}
function comments(c, ind, out) { if (c) for (const x of c) out.push(x === "" ? "" : ind + x); }
const trail = (t) => (t ? " " + t : "");

function printStmt(s, ind, out) {
    comments(s.comments, ind, out);
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
    if (d.kind === "slot") printStmts(d.body, "  ", out);
    return out;
}

export function print(brain) {
    const out = [`version ${brain.version ?? 1}`, `brain "${brain.name}"`, `levels ${(brain.levels || LEVELS).join(" ")}`];
    for (const d of brain.decls) printDecl(d, out);
    if (brain.tail && brain.tail.length) comments(brain.tail, "", out);
    return out.join("\n") + "\n";
}

// A slot's text alone (the Code view's per-item diff, the graph's labels)
export function printSlotBody(d) { const out = []; printStmts(d.body, "", out); return out.join("\n"); }

// ---------------------------------------------------------------- syntax highlighting (Code view)
const HL_KW = /^(version|brain|levels|param|curve|list|table|slot|if|elif|else|return|goto|fallback|seek|evade|pass|and|or|not|in)$/;
const HL_CONST = /^(yes|no|true|false|steps|points)$/;
const HL_TYPE = /^(f32|f64|int|bool)$/;
export function highlightLine(s, known = {}) {
    // returns HTML for one line
    const esc = (t) => t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    let h = s.indexOf("#");
    const code = h >= 0 ? s.slice(0, h) : s, com = h >= 0 ? s.slice(h) : "";
    let out = "", i = 0;
    const re = /([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)|([0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?f?)|("[^"]*")|(\s+)|(.)/g;
    let m;
    while ((m = re.exec(code))) {
        if (m[1]) {
            const w = m[1];
            const cls = HL_KW.test(w) ? "k" : HL_CONST.test(w) ? "c" : HL_TYPE.test(w) ? "t" : known.params && known.params.has(w) ? "p"
                : known.curves && known.curves.has(w) ? "u" : known.lists && known.lists.has(w) ? "l" : known.weapons && known.weapons.has(w) ? "w"
                : known.builtins && known.builtins.has(w) ? "b" : known.sensors && known.sensors.has(w) ? "s" : "";
            out += cls ? `<span class="h${cls}">${esc(w)}</span>` : esc(w);
        } else if (m[2]) out += `<span class="hn">${esc(m[2])}</span>`;
        else if (m[3]) out += `<span class="hstr">${esc(m[3])}</span>`;
        else out += esc(m[0]);
        i = re.lastIndex;
    }
    if (com) out += `<span class="hcm">${esc(com)}</span>`;
    return out;
}
