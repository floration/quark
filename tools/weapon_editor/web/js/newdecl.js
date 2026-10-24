// Starting logic from Blocks and Graph (the Code view needs no help): a new slot (hit / kill, for every weapon or one),
// a curve (a weapon's falloff, or one of the set's own) or a param (the set's own constant), added to the AST where the
// printer keeps the file tidy (params with the params, curves before the slots, slots last). The "+ Add" popover and
// the empty-set hint both views show are built here, so the two views offer the same things the same way.

import { catalog, symbols, slotKey, declKey } from "./ast.js";
import { WEAPONS } from "./catalog.js";

const KEYWORDS = new Set(["version", "weapons", "label", "param", "curve", "slot", "if", "elif", "else", "return", "pass", "and", "or", "not",
    "yes", "no", "true", "false", "bool", "int", "f32", "f64", "weapon", "hit", "kill"]);
const NAME_RE = /^[A-Za-z_][A-Za-z0-9_]{0,31}$/;

// a name the set can use for its own param / curve: '' when fine, else why not
export function badOwnName(set, name) {
    if (!NAME_RE.test(name)) return "a name is letters, digits and _ (not starting with a digit), 32 at most";
    if (KEYWORDS.has(name) || WEAPONS.includes(name)) return `'${name}' is a keyword or a weapon`;
    const cat = catalog();
    if (cat && cat.builtinShape[name]) return `'${name}' is a builtin`;
    if (cat && cat.slots.some((s) => s.sensors.some((x) => x.name === name) || s.outputs.some((x) => x.name === name))) return `'${name}' is a sensor or an output`;
    const S = symbols(set);
    if (S.params.has(name) || S.curves.has(name)) return `'${name}' is already in the set`;
    return "";
}

// spec: {kind: "slot", name: "hit"|"kill", weapon?} | {kind: "curve", name, form: "points"|"expr"} |
// {kind: "param", name, type, value}  ->  {set, key} or {error}
export function addDecl(set, spec) {
    let d;
    const S = symbols(set);
    if (spec.kind === "slot") {
        d = { kind: "slot", name: spec.name, ...(spec.weapon ? { weapon: spec.weapon } : {}), body: [{ op: "pass" }] };
        if (S.slots.has(slotKey(d))) return { error: `slot ${slotKey(d)} is already in the set`, key: declKey(d) };
    } else if (spec.kind === "curve") {
        const m = /^(\w+)\.falloff$/.exec(spec.name);
        if (m) {
            if (!WEAPONS.includes(m[1])) return { error: `'${m[1]}' isn't a weapon` };
            if (S.curves.has(spec.name)) return { error: `${spec.name} is already in the set`, key: "curve:" + spec.name };
            // a weapon's falloff is data (points): full damage to 20 m, half at 40 m, as a start to drag
            d = { kind: "curve", name: spec.name, arg: "dist", argType: "f32", form: "points", points: [[0, 1], [20, 1], [40, 0.5]] };
        } else {
            const why = badOwnName(set, spec.name);
            if (why) return { error: why };
            d = spec.form === "expr" ? { kind: "curve", name: spec.name, arg: "x", argType: "f32", form: "expr", body: { op: "name", name: "x" } }
                : { kind: "curve", name: spec.name, arg: "x", argType: "f32", form: "points", points: [[0, 0], [10, 1]] };
        }
    } else if (spec.kind === "param") {
        const why = badOwnName(set, spec.name);
        if (why) return { error: why };
        const type = ["f32", "f64", "int", "bool"].includes(spec.type) ? spec.type : "f32";
        let value = type === "bool" ? !!spec.value : Number(spec.value);
        if (type !== "bool" && !Number.isFinite(value)) return { error: "the value is a number" };
        if (type === "int") value = Math.trunc(value);
        d = { kind: "param", name: spec.name, type, value };
    } else return { error: "nothing to add" };
    const decls = set.decls.slice();
    const lastOf = (f) => { let at = -1; decls.forEach((x, i) => { if (f(x)) at = i; }); return at; };
    let at;
    if (d.kind === "slot") at = decls.length;
    else if (d.kind === "curve") { const s0 = decls.findIndex((x) => x.kind === "slot"); at = s0 < 0 ? decls.length : s0; }
    else at = Math.max(lastOf((x) => x.kind === "param"), lastOf((x) => x.kind === "label")) + 1;
    decls.splice(at, 0, d);
    return { set: { ...set, decls }, key: declKey(d) };
}

const h = (tag, attrs = {}, ...kids) => {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) if (k === "class") e.className = v; else if (k.startsWith("on")) e[k] = v; else e.setAttribute(k, v);
    for (const c of kids) e.append(c);
    return e;
};

// the "+ Add" button and its popover: three rows (slot, curve, param). opts: {app, curveForms: ["points", "expr"],
// onAdded(key, decl kind)}. Adding goes through app.brainEdited (the one AST), so every view and the text follow.
export function addButton(opts) {
    const wrap = h("span", { class: "nd-wrap" });
    const btn = h("button", { class: "nd-btn", title: "start logic: a slot (runs on each hit / kill), a curve or a param" }, "+ Add");
    wrap.append(btn);
    let pop = null;
    const close = () => { if (pop) { pop.remove(); pop = null; document.removeEventListener("pointerdown", outside, true); } };
    const outside = (e) => { if (pop && !wrap.contains(e.target)) close(); };
    btn.onclick = () => {
        if (pop) return close();
        pop = addForm({ ...opts, onAdded: (k, kind) => { close(); opts.onAdded && opts.onAdded(k, kind); } });
        pop.classList.add("nd-pop");
        wrap.append(pop);
        document.addEventListener("pointerdown", outside, true);
        const first = pop.querySelector("select,input"); if (first) first.focus();
    };
    return wrap;
}

export function addForm(opts) {
    const app = opts.app, forms = opts.curveForms || ["points", "expr"];
    const sel = (options, value) => { const s = h("select"); for (const [v, t] of options) { const o = h("option", { value: v }, t); s.append(o); } s.value = value; return s; };
    const weaponOpts = [["", "every weapon"], ...WEAPONS.map((w) => [w, app.titles ? app.titles.get(w) || w : w])];
    const add = (spec) => {
        if (!app.parseOk) { app.toast("the code has an error: fix it on the Code tab first", 3500); return; }
        const r = addDecl(app.ast, spec);
        if (r.error) { app.toast(r.error, 3500); if (r.key) opts.onAdded && opts.onAdded(r.key, spec.kind, true); return; }
        app.brainEdited(r.set, "add");
        opts.onAdded && opts.onAdded(r.key, spec.kind);
    };
    const box = h("div", { class: "nd-form" });
    // slot
    const sName = sel([["hit", "hit"], ["kill", "kill"]], "hit"), sW = sel(weaponOpts, "");
    const sDoc = h("div", { class: "nd-doc" });
    const slotDoc = () => { const c = catalog() && catalog().slotByName.get(sName.value); sDoc.textContent = (c ? "runs " + (c.when || "") : "") + (sW.value ? ` · for the ${sW.value} only, instead of the general ${sName.value} slot` : ""); };
    sName.onchange = sW.onchange = slotDoc; slotDoc();
    box.append(h("div", { class: "nd-row" }, h("b", {}, "slot"), sName, sW, h("button", { class: "primary", onclick: () => add({ kind: "slot", name: sName.value, weapon: sW.value || undefined }) }, "Add slot")), sDoc);
    // curve
    const cKind = sel([["falloff", "a weapon's falloff"], ["own", "the set's own"]], "falloff");
    const cW = sel(WEAPONS.map((w) => [w, app.titles ? app.titles.get(w) || w : w]), app.sel || "rifle");
    const cName = h("input", { placeholder: "name", size: "10" });
    const cForm = sel(forms.map((f) => [f, f === "expr" ? "expression of x" : "points (drag)"]), forms[0]);
    const cDoc = h("div", { class: "nd-doc" });
    const curveUi = () => {
        const own = cKind.value === "own";
        cW.style.display = own ? "none" : ""; cName.style.display = cForm.style.display = own ? "" : "none";
        cDoc.textContent = own ? "a function slots can call: name(x)" : `replaces ${cW.value}'s falloff fields: damage x the curve at the hit's distance` + (forms.includes("points") ? "" : " (points: edited in Blocks and on the Weapon tab)");
    };
    cKind.onchange = cW.onchange = curveUi; curveUi();
    box.append(h("div", { class: "nd-row" }, h("b", {}, "curve"), cKind, cW, cName, cForm,
        h("button", { class: "primary", onclick: () => add(cKind.value === "own" ? { kind: "curve", name: cName.value.trim(), form: cForm.value } : { kind: "curve", name: cW.value + ".falloff" }) }, "Add curve")), cDoc);
    // param
    const pName = h("input", { placeholder: "name", size: "10" }), pType = sel([["f32", "f32"], ["f64", "f64"], ["int", "int"], ["bool", "bool"]], "f32");
    const pVal = h("input", { value: "1", size: "6" });
    box.append(h("div", { class: "nd-row" }, h("b", {}, "param"), pName, pType, pVal,
        h("button", { class: "primary", onclick: () => add({ kind: "param", name: pName.value.trim(), type: pType.value, value: pType.value === "bool" ? /^(yes|true|1)$/i.test(pVal.value.trim()) : pVal.value }) }, "Add param")),
        h("div", { class: "nd-doc" }, "a constant of the set's own that slots and curves read by name (a weapon's fields: the Weapon tab)"));
    return box;
}

// the empty state: what logic is and one-click starts (a general hit slot, one for the selected weapon, a kill slot,
// the selected weapon's falloff curve), then the full form
export function emptyHint(opts) {
    const app = opts.app, w = app.sel || "rifle", title = app.titles ? app.titles.get(w) || w : w;
    const box = h("div", { class: "nd-empty" });
    box.append(h("h3", {}, "No logic in this set yet"),
        h("p", { class: "muted" }, "A slot runs on every hit or kill and can change the damage, the knockback, heal the shooter or refund the cooldown. "
            + "A curve shapes damage by distance. Weapon numbers (damage, refire...) are fields: the Weapon tab."));
    const quick = h("div", { class: "nd-row" });
    const go = (spec) => () => {
        if (!app.parseOk) { app.toast("the code has an error: fix it on the Code tab first", 3500); return; }
        const r = addDecl(app.ast, spec);
        if (r.error) { app.toast(r.error, 3500); return; }
        app.brainEdited(r.set, "add");
        opts.onAdded && opts.onAdded(r.key, spec.kind);
    };
    quick.append(h("button", { class: "primary", onclick: go({ kind: "slot", name: "hit" }) }, "+ slot hit (every weapon)"),
        h("button", { onclick: go({ kind: "slot", name: "hit", weapon: w }) }, `+ slot hit ${w}`),
        h("button", { onclick: go({ kind: "slot", name: "kill" }) }, "+ slot kill"),
        ...(opts.curveForms && !opts.curveForms.includes("points") ? [] : [h("button", { onclick: go({ kind: "curve", name: w + ".falloff" }) }, `+ ${title} falloff curve`)]));
    box.append(quick, h("div", { class: "muted nd-doc" }, "or anything else:"), addForm(opts));
    return box;
}
