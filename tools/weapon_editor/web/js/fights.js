// Fights tab: bot vs bot on any map with the weapon set applied: watch one live (the bot editor's live view and
// recording) or run a batch, standard vs edited side by side (win rates, per weapon kills / damage / accuracy /
// time to kill). Runs through quark_maptool sim / simbatch with the set's text (`weapons`), the pool
// (`weaponsAllowed`) and an optional custom mode (`qmode`); no mock: without the helper the tab says what it needs.

import { api } from "./api.js";
import { MapView } from "./mapview.js";
import { Map3D } from "./map3d.js";
import { parseQmap } from "./qmap.js";
import { Recording, LiveView } from "./live.js";
import { RealRun, runBatch, toFightRequest } from "./fight.js";
import { WEAPONS } from "./catalog.js";
import { $, esc, fmtNum, diffSpan } from "./util.js";

const MODES = [["ffa", "free for all"], ["vs", "team vs"], ["duel", "duel (1v1 rounds)"], ["1v1", "1v1"]];
const SKILLS = ["BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"];

export class FightsView {
    constructor(app) {
        this.app = app;
        this.opts = Object.assign({ map: "default/harbor", mode: "ffa", bots: 6, skill: "ADVANCED", secs: 120, matches: 20, pool: [], three: false }, (app.ui && app.ui.fights) || {});
        this.maps = [];
        this.mv = new MapView($("fightCanvas"), { handlers: { click: (w) => { if (this.live.rec) { this.live.sel = this.live.pick ? this.live.pick(w) : -1; this.mv.invalidate(); } } } });
        this.live = new LiveView(app, this.mv);
        this.m3 = new Map3D($("fight3d"), { bodies: () => this._bodies(), bodiesKey: () => (this.live.rec ? this.live.displayTime().toFixed(2) + ":" + this.live.rec.frames.length : "") });
        this.batch = null;
        this._bar();
        api.maps().then(async (r) => { this.maps = r.maps || []; this._bar(); await this._loadMap(this.opts.map); }).catch(() => this._bar());
        $("fPause").onclick = () => { if (!this.run) return; this.paused = !this.paused; this.run.control(this.paused ? "pause" : "resume"); $("fPause").textContent = this.paused ? "▶" : "❚❚"; };
        $("fSpeed").onchange = () => { if (this.run) this.run.control("speed " + $("fSpeed").value); };
        $("fTime").oninput = () => { if (!this.live.rec) return; this.live.follow = +$("fTime").value >= +$("fTime").max - 5; this.live.t = +$("fTime").value / 1000; this.mv.invalidate(); };
        setInterval(() => this._tick(), 200);
    }
    _save() { this.app.ui.fights = { ...this.opts }; this.app.saveUi(); }
    refresh() { this._banner(); this._side(); }
    shown() { this.mv.resize(); if (this.map && !this._fitted && this.mv.h > 50) { this._fitted = true; this.mv.fit(); } if (this.opts.three && this.m3.gl) this.m3.start(); }
    get pool() { return this.opts.pool && this.opts.pool.length ? this.opts.pool : []; }

    _banner() {
        const b = $("fightBanner"), H = this.app.helper;
        if (!H.sim) {
            b.innerHTML = `<b>Fights need the helper.</b> quark_maptool with the bot editor's <code>sim</code> / <code>simbatch</code> runs the game's own bots; ${this.app.config.maptool ? "this quark_maptool has no sim (an older build): rebuild the game (cmake --build build) or update the Quark folder" : "no quark_maptool was found next to the editor (install the editor from the Quark Project app, or set \"maptool\" in weapon_editor.json)"}. The weapon set reaches the bots once sim takes the <code>weapons</code> key (the weapons helper).`;
            b.classList.add("on");
        } else if (this.lastStart && this.lastStart.weaponSet === undefined) {
            b.innerHTML = `<b>This helper ignores the weapon set:</b> sim ran, but its start line has no <code>weaponSet</code>, so the bots fought with the standard weapons. The weapons helper (DESIGN.md section 4) adds it.`;
            b.classList.add("on");
        } else b.classList.remove("on");
        for (const id of ["fLive", "fBatch"]) { const e = $(id); if (e) e.disabled = !H.sim; }
    }
    _bar() {
        const o = this.opts, b = $("fightBar");
        b.innerHTML = `<label class="small">map <select id="fMap">${this.maps.map((m) => `<option value="${esc(m.source + "/" + m.name)}">${esc(m.name)}${m.forMode ? " · " + esc(m.forMode) : ""}</option>`).join("") || '<option value="">(no maps: Generate them in the Quark folder)</option>'}</select></label>
            <label class="small">mode <select id="fMode">${MODES.map(([k, t]) => `<option value="${k}">${t}</option>`).join("")}</select></label>
            <label class="small">bots <input id="fBots" type="number" min="2" max="16" style="width:48px"></label>
            <label class="small">skill <select id="fSkill">${SKILLS.map((s) => `<option>${s}</option>`).join("")}</select></label>
            <label class="small">seconds <input id="fSecs" type="number" min="10" max="900" step="10" style="width:60px"></label>
            <span class="small muted">weapons</span><span class="pool" id="fPool"></span>
            <button id="fLive" class="primary" title="one match with the edited set, live">▶ Watch live</button>
            <label class="small">batch <input id="fMatches" type="number" min="2" max="500" style="width:56px"> matches</label>
            <button id="fBatch" title="the same matches with the comparison set and the edited one, as fast as the computer goes">Run batch: ${esc(this.app.cmpLabel)} vs edited</button>
            <button id="fStop" class="danger">Stop</button>
            <span class="grow"></span><button id="f2d">Top-down</button><button id="f3d">3D</button>`;
        const bind = (id, k, num) => { const e = $(id); e.value = o[k]; e.onchange = () => { o[k] = num ? +e.value : e.value; this._save(); if (k === "map") this._loadMap(e.value); }; };
        bind("fMap", "map"); bind("fMode", "mode"); bind("fBots", "bots", true); bind("fSkill", "skill"); bind("fSecs", "secs", true); bind("fMatches", "matches", true);
        const pool = $("fPool");
        for (const w of WEAPONS.filter((x) => x !== "syringe" && x !== "locator")) {
            const l = document.createElement("label");
            l.innerHTML = `<input type="checkbox" value="${w}" ${!o.pool.length || o.pool.includes(w) ? "checked" : ""}>${w}`;
            l.querySelector("input").onchange = () => { o.pool = [...pool.querySelectorAll("input:checked")].map((x) => x.value); if (o.pool.length === pool.querySelectorAll("input").length) o.pool = []; this._save(); };
            pool.append(l);
        }
        $("fLive").onclick = () => this.watch();
        $("fBatch").onclick = () => this.runBatch();
        $("fStop").onclick = () => { if (this.run) this.run.stop(); if (runBatch.current) runBatch.current.stop(); };
        $("f2d").onclick = () => this._three(false); $("f3d").onclick = () => this._three(true);
        $("f2d").classList.toggle("on", !o.three); $("f3d").classList.toggle("on", !!o.three);
        if (!this.m3.gl) $("f3d").disabled = true;
        $("pg-fights").classList.toggle("three", !!o.three);
        this._banner();
    }
    _three(on) { this.opts.three = on; this._save(); $("f2d").classList.toggle("on", !on); $("f3d").classList.toggle("on", on); $("pg-fights").classList.toggle("three", on); if (on && this.m3.gl) this.m3.start(); else this.m3.stop(); }
    async _loadMap(v) {
        if (!v) return;
        const [src, ...rest] = v.split("/"); const name = rest.join("/");
        if (!this.maps.some((m) => m.name === name)) { const m0 = this.maps.find((m) => m.forMode === "ffa") || this.maps[0]; if (!m0) return; return this._loadMap(m0.source + "/" + m0.name); }
        try {
            const text = await api.map(name, src);
            this.map = parseQmap(text, name); this.map.text = text; this.map.source = src;
            this.mv.setMap(this.map); this._fitted = this.mv.h > 50;
            if (this.m3.gl) this.m3.setMap(this.map);
            this.opts.map = src + "/" + name; const s = $("fMap"); if (s) s.value = this.opts.map;
        } catch (e) { this.app.toast("can't load " + name + ": " + e.message, 3500); }
    }
    _request(text) {
        const o = this.opts, n = Math.max(2, Math.min(16, o.bots | 0));
        const team = (i) => (o.mode === "ffa" ? "any" : i % 2 ? "b" : "a");
        return toFightRequest({ map: { name: this.map.name, source: this.map.source, text: this.map.text }, mode: o.mode, seed: 1, secs: o.secs, speed: $("fSpeed").value === "max" ? "max" : +$("fSpeed").value || 1,
            skill: o.skill, bots: Array.from({ length: n }, (_, i) => ({ name: "bot" + (i + 1), team: team(i), skill: o.skill })), weapons: text, weaponsAllowed: this.pool });
    }
    watch() {
        if (!this.app.helper.sim) return this._banner();
        if (!this.map) { this.app.toast("no map loaded"); return; }
        if (this.run) this.run.stop();
        const rec = new Recording();
        this.live.attach(rec);
        this.paused = false; $("fPause").textContent = "❚❚";
        this.run = new RealRun(this._request(this.app.text), {
            onLine: (l) => { if (l.type === "start") { this.lastStart = l; this._banner(); } if (l.type === "error") this.app.toast("sim: " + l.error, 5000); rec.push(l); },
            onExit: (x) => { if (x && x.code) this.app.toast("sim ended: " + (x.stderr || x.code), 5000); },
            onError: (e) => this.app.toast("sim failed: " + e.message, 5000),
        }, "sim");
    }
    async runBatch() {
        if (!this.app.helper.sim) return this._banner();
        if (!this.map) return;
        const n = Math.max(2, Math.min(500, this.opts.matches | 0));
        this.batch = { running: true, std: null, ed: null, progress: 0, n };
        this._side();
        try {
            this.batch.std = await runBatch(this._request(this.app.cmp.text), n, (o) => { if (o.type === "progress") { this.batch.progress = (o.done ?? o.matches ?? 0) / n / 2; this._side(); } if (o.type === "start") { this.lastStart = o; this._banner(); } });
            this.batch.ed = await runBatch(this._request(this.app.text), n, (o) => { if (o.type === "progress") { this.batch.progress = 0.5 + (o.done ?? o.matches ?? 0) / n / 2; this._side(); } });
        } catch (e) { this.app.toast("batch: " + e.message, 5000); }
        this.batch.running = false;
        this._side();
    }
    _bodies() {
        const L = this.live, R = L.rec;
        if (!R || !R.frames.length) return [];
        const t = L.displayTime(), F = R.frames[R.indexAt(t)], s = {}, out = [];
        for (let j = 0; j < F.bots.length; j++) { const b = L.botAt(j, t, s); if (!b || !b.alive) continue; const info = R.bots.get(b.id) || {}; out.push({ x: b.x, y: b.y, z: b.z, team: info.team, color: info.team === "a" ? "#f05252" : info.team === "b" ? "#3f83f8" : info.color }); }
        return out;
    }
    _tick() {
        if (this.app.page !== "fights") return;
        const R = this.live.rec;
        if (R) {
            const d = Math.max(1, R.duration);
            $("fTime").max = Math.round(d * 1000);
            if (this.live.follow) $("fTime").value = $("fTime").max;
            $("fTlabel").textContent = this.live.displayTime().toFixed(1) + " / " + d.toFixed(1) + " s";
            $("fightInfo").textContent = `${this.map ? this.map.name : ""} · ${R.end ? "finished" : "live"}${this.lastStart && this.lastStart.weaponSet ? " · set " + this.lastStart.weaponSet : ""}`;
        } else $("fightInfo").textContent = this.map ? this.map.name + " · wheel zooms · drag pans" : "";
        this._side();
    }
    // per weapon numbers from a batch end line (the helper's `weapons` list; else from the bots' totals)
    _weaponRows(end) {
        if (!end) return null;
        if (Array.isArray(end.weapons)) return new Map(end.weapons.map((w) => [String(w.weapon).toLowerCase(), w]));
        return null;
    }
    _side() {
        const box = $("fightSide");
        if (!box) return;
        let h = "";
        const B = this.batch;
        if (B) {
            h += `<h3>Batch ${B.running ? `<span class="muted">running ${Math.round(B.progress * 100)}%</span>` : ""}</h3>`;
            const S = B.std, E = B.ed;
            if (S || E) {
                h += `<table class="score"><tr><th></th><th>${esc(this.app.cmpLabel)}</th><th>edited</th></tr>`;
                const tot = (e, k) => (e && e.totals ? e.totals[k] : e && Array.isArray(e.bots) ? e.bots.reduce((a, b) => a + (b[k] || 0), 0) : null);
                for (const [k, lab] of [["kills", "kills"], ["damage", "damage"], ["shots", "shots"], ["hits", "hits"]]) {
                    const a = tot(S, k), b = tot(E, k);
                    h += `<tr><td>${lab}</td><td>${a == null ? "–" : fmtNum(a)}</td><td>${b == null ? "–" : fmtNum(b)}${a != null && b != null ? diffSpan(b, a) : ""}</td></tr>`;
                }
                h += `<tr><td>matches</td><td>${S ? S.matches ?? B.n : "–"}</td><td>${E ? E.matches ?? B.n : "–"}</td></tr></table>`;
                const ws = this._weaponRows(S), we = this._weaponRows(E);
                if (ws || we) {
                    h += `<h3>Per weapon</h3><table class="score"><tr><th>weapon</th><th>kills</th><th>accuracy</th><th>TTK s</th></tr>`;
                    for (const w of WEAPONS) {
                        const a = ws && ws.get(w), b = we && we.get(w);
                        if (!a && !b) continue;
                        const acc = (x) => (x ? (x.accuracy != null ? x.accuracy : x.shots ? x.hits / x.shots : null) : null);
                        h += `<tr><td>${w}</td><td>${b ? b.kills : "–"}${a && b ? diffSpan(b.kills, a.kills) : ""}</td><td>${acc(b) == null ? "–" : Math.round(acc(b) * 100) + "%"}</td><td>${b && b.ttk != null ? fmtNum(b.ttk) : "–"}${a && b && a.ttk != null && b.ttk != null ? diffSpan(b.ttk, a.ttk, true) : ""}</td></tr>`;
                    }
                    h += `</table>`;
                } else if (S || E) h += `<div class="muted small">per weapon numbers come when simbatch's end line has <code>weapons</code> (the weapons helper)</div>`;
            }
        }
        const live = document.createElement("div");
        if (this.live.rec) this.live.renderSide(live);
        else live.innerHTML = `<h3>Live</h3><div class="muted">▶ Watch live runs one match with the edited set${this.app.helper.sim ? "" : " (needs the helper)"}. A batch runs the comparison set and the edited set on the same seeds.</div>`;
        box.innerHTML = h;
        box.append(live);
    }
}
