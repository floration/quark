// Simulator page: pick a map and mode, place bots (team, skill, brain, start + facing), give them objectives, save
// scenarios, run a match live (pause / resume / speed / step / stop, scrub what was recorded), inspect bots.

import { MapView, TEAM_COLORS } from "./mapview.js";
import { Map3D } from "./map3d.js";
import { MODES, MODE_TITLES, TEAM_MODES, mapSupports, mapObjectives, floorOrNull } from "./qmap.js";
import { LiveView, Recording } from "./live.js";
import { buildRequest } from "./helper.js";
import { api } from "./api.js";

const $ = (id) => document.getElementById(id);
const SKILLS = ["BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"];
const NAMES = ["ANNA", "BORIS", "CLEO", "DMITRI", "ELSA", "FINN", "GRETA", "HUGO", "IRIS", "JONAS", "KAI", "LENA", "MILO", "NORA", "OSKAR", "PIA"];
const OBJ_KINDS = [["default", "the brain decides"], ["goto", "go to a point"], ["hold", "hold an area"], ["follow", "follow waypoints"], ["objective", "attack / defend a mode objective"]];
const esc = (x) => String(x ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

export function newScenario() {
    return { format: "qbe-scenario", version: 1, name: "untitled", map: null, mode: "ffa", seed: 1, secs: 120, bots: [] };
}

export class SimPage {
    constructor(app) {
        this.app = app;
        this.sc = newScenario();
        this.sel = -1;              // selected scenario bot
        this.tool = "select";
        this.pick = null;           // {kind: "goto"|"hold"|"follow", bot} while picking points on the map
        this.mv = new MapView($("mapCanvas"), { handlers: {
            down: (w, ev) => this._down(w, ev), move: (w, ev) => this._move(w, ev), up: (w, ev) => this._up(w, ev), click: (w, ev) => this._click(w, ev),
        } });
        this.mv.onMouse = (w) => this._status(w);
        this.live = new LiveView(app, this.mv);
        this.mode3d = false;
        this.editOverlay = this._drawScenario.bind(this);
        this.editScreen = this._drawScenarioLabels.bind(this);
        this.mv.overlays = [this.editOverlay];
        this.mv.screenOverlays = [this.editScreen];
        this.run = null; this.rec = null;
        this._wire();
        app.on("maps", () => this._fillMaps());
        app.on("brains", () => this.renderBots());
        app.on("map", (m) => this._onMap(m));
        setInterval(() => this._tickSide(), 200);
    }

    async start() {
        this._fillMaps();
        // last session's scenario, else the first map built for FFA
        let saved = null;
        try { saved = await api.state("scenario"); } catch {}   // (a file through serve.py: the shell's browser storage starts empty)
        const q = new URLSearchParams(location.search);
        const want = q.get("map") ? { name: q.get("map"), source: q.get("source") || "default" } : saved && saved.map;
        const m = want && this.app.maps.find((x) => x.name === want.name && x.source === want.source) || this.app.maps.find((x) => x.name === "temple") || this.app.maps[0];
        if (saved && saved.format === "qbe-scenario" && !q.get("map")) this.sc = saved;
        if (q.get("mode")) this.sc.mode = q.get("mode");
        if (m) { this.sc.map = { name: m.name, source: m.source }; await this.app.loadMap(m.name, m.source); }
        this._scenarioFields();
        this._refreshScenarioList();
    }

    // ---------------------------------------------------------------- wiring
    _wire() {
        $("mapSel").onchange = async () => {
            const v = $("mapSel").value, source = v.slice(0, v.indexOf("/")), name = v.slice(v.indexOf("/") + 1);
            this.sc.map = { name, source };
            // positions are per map: starts, and the goto / hold / waypoint points (an objective's target is the
            // new map's own: the mode picks it)
            for (const b of this.sc.bots) {
                b.pos = null;
                const o = b.objective || {};
                if (o.kind === "goto" || o.kind === "hold") delete o.pos;
                else if (o.kind === "follow") o.points = [];
                else if (o.kind === "objective") delete o.target;
            }
            // the map's own mode, unless one was picked by hand
            const info = this.app.maps.find((x) => x.name === name && x.source === source);
            if (!this.sc.modeByHand && info && info.for && MODES.includes(info.for) && info.for !== this.sc.mode) { this.sc.mode = info.for; this._teamsForMode(); }
            this.pick = null; this._hint();
            await this.app.loadMap(name, source);
        };
        $("modeSel").onchange = () => { this.sc.mode = $("modeSel").value; this.sc.modeByHand = true; this._modeWarn(); this._teamsForMode(); this.renderBots(); this._save(); };
        $("slice").oninput = () => {
            const m = this.app.map; if (!m) return;
            const v = +$("slice").value;
            const y = v >= 100 ? Infinity : m.bounds.y0 + (m.bounds.y1 - m.bounds.y0) * v / 100;
            $("sliceVal").textContent = v >= 100 ? "all" : `y < ${y.toFixed(1)} m`;
            this.mv.setSlice(y);
            if (this.m3) this.m3.setSlice(y);
        };
        const opt = (id, f) => ($(id).onchange = () => { f($(id).checked); this.mv.invalidate(true); });
        opt("optLabels", (v) => (this.mv.show.labels = v));
        opt("optClip", (v) => (this.mv.show.clip = v));
        opt("optGrid", (v) => (this.mv.show.grid = v));
        opt("optTrails", (v) => (this.live.opts.trails = v));
        opt("optCones", (v) => (this.live.opts.cones = v));
        opt("optPaths", (v) => (this.live.opts.paths = v));
        $("toolSelect").onclick = () => this._setTool("select");
        $("toolAdd").onclick = () => this._setTool("add");
        $("toolFit").onclick = () => this.mv.fit();
        $("tool3d").onclick = () => this._toggle3d();
        $("addBot").onclick = () => this.addBot();
        $("fill2").onclick = () => this.fill(2);
        $("fill4").onclick = () => this.fill(4);
        $("clearBots").onclick = () => { this.sc.bots = []; this.sel = -1; this.renderBots(); this._save(); };
        $("scName").onchange = () => { this.sc.name = $("scName").value.trim() || "untitled"; this._save(); };
        $("scSeed").onchange = () => { this.sc.seed = +$("scSeed").value || 1; this._save(); };
        $("scSecs").onchange = () => { this.sc.secs = Math.max(5, +$("scSecs").value || 120); this._save(); };
        $("scSave").onclick = async () => {
            const n = (this.sc.name || "untitled").replace(/[^A-Za-z0-9_.-]/g, "_");
            try { await api.saveScenario(n, this.sc); this.app.toast("Scenario saved: " + n); this._refreshScenarioList(); } catch (e) { this.app.toast("Save failed: " + e.message); }
        };
        $("scLoad").onchange = async () => {
            const n = $("scLoad").value; $("scLoad").value = "";
            if (!n) return;
            try { this._loadScenario(await api.scenario(n)); } catch (e) { this.app.toast("Load failed: " + e.message); }
        };
        $("scExport").onclick = () => this.app.download((this.sc.name || "scenario") + ".json", JSON.stringify(this.sc, null, 1), "application/json");
        $("scImport").onclick = async () => { const f = await this.app.pickFile(".json"); if (f) try { this._loadScenario(JSON.parse(f.text)); } catch (e) { this.app.toast("Not a scenario: " + e.message); } };
        $("runStart").onclick = () => this.startRun();
        $("runPause").onclick = () => this._pause();
        $("runStep").onclick = () => { if (this.run) { this.run.control("step"); this.paused = true; this._runButtons(); } };
        $("runStop").onclick = () => { if (this.run) this.run.stop(); };
        $("runSpeed").onchange = () => { if (this.run) this.run.control("speed " + $("runSpeed").value); };
        $("timeline").oninput = () => { if (!this.rec) return; this.live.follow = false; this.live.t = +$("timeline").value; this.mv.invalidate(); this._tickSide(true); };
        $("runLive").onclick = () => { this.live.follow = true; this._tickSide(true); };
        window.addEventListener("keydown", (e) => {
            if (this.app.page !== "sim" || /INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName)) return;
            if (e.key === "f" || e.key === "F") this.mv.fit();
            if (e.key === "Escape") { this.pick = null; this._setTool("select"); this._hint(); }
            if ((e.key === "Delete" || e.key === "Backspace") && this.sel >= 0 && !this.rec) { this.sc.bots.splice(this.sel, 1); this.sel = -1; this.renderBots(); this._save(); }
            if (e.key === " " && this.run) { e.preventDefault(); this._pause(); }
        });
    }

    _fillMaps() {
        const s = $("mapSel");
        const cur = this.sc.map ? this.sc.map.source + "/" + this.sc.map.name : "";
        s.textContent = "";
        const groups = { default: "Game maps", user: "Your maps (map editor / user folder)", extra: "Other folders" };
        for (const [src, label] of Object.entries(groups)) {
            const ms = this.app.maps.filter((m) => m.source === src);
            if (!ms.length) continue;
            const og = document.createElement("optgroup"); og.label = label;
            for (const m of ms) { const o = document.createElement("option"); o.value = m.source + "/" + m.name; o.textContent = `${m.title || m.name}${m.for ? "  (" + m.for + ")" : ""}${m.handmade ? " ✎" : ""}${m.shadows ? " · same name as a game map" : ""}`; if (m.shadows) o.title = `${m.name}.qmap in ${m.dir}: the game plays this file instead of its own ${m.name}`; og.append(o); }
            s.append(og);
        }
        if (cur) s.value = cur;
        const ms = $("modeSel");
        ms.textContent = "";
        for (const m of MODES) { const o = document.createElement("option"); o.value = m; o.textContent = MODE_TITLES[m]; ms.append(o); }
        ms.value = this.sc.mode;
    }

    _onMap(m) {
        this.mv.setMap(m);
        if (this.m3) this.m3.setMap(m);
        $("slice").value = 100; $("sliceVal").textContent = "all";
        const info = this.app.maps.find((x) => x.name === m.name && x.source === m.source);
        if (!this.sc.mode || (info && info.for && this.sc.mode === "ffa" && !this.sc.bots.length && !this.sc.modeByHand)) this.sc.mode = (info && info.for) || "ffa";
        $("modeSel").value = this.sc.mode;
        $("mapSel").value = m.source + "/" + m.name;
        this._modeWarn();
        this.renderBots();
        this._status();
        if (this.rec) this._endLive();
        this._save();
    }

    _modeWarn() {
        const m = this.app.map; if (!m) return;
        const mode = this.sc.mode, info = this.app.maps.find((x) => x.name === m.name && x.source === m.source) || {};
        let w = "";
        if (!mapSupports(m, mode)) w = `This map can't host ${MODE_TITLES[mode]}: it lacks what the mode needs (${{ bomb: "team spawns and bomb sites", koth: "team spawns and a hill", race: "race spawns and checkpoints", points: "2+ points and stage-0 attack / defend spawns", payload: "team spawns and a payload path", npayload: "team spawns and a payload path", hide: "2+ spawns", tag: "2+ spawns" }[mode] || "team a and b spawns"}).`;
        else if (info.modes && info.modes.length && !info.modes.includes(mode)) w = `Not offered for ${MODE_TITLES[mode]} in the game (built for ${info.for || "?"}): it may not work.`;
        $("modeWarn").textContent = w;
    }
    _teamsForMode() {
        const team = TEAM_MODES.has(this.sc.mode);
        this.sc.bots.forEach((b, i) => { if (team && b.team === "any") b.team = i % 2 ? "b" : "a"; if (!team) b.team = "any"; });
    }

    _scenarioFields() { $("scName").value = this.sc.name; $("scSeed").value = this.sc.seed; $("scSecs").value = this.sc.secs; $("modeSel").value = this.sc.mode; }
    async _refreshScenarioList() {
        try {
            const r = await api.scenarios();
            const s = $("scLoad"); s.textContent = "";
            const o = document.createElement("option"); o.value = ""; o.textContent = r.scenarios.length ? "Load…" : "Load… (none saved)"; s.append(o);
            for (const x of r.scenarios) { const e = document.createElement("option"); e.value = x.name; e.textContent = x.name; s.append(e); }
        } catch {}
    }
    async _loadScenario(s) {
        if (!s || s.format !== "qbe-scenario") throw new Error("not a bot editor scenario");
        this.sc = s; this.sel = -1;
        this._scenarioFields();
        const m = s.map && this.app.maps.find((x) => x.name === s.map.name && x.source === s.map.source);
        if (m) await this.app.loadMap(m.name, m.source); else { this.app.toast("The scenario's map isn't here: " + (s.map && s.map.name)); this.renderBots(); }
        this._save();
    }
    // the scenario being edited is autosaved as a file (<data dir>/bot_editor/state/scenario.json), debounced
    _save() {
        clearTimeout(this._saveT);
        this._saveT = setTimeout(() => api.saveState("scenario", this.sc).catch(() => {}), 600);
    }

    // ---------------------------------------------------------------- bots
    addBot(pos) {
        const team = TEAM_MODES.has(this.sc.mode) ? (this.sc.bots.filter((b) => b.team === "a").length > this.sc.bots.filter((b) => b.team === "b").length ? "b" : "a") : "any";
        const used = new Set(this.sc.bots.map((b) => b.name));
        const name = NAMES.find((n) => !used.has(n)) || "BOT" + this.sc.bots.length;
        const b = { name, team, skill: "ADVANCED", brain: "default", pos: pos || null, yaw: 0, objective: { kind: "default" } };
        if (!pos) this._autoPlace(b);
        this.sc.bots.push(b);
        this.sel = this.sc.bots.length - 1;
        this.renderBots(); this._save();
        return b;
    }
    _autoPlace(b) {
        const m = this.app.map; if (!m) return;
        const sp = m.spawns.filter((s) => (b.team === "any" ? true : s.team === b.team || s.team === "any"));
        const taken = this.sc.bots.filter((x) => x.pos).map((x) => x.pos);
        const free = sp.find((s) => !taken.some((p) => Math.hypot(p.x - s.x, p.z - s.z) < 1));
        const s = free || sp[0] || m.spawns[0];
        if (s) { b.pos = { x: s.x, y: s.y, z: s.z }; b.yaw = Math.round(s.yaw * 180 / Math.PI); }
    }
    fill(n) {
        this.sc.bots = [];
        if (!TEAM_MODES.has(this.sc.mode) && n) { for (let i = 0; i < n * 2; i++) this.addBot(); }
        else for (let i = 0; i < n * 2; i++) this.addBot();
        this.sel = -1; this.renderBots(); this._save();
    }

    renderBots() {
        const L = $("botList");
        L.textContent = "";
        $("botCount").textContent = this.sc.bots.length ? `(${this.sc.bots.length})` : "";
        this.sc.bots.forEach((b, i) => {
            const r = document.createElement("div");
            r.className = "botrow" + (i === this.sel ? " sel" : "");
            const c = TEAM_COLORS[b.team] || TEAM_COLORS.any;
            r.innerHTML = `<span class="dot" style="background:${c}"></span><div><b>${esc(b.name)}</b> <span class="sub">${b.skill.toLowerCase()} · ${esc(b.brain)}</span><div class="sub">${esc(objText(b.objective))}${b.pos ? "" : " · spawns at a spawn point"}</div></div><button title="remove">×</button>`;
            r.onclick = (e) => { if (e.target.tagName === "BUTTON") { this.sc.bots.splice(i, 1); if (this.sel >= this.sc.bots.length) this.sel = -1; this.renderBots(); this._save(); return; } this.sel = i; this.renderBots(); };
            L.append(r);
        });
        this._botEditor();
        this.mv.invalidate();
    }

    _botEditor() {
        const E = $("botEdit");
        E.textContent = "";
        const b = this.sc.bots[this.sel];
        if (!b) return;
        const team = TEAM_MODES.has(this.sc.mode);
        const brains = ["default", ...this.app.brainList.filter((x) => x.source === "user").map((x) => x.name), "(editor)"];
        const objs = this.app.map ? mapObjectives(this.app.map) : [];
        const o = b.objective || (b.objective = { kind: "default" });
        const sel = (opts, v) => opts.map(([k, t]) => `<option value="${esc(k)}" ${k === v ? "selected" : ""}>${esc(t)}</option>`).join("");
        E.innerHTML = `<div class="botedit">
          <div class="field"><span>Name</span><input data-k="name" value="${esc(b.name)}"></div>
          <div class="field"><span>Team</span><select data-k="team" ${team ? "" : "disabled"}>${sel(team ? [["a", "A (red)"], ["b", "B (blue)"]] : [["any", "free for all"]], b.team)}</select></div>
          <div class="field"><span>Skill</span><select data-k="skill">${sel(SKILLS.map((s) => [s, s.toLowerCase()]), b.skill)}</select></div>
          <div class="field"><span>Brain</span><select data-k="brain">${sel(brains.map((n) => [n, n === "(editor)" ? "(the brain open in the editor, unsaved)" : n]), b.brain)}</select></div>
          <div class="field"><span>Start</span><div class="row">${b.pos ? `<span class="muted">${b.pos.x.toFixed(1)}, ${b.pos.y.toFixed(1)}, ${b.pos.z.toFixed(1)} · ${b.yaw}°</span><button data-a="spawn">use spawns</button>` : `<span class="muted">a spawn point</span><button data-a="place">place</button>`}</div></div>
          <div class="field"><span>Facing °</span><input data-k="yaw" type="number" value="${b.yaw}" step="15"></div>
          <h3>Objective</h3>
          <div class="field"><span>Do</span><select data-o="kind">${sel(OBJ_KINDS, o.kind)}</select></div>
          <div id="objFields"></div>
        </div>`;
        const of = E.querySelector("#objFields");
        if (o.kind === "goto") of.innerHTML = `<div class="row"><span class="muted">${o.pos ? fmtP(o.pos) : "no point yet"}</span><button data-a="pick-goto">Pick on map</button></div>`;
        else if (o.kind === "hold") of.innerHTML = `<div class="row"><span class="muted">${o.pos ? fmtP(o.pos) : "no centre yet"}</span><button data-a="pick-hold">Pick centre</button></div><div class="field"><span>Radius m</span><input data-o="radius" type="number" value="${o.radius || 4}" min="0.5" step="0.5"></div>`;
        else if (o.kind === "follow") of.innerHTML = `<div class="row"><span class="muted">${(o.points || []).length} waypoints</span><button data-a="pick-follow">Add on map</button><button data-a="clear-follow">Clear</button></div><label class="row muted"><input type="checkbox" data-o="loop" ${o.loop !== false ? "checked" : ""}> loop</label>`;
        else if (o.kind === "objective") of.innerHTML = objs.length ? `<div class="field"><span>Target</span><select data-o="target">${objs.map((x, i) => `<option value="${i}" ${o.target && o.target.kind === x.kind && o.target.index === x.index ? "selected" : ""}>${esc(x.label)}</option>`).join("")}</select></div>
            <div class="field"><span>Role</span><select data-o="role">${sel([["attack", "attack"], ["defend", "defend"]], o.role || "attack")}</select></div>` : `<div class="warn">This map has no mode objectives.</div>`;
        else of.innerHTML = `<div class="muted" style="font-size:12px">The brain's <code>objective ${esc(this.sc.mode === "1v1" ? "arena" : this.sc.mode)}</code> slot picks where to go (Brain tab).</div>`;
        if (o.kind && o.kind !== "default") of.insertAdjacentHTML("beforeend", `<label class="check muted" title="off: the bot forgets enemies it can't see (no hunting, no turning to sounds), so a hold stays put"><input type="checkbox" data-o="chase" ${o.chase !== false ? "checked" : ""}><span>chase enemies it lost sight of</span></label>`);
        E.querySelectorAll("[data-k]").forEach((inp) => (inp.onchange = () => {
            const k = inp.dataset.k;
            b[k] = k === "yaw" ? +inp.value || 0 : inp.value;
            this.renderBots(); this._save();
        }));
        E.querySelectorAll("[data-o]").forEach((inp) => (inp.onchange = () => {
            const k = inp.dataset.o;
            if (k === "kind") { b.objective = { kind: inp.value }; if (inp.value === "objective" && objs[0]) b.objective.target = { kind: objs[0].kind, index: objs[0].index }; if (inp.value === "goto" || inp.value === "hold") this._startPick(inp.value, b); if (inp.value === "follow") this._startPick("follow", b); }
            else if (k === "radius") o.radius = Math.max(0.5, +inp.value || 4);
            else if (k === "loop") o.loop = inp.checked;
            else if (k === "chase") o.chase = inp.checked;
            else if (k === "role") o.role = inp.value;
            else if (k === "target") { const x = objs[+inp.value]; o.target = { kind: x.kind, index: x.index }; }
            this.renderBots(); this._save();
        }));
        E.querySelectorAll("[data-a]").forEach((btn) => (btn.onclick = () => {
            const a = btn.dataset.a;
            if (a === "spawn") { b.pos = null; this.renderBots(); this._save(); }
            else if (a === "place") { this._setTool("add"); this.placeFor = this.sel; this._hint("Click the map to place " + b.name + "."); }
            else if (a.startsWith("pick-")) this._startPick(a.slice(5), b);
            else if (a === "clear-follow") { o.points = []; this.renderBots(); this._save(); }
        }));
    }

    _startPick(kind, b) {
        this.pick = { kind, bot: b };
        this._hint(kind === "follow" ? `Click the map to add waypoints for ${b.name}; Esc when done.` : `Click the map: ${kind === "goto" ? "where " + b.name + " goes" : "the centre of the area " + b.name + " holds"}.`);
    }
    _hint(t = "") { $("mapHint").textContent = t; }
    _setTool(t) { this.tool = t; $("toolSelect").classList.toggle("on", t === "select"); $("toolAdd").classList.toggle("on", t === "add"); if (t === "add") this._hint("Click the map to place a bot (drag to set its facing)."); else if (!this.pick) this._hint(); }

    // ---------------------------------------------------------------- map interaction (editing)
    _floorOrNull(w) { const m = this.app.map; return m ? floorOrNull(m, w.x, w.z, this.mv.slice === Infinity ? 1e9 : this.mv.slice) : null; }
    _hitBot(w) {
        const r = Math.max(0.8, 10 / this.mv.cam.s);
        for (let i = this.sc.bots.length - 1; i >= 0; i--) { const p = this.sc.bots[i].pos; if (p && Math.hypot(p.x - w.x, p.z - w.z) < r) return i; }
        return -1;
    }
    _hitYaw(w) {
        const b = this.sc.bots[this.sel];
        if (!b || !b.pos) return false;
        const a = b.yaw * Math.PI / 180, d = 26 / this.mv.cam.s;
        const hx = b.pos.x - Math.sin(a) * d, hz = b.pos.z - Math.cos(a) * d;
        return Math.hypot(hx - w.x, hz - w.z) < 8 / this.mv.cam.s;
    }
    _down(w, ev) {
        if (this.rec) {   // live: select a bot
            const id = this.live.pick(w);
            if (id >= 0) { this.live.sel = id; this._tickSide(true); this.mv.invalidate(); return true; }
            return false;
        }
        if (this.pick) {
            const y = this._floorOrNull(w);
            if (y == null) { this._hint("No floor there: click a point on the map (Esc: stop picking)."); return true; }
            const p = [w.x, y, w.z], o = this.pick.bot.objective;
            if (this.pick.kind === "follow") { (o.points || (o.points = [])).push(p); }
            else { o.pos = p; if (this.pick.kind === "hold" && !o.radius) o.radius = 4; this.pick = null; this._hint(); }
            this.renderBots(); this._save();
            return true;
        }
        if (this.tool === "add") {
            const idx = this.placeFor != null && this.sc.bots[this.placeFor] ? this.placeFor : -1;
            const y = this._floorOrNull(w);
            if (y == null) { this._hint("No floor there: a bot is placed on the map (Esc: stop placing)."); return true; }
            const pos = { x: w.x, y, z: w.z };
            let b;
            if (idx >= 0) { b = this.sc.bots[idx]; b.pos = pos; this.sel = idx; } else b = this.addBot(pos);
            this.placeFor = null;
            this._drag = { kind: "yaw", b };
            this.renderBots();
            return true;
        }
        if (this.sel >= 0 && this._hitYaw(w)) { this._drag = { kind: "yaw", b: this.sc.bots[this.sel] }; return true; }
        const i = this._hitBot(w);
        if (i >= 0) { this.sel = i; this._drag = { kind: "move", b: this.sc.bots[i], dx: this.sc.bots[i].pos.x - w.x, dz: this.sc.bots[i].pos.z - w.z }; this.renderBots(); return true; }
        // objective handles of the selected bot (drag goto / hold centre / waypoints)
        const b = this.sc.bots[this.sel];
        if (b && b.objective) {
            const near = (p) => p && Math.hypot(p[0] - w.x, p[2] - w.z) < 8 / this.mv.cam.s;
            if (near(b.objective.pos)) { this._drag = { kind: "opos", b }; return true; }
            const k = (b.objective.points || []).findIndex(near);
            if (k >= 0) { this._drag = { kind: "wp", b, k }; return true; }
        }
        return false;
    }
    _move(w) {
        const d = this._drag; if (!d) return;
        if (d.kind === "move") { const p = { x: w.x + d.dx, z: w.z + d.dz }, y = this._floorOrNull(p); if (y != null) d.b.pos = { x: p.x, y, z: p.z }; }   // (never off the floor)
        else if (d.kind === "yaw") { const dx = w.x - d.b.pos.x, dz = w.z - d.b.pos.z; if (Math.hypot(dx, dz) > 0.3) { let a = Math.atan2(-dx, -dz) * 180 / Math.PI; if (!window.event || !window.event.shiftKey) a = Math.round(a / 15) * 15; d.b.yaw = Math.round(a); } }
        else if (d.kind === "opos" || d.kind === "wp") {
            const y = this._floorOrNull(w);
            if (y != null) { if (d.kind === "opos") d.b.objective.pos = [w.x, y, w.z]; else d.b.objective.points[d.k] = [w.x, y, w.z]; }
        }
        this.mv.invalidate();
    }
    _up() { if (this._drag) { this._drag = null; if (this.tool === "add") this._setTool("select"); this.renderBots(); this._save(); } }
    _click(w) {
        if (this.rec) { this.live.sel = -1; this._tickSide(true); return; }
        if (this.sel >= 0) { this.sel = -1; this.renderBots(); }
    }

    _status(w) {
        const m = this.app.map;
        const pos = w || this.mv.mouse;
        $("mapInfo").textContent = m ? `${m.title} · ${m.nBoxes} boxes${m.geometryFrom ? " (read by the game)" : ""} · ${(m.bounds.x1 - m.bounds.x0).toFixed(0)}×${(m.bounds.z1 - m.bounds.z0).toFixed(0)} m${pos ? ` · x ${pos.x.toFixed(1)} z ${pos.z.toFixed(1)}` : ""} · ${this.mv.frameMs.toFixed(1)} ms` : "";
    }

    // ---------------------------------------------------------------- drawing the scenario (edit mode)
    _drawScenario(ctx, mv) {
        const px = 1 / mv.cam.s;
        this.sc.bots.forEach((b, i) => {
            const o = b.objective || {};
            const sel = i === this.sel;
            if (!b.pos) return;
            const c = TEAM_COLORS[b.team] || TEAM_COLORS.any;
            ctx.globalAlpha = sel ? 1 : 0.55;
            if (o.kind === "goto" && o.pos) { line(ctx, b.pos.x, b.pos.z, o.pos[0], o.pos[2], "#fde047", px, true); cross(ctx, o.pos[0], o.pos[2], 7 * px, "#fde047", px); }
            if (o.kind === "hold" && o.pos) { ctx.strokeStyle = "#fde047"; ctx.lineWidth = 2 * px; ctx.setLineDash([5 * px, 4 * px]); ctx.beginPath(); ctx.arc(o.pos[0], o.pos[2], o.radius || 4, 0, 7); ctx.stroke(); ctx.setLineDash([]); line(ctx, b.pos.x, b.pos.z, o.pos[0], o.pos[2], "#fde047", px, true); dot(ctx, o.pos[0], o.pos[2], 4 * px, "#fde047"); }
            if (o.kind === "follow" && o.points && o.points.length) {
                ctx.strokeStyle = "#a3e635"; ctx.lineWidth = 2 * px; ctx.setLineDash([6 * px, 4 * px]);
                ctx.beginPath(); ctx.moveTo(b.pos.x, b.pos.z); for (const p of o.points) ctx.lineTo(p[0], p[2]); if (o.loop !== false && o.points.length > 1) ctx.lineTo(o.points[0][0], o.points[0][2]); ctx.stroke(); ctx.setLineDash([]);
                for (const p of o.points) dot(ctx, p[0], p[2], 4.5 * px, "#a3e635");
            }
            if (o.kind === "objective" && o.target && this.app.map) {
                const x = mapObjectives(this.app.map).find((q) => q.kind === o.target.kind && q.index === o.target.index);
                if (x) line(ctx, b.pos.x, b.pos.z, x.x, x.z, o.role === "defend" ? "#38bdf8" : "#fb7185", px, true);
            }
            ctx.globalAlpha = 1;
            const r = Math.max(0.5, 7 * px), a = b.yaw * Math.PI / 180, fx = -Math.sin(a), fz = -Math.cos(a);
            ctx.fillStyle = "rgba(255,255,255,0.06)";
            ctx.beginPath(); ctx.moveTo(b.pos.x, b.pos.z); ctx.arc(b.pos.x, b.pos.z, 12, Math.atan2(fz, fx) - 0.9, Math.atan2(fz, fx) + 0.9); ctx.closePath(); ctx.fill();
            ctx.fillStyle = c; ctx.strokeStyle = sel ? "#fff" : "#000"; ctx.lineWidth = (sel ? 2.5 : 1.2) * px;
            ctx.beginPath(); ctx.arc(b.pos.x, b.pos.z, r, 0, 7); ctx.fill(); ctx.stroke();
            ctx.strokeStyle = "#fff"; ctx.lineWidth = 2 * px;
            ctx.beginPath(); ctx.moveTo(b.pos.x, b.pos.z); ctx.lineTo(b.pos.x + fx * r * 1.9, b.pos.z + fz * r * 1.9); ctx.stroke();
            if (sel) {   // facing handle
                const d = 26 * px;
                ctx.strokeStyle = "rgba(255,255,255,.5)"; ctx.lineWidth = px;
                ctx.beginPath(); ctx.arc(b.pos.x, b.pos.z, d, 0, 7); ctx.stroke();
                dot(ctx, b.pos.x + fx * d, b.pos.z + fz * d, 5 * px, "#fff");
            }
        });
    }
    _drawScenarioLabels(ctx, mv) {
        if (!mv.show.labels) return;
        ctx.font = "600 11px system-ui, sans-serif"; ctx.textAlign = "center"; ctx.textBaseline = "bottom";
        for (const b of this.sc.bots) {
            if (!b.pos) continue;
            const p = mv.toScreen(b.pos.x, b.pos.z);
            ctx.lineWidth = 3; ctx.strokeStyle = "rgba(0,0,0,.8)"; ctx.strokeText(b.name, p.x, p.y - 11);
            ctx.fillStyle = "#fff"; ctx.fillText(b.name, p.x, p.y - 11);
        }
        const o = this.sc.bots[this.sel] && this.sc.bots[this.sel].objective;
        if (o && o.points) o.points.forEach((q, i) => { const p = mv.toScreen(q[0], q[2]); ctx.fillStyle = "#0b0f14"; ctx.font = "700 9px system-ui"; ctx.textBaseline = "middle"; ctx.fillText(String(i + 1), p.x, p.y + 0.5); ctx.textBaseline = "bottom"; ctx.font = "600 11px system-ui, sans-serif"; });
        const unplaced = this.sc.bots.filter((b) => !b.pos).length;
        if (unplaced) { ctx.textAlign = "left"; ctx.fillStyle = "#9fb0c4"; ctx.fillText(`${unplaced} bot${unplaced > 1 ? "s" : ""} start at spawn points`, 10, mv.h - 26); }
    }

    // ---------------------------------------------------------------- 3D
    _toggle3d() {
        this.mode3d = !this.mode3d;
        $("tool3d").classList.toggle("on", this.mode3d);
        $("mapWrap").classList.toggle("three", this.mode3d);
        if (this.mode3d) {
            if (!this.m3) { this.m3 = new Map3D($("map3d"), this); if (this.app.map) this.m3.setMap(this.app.map); }
            this.m3.setSlice(this.mv.slice);
            this.m3.start();
        } else if (this.m3) this.m3.stop();
    }

    // ---------------------------------------------------------------- running
    async startRun() {
        const m = this.app.map;
        if (!m) return;
        if (!this.sc.bots.length) { this.fill(TEAM_MODES.has(this.sc.mode) ? 2 : 2); }
        if (this.run) this.run.stop();
        const brains = {};
        for (const b of this.sc.bots) {
            if (b.brain === "default") continue;
            if (!this.app.brainText.has(b.brain) && b.brain !== "(editor)") await this.app.loadBrainText(b.brain).catch(() => {});
            const t = this.app.brainTextFor(b.brain);
            if (t) brains[b.brain] = t;
        }
        const uses = this.app.helper.uses || {};
        const ignored = [];
        if (!uses.brains && this.sc.bots.some((b) => b.brain && b.brain !== "default")) ignored.push("brains other than the default");
        if (!uses.objectives && this.sc.bots.some((b) => b.objective && b.objective.kind !== "default")) ignored.push("objective overrides");
        if (ignored.length) this.app.toast(`This quark_maptool doesn't run ${ignored.join(" or ")}: ${!uses.brains ? "every bot plays the game's built-in brain (the game's brain code isn't in this build yet)" : "bots play the mode's own objectives"}. (?mock in the address: the stand-in simulation, which uses them.)`, 9000);
        const warnedBrains = !uses.brains && ignored.length > 0;
        const sc = { ...this.sc, mapSource: m.source, speed: $("runSpeed").value === "max" ? "max" : +$("runSpeed").value };
        const req = buildRequest(sc, m, brains);
        req.mode = this.sc.mode;
        const rec = new Recording();
        this.rec = rec;
        this.live.attach(rec);
        this.live.sel = -1;
        this.paused = false;
        const run = this.app.helper.start(req, {
            onLine: (l) => {
                if (l.type === "error") { this.app.toast("Helper: " + l.error, 8000); return; }
                if (l.type === "warning") { if (!(warnedBrains && /brain/i.test(l.warning || ""))) this.app.toast("Helper: " + (l.warning || ""), 5000); return; }
                if (l.type === "start") for (const b of l.bots || []) { const p = this.app.paramsOf(b.brain || "default").fov; if (p) b.fov = p[Math.max(0, ["BEGINNER", "INTERMEDIATE", "ADVANCED", "EXPERT"].indexOf(String(b.skill).toUpperCase()))]; }
                rec.push(l); this.mv.invalidate();
            },
            onExit: (x) => { if (this.run === run) { this.run = null; this._runButtons(); this._tickSide(true); if (x && x.code) this.app.toast("helper exited with " + x.code + ": " + (x.stderr || "")); } },
            onError: (e) => { this.app.toast("Run failed: " + e.message, 6000); this.run = null; this._runButtons(); },
        }, m, (name) => this.app.paramsOf(name));
        this.run = run;
        this._runButtons();
        $("runStart").textContent = "▶ Run again";
        this._hint();
    }
    _pause() {
        if (!this.run) return;
        this.paused = !this.paused;
        this.run.control(this.paused ? "pause" : "resume");
        this._runButtons();
    }
    _endLive() {
        if (this.run) this.run.stop();
        this.run = null; this.rec = null;
        this.live.detach();
        this.mv.overlays = [this.editOverlay];
        this.mv.screenOverlays = [this.editScreen];
        $("runStart").textContent = "▶ Run";
        this._runButtons();
        this._tickSide(true);
    }
    _runButtons() {
        const on = !!this.run;
        $("runPause").disabled = !on; $("runStep").disabled = !on; $("runStop").disabled = !on;
        $("runPause").textContent = this.paused ? "Resume" : "Pause";
        $("timeline").disabled = !this.rec; $("runLive").disabled = !this.rec;
        let edit = $("runEdit");
        if (this.rec && !on && !edit) {
            edit = document.createElement("button"); edit.id = "runEdit"; edit.textContent = "Back to editing";
            edit.onclick = () => this._endLive();
            $("runBar").append(edit);
        } else if ((!this.rec || on) && edit) edit.remove();
    }
    _tickSide(force) {
        const side = $("liveSide");
        if (this.rec) {
            const tl = $("timeline");
            tl.max = this.rec.duration.toFixed(2);
            if (this.live.follow) tl.value = this.rec.duration;
            const t = this.live.displayTime();
            $("tlabel").textContent = `${t.toFixed(1)} / ${this.rec.duration.toFixed(1)} s${this.live.follow ? " · live" : ""}`;
            $("runLive").classList.toggle("on", this.live.follow);
            if (force || this.run || !this._sideStatic) { this.live.renderSide(side); this._sideStatic = !this.run; }
        } else if (force || !this._legend) {
            this._legend = true;
            $("tlabel").textContent = "--";
            side.innerHTML = `<h3>How to</h3><div class="muted" style="font-size:12px;line-height:1.5">
              <b>Place bots</b>: “+ Bot” or the Place tool, then drag a bot to move it and the white handle to turn it (15° steps, Shift: free).
              Wheel zooms, right / middle drag pans, <kbd>F</kbd> fits. “Show below” hides floors above a height (indoor maps).<br>
              <b>Objectives</b>: pick a bot, choose what it does, click the map. Drag the yellow / green markers to move them.<br>
              <b>Run</b> plays the match live; scrub the timeline; click a bot for its state and current decision.</div>
              <h3>Legend</h3><div class="legend">
              <i style="background:linear-gradient(90deg,#16222f,#1e6868,#968f54,#ecdeb4)"></i><span>box tops by height (low → high)</span>
              <i style="background:#3b3f46"></i><span>wallbang (shots go through)</span><i style="background:#b4401a"></i><span>lava</span><i style="background:#1e4f8f"></i><span>water</span>
              <i style="background:rgba(74,222,128,.5);border:1px solid #4ade80"></i><span>ladder</span>
              <i style="border:2px dashed #fb923c"></i><span>bomb site</span><i style="border:2px dashed #facc15"></i><span>hill</span><i style="border:2px dashed #22d3ee"></i><span>multi point</span>
              <i style="border:2px dashed #c084fc"></i><span>race checkpoint</span><i style="border-top:2px dashed #e879f9"></i><span>payload track</span>
              <i style="background:#f05252;clip-path:polygon(50% 0,100% 100%,0 100%)"></i><span>spawn (team A red, B blue, grey any)</span></div>`;
        }
        this._status();
    }
}

function objText(o) {
    if (!o || o.kind === "default") return "objective: the brain's";
    if (o.kind === "goto") return o.pos ? `goto ${fmtP(o.pos)}` : "goto (pick a point)";
    if (o.kind === "hold") return o.pos ? `hold ${fmtP(o.pos)} r ${o.radius || 4} m` : "hold (pick a centre)";
    if (o.kind === "follow") return `follow ${(o.points || []).length} waypoints`;
    if (o.kind === "objective") return `${o.role || "attack"} ${o.target ? o.target.kind + " " + (o.target.index + 1) : "?"}`;
    return o.kind;
}
const fmtP = (p) => `${p[0].toFixed(1)}, ${p[2].toFixed(1)}`;
function line(ctx, x0, z0, x1, z1, c, px, dash) { ctx.strokeStyle = c; ctx.lineWidth = 1.5 * px; if (dash) ctx.setLineDash([5 * px, 4 * px]); ctx.beginPath(); ctx.moveTo(x0, z0); ctx.lineTo(x1, z1); ctx.stroke(); ctx.setLineDash([]); }
function cross(ctx, x, z, r, c, px) { ctx.strokeStyle = c; ctx.lineWidth = 2.5 * px; ctx.beginPath(); ctx.moveTo(x - r, z - r); ctx.lineTo(x + r, z + r); ctx.moveTo(x + r, z - r); ctx.lineTo(x - r, z + r); ctx.stroke(); }
function dot(ctx, x, z, r, c) { ctx.fillStyle = c; ctx.beginPath(); ctx.arc(x, z, r, 0, 7); ctx.fill(); }
