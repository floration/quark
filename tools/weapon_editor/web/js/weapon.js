// Weapon tab: the selected weapon's fields in groups (damage, fire, projectile, spread / scope, alt fire, knockback,
// falloff), each a slider + number with its unit, the standard value and the change; its numbers (every fire mode:
// damage, shots / time to kill, DPS, edited vs the comparison) and damage vs distance; the logic that touches it.
// Cards flow as many across as fit (dense on a wide screen).

import { GROUPS, clampField, fmtValue } from "./catalog.js";
import { sameVal, symbols, slotKey } from "./ast.js";
import { lines } from "./charts.js";
import { esc, el, fin, fmtT, fmtNum, diffSpan } from "./util.js";

const SL = 1000;
// the slider's scale: cube-root for wide non-negative ranges (small values get room), else linear
const curved = (f) => f.min >= 0 && f.max - f.min > 20 * Math.max(1e-3, Math.abs(f.default) || 1e-3);
const toPos = (f, v) => { const t = (v - f.min) / (f.max - f.min || 1); return Math.round(SL * (curved(f) ? Math.cbrt(Math.max(0, t)) : t)); };
const fromPos = (f, x) => { let t = x / SL; if (curved(f)) t = t * t * t; let v = f.min + (f.max - f.min) * t; if (f.type === "int") v = Math.round(v); return clampField(f, +v.toPrecision(3)); };

export class WeaponView {
    constructor(app, root) { this.app = app; this.root = root; this.rows = new Map(); this.w = null; }

    refresh(from) {
        const app = this.app;
        if (this.w !== app.sel || from !== "weapon" || !this.rows.size) return this.render();
        this.liveUpdate();
    }
    // a field edit from this tab: update in place (the slider being dragged keeps its focus)
    liveUpdate() {
        if (this.w !== this.app.sel) return this.render();
        for (const [k, r] of this.rows) this._paintRow(k, r);
        this._numbers(); this._chart(); this._head();
    }

    render() {
        const app = this.app, cat = app.cat, w = app.sel, W = cat.byId.get(w) || { id: w, title: w };
        this.w = w; this.rows.clear();
        const root = this.root;
        root.innerHTML = "";
        const head = el("div", "whead");
        head.innerHTML = `<h2>${esc(W.title)}</h2><span class="kind">${esc(W.kind || "")}${W.altFire ? " · alt: " + esc(W.altFire) : ""}</span>
            <label class="small muted">label in this set <input class="label" maxlength="24" placeholder="(the game's own name)" title="the weapon's display name in this set: A-Z, 0-9, spaces (24)"></label>
            <span class="grow"></span><span id="wChanged" class="small"></span>
            <button id="wReset" class="danger" title="every value of this weapon back to the standard">Reset ${esc(W.title)}</button>`;
        const lab = head.querySelector("input.label");
        lab.value = app.labels.get(w) || "";
        lab.onchange = () => { const v = lab.value.toUpperCase().replace(/[^A-Z0-9 ]/g, "").slice(0, 24).trim(); lab.value = v; app.setLabel(w, v); };
        head.querySelector("#wReset").onclick = () => app.resetWeapon(w);
        root.append(head);
        this.headEl = head;
        const grid = el("div", "cards");
        // numbers first: what the edits do
        this.numCard = el("div", "card"); grid.append(this.numCard);
        this.chartCard = el("div", "card"); grid.append(this.chartCard);
        const fields = cat.fieldsOf(w).filter((f) => !f.readOnly);
        for (const [g, title] of GROUPS) {
            const fs = fields.filter((f) => (f.group || "damage") === g);
            if (!fs.length) continue;
            const card = el("div", "card");
            const h = el("h4", null, `${title} <span class="cnt"></span><button title="this group back to the standard">reset</button>`);
            h.querySelector("button").onclick = () => { let ast = app.ast; for (const f of fs) if (!sameVal(app.values.get(f.key), f.default, f.type)) { app.setField(f.key, f.default, "reset"); } };
            card.append(h);
            const t = el("table", "params");
            for (const f of fs) t.append(this._row(f));
            card.append(t);
            card._cnt = h.querySelector(".cnt"); card._fs = fs;
            grid.append(card);
        }
        this.logicCard = el("div", "card"); grid.append(this.logicCard);
        root.append(grid);
        this.grid = grid;
        this._numbers(); this._chart(); this._logic(); this._head();
    }
    _head() {
        const n = [...this.app.changed].filter((k) => k.startsWith(this.w + ".")).length;
        const c = this.headEl && this.headEl.querySelector("#wChanged");
        if (c) { c.textContent = n ? `${n} value${n > 1 ? "s" : ""} changed` : "standard"; c.className = "small " + (n ? "warn" : "muted"); }
        for (const card of this.grid ? this.grid.children : []) if (card._cnt) { const k = card._fs.filter((f) => this.app.changed.has(f.key)).length; card._cnt.textContent = k ? k + " changed" : ""; }
    }
    _row(f) {
        const tr = el("tr");
        const short = f.field || f.key.split(".").slice(1).join(".");
        tr.innerHTML = `<td class="lab" title="${esc(f.key)} ${f.type}${f.min !== undefined ? ` (${f.min}..${f.max})` : ""}">${esc(short)}<span class="doc">${esc(f.doc || "")}</span></td>`;
        const tdS = el("td", "sl"), tdN = el("td"), tdU = el("td", "u", esc(f.unit || "")), tdStd = el("td", "std"), tdD = el("td", "df"), tdR = el("td");
        let inp, sl = null;
        if (f.type === "bool") {
            inp = el("input"); inp.type = "checkbox";
            inp.onchange = () => this.app.setField(f.key, inp.checked);
            tdS.append(inp);
        } else {
            sl = el("input"); sl.type = "range"; sl.min = 0; sl.max = SL; sl.step = 1;
            sl.oninput = () => { const v = fromPos(f, +sl.value); inp.value = fmtValue(v, f.type); this.app.setField(f.key, v); };
            tdS.append(sl);
            inp = el("input"); inp.type = "number"; inp.step = f.type === "int" ? 1 : "any";
            inp.onchange = () => { const x = Number(inp.value); if (!Number.isFinite(x)) { this._paintRow(f.key, this.rows.get(f.key)); return; } const v = clampField(f, x); if (v !== x) this.app.toast(`${f.key}: kept within ${f.min}..${f.max} (the game clamps it)`, 2500); this.app.setField(f.key, v); };
            tdN.append(inp);
        }
        tdStd.textContent = fmtValue(f.default, f.type);
        tdStd.title = "standard";
        const rs = el("button", "rs", "↺"); rs.title = "back to the standard";
        rs.onclick = () => this.app.setField(f.key, f.default, "reset");
        tdR.append(rs);
        tr.append(tdS, tdN, tdU, tdStd, tdD, tdR);
        const r = { tr, f, inp, sl, tdD };
        this.rows.set(f.key, r);
        this._paintRow(f.key, r);
        return tr;
    }
    _paintRow(key, r) {
        const v = this.app.values.get(key), f = r.f;
        const mod = !sameVal(v, f.default, f.type);
        r.tr.classList.toggle("mod", mod);
        r.tr.classList.toggle("clamped", (this.app.values.clamped || []).includes(key));
        if (f.type === "bool") { r.inp.checked = !!v; r.tdD.textContent = ""; return; }
        if (document.activeElement !== r.inp) r.inp.value = fmtValue(v, f.type);
        if (r.sl && document.activeElement !== r.sl) r.sl.value = toPos(f, +v);
        if (!mod) { r.tdD.innerHTML = ""; return; }
        const pct = f.default ? ` (${(+v / f.default * 100).toFixed(0)}%)` : "";
        r.tdD.innerHTML = `<span class="warn">${+v > +f.default ? "+" : ""}${fmtNum(+v - +f.default)}</span><span class="muted">${pct}</span>`;
    }
    _numbers() {
        const app = this.app, w = this.w;
        const rows = app.stats.rows.filter((r) => r.weapon === w);
        const cmp = new Map((app.cmpStats ? app.cmpStats.rows : []).map((r) => [r.id, r]));
        let h = `<h4>Numbers <span class="muted small">vs ${esc(app.cmpLabel)} · ${app.health} hp${app.dist ? " · at " + app.dist + " m" : ""}</span></h4>`;
        h += `<table class="score"><tr><th>fire mode</th><th>body</th><th>head</th><th>shots b / h</th><th>time to kill b / h</th><th>DPS b / h</th></tr>`;
        for (const r of rows) {
            const c = cmp.get(r.id) || {};
            const mode = r.label.split(" · ").slice(1).join(" · ") || "primary";
            if (r.support) { h += `<tr><td>${esc(mode)}</td><td colspan="5" class="muted" style="text-align:left">${esc(r.notes.join("; "))}</td></tr>`; continue; }
            h += `<tr title="${esc(r.notes.join("\n"))}"><td>${esc(mode)}</td><td>${fmtNum(r.body)}${diffSpan(r.body, c.body)}</td><td>${fmtNum(r.head)}${diffSpan(r.head, c.head)}</td>`
                + `<td>${fin(r.stkBody)} / ${fin(r.stkHead)}${diffSpan(r.stkBody, c.stkBody, true)}</td><td>${fmtT(r.ttkBody)} / ${fmtT(r.ttkHead)} s${diffSpan(r.ttkBody, c.ttkBody, true)}</td>`
                + `<td>${fmtNum(r.dpsBody)} / ${fmtNum(r.dpsHead)}${diffSpan(r.dpsBody, c.dpsBody)}</td></tr>`;
        }
        h += `</table>`;
        const notes = [...new Set(rows.flatMap((r) => r.notes))];
        if (notes.length) h += `<div class="muted small" style="margin-top:6px">${notes.map(esc).join(" · ")}</div>`;
        this.numCard.innerHTML = h;
    }
    _chart() {
        const app = this.app, w = this.w;
        const rows = app.stats.rows.filter((r) => r.weapon === w && !r.support && r.variant !== "splash");
        const cmp = new Map((app.cmpStats ? app.cmpStats.rows : []).map((r) => [r.id, r]));
        this.chartCard.innerHTML = `<h4>Damage per shot vs distance <span class="legend" style="margin-left:auto"><span><span class="solid"></span>edited</span><span><span class="dash"></span>${esc(app.cmpLabel)}</span></span></h4>`;
        if (!rows.length) { this.chartCard.append(el("div", "muted small", "no damage")); return; }
        const box = el("div", "cards small");
        this.chartCard.append(box);
        const xMax = 80;
        for (const r of rows.slice(0, 4)) {
            const c = cmp.get(r.id);
            const pts = (row) => { const out = []; for (let d = 0; d <= xMax; d += 1) out.push([d, row ? row.per(d, false) : 0]); return out; };
            const cv = el("canvas"); cv.style.height = "130px";
            box.append(cv);
            requestAnimationFrame(() => lines(cv, [{ label: "edited", color: "edit", points: pts(r) }, ...(c ? [{ label: app.cmpLabel, color: "std", dash: true, points: pts(c) }] : [])],
                { xMax, xUnit: "m", yUnit: "hp", title: r.label.split(" · ").slice(1).join(" · ") || "primary" }));
        }
    }
    _logic() {
        const app = this.app, w = this.w, S = symbols(app.ast);
        const parts = [];
        const own = ["hit", "kill"].map((n) => S.slots.get(n + " " + w)).filter(Boolean);
        const gen = ["hit", "kill"].map((n) => S.slots.get(n)).filter((d) => d && !S.slots.has(d.name + " " + w));
        const curve = S.curves.get(w + ".falloff");
        let h = `<h4>Logic for this weapon</h4>`;
        if (!own.length && !gen.length && !curve) h += `<div class="muted small">none: the game's damage as the fields give it. Add <code>slot hit ${w}:</code> or <code>curve ${w}.falloff(dist f32) = points 0: 1, 30: 0.5</code> on the Code tab (or Blocks / Graph).</div>`;
        for (const d of own) h += `<div class="row"><a href="#" data-k="slot:${esc(slotKey(d))}">slot ${esc(slotKey(d))}</a><span class="muted small">runs instead of the general ${d.name} slot</span></div>`;
        for (const d of gen) h += `<div class="row"><a href="#" data-k="slot:${esc(slotKey(d))}">slot ${esc(slotKey(d))}</a><span class="muted small">every weapon without its own</span></div>`;
        if (curve) h += `<div class="row"><a href="#" data-k="curve:${esc(curve.name)}">curve ${esc(curve.name)}</a><span class="muted small">replaces the falloff fields</span></div>`;
        this.logicCard.innerHTML = h;
        this.logicCard.querySelectorAll("a[data-k]").forEach((a) => (a.onclick = (e) => { e.preventDefault(); app.openDecl(a.dataset.k); }));
    }
}
