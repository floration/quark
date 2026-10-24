// Batch runs: the current scenario's bots, N matches per map (seed, seed+1, ...), as fast as the helper goes;
// results summed per bot and per brain (kills, deaths, K/D, damage, accuracy, objective time, wins).
// With quark_maptool sim: one `simbatch` per map (every core but one, its own per bot / per brain / per match tables,
// summed here over the maps). Else (MOCK, an older helper) one match at a time.

import { MODES, MODE_TITLES, TEAM_MODES, parseQmap } from "./qmap.js";
import { buildRequest, runToEnd, runBatch } from "./helper.js";
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
        $("bStop").onclick = () => { this.stopping = true; if (runBatch.current) runBatch.current.control("stop"); };
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
        this.sim = this.app.helper.batch ? { maps: [], matches: [] } : null;
        if (this.sim) {
            for (const [source, name] of maps) {
                if (this.stopping) break;
                const text = await api.map(name, source);
                const map = parseQmap(text, name); map.text = text; map.source = source;
                const bots = sc.bots.map((b) => (sc.map && sc.map.name === name && sc.map.source === source ? b : { ...b, pos: null, objective: b.objective && b.objective.kind === "objective" ? b.objective : { kind: "default" } }));
                const req = buildRequest({ ...sc, bots, mode, seed: seed0, secs, mapSource: source, speed: "max" }, map, brains);
                $("bStatus").textContent = `${name}: starting ${N} matches…`;
                let last = 0;
                try {
                    const end = await runBatch(req, N, (o) => {
                        if (o.type === "start") $("bStatus").textContent = `${name}: ${N} matches on ${o.threads} threads${o.brainSupport ? "" : " (built-in brain: this build has no brain code)"}`;
                        else if (o.type === "match") { this.sim.matches.push({ map: name, ...o }); }
                        else if (o.type === "progress") $("bStatus").textContent = `${name}: ${o.done} / ${o.total} done, ${o.running} running`;
                        if (o.done != null) { $("bProg").style.width = ((done + o.done) / total * 100).toFixed(1) + "%"; if (performance.now() - last > 300) { last = performance.now(); this._tableSim(); } }
                    });
                    this.sim.maps.push({ map: name, end });
                } catch (e) { this.sim.maps.push({ map: name, error: e.message }); }
                runBatch.current = null;
                done += N;
                $("bProg").style.width = (done / total * 100).toFixed(1) + "%";
                this._tableSim();
            }
            const n = this.sim.matches.length;
            $("bStatus").textContent = `${this.stopping ? "stopped" : "done"}: ${n} matches in ${((performance.now() - t0) / 1000).toFixed(1)} s (quark_maptool simbatch${this.app.helper.brainSupport ? "" : "; every bot played the built-in brain: this build has no brain code"})`;
            $("bRun").disabled = false; $("bStop").disabled = true; $("bCsv").disabled = !n;
            return;
        }
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
    // simbatch's totals summed over the maps (a bot is the same line-up slot on every map)
    _tableSim() {
        const S = this.sim, sum = (rows, key) => {
            const M = new Map();
            for (const r of rows) {
                const k = key(r);
                if (!M.has(k)) M.set(k, { ...r, matches: 0, wins: 0, kills: 0, deaths: 0, damage: 0, score: 0, objTime: 0, shots: 0, hits: 0, aliveTime: 0, alive: 0 });
                const a = M.get(k);
                for (const f of ["wins", "kills", "deaths", "damage", "score", "objTime", "shots", "hits", "aliveTime"]) a[f] += r[f] || 0;
                const n = r.matches ?? r.botMatches ?? 0;
                a.matches += n; a.alive += (r.aliveShare || 0) * n;
            }
            return [...M.values()];
        };
        const ends = S.maps.filter((m) => m.end).map((m) => m.end);
        let bots = sum(ends.flatMap((e) => e.bots || []), (b) => b.index + "|" + b.name);
        let brains = sum(ends.flatMap((e) => e.brains || []), (b) => b.brain);
        // live (while the first map runs): from the match lines so far
        if (!ends.length && S.matches.length) {
            bots = sum(S.matches.flatMap((m) => (m.bots || []).map((b) => ({ ...b, index: b.id, matches: 1, wins: b.won ? 1 : 0, aliveShare: m.t ? b.aliveTime / m.t : 0 }))), (b) => b.index + "|" + b.name);
            brains = sum(S.matches.flatMap((m) => (m.bots || []).map((b) => ({ ...b, matches: 1, wins: b.won ? 1 : 0, aliveShare: m.t ? b.aliveTime / m.t : 0 }))), (b) => b.brain);
        }
        for (const br of brains) { const any = bots.find((b) => b.brain === br.brain && b.brainStatus); if (any) { br.brainStatus = any.brainStatus; br.brainLabel = br.brainLabel || any.brainLabel; } }
        const per = (x, n, d) => (x / Math.max(1, n)).toFixed(d);
        const head = (first) => `<tr><th>${first}</th><th>matches</th><th>wins</th><th>kills</th><th>deaths</th><th>K/D</th><th>kills/match</th><th>dmg/match</th><th>accuracy</th><th>obj s/match</th><th>alive</th></tr>`;
        const row = (a, first) => `<tr><td>${first}</td><td>${a.matches}</td><td>${Math.round(a.wins / Math.max(1, a.matches) * 100)}%</td><td>${a.kills}</td><td>${a.deaths}</td><td>${per(a.kills, a.deaths, 2)}</td><td>${per(a.kills, a.matches, 1)}</td><td>${per(a.damage, a.matches, 0)}</td><td>${a.shots ? Math.round(a.hits / a.shots * 100) + "%" : "-"}</td><td>${per(a.objTime, a.matches, 1)}</td><td>${a.matches ? Math.round(a.alive / a.matches * 100) + "%" : "-"}</td></tr>`;
        const wins = S.maps.map((m) => {
            if (m.error) return `<tr><td>${esc(m.map)}</td><td colspan="4" class="err">${esc(m.error)}</td></tr>`;
            const e = m.end, tw = e.teamWins, pw = e.playerWins;
            const res = tw ? `team A ${tw.a} · team B ${tw.b} · draws ${tw.draws}` : pw && pw.length ? pw.map((p) => `${esc(((e.bots || []).find((b) => b.index === p.player) || {}).name || "#" + p.player)} ${p.wins}`).join(" · ") + ` · draws ${e.draws ?? 0}` : "no match ended (see timeouts)";
            return `<tr><td>${esc(m.map)}</td><td>${e.matchesPlayed}</td><td>${res}</td><td>${e.timeouts ? e.timeouts + " ran out of time" : "-"}</td><td>${e.perf ? e.perf.matchesPerSecond.toFixed(1) + " matches/s on " + e.perf.threads + " threads" : ""}</td></tr>`;
        }).join("");
        const ms = S.matches.slice().sort((a, b) => (a.map < b.map ? -1 : a.map > b.map ? 1 : a.seed - b.seed));
        const wname = (m) => (m.winner ? m.winner.text || (m.winner.team ? "team " + m.winner.team : "#" + m.winner.player) : m.leader ? "(time) " + (m.leader.team ? "team " + m.leader.team : ((m.bots || []).find((b) => b.id === m.leader.player) || {}).name) + " ahead" : "-");
        $("batchMain").innerHTML = `<h3>Per brain</h3><table class="score">${head("brain")}${brains.sort((a, b) => b.wins / Math.max(1, b.matches) - a.wins / Math.max(1, a.matches)).map((a) => row(a, `<b>${esc(a.brainLabel || a.brain)}</b>${a.brainStatus === "unsupported" ? ' <span class="warn" title="this build plays the built-in brain">not applied</span>' : ""}`)).join("")}</table>
          <p class="muted" style="font-size:11px">A brain's matches count every bot playing it (bot-matches); wins: matches its bot or team won.</p>
          <h3>Per bot</h3><table class="score">${head("bot")}${bots.map((a) => row(a, `${esc(a.name)} <span class="muted">${esc(a.brainLabel || a.brain)} · ${esc(String(a.skill || "").toLowerCase())}${a.team && new Set(bots.map((x) => x.team)).size > 1 ? " · team " + esc(a.team) : ""}${a.objective && a.objective.type && a.objective.type !== "default" ? " · " + esc(a.objective.type) : ""}</span>`)).join("")}</table>
          <h3>Wins per map</h3><table class="score"><tr><th>map</th><th>matches</th><th>wins</th><th>timeouts</th><th>speed</th></tr>${wins}</table>
          <h3>Matches</h3><table class="score"><tr><th>map</th><th>seed</th><th>result</th><th>score</th><th>top fragger</th><th>length</th></tr>${ms.map((m) => `<tr><td>${esc(m.map)}</td><td>${m.seed}</td><td>${esc(wname(m))}</td><td>${m.score ? m.score.join("–") : ""}</td><td>${esc(((m.bots || []).slice().sort((a, b) => b.kills - a.kills)[0] || {}).name)}</td><td>${(m.t || 0).toFixed(0)} s</td></tr>`).join("")}</table>`;
    }
    _csv() {
        if (this.sim) {
            const lines = ["map,seed,bot,brain,skill,team,kills,deaths,damage,score,shots,hits,objTime,aliveTime,won,reason,length"];
            const ms = this.sim.matches.slice().sort((a, b) => (a.map < b.map ? -1 : a.map > b.map ? 1 : a.seed - b.seed));
            for (const m of ms) for (const b of m.bots || []) lines.push([m.map, m.seed, b.name, b.brainLabel || b.brain, b.skill, b.team, b.kills, b.deaths, b.damage, b.score, b.shots, b.hits, b.objTime, b.aliveTime, b.won ? 1 : 0, m.reason, m.t].map((x) => (x == null ? "" : String(x).includes(",") ? `"${x}"` : x)).join(","));
            this.app.download("batch.csv", lines.join("\n") + "\n", "text/csv");
            return;
        }
        const lines = ["map,seed,bot,brain,skill,team,kills,deaths,damage,shots,hits,objTime,won"];
        for (const r of this.results) if (r.end) for (const s of r.end.scores || []) lines.push([r.map, r.seed, s.name, s.brain, s.skill, s.team, s.kills, s.deaths, s.damage, s.shots, s.hits, s.objTime, wonBy(r.end, s) ? 1 : 0].join(","));
        this.app.download("batch.csv", lines.join("\n") + "\n", "text/csv");
    }
}
