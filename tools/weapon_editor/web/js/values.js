// Values tab: every weapon x fire mode (alt fires and blast-only rows as rows of their own): damage body / head,
// shots and time to kill body / head, DPS body / head, refire, range, projectile speed and flight time, splash,
// falloff; the edited set against the comparison (the standard, or a saved set) with green / red differences;
// sortable, filterable; charts: DPS and time-to-kill bars, damage vs distance per weapon.

import { barPairs, lines } from "./charts.js";
import { esc, el, fin, fmtT, fmtNum, diffSpan } from "./util.js";

const COLS = [
    // [key, title, group, lowerBetter, fmt]
    ["body", "body", "damage / shot", false], ["head", "head", "damage / shot", false],
    ["stkBody", "body", "shots to kill", true, "int"], ["stkHead", "head", "shots to kill", true, "int"],
    ["ttkBody", "body", "time to kill (s)", true, "t"], ["ttkHead", "head", "time to kill (s)", true, "t"],
    ["dpsBody", "body", "DPS", false], ["dpsHead", "head", "DPS", false],
    ["refire", "refire s", "fire", true], ["range", "range m", "fire", false], ["speed", "speed m/s", "fire", false], ["t20", "to 20 m s", "fire", true, "t"],
    ["splashMax", "max", "splash", false], ["splashRadius", "radius m", "splash", false],
    ["fall20", "x 20 m", "falloff", false], ["fall40", "x 40 m", "falloff", false],
];

export class ValuesView {
    constructor(app, root) {
        this.app = app; this.root = root;
        this.sort = { key: null, asc: true };
        this.filter = ""; this.kind = "all"; this.onlyChanged = false; this.showStd = false;
        this._build();
    }
    _build() {
        const r = this.root;
        r.innerHTML = "";
        const bar = el("div", "vbar");
        bar.innerHTML = `<input id="vFilter" placeholder="filter: weapon, mode…" style="width:200px">
            <span class="filters" id="vKinds"></span>
            <label class="check"><input type="checkbox" id="vOnly"> only changed</label>
            <label class="check"><input type="checkbox" id="vStd"> show the comparison's numbers</label>
            <span class="grow"></span><span class="muted small" id="vNote"></span>`;
        r.append(bar);
        const kinds = ["all", "hitscan", "projectile", "beam", "melee"];
        const kc = bar.querySelector("#vKinds");
        for (const k of kinds) { const c = el("span", "chip" + (k === this.kind ? " on" : ""), k); c.onclick = () => { this.kind = k; kc.querySelectorAll(".chip").forEach((x) => x.classList.toggle("on", x === c)); this._table(); }; kc.append(c); }
        bar.querySelector("#vFilter").oninput = (e) => { this.filter = e.target.value.trim().toLowerCase(); this._table(); };
        bar.querySelector("#vOnly").onchange = (e) => { this.onlyChanged = e.target.checked; this._table(); };
        bar.querySelector("#vStd").onchange = (e) => { this.showStd = e.target.checked; this._table(); };
        this.tableBox = el("div"); r.append(this.tableBox);
        r.append(el("h3", null, "Charts"));
        this.charts = el("div", "cards"); r.append(this.charts);
        r.append(el("h3", null, "Damage per shot vs distance"));
        this.small = el("div", "cards small"); r.append(this.small);
    }
    refresh() { if (!this.app.stats) return; this._table(); this._charts(); }

    _rows() {
        const app = this.app;
        const cmp = new Map((app.cmpStats ? app.cmpStats.rows : []).map((r) => [r.id, r]));
        let rows = app.stats.rows.filter((r) => !r.support).map((r) => ({ r, c: cmp.get(r.id) || null }));
        rows = rows.map((x) => ({ ...x, changed: x.c && COLS.some(([k]) => { const a = x.r[k], b = x.c[k]; return Number.isFinite(a) || Number.isFinite(b) ? !(Math.abs(a - b) <= 1e-6 * Math.max(1, Math.abs(b))) && a !== b : false; }) }));
        if (this.kind !== "all") rows = rows.filter((x) => x.r.kind === this.kind || (this.kind === "projectile" && x.r.kind === "projectile"));
        if (this.filter) rows = rows.filter((x) => (x.r.label + " " + x.r.weapon + " " + x.r.kind).toLowerCase().includes(this.filter));
        if (this.onlyChanged) rows = rows.filter((x) => x.changed);
        if (this.sort.key) {
            const k = this.sort.key, s = this.sort.asc ? 1 : -1;
            const val = (x) => (k === "label" ? x.r.label : Number.isFinite(x.r[k]) ? x.r[k] : Infinity);
            rows.sort((a, b) => { const A = val(a), B = val(b); return (typeof A === "string" ? A.localeCompare(B) : A - B) * s; });
        }
        return rows;
    }
    _table() {
        const app = this.app;
        if (!app.stats) return;
        const rows = this._rows();
        this.root.querySelector("#vNote").textContent = `${app.health} hp target${app.dist ? ", hits at " + app.dist + " m" : ""} · differences vs ${app.cmpLabel} · green: stronger, red: weaker`;
        const groups = [];
        for (const c of COLS) { const g = groups[groups.length - 1]; if (g && g.name === c[2]) g.n++; else groups.push({ name: c[2], n: 1 }); }
        let h = `<table class="vt"><thead><tr><th class="grp"></th>${groups.map((g) => `<th class="grp" colspan="${g.n}">${g.name}</th>`).join("")}<th class="grp"></th></tr>`;
        const sc = (k) => (this.sort.key === k ? ` class="sorted${this.sort.asc ? " asc" : ""}"` : "");
        h += `<tr><th data-k="label"${sc("label")}>weapon · fire mode</th>${COLS.map(([k, t]) => `<th data-k="${k}"${sc(k)}>${t}</th>`).join("")}<th>notes</th></tr></thead><tbody>`;
        const f = (v, kind) => (kind === "int" ? fin(v) : kind === "t" ? (v == null ? "–" : fmtT(v)) : v == null ? "–" : fmtNum(v));
        for (const { r, c, changed } of rows) {
            h += `<tr class="${changed ? "changed" : ""}${r.weapon === app.sel ? " sel" : ""}" data-w="${r.weapon}"><td>${esc(r.label)}</td>`;
            for (const [k, , , lower, kind] of COLS) {
                const a = r[k], b = c ? c[k] : undefined;
                const show = (k === "splashMax" || k === "splashRadius") && !r.splashRadius && !(c && c.splashRadius) ? "" : f(a, kind);
                h += `<td class="n">${show}${c && show !== "" ? diffSpan(a, b, lower) : ""}${this.showStd && c && show !== "" ? `<span class="d muted">${f(b, kind)}</span>` : ""}</td>`;
            }
            h += `<td class="notes">${esc(r.notes.join(" · "))}</td></tr>`;
        }
        h += `</tbody></table>`;
        if (!rows.length) h += `<div class="muted pad">nothing matches</div>`;
        this.tableBox.innerHTML = h;
        this.tableBox.querySelectorAll("th[data-k]").forEach((th) => (th.onclick = () => {
            const k = th.dataset.k;
            if (this.sort.key === k) { if (!this.sort.asc) this.sort = { key: null, asc: true }; else this.sort.asc = false; } else this.sort = { key: k, asc: true };
            this._table();
        }));
        this.tableBox.querySelectorAll("tr[data-w]").forEach((tr) => (tr.ondblclick = () => { app.selectWeapon(tr.dataset.w); app.showPage("weapon"); }));
    }
    _charts() {
        const app = this.app;
        const cmp = new Map((app.cmpStats ? app.cmpStats.rows : []).map((r) => [r.id, r]));
        const rows = app.stats.rows.filter((r) => !r.support && r.variant !== "splash");
        const mk = (title, key, unit, lower, note) => {
            const card = el("div", "card");
            card.innerHTML = `<h4>${title} <span class="legend" style="margin-left:auto"><span><span class="sw" style="background:var(--s-edit)"></span>edited</span><span><span class="sw" style="background:var(--s-std)"></span>${esc(app.cmpLabel)}</span></span></h4>${note ? `<div class="muted small">${note}</div>` : ""}`;
            const cv = el("canvas"); cv.style.height = Math.max(160, rows.length * 22 + 26) + "px";
            card.append(cv);
            this.charts.append(card);
            requestAnimationFrame(() => barPairs(cv, rows.map((r) => ({ label: r.label, a: r[key], b: (cmp.get(r.id) || {})[key] })), { unit, lowerBetter: lower, labelW: 170 }));
        };
        this.charts.innerHTML = "";
        mk("DPS, body", "dpsBody", "hp/s", false, "sustained: damage per shot / refire");
        mk("DPS, head", "dpsHead", "hp/s", false, "the head's damage where the weapon has headshots");
        mk(`Time to kill, body (${app.health} hp)`, "ttkBody", "s", true, "(shots - 1) x refire; ∞ (no bar): can't kill");
        mk(`Time to kill, head (${app.health} hp)`, "ttkHead", "s", true, "");
        // damage vs distance, one small chart per weapon (primary fire; alt fires on the Weapon tab)
        this.small.innerHTML = "";
        const prim = app.stats.rows.filter((r) => r.mode === "primary" && !r.support && r.variant !== "splash");
        for (const r of prim) {
            const c = cmp.get(r.id);
            const card = el("div", "card");
            const cv = el("canvas"); cv.style.height = "120px";
            card.append(cv);
            this.small.append(card);
            const pts = (row) => { const o = []; for (let d = 0; d <= 80; d++) o.push([d, row.per(d, false)]); return o; };
            requestAnimationFrame(() => lines(cv, [{ label: "edited", color: "edit", points: pts(r) }, ...(c ? [{ label: app.cmpLabel, color: "std", dash: true, points: pts(c) }] : [])], { xMax: 80, xUnit: "m", yUnit: "hp", title: r.label }));
        }
    }
}
