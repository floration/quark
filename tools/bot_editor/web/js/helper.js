// The one place the page talks to the game's helper (quark_maptool). Swapping the mock for the real helper, or
// adapting to the helper's final line format, is this file only (normalizeLine / buildRequest).
//
// Run request (POST /api/run, written to `quark_maptool bots --stream` stdin as one JSON line):
//   { map: {name, source, text}, mode, seed, secs, speed, bots: [{id, name, team: "a"|"b"|"any", skill: "BEGINNER".."EXPERT",
//     brain, pos: [x,y,z]|null, yaw: deg, objective: {kind: "default"|"goto"|"hold"|"follow"|"objective", pos, radius, points,
//     target: {kind, index}, role: "attack"|"defend"}}], brains: {<name>: <.qbrain text>} }
// Stream lines (one JSON object per line), as the page uses them after normalizeLine:
//   {type:"start", t, map, mode, tickRate, secs, bots:[{id, name, team, skill, brain}], mock?}
//   {type:"frame", t, bots:[{id, pos:[x,y,z], yaw, pitch, vel:[x,y,z], health, weapon, alive, target (id|-1),
//     goal:[x,y,z]|null, path:[[x,y,z]...], state, decision, fov}], events:[{type:"kill", killer, victim, weapon} |
//     {type:"hit", attacker, victim, damage, weapon} | {type:"spawn", id, pos} | {type:"score", ...} | ...]}
//   {type:"end", t, reason, winner, scores:[{id, name, team, brain, skill, kills, deaths, damage, shots, hits, objTime}]}
// Control lines (page -> helper stdin): pause | resume | speed <x> | step | stop

import { api } from "./api.js";
import { MockSim } from "./mocksim.js";

const DEG = Math.PI / 180;

// The real helper's lines -> the shape above. Accepts a few likely spellings so a small difference in the final
// interface doesn't break the page (vectors as {x,y,z} or [x,y,z]; time as t / time / tick+tickRate; yaw in degrees
// when "yawDeg" is given).
let tickRate = 0;
export function normalizeLine(o) {
    if (!o || typeof o !== "object") return null;
    const type = o.type || o.kind || (o.bots && "frame") || null;
    const vec = (v) => (v == null ? null : Array.isArray(v) ? v : [v.x, v.y, v.z]);
    const time = (x) => (x.t ?? x.time ?? (x.tick != null && tickRate ? x.tick / tickRate : 0));
    if (type === "start" || type === "hello" || type === "match") {
        tickRate = o.tickRate || o.tick_rate || 0;
        return { ...o, type: "start", t: time(o), bots: (o.bots || o.players || []).map((b) => ({ ...b, id: b.id ?? b.slot })) };
    }
    if (type === "frame" || type === "state" || type === "tick") {
        return { type: "frame", t: time(o), events: (o.events || []).map(normEvent),
            bots: (o.bots || o.players || []).map((b) => ({
                id: b.id ?? b.slot, pos: vec(b.pos ?? b.origin), yaw: b.yawDeg != null ? b.yawDeg * DEG : (b.yaw ?? 0),
                pitch: b.pitchDeg != null ? b.pitchDeg * DEG : (b.pitch ?? 0), vel: vec(b.vel ?? b.velocity) || [0, 0, 0],
                health: b.health ?? b.hp ?? 0, weapon: b.weapon ?? "", alive: b.alive ?? (b.health > 0),
                target: b.target ?? b.debug?.target ?? -1, goal: vec(b.goal ?? b.debug?.goal), path: (b.path ?? b.debug?.path ?? []).map(vec),
                state: b.state ?? b.debug?.state ?? "", decision: b.decision ?? b.debug?.decision ?? b.debug?.label ?? "", fov: b.fov ?? b.debug?.fov,
            })) };
    }
    if (type === "end" || type === "result" || type === "done") return { ...o, type: "end", t: time(o), scores: o.scores || o.players || [] };
    if (type === "event") return { type: "frame", t: time(o), bots: null, events: [normEvent(o.event || o)] };
    return { ...o, type: type || "unknown" };
}
function normEvent(e) {
    const t = e.type || e.kind;
    if (t === "kill" || t === "frag" || t === "death") return { type: "kill", killer: e.killer ?? e.attacker ?? -1, victim: e.victim ?? e.id, weapon: e.weapon ?? e.cause ?? "" };
    return { ...e, type: t };
}

export function buildRequest(scenario, map, brains) {
    return {
        map: { name: map.name, source: scenario.mapSource || "default", text: map.text },
        mode: scenario.mode, seed: scenario.seed ?? 1, secs: scenario.secs ?? 120, speed: scenario.speed ?? 1,
        bots: scenario.bots.map((b, i) => ({ id: i, name: b.name, team: b.team, skill: b.skill, brain: b.brain,
            pos: b.pos ? [b.pos.x, b.pos.y, b.pos.z] : null, yaw: b.yaw ?? 0, objective: b.objective || { kind: "default" } })),
        brains,
    };
}

// ---------------------------------------------------------------- the real helper (through serve.py)
class RealRun {
    constructor(req, handlers) {
        this.h = handlers; this.count = 0; this.closed = false; this.id = null;
        api.post("/api/run", { args: ["bots", "--stream"], request: req }).then((r) => { this.id = r.id; this._open(); })
            .catch((e) => handlers.onError && handlers.onError(e));
    }
    _open() {
        const es = new EventSource(`/api/run/${this.id}/events?from=${this.count}`);
        this.es = es;
        es.onmessage = (ev) => {
            for (const line of ev.data.split("\n")) {
                this.count++;
                let o; try { o = JSON.parse(line); } catch { continue; }
                const n = normalizeLine(o);
                if (n) this.h.onLine(n);
            }
        };
        es.addEventListener("exit", (ev) => { this.closed = true; es.close(); this.h.onExit && this.h.onExit(JSON.parse(ev.data)); });
        es.onerror = () => { es.close(); if (!this.closed) setTimeout(() => !this.closed && this._open(), 500); };
    }
    control(line) { if (this.id) return api.post(`/api/run/${this.id}/control`, { line }); }
    stop() { if (this.id) api.post(`/api/run/${this.id}/stop`, {}); }
}

// ---------------------------------------------------------------- the mock (same lines, in the page)
class MockRun {
    constructor(req, handlers, map, paramsOf) {
        this.h = handlers;
        this.sim = new MockSim(map, req, paramsOf);
        this.speed = req.speed || 1; this.paused = false; this.closed = false;
        this.acc = 0; this.last = performance.now();
        handlers.onLine(this.sim.startLine());
        this.timer = setInterval(() => this._tick(), 25);
    }
    _tick() {
        if (this.closed) return;
        const now = performance.now(), el = Math.min(0.25, (now - this.last) / 1000);
        this.last = now;
        if (this.paused) return;
        if (this.speed === "max") { this._steps(400); return; }
        this.acc += el * this.speed;
        const n = Math.floor(this.acc / this.sim.dt);
        this.acc -= n * this.sim.dt;
        this._steps(Math.min(n, 400));
    }
    _steps(n) {
        for (let i = 0; i < n; i++) {
            const f = this.sim.step();
            if (!f) { this._end("time"); return; }
            this.h.onLine(f);
            if (this.sim.over) { this._end("time"); return; }
        }
    }
    _end(reason) {
        if (this.closed) return;
        this.closed = true; clearInterval(this.timer);
        this.h.onLine(this.sim.endLine(reason));
        this.h.onExit && this.h.onExit({ code: 0, mock: true });
    }
    control(line) {
        const [cmd, arg] = line.split(" ");
        if (cmd === "pause") this.paused = true;
        else if (cmd === "resume") { this.paused = false; this.last = performance.now(); }
        else if (cmd === "speed") this.speed = arg === "max" ? "max" : Math.max(0.05, Math.min(64, +arg || 1));
        else if (cmd === "step") { this.paused = true; this._steps(1); }
        else if (cmd === "stop") this._end("stopped");
    }
    stop() { this.control("stop"); }
}

// Run a whole match as fast as possible and return its end line (batch runs). The mock runs in slices so the page
// stays responsive; the real helper runs with speed max and only the end line is kept.
export function runToEnd(helper, req, map, paramsOf, onProgress) {
    return new Promise((resolve, reject) => {
        if (helper.kind === "mock") {
            const sim = new MockSim(map, req, paramsOf);
            const slice = () => {
                const t0 = performance.now();
                while (performance.now() - t0 < 12) { if (!sim.step() || sim.over) { resolve(sim.endLine("time")); return; } }
                onProgress && onProgress(sim.t / sim.secs);
                setTimeout(slice, 0);
            };
            slice();
        } else {
            let end = null;
            const run = new RealRun({ ...req, speed: "max" }, {
                onLine: (l) => { if (l.type === "end") end = l; else if (l.type === "frame" && onProgress && req.secs) onProgress(l.t / req.secs); },
                onExit: (x) => (end ? resolve(end) : reject(new Error("helper ended without a result: " + (x.stderr || x.code)))),
                onError: reject,
            });
            run.control("speed max");
        }
    });
}

export async function createHelper(config) {
    const real = !!(config && config.maptool) && !new URLSearchParams(location.search).has("mock");
    return {
        kind: real ? "real" : "mock",
        label: real ? "quark_maptool" : "MOCK (no quark_maptool found: a stand-in simulation)",
        start(req, handlers, map, paramsOf) { return real ? new RealRun(req, handlers) : new MockRun(req, handlers, map, paramsOf); },
    };
}
