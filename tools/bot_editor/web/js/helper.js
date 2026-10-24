// The one place the page talks to the game's helper (quark_maptool). Swapping the mock for the real helper, or
// adapting to the helper's final line format, is this file only (normalizeLine / buildRequest).
//
// quark_maptool `sim` / `simbatch` (tools/bot_editor/native/SIM.md) run the game's own server and bots with each
// bot's brain and objective override; an older helper without them falls back to `bots` (no brains, no overrides);
// no helper at all: the MOCK in the page.
//
// The page's run request (toSimRequest / toHelperRequest turn it into the helper's, written to its stdin as one line):
//   { map: {name, source, text}, mode, seed, secs, speed, bots: [{id, name, team: "a"|"b"|"any", skill: "BEGINNER".."EXPERT",
//     brain, pos: [x,y,z]|null, yaw: deg, objective: {kind: "default"|"goto"|"hold"|"follow"|"objective", pos, radius, points,
//     target: {kind, index}, role: "attack"|"defend"}}], brains: {<name>: <.qbrain text>} }
// Stream lines (one JSON object per line), as the page uses them after normalizeLine:
//   {type:"start", t, map, mode, tickRate, secs, bots:[{id, name, team, skill, brain}], mock?}
//   {type:"frame", t, bots:[{id, pos:[x,y,z], yaw, pitch, vel:[x,y,z], health, weapon, alive, target (id|-1),
//     goal:[x,y,z]|null, path:[[x,y,z]...], state, decision, fov}], events:[{type:"kill", killer, victim, weapon} |
//     {type:"hit", attacker, victim, damage, weapon} | {type:"spawn", id, pos} | {type:"score", ...} | ...]}
//   {type:"end", t, reason, winner, scores:[{id, name, team, brain, skill, kills, deaths, damage, shots, hits, objTime}]}
// Control lines (page -> helper stdin): pause | resume | speed <x> | step | stop (sim: "speed max" is realtime off,
// a number is realtime on + speed)

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
        lastFfa = o.teams != null ? !o.teams : ["ffa", "duel", "1v1", "tourney", "hide", "tag", "race"].includes(o.mode);
        lastPaths.clear();
        for (const b of o.bots || []) { startBrains.set(b.id, b.brain || "default"); startSkills.set(b.id, b.skill); }
        return { ...o, type: "start", t: time(o), bots: (o.bots || o.players || []).map((b) => ({ ...b, id: b.id ?? b.slot })) };
    }
    if (type === "frame" || type === "sample" || type === "state" || type === "tick") {
        return { type: "frame", t: time(o), events: (o.events || []).map(normEvent),
            bots: (o.bots || o.players || []).map((b) => ({
                // sim sends a bot's path only when it changed: keep the last one
                ...(() => { const id = b.id ?? b.slot, raw = b.path ?? b.debug?.path; if (raw !== undefined) lastPaths.set(id, (raw || []).map(vec)); else if (b.alive === false) lastPaths.set(id, []); return { path: lastPaths.get(id) || [] }; })(),
                id: b.id ?? b.slot, pos: vec(b.pos ?? b.origin), yaw: b.yawDeg != null ? b.yawDeg * DEG : (b.yaw ?? 0),
                pitch: b.pitchDeg != null ? b.pitchDeg * DEG : (b.pitch ?? 0), vel: vec(b.vel ?? b.velocity) || [0, 0, 0],
                health: b.health ?? b.hp ?? 0, weapon: String(b.weaponName ?? b.weapon ?? "").toLowerCase(), alive: b.alive ?? (b.health > 0),
                target: b.target ?? b.debug?.target ?? -1, goal: vec(b.goal ?? b.debug?.goal),
                waypoint: vec(b.waypoint), state: b.state ?? b.debug?.state ?? "", fov: b.fov ?? b.debug?.fov,
                decision: b.decision ?? b.label ?? b.debug?.decision ?? b.debug?.label ?? (b.state ? b.state + (b.goal ? " → goal " + vec(b.goal).map((v) => (+v).toFixed(0)).join(" ") : "") : ""),
                kills: b.kills, deaths: b.deaths, damage: b.damage, score: b.score,
            })) };
    }
    if (type === "end" && o.perf && Array.isArray(o.bots)) {   // sim's end line
        const w = o.winner, ffa = lastFfa;
        const winner = w ? (ffa || w.team == null ? w.player : w.team) : null;
        return { ...o, type: "end", t: time(o), reason: o.reason, winner, winnerText: w ? w.text : "", leader: o.leader || null,
            scores: o.bots.map((x) => ({ ...x, skill: x.skill ?? startSkills.get(x.id) })) };
    }
    if (type === "end" || type === "result" || type === "done") {
        const r = o.result || {};
        let scores = o.scores || r.players || o.players || [];
        scores = scores.map((x) => ({ ...x, brain: x.brain ?? startBrains.get(x.id), skill: x.skill ?? startSkills.get(x.id) }));
        let winner = o.winner ?? r.winner;
        if (winner === undefined && scores.length) {
            const teams = new Set(scores.map((x) => x.team));
            if (teams.size > 1 && !lastFfa) { const k = {}; for (const x of scores) k[x.team] = (k[x.team] || 0) + (x.score ?? x.kills ?? 0); const best = Object.entries(k).sort((a, b) => b[1] - a[1]); winner = best.length > 1 && best[0][1] === best[1][1] ? "draw" : best[0][0]; }
            else winner = scores.reduce((a, b) => ((b.kills || 0) > (a.kills || 0) ? b : a)).id;
        }
        return { ...o, type: "end", t: time(o), reason: o.reason || (r.stopped ? "stopped" : "time"), winner, scores };
    }
    if (type === "event") return { type: "frame", t: time(o), bots: null, events: [normEvent(o.event || o)] };
    if (type === "error") return { type: "error", error: o.error || "helper error" };
    return { ...o, type: type || "unknown" };
}
const startBrains = new Map(), startSkills = new Map(), lastPaths = new Map();
let lastFfa = false;
function normEvent(e) {
    const t = e.kind || e.type;
    if (t === "kill" || t === "frag" || t === "death") return { type: "kill", killer: e.killer ?? e.attacker ?? e.who ?? -1, victim: e.victim ?? e.id, weapon: String(e.weapon ?? e.cause ?? e.detail ?? "").toLowerCase() };
    return { ...e, type: t };
}

// the page's run request -> quark_maptool `bots --stream` (tools/map_editor/native/README.md "bots")
export function toHelperRequest(req) {
    return {
        text: req.map.text, mapName: req.map.name, mode: req.mode, seconds: req.secs, seed: req.seed, rate: 20, paths: true,
        speed: req.speed === "max" ? 0 : req.speed,
        // sent as "placed" (the same specs): with a "bots" list and no "placed", quark_maptool adds 4 more bots
        // (bots_cmd.cpp: more = ... : req.has("placed") ? 0 : 4), so the match would not be the line-up asked for
        placed: req.bots.map((b) => ({ team: b.team === "b" ? "b" : "a", skill: String(b.skill || "advanced").toLowerCase(), name: b.name, brain: b.brain,
            ...(b.pos ? { pos: b.pos, yaw: (b.yaw || 0) * DEG } : {}), objective: b.objective })),
        brains: req.brains,
    };
}

// the page's objective -> sim's override (SIM.md "Objective overrides"); null: none
export function toSimObjective(o) {
    if (!o || !o.kind || o.kind === "default") return null;
    const chase = o.chase === false ? { chase: false } : {};
    if (o.kind === "goto") return o.pos ? { type: "goto", pos: o.pos, ...(o.radius ? { radius: o.radius } : {}), ...chase } : null;
    if (o.kind === "hold") return o.pos ? { type: "hold", pos: o.pos, radius: o.radius || 4, ...chase } : null;
    if (o.kind === "follow") return o.points && o.points.length ? { type: "route", points: o.points, loop: o.loop !== false, ...chase } : null;
    if (o.kind === "objective") return { type: o.role === "defend" ? "defend" : "attack", ...(o.target ? { objectiveIndex: o.target.index } : {}), ...chase };
    return null;
}

// the page's run request -> quark_maptool `sim` / `simbatch` (SIM.md)
export function toSimRequest(req, extra = {}) {
    const max = req.speed === "max";
    return {
        map: { name: req.map.name, source: req.map.source, text: req.map.text }, mode: req.mode, seed: req.seed, seconds: req.secs,
        rate: 20, realtime: !max, speed: max ? 0 : (+req.speed || 1), paths: true, skill: "ADVANCED",
        bots: req.bots.map((b) => {
            const o = toSimObjective(b.objective);
            return { ...(b.team === "a" || b.team === "b" ? { team: b.team } : {}), skill: String(b.skill || "ADVANCED").toUpperCase(), name: b.name,
                brain: b.brain || "default", ...(b.pos ? { pos: b.pos, yaw: (b.yaw || 0) * DEG } : {}), ...(o ? { objective: o } : {}) };
        }),
        brains: req.brains || {},
        ...extra,
    };
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
    // cmd: "sim" (live, with brains and overrides), "simbatch" (raw lines: see runBatch) or "bots" (an older helper)
    constructor(req, handlers, cmd = "sim") {
        this.h = handlers; this.count = 0; this.closed = false; this.id = null; this.cmd = cmd; this.queue = [];
        const request = cmd === "bots" ? toHelperRequest(req) : cmd === "sim" ? toSimRequest(req) : req;
        api.post("/api/run", { args: [cmd, "--stream"], request }).then((r) => { this.id = r.id; this._open(); for (const l of this.queue.splice(0)) this.control(l); })
            .catch((e) => handlers.onError && handlers.onError(e));
    }
    _open() {
        const es = new EventSource(`/api/run/${this.id}/events?from=${this.count}`);
        this.es = es;
        es.onmessage = (ev) => {
            for (const line of ev.data.split("\n")) {
                this.count++;
                let o; try { o = JSON.parse(line); } catch { continue; }
                if (this.cmd === "simbatch") { this.h.onLine(o); continue; }
                if (o.error && !o.type) o.type = "error";
                const n = normalizeLine(o);
                if (n) this.h.onLine(n);
            }
        };
        es.addEventListener("exit", (ev) => { this.closed = true; es.close(); this.h.onExit && this.h.onExit(JSON.parse(ev.data)); });
        es.onerror = () => { es.close(); if (!this.closed) setTimeout(() => !this.closed && this._open(), 500); };
    }
    control(line) {
        if (!this.id) { this.queue.push(line); return; }
        const send = (l) => api.post(`/api/run/${this.id}/control`, { line: l }).catch(() => {});
        if (this.cmd === "sim" && line.startsWith("speed ")) {   // max: realtime off; a number: realtime on at that speed
            if (line === "speed max") return send("realtime off");
            return send("realtime on").then(() => send(line));
        }
        if (line === "speed max") line = "speed 0";
        return send(line);
    }
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
            }, helper.cmd || "bots");
        }
    });
}

// a whole batch through `simbatch` (N matches on every core but one): onLine gets its raw lines
// (start / match / progress / end, SIM.md "simbatch"); resolves with the end line
export function runBatch(req, matches, onLine) {
    return new Promise((resolve, reject) => {
        let end = null;
        const threads = Math.max(1, Math.min(16, (navigator.hardwareConcurrency || 2) - 1));
        const run = new RealRun(toSimRequest(req, { matches, threads, progressEvery: 0.25, realtime: false, speed: 0, until: "matchEnd" }), {
            onLine: (o) => { if (o.type === "end") end = o; else if (o.type === "error" || (o.error && !o.type)) reject(new Error(o.error)); onLine && onLine(o); },
            onExit: (x) => (end ? resolve(end) : reject(new Error("simbatch ended without a result: " + (x.stderr || x.code)))),
            onError: reject,
        }, "simbatch");
        runBatch.current = run;
    });
}

export async function createHelper(config) {
    const real = !!(config && config.maptool) && !new URLSearchParams(location.search).has("mock");
    // does this quark_maptool have sim (SIM.md)? simcheck answers {brainSupport, ok}; an older one has no such command
    let sim = false, brainSupport = false;
    if (real) {
        try {
            const r = await api.post("/api/helper", { args: ["simcheck"], request: { text: "version 1\n" }, timeout: 20 });
            if (r && r.answer && "brainSupport" in r.answer) { sim = true; brainSupport = !!r.answer.brainSupport; }
        } catch {}
    }
    // What the helper acts on. quark_maptool `bots` stores a spec's brain and objective and echoes them, but every
    // bot plays the game's built-in brain (tools/map_editor/native/README.md "bots"); the page says so when a run
    // asks for more. Flip these when the helper runs .qbrain text (tools/bot_editor/native/SIM.md).
    return {
        uses: !real ? { brains: true, objectives: true } : { brains: brainSupport, objectives: sim },
        cmd: real ? (sim ? "sim" : "bots") : null,
        batch: real && sim,
        brainSupport,
        kind: real ? "real" : "mock",
        label: real ? "quark_maptool" + (sim ? " sim" : " (old: no sim)") : "MOCK (no quark_maptool found: a stand-in simulation)",
        start(req, handlers, map, paramsOf) { return real ? new RealRun(req, handlers, sim ? "sim" : "bots") : new MockRun(req, handlers, map, paramsOf); },
    };
}
