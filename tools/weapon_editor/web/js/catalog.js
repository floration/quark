// The weapon catalog: every field a weapon set can write (DESIGN.md 1.1), the 11 weapons, the logic slots' sensors
// and outputs, the builtins. The game's own catalog (`wpnparams` / `quark_tests exportweapons`, DESIGN.md section 3)
// replaces the built-in one through normalizeCatalog(); the built-in one is the standard table of
// src/game/gamedefs.h (baseWeaponDef and the alt-fire constants) as of 1.18, for the MOCK.

export const WEAPONS = ["rifle", "rocket", "knife", "sniper", "laser", "shotgun", "sticky", "crossbow", "bubble", "syringe", "locator"];
export const SLOT_ORDER = ["rocket", "rifle", "laser", "shotgun", "sniper", "sticky", "crossbow", "bubble", "knife", "syringe", "locator"];   // kWeaponSlots

// the standard table (gamedefs.h baseWeaponDef): refire, damage, range, projectile, projSpeed, splashRadius, spread,
// scopedSpread, zoomFov, beam, pellets, gravity, sticky, life, semiAuto
const F = Math.fround;
const BASE = {
    rifle:    [F(0.75 * 2 / 9.9), 4.95, 300, false, 0, 0, 0, 0, 60, false, 1, 0, false, 0, false],
    rocket:   [1.2, 34, 0, true, 48, 3.6, 0, -1, 0, false, 1, 0, false, 5, false],
    knife:    [0.75, 30, 3.5, false, 0, 0, 0, -1, 0, false, 1, 0, false, 0, false],
    sniper:   [1.5, 40, 500, false, 0, 0, 0, 0, 45, false, 1, 0, false, 0, false],
    laser:    [0.001, 0.04, 24, false, 0, 0, 0, -1, 0, true, 1, 0, false, 0, false],
    shotgun:  [0.75, 1.7, 60, false, 0, 0, 0.085, -1, 0, false, 20, 0, false, 0, false],
    sticky:   [0.75, 29.75, 0, true, 22, 3.4, 0, -1, 0, false, 1, 15, true, 30, false],
    crossbow: [2.0, 50, 0, true, 76.5, 0, 0, 0, 70, false, 1, 6, false, 3, false],
    bubble:   [0.2, 10, 0, true, 15.6, 1.8, 0.04, -1, 0, false, 1, -1.5, false, 4, false],
    syringe:  [1.0, 10, 0, false, 0, 0, 0, -1, 0, false, 1, 0, false, 0, false],
    locator:  [5.0, 0, 0, false, 0, 0, 0, -1, 0, false, 1, 0, false, 0, false],
};
const KIND = { rifle: "hitscan", rocket: "projectile", knife: "melee", sniper: "hitscan", laser: "beam", shotgun: "hitscan", sticky: "projectile",
    crossbow: "projectile", bubble: "projectile", syringe: "support", locator: "support" };
const ALT = { rifle: "scope: zooms, 10% slower fire", rocket: "lob: slower, drops", knife: "throw: a projectile, then the knife is away", sniper: "scope: zooms, tighter",
    laser: "pull beam: pulls harder, less damage", shotgun: "bouncing pellets: ricochet off walls, less damage", sticky: "detonate your stickies",
    crossbow: "scope: zooms", bubble: "stream: 5-15 bubbles at once, long reload", syringe: "throw: heals a teammate, resets their cooldowns", locator: "" };
const TITLE = { rifle: "Rifle", rocket: "Rocket launcher", knife: "Knife", sniper: "Sniper", laser: "Laser", shotgun: "Shotgun", sticky: "Sticky launcher",
    crossbow: "Crossbow", bubble: "Bubble gun", syringe: "Syringe", locator: "Locator" };

// WeaponDef fields: [field, type, unit, min, max, group, doc]
const DEF_FIELDS = [
    ["refire", "f32", "s", 0.001, 10, "fire", "seconds between shots (the laser: between damage ticks)"],
    ["damage", "f32", "hp", 0, 500, "damage", "damage per hit / per pellet / at the centre of a blast"],
    ["range", "f32", "m", 0, 1000, "fire", "hitscan range (the knife: its reach)"],
    ["projSpeed", "f32", "m/s", 0, 400, "projectile", "projectile speed"],
    ["splashRadius", "f32", "m", 0, 20, "projectile", "blast radius (0: direct hits only); damage falls linearly to 0 at the edge"],
    ["spread", "f32", "rad", 0, 0.5, "spread", "hip-fire spread (a fixed pattern)"],
    ["scopedSpread", "f32", "rad", -1, 0.5, "spread", "spread while scoped (< 0: can't scope)"],
    ["zoomFov", "f32", "deg", 0, 120, "spread", "horizontal field of view while scoped"],
    ["pellets", "int", "", 1, 64, "damage", "hitscan rays per shot"],
    ["gravity", "f32", "m/s²", -50, 100, "projectile", "projectile drop (negative floats up)"],
    ["life", "f32", "s", 0, 60, "projectile", "projectile lifetime"],
    ["semiAuto", "bool", "", 0, 1, "fire", "one shot per click (a click up to 0.15 s early is kept)"],
];
const EXTRA_FIELDS = [
    ["headDamage", "f32", "hp", 0, 500, "damage", "damage of a hit on the head (0: no headshots)"],
    ["knock", "f32", "", 0, 100, "knockback", "knockback strength (shotgun: m/s per pellet's worth; crossbow: m/s; laser: m/s per s of beam; splash: m/s at the centre)"],
    ["falloffStart", "f32", "m", 0, 1000, "falloff", "full damage up to this distance"],
    ["falloffEnd", "f32", "m", 0, 1000, "falloff", "damage reaches falloffMin at this distance"],
    ["falloffMin", "f32", "x", 0, 1, "falloff", "damage multiplier at falloffEnd and beyond (1: no falloff)"],
    ["directDamage", "f32", "hp", 0, 500, "damage", "a projectile that hits the player itself does at least this (splash weapons)"],
    ["selfDamage", "f32", "x", 0, 2, "damage", "x your own splash damage (a blast below your feet does 0.1 of it, as always)"],
    ["wallbang", "f32", "x", 0, 1, "damage", "x damage through a wallbang wall (hitscan)"],
];
const HEAD = { sniper: 50, crossbow: 75, rifle: 7.5, shotgun: 2.55 };
const KNOCK = { shotgun: 0.3, crossbow: 16, laser: 20, rocket: 11, sticky: 11, bubble: 11, knife: 1, rifle: 1, sniper: 1, syringe: 1, locator: 1 };   // (as quark_tests exportweapons gives them)
// alt-fire fields: [weapon, field, type, unit, min, max, default, doc]
const ALT_FIELDS = [
    ["rifle", "scopedRefire", "f32", "x", 0.1, 10, F(1 / 0.9), "x refire while scoped"],
    ["rocket", "lobSpeed", "f32", "x", 0.05, 3, 0.7, "the lob's speed (x projSpeed)"],
    ["rocket", "lobGravity", "f32", "m/s²", 0, 100, 13.5, "the lob's drop"],
    ["knife", "throwSpeed", "f32", "m/s", 1, 200, 38, "a thrown knife's speed"],
    ["knife", "throwGravity", "f32", "m/s²", 0, 100, 14, "a thrown knife's drop"],
    ["knife", "throwLife", "f32", "s", 0.1, 30, 2, "a thrown knife's lifetime"],
    ["knife", "away", "f32", "s", 0, 60, 4, "after a throw the knife can't be used for this long"],
    ["knife", "grow", "f32", "m", 0, 1, 0.04, "a swing hits boxes this much wider on each side"],
    ["shotgun", "bounces", "int", "", 0, 10, 2, "times an alt-fire pellet ricochets"],
    ["shotgun", "bounceDamage", "f32", "x", 0, 2, 0.5, "x damage of a pellet that has bounced"],
    ["laser", "pullScale", "f32", "x", 0, 10, 1.82, "the pull beam pulls this much harder than the beam pushes"],
    ["laser", "pullDamage", "f32", "x", 0, 2, 0.75, "x damage of the pull beam"],
    ["bubble", "streamMin", "int", "", 1, 64, 5, "bubbles in a stream with the nearest enemy within streamNear"],
    ["bubble", "streamMax", "int", "", 1, 64, 15, "bubbles in a stream with the nearest enemy beyond streamFar"],
    ["bubble", "streamNear", "f32", "m", 0, 200, 5, "the stream's near distance"],
    ["bubble", "streamFar", "f32", "m", 0, 500, 30, "the stream's far distance"],
    ["bubble", "streamRefire", "f32", "s", 0.05, 20, 1.8, "the reload after a stream"],
    ["bubble", "streamSlow", "f32", "m/s", 0, 200, 8.4, "the slowest bubble of a stream"],
    ["bubble", "streamFast", "f32", "m/s", 0, 200, 24, "the fastest bubble of a stream"],
    ["syringe", "throwSpeed", "f32", "m/s", 1, 200, 30, "a thrown syringe's speed"],
    ["syringe", "throwGravity", "f32", "m/s²", 0, 100, 9, "a thrown syringe's drop"],
    ["syringe", "throwLife", "f32", "s", 0.1, 30, 2, "a thrown syringe's lifetime"],
    ["syringe", "throwHeal", "f32", "hp", 0, 200, 30, "health a thrown syringe gives the teammate it hits"],
    ["syringe", "away", "f32", "s", 0, 60, 4, "after a throw the syringe can't be used for this long"],
    ["sticky", "max", "int", "", 1, 32, 6, "stickies a player can have out (a new one removes the oldest)"],
    ["sticky", "armTime", "f32", "s", 0, 10, 0.35, "a sticky can be set off this long after it lands"],
    ["sticky", "inherit", "f32", "x", 0, 2, 0.9, "share of the shooter's velocity a sticky keeps"],
];

export const GROUPS = [["damage", "Damage"], ["fire", "Fire"], ["projectile", "Projectile"], ["spread", "Spread / scope"], ["alt", "Alt fire"], ["knockback", "Knockback"], ["falloff", "Falloff"]];

// the built-in catalog in the normalized shape (see normalizeCatalog)
export function builtinCatalog() {
    const fields = [];
    for (const w of WEAPONS) {
        const b = BASE[w];
        DEF_FIELDS.forEach(([field, type, unit, min, max, group, doc], i) => {
            const v = b[[0, 1, 2, 4, 5, 6, 7, 8, 10, 11, 13, 14][i]];
            fields.push({ weapon: w, field, key: w + "." + field, type, unit, min, max, default: type === "bool" ? !!v : v, doc, group });
        });
        for (const [field, type, unit, min, max, group, doc] of EXTRA_FIELDS) {
            const v = field === "headDamage" ? HEAD[w] || 0 : field === "knock" ? KNOCK[w] || 0 : field === "falloffMin" ? 1 : field === "directDamage" ? (w === "rocket" ? 50 : 0)
                : field === "selfDamage" ? 1 : field === "wallbang" ? 0.5 : 0;
            fields.push({ weapon: w, field, key: w + "." + field, type, unit, min, max, default: v, doc, group });
        }
    }
    for (const [w, field, type, unit, min, max, def, doc] of ALT_FIELDS) fields.push({ weapon: w, field, key: w + "." + field, type, unit, min, max, default: def, doc, group: "alt", alt: true });
    const weapons = WEAPONS.map((id, i) => ({ id, index: i, name: id.toUpperCase(), title: TITLE[id], kind: KIND[id], altFire: ALT[id],
        projectile: BASE[id][3], beam: BASE[id][9], sticky: BASE[id][12] }));
    return finish({ fields, weapons, slots: builtinSlots(), builtins: BUILTINS, fromGame: false, source: "built-in (gamedefs.h 1.18)" });
}

export const BUILTINS = [
    { name: "min", sig: "min(a, b)", doc: "the smaller" }, { name: "max", sig: "max(a, b)", doc: "the larger" },
    { name: "abs", sig: "abs(x)", doc: "absolute value" }, { name: "clamp", sig: "clamp(x, lo, hi)", doc: "x kept within lo..hi" },
    { name: "sqrt", sig: "sqrt(x)", doc: "square root" }, { name: "floor", sig: "floor(x)", doc: "round down" }, { name: "ceil", sig: "ceil(x)", doc: "round up" },
    { name: "pow", sig: "pow(x, y)", doc: "x to the power y" }, { name: "lerp", sig: "lerp(a, b, t)", doc: "a + (b - a) * t" },
    { name: "f32", sig: "f32(x)", doc: "as a float" }, { name: "f64", sig: "f64(x)", doc: "as a double" }, { name: "int", sig: "int(x)", doc: "as an int (toward 0)" },
];
const BUILTIN_SHAPE = { min: [2, "common"], max: [2, "common"], abs: [1, "arg"], clamp: [3, "common"], sqrt: [1, "arg"], floor: [1, "arg"], ceil: [1, "arg"],
    pow: [2, "common"], lerp: [3, "common"], f32: [1, "f32"], f64: [1, "f64"], int: [1, "int"] };

function builtinSlots() {
    const s = (name, type, unit, doc) => ({ name, type, unit, doc });
    const hitSensors = [
        s("weapon", "weapon", "", "the weapon that hit"), s("alt", "bool", "", "alt fire"), s("dist", "f32", "m", "shooter's eye to the hit"),
        s("head", "bool", "", "a headshot"), s("direct", "bool", "", "the projectile hit the player itself"), s("splash", "f32", "", "blast fraction 0..1 (1 for a non-splash hit)"),
        s("self", "bool", "", "you hit yourself"), s("airborne", "bool", "", "the victim isn't on the ground"), s("victimHealth", "f32", "hp", "the victim's health before the hit"),
        s("shooterHealth", "f32", "hp", "the shooter's health"), s("victimSpeed", "f32", "m/s", "the victim's speed"), s("shooterSpeed", "f32", "m/s", "the shooter's speed"),
        s("time", "f64", "s", "since the round started"), s("streak", "int", "", "this shooter's hits on this victim within 1 s, this one included"),
    ];
    return [
        { name: "hit", when: "every hit (server only); `hit <weapon>` runs instead for that weapon", sensors: hitSensors,
          outputs: [{ name: "damage", type: "f32", unit: "hp", default: "the damage the game worked out", doc: "after falloff, head, wallbang, splash" },
                    { name: "knock", type: "f32", unit: "x", default: 1, doc: "x knockback" },
                    { name: "heal", type: "f32", unit: "hp", default: 0, doc: "health to the shooter (up to max)" }] },
        { name: "kill", when: "every kill; `kill <weapon>` runs instead for that weapon",
          sensors: [s("weapon", "weapon", "", "the weapon"), s("alt", "bool", "", "alt-fire"), s("dist", "f32", "m", "the shooter's eye to the hit"), s("head", "bool", "", "a headshot kill"),
                    s("airborne", "bool", "", "the victim isn't on the ground"), s("shooterHealth", "f32", "hp", "the shooter's health"), s("victimSpeed", "f32", "m/s", "the victim's speed"),
                    s("shooterSpeed", "f32", "m/s", "the shooter's speed"), s("time", "f64", "s", "since the round started"), s("kills", "int", "", "the shooter's kills this life, this one included")],
          outputs: [{ name: "heal", type: "f32", unit: "hp", default: 0, doc: "health to the shooter" }, { name: "refund", type: "bool", unit: "", default: false, doc: "the shooter's weapon cooldown resets" }] },
    ];
}

// any catalog (the game's, or the built-in) -> {fields, byKey, weapons, byId, slots, slotByName, builtins, builtinShape, defaultText, ...}
export function normalizeCatalog(c) {
    const base = builtinCatalog();
    if (!c || !Array.isArray(c.fields) || !c.fields.length) return base;
    const old = base.byKey;
    const fields = c.fields.map((f) => {
        const o = old.get(f.key || f.weapon + "." + f.field) || {};
        const key = f.key || f.weapon + "." + f.field;
        return { ...o, ...f, key, field: f.field || key.split(".").slice(1).join("."), weapon: f.weapon || key.split(".")[0],
            group: f.group || o.group || (f.alt ? "alt" : "damage"), unit: f.unit ?? o.unit ?? "", doc: f.doc || o.doc || "" };
    });
    // the game's weapons: {id: <number>, name: "rifle", title: "RIFLE", kind, altFire, slot}; the page keys them by name
    const weapons = Array.isArray(c.weapons) && c.weapons.length ? c.weapons.map((w, i) => {
        const id = typeof w.id === "string" ? w.id : String(w.name || "").toLowerCase();
        const b = base.byId.get(id) || {};
        return { ...b, ...w, id, index: typeof w.id === "number" ? w.id : w.index ?? i, title: b.title || w.title || id, gameTitle: w.title || "" };
    }) : base.weapons;
    const slots = Array.isArray(c.slots) && c.slots.length ? c.slots.map((s) => ({ ...s, when: s.when || s.doc || "", sensors: (s.sensors || []).map((x) => (typeof x === "string" ? { name: x, type: "f32" } : x)), outputs: s.outputs || [] })) : base.slots;
    const builtins = Array.isArray(c.builtins) && c.builtins.length ? c.builtins.map((b) => (typeof b === "string" ? { name: b, sig: b + "()" } : b)) : base.builtins;
    return finish({ fields, weapons, slots, builtins, defaultText: c.defaultText, fromGame: true, source: c.source || "the game" });
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

// the standard set as .qweapons text, every field (the MOCK's exportweapons defaultText)
export function standardText(cat) {
    const out = ["version 1", 'weapons "standard"'];
    for (const f of cat.fields) out.push(`param ${f.key} ${f.type} = ${fmtValue(f.default, f.type)}`);
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
