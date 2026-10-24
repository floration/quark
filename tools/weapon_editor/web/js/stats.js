// The numbers of a weapon set, worked out from its field values (the MOCK's wpnstats, and the page's charts): one
// row per weapon and fire mode (primary / alt, and a splash-only row for blast weapons), each with damage body /
// head per shot, shots and time to kill, sustained DPS, range, projectile speed and flight times, splash and falloff.
//
//   shots to kill  = ceil(health / damage per shot)
//   time to kill   = (shots - 1) x refire  (+ the arm time for stickies: the last one has to arm before it goes off)
//   sustained DPS  = damage per shot / refire  (all pellets: damage x pellets; the laser's beam: damage per 1 ms tick / refire)
//   head           = headDamage per hit / pellet when the weapon has headshots (headDamage > 0), else the body numbers
//   rockets        = a direct hit does at least directDamage; "splash" rows are the blast's centre without a direct hit
//   at a distance  = x falloff (the fields, or the set's <weapon>.falloff curve); 0 beyond a hitscan's range or a
//                    projectile's speed x life; shotgun pellets that miss a 0.8 x 1.8 m body at that distance don't count
//
// The game's own numbers come from quark_maptool wpnstats when it has it; this is the same arithmetic on the same fields.

import { WEAPONS } from "./catalog.js";
import { evalCurve } from "./ast.js";

export const HALF_W = 0.4, HEIGHT = 1.8, HEAD_FRAC = 0.2, HEAD_W_FRAC = 0.45;
export const DISTANCES = [10, 20, 40];

// gamedefs.h pelletOffset: pellet k of count, angle a around the aim and radius r (radians) off it
export function pelletOffset(k, count, spread) {
    if (count <= 1 || spread <= 0 || k <= 0 || k >= count) return [0, 0];
    const rest = count - 1;
    let rings = Math.round((Math.sqrt(1 + 4 * rest / 3) - 1) / 2);
    if (rings < 1) rings = 1;
    const tri = rings * (rings + 1) / 2;
    let before = 1;
    for (let j = 1; j <= rings; j++) {
        const n = j === rings ? count - before : Math.round(rest * j / tri);
        if (n <= 0) continue;
        if (k < before + n) {
            const r = spread * Math.sqrt((before + n * 0.5) / count);
            const a = 1.5707963 + 6.2831853 * ((k - before) + ((j & 1) ? 0 : 0.5)) / n;
            return [a, r];
        }
        before += n;
    }
    return [0, 0];
}
export function burstOffset(shot, spread) {
    const i = shot % 16;
    return [2.3999632 * i, spread <= 0 ? 0 : spread * Math.sqrt(i / 16)];
}
// share of a shot's pellets landing on the body (and on the head) of a player d metres away, aimed at the body's
// centre (head = false) or the head's centre (head = true): {body, head} counts
export function pelletHits(pellets, spread, d, aimHead) {
    const headH = HEIGHT * HEAD_FRAC, headHW = HALF_W * HEAD_W_FRAC;
    const aimY = aimHead ? HEIGHT - headH / 2 : HEIGHT / 2;
    let body = 0, head = 0;
    for (let k = 0; k < pellets; k++) {
        const [a, r] = pelletOffset(k, pellets, spread);
        const off = Math.tan(r) * Math.max(0, d);
        const x = Math.cos(a) * off, y = aimY + Math.sin(a) * off;
        const inHead = Math.abs(x) <= headHW && y >= HEIGHT - headH && y <= HEIGHT;
        const inBody = Math.abs(x) <= HALF_W && y >= 0 && y <= HEIGHT;
        if (aimHead && inHead) head++;
        else if (inBody) body++;
    }
    return { body, head };
}

// how far a projectile can hit a target on the floor it is fired from: launched from the eye (1.6 m, kEyeHeight) at the
// best angle (every 0.5 degree), its flight cut by the ground or its life; one that doesn't drop flies speed x life
// (wpn_stats.cpp ballisticReach: the same loop)
export const EYE_HEIGHT = 1.6;
export function ballisticReach(speed, gravity, life) {
    if (gravity <= 0 || speed <= 0) return speed * life;
    let best = 0;
    for (let i = 0; i <= 180; i++) {
        const a = i * 0.5 * 3.14159265358979 / 180, vx = speed * Math.cos(a), vy = speed * Math.sin(a);
        const tGround = (vy + Math.sqrt(vy * vy + 2 * gravity * EYE_HEIGHT)) / gravity;
        best = Math.max(best, vx * Math.min(life, tGround));
    }
    return best;
}

export function bubbleCount(v, dist) {
    const near = v("bubble.streamNear"), far = v("bubble.streamFar"), mn = v("bubble.streamMin"), mx = v("bubble.streamMax");
    let f = far > near ? (dist - near) / (far - near) : dist >= far ? 1 : 0;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    return mn + Math.floor(f * (mx - mn) + 0.5);
}

const ROW_DEFS = [
    // [weapon, mode, variant, label suffix]
    ["rifle", "primary", ""], ["rifle", "alt", "", "scoped"],
    ["rocket", "primary", "direct", "direct hit"], ["rocket", "primary", "splash", "splash only"], ["rocket", "alt", "direct", "lob · direct hit"], ["rocket", "alt", "splash", "lob · splash only"],
    ["knife", "primary", "", "swing"], ["knife", "alt", "", "throw"],
    ["sniper", "primary", ""],
    ["laser", "primary", "", "beam"], ["laser", "alt", "", "pull beam"],
    ["shotgun", "primary", ""], ["shotgun", "alt", "", "bounce (after a bounce)"],
    ["sticky", "primary", "", "blast centre"],
    ["crossbow", "primary", ""],
    ["bubble", "primary", ""], ["bubble", "alt", "", "stream"],
    ["syringe", "primary", "", "self heal"], ["syringe", "alt", "", "throw (heal a teammate)"],
    ["locator", "primary", ""],
];

// values: Map key -> value; opts: {health, dist (the distance the shots-to-kill columns use; 0 = every hit lands),
// set (the AST, for <weapon>.falloff curves), titles: Map weapon -> display name}
// numbers the game keeps fixed (gamedefs.h: kSyringeThrowSpeed / Gravity / Life, kSyringeAway): a set can't change
// them, so the catalog has no field for them; wpnstats reads the same constants (the load-time check compares)
const FIXED = { "syringe.throwSpeed": 30, "syringe.throwGravity": 9, "syringe.throwLife": 2, "syringe.away": 4 };

export function computeStats(values, opts = {}) {
    const H = opts.health || 100, D = opts.dist || 0;
    const v = (k) => { const x = values.get(k); return x === undefined ? FIXED[k] || 0 : typeof x === "boolean" ? (x ? 1 : 0) : +x; };
    const curves = new Map();
    if (opts.set) for (const d of opts.set.decls) if (d.kind === "curve") curves.set(d.name, d);
    const rows = [];
    for (const [w, mode, variant, suffix] of ROW_DEFS) {
        const r = row(w, mode, variant, v, curves, opts.set, H, D);
        if (!r) continue;
        r.label = (opts.titles && opts.titles.get(w) || w) + (suffix ? " · " + suffix : mode === "alt" ? " · alt" : "");
        rows.push(r);
    }
    return { rows, health: H, dist: D };
}

function falloffFn(w, v, curves, set) {
    const c = curves.get(w + ".falloff");
    if (c) return (d) => { const y = +evalCurve(c, d, set); return Number.isFinite(y) ? Math.max(0, y) : 1; };
    const s = v(w + ".falloffStart"), e = v(w + ".falloffEnd"), m = v(w + ".falloffMin");
    if (m >= 1 || e <= s) return () => 1;
    return (d) => (d <= s ? 1 : d >= e ? m : 1 + (m - 1) * (d - s) / (e - s));
}

function row(w, mode, variant, v, curves, set, H, D) {
    const kindOf = { rifle: "hitscan", sniper: "hitscan", shotgun: "hitscan", laser: "beam", knife: "melee", rocket: "projectile", sticky: "projectile",
        crossbow: "projectile", bubble: "projectile", syringe: "support", locator: "support" };
    const r = { id: `${w}:${mode}${variant ? ":" + variant : ""}`, weapon: w, mode, variant, kind: kindOf[w], notes: [] };
    const fall = falloffFn(w, v, curves, set);
    let dmg = v(w + ".damage"), head = v(w + ".headDamage"), pellets = Math.max(1, Math.round(v(w + ".pellets"))), refire = Math.max(0.001, v(w + ".refire"));
    let range = v(w + ".range"), speed = v(w + ".projSpeed"), life = v(w + ".life"), gravity = v(w + ".gravity");
    let spread = v(w + ".spread"), splash = v(w + ".splashRadius"), direct = v(w + ".directDamage");
    r.headshots = head > 0;
    if (!r.headshots) head = dmg;
    let per = (d, h) => 0;       // damage per shot at distance d (h: aimed at the head)
    let extraTtk = 0;
    if (w === "locator") { r.support = true; r.notes.push("no damage: shows a hider / runner for a moment"); refire = v("locator.refire"); }
    else if (w === "syringe") {
        r.support = true;
        if (mode === "primary") { r.heal = dmg; r.notes.push(`heals you ${fmt(dmg)} hp a use (${fmt(dmg / refire)} hp/s)`); }
        else { const away = v("syringe.away"); r.heal = v("syringe.throwHeal"); refire = Math.max(refire, away); speed = v("syringe.throwSpeed"); gravity = v("syringe.throwGravity"); life = v("syringe.throwLife"); r.reach = ballisticReach(speed, gravity, life);
            r.notes.push(`a teammate it hits gets ${fmt(r.heal)} hp and every cooldown reset; away ${fmt(away)} s`); r.kind = "projectile"; }
    } else if (w === "laser") {
        if (mode === "alt") { const pd = v("laser.pullDamage"); dmg *= pd; head *= pd; r.notes.push(`pulls x${fmt(v("laser.pullScale"))} harder for x${fmt(pd)} damage`); }
        per = (d, h) => (d > range ? 0 : (h ? head : dmg) * fall(d));
        r.beam = true;
        r.notes.push(`a damage tick every ${fmt(refire * 1000)} ms`);
    } else if (r.kind === "hitscan") {
        if (mode === "alt" && w === "rifle") { refire *= v("rifle.scopedRefire"); spread = Math.max(0, v("rifle.scopedSpread")); r.notes.push(`scoped: x${fmt(v("rifle.scopedRefire"))} refire`); }
        let mult = 1;
        if (mode === "alt" && w === "shotgun") { mult = v("shotgun.bounceDamage"); r.notes.push(`pellets bounce ${v("shotgun.bounces")}x; a bounced pellet does x${fmt(mult)} (direct pellets: full)`); }
        if (pellets > 1) {
            per = (d, h) => {
                if (d > range) return 0;
                const p = pelletHits(pellets, spread, d, h);
                return (p.body * dmg + p.head * head) * mult * fall(d);
            };
        } else per = (d, h) => (d > range ? 0 : (h ? head : dmg) * mult * fall(d));
    } else if (w === "knife" && mode === "primary") {
        per = (d, h) => (d > range ? 0 : (h ? head : dmg) * fall(d));
        r.notes.push(`reach ${fmt(range)} m; hit boxes +${fmt(v("knife.grow"))} m`);
    } else {
        // projectiles: rocket (and its lob), sticky, crossbow, bubble (and its stream), the thrown knife
        if (w === "rocket" && mode === "alt") { speed *= v("rocket.lobSpeed"); gravity = v("rocket.lobGravity"); }
        if (w === "knife") { const away = v("knife.away"); speed = v("knife.throwSpeed"); gravity = v("knife.throwGravity"); life = v("knife.throwLife"); refire = Math.max(refire, away); r.kind = "projectile";
            r.notes.push(`one throw, then the knife is away ${fmt(away)} s`); }
        if (w === "sticky") { extraTtk = v("sticky.armTime"); r.notes.push(`arms in ${fmt(extraTtk)} s; ${v("sticky.max")} out at once`); }
        const reach = ballisticReach(speed, gravity, life);
        if (gravity > 0 && reach < speed * life - 1e-6) r.notes.push(`reaches ${fmt(reach)} m on flat ground (aimed up; ${fmt(speed * life)} m straight before it expires)`);
        let base = dmg, baseHead = head;
        if (splash > 0) {
            if (variant === "splash") { base = dmg; baseHead = dmg; r.headshots = false; r.notes.push(`the blast's centre; 0 at ${fmt(splash)} m`); }
            else if (direct > 0) { base = Math.max(direct, dmg); baseHead = r.headshots ? Math.max(direct, head) : base; r.notes.push(`direct hit: at least ${fmt(direct)}`); }
        }
        if (w === "bubble" && mode === "alt") {
            refire = v("bubble.streamRefire");
            const fastest = Math.max(v("bubble.streamSlow"), v("bubble.streamFast"));
            per = (d, h) => (d > fastest * life ? 0 : bubbleCount(v, d) * base * fall(d));
            r.notes.push(`${bubbleCount(v, 0)} bubbles within ${fmt(v("bubble.streamNear"))} m, ${v("bubble.streamMax")} from ${fmt(v("bubble.streamFar"))} m; ${fmt(v("bubble.streamSlow"))}-${fmt(fastest)} m/s`);
            speed = fastest;
        } else per = (d, h) => (reach > 0 && d > reach ? 0 : (h ? baseHead : base) * fall(d));
        r.reach = reach;
        if (gravity) r.notes.push(`drops ${fmt(gravity)} m/s²`);
    }
    r.refire = refire;
    r.pellets = pellets;
    r.range = r.kind === "hitscan" || r.kind === "beam" || r.kind === "melee" ? range : r.reach || 0;
    r.speed = r.kind === "projectile" ? speed : 0;
    r.splashRadius = r.kind === "projectile" && w !== "knife" && w !== "syringe" ? splash : 0;
    r.splashMax = r.splashRadius > 0 ? dmg : 0;
    r.per = per;
    // the Values table's extras: the knockback field (its unit is the weapon's: see the field's doc), one shot's damage
    // through a wallbang wall (hitscan: body x wallbang; the laser's beam too), your own damage from your blast's centre
    r.knock = v(w + ".knock");
    r.wallbang = r.kind === "hitscan" || r.kind === "beam" ? per(D, false) * v(w + ".wallbang") : null;
    r.selfDmg = r.splashRadius > 0 ? r.splashMax * v(w + ".selfDamage") : null;
    r.body = per(D, false);
    r.head = per(D, true);
    r.burst = r.body;
    const stk = (x) => (x > 1e-9 ? Math.ceil(H / x - 1e-9) : Infinity);
    r.stkBody = stk(r.body); r.stkHead = stk(r.head);
    const ttk = (n) => (Number.isFinite(n) ? (n - 1) * refire + extraTtk : Infinity);
    r.ttkBody = ttk(r.stkBody); r.ttkHead = ttk(r.stkHead);
    r.dpsBody = r.body / refire; r.dpsHead = r.head / refire;
    for (const d of DISTANCES) {
        r["t" + d] = r.kind === "projectile" && speed > 0 ? (r.reach && d > r.reach ? null : d / speed) : r.kind === "support" ? null : 0;
        r["fall" + d] = fall(d);
        r["at" + d] = per(d, false);
    }
    if (r.support) { r.body = r.head = r.dpsBody = r.dpsHead = 0; r.stkBody = r.stkHead = r.ttkBody = r.ttkHead = Infinity; }
    return r;
}

const fmt = (x) => { const a = Math.abs(x); return (a >= 100 ? x.toFixed(0) : a >= 10 ? x.toFixed(1) : a >= 1 ? x.toFixed(2) : x.toFixed(3)).replace(/(\.\d*?)0+$/, "$1").replace(/\.$/, "") || "0"; };
export { fmt as fmtNum };

// edited minus standard per row id, for the columns that are numbers
export const NUM_COLS = ["body", "head", "stkBody", "stkHead", "ttkBody", "ttkHead", "dpsBody", "dpsHead", "refire", "range", "speed", "t10", "t20", "t40", "splashMax", "splashRadius", "fall10", "fall20", "fall40", "knock", "wallbang", "selfDmg"];
export function diffRows(a, b) {
    const by = new Map(b.map((r) => [r.id, r]));
    return a.map((r) => {
        const o = by.get(r.id), d = {};
        if (o) for (const k of NUM_COLS) { const x = r[k], y = o[k]; d[k] = Number.isFinite(x) && Number.isFinite(y) ? x - y : x === y ? 0 : null; }
        return { row: r, std: o, diff: d };
    });
}
export { WEAPONS };
