// Fights: bot vs bot through the bot editor's simulator (quark_maptool sim / simbatch, tools/bot_editor/native/SIM.md)
// with the weapon set applied: the request carries `weapons` (the set's .qweapons text), `qmode` (a custom mode text,
// optional) and `weaponsAllowed` (the weapon pool). normalizeLine / RealRun are the bot editor's helper.js (copied:
// the bot editor's files are its own); there is no mock fight: without a helper the Fights tab says what it needs.

import { api } from "./api.js";

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


// the page's fight -> sim's request (SIM.md "sim"), plus the weapon set keys
export function toFightRequest(f) {
    const max = f.speed === "max";
    return {
        map: { name: f.map.name, source: f.map.source || "default", text: f.map.text }, mode: f.mode, seed: f.seed ?? 1, seconds: f.secs ?? 120,
        rate: 20, realtime: !max, speed: max ? 0 : (+f.speed || 1), paths: true, skill: f.skill || "ADVANCED",
        bots: f.bots.map((b) => ({ ...(b.team === "a" || b.team === "b" ? { team: b.team } : {}), skill: String(b.skill || f.skill || "ADVANCED").toUpperCase(), name: b.name, brain: "default" })),
        brains: {},
        ...(f.weapons ? { weapons: f.weapons } : {}),
        ...(f.qmode ? { qmode: f.qmode } : {}),
        ...(f.weaponsAllowed && f.weaponsAllowed.length ? { weaponsAllowed: f.weaponsAllowed } : {}),
    };
}

export class RealRun {
    // cmd: "sim" (live, with brains and overrides), "simbatch" (raw lines: see runBatch) or "bots" (an older helper)
    constructor(req, handlers, cmd = "sim") {
        this.h = handlers; this.count = 0; this.closed = false; this.id = null; this.cmd = cmd; this.queue = [];
        const request = req;
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


// a batch through simbatch: resolves with its end line (SIM.md "simbatch"); onLine gets every raw line
export function runBatch(request, matches, onLine) {
    return new Promise((resolve, reject) => {
        let end = null;
        const threads = Math.max(1, Math.min(16, (navigator.hardwareConcurrency || 2) - 1));
        const run = new RealRun({ ...request, matches, threads, progressEvery: 0.25, realtime: false, speed: 0, until: "matchEnd" }, {
            onLine: (o) => { if (o.type === "end") end = o; else if (o.type === "error" || (o.error && !o.type)) reject(new Error(o.error)); onLine && onLine(o); },
            onExit: (x) => (end ? resolve(end) : reject(new Error("simbatch ended without a result: " + (x.stderr || x.code)))),
            onError: reject,
        }, "simbatch");
        runBatch.current = run;
    });
}
