// Drive: play the physics yourself. Pointer lock, the game's default binds (W A S D, Space jump, Left Shift dash,
// Q grapple; C crouch, which the game ignores), first person or chase camera over the test room / map. Each frame's
// input goes out as 1..8 ms commands (the view turned smoothly across them) to `physdrive --stream` (the game's real
// pmove, over a WebSocket) or to the MOCK in the page; the answer is drawn as soon as it is back, the view itself
// turns locally at once (no round trip for the mouse). The HUD and the speed graph are throttled; the 3D view
// redraws every frame from preallocated buffers.
import { GLScene, lookAt, hexRgb } from "./gl.js";
import { computeMetrics } from "./sim.js";
import { cssVar } from "./app.js";

const $ = (id) => document.getElementById(id);
const BTN_JUMP = 1, BTN_CROUCH = 2, BTN_DASH = 1 << 11, BTN_GRAPPLE = 1 << 12, MF_HOOKED = 32;
const DEG = Math.PI / 180;

export class DriveView {
    constructor(app) {
        this.app = app; this.c = $("driveCanvas");
        this.keys = new Set(); this.yaw = -90; this.pitch = 0; this.mdx = 0; this.mdy = 0;
        this.st = null; this.trail = []; this.evlog = []; this.hist = new Float32Array(600); this.histN = 0; this.histT = 0;
        this.frames = new Float32Array(240); this.fi = 0;
        this.scene = new GLScene(this.c);
        if (!this.scene.gl) $("driveMsg").innerHTML = "<div>WebGL isn't available in this browser: the Drive tab needs it.</div>";
        // Pointer lock can be refused (the Quark Project app's Electron shell denied it until it allowed 'pointerLock';
        // a sandboxed frame; an old browser): then Drive still starts, with the mouse looking while a button is held.
        this.c.addEventListener("click", () => {
            if (document.pointerLockElement === this.c || this.forceLocked) return;
            let r;
            try { r = this.c.requestPointerLock ? this.c.requestPointerLock() : null; } catch (_) { this._freeLook(); return; }
            if (!this.c.requestPointerLock) this._freeLook();
            else if (r && r.catch) r.catch(() => this._freeLook());
        });
        document.addEventListener("pointerlockerror", () => this._freeLook());
        document.addEventListener("pointerlockchange", () => { const on = document.pointerLockElement === this.c; this._msg(on); if (!on) this.keys.clear(); });
        document.addEventListener("mousemove", (e) => {
            if (document.pointerLockElement === this.c || (this.forceLocked && this.freeLook && (e.buttons & 1))) { this.mdx += e.movementX; this.mdy += e.movementY; }
        });
        const isGame = () => document.pointerLockElement === this.c || this.forceLocked;
        window.addEventListener("keydown", (e) => {
            if (this.freeLook && this.forceLocked && e.code === "Escape") { this.forceLocked = false; this.keys.clear(); this._msg(false); return; }
            if (!isGame()) return;
            if (["KeyW", "KeyA", "KeyS", "KeyD", "Space", "ShiftLeft", "ShiftRight", "KeyQ", "KeyC", "ControlLeft"].includes(e.code)) e.preventDefault();
            if (e.code === "KeyR" && !e.repeat) this.reset();
            if (e.code === "KeyV" && !e.repeat) { const s = $("driveCam"); s.value = s.value === "fp" ? "chase" : "fp"; }
            this.keys.add(e.code);
        });
        window.addEventListener("keyup", (e) => this.keys.delete(e.code));
        window.addEventListener("blur", () => this.keys.clear());
        $("driveReset").onclick = () => this.reset();
        $("drivePhys").onchange = () => this._physics();
        $("driveStart").onchange = () => this.reset();
        $("driveToGraph").onclick = () => this.toGraph();
        app.on("page", (p) => { if (p === "drive") this.enter(); else this.leave(); });
        app.on("physics", () => { if ($("drivePhys").value === "edit") this._physics(); });
        app.on("map", () => { this._starts(); this.restart(); });
        app.on("scenario", () => this._starts());
        this._starts();
        this._loop = this._loop.bind(this);
    }
    _freeLook() {   // pointer lock refused: play without it (hold the left button and drag to look; Esc stops)
        if (document.pointerLockElement === this.c || !this.active) return;
        this.freeLook = true; this.forceLocked = true; this._msg(true);
        $("driveHint").style.display = "";
    }
    _msg(playing) {
        $("driveMsg").style.display = playing ? "none" : "";
        if (!playing) $("driveHint").style.display = "none";
    }
    _starts() {
        const sel = $("driveStart"), keep = sel.value;
        sel.innerHTML = "";
        const add = (label, start) => { const o = new Option(label, sel.options.length); o._start = start; sel.append(o); };
        const sc = this.app.scenario;
        if (sc && sc.start) add("the scenario's start", { pos: [...sc.start.pos], yaw: sc.start.yaw || 0, pitch: 0 });
        const mc = this.app.mapChoice;
        if (mc.room && mc.info && Array.isArray(mc.info.parts)) {
            for (const p of mc.info.parts) if (p.start) add(p.label || p.id, { pos: Array.isArray(p.start) ? p.start : [p.start.x, p.start.y, p.start.z], yaw: p.yaw ?? p.yawDeg ?? -90, pitch: 0 });
        } else if (mc.room) {
            const seen = new Set();
            for (const p of this.app.presets) { const k = p.map; if (seen.has(k)) continue; seen.add(k); add(k.replace("room:", "room: "), { pos: [...p.scenario.start.pos], yaw: p.scenario.start.yaw || 0, pitch: 0 }); }
        }
        if (mc.map) mc.map.spawns.forEach((s, i) => add(`${s.raw} spawn ${i + 1}`, { pos: [s.x, s.y, s.z], yaw: s.yaw / DEG, pitch: 0 }));
        if (keep && +keep < sel.options.length) sel.value = keep;
    }
    startPos() { const o = $("driveStart").selectedOptions[0]; return o && o._start ? o._start : { pos: [0, 0, 0], yaw: -90, pitch: 0 }; }
    phys() { return $("drivePhys").value === "std" ? this.app.std : this.app.physics; }
    async enter() {
        this.active = true;
        this.scene.setMap(this.app.mapChoice.map);
        if (!this.d) await this.restart();
        this.last = performance.now(); this.acc = 0;
        cancelAnimationFrame(this.raf); this.raf = requestAnimationFrame(this._loop);
    }
    leave() {
        this.active = false; cancelAnimationFrame(this.raf);
        if (document.pointerLockElement === this.c) document.exitPointerLock();
        if (this.freeLook && this.forceLocked) { this.forceLocked = false; this.keys.clear(); this._msg(false); }
    }
    async restart() {
        if (this.d) { this.d.stop(); this.d = null; }
        if (!this.active) return;
        const st = this.startPos();
        this.yaw = st.yaw; this.pitch = 0;
        this.scene.setMap(this.app.mapChoice.map);
        const d = await this.app.helper.drive({ physics: this.phys(), map: this.app.mapChoice, start: { pos: st.pos, yaw: st.yaw, pitch: 0 } }, {
            onState: (s, isStart) => this._state(s, isStart),
            onReady: (dd) => { $("driveVia").textContent = "pmove: " + (this.app.helper.kind === "real" ? "quark_maptool physdrive (" + dd.via + ")" : dd.via); },
            onExit: (x) => { if (this.d === d) { this.d = null; $("driveVia").innerHTML = `<span class="err">the drive helper stopped${x && x.stderr ? ": " + x.stderr.slice(-200) : ""}</span> · Reset starts it again`; } },
            onError: (e) => this.app.toast("drive: " + e, 4000),
        });
        this.d = d;
        this.trail = []; this.evlog = []; this.histN = 0; this.st = { t: 0, pos: [...st.pos], vel: [0, 0, 0], speed: 0, hspeed: 0, dashCharges: 2, flags: 1, onGround: true };
    }
    reset() {
        const st = this.startPos();
        this.yaw = st.yaw; this.pitch = 0; this.trail = []; this.evlog = []; this.histN = 0;
        if (!this.d) { this.restart(); return; }
        this.d.reset({ pos: st.pos, yaw: st.yaw, pitch: 0 });
        this.st = { ...this.st, pos: [...st.pos], vel: [0, 0, 0], speed: 0, hspeed: 0 };
        if (this.app.helper.kind === "real") (this.batches || (this.batches = [])).push(0);   // (the reset's own answer comes back in order with the commands')
    }
    _physics() { if (this.d) { this.d.setPhysics(this.phys()); if (this.app.helper.kind === "real") (this.batches || (this.batches = [])).push(0); } }
    _state(s, isStart) {
        s.recv = performance.now();
        this.st = s;
        if (!isStart) this.pending = Math.max(0, (this.pending || 0) - ((this.batches && this.batches.shift()) || 0));
        if (this.trail.length < 60000 && (!this.trail.length || s.t - this.trail[this.trail.length - 1].t >= 0.0099)) this.trail.push({ t: s.t, pos: [...s.pos], vel: [...s.vel], speed: s.speed, hspeed: s.hspeed, dashCharges: s.dashCharges, onGround: s.onGround, flags: s.flags });
        if (s.events && s.events.length) for (const e of s.events) { this.evlog.push(e); if (this.evlog.length > 400) this.evlog.shift(); this._evDirty = true; }
    }
    _loop(now) {
        if (!this.active) return;
        this.raf = requestAnimationFrame(this._loop);
        const dtF = now - (this.prevFrame || now); this.prevFrame = now;
        this.frames[this.fi++ % this.frames.length] = dtF;
        // mouse: turn the view now
        const sens = +$("driveSens").value || 0.08;
        const y0 = this.yaw, p0 = this.pitch;
        this.yaw -= this.mdx * sens; this.pitch = Math.max(-88.8, Math.min(88.8, this.pitch - this.mdy * sens)); this.mdx = this.mdy = 0;
        // input since the last frame, as 1..8 ms commands
        this.acc += Math.min(250, now - this.last); this.last = now;
        let ms = Math.floor(this.acc);
        const locked = document.pointerLockElement === this.c || !!this.forceLocked;
        if (this.d && ms > 0 && (this.d.inflight() < 12)) {
            this.acc -= ms;
            const k = this.keys;
            const forward = (k.has("KeyW") ? 127 : 0) - (k.has("KeyS") ? 127 : 0), side = (k.has("KeyD") ? 127 : 0) - (k.has("KeyA") ? 127 : 0);
            const buttons = !locked ? 0 : (k.has("Space") ? BTN_JUMP : 0) | (k.has("ShiftLeft") || k.has("ShiftRight") ? BTN_DASH : 0) | (k.has("KeyQ") ? BTN_GRAPPLE : 0) | (k.has("KeyC") || k.has("ControlLeft") ? BTN_CROUCH : 0);
            const cmds = [];
            let done = 0;
            while (done < ms) {
                const n = Math.min(8, ms - done); done += n;
                const f = done / ms;
                cmds.push({ forward: locked ? forward : 0, side: locked ? side : 0, buttons, msec: n, yaw: +(y0 + (this.yaw - y0) * f).toFixed(3), pitch: +(p0 + (this.pitch - p0) * f).toFixed(3) });
            }
            (this.batches || (this.batches = [])).push(ms); if (this.batches.length > 64) this.batches.shift(); this.pending = (this.pending || 0) + ms;
            this.d.send(cmds);
        }
        this._render(now);
        if (now - (this.hudAt || 0) > 50) { this.hudAt = now; this._hud(); }
    }
    _render(now) {
        const s = this.st; if (!s || !this.scene.gl) return;
        // what has been sent but not answered yet: carried on at the last velocity (a few ms; drawn, never fed back)
        const ahead = this.app.helper.kind === "real" ? Math.min(25, this.pending || 0) / 1000 : 0;
        const px = s.pos[0] + s.vel[0] * ahead, py = s.pos[1] + s.vel[1] * ahead, pz = s.pos[2] + s.vel[2] * ahead;
        const yr = this.yaw * DEG, pr = this.pitch * DEG;
        const fx = -Math.sin(yr) * Math.cos(pr), fy = Math.sin(pr), fz = -Math.cos(yr) * Math.cos(pr);
        const sc = this.scene;
        sc.clearDyn();
        let eye, at;
        if ($("driveCam").value === "chase") {
            eye = [px - fx * 5, py + 1.6 - fy * 5 + 0.8, pz - fz * 5]; at = [px, py + 1.2, pz];
            sc.addBox(px - 0.4, py, pz - 0.4, px + 0.4, py + 1.8, pz + 0.4, s.onGround ? [0.35, 0.66, 1] : [0.95, 0.7, 0.3]);
        } else { eye = [px, py + 1.6, pz]; at = [px + fx, py + 1.6 + fy, pz + fz]; }
        if (s.hookPoint && (s.flags & MF_HOOKED)) { const h = s.hookPoint; sc.addBox(h[0] - 0.15, h[1] - 0.15, h[2] - 0.15, h[0] + 0.15, h[1] + 0.15, h[2] + 0.15, [0.7, 0.5, 0.95]); }
        if (this._trailN !== this.trail.length && now - (this._trailAt || 0) > 100) {   // (the trail: 10 Hz)
            this._trailAt = now; this._trailN = this.trail.length;
            const pts = []; const tr = this.trail, from = Math.max(0, tr.length - 3000);
            for (let i = from; i < tr.length; i += 2) pts.push([tr[i].pos[0], tr[i].pos[1] + 0.05, tr[i].pos[2]]);
            sc.setStrips([{ pts, color: hexRgb(cssVar("--run0")) }]);
        }
        const fovH = (+$("driveFov").value || 100) * DEG, asp = this.c.clientWidth / Math.max(1, this.c.clientHeight);
        const fovY = 2 * Math.atan(Math.tan(fovH / 2) / asp);
        sc.render(eye, lookAt(eye, at), fovY, { near: 0.05, far: 2500, fogFar: 450 });
    }
    _hud() {
        const s = this.st; if (!s) return;
        $("hudSpeed").textContent = s.hspeed.toFixed(1);
        $("hudSub").textContent = `${s.speed.toFixed(1)} m/s · up ${s.vel[1].toFixed(1)} · ${s.onGround ? "on the floor" : "in the air"} · y ${s.pos[1].toFixed(2)}`;
        const max = Math.max(1, Math.round(2 * ((this.app.physics.move && this.app.physics.move.dash_charges) || 1)));
        const ch = $("hudCharges"); const want = `${s.dashCharges}/${max}`;
        if (ch.dataset.v !== want) { ch.dataset.v = want; ch.innerHTML = Array.from({ length: Math.min(10, max) }, (_, i) => `<i class="${i < s.dashCharges ? "" : "off"}"></i>`).join(""); }
        // frame times: average fps, 1% and 0.1% lows (the worst frames)
        const n = Math.min(this.fi, this.frames.length), arr = Array.from(this.frames.subarray(0, n)).sort((a, b) => b - a);
        const avg = arr.reduce((a, b) => a + b, 0) / Math.max(1, n);
        const low1 = arr[Math.floor(n * 0.01)] || avg;
        $("driveFps").textContent = `${(1000 / avg).toFixed(0)} fps · 1% low ${(1000 / low1).toFixed(0)}${this.d && this.d.lat ? ` · helper round trip ${this.d.lat.toFixed(1)} ms` : ""}`;
        // speed graph (last 10 s)
        if (this.trail.length) {
            const g = $("driveGraph"), dpr = window.devicePixelRatio || 1, W = g.clientWidth, H = g.clientHeight;
            if (g.width !== Math.round(W * dpr)) { g.width = Math.round(W * dpr); g.height = Math.round(H * dpr); }
            const x = g.getContext("2d"); x.setTransform(dpr, 0, 0, dpr, 0, 0); x.clearRect(0, 0, W, H);
            const tr = this.trail, tEnd = tr[tr.length - 1].t, t0 = tEnd - 10;
            let top = 12; for (let i = tr.length - 1; i >= 0 && tr[i].t >= t0; i--) top = Math.max(top, tr[i].hspeed);
            x.strokeStyle = cssVar("--line"); x.beginPath(); for (let v = 0; v <= top; v += 5) { const y = H - 2 - (H - 6) * v / top; x.moveTo(0, y); x.lineTo(W, y); } x.stroke();
            x.strokeStyle = cssVar("--run0"); x.lineWidth = 1.5; x.beginPath();
            let first = true;
            for (let i = Math.max(0, tr.length - 1200); i < tr.length; i++) { const p = tr[i]; if (p.t < t0) continue; const px = W * (p.t - t0) / 10, py = H - 2 - (H - 6) * p.hspeed / top; first ? x.moveTo(px, py) : x.lineTo(px, py); first = false; }
            x.stroke();
            x.fillStyle = cssVar("--fg3"); x.font = "10px system-ui"; x.fillText(`top ${top.toFixed(1)} m/s (lines every 5)`, 4, 11);
        }
        if (this._evDirty) {
            this._evDirty = false;
            $("driveEvents").textContent = this.evlog.slice(-14).reverse().map((e) => `${(+e.t).toFixed(2)}s ${e.kind}${e.tech ? " (" + e.tech + ")" : ""}${e.hspeed != null ? " " + (+e.hspeed).toFixed(1) + " m/s" : e.speed != null ? " " + (+e.speed).toFixed(1) + " m/s" : ""}${e.impact != null ? " impact " + (+e.impact).toFixed(1) : ""}`).join("\n");
            $("driveEvents").style.whiteSpace = "pre";
        }
    }
    toGraph() {
        if (this.trail.length < 2) return this.app.toast("Drive first: nothing recorded yet");
        const t0 = this.trail[0].t;
        const trace = this.trail.map((p) => ({ ...p, t: p.t - t0 }));
        const events = this.evlog.filter((e) => e.t >= t0).map((e) => ({ ...e, t: e.t - t0 }));
        const run = { label: "drive " + (this.app.extraRuns.length + 1), color: cssVar(["--run4", "--run5", "--run3", "--run2"][this.app.extraRuns.length % 4]), trace, events, drive: true };
        run.metrics = computeMetrics(run, null);
        this.app.extraRuns.push(run);
        this.app.emit("results");
        this.app.toast(`Sent ${trace[trace.length - 1].t.toFixed(1)} s of driving to the Movement graph and Path tabs`);
    }
}
