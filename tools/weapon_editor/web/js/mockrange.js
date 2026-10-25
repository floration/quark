// The MOCK's wpnrange (DESIGN.md section 4): one shooter fires one weapon at still or moving dummy targets for some
// seconds, with the set's field values and its hit / kill slots. A simple stand-in for the game's own Server (the
// helper's answer replaces it): perfect / head / body / bot aim, hitscan rays and pellets in the game's fixed pattern,
// projectiles stepped every 1 ms with gravity, lead and drop compensation, splash to the closest point of each
// hitbox, the laser's beam in 10 ms slices, the knife's reach, a 1 s respawn. Map walls block shots when a map is
// given (its boxes); the built-in range is a flat floor.
//
// request: {weapon, alt?, order?: "lanes"|"nearest", targets: [{pos: [x,y,z], moving?: {axis: "x"|"z", speed, amplitude}, health?}],
//           shooter: {pos: [x,y,z], aim: "perfect"|"head"|"body"|"bot", skill?}, seconds, seed, health?}
// answer:  {ok, weapon, alt, seconds, targets: [{index, hits, headHits, damage, kills, firstKill, shots}],
//           shots: [{t, hit, head, dmg, target, dist}], frames: [{t, players: [{pos, yaw, pitch, health, alive}],
//           projectiles: [{pos}], shots: [{from, to, hit}]}], totals: {shots, hits, headHits, damage, kills, dps, shotsHit, accuracy}, notes}

import { pelletOffset, burstOffset, bubbleCount, HALF_W, HEIGHT, HEAD_FRAC, HEAD_W_FRAC } from "./stats.js";
import { runSlot, symbols, evalCurve } from "./ast.js";

const EYE = 1.6, FRAME = 0.02, RESPAWN = 1.0;
const HEAD_H = HEIGHT * HEAD_FRAC, HEAD_HW = HALF_W * HEAD_W_FRAC;

function rng(seed) {
    let a = (seed >>> 0) || 1;
    return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const len = (a) => Math.hypot(a[0], a[1], a[2]);
const norm = (a) => { const l = len(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };

// a ray against an AABB: entry distance or Infinity
function rayBox(o, d, x0, y0, z0, x1, y1, z1, maxT) {
    let t0 = 0, t1 = maxT;
    const lo = [x0, y0, z0], hi = [x1, y1, z1];
    for (let k = 0; k < 3; k++) {
        if (Math.abs(d[k]) < 1e-12) { if (o[k] < lo[k] || o[k] > hi[k]) return Infinity; continue; }
        let a = (lo[k] - o[k]) / d[k], b = (hi[k] - o[k]) / d[k];
        if (a > b) { const t = a; a = b; b = t; }
        if (a > t0) t0 = a;
        if (b < t1) t1 = b;
        if (t0 > t1) return Infinity;
    }
    return t0;
}
// the direction of the aim d turned by angle a around it and r off it (the game's pellet / burst pattern)
function offsetDir(d, a, r) {
    if (r <= 0) return d;
    let up = Math.abs(d[1]) > 0.99 ? [1, 0, 0] : [0, 1, 0];
    const right = norm([d[1] * up[2] - d[2] * up[1], d[2] * up[0] - d[0] * up[2], d[0] * up[1] - d[1] * up[0]]);
    up = [right[1] * d[2] - right[2] * d[1], right[2] * d[0] - right[0] * d[2], right[0] * d[1] - right[1] * d[0]];
    const tr = Math.tan(r), ca = Math.cos(a) * tr, sa = Math.sin(a) * tr;
    return norm([d[0] + right[0] * ca + up[0] * sa, d[1] + right[1] * ca + up[1] * sa, d[2] + right[2] * ca + up[2] * sa]);
}

export function mockRange(req, values, set, map = null) {
    const v = (k) => { const x = values.get(k); return x === undefined ? 0 : typeof x === "boolean" ? (x ? 1 : 0) : +x; };
    const S = symbols(set || { decls: [] });
    const w = req.weapon || "rifle", alt = !!req.alt;
    const seconds = Math.max(0.5, Math.min(60, +req.seconds || 10));
    const rand = rng(req.seed || 1);
    const notes = [];
    const shooter = { pos: (req.shooter && req.shooter.pos) || [0, 0, 0], aim: (req.shooter && req.shooter.aim) || "perfect", skill: (req.shooter && req.shooter.skill) ?? 0.7 };
    const eye = [shooter.pos[0], shooter.pos[1] + EYE, shooter.pos[2]];
    const maxHealth = req.health || 100;
    const T = (req.targets || []).map((t, i) => ({ index: i, base: t.pos.slice(), pos: t.pos.slice(), vel: [0, 0, 0], moving: t.moving || null, maxHealth: t.health || maxHealth,
        health: t.health || maxHealth, alive: true, respawnAt: 0, hits: 0, headHits: 0, damage: 0, kills: 0, firstKill: null, shots: 0, lastHit: [] }));
    // the weapon's numbers for this fire mode
    const fall = (() => {
        const c = S.curves.get(w + ".falloff");
        if (c) return (d) => { const y = +evalCurve(c, d, set, S, values); return Number.isFinite(y) ? Math.max(0, y) : 1; };
        const s0 = v(w + ".falloffStart"), e = v(w + ".falloffEnd"), m = v(w + ".falloffMin");
        if (m >= 1 || e <= s0) return () => 1;
        return (d) => (d <= s0 ? 1 : d >= e ? m : 1 + (m - 1) * (d - s0) / (e - s0));
    })();
    let refire = Math.max(0.001, v(w + ".refire")), dmg = v(w + ".damage"), head = v(w + ".headDamage"), pellets = Math.max(1, Math.round(v(w + ".pellets")));
    let spread = v(w + ".spread"), range = v(w + ".range"), speed = v(w + ".projSpeed"), grav = v(w + ".gravity"), life = v(w + ".life");
    let splash = v(w + ".splashRadius"), direct = v(w + ".directDamage");
    const kinds = { rifle: "hitscan", sniper: "hitscan", shotgun: "hitscan", laser: "beam", knife: "melee", rocket: "proj", sticky: "proj", crossbow: "proj", bubble: "proj", syringe: "support", locator: "support" };
    let kind = kinds[w] || "hitscan";
    let mult = 1, stream = false, away = 0, sticky = w === "sticky";
    if (alt) {
        if (w === "rifle") { refire *= v("rifle.scopedRefire"); spread = Math.max(0, v("rifle.scopedSpread")); }
        else if (w === "sniper" || w === "crossbow") spread = Math.max(0, v(w + ".scopedSpread"));
        else if (w === "rocket") { speed *= v("rocket.lobSpeed"); grav = v("rocket.lobGravity"); }
        else if (w === "laser") { mult = v("laser.pullDamage"); }
        else if (w === "shotgun") notes.push("bouncing pellets: the range has no walls to bounce off, so they fly like the primary's");
        else if (w === "knife") { kind = "proj"; speed = v("knife.throwSpeed"); grav = v("knife.throwGravity"); life = v("knife.throwLife"); away = v("knife.away"); splash = 0; }
        else if (w === "bubble") { stream = true; refire = v("bubble.streamRefire"); }
        else if (w === "sticky") notes.push("alt sets the stickies off: the mock sets each one off as soon as it is armed");
    }
    if (sticky) notes.push("stickies are set off as soon as they arm (the fastest a player could)");
    if (w === "knife" && !alt) { range = v("knife.range") + v("knife.grow"); if (T.every((x) => len(sub(x.base, shooter.pos)) > range + HALF_W)) notes.push(`every target is out of the knife's reach (${range.toFixed(2)} m): put one closer`); }
    // the map's solid boxes (walls block shots and projectiles)
    const solid = [];
    if (map && map.boxes) for (let i = 0; i < map.nBoxes; i++) { const mt = map.mats[i]; if (mt === 5 || mt === 6 || mt === 9) continue; solid.push(i); }
    const wallT = (o, d, maxT) => {
        let best = maxT;
        if (!map) { if (d[1] < -1e-9) { const t = -o[1] / d[1]; if (t > 0 && t < best) best = t; } return best; }
        const B = map.boxes;
        for (const i of solid) { const k = i * 6; const t = rayBox(o, d, B[k], B[k + 1], B[k + 2], B[k + 3], B[k + 4], B[k + 5], best); if (t < best && t > 1e-4) best = t; }
        return best;
    };
    const shots = [], frames = [];
    let frameShots = [];
    const projs = [];
    let t = 0, cool = 0, nextFrame = 0, shotNo = 0, shotsHit = 0;
    // shots and hits in one unit, as wpnrange counts them: a shot (a projectile carries its own), the laser's 1 ms beam
    // ticks (a 10 ms slice is n of them); a target's hit = a shot that damaged it, however many pellets landed
    let cur = { id: 0, n: 1, hit: false };
    const order = req.order || "lanes";   // "lanes": kill each target once in turn; "nearest": always the nearest alive
    let ptr = 0;
    const focus = () => {
        if (order === "lanes" && T.length && T[ptr % T.length].alive) return T[ptr % T.length];
        let best = null, bd = Infinity;
        for (const x of T) if (x.alive) { const d = len(sub(x.pos, eye)); if (d < bd) { bd = d; best = x; } }
        return best;
    };
    // (a support weapon: one still frame; here, after focus and projs, which frame() reads)
    if (kind === "support") return { ok: true, weapon: w, alt, seconds, targets: T.map(pub), shots: [], frames: [frame(0, [])], totals: tot(0), notes: [`the ${w} does no damage: nothing to test on the range`], via: "mock" };
    const aimPoint = (x) => [x.pos[0], x.pos[1] + (shooter.aim === "head" ? HEIGHT - HEAD_H / 2 : HEIGHT / 2), x.pos[2]];
    const moveTargets = (dt) => {
        for (const x of T) {
            if (!x.alive && t >= x.respawnAt) { x.alive = true; x.health = x.maxHealth; }
            if (!x.moving) continue;
            const ax = x.moving.axis === "z" ? 2 : 0, A = Math.max(0.1, x.moving.amplitude || 3), sp = x.moving.speed || 5;
            const ph = (sp / A) * t;
            x.pos[ax] = x.base[ax] + A * Math.sin(ph);
            x.vel[ax] = sp * Math.cos(ph);
        }
    };
    const predict = (x, dt) => [x.pos[0] + x.vel[0] * dt, x.pos[1], x.pos[2] + x.vel[2] * dt];
    // a hit on target x: the game's damage, then the set's hit slot, then the kill slot
    const hit = (x, base, isHead, dist, isDirect, frac, at) => {
        const S2 = { damage: base, alt, dist, head: isHead, direct: isDirect, splash: frac, self: false, airborne: false, victimHealth: x.health,
            shooterHealth: maxHealth, victimSpeed: Math.hypot(x.vel[0], x.vel[2]), shooterSpeed: 0, time: t, streak: 1 + x.lastHit.filter((u) => u > t - 1).length };
        const r = runSlot(set || { decls: [] }, "hit", w, S2, S, values);
        const d = r.outputs.damage;
        x.lastHit.push(t); if (x.lastHit.length > 64) x.lastHit.shift();
        if (x.lastShot !== cur.id) { x.lastShot = cur.id; x.hits += cur.n; if (!cur.hit) { cur.hit = true; shotsHit += cur.n; } }
        if (isHead && x.lastHead !== cur.id) { x.lastHead = cur.id; x.headHits++; }   // (a blast with a head pellet: one head hit)
        x.damage += Math.min(d, Math.max(0, x.health));
        x.health -= d;
        shots.push({ t: +t.toFixed(3), hit: true, head: isHead, dmg: +d.toFixed(3), target: x.index, dist: +dist.toFixed(2) });
        if (x.health <= 0 && x.alive) {
            x.alive = false; x.kills++; x.respawnAt = t + RESPAWN;
            if (x.firstKill === null) x.firstKill = +t.toFixed(3);
            if (order === "lanes" && T[ptr % T.length] === x) ptr = (ptr + 1) % T.length;
            const k = runSlot(set || { decls: [] }, "kill", w, { weapon: w, head: isHead, airborne: false, dist, kills: T.reduce((a, y) => a + y.kills, 0) }, S, values);
            if (k.outputs.refund) cool = 0;
        }
        return d;
    };
    // a hitscan ray from the eye: the nearest target box (head first) before a wall
    const ray = (dir, maxT, scale) => {
        const wt = wallT(eye, dir, maxT);
        let best = null, bt = wt;
        for (const x of T) {
            if (!x.alive) continue;
            const tb = rayBox(eye, dir, x.pos[0] - HALF_W, x.pos[1], x.pos[2] - HALF_W, x.pos[0] + HALF_W, x.pos[1] + HEIGHT, x.pos[2] + HALF_W, bt);
            if (tb < bt) { bt = tb; best = x; }
        }
        const end = [eye[0] + dir[0] * bt, eye[1] + dir[1] * bt, eye[2] + dir[2] * bt];
        if (!best) return { end, x: null };
        const th = rayBox(eye, dir, best.pos[0] - HEAD_HW, best.pos[1] + HEIGHT - HEAD_H, best.pos[2] - HEAD_HW, best.pos[0] + HEAD_HW, best.pos[1] + HEIGHT, best.pos[2] + HEAD_HW, maxT);
        const isHead = Number.isFinite(th) && head > 0;
        const base = (isHead ? head : dmg) * fall(bt) * scale;
        hit(best, base, isHead, bt, false, 1, end);
        return { end, x: best };
    };
    const explode = (p, directX, isDirectHead) => {
        for (const x of T) {
            if (!x.alive) continue;
            const cx = Math.max(x.pos[0] - HALF_W, Math.min(x.pos[0] + HALF_W, p[0])), cy = Math.max(x.pos[1], Math.min(x.pos[1] + HEIGHT, p[1])), cz = Math.max(x.pos[2] - HALF_W, Math.min(x.pos[2] + HALF_W, p[2]));
            const d = Math.hypot(p[0] - cx, p[1] - cy, p[2] - cz), dist = len(sub(p, eye));
            if (x === directX) { const base = Math.max(dmg, direct) * fall(dist); hit(x, base, false, dist, true, 1, p); continue; }
            if (splash <= 0 || d >= splash) continue;
            const frac = 1 - d / splash;
            hit(x, dmg * frac * fall(dist), false, dist, false, frac, p);
        }
    };
    const fire = (x, n = 1) => {
        shotNo += n;
        cur = { id: shotNo, n, hit: false };
        const tp = aimPoint(x);
        let aimErr = 0;
        if (shooter.aim === "bot") aimErr = (1 - Math.max(0, Math.min(1, shooter.skill))) * 0.03 + 0.002;
        const jitter = (d) => (aimErr > 0 ? offsetDir(d, rand() * 6.2831853, Math.abs(gauss(rand)) * aimErr) : d);
        x.shots += n;
        if (kind === "hitscan" || kind === "beam" || kind === "melee") {
            const d0 = jitter(norm(sub(tp, eye)));
            const maxT = range;
            const n = kind === "beam" ? 1 : pellets;
            const scale = mult;
            for (let k = 0; k < n; k++) {
                const [a, r] = pellets > 1 ? pelletOffset(k, pellets, spread) : burstOffset(shotNo, spread);
                const d = offsetDir(d0, a, r);
                const res = ray(d, maxT, kind === "beam" ? beamScale : scale);
                if (!res.x) shots.push({ t: +t.toFixed(3), hit: false, head: false, dmg: 0, target: -1, dist: 0 });
                if (k < 6) frameShots.push({ from: eye.slice(), to: res.end.map((q) => +q.toFixed(2)), hit: !!res.x });
            }
            return;
        }
        // projectiles: aim with lead and drop compensation (perfect: solved; bot: the same plus aim error)
        const count = stream ? bubbleCount(v, len(sub(tp, eye))) : 1;
        for (let k = 0; k < count; k++) {
            const sp = stream ? v("bubble.streamSlow") + (v("bubble.streamFast") - v("bubble.streamSlow")) * (count > 1 ? k / (count - 1) : 1) : speed;
            let p = tp, tf = len(sub(p, eye)) / Math.max(1, sp);
            for (let it = 0; it < 5; it++) { const q = predict(x, tf); p = [q[0], tp[1] + 0.5 * grav * tf * tf, q[2]]; tf = len(sub(p, eye)) / Math.max(1, sp); }
            let d = jitter(norm(sub(p, eye)));
            const [a, r] = stream ? burstOffset(k, v("bubble.spread")) : burstOffset(shotNo, spread);
            d = offsetDir(d, a, r);
            projs.push({ pos: eye.slice(), vel: [d[0] * sp, d[1] * sp, d[2] * sp], born: t, stuck: false, landed: 0, grav, shot: cur });
        }
    };
    let beamScale = 1;
    const DT = 0.001;
    const steps = Math.round(seconds / DT);
    let beamAcc = 0;
    for (let i = 0; i <= steps; i++) {
        t = i * DT;
        moveTargets(DT);
        // fire
        cool -= DT;
        const x = focus();
        if (kind === "beam") {
            beamAcc += DT;
            if (beamAcc >= 0.01 - 1e-9) {   // the beam's ticks in 10 ms slices
                const ticks = Math.round(beamAcc / refire);
                beamAcc = 0;
                if (x && ticks > 0) { beamScale = ticks * mult; fire(x, ticks); }
            }
        } else if (x && cool <= 0) { fire(x); cool = Math.max(refire, away); }
        // projectiles
        for (let j = projs.length - 1; j >= 0; j--) {
            const p = projs[j];
            cur = p.shot;
            if (p.stuck) { if (t - p.landed >= v("sticky.armTime")) { explode(p.pos, null); projs.splice(j, 1); } continue; }
            const o = p.pos.slice();
            p.vel[1] -= p.grav * DT;
            const step = [p.vel[0] * DT, p.vel[1] * DT, p.vel[2] * DT], sl = len(step) || 1e-9, dn = [step[0] / sl, step[1] / sl, step[2] / sl];
            let hitX = null, ht = sl;
            for (const tg of T) {
                if (!tg.alive) continue;
                const tb = rayBox(o, dn, tg.pos[0] - HALF_W - 0.05, tg.pos[1] - 0.05, tg.pos[2] - HALF_W - 0.05, tg.pos[0] + HALF_W + 0.05, tg.pos[1] + HEIGHT + 0.05, tg.pos[2] + HALF_W + 0.05, ht);
                if (tb < ht) { ht = tb; hitX = tg; }
            }
            const wt = wallT(o, dn, ht);
            if (wt < ht) { hitX = null; ht = wt; }
            p.pos = [o[0] + dn[0] * ht, o[1] + dn[1] * ht, o[2] + dn[2] * ht];
            if (hitX || ht < sl) {
                if (hitX && sticky) { p.stuck = true; p.landed = t; continue; }
                if (!hitX && sticky) { p.stuck = true; p.landed = t; continue; }
                if (splash > 0) explode(p.pos, hitX);
                else if (hitX) {
                    const isHead = head > 0 && p.pos[1] >= hitX.pos[1] + HEIGHT - HEAD_H;
                    hit(hitX, (isHead ? head : dmg) * fall(len(sub(p.pos, eye))), isHead, len(sub(p.pos, eye)), true, 1, p.pos);
                } else shots.push({ t: +t.toFixed(3), hit: false, head: false, dmg: 0, target: -1, dist: 0 });
                projs.splice(j, 1);
                continue;
            }
            if (t - p.born > life && life > 0) { if (splash > 0 && !sticky) explode(p.pos, null); projs.splice(j, 1); }
        }
        if (t >= nextFrame - 1e-9) { frames.push(frame(t, frameShots)); frameShots = []; nextFrame += FRAME; }
    }
    return { ok: true, weapon: w, alt, seconds, targets: T.map(pub), shots: shots.slice(0, 20000), frames, totals: tot(seconds), notes, via: "mock" };

    function frame(tt, fs) {
        const yaw = (() => { const x = focus(); if (!x) return 0; const d = sub(x.pos, shooter.pos); return Math.atan2(-d[0], -d[2]); })();
        return { t: +tt.toFixed(3), players: [{ pos: shooter.pos.slice(), yaw, pitch: 0, health: maxHealth, alive: true, shooter: true },
            ...T.map((x) => ({ pos: x.pos.map((q) => +q.toFixed(3)), yaw: 0, pitch: 0, health: Math.max(0, +x.health.toFixed(2)), alive: x.alive }))],
            projectiles: projs.map((p) => ({ pos: p.pos.map((q) => +q.toFixed(2)), stuck: p.stuck })), shots: fs };
    }
    function pub(x) { return { index: x.index, pos: x.base, hits: x.hits, headHits: x.headHits, damage: +x.damage.toFixed(2), kills: x.kills, firstKill: x.firstKill, shots: x.shots }; }
    function tot(sec) {
        const hits = T.reduce((a, x) => a + x.hits, 0), dmgSum = T.reduce((a, x) => a + x.damage, 0);
        return { shots: shotNo, hits, headHits: T.reduce((a, x) => a + x.headHits, 0), damage: +dmgSum.toFixed(2), kills: T.reduce((a, x) => a + x.kills, 0), dps: sec ? +(dmgSum / sec).toFixed(2) : 0,
            shotsHit, accuracy: shotNo ? shotsHit / shotNo : null };
    }
}
function gauss(r) { let u = 0, v = 0; while (u === 0) u = r(); while (v === 0) v = r(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(6.2831853 * v); }

// the built-in range: a flat floor 200 x 40 m with the shooter at the origin facing -z and lanes at 5, 10, 20, 40, 80 m
export const RANGE_DISTANCES = [5, 10, 20, 40, 80];
const LANE_X = [-3, 3, -2, 6, 0];   // (staggered so that no target stands on the line from the shooter to another one)
export function builtinRangeMap() {
    const lines = ["version 1", "title Range", "box -20 -1 -100 20 0 6", "accent -20 0 -100 -19.5 0.6 6", "accent 19.5 0 -100 20 0.6 6", "spawn a 0 0 0 0"];
    RANGE_DISTANCES.forEach((d, i) => lines.push(`trim ${LANE_X[i] - 1.2} 0 ${-d - 0.05} ${LANE_X[i] + 1.2} 0.05 ${-d + 0.05}`));
    return lines.join("\n") + "\n";
}
export function rangeTargets(preset) {
    const mk = (d, x = 0, moving = null) => ({ pos: [x, 0, -d], ...(moving ? { moving } : {}) });
    switch (preset) {
        case "one10": return [mk(10)];
        case "one20": return [mk(20)];
        case "one40": return [mk(40)];
        case "strafe": return RANGE_DISTANCES.map((d, i) => mk(d, LANE_X[i] * 2.5, { axis: "x", speed: 6, amplitude: 2 }));
        case "strafe10": return [mk(10, 0, { axis: "x", speed: 6, amplitude: 3 })];
        case "group": return [mk(12, -1.5), mk(12, 0), mk(12, 1.5)];
        default: return RANGE_DISTANCES.map((d, i) => mk(d, LANE_X[i]));
    }
}
