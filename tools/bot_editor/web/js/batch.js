// Batch runs: the current scenario's bots, N matches per map (seed, seed+1, ...), as fast as the helper goes;
// results summed per brain, per bot and per map from the match lines (kills, deaths, K/D, damage, accuracy, objective
// time, wins), each brain's win rate with its 95% interval. A run can be pinned (kept in the data folder) and any two
// runs compared: each brain's win rate in both, the difference and whether it's more than chance.
// With quark_maptool sim: one `simbatch` per map (every core but one). Else (MOCK, an older helper) one match at a time.

import { MODES, MODE_TITLES, TEAM_MODES, parseQmap, mapKey, mapTitle, mapLabel } from "./qmap.js";
import { buildRequest, runToEnd, runBatch } from "./helper.js";
import { api } from "./api.js";

const $ = (id) => document.getElementById(id);
const esc = (x) => String(x ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const pct = (x) => Math.round(x * 100) + "%";
const MAX_PINS = 30;

// ---------------------------------------------------------------- statistics (pure: the node tests use these)
// Wilson score interval for w wins in n matches (95%): sane at 0 and n wins and for small n, unlike p ± 1.96·se
export function wilson(w, n, z = 1.96) {
    if (!n) return { p: 0, lo: 0, hi: 1 };
    const p = w / n, z2 = z * z, d = 1 + z2 / n;
    const c = (p + z2 / (2 * n)) / d, h = (z * Math.sqrt(p * (1 - p) / n + z2 / (4 * n * n))) / d;
    return { p, lo: Math.max(0, c - h), hi: Math.min(1, c + h) };
}
// the difference of two win rates b - a with its 95% interval (Newcombe's method, from the two Wilson intervals);
// sure: the interval leaves out 0 (more than chance at 95%)
export function rateDiff(wa, na, wb, nb) {
    const A = wilson(wa, na), B = wilson(wb, nb), d = B.p - A.p;
    const lo = d - Math.sqrt((B.p - B.lo) ** 2 + (A.hi - A.p) ** 2), hi = d + Math.sqrt((B.hi - B.p) ** 2 + (A.p - A.lo) ** 2);
    return { d, lo, hi, sure: na > 0 && nb > 0 && (lo > 0 || hi < 0) };
}
// "62% ±7": the rate and half the interval's width (the title gives the interval itself)
export function rateText(w, n, mirror = 0) {
    if (n && mirror >= n) return { text: "every bot", title: "every bot played this brain: it won every match it played, which says nothing" };
    const c = wilson(w, n);
    return { text: n ? `${pct(c.p)} ±${Math.round((c.hi - c.lo) * 50)}` : "-", title: n ? `${w} of ${n} matches; 95% interval ${pct(c.lo)}–${pct(c.hi)} (Wilson)` : "" };
}

// Per brain, per bot and per map, from match lines {map, seed, t, reason, winner: {team, player, text} | null,
// bots: [{id, name, team, skill, brain, brainLabel, kills, deaths, damage, shots, hits, objTime, aliveTime, won}]}.
// A brain's n is the matches it played in, w the ones its bot (or its team) won: several bots with one brain are one
// match, so the interval isn't made narrower by them. Its other numbers are per bot-match.
export function summarize(matches) {
    const brains = new Map(), bots = new Map(), maps = new Map();
    const z = () => ({ n: 0, w: 0, botMatches: 0, kills: 0, deaths: 0, damage: 0, shots: 0, hits: 0, objTime: 0, alive: 0, t: 0 });
    for (const m of matches) {
        // per map by source/name (a map of yours named like a shipped one is another map); map: its label
        const mk = m.mapKey || m.map;
        const M = maps.get(mk) || maps.set(mk, { key: mk, map: m.mapLabel || m.map, matches: 0, errors: 0, a: 0, b: 0, draws: 0, timeouts: 0, players: new Map(), t: 0 }).get(mk);
        if (m.error) { M.errors++; continue; }
        M.matches++; M.t += m.t || 0;
        const w = m.winner;
        if (!w) M.timeouts++;
        else if (w.team === "a" || w.team === "b") M[w.team]++;
        else if (w.player != null) { const nm = ((m.bots || []).find((b) => b.id === w.player) || {}).name || "#" + w.player; M.players.set(nm, (M.players.get(nm) || 0) + 1); }
        else M.draws++;
        const seen = new Map();
        for (const b of m.bots || []) {
            const key = b.brainLabel || b.brain || "default";
            const B = brains.get(key) || brains.set(key, { key, name: b.brain || "default", label: key, status: b.brainStatus, mirror: 0, ...z() }).get(key);
            const R = bots.get(b.id + "|" + b.name) || bots.set(b.id + "|" + b.name, { key: b.id + "|" + b.name, name: b.name, team: b.team, skill: b.skill, brain: b.brain, label: key, objective: b.objective, ...z() }).get(b.id + "|" + b.name);
            for (const A of [B, R]) {
                A.botMatches++; A.kills += b.kills || 0; A.deaths += b.deaths || 0; A.damage += b.damage || 0;
                A.shots += b.shots || 0; A.hits += b.hits || 0; A.objTime += b.objTime || 0; A.alive += b.aliveTime || 0; A.t += m.t || 0;
            }
            R.n++; if (b.won) R.w++;
            const s = seen.get(key) || seen.set(key, { won: false }).get(key);
            if (b.won) s.won = true;
        }
        for (const [key, s] of seen) { const B = brains.get(key); B.n++; if (s.won) B.w++; if (seen.size === 1) B.mirror++; }
    }
    return { brains: [...brains.values()], bots: [...bots.values()], maps: [...maps.values()].map((M) => ({ ...M, players: [...M.players].map(([name, wins]) => ({ name, wins })) })) };
}

export class BatchPage {
    constructor(app) {
        this.app = app;
        this.run0 = null;          // the latest run: {settings, matches: [match lines], perf: {map: end.perf}}
        this.pins = [];            // pinned runs (state "batchpins"): {id, title, at, settings, summary}
        this.cmp = { a: null, b: "current" };   // compared runs: a pin id (or "current"), b likewise
        this.picked = new Set();   // "source/name" of the maps to run
        app.on("maps", () => this._fill());
        app.on("page", (p) => { if (p === "batch") this._fillMode(); });
        $("bRun").onclick = () => this.run();
        $("bStop").onclick = () => { this.stopping = true; if (runBatch.current) runBatch.current.control("stop"); };
        $("bCsv").onclick = () => this._csv();
        $("bMode").onchange = () => { this._paintPicks(); this._saveSettings(); };
        for (const id of ["bN", "bSecs", "bSeed"]) $(id).onchange = () => { this._paintPicks(); this._saveSettings(); };
        document.querySelectorAll("#bPicks [data-pick]").forEach((b) => (b.onclick = () => this._pick(b.dataset.pick)));
    }
    async start() {
        let st = null, pins = null;
        try { [st, pins] = await Promise.all([api.state("batch"), api.state("batchpins")]); } catch {}
        if (st && typeof st === "object") {
            if (Array.isArray(st.maps)) this.picked = new Set(st.maps);
            for (const [id, k] of [["bN", "n"], ["bSecs", "secs"], ["bSeed", "seed"]]) if (Number.isFinite(st[k])) $(id).value = st[k];
            this._wantMode = st.mode || "";
        }
        if (Array.isArray(pins)) this.pins = pins.filter((p) => p && p.summary).slice(-MAX_PINS);
        if (this.pins.length) this.cmp.a = this.pins[this.pins.length - 1].id;
        this._fill();
        this._render();
    }
    _saveSettings() {
        clearTimeout(this._st);
        this._st = setTimeout(() => api.saveState("batch", { maps: [...this.picked], n: +$("bN").value, secs: +$("bSecs").value, seed: +$("bSeed").value, mode: $("bMode").value }).catch(() => {}), 500);
    }

    // ---- settings: mode (the scenario's by default) and the map picks
    _mode() { return $("bMode").value || this.app.sim.sc.mode; }
    _fillMode() {
        const ms = $("bMode"), cur = ms.options.length ? ms.value : (this._wantMode || "");
        ms.textContent = "";
        const o = document.createElement("option"); o.value = ""; o.textContent = `the scenario's (${MODE_TITLES[this.app.sim.sc.mode] || this.app.sim.sc.mode})`; ms.append(o);
        for (const m of MODES) { const e = document.createElement("option"); e.value = m; e.textContent = MODE_TITLES[m]; ms.append(e); }
        ms.value = cur;
        this._paintPicks();
    }
    _fill() {
        const G = $("bMaps");
        G.textContent = "";
        const have = new Set(this.app.maps.map((m) => m.source + "/" + m.name));
        for (const k of [...this.picked]) if (!have.has(k)) this.picked.delete(k);
        for (const m of this.app.maps) {
            const k = m.source + "/" + m.name, l = document.createElement("label");
            l.className = "mpick"; l.dataset.k = k;
            l.title = `${m.title || m.name} · built for ${m.for || "?"}${m.modes && m.modes.length ? " · offered for " + m.modes.join(", ") : ""}${m.ranked ? " · ranked" : ""}${m.source !== "default" ? " · " + m.source + " map" : ""}`;
            l.innerHTML = `<input type="checkbox"${this.picked.has(k) ? " checked" : ""}><span>${esc(mapTitle(m))}</span>${m.source !== "default" ? `<i class="muted">${m.source === "user" ? "yours" : "other folder"}</i>` : ""}`;
            l.querySelector("input").onchange = (e) => { if (e.target.checked) this.picked.add(k); else this.picked.delete(k); this._paintPicks(); this._saveSettings(); };
            G.append(l);
        }
        this._fillMode();
    }
    _offered(m, mode) { return m.for === mode || (m.modes || []).includes(mode); }
    _pick(kind) {
        const mode = this._mode(), sc = this.app.sim.sc;
        const keys = this.app.maps.filter((m) => kind === "all" || (kind === "ranked" && m.ranked) || (kind === "mode" && this._offered(m, mode)) ||
            (kind === "this" && sc.map && m.name === sc.map.name && m.source === sc.map.source)).map((m) => m.source + "/" + m.name);
        this.picked = new Set(kind === "none" ? [] : keys);
        $("bMaps").querySelectorAll(".mpick").forEach((l) => (l.querySelector("input").checked = this.picked.has(l.dataset.k)));
        this._paintPicks(); this._saveSettings();
    }
    _paintPicks() {
        const mode = this._mode();
        $("bMaps").querySelectorAll(".mpick").forEach((l) => {
            const m = this.app.maps.find((x) => x.source + "/" + x.name === l.dataset.k);
            l.classList.toggle("off", !!m && !this._offered(m, mode));   // (not offered for this mode: may not work)
        });
        const n = this.picked.size;
        $("bPickInfo").textContent = n ? `${n} map${n > 1 ? "s" : ""} · ${(+$("bN").value || 10) * n} matches` : "none picked: the scenario's map";
    }

    // ---- running
    async run() {
        const sim = this.app.sim, sc = sim.sc;
        if (!sc.bots.length) { this.app.toast("Add bots in the Simulator tab first (they are the batch's line-up)."); return; }
        let maps = this.app.maps.filter((m) => this.picked.has(mapKey(m)));
        if (!maps.length && this.app.map) maps = [this.app.maps.find((m) => m.name === this.app.map.name && m.source === this.app.map.source) || { name: this.app.map.name, source: this.app.map.source, title: this.app.map.title }];
        const N = Math.max(1, Math.min(1000, +$("bN").value || 10)), secs0 = Math.max(5, Math.min(7200, +$("bSecs").value || 900)), seed0 = +$("bSeed").value || 1;
        const mode = this._mode();
        // the helper plays each match to its end (until matchEnd) with this cap; the MOCK has no match end, so its
        // matches are the cap itself, kept short
        const secs = this.app.helper.batch ? secs0 : Math.min(secs0, 120);
        this.stopping = false;
        $("bRun").disabled = true; $("bStop").disabled = false; $("bCsv").disabled = true;
        const brains = {};
        for (const b of sc.bots) if (b.brain !== "default") { if (!this.app.brainText.has(b.brain) && b.brain !== "(editor)") await this.app.loadBrainText(b.brain).catch(() => {}); const t = this.app.brainTextFor(b.brain); if (t) brains[b.brain] = t; }
        const settings = { at: new Date().toISOString(), mode, N, seed0, secs, maps: maps.map(mapLabel), mapKeys: maps.map(mapKey),
            lineup: sc.bots.map((b) => ({ name: b.name, team: b.team, skill: b.skill, brain: b.brain })), mock: this.app.helper.kind === "mock" };
        const R = (this.run0 = { settings, matches: [], perf: {}, brainSupport: this.app.helper.brainSupport });
        this.cmp.b = "current";
        const total = maps.length * N, ran = [];   // (the maps that ran: a Stop leaves the rest out)
        let done = 0, last = 0;
        const t0 = performance.now(), prog = (x) => ($("bProg").style.width = (x / total * 100).toFixed(1) + "%");
        const live = () => { if (performance.now() - last > 300) { last = performance.now(); this._render(); } };
        for (const row of maps) {
            if (this.stopping) break;
            const { source, name } = row, mk = mapKey(row), ml = mapLabel(row), tag = { map: name, mapKey: mk, mapLabel: ml };
            ran.push(ml);
            const text = await api.map(name, source);
            const map = parseQmap(text, name); map.text = text; map.source = source;
            // placed positions belong to the scenario's map: other maps start everyone at spawn points
            const bots = sc.bots.map((b) => (sc.map && sc.map.name === name && sc.map.source === source ? b : { ...b, pos: null, objective: b.objective && b.objective.kind === "objective" ? b.objective : { kind: "default" } }));
            if (this.app.helper.batch) {
                const req = buildRequest({ ...sc, bots, mode, seed: seed0, secs, mapSource: source, speed: "max" }, map, brains);
                $("bStatus").textContent = `${ml}: starting ${N} matches (${MODE_TITLES[mode] || mode})…`;
                try {
                    const end = await runBatch(req, N, (o) => {
                        if (o.type === "start") $("bStatus").textContent = `${ml}: ${N} matches of ${MODE_TITLES[mode] || mode} on ${o.threads} threads${o.brainSupport ? "" : " (built-in brain: this build has no brain code)"}`;
                        else if (o.type === "match") R.matches.push({ ...o, ...tag });
                        else if (o.type === "progress") $("bStatus").textContent = `${ml}: ${o.done} / ${o.total} done, ${o.running} running`;
                        if (o.done != null) { prog(done + o.done); live(); }
                    });
                    R.perf[mk] = end.perf;
                } catch (e) { R.matches.push({ ...tag, seed: seed0, error: e.message }); }
                runBatch.current = null;
                done += N;
            } else {
                for (let k = 0; k < N && !this.stopping; k++) {
                    const req = buildRequest({ ...sc, bots, mode, seed: seed0 + k, secs, mapSource: source, speed: "max" }, map, brains);
                    $("bStatus").textContent = `${ml}: match ${k + 1} / ${N} (seed ${seed0 + k})`;
                    try {
                        const end = await runToEnd(this.app.helper, req, map, (n) => this.app.paramsOf(n), (f) => prog(done + Math.min(1, f)));
                        R.matches.push({ ...fromEndLine(name, seed0 + k, end, req.bots), ...tag });
                    } catch (e) { R.matches.push({ ...tag, seed: seed0 + k, error: e.message }); }
                    done++;
                    prog(done); live();
                }
            }
            prog(done);
            this._render();
        }
        const n = R.matches.filter((m) => !m.error).length;
        $("bStatus").textContent = `${this.stopping ? "stopped" : "done"}: ${n} matches of ${MODE_TITLES[mode] || mode} on ${ran.join(", ") || "no map"} in ${((performance.now() - t0) / 1000).toFixed(1)} s (${this.app.helper.batch ? "quark_maptool simbatch" : this.app.helper.kind === "mock" ? "MOCK simulation" : "quark_maptool"}${this.app.helper.batch && !this.app.helper.brainSupport ? "; every bot played the built-in brain: this build has no brain code" : ""})`;
        $("bRun").disabled = false; $("bStop").disabled = true; $("bCsv").disabled = !n;
        this._render();
    }

    // ---- pins
    _pin() {
        const R = this.run0;
        if (!R || !R.matches.length) return;
        const s = R.settings, id = Date.now().toString(36);
        const brainsTxt = [...new Set(s.lineup.map((b) => b.brain))].join(" vs ");
        const title = `${brainsTxt} · ${MODE_TITLES[s.mode] || s.mode} · ${s.maps.length > 3 ? s.maps.length + " maps" : s.maps.join(", ")}`;
        const summary = summarize(R.matches);
        for (const b of summary.bots) delete b.objective;
        this.pins.push({ id, title, at: s.at, settings: s, summary });
        if (this.pins.length > MAX_PINS) this.pins.shift();
        this.cmp = { a: id, b: "current" };
        R.pinned = id;
        this._savePins();
        this._render();
    }
    _savePins() { api.saveState("batchpins", this.pins).catch((e) => this.app.toast("Couldn't keep the pins: " + e.message)); }
    _runOf(id) {
        if (id === "current") return this.run0 && this.run0.matches.length ? { id: "current", title: "latest run", at: this.run0.settings.at, settings: this.run0.settings, summary: summarize(this.run0.matches) } : null;
        return this.pins.find((p) => p.id === id) || null;
    }

    // ---- results
    _render() {
        const R = this.run0, S = R && R.matches.length ? summarize(R.matches) : null;
        const cards = [this._pinsCard(), S ? this._brainsCard(S) : "", this._compareCard(), S ? this._mapsCard(S, R) : "", S ? this._botsCard(S) : "", S ? this._matchesCard(R) : ""].filter(Boolean);
        $("batchMain").innerHTML = cards.length ? `<div class="bgrid">${cards.join("")}</div>` : `<div class="muted">No batch yet.</div>`;
        const M = $("batchMain");
        const pinBtn = M.querySelector("[data-act=pin]");
        if (pinBtn) pinBtn.onclick = () => this._pin();
        M.querySelectorAll("[data-cmp]").forEach((e) => (e.onchange = () => { this.cmp[e.dataset.cmp] = e.value; this._render(); }));
        M.querySelectorAll("[data-unpin]").forEach((e) => (e.onclick = () => {
            this.pins = this.pins.filter((p) => p.id !== e.dataset.unpin);
            for (const k of ["a", "b"]) if (this.cmp[k] === e.dataset.unpin) this.cmp[k] = k === "b" ? "current" : (this.pins[this.pins.length - 1] || {}).id || null;
            if (this.run0 && this.run0.pinned === e.dataset.unpin) this.run0.pinned = null;
            this._savePins(); this._render();
        }));
        M.querySelectorAll("[data-rename]").forEach((e) => (e.onclick = async () => {
            const p = this.pins.find((x) => x.id === e.dataset.rename);
            const t = p && (await this.app.prompt("Name this run", p.title, "Shown in the pins and the compare lists."));
            if (t) { p.title = t.slice(0, 120); this._savePins(); this._render(); }
        }));
    }
    _pinsCard() {
        const R = this.run0, canPin = R && R.matches.some((m) => !m.error) && !R.pinned && $("bRun") && !$("bRun").disabled;
        if (!this.pins.length && !canPin) return "";
        const rows = this.pins.slice().reverse().map((p) => {
            const s = p.settings, top = p.summary.brains.slice().sort((a, b) => b.w / Math.max(1, b.n) - a.w / Math.max(1, a.n)).map((b) => `${esc(b.name)} ${rateText(b.w, b.n, b.mirror).text}`).join(" · ");
            return `<tr><td><b>${esc(p.title)}</b><div class="muted">${esc(new Date(p.at).toLocaleString())} · ${s.N}×${s.maps.length} matches · seed ${s.seed0}${s.mock ? " · MOCK" : ""}</div></td><td>${top}</td><td class="nowrap"><button data-rename="${p.id}" title="rename">✎</button> <button data-unpin="${p.id}" title="forget this run">×</button></td></tr>`;
        }).join("");
        return `<section class="bcard"><h3 class="row" style="justify-content:space-between">Pinned runs ${canPin ? `<button data-act="pin" class="primary" title="keep this run's results (in the data folder) to compare later runs with">📌 Pin the latest run</button>` : ""}</h3>
            ${rows ? `<table class="score"><tr><th>run</th><th>win rate (95%)</th><th></th></tr>${rows}</table>` : `<div class="muted">Pin a run to compare the next ones with it (after a brain edit, say).</div>`}</section>`;
    }
    _compareCard() {
        const opts = [...(this.run0 && this.run0.matches.length ? [["current", "latest run" + (this.run0.pinned ? " (pinned)" : "")]] : []), ...this.pins.slice().reverse().map((p) => [p.id, p.title + " · " + new Date(p.at).toLocaleTimeString()])];
        if (opts.length < 2) return "";
        if (!opts.some((o) => o[0] === this.cmp.a)) this.cmp.a = (opts.find((o) => o[0] !== this.cmp.b) || opts[0])[0];
        if (!opts.some((o) => o[0] === this.cmp.b)) this.cmp.b = opts[0][0];
        const sel = (k) => `<select data-cmp="${k}">${opts.map(([v, t]) => `<option value="${esc(v)}"${v === this.cmp[k] ? " selected" : ""}>${esc(t)}</option>`).join("")}</select>`;
        const A = this._runOf(this.cmp.a), B = this._runOf(this.cmp.b);
        let body = "";
        if (A && B && A !== B) {
            // brains matched by name (an edited brain keeps its name, its label's hash changes)
            const names = [...new Set([...A.summary.brains, ...B.summary.brains].map((b) => b.name))];
            const sum = (S, n) => S.brains.filter((b) => b.name === n).reduce((a, b) => ({ n: a.n + b.n, w: a.w + b.w, mirror: a.mirror + (b.mirror || 0), kills: a.kills + b.kills, deaths: a.deaths + b.deaths, labels: [...a.labels, b.label] }), { n: 0, w: 0, mirror: 0, kills: 0, deaths: 0, labels: [] });
            const rows = names.map((n) => {
                const a = sum(A.summary, n), b = sum(B.summary, n), d = rateDiff(a.w, a.n, b.w, b.n), ta = rateText(a.w, a.n, a.mirror), tb = rateText(b.w, b.n, b.mirror);
                const verdict = (a.n && a.mirror >= a.n) || (b.n && b.mirror >= b.n) ? `<span class="muted">-</span>` : !a.n || !b.n ? `<span class="muted">only in ${a.n ? "A" : "B"}</span>` : d.sure ? `<b class="${d.d > 0 ? "ok" : "err"}">${d.d > 0 ? "better" : "worse"}</b>` : `<span class="muted">no clear difference</span>`;
                const kd = (x) => (x.n ? (x.kills / Math.max(1, x.deaths)).toFixed(2) : "-");
                return `<tr><td><b>${esc(n)}</b><div class="muted" title="labels: name#hash of what the brain does">${esc([...new Set(a.labels)].join(", ") || "-")} → ${esc([...new Set(b.labels)].join(", ") || "-")}</div></td><td title="${ta.title}">${ta.text}</td><td title="${tb.title}">${tb.text}</td>
                    <td title="95% interval of the difference: ${a.n && b.n ? `${Math.round(d.lo * 100)} to ${Math.round(d.hi * 100)} points` : "-"}">${a.n && b.n ? `${d.d >= 0 ? "+" : ""}${Math.round(d.d * 100)} pts <span class="muted">(${Math.round(d.lo * 100)}…${Math.round(d.hi * 100)})</span>` : "-"}</td><td>${verdict}</td><td>${kd(a)} → ${kd(b)}</td></tr>`;
            }).join("");
            const same = JSON.stringify([A.settings.mode, A.settings.mapKeys || A.settings.maps, A.settings.lineup.map((b) => [b.team, b.skill])]) === JSON.stringify([B.settings.mode, B.settings.mapKeys || B.settings.maps, B.settings.lineup.map((b) => [b.team, b.skill])]);
            body = `<table class="score"><tr><th>brain</th><th>A win rate</th><th>B win rate</th><th>B − A</th><th>at 95%</th><th>K/D</th></tr>${rows}</table>
                <p class="muted" style="font-size:11px">${same ? "Same mode, maps and line-up (teams, skills)." : '<span class="warn">The two runs differ in mode, maps or line-up: compare with care.</span>'} “Better / worse”: the 95% interval of the difference leaves out 0. More matches narrow it.</p>`;
        } else body = `<div class="muted">Pick two different runs.</div>`;
        return `<section class="bcard"><h3>Compare runs</h3><div class="row" style="margin-bottom:6px"><span class="muted">A</span>${sel("a")}<span class="muted">B</span>${sel("b")}</div>${body}</section>`;
    }
    _brainsCard(S) {
        const per = (x, n, d) => (x / Math.max(1, n)).toFixed(d);
        const rows = S.brains.slice().sort((a, b) => b.w / Math.max(1, b.n) - a.w / Math.max(1, a.n));
        const notApplied = (a) => a.name !== "default" && this.run0 && !this.run0.settings.mock && !this.run0.brainSupport;
        const tr = (a) => { const r = rateText(a.w, a.n, a.mirror); return `<tr><td><b>${esc(a.label)}</b>${notApplied(a) ? ' <span class="warn" title="this build plays the built-in brain">not applied</span>' : ""}</td><td>${a.n}</td><td title="${r.title}">${r.text}</td><td>${per(a.kills, a.deaths, 2)}</td><td>${per(a.kills, a.botMatches, 1)}</td><td>${per(a.damage, a.botMatches, 0)}</td><td>${a.shots ? pct(a.hits / a.shots) : "-"}</td><td>${per(a.objTime, a.botMatches, 1)}</td><td>${a.t ? pct(a.alive / a.t) : "-"}</td></tr>`; };
        return `<section class="bcard"><h3>Per brain</h3>${winChart(rows.filter((a) => a.mirror < a.n))}<table class="score"><tr><th>brain</th><th>matches</th><th title="matches its bot or team won, with the 95% interval">wins</th><th>K/D</th><th>kills/m</th><th>dmg/m</th><th>acc</th><th>obj s/m</th><th>alive</th></tr>${rows.map(tr).join("")}</table>
            <p class="muted" style="font-size:11px">Wins: matches a bot of that brain (or its team) won, ±: half the 95% interval. Per-match numbers are per bot (/m).</p></section>`;
    }
    _botsCard(S) {
        const per = (x, n, d) => (x / Math.max(1, n)).toFixed(d), teams = new Set(S.bots.map((x) => x.team)).size > 1;
        const tr = (a) => { const r = rateText(a.w, a.n); return `<tr><td>${esc(a.name)} <span class="muted">${esc(a.label)} · ${esc(String(a.skill || "").toLowerCase())}${teams ? " · " + esc(a.team) : ""}${a.objective && a.objective.type && a.objective.type !== "default" ? " · " + esc(a.objective.type) : ""}</span></td><td>${a.n}</td><td title="${r.title}">${r.text}</td><td>${a.kills}</td><td>${a.deaths}</td><td>${per(a.kills, a.deaths, 2)}</td><td>${per(a.damage, a.n, 0)}</td><td>${a.shots ? pct(a.hits / a.shots) : "-"}</td><td>${per(a.objTime, a.n, 1)}</td><td>${a.t ? pct(a.alive / a.t) : "-"}</td></tr>`; };
        return `<section class="bcard"><h3>Per bot</h3><table class="score"><tr><th>bot</th><th>matches</th><th>wins</th><th>kills</th><th>deaths</th><th>K/D</th><th>dmg/m</th><th>acc</th><th>obj s/m</th><th>alive</th></tr>${S.bots.map(tr).join("")}</table></section>`;
    }
    _mapsCard(S, R) {
        const rows = S.maps.map((M) => {
            const perf = R.perf[M.key];
            const res = M.a + M.b ? `team A ${M.a} · team B ${M.b} · draws ${M.draws}` : M.players.length ? M.players.map((p) => `${esc(p.name)} ${p.wins}`).join(" · ") + ` · draws ${M.draws}` : M.matches ? "no match ended (see timeouts)" : "";
            return `<tr><td>${esc(M.map)}</td><td>${M.matches}${M.errors ? ` <span class="err">${M.errors} failed</span>` : ""}</td><td>${res}</td><td>${M.timeouts || "-"}</td><td>${M.matches ? (M.t / M.matches).toFixed(0) + " s" : "-"}</td><td class="muted">${perf ? perf.matchesPerSecond.toFixed(1) + "/s on " + perf.threads : ""}</td></tr>`;
        }).join("");
        const errs = R.matches.filter((m) => m.error);
        return `<section class="bcard"><h3>Wins per map</h3>${mapChart(S.maps)}<table class="score"><tr><th>map</th><th>matches</th><th>wins</th><th>timeouts</th><th>avg length</th><th>speed</th></tr>${rows}</table>${errs.length ? `<div class="err">${esc(errs[0].error)}</div>` : ""}</section>`;
    }
    _matchesCard(R) {
        const ms = R.matches.filter((m) => !m.error).slice().sort(byMap);
        const wname = (m) => (m.winner ? m.winner.text || (m.winner.team ? "team " + m.winner.team : "#" + m.winner.player) : m.leader ? "(time) " + (m.leader.team ? "team " + m.leader.team : ((m.bots || []).find((b) => b.id === m.leader.player) || {}).name) + " ahead" : "-");
        return `<section class="bcard"><h3>Matches</h3><div class="bscroll"><table class="score"><tr><th>map</th><th>seed</th><th>result</th><th>score</th><th>top fragger</th><th>length</th></tr>${ms.map((m) => `<tr><td>${esc(m.mapLabel || m.map)}</td><td>${m.seed}</td><td>${esc(wname(m))}</td><td>${m.score ? m.score.join("–") : ""}</td><td>${esc(((m.bots || []).slice().sort((a, b) => b.kills - a.kills)[0] || {}).name)}</td><td>${(m.t || 0).toFixed(0)} s</td></tr>`).join("")}</table></div></section>`;
    }
    _csv() {
        const R = this.run0;
        if (!R) return;
        const lines = ["map,source,seed,bot,brain,skill,team,kills,deaths,damage,score,shots,hits,objTime,aliveTime,won,reason,length"];
        const ms = R.matches.filter((m) => !m.error).slice().sort(byMap);
        for (const m of ms) for (const b of m.bots || []) lines.push([m.map, (m.mapKey || "default/").split("/")[0], m.seed, b.name, b.brainLabel || b.brain, b.skill, b.team, b.kills, b.deaths, b.damage, b.score, b.shots, b.hits, b.objTime, b.aliveTime, b.won ? 1 : 0, m.reason, m.t].map((x) => (x == null ? "" : String(x).includes(",") ? `"${x}"` : x)).join(","));
        this.app.download("batch.csv", lines.join("\n") + "\n", "text/csv");
    }
}

// match lines in map order (by label), then seed
const byMap = (a, b) => { const x = a.mapLabel || a.map, y = b.mapLabel || b.map; return x < y ? -1 : x > y ? 1 : a.seed - b.seed; };

// a MOCK / older helper's end line -> a simbatch-like match line (the summaries read only those)
function fromEndLine(map, seed, end, specs) {
    const w = end.winner;
    const winner = w == null ? null : typeof w === "number" ? { player: w, text: (((end.scores || []).find((s) => s.id === w)) || {}).name + " wins" } : w === "draw" ? { text: "draw" } : { team: w };
    return { map, seed, t: end.t || 0, reason: end.reason, winner,
        bots: (end.scores || []).map((s) => { const sp = specs.find((b) => b.id === s.id) || {}; return { ...s, name: s.name || sp.name, brain: s.brain || sp.brain || "default", skill: s.skill || sp.skill, team: s.team || sp.team,
            won: typeof w === "number" ? w === s.id : w != null && w === (s.team || sp.team) }; }) };
}

// ---------------------------------------------------------------- small charts (inline SVG, theme colours)
// win rate per brain: a bar per brain from 0 with its 95% interval as a whisker, the rate written at its end
function winChart(rows) {
    if (!rows.length) return "";
    const W = 420, rowH = 22, L = 150, X = (p) => L + p * (W - L - 50), H = rows.length * rowH + 18;
    let g = "";
    rows.forEach((a, i) => {
        const c = wilson(a.w, a.n), y = 4 + i * rowH, label = a.label.length > 22 ? a.label.slice(0, 21) + "…" : a.label;
        g += `<g><title>${esc(a.label)}: ${rateText(a.w, a.n).title}</title><rect x="0" y="${y}" width="${W}" height="${rowH}" fill="transparent"/>
            <text x="${L - 6}" y="${y + 15}" text-anchor="end" class="ch-t">${esc(label)}</text>
            <rect x="${L}" y="${y + 5}" width="${Math.max(0, X(c.p) - L)}" height="12" rx="3" class="ch-bar"/>
            <path d="M${X(c.lo)},${y + 11}H${X(c.hi)}M${X(c.lo)},${y + 6}V${y + 16}M${X(c.hi)},${y + 6}V${y + 16}" class="ch-ci"/>
            <text x="${X(c.hi) + 5}" y="${y + 15}" class="ch-v">${pct(c.p)}</text></g>`;
    });
    const ax = [0, 0.5, 1].map((p) => `<path d="M${X(p)},2V${H - 14}" class="ch-grid"/><text x="${X(p)}" y="${H - 2}" text-anchor="middle" class="ch-a">${pct(p)}</text>`).join("");
    return `<svg class="chart" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-label="win rate per brain with 95% intervals">${ax}${g}</svg>`;
}
// wins per map: one stacked bar per map (team A, team B, draws, timeouts; free for all: wins and the rest)
function mapChart(maps) {
    const ms = maps.filter((m) => m.matches);
    if (!ms.length) return "";
    const W = 420, rowH = 20, L = 110, H = ms.length * rowH + 22, teams = ms.some((m) => m.a + m.b);
    const parts = teams ? [["a", "team A", "ch-a-team"], ["b", "team B", "ch-b-team"], ["draws", "draws", "ch-draw"], ["timeouts", "timeouts", "ch-to"]]
        : [["won", "won by a bot", "ch-bar"], ["draws", "draws", "ch-draw"], ["timeouts", "timeouts", "ch-to"]];
    let g = "";
    ms.forEach((m, i) => {
        const y = 2 + i * rowH, vals = { ...m, won: m.players.reduce((a, p) => a + p.wins, 0) };
        let x = L;
        g += `<text x="${L - 6}" y="${y + 13}" text-anchor="end" class="ch-t">${esc(m.map)}</text>`;
        for (const [k, name, cls] of parts) {
            const w = (vals[k] || 0) / m.matches * (W - L - 10);
            if (w > 0) g += `<rect x="${x}" y="${y + 3}" width="${Math.max(0, w - 2)}" height="12" rx="2" class="${cls}"><title>${esc(m.map)}: ${name} ${vals[k]} of ${m.matches}</title></rect>`;
            x += w;
        }
    });
    const key = parts.map(([, name, cls], i) => `<rect x="${L + i * 78}" y="${H - 11}" width="10" height="8" rx="2" class="${cls}"/><text x="${L + i * 78 + 14}" y="${H - 3}" class="ch-a">${name}</text>`).join("");
    return `<svg class="chart" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-label="wins per map">${g}${key}</svg>`;
}
