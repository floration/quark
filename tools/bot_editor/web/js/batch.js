// Batch runs: the current scenario's bots, N matches per map (seed, seed+1, ...), as fast as the helper goes;
// results summed per bot and per brain (kills, deaths, K/D, damage, accuracy, objective time, wins).

import { MODES, MODE_TITLES, TEAM_MODES, parseQmap } from "./qmap.js";
import { buildRequest, runToEnd } from "./helper.js";
import { api } from "./api.js";

const $ = (id) => document.getElementById(id);
const esc = (x) => String(x ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

// the end line's winner is a bot id (free for all, duels) or a team ("a" / "b" / "draw")
function wonBy(end, s) { return typeof end.winner === "number" ? end.winner === s.id : end.winner != null && end.winner === s.team; }

export class BatchPage {
    constructor(app) {
        this.app = app;
        this.results = [];
        app.on("maps", () => this._fill());
        $("bRun").onclick = () => this.run();
        $("bStop").onclick = () => { this.stopping = true; };
        $("bCsv").onclick = () => this._csv();
    }
    start() { this._fill(); }
    _fill() {
        const s = $("bMaps"), cur = new Set([...s.selectedOptions].map((o) => o.value));
        s.textContent = "";
        for (const m of this.app.maps) { const o = document.createElement("option"); o.value = m.source + "/" + m.name; o.textContent = `${m.title || m.name} (${m.source})`; if (cur.has(o.value)) o.selected = true; s.append(o); }
        const ms = $("bMode");
        if (!ms.options.length) for (const m of MODES) { const o = document.createElement("option"); o.value = m; o.textContent = MODE_TITLES[m]; ms.append(o); }
    }
    async run() {
        const sim = this.app.sim;
        const sc = sim.sc;
        if (!sc.bots.length) { this.app.toast("Add bots in the Simulator tab first (they are the batch's line-up)."); return; }
        let maps = [...$("bMaps").selectedOptions].map((o) => o.value.split("/"));
        if (!maps.length && this.app.map) maps = [[this.app.map.source, this.app.map.name]];
        const N = Math.max(1, Math.min(1000, +$("bN").value || 10)), secs = Math.max(5, +$("bSecs").value || 90), seed0 = +$("bSeed").value || 1;
        const mode = $("bMode").value || sc.mode;
        this.stopping = false; this.results = [];
        $("bRun").disabled = true; $("bStop").disabled = false; $("bCsv").disabled = true;
        const brains = {};
        for (const b of sc.bots) if (b.brain !== "default") { if (!this.app.brainText.has(b.brain) && b.brain !== "(editor)") await this.app.loadBrainText(b.brain).catch(() => {}); const t = this.app.brainTextFor(b.brain); if (t) brains[b.brain] = t; }
        const total = maps.length * N;
        let done = 0;
        const t0 = performance.now();
        for (const [source, name] of maps) {
            const text = await api.map(name, source);
            const map = parseQmap(text, name); map.text = text; map.source = source;
            for (let k = 0; k < N && !this.stopping; k++) {
                // placed positions belong to the scenario's map: other maps start everyone at spawn points
                const bots = sc.bots.map((b) => (sc.map && sc.map.name === name && sc.map.source === source ? b : { ...b, pos: null, objective: b.objective && b.objective.kind === "objective" ? b.objective : { kind: "default" } }));
                const req = buildRequest({ ...sc, bots, mode, seed: seed0 + k, secs, mapSource: source, speed: "max" }, map, brains);
                $("bStatus").textContent = `${name}: match ${k + 1} / ${N} (seed ${seed0 + k})`;
                try {
                    const end = await runToEnd(this.app.helper, req, map, (n) => this.app.paramsOf(n), (f) => { $("bProg").style.width = ((done + Math.min(1, f)) / total * 100).toFixed(1) + "%"; });
                    this.results.push({ map: name, seed: seed0 + k, end, bots: req.bots });
                } catch (e) { this.results.push({ map: name, seed: seed0 + k, error: e.message }); }
                done++;
                $("bProg").style.width = (done / total * 100).toFixed(1) + "%";
                this._table();
            }
        }
        $("bStatus").textContent = `${this.stopping ? "stopped" : "done"}: ${this.results.length} matches in ${((performance.now() - t0) / 1000).toFixed(1)} s${this.app.helper.kind === "mock" ? " (MOCK simulation)" : ""}`;
        $("bRun").disabled = false; $("bStop").disabled = true; $("bCsv").disabled = !this.results.length;
    }
    _aggregate() {
        const perBot = new Map(), perBrain = new Map();
        // a brain's "matches" are the matches it played in (several bots with one brain count one match); its stats
        // are per bot-match (kills/match = per bot), its wins are matches where one of its bots (or their team) won
        const add = (M, key, base) => { if (!M.has(key)) M.set(key, { ...base, matches: 0, botMatches: 0, kills: 0, deaths: 0, damage: null, shots: null, hits: 0, objTime: null, wins: 0, seen: null }); return M.get(key); };
        for (const r of this.results) {
            if (!r.end) continue;
            for (const M of [perBot, perBrain]) for (const a of M.values()) a.seen = null;
            for (const s of r.end.scores || []) {
                const spec = r.bots.find((b) => b.id === s.id) || {};
                const won = wonBy(r.end, s);
                for (const [M, key, base] of [[perBot, s.name || spec.name, { name: s.name || spec.name, brain: s.brain || spec.brain, skill: s.skill || spec.skill }], [perBrain, (s.brain || spec.brain) + " · " + (s.skill || spec.skill), { name: s.brain || spec.brain, skill: s.skill || spec.skill }]]) {
                    const a = add(M, key, base);
                    if (a.seen !== r) { a.seen = r; a.matches++; a.wonThis = false; }
                    a.botMatches++; a.kills += s.kills || 0; a.deaths += s.deaths || 0;
                    if (s.damage != null) a.damage = (a.damage || 0) + s.damage;
                    if (s.shots != null) { a.shots = (a.shots || 0) + s.shots; a.hits += s.hits || 0; }
                    if (s.objTime != null) a.objTime = (a.objTime || 0) + s.objTime;
                    if (won && !a.wonThis) { a.wonThis = true; a.wins++; }
                }
            }
        }
        return { perBot: [...perBot.values()], perBrain: [...perBrain.values()] };
    }
    _table() {
        const { perBot, perBrain } = this._aggregate();
        // "-": the helper doesn't report it (quark_maptool gives kills and deaths, not damage, shots or objective time)
        const per = (x, n, d) => (x == null ? "-" : (x / Math.max(1, n)).toFixed(d));
        const row = (a, first) => `<tr><td>${first}</td><td>${a.matches}</td><td>${a.kills}</td><td>${a.deaths}</td><td>${(a.kills / Math.max(1, a.deaths)).toFixed(2)}</td><td>${per(a.kills, a.botMatches, 1)}</td><td>${per(a.damage, a.botMatches, 0)}</td><td>${a.shots ? Math.round(a.hits / a.shots * 100) + "%" : "-"}</td><td>${per(a.objTime, a.botMatches, 1)}</td><td>${Math.round(a.wins / Math.max(1, a.matches) * 100)}%</td></tr>`;
        const head = `<tr><th></th><th>matches</th><th>kills</th><th>deaths</th><th>K/D</th><th>kills/match</th><th>dmg/match</th><th>accuracy</th><th>obj s/match</th><th>wins</th></tr>`;
        const errs = this.results.filter((r) => r.error);
        $("batchMain").innerHTML = `<h3>Per brain (and skill)</h3><table class="score">${head}${perBrain.sort((a, b) => b.kills / Math.max(1, b.deaths) - a.kills / Math.max(1, a.deaths)).map((a) => row(a, `<b>${esc(a.name)}</b> <span class="muted">${esc((a.skill || "").toLowerCase())}</span>`)).join("")}</table>
          <h3>Per bot</h3><table class="score">${head}${perBot.map((a) => row(a, `${esc(a.name)} <span class="muted">${esc(a.brain)} · ${esc((a.skill || "").toLowerCase())}</span>`)).join("")}</table>
          <h3>Matches</h3><table class="score"><tr><th>map</th><th>seed</th><th>winner</th><th>top fragger</th><th>length</th></tr>${this.results.map((r) => r.error ? `<tr><td>${esc(r.map)}</td><td>${r.seed}</td><td colspan="3" class="err">${esc(r.error)}</td></tr>` : `<tr><td>${esc(r.map)}</td><td>${r.seed}</td><td>${esc(typeof r.end.winner === "number" ? ((r.end.scores || []).find((s) => s.id === r.end.winner) || {}).name : r.end.winner)}</td><td>${esc(((r.end.scores || []).slice().sort((a, b) => b.kills - a.kills)[0] || {}).name)}</td><td>${(r.end.t || 0).toFixed(0)} s</td></tr>`).join("")}</table>
          ${errs.length ? `<div class="err">${errs.length} match(es) failed</div>` : ""}`;
    }
    _csv() {
        const lines = ["map,seed,bot,brain,skill,team,kills,deaths,damage,shots,hits,objTime,won"];
        for (const r of this.results) if (r.end) for (const s of r.end.scores || []) lines.push([r.map, r.seed, s.name, s.brain, s.skill, s.team, s.kills, s.deaths, s.damage, s.shots, s.hits, s.objTime, wonBy(r.end, s) ? 1 : 0].join(","));
        this.app.download("batch.csv", lines.join("\n") + "\n", "text/csv");
    }
}
