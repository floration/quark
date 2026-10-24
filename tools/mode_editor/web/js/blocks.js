// Adapted from tools/bot_editor/web/js/blocks.js at c7aae27. Blocks view: a hand-written Scratch / Blockly-like
// editor over the mode logic AST. The model is a block tree made from the AST (toBlocks) and turned back into it
// (fromBlocks): fromBlocks(toBlocks(x)) deep-equals x. Statements stack (drag between stacks), expressions plug into
// inputs, the palette is the catalog (logic, maths, players and teams, sensors, actions, objects, timers, state,
// params), dropping on the palette deletes, dropping on the workspace leaves a loose block (dashed, not in the file).
// Changes from the bot editor: for / break / actions / indexed sets, aggregates, strings; a slot's hat names its event.

import { decompose, compose, CATALOG, WEAPONS, symbols, slotKey, newExpr, newStmt, declKey } from "./lang.js";
import { exprNumText, printDeclHead, roundTo, printSet } from "./qmode.js";

let UID = 1;
// ---------------------------------------------------------------- model (no DOM: node-testable)
export function toBlocks(node) {
    const p = decompose(node);
    const b = { uid: UID++, cat: p.cat, op: p.op, fields: p.fields, inputs: {}, stacks: {} };
    for (const [k, e] of p.inputs) b.inputs[k] = toBlocks(e);
    for (const [k, list] of p.stacks) b.stacks[k] = list.map(toBlocks);
    return b;
}
export function fromBlocks(b) {
    if (!b) return { op: "hole" };
    const inputs = Object.entries(b.inputs).map(([k, v]) => [k, fromBlocks(v)]);
    const stacks = Object.entries(b.stacks).map(([k, v]) => [k, v.map(fromBlocks)]);
    return compose({ cat: b.cat, op: b.op, fields: b.fields, inputs, stacks });
}
export function brainToBlocks(ast) {
    const { decls, ...head } = ast;
    return { head: JSON.parse(JSON.stringify(head)), decls: decls.map(toBlocks) };
}
export function blocksToBrain(m) { return { ...JSON.parse(JSON.stringify(m.head)), decls: m.decls.map(fromBlocks) }; }
export const logicToBlocks = brainToBlocks, blocksToLogic = blocksToBrain;

// ---------------------------------------------------------------- categories and colours
const TIMER_ACTIONS = new Set(["startTimer", "stopTimer", "timerLeft"]);
const actionCat = (fn) => (TIMER_ACTIONS.has(fn) ? "timer" : CATALOG.actions[fn] && CATALOG.actions[fn].object ? "object" : "action");
const CAT_OF = (b) => {
    if (b.cat === "decl") return "hat";
    if (b.cat === "stmt") {
        if (b.op === "if" || b.op === "break" || b.op === "pass") return "logic";
        if (b.op === "for") return "random";
        if (b.op === "set" || b.op === "return") return "set";
        if (b.op === "do") return actionCat(b.fields.fn);
        return "action";
    }
    switch (b.op) {
        case "num": case "+": case "-": case "*": case "/": case "%": case "neg": return "math";
        case "bool": case "and": case "or": case "not": case "?:": case "<": case "<=": case ">": case ">=": case "==": case "!=": return "logic";
        case "agg": case "in": return "random";
        case "str": return "data";
        case "call": {
            const fn = b.fields.fn;
            if (TIMER_ACTIONS.has(fn)) return "timer";
            if (CATALOG.actions[fn]) return actionCat(fn);
            const f = CATALOG.functions[fn];
            if (f) return ["int", "f32", "f64", "min", "max", "clamp", "abs", "sqrt"].includes(fn) ? "math" : "name";
            return "curve";
        }
        case "index": return "data";
        case "name": return "name";
        default: return "hole";
    }
};
const BIN_OPS = ["+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "and", "or"];
const AGG_FNS = ["count", "any", "first", "random", "best", "names"];

function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }

// ---------------------------------------------------------------- the view
export class BlocksView {
    constructor(root, app) {
        this.root = root; this.app = app;
        this.model = null; this.declIx = -1; this.floating = new Map();
        root.classList.add("blocks");
        this.paletteEl = el("div", "bk-palette");
        this.catsEl = el("div", "bk-cats");
        this.itemsEl = el("div", "bk-items");
        this.paletteEl.append(this.catsEl, this.itemsEl);
        this.ws = el("div", "bk-ws");
        this.canvas = el("div", "bk-canvas");
        this.ws.append(this.canvas);
        root.append(this.paletteEl, this.ws);
        this.cat = "logic";
        this._drag = null;
        document.addEventListener("pointermove", (e) => this._dragMove(e));
        document.addEventListener("pointerup", (e) => this._dragEnd(e));
    }

    setBrain(ast, declKeySel) {
        this.brain = ast;
        this.S = symbols(ast);
        this.model = brainToBlocks(ast);
        if (declKeySel !== undefined) this.declIx = ast.decls.findIndex((d) => declKey(d) === declKeySel);
        if (this.declIx < 0 || this.declIx >= ast.decls.length) this.declIx = ast.decls.findIndex((d) => d.kind === "slot");
        this.render();
    }
    select(key) { const i = this.brain.decls.findIndex((d) => declKey(d) === key); if (i >= 0) { this.declIx = i; this.render(); } }

    _changed() {
        const ast = blocksToBrain(this.model);
        this.brain = ast;
        this.S = symbols(ast);
        this.app.brainEdited(ast, "blocks");
        this.render();
    }

    render() {
        const dsel = this.brain && this.declIx >= 0 ? this.brain.decls[this.declIx] : null;
        this.slotCat = dsel && dsel.kind === "slot" ? CATALOG.slots[dsel.name] : null;
        this._palette();
        const c = this.canvas;
        c.textContent = "";
        if (!this.model || this.declIx < 0) { c.append(el("div", "muted", "Pick a slot, proc or curve on the left.")); return; }
        const b = this.model.decls[this.declIx];
        const decl = this.brain.decls[this.declIx];
        this._datalists();
        const top = el("div", "bk-root");
        top.append(this._renderDecl(b, decl));
        c.append(top);
        const fl = this.floating.get(declKey(decl)) || [];
        for (const f of fl) {
            const w = el("div", "bk-float");
            w.style.left = f.x + "px"; w.style.top = f.y + "px";
            const be = f.block.cat === "stmt" ? this._renderStmt(f.block) : this._renderExpr(f.block);
            be._src = { kind: "float", list: fl, item: f };
            w.append(be);
            c.append(w);
        }
        if (fl.length) c.append(el("div", "bk-note", `${fl.length} loose block${fl.length > 1 ? "s" : ""} (not in the file until plugged in)`));
    }

    // ---- palette: the catalog
    _palette() {
        const cats = [["logic", "Logic"], ["math", "Maths"], ["random", "Players / teams"], ["sensor", "Sensors"], ["action", "Actions"], ["object", "Objects"],
            ["timer", "Timers"], ["state", "State"], ["param", "Params"]];
        this.catsEl.textContent = "";
        for (const [k, label] of cats) {
            const b = el("button", "bk-cat c-" + (k === "param" ? "param" : k === "sensor" ? "name" : k === "state" ? "data" : k) + (this.cat === k ? " on" : ""), label);
            b.onclick = () => { this.cat = k; this._palette(); };
            this.catsEl.append(b);
        }
        const items = this.itemsEl;
        items.textContent = "";
        const add = (node, label) => {
            const blk = toBlocks(node);
            const e = blk.cat === "stmt" ? this._renderStmt(blk, true) : this._renderExpr(blk, true);
            e._src = { kind: "palette", node };
            this._draggable(e);   // (rendered as a preview, which skips _draggable: a palette block is dragged all the same)
            const w = el("div", "bk-pitem");
            if (label) w.title = label;
            w.append(e);
            items.append(w);
        };
        const S = this.S || symbols({ decls: [] });
        const H = (s) => items.append(el("div", "bk-ph", s));
        const C = CATALOG;
        if (this.cat === "logic") {
            add(newStmt("if"), "if"); add(newStmt("if", { else: true }), "if / else");
            ["and", "or"].forEach((o) => add(newExpr(o))); add(newExpr("not"));
            ["<", "<=", ">", ">=", "==", "!="].forEach((o) => add(newExpr(o)));
            add(newExpr("?:")); add({ op: "bool", v: true }); add({ op: "bool", v: false });
            add(newStmt("pass")); add(newStmt("break"), "leave the for loop");
            H("constants"); ["A", "B", "none"].forEach((n) => add({ op: "name", name: n }));
            ["warmup", "freeze", "live", "roundEnd", "matchEnd"].forEach((n) => add({ op: "name", name: n }, "phase"));
        } else if (this.cat === "math") {
            ["+", "-", "*", "/"].forEach((o) => add(newExpr(o))); add(newExpr("%"), "int remainder (x % 0 is 0)"); add(newExpr("neg"));
            add({ op: "num", type: "f32", v: 1 }); add({ op: "num", type: "f64", v: 1 }); add({ op: "num", type: "int", v: 1 });
            ["min", "max", "clamp", "abs", "sqrt", "int", "f32", "f64"].forEach((f) => add(newExpr("call", { fn: f }), C.functions[f] && C.functions[f].doc));
            if (S.curves.size) { H("curves"); for (const [n] of S.curves) add({ op: "call", fn: n, args: [{ op: "hole" }] }); }
        } else if (this.cat === "random") {
            H("loops: p walks the set's player slots 0..31 in order");
            add(newStmt("for")); add(newStmt("for", { where: true }), "for p in playing where <cond>");
            add(newStmt("for", { set: { set: "team", team: { op: "hole" } } }), "for p in team(t)");
            H("aggregates (one value from a set)");
            for (const f of AGG_FNS) add(newExpr("agg", { fn: f }), `${f}: ${C.aggregates[f] || "text"}`);
            add(newExpr("in"), "p in <set>: is the player in the set");
            H("players and teams");
            for (const f of ["team", "other", "alive", "isBot", "health", "name", "aliveCount", "teamCount"]) add(newExpr("call", { fn: f }), C.functions[f] && C.functions[f].type);
        } else if (this.cat === "sensor") {
            const sc = this.slotCat;
            if (sc && Object.keys(sc.args).length) { H("this slot's arguments"); for (const k in sc.args) add({ op: "name", name: k }, sc.args[k]); }
            if (sc && sc.out) { H(`returns ${sc.out[0]} (${sc.out[1]})`); add(newStmt("return"), "return " + sc.out[0]); }
            H("fields (rw ones can be set)");
            for (const [k, f] of Object.entries(C.fields)) if (!f.object) add({ op: "name", name: k }, `${f.type}${f.rw ? " rw" : ""}${f.doc ? ": " + f.doc : ""}`);
            H("player / team arrays");
            for (const [k, a] of Object.entries(C.arrays)) if (!a.object) add(newExpr("index", { array: k }), `${a.type}[${a.index}]${a.rw ? " rw" : ""}`);
            H("zones and spawns");
            add(newExpr("call", { fn: "inZone" })); ["hill", "sites"].forEach((n) => add({ op: "name", name: n })); ["site", "point", "checkpoint"].forEach((f) => add(newExpr("call", { fn: f })));
            add(newExpr("call", { fn: "mapSpawn" }));
            ["spawn.any", "spawn.a", "spawn.b", "spawn.attack", "spawn.defend", "spawn.race"].forEach((n) => add({ op: "name", name: n }));
        } else if (this.cat === "action") {
            add(newStmt("set", { name: "x" }), "a local, a state value or a rw field"); add(newStmt("set", { indexed: true }), "score[p] = ...");
            add(newStmt("return"), "return a value (query slots)"); add(newStmt("return", { n: 0 }), "return (event slots and procs)");
            H("actions");
            for (const [fn, a] of Object.entries(C.actions)) if (!a.object && !TIMER_ACTIONS.has(fn)) add(newStmt("do", { fn }), a.doc || "");
            add({ op: "str", v: "text" }, "text for announce / log / endRound / endMatch");
            if (S.procs.size) { H("procs (this file)"); for (const [n] of S.procs) add({ op: "do", fn: n, args: [] }); }
        } else if (this.cat === "object") {
            for (const [name, o] of Object.entries(C.objects)) {
                H(`${name}${S.uses.has(name) ? " (used)" : ""}: ${o.doc}`);
                for (const fn of o.steps) { const a = C.actions[fn]; add(a && a.value ? newExpr("call", { fn }) : newStmt("do", { fn }), a && a.doc); }
                for (const [k, f] of Object.entries(C.fields)) if (f.object === name) add({ op: "name", name: k }, f.type);
                for (const [k, a] of Object.entries(C.arrays)) if (a.object === name) add(newExpr("index", { array: k }), `${a.type}[${a.index}]`);
            }
            H("bomb states"); ["carried", "dropped", "planted", "defused", "exploded"].forEach((n) => add({ op: "name", name: n }));
        } else if (this.cat === "timer") {
            if (!S.timers.size) H("no timers yet: declare one in the Code view (timer <name>)");
            for (const [n] of S.timers) {
                H(n);
                add({ op: "do", fn: "startTimer", args: [{ op: "name", name: n }, { op: "hole" }] });
                add({ op: "do", fn: "stopTimer", args: [{ op: "name", name: n }] });
                add({ op: "call", fn: "timerLeft", args: [{ op: "name", name: n }] });
            }
            H("the clock"); add(newStmt("do", { fn: "setClock" })); add(newStmt("do", { fn: "addTime" }));
            add(newStmt("do", { fn: "touchObjective" })); add(newExpr("call", { fn: "holdOvertime" }), "true while overtime holds the round open");
            ["now", "phaseEnd", "liveSince", "hud.clock", "hud.overtime"].forEach((n) => add({ op: "name", name: n }));
        } else if (this.cat === "state") {
            if (!S.states.size) H("no state yet: declare it in the Code view (state match|round <name>[team|player] <type> = <v>)");
            for (const [n, d] of S.states) {
                if (d.per) { add(newExpr("index", { array: n }), `${d.life}: ${d.type} per ${d.per}`); add({ op: "set", name: n, index: { op: "hole" }, value: { op: "hole" } }); }
                else { add({ op: "name", name: n }, `${d.life}: ${d.type}`); add({ op: "set", name: n, value: { op: "hole" } }); }
            }
            H("the engine's scores"); add(newExpr("index", { array: "score" })); add(newExpr("index", { array: "teamScore" }));
            add(newStmt("do", { fn: "addScore" })); add(newStmt("do", { fn: "addTeamScore" }));
        } else if (this.cat === "param") {
            for (const [n, p] of S.params) add({ op: "name", name: n }, (p.trail || "").replace(/^#\s?/, ""));
        }
    }

    // ---- rendering
    _renderDecl(b, decl) {
        const w = el("div", "bk-hat");
        const head = el("div", "bk-hathead");
        if (decl.kind === "slot" || decl.kind === "proc") {
            const title = decl.kind === "proc" ? decl.name + "()" : decl.name === "timer" ? `timer ${decl.timer} ran out` : decl.name;
            head.append(el("span", "bk-kw", decl.kind === "proc" ? "proc" : "when"), el("span", "bk-title", title));
            const cat = decl.kind === "slot" ? CATALOG.slots[decl.name] : null;
            if (cat) {
                const meta = el("div", "bk-meta");
                meta.append(el("span", "muted", `${cat.kind === "query" ? "query, " : ""}called ${cat.when}`));
                const args = Object.entries(cat.args);
                if (args.length) { const s = el("div", "bk-chips"); s.append(el("span", "muted", "reads")); args.forEach(([k, t]) => s.append(el("span", "chip c-name", `${k}: ${t}`))); meta.append(s); }
                if (cat.out) { const s = el("div", "bk-chips"); s.append(el("span", "muted", "returns")); s.append(el("span", "chip c-set", `${cat.out[0]}: ${cat.out[1]}`)); meta.append(s); }
                head.append(meta);
            } else if (decl.kind === "proc") head.append(el("div", "bk-meta muted", "a statement block, called as " + decl.name + "(); inlined where it is called"));
            w.append(this._comments(b.fields), head, this._stack(b.stacks.body, b, "body"));
        } else if (decl.kind === "curve" && decl.form === "expr") {
            head.append(el("span", "bk-kw", "curve"), el("span", "bk-title", `${decl.name}(${decl.arg} ${decl.argType}) =`));
            w.append(this._comments(b.fields), head, this._slot(b, "body"));
        } else {
            head.append(el("span", "bk-title", printDeclHead(decl)));
            w.append(head, el("div", "muted pad", "Edit it in the Values view (or the Code view)."));
        }
        if (b.fields.trail) w.append(el("div", "bk-trail", b.fields.trail));
        return w;
    }

    _comments(f) {
        const c = (f.comments || []).filter((x) => x !== "");
        if (!c.length) return document.createDocumentFragment();
        const n = el("div", "bk-comment");
        n.textContent = c.join("\n");
        return n;
    }

    _stack(list) {
        const s = el("div", "bk-stack");
        s._drop = { kind: "stack", list };
        list.forEach((blk, i) => {
            const e = this._renderStmt(blk);
            e._src = { kind: "stack", list, index: i, block: blk };
            s.append(e);
        });
        if (!list.length) s.append(el("div", "bk-empty", "drop statements here"));
        return s;
    }

    _setRow(row, b, preview) {
        // `in everyone | playing | team(<t>)`
        const f = b.fields;
        row.append(el("span", "bk-kw", "in"));
        row.append(this._select(["everyone", "playing", "team"], f.set.set, (v) => {
            if (v === "team") { f.set = { set: "team", team: true }; b.inputs["set.team"] = toBlocks({ op: "hole" }); }
            else { f.set = { set: v }; delete b.inputs["set.team"]; }
            this._changed();
        }, preview));
        if (f.set.set === "team") { row.append(el("span", "bk-p", "(")); row.append(this._slot(b, "set.team", preview)); row.append(el("span", "bk-p", ")")); }
    }
    _optional(row, b, key, label, preview, required = false) {
        if (b.fields[key]) {
            row.append(el("span", "bk-kw", label), this._slot(b, key, preview));
            if (!preview && !required) { const x = el("button", "bk-mini", "×"); x.title = "remove " + label; x.onclick = (e) => { e.stopPropagation(); delete b.fields[key]; delete b.inputs[key]; this._changed(); }; row.append(x); }
        } else if (!preview) {
            const p = el("button", "bk-mini", "+ " + label);
            p.onclick = (e) => { e.stopPropagation(); b.fields[key] = true; b.inputs[key] = toBlocks({ op: "hole" }); this._changed(); };
            row.append(p);
        }
    }

    _renderStmt(b, preview = false) {
        const w = el("div", "bk-block bk-stmt c-" + CAT_OF(b));
        w._blk = b;
        if (!preview) w.append(this._comments(b.fields));
        const row = el("div", "bk-row");
        w.append(row);
        const kw = (t) => row.append(el("span", "bk-kw", t));
        switch (b.op) {
            case "set":
                kw("set");
                row.append(this._nameField(b, "name", b.fields.index ? "bk-dl-arr" : "bk-dl-set"));
                if (b.fields.index) { row.append(el("span", "bk-p", "[")); row.append(this._slot(b, "index", preview)); row.append(el("span", "bk-p", "]")); }
                row.append(el("span", "bk-kw", "="), this._slot(b, "value", preview));
                break;
            case "return": {
                kw("return");
                const n = b.fields.values;
                for (let i = 0; i < n; i++) { if (i) row.append(el("span", "bk-kw", ",")); row.append(this._slot(b, "values." + i, preview)); }
                if (!preview && n === 0) {
                    const plus = el("button", "bk-mini", "+"); plus.title = "return a value (query slots)";
                    plus.onclick = (e) => { e.stopPropagation(); b.inputs["values.0"] = toBlocks({ op: "hole" }); b.fields.values = 1; this._changed(); };
                    row.append(plus);
                } else if (!preview) { const minus = el("button", "bk-mini", "−"); minus.title = "no value"; minus.onclick = (e) => { e.stopPropagation(); delete b.inputs["values." + (n - 1)]; b.fields.values = n - 1; this._changed(); }; row.append(minus); }
                break;
            }
            case "do": {
                const fn = b.fields.fn;
                row.append(el("span", "bk-fn", fn));
                row.append(el("span", "bk-p", "("));
                const n = b.fields.args;
                for (let i = 0; i < n; i++) { if (i) row.append(el("span", "bk-p", ",")); row.append(this._slot(b, "args." + i, preview)); }
                row.append(el("span", "bk-p", ")"));
                const a = CATALOG.actions[fn];
                if (!preview && a && a.text) {
                    const plus = el("button", "bk-mini", "+ text"); plus.title = "one more text argument";
                    plus.onclick = (e) => { e.stopPropagation(); b.inputs["args." + n] = toBlocks({ op: "str", v: "" }); b.fields.args = n + 1; this._changed(); };
                    row.append(plus);
                    if (n > a.args.length) { const minus = el("button", "bk-mini", "−"); minus.onclick = (e) => { e.stopPropagation(); delete b.inputs["args." + (n - 1)]; b.fields.args = n - 1; this._changed(); }; row.append(minus); }
                }
                break;
            }
            case "for": {
                row.remove();
                const r = el("div", "bk-row");
                r.append(el("span", "bk-kw", "for each"));
                r.append(this._nameField(b, "var", null));
                this._setRow(r, b, preview);
                this._optional(r, b, "where", "where", preview);
                if (b.fields.trail && !preview) r.append(el("span", "bk-trail", b.fields.trail));
                w.append(r, preview ? el("div", "bk-cmouth") : this._stack(b.stacks.body));
                break;
            }
            case "if": {
                row.remove();
                const arms = b.fields.arms;
                arms.forEach((a, i) => {
                    if (i && !preview) w.append(this._comments(a));
                    const r = el("div", "bk-row");
                    r.append(el("span", "bk-kw", i ? "elif" : "if"), this._slot(b, `arms.${i}.cond`, preview));
                    if (!preview && arms.length > 1) { const x = el("button", "bk-mini", "×"); x.title = "remove this arm"; x.onclick = (e) => { e.stopPropagation(); this._removeArm(b, i); }; r.append(x); }
                    if (a.trail && !preview) r.append(el("span", "bk-trail", a.trail));
                    w.append(r);
                    w.append(preview ? el("div", "bk-cmouth") : this._stack(b.stacks[`arms.${i}.body`]));
                });
                if (b.fields.else) {
                    const r = el("div", "bk-row");
                    r.append(el("span", "bk-kw", "else"));
                    if (!preview) { const x = el("button", "bk-mini", "×"); x.title = "remove else"; x.onclick = (e) => { e.stopPropagation(); delete b.fields.else; delete b.stacks["else.body"]; this._changed(); }; r.append(x); }
                    if (b.fields.else.trail && !preview) r.append(el("span", "bk-trail", b.fields.else.trail));
                    w.append(r, preview ? el("div", "bk-cmouth") : this._stack(b.stacks["else.body"]));
                }
                if (!preview) {
                    const r = el("div", "bk-row bk-foot");
                    const ae = el("button", "bk-mini", "+ elif");
                    ae.onclick = (e) => { e.stopPropagation(); const i = b.fields.arms.length; b.fields.arms.push({}); b.inputs[`arms.${i}.cond`] = toBlocks({ op: "hole" }); b.stacks[`arms.${i}.body`] = [toBlocks({ op: "pass" })]; this._changed(); };
                    r.append(ae);
                    if (!b.fields.else) { const el2 = el("button", "bk-mini", "+ else"); el2.onclick = (e) => { e.stopPropagation(); b.fields.else = {}; b.stacks["else.body"] = [toBlocks({ op: "pass" })]; this._changed(); }; r.append(el2); }
                    w.append(r);
                }
                break;
            }
            default: kw(b.op);
        }
        if (b.fields.trail && !preview && b.op !== "for" && b.op !== "if") row.append(el("span", "bk-trail", b.fields.trail));
        if (!preview) this._draggable(w);
        return w;
    }

    _removeArm(b, i) {
        const n = b.fields.arms.length;
        for (let k = i; k < n - 1; k++) { b.inputs[`arms.${k}.cond`] = b.inputs[`arms.${k + 1}.cond`]; b.stacks[`arms.${k}.body`] = b.stacks[`arms.${k + 1}.body`]; }
        delete b.inputs[`arms.${n - 1}.cond`]; delete b.stacks[`arms.${n - 1}.body`];
        b.fields.arms.splice(i, 1);
        this._changed();
    }

    _slot(owner, key, preview = false) {
        const s = el("span", "bk-slot");
        const child = owner.inputs[key];
        if (child && child.op !== "hole") {
            const e = this._renderExpr(child, preview);
            e._src = { kind: "input", owner, key, block: child };
            s.append(e);
        } else s.classList.add("empty");
        if (!preview) s._drop = { kind: "input", owner, key };
        return s;
    }

    _renderExpr(b, preview = false) {
        const w = el("span", "bk-block bk-expr c-" + CAT_OF(b));
        w._blk = b;
        const f = b.fields;
        const t = (x, cls = "bk-kw") => w.append(el("span", cls, x));
        switch (b.op) {
            case "num": {
                const inp = el("input", "bk-num");
                inp.value = exprNumText(f.v, f.type).replace(/f$/, "");
                inp.style.width = `calc(${Math.max(2, inp.value.length) + 1}ch + 12px)`;
                inp.oninput = () => { inp.style.width = `calc(${Math.max(2, inp.value.length) + 1}ch + 12px)`; };
                inp.disabled = preview;
                inp.onchange = () => { const v = Number(inp.value); if (Number.isFinite(v)) { f.v = roundTo(v, f.type); this._changed(); } else inp.value = String(f.v); };
                w.append(inp);
                const ty = el("button", "bk-type", f.type);
                ty.title = "type: click to change (f32 float, f64 double, int)";
                ty.onclick = (e) => { e.stopPropagation(); if (preview) return; f.type = f.type === "f32" ? "f64" : f.type === "f64" ? "int" : "f32"; if (f.type === "int") f.v = Math.trunc(f.v); this._changed(); };
                w.append(ty);
                break;
            }
            case "bool": {
                const bt = el("button", "bk-toggle", f.v ? "yes" : "no");
                bt.onclick = (e) => { e.stopPropagation(); if (preview) return; f.v = !f.v; this._changed(); };
                w.append(bt);
                break;
            }
            case "str": {
                const inp = el("input", "bk-namein bk-str");
                inp.value = f.v;
                const fit = () => { inp.style.width = `calc(${Math.max(3, inp.value.length) + 1}ch + 16px)`; };
                fit();
                inp.disabled = preview;
                inp.oninput = fit;
                inp.onchange = () => { if (/["\\]/.test(inp.value) || new TextEncoder().encode(inp.value).length > 120) { inp.value = f.v; this.app.app && this.app.app.toast("text: no \" or \\, at most 120 bytes"); return; } f.v = inp.value; this._changed(); };
                w.append(el("span", "bk-p", "“"), inp, el("span", "bk-p", "”"));
                break;
            }
            case "name": {
                w.classList.add(this._nameClass(f.name));
                if (preview) t(f.name, "bk-name"); else w.append(this._nameField(b, "name", "bk-dl-read"));
                break;
            }
            case "call": {
                t(f.fn, "bk-fn");
                t("(", "bk-p");
                for (let i = 0; i < f.args; i++) { if (i) t(",", "bk-p"); w.append(this._slot(b, "args." + i, preview)); }
                t(")", "bk-p");
                break;
            }
            case "agg": {
                w.append(this._select(AGG_FNS, f.fn, (v) => {
                    f.fn = v;
                    if (v === "best" && !f.by) { f.by = true; b.inputs.by = toBlocks({ op: "hole" }); }
                    if (v !== "best" && f.by) { delete f.by; delete b.inputs.by; }
                    this._changed();
                }, preview, "bk-op"));
                t("(", "bk-p");
                if (preview) t(f.var, "bk-name"); else w.append(this._nameField(b, "var", null));
                const holder = el("span", "bk-row");
                holder.style.display = "inline-flex";
                this._setRow(holder, b, preview);
                this._optional(holder, b, "where", "where", preview);
                if (f.fn === "best") this._optional(holder, b, "by", "by", preview, true);
                w.append(holder);
                t(")", "bk-p");
                break;
            }
            case "index": {
                if (preview) t(f.array, "bk-fn"); else w.append(this._nameField(b, "array", "bk-dl-arr"));
                t("[", "bk-p"); w.append(this._slot(b, "index", preview)); t("]", "bk-p");
                break;
            }
            case "in": {
                w.append(this._slot(b, "a", preview));
                const holder = el("span", "bk-row");
                holder.style.display = "inline-flex";
                this._setRow(holder, b, preview);
                w.append(holder);
                break;
            }
            case "neg": t("−"); w.append(this._slot(b, "a", preview)); break;
            case "not": t("not"); w.append(this._slot(b, "a", preview)); break;
            case "?:": w.append(this._slot(b, "c", preview)); t("?"); w.append(this._slot(b, "a", preview)); t(":"); w.append(this._slot(b, "b", preview)); break;
            case "hole": t("_", "muted"); break;
            default: {
                w.append(this._slot(b, "a", preview));
                w.append(this._select(BIN_OPS, b.op, (v) => { b.op = v; this._changed(); }, preview, "bk-op"));
                w.append(this._slot(b, "b", preview));
            }
        }
        if (!preview) this._draggable(w);
        return w;
    }

    _nameClass(n) {
        const S = this.S;
        if (S && S.params.has(n)) return "ref-param";
        if (S && (S.states.has(n) || S.timers.has(n))) return "ref-list";
        if (CATALOG.constants[n] !== undefined || WEAPONS.includes(n)) return "ref-weapon";
        return "ref-sensor";
    }
    _readable() {
        const out = [];
        if (this.slotCat) out.push(...Object.keys(this.slotCat.args));
        out.push(...Object.keys(CATALOG.fields));
        if (this.S) out.push(...this.S.params.keys(), ...[...this.S.states.values()].filter((d) => !d.per).map((d) => d.name), ...this.S.timers.keys());
        out.push(...Object.keys(CATALOG.constants), ...WEAPONS);
        const d = this.brain && this.brain.decls[this.declIx];
        if (d && d.body) this._locals(d.body, out);
        return out;
    }
    _locals(body, out) {
        const walk = (b) => b.forEach((s) => { if (s.op === "set" && !s.index) out.push(s.name); if (s.op === "for") { out.push(s.var); walk(s.body); } if (s.op === "if") { s.arms.forEach((a) => walk(a.body)); if (s.else) walk(s.else.body); } });
        walk(body);
    }
    _settable() {
        const out = Object.entries(CATALOG.fields).filter(([, f]) => f.rw).map(([k]) => k);
        if (this.S) out.push(...[...this.S.states.values()].filter((d) => !d.per).map((d) => d.name));
        const d = this.brain && this.brain.decls[this.declIx];
        if (d && d.body) { const loc = []; this._locals(d.body, loc); out.push(...loc); }
        return out;
    }
    _arrays() {
        const out = Object.keys(CATALOG.arrays);
        if (this.S) out.push(...[...this.S.states.values()].filter((d) => d.per).map((d) => d.name));
        return out;
    }

    _nameField(b, key, listId) {
        const inp = el("input", "bk-namein");
        inp.value = b.fields[key];
        const fit = () => { inp.style.width = `calc(${Math.max(3, inp.value.length) + 1}ch + 12px)`; };
        fit();
        if (listId) inp.setAttribute("list", listId);
        inp.onchange = () => { const v = inp.value.trim(); if (/^[A-Za-z_][A-Za-z0-9_.]*$/.test(v)) { b.fields[key] = v; this._changed(); } else inp.value = b.fields[key]; };
        inp.oninput = fit;
        return inp;
    }
    _datalists() {
        const mk = (id, names) => {
            let dl = document.getElementById(id);
            if (!dl) { dl = el("datalist"); dl.id = id; document.body.append(dl); }
            const want = [...new Set(names)].join("\n");
            if (dl._names === want) return;
            dl._names = want; dl.textContent = "";
            for (const o of new Set(names)) { const op = el("option"); op.value = o; dl.append(op); }
        };
        mk("bk-dl-read", this._readable());
        mk("bk-dl-set", this._settable());
        mk("bk-dl-arr", this._arrays());
    }

    _select(options, value, onChange, disabled, cls = "bk-sel") {
        const s = el("select", cls);
        for (const o of options) { const op = el("option", null, o); op.value = o; if (o === value) op.selected = true; s.append(op); }
        if (!options.includes(value)) { const op = el("option", null, value); op.selected = true; s.prepend(op); }
        s.disabled = !!disabled;
        s.onchange = () => onChange(s.value);
        return s;
    }

    // ---- drag and drop (pointer events; unchanged from the bot editor)
    _draggable(w) {
        w.addEventListener("pointerdown", (e) => {
            if (e.button !== 0) return;
            if (e.target.closest("input,select,button")) return;
            e.stopPropagation();
            this._drag = { el: w, startX: e.clientX, startY: e.clientY, active: false, src: () => w._src };
        });
    }
    _dragMove(e) {
        const d = this._drag;
        if (!d) return;
        if (!d.active) {
            if (Math.abs(e.clientX - d.startX) + Math.abs(e.clientY - d.startY) < 5) return;
            d.active = true;
            const src = d.src();
            if (!src) { this._drag = null; return; }
            d.from = src;
            d.block = src.kind === "palette" ? toBlocks(JSON.parse(JSON.stringify(src.node))) : src.block || (src.item && src.item.block);
            const r = d.el.getBoundingClientRect();
            d.dx = e.clientX - r.left; d.dy = e.clientY - r.top;
            d.ghost = d.el.cloneNode(true);
            d.ghost.classList.add("bk-ghost");
            d.ghost.style.width = r.width + "px";
            document.body.append(d.ghost);
            if (src.kind !== "palette") d.el.classList.add("bk-dragging");
            this.root.classList.add("dragging");
        }
        d.ghost.style.left = e.clientX - d.dx + "px";
        d.ghost.style.top = e.clientY - d.dy + "px";
        this._clearHints();
        const t = this._target(e, d);
        d.target = t;
        if (t && t.kind === "stack") {
            const line = el("div", "bk-insert");
            const kids = [...t.el.children].filter((c) => c._blk);
            if (t.index < kids.length) t.el.insertBefore(line, kids[t.index]); else t.el.append(line);
            this._hint = line;
        } else if (t && t.kind === "input") { t.el.classList.add("bk-hot"); this._hintEl = t.el; }
        else if (t && t.kind === "trash") { this.paletteEl.classList.add("bk-trash"); }
    }
    _clearHints() {
        if (this._hint) { this._hint.remove(); this._hint = null; }
        if (this._hintEl) { this._hintEl.classList.remove("bk-hot"); this._hintEl = null; }
        this.paletteEl.classList.remove("bk-trash");
    }
    _target(e, d) {
        const under = document.elementFromPoint(e.clientX, e.clientY);
        if (!under) return null;
        if (this.paletteEl.contains(under)) return d.from.kind === "palette" ? null : { kind: "trash" };
        if (!this.ws.contains(under)) return null;
        let n = under;
        const isStmt = d.block.cat === "stmt";
        while (n && n !== this.ws) {
            if (n === d.el || (d.el.contains(n) && d.from.kind !== "palette")) return null;
            if (n._drop) {
                if (n._drop.kind === "stack" && isStmt) {
                    const kids = [...n.children].filter((c) => c._blk);
                    let index = kids.length;
                    for (let i = 0; i < kids.length; i++) { const r = kids[i].getBoundingClientRect(); if (e.clientY < r.top + r.height / 2) { index = i; break; } }
                    return { kind: "stack", el: n, list: n._drop.list, index };
                }
                if (n._drop.kind === "input" && !isStmt) return { kind: "input", el: n, owner: n._drop.owner, key: n._drop.key };
            }
            n = n.parentElement;
        }
        const r = this.canvas.getBoundingClientRect();
        return { kind: "float", x: e.clientX - r.left - d.dx, y: e.clientY - r.top - d.dy };
    }
    _dragEnd() {
        const d = this._drag;
        this._drag = null;
        if (!d || !d.active) return;
        d.ghost.remove();
        d.el.classList.remove("bk-dragging");
        this.root.classList.remove("dragging");
        this._clearHints();
        const t = d.target;
        if (!t) { this.render(); return; }
        const key = declKey(this.brain.decls[this.declIx]);
        if (!this.floating.has(key)) this.floating.set(key, []);
        const fl = this.floating.get(key);
        const src = d.from;
        let fixIndex = (list, i) => i;
        if (src.kind === "stack") {
            src.list.splice(src.index, 1);
            if (src.list.length === 0 && src.list !== this.model.decls[this.declIx].stacks.body) src.list.push(toBlocks({ op: "pass" }));
            fixIndex = (list, i) => (list === src.list && src.index < i ? i - 1 : i);
        } else if (src.kind === "input") src.owner.inputs[src.key] = toBlocks({ op: "hole" });
        else if (src.kind === "float") fl.splice(fl.indexOf(src.item), 1);
        if (t.kind === "stack") {
            let i = fixIndex(t.list, t.index);
            if (t.list.length === 1 && t.list[0].op === "pass" && d.block.op !== "pass") { t.list.splice(0, 1); i = 0; }
            t.list.splice(i, 0, d.block);
        } else if (t.kind === "input") {
            const old = t.owner.inputs[t.key];
            if (old && old.op !== "hole") fl.push({ block: old, x: 20, y: 20 + fl.length * 34 });
            t.owner.inputs[t.key] = d.block;
        } else if (t.kind === "float") fl.push({ block: d.block, x: Math.max(0, t.x), y: Math.max(0, t.y) });
        this._changed();
    }
}
