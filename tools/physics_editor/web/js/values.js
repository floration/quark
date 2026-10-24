// Values: every movement value, grouped (general, ground, air, dash, super dash, grapple, ladder), each as a slider
// (logarithmic: 1 sits near the middle), the multiplier (x the standard value: what the .qmode stores) and the
// absolute value (standard x multiplier, in its unit). Changed rows are highlighted; reset per value, per group, all.
import { GROUPS, SWITCHES, getVal, setVal, clonePhysics } from "./params.js";
import { fmtNum } from "./qmode.js";

const SL = 1000;
const isLog = (p) => p.min > 0 && p.max / p.min > 4;
const toPos = (p, v) => isLog(p) ? Math.round(SL * Math.log(v / p.min) / Math.log(p.max / p.min)) : Math.round(SL * (v - p.min) / (p.max - p.min));
const fromPos = (p, x) => {
    let v = isLog(p) ? p.min * Math.pow(p.max / p.min, x / SL) : p.min + (p.max - p.min) * x / SL;
    v = Math.abs(v - 1) < 0.025 ? 1 : +v.toFixed(v < 1 ? 3 : 2);   // (snaps to 1: the standard)
    return Math.max(p.min, Math.min(p.max, v));
};
const absFmt = (v) => (Math.abs(v) >= 100 ? v.toFixed(1) : Math.abs(v) >= 10 ? v.toFixed(2) : v.toFixed(3)).replace(/\.?0+$/, "") || "0";

export class ValuesView {
    constructor(app, el) {
        this.app = app; this.el = el; this.rows = new Map();
        this.render();
        app.on("physics", (from) => { if (from !== "values") this.refresh(); });
    }
    render() {
        const app = this.app, cat = app.cat;
        this.el.innerHTML = "";
        const wrap = document.createElement("div"); wrap.id = "values";
        const bar = document.createElement("div"); bar.className = "vbar";
        bar.innerHTML = `<input id="vFilter" placeholder="filter: name, unit, group…" style="width:220px"><label class="check"><input type="checkbox" id="vOnly"> only changed</label>
            <span class="grow"></span><span class="muted small">x = multiplier (what the custom mode stores; 1 = standard) · drag a slider, type either number · <span class="warn">orange</span> = changed</span>`;
        wrap.append(bar);
        const groups = new Map(GROUPS.map((g) => [g, []]));
        for (const p of cat.all) { const g = p.group === "top" ? "general" : p.group; if (!groups.has(g)) groups.set(g, []); groups.get(g).push(p); }
        this.groupEls = new Map();
        for (const [g, list] of groups) {
            if (!list.length && g !== "general") continue;
            const sec = document.createElement("div"); sec.className = "vgroup"; sec.dataset.group = g;
            const h = document.createElement("h3");
            h.innerHTML = `<span>${g}</span><span class="cnt"></span><button title="every value in this group back to standard">reset group</button>`;
            h.querySelector("button").onclick = () => {
                const ph = clonePhysics(app.physics);
                for (const p of list) setVal(ph, p.key, 1);
                if (g === "general") for (const s of SWITCHES) ph[s.key] = true;
                app.setPhysics(ph, "reset");
                this.refresh();
            };
            sec.append(h);
            const t = document.createElement("table"); t.className = "params";
            t.innerHTML = `<tr><th>value</th><th></th><th>x standard</th><th>value</th><th></th><th>standard</th><th></th></tr>`;
            if (g === "general") for (const s of SWITCHES) t.append(this._switchRow(s));
            for (const p of list) t.append(this._row(p));
            sec.append(t);
            wrap.append(sec);
            this.groupEls.set(g, { sec, cnt: h.querySelector(".cnt"), list });
        }
        this.el.append(wrap);
        const filt = bar.querySelector("#vFilter"), only = bar.querySelector("#vOnly");
        const apply = () => {
            const f = filt.value.trim().toLowerCase();
            for (const [key, r] of this.rows) {
                const p = r.p;
                const txt = (p.label + " " + p.key + " " + (p.unit || "") + " " + (p.group || "") + " " + (p.doc || "")).toLowerCase();
                const changed = r.tr.classList.contains("mod");
                r.tr.style.display = (!f || txt.includes(f)) && (!only.checked || changed) ? "" : "none";
            }
        };
        filt.oninput = apply; only.onchange = apply;
        this._apply = apply;
        this.refresh();
    }
    _switchRow(s) {
        const tr = document.createElement("tr");
        tr.innerHTML = `<td class="lab">${s.label}<span class="doc">${s.doc}</span></td><td class="sl" colspan="4"><label class="check"><input type="checkbox"> on</label></td><td class="std">on</td><td><button class="rs" title="reset">↺</button></td>`;
        const cb = tr.querySelector("input");
        cb.onchange = () => { const ph = clonePhysics(this.app.physics); ph[s.key] = cb.checked; this.app.setPhysics(ph, "values"); this._mark(s.key); };
        tr.querySelector(".rs").onclick = () => { const ph = clonePhysics(this.app.physics); ph[s.key] = true; this.app.setPhysics(ph, "values"); this.refresh(); };
        this.rows.set(s.key, { tr, cb, p: { key: s.key, label: s.label, doc: s.doc, group: "general" }, sw: true });
        return tr;
    }
    _row(p) {
        const tr = document.createElement("tr");
        if (p.inactive) tr.classList.add("inactive");
        const doc = p.note || p.doc || "";
        tr.innerHTML = `<td class="lab" title="${p.top ? p.key : "move_" + p.key}">${p.label}<span class="doc"></span></td>
            <td class="sl"><input type="range" min="0" max="${SL}" step="1"></td>
            <td><input type="number" class="mult" step="${p.step || 0.01}" min="${p.min}" max="${p.max}"></td>
            <td><input type="number" class="abs" step="any"></td><td class="u">${p.unit || ""}</td>
            <td class="std">${absFmt(p.base)} ${p.unit || ""}</td><td><button class="rs" title="back to standard">↺</button></td>`;
        tr.querySelector(".doc").textContent = doc + ` (x ${p.min} to ${p.max})`;
        const sl = tr.querySelector("input[type=range]"), mu = tr.querySelector(".mult"), ab = tr.querySelector(".abs");
        const commit = (v, from) => {
            if (!Number.isFinite(v)) return;
            v = Math.max(p.min, Math.min(p.max, v));
            const ph = clonePhysics(this.app.physics);
            setVal(ph, p.key, v);
            this.app.setPhysics(ph, "values");
            this._show(p.key, v, from);
        };
        sl.oninput = () => commit(fromPos(p, +sl.value), "slider");
        mu.oninput = () => { if (mu.value !== "") commit(+mu.value, "mult"); };
        ab.oninput = () => { if (ab.value !== "" && p.base) commit(+ab.value / p.base, "abs"); };
        mu.onblur = ab.onblur = () => this._show(p.key, getVal(this.app.physics, p.key));
        tr.querySelector(".rs").onclick = () => commit(1);
        this.rows.set(p.key, { tr, sl, mu, ab, p });
        return tr;
    }
    _show(key, v, from) {
        const r = this.rows.get(key);
        if (!r || r.sw) return;
        if (from !== "slider") r.sl.value = toPos(r.p, v);
        if (from !== "mult") r.mu.value = +(+v).toPrecision(4);
        if (from !== "abs") r.ab.value = absFmt(r.p.base * v);
        const cs = Array.isArray(r.p.constants) && r.p.constants.length ? r.p.constants : r.p.cpp ? [{ name: r.p.cpp, base: r.p.base }] : [];
        r.tr.title = `${r.p.top ? r.p.key : "move_" + r.p.key}=${fmtNum(v)}` + (r.p.doc ? `\n${r.p.doc}` : "")
            + (cs.length ? "\nscales " + cs.map((c) => `${c.file ? c.file + ": " : ""}${c.name}${c.base != null ? " (" + c.base + ")" : ""}`).join(", ") : "");
        this._mark(key);
    }
    _mark(key) {
        const r = this.rows.get(key);
        const ph = this.app.physics;
        const changed = r.sw ? ph[key] === false : Math.abs(getVal(ph, key) - 1) > 1e-9;
        r.tr.classList.toggle("mod", changed);
        for (const [g, ge] of this.groupEls) {
            let n = ge.list.filter((p) => Math.abs(getVal(ph, p.key) - 1) > 1e-9).length;
            if (g === "general") n += SWITCHES.filter((s) => ph[s.key] === false).length;
            ge.cnt.textContent = n ? `${n} changed` : "";
        }
    }
    refresh() {
        const ph = this.app.physics;
        for (const [key, r] of this.rows) {
            if (r.sw) { r.cb.checked = ph[key] !== false; this._mark(key); }
            else if (document.activeElement !== r.mu && document.activeElement !== r.ab) this._show(key, getVal(ph, key));
        }
    }
}
