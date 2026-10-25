// The MOCK's scenario runner (the page's stand-in for `quark_maptool physsim`) and the metrics both use: the helper's
// metrics win where it gives them, the page fills in the rest from the trace (computeMetrics).

import { BoxWorld, newState, cloneState, tuningOf, pmoveStep, maxChargesFor, BTN_JUMP, BTN_CROUCH, BTN_DASH, BTN_GRAPPLE, MF_ONGROUND, MF_HOOKED } from "./mockpm.js";

const DEG = Math.PI / 180;
const r3 = (v) => Math.round(v * 1000) / 1000;

// best air-strafe yaw (degrees) for this ms: the wish direction at the angle to the velocity that gains the most
// (pmove's airAccelerate: cur = cap - accel * cap * dt); on the floor, straight along the velocity
export function strafeYawDeg(s, side, forward, mt, curYawDeg) {
    const h = Math.hypot(s.vel[0], s.vel[2]);
    if (h < 0.5 || !side) return curYawDeg;
    const dx = s.vel[0] / h, dz = s.vel[2] / h;
    const airAccel = 2.2 * mt.m.air_accel * 10, cap = 2.3 * mt.m.air_cap * 1.1;
    const c = Math.max(-1, Math.min(1, (cap - airAccel * cap * 0.001) / h));
    const th = (s.flags & MF_ONGROUND) ? 0 : Math.acos(c);
    const sg = side > 0 ? 1 : -1;
    // the wish direction: velocity turned by th toward the strafe side (right of d = (-dz, dx))
    const wx = Math.cos(th) * dx + sg * Math.sin(th) * -dz, wz = Math.cos(th) * dz + sg * Math.sin(th) * dx;
    // the yaw whose wish (forward / side keys) points along w: forward-only fwd(y) = w; side only: right / left
    const base = Math.atan2(-wx, -wz);
    const off = forward ? Math.atan2(side / 127, forward / 127) : sg * Math.PI / 2;
    return (base + off) / DEG;
}

export class MockSim {
    constructor(map) { this.map = map; this.world = new BoxWorld(map); }
    run(scenario, physics, sampleMs = 10) {
        const t0 = performance.now();
        const mt = tuningOf(physics);
        const s = newState(scenario.start || {});
        if (scenario.start && scenario.start.onGround == null) {
            const b = [s.pos[0] - 0.4, s.pos[1], s.pos[2] - 0.4, s.pos[0] + 0.4, s.pos[1] + 1.8, s.pos[2] + 0.4];
            if (this.world.sweepAxis(b, 1, -0.05) <= -0.049) s.flags &= ~MF_ONGROUND;
        }
        s.dashCharges = Math.min(scenario.start?.dashCharges ?? maxChargesFor(mt), maxChargesFor(mt));
        const steps = [...(scenario.steps || [])].sort((a, b) => a.atMs - b.atMs);
        const dur = Math.max(1, Math.min(120000, scenario.durationMs | 0 || 2000));
        let yaw = scenario.start?.yaw ?? 0, pitch = scenario.start?.pitch ?? 0;
        const trace = [], events = [];
        let si = -1, cur = null, side = 0, course = yaw;
        const sample = (t) => trace.push({ t: r3(t / 1000), pos: s.pos.map(r3), vel: s.vel.map(r3), speed: r3(Math.hypot(...s.vel)),
            hspeed: r3(Math.hypot(s.vel[0], s.vel[2])), flags: s.flags, dashCharges: s.dashCharges, dashTimer: r3(s.dashTimer),
            onGround: (s.flags & MF_ONGROUND) !== 0, yaw: r3(yaw), pitch: r3(pitch) });
        sample(0);
        for (let t = 0; t < dur; t++) {
            while (si + 1 < steps.length && steps[si + 1].atMs <= t) {
                cur = steps[++si];
                if (cur.yaw != null) yaw = cur.yaw;
                if (cur.pitch != null) pitch = cur.pitch;
                side = cur.side | 0;
                course = yaw;   // (the strafe bot weaves around the heading the step starts facing, like the helper)
            }
            const st = cur || { forward: 0, side: 0 };
            yaw += (st.yawRate || 0) / 1000;
            pitch = Math.max(-88.8, Math.min(88.8, pitch + (st.pitchRate || 0) / 1000));
            if (st.strafeBot) yaw = strafeYawDeg(s, side, st.forward | 0, mt, yaw);
            let buttons = (st.jump ? BTN_JUMP : 0) | (st.crouch ? BTN_CROUCH : 0) | (st.dash ? BTN_DASH : 0) | (st.hook ? BTN_GRAPPLE : 0);
            const cmd = { forward: st.forward | 0, side, buttons, msec: 1, yaw: yaw * DEG, pitch: pitch * DEG };
            if (st.jumpAtEdge && !(buttons & BTN_JUMP) && (s.flags & MF_ONGROUND)) {
                const probe = cloneState(s);
                pmoveStep(probe, cmd, this.world, 0.001, mt, () => {});
                if (!(probe.flags & MF_ONGROUND) && probe.vel[1] <= 0) cmd.buttons |= BTN_JUMP;
            }
            let landed = false;
            pmoveStep(s, cmd, this.world, 0.001, mt, (kind, d) => {
                if (kind === "land") landed = true;
                if (kind === "dash") d.charges = s.dashCharges;
                events.push({ t: r3((t + 1) / 1000), kind, pos: s.pos.map(r3), hspeed: r3(Math.hypot(s.vel[0], s.vel[2])), ...d });
            });
            // strafeSwitch "land": swap A / D at each landing; true: weave, swapping once the velocity has swung
            // strafeSwing degrees (25) past the course toward the turn (the helper's two modes)
            if (landed && st.strafeSwitch === "land") side = -side;
            else if (st.strafeSwitch === true && Math.hypot(s.vel[0], s.vel[2]) > 1) {
                const head = Math.atan2(-s.vel[0], -s.vel[2]) / DEG;
                const dev = ((head - course) % 360 + 540) % 360 - 180, lim = st.strafeSwing || 25;
                if ((side > 0 && dev < -lim) || (side < 0 && dev > lim)) side = -side;
            }
            const tt = t + 1;
            if (tt % sampleMs === 0 || tt === dur || (events.length && events[events.length - 1].t === r3(tt / 1000) && trace[trace.length - 1].t !== r3(tt / 1000))) sample(tt);
        }
        return { trace, events, metrics: {}, ms: performance.now() - t0 };
    }
}

// ---------------------------------------------------------------- metrics
// The helper's metric names (tools/physics_editor/native/sim.cpp), in the order the page shows them. With the real
// helper its metrics are shown as they come; with the MOCK, computeMetrics makes the same ones from the trace.
export const METRICS = [
    { key: "jumpHeight", label: "jump height", unit: "m" },
    { key: "airTime", label: "air time (first take-off)", unit: "s" },
    { key: "jumpDistance", label: "jump distance", unit: "m" },
    { key: "takeoff", label: "first take-off", unit: "" },
    { key: "maxHeight", label: "max height", unit: "m" },
    { key: "topSpeed", label: "top horizontal speed", unit: "m/s" },
    { key: "maxSpeed", label: "top speed (3D)", unit: "m/s" },
    { key: "timeToTopSpeed", label: "time to top speed", unit: "s" },
    { key: "timeTo90", label: "time to 90% of it", unit: "s" },
    { key: "speedAfter", label: "speed at the end", unit: "m/s" },
    { key: "distance", label: "distance (start to end)", unit: "m" },
    { key: "pathLength", label: "path length", unit: "m" },
    { key: "speedAtRelease", label: "speed when keys let go", unit: "m/s" },
    { key: "stopTime", label: "stop time", unit: "s" },
    { key: "stopDistance", label: "stop distance", unit: "m" },
    { key: "hopSpeeds", label: "speed at each landing", unit: "m/s" },
    { key: "jumps", label: "jumps", unit: "" },
    { key: "landings", label: "landings", unit: "" },
    { key: "dashes", label: "dashes", unit: "" },
    { key: "dashDistance", label: "dash distance", unit: "m" },
    { key: "dashDuration", label: "dash duration", unit: "s" },
    { key: "dashExitSpeed", label: "dash exit speed", unit: "m/s" },
    { key: "dashJumpSpeed", label: "dash jump speed", unit: "m/s" },
    { key: "superDashSpeed", label: "super dash speed", unit: "m/s" },
    { key: "bounceSpeed", label: "wall bounce speed", unit: "m/s" },
    { key: "hookDistance", label: "hook distance", unit: "m" },
    { key: "hookTime", label: "time on the hook", unit: "s" },
    { key: "hookTopSpeed", label: "top speed on the hook", unit: "m/s" },
    { key: "speedAtUnhook", label: "speed at let-go", unit: "m/s" },
    { key: "ladderTime", label: "ladder time", unit: "s" },
    { key: "climbHeight", label: "climbed", unit: "m" },
    { key: "climbRate", label: "climb rate", unit: "m/s" },
    { key: "landImpact", label: "hardest landing", unit: "m/s" },
    { key: "fallHeight", label: "biggest drop", unit: "m" },
    { key: "gapsCleared", label: "gaps cleared", unit: "" },
    { key: "widestGap", label: "widest gap cleared", unit: "m" },
    { key: "gaps", label: "gaps", unit: "m" },
    { key: "duration", label: "scenario length", unit: "s" },
    { key: "part", label: "room part", unit: "" },
];
export const HIDDEN_METRICS = new Set(["endPos", "landed"]);
const BY_KEY = new Map(METRICS.map((m) => [m.key, m]));
// the rows to show for these runs' metrics: the preset's own keys first, then the known ones in order, then any key
// the helper sends that the page doesn't know (shown under its own name)
export function metricRows(list, first) {
    const have = new Set();
    for (const m of list) for (const k of Object.keys(m || {})) if (m[k] != null && !HIDDEN_METRICS.has(k)) have.add(k);
    const rows = [], put = (k) => { if (!have.has(k)) return; have.delete(k); rows.push(BY_KEY.get(k) || { key: k, label: k, unit: "" }); };
    // the preset's own metrics always get a row, even when no run has one (a jump that didn't land: missingMetric says why)
    for (const k of first || []) if (!have.has(k) && !rows.some((r) => r.key === k)) rows.push({ ...(BY_KEY.get(k) || { key: k, label: k, unit: "" }), missing: true }); else put(k);
    for (const m of METRICS) put(m.key);
    for (const k of [...have]) put(k);
    return rows;
}

// why a run has no value for a metric (shown in its place, so a preset's headline number never just vanishes)
const LANDING = new Set(["jumpDistance", "airTime", "landImpact"]);
export function missingMetric(key, m, scenario) {
    const s = (+(m && m.duration) || (scenario && scenario.durationMs / 1000) || 0).toFixed(1).replace(/\.0$/, "") + " s";
    if (LANDING.has(key)) return m && m.takeoff ? `didn't land within ${s}` : key === "landImpact" ? `no landing in ${s}` : `no take-off in ${s}`;
    if (key === "jumpHeight" || key === "takeoff") return `no take-off in ${s}`;
    if (key.startsWith("dash") || key === "bounceSpeed" || key === "superDashSpeed") return `no ${key === "bounceSpeed" ? "wall bounce" : key === "superDashSpeed" ? "super dash" : "dash"} in ${s}`;
    if (key.startsWith("hook") || key === "speedAtUnhook") return `the hook didn't catch in ${s}`;
    return `not reached in ${s}`;
}

export function computeMetrics(run, scenario) {
    const T = run.trace || [], E = run.events || [];
    const m = {};
    if (!T.length) return m;
    const s0 = T[0].pos, end = T[T.length - 1];
    let maxY = -1e9, top = 0, top3 = 0, path = 0;
    for (let i = 0; i < T.length; i++) {
        const p = T[i];
        if (p.pos[1] > maxY) maxY = p.pos[1];
        if (p.hspeed > top) top = p.hspeed;
        if (p.speed > top3) top3 = p.speed;
        if (i) path += Math.hypot(p.pos[0] - T[i - 1].pos[0], p.pos[1] - T[i - 1].pos[1], p.pos[2] - T[i - 1].pos[2]);
    }
    const r4 = (v) => Math.round(v * 1e4) / 1e4;
    m.duration = end.t;
    m.topSpeed = r4(top); m.maxSpeed = r4(top3);
    const reach = (f) => T.find((p) => p.hspeed >= top * f);
    if (top > 0.1) { m.timeToTopSpeed = reach(0.99).t; m.timeTo90 = reach(0.9).t; }
    m.speedAfter = r4(end.hspeed);
    m.maxHeight = r4(maxY - s0[1]);
    m.distance = r4(Math.hypot(end.pos[0] - s0[0], end.pos[2] - s0[2]));
    m.pathLength = r4(path);
    const atTime = (t) => sampleAt(T, t);
    const off = E.find((e) => ["jump", "dashJump", "superDash", "ladderOff"].includes(e.kind));
    if (off) {
        const land = E.find((e) => e.kind === "land" && e.t > off.t);
        let apex = -1e9;
        for (const p of T) if (p.t >= off.t && (!land || p.t <= land.t) && p.pos[1] > apex) apex = p.pos[1];
        const jp = off.pos || atTime(off.t).pos;
        m.jumpHeight = r4(apex - jp[1]);
        m.takeoff = off.kind === "ladderOff" ? "ladder" : off.kind;
        if (land) { m.airTime = +(land.t - off.t).toFixed(3); const lp = land.pos || atTime(land.t).pos; m.jumpDistance = r4(Math.hypot(lp[0] - jp[0], lp[2] - jp[2])); }
    }
    const lands = E.filter((e) => e.kind === "land");
    m.jumps = E.filter((e) => e.kind === "jump").length;
    m.landings = lands.length;
    m.dashes = E.filter((e) => e.kind === "dash").length;
    m.hopSpeeds = lands.map((e) => e.hspeed);
    if (lands.length) m.landImpact = r4(lands.reduce((a, e) => Math.max(a, e.impact || 0), 0));
    const dash = E.find((e) => e.kind === "dash");
    if (dash) {
        const fin = E.find((e) => ["dashEnd", "dashJump", "superDash"].includes(e.kind) && e.t >= dash.t);
        const a = atTime(dash.t - 0.001), b = fin ? atTime(fin.t) : end;
        m.dashDistance = r4(Math.hypot(b.pos[0] - a.pos[0], b.pos[2] - a.pos[2]));
        if (fin) { m.dashDuration = +(fin.t - dash.t).toFixed(3); m.dashExitSpeed = r4(fin.speed ?? fin.hspeed); }
    }
    const pick = (k) => E.find((e) => e.kind === k);
    if (pick("superDash")) m.superDashSpeed = r4(pick("superDash").speed);
    if (pick("dashJump")) m.dashJumpSpeed = r4(pick("dashJump").speed);
    if (pick("bounce")) m.bounceSpeed = r4(pick("bounce").speed);
    const hook = pick("hook");
    if (hook) {
        const un = E.find((e) => e.kind === "unhook" && e.t > hook.t);
        if (un) { m.hookTime = +(un.t - hook.t).toFixed(3); m.speedAtUnhook = r4(un.hspeed); let ht = 0; for (const p of T) if (p.t >= hook.t && p.t <= un.t) ht = Math.max(ht, p.speed); m.hookTopSpeed = r4(ht); }
    }
    const lad = pick("ladder");
    if (lad) {
        const offL = E.find((e) => e.kind === "ladderOff" && e.t > lad.t) || end;
        const a = atTime(lad.t), b = atTime(offL.t);
        m.ladderTime = +(offL.t - lad.t).toFixed(3); m.climbHeight = r4(b.pos[1] - a.pos[1]);
        if (m.ladderTime > 0) m.climbRate = r4(m.climbHeight / m.ladderTime);
    }
    // stopping: from the first step that lets go of every key after moving
    if (scenario && scenario.steps) {
        const st = [...scenario.steps].sort((x, y) => x.atMs - y.atMs);
        const rel = st.find((s, i) => i > 0 && !s.forward && !s.side && (st[i - 1].forward || st[i - 1].side));
        if (rel) {
            const t0 = rel.atMs / 1000, a = atTime(t0);
            m.speedAtRelease = r4(a.hspeed);
            const stop = T.find((p) => p.t >= t0 && p.hspeed < 0.05 && p.onGround);
            if (stop) { m.stopTime = +(stop.t - t0).toFixed(3); m.stopDistance = r4(Math.hypot(stop.pos[0] - a.pos[0], stop.pos[2] - a.pos[2])); }
        }
    }
    // the biggest drop: highest point before each landing minus the landing height
    let hi = s0[1], drop = 0, li = 0;
    for (const p of T) { if (p.pos[1] > hi) hi = p.pos[1]; while (li < lands.length && lands[li].t <= p.t) { drop = Math.max(drop, hi - (lands[li].pos ? lands[li].pos[1] : p.pos[1])); hi = p.pos[1]; li++; } }
    if (lands.length) m.fallHeight = r4(drop);
    return m;
}

function sampleAt(tr, t) {
    let lo = 0, hi = tr.length - 1;
    if (t >= tr[hi].t) return tr[hi];
    while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (tr[mid].t <= t) lo = mid; else hi = mid - 1; }
    return tr[lo];
}

// the helper's metrics when it gave any (they are the game's), else the page's
export function mergeMetrics(helper, page) {
    return helper && Object.keys(helper).length ? helper : page;
}
