// Range tab: one shooter fires the selected weapon (primary or alt) at still or moving dummy targets on the built-in
// range (a flat floor, lanes at 5 / 10 / 20 / 40 / 80 m) or on any map (in front of its first spawn): perfect, head,
// body or bot aim, for some seconds; the edited set and the comparison set run the same scene, side by side per
// target. Top-down (the bot editor's map view) or 3D replay with shots, projectiles and hit markers on a timeline.
// The game's own server answers through wpnrange when the helper has it; else the page's stand-in (mockrange.js).

import { api } from "./api.js";
import { MapView } from "./mapview.js";
import { Map3D } from "./map3d.js";
import { parseQmap, floorAt } from "./qmap.js";
import { builtinRangeMap, rangeTargets, RANGE_DISTANCES } from "./mockrange.js";
import { WEAPONS } from "./catalog.js";
import { $, esc, el, fmtNum, diffSpan } from "./util.js";

const PRESETS = [["lanes", "lanes 5-80 m, still"], ["strafe", "lanes, strafing 6 m/s"], ["one10", "one at 10 m"], ["one20", "one at 20 m"], ["one40", "one at 40 m"], ["strafe10", "one at 10 m, strafing"], ["group", "three side by side, 12 m"], ["close", "one at 2 m (melee)"]];
const AIMS = [["perfect", "perfect (body)"], ["head", "perfect (head)"], ["bot", "bot (aim error by skill)"]];

export class RangeView {
    constructor(app) {
        this.app = app;
        this.opts = Object.assign({ map: "", weapon: null, alt: false, preset: "lanes", aim: "perfect", skill: 0.7, seconds: 10, compare: true, three: false }, (app.ui && app.ui.range) || {});
        this.res = null; this.cmp = null; this.t = 0; this.playing = false; this.speed = 1;
        this.mv = new MapView($("rangeCanvas"), {});
        this.mv.show.spawns = false;
        this.mv.overlays = [(ctx, mv) => this._draw(ctx, mv)];
        this.mv.screenOverlays = [(ctx, mv) => this._drawScreen(ctx, mv)];
        this.m3 = new Map3D($("range3d"), { bodies: () => this._bodies(), bodiesKey: () => (this.res ? this.t.toFixed(3) + ":" + (this.res.frames.length) : "") });
        this.maps = [];
        this._bar();
        this._timeline();
        this.builtin = parseQmap(builtinRangeMap(), "range");
        this.map = this.builtin; this.mapText = null;
        this.mv.setMap(this.map);
        this._zoomToLanes();
        if (this.m3.gl) this.m3.setMap(this.map);
        app.on("select", (w) => { if (!this.opts.weapon) { this._syncWeapon(); this.res = null; this._side(); } });
        api.maps().then((r) => { this.maps = r.maps || []; this._bar(); if (this.opts.map) this._loadMap(this.opts.map); }).catch(() => {});
    }
    get weapon() { return this.opts.weapon || this.app.sel; }
    _save() { this.app.ui.range = { ...this.opts }; this.app.saveUi(); }
    refresh(from) {
        if (from === "show" || from === "stale") { this.mv.resize(); this._syncWeapon(); }
        if (this.res && from && !["show", "select"].includes(from)) { this._stale = true; this._side(); }   // the set changed: the run is from before
        else this._side();
    }
    shown() { this.mv.resize(); if (!this._zoomed && this.map === this.builtin && this.mv.h > 50) { this._zoomed = true; this._zoomToLanes(); } if (this.opts.three && this.m3.gl) this.m3.start(); if ((!this.res && !this._autoRan) || this._stale) { this._autoRan = true; this.run(); } }
    _syncWeapon() { const s = $("rWeapon"); if (s) s.value = this.opts.weapon || ""; const lbl = $("rWeaponCur"); if (lbl) lbl.textContent = `the selected one (${this.app.sel})`; }

    _bar() {
        const b = $("rangeBar"), o = this.opts;
        b.innerHTML = `<label class="small">map <select id="rMap"><option value="">the built-in range</option>${this.maps.map((m) => `<option value="${esc(m.source + "/" + m.name)}">${esc(m.name)}${m.source !== "default" ? " (" + esc(m.source) + ")" : ""}</option>`).join("")}</select></label>
            <label class="small">weapon <select id="rWeapon"><option value="" id="rWeaponCur">the selected one</option>${WEAPONS.map((w) => `<option value="${w}">${w}</option>`).join("")}</select></label>
            <label class="check"><input type="checkbox" id="rAlt"> alt fire</label>
            <label class="small">targets <select id="rPreset">${PRESETS.map(([k, t]) => `<option value="${k}">${t}</option>`).join("")}</select></label>
            <label class="small">aim <select id="rAim">${AIMS.map(([k, t]) => `<option value="${k}">${t}</option>`).join("")}</select></label>
            <label class="small" id="rSkillBox">skill <input id="rSkill" type="number" min="0" max="1" step="0.1" style="width:52px"></label>
            <label class="small">seconds <input id="rSecs" type="number" min="1" max="60" step="1" style="width:52px"></label>
            <label class="check" title="run the comparison set (standard, or the set chosen on the left) on the same scene"><input type="checkbox" id="rCmp"> compare</label>
            <button id="rRun" class="primary" title="Ctrl+Enter">▶ Fire</button>
            <span class="grow"></span>
            <button id="r2d">Top-down</button><button id="r3d">3D</button>`;
        const sel = (id, k, num) => { const e = $(id); e.value = o[k] ?? ""; e.onchange = () => { o[k] = num ? +e.value : e.value; this._save(); if (k === "map") this._loadMap(e.value); else this.run(); }; };
        sel("rMap", "map"); sel("rPreset", "preset"); sel("rAim", "aim"); sel("rSkill", "skill", true); sel("rSecs", "seconds", true);
        const ws = $("rWeapon"); ws.value = o.weapon || ""; ws.onchange = () => { o.weapon = ws.value || null; this._save(); this.run(); };
        $("rAlt").checked = o.alt; $("rAlt").onchange = () => { o.alt = $("rAlt").checked; this._save(); this.run(); };
        $("rCmp").checked = o.compare; $("rCmp").onchange = () => { o.compare = $("rCmp").checked; this._save(); this.run(); };
        $("rRun").onclick = () => this.run();
        $("r2d").onclick = () => this._three(false); $("r3d").onclick = () => this._three(true);
        $("r2d").classList.toggle("on", !o.three); $("r3d").classList.toggle("on", !!o.three);
        if (!this.m3.gl) $("r3d").disabled = true;
        $("pg-range").classList.toggle("three", !!o.three);
        this._syncWeapon();
    }
    _three(on) { this.opts.three = on; this._save(); $("r2d").classList.toggle("on", !on); $("r3d").classList.toggle("on", on); $("pg-range").classList.toggle("three", on); if (on && this.m3.gl) this.m3.start(); else this.m3.stop(); }
    async _loadMap(v) {
        if (!v) { this.map = this.builtin; this.mapText = null; this.mapName = null; }
        else {
            const [src, ...rest] = v.split("/"); const name = rest.join("/");
            try { this.mapText = await api.map(name, src); this.map = parseQmap(this.mapText, name); this.mapName = name; }
            catch (e) { this.app.toast("can't load " + name + ": " + e.message, 3500); return; }
        }
        this.mv.setMap(this.map);
        if (!v) this._zoomToLanes();
        if (this.m3.gl) this.m3.setMap(this.map);
        this.res = null; this.run();
    }
    _zoomToLanes() { const mv = this.mv; mv.cam.x = 0; mv.cam.z = -42; mv.cam.s = Math.max(2, Math.min(16, ((mv.h || 600) - 40) / 104)); mv.invalidate(true); }
    // the scene: shooter and targets (on a map: in front of its first spawn, on the floor)
    _scene() {
        let targets = this.opts.preset === "close" ? [{ pos: [0, 0, -2] }] : rangeTargets(this.opts.preset);
        let shooter = [0, 0, 0];
        if (this.map !== this.builtin) {
            const sp = this.map.spawns[0] || { x: 0, y: 0, z: 0, yaw: 0 };
            const fx = -Math.sin(sp.yaw), fz = -Math.cos(sp.yaw), rx = Math.cos(sp.yaw), rz = -Math.sin(sp.yaw);
            shooter = [sp.x, sp.y, sp.z];
            targets = targets.map((t) => {
                const d = -t.pos[2], side = t.pos[0];
                const x = sp.x + fx * d + rx * side, z = sp.z + fz * d + rz * side;
                const y = floorAt(this.map, x, z, sp.y + 4);
                return { ...t, pos: [x, y, z], ...(t.moving ? { moving: { ...t.moving, axis: Math.abs(rx) > Math.abs(rz) ? "x" : "z" } } : {}) };
            });
        }
        return { shooter: { pos: shooter, aim: this.opts.aim === "head" ? "head" : this.opts.aim === "bot" ? "bot" : "body", skill: this.opts.skill }, targets };
    }
    async run() {
        const app = this.app;
        if (!app.ast) return;
        const sc = this._scene();
        const req = { weapon: this.weapon, alt: !!this.opts.alt, targets: sc.targets, shooter: sc.shooter, seconds: Math.max(1, Math.min(60, this.opts.seconds || 10)), seed: 1, health: app.health, order: this.opts.preset.startsWith("strafe") || this.opts.preset === "lanes" ? "lanes" : "nearest" };
        const token = (this._tok = (this._tok || 0) + 1);
        $("rangeMsg").textContent = "firing…";
        const notes = [];
        const ctx = (text, ast, values) => ({ text, set: ast, values, map: this.map === this.builtin ? null : this.map, mapText: this.mapText, mapName: this.mapName, onNote: (n) => notes.push(n) });
        const res = await app.helper.range(req, ctx(app.text, app.ast, app.values));
        const cmp = this.opts.compare ? await app.helper.range(req, ctx(app.cmp.text, app.cmp.ast, app.cmp.values)) : null;
        if (token !== this._tok) return;
        $("rangeMsg").textContent = "";
        this.req = req; this.res = res; this.cmp = cmp; this._stale = false; this.notes = notes;
        this.t = 0; this.playing = true; this._last = performance.now();
        $("rTime").max = Math.round(req.seconds * 1000);
        this._side();
        this.mv.animating = true;
    }
    _timeline() {
        $("rPlay").onclick = () => { if (!this.res) return; if (this.t >= this.req.seconds - 1e-3) this.t = 0; this.playing = !this.playing; this._last = performance.now(); };
        $("rTime").oninput = () => { this.t = +$("rTime").value / 1000; this.playing = false; this.mv.invalidate(); this.m3.dirty = true; };
        $("rSpeed").onchange = () => { this.speed = +$("rSpeed").value; };
        const tick = () => {
            requestAnimationFrame(tick);
            if (!this.res || !this.playing) { $("rPlay").textContent = "▶"; return; }
            const now = performance.now();
            this.t = Math.min(this.req.seconds, this.t + (now - this._last) / 1000 * this.speed);
            this._last = now;
            if (this.t >= this.req.seconds) this.playing = false;
            $("rPlay").textContent = this.playing ? "❚❚" : "▶";
            if (document.activeElement !== $("rTime")) $("rTime").value = Math.round(this.t * 1000);
            $("rTlabel").textContent = this.t.toFixed(2) + " / " + this.req.seconds.toFixed(0) + " s";
            this._liveNumbers();
        };
        requestAnimationFrame(tick);
    }
    _frame() {
        const F = this.res && this.res.frames;
        if (!F || !F.length) return null;
        const i = Math.max(0, Math.min(F.length - 1, Math.floor(this.t / ((F[1] ? F[1].t - F[0].t : 0.02) || 0.02))));
        return { F, i, f: F[i] };
    }
    _bodies() {
        const fr = this._frame();
        if (!fr) return [];
        const out = [];
        for (const p of fr.f.players) if (p.alive) out.push({ x: p.pos[0], y: p.pos[1], z: p.pos[2], color: p.shooter ? "#3987e5" : p.health < 50 ? "#e66767" : "#c98500" });
        for (const q of fr.f.projectiles || []) out.push({ x: q.pos[0], y: q.pos[1] - 0.12, z: q.pos[2], color: "#f0efec", hw: 0.12, h: 0.24 });
        return out;
    }
    _draw(ctx, mv) {
        const px = 1 / mv.cam.s;
        // the lanes' distances
        if (this.map === this.builtin) {
            ctx.font = `600 ${13 * px}px ui-monospace, monospace`; ctx.textAlign = "left"; ctx.lineJoin = "round";
            ctx.strokeStyle = "rgba(10,12,16,.85)"; ctx.lineWidth = 3 * px; ctx.fillStyle = "#f0efec";
            for (const d of RANGE_DISTANCES) { ctx.strokeText(d + " m", 8.5, -d + 4 * px); ctx.fillText(d + " m", 8.5, -d + 4 * px); }
        }
        const fr = this._frame();
        if (!fr) return;
        const { F, i, f } = fr;
        // shots of the last 60 ms
        for (let k = Math.max(0, i - 3); k <= i; k++) for (const s of F[k].shots || []) {
            ctx.strokeStyle = s.hit ? "rgba(249,200,80,.9)" : "rgba(200,200,200,.35)"; ctx.lineWidth = 1.5 * px;
            ctx.beginPath(); ctx.moveTo(s.from[0], s.from[2]); ctx.lineTo(s.to[0], s.to[2]); ctx.stroke();
        }
        for (const q of f.projectiles || []) { ctx.fillStyle = q.stuck ? "#e87ba4" : "#f0efec"; ctx.beginPath(); ctx.arc(q.pos[0], q.pos[2], Math.max(0.15, 3 * px), 0, 7); ctx.fill(); }
        f.players.forEach((p, j) => {
            const x = p.pos[0], z = p.pos[2];
            if (p.shooter) {
                ctx.fillStyle = "#3987e5"; ctx.beginPath(); ctx.arc(x, z, 0.5, 0, 7); ctx.fill();
                ctx.strokeStyle = "#3987e5"; ctx.lineWidth = 2 * px; ctx.beginPath(); ctx.moveTo(x, z); ctx.lineTo(x - Math.sin(p.yaw) * 1.6, z - Math.cos(p.yaw) * 1.6); ctx.stroke();
                return;
            }
            if (!p.alive) { ctx.strokeStyle = "#6b7280"; ctx.lineWidth = 2 * px; ctx.beginPath(); ctx.moveTo(x - 0.4, z - 0.4); ctx.lineTo(x + 0.4, z + 0.4); ctx.moveTo(x + 0.4, z - 0.4); ctx.lineTo(x - 0.4, z + 0.4); ctx.stroke(); return; }
            const hp = Math.max(0, Math.min(1, p.health / (this.req.health || 100)));
            ctx.fillStyle = "#c98500"; ctx.fillRect(x - 0.4, z - 0.4, 0.8, 0.8);
            ctx.fillStyle = "#1baf7a"; ctx.fillRect(x - 0.6, z + 0.6, 1.2 * hp, 0.18);
            ctx.strokeStyle = "rgba(0,0,0,.5)"; ctx.lineWidth = px; ctx.strokeRect(x - 0.6, z + 0.6, 1.2, 0.18);
        });
    }
    _drawScreen(ctx, mv) {
        const info = $("rangeInfo");
        if (!this.res) { info.textContent = "wheel zooms · drag pans"; return; }
        info.textContent = `${this.res.via === "wpnrange" ? "the game's server (wpnrange)" : "the page's stand-in (MOCK)"} · ${this.weapon}${this.opts.alt ? " alt" : ""} · ${this.t.toFixed(2)} s`;
    }
    _liveNumbers() {
        if (!this.res || !this._dmgEl) return;
        let d = 0, k = 0;
        for (const s of this.res.shots) { if (s.t > this.t) break; d += s.dmg; }
        this._dmgEl.textContent = `${fmtNum(d)} damage so far`;
    }
    _side() {
        const app = this.app, box = $("rangeSide");
        const w = this.weapon;
        let h = `<h3>${esc(app.titles ? app.titles.get(w) : w)}${this.opts.alt ? " · alt" : ""}</h3>`;
        if (!this.res) { box.innerHTML = h + `<div class="muted">▶ Fire runs the scene${this.app.helper.wpn ? " on the game's server" : " (the page's stand-in: the helper has no wpnrange yet)"}.</div>`; return; }
        const R = this.res, C = this.cmp;
        if (this._stale) h += `<div class="warn small">the set changed since this run: ▶ Fire again</div>`;
        const T = R.totals, CT = C ? C.totals : null;
        h += `<table class="score"><tr><th></th><th>edited</th>${C ? `<th>${esc(app.cmpLabel)}</th>` : ""}</tr>`;
        const line = (lab, a, b, lower) => `<tr><td>${lab}</td><td>${fmtNum(a)}${C ? diffSpan(a, b, lower) : ""}</td>${C ? `<td>${fmtNum(b)}</td>` : ""}</tr>`;
        h += line("shots", T.shots, CT && CT.shots) + line("hits", T.hits, CT && CT.hits) + line("head hits", T.headHits, CT && CT.headHits) + line("damage", T.damage, CT && CT.damage) + line("kills", T.kills, CT && CT.kills) + line("damage / s", T.dps, CT && CT.dps);
        h += `</table><div class="small muted" id="rDmgNow"></div>`;
        h += `<h3>Per target</h3><table class="score"><tr><th>target</th><th>hits</th><th>head</th><th>damage</th><th>kills</th><th>first kill s</th></tr>`;
        R.targets.forEach((t, i) => {
            const c = C ? C.targets[i] : null;
            const d = Math.hypot(t.pos[0] - this.req.shooter.pos[0], t.pos[2] - this.req.shooter.pos[2]);
            const mv = this.req.targets[i].moving ? " ↔" : "";
            h += `<tr><td>${fmtNum(d)} m${mv}</td><td>${t.hits}${c ? diffSpan(t.hits, c.hits) : ""}</td><td>${t.headHits}</td><td>${fmtNum(t.damage)}${c ? diffSpan(t.damage, c.damage) : ""}</td><td>${t.kills}${c ? diffSpan(t.kills, c.kills) : ""}</td>`
                + `<td>${t.firstKill == null ? "–" : fmtNum(t.firstKill)}${c && t.firstKill != null && c.firstKill != null ? diffSpan(t.firstKill, c.firstKill, true) : c && c.firstKill == null && t.firstKill != null ? '<span class="d buff">new</span>' : ""}</td></tr>`;
        });
        h += `</table>`;
        const notes = [...(R.notes || []), ...(this.notes || [])];
        if (notes.length) h += `<div class="small muted" style="margin-top:6px">${notes.map(esc).join("<br>")}</div>`;
        h += `<h3>Hits</h3><div class="feed">${R.shots.filter((s) => s.hit).slice(0, 60).map((s) => `<div>${s.t.toFixed(2)} s · target ${s.target + 1} · ${fmtNum(s.dmg)}${s.head ? " · head" : ""} · ${fmtNum(s.dist)} m</div>`).join("") || '<div class="muted">no hits</div>'}</div>`;
        h += `<div class="small muted" style="margin-top:6px">${R.via === "wpnrange" ? "the game's own server (quark_maptool wpnrange)" : "the page's stand-in: perfect lead and drop, 1 ms steps, 1 s respawn; the game's server replaces it when the helper has wpnrange"}</div>`;
        box.innerHTML = h;
        this._dmgEl = $("rDmgNow");
    }
}
