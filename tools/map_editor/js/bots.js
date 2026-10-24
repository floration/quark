// Bots: place bot starts, run a headless match with the game's bots in quark_maptool (live: `bots --stream`, the
// samples drawn as they arrive; or the whole match first), then play it back in 3D: a timeline to scrub, speed, trails,
// per-bot toggles, each bot's path and goal, event markers. Kept apart from the rest so the bot editor can grow here.
import * as THREE from "../vendor/three.module.min.js";
import { el, fill, $, select, check, numInput, banner, fmt } from "./util.js";
import { SegmentBuffer, label } from "./lines.js";
import { MODES } from "./fallback_parse.js";

const SKILLS = ["beginner", "intermediate", "advanced", "expert"];
const EVENT_COLORS = { kill: "#ff4a4a", death: "#9aa3b0", spawn: "#7cf0ff", score: "#ffd23f", round: "#ffffff", objective: "#3fff8a", phase: "#b57aff" };
const BOT_BODY = new THREE.BoxGeometry(0.8, 1.8, 0.8).translate(0, 0.9, 0), BOT_NOSE = new THREE.BoxGeometry(0.2, 0.15, 0.5).translate(0, 1.6, -0.55);
const GOAL = new THREE.OctahedronGeometry(0.35), EVENT_BALL = new THREE.SphereGeometry(0.4, 10, 8), NOSE_MAT = new THREE.MeshBasicMaterial({ color: 0xffffff });
for (const g of [BOT_BODY, BOT_NOSE, GOAL, EVENT_BALL]) g.userData.shared = true;
NOSE_MAT.userData.shared = true;
const BOT_HUES = { a: [0.58, 0.62, 0.54, 0.66, 0.5], b: [0.0, 0.04, 0.96, 0.08, 0.92] };

function botColor(team, id) {
  const h = (BOT_HUES[team] || null);
  if (h) return new THREE.Color().setHSL(h[id % h.length], 0.75, 0.55 + 0.08 * ((id / h.length | 0) % 3));
  return new THREE.Color().setHSL(((id * 0.618) % 1), 0.7, 0.58);
}

export class Bots {
  constructor(app) {
    this.app = app; this.doc = app.doc; this.view = app.view; this.api = app.api;
    this.panel = $("#rp-bots"); this.dock = $("#dockBottom");
    this.opts = { mode: "ffa", bots: 4, seconds: 60, skill: "intermediate", useStarts: true, simSpeed: 0 };
    this.play = { t: 0, playing: false, speed: 1, follow: true, trails: true, trailS: 6, paths: false, events: true, hidden: new Set(), cam: null };
    this.run = null; this.live = null;
    this.tick = this.tick.bind(this);
    this.doc.on("open", () => { this.close(); const sc = this.doc.sidecar.bots; if (sc) Object.assign(this.opts, sc); else this.opts.mode = this.doc.map.preferredMode || "ffa"; this.render(); });
    this.doc.on("map", (keys) => { if (keys && keys.includes("sc.botStarts")) this.render(); });
    this.render();
  }
  get playing() { return !!this.run && this.play.playing; }

  // ---- running
  request() {
    const o = this.opts;
    const req = { mode: o.mode, bots: o.bots, seconds: o.seconds, skill: o.skill };
    req.speed = o.simSpeed || 0;   // (0: as fast as it goes; a live run left without it would be real time)
    if (o.useStarts && (this.doc.sidecar.botStarts || []).length) req.placed = this.doc.sidecar.botStarts.map(b => ({ pos: b.pos, yaw: b.yaw, team: b.team || "any" }));
    return req;
  }
  // the options are kept in the sidecar: a change marks the map unsaved (no undo step: they are settings, not the map)
  saveOpts() {
    const d = this.doc, next = { ...this.opts };
    if (JSON.stringify(d.sidecar.bots) === JSON.stringify(next)) return;
    d.sidecar.bots = next; d.seq++; d.emit("dirty");
  }
  newRun(meta) {
    return { meta, rate: meta.rate || 10, bots: (meta.bots || []).map((b, j) => ({ ...b, id: b.id ?? j, color: botColor(b.team, b.id ?? j) })), frames: [], events: [], done: false, result: null, seconds: meta.seconds || this.opts.seconds };
  }
  async start(liveMode) {
    if (!this.api.hasTool) { banner("quark_maptool isn't there: build it and rebuild the editor", true); return; }
    this.stop();
    let text;
    try { text = (await this.api.tool("serialize", this.app.mapForSave())).text; } catch (e) { banner("bots: " + e.message, true); return; }
    const req = { text, name: this.doc.name || "untitled", ...this.request() };
    if (!liveMode) {
      this.status = "running " + req.seconds + " s of bots..."; this.render();
      try {
        const r = await this.api.tool("bots", req);
        this.run = this.fromTraces(r); this.status = null;
        this.openDock(); this.play.t = 0; this.play.playing = true; this.render();
      } catch (e) { this.status = null; banner("bots failed: " + e.message, true, 0); this.render(); }
      return;
    }
    this.status = "live"; this.render();
    const s = this.api.streamBots(req, (msg) => this.onMsg(msg));
    this.live = s;
    s.done.then(() => { if (this.run) this.run.done = true; }).catch(e => { if (e.name !== "AbortError") banner("bots stream: " + e.message, true, 0); })
      .finally(() => { this.live = null; this.status = null; this.render(); this.renderDock(); });
  }
  onMsg(m) {
    if (m.type === "start") { this.run = this.newRun(m); this.openDock(); this.play.t = 0; this.play.playing = true; this.play.liveFollow = true; this.render(); return; }
    if (!this.run) return;
    if (m.type === "sample") {
      const B = {};
      for (const b of m.bots || []) B[b.id] = b;
      this.run.frames.push({ t: m.t, B });
      if (this.play.liveFollow) this.play.t = m.t;
    } else if (m.type === "event") this.run.events.push(m);
    else if (m.type === "done") { this.run.done = true; this.run.result = m.result || null; this.run.seconds = m.t || this.run.seconds; this.render(); }
    else if (m.type === "error") banner("bots: " + m.error, true, 0);
    this.dockDirty = true;
  }
  // the non-stream answer: {rate, bots:[{name, team, trace:[[t,x,y,z,yaw,state]...]}], events, result: {score, players, ...}}
  fromTraces(r) {
    const run = this.newRun({ rate: r.rate || 10, bots: (r.bots || []).map((b, j) => ({ id: b.id ?? j, name: b.name, team: b.team, skill: b.skill })), seconds: this.opts.seconds });
    const times = new Map();
    (r.bots || []).forEach((b, j) => {
      for (const s of b.trace || []) {
        const t = Math.round(s[0] * 1000) / 1000;
        let f = times.get(t); if (!f) times.set(t, f = { t, B: {} });
        f.B[b.id ?? j] = { pos: [s[1], s[2], s[3]], yaw: s[4], state: s[5], alive: s[5] !== "dead" };
      }
    });
    run.frames = [...times.values()].sort((a, b) => a.t - b.t);
    run.events = r.events || [];
    run.done = true; run.result = r.result || { score: r.score, players: r.players };
    if (run.frames.length) run.seconds = run.frames[run.frames.length - 1].t;
    return run;
  }
  stop() { if (this.live) { this.live.ctl("stop"); setTimeout(() => this.live && this.live.abort(), 1500); } }
  close() { this.stop(); this.run = null; this.objs = null; this.play.playing = false; this.view.anim.delete(this.tick); this.view.clearLayer("bots"); this.dock.hidden = true; this.view.resize(); }

  // ---- playback
  openDock() { this.dock.hidden = false; this.view.resize(); this.view.anim.add(this.tick); this.renderDock(); }
  frameAt(t) {   // [frame before, frame after, fraction]
    const F = this.run.frames; if (!F.length) return null;
    let lo = 0, hi = F.length - 1;
    if (t <= F[0].t) return [F[0], F[0], 0];
    if (t >= F[hi].t) return [F[hi], F[hi], 0];
    while (hi - lo > 1) { const mid = (lo + hi) >> 1; if (F[mid].t <= t) lo = mid; else hi = mid; }
    return [F[lo], F[hi], (t - F[lo].t) / Math.max(1e-6, F[hi].t - F[lo].t)];
  }
  end() { const F = this.run.frames; return F.length ? F[F.length - 1].t : 0; }
  tick(dt) {
    if (!this.run) return false;
    const P = this.play;
    if (P.playing) {
      P.t += dt * P.speed;
      const end = this.end();
      if (P.t >= end) { P.t = end; if (this.run.done) P.playing = false; else P.liveFollow = true; }
      else if (this.live) P.liveFollow = end - P.t < 0.25;
    }
    const changed = P.playing || this.dockDirty || P.lastDrawn !== P.t;
    if (changed) { this.draw(); this.drawScrub(); this.dockDirty = false; P.lastDrawn = P.t; }
    return P.playing || !!this.live;
  }
  // Playback draws into objects kept between frames: per bot a body, a nose and a label (its text remade only when
  // it changes), one instanced mesh for every trail / path segment, a pool for event markers. A frame writes
  // positions and colours; nothing is made or thrown away while it plays (no garbage-collection hitches).
  pool() {
    const L = this.view.layers.bots;
    if (this.objs && this.objs.run === this.run) return this.objs;
    this.view.clearLayer("bots");
    const o = this.objs = { run: this.run, bots: new Map(), events: new Map(), segs: new SegmentBuffer({ width: 0.09 }), goals: new Map() };
    L.add(o.segs.mesh);
    for (const bot of this.run.bots) {
      const g = new THREE.Group();
      const body = new THREE.Mesh(BOT_BODY, new THREE.MeshBasicMaterial({ color: bot.color, transparent: true, opacity: 1 }));
      g.add(body, new THREE.Mesh(BOT_NOSE, NOSE_MAT));
      L.add(g);
      o.bots.set(bot.id, { g, body, label: null, text: "" });
    }
    return o;
  }
  draw() {
    const run = this.run, P = this.play, L = this.view.layers.bots;
    const fr = this.frameAt(P.t); if (!fr) return;
    const o = this.pool();
    const [a, b, f] = fr;
    const segs = o.segs.begin(), goalsSeen = new Set();
    for (const bot of run.bots) {
      const B = o.bots.get(bot.id);
      const sa = a.B[bot.id], sb = b.B[bot.id] || sa;
      if (P.hidden.has(bot.id) || !sa) { B.g.visible = false; continue; }
      B.g.visible = true;
      const pos = sa.pos.map((v, k) => v + (sb.pos[k] - v) * (sa.alive !== false && sb.alive !== false ? f : 0));
      let dy = (sb.yaw ?? 0) - (sa.yaw ?? 0); if (dy > Math.PI) dy -= 2 * Math.PI; if (dy < -Math.PI) dy += 2 * Math.PI;
      B.g.position.fromArray(pos); B.g.rotation.y = (sa.yaw ?? 0) + dy * f;
      const dead = sa.alive === false || sa.state === "dead";
      B.body.material.opacity = dead ? 0.25 : 1; B.body.material.depthWrite = !dead;
      const text = bot.name + (sa.state ? " " + sa.state : "") + (sa.health !== undefined ? " " + sa.health : "");
      if (text !== B.text) {
        if (B.label) { B.g.remove(B.label); B.label.material.dispose(); }   // (the texture is cached by text)
        B.label = label(text, "#" + bot.color.getHexString(), 0.6); B.label.position.set(0, 2.4, 0); B.g.add(B.label); B.text = text;
      }
      if (P.trails) {   // the last few seconds of positions
        const F = run.frames, i = F.indexOf(a);
        let prev = [pos[0], pos[1] + 0.1, pos[2]];
        for (let k = i; k >= 0 && F[k].t >= P.t - P.trailS; k--) {
          const s = F[k].B[bot.id]; if (!s) break;
          const p = [s.pos[0], s.pos[1] + 0.1, s.pos[2]];
          if (Math.hypot(p[0] - prev[0], p[2] - prev[2]) < 8) segs.add(p, prev, bot.color, 0.35 + 0.65 * (1 - (P.t - F[k].t) / P.trailS));
          prev = p;
        }
      }
      if (P.paths && sa.path && sa.path.length > 1) {
        for (let k = 0; k + 1 < sa.path.length; k++) segs.add([sa.path[k][0], sa.path[k][1] + 0.3, sa.path[k][2]], [sa.path[k + 1][0], sa.path[k + 1][1] + 0.3, sa.path[k + 1][2]], bot.color, 1, 0.5);
      }
      if (P.paths && sa.goal) {
        let gm = o.goals.get(bot.id);
        if (!gm) { gm = new THREE.Mesh(GOAL, new THREE.MeshBasicMaterial({ color: bot.color, wireframe: true })); o.goals.set(bot.id, gm); L.add(gm); }
        gm.visible = true; gm.position.set(sa.goal[0], sa.goal[1] + 0.5, sa.goal[2]); goalsSeen.add(bot.id);
      }
    }
    for (const [id, gm] of o.goals) if (!goalsSeen.has(id)) gm.visible = false;
    segs.end();
    // events of the last 3 s: a fading ball and a label each, made when an event first shows, dropped when it leaves
    const live = new Set();
    if (P.events) for (const e of run.events) {
      if (!e.pos || e.t > P.t || e.t < P.t - 3) continue;
      live.add(e);
      let m = o.events.get(e);
      if (!m) {
        const col = EVENT_COLORS[e.kind] || "#fff";
        m = { ball: new THREE.Mesh(EVENT_BALL, new THREE.MeshBasicMaterial({ color: col, transparent: true })), label: label(e.kind + (e.detail ? " " + e.detail : ""), col, 0.4) };
        m.ball.position.set(e.pos[0], e.pos[1] + 1, e.pos[2]); m.label.position.set(e.pos[0], e.pos[1] + 1.8, e.pos[2]);
        L.add(m.ball, m.label); o.events.set(e, m);
      }
      m.ball.material.opacity = 1 - (P.t - e.t) / 3;
    }
    for (const [e, m] of o.events) if (!live.has(e)) { L.remove(m.ball, m.label); m.ball.material.dispose(); m.label.material.dispose(); o.events.delete(e); }
    if (P.cam !== null && P.cam !== undefined) {   // follow a bot with the orbit camera
      const s = a.B[P.cam];
      if (s) { const v = this.view; if (v.camMode !== "orbit") this.app.setCam("orbit"); v.cam.target.set(s.pos[0], s.pos[1] + 1, s.pos[2]); }
    }
    this.view.redraw();
    const tl = this.dock.querySelector(".tlabel");
    if (tl) tl.textContent = P.t.toFixed(1) + " / " + this.end().toFixed(1) + " s" + (this.live ? "  LIVE" : "");
  }

  // ---- the dock (timeline)
  renderDock() {
    const P = this.play, run = this.run; if (!run) return;
    const playBtn = el("button", { class: "primary", onclick: () => { if (!P.playing && P.t >= this.end() && run.done) P.t = 0; P.playing = !P.playing; P.liveFollow = false; this.renderDock(); } }, P.playing ? "Pause" : "Play");
    const row = el("div", { class: "row1" }, playBtn,
      el("span", { class: "tlabel num" }, ""),
      "speed", select([0.25, 0.5, 1, 2, 4, 8].map(s => [s, s + "x"]), P.speed, v => { P.speed = +v; }),
      check("trails", P.trails, v => { P.trails = v; this.dockDirty = true; }), numInput(P.trailS, v => { P.trailS = Math.max(0.5, v); this.dockDirty = true; }, { step: 1, width: "48px" }), "s",
      check("paths + goals", P.paths, v => { P.paths = v; this.dockDirty = true; }),
      check("events", P.events, v => { P.events = v; this.dockDirty = true; }),
      "follow", select([["", "(free camera)"], ...run.bots.map(b => [b.id, b.name])], P.cam ?? "", v => { P.cam = v === "" ? null : +v; this.dockDirty = true; }),
      el("span", { class: "spacer" }),
      this.live ? el("button", { onclick: () => this.live.ctl(this.simPaused ? "resume" : "pause").then(() => { this.simPaused = !this.simPaused; this.renderDock(); }) }, this.simPaused ? "Resume match" : "Pause match") : null,
      this.live ? el("button", { onclick: () => this.live.ctl("step") }, "Step") : null,
      this.live ? el("button", { class: "danger", onclick: () => this.stop() }, "Stop match") : null,
      el("button", { onclick: () => this.close() }, "Close"));
    const scrub = el("div", { class: "scrub" }, el("canvas", {}));
    const seek = (e) => { const r = scrub.getBoundingClientRect(); const dur = Math.max(this.end(), run.seconds || 0, 1e-3); P.t = Math.max(0, Math.min(this.end(), (e.clientX - r.left) / r.width * dur)); P.liveFollow = false; this.dockDirty = true; };
    scrub.addEventListener("mousedown", (e) => { seek(e); const mv = (ev) => seek(ev); const up = () => { window.removeEventListener("mousemove", mv); window.removeEventListener("mouseup", up); }; window.addEventListener("mousemove", mv); window.addEventListener("mouseup", up); });
    const bots = el("div", { class: "bots" }, run.bots.map(b => check(el("span", {}, el("span", { class: "swatch", style: { background: "#" + b.color.getHexString() } }), " " + b.name + " (" + b.team + ")"), !P.hidden.has(b.id), v => { if (v) P.hidden.delete(b.id); else P.hidden.add(b.id); this.dockDirty = true; })));
    fill(this.dock, el("div", { class: "timeline" }, row, scrub, bots));
    this.dockDirty = true;
    this.view.redraw();
  }
  drawScrub() {
    const c = this.dock.querySelector(".scrub canvas"); if (!c || !this.run) return;
    const w = c.clientWidth, h = c.clientHeight; if (c.width !== w) c.width = w; if (c.height !== h) c.height = h;
    const g = c.getContext("2d"), run = this.run, dur = Math.max(this.end(), run.seconds || 0, 1e-3);
    g.clearRect(0, 0, w, h);
    g.fillStyle = "#2a3a52"; g.fillRect(0, 0, this.end() / dur * w, h);   // what has arrived (live) / the run
    g.fillStyle = "#6f7682"; g.font = "10px monospace";
    for (let s = 0; s <= dur; s += dur > 120 ? 30 : dur > 30 ? 10 : 5) { const x = s / dur * w; g.fillRect(x, h - 6, 1, 6); g.fillText(s + "s", x + 2, h - 1); }
    for (const e of run.events) { const x = e.t / dur * w; g.fillStyle = EVENT_COLORS[e.kind] || "#fff"; g.fillRect(x - 1, 2, 2, h - 14); }
    const x = this.play.t / dur * w;
    g.fillStyle = "#fff"; g.fillRect(x - 1, 0, 2, h);
  }

  // ---- the side panel
  render() {
    const o = this.opts, P = this.panel, d = this.doc, tool = this.api.hasTool;
    const starts = (d.sidecar && d.sidecar.botStarts) || [];
    const out = [el("h3", {}, "Bot match")];
    if (!tool) out.push(el("p", { class: "warn" }, "quark_maptool isn't next to serve.py: build it to run bots."));
    out.push(el("div", { class: "form" },
      el("label", {}, "mode"), select(MODES, o.mode, v => { o.mode = v; this.saveOpts(); }),
      el("label", {}, "bots"), numInput(o.bots, v => { o.bots = Math.max(1, Math.min(32, Math.round(v))); this.saveOpts(); }, { step: 1 }),
      el("label", {}, "seconds"), numInput(o.seconds, v => { o.seconds = Math.max(1, Math.min(1800, v)); this.saveOpts(); }, { step: 10 }),
      el("label", {}, "skill"), select(SKILLS, o.skill, v => { o.skill = v; this.saveOpts(); }),
      el("label", { title: "live runs: how fast the match runs (0 = as fast as it can)" }, "live speed"), select([[0, "as fast as it can"], [1, "real time"], [2, "2x"], [4, "4x"]], o.simSpeed, v => { o.simSpeed = +v; this.saveOpts(); }),
      el("label", {}, ""), check("start at the placed bot starts (" + starts.length + ")", o.useStarts, v => { o.useStarts = v; this.saveOpts(); this.render(); })));
    if (o.useStarts && starts.length) out.push(el("p", { class: "faint" }, "The placed bot starts add " + starts.length + " bot" + (starts.length > 1 ? "s" : "") + " to the " + o.bots + " above: " + (o.bots + starts.length) + " in the match."));
    out.push(el("div", { class: "btnrow" },
      el("button", { class: "primary", disabled: !tool || !!this.status, onclick: () => this.start(true) }, "Run live"),
      el("button", { disabled: !tool || !!this.status, onclick: () => this.start(false) }, "Run, then play"),
      this.live ? el("button", { class: "danger", onclick: () => this.stop() }, "Stop") : null));
    if (this.status) out.push(el("p", { class: "warn" }, this.status));
    out.push(el("h3", {}, "Bot starts", el("span", { class: "spacer" }), el("button", { onclick: () => this.app.setTool("botstart") }, "Place"),
      starts.length ? el("button", { onclick: () => d.edit("clear bot starts", ["sc.botStarts"], (m, sc) => { sc.botStarts = []; }, { touch: [] }) }, "Clear") : null));
    out.push(el("p", { class: "faint" }, "Click the floor with the Bot tool (team from the toolbar). Saved with the map; Q / E turn the selected one."));
    starts.forEach((b, i) => out.push(el("div", { class: "row", style: { display: "flex", gap: "6px", cursor: "pointer" }, onclick: () => { d.select([{ k: "botstart", i }]); this.app.frameSel(); } },
      el("span", {}, "#" + (i + 1) + " " + (b.team || "any")), el("span", { class: "faint num" }, b.pos.map(v => fmt(v, 1)).join(" ")))));
    if (this.run && this.run.result) {
      const R = this.run.result;
      out.push(el("h3", {}, "Result"));
      if (R.score) out.push(el("p", {}, "score " + R.score.join(" : ")));
      if (R.players) out.push(el("table", { class: "t" }, el("tr", {}, el("th", {}, "bot"), el("th", {}, "team"), el("th", { class: "n" }, "kills"), el("th", { class: "n" }, "deaths")),
        R.players.map(p => el("tr", {}, el("td", {}, p.name), el("td", {}, p.team), el("td", { class: "n" }, String(p.kills ?? "")), el("td", { class: "n" }, String(p.deaths ?? ""))))));
      const kinds = {}; for (const e of this.run.events) kinds[e.kind] = (kinds[e.kind] || 0) + 1;
      out.push(el("p", { class: "faint" }, Object.entries(kinds).map(([k, n]) => n + " " + k).join(", ")));
    }
    fill(P, ...out);
  }
}
