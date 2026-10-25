// A stand-in for `quark_maptool bots --stream` until the real helper is there: a small, seeded, deterministic
// simulation that writes the same JSON lines (see helper.js for the schema). It is NOT the game's bots: it walks a
// 1 m layered walk grid built from the map's boxes, sees through a coarse occupancy test, and fights with a few of
// the brain's numbers (fov, reactMs, memory, strafe, trackBase, fireCone, hearGun) so that brain edits show *some*
// effect. Everything the page shows from a mock run is labelled MOCK.

import { floorAt, mapObjectives, TEAM_MODES } from "./qmap.js";

const CELL = 1.0, CLEAR = 1.8, STEP = 0.65, DROP = 6.0;
const WEAPONS = ["rifle", "rocket", "shotgun", "laser", "sniper", "crossbow", "sticky"];

export function rng(seed) {   // xorshift32, as the game's bots (bots.cpp rnd)
    let s = (seed >>> 0) || 0x9e3779b9;
    const next = () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
    next.gauss = () => { const u = Math.max(1e-9, next()), v = next(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(6.283185307 * v); };
    return next;
}

// ---- walk grid: per 1 m cell, the merged solid intervals and the standable surfaces on them
export class WalkGrid {
    constructor(map) {
        const b = map.bounds;
        this.x0 = Math.floor(b.x0) - 1; this.z0 = Math.floor(b.z0) - 1;
        this.W = Math.ceil(b.x1 - this.x0) + 2; this.H = Math.ceil(b.z1 - this.z0) + 2;
        const N = this.W * this.H;
        const lists = new Array(N);
        const B = map.boxes;
        for (let i = 0; i < map.nBoxes; i++) {
            const mt = map.mats[i];
            if (mt === 5 || mt === 8 || mt === 6) continue;     // far, trim, water: not solid for walking
            const o = i * 6;
            const lava = mt === 7;
            const cx0 = Math.max(0, Math.floor((B[o] - this.x0) / CELL + 0.25)), cx1 = Math.min(this.W - 1, Math.ceil((B[o + 3] - this.x0) / CELL - 0.25) - 1);
            const cz0 = Math.max(0, Math.floor((B[o + 2] - this.z0) / CELL + 0.25)), cz1 = Math.min(this.H - 1, Math.ceil((B[o + 5] - this.z0) / CELL - 0.25) - 1);
            for (let z = cz0; z <= cz1; z++) for (let x = cx0; x <= cx1; x++) {
                const c = z * this.W + x;
                (lists[c] || (lists[c] = [])).push(B[o + 1], B[o + 4], lava ? 1 : 0);
            }
        }
        // merge intervals; surfaces = interval tops with CLEAR head room, not lava
        this.solidStart = new Int32Array(N + 1);
        const solids = [], surfStart = new Int32Array(N + 1), surfs = [];
        for (let c = 0; c < N; c++) {
            this.solidStart[c] = solids.length / 2;
            surfStart[c] = surfs.length;
            const L = lists[c];
            if (!L) continue;
            const iv = [];
            for (let k = 0; k < L.length; k += 3) iv.push([L[k], L[k + 1], L[k + 2]]);
            iv.sort((a, b) => a[0] - b[0]);
            const merged = [];
            for (const v of iv) {
                const last = merged[merged.length - 1];
                if (last && v[0] <= last[1] + 0.05) { if (v[1] >= last[1]) { last[1] = v[1]; last[2] = v[2]; } }
                else merged.push(v.slice());
            }
            for (let k = 0; k < merged.length; k++) {
                solids.push(merged[k][0], merged[k][1]);
                const nextBottom = k + 1 < merged.length ? merged[k + 1][0] : 1e9;
                if (!merged[k][2] && nextBottom - merged[k][1] >= CLEAR) surfs.push(merged[k][1]);
            }
        }
        this.solidStart[N] = solids.length / 2;
        surfStart[N] = surfs.length;
        this.solids = new Float32Array(solids);
        this.surfStart = surfStart;
        this.surfY = new Float32Array(surfs);
        this.nNodes = surfs.length;
        this.nodeCell = new Int32Array(this.nNodes);
        for (let c = 0; c < N; c++) for (let k = surfStart[c]; k < surfStart[c + 1]; k++) this.nodeCell[k] = c;
        this.ladders = map.ladders;
        // A* scratch (reused: no allocation per search)
        this.g = new Float32Array(this.nNodes);
        this.from = new Int32Array(this.nNodes);
        this.stamp = new Uint32Array(this.nNodes);
        this.closed = new Uint32Array(this.nNodes);
        this.gen = 0;
        this.heap = new Int32Array(Math.max(16, this.nNodes * 2));
        this.heapF = new Float32Array(Math.max(16, this.nNodes * 2));
    }
    cellOf(x, z) {
        const cx = Math.floor((x - this.x0) / CELL), cz = Math.floor((z - this.z0) / CELL);
        if (cx < 0 || cz < 0 || cx >= this.W || cz >= this.H) return -1;
        return cz * this.W + cx;
    }
    // the surface node nearest below y + STEP (else the closest) in the cell at (x, z), searching nearby cells
    nodeAt(x, z, y, R = 4) {
        let best = -1, bd = 1e9;
        for (let dz = -R; dz <= R; dz++) for (let dx = -R; dx <= R; dx++) {
            const c = this.cellOf(x + dx * CELL, z + dz * CELL);
            if (c < 0) continue;
            const h = Math.hypot(dx, dz) * CELL;
            if (h >= bd) continue;
            for (let k = this.surfStart[c]; k < this.surfStart[c + 1]; k++) {
                const sy = this.surfY[k];
                const d = h + (sy <= y + STEP ? (y - sy) * 0.5 : (sy - y) * 4 + 10);
                if (d < bd) { bd = d; best = k; }
            }
        }
        return best;
    }
    nodePos(n) {
        const c = this.nodeCell[n];
        return { x: this.x0 + (c % this.W + 0.5) * CELL, y: this.surfY[n], z: this.z0 + (Math.floor(c / this.W) + 0.5) * CELL };
    }
    solidAt(x, y, z) {
        const c = this.cellOf(x, z);
        if (c < 0) return false;
        for (let k = this.solidStart[c]; k < this.solidStart[c + 1]; k++) if (y > this.solids[2 * k] && y < this.solids[2 * k + 1]) return true;
        return false;
    }
    los(a, b) {
        const dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z, d = Math.hypot(dx, dy, dz);
        const n = Math.ceil(d / 0.6);
        for (let i = 1; i < n; i++) { const t = i / n; if (this.solidAt(a.x + dx * t, a.y + dy * t, a.z + dz * t)) return false; }
        return true;
    }
    _onLadder(c, y) {
        if (!this.ladders.length) return null;
        const x = this.x0 + (c % this.W + 0.5) * CELL, z = this.z0 + (Math.floor(c / this.W) + 0.5) * CELL;
        for (const l of this.ladders) if (x > l.x0 - 1 && x < l.x1 + 1 && z > l.z0 - 1 && z < l.z1 + 1 && y > l.y0 - 1 && y < l.y1 + 1) return l;
        return null;
    }
    // A* from node a to node b: an array of node ids (empty: no path). Max `limit` expansions.
    path(a, b, limit = 60000) {
        if (a < 0 || b < 0) return [];
        const gen = ++this.gen, W = this.W;
        const g = this.g, from = this.from, stamp = this.stamp, closed = this.closed, heap = this.heap, heapF = this.heapF;
        let hn = 0;
        const bp = this.nodePos(b);
        const push = (n, f) => {
            let i = hn++;
            while (i > 0) { const p = (i - 1) >> 1; if (heapF[p] <= f) break; heap[i] = heap[p]; heapF[i] = heapF[p]; i = p; }
            heap[i] = n; heapF[i] = f;
        };
        const pop = () => {
            const top = heap[0], ln = heap[--hn], lf = heapF[hn];
            let i = 0;
            for (;;) {
                let c = 2 * i + 1;
                if (c >= hn) break;
                if (c + 1 < hn && heapF[c + 1] < heapF[c]) c++;
                if (heapF[c] >= lf) break;
                heap[i] = heap[c]; heapF[i] = heapF[c]; i = c;
            }
            heap[i] = ln; heapF[i] = lf;
            return top;
        };
        stamp[a] = gen; g[a] = 0; from[a] = -1; push(a, 0);
        let found = false, iters = 0;
        while (hn > 0 && iters++ < limit) {
            const n = pop();
            if (closed[n] === gen) continue;
            closed[n] = gen;
            if (n === b) { found = true; break; }
            const c = this.nodeCell[n], y = this.surfY[n], cx = c % W, cz = (c - cx) / W;
            const relax = (m, cost) => {
                if (closed[m] === gen) return;
                const ng = g[n] + cost;
                if (stamp[m] !== gen || ng < g[m]) {
                    stamp[m] = gen; g[m] = ng; from[m] = n;
                    const p = this.nodePos(m);
                    push(m, ng + Math.hypot(p.x - bp.x, p.z - bp.z) + Math.abs(p.y - bp.y) * 0.5);
                }
            };
            for (let dz = -1; dz <= 1; dz++) for (let dx = -1; dx <= 1; dx++) {
                if (!dx && !dz) continue;
                const nx = cx + dx, nz = cz + dz;
                if (nx < 0 || nz < 0 || nx >= W || nz >= this.H) continue;
                const nc = nz * W + nx;
                let best = -1, by = -1e9;
                for (let k = this.surfStart[nc]; k < this.surfStart[nc + 1]; k++) {
                    const sy = this.surfY[k];
                    if (sy <= y + STEP && sy >= y - DROP && sy > by) { by = sy; best = k; }
                }
                if (best < 0) continue;
                if (dx && dz) {   // no corner cutting through walls
                    const c1 = cz * W + nx, c2 = nz * W + cx;
                    if (!this._hasSurfNear(c1, y) || !this._hasSurfNear(c2, y)) continue;
                }
                relax(best, (dx && dz ? 1.414 : 1) + Math.max(0, by - y) * 0.5 + Math.max(0, y - by - 1) * 0.3);
            }
            const l = this._onLadder(c, y);
            if (l) for (let k = this.surfStart[c]; k < this.surfStart[c + 1]; k++) {
                const sy = this.surfY[k];
                if (k !== n && sy > y && sy <= l.y1 + 0.5) relax(k, (sy - y) * 1.5);
            }
        }
        if (!found) return [];
        const out = [];
        for (let n = b; n >= 0; n = from[n]) out.push(n);
        return out.reverse();
    }
    _hasSurfNear(c, y) {
        for (let k = this.surfStart[c]; k < this.surfStart[c + 1]; k++) if (Math.abs(this.surfY[k] - y) <= STEP + 0.1) return true;
        return false;
    }
}

// ---- the simulation
const SKILLS = ["BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"];
const DEFAULT_PARAMS = { fov: [1.55, 1.7, 1.85, 2.0], reactMs: [450, 300, 200, 150], memory: [1.5, 2.5, 3.5, 5.0], strafe: [0.35, 0.7, 1, 1],
    trackBase: [0.1, 0.06, 0.045, 0.03], fireCone: [0.15, 0.1, 0.07, 0.04], hearGun: [45, 55, 65, 75], jukeRate: [1, 1.4, 2, 2.6] };

export class MockSim {
    // req: the run request (helper.js); paramsOf(brainName) -> {name: [4 values]} for the numbers above
    constructor(map, req, paramsOf) {
        this.map = map;
        this.req = req;
        this.mode = req.mode || "ffa";
        this.teams = TEAM_MODES.has(this.mode);
        this.rnd = rng((req.seed || 1) * 2654435761);
        this.grid = map._walk || (map._walk = new WalkGrid(map));
        this.t = 0;
        this.dt = 1 / 20;
        this.secs = req.secs || 120;
        this.objectives = mapObjectives(map);
        this.events = [];
        this.bots = (req.bots || []).map((b, i) => this._bot(b, i, paramsOf));
        this.over = false;
    }
    _p(bot, name) { const v = bot.params[name] ?? DEFAULT_PARAMS[name]; return Array.isArray(v) ? v[bot.skillIx] : v; }
    _bot(b, i, paramsOf) {
        const skillIx = Math.max(0, SKILLS.indexOf((b.skill || "ADVANCED").toUpperCase()));
        const params = Object.assign({}, DEFAULT_PARAMS, (paramsOf && paramsOf(b.brain || "default")) || {});
        const team = this.teams ? (b.team === "b" ? "b" : "a") : "any";
        const bot = { id: b.id ?? i, name: b.name || "BOT" + i, team, skillIx, skill: SKILLS[skillIx], brain: b.brain || "default", params,
            start: b.pos ? { x: b.pos[0], y: b.pos[1], z: b.pos[2], yaw: (b.yaw || 0) * Math.PI / 180 } : null,
            objective: b.objective || { kind: "default" },
            pos: { x: 0, y: 0, z: 0 }, vel: { x: 0, y: 0, z: 0 }, yaw: 0, pitch: 0, health: 100, alive: false, respawnAt: 0,
            weapon: "rifle", target: -1, seenAt: {}, reactAt: 0, lastSeen: null, lastSeenT: -99, path: [], pathIx: 0, goal: null, goalUntil: 0,
            repathAt: 0, strafeDir: 0, strafeUntil: 0, state: "spawn", decision: "", kills: 0, deaths: 0, damage: 0, shots: 0, hits: 0,
            aimErr: 0, followIx: 0, nextFire: 0, objTime: 0 };
        this._spawn(bot, true);
        return bot;
    }
    _spawn(bot, first) {
        let s = first && bot.start;
        if (!s) {
            const sp = this.map.spawns.filter((p) => !this.teams || p.team === bot.team || p.team === "any");
            const pool = sp.length ? sp : this.map.spawns;
            const p = pool[Math.floor(this.rnd() * pool.length)] || { x: 0, y: 0, z: 0, yaw: 0 };
            s = { x: p.x, y: p.y, z: p.z, yaw: p.yaw };
        }
        bot.pos = { x: s.x, y: floorAt(this.map, s.x, s.z, s.y + 1.0), z: s.z };
        bot.yaw = s.yaw || 0; bot.health = 100; bot.alive = true; bot.target = -1; bot.path = []; bot.goal = null; bot.goalUntil = 0;
        bot.state = "spawn"; bot.decision = "spawned";
        this.events.push({ type: "spawn", id: bot.id, pos: [bot.pos.x, bot.pos.y, bot.pos.z] });
    }
    _enemies(bot) { return this.bots.filter((o) => o !== bot && o.alive && (!this.teams || o.team !== bot.team)); }

    // the goal: per-bot override first, else the mode's objective, else roam / hunt
    _objectiveGoal(bot) {
        const o = bot.objective || { kind: "default" };
        const P = (p) => ({ x: p[0], y: p[1] ?? floorAt(this.map, p[0], p[2]), z: p[2] });
        if (o.kind === "goto" && o.pos) return { pos: P(o.pos), label: "override: goto", hold: 0.5 };
        if (o.kind === "hold" && o.pos) {
            const r = o.radius || 4, a = this.rnd() * 6.283, d = Math.sqrt(this.rnd()) * r;
            return { pos: { x: o.pos[0] + Math.cos(a) * d, y: o.pos[1] ?? 0, z: o.pos[2] + Math.sin(a) * d }, label: `override: hold r=${r}`, hold: 3 };
        }
        if (o.kind === "follow" && o.points && o.points.length) {
            const d = bot.goal && Math.hypot(bot.goal.x - bot.pos.x, bot.goal.z - bot.pos.z);
            if (d !== null && d < 1.5) bot.followIx = (bot.followIx + 1) % o.points.length;
            return { pos: P(o.points[bot.followIx % o.points.length]), label: `override: waypoint ${bot.followIx % o.points.length + 1}/${o.points.length}`, hold: 0 };
        }
        if (o.kind === "objective" && o.target) {
            const ob = this.objectives.find((x) => x.kind === o.target.kind && x.index === o.target.index);
            if (ob) return { pos: { x: ob.x, y: ob.y, z: ob.z }, label: `override: ${o.role || "attack"} ${ob.label}`, hold: o.role === "defend" ? 6 : 1 };
        }
        const m = this.map, mode = this.mode;
        const pick = (kind, i = 0) => this.objectives.find((x) => x.kind === kind && x.index === i);
        let ob = null;
        if (mode === "koth") ob = pick("hill");
        else if (mode === "bomb" && m.sites.length) ob = pick("site", bot.team === "a" ? 0 : bot.id % m.sites.length);
        else if (mode === "points" && m.points.length) ob = pick("point", Math.min(m.points.length - 1, Math.floor(this.t / 60)));
        else if (mode === "race" && m.checkpoints.length) ob = pick("checkpoint", Math.min(m.checkpoints.length - 1, Math.floor(bot.objTime / 8)));
        else if ((mode === "payload" || mode === "npayload") && m.path.length > 1) {
            const f = Math.min(1, this.t / this.secs), p = m.path[Math.min(m.path.length - 1, Math.floor(f * (m.path.length - 1)))];
            return { pos: { x: p.x, y: p.y, z: p.z }, label: `objective.${mode}: cart`, hold: 1 };
        }
        if (ob) return { pos: { x: ob.x, y: ob.y, z: ob.z }, label: `objective.${mode}: ${ob.label}`, hold: 2 };
        if (bot.lastSeen && this.t - bot.lastSeenT < this._p(bot, "memory") * 2) return { pos: bot.lastSeen, label: "objective.default: hunt last seen", hold: 0 };
        const sp = m.spawns[Math.floor(this.rnd() * m.spawns.length)];
        return { pos: { x: sp.x, y: sp.y, z: sp.z }, label: "objective.default: roam to a spawn", hold: 0 };
    }

    _repath(bot, goal) {
        const g = this.grid;
        const a = g.nodeAt(bot.pos.x, bot.pos.z, bot.pos.y), b = g.nodeAt(goal.x, goal.z, goal.y);
        const ids = g.path(a, b);
        bot.path = ids.filter((_, i) => i % 2 === 1 || i === ids.length - 1).map((n) => g.nodePos(n));
        bot.pathIx = 0;
        bot.repathAt = this.t + 1.5 + this.rnd();
    }

    step() {
        if (this.over) return null;
        const dt = this.dt;
        this.t += dt;
        this.events = this.events || [];
        for (const bot of this.bots) {
            if (!bot.alive) { if (this.t >= bot.respawnAt) this._spawn(bot, false); continue; }
            this._think(bot, dt);
        }
        const frame = this._frame();
        if (this.t >= this.secs) { this.over = true; }
        return frame;
    }

    _think(bot, dt) {
        const eye = { x: bot.pos.x, y: bot.pos.y + 1.6, z: bot.pos.z };
        const fov = this._p(bot, "fov");
        // perceive (20 Hz: every step here)
        let best = null, bestScore = 1e9;
        for (const e of this._enemies(bot)) {
            const dx = e.pos.x - bot.pos.x, dz = e.pos.z - bot.pos.z, d = Math.hypot(dx, dz);
            if (d > 150) continue;
            const ang = Math.atan2(-dx, -dz), off = Math.abs(wrap(ang - bot.yaw));
            const heard = e.firedAt && this.t - e.firedAt < 0.2 && d < this._p(bot, "hearGun");
            const vis = off < fov / 2 && this.grid.los(eye, { x: e.pos.x, y: e.pos.y + 1.2, z: e.pos.z });
            if (vis) {
                if (!bot.seenAt[e.id] || this.t - bot.seenAt[e.id] > 0.3) bot.reactAt = this.t + this._p(bot, "reactMs") * 0.001 * (1 + 0.5 * off / (fov / 2)) * (d > 40 ? 1.25 : 1);
                bot.seenAt[e.id] = this.t;
                const score = d + 12 * off - (bot.target === e.id ? 6 : 0);
                if (score < bestScore) { bestScore = score; best = e; }
            } else if (heard && !best) {
                bot.lastSeen = { x: e.pos.x + this.rnd.gauss() * d * 0.15, y: e.pos.y, z: e.pos.z + this.rnd.gauss() * d * 0.15 };
                bot.lastSeenT = this.t;
                bot.decision = "hearing: gunfire, turn to it";
            }
        }
        const T = best && this.t >= bot.reactAt ? best : null;
        bot.target = best ? best.id : -1;
        let moveDir = null, speed = 7.5, wantYaw = bot.yaw;
        if (T) {
            bot.state = "fight";
            bot.lastSeen = { ...T.pos }; bot.lastSeenT = this.t;
            const dx = T.pos.x - bot.pos.x, dz = T.pos.z - bot.pos.z, d = Math.hypot(dx, dz);
            bot.weapon = d < 7 ? "shotgun" : d < 18 ? "rocket" : d < 35 ? "rifle" : "sniper";
            wantYaw = Math.atan2(-dx, -dz);
            // strafe roll (strafe / jukeRate)
            if (this.t >= bot.strafeUntil) {
                bot.strafeDir = this.rnd() < this._p(bot, "strafe") ? (this.rnd() < 0.5 ? -1 : 1) : 0;
                bot.strafeUntil = this.t + 0.15 + this.rnd() * 2.0 / this._p(bot, "jukeRate");
            }
            const ideal = { shotgun: 4, rocket: 14, rifle: 18, sniper: 40 }[bot.weapon];
            const fwd = d > ideal + 3 ? 1 : d < ideal - 3 ? -1 : 0;
            const fx = -Math.sin(wantYaw), fz = -Math.cos(wantYaw);
            moveDir = { x: fx * fwd + -fz * bot.strafeDir, z: fz * fwd + fx * bot.strafeDir };
            speed = 6.5;
            bot.decision = `fight ${T.name}: ${bot.weapon} @ ${d.toFixed(0)} m, strafe ${bot.strafeDir < 0 ? "left" : bot.strafeDir > 0 ? "right" : "none"}`;
            // fire
            const err = Math.abs(wrap(wantYaw - bot.yaw));
            if (err < this._p(bot, "fireCone") + 0.35 / Math.max(d, 1) && this.t >= bot.nextFire) {
                bot.nextFire = this.t + ({ shotgun: 0.9, rocket: 0.8, rifle: 0.12, sniper: 1.4 }[bot.weapon]);
                bot.shots++; bot.firedAt = this.t;
                const spread = this._p(bot, "trackBase") * (1 + Math.hypot(T.vel.x, T.vel.z) * 0.15);
                const hitP = Math.max(0.03, Math.min(0.95, 0.5 / (1 + spread * d * 0.8)));
                const dmg = { shotgun: 60, rocket: 70, rifle: 9, sniper: 80 }[bot.weapon];
                if (this.rnd() < hitP) {
                    bot.hits++;
                    const done = Math.min(T.health, dmg);
                    T.health -= done; bot.damage += done;
                    this.events.push({ type: "hit", attacker: bot.id, victim: T.id, damage: done, weapon: bot.weapon });
                    if (T.health <= 0) {
                        T.alive = false; T.deaths++; bot.kills++; T.respawnAt = this.t + 2.0;
                        this.events.push({ type: "kill", killer: bot.id, victim: T.id, weapon: bot.weapon });
                    }
                }
            }
        } else {
            if (this.t >= bot.goalUntil || !bot.goal) {
                const g = this._objectiveGoal(bot);
                bot.goal = g.pos; bot.goalLabel = g.label; bot.goalHold = g.hold;
                bot.goalUntil = this.t + 8 + this.rnd() * 6;
                this._repath(bot, bot.goal);
            } else if (this.t >= bot.repathAt && bot.path.length === 0) this._repath(bot, bot.goal);
            bot.state = best ? "react" : bot.goalLabel && bot.goalLabel.startsWith("override") ? "override" : "objective";
            bot.decision = best ? `spotted ${best.name}, reacting` : bot.goalLabel;
            const wp = bot.path[bot.pathIx];
            if (wp) {
                const dx = wp.x - bot.pos.x, dz = wp.z - bot.pos.z, d = Math.hypot(dx, dz);
                if (d < 0.7) bot.pathIx++;
                moveDir = { x: dx, z: dz };
                wantYaw = Math.atan2(-dx, -dz);
            } else if (bot.goal) {
                const d = Math.hypot(bot.goal.x - bot.pos.x, bot.goal.z - bot.pos.z);
                if (d >= 2) { moveDir = { x: bot.goal.x - bot.pos.x, z: bot.goal.z - bot.pos.z }; wantYaw = Math.atan2(-moveDir.x, -moveDir.z); }
                if (d < 2) { bot.objTime += dt; if (bot.goalHold === 0) bot.goalUntil = this.t; }
                wantYaw = bot.yaw + Math.sin(this.t * 0.7 + bot.id) * 0.04;   // look around
            }
        }
        // turn (capped hand speed)
        const turn = [4, 6, 9, 13][bot.skillIx] * dt;
        bot.yaw += Math.max(-turn, Math.min(turn, wrap(wantYaw - bot.yaw)));
        bot.yaw = wrap(bot.yaw);
        // move: slide on the walk grid (stay on standable cells)
        if (moveDir) {
            const l = Math.hypot(moveDir.x, moveDir.z) || 1;
            const vx = moveDir.x / l * speed, vz = moveDir.z / l * speed;
            const nx = bot.pos.x + vx * dt, nz = bot.pos.z + vz * dt;
            const n = this.grid.nodeAt(nx, nz, bot.pos.y);
            const np = n >= 0 ? this.grid.nodePos(n) : null;
            if (np && np.y <= bot.pos.y + STEP + 0.5 && Math.abs(np.x - nx) < 1.2 && Math.abs(np.z - nz) < 1.2) {
                bot.vel = { x: vx, y: (np.y - bot.pos.y) / dt, z: vz };
                bot.pos = { x: nx, y: bot.pos.y + Math.max(-12 * dt * 3, Math.min(8 * dt, np.y - bot.pos.y)), z: nz };
                if (np.y > bot.pos.y + 0.01 && this.grid._onLadder(this.grid.cellOf(nx, nz), bot.pos.y)) bot.pos.y = Math.min(np.y, bot.pos.y + 4 * dt);
            } else { bot.vel = { x: 0, y: 0, z: 0 }; bot.path = []; }
        } else bot.vel = { x: 0, y: 0, z: 0 };
        if (this.map.voidY !== null && bot.pos.y < this.map.voidY) {
            bot.alive = false; bot.deaths++; bot.respawnAt = this.t + 2;
            this.events.push({ type: "kill", killer: -1, victim: bot.id, weapon: "fall" });
        }
    }

    _frame() {
        const f = { type: "frame", t: +this.t.toFixed(3), bots: this.bots.map((b) => ({
            id: b.id, pos: [r2(b.pos.x), r2(b.pos.y), r2(b.pos.z)], yaw: r3(b.yaw), pitch: 0, vel: [r2(b.vel.x), r2(b.vel.y), r2(b.vel.z)],
            health: Math.max(0, Math.round(b.health)), weapon: b.weapon, alive: b.alive, target: b.target,
            goal: b.goal ? [r2(b.goal.x), r2(b.goal.y), r2(b.goal.z)] : null,
            path: b.path.slice(b.pathIx, b.pathIx + 40).map((p) => [r2(p.x), r2(p.y), r2(p.z)]),
            state: b.alive ? b.state : "dead", decision: b.alive ? b.decision : "dead", fov: this._p(b, "fov") })),
            events: this.events };
        this.events = [];
        return f;
    }

    startLine() {
        return { type: "start", t: 0, mock: true, map: this.map.name, mode: this.mode, tickRate: 20, secs: this.secs,
            bots: this.bots.map((b) => ({ id: b.id, name: b.name, team: b.team, skill: b.skill, brain: b.brain })) };
    }
    endLine(reason = "time") {
        const scores = this.bots.map((b) => ({ id: b.id, name: b.name, team: b.team, brain: b.brain, skill: b.skill, kills: b.kills, deaths: b.deaths,
            damage: Math.round(b.damage), shots: b.shots, hits: b.hits, objTime: +b.objTime.toFixed(1) }));
        let winner = null;
        if (this.teams) {
            const k = { a: 0, b: 0 };
            for (const s of scores) k[s.team] += s.kills + s.objTime * 0.2;
            winner = k.a === k.b ? "draw" : k.a > k.b ? "a" : "b";
        } else if (scores.length) winner = scores.reduce((a, b) => (b.kills > a.kills ? b : a)).id;
        return { type: "end", t: +this.t.toFixed(3), reason, winner, scores };
    }
}

const wrap = (a) => { while (a > Math.PI) a -= 2 * Math.PI; while (a < -Math.PI) a += 2 * Math.PI; return a; };
const r2 = (v) => Math.round(v * 100) / 100, r3 = (v) => Math.round(v * 1000) / 1000;
export { WEAPONS };
