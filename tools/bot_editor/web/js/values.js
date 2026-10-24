// Values panel: every param with its four per-level values and unit, shares / chances as %, every curve as an
// editable plot, the dice rolls of each slot as odds per level, weapon lists and tables. Edits go back into the AST.

import { symbols, decisionOdds, evalExpr, paramValue, clone, declKey, slotKey, WEAPONS } from "./ast.js";
import { printExpr, canonNum, roundTo, LEVELS } from "./dsl.js";
import { CurveWidget, LEVEL_COLORS, LEVEL_NAMES } from "./curves.js";

const esc = (x) => String(x ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
export function unitOf(d) { const m = /\(([^()]*)\)\s*$/.exec(d.trail || ""); return m ? m[1] : ""; }
export function docOf(d) { return (d.trail || "").replace(/^#\s?/, "").replace(/\s*\([^()]*\)\s*$/, ""); }
// shown as a percentage: shares, chances, fractions, qualities in 0..1
function pctKind(d) {
    const doc = (d.trail || "").toLowerCase();
    if (/per second|per s\b|times a second|\/s\b/.test(doc) || /rate$/i.test(d.name) && !/turn/i.test(d.name)) return "rate";
    if (/share|chance|fraction|quality|probab/.test(doc) || /chance/i.test(d.name)) return "share";
    return null;
}
function pctText(kind, v) {
    if (kind === "share" && v >= 0 && v <= 1) return Math.round(v * 1000) / 10 + "%";
    if (kind === "rate" && v >= 0) return v === 0 ? "never" : `${Math.round((1 - Math.exp(-v)) * 1000) / 10}%/s`;
    return "";
}

export class ValuesView {
    constructor(root, app) { this.root = root; this.app = app; this.widgets = []; }

    setBrain(brain, def) {
        this.brain = clone(brain);
        this.def = def;
        this.render();
    }
    _changed() { this.app.brainEdited(clone(this.brain), "values"); }
    scrollTo(key) { const e = this.root.querySelector(`[data-key="${CSS.escape(key)}"]`); if (e) { e.scrollIntoView({ block: "center" }); e.classList.add("flash"); setTimeout(() => e.classList.remove("flash"), 900); } }

    render() {
        const R = this.root, B = this.brain;
        const defS = this.def ? symbols(this.def) : null;
        R.textContent = "";
        // ---- params, grouped by the file's section comments
        const groups = [];
        let cur = null;
        for (const d of B.decls) {
            const heading = (d.comments || []).filter((c) => c && c.startsWith("#")).pop();
            if (d.kind !== "param") { if (heading) cur = null; continue; }
            if (heading || !cur) { cur = { title: heading ? heading.replace(/^#\s*/, "") : "params", items: [] }; groups.push(cur); }
            cur.items.push(d);
        }
        const key = document.createElement("div");
        key.className = "lvl-key";
        key.innerHTML = LEVEL_NAMES.map((n, i) => `<span><i style="background:${LEVEL_COLORS[i]}"></i>${n}</span>`).join("") + `<span class="muted">· edited values are outlined; hover one for the default</span>`;
        R.append(key);
        for (const g of groups) {
            const sec = document.createElement("div"); sec.className = "vgroup";
            sec.innerHTML = `<h3>${esc(g.title)}</h3>`;
            const t = document.createElement("table"); t.className = "params";
            t.innerHTML = `<tr><th>param</th><th>unit</th>${LEVELS.map((l, i) => `<th style="color:${LEVEL_COLORS[i]}">${l.slice(0, 3).toLowerCase()}</th>`).join("")}<th></th><th>what it does</th></tr>`;
            for (const d of g.items) t.append(this._paramRow(d, defS && defS.params.get(d.name)));
            sec.append(t);
            R.append(sec);
        }
        // ---- odds
        const odds = decisionOdds(B);
        if (odds.length) {
            const sec = document.createElement("div"); sec.className = "vgroup";
            sec.innerHTML = `<h3>Dice: how often each choice happens</h3><div class="muted" style="font-size:12px;margin-bottom:4px">roll() &lt; X is true X of the time; chance(rate) fires rate times a second on average (shown as the chance within one second). Edit the numbers in the params above or in the slot.</div>`;
            const t = document.createElement("table"); t.className = "odds";
            t.innerHTML = `<tr><th>slot</th><th>test</th>${LEVELS.map((l, i) => `<th style="color:${LEVEL_COLORS[i]}">${l.slice(0, 3).toLowerCase()}</th>`).join("")}</tr>`;
            for (const o of odds) {
                const tr = document.createElement("tr");
                let cells = "";
                for (let L = 0; L < 4; L++) {
                    const v = Number(evalExpr(o.expr, B, { level: L }));
                    cells += `<td class="p">${!Number.isFinite(v) ? "?" : o.kind === "roll" ? Math.round(Math.max(0, Math.min(1, v)) * 1000) / 10 + "%" : v <= 0 ? "never" : Math.round((1 - Math.exp(-v)) * 1000) / 10 + "%/s"}</td>`;
                }
                tr.innerHTML = `<td><a href="#" data-open="slot:${esc(o.slot)}">${esc(o.slot)}</a></td><td class="n" style="font-family:var(--mono)">${esc(o.kind === "roll" ? "roll() < " + printExpr(o.expr) : "chance(" + printExpr(o.expr) + ")")}</td>${cells}`;
                t.append(tr);
            }
            sec.append(t);
            R.append(sec);
        }
        // ---- curves
        const curves = B.decls.filter((d) => d.kind === "curve");
        if (curves.length) {
            const sec = document.createElement("div"); sec.className = "vgroup";
            sec.innerHTML = `<h3>Curves</h3><div class="muted" style="font-size:12px;margin-bottom:6px">steps / points curves: drag the points, double-click to add, right-click to remove. Expression curves: drag the sliders (one per number in the formula).</div>`;
            const grid = document.createElement("div"); grid.className = "curves";
            for (const c of curves) {
                const card = document.createElement("div"); card.className = "curve"; card.dataset.key = declKey(c);
                const mod = defS && defS.curves.get(c.name) && JSON.stringify(stripMeta(defS.curves.get(c.name))) !== JSON.stringify(stripMeta(c));
                card.innerHTML = `<div class="cf"><b>${esc(c.name)}</b>(${esc(c.arg)}) ${mod ? '<span class="warn">edited</span>' : ""} <span class="muted">${esc(c.form === "expr" ? "= " + printExpr(c.body) : c.form)}</span></div><div class="muted" style="font-size:11px">${esc(docOf(c))}</div>`;
                const holder = document.createElement("div");
                card.append(holder);
                const tip = document.createElement("div"); tip.className = "muted"; tip.style.cssText = "font:11px var(--mono);min-height:14px";
                const w = new CurveWidget(holder, c, () => this.brain, () => this._changed());
                w.tipEl = tip;
                card.append(tip);
                if (mod) { const rb = document.createElement("button"); rb.textContent = "reset to default"; rb.onclick = () => this._reset(c); card.append(rb); }
                grid.append(card);
                this.widgets.push(w);
            }
            sec.append(grid);
            R.append(sec);
            requestAnimationFrame(() => this.widgets.forEach((w) => w.draw()));
        }
        // ---- lists and tables
        const lists = B.decls.filter((d) => d.kind === "list"), tables = B.decls.filter((d) => d.kind === "table");
        if (lists.length || tables.length) {
            const sec = document.createElement("div"); sec.className = "vgroup";
            sec.innerHTML = `<h3>Weapon lists (best first) and tables</h3>`;
            for (const l of lists) {
                const row = document.createElement("div"); row.className = "row"; row.style.margin = "3px 0"; row.dataset.key = declKey(l);
                row.innerHTML = `<b style="font-family:var(--mono);min-width:110px">${esc(l.name)}</b>`;
                l.items.forEach((w, i) => {
                    const chip = document.createElement("span"); chip.className = "chip"; chip.style.cursor = "pointer";
                    chip.textContent = w; chip.title = i ? "click: move earlier (preferred)" : "first choice";
                    chip.onclick = () => { if (!i) return; [l.items[i - 1], l.items[i]] = [l.items[i], l.items[i - 1]]; this._changed(); };
                    row.append(chip);
                });
                const doc = document.createElement("span"); doc.className = "muted"; doc.style.fontSize = "11px"; doc.textContent = docOf(l); row.append(doc);
                sec.append(row);
            }
            for (const tb of tables) {
                const row = document.createElement("div"); row.className = "row"; row.style.margin = "6px 0"; row.dataset.key = declKey(tb);
                row.innerHTML = `<b style="font-family:var(--mono);min-width:110px">${esc(tb.name)}</b>`;
                const add = (label, get, set) => { const s = document.createElement("label"); s.className = "chip"; s.innerHTML = `${esc(label)} `; const i = document.createElement("input"); i.style.width = "48px"; i.value = canonNum(get(), "f32"); i.onchange = () => { if (Number.isFinite(+i.value)) { set(roundTo(+i.value, "f32")); this._changed(); } }; s.append(i); row.append(s); };
                tb.entries.forEach((en) => add(en[0], () => en[1], (v) => (en[1] = v)));
                if (tb.else !== undefined) add("else", () => tb.else, (v) => (tb.else = v));
                const doc = document.createElement("span"); doc.className = "muted"; doc.style.fontSize = "11px"; doc.textContent = unitOf(tb) || docOf(tb); row.append(doc);
                sec.append(row);
            }
            R.append(sec);
        }
        R.querySelectorAll("[data-open]").forEach((a) => (a.onclick = (e) => { e.preventDefault(); this.app.brain.openDecl(a.dataset.open, "code"); }));
    }

    _paramRow(d, def) {
        const tr = document.createElement("tr");
        tr.dataset.key = declKey(d);
        const mod = def && JSON.stringify(def.values) !== JSON.stringify(d.values);
        const kind = pctKind(d);
        tr.innerHTML = `<td class="n ${mod ? "mod" : ""}" title="${esc(d.type)}">${esc(d.name)}</td><td class="u">${esc(unitOf(d))}</td>`;
        const four = d.values.length === 4;
        for (let L = 0; L < 4; L++) {
            const td = document.createElement("td");
            if (!four && L > 0) { tr.lastChild.colSpan = 4; break; }
            const v = paramValue(d, L), dv = def ? paramValue(def, L) : undefined;
            if (d.type === "bool") {
                const cb = document.createElement("input"); cb.type = "checkbox"; cb.checked = v;
                cb.onchange = () => { this._set(d, L, cb.checked); };
                if (def && dv !== v) cb.style.outline = "2px solid var(--warn)";
                td.append(cb);
            } else {
                const inp = document.createElement("input");
                inp.value = canonNum(v, d.type);
                if (def && dv !== v) { inp.classList.add("mod"); inp.title = "default: " + canonNum(dv, d.type); }
                inp.onchange = () => { const x = Number(inp.value); if (!Number.isFinite(x) || (d.type === "int" && !Number.isInteger(x))) { inp.value = canonNum(v, d.type); return; } this._set(d, L, roundTo(x, d.type)); };
                inp.onkeydown = (e) => { if (e.key === "ArrowUp" || e.key === "ArrowDown") { e.preventDefault(); const step = d.type === "int" ? 1 : Math.max(0.001, Math.abs(+inp.value || 1) * (e.shiftKey ? 0.1 : 0.01)); inp.value = +(+inp.value + (e.key === "ArrowUp" ? step : -step)).toPrecision(6); inp.onchange(); } };
                td.append(inp);
                const p = pctText(kind, v);
                if (p) { const s = document.createElement("span"); s.className = "pct"; s.textContent = p; td.append(s); }
            }
            tr.append(td);
        }
        const tdb = document.createElement("td");
        const sp = document.createElement("button"); sp.className = "bk-mini";
        sp.textContent = four ? "=" : "⇹"; sp.title = four ? "one value for every level (the beginner's)" : "a value per level";
        sp.onclick = () => { d.values = four ? [d.values[0]] : [d.values[0], d.values[0], d.values[0], d.values[0]]; this._changed(); };
        tdb.append(sp);
        if (mod) { const rb = document.createElement("button"); rb.className = "bk-mini"; rb.textContent = "↺"; rb.title = "back to the default: " + def.values.map((x) => (d.type === "bool" ? (x ? "yes" : "no") : canonNum(x, d.type))).join(" "); rb.onclick = () => this._reset(d); tdb.append(rb); }
        tr.append(tdb);
        const doc = document.createElement("td"); doc.className = "doc"; doc.textContent = docOf(d); tr.append(doc);
        return tr;
    }
    _set(d, L, v) {
        if (d.values.length === 1) d.values = [v];
        else { d.values[L] = v; if (d.values.every((x) => x === d.values[0])) d.values = [d.values[0]]; }
        this._changed();
    }
    _reset(d) {
        const o = this.def && this.def.decls.find((x) => declKey(x) === declKey(d));
        if (!o) return;
        const i = this.brain.decls.indexOf(d);
        this.brain.decls[i] = { ...clone(o), id: d.id };
        this._changed();
    }
}
function stripMeta(d) { const c = clone(d); delete c.id; delete c.comments; delete c.trail; return c; }
