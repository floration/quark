// The MOCK physics: a JavaScript stand-in for the game's pmove (src/game/pmove.cpp, ported step for step: run,
// friction, air strafing with the A/D cap and boost, air drift and W-only air control, bhop bounce, dash with charges,
// cooldown and recharge, dash jump, super dash, dash wall bounce, grapple, ladders, step-up) over a box world read
// from .qmap text. Used only when quark_maptool isn't built: the badge says MOCK and results are NOT the game's.
// The real numbers come from `quark_maptool physsim` / `physdrive`, which run the game's own pmove().
//
// Units: metres, seconds, y up; yaw / pitch in radians here (the page's scenarios are in degrees: sim.js converts).

import { normalizeCatalog } from "./params.js";
import { resolve, dynamicPlan } from "./expr.js";

export const BTN_JUMP = 1, BTN_CROUCH = 2, BTN_DASH = 1 << 11, BTN_GRAPPLE = 1 << 12;
export const MF_ONGROUND = 1, MF_CROUCHED = 2, MF_JUMPHELD = 4, MF_HOOKHELD = 16, MF_HOOKED = 32, MF_DASHHELD = 64;

const K = {
    gravity: 22, jump: 7.34, ground: 9.5, crouch: 4, groundAccel: 16, airAccel: 2.2, airCap: 2.3, strafeCapAD: 1.1, strafeAccelAD: 1.15,
    overstrafe: 0.35, airControl: 4, airControlFrom: 9.5, airControlW: 0.9, airDrift: 8, strafeBoost: 0.1, friction: 7, stopSpeed: 2,
    step: 0.55, dashSpeed: 24, dashTime: 0.12, dashExitMin: 11, dashKeep: 1, dashExitMax: 32, dashVerticalUp: 0.4, dashVerticalDown: 0.9, dashJumpWindow: 0.1,
    superDashDown: 0.25, superDashBoost: 5, superDashJump: 0.9, superDashMax: 36, dashBounceSpeed: 16,
    dashBounceAngle: 45, dashMaxBounces: 2, bounceGain: 0.5, bounceMax: 8, hookAccel: 38, hookMax: 24, hookGravity: 0.5, hookRelease: 1.2,
    ladderSpeed: 6.75, ladderSide: 4.5, grappleRange: 45, hookCooldown: 1, dashCharges: 2, dashCooldown: 0.6, dashRecharge: 1.2,
    maxCharges: 10, halfWidth: 0.4, height: 1.8, crouchHeight: 1.2, eye: 1.6, crouchEye: 1.05,
};
export const MOCK_CONSTANTS = K;

// ---------------------------------------------------------------- the world: solid boxes in an XZ grid
const NONSOLID = new Set([5, 6, 7, 8]);   // far, water, lava, trim (rungs / rails: decoration here)
export class BoxWorld {
    constructor(map) {
        const n = map.nBoxes, B = map.boxes;
        const idx = [];
        for (let i = 0; i < n; i++) if (!NONSOLID.has(map.mats[i])) idx.push(i);
        this.n = idx.length;
        this.b = new Float64Array(this.n * 6);
        this.rayable = new Uint8Array(this.n);
        idx.forEach((i, k) => { for (let j = 0; j < 6; j++) this.b[k * 6 + j] = B[i * 6 + j]; this.rayable[k] = map.mats[i] !== 9 ? 1 : 0; });
        this.ladders = (map.ladders || []).map((l) => [l.x0, l.y0, l.z0, l.x1, l.y1, l.z1]);
        let x0 = 1e9, z0 = 1e9, x1 = -1e9, z1 = -1e9;
        for (let k = 0; k < this.n; k++) { const o = k * 6; x0 = Math.min(x0, this.b[o]); z0 = Math.min(z0, this.b[o + 2]); x1 = Math.max(x1, this.b[o + 3]); z1 = Math.max(z1, this.b[o + 5]); }
        if (x0 > x1) { x0 = z0 = -10; x1 = z1 = 10; }
        this.cs = 8; this.gx0 = x0; this.gz0 = z0;
        this.gw = Math.max(1, Math.ceil((x1 - x0) / this.cs)); this.gh = Math.max(1, Math.ceil((z1 - z0) / this.cs));
        const cells = Array.from({ length: this.gw * this.gh }, () => []);
        for (let k = 0; k < this.n; k++) {
            const o = k * 6;
            const [a0, a1, c0, c1] = this._range(this.b[o], this.b[o + 3], this.b[o + 2], this.b[o + 5]);
            for (let cz = c0; cz <= c1; cz++) for (let cx = a0; cx <= a1; cx++) cells[cz * this.gw + cx].push(k);
        }
        this.cells = cells.map((c) => Int32Array.from(c));
        this.stamp = new Uint32Array(this.n); this.sv = 1;
    }
    _range(xa, xb, za, zb) {
        const c = (v, n) => Math.max(0, Math.min(n - 1, Math.floor(v)));
        return [c((xa - this.gx0) / this.cs, this.gw), c((xb - this.gx0) / this.cs, this.gw), c((za - this.gz0) / this.cs, this.gh), c((zb - this.gz0) / this.cs, this.gh)];
    }
    // visit every box whose cell overlaps [x0,x1] x [z0,z1] once
    _each(x0, x1, z0, z1, f) {
        const [a0, a1, c0, c1] = this._range(x0, x1, z0, z1);
        const sv = ++this.sv, st = this.stamp;
        if (sv > 4e9) { st.fill(0); this.sv = 1; }
        for (let cz = c0; cz <= c1; cz++) for (let cx = a0; cx <= a1; cx++) {
            const c = this.cells[cz * this.gw + cx];
            for (let i = 0; i < c.length; i++) { const k = c[i]; if (st[k] === sv) continue; st[k] = sv; f(k); }
        }
    }
    sweepAxis(box, axis, delta) {
        if (delta === 0) return 0;
        const a1 = (axis + 1) % 3, a2 = (axis + 2) % 3, eps = 0.001, b = this.b;
        let x0 = box[0], x1 = box[3], z0 = box[2], z1 = box[5];
        if (axis === 0) { if (delta > 0) x1 += delta; else x0 += delta; }
        if (axis === 2) { if (delta > 0) z1 += delta; else z0 += delta; }
        this._each(x0, x1, z0, z1, (k) => {
            const o = k * 6;
            if (box[3 + a1] <= b[o + a1] || box[a1] >= b[o + 3 + a1]) return;
            if (box[3 + a2] <= b[o + a2] || box[a2] >= b[o + 3 + a2]) return;
            if (delta > 0) {
                if (box[3 + axis] <= b[o + axis] + eps) { const al = b[o + axis] - box[3 + axis] - eps; if (al < delta) delta = Math.max(al, 0); }
            } else if (box[axis] >= b[o + 3 + axis] - eps) { const al = b[o + 3 + axis] - box[axis] + eps; if (al > delta) delta = Math.min(al, 0); }
        });
        return delta;
    }
    boxSolid(box) {
        let hit = false; const b = this.b;
        this._each(box[0], box[3], box[2], box[5], (k) => {
            const o = k * 6;
            if (box[0] < b[o + 3] && box[3] > b[o] && box[1] < b[o + 4] && box[4] > b[o + 1] && box[2] < b[o + 5] && box[5] > b[o + 2]) hit = true;
        });
        return hit;
    }
    onLadder(box) {
        for (const l of this.ladders) if (box[0] < l[3] && box[3] > l[0] && box[1] < l[4] && box[4] > l[1] && box[2] < l[5] && box[5] > l[2]) return true;
        return false;
    }
    raycast(o, d, maxT) {
        const ex = o[0] + d[0] * maxT, ez = o[2] + d[2] * maxT;
        let best = -1; const b = this.b;
        this._each(Math.min(o[0], ex), Math.max(o[0], ex), Math.min(o[2], ez), Math.max(o[2], ez), (k) => {
            if (!this.rayable[k]) return;
            const q = k * 6;
            let t0 = 0, t1 = best >= 0 ? best : maxT;
            for (let a = 0; a < 3; a++) {
                if (Math.abs(d[a]) < 1e-12) { if (o[a] < b[q + a] || o[a] > b[q + 3 + a]) return; continue; }
                let ta = (b[q + a] - o[a]) / d[a], tb = (b[q + 3 + a] - o[a]) / d[a];
                if (ta > tb) { const t = ta; ta = tb; tb = t; }
                t0 = Math.max(t0, ta); t1 = Math.min(t1, tb);
                if (t0 > t1) return;
            }
            if (best < 0 || t0 < best) best = t0;
        });
        return best;
    }
}

// ---------------------------------------------------------------- movement
export function newState(start) {
    return {
        pos: [...(start.pos || [0, 0, 0])], vel: [...(start.vel || [0, 0, 0])], flags: start.onGround === false ? 0 : MF_ONGROUND,
        dashCharges: start.dashCharges ?? K.dashCharges, dashCooldownMs: 0, dashRecharging: 0, dashRechargeMs: new Array(K.maxCharges).fill(0),
        dashTimer: 0, dashDirX: 0, dashDirZ: 0, dashUp: 0, dashExitSpeed: 0, dashFromSpeed: 0, jumpBuffer: 0, jumpGrace: 0, dashBounces: 0,
        hookCooldown: 0, hookPoint: [0, 0, 0], onLadder: false, phaseMs: 0,
    };
}
export function cloneState(s) { return { ...s, pos: [...s.pos], vel: [...s.vel], dashRechargeMs: [...s.dashRechargeMs], hookPoint: [...s.hookPoint] }; }

// physics (INTERFACE.md form) -> tuning. Expressions (v2): static ones are worked out here once; dynamic ones (p.*) get
// a plan that pmoveStep runs at the start of every 1 ms step, from the state as the step begins.
let defCat = null;
export function tuningOf(ph, cat) {
    const mt = { gravity: ph.gravity ?? 1, speed: ph.speed ?? 1, jump: ph.jump ?? 1, dash: ph.dash !== false, grapple: ph.grapple !== false, m: flatMove(ph.move) };
    if (ph.expr && Object.keys(ph.expr).length) {
        cat = cat || defCat || (defCat = normalizeCatalog());
        const r = resolve(ph, cat);
        for (const k of r.order) { if (k === "gravity" || k === "speed" || k === "jump") mt[k] = r.S[k]; else mt.m[k] = r.S[k]; }
        const plan = dynamicPlan(ph, cat);
        if (plan) {
            mt.dyn = plan;
            mt.dynInputs = new Set(plan.keys.flatMap((k) => r.info[k].inputs));
            mt.P = { speed: 0, hspeed: 0, vspeed: 0, air: 0, ground: 0, onground: 1, charges: 0, dashing: 0, hooked: 0, height: 0 };
            mt.set = (k, v) => { if (k === "gravity" || k === "speed" || k === "jump") mt[k] = v; else mt.m[k] = v; };
        }
    }
    return mt;
}
// the player's state as pmove's equations see it (pmove.cpp pmoveExpr): p.air / p.ground from the ms since the floor
// flag last changed (MoveState::phaseMs), p.height a hull sweep down (in the air only)
function dynInputs(s, w, mt) {
    const P = mt.P, v = s.vel, ground = (s.flags & MF_ONGROUND) !== 0;
    P.hspeed = Math.hypot(v[0], v[2]); P.speed = Math.hypot(v[0], v[1], v[2]); P.vspeed = v[1];
    P.air = ground ? 0 : (s.phaseMs || 0) / 1000; P.ground = ground ? (s.phaseMs || 0) / 1000 : 0; P.onground = ground ? 1 : 0;
    P.charges = s.dashCharges; P.dashing = s.dashTimer > 0 ? 1 : 0; P.hooked = (s.flags & MF_HOOKED) ? 1 : 0;
    P.height = 0;
    if (!ground && mt.dynInputs.has("height")) P.height = -w.sweepAxis(box(s.pos, false), 1, -50);
    mt.dyn.update(P, mt.set);
}
function flatMove(mv) {
    const keys = ["crouch_speed", "ground_accel", "friction", "stop_speed", "air_accel", "air_cap", "overstrafe", "air_control", "step", "dash_speed",
        "dash_time", "dash_exit_min", "dash_keep", "dash_exit_max", "dash_vertical", "dash_charges", "dash_cooldown", "bounce_gain", "bounce_max",
        "hook_range", "hook_accel", "hook_max", "hook_gravity", "hook_release", "hook_cooldown", "ladder_speed", "ladder_side", "dash_jump_window",
        "super_dash_boost", "super_dash_jump", "super_dash_max", "dash_bounce_angle", "air_drift", "strafe_boost"];
    const o = {};
    for (const k of keys) o[k] = mv && mv[k] != null ? +mv[k] : 1;
    return o;
}

const box = (p, crouched) => { const h = crouched ? K.crouchHeight : K.height, w = K.halfWidth; return [p[0] - w, p[1], p[2] - w, p[0] + w, p[1] + h, p[2] + w]; };
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const maxChargesFor = (mt) => Math.min(K.maxCharges, Math.max(1, Math.round(K.dashCharges * mt.m.dash_charges)));
const cooldownMsFor = (mt) => Math.min(60000, Math.max(0, Math.round(K.dashCooldown * mt.m.dash_cooldown * 1000)));
const rechargeMsFor = (mt) => Math.min(60000, Math.max(1, Math.round(K.dashRecharge * mt.m.dash_cooldown * 1000)));
const dashBurst = (s, mt) => (s.dashBounces > 0 ? s.dashExitSpeed : Math.max(K.dashSpeed * mt.m.dash_speed, s.dashExitSpeed));
function refillCharges(s, max, target) {
    const ready = max - s.dashCharges - s.dashRecharging;
    const add = Math.min(Math.min(target, max) - s.dashCharges, ready);
    if (add > 0) s.dashCharges += add;
}
function setCharges(s, max, n) { if (s.dashCharges > n) s.dashCharges = n; else refillCharges(s, max, n); }

function slideMove(s, w, d, crouched) {
    let blocked = 0;
    for (const i of [1, 0, 2]) {
        const b = box(s.pos, crouched);
        const m = w.sweepAxis(b, i, d[i]);
        if (Math.abs(m - d[i]) > 1e-5) blocked |= 1 << i;
        s.pos[i] += m;
    }
    return blocked;
}

function airAccelerate(mt, vel, wd, wishSpeed, accel, dt, side, cross) {
    const cur = vel[0] * wd[0] + vel[2] * wd[2];
    const add = wishSpeed - cur;
    if (add <= 0) return;
    const h = Math.hypot(vel[0], vel[2]);
    if (side !== 0) { const t = clamp((h - cross) / (0.25 * cross), 0, 1); accel *= K.strafeAccelAD * (1 + K.strafeBoost * mt.m.strafe_boost * t); }
    let px = wd[0] * Math.min(accel * wishSpeed * dt, add), pz = wd[2] * Math.min(accel * wishSpeed * dt, add);
    if (h > 1e-3) {
        const ax = vel[0] / h, az = vel[2] / h, pa = px * ax + pz * az;
        if (pa < 0) { const k = pa * (1 - K.overstrafe * mt.m.overstrafe); px -= ax * k; pz -= az * k; }
    }
    vel[0] += px; vel[2] += pz;
}

// One 1 ms step (pmoveStep). ev(kind, data) reports events.
export function pmoveStep(s, cmd, w, dt, mt, ev) {
    if (mt.dyn) dynInputs(s, w, mt);
    const sy = Math.sin(cmd.yaw), cy = Math.cos(cmd.yaw);
    const fwd = [-sy, 0, -cy], right = [cy, 0, -sy];
    const crouched = false;   // (the game ignores crouch: pmove.cpp wantCrouch = false)
    let wx = fwd[0] * (cmd.forward / 127) + right[0] * (cmd.side / 127), wz = fwd[2] * (cmd.forward / 127) + right[2] * (cmd.side / 127);
    const wishLen = Math.hypot(wx, wz);
    const wd = wishLen > 0 ? [wx / wishLen, 0, wz / wishLen] : [0, 0, 0];
    const jumpSpeed = K.jump * mt.jump;
    const maxSpeed = K.ground * mt.speed;
    const wishSpeed = Math.min(wishLen, 1) * maxSpeed;
    let onGround = (s.flags & MF_ONGROUND) !== 0;
    const jumpHeld = (cmd.buttons & BTN_JUMP) !== 0;
    const grounded = onGround;
    const dashActive = s.dashTimer > 0;
    const freshJump = jumpHeld && !(s.flags & MF_JUMPHELD);
    const jumpWindow = K.dashJumpWindow * mt.m.dash_jump_window;
    if (freshJump) s.jumpBuffer = jumpWindow;
    else if (!dashActive) s.jumpBuffer = Math.max(0, s.jumpBuffer - dt);
    s.jumpGrace = Math.max(0, s.jumpGrace - dt);
    if (onGround && jumpHeld && !dashActive) {
        s.vel[1] = jumpSpeed; onGround = false;
        if (freshJump) s.jumpGrace = jumpWindow;
        ev("jump", { bhop: !freshJump, vy: jumpSpeed });
    }
    s.hookCooldown = Math.max(0, s.hookCooldown - dt);
    const stepMs = Math.max(1, Math.round(dt * 1000));
    if (!dashActive) s.dashCooldownMs = Math.max(0, s.dashCooldownMs - stepMs);
    {
        const n = Math.min(s.dashRecharging, K.maxCharges);
        let k = 0;
        for (let i = 0; i < n; i++) {
            let v = s.dashRechargeMs[i];
            if (!(dashActive && i === n - 1)) v = Math.max(0, v - stepMs);
            if (v > 0) s.dashRechargeMs[k++] = v;
        }
        for (let i = k; i < K.maxCharges; i++) s.dashRechargeMs[i] = 0;
        s.dashRecharging = k;
    }
    const maxCharges = maxChargesFor(mt);
    if (s.dashCharges > maxCharges) s.dashCharges = maxCharges;
    while (s.dashRecharging > 0 && s.dashCharges + s.dashRecharging > maxCharges) {
        for (let i = 1; i < s.dashRecharging; i++) s.dashRechargeMs[i - 1] = s.dashRechargeMs[i];
        s.dashRechargeMs[--s.dashRecharging] = 0;
    }
    const dashHeld = (cmd.buttons & BTN_DASH) !== 0 && mt.dash;
    const dashStart = dashHeld && !(s.flags & MF_DASHHELD) && s.dashCharges > 0 && s.dashCooldownMs === 0 && !dashActive;
    let chargesSet = false;
    if (dashStart) {
        const d = wishLen > 0 ? wd : fwd;
        const h = Math.hypot(s.vel[0], s.vel[2]);
        s.dashDirX = d[0]; s.dashDirZ = d[2];
        s.dashFromSpeed = h;
        s.dashExitSpeed = Math.min(K.dashExitMax * mt.m.dash_exit_max, Math.max(K.dashExitMin * mt.m.dash_exit_min, h + K.dashKeep * mt.m.dash_keep));
        s.dashUp = Math.sin(cmd.pitch) * ((cmd.pitch > 0 ? K.dashVerticalUp : K.dashVerticalDown) * mt.m.dash_vertical);
        s.dashTimer = K.dashTime * mt.m.dash_time;
        s.dashBounces = 0;
        s.dashCharges--;
        if (s.dashRecharging < K.maxCharges) s.dashRechargeMs[s.dashRecharging++] = rechargeMsFor(mt);
        ev("dash", { charges: s.dashCharges });
    }
    let dashing = s.dashTimer > 0;
    if (dashing) {
        const burst = dashBurst(s, mt);
        const down = s.dashUp <= -K.superDashDown * (K.dashVerticalDown * mt.m.dash_vertical);
        const wantJump = jumpHeld || s.jumpBuffer > 0;
        const graceJump = dashStart && !grounded && s.jumpGrace > 0 && !down;
        if ((grounded && wantJump) || graceJump) {
            let sp = burst, vy = graceJump ? s.vel[1] : jumpSpeed, level = 1, tech = "dash jump";
            if (grounded && down) {
                const cap = K.superDashMax * mt.m.super_dash_max;
                sp = Math.min(Math.max(s.dashExitSpeed, s.dashFromSpeed + K.superDashBoost * mt.m.super_dash_boost), Math.max(cap, s.dashExitSpeed));
                const up = Math.min(Math.max(0, -s.dashUp) * mt.m.super_dash_jump, Math.min(0.99, K.superDashJump * mt.m.super_dash_jump));
                vy = sp * up;
                level = Math.sqrt(1 - up * up);
                setCharges(s, maxCharges, 1);
                tech = "super dash";
            } else {
                if (s.dashRecharging > 0) s.dashRechargeMs[--s.dashRecharging] = 0;
                refillCharges(s, maxCharges, maxCharges);
            }
            chargesSet = true;
            s.vel = [s.dashDirX * sp * level, vy, s.dashDirZ * sp * level];
            s.dashTimer = 0; s.jumpBuffer = 0; s.jumpGrace = 0;
            dashing = false; onGround = false;
            ev(tech === "super dash" ? "superDash" : "dashJump", { speed: sp, vy });
        }
    }
    if (dashing) {
        const hs = Math.sqrt(Math.max(0, 1 - s.dashUp * s.dashUp));
        const burst = dashBurst(s, mt);
        s.vel = [s.dashDirX * burst * hs, s.dashUp * burst, s.dashDirZ * burst * hs];
        onGround = false;
        s.dashTimer -= dt;
        if (s.dashTimer <= 0) {
            s.dashTimer = 0;
            s.vel[0] = s.dashDirX * s.dashExitSpeed * hs; s.vel[2] = s.dashDirZ * s.dashExitSpeed * hs; s.vel[1] = s.dashUp * s.dashExitSpeed;
            ev("dashEnd", { speed: s.dashExitSpeed });
        }
    }
    // grapple
    const hookHeld = (cmd.buttons & BTN_GRAPPLE) !== 0 && mt.grapple;
    const wasHooked = (s.flags & MF_HOOKED) !== 0;
    if (!hookHeld) s.flags &= ~MF_HOOKHELD;
    if (hookHeld && !(s.flags & MF_HOOKHELD) && !(s.flags & MF_HOOKED) && s.hookCooldown <= 0) {
        const eye = [s.pos[0], s.pos[1] + K.eye, s.pos[2]];
        const cp = Math.cos(cmd.pitch), dir = [-sy * cp, Math.sin(cmd.pitch), -cy * cp];
        const t = w.raycast(eye, dir, K.grappleRange * mt.m.hook_range);
        if (t >= 0) { s.hookPoint = [eye[0] + dir[0] * t, eye[1] + dir[1] * t, eye[2] + dir[2] * t]; s.flags |= MF_HOOKED | MF_HOOKHELD; ev("hook", { point: [...s.hookPoint] }); }
    }
    if (!hookHeld) s.flags &= ~MF_HOOKED;
    let hooked = (s.flags & MF_HOOKED) !== 0;
    if (hooked) {
        const to = [s.hookPoint[0] - s.pos[0], s.hookPoint[1] - (s.pos[1] + 1), s.hookPoint[2] - s.pos[2]];
        const dist = Math.hypot(to[0], to[1], to[2]);
        if (dist < K.hookRelease * mt.m.hook_release) { s.flags &= ~MF_HOOKED; hooked = false; }
        else if (!dashing) {
            const before = Math.hypot(...s.vel), a = K.hookAccel * mt.m.hook_accel * dt / dist;
            s.vel[0] += to[0] * a; s.vel[1] += to[1] * a; s.vel[2] += to[2] * a;
            const sp = Math.hypot(...s.vel), cap = Math.max(K.hookMax * mt.m.hook_max, before);
            if (sp > cap) { const k = cap / sp; s.vel[0] *= k; s.vel[1] *= k; s.vel[2] *= k; }
            onGround = false;
        }
    }
    if (wasHooked && !(s.flags & MF_HOOKED)) { s.hookCooldown = K.hookCooldown * mt.m.hook_cooldown; ev("unhook", {}); }
    // ladders, ground, air
    const onLadder = !hooked && !dashing && w.onLadder(box(s.pos, crouched));
    if (onLadder && !s.onLadder) ev("ladder", {});
    if (!onLadder && s.onLadder) ev("ladderOff", {});
    s.onLadder = onLadder;
    if (onLadder) {
        const jumpPress = jumpHeld && !(s.flags & MF_JUMPHELD);
        if (jumpPress) s.vel = [fwd[0] * -4, jumpSpeed * 0.7, fwd[2] * -4];
        else {
            const climb = Math.min(wishLen, 1) * (K.ladderSpeed * mt.m.ladder_speed) * (cmd.pitch < -0.45 ? -1 : 1);
            const side = Math.min(wishSpeed, K.ladderSide * mt.m.ladder_side);
            s.vel = [wd[0] * side, wishLen > 0 ? climb : 0, wd[2] * side];
        }
        onGround = false;
    } else if (onGround) {
        const sp = Math.hypot(s.vel[0], s.vel[2]);
        if (sp > 0) {
            const drop = Math.max(sp, K.stopSpeed * mt.m.stop_speed) * (K.friction * mt.m.friction) * dt;
            const ns = Math.max(sp - drop, 0) / sp;
            s.vel[0] *= ns; s.vel[2] *= ns;
        }
        const cur = s.vel[0] * wd[0] + s.vel[2] * wd[2], add = wishSpeed - cur;
        if (add > 0) { const a = Math.min(K.groundAccel * mt.m.ground_accel * wishSpeed * dt, add); s.vel[0] += wd[0] * a; s.vel[2] += wd[2] * a; }
        s.vel[1] = 0;
    } else if (!dashing) {
        const airAccel = K.airAccel * mt.m.air_accel * 10, airCap = K.airCap * mt.m.air_cap, airControl = K.airControl * mt.m.air_control;
        const cross = airAccel * airCap / airControl;
        const strafeCap = cmd.side !== 0 ? airCap * K.strafeCapAD : airCap;
        airAccelerate(mt, s.vel, wd, Math.min(wishSpeed, strafeCap), airAccel, dt, cmd.side, cross);
        const runSpeed = K.ground * mt.speed;
        let h = Math.hypot(s.vel[0], s.vel[2]);
        if (wishLen > 0 && h < runSpeed) {
            const d = s.vel[0] * wd[0] + s.vel[2] * wd[2];
            let a = Math.min(K.airDrift * mt.m.air_drift * Math.min(wishLen, 1) * dt, runSpeed - d);
            a = Math.min(a, -d + Math.sqrt(Math.max(0, d * d + runSpeed * runSpeed - h * h)));
            if (a > 0) { s.vel[0] += wd[0] * a; s.vel[2] += wd[2] * a; }
            h = Math.hypot(s.vel[0], s.vel[2]);
        }
        if (wishLen > 0 && cmd.side === 0 && h > 0.5) {
            const rate = K.airControlW * Math.min(airControl, airControl * K.airControlFrom * mt.speed / Math.max(h, 1e-3));
            const k = Math.min(1, rate * dt);
            const nx = s.vel[0] / h + (wd[0] - s.vel[0] / h) * k, nz = s.vel[2] / h + (wd[2] - s.vel[2] / h) * k;
            const nl = Math.hypot(nx, nz);
            if (nl > 1e-4) { s.vel[0] = nx / nl * h; s.vel[2] = nz / nl * h; }
        }
        s.vel[1] -= K.gravity * mt.gravity * dt * (hooked ? K.hookGravity * mt.m.hook_gravity : 1);
    }
    const delta = [s.vel[0] * dt, s.vel[1] * dt, s.vel[2] * dt];
    const before = cloneState(s);
    let blocked = slideMove(s, w, delta, crouched);
    const airStep = !onGround;
    if (blocked & 0b101) {
        const st = cloneState(before);
        const up = w.sweepAxis(box(st.pos, crouched), 1, K.step * mt.m.step);
        st.pos[1] += up;
        const sb = slideMove(st, w, [delta[0], 0, delta[2]], crouched);
        const rest = airStep ? delta[1] - up : -up;
        st.pos[1] += w.sweepAxis(box(st.pos, crouched), 1, rest);
        const d0 = (s.pos[0] - before.pos[0]) ** 2 + (s.pos[2] - before.pos[2]) ** 2;
        const d1 = (st.pos[0] - before.pos[0]) ** 2 + (st.pos[2] - before.pos[2]) ** 2;
        if (d1 > d0 + 1e-6 || (!(sb & 0b101) && d1 >= d0)) {
            if (st.pos[1] - s.pos[1] > 0.05) ev("step", { up: st.pos[1] - s.pos[1] });
            s.pos = st.pos; blocked &= ~0b101;
        }
    }
    if (dashing && (blocked & 0b101) && s.dashBounces < K.dashMaxBounces) {
        const c = Math.cos(K.dashBounceAngle * mt.m.dash_bounce_angle * Math.PI / 180);
        const bx = (blocked & 1) && Math.abs(s.dashDirX) > c, bz = !bx && (blocked & 4) && Math.abs(s.dashDirZ) > c;
        if (bx || bz) {
            if (bx) { s.dashDirX = -s.dashDirX; blocked &= ~1; } else { s.dashDirZ = -s.dashDirZ; blocked &= ~4; }
            s.dashBounces++;
            setCharges(s, maxCharges, 1);
            chargesSet = true;
            const bounce = K.dashBounceSpeed * mt.m.dash_speed, hs = Math.sqrt(Math.max(0, 1 - s.dashUp * s.dashUp));
            s.dashExitSpeed = bounce; s.dashFromSpeed = bounce;
            s.vel[0] = s.dashDirX * bounce * hs; s.vel[2] = s.dashDirZ * bounce * hs;
            ev("bounce", { speed: bounce, bounces: s.dashBounces });
        }
    }
    if (blocked & 1) s.vel[0] = 0;
    if (blocked & 4) s.vel[2] = 0;
    if ((blocked & 2) && s.vel[1] > 0) s.vel[1] = 0;
    const feet = box(s.pos, crouched);
    const canLand = s.vel[1] <= 0.01;
    const probe = canLand ? w.sweepAxis(feet, 1, -0.05) : 0;
    let nowGround = canLand && probe > -0.049, bounced = false;
    if (nowGround) {
        s.pos[1] += probe;
        const impact = -before.vel[1];
        if (jumpHeld && impact > jumpSpeed && !onLadder && !dashing) {
            s.vel[1] = jumpSpeed + Math.min((impact - jumpSpeed) * (K.bounceGain * mt.m.bounce_gain), K.bounceMax * mt.m.bounce_max);
            nowGround = false; bounced = true;
            ev("jump", { bhop: true, bounce: true, impact, vy: s.vel[1] });
        } else if (s.vel[1] < 0) s.vel[1] = 0;
    } else if (onGround && s.vel[1] <= 0) {
        const down = w.sweepAxis(feet, 1, -(K.step * mt.m.step));
        if (down > -(K.step * mt.m.step) + 0.001) { s.pos[1] += down; nowGround = true; s.vel[1] = 0; }
    }
    if ((dashActive || dashStart) && s.dashTimer <= 0) s.dashCooldownMs = cooldownMsFor(mt);
    const wasOn = (before.flags & MF_ONGROUND) !== 0;
    s.flags &= MF_HOOKED;
    if (nowGround && !(s.flags & MF_HOOKED)) s.flags |= MF_ONGROUND;
    if (!chargesSet && !dashing && !onLadder) {
        if (bounced || (jumpHeld && (s.flags & MF_ONGROUND))) refillCharges(s, maxCharges, 1);
        else if (s.flags & MF_ONGROUND) refillCharges(s, maxCharges, maxCharges);
    }
    if (((s.flags & MF_ONGROUND) && !wasOn) || bounced) ev("land", { impact: -before.vel[1], hspeed: Math.hypot(before.vel[0], before.vel[2]) });
    if (jumpHeld) s.flags |= MF_JUMPHELD;
    if (dashHeld) s.flags |= MF_DASHHELD;
    if (mt.dyn) s.phaseMs = ((s.flags & MF_ONGROUND) !== 0) !== wasOn ? 0 : Math.min(65535, (s.phaseMs || 0) + 1);
    if (s.pos[1] < -100) s.pos[1] = -100;
}

export function pmove(s, cmd, w, mt, ev) {
    const n = Math.max(1, Math.min(8, cmd.msec | 0 || 1));
    for (let i = 0; i < n; i++) pmoveStep(s, cmd, w, 0.001, mt, ev);
}
