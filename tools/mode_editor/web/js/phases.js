// New for the mode editor (no bot editor counterpart). The Phases view: the engine's phase machine (warmup, freeze,
// round, overtime, round end, match end) as columns, each with the slots that fire there, coloured by what runs them:
// INTERPRETED (this file changes the slot or a proc it calls), NATIVE (the built-in text: the game's C++ runs it), or
// NOT WRITTEN (the engine's own default). Objects sit in the round with their events; timers and state on the side;
// an "engine" box lists what no logic can change. Click a slot or object to open it.
//
// phasesModel() is pure (no DOM): the tests and the view both use it.

import { CATALOG, declKey, slotKey, symbols, interpretedSlots } from "./lang.js";

export const PHASES = [
    { id: "warmup", title: "Warmup", doc: "players join; autostart counts down when enough are in", params: ["warmupCountdown", "warmupRespawn"] },
    { id: "freeze", title: "Freeze", doc: "spawned, frozen, the countdown", params: ["freezeTime"] },
    { id: "live", title: "Round", doc: "the round is live: objects step, then tick", params: ["roundTime", "matchTime", "respawn"] },
    { id: "overtime", title: "Overtime", doc: "the round clock ran out: timeUp runs each tick; an objective touch holds the round open", params: ["overtime"] },
    { id: "roundEnd", title: "Round end", doc: "the round result screen; then roundOver decides: next round or match end", params: ["roundEndTime"] },
    { id: "matchEnd", title: "Match end", doc: "the match result screen, then back to warmup (engine only)", params: ["matchEndTime"] },
];
// catalog phase -> [column, "enter" (fires as the column starts) | "during"]
const PLACE = {
    warmup: ["warmup", "during"], "matchEnd>warmup": ["warmup", "enter"], "warmup>freeze": ["freeze", "enter"], freeze: ["freeze", "during"],
    "freeze>live": ["live", "enter"], live: ["live", "during"], roundEnd: ["roundEnd", "enter"],
};

// -> {columns: [{...PHASES[i], enter: [slot], during: [slot], objects: [obj], params: [{name, value}]}], timers, states, engine}
//    slot = {key, name, label, status: "interpreted"|"native"|"unwritten"|"inactive", kind, when, cost, object}
export function phasesModel(ast, def, check) {
    const S = symbols(ast);
    const interp = def ? interpretedSlots(ast, def) : new Set();
    const uses = new Set([...S.uses.keys()]);
    const cols = PHASES.map((p) => ({ ...p, enter: [], during: [], objects: [], params: [] }));
    const col = (id) => cols.find((c) => c.id === id);
    const info = (check && check.info) || {};
    const mk = (name, d, cat, label) => {
        const key = d ? declKey(d) : "slot:" + name;
        let status = d ? (interp.has(slotKey(d)) ? "interpreted" : "native") : "unwritten";
        if (cat.object && !uses.has(cat.object)) status = "inactive";
        return { key, name, label: label || name, status, kind: cat.kind, when: cat.when, cost: info[key] || null, object: cat.object || null };
    };
    for (const [name, cat] of Object.entries(CATALOG.slots)) {
        let [c, at] = PLACE[cat.phase] || ["live", "during"];
        if (name === "timeUp") { c = "overtime"; at = "during"; }
        if (name === "roundOver") { c = "roundEnd"; at = "during"; }
        if (cat.object) continue;   // drawn under their object
        if (cat.timer) {
            const ts = ast.decls.filter((d) => d.kind === "slot" && d.name === "timer");
            for (const d of ts) col("live").during.push(mk(name, d, cat, "timer " + d.timer));
            continue;
        }
        const d = S.slots.get(name);
        col(c)[at].push(mk(name, d, cat));
    }
    for (const [obj, o] of Object.entries(CATALOG.objects)) {
        const on = uses.has(obj);
        const events = o.events.map((ev) => { const cat = CATALOG.slots[ev] || { kind: "event", when: "", object: obj }; return mk(ev, S.slots.get(ev), { ...cat, object: obj }); });
        if (on || events.some((e) => e.status !== "inactive" && e.status !== "unwritten")) col(obj === "tiebreak" ? "roundEnd" : obj === "it" ? "freeze" : "live").objects.push({ name: obj, on, doc: o.doc, steps: o.steps, events, params: o.params });
    }
    for (const c of cols) for (const p of c.params.length ? [] : PHASES.find((x) => x.id === c.id).params) {
        const d = S.params.get(p);
        if (d) c.params.push({ name: p, value: d.value, key: declKey(d) });
    }
    const timers = [...S.timers.values()].map((d) => ({ name: d.name, key: declKey(d), slot: S.slots.has("timer." + d.name) }));
    const states = [...S.states.values()].map((d) => ({ name: d.name, key: declKey(d), life: d.life, per: d.per, type: d.type }));
    return { columns: cols, timers, states, engine: CATALOG.engine || [] };
}

const esc = (x) => String(x ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const STATUS_TEXT = { interpreted: "interpreted (this file changes it)", native: "native (the built-in text, run as C++)", unwritten: "not written: the engine's default", inactive: "its object isn't in use" };

export class PhasesView {
    constructor(root, app) { this.root = root; this.app = app; }
    setBrain(ast, def, check) { this.model = phasesModel(ast, def, check); this.render(); }
    render() {
        const M = this.model, R = this.root;
        const chip = (s) => `<div class="ph-slot ${s.status}" data-open="${esc(s.status === "unwritten" || s.status === "inactive" ? "" : s.key)}" data-slot="${esc(s.name)}" title="${esc(s.when)} · ${esc(STATUS_TEXT[s.status])}${s.cost ? ` · ${s.cost.insns} insns (worst ${s.cost.worst})` : ""}"><span>${esc(s.label)}</span>${s.kind === "query" ? '<i>?</i>' : ""}${s.cost && s.status === "interpreted" ? `<em>${s.cost.insns}</em>` : ""}</div>`;
        let h = `<div class="ph-legend"><span class="ph-slot interpreted"><span>interpreted</span></span><span class="ph-slot native"><span>native (built-in)</span></span><span class="ph-slot unwritten"><span>not written</span></span><span class="ph-slot inactive"><span>object not in use</span></span><span class="muted">? = a query (returns a value) · number = instructions per call</span></div>`;
        h += `<div class="ph-row">`;
        M.columns.forEach((c, i) => {
            h += `<div class="ph-col ph-${c.id}"><header><b>${esc(c.title)}</b>${i < M.columns.length - 1 ? '<span class="ph-arrow">→</span>' : '<span class="ph-arrow">↺</span>'}</header><div class="muted ph-doc">${esc(c.doc)}</div>`;
            if (c.params.length) h += `<div class="ph-params">${c.params.map((p) => `<span class="chip" data-open="${esc(p.key)}">${esc(p.name)} = ${esc(p.value)}</span>`).join(" ")}</div>`;
            if (c.enter.length) h += `<div class="ph-lane"><div class="ph-lt">on entering</div>${c.enter.map(chip).join("")}</div>`;
            if (c.during.length) h += `<div class="ph-lane"><div class="ph-lt">during</div>${c.during.map(chip).join("")}</div>`;
            for (const o of c.objects) h += `<div class="ph-obj ${o.on ? "on" : ""}" title="${esc(o.doc)}"><div class="ph-lt">use ${esc(o.name)}${o.on ? "" : " (not in use)"}</div><div class="muted" style="font-size:11px">steps: ${esc(o.steps.join(", "))}</div>${o.events.map(chip).join("")}</div>`;
            if (!c.enter.length && !c.during.length && !c.objects.length) h += `<div class="muted ph-doc">no slots: engine only</div>`;
            h += `</div>`;
        });
        h += `</div><div class="ph-side">`;
        h += `<div class="ph-box"><h4>Timers</h4>${M.timers.length ? M.timers.map((t) => `<div data-open="${esc(t.key)}" class="ph-it">${esc(t.name)}${t.slot ? "" : ' <span class="warn">(no slot timer)</span>'}</div>`).join("") : '<div class="muted">none</div>'}</div>`;
        h += `<div class="ph-box"><h4>State</h4>${M.states.length ? M.states.map((s) => `<div data-open="${esc(s.key)}" class="ph-it">${esc(s.name)}${s.per ? `[${esc(s.per)}]` : ""} <span class="muted">${esc(s.type)}, resets each ${esc(s.life)}</span></div>`).join("") : '<div class="muted">none</div>'}</div>`;
        h += `<div class="ph-box ph-engine"><h4>Stays engine</h4><ul>${M.engine.map((e) => `<li>${esc(e)}</li>`).join("")}</ul></div></div>`;
        R.innerHTML = h;
        R.querySelectorAll("[data-open]").forEach((e) => (e.onclick = () => {
            if (e.dataset.open) { this.app.openDecl(e.dataset.open, "code"); return; }
            const name = e.dataset.slot;
            if (name && this.app.addSlot) this.app.addSlot(name);
        }));
    }
}
