// Other games tab: Quark's weapons next to other games' (web/data/games.json, researched separately with a source
// per number; schema below). Everything is normalised by each game's base health, so different health pools compare
// fairly: shots to kill, time to kill and DPS as % of health per second. Filters by game and weapon class; for the
// selected Quark weapon, the closest weapons in other games (by time to kill, shots to kill and DPS share).
//
// games.json: {version: 1, games: [{id, name, health, source, patch, units?, weapons: [{name, class, body, head,
//   limb, splash: {max, radius} | null, pellets, fireInterval (s), rpm, projectileSpeed, range, falloff, alt, notes,
//   source}]}]}   (null for unknown; body / head / limb are per hit, per pellet when pellets > 1)

import { scatter } from "./charts.js";
import { esc, el, fin as finInf, fmtT as fmtTInf, fmtNum } from "./util.js";
// null (unknown: no health, no fire interval) shows "?", Infinity "∞"
const fin = (x) => (x == null ? "?" : finInf(x));
const fmtT = (x) => (x == null ? "?" : fmtTInf(x));

// used only when web/data/games.json is missing: a few well-known numbers, unsourced (shown as such)
const FALLBACK = { version: 1, fallback: true, games: [
    { id: "q3a", name: "Quake III Arena", health: 100, source: null, patch: "1.32 (from memory, unverified)", weapons: [
        { name: "Machinegun", class: "machine gun", body: 7, head: null, pellets: 1, fireInterval: 0.1, notes: "FFA damage" },
        { name: "Shotgun", class: "shotgun", body: 10, head: null, pellets: 11, fireInterval: 1.0 },
        { name: "Rocket Launcher", class: "rocket launcher", body: 100, head: null, pellets: 1, fireInterval: 0.8, splash: { max: 100, radius: null } },
        { name: "Lightning Gun", class: "beam", body: 8, head: null, pellets: 1, fireInterval: 0.05 },
        { name: "Railgun", class: "sniper rifle", body: 100, head: null, pellets: 1, fireInterval: 1.5 },
        { name: "Plasma Gun", class: "energy", body: 20, head: null, pellets: 1, fireInterval: 0.1 },
    ] },
] };
// weapon classes in families (any spelling in games.json maps by keyword)
const FAMILIES = [
    ["melee", /melee|knife|sword|blade|axe|fist|katana|hammer|bat\b/i], ["sniper", /sniper|marksman|rail|dmr/i], ["shotgun", /shotgun|scatter/i],
    ["explosive", /rocket|launcher|grenade|explosive|bazooka|mortar|sticky|demo/i], ["beam", /beam|laser|lightning|energy|plasma|pulse|arc/i],
    ["bow", /bow|crossbow|arrow|bolt/i], ["pistol", /pistol|revolver|handgun|sidearm/i], ["smg", /smg|submachine/i],
    ["rifle", /rifle|machine gun|machinegun|lmg|minigun|chaingun|carbine|\bar\b|auto/i], ["support", /heal|support|syringe|medi/i],
];
export const familyOf = (cls) => { for (const [f, re] of FAMILIES) if (re.test(cls || "")) return f; return "other"; };
const QUARK_FAMILY = { rifle: "rifle", rocket: "explosive", knife: "melee", sniper: "sniper", laser: "beam", shotgun: "shotgun", sticky: "explosive", crossbow: "bow", bubble: "explosive", syringe: "support", locator: "support" };

// a falloff in any of games.json's shapes -> {text, at(d) -> damage multiplier | null (not numeric)}
export function falloffOf(f, body) {
    if (f == null) return { text: "", at: null };
    if (typeof f === "number") return { text: "x" + f, at: null };
    if (typeof f === "string") return { text: f, at: null };
    const n = (x) => Number.isFinite(+x) && x !== null && x !== "";
    if (Array.isArray(f.brackets) && f.brackets.length) {
        const B = f.brackets;
        const text = B.map((b) => `${b.from}-${b.to} m: ${b.body ?? "?"}${b.head != null ? "/" + b.head : ""}`).join(" · ");
        const b0 = +B[0].body || body || 0;
        return { text, at: b0 ? (d) => { const b = B.find((x) => d >= x.from && d < x.to) || B[B.length - 1]; return b && n(b.body) ? +b.body / b0 : 1; } : null };
    }
    if (n(f.rangeModifier)) { const per = n(f.per) ? +f.per : 12.7; return { text: `x${f.rangeModifier} per ${per} m`, at: (d) => Math.pow(+f.rangeModifier, d / per) }; }
    if (n(f.start) && n(f.end) && +f.end > +f.start) {
        const mult = n(f.multiplier) ? +f.multiplier : n(f.minDamage) && body ? +f.minDamage / body : null;
        const text = `full to ${f.start} m, ${mult != null ? "x" + fmtNum(mult) : "less"} at ${f.end} m` + (f.note ? ` (${f.note})` : "");
        return { text, at: mult != null ? (d) => (d <= +f.start ? 1 : d >= +f.end ? mult : 1 + (mult - 1) * (d - +f.start) / (+f.end - +f.start)) : null };
    }
    const text = Object.entries(f).map(([k, v]) => `${k} ${typeof v === "object" ? JSON.stringify(v) : v}`).join(", ");
    return { text, at: null };
}

// one weapon -> the normalised row. opts: {health (the target's, else the game's; null: raw damage only), scale (x damage)}
export function normWeapon(g, w, opts = {}) {
    const H = opts.health ?? (Number.isFinite(+g.health) && g.health !== null ? +g.health : null);
    const k = Number.isFinite(+opts.scale) && opts.scale > 0 ? +opts.scale : 1;
    const pel = Math.max(1, +w.pellets || 1);
    const perSecond = /per second|\/ ?s\b|dps/i.test(w.notes || "") && (w.class === "beam" || w.fireInterval == null) && w.body != null;
    const interval = perSecond ? null : Number.isFinite(+w.fireInterval) && +w.fireInterval > 0 && w.fireInterval !== null ? +w.fireInterval : Number.isFinite(+w.rpm) && +w.rpm > 0 && w.rpm !== null ? 60 / +w.rpm : null;
    const body = w.body == null ? null : +w.body * pel * k;
    const head = w.head == null ? body : +w.head * pel * k;
    const stk = (d) => (H && d > 0 ? Math.ceil(H / d - 1e-9) : null);
    const fall = falloffOf(w.falloff, w.body);
    const r = { game: g.name, gameId: g.id, weapon: w.name, user: w.user || "", cls: w.class || "", family: familyOf(w.class), health: H, pellets: pel, body, head, interval, perSecond,
        splash: w.splash || null, notes: w.notes || "", source: w.source || g.source || null, patch: g.patch || "", fallText: fall.text, alt: !!opts.alt };
    r.at20 = body != null && fall.at ? body * fall.at(20) : null;
    if (perSecond) {   // a beam given as damage per second
        r.stkBody = r.stkHead = null;
        r.ttkBody = r.ttkHead = H && body ? H / body : null;
        r.dpsPct = H && body ? body / H * 100 : null; r.dpsPctHead = r.dpsPct;
        r.pctShot = null;
        return r;
    }
    r.stkBody = body ? stk(body) : null; r.stkHead = head ? stk(head) : null;
    r.ttkBody = interval != null && r.stkBody != null ? (r.stkBody - 1) * interval : null;
    r.ttkHead = interval != null && r.stkHead != null ? (r.stkHead - 1) * interval : null;
    r.pctShot = H && body ? body / H * 100 : null;
    r.dpsPct = H && body && interval ? body / interval / H * 100 : null;
    r.dpsPctHead = H && head && interval ? head / interval / H * 100 : null;
    return r;
}
// every weapon of a game and its alt fires as rows
export function gameRows(g, opts) {
    const out = [];
    for (const w of g.weapons) {
        out.push(normWeapon(g, w, opts));
        const alts = Array.isArray(w.alt) ? w.alt : w.alt && typeof w.alt === "object" ? [w.alt] : [];
        for (const a of alts) out.push(normWeapon(g, { ...a, name: a.name || w.name + " (alt)", class: a.class || w.class, user: a.user || w.user }, { ...opts, alt: true }));
    }
    return out;
}
// the target healths a game offers: its own and every hero / class health its weapons name
export function healthChoices(g) {
    const s = new Set();
    if (Number.isFinite(+g.health) && g.health !== null) s.add(+g.health);
    for (const w of g.weapons) if (Number.isFinite(+w.userHealth) && w.userHealth !== null) s.add(+w.userHealth);
    return [...s].sort((a, b) => a - b);
}

export class GamesView {
    constructor(app, root) {
        this.app = app; this.root = root;
        this.games = null; this.gopt = {}; this.offGames = new Set(); this.fams = new Set(); this.q = ""; this.sort = { key: "ttkBody", asc: true }; this.showStd = false;
        this.load();
        // the Closest card and the table's highlighted row follow the weapon list
        app.on("select", () => { if (app.page === "games") this.refresh(); });
    }
    async load() {
        try {
            const r = await fetch("/web/data/games.json", { cache: "no-store" });
            if (!r.ok) throw new Error(r.status);
            const j = await r.json();
            if (!j || !Array.isArray(j.games)) throw new Error("no games");
            this.data = j;
        } catch { this.data = FALLBACK; }
        this.games = this.data.games.filter((g) => g && Array.isArray(g.weapons));
        if (this.app.page === "games") this.refresh();
    }
    _quarkRows(stats, edited) {
        const app = this.app;
        return stats.rows.filter((r) => !r.support && r.variant !== "splash").map((r) => {
            const H = app.health;
            return { game: edited ? "Quark" : `Quark (${app.cmpLabel})`, gameId: edited ? "quark" : "quark-std", weapon: r.label, w: r.weapon, quark: true, edited, cls: QUARK_FAMILY[r.weapon], family: QUARK_FAMILY[r.weapon],
                health: H, pellets: r.pellets, body: r.body, head: r.head, interval: r.refire, stkBody: r.stkBody, stkHead: r.stkHead, ttkBody: r.ttkBody, ttkHead: r.ttkHead,
                pctShot: r.body / H * 100, dpsPct: r.dpsBody / H * 100, dpsPctHead: r.dpsHead / H * 100, notes: r.notes.join(" · "), source: null, patch: "this set", mode: r.mode };
        });
    }
    refresh() {
        const app = this.app, root = this.root;
        if (!this.games) { root.innerHTML = `<div class="muted pad">loading web/data/games.json…</div>`; return; }
        const others = this.games.flatMap((g) => gameRows(g, this.gopt[g.id] || {}));
        const quark = this._quarkRows(app.stats, true);
        const qstd = this.showStd && app.cmpStats ? this._quarkRows(app.cmpStats, false) : [];
        root.innerHTML = "";
        // toolbar
        const bar = el("div", "vbar");
        const nAlt = others.filter((r) => r.alt).length, nW = others.length - nAlt;
        bar.innerHTML = `<input placeholder="filter: game, weapon, class…" style="width:200px" value="${esc(this.q)}"><label class="check"><input type="checkbox" ${this.showStd ? "checked" : ""}> also Quark ${esc(app.cmpLabel)}</label>
            <span class="grow"></span><span class="muted small">${this.games.length} games · ${nW} weapons${nAlt ? `, ${nAlt} alt fires` : ""} · ${this.data.fallback ? '<span class="warn">web/data/games.json not found: a small unsourced sample</span>' : "web/data/games.json" + (this.data.updated ? " (" + esc(this.data.updated) + ")" : "")} · normalised by each game's health: shots and time to kill, DPS in % of health / s</span>`;
        const inp = bar.querySelector("input");
        inp.oninput = () => { this.q = inp.value.trim().toLowerCase(); this._refocus = true; clearTimeout(this._qt); this._qt = setTimeout(() => this.refresh(), 200); };
        bar.querySelector("input[type=checkbox]").onchange = (e) => { this.showStd = e.target.checked; this.refresh(); };
        root.append(bar);
        const fg = el("div", "vbar");
        const gc = el("span", "filters"); gc.append(el("span", "muted small", "games "));
        for (const g of [{ id: "quark", name: "Quark" }, ...this.games]) {
            const c = el("span", "chip" + (this.offGames.has(g.id) ? "" : " on"), esc(g.name));
            c.title = g.patch ? `${g.name}: ${g.patch}, ${g.health} hp` : g.name;
            c.onclick = () => { if (this.offGames.has(g.id)) this.offGames.delete(g.id); else this.offGames.add(g.id); this.refresh(); };
            gc.append(c);
        }
        fg.append(gc);
        root.append(fg);
        const fc = el("div", "vbar"), fl = el("span", "filters");
        fl.append(el("span", "muted small", "classes "));
        for (const f of ["melee", "pistol", "smg", "rifle", "sniper", "shotgun", "explosive", "beam", "bow", "other"]) {
            const c = el("span", "chip" + (this.fams.size === 0 || this.fams.has(f) ? " on" : ""), f);
            c.onclick = () => { if (this.fams.has(f)) this.fams.delete(f); else this.fams.add(f); this.refresh(); };
            fl.append(c);
        }
        const allc = el("span", "chip", "all"); allc.onclick = () => { this.fams.clear(); this.refresh(); }; fl.append(allc);
        fc.append(fl);
        root.append(fc);
        const keep = (r) => !this.offGames.has(r.gameId === "quark-std" ? "quark" : r.gameId) && (this.fams.size === 0 || this.fams.has(r.family)) && (!this.q || `${r.game} ${r.weapon} ${r.cls}`.toLowerCase().includes(this.q));
        const rows = [...quark, ...qstd, ...others].filter(keep);
        const grid = el("div", "cards wide");
        // closest weapons for the selected Quark weapon
        const sel = quark.find((r) => r.w === app.sel && r.mode === "primary") || quark.find((r) => r.w === app.sel);
        const close = el("div", "card");
        if (sel) {
            const lg = (x) => Math.log(Math.max(0.05, Number.isFinite(x) ? x : 30));
            const dist = (o) => Math.abs(lg(o.ttkBody + 0.1) - lg(sel.ttkBody + 0.1)) + 0.8 * (o.perSecond ? 1 : Math.abs(lg(o.stkBody) - lg(sel.stkBody))) + 0.6 * Math.abs(lg(o.dpsPct || 0.1) - lg(sel.dpsPct || 0.1)) + (o.family === sel.family ? 0 : 0.6);
            const cands = others.filter((o) => Number.isFinite(o.ttkBody) && (Number.isFinite(o.stkBody) || o.perSecond)).map((o) => ({ o, d: dist(o) })).sort((a, b) => a.d - b.d).slice(0, 10);
            let h = `<h4>Closest to ${esc(sel.weapon)} <span class="muted small">by time to kill, shots to kill, DPS share and class</span></h4><table class="score"><tr><th>game · weapon</th><th>class</th><th>shots</th><th>TTK s</th><th>DPS %/s</th></tr>`;
            h += `<tr class="sel"><td><b>Quark · ${esc(sel.weapon)}</b></td><td>${esc(sel.family)}</td><td>${fin(sel.stkBody)}</td><td>${fmtT(sel.ttkBody)}</td><td>${fmtNum(sel.dpsPct)}</td></tr>`;
            for (const { o } of cands) h += `<tr title="${esc(o.notes)}"><td>${esc(o.game)} · ${esc(o.weapon)}</td><td>${esc(o.cls)}</td><td>${fin(o.stkBody)}</td><td>${fmtT(o.ttkBody)}</td><td>${o.dpsPct == null ? "–" : fmtNum(o.dpsPct)}</td></tr>`;
            h += `</table><div class="muted small" style="margin-top:4px">pick another weapon on the left</div>`;
            close.innerHTML = h;
        } else close.innerHTML = `<h4>Closest weapons</h4><div class="muted small">the selected weapon does no damage</div>`;
        grid.append(close);
        const sc = el("div", "card");
        sc.innerHTML = `<h4>Time to kill vs shots to kill <span class="legend" style="margin-left:auto"><span><span class="sw" style="background:var(--s-hi)"></span>Quark (this set)</span>${this.showStd ? `<span><span class="sw" style="background:var(--s-std)"></span>Quark ${esc(app.cmpLabel)}</span>` : ""}<span><span class="sw" style="background:var(--s-other)"></span>other games</span></span></h4>`;
        const cv = el("canvas"); cv.style.height = "300px"; sc.append(cv);
        grid.append(sc);
        const opt = el("div", "card");
        opt.innerHTML = `<h4>Target health and damage scale per game <span class="muted small">heroes / classes: pick whose health; no verified health: type one</span></h4>`;
        const ot = el("table", "score");
        ot.innerHTML = `<tr><th>game</th><th>target health</th><th>x damage</th><th></th></tr>`;
        for (const g of this.games) {
            const o = this.gopt[g.id] || {};
            const ch = healthChoices(g);
            const tr = el("tr");
            const td1 = el("td", null, esc(g.name)); td1.title = g.healthNote || "";
            const td2 = el("td");
            const sel = el("select");
            sel.innerHTML = (ch.length ? "" : `<option value="">? (none verified)</option>`) + ch.map((h) => `<option value="${h}">${h}${+g.health === h ? " (base)" : ""}</option>`).join("") + `<option value="custom">custom…</option>`;
            const cur = o.health ?? (ch.includes(+g.health) ? +g.health : "");
            sel.value = ch.includes(cur) ? String(cur) : cur === "" ? "" : "custom";
            const num = el("input"); num.type = "number"; num.min = 1; num.style.width = "64px"; num.value = o.health ?? ""; num.style.display = sel.value === "custom" ? "" : "none";
            sel.onchange = () => { if (sel.value === "custom") { num.style.display = ""; num.focus(); return; } this.gopt[g.id] = { ...o, health: sel.value ? +sel.value : null }; this.refresh(); };
            num.onchange = () => { const h = +num.value; if (h > 0) { this.gopt[g.id] = { ...o, health: h }; this.refresh(); } };
            td2.append(sel, num);
            const td3 = el("td"); const sc = el("input"); sc.type = "number"; sc.step = "0.05"; sc.min = 0.05; sc.style.width = "60px"; sc.value = o.scale ?? 1;
            sc.title = "a damage multiplier for this game (a mutator, a hardcore mode)";
            sc.onchange = () => { const v = +sc.value; this.gopt[g.id] = { ...o, scale: v > 0 ? v : 1 }; this.refresh(); };
            td3.append(sc);
            const users = [...new Set(g.weapons.filter((w) => w.userHealth != null).map((w) => `${w.user} ${w.userHealth}`))];
            tr.append(td1, td2, td3, el("td", "muted small", esc(users.slice(0, 4).join(", ") + (users.length > 4 ? "…" : ""))));
            ot.append(tr);
        }
        opt.append(ot);
        grid.append(opt);
        root.append(grid);
        requestAnimationFrame(() => scatter(cv, rows.filter((r) => Number.isFinite(r.ttkBody) && Number.isFinite(r.stkBody) && r.stkBody != null && r.ttkBody != null).map((r) => ({ x: Math.max(0.01, r.ttkBody), y: r.stkBody, label: `${r.game} · ${r.weapon}`, sub: `${r.cls} · ${r.health} hp`, hi: !!r.quark, edited: r.quark ? r.edited : undefined, short: r.quark && r.mode === "primary" ? r.w : "" })),
            { logX: true, logY: true, xLabel: "time to kill, body (s, log)", yLabel: "shots to kill, body (log)" }));
        // the table
        const cols = [["game", "game"], ["weapon", "weapon"], ["cls", "class"], ["health", "health"], ["body", "body / shot"], ["head", "head / shot"], ["interval", "interval s"],
            ["at20", "body @20 m"], ["pctShot", "% hp / shot"], ["stkBody", "shots b"], ["stkHead", "shots h"], ["ttkBody", "TTK b s"], ["ttkHead", "TTK h s"], ["dpsPct", "DPS %/s b"], ["dpsPctHead", "DPS %/s h"]];
        const k = this.sort.key, s = this.sort.asc ? 1 : -1;
        rows.sort((a, b) => { const A = a[k], B = b[k]; if (typeof A === "string" || typeof B === "string") return String(A).localeCompare(String(B)) * s; const x = Number.isFinite(A) ? A : Infinity, y = Number.isFinite(B) ? B : Infinity; return (x - y) * s; });
        let h = `<table class="vt"><thead><tr>${cols.map(([c, t]) => `<th data-k="${c}" class="${(c === "weapon" || c === "cls" ? "l " : "") + (k === c ? "sorted" + (this.sort.asc ? " asc" : "") : "")}">${t}</th>`).join("")}<th class="l">source</th></tr></thead><tbody>`;
        const n = (v) => (v == null ? "?" : fmtNum(v));
        for (const r of rows) {
            h += `<tr class="${r.quark ? "quark" : ""}${r.quark && r.w === app.sel ? " sel" : ""}" title="${esc([r.user ? "user: " + r.user : "", r.patch, r.fallText ? "falloff: " + r.fallText : "", r.notes].filter(Boolean).join("\n"))}">`
                + `<td>${esc(r.game)}</td><td style="text-align:left">${r.alt ? '<span class="muted">alt · </span>' : ""}${esc(r.weapon)}</td><td style="text-align:left">${esc(r.cls)}</td><td class="n">${r.health == null ? '<span class="warn" title="no verified base health: set one in Target health per game">?</span>' : r.health}</td>`
                + `<td class="n">${n(r.body)}${r.perSecond ? '<span class="muted">/s</span>' : ""}${r.pellets > 1 ? `<span class="muted"> (${r.pellets}×)</span>` : ""}</td><td class="n">${n(r.head)}</td><td class="n">${r.perSecond ? "per s" : r.interval == null ? "?" : n(r.interval)}</td><td class="n" title="${esc(r.fallText)}">${r.at20 == null ? (r.fallText ? '<span class="muted">' + esc(r.fallText.slice(0, 18)) + (r.fallText.length > 18 ? "…" : "") + "</span>" : "") : n(r.at20)}</td><td class="n">${n(r.pctShot)}</td>`
                + `<td class="n">${fin(r.stkBody)}</td><td class="n">${fin(r.stkHead)}</td><td class="n">${fmtT(r.ttkBody)}</td><td class="n">${fmtT(r.ttkHead)}</td><td class="n">${n(r.dpsPct)}</td><td class="n">${n(r.dpsPctHead)}</td>`
                + `<td class="src">${r.source ? `<a href="${esc(r.source)}" target="_blank" rel="noopener noreferrer">source</a>` : r.quark ? "" : '<span class="muted">none</span>'}</td></tr>`;
        }
        h += `</tbody></table>`;
        const tb = el("div"); tb.innerHTML = h;
        tb.querySelectorAll("th[data-k]").forEach((th) => (th.onclick = () => { const c = th.dataset.k; this.sort = this.sort.key === c ? { key: c, asc: !this.sort.asc } : { key: c, asc: true }; this.refresh(); }));
        root.append(el("h3", null, "Every weapon"), tb);
        if (this._refocus) { this._refocus = false; inp.focus(); inp.setSelectionRange(inp.value.length, inp.value.length); }
    }
}
