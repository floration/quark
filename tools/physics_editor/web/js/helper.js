// The one place the page talks to the game's helper (quark_maptool, INTERFACE.md): physparams, physpresets, physroom,
// physsim, physexport and physdrive --stream. Without the helper (or with ?mock in the address) the same calls are
// answered by the page's MOCK (mockpm.js / sim.js / room.js), and the header badge says MOCK.

import { api } from "./api.js";
import { parseQmap } from "./qmap.js";
import { MockSim } from "./sim.js";
import { mockRoomText, mockPresets } from "./room.js";
import { newState, tuningOf, pmove, maxChargesFor, MF_ONGROUND } from "./mockpm.js";
import { compactPhysics, cppList, normalizeCatalog } from "./params.js";
import { mergeQmode } from "./qmode.js";

const DEG = Math.PI / 180;

// a helper trace sample / drive answer in the page's form (vectors as arrays, t in seconds)
const vec = (v) => (v == null ? [0, 0, 0] : Array.isArray(v) ? v : [v.x, v.y, v.z]);
export function normSample(o) {
    const vel = vec(o.vel);
    const t = o.t ?? (o.ms != null ? o.ms / 1000 : o.tMs != null ? o.tMs / 1000 : 0);
    return { ...o, t, pos: vec(o.pos), vel, speed: o.speed ?? Math.hypot(...vel), hspeed: o.hspeed ?? Math.hypot(vel[0], vel[2]),
        onGround: o.onGround ?? ((o.flags | 0) & MF_ONGROUND) !== 0, dashCharges: o.dashCharges ?? 0, dashTimer: o.dashTimer ?? 0 };
}
export function normEvent(e) {
    const t = e.t ?? (e.ms != null ? e.ms / 1000 : e.tMs != null ? e.tMs / 1000 : 0);
    return { ...e, t, kind: e.kind || e.type || "event", pos: e.pos ? vec(e.pos) : undefined };
}
function normRun(r) {
    return { trace: (r.trace || []).map(normSample), events: (r.events || []).map(normEvent), metrics: r.metrics || {}, label: r.label, clamped: r.clamped };
}

// ---------------------------------------------------------------- drive: the real pmove over a WebSocket (or SSE)
class RealDrive {
    constructor(init, h) {
        this.h = h; this.closed = false; this.sent = []; this.lat = 0; this.queue = []; this.ready = false; this.via = "WebSocket";
        let ws;
        try { ws = new WebSocket(`ws://${location.host}/api/drive/ws`); } catch { ws = null; }
        if (!ws) { this._sse(init); return; }
        this.ws = ws;
        let opened = false;
        ws.onopen = () => { opened = true; ws.send(JSON.stringify(init)); };
        ws.onmessage = (ev) => this._line(ev.data);
        ws.onerror = () => { if (!opened && !this.closed) { this.ws = null; this._sse(init); } };
        ws.onclose = () => { if (opened && !this.closed) { this.closed = true; this.h.onExit && this.h.onExit({ code: -1, why: "closed" }); } };
    }
    async _sse(init) {
        this.via = "SSE + POST";
        try {
            const r = await api.post("/api/drive", { request: init });
            this.id = r.id;
            const es = new EventSource(`/api/drive/${this.id}/events`);
            this.es = es;
            es.onmessage = (ev) => { for (const l of ev.data.split("\n")) this._line(l); };
            es.addEventListener("exit", (ev) => { this.closed = true; es.close(); this.h.onExit && this.h.onExit(JSON.parse(ev.data)); });
            this.ready = true;
            for (const l of this.queue.splice(0)) this._send(l);
            this.h.onReady && this.h.onReady(this);
        } catch (e) { this.h.onExit && this.h.onExit({ code: -1, why: e.message }); }
    }
    _line(s) {
        let o; try { o = JSON.parse(s); } catch { return; }
        if (o.type === "_ready") { this.ready = true; for (const l of this.queue.splice(0)) this._send(l); this.h.onReady && this.h.onReady(this); return; }
        if (o.type === "_exit") { this.closed = true; this.h.onExit && this.h.onExit(o); return; }
        if (o.type === "done") return;
        if (o.type === "start") {   // (physdrive's first line, before any control: the start state; no answer to time)
            if (o.pos) { const st = normSample(o); st.events = []; this.h.onState(st, true); }
            return;
        }
        const t0 = this.sent.shift();
        if (o.type === "warning" || (o.error && !o.pos)) { this.h.onError && this.h.onError(o.warning || o.message || o.error || "warning"); if (!o.pos) return; }
        if (t0 != null) this.lat = this.lat ? this.lat * 0.9 + (performance.now() - t0) * 0.1 : performance.now() - t0;
        if (!o.pos) return;   // (an answer to a physics / reset line without a state)
        const st = normSample(o);
        st.events = (o.events || []).map(normEvent);
        this.h.onState(st);
    }
    _send(line) {
        if (this.closed) return;
        if (!this.ready) { this.queue.push(line); return; }
        this.sent.push(performance.now());
        if (this.sent.length > 64) this.sent.splice(0, this.sent.length - 64);
        if (this.ws) this.ws.send(line);
        else api.post(`/api/drive/${this.id}/control`, { line }).catch(() => {});
    }
    inflight() { return this.sent.length; }
    send(cmds) { this._send(JSON.stringify({ cmd: cmds })); }
    setPhysics(ph) { this._send(JSON.stringify({ physics: compactPhysics(ph, this.h.expr) })); }
    reset(start) { this._send(start ? JSON.stringify({ reset: true, start }) : "reset"); }
    stop() {
        if (this.closed) return;
        this.closed = true;
        try { if (this.ws) { this.ws.send("stop"); this.ws.close(); } else if (this.id) api.post(`/api/drive/${this.id}/stop`, {}).catch(() => {}); } catch {}
        if (this.es) this.es.close();
    }
}

class MockDrive {
    constructor(init, world, h) {
        this.h = h; this.world = world; this.via = "the page's MOCK pmove"; this.lat = 0; this.t = 0;
        this.setPhysics(init.physics); this.reset(init.start);
        setTimeout(() => h.onReady && h.onReady(this), 0);
    }
    setPhysics(ph) { this.mt = tuningOf(ph); }
    reset(start) {
        this.start = start || this.start || { pos: [0, 0, 0] };
        this.s = newState(this.start); this.s.dashCharges = maxChargesFor(this.mt); this.t = 0;
    }
    inflight() { return 0; }
    send(cmds) {
        const evs = [];
        for (const c of cmds) {
            pmove(this.s, { ...c, yaw: c.yaw * DEG, pitch: c.pitch * DEG }, this.world, this.mt, (kind, d) => evs.push({ t: this.t / 1000, kind, ...d }));
            this.t += c.msec;
        }
        const s = this.s;
        this.h.onState({ t: this.t / 1000, pos: [...s.pos], vel: [...s.vel], speed: Math.hypot(...s.vel), hspeed: Math.hypot(s.vel[0], s.vel[2]),
            flags: s.flags, dashCharges: s.dashCharges, onGround: (s.flags & MF_ONGROUND) !== 0, hookPoint: (s.flags & 32) ? [...s.hookPoint] : null, events: evs });
    }
    stop() {}
}

// ---------------------------------------------------------------- the helper
export async function createHelper(config) {
    const wantReal = !!(config && config.maptool) && !new URLSearchParams(location.search).has("mock");
    let params = null, why = "";
    if (wantReal) {
        try {
            const r = await api.helper(["physparams"], {}, 20);
            if (r && r.code === 0 && r.answer && Array.isArray(r.answer.params)) params = r.answer;
            else why = "this quark_maptool has no physics commands (" + ((r && r.answer && r.answer.error) || (r && r.stderr) || "no answer").toString().slice(0, 120) + ")";
        } catch (e) { why = e.message; }
    } else why = config && config.maptool ? "?mock in the address" : "quark_maptool not found";
    const real = !!params;
    // v2 equations: the helper runs them when it has `physexpr` (the game's own evaluator, src/game/moveexpr.cpp)
    let hasExpr = false;
    if (real) {
        try {
            const r = await api.helper(["physexpr"], { physics: { expr: { gravity: "1" } } }, 20);
            hasExpr = r && r.code === 0 && r.answer && !r.answer.error;
        } catch {}
    }
    const mapCache = new Map();
    const parsed = (key, text) => {
        if (!mapCache.has(key)) { const m = parseQmap(text, key); mapCache.set(key, { map: m, sim: new MockSim(m) }); }
        return mapCache.get(key);
    };
    let roomText = null, presets = null, roomInfo = null;
    const H = {
        kind: real ? "real" : "mock",
        label: real ? "quark_maptool" : "MOCK PHYSICS",
        why,
        hasExpr,
        catalog: normalizeCatalog(params),
        // physexpr on a set: {items: [{key, text, ok, dynamic, error, deps, inputs, value}], ...} (null without it)
        async exprCheck(physics) {
            if (!hasExpr) return null;
            const r = await api.helper(["physexpr"], { physics: compactPhysics(physics, true) }, 20);
            const a = r.answer || {};
            if (r.code !== 0 && !a.items) throw new Error(a.error || r.stderr || "physexpr failed");
            const items = a.items || a.exprs || (a.report && a.report.items) || [];
            return { ...a, items: items.map((i) => ({ ...i, key: i.key || i.setting, value: i.value != null ? +i.value : i.atRest != null ? +i.atRest : null })) };
        },
        async room() {
            if (roomText) return { text: roomText, info: roomInfo };
            if (real) {
                try {
                    const r = await api.helper(["physroom"], {}, 20);
                    if (r.code === 0 && r.answer && r.answer.text) { roomText = r.answer.text; roomInfo = r.answer; }
                } catch {}
            }
            if (!roomText) { roomText = mockRoomText(); roomInfo = { mock: true }; }
            return { text: roomText, info: roomInfo };
        },
        async presets() {
            if (presets) return presets;
            if (real) {
                try {
                    const r = await api.helper(["physpresets"], {}, 20);
                    const list = r.answer && (r.answer.presets || (Array.isArray(r.answer) ? r.answer : null));
                    if (r.code === 0 && list) presets = list;
                } catch {}
            }
            if (!presets) presets = mockPresets();
            return presets;
        },
        // req: {physics, compare: [physics], scenario, map: {room: true} | {name, source, text}, sampleMs}
        async sim(req) {
            if (real) {
                const request = { physics: compactPhysics(req.physics, hasExpr), compare: (req.compare || []).map((c) => compactPhysics(c, hasExpr)), scenario: req.scenario,
                    sampleMs: req.sampleMs || 10, map: req.map && !req.map.room ? { text: req.map.text, name: req.map.name } : "room" };
                const r = await api.helper(["physsim"], request, 60);
                const a = r.answer || {};
                if (r.code !== 0 || a.error || !Array.isArray(a.runs)) throw new Error(a.error || r.stderr || "physsim failed");
                return { runs: a.runs.map(normRun), ms: a.ms ?? r.wallMs, wallMs: r.wallMs, worker: !!r.worker, via: "quark_maptool physsim" };
            }
            const t0 = performance.now();
            const text = req.map && !req.map.room ? req.map.text : (await H.room()).text;
            const { sim } = parsed(req.map && !req.map.room ? "map:" + req.map.name : "room", text);
            const runs = [req.physics, ...(req.compare || [])].map((ph) => sim.run(req.scenario, ph, req.sampleMs || 10));
            return { runs, ms: performance.now() - t0, via: "the page's MOCK" };
        },
        async exportSet(physics, name) {
            if (real) {
                try {
                    const r = await api.helper(["physexport"], { physics: compactPhysics(physics, hasExpr), name }, 20);
                    if (r.code === 0 && r.answer && r.answer.cpp != null) return { ...r.answer, via: "quark_maptool physexport" };
                } catch {}
            }
            return { qmode: mergeQmode("", physics, name), cpp: cppList(physics, H.catalog), via: "made in the page (MOCK)" };
        },
        // init: {physics, map: {room:true}|{text,name}, start}; h: {onState, onReady, onExit, onError}
        async drive(init, h) {
            if (real) {
                h.expr = hasExpr;
                const first = { physics: compactPhysics(init.physics, hasExpr), start: init.start, map: init.map && !init.map.room ? { text: init.map.text, name: init.map.name } : "room" };
                return new RealDrive(first, h);
            }
            const text = init.map && !init.map.room ? init.map.text : (await H.room()).text;
            const { sim } = parsed(init.map && !init.map.room ? "map:" + init.map.name : "room", text);
            return new MockDrive(init, sim.world, h);
        },
    };
    return H;
}
