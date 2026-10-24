// The "When" view (#when): one lane per moment, in time order (edit -> pre-commit -> before commit -> release_check.sh
// -> push -> testing -> [final] -> v* tag, then PRs and schedules). Which suite / job sits in which lane comes from
// model.suites[].when and each job's if / needs (model.js whenLanes); only the lane order is fixed.

import { el, clip, secs } from "./util.js";
import { LANES, whenLanes, ciFor, worstConclusion, conclClass, itemKey, sharedPorts } from "./model.js";

export function renderWhen(root, app) {
    const ix = app.ix, m = ix.m;
    root.textContent = "";
    const wrap = el("div", "when");
    const head = el("div", "when-head");
    head.append(el("h2", null, "When the tests run"), el("span", "muted", "top to bottom in time: each lane is a moment; click anything to open it"));
    const legend = el("span", "when-legend small");
    for (const [c, t] of [["ok", "passed"], ["bad", "failed"], ["skip", "skipped"], ["run", "running / other"]]) { const s = el("span", "lg"); s.append(el("i", "dot cc-" + c), document.createTextNode(t)); legend.append(s); }
    head.append(legend);
    wrap.append(head);
    const lanes = whenLanes(m);
    let chartLane = null;   // (quark_tests runs in several lanes: the chart once, a pointer to it after that)
    for (const L of LANES) {
        const entries = lanes.get(L.id) || [];
        if (!entries.length && (L.id === "pr" || L.id === "sched")) continue;
        const sec = el("section", "lane lane-" + L.id);
        sec.id = "lane-" + L.id;
        const lab = el("div", "lane-label");
        lab.append(el("b", null, L.title), el("span", "muted small", L.sub));
        const body = el("div", "lane-body");
        if (L.id === "edit") body.append(el("div", "muted", "Nothing runs on a save. The tests editor writes the file through the server (a backup is kept); the lanes below run it."));
        else if (!entries.length) body.append(el("div", "muted small", "nothing runs here"));
        const suites = new Map();
        for (const e of entries) { if (!e.suite) continue; if (!suites.has(e.suite.id)) suites.set(e.suite.id, { suite: e.suite, items: [], whys: [] }); const s = suites.get(e.suite.id); if (e.item) s.items.push(e); else s.whys.push(e.why); }
        for (const { suite, items, whys } of suites.values()) {
            if (suite.id === "quark_tests" && !chartLane) { body.append(gantt(app, suite)); chartLane = L; }
            else if (suite.id === "quark_tests") body.append(again(app, suite, chartLane, whys));
            else if (suite.kind === "ci") body.append(ciLane(app, suite, items, L.id, whys));
            else body.append(stepsCard(app, suite, whys));
        }
        if (L.id === "testing" || L.id === "tag" || L.id === "final") {
            const n = el("div", "lane-note muted small");
            n.textContent = L.id === "final" ? "beside the normal build of that push (or alone, on a manual run with final ticked); nothing waits for it" : "plus every job in “push to a branch” (a push to testing / a tag is a push too)";
            body.append(n);
        }
        sec.append(lab, body);
        wrap.append(sec);
    }
    root.append(wrap);
}

function card(title, cls, onclick) {
    const c = el("div", "wcard " + (cls || ""));
    const h = el("div", "wcard-h");
    const t = el("span", "wcard-t" + (onclick ? " link" : ""), title);
    if (onclick) t.onclick = onclick;
    h.append(t);
    c.append(h);
    return { c, h };
}

function stepsCard(app, suite, whys) {
    const { c, h } = card(suite.title, "w-" + suite.kind, () => app.select({ kind: "suite", suite }));
    h.append(el("span", "muted small", whys.filter(Boolean).join("; ")));
    const chain = el("div", "wchain");
    (suite.items || []).forEach((s, i) => {
        if (i) chain.append(el("span", "warrow", "→"));
        const b = el("span", "wchip link", clip(s.name, 48));
        b.title = s.text || s.name;
        b.onclick = () => app.select({ kind: "item", item: s });
        chain.append(b);
    });
    c.append(chain);
    return c;
}

// a later lane that runs quark_tests again (release_check.sh): a line pointing at the chart
function again(app, suite, chartLane, whys) {
    const { c, h } = card("quark_tests", "w-local", () => app.select({ kind: "suite", suite }));
    const a = el("span", "link small", "the same groups as the chart in “" + chartLane.title + "” ↑");
    a.onclick = () => document.getElementById("lane-" + chartLane.id).scrollIntoView({ block: "start", behavior: "smooth" });
    h.append(el("span", "muted small", whys.filter(Boolean).join("; ") + " · "), a);
    return c;
}

// quark_tests: the par groups side by side in time (bar = its timing), then the alone groups one after another. A
// group with no time yet (never run on this machine) gets the median time, hatched, so its name still shows.
function gantt(app, suite) {
    const ix = app.ix;
    const groups = (suite.items || []).filter((g) => g.kind === "group");
    const par = groups.filter((g) => g.list === "par").sort((a, b) => a.order - b.order);
    const alone = groups.filter((g) => g.list !== "par").sort((a, b) => a.order - b.order);
    const has = (g) => !!(g.timing && g.timing.sec != null);
    const known = groups.filter(has).map((g) => g.timing.sec).sort((a, b) => a - b);
    const guess = known.length ? Math.max(1, known[known.length >> 1]) : 10;
    const t = (g) => (has(g) ? Math.max(g.timing.sec, 0.5) : guess);   // (a 0 s group: a sliver, not "no time")
    const missing = groups.filter((g) => !has(g)).length;
    const parWall = Math.max(1, ...par.map(t));
    const total = parWall + alone.reduce((s, g) => s + Math.max(t(g), 1), 0);
    const { c, h } = card("quark_tests", "w-local", () => app.select({ kind: "suite", suite }));
    h.append(el("span", "muted small", `${par.length} par groups at once, then ${alone.length} alone · about ${secs(total)} with enough cores · ` + ((suite.when || [])[0] || {}).text));
    if (missing) {
        const n = el("span", "warn small", `${missing} group${missing > 1 ? "s have" : " has"} no time yet (hatched, drawn at the median): `);
        const go = el("span", "link", "run the whole suite once");
        go.onclick = () => app.select({ kind: "suite", suite });
        n.append(go);
        h.append(n);
    }
    const shared = sharedPorts(ix);
    const g = el("div", "gantt");
    const rows = Math.max(par.length, 1);
    g.style.height = rows * 15 + 24 + "px";
    const pct = (x) => (100 * x) / total + "%";
    const bar = (grp, left, top, w, cls) => {
        const lr = grp.lastRun;
        const b = el("div", "gbar " + cls + (shared.has(grp.name) || (ix.itemLint.get(itemKey(grp)) || []).length ? " warn" : "") + (has(grp) ? "" : " notime") + (lr && lr.status === "failed" ? " failed" : ""));
        b.style.left = pct(left); b.style.width = `calc(${pct(w)} - 2px)`; b.style.top = top + "px";
        b.textContent = grp.name;
        b.title = `${grp.name}: ${grp.timing ? secs(grp.timing.sec) + " (" + grp.timing.source + ")" : "no time yet"}${lr ? "\nlast local run: " + lr.status + (lr.fails ? " (" + lr.fails + " failures)" : "") : ""}${grp.flaky ? "\nflaky: " + grp.flaky : ""}${shared.has(grp.name) ? "\nshares a TP() port" : ""}`;
        b.onclick = () => app.select({ kind: "item", item: grp });
        g.append(b);
    };
    par.forEach((grp, i) => bar(grp, 0, i * 15, Math.max(t(grp), total / 200), "par"));
    let x = parWall;
    for (const grp of alone) { const w = Math.max(t(grp), 1); bar(grp, x, 0, w, "alone"); x += w; }
    const sep = el("div", "gsep"); sep.style.left = pct(parWall); g.append(sep);
    const ax = el("div", "gaxis");
    ax.append(el("span", null, "0"), el("span", "gmid", "par done ~" + secs(parWall)), el("span", null, secs(total)));
    ax.querySelector(".gmid").style.left = pct(parWall);
    g.append(ax);
    c.append(g);
    const cmds = (suite.items || []).filter((x) => x.kind === "shortcut" || x.kind === "tool");
    if (cmds.length) c.append(el("div", "muted small", `${cmds.length} shortcut and tool commands (quark_tests <name>) run only by hand.`));
    return c;
}

// CI jobs of one lane, in columns by needs depth (within the workflow)
function ciLane(app, suite, entries, laneId, whys) {
    const { c, h } = card(suite.title, "w-ci", () => app.select({ kind: "suite", suite }));
    const trig = (suite.when || []).map((w) => w.text).filter(Boolean);
    h.append(el("span", "muted small", laneId === "push" || laneId === "testing" || laneId === "tag" || laneId === "final" ? "" : trig.join("; ")));
    if (whys.length) c.append(el("div", "muted small", whys.join("; ")));
    const byId = new Map((suite.items || []).map((j) => [j.jobId, j]));
    const depth = new Map();
    const dep = (j, seen = new Set()) => { if (depth.has(j.jobId)) return depth.get(j.jobId); if (seen.has(j.jobId)) return 0; seen.add(j.jobId); const d = Math.max(-1, ...(j.needs || []).map((n) => (byId.get(n) ? dep(byId.get(n), seen) : -1))) + 1; depth.set(j.jobId, d); return d; };
    const cols = [];
    for (const e of entries) { const d = dep(e.item); (cols[d] = cols[d] || []).push(e); }
    const flow = el("div", "wflow");
    cols.forEach((col, i) => {
        if (!col) return;
        if (flow.children.length) flow.append(el("span", "warrow", "→"));
        const cc = el("div", "wcol");
        for (const e of col) {
            const j = e.item;
            const res = ciFor(app.ci, j), concl = worstConclusion(res);
            const b = el("div", "wjob link cc-" + (conclClass(concl) || "none"));
            b.append(el("b", null, j.jobId));
            const why = j.ifText || (j.if ? clip(j.if, 60) : "");
            if (why && laneId !== "push") b.append(el("span", "small muted", clip(why, 70)));
            if ((j.needs || []).length) b.append(el("span", "small muted", "needs " + j.needs.join(", ")));
            if (concl) b.append(el("span", "small", "last: " + concl));
            b.title = (j.if ? "if: " + j.if + "\n" : "") + res.map((r) => r.name + ": " + r.conclusion).join("\n");
            b.onclick = () => app.select({ kind: "item", item: j });
            cc.append(b);
        }
        flow.append(cc);
    });
    c.append(flow);
    return c;
}
