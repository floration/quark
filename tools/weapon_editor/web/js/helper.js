// The one place the page talks to the game's helper (quark_maptool, DESIGN.md section 4). Every answer the page
// uses has a MOCK in the page (catalog.js, ast.js validate, stats.js, mockrange.js), so the page works with no
// helper at all; with one, its answers replace the mock's.
//
//   wpnparams {weapons?}                     -> the catalog (DESIGN.md 3) + values {key: value} + changed [keys]
//   wpncheck  {weapons}                      -> {ok, errors: [{line, col, msg}], warnings: [{line, col, msg}], ast?}
//   wpnstats  {weapons, compare?: [text], health?, dist?} -> {rows: [row], compare: [{rows}]}   (row: stats.js)
//   wpnrange  {weapons, map?, weapon, alt?, targets, shooter, seconds, seed, health?, order?} -> mockrange.js's answer
//   sim / simbatch (the bot editor's, tools/bot_editor/native/SIM.md) with `weapons` (the set's text), `qmode`
//   (a custom mode text) and `weaponsAllowed` ([weapon names]) keys; the start line echoes `weaponSet` (the set's
//   name as applied) so the page can tell a helper that applies sets from one that ignores the key.
//
// A helper without the wpn* commands (an older quark_maptool) answers with an error: the page stays on the MOCK
// for those and says so. The catalog then comes from `quark_tests exportweapons` (serve.py /api/catalog) when
// that exists, else the built-in table.

import { api } from "./api.js";
import { builtinCatalog, normalizeCatalog, BUILTIN_SOURCE } from "./catalog.js";
import { parse } from "./dsl.js";
import { validate } from "./ast.js";
import { computeStats } from "./stats.js";
import { mockRange } from "./mockrange.js";

const wantMock = () => new URLSearchParams(location.search).has("mock");

async function call(cmd, request, timeout = 60) {
    const r = await api.helper([cmd], request, timeout);
    if (!r || r.code !== 0 || !r.answer || typeof r.answer !== "object" || r.answer.error) {
        const e = new Error((r && r.answer && r.answer.error) || (r && r.stderr && r.stderr.trim().split("\n").pop()) || `${cmd} failed`);
        e.helper = r;
        throw e;
    }
    return r.answer;
}

export async function createHelper(config) {
    const H = {
        kind: "mock", wpn: false, sim: false, catalogSource: BUILTIN_SOURCE, why: "",
        label: "MOCK", rawCatalog: null, mockWanted: wantMock(),
    };
    const hasTool = !!(config && config.maptool) && !wantMock();
    if (hasTool) {
        try {
            const a = await call("wpnparams", {}, 30);
            if (Array.isArray(a.fields) && a.fields.length) { H.wpn = true; H.rawCatalog = a; H.catalogSource = "quark_maptool wpnparams"; H.features = a.features || ["stats", "range", "check"]; }
        } catch (e) { H.why = "quark_maptool has no wpnparams yet (" + e.message.slice(0, 120) + ")"; }
        try {
            const r = await api.helper(["simcheck"], { text: "version 1\n" }, 20);
            if (r && r.answer && "brainSupport" in r.answer) H.sim = true;
        } catch {}
    } else H.why = wantMock() ? "?mock in the address" : "no quark_maptool found";
    if (!H.rawCatalog && !wantMock()) {
        try {
            const c = await api.catalog();
            if (c && c.catalog && Array.isArray(c.catalog.fields)) { H.rawCatalog = c.catalog; H.catalogSource = "quark_tests exportweapons"; }
        } catch {}
    }
    H.catalog = H.rawCatalog ? normalizeCatalog(H.rawCatalog) : builtinCatalog();
    H.kind = H.wpn ? "real" : "mock";
    H.label = H.wpn ? "quark_maptool" : "MOCK";
    H.title = H.wpn ? `the game's helper answers (catalog: ${H.catalogSource})`
        : `MOCK: the page works the numbers out itself (${H.why}); catalog: ${H.catalogSource}. Fights ${H.sim ? "use quark_maptool sim" : "need the helper"}.`;

    // errors and warnings of a text: the page's own parse + checks, then the game's (wpncheck, or quark_tests
    // checkweapons through serve.py) when there is one: those are authoritative
    H.checkLocal = (text) => {
        let set = null;
        try { set = parse(text); } catch (e) { return { ok: false, set: null, problems: [{ severity: "error", msg: e.msg || e.message, line: e.line || 1, col: e.col || 1 }], via: "page" }; }
        const v = validate(set);
        return { ok: !v.errors.length, set, problems: v.problems, via: "page" };
    };
    H.checkGame = async (text) => {
        const conv = (a, sev) => (a || []).map((x) => (typeof x === "string" ? { severity: sev, msg: x, line: 0, col: 0 } : { severity: sev, msg: x.msg || x.message || String(x), line: x.line || 0, col: x.col || 0 }));
        if (H.wpn) {
            try { const a = await call("wpncheck", { weapons: text }, 20); return { ok: !!a.ok, problems: [...conv(a.errors, "error"), ...conv(a.warnings, "warning")], via: "wpncheck" }; } catch {}
        }
        try {
            const r = await api.check(text);
            if (r && r.answer) return { ok: !!r.answer.ok, problems: [...conv(r.answer.errors, "error"), ...conv(r.answer.warnings, "warning")], via: "checkweapons" };
        } catch {}
        return null;
    };

    // the numbers: the page's (always, at once) and the game's when it has wpnstats
    H.statsLocal = (values, opts) => computeStats(values, opts);
    H.statsGame = async (text, compare, opts = {}) => {
        if (!H.wpn) return null;
        try {
            const a = await call("wpnstats", { weapons: text, compare: compare ? [compare] : [], health: opts.health || 100, dist: opts.dist || 0 }, 30);
            return Array.isArray(a.rows) ? a : null;
        } catch { return null; }
    };

    // once per catalog load: the page's numbers (stats.js, used everywhere because they are instant) against the
    // game's wpnstats on the standard set at two distances, so a change in gamedefs.h that stats.js doesn't follow
    // shows up as a warning instead of silently wrong numbers. -> null (no wpnstats) or {diffs: [text]}
    H.selfCheck = async (text, values, set) => {
        if (!H.wpn) return null;
        const COLS = ["body", "head", "stkBody", "stkHead", "ttkBody", "ttkHead", "dpsBody", "dpsHead", "refire", "range", "speed", "splashRadius", "splashMax", "at10", "at20", "at40"];
        const same = (a, b) => (a == null || !Number.isFinite(a) ? b == null || !Number.isFinite(b) : b != null && Math.abs(a - b) <= 1e-4 * Math.max(1, Math.abs(a)));
        const diffs = [];
        for (const dist of [0, 20]) {
            const g = await H.statsGame(text, null, { health: 100, dist });
            if (!g) return null;
            const mine = new Map(computeStats(values, { health: 100, dist, set }).rows.map((r) => [r.id, r]));
            for (const gr of g.rows) {
                const r = mine.get(gr.id);
                if (!r) { diffs.push(`${gr.id}: the page has no such row`); continue; }
                for (const k of COLS) if (!same(gr[k], r[k])) diffs.push(`${gr.id} ${k} at ${dist} m: game ${gr[k] ?? "∞"}, page ${Number.isFinite(r[k]) ? +r[k].toPrecision(7) : "∞"}`);
                for (const p of gr.per || []) if (!same(p.body, r.per(p.d, false))) { diffs.push(`${gr.id} damage at ${p.d} m: game ${p.body}, page ${+r.per(p.d, false).toPrecision(7)}`); break; }
            }
        }
        return { diffs };
    };

    // the range: the game's own server when the helper has wpnrange, else the page's stand-in
    H.range = async (req, ctx) => {
        if (H.wpn) {
            try {
                const a = await call("wpnrange", { ...req, weapons: ctx.text, ...(ctx.mapText ? { map: { name: ctx.mapName, text: ctx.mapText } } : {}) }, 120);
                if (a && Array.isArray(a.targets)) return { ...a, via: "wpnrange" };
            } catch (e) { ctx.onNote && ctx.onNote("wpnrange failed (" + e.message + "): the page's stand-in ran instead"); }
        }
        await new Promise((r) => setTimeout(r, 0));
        return mockRange(req, ctx.values, ctx.set, ctx.map);
    };
    return H;
}
