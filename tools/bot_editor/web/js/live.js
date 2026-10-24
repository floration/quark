// A live (or finished) run: the recorded stream, scrubbing, and drawing bots on the map view: body, view cone, path,
// goal, target line, trail, kill feed, scoreboard and the selected bot's inspector. Frames are kept as they came
// (one JSON object per helper line); the draw loop reads them and allocates nothing per frame beyond a few numbers.

import { TEAM_COLORS } from "./mapview.js";

const BOT_COLORS = ["#f97316", "#22c55e", "#eab308", "#06b6d4", "#e879f9", "#a3e635", "#fb7185", "#60a5fa", "#fbbf24", "#34d399", "#c084fc", "#f472b6"];

export class Recording {
    constructor() { this.start = null; this.frames = []; this.events = []; this.end = null; this.bots = new Map(); this.lastT = 0; }
    push(line) {
        if (line.type === "start") {
            this.start = line;
            for (const b of line.bots || []) this.bots.set(b.id, { ...b, color: BOT_COLORS[this.bots.size % BOT_COLORS.length] });
        } else if (line.type === "frame") {
            if (line.bots) { this.frames.push(line); this.lastT = line.t; }
            for (const e of line.events || []) this.events.push({ ...e, t: line.t });
        } else if (line.type === "end") this.end = line;
    }
    get duration() { return this.lastT; }
    // index of the last frame at or before t
    indexAt(t) {
        const F = this.frames;
        let lo = 0, hi = F.length - 1;
        if (hi < 0) return -1;
        if (t >= F[hi].t) return hi;
        while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (F[mid].t <= t) lo = mid; else hi = mid - 1; }
        return lo;
    }
    scoresAt(t) {
        const s = new Map();
        for (const [id, b] of this.bots) s.set(id, { id, name: b.name, team: b.team, brain: b.brain, kills: 0, deaths: 0, damage: 0 });
        for (const e of this.events) {
            if (e.t > t) break;
            if (e.type === "kill") { const k = s.get(e.killer), v = s.get(e.victim); if (k && e.killer !== e.victim) k.kills++; if (v) v.deaths++; }
            else if (e.type === "hit") { const a = s.get(e.attacker); if (a) a.damage += e.damage || 0; }
        }
        return [...s.values()];
    }
}

const lerp = (a, b, f) => a + (b - a) * f;
const lerpAngle = (a, b, f) => { let d = b - a; while (d > Math.PI) d -= 2 * Math.PI; while (d < -Math.PI) d += 2 * Math.PI; return a + d * f; };

export class LiveView {
    constructor(app, mapView) {
        this.app = app; this.mv = mapView;
        this.rec = null; this.t = 0; this.follow = true; this.sel = -1; this.opts = { trails: true, cones: true, paths: true, followCam: false };
        this._draw = this._draw.bind(this);
        this._screen = this._screen.bind(this);
    }
    attach(rec) { this.rec = rec; this.t = 0; this.follow = true; this.mv.overlays = [this._draw]; this.mv.screenOverlays = [this._screen]; this.mv.animating = true; }
    detach() { this.mv.animating = false; }

    displayTime() { if (!this.rec) return 0; if (this.follow) this.t = this.rec.duration; return this.t; }

    // interpolated state of bot j at time t: writes into `out` (reused)
    botAt(j, t, out) {
        const R = this.rec, i = R.indexAt(t);
        if (i < 0) return null;
        const A = R.frames[i], B = R.frames[Math.min(i + 1, R.frames.length - 1)];
        const a = A.bots[j], b = B.bots[j] && B.bots[j].id === a.id ? B.bots[j] : a;
        const f = B.t > A.t && b.alive === a.alive ? Math.max(0, Math.min(1, (t - A.t) / (B.t - A.t))) : 0;
        out.id = a.id; out.alive = a.alive;
        out.x = lerp(a.pos[0], b.pos[0], f); out.y = lerp(a.pos[1], b.pos[1], f); out.z = lerp(a.pos[2], b.pos[2], f);
        out.yaw = lerpAngle(a.yaw, b.yaw, f);
        out.src = a;
        return out;
    }

    _draw(ctx, mv) {
        const R = this.rec;
        if (!R || !R.frames.length) return;
        const t = this.displayTime();
        const i = R.indexAt(t);
        const F = R.frames[i];
        const px = 1 / mv.cam.s;
        const s = this._tmp || (this._tmp = {});
        const nb = F.bots.length;
        // trails (last 3 s)
        if (this.opts.trails) {
            ctx.lineWidth = 1.5 * px;
            for (let j = 0; j < nb; j++) {
                const info = R.bots.get(F.bots[j].id);
                ctx.strokeStyle = info ? info.color : "#888";
                ctx.globalAlpha = 0.55;
                ctx.beginPath();
                let pen = false;
                for (let k = i; k >= 0 && R.frames[k].t > t - 3; k--) {
                    const b = R.frames[k].bots[j];
                    if (!b || !b.alive) { pen = false; continue; }
                    if (pen) ctx.lineTo(b.pos[0], b.pos[2]); else { ctx.moveTo(b.pos[0], b.pos[2]); pen = true; }
                }
                ctx.stroke();
            }
            ctx.globalAlpha = 1;
        }
        for (let j = 0; j < nb; j++) {
            const b = this.botAt(j, t, s);
            if (!b) continue;
            const src = b.src, info = R.bots.get(b.id) || {};
            const sel = b.id === this.sel;
            if (!b.alive) {
                ctx.strokeStyle = "#6b7280"; ctx.lineWidth = 2 * px;
                const r = 0.5;
                ctx.beginPath(); ctx.moveTo(b.x - r, b.z - r); ctx.lineTo(b.x + r, b.z + r); ctx.moveTo(b.x + r, b.z - r); ctx.lineTo(b.x - r, b.z + r); ctx.stroke();
                continue;
            }
            const team = TEAM_COLORS[info.team] || info.color || "#ccc";
            // path and goal
            if (this.opts.paths && (sel || this.sel < 0)) {
                if (src.path && src.path.length) {
                    ctx.strokeStyle = sel ? "#fde047" : "rgba(253,224,71,0.45)"; ctx.lineWidth = (sel ? 2 : 1.2) * px;
                    ctx.setLineDash([4 * px, 3 * px]);
                    ctx.beginPath(); ctx.moveTo(b.x, b.z);
                    for (const p of src.path) ctx.lineTo(p[0], p[2]);
                    ctx.stroke(); ctx.setLineDash([]);
                }
                if (src.goal) {
                    const g = src.goal, r = 6 * px;
                    ctx.strokeStyle = sel ? "#fde047" : "rgba(253,224,71,0.6)"; ctx.lineWidth = 2 * px;
                    ctx.beginPath(); ctx.moveTo(g[0] - r, g[2] - r); ctx.lineTo(g[0] + r, g[2] + r); ctx.moveTo(g[0] + r, g[2] - r); ctx.lineTo(g[0] - r, g[2] + r); ctx.stroke();
                }
            }
            // view cone
            const fx = -Math.sin(b.yaw), fz = -Math.cos(b.yaw);
            if (this.opts.cones) {
                const fov = src.fov || info.fov || 1.8, len = sel ? 30 : 14;
                const a0 = Math.atan2(fz, fx);
                ctx.fillStyle = sel ? "rgba(255,255,255,0.10)" : "rgba(255,255,255,0.05)";
                ctx.beginPath(); ctx.moveTo(b.x, b.z); ctx.arc(b.x, b.z, len, a0 - fov / 2, a0 + fov / 2); ctx.closePath(); ctx.fill();
            }
            // target line
            if (src.target >= 0) {
                const T = F.bots.find((x) => x.id === src.target);
                if (T && T.alive) {
                    ctx.strokeStyle = "rgba(248,113,113,0.85)"; ctx.lineWidth = 1.5 * px;
                    ctx.beginPath(); ctx.moveTo(b.x, b.z); ctx.lineTo(T.pos[0], T.pos[2]); ctx.stroke();
                }
            }
            // body
            const r = Math.max(0.45, 6 * px);
            ctx.fillStyle = team; ctx.strokeStyle = sel ? "#fff" : "#000"; ctx.lineWidth = (sel ? 2.5 : 1.2) * px;
            ctx.beginPath(); ctx.arc(b.x, b.z, r, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            ctx.strokeStyle = "#fff"; ctx.lineWidth = 2 * px;
            ctx.beginPath(); ctx.moveTo(b.x, b.z); ctx.lineTo(b.x + fx * r * 2, b.z + fz * r * 2); ctx.stroke();
            // health arc
            const hp = Math.max(0, Math.min(1, (src.health || 0) / 100));
            ctx.strokeStyle = hp > 0.5 ? "#4ade80" : hp > 0.25 ? "#facc15" : "#f87171"; ctx.lineWidth = 2 * px;
            ctx.beginPath(); ctx.arc(b.x, b.z, r + 3 * px, -Math.PI / 2, -Math.PI / 2 + hp * Math.PI * 2); ctx.stroke();
        }
        if (this.opts.followCam && this.sel >= 0) {
            const j = F.bots.findIndex((x) => x.id === this.sel);
            if (j >= 0 && this.botAt(j, t, s)) { mv.cam.x = s.x; mv.cam.z = s.z; }
        }
    }

    _screen(ctx, mv) {
        const R = this.rec;
        if (!R || !R.frames.length || !mv.show.labels) return;
        const t = this.displayTime(), s = this._tmp2 || (this._tmp2 = {});
        const F = R.frames[R.indexAt(t)];
        ctx.font = "600 11px system-ui, sans-serif"; ctx.textAlign = "center"; ctx.textBaseline = "bottom";
        for (let j = 0; j < F.bots.length; j++) {
            const b = this.botAt(j, t, s);
            if (!b || !b.alive) continue;
            const info = R.bots.get(b.id) || {};
            const p = mv.toScreen(b.x, b.z);
            const label = info.name || "#" + b.id;
            ctx.lineWidth = 3; ctx.strokeStyle = "rgba(0,0,0,.8)"; ctx.strokeText(label, p.x, p.y - 12);
            ctx.fillStyle = "#fff"; ctx.fillText(label, p.x, p.y - 12);
            if (b.id === this.sel && b.src.decision) {
                ctx.font = "11px ui-monospace, monospace";
                ctx.strokeText(b.src.decision, p.x, p.y + 26); ctx.fillStyle = "#fde68a"; ctx.fillText(b.src.decision, p.x, p.y + 26);
                ctx.font = "600 11px system-ui, sans-serif";
            }
        }
        if (R.start && R.start.mock) {
            ctx.textAlign = "right"; ctx.textBaseline = "bottom"; ctx.fillStyle = "rgba(253,230,138,.8)";
            ctx.fillText("MOCK SIMULATION (not the game's bots)", mv.w - 10, mv.h - 8);
        }
    }

    // nearest live bot to a world point (click to select)
    pick(w) {
        const R = this.rec;
        if (!R || !R.frames.length) return -1;
        const t = this.displayTime(), F = R.frames[R.indexAt(t)], s = {};
        let best = -1, bd = Math.max(1.2, 14 / this.mv.cam.s);
        for (let j = 0; j < F.bots.length; j++) { const b = this.botAt(j, t, s); if (!b) continue; const d = Math.hypot(b.x - w.x, b.z - w.z); if (d < bd) { bd = d; best = b.id; } }
        return best;
    }

    // the right panel: scoreboard, kill feed, inspector (rebuilt ~5 times a second, not every frame)
    renderSide(el) {
        const R = this.rec;
        if (!R) { el.textContent = ""; return; }
        const t = this.displayTime();
        const scores = R.scoresAt(t).sort((a, b) => b.kills - a.kills || a.deaths - b.deaths);
        const esc = (x) => String(x ?? "").replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]));
        let h = `<h3>Score ${R.end ? "(final)" : ""}</h3><table class="score"><tr><th>Bot</th><th>Brain</th><th>K</th><th>D</th><th>Dmg</th></tr>`;
        for (const s of scores) h += `<tr data-id="${s.id}" class="${s.id === this.sel ? "sel" : ""}"><td><span style="color:${TEAM_COLORS[s.team] || (R.bots.get(s.id) || {}).color}">●</span> ${esc(s.name)}</td><td class="muted">${esc(s.brain)}</td><td>${s.kills}</td><td>${s.deaths}</td><td>${Math.round(s.damage)}</td></tr>`;
        h += "</table>";
        // inspector
        const i = R.indexAt(t);
        const F = i >= 0 ? R.frames[i] : null;
        const b = F && F.bots.find((x) => x.id === this.sel);
        h += "<h3>Inspector</h3>";
        if (b) {
            const info = R.bots.get(b.id) || {};
            const tgt = b.target >= 0 ? (R.bots.get(b.target) || {}).name || "#" + b.target : "none";
            const sp = Math.hypot(b.vel[0], b.vel[2]);
            h += `<div class="insp"><span>bot</span><span>${esc(info.name)} (${esc(info.skill)}, ${esc(info.brain)})</span><span>state</span><span>${esc(b.state)}</span>
                <span>health</span><span>${b.health}</span><span>weapon</span><span>${esc(b.weapon)}</span><span>target</span><span>${esc(tgt)}</span>
                <span>position</span><span>${b.pos.map((v) => v.toFixed(1)).join(" ")}</span><span>speed</span><span>${sp.toFixed(1)} m/s</span>
                <span>goal</span><span>${b.goal ? b.goal.map((v) => v.toFixed(1)).join(" ") : "-"}</span><span>path</span><span>${(b.path || []).length} waypoints</span></div>
                <div class="decision">${esc(b.decision || "-")}</div>
                <label class="row muted" style="margin-top:4px"><input type="checkbox" id="followCam" ${this.opts.followCam ? "checked" : ""}> camera follows</label>`;
        } else h += `<div class="muted">Click a bot on the map (or a row above).</div>`;
        // feed
        const kills = [];
        for (let k = R.events.length - 1; k >= 0 && kills.length < 40; k--) { const e = R.events[k]; if (e.t <= t && e.type === "kill") kills.push(e); }
        h += `<h3>Kill feed</h3><div class="feed">`;
        for (const e of kills) {
            const k = R.bots.get(e.killer), v = R.bots.get(e.victim);
            h += `<div><span class="muted">${e.t.toFixed(1)}</span> ${k ? `<b style="color:${TEAM_COLORS[k.team] || k.color}">${esc(k.name)}</b>` : "<i>world</i>"} <span class="muted">[${esc(e.weapon)}]</span> <b style="color:${v ? TEAM_COLORS[v.team] || v.color : "#ccc"}">${esc(v ? v.name : e.victim)}</b></div>`;
        }
        if (!kills.length) h += `<div class="muted">no kills yet</div>`;
        h += "</div>";
        if (R.end) h += `<h3>Result</h3><div>${esc(R.end.reason || "")} · winner: <b>${esc(typeof R.end.winner === "number" ? (R.bots.get(R.end.winner) || {}).name : R.end.winner)}</b></div>`;
        el.innerHTML = h;
        el.querySelectorAll("tr[data-id]").forEach((tr) => (tr.onclick = () => { this.sel = +tr.dataset.id; this.mv.invalidate(); this.renderSide(el); }));
        const fc = el.querySelector("#followCam");
        if (fc) fc.onchange = () => { this.opts.followCam = fc.checked; };
    }
}
