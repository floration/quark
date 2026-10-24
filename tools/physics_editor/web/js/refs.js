// Other games' movement values (tools/physics_editor/references/: index.json + <game>.json, served by serve.py at
// /references/). A game file: {id, name, year, engine, scale, values: {<quark key>: {value, unit, mult, source,
// confidence, note?}}, extras, notes, sources}; `mult` is the game's value as a Quark multiplier (1 = Quark's standard).
// The Values tab shows picked games as columns; a game can be applied (one value or all of them) and compared as a
// physics set (only the settings it maps; the rest stay at Quark's standard).
import { standardPhysics, setVal } from "./params.js";

const LS = "qpe.games";
let indexP = null;
const games = new Map();   // id -> Promise<game>

// index.json, normalised to {groups: [{name, games: [{id, name, year, engine, ...}]}], all: [games]}; the shapes
// accepted: {groups: [{name|label, games|items: [id | {id, ...}]}]}, {games: [{id, group|family|series|category}]},
// a plain array, or {<group>: [games]}
export function normIndex(raw) {
    const groups = [];
    const byId = new Map();
    const game = (g) => {
        const o = typeof g === "string" ? { id: g } : { ...g };
        if (!o.id) return null;
        if (!o.name) o.name = o.title || o.id;
        if (byId.has(o.id)) return Object.assign(byId.get(o.id), o);
        byId.set(o.id, o);
        return o;
    };
    const flat = Array.isArray(raw) ? raw : raw && Array.isArray(raw.games) ? raw.games : null;
    if (raw && Array.isArray(raw.groups)) {
        for (const gr of raw.groups) {
            const list = (gr.games || gr.items || gr.ids || []).map(game).filter(Boolean);
            groups.push({ name: gr.name || gr.label || gr.title || gr.id || "games", desc: gr.desc || gr.description || "", games: list });
        }
        if (flat) for (const g of flat) game(g);   // (details for ids listed in groups)
    } else if (flat) {
        const m = new Map();
        for (const g0 of flat) {
            const g = game(g0); if (!g) continue;
            const gn = g.group || g.family || g.series || g.category || "games";
            if (!m.has(gn)) m.set(gn, []);
            m.get(gn).push(g);
        }
        for (const [name, list] of m) groups.push({ name, games: list });
    } else if (raw && typeof raw === "object") {
        for (const [name, list] of Object.entries(raw)) if (Array.isArray(list)) groups.push({ name, games: list.map(game).filter(Boolean) });
    }
    return { groups: groups.filter((g) => g.games.length), all: [...byId.values()], raw };
}

export async function loadIndex(force) {
    if (!indexP || force) {
        indexP = fetch("/references/index.json", { cache: "no-store" })
            .then((r) => (r.ok ? r.json() : null))
            .then((j) => (j ? normIndex(j) : { groups: [], all: [], missing: true }))
            .catch(() => ({ groups: [], all: [], missing: true }));
    }
    return indexP;
}
export async function loadGame(id) {
    if (!games.has(id)) {
        games.set(id, (async () => {
            const idx = await loadIndex();
            const ent = idx.all.find((g) => g.id === id) || { id };
            const file = ent.file || ent.path || `${id}.json`;
            const r = await fetch("/references/" + file.replace(/^\/+/, ""), { cache: "no-store" });
            if (!r.ok) throw new Error(`references/${file}: ${r.status}`);
            const g = await r.json();
            return normGame({ ...ent, ...g, id: g.id || id });
        })());
        games.get(id).catch(() => games.delete(id));
    }
    return games.get(id);
}
// confidence: references/ uses exact (the game's code / config) | measured (a wiki, a measurement) | estimated
// (derived here); older or other files may give high / medium / low or a number. -> the dot's class
export const CONF_CLASS = { exact: "high", measured: "medium", estimated: "low", high: "high", medium: "medium", low: "low" };
export function confClass(c) {
    if (typeof c === "string") return CONF_CLASS[c.toLowerCase()] || "";
    if (typeof c === "number") return c >= 0.75 ? "high" : c >= 0.45 ? "medium" : "low";
    return "";
}
// what each confidence word means (index.json's `confidence`, else these)
export const CONF_DOC = {
    exact: "read from source code, a shipped config or a cvar default (converted by the stated scale)",
    measured: "a community wiki / measurement, not the code",
    estimated: "derived (an equivalence between different formulas, a v^2/2g, a simulation, an assumed scale)",
};
export async function confDoc(word) { const i = await loadIndex(); return (i.raw && i.raw.confidence && i.raw.confidence[word]) || CONF_DOC[word] || ""; }

// `facts`: comparisons with no Quark setting behind them, with Quark's standard number where there is one
// (references/README.md: jump height 1.22 m, 0.072 s to 90% of run speed, 1.21 m / 0.364 s to stop)
export const FACTS = {
    jumpHeight: { label: "jump height", quark: 1.22, unit: "m" },
    timeTo90: { label: "time to 90% of run speed", quark: 0.072, unit: "s" },
    stopDistance: { label: "stopping distance from run speed", quark: 1.21, unit: "m" },
    stopTime: { label: "stopping time from run speed", quark: 0.364, unit: "s" },
    runSpeed: { label: "run speed", quark: 9.5, unit: "m/s" },
    walkSpeed: { label: "walk speed" }, sprintSpeed: { label: "sprint speed" }, crouchSpeed: { label: "crouch speed", note: "Quark has no crouch" },
};
export function factLabel(k) { return (FACTS[k] && FACTS[k].label) || k.replace(/([a-z0-9])([A-Z])/g, "$1 $2").toLowerCase(); }

// values keyed by Quark's setting keys (move_friction -> friction), each with a numeric mult where there is one
function normGame(g) {
    const values = {};
    for (const [k0, v0] of Object.entries(g.values || {})) {
        const k = k0.startsWith("move_") ? k0.slice(5) : k0;
        const v = v0 && typeof v0 === "object" ? { ...v0 } : { value: v0 };
        if (typeof v.mult === "string") v.mult = parseFloat(v.mult);
        v.conf = confClass(v.confidence);
        if (k === "dash" || k === "grapple") { v.toggle = v.value === false || v.value === 0 || v.mult === 0 ? false : v.value === true || v.value === 1 || v.mult === 1 ? true : null; }
        values[k] = v;
    }
    const facts = {};
    for (const [k, f0] of Object.entries(g.facts || {})) {
        const f = f0 && typeof f0 === "object" ? { ...f0 } : { value: f0 };
        f.conf = confClass(f.confidence);
        facts[k] = f;
    }
    return { ...g, values, facts, extras: Array.isArray(g.extras) ? g.extras : [] };
}

// the game as a physics set: each mapped setting at the game's multiplier, clamped to what the game (Quark) allows;
// the rest at the standard. -> {physics, mapped: [keys], clamped: [{key, mult, used}], missing: [keys]}
export function gamePhysics(g, cat) {
    const ph = standardPhysics(cat);
    const mapped = [], clamped = [], missing = [];
    for (const p of cat.all) {
        const v = g.values[p.key];
        if (!v || !Number.isFinite(v.mult)) { missing.push(p.key); continue; }
        const used = Math.max(p.min, Math.min(p.max, v.mult));
        if (Math.abs(used - v.mult) > 1e-9) clamped.push({ key: p.key, mult: v.mult, used });
        setVal(ph, p.key, +used.toPrecision(6));
        mapped.push(p.key);
    }
    for (const k of ["dash", "grapple"]) { const v = g.values[k]; if (v && v.toggle != null) { ph[k] = v.toggle; mapped.push(k); } }
    return { physics: ph, mapped, clamped, missing };
}

export function loadPicks() { try { const v = JSON.parse(localStorage.getItem(LS) || "[]"); return Array.isArray(v) ? v.filter((x) => typeof x === "string") : []; } catch { return []; } }
export function savePicks(ids) { try { localStorage.setItem(LS, JSON.stringify(ids)); } catch {} }

const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

// The picker: a button that opens a searchable, grouped multi-select. onChange(ids) after every tick.
export class GamePicker {
    constructor(host, picks, onChange) {
        this.picks = [...picks]; this.onChange = onChange;
        this.btn = document.createElement("button");
        this.btn.className = "gpick-btn";
        this.btn.title = "show other games' default movement values next to Quark's (references/)";
        this.pop = document.createElement("div");
        this.pop.className = "gpick";
        this.pop.style.display = "none";
        host.append(this.btn, this.pop);
        this.btn.onclick = (e) => { e.stopPropagation(); this.toggle(); };
        document.addEventListener("pointerdown", (e) => { if (this.pop.style.display !== "none" && !this.pop.contains(e.target) && e.target !== this.btn) this.toggle(false); });
        this._label();
    }
    _label() { this.btn.innerHTML = `Compare with games${this.picks.length ? ` <b>(${this.picks.length})</b>` : ""} ▾`; }
    async toggle(on) {
        const show = on ?? this.pop.style.display === "none";
        this.pop.style.display = show ? "" : "none";
        if (!show) return;
        const idx = await loadIndex();
        this.render(idx);
        const s = this.pop.querySelector("input.gsearch"); if (s) s.focus();
    }
    render(idx) {
        if (idx.missing || !idx.groups.length) {
            this.pop.innerHTML = `<div class="muted small" style="padding:8px">No reference data: references/index.json isn't there yet.</div>`;
            return;
        }
        const hasDist = idx.all.some((g) => Number.isFinite(g.distance));
        this.pop.innerHTML = `<div class="ghead"><input class="gsearch" placeholder="search: name, engine, year…">${hasDist ? `<span class="seg gsort"><button data-s="group" title="grouped by engine family">groups</button><button data-s="dist" title="closest to Quark's standard first (index.json's distance over the settings both have)">closest</button></span>` : ""}<button class="gnone" title="untick every game">none</button></div><div class="glist"></div>`;
        const list = this.pop.querySelector(".glist"), search = this.pop.querySelector(".gsearch");
        let sort = "group";
        try { sort = localStorage.getItem(LS + ".sort") || "group"; } catch {}
        if (!hasDist) sort = "group";
        const item = (g, gr) => {
            const meta = [g.engine, g.year, Number.isFinite(g.values) ? g.values + " values" : "", Number.isFinite(g.distance) ? `distance ${g.distance} from Quark's standard${g.distanceKeys ? " over " + g.distanceKeys + " settings" : ""}` : ""].filter(Boolean).join(" · ");
            return `<label class="check" title="${esc(meta)}"><input type="checkbox" data-id="${esc(g.id)}" ${this.picks.includes(g.id) ? "checked" : ""}>${esc(g.name)}${g.year ? ` <span class="muted">${esc(g.year)}</span>` : ""}`
                + `<span class="gmeta muted">${sort === "dist" && gr ? esc(gr) + " · " : ""}${Number.isFinite(g.values) ? g.values + " values" : ""}${Number.isFinite(g.distance) ? ` · <span class="gdist" style="--d:${Math.min(1, g.distance)}">${(+g.distance).toFixed(2)}</span>` : ""}</span></label>`;
        };
        const draw = () => {
            const f = search.value.trim().toLowerCase();
            const match = (g, gr) => !f || `${g.name} ${g.id} ${g.engine || ""} ${g.year || ""} ${gr}`.toLowerCase().includes(f);
            this.pop.querySelectorAll(".gsort button").forEach((b) => b.classList.toggle("on", b.dataset.s === sort));
            let h = "";
            if (sort === "dist") {
                const all = [];
                for (const gr of idx.groups) for (const g of gr.games) if (match(g, gr.name)) all.push([g, gr.name]);
                all.sort((a, b) => (Number.isFinite(a[0].distance) ? a[0].distance : 1e9) - (Number.isFinite(b[0].distance) ? b[0].distance : 1e9));
                h += `<div class="ggroup"><div class="gname">closest to Quark first <span class="muted">(0 = the same values)</span></div>${all.map(([g, gr]) => item(g, gr)).join("")}</div>`;
                if (!all.length) h = "";
            } else for (const gr of idx.groups) {
                const gs = gr.games.filter((g) => match(g, gr.name));
                if (!gs.length) continue;
                h += `<div class="ggroup"><div class="gname" title="${esc(gr.desc || "")}">${esc(gr.name)}</div>`;
                for (const g of gs) h += item(g, null);
                h += "</div>";
            }
            list.innerHTML = h || `<div class="muted small">nothing matches</div>`;
            list.querySelectorAll("input[data-id]").forEach((cb) => (cb.onchange = () => {
                const id = cb.dataset.id;
                this.picks = cb.checked ? [...this.picks.filter((x) => x !== id), id] : this.picks.filter((x) => x !== id);
                this._changed();
            }));
        };
        search.oninput = draw;
        this.pop.querySelectorAll(".gsort button").forEach((b) => (b.onclick = () => { sort = b.dataset.s; try { localStorage.setItem(LS + ".sort", sort); } catch {} draw(); }));
        this.pop.querySelector(".gnone").onclick = () => { this.picks = []; draw(); this._changed(); };
        draw();
    }
    set(ids) { this.picks = [...ids]; this._label(); }
    _changed() { this._label(); savePicks(this.picks); this.onChange(this.picks); }
}
