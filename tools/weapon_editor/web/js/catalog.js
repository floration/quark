// The weapon catalog: every field a weapon set can write (DESIGN.md 1.1), the 11 weapons, the logic slots' sensors
// and outputs, the builtins. The game's own catalog (`wpnparams` / `quark_tests exportweapons`, DESIGN.md section 3)
// goes through normalizeCatalog(); the built-in one (the MOCK's, and the page's when neither answers) is the same
// export, generated into catalog_data.js by build_catalog.py, so its numbers can't drift from src/game/gamedefs.h.
// What this file adds is presentation only: the field groups and the weapons' titles.

import DATA, { GAME_VERSION } from "./catalog_data.js";

export const WEAPONS = ["rifle", "rocket", "knife", "sniper", "laser", "shotgun", "sticky", "crossbow", "bubble", "syringe", "locator"];
export const SLOT_ORDER = ["rocket", "rifle", "laser", "shotgun", "sniper", "sticky", "crossbow", "bubble", "knife", "syringe", "locator"];   // kWeaponSlots

const TITLE = { rifle: "Rifle", rocket: "Rocket launcher", knife: "Knife", sniper: "Sniper", laser: "Laser", shotgun: "Shotgun", sticky: "Sticky launcher",
    crossbow: "Crossbow", bubble: "Bubble gun", syringe: "Syringe", locator: "Locator" };
// the Weapon tab's card of each field (an alt fire field: "alt"; one the catalog doesn't name: "damage")
const FIELD_GROUP = { refire: "fire", range: "fire", semiAuto: "fire", projectile: "fire", beam: "fire", sticky: "fire",
    damage: "damage", pellets: "damage", headDamage: "damage", directDamage: "damage", selfDamage: "damage", wallbang: "damage",
    projSpeed: "projectile", splashRadius: "projectile", gravity: "projectile", life: "projectile",
    spread: "spread", scopedSpread: "spread", zoomFov: "spread", knock: "knockback", falloffStart: "falloff", falloffEnd: "falloff", falloffMin: "falloff" };

export const GROUPS = [["damage", "Damage"], ["fire", "Fire"], ["projectile", "Projectile"], ["spread", "Spread / scope"], ["alt", "Alt fire"], ["knockback", "Knockback"], ["falloff", "Falloff"]];
export const BUILTIN_SOURCE = `built-in (quark_tests exportweapons, ${GAME_VERSION})`;

// the built-in catalog in the normalized shape (see normalizeCatalog)
export function builtinCatalog() { return build(DATA, false, BUILTIN_SOURCE); }

export const BUILTINS = [
    { name: "min", sig: "min(a, b)", doc: "the smaller" }, { name: "max", sig: "max(a, b)", doc: "the larger" },
    { name: "abs", sig: "abs(x)", doc: "absolute value" }, { name: "clamp", sig: "clamp(x, lo, hi)", doc: "x kept within lo..hi" },
    { name: "sqrt", sig: "sqrt(x)", doc: "square root" }, { name: "floor", sig: "floor(x)", doc: "round down" }, { name: "ceil", sig: "ceil(x)", doc: "round up" },
    { name: "pow", sig: "pow(x, y)", doc: "x to the power y" }, { name: "lerp", sig: "lerp(a, b, t)", doc: "a + (b - a) * t" },
    { name: "f32", sig: "f32(x)", doc: "as a float" }, { name: "f64", sig: "f64(x)", doc: "as a double" }, { name: "int", sig: "int(x)", doc: "as an int (toward 0)" },
];
const BUILTIN_SHAPE = { min: [2, "common"], max: [2, "common"], abs: [1, "arg"], clamp: [3, "common"], sqrt: [1, "arg"], floor: [1, "arg"], ceil: [1, "arg"],
    pow: [2, "common"], lerp: [3, "common"], f32: [1, "f32"], f64: [1, "f64"], int: [1, "int"] };

// any catalog (the game's, or the built-in) -> {fields, byKey, weapons, byId, slots, slotByName, builtins, builtinShape, defaultText, ...}
export function normalizeCatalog(c) {
    if (!c || !Array.isArray(c.fields) || !c.fields.length) return builtinCatalog();
    return build(c, true, c.source || "the game");
}

function build(c, fromGame, source) {
    const fields = c.fields.map((f) => {
        const key = f.key || f.weapon + "." + f.field;
        const field = f.field || key.split(".").slice(1).join(".");
        return { ...f, key, field, weapon: f.weapon || key.split(".")[0], group: f.group || (f.alt ? "alt" : FIELD_GROUP[field] || "damage"), unit: f.unit ?? "", doc: f.doc || "" };
    });
    const def = (k) => { const f = fields.find((x) => x.key === k); return f ? f.default : undefined; };
    // the game's weapons: {id: <number>, name: "rifle", title: "RIFLE", kind, altFire, slot}; the page keys them by name
    const weapons = (Array.isArray(c.weapons) && c.weapons.length ? c.weapons : WEAPONS.map((id, i) => ({ id: i, name: id }))).map((w, i) => {
        const id = typeof w.id === "string" ? w.id : String(w.name || "").toLowerCase();
        return { ...w, id, index: typeof w.id === "number" ? w.id : w.index ?? i, name: id.toUpperCase(), title: TITLE[id] || w.title || id, gameTitle: w.title || "",
            projectile: !!def(id + ".projectile"), beam: !!def(id + ".beam"), sticky: !!def(id + ".sticky") };
    });
    const slots = (c.slots || []).map((s) => ({ ...s, when: s.when || s.doc || "", sensors: (s.sensors || []).map((x) => (typeof x === "string" ? { name: x, type: "f32" } : x)), outputs: s.outputs || [] }));
    const builtins = Array.isArray(c.builtins) && c.builtins.length ? c.builtins.map((b) => (typeof b === "string" ? { name: b, sig: b + "()" } : b)) : BUILTINS;
    return finish({ fields, weapons, slots, builtins, defaultText: c.defaultText, fromGame, source });
}

function finish(cat) {
    cat.byKey = new Map(cat.fields.map((f) => [f.key, f]));
    cat.byId = new Map(cat.weapons.map((w) => [w.id, w]));
    cat.slotByName = new Map(cat.slots.map((s) => [s.name, s]));
    cat.builtinShape = {};
    for (const b of cat.builtins) {
        const m = /^(\w+)\(([^)]*)\)\s*(?:->\s*(\w+))?/.exec(b.sig || b.name + "()");
        const known = BUILTIN_SHAPE[b.name];
        const args = known ? known[0] : m && m[2].trim() ? m[2].split(",").length : 0;
        cat.builtinShape[b.name] = { args, type: known ? known[1] : (m && m[3]) || "arg", doc: b.doc || "" };
    }
    cat.defaults = new Map(cat.fields.map((f) => [f.key, f.default]));
    cat.fieldsOf = (w) => cat.fields.filter((f) => f.weapon === w);
    if (!cat.defaultText) cat.defaultText = standardText(cat);
    return cat;
}

// the standard set as .qweapons text, every field a set can write (a catalog without its own defaultText)
export function standardText(cat) {
    const out = ["version 1", 'weapons "standard"'];
    for (const f of cat.fields) if (!f.readOnly) out.push(`param ${f.key} ${f.type} = ${fmtValue(f.default, f.type)}`);
    return out.join("\n") + "\n";
}

// a value as data text of its type (the shortest decimal that reads back as the same f32)
export function fmtValue(v, type) {
    if (type === "bool") return v ? "yes" : "no";
    if (type === "int") return String(Math.trunc(+v));
    const t = Math.fround(+v);
    if (!Number.isFinite(t)) return String(t);
    for (let p = 1; p <= 9; p++) { const s = +t.toPrecision(p); if (Math.fround(s) === t) return String(s); }
    return String(t);
}

// clamp the way CustomRules::fromText does: out of range -> the nearest bound, nan -> the default
export function clampField(f, v) {
    if (f.type === "bool") return !!v;
    let x = +v;
    if (!Number.isFinite(x)) return f.default;
    if (f.type === "int") x = Math.trunc(x);
    if (typeof f.min === "number" && x < f.min) x = f.min;
    if (typeof f.max === "number" && x > f.max) x = f.max;
    return f.type === "f32" ? +fmtValue(x, "f32") : x;
}
