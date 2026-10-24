// What the page derives from /api/model: lookups, lint per item, CI keys, the "when" lanes, routes. No DOM.

export const wfOf = (suite) => (suite.file || suite.id.replace(/^ci:/, "")).split("/").pop();
export const stripMatrix = (name) => String(name || "").replace(/\s*\([^()]*\)\s*$/, "");

export function indexModel(m) {
    const ix = { m, items: new Map(), suites: new Map(), groups: new Map(), cmds: new Map(), jobs: new Map(), fnLint: new Map(), itemLint: new Map(), fileLint: new Map() };
    for (const s of m.suites || []) {
        ix.suites.set(s.id, s);
        for (const it of s.items || []) {
            it._suite = s;
            if (!it.file) it.file = s.file;
            ix.items.set(s.id + "|" + it.id, it);
            if (it.kind === "group") ix.groups.set(it.name, it);
            else if (it.kind === "shortcut" || it.kind === "tool") ix.cmds.set(it.name, it);
            else if (it.kind === "job") { ix.jobs.set(wfOf(s) + "/" + it.jobId, it); if (it.name) ix.jobs.set(wfOf(s) + "/" + it.name, it); }
        }
    }
    const fns = m.functions || {};
    for (const n in fns) fns[n].name = fns[n].name || n;
    // lint -> the item (or function) whose line range holds it
    const ranges = [];
    for (const s of m.suites || []) for (const it of s.items || []) ranges.push({ file: it.file, a: it.line, b: it.endLine || it.line, key: s.id + "|" + it.id });
    for (const l of m.lint || []) {
        if (!ix.fileLint.has(l.file)) ix.fileLint.set(l.file, []);
        ix.fileLint.get(l.file).push(l);
        let hit = false;
        for (const r of ranges) if (r.file === l.file && l.line >= r.a && l.line <= r.b) { push(ix.itemLint, r.key, l); hit = true; }
        for (const n in fns) { const f = fns[n]; if (f.file === l.file && l.line >= f.line && l.line <= (f.endLine || f.line)) { push(ix.fnLint, n, l); hit = true; } }
        if (!hit) l._loose = true;
    }
    return ix;
}
function push(map, k, v) { if (!map.has(k)) map.set(k, []); map.get(k).push(v); }

export const itemKey = (it) => it._suite.id + "|" + it.id;
export function lintFor(ix, sel) {
    if (!sel) return [];
    if (sel.kind === "fn") return ix.fnLint.get(sel.fn.name) || [];
    if (sel.kind === "item") return ix.itemLint.get(itemKey(sel.item)) || [];
    if (sel.kind === "suite") return (ix.m.lint || []).filter((l) => l.file === sel.suite.file || (sel.suite.items || []).some((it) => it.file === l.file && l.line >= it.line && l.line <= (it.endLine || it.line)));
    return [];
}

// ---- CI: the last result for a job (a matrix job: every "<name> (<value>)" run)
export function ciFor(ci, job) {
    if (!ci || !ci.jobs || !job) return [];
    const wf = wfOf(job._suite), out = [];
    for (const k in ci.jobs) {
        const [w, ...rest] = k.split("/");
        const name = rest.join("/");
        if (w !== wf) continue;
        if (name === job.jobId || name === job.name || stripMatrix(name) === job.jobId || stripMatrix(name) === job.name) out.push({ key: k, name, ...ci.jobs[k] });
    }
    return out;
}
export function worstConclusion(list) {
    const rank = { failure: 5, cancelled: 4, timed_out: 5, startup_failure: 5, action_required: 3, in_progress: 2, queued: 2, skipped: 1, success: 0, neutral: 0 };
    let best = null;
    for (const r of list) if (!best || (rank[r.conclusion] ?? 2) > (rank[best] ?? 2)) best = r.conclusion || "in_progress";
    return best;
}
export const conclClass = (c) => (!c ? "" : c === "success" ? "ok" : c === "skipped" || c === "neutral" ? "skip" : /fail|timed|cancel|startup/.test(c) ? "bad" : "run");

// ---- TP() ports a group's calls use (two par groups on one port can collide)
export function portsOf(item) {
    const out = [];
    for (const c of item.calls || []) for (const mm of String(c.args || "").matchAll(/TP\((\d+)\)/g)) out.push(+mm[1]);
    return out;
}
export function sharedPorts(ix) {
    const by = new Map();
    for (const g of ix.groups.values()) if (g.list === "par") for (const p of portsOf(g)) push(by, p, g.name);
    const out = new Map();   // group name -> [{port, with: [names]}]
    for (const [p, names] of by) if (new Set(names).size > 1) for (const n of new Set(names)) push(out, n, { port: p, with: [...new Set(names)].filter((x) => x !== n) });
    return out;
}

// ---- when: the lanes in time order, and where each suite / job goes
export const LANES = [
    { id: "edit", title: "edit", sub: "you save a file in the working copy" },
    { id: "precommit", title: "pre-commit hook", sub: "git commit runs it (core.hooksPath tools/githooks)" },
    { id: "commit", title: "before commit", sub: "build, then build/quark_tests (by hand or by Claude)" },
    { id: "check", title: "release_check.sh", sub: "before a push: every group, then every map hosted" },
    { id: "push", title: "push to a branch", sub: "CI on every push (any branch)" },
    { id: "testing", title: "push to testing", sub: "the signed prerelease; also runs everything above it" },
    { id: "final", title: "[final]", sub: "only a human marks a final test: sanitizers + fuzzers" },
    { id: "tag", title: "v* tag", sub: "production: on the default branch, every platform" },
    { id: "pr", title: "pull request", sub: "a PR into the default branch" },
    { id: "sched", title: "schedule / manual", sub: "cron or a manual run" },
];
const TRIGGER_LANE = { "pre-commit": "precommit", "before-commit": "commit", ctest: "commit", "release-check": "check", pull_request: "pr", schedule: "sched", workflow_run: "sched", manual: "sched", workflow_dispatch: "sched" };

export function jobLanes(job, byJobId, memo = new Map()) {
    if (memo.has(job.jobId)) return memo.get(job.jobId);
    memo.set(job.jobId, new Set(["push"]));
    let raw = String(job.if || "");
    // a negated group is an exclusion ("not on a final run"), not where it runs
    for (let k = 0; k < 4; k++) raw = raw.replace(/!\s*\([^()]*\)/g, "");
    const own = new Set();
    if (/sanitizers|'final'|\[final\]|inputs\.final/.test(raw)) own.add("final");
    if (/'production'|refs\/tags|ref_type\s*==\s*'tag'|startsWith\(github\.ref,\s*'refs\/tags/.test(raw)) own.add("tag");
    if (/'testing'|refs\/heads\/testing/.test(raw)) own.add("testing");
    if (/'site'/.test(raw)) own.add("sched");
    let lanes = own;
    if (!own.size) {
        lanes = new Set();
        for (const n of job.needs || []) { const d = byJobId.get(n); if (d) for (const l of jobLanes(d, byJobId, memo)) lanes.add(l); }
        if (!lanes.size || [...lanes].every((l) => l === "push")) lanes = new Set(["push"]);
        else lanes.delete("push");
    }
    memo.set(job.jobId, lanes);
    return lanes;
}

// -> Map lane id -> [{suite, item|null, why}]
export function whenLanes(m) {
    const out = new Map(LANES.map((l) => [l.id, []]));
    out.get("edit").push({ suite: null, item: null, why: "the tests editor saves through the server; nothing runs yet" });
    for (const s of m.suites || []) {
        const trig = new Set((s.when || []).map((w) => w.trigger));
        if (s.kind === "ci") {
            const byJobId = new Map((s.items || []).map((j) => [j.jobId, j]));
            const pushy = trig.has("push") || trig.has("tag");
            const memo = new Map();
            for (const j of s.items || []) {
                if (pushy) for (const l of jobLanes(j, byJobId, memo)) out.get(l).push({ suite: s, item: j, why: j.ifText || (j.if ? j.if : "every push") });
                for (const t of trig) if (TRIGGER_LANE[t] && (!pushy || t === "pull_request")) { const l = TRIGGER_LANE[t]; if (!out.get(l).some((x) => x.item === j)) out.get(l).push({ suite: s, item: j, why: (s.when.find((w) => w.trigger === t) || {}).text || t }); }
            }
            if (pushy && trig.has("workflow_dispatch")) out.get("sched").push({ suite: s, item: null, why: (s.when.find((w) => w.trigger === "workflow_dispatch") || {}).text || "a manual run" });
        } else {
            for (const w of s.when || []) { const l = TRIGGER_LANE[w.trigger] || (w.trigger === "push" ? "push" : null); if (l) out.get(l).push({ suite: s, item: null, why: w.text }); }
        }
    }
    return out;
}

// ---- routes
export function parseRoute(hash) {
    let h = decodeURIComponent((hash || "").replace(/^#/, ""));
    let query = {};
    const qi = h.indexOf("?");
    if (qi >= 0) { for (const kv of h.slice(qi + 1).split("&")) { const [k, v] = kv.split("="); if (k) query[k] = v ?? ""; } h = h.slice(0, qi); }
    const [kind, ...rest] = h.split("/");
    const r = { kind: kind || "", query };
    if (kind === "suite") r.id = rest.join("/");
    else if (kind === "group" || kind === "fn" || kind === "cmd") r.name = rest.join("/");
    else if (kind === "ci") { r.wf = rest[0]; r.job = stripMatrix(rest.slice(1).join("/")); r.rawJob = rest.slice(1).join("/"); }
    else if (kind === "item") { r.suite = rest[0]; r.id = rest.slice(1).join("/"); }
    return r;
}
export function routeFor(sel) {
    if (!sel) return "";
    if (sel.kind === "when") return "#when";
    if (sel.kind === "fn") return "#fn/" + sel.fn.name;
    if (sel.kind === "suite") return "#suite/" + sel.suite.id;
    const it = sel.item;
    if (it.kind === "group") return "#group/" + it.name;
    if (it.kind === "job") return "#ci/" + wfOf(it._suite) + "/" + encodeURIComponent(it.jobId);
    if (it.kind === "shortcut" || it.kind === "tool") return "#cmd/" + it.name;
    return "#item/" + it._suite.id + "/" + it.id;
}
export function resolveRoute(ix, r) {
    if (r.kind === "when") return { kind: "when" };
    if (r.kind === "suite") { const s = ix.suites.get(r.id); return s ? { kind: "suite", suite: s } : null; }
    if (r.kind === "group") { const g = ix.groups.get(r.name) || ix.cmds.get(r.name); return g ? { kind: "item", item: g } : null; }
    if (r.kind === "cmd") { const g = ix.cmds.get(r.name) || ix.groups.get(r.name); return g ? { kind: "item", item: g } : null; }
    if (r.kind === "fn") { const f = (ix.m.functions || {})[r.name]; return f ? { kind: "fn", fn: f } : null; }
    if (r.kind === "ci") {
        const j = ix.jobs.get(r.wf + "/" + r.job) || ix.jobs.get(r.wf + "/" + r.rawJob);
        if (j) return { kind: "item", item: j, run: r.query.run };
        const s = ix.suites.get("ci:" + r.wf);
        return s ? { kind: "suite", suite: s } : null;
    }
    if (r.kind === "item") { const it = ix.items.get(r.suite + "|" + r.id); return it ? { kind: "item", item: it } : null; }
    return null;
}
export const selKey = (sel) => (!sel ? "" : sel.kind === "fn" ? "fn:" + sel.fn.name : sel.kind === "suite" ? "suite:" + sel.suite.id : sel.kind === "item" ? itemKey(sel.item) : sel.kind);
// the same selection in a new model (after a save / a poll)
export function reselect(ix, sel) {
    if (!sel) return null;
    if (sel.kind === "when") return sel;
    if (sel.kind === "fn") { const f = ix.m.functions[sel.fn.name]; return f ? { kind: "fn", fn: f } : null; }
    if (sel.kind === "suite") { const s = ix.suites.get(sel.suite.id); return s ? { kind: "suite", suite: s } : null; }
    const it = ix.items.get(itemKey(sel.item));
    if (it) return { ...sel, item: it };
    if (sel.item.kind === "group") { const g = ix.groups.get(sel.item.name); if (g) return { ...sel, item: g }; }   // (moved par <-> alone)
    return null;
}

// a file's language for the highlighter
export const langOf = (path) => (/\.(ya?ml)$/i.test(path) ? "yaml" : /\.(sh)$/i.test(path) || /githooks\//.test(path) ? "sh" : /CMakeLists|\.cmake$/.test(path) ? "cmake" : "cpp");
