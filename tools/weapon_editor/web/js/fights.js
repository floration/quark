// Fights tab: bot vs bot on any map with the weapon set applied: watch one live (the bot editor's live view and
// recording) or run a batch, standard vs edited side by side (match results, per weapon kills / deaths / fights won /
// damage / accuracy / time to kill / DPS) in any mode the game has. Runs through quark_maptool sim / simbatch with the set's text (`weapons`), the pool
// (`weaponsAllowed`) and an optional custom mode (`qmode`); no mock: without the helper the tab says what it needs.

import { api } from "./api.js";
import { MapView } from "./mapview.js";
import { Map3D } from "./map3d.js";
import { parseQmap, mapSupports } from "./qmap.js";
import { Recording, LiveView } from "./live.js";
import { RealRun, runBatch, toFightRequest } from "./fight.js";
import { WEAPONS } from "./catalog.js";
import { $, esc, fmtNum, diffSpan } from "./util.js";

// every mode bots play (race has no fights); a map built for another mode may not work in it: the bar says so
const MODES = [["ffa", "free for all"], ["vs", "team vs"], ["duel", "duel (1v1 rounds)"], ["1v1", "1v1"], ["tourney", "tourney"], ["koth", "king of the hill"],
    ["bomb", "bomb"], ["payload", "payload"], ["npayload", "payload (both ways)"], ["points", "points"], ["tag", "tag"], ["hide", "hide and seek"]];
const TEAM_MODES = new Set(["vs", "koth", "bomb", "payload", "npayload", "points"]);
const SKILLS = ["BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"];
// a batch's game seconds per match: by default each match runs to its own end (the game's clock: an FFA match is
// 600 s, a koth match on summit took 590-660 s, duel 320-450 s), capped at WHOLE_MATCH; a shorter cap ends matches
// early and their winner is whoever was ahead (the leader)
const WHOLE_MATCH = 1800;
const BATCH_SECS = [[0, "whole match"], [60, "60 s"], [120, "120 s"], [300, "300 s"], [600, "600 s"], [900, "900 s"]];

export class FightsView {
    constructor(app) {
        this.app = app;
        this.opts = Object.assign({ map: "default/harbor", mode: "ffa", bots: 6, skill: "ADVANCED", secs: 120, batchSecs: 0, matches: 20, pool: [], three: false }, (app.ui && app.ui.fights) || {});
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
        // a reload or a closed tab must not leave quark_maptool playing on (a live sim runs up to its seconds, a batch
        // every match): stop both as the page goes (keepalive: the request outlives the page)
        window.addEventListener("pagehide", () => {
            for (const r of [this.run, runBatch.current]) if (r && r.id && !r.closed)
                fetch(`/api/run/${r.id}/stop`, { method: "POST", headers: { "X-Quark-Editor": "1" }, keepalive: true }).catch(() => {});
        });
    }
    _save() { this.app.ui.fights = { ...this.opts }; this.app.saveUi(); }
    refresh() { this._banner(); this._side(); }
    shown() { this.mv.resize(); if (this.map && !this._fitted && this.mv.h > 50) { this._fitted = true; this.mv.fit(); } if (this.opts.three && this.m3.gl) this.m3.start(); }
    get pool() { return this.opts.pool && this.opts.pool.length ? this.opts.pool : []; }

    _banner() {
        const b = $("fightBanner"), H = this.app.helper, start = this.lastStart || this.helperStart;
        if (!H.sim) {
            const why = H.mockWanted ? "the page is in MOCK mode (<code>?mock</code> in the address): open it without <code>?mock</code> for fights"
                : this.app.config.maptool ? "this quark_maptool has no sim (an older build): rebuild the game (cmake --build build) or update the Quark folder"
                : "no quark_maptool was found next to the editor (install the editor from the Quark Project app, or set \"maptool\" in weapon_editor.json)";
            b.innerHTML = `<b>Fights need the helper.</b> quark_maptool with the bot editor's <code>sim</code> / <code>simbatch</code> runs the game's own bots; ${why}.`;
            b.classList.add("on");
        } else if (start && start.weaponSet === undefined) {
            b.innerHTML = `<b>This helper ignores the weapon set:</b> sim ran, but its start line has no <code>weaponSet</code>, so the bots fought with the standard weapons. The weapons helper (DESIGN.md section 4) adds it.`;
            b.classList.add("on");
        } else b.classList.remove("on");
        for (const id of ["fLive", "fBatch"]) { const e = $(id); if (e) e.disabled = !H.sim; }
    }
    _bar() {
        const o = this.opts, b = $("fightBar");
        b.innerHTML = `<label class="small">map <select id="fMap">${this.maps.map((m) => `<option value="${esc(m.source + "/" + m.name)}">${esc(m.name)}${m.for ? " · " + esc(m.for) : ""}</option>`).join("") || '<option value="">(no maps: Generate them in the Quark folder)</option>'}</select></label>
            <label class="small">mode <select id="fMode">${MODES.map(([k, t]) => `<option value="${k}">${t}</option>`).join("")}</select></label>
            <label class="small">bots <input id="fBots" type="number" min="2" max="16" style="width:48px"></label>
            <label class="small">skill <select id="fSkill">${SKILLS.map((s) => `<option>${s}</option>`).join("")}</select></label>
            <label class="small">seconds <input id="fSecs" type="number" min="10" max="900" step="10" style="width:60px"></label>
            <span class="small muted">weapons</span><span class="pool" id="fPool"></span>
            <button id="fLive" class="primary" title="one match with the edited set, live">▶ Watch live</button>
            <label class="small" title="each match runs to its own end (the game's clock), or is cut at these seconds: then whoever was ahead wins it">batch <input id="fMatches" type="number" min="2" max="500" style="width:56px"> matches of <select id="fBSecs">${BATCH_SECS.map(([v, t]) => `<option value="${v}">${t}</option>`).join("")}</select></label>
            <button id="fBatch" title="the same matches with the comparison set and the edited one, as fast as the computer goes">Run batch: ${esc(this.app.cmpLabel)} vs edited</button>
            <button id="fStop" class="danger">Stop</button>
            <span class="small warn" id="fFit"></span>
            <span class="grow"></span><button id="f2d">Top-down</button><button id="f3d">3D</button>`;
        const bind = (id, k, num) => { const e = $(id); e.value = o[k]; e.onchange = () => { o[k] = num ? +e.value : e.value; this._save(); if (k === "map") this._loadMap(e.value); else if (k === "mode") this._fit(); }; };
        bind("fMap", "map"); bind("fMode", "mode"); bind("fBots", "bots", true); bind("fSkill", "skill"); bind("fSecs", "secs", true); bind("fMatches", "matches", true); bind("fBSecs", "batchSecs", true);
        const pool = $("fPool");
        for (const w of WEAPONS.filter((x) => x !== "syringe" && x !== "locator")) {
            const l = document.createElement("label");
            l.innerHTML = `<input type="checkbox" value="${w}" ${!o.pool.length || o.pool.includes(w) ? "checked" : ""}>${w}`;
            // (an empty pool is the request's "every weapon": the last box can't be unticked, so the boxes never lie)
            l.querySelector("input").onchange = (ev) => {
                const on = [...pool.querySelectorAll("input:checked")].map((x) => x.value);
                if (!on.length) { ev.target.checked = true; this.app.toast("the bots need at least one weapon", 2500); return; }
                o.pool = on.length === pool.querySelectorAll("input").length ? [] : on; this._save();
            };
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
        if (!this.maps.some((m) => m.name === name)) { const m0 = this.maps.find((m) => m.for === "ffa") || this.maps[0]; if (!m0) return; return this._loadMap(m0.source + "/" + m0.name); }
        try {
            const text = await api.map(name, src);
            this.map = parseQmap(text, name); this.map.text = text; this.map.source = src;
            this.mv.setMap(this.map); this._fitted = this.mv.h > 50;
            if (this.m3.gl) this.m3.setMap(this.map);
            this.opts.map = src + "/" + name; const s = $("fMap"); if (s) s.value = this.opts.map;
            this._fit();
        } catch (e) { this.app.toast("can't load " + name + ": " + e.message, 3500); }
    }
    // does the map have what the mode needs (spawns, a hill, sites...)? (qmap.js mapSupports, as the game's menus warn)
    _fit() {
        const e = $("fFit"); if (!e) return;
        const ok = !this.map || mapSupports(this.map, this.opts.mode);
        const info = this.maps.find((m) => this.map && m.name === this.map.name);
        e.textContent = ok ? "" : `${this.map.name} isn't built for ${this.opts.mode}${info && info.for ? ` (it's a ${info.for} map)` : ""}: the match may not work`;
    }
    _request(text, secs = this.opts.secs) {
        const o = this.opts, n = Math.max(2, Math.min(16, o.bots | 0));
        const team = (i) => (TEAM_MODES.has(o.mode) ? (i % 2 ? "b" : "a") : "any");
        return toFightRequest({ map: { name: this.map.name, source: this.map.source, text: this.map.text }, mode: o.mode, seed: 1, secs, speed: $("fSpeed").value === "max" ? "max" : +$("fSpeed").value || 1,
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
        const secs = this.opts.batchSecs > 0 ? this.opts.batchSecs : WHOLE_MATCH;
        this.batch = { running: true, std: null, ed: null, progress: 0, n, mode: this.opts.mode, map: this.map.name, cmpLabel: this.app.cmpLabel, secs: this.opts.batchSecs > 0 ? secs : 0, teams: TEAM_MODES.has(this.opts.mode) || this.opts.mode === "duel" };
        this._side();
        // (the start line goes to the banner's helper check only: the live view keeps the recording it shows)
        const onLine = (base) => (o) => { if (o.type === "progress") { this.batch.progress = base + (o.done ?? 0) / n / 2; this._side(); } if (o.type === "start") { this.helperStart = o; if (typeof o.teams === "boolean") this.batch.teams = o.teams; this._banner(); } };
        try {
            this.batch.std = await runBatch(this._request(this.app.cmp.text, secs), n, onLine(0));
            this.batch.ed = await runBatch(this._request(this.app.text, secs), n, onLine(0.5));
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
    // per weapon numbers from a batch end line (SIM.md: weapons [{weapon, kills, deaths, damage, shots, hits, accuracy,
    // ttk, dps, heldSeconds}], every bot and match together)
    _weaponRows(end) {
        if (!end || !Array.isArray(end.weapons)) return null;
        return new Map(end.weapons.map((w) => [String(w.weapon).toLowerCase(), w]));
    }
    // the matches' results, team modes: each team's share, a match cut at the batch's seconds going to the team that
    // was ahead (simbatch's `leader`; level: a draw). Free for all: every bot plays the same set, so which bot won says
    // nothing about it: the per weapon table compares the sets there.
    _results(e, teams) {
        if (!e) return "–";
        const M = Array.isArray(e.matches) ? e.matches : [];
        if (!teams) return M.length ? `same set for every bot: see Per weapon` : "no match finished";
        if (!M.length) return "no match finished";
        const c = { a: 0, b: 0, draw: 0 }; let cut = 0;
        for (const m of M) {
            const out = m.reason === "matchEnd" ? m.winner : m.leader;
            if (m.reason !== "matchEnd") cut++;
            const t = out && (out.team === "a" || out.team === "b") ? out.team : "draw";
            c[t]++;
        }
        const pct = (x) => Math.round(100 * x / M.length) + "%";
        return `A ${pct(c.a)} · B ${pct(c.b)}${c.draw ? " · draw " + pct(c.draw) : ""}${cut ? ` <span class="muted" title="matches cut at the batch's seconds go to the team that was ahead">(${cut} by the lead)</span>` : ""}`;
    }
    // the mean game time of the matches that ended by themselves (stronger weapons: shorter rounds)
    _length(e) {
        const M = Array.isArray(e && e.matches) ? e.matches.filter((m) => m.reason === "matchEnd" && Number.isFinite(m.t)) : [];
        return M.length ? M.reduce((a, m) => a + m.t, 0) / M.length : null;
    }
    _side() {
        const box = $("fightSide");
        if (!box) return;
        let h = "";
        const B = this.batch;
        if (B) {
            h += `<h3>Batch <span class="muted small">${esc(B.map)} · ${esc(B.mode)} · ${B.n} matches each${B.secs ? ` of ${B.secs} s` : ""}</span> ${B.running ? `<span class="muted">running ${Math.round(B.progress * 100)}%</span>` : ""}</h3>`;
            const S = B.std, E = B.ed;
            if (S || E) {
                h += `<table class="score"><tr><th></th><th>${esc(B.cmpLabel)}</th><th>edited</th></tr>`;
                const tot = (e, k) => (e && e.totals ? e.totals[k] : e && Array.isArray(e.bots) ? e.bots.reduce((a, b) => a + (b[k] || 0), 0) : null);
                for (const [k, lab] of [["kills", "kills"], ["damage", "damage"], ["shots", "shots"], ["hits", "hits"]]) {
                    const a = tot(S, k), b = tot(E, k);
                    h += `<tr><td>${lab}</td><td>${a == null ? "–" : fmtNum(a)}</td><td>${b == null ? "–" : fmtNum(b)}${a != null && b != null ? diffSpan(b, a) : ""}</td></tr>`;
                }
                const played = (e) => (e ? `${e.matchesPlayed ?? (Array.isArray(e.matches) ? e.matches.length : B.n)}${e.timeouts ? ` (${e.timeouts} cut at ${B.secs || WHOLE_MATCH} s)` : ""}` : "–");
                h += `<tr><td>matches</td><td>${played(S)}</td><td>${played(E)}</td></tr>`;
                const la = this._length(S), lb = this._length(E);
                h += `<tr><td title="mean game time of the matches that ended by themselves">match length s</td><td>${la == null ? "–" : fmtNum(Math.round(la))}</td><td>${lb == null ? "–" : fmtNum(Math.round(lb))}${la != null && lb != null ? diffSpan(Math.round(lb), Math.round(la)) : ""}</td></tr>`;
                h += `<tr><td>results</td><td>${this._results(S, B.teams)}</td><td>${this._results(E, B.teams)}</td></tr></table>`;
                const ws = this._weaponRows(S), we = this._weaponRows(E);
                if (ws || we) {
                    // fights won = kills / (kills + deaths while holding it): how often a fight with that weapon in hand ended
                    // the other's way; a mirror match's team results can't tell sets apart, this can
                    h += `<h3>Per weapon <span class="muted small">edited, change vs ${esc(B.cmpLabel)}</span></h3><table class="score"><tr><th>weapon</th><th title="kills with it">kills</th><th title="kills / (kills + deaths while holding it)">fights won</th><th title="damage to enemies">damage</th><th title="hits / shots: ${esc(B.cmpLabel)} → edited">accuracy</th><th title="mean time from a fight's first hit to the kill">TTK s</th><th title="damage / seconds held">DPS</th></tr>`;
                    const won = (x) => (x && x.kills + (x.deaths || 0) > 0 ? x.kills / (x.kills + (x.deaths || 0)) : null);
                    const acc = (x) => (x ? (x.accuracy != null ? x.accuracy : x.shots ? x.hits / x.shots : null) : null);
                    const pct = (v) => (v == null ? "–" : Math.round(v * 100) + "%");
                    const cell = (b, a, k, lower, f = fmtNum) => `${b && b[k] != null ? f(b[k]) : "–"}${a && b && a[k] != null && b[k] != null ? diffSpan(b[k], a[k], lower) : ""}`;
                    for (const w of WEAPONS) {
                        const a = ws && ws.get(w), b = we && we.get(w);
                        if (!a && !b) continue;
                        const wa = won(a), wb = won(b);
                        h += `<tr><td>${esc(this.app.titles ? this.app.titles.get(w) || w : w)}</td><td>${cell(b, a, "kills")}</td>`
                            + `<td>${pct(wb)}${wa != null && wb != null ? diffSpan(Math.round(wb * 100), Math.round(wa * 100)) : ""}</td>`
                            + `<td>${cell(b, a, "damage", false, (x) => fmtNum(Math.round(x)))}</td>`
                            + `<td><span class="muted">${pct(acc(a))}</span> → ${pct(acc(b))}</td>`
                            + `<td>${cell(b, a, "ttk", true)}</td><td>${cell(b, a, "dps")}</td></tr>`;
                    }
                    h += `</table>`;
                } else h += `<div class="muted small">per weapon numbers come when simbatch's end line has <code>weapons</code> (the weapons helper)</div>`;
            }
        }
        const live = document.createElement("div");
        if (this.live.rec) this.live.renderSide(live);
        else live.innerHTML = `<h3>Live</h3><div class="muted">▶ Watch live runs one match with the edited set${this.app.helper.sim ? "" : " (needs the helper)"}. A batch runs the comparison set and the edited set on the same seeds.</div>`;
        box.innerHTML = h;
        box.append(live);
    }
}
