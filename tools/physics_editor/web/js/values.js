// Values: every movement value, grouped (general, ground, air, dash, super dash, grapple, ladder). Each setting is
//   fixed     a slider (logarithmic: 1 sits near the middle), the multiplier (what the .qmode stores; 1 = standard)
//             and the absolute value (standard x multiplier, in its unit), or
//   equation  an expression (expr.<key>=, INTERFACE.md v2): other settings, the player's state (p.*), functions, or
//   curve     curve(input, x0, x1, y0, y1, "ease") built visually.
// "Compare with games" adds a column per picked game (references/): its value and multiplier per setting, with its
// source and confidence on hover; "use" takes one value, "load all" every value the game maps (recorded in the set as
// an import). Changed rows are highlighted; reset per value, per group, all.
import { GROUPS, SWITCHES, getVal, setVal, clonePhysics, hasExpr, setExpr } from "./params.js";
import { fmtNum } from "./qmode.js";
import { resolve, parseCurve, formatCurve, check } from "./expr.js";
import { ExprEditor, CurveEditor, fmtV } from "./exprui.js";
import { GamePicker, CONF_DOC, FACTS, factLabel } from "./refs.js";

const SL = 1000;
const isLog = (p) => p.min > 0 && p.max / p.min > 4;
const toPos = (p, v) => isLog(p) ? Math.round(SL * Math.log(v / p.min) / Math.log(p.max / p.min)) : Math.round(SL * (v - p.min) / (p.max - p.min));
const fromPos = (p, x) => {
    let v = isLog(p) ? p.min * Math.pow(p.max / p.min, x / SL) : p.min + (p.max - p.min) * x / SL;
    v = Math.abs(v - 1) < 0.025 ? 1 : +v.toFixed(v < 1 ? 3 : 2);   // (snaps to 1: the standard)
    return Math.max(p.min, Math.min(p.max, v));
};
const absFmt = (v) => (Math.abs(v) >= 100 ? v.toFixed(1) : Math.abs(v) >= 10 ? v.toFixed(2) : v.toFixed(3)).replace(/\.?0+$/, "") || "0";
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const r6 = (v) => +(+v).toPrecision(6);

// the import record (physics.imports: [{id, name, keys}]): a key comes from one game at most
export function markImport(ph, game, keys) {
    const list = (ph.imports || []).map((i) => ({ ...i, keys: i.keys.filter((k) => !keys.includes(k)) }));
    let e = list.find((i) => i.id === game.id);
    if (!e) { e = { id: game.id, name: game.name || game.id, keys: [] }; list.push(e); }
    e.keys = [...e.keys, ...keys.filter((k) => !e.keys.includes(k))];
    ph.imports = list.filter((i) => i.keys.length);
    if (!ph.imports.length) delete ph.imports;
}
export function unimport(ph, key) {
    if (!ph.imports) return;
    ph.imports = ph.imports.map((i) => ({ ...i, keys: i.keys.filter((k) => k !== key) })).filter((i) => i.keys.length);
    if (!ph.imports.length) delete ph.imports;
}

export class ValuesView {
    constructor(app, el) {
        this.app = app; this.el = el; this.rows = new Map(); this.mode = new Map();
        this.render();
        app.on("physics", (from) => { if (from !== "values") { this._modesFromPhysics(); this.refresh(); } else this.refresh(true); });
        app.on("games", () => this.render());
        app.on("exprcheck", () => this._exprStatus());
        app.on("theme", () => this._exprStatus());   // (the plots: drawn in the theme's colours)
    }
    games() { return (this.app.games && this.app.games.list) || []; }
    _modesFromPhysics() {
        const ph = this.app.physics;
        for (const p of this.app.cat.all) {
            if (!hasExpr(ph, p.key)) { this.mode.set(p.key, "fixed"); continue; }
            const cur = this.mode.get(p.key);
            if (cur === "equation" || cur === "curve") { if (cur === "curve" && !parseCurve(ph.expr[p.key], this.app.cat.keySet)) this.mode.set(p.key, "equation"); continue; }
            this.mode.set(p.key, parseCurve(ph.expr[p.key], this.app.cat.keySet) ? "curve" : "equation");
        }
    }
    render() {
        const app = this.app, cat = app.cat;
        const keepScroll = this.wrap ? this.wrap.scrollTop : 0;
        const filt0 = this.wrap ? this.wrap.querySelector("#vFilter").value : "", only0 = this.wrap ? this.wrap.querySelector("#vOnly").checked : false;
        this.el.innerHTML = ""; this.rows = new Map();
        this._modesFromPhysics();
        const wrap = document.createElement("div"); wrap.id = "values"; this.wrap = wrap;
        const bar = document.createElement("div"); bar.className = "vbar";
        bar.innerHTML = `<input id="vFilter" placeholder="filter: name, unit, group…" style="width:220px"><label class="check"><input type="checkbox" id="vOnly"> only changed</label>
            <span id="vGames"></span><span class="grow"></span><span class="muted small">x = multiplier (1 = standard) · <b>fixed</b> / <b>ƒ</b> equation / <b>⌒</b> curve per setting · <span class="warn">orange</span> = changed</span>`;
        wrap.append(bar);
        // one picker for the page's life: a re-render (games loaded, picks changed) moves it into the new bar, so an open popup stays open
        if (!this.picker) this.picker = new GamePicker(bar.querySelector("#vGames"), (app.games && app.games.picks) || [], (ids) => app.setGamePicks(ids));
        else bar.querySelector("#vGames").append(this.picker.btn, this.picker.pop);
        const games = this.games();
        // the groups side by side when there's room (a 2560 px screen: two columns), every table on one set of column
        // widths so the sliders and numbers line up from group to group
        wrap.style.setProperty("--ng", games.length);
        const grid = document.createElement("div"); grid.className = "vgrid";
        const cols = `<colgroup><col class="c-lab"><col class="c-md"><col class="c-sl"><col class="c-num"><col class="c-num"><col class="c-u"><col class="c-std"><col class="c-rs">${games.map(() => `<col class="c-game">`).join("")}</colgroup>`;
        const groups = new Map(GROUPS.map((g) => [g, []]));
        for (const p of cat.all) { const g = p.group === "top" ? "general" : p.group; if (!groups.has(g)) groups.set(g, []); groups.get(g).push(p); }
        this.groupEls = new Map();
        for (const [g, list] of groups) {
            if (!list.length && g !== "general") continue;
            const sec = document.createElement("div"); sec.className = "vgroup"; sec.dataset.group = g;
            const h = document.createElement("h3");
            h.innerHTML = `<span>${g}</span><span class="cnt"></span><button title="every value in this group back to standard (equations too)">reset group</button>`;
            h.querySelector("button").onclick = () => {
                const ph = clonePhysics(app.physics);
                for (const p of list) { setVal(ph, p.key, 1); setExpr(ph, p.key, null); unimport(ph, p.key); this.mode.set(p.key, "fixed"); }
                if (g === "general") for (const s of SWITCHES) { ph[s.key] = true; unimport(ph, s.key); }
                this._restFixed(ph); app.setPhysics(ph, "reset");
                this.refresh();
            };
            sec.append(h);
            const t = document.createElement("table"); t.className = "params";
            t.innerHTML = cols + `<tr><th>value</th><th>as</th><th></th><th>x standard</th><th>value</th><th></th><th>standard</th><th></th>${games.map((gm) => this._gameHead(gm)).join("")}</tr>`;
            t.querySelectorAll("th.gh").forEach((th) => this._gameHeadWire(th));
            if (g === "general") for (const s of SWITCHES) t.append(this._switchRow(s, games));
            for (const p of list) { const [a, b] = this._row(p, games); t.append(a, b); }
            sec.append(t);
            grid.append(sec);
            this.groupEls.set(g, { sec, cnt: h.querySelector(".cnt"), list });
        }
        const none = document.createElement("div"); none.className = "vnone muted"; none.style.display = "none";
        wrap.append(grid, none);
        const facts = games.length ? this._factsSection(games) : null;
        if (facts) wrap.append(facts);
        this.el.append(wrap);
        const filt = bar.querySelector("#vFilter"), only = bar.querySelector("#vOnly");
        filt.value = filt0; only.checked = only0;
        const apply = () => {
            const f = filt.value.trim().toLowerCase();
            const shown = new Map();
            for (const [, r] of this.rows) {
                const p = r.p;
                const txt = (p.label + " " + p.key + " " + (p.unit || "") + " " + (p.group || "") + " " + (p.doc || "")).toLowerCase();
                const changed = r.tr.classList.contains("mod");
                const show = (!f || txt.includes(f)) && (!only.checked || changed);
                r.tr.style.display = show ? "" : "none";
                if (r.xtr) r.xtr.style.display = show && this.mode.get(p.key) !== "fixed" && this.mode.get(p.key) ? "" : "none";
                const g = p.group === "top" || !p.group ? "general" : p.group;
                if (show) shown.set(g, (shown.get(g) || 0) + 1);
            }
            // a group with nothing left to show goes, header and all; nothing at all: say so
            let any = 0;
            for (const [g, ge] of this.groupEls) { const n = shown.get(g) || 0; ge.sec.style.display = n ? "" : "none"; any += n; }
            none.style.display = any ? "none" : "";
            none.textContent = any ? "" : only.checked && !f ? "Nothing changed: every value is at the standard physics." : `Nothing matches '${filt.value.trim()}'${only.checked ? " among the changed values" : ""}.`;
        };
        filt.oninput = apply; only.onchange = apply;
        this._apply = apply;
        this.refresh();
        apply();
        wrap.scrollTop = keepScroll;
    }

    // ---- game columns
    _gameHead(gm) {
        const g = gm.game, n = gm.mapped ? gm.mapped.length : 0;
        const sc = g && g.scale ? (typeof g.scale === "object" ? g.scale.why || JSON.stringify(g.scale) : g.scale) : "";
        const tip = [g && [g.engine, g.year].filter(Boolean).join(" · "), sc && "scale: " + sc, g && g.notes,
            g && g.extras && g.extras.length ? "not in Quark (or different):\n- " + g.extras.join("\n- ") : "",
            gm.clamped && gm.clamped.length ? `outside Quark's range (clamped when applied): ${gm.clamped.map((c) => c.key).join(", ")}` : "", gm.error].filter(Boolean).join("\n\n");
        return `<th class="gh" data-id="${esc(gm.id)}" title="${esc(tip)}">
            <span class="gt">${esc(g ? g.name : gm.id)}</span>${gm.error ? ` <span class="err">✗</span>` : ` <span class="muted">${n} mapped</span>`}
            <button class="gall" title="load every value this game maps into the set (marked as an import; values outside Quark's range are clamped)">load all</button><button class="gx" title="stop comparing with this game">×</button></th>`;
    }
    _gameHeadWire(th) {
        const id = th.dataset.id;
        th.querySelector(".gx").onclick = () => this.app.setGamePicks(this.app.games.picks.filter((x) => x !== id));
        th.querySelector(".gall").onclick = () => this.loadGame(id);
    }
    _gameCell(gm, key) {
        const g = gm.game;
        if (!g) return `<td class="gv muted">${gm.error ? "–" : "…"}</td>`;
        const v = g.values[key];
        if (!v || (v.mult == null && v.toggle == null)) return `<td class="gv na" title="${esc(v && v.note ? v.note : `${g.name} has nothing this setting maps to: applying the game keeps it as it is`)}">no analogue</td>`;
        const word = typeof v.confidence === "string" ? v.confidence : "";
        const tip = [v.raw != null && `the game's own: ${v.raw}`, v.source && "source: " + v.source, v.confidence != null && `confidence: ${v.confidence}${CONF_DOC[word] ? " (" + CONF_DOC[word] + ")" : ""}`, v.note].filter(Boolean).join("\n");
        const dot = v.conf ? `<i class="gconf ${esc(v.conf)}" title="confidence: ${esc(word || v.conf)}"></i>` : "";
        if (key === "dash" || key === "grapple") return `<td class="gv" title="${esc(tip)}"><span class="gval">${v.toggle ? "on" : "off"}</span>${dot}<button class="guse" data-k="${key}" title="use this game's value">use</button></td>`;
        const p = this.app.cat.byKey.get(key), out = !!v.outsideClamp || (p && (v.mult < p.min || v.mult > p.max));
        const used = p ? Math.max(p.min, Math.min(p.max, v.mult)) : v.mult;
        const val = v.value != null && typeof v.value === "number" ? `${absFmt(v.value)} ${esc(v.unit || "")}` : v.value != null ? esc(v.value) : "";
        return `<td class="gv${out ? " gclamp" : ""}" title="${esc(tip + (out ? `\n×${fmtV(v.mult)} is outside what Quark allows${p ? ` (${p.min}..${p.max})` : ""}: using it gives ×${fmtV(used)}` : ""))}">
            <span class="gval">${val}</span> <span class="gmult">×${fmtV(v.mult)}</span>${out ? `<span class="gout" title="outside Quark's range: clamped to ×${fmtV(used)} when applied">⚠</span>` : ""}${dot}<button class="guse" data-k="${key}" title="use this game's value${out ? ` (clamped to ×${fmtV(used)})` : ""}">use</button></td>`;
    }
    // the games' `facts`: numbers with no Quark setting behind them (jump height, time to 90%, stopping distance,
    // walk / sprint speeds...), next to Quark's standard where it has one; for reading, nothing to apply
    _factsSection(games) {
        const keys = [];
        for (const gm of games) if (gm.game) for (const k of Object.keys(gm.game.facts || {})) if (!keys.includes(k)) keys.push(k);
        if (!keys.length) return null;
        const order = Object.keys(FACTS);
        keys.sort((a, b) => (order.includes(a) ? order.indexOf(a) : 99) - (order.includes(b) ? order.indexOf(b) : 99));
        const sec = document.createElement("div"); sec.className = "vgroup vfacts"; sec.dataset.group = "facts";
        sec.innerHTML = `<h3><span>reference facts</span><span class="fnote muted">no Quark setting behind these: read them, nothing to apply</span></h3>`;
        const t = document.createElement("table"); t.className = "params facts";
        let h = `<tr><th>fact</th><th>Quark (standard)</th>${games.map((gm) => `<th class="gh"><span class="gt">${esc(gm.game ? gm.game.name : gm.id)}</span></th>`).join("")}</tr>`;
        for (const k of keys) {
            const F = FACTS[k] || {};
            h += `<tr class="frow" data-k="${esc(k)}"><td class="lab">${esc(factLabel(k))}<span class="doc">${esc(k)}${F.note ? " · " + esc(F.note) : ""}</span></td><td class="std">${F.quark != null ? `${F.quark} ${esc(F.unit || "")}` : `<span class="muted">–</span>`}</td>`;
            for (const gm of games) {
                const f = gm.game && gm.game.facts && gm.game.facts[k];
                if (!f) { h += `<td class="gv na">–</td>`; continue; }
                const word = typeof f.confidence === "string" ? f.confidence : "";
                const tip = [f.source && "source: " + f.source, f.confidence != null && `confidence: ${f.confidence}${CONF_DOC[word] ? " (" + CONF_DOC[word] + ")" : ""}`, f.note].filter(Boolean).join("\n");
                const val = typeof f.value === "number" ? absFmt(f.value) : esc(f.value);
                const rel = typeof f.value === "number" && F.quark ? ` <span class="gmult">×${fmtV(f.value / F.quark)}</span>` : "";
                h += `<td class="gv" title="${esc(tip)}"><span class="gval">${val} ${esc(f.unit || "")}</span>${rel}${f.conf ? `<i class="gconf ${esc(f.conf)}"></i>` : ""}</td>`;
            }
            h += "</tr>";
        }
        t.innerHTML = h;
        sec.append(t);
        return sec;
    }
    _wireGameCells(tr) {
        tr.querySelectorAll(".guse").forEach((b) => {
            const td = b.closest("td"), id = td.dataset.id;
            b.onclick = () => this.useGameValue(id, b.dataset.k);
        });
    }
    useGameValue(id, key) {
        const gm = this.games().find((x) => x.id === id);
        if (!gm || !gm.game) return;
        const v = gm.game.values[key];
        const ph = clonePhysics(this.app.physics);
        if (key === "dash" || key === "grapple") ph[key] = !!v.toggle;
        else {
            const p = this.app.cat.byKey.get(key);
            setVal(ph, key, r6(Math.max(p.min, Math.min(p.max, v.mult))));
            setExpr(ph, key, null); this.mode.set(key, "fixed");
        }
        markImport(ph, gm.game, [key]);
        this._restFixed(ph); this.app.setPhysics(ph, "values");
        this.refresh();
    }
    loadGame(id) {
        const gm = this.games().find((x) => x.id === id);
        if (!gm || !gm.game) return;
        const ph = clonePhysics(this.app.physics);
        const keys = [];
        for (const k of gm.mapped) {
            const v = gm.game.values[k];
            if (k === "dash" || k === "grapple") { ph[k] = !!v.toggle; keys.push(k); continue; }
            const p = this.app.cat.byKey.get(k);
            setVal(ph, k, r6(Math.max(p.min, Math.min(p.max, v.mult))));
            setExpr(ph, k, null); this.mode.set(k, "fixed");
            keys.push(k);
        }
        markImport(ph, gm.game, keys);
        this._restFixed(ph); this.app.setPhysics(ph, "values");
        this.refresh();
        const miss = this.app.cat.all.length - keys.filter((k) => k !== "dash" && k !== "grapple").length;
        // a mechanic the game has off in standard play (Quake III's grapple is a server option): its values still load,
        // for when it's switched back on; said so, since the switch goes off at the same time
        const off = [["dash", /^(super_)?dash_/], ["grapple", /^hook_/]].filter(([k, re]) => ph[k] === false && keys.some((x) => re.test(x))).map(([k]) => k);
        this.app.toast(`${gm.game.name}: ${keys.length} values loaded (marked as an import)${miss ? `; ${miss} settings have no analogue and kept their values` : ""}${gm.clamped && gm.clamped.length ? `; ${gm.clamped.length} clamped to Quark's range` : ""}${off.length ? `; ${off.join(" and ")} switched off as in the game, with its values loaded for when it's on` : ""}`, 6000);
    }

    // ---- rows
    _switchRow(s, games) {
        const tr = document.createElement("tr");
        tr.innerHTML = `<td class="lab">${s.label}<span class="doc">${s.doc}</span></td><td></td><td class="sl" colspan="4"><label class="check"><input type="checkbox"> on</label></td><td class="std">on</td><td><button class="rs" title="reset">↺</button></td>`
            + games.map((gm) => this._gameCell(gm, s.key).replace("<td", `<td data-id="${esc(gm.id)}"`)).join("");
        const cb = tr.querySelector("input");
        cb.onchange = () => { const ph = clonePhysics(this.app.physics); ph[s.key] = cb.checked; unimport(ph, s.key); this.app.setPhysics(ph, "values"); this._mark(s.key); };
        tr.querySelector(".rs").onclick = () => { const ph = clonePhysics(this.app.physics); ph[s.key] = true; unimport(ph, s.key); this.app.setPhysics(ph, "values"); this.refresh(); };
        this._wireGameCells(tr);
        this.rows.set(s.key, { tr, cb, p: { key: s.key, label: s.label, doc: s.doc, group: "general" }, sw: true });
        return tr;
    }
    _row(p, games) {
        const tr = document.createElement("tr");
        tr.className = "prow";
        if (p.inactive) tr.classList.add("inactive");
        const doc = p.note || p.doc || "";
        tr.innerHTML = `<td class="lab" title="${p.top ? p.key : "move_" + p.key}">${p.label}<span class="xb"></span><span class="doc"></span></td>
            <td class="md"><div class="seg" title="fixed value, equation or curve"><button data-m="fixed" title="a fixed multiplier">fixed</button><button data-m="equation" title="an equation: other settings, the player's state (p.*), functions">ƒ</button><button data-m="curve" title="a curve over one input, with bounds and easing">⌒</button></div></td>
            <td class="sl"><input type="range" min="0" max="${SL}" step="1"></td>
            <td><input type="number" class="mult" step="${p.step || 0.01}" min="${p.min}" max="${p.max}"></td>
            <td><input type="number" class="abs" step="any"></td><td class="u">${p.unit || ""}</td>
            <td class="std">${absFmt(p.base)} ${p.unit || ""}</td><td><button class="rs" title="back to standard (a fixed 1)">↺</button></td>`
            + games.map((gm) => this._gameCell(gm, p.key).replace("<td", `<td data-id="${esc(gm.id)}"`)).join("");
        tr.querySelector(".doc").textContent = doc + ` (x ${p.min} to ${p.max})`;
        const xtr = document.createElement("tr"); xtr.className = "xrow"; xtr.style.display = "none";
        xtr.innerHTML = `<td colspan="${8 + games.length}"><div class="xhost"></div></td>`;
        const sl = tr.querySelector("input[type=range]"), mu = tr.querySelector(".mult"), ab = tr.querySelector(".abs");
        const commit = (v, from) => {
            if (!Number.isFinite(v)) return;
            v = Math.max(p.min, Math.min(p.max, v));
            const ph = clonePhysics(this.app.physics);
            setVal(ph, p.key, v);
            setExpr(ph, p.key, null); this.mode.set(p.key, "fixed");
            unimport(ph, p.key);
            this._restFixed(ph);
            this.app.setPhysics(ph, "values", from ? "v:" + p.key : undefined);   // (a drag or typing: one undo step)
            this._show(p.key, v, from);
        };
        sl.oninput = () => commit(fromPos(p, +sl.value), "slider");
        mu.oninput = () => { if (mu.value !== "" && this.mode.get(p.key) === "fixed") commit(+mu.value, "mult"); };
        ab.oninput = () => { if (ab.value !== "" && p.base && this.mode.get(p.key) === "fixed") commit(+ab.value / p.base, "abs"); };
        mu.onblur = ab.onblur = () => this._show(p.key, getVal(this.app.physics, p.key));
        tr.querySelector(".rs").onclick = () => commit(1);
        tr.querySelectorAll(".seg button").forEach((b) => (b.onclick = () => this.setMode(p.key, b.dataset.m)));
        this._wireGameCells(tr);
        this.rows.set(p.key, { tr, xtr, sl, mu, ab, p, host: xtr.querySelector(".xhost") });
        return [tr, xtr];
    }
    // fixed / equation / curve
    setMode(key, m) {
        const r = this.rows.get(key), ph = this.app.physics, cur = getVal(ph, key);
        const had = hasExpr(ph, key) ? ph.expr[key] : null;
        this.mode.set(key, m);
        if (m === "fixed") { if (had != null) this.setExprText(key, null); else this.refresh(); return; }
        if (m === "equation") { this._editor(r, m); this.setExprText(key, had != null ? had : fmtNum(cur)); setTimeout(() => r.ed && r.ed.inp && r.ed.inp.focus(), 0); return; }
        const c = had != null ? parseCurve(had, this.app.cat.keySet) : null;
        this._editor(r, m);
        this.setExprText(key, c ? had : formatCurve(r.ed.defaultCurve(cur)));
    }
    _editor(r, m) {
        if (r.edKind === m && r.ed) return r.ed;
        const key = r.p.key, cat = this.app.cat;
        r.host.innerHTML = "";
        const onText = (t) => this.setExprText(key, t, true);
        r.ed = m === "curve" ? new CurveEditor(r.host, { cat, key, onText }) : new ExprEditor(r.host, { cat, key, onText });
        r.edKind = m;
        return r.ed;
    }
    // an expression typed / built for key (null: back to a fixed value): stored with its value at rest as the fixed line
    setExprText(key, text, fromEditor) {
        const ph = clonePhysics(this.app.physics);
        setExpr(ph, key, text);
        this._restFixed(ph);
        unimport(ph, key);
        this.fromEditor = fromEditor ? key : null;
        this.app.setPhysics(ph, "values", fromEditor ? "x:" + key : undefined);
        this.fromEditor = null;
    }
    // every expression's fixed line = its value standing still (what an older game reads); after any change, since
    // an expression may read the setting that changed (friction=sqrt(speed): moving speed moves friction's line)
    _restFixed(ph) {
        if (!ph.expr || !Object.keys(ph.expr).length) return;
        const r = resolve(ph, this.app.cat);
        for (const k of r.order) if (hasExpr(ph, k) && Number.isFinite(r.S[k])) setVal(ph, k, r6(r.S[k]));
    }
    _show(key, v, from) {
        const r = this.rows.get(key);
        if (!r || r.sw) return;
        if (from !== "slider") r.sl.value = toPos(r.p, v);
        if (from !== "mult") r.mu.value = +(+v).toPrecision(4);
        if (from !== "abs") r.ab.value = absFmt(r.p.base * v);
        const cs = Array.isArray(r.p.constants) && r.p.constants.length ? r.p.constants : r.p.cpp ? [{ name: r.p.cpp, base: r.p.base }] : [];
        const ex = hasExpr(this.app.physics, key) ? this.app.physics.expr[key] : null;
        r.tr.title = `${r.p.top ? r.p.key : "move_" + r.p.key}=${fmtNum(v)}` + (ex ? `\nexpr.${key}=${ex}` : "") + (r.p.doc ? `\n${r.p.doc}` : "")
            + (cs.length ? "\nscales " + cs.map((c) => `${c.file ? c.file + ": " : ""}${c.name}${c.base != null ? " (" + c.base + ")" : ""}`).join(", ") : "");
        this._mark(key);
    }
    _mark(key) {
        const r = this.rows.get(key);
        const ph = this.app.physics;
        const changed = r.sw ? ph[key] === false : Math.abs(getVal(ph, key) - 1) > 1e-9 || hasExpr(ph, key);
        r.tr.classList.toggle("mod", changed);
        const imp = (ph.imports || []).find((i) => i.keys.includes(key));
        r.tr.classList.toggle("imp", !!imp);
        r.tr.querySelector(".lab").dataset.imp = imp ? `imported from ${imp.name}` : "";
        for (const [g, ge] of this.groupEls) {
            let n = ge.list.filter((p) => Math.abs(getVal(ph, p.key) - 1) > 1e-9 || hasExpr(ph, p.key)).length;
            if (g === "general") n += SWITCHES.filter((s) => ph[s.key] === false).length;
            ge.cnt.textContent = n ? `${n} changed` : "";
        }
    }
    refresh(fromValues) {
        const ph = this.app.physics;
        this.res = resolve(ph, this.app.cat);
        for (const [key, r] of this.rows) {
            if (r.sw) { r.cb.checked = ph[key] !== false; this._mark(key); continue; }
            const m = this.mode.get(key) || "fixed";
            r.tr.querySelectorAll(".seg button").forEach((b) => b.classList.toggle("on", b.dataset.m === m));
            const isX = m !== "fixed";
            r.tr.classList.toggle("xmode", isX);
            r.sl.disabled = isX; r.mu.readOnly = isX; r.ab.readOnly = isX;
            const info = this.res.info[key];
            const xb = r.tr.querySelector(".xb");
            if (isX) {
                xb.innerHTML = info && info.ok ? `<span class="xbadge ${info.dynamic ? "dyn" : "sta"}">${info.dynamic ? "dynamic" : "static"}</span>` : `<span class="xbadge bad">error</span>`;
                if (r.xtr.style.display === "none" && r.tr.style.display !== "none") r.xtr.style.display = "";
                const ed = this._editor(r, m);
                if (this.fromEditor !== key) ed.setText(hasExpr(ph, key) ? ph.expr[key] : "");
                if (ed.status) ed.status(info, ph, this._helperValue(key));
            } else {
                xb.innerHTML = "";
                r.xtr.style.display = "none";
                if (r.ed) { r.host.innerHTML = ""; r.ed = null; r.edKind = null; }
            }
            if (fromValues && !isX && (document.activeElement === r.mu || document.activeElement === r.ab || document.activeElement === r.sl)) { this._mark(key); continue; }
            if (document.activeElement !== r.mu && document.activeElement !== r.ab) this._show(key, info && info.ok ? info.value : getVal(ph, key));
            else this._mark(key);
        }
        if (this._apply) this._apply();
    }
    // the game's own evaluator's value (physexpr), when the helper has it and answered for this set
    _helperValue(key) {
        const hc = this.app.exprCheck;
        if (!hc || !hc.items) return null;
        const it = hc.items.find((i) => i.key === key);
        return it && it.ok && Number.isFinite(it.value) ? it.value : null;
    }
    _exprStatus() {
        for (const [key, r] of this.rows) if (r.ed && r.ed.status && this.res) r.ed.status(this.res.info[key], this.app.physics, this._helperValue(key));
    }
}
