// Adapted from tools/bot_editor/web/js/values.js at c7aae27. Values view for a mode: this editor's keys (time limit,
// score limit, rounds, map pool), the keys other editors own (read-only, kept as written), every param with its
// unit, range, doc and built-in default, state (lifetime, per team / player, initial value), timers (who starts and
// stops them), objects in use and every curve as an editable plot. Edits go back into the AST / the key lines.

import { symbols, clone, declKey, CATALOG, walkStmts, paramDefault } from "./lang.js";
import { canonNum, roundTo, printExpr, dataText } from "./qmode.js";
import { CurveWidget } from "./curves.js";
import { OWNER_TEXT, PHYSICS_EDITOR_URL } from "./qfile.js";

const esc = (x) => String(x ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const catParam = (d) => (d.kind === "param" && CATALOG.params[d.name]) || null;
export function unitOf(d) { const c = catParam(d); if (c && c.unit) return c.unit; const m = /^#\s*(s|m|m\/s|hp\/s|\/s)\b/.exec(d.trail || ""); return m ? m[1] : ""; }
export function docOf(d) { const c = catParam(d); return (d.trail || "").replace(/^#\s?/, "") || (c && c.doc) || ""; }

const KEY_INFO = {
    timelimit: { label: "time limit", unit: "s", min: 0, max: 3600, doc: "0 = the mode's own clock; beats the logic's params (server.cpp:206-212)" },
    scorelimit: { label: "score limit", unit: "points", min: 0, max: 10000, doc: "0 = the mode's own target" },
    rounds: { label: "rounds to win", unit: "rounds", min: 0, max: 50, doc: "0 = the mode's own" },
};

export class ValuesView {
    constructor(root, app) { this.root = root; this.app = app; this.widgets = []; }

    setBrain(ast, def, file) {
        this.brain = clone(ast);
        this.def = def;
        this.file = file;   // {keys: [{raw, key, value, owner}], modeKeys: {k: v}, maps: [names]}
        this.render();
    }
    _changed() { this.app.brainEdited(clone(this.brain), "values"); }
    scrollTo(key) { const e = this.root.querySelector(`[data-key="${CSS.escape(key)}"]`); if (e) { e.scrollIntoView({ block: "center" }); e.classList.add("flash"); setTimeout(() => e.classList.remove("flash"), 900); } }

    render() {
        const R = this.root, B = this.brain, F = this.file || { keys: [], modeKeys: {} };
        const defS = this.def ? symbols(this.def) : null;
        const S = symbols(B);
        R.textContent = "";
        this.widgets = [];
        const section = (title, note) => { const sec = document.createElement("div"); sec.className = "vgroup"; sec.innerHTML = `<h3>${esc(title)}</h3>${note ? `<div class="muted vnote">${note}</div>` : ""}`; R.append(sec); return sec; };

        // ---- this editor's keys
        {
            const sec = section("Mode keys", `The key=value lines this editor owns. The host's time limit / score limit / rounds (when &gt; 0) beat the logic's params, which beat the base mode's defaults.`);
            const t = document.createElement("table"); t.className = "params";
            t.innerHTML = `<tr><th>key</th><th>unit</th><th>value</th><th></th><th>what it does</th></tr>`;
            const mk = F.modeKeys || {};
            const ro = (k, v, doc) => { const tr = document.createElement("tr"); tr.innerHTML = `<td class="n">${esc(k)}</td><td class="u"></td><td class="ro mono">${esc(v ?? "(missing)")}</td><td></td><td class="doc">${doc}</td>`; t.append(tr); };
            ro("custommode", mk.custommode, "the mode's name (Save as changes it)");
            ro("mode", mk.mode, "the base mode it plays as: objectives, teams, HUD, bots (make a new mode to change it)");
            for (const [k, info] of Object.entries(KEY_INFO)) {
                const tr = document.createElement("tr"); tr.dataset.key = "key:" + k;
                tr.innerHTML = `<td class="n">${esc(k)}</td><td class="u">${esc(info.unit)}</td>`;
                const td = document.createElement("td");
                const inp = document.createElement("input");
                inp.value = mk[k] ?? "0";
                inp.title = `${info.label}: ${info.min}..${info.max}`;
                inp.onchange = () => {
                    const x = Number(inp.value);
                    if (!Number.isInteger(x) || x < info.min || x > info.max) { this.app.app.toast(`${k}: a whole number ${info.min}..${info.max} (the game clamps to that)`); inp.value = mk[k] ?? "0"; return; }
                    this.app.setKey(k, String(x));
                };
                td.append(inp); tr.append(td);
                tr.append(document.createElement("td"));
                const doc = document.createElement("td"); doc.className = "doc"; doc.textContent = info.doc; tr.append(doc);
                t.append(tr);
            }
            {
                const tr = document.createElement("tr"); tr.dataset.key = "key:maps";
                tr.innerHTML = `<td class="n">maps</td><td class="u">pool</td>`;
                const td = document.createElement("td"); td.colSpan = 2;
                const inp = document.createElement("input"); inp.style.width = "100%"; inp.style.textAlign = "left";
                inp.value = mk.maps ?? ""; inp.placeholder = "empty: the host's map only"; inp.setAttribute("list", "mapNames");
                inp.onchange = () => {
                    const v = inp.value.split(",").map((x) => x.trim()).filter(Boolean);
                    if (v.some((x) => !/^[A-Za-z0-9 _-]{1,40}$/.test(x))) { this.app.app.toast("map names: letters, digits, space, _ and - (comma separated)"); inp.value = mk.maps ?? ""; return; }
                    if (v.length > 32) { this.app.app.toast("at most 32 maps"); return; }
                    this.app.setKey("maps", v.join(","));
                };
                td.append(inp); tr.append(td);
                const doc = document.createElement("td"); doc.className = "doc"; doc.textContent = "map pool, comma separated: rotates after each match"; tr.append(doc);
                t.append(tr);
            }
            sec.append(t);
        }
        // ---- the others' keys
        const foreign = (F.keys || []).filter((l) => l.owner !== "mode" && l.key !== null);
        if (foreign.length) {
            const sec = section("Other keys (read-only here)", `Kept exactly as written, in place. Movement keys belong to the <a href="${PHYSICS_EDITOR_URL}" target="_blank" rel="noopener">physics editor</a>.`);
            const t = document.createElement("table"); t.className = "params keys";
            t.innerHTML = `<tr><th>line</th><th>key</th><th>value</th><th>owner</th></tr>`;
            for (const l of foreign) {
                const tr = document.createElement("tr");
                tr.innerHTML = `<td class="u">${l.n}</td><td class="n">${esc(l.key)}</td><td class="ro mono">${esc(l.value)}</td><td class="doc own-${l.owner}">${esc(OWNER_TEXT[l.owner])}</td>`;
                t.append(tr);
            }
            sec.append(t);
        }

        // ---- params, grouped by the file's section comments
        const groups = [];
        let cur = null;
        for (const d of B.decls) {
            const heading = (d.comments || []).filter((c) => c && c.startsWith("#") && !/^#\s*\(fixture/.test(c)).pop();
            if (d.kind !== "param") { if (heading) cur = null; continue; }
            if (!cur) { cur = { title: "params", items: [] }; groups.push(cur); }
            cur.items.push(d);
        }
        for (const g of groups) {
            const sec = section("Params", "Numbers the logic and the objects read. A file that only changes numbers runs all native (no interpreted slot).");
            const t = document.createElement("table"); t.className = "params";
            t.innerHTML = `<tr><th>param</th><th>unit</th><th>value</th><th>built-in</th><th>range</th><th></th><th>what it does</th></tr>`;
            for (const d of g.items) t.append(this._paramRow(d, defS && defS.params.get(d.name)));
            sec.append(t);
        }
        // ---- state
        if (S.states.size) {
            const sec = section("State", "Values the logic keeps: <b>match</b> ones reset at reset and match start, <b>round</b> ones at each round setup. A per-player array holds 32 values, per-team 2 (512 in all).");
            const t = document.createElement("table"); t.className = "params";
            t.innerHTML = `<tr><th>state</th><th>resets</th><th>per</th><th>type</th><th>starts at</th><th>what it holds</th></tr>`;
            for (const d of S.states.values()) {
                const tr = document.createElement("tr"); tr.dataset.key = declKey(d);
                const mod = defS && defS.states.get(d.name) && JSON.stringify(defS.states.get(d.name).value) !== JSON.stringify(d.value);
                tr.innerHTML = `<td class="n ${mod ? "mod" : ""}">${esc(d.name)}</td>`;
                const life = document.createElement("td"); life.append(this._choose(["match", "round"], d.life, (v) => { d.life = v; this._changed(); })); tr.append(life);
                const per = document.createElement("td"); per.append(this._choose(["-", "team", "player"], d.per || "-", (v) => { d.per = v === "-" ? null : v; this._changed(); })); tr.append(per);
                const ty = document.createElement("td"); ty.className = "u"; ty.textContent = d.type; tr.append(ty);
                const v = document.createElement("td");
                if (d.type === "bool") { const cb = document.createElement("input"); cb.type = "checkbox"; cb.checked = d.value; cb.onchange = () => { d.value = cb.checked; this._changed(); }; v.append(cb); }
                else if (d.type === "team" || d.type === "player") v.append(this._choose(d.type === "team" ? ["none", "A", "B"] : ["none"], d.value, (x) => { d.value = x; this._changed(); }));
                else { const inp = document.createElement("input"); inp.value = canonNum(d.value, d.type); inp.onchange = () => { const x = Number(inp.value); if (!Number.isFinite(x) || (d.type === "int" && !Number.isInteger(x))) { inp.value = canonNum(d.value, d.type); return; } d.value = roundTo(x, d.type); this._changed(); }; v.append(inp); }
                tr.append(v);
                const doc = document.createElement("td"); doc.className = "doc"; doc.textContent = (d.trail || "").replace(/^#\s?/, ""); tr.append(doc);
                t.append(tr);
            }
            sec.append(t);
        }
        // ---- timers
        if (S.timers.size) {
            const sec = section("Timers", "One-shot clocks: startTimer(name, s) arms one, slot timer &lt;name&gt; runs when it runs out (checked once a tick, before tick).");
            const t = document.createElement("table"); t.className = "params";
            t.innerHTML = `<tr><th>timer</th><th>started in</th><th>stopped in</th><th>read in</th><th>slot</th></tr>`;
            for (const d of S.timers.values()) {
                const where = { startTimer: new Set(), stopTimer: new Set(), timerLeft: new Set() };
                for (const x of B.decls) if (x.kind === "slot" || x.kind === "proc") walkStmts(x.body, (s) => {
                    const label = x.kind === "proc" ? x.name + "()" : x.name === "timer" ? "timer " + x.timer : x.name;
                    if (s.op === "do" && where[s.fn] && s.args[0] && s.args[0].name === d.name) where[s.fn].add(label);
                    if (JSON.stringify(s).includes(`"fn":"timerLeft","args":[{"op":"name","name":"${d.name}"`)) where.timerLeft.add(label);
                });
                const tr = document.createElement("tr"); tr.dataset.key = declKey(d);
                const has = S.slots.has("timer." + d.name);
                tr.innerHTML = `<td class="n">${esc(d.name)}</td><td>${esc([...where.startTimer].join(", ") || "-")}</td><td>${esc([...where.stopTimer].join(", ") || "-")}</td><td>${esc([...where.timerLeft].join(", ") || "-")}</td><td>${has ? `<a href="#" data-open="slot:timer.${esc(d.name)}">slot timer ${esc(d.name)}</a>` : '<span class="warn">no slot timer ' + esc(d.name) + "</span>"}</td>`;
                t.append(tr);
            }
            sec.append(t);
        }
        // ---- objects
        if (S.uses.size) {
            const sec = section("Objects in use", "Native mechanics this mode switches on with <b>use</b>; they are tuned through their params and raise events.");
            for (const d of S.uses.values()) {
                const o = CATALOG.objects[d.object];
                const row = document.createElement("div"); row.className = "row"; row.style.margin = "3px 0"; row.dataset.key = declKey(d);
                row.innerHTML = `<b class="mono" style="min-width:90px">${esc(d.object)}</b><span class="muted">${esc(o ? o.doc : "unknown object")}</span>`
                    + (o ? `<span class="chip">steps: ${esc(o.steps.join(", "))}</span>${o.events.length ? `<span class="chip">events: ${esc(o.events.join(", "))}</span>` : ""}${o.params.length ? `<span class="chip">params: ${esc(o.params.join(", "))}</span>` : ""}` : "");
                sec.append(row);
            }
        }
        // ---- curves
        const curves = B.decls.filter((d) => d.kind === "curve");
        if (curves.length) {
            const sec = section("Curves", "steps / points curves: drag the points, double-click to add, right-click to remove. Expression curves: drag the sliders (one per number in the formula).");
            const grid = document.createElement("div"); grid.className = "curves";
            for (const c of curves) {
                const card = document.createElement("div"); card.className = "curve"; card.dataset.key = declKey(c);
                card.innerHTML = `<div class="cf"><b>${esc(c.name)}</b>(${esc(c.arg)}) <span class="muted">${esc(c.form === "expr" ? "= " + printExpr(c.body) : c.form)}</span></div><div class="muted" style="font-size:11px">${esc(docOf(c))}</div>`;
                const holder = document.createElement("div");
                card.append(holder);
                const tip = document.createElement("div"); tip.className = "muted"; tip.style.cssText = "font:11px var(--mono);min-height:14px";
                const w = new CurveWidget(holder, c, () => this.brain, () => this._changed());
                w.tipEl = tip;
                card.append(tip);
                grid.append(card);
                this.widgets.push(w);
            }
            sec.append(grid);
            requestAnimationFrame(() => this.widgets.forEach((w) => w.draw()));
        }
        R.querySelectorAll("[data-open]").forEach((a) => (a.onclick = (e) => { e.preventDefault(); this.app.openDecl(a.dataset.open, "code"); }));
    }

    _choose(opts, v, on) {
        const s = document.createElement("select");
        for (const o of opts) { const e = document.createElement("option"); e.textContent = o; e.value = o; if (o === v) e.selected = true; s.append(e); }
        s.onchange = () => on(s.value);
        return s;
    }

    _paramRow(d, def) {
        const tr = document.createElement("tr");
        tr.dataset.key = declKey(d);
        const c = catParam(d);
        const builtin = def ? def.value : paramDefault(d.name, this.app.base);
        const mod = builtin !== undefined && builtin !== d.value;
        tr.innerHTML = `<td class="n ${mod ? "mod" : ""}" title="${esc(d.type)}">${esc(d.name)}</td><td class="u">${esc(unitOf(d))}</td>`;
        const td = document.createElement("td");
        if (d.type === "bool") {
            const cb = document.createElement("input"); cb.type = "checkbox"; cb.checked = d.value;
            cb.onchange = () => { d.value = cb.checked; this._changed(); };
            td.append(cb);
        } else {
            const inp = document.createElement("input");
            inp.value = canonNum(d.value, d.type);
            if (mod) inp.classList.add("mod");
            const range = c && typeof c.min === "number" ? [c.min, c.max] : null;
            inp.onchange = () => {
                const x = Number(inp.value);
                if (!Number.isFinite(x) || (d.type === "int" && !Number.isInteger(x))) { inp.value = canonNum(d.value, d.type); return; }
                if (range && (x < range[0] || x > range[1])) this.app.app.toast(`${d.name}: ${x} is outside ${range[0]}..${range[1]} (a warning in Checks)`);
                d.value = roundTo(x, d.type); this._changed();
            };
            inp.onkeydown = (e) => { if (e.key === "ArrowUp" || e.key === "ArrowDown") { e.preventDefault(); const step = d.type === "int" ? 1 : Math.max(0.001, Math.abs(+inp.value || 1) * (e.shiftKey ? 0.1 : 0.01)); inp.value = +(+inp.value + (e.key === "ArrowUp" ? step : -step)).toPrecision(6); inp.onchange(); } };
            td.append(inp);
        }
        tr.append(td);
        const bi = document.createElement("td"); bi.className = "u mono"; bi.textContent = builtin === undefined ? "-" : dataText(builtin, d.type); tr.append(bi);
        const rg = document.createElement("td"); rg.className = "u"; rg.textContent = c && typeof c.min === "number" ? `${c.min}..${c.max}` : ""; if (c && !CATALOG.fromGame) rg.title = "the editor's suggested range (the game's comes with exportmode)"; tr.append(rg);
        const tdb = document.createElement("td");
        if (mod && def) { const rb = document.createElement("button"); rb.className = "bk-mini"; rb.textContent = "↺"; rb.title = "back to the built-in: " + dataText(def.value, d.type); rb.onclick = () => this._reset(d); tdb.append(rb); }
        tr.append(tdb);
        const doc = document.createElement("td"); doc.className = "doc"; doc.textContent = docOf(d); tr.append(doc);
        return tr;
    }
    _reset(d) {
        const o = this.def && this.def.decls.find((x) => declKey(x) === declKey(d));
        if (!o) return;
        const i = this.brain.decls.indexOf(d);
        this.brain.decls[i] = { ...clone(o), id: d.id };
        this._changed();
    }
}
