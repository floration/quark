// The tests editor page: the tree, the three views (Code / Graph / Blocks) and the When view, the details panel,
// saving (rules, 409 conflicts), runs, routing. Data contract: tools/tests_editor/API.md.

import { $, el, esc, clip, dallas, secs, toast, modal, unified, diffHtml } from "./util.js";
import { api } from "./api.js";
import { indexModel, parseRoute, routeFor, resolveRoute, reselect, selKey, itemKey, lintFor, ciFor, worstConclusion, conclClass, wfOf, sharedPorts } from "./model.js";
import { ruleHits } from "./patch.js";
import { CodeView } from "./code.js";
import { GraphView } from "./graph.js";
import { BlocksView } from "./blocks.js";
import { renderWhen } from "./when.js";

const POLL_MODEL_MS = 15000, POLL_CI_MS = 60000, POLL_RUN_MS = 500;

class App {
    constructor() {
        this.files = new Map();     // path -> {path, disk, text, sha, mtime, scrollTop}
        this.ix = null; this.ci = null; this.runs = [];
        this.sel = null; this.view = "code"; this.run = null;
        this.collapsed = new Set(["fns", "quark_tests:shortcut", "quark_tests:tool"]);
        try { const v = localStorage.getItem("te.view"); if (v) this.view = v; const c = localStorage.getItem("te.collapsed"); if (c) this.collapsed = new Set(JSON.parse(c)); } catch { }
        const embed = new URLSearchParams(location.search).get("embed") === "1" || /[?&]embed=1/.test(location.hash);
        if (embed) document.body.classList.add("embed");
        this.code = new CodeView($("vCode"), this);
        this.graph = new GraphView($("vGraph"), this);
        this.blocks = new BlocksView($("vBlocks"), this);
        for (const b of document.querySelectorAll("#viewTabs [data-view]")) b.onclick = () => this.setView(b.dataset.view);
        $("whenBtn").onclick = () => this.select({ kind: "when" });
        $("allDiffBtn").onclick = () => this.showDiff(null);
        $("search").oninput = () => this._filterTree();
        $("search").addEventListener("keydown", (e) => { if (e.key === "Enter") { const r = $("tree").querySelector(".trow:not([hidden])"); if (r) r.click(); } if (e.key === "Escape") { e.target.value = ""; this._filterTree(); } });
        $("dockToggle").onclick = () => this.toggleDock();
        $("runStop").onclick = () => this.stopRun();
        $("runFails").onclick = () => this.nextFailure();
        this._panels();
        window.addEventListener("hashchange", () => this.route());
        window.addEventListener("keydown", (e) => {
            if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s" && document.activeElement !== this.code.ta) {
                e.preventDefault();
                if (this.view === "code" && this.code.path) this.code.save();
                else { const b = this.blocks.draftBar.querySelector("button.primary"); if (b && !b.disabled) b.click(); }
            }
            const typing = /INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName);
            if (e.key === "/" && !typing) { e.preventDefault(); $("search").focus(); }
            if ((e.key === "[" || e.key === "]") && !typing && !e.ctrlKey && !e.metaKey && !e.altKey) { e.preventDefault(); this.togglePanel(e.key === "[" ? "l" : "r"); }
        });
        window.addEventListener("beforeunload", (e) => { if ([...this.files.values()].some((f) => f.text !== f.disk) || this.blocks.hasDrafts()) { e.preventDefault(); e.returnValue = ""; } });
        window.addEventListener("focus", () => { this.pollModel(); this.checkDisk(); });
        this.start();
    }

    async start() {
        const r = await api.model();
        if (!r.ok) { $("tree").innerHTML = `<div class="err pad">${esc((r.data && r.data.error) || "no model")}</div>`; $("main").classList.add("nomodel"); return; }
        this.setModel(r.data, true);
        this.route();
        api.ci().then((c) => { if (c.ok) { this.ci = c.data; this._afterCi(); } });
        this.loadRuns();
        setInterval(() => { if (!document.hidden) this.pollModel(); }, POLL_MODEL_MS);
        setInterval(() => { if (!document.hidden) api.ci().then((c) => { if (c.ok) { this.ci = c.data; this._afterCi(); } }); }, POLL_CI_MS);
        setInterval(() => { if (!document.hidden) this.checkDisk(); }, 30000);
    }

    // ---------------------------------------------------------------- model
    setModel(m, first = false) {
        const sig = JSON.stringify([m.generatedAt, m.repo && m.repo.sha, (m.repo && m.repo.dirty || []).length, (m.suites || []).map((s) => (s.items || []).length), (m.lint || []).length, Object.keys(m.functions || {}).length]);
        const same = this._mtext && this._mtext === JSON.stringify(m);
        if (same) return false;
        this._mtext = JSON.stringify(m);
        this.ix = indexModel(m);
        this._header();
        this._tree();
        if (!first && this.sel) {
            const s = reselect(this.ix, this.sel);
            this.sel = s;
            if (s) this._showSel(false); else this.select({ kind: "when" });
        }
        this._sig = sig;
        return true;
    }
    async pollModel() {
        if (this._polling) return;
        this._polling = true;
        const r = await api.model();
        this._polling = false;
        if (r.ok) this.setModel(r.data);
    }
    _header() {
        const m = this.ix.m, rp = m.repo || {};
        const f = $("facts");
        f.textContent = "";
        const add = (t, cls, title) => { const s = el("span", cls || "fact", t); if (title) s.title = title; f.append(s); return s; };
        add(rp.branch || "?", "fact mono", rp.path || "");
        add((rp.sha || "").slice(0, 7), "fact mono muted");
        if (rp.version) add("v" + rp.version, "fact muted");
        const dirty = (rp.dirty || []).length;
        const d = add(dirty ? dirty + " changed file" + (dirty > 1 ? "s" : "") : "clean", "fact " + (dirty ? "warn" : "muted"), (rp.dirty || []).join("\n"));
        if (dirty) { d.classList.add("link"); d.onclick = () => this.showDiff(null); }
        const groups = this.ix.groups.size, jobs = [...this.ix.items.values()].filter((x) => x.kind === "job").length;
        add(`${groups} groups · ${jobs} CI jobs · ${Object.keys(m.functions || {}).length} functions`, "fact muted");
        const lint = (m.lint || []).length;
        if (lint) add("⚠ " + lint + " lint", "fact warn", (m.lint || []).map((l) => `${l.file}:${l.line} ${l.text}`).join("\n"));
        add("model " + dallas(m.generatedAt, true), "fact muted small");
    }

    // ---------------------------------------------------------------- tree
    _treeRows() {
        const ix = this.ix, out = [];   // [{sec, sub, key, label, meta, sel, lint, dot}]
        for (const s of ix.m.suites || []) {
            const secId = s.id;
            out.push({ head: true, sec: secId, label: s.title, kind: s.kind, sel: { kind: "suite", suite: s }, key: "suite:" + s.id, lint: lintFor(ix, { kind: "suite", suite: s }).length });
            const subs = s.id === "quark_tests" ? [["par", "par: in parallel", (it) => it.kind === "group" && it.list === "par"], ["alone", "alone: one by one", (it) => it.kind === "group" && it.list !== "par"], ["shortcut", "shortcut commands", (it) => it.kind === "shortcut"], ["tool", "tools", (it) => it.kind === "tool"]] : [[null, null, () => true]];
            for (const [sub, title, pick] of subs) {
                const items = (s.items || []).filter(pick);
                if (!items.length) continue;
                if (sub) {
                    const tsec = (items.reduce((a, g) => (sub === "par" ? Math.max(a, (g.timing && g.timing.sec) || 0) : a + ((g.timing && g.timing.sec) || 0)), 0));
                    const untimed = items.some((g) => !g.timing || g.timing.sec == null);   // (then it's a floor, not a time)
                    out.push({ subhead: true, sec: secId, sub: secId + ":" + sub, label: title, meta: items.length + (tsec >= 1 && (sub === "par" || sub === "alone") ? (untimed ? " · ≥" : " · ~") + secs(tsec) : "") });
                }
                if (sub === "par" || sub === "alone") items.sort((a, b) => a.order - b.order);
                for (const it of items) out.push({ sec: secId, sub: sub ? secId + ":" + sub : null, key: itemKey(it), label: it.kind === "job" ? it.jobId : it.name, meta: this._rowMeta(it), sel: { kind: "item", item: it }, lint: (ix.itemLint.get(itemKey(it)) || []).length, item: it });
            }
        }
        const fns = Object.values(ix.m.functions || {}).sort((a, b) => a.name.localeCompare(b.name));
        out.push({ head: true, sec: "fns", label: "test functions", kind: "fn", meta: String(fns.length) });
        for (const f of fns) out.push({ sec: "fns", key: "fn:" + f.name, label: f.name, meta: (f.checks || []).length + "", sel: { kind: "fn", fn: f }, lint: (ix.fnLint.get(f.name) || []).length, fn: f });
        return out;
    }
    _rowMeta(it) {
        if (it.kind === "group") return it.timing ? secs(it.timing.sec) : "";
        if (it.kind === "job") return (it.steps || []).length + " steps";
        if (it.kind === "shortcut" || it.kind === "tool") return (it.calls || []).length ? (it.calls || []).length + " calls" : "";
        return "";
    }
    _tree() {
        const rows = this._treeRows();
        const sig = rows.map((r) => r.key || r.sub || r.sec).join("|");
        const tree = $("tree");
        if (sig === this._treeSig && this._rowEls) {   // same rows: update the texts in place
            rows.forEach((r) => { const e = this._rowEls.get(r.key || "h:" + (r.sub || r.sec)); if (e) this._fillRow(e, r); });
            this._ciDots();
            this._markSel();
            return;
        }
        this._treeSig = sig;
        this._rowEls = new Map();
        const frag = document.createDocumentFragment();
        const fnRows = [];
        for (const r of rows) {
            if (r.sec === "fns" && !r.head) { fnRows.push(r); continue; }
            frag.append(this._mkRow(r));
        }
        tree.textContent = "";
        tree.append(frag);
        // the functions list is long: built on first open (or a search)
        this._fnRows = fnRows; this._fnBuilt = false;
        if (!this.collapsed.has("fns")) this._buildFns();
        this._applyCollapse();
        this._ciDots();
        this._markSel();
        this._filterTree();
    }
    _buildFns() {
        if (this._fnBuilt) return;
        this._fnBuilt = true;
        const frag = document.createDocumentFragment();
        for (const r of this._fnRows) frag.append(this._mkRow(r));
        $("tree").append(frag);
        this._applyCollapse();
        this._markSel();
    }
    _mkRow(r) {
        const e = el("div", r.head ? "thead" : r.subhead ? "tsub" : "trow");
        e.dataset.sec = r.sec;
        if (r.sub) e.dataset.sub = r.sub;
        if (r.key) e.dataset.key = r.key;
        if (r.head || r.subhead) {
            const tw = el("span", "ttw", "▾");
            tw.onclick = (ev) => { ev.stopPropagation(); this._toggle(r.subhead ? r.sub : r.sec); };
            e.append(tw);
            if (r.head) e.append(el("span", "tkind k-" + r.kind, { local: "local", ci: "CI", script: "sh", hook: "hook", ctest: "ctest", fn: "fn" }[r.kind] || r.kind));
        } else e.append(el("i", "tdot"));
        e.append(el("span", "tname"), el("span", "tmeta"), el("span", "tbadge"));
        if (r.sel) e.onclick = () => this.select(r.sel);
        else if (r.head || r.subhead) e.onclick = () => this._toggle(r.subhead ? r.sub : r.sec);
        this._rowEls.set(r.key || "h:" + (r.sub || r.sec), e);
        this._fillRow(e, r);
        return e;
    }
    _fillRow(e, r) {
        e.querySelector(".tname").textContent = r.label;
        e.querySelector(".tmeta").textContent = r.meta || "";
        const b = e.querySelector(".tbadge");
        b.textContent = r.lint ? "⚠" + r.lint : "";
        b.title = r.lint ? r.lint + " lint finding(s)" : "";
        if (r.item && r.item.flaky) e.title = "flaky: " + r.item.flaky;
        e._row = r;
    }
    _ciDots() {
        if (!this._rowEls) return;
        for (const [k, e] of this._rowEls) {
            const r = e._row;
            if (r && r.item && r.item.kind === "group") {   // (a group: its last local run)
                const lr = r.item.lastRun, d = e.querySelector(".tdot");
                d.className = "tdot cc-" + (!lr ? "none" : lr.status === "passed" ? "ok" : lr.status === "failed" ? "bad" : "skip");
                d.title = lr ? `last local run: ${lr.status}${lr.fails ? " (" + lr.fails + " failure" + (lr.fails > 1 ? "s" : "") + ")" : ""} · ${dallas(lr.at)}` : "not run on this machine yet";
                continue;
            }
            if (!r || !r.item || r.item.kind !== "job") continue;
            const c = worstConclusion(ciFor(this.ci, r.item));
            const d = e.querySelector(".tdot");
            d.className = "tdot cc-" + (conclClass(c) || "none");
            d.title = c ? "last CI: " + c : "";
        }
    }
    _toggle(id) {
        if (this.collapsed.has(id)) this.collapsed.delete(id); else this.collapsed.add(id);
        try { localStorage.setItem("te.collapsed", JSON.stringify([...this.collapsed])); } catch { }
        if (id === "fns") this._buildFns();
        this._applyCollapse();
    }
    _applyCollapse() {
        const searching = !!$("search").value.trim();
        for (const e of $("tree").children) {
            const sec = e.dataset.sec, sub = e.dataset.sub;
            if (e.classList.contains("thead")) { e.classList.toggle("closed", this.collapsed.has(sec) && !searching); continue; }
            const hidden = !searching && (this.collapsed.has(sec) || (e.classList.contains("trow") && sub && this.collapsed.has(sub)));
            e.classList.toggle("folded", hidden);
            if (e.classList.contains("tsub")) e.classList.toggle("closed", this.collapsed.has(sub) && !searching);
        }
    }
    _filterTree() {
        const q = $("search").value.trim().toLowerCase();
        if (q) this._buildFns();
        const tree = $("tree");
        const seen = new Set(), subSeen = new Set();
        for (const e of tree.querySelectorAll(".trow")) {
            const r = e._row;
            const hit = !q || r.label.toLowerCase().includes(q) || (r.item && r.item.calls && r.item.calls.some((c) => c.fn.toLowerCase().includes(q))) || (r.item && r.item.kind === "job" && (r.item.steps || []).some((s) => (s.name || "").toLowerCase().includes(q)));
            e.hidden = !hit;
            if (hit) { seen.add(e.dataset.sec); if (e.dataset.sub) subSeen.add(e.dataset.sub); }
        }
        for (const e of tree.querySelectorAll(".thead")) e.hidden = !!q && !seen.has(e.dataset.sec) && !(e._row && e._row.label.toLowerCase().includes(q));
        for (const e of tree.querySelectorAll(".tsub")) e.hidden = !!q && !subSeen.has(e.dataset.sub);
        this._applyCollapse();
    }
    _markSel() {
        const k = this.sel ? selKey(this.sel) : "";
        const tree = $("tree");
        const old = tree.querySelector(".sel");
        if (old && old.dataset.key !== k) old.classList.remove("sel");
        if (!k) return;
        let e = this._rowEls && this._rowEls.get(k);
        if (!e && k.startsWith("fn:")) { this._buildFns(); e = this._rowEls.get(k); }
        if (e) {
            e.classList.add("sel");
            if (e.classList.contains("folded")) { const sec = e.dataset.sec, sub = e.dataset.sub; this.collapsed.delete(sec); if (sub) this.collapsed.delete(sub); this._applyCollapse(); }
            const r = e.getBoundingClientRect(), tr = tree.getBoundingClientRect();
            if (r.top < tr.top || r.bottom > tr.bottom) e.scrollIntoView({ block: "nearest" });
        }
    }

    // ---------------------------------------------------------------- selection, routing
    route() {
        if (!this.ix) return;
        const r = parseRoute(location.hash);
        if (r.query.view && ["code", "graph", "blocks"].includes(r.query.view)) this.view = r.query.view;
        let s = r.kind ? resolveRoute(this.ix, r) : null;
        if (!s) s = { kind: "when" };
        if (r.kind && !resolveRoute(this.ix, r) && r.kind !== "when") toast("nothing at " + location.hash + ": showing When", true);
        this.sel = s;
        this._showSel(true);
    }
    select(sel) {
        if (!sel) return;
        const h = routeFor(sel);
        this.sel = sel;
        if (location.hash !== h) { this._noRoute = true; history.pushState(null, "", h || "#"); }
        this._showSel(true);
    }
    _showSel(scroll) {
        const s = this.sel;
        this._markSel();
        const isWhen = s.kind === "when";
        document.body.classList.toggle("when-on", isWhen);
        $("whenBtn").classList.toggle("on", isWhen);
        if (isWhen) { renderWhen($("vWhen"), this); $("details").textContent = ""; this._detailsWhen(); return; }
        this._showView(scroll);
        this._details();
    }
    setView(v) {
        this.view = v;
        try { localStorage.setItem("te.view", v); } catch { }
        if (this.sel && this.sel.kind !== "when") this._showView(true);
    }
    _showView(scroll) {
        for (const b of document.querySelectorAll("#viewTabs [data-view]")) b.classList.toggle("on", b.dataset.view === this.view);
        for (const id of ["vCode", "vGraph", "vBlocks"]) $(id).classList.toggle("on", id === "v" + this.view[0].toUpperCase() + this.view.slice(1));
        const s = this.sel;
        const where = this._where(s);
        $("selTitle").textContent = this._title(s);
        if (this.view === "code") { if (where) this.code.open(where.file, where.line, where.endLine); this._codeMarks(); }
        else if (this.view === "graph") this.graph.show(s);
        else this.blocks.show(s);
    }
    _where(s) {
        if (!s) return null;
        if (s.kind === "fn") return { file: s.fn.file, line: s.fn.line, endLine: s.fn.endLine };
        if (s.kind === "suite") return { file: s.suite.file, line: s.suite.line, endLine: s.suite.line };
        const it = s.item;
        return { file: it.file || it._suite.file, line: it.line, endLine: it.endLine || it.line };
    }
    _title(s) {
        if (s.kind === "fn") return "function " + s.fn.name;
        if (s.kind === "suite") return s.suite.title;
        const it = s.item;
        return { group: "group ", job: "job ", shortcut: "command ", tool: "tool ", step: "" }[it.kind] + (it.jobId || it.name) + "  ·  " + it._suite.title;
    }
    _codeMarks() {
        const marks = new Map();
        for (const l of (this.ix.fileLint.get(this.code.path) || [])) marks.set(l.line, l.level === "error" ? "ge" : "gw");
        this.code.setMarks(marks);
    }
    jumpToCode(file, line) { this.view = "code"; for (const b of document.querySelectorAll("#viewTabs [data-view]")) b.classList.toggle("on", b.dataset.view === "code"); for (const id of ["vCode", "vGraph", "vBlocks"]) $(id).classList.toggle("on", id === "vCode"); this.code.open(file, line, line).then(() => this._codeMarks()); }

    // ---------------------------------------------------------------- details panel
    _details() {
        const s = this.sel, ix = this.ix, d = $("details");
        d.textContent = "";
        const sec = (title) => { const h = el("h3", null, title); d.append(h); return h; };
        const kv = (k, v, cls) => { const r = el("div", "kv"); r.append(el("span", "k", k)); const vv = el("span", "v " + (cls || "")); if (v instanceof Node) vv.append(v); else vv.textContent = v; r.append(vv); d.append(r); return vv; };
        const where = this._where(s);
        const head = el("div", "dhead");
        head.append(el("div", "dtitle", this._title(s).split("  ·  ")[0]));
        if (where) { const a = el("span", "link mono small", where.file + ":" + where.line + (where.endLine && where.endLine !== where.line ? "-" + where.endLine : "")); a.onclick = () => this.jumpToCode(where.file, where.line); head.append(a); }
        d.append(head);
        const suite = s.kind === "suite" ? s.suite : s.kind === "item" ? s.item._suite : ix.suites.get("quark_tests");
        const it = s.kind === "item" ? s.item : null;
        // run (first: it stays above the fold on a short screen)
        this._runSection(d, s);
        if (it && it.kind === "group" && it.lastRun) {
            const lr = it.lastRun;
            sec("Last local run");
            const r = el("div", "dline");
            r.append(el("i", "dot cc-" + (lr.status === "passed" ? "ok" : "bad")), el("b", null, " " + lr.status), el("span", "muted small", (lr.fails ? " · " + lr.fails + " failure" + (lr.fails > 1 ? "s" : "") : "") + " · " + dallas(lr.at) + (lr.source && /^imported/.test(lr.source) ? " · imported log" : "")));
            d.append(r);
            for (const f of lr.failLines || []) d.append(this._failRow(f));
        }
        // when
        sec("When it runs");
        if (s.kind === "fn") {
            const ub = s.fn.usedBy || [], via = s.fn.runsVia || [];
            const itemOf = (u) => [...ix.items.values()].find((x) => x.id === u && x._suite.id === "quark_tests");
            const label = (u) => u.replace(/^group:/, "group ").replace(/^cmd:/, "quark_tests ");
            const isTest = (s.fn.checks || []).length || /\bvoid$/.test(s.fn.ret || "");
            if (!ub.length && !via.length) d.append(el("div", isTest ? "warn small" : "muted small", isTest ? "No group or command calls it, directly or through another function: it never runs." : "No group or command reaches it (a helper, not a test)."));
            for (const u of ub) {
                const g = itemOf(u);
                const r = el("div", "dline");
                const a = el("span", "link", label(u));
                if (g) a.onclick = () => this.select({ kind: "item", item: g });
                r.append(a);
                if (g && g.kind === "group") r.append(el("span", "muted small", g.list === "par" ? " · par, before every commit" : " · alone, before every commit"));
                d.append(r);
            }
            if (via.length) d.append(el("div", "dline small muted", ub.length ? "and through other functions:" : "through other functions:"));
            for (const v of via) {
                const g = itemOf(v.item);
                const r = el("div", "dline small");
                const a = el("span", "link", label(v.item));
                if (g) a.onclick = () => this.select({ kind: "item", item: g });
                r.append(a);
                for (const p of v.path) {
                    r.append(document.createTextNode(" → "));
                    const f = (ix.m.functions || {})[p];
                    const b = el("span", f ? "link mono" : "mono", p);
                    if (f) b.onclick = () => this.select({ kind: "fn", fn: f });
                    r.append(b);
                }
                r.append(document.createTextNode(" → "), el("span", "mono", s.fn.name));
                d.append(r);
            }
        } else if (it && (it.kind === "shortcut" || it.kind === "tool")) {
            const r = el("div", "dline");
            r.append(el("span", "chip c-when", "manual"), document.createTextNode(" only by hand: quark_tests " + it.name + this._argsHelp(it)));
            d.append(r);
            if ((it.aliases || []).length) d.append(el("div", "dline small muted", "the same code also answers: " + it.aliases.map((a) => "quark_tests " + a).join(", ")));
            if (it.shadowsGroup && it.argsSpec && it.argsSpec.min > 0) d.append(el("div", "dline small warn", `Without ${this._argsHelp(it).trim() || "its arguments"}, quark_tests ${it.name} runs the group ${it.name} instead.`));
        } else {
            for (const w of (suite && suite.when) || []) { const r = el("div", "dline"); r.append(el("span", "chip c-when", w.trigger), document.createTextNode(" " + w.text)); d.append(r); }
            if (it && it.kind === "group") d.append(el("div", "dline small", it.list === "par" ? `in the par lane: at the same time as the other ${[...ix.groups.values()].filter((g) => g.list === "par").length - 1} par groups (one process each)` : `in the alone lane: after every par group, one at a time (position ${it.order + 1})`));
            if (it && it.kind === "job") {
                d.append(el("div", "dline", it.ifText || (it.if ? "if " + it.if : "on every run of the workflow")));
                if (it.if) { const c = el("div", "dline mono small muted", "if: " + it.if); d.append(c); }
                if ((it.needs || []).length) { const r = el("div", "dline small"); r.append(document.createTextNode("after ")); it.needs.forEach((n, i) => { if (i) r.append(document.createTextNode(", ")); const j = ix.jobs.get(wfOf(it._suite) + "/" + n); const a = el("span", j ? "link" : "err", n); if (j) a.onclick = () => this.select({ kind: "item", item: j }); r.append(a); }); r.append(document.createTextNode(" succeed")); d.append(r); }
            }
        }
        // how
        if (it || s.kind === "suite") {
            sec("How");
            if (it && it.run) { const cmdText = it.run.cmd + this._argsHelp(it); const c = el("code", "cmd", cmdText); const cp = el("button", "mini", "copy"); cp.onclick = () => navigator.clipboard && navigator.clipboard.writeText(cmdText).then(() => toast("copied")); const w = el("div", "cmdline"); w.append(c, cp); d.append(w); if (it.kind === "group") kv("parallel", it.run.parallel ? "yes (par)" : "no (alone)"); }
            if (it && it.argsSpec && it.argsSpec.hint) kv("args", it.argsSpec.hint, "small");
            if (it && it.kind === "job") {
                kv("runs on", it.runsOn || "?", "mono");
                if (it.timeoutMin) kv("timeout", it.timeoutMin + " min");
                if (it.matrix) kv("matrix", Object.entries(it.matrix).map(([k, v]) => k + ": " + [].concat(v).join(", ")).join("; "), "mono small");
                kv("steps", (it.steps || []).length + "");
            }
            if (it && it.kind === "step" && it.text) { const p = el("pre", "dpre mono small", it.text); d.append(p); }
            if (s.kind === "suite") kv("items", (suite.items || []).length + "");
        }
        if (s.kind === "fn") {
            sec("How");
            kv("signature", s.fn.name + "(" + (s.fn.args || "") + ")", "mono small");
            kv("checks", (s.fn.checks || []).length + "");
            kv("calls", (s.fn.calls || []).join(", ") || "none", "small");
            if (s.fn.doc) d.append(el("div", "dline small muted", s.fn.doc));
        }
        // timing, flaky
        if (it && (it.timing || it.flaky)) {
            sec("Timing");
            if (it.timing) kv("time", secs(it.timing.sec), "");
            if (it.timing) kv("from", it.timing.source, "small muted");
            if (it.flaky) kv("flaky", it.flaky, "warn small");
        }
        if (it && it.kind === "group") {
            const sp = sharedPorts(ix).get(it.name);
            if (sp) d.append(el("div", "dline warn small", "⚠ shares " + sp.map((x) => "TP(" + x.port + ") with " + x.with.join(", ")).join("; ")));
        }
        // CI
        if (it && it.kind === "job" || s.kind === "suite" && s.suite.kind === "ci") {
            sec("Last CI");
            this._ciBox = el("div");
            d.append(this._ciBox);
            this._fillCi();
        }
        // lint
        const lint = lintFor(ix, s);
        sec("Lint" + (lint.length ? " (" + lint.length + ")" : ""));
        if (!lint.length) d.append(el("div", "muted small", "clean"));
        for (const l of lint) {
            const r = el("div", "lint " + l.level);
            r.append(el("span", "lvl", l.level), el("span", "ltext", l.text));
            const a = el("span", "link mono small", "line " + l.line);
            a.onclick = () => this.jumpToCode(l.file, l.line);
            r.append(a);
            d.append(r);
        }
        // rules
        const rules = ix.m.rules || [];
        if (rules.length) {
            const det = el("details", "rules");
            det.append(el("summary", null, "Rules (" + rules.length + ")"));
            for (const r of rules) { const x = el("div", "rule small"); x.append(el("b", null, r.id + " "), document.createTextNode(r.text), el("span", "muted", r.source ? "  (" + r.source + ")" : "")); det.append(x); }
            d.append(det);
        }
    }
    _detailsWhen() {
        const d = $("details"), ix = this.ix;
        d.append(el("div", "dtitle", "When"));
        d.append(el("div", "dline small muted", "Every lane, in time order. The lanes come from each suite's triggers and each CI job's if / needs; edit a job's If or Needs in its Blocks to move it."));
        const rules = ix.m.rules || [];
        if (rules.length) { d.append(el("h3", null, "Rules")); for (const r of rules) { const x = el("div", "rule small"); x.append(el("b", null, r.id + " "), document.createTextNode(r.text)); d.append(x); } }
        d.append(el("h3", null, "Recent local runs"));
        this._runsBox = el("div", "druns");
        d.append(this._runsBox);
        this._fillRuns();
    }
    _fillCi() {
        const box = this._ciBox, s = this.sel;
        if (!box || !s) return;
        box.textContent = "";
        if (!this.ci) { box.append(el("div", "muted small", "loading…")); return; }
        if (!this.ci.available) { box.append(el("div", "muted small", "CI status unavailable: the GitHub Manager (127.0.0.1:8767) isn't running.")); return; }
        const jobs = s.kind === "item" ? [s.item] : s.suite.items || [];
        let any = false;
        for (const j of jobs) for (const r of ciFor(this.ci, j)) {
            any = true;
            const row = el("div", "cirow");
            row.append(el("i", "dot cc-" + (conclClass(r.conclusion) || "none")), el("b", null, r.name), el("span", "small", r.conclusion || "running"));
            const meta = el("div", "small muted", [r.branch, dallas(r.at), r.durationSec ? secs(r.durationSec) : ""].filter(Boolean).join(" · "));
            row.append(meta);
            if ((r.failedSteps || []).length) row.append(el("div", "small err", "failed: " + r.failedSteps.join(", ")));
            if (r.url) { const a = el("a", "small", "run " + (r.runId || "") + " ↗"); a.href = r.url; a.target = "_blank"; a.rel = "noopener"; row.append(a); }
            if (s.run && String(r.runId) === String(s.run)) row.classList.add("hl");
            box.append(row);
        }
        if (s.run) box.prepend(el("div", "small muted", "opened from run " + s.run));
        if (!any) box.append(el("div", "muted small", "no run seen yet"));
    }
    _afterCi() { this._ciDots(); this._fillCi(); if (this.sel && this.sel.kind === "when") renderWhen($("vWhen"), this); else if (this.view === "graph") this.graph.render(); }
    // ---------------------------------------------------------------- the run section of the details panel
    _argsHelp(it) {
        const sp = it.argsSpec;
        if (!sp || sp.max === 0) return "";
        const ps = sp.params || [];
        const words = ps.map((p, i) => { const n = p.name || (sp.choices ? "NAME" : "arg" + (i + 1)); return i < sp.min ? n.toUpperCase() : "[" + n + (p.default ? "=" + p.default.replace(/^"|"$/g, "") : "") + "]"; });
        return words.length ? " " + words.join(" ") + (sp.max === null ? " …" : "") : sp.max === null ? " …" : "";
    }
    _runSection(d, s) {
        const busy = !!(this.run && this.run.status === "running");
        let first = true;
        const btn = (label, onclick, cls = "primary") => { const b = el("button", cls + " runbtn", label); b.disabled = busy; b.onclick = onclick; if (first) { b.id = "runBtn"; first = false; } return b; };
        const it = s.kind === "item" ? s.item : null;
        const su = s.kind === "suite" ? s.suite : it ? it._suite : null;
        let filter = null;
        const h = el("h3", null, "Run here");
        if (s.kind === "suite" && s.suite.id === "quark_tests") { d.append(h); this._suiteRunner(d, btn); filter = "suite"; }
        else if (it && it.kind === "group") {
            d.append(h);
            const r = el("div", "runrow");
            r.append(btn("▶ Run group " + it.name, () => this.startRun("group:" + it.name)), el("span", "muted small", "low priority, one run at a time"));
            d.append(r);
            filter = "group:" + it.name;
        } else if (it && (it.kind === "shortcut" || it.kind === "tool")) {
            d.append(h);
            filter = "cmd:" + it.name;
            if (it.run && it.run.byHand) d.append(el("div", "dline small muted", "Not run from here: " + it.run.byHand + "."));
            else this._argsRunner(d, it, btn);
        } else if (su && su.kind === "script") {
            d.append(h);
            const r = el("div", "runrow");
            r.append(btn("▶ Run " + su.title, () => this.startRun(su.id)), el("span", "muted small", su.id === "script:release_check.sh" ? "every group, then every map: minutes" : "low priority"));
            d.append(r);
            filter = su.id;
        }
        if (!filter) { this._runsBox = null; return; }
        this._runsBox = el("div", "druns");
        this._runsBox.dataset.cmd = filter;
        d.append(this._runsBox);
        this._fillRuns();
    }
    // quark_tests: the whole suite, the failed groups again, or a chosen set (QUARK_TEST_ONLY)
    _suiteRunner(d, btn) {
        const groups = [...this.ix.groups.values()].sort((a, b) => (a.list === "par") === (b.list === "par") ? a.order - b.order : a.list === "par" ? -1 : 1);
        const failed = groups.filter((g) => g.lastRun && g.lastRun.status === "failed").map((g) => g.name);
        const passed = groups.filter((g) => g.lastRun && g.lastRun.status === "passed").length;
        const r = el("div", "runrow");
        r.append(btn("▶ Run the whole suite", () => this.startRun("suite")));
        const rf = btn("↻ Rerun failed (" + failed.length + ")", () => this.startRun("suite", { groups: failed }), "");
        rf.dataset.empty = failed.length ? "0" : "1";
        if (!failed.length) rf.disabled = true;
        rf.title = failed.length ? failed.join(", ") : "no group failed in its last local run";
        r.append(rf);
        d.append(r);
        d.append(el("div", "dline small muted", `build/quark_tests: ${groups.filter((g) => g.list === "par").length} par groups at once, then the alone ones · last here: ${passed} passed, ${failed.length} failed, ${groups.length - passed - failed.length} not run yet`));
        // a chosen set
        if (!this.pick) { try { this.pick = new Set(JSON.parse(localStorage.getItem("te.pick") || "[]")); } catch { this.pick = new Set(); } }
        for (const n of [...this.pick]) if (!this.ix.groups.has(n)) this.pick.delete(n);
        const keep = () => { try { localStorage.setItem("te.pick", JSON.stringify([...this.pick])); } catch { } };
        const chips = el("div", "gchips");
        const runPick = btn("", () => this.startRun("suite", { groups: groups.filter((g) => this.pick.has(g.name)).map((g) => g.name) }), "");
        const sync = () => { runPick.textContent = "▶ Run " + this.pick.size + " chosen"; runPick.dataset.empty = this.pick.size ? "0" : "1"; runPick.disabled = !this.pick.size || !!(this.run && this.run.status === "running"); for (const c of chips.children) c.classList.toggle("on", this.pick.has(c.dataset.g)); };
        for (const g of groups) {
            const c = el("button", "gchip");
            c.dataset.g = g.name;
            const lr = g.lastRun;
            c.append(el("i", "dot cc-" + (!lr ? "none" : lr.status === "passed" ? "ok" : "bad")), document.createTextNode(g.name));
            c.title = (g.list === "par" ? "par" : "alone") + (g.timing ? " · " + secs(g.timing.sec) : "") + (lr ? " · last: " + lr.status : " · not run here yet");
            c.onclick = () => { if (this.pick.has(g.name)) this.pick.delete(g.name); else this.pick.add(g.name); keep(); sync(); };
            chips.append(c);
        }
        const pr = el("div", "runrow");
        const sel = (label, names) => { const b = el("button", "mini", label); b.onclick = () => { this.pick = new Set(names); keep(); sync(); }; return b; };
        pr.append(runPick, sel("none", []), sel("failed", failed), sel("par", groups.filter((g) => g.list === "par").map((g) => g.name)), sel("alone", groups.filter((g) => g.list !== "par").map((g) => g.name)));
        const det = el("details", "pick");
        det.open = this.pick.size > 0;
        det.append(el("summary", null, "Run chosen groups (QUARK_TEST_ONLY)"), pr, chips);
        d.append(det);
        sync();
        // a log from a terminal run (build/quark_tests > log.txt) feeds the group times and results too
        const imp = el("span", "link small", "Import a run log…");
        const file = el("input"); file.type = "file"; file.accept = ".txt,.log,text/plain"; file.hidden = true;
        file.onchange = async () => {
            const f = file.files[0]; if (!f) return;
            const r2 = await api.importLog(await f.text(), f.name);
            file.value = "";
            if (!r2.ok) { toast("import failed: " + ((r2.data && r2.data.error) || r2.status), true); return; }
            toast(r2.data.groups ? `imported ${r2.data.groups} group${r2.data.groups > 1 ? "s" : ""} from ${f.name}` : "no '(group X: N s)' lines in " + f.name, !r2.data.groups);
            this.pollModel();
        };
        imp.onclick = () => file.click();
        const ir = el("div", "dline");
        ir.append(imp, file, el("span", "muted small", " (the output of build/quark_tests run elsewhere: its group times and results)"));
        d.append(ir);
    }
    // a shortcut / tool command: its arguments (checked against argsSpec), then Run
    _argsRunner(d, it, btn) {
        const sp = it.argsSpec || { min: 0, max: 0, params: [] };
        let input = null;
        if (sp.max !== 0) {
            input = el("input", "mono argsin");
            input.id = "runArgs";
            input.placeholder = this._argsHelp(it).trim() || "arguments";
            try { input.value = (JSON.parse(localStorage.getItem("te.args") || "{}"))[it.name] || ""; } catch { }
            if (sp.choices) {
                const dl = el("datalist"); dl.id = "argChoices";
                for (const c of sp.choices) { const o = el("option"); o.value = c; dl.append(o); }
                d.append(dl);
                input.setAttribute("list", "argChoices");
            }
            const row = el("div", "runrow");
            row.append(el("span", "muted small", "quark_tests " + it.name), input);
            d.append(row);
            if (sp.choices) d.append(el("div", "dline small muted", "NAME: " + sp.choices.join(", ")));
        }
        const go = () => {
            const args = input ? input.value.trim().split(/\s+/).filter(Boolean) : [];
            const n = args.length;
            if (n < sp.min || (sp.max !== null && n > sp.max)) { toast(`quark_tests ${it.name} takes ${sp.min === sp.max ? sp.min : sp.min + " to " + (sp.max === null ? "any" : sp.max)} argument(s)`, true); if (input) input.focus(); return; }
            if (args.some((a) => !/^[\w.:,+-]{1,64}$/.test(a))) { toast("arguments: letters, digits and . _ - : , + only", true); return; }
            if (sp.choices && n && !sp.choices.includes(args[0])) { toast(`no such name “${args[0]}”: one of ${sp.choices.join(", ")}`, true, 6000); if (input) input.focus(); return; }   // (quark_tests would print nothing and pass)
            try { const m = JSON.parse(localStorage.getItem("te.args") || "{}"); m[it.name] = args.join(" "); localStorage.setItem("te.args", JSON.stringify(m)); } catch { }
            this.startRun("cmd:" + it.name, args.length ? { args } : null);
        };
        if (input) input.addEventListener("keydown", (e) => { if (e.key === "Enter") go(); });
        const r = el("div", "runrow");
        r.append(btn("▶ Run quark_tests " + it.name, go), el("span", "muted small", "low priority, one run at a time"));
        d.append(r);
    }
    _failRow(f) {
        const r = el("div", "fline");
        r.append(el("span", "mono small", clip(f.text, 160)));
        if (f.file) { const a = el("span", "link mono small", f.file + ":" + f.line); a.title = "open the CHECK in Code"; a.onclick = () => this.jumpToCode(f.file, f.line); r.append(a); }
        return r;
    }

    // ---------------------------------------------------------------- files, saving
    async getFile(path, fresh = false) {
        if (!path) return null;
        let f = this.files.get(path);
        if (f && !fresh) return f;
        const r = await api.file(path);
        if (!r.ok) { toast(`can't open ${path}: ${(r.data && r.data.error) || r.status}`, true); return f || null; }
        if (f && f.text !== f.disk) { f.disk = r.data.eol === "crlf" ? r.data.text.replace(/\r\n/g, "\n") : r.data.text; f.sha = r.data.sha256; return f; }   // (keep the unsaved edits)
        const text = r.data.eol === "crlf" ? r.data.text.replace(/\r\n/g, "\n") : r.data.text;   // (edited as LF, saved as the file has it)
        f = { path, disk: text, text, sha: r.data.sha256, mtime: r.data.mtime, eol: r.data.eol || "lf", scrollTop: f ? f.scrollTop : 0 };
        this.files.set(path, f);
        return f;
    }
    codeEdited(path) { this._dirtyMarks(); if (this.view !== "blocks") return; }
    _dirtyMarks() {
        const n = [...this.files.values()].filter((f) => f.text !== f.disk).length;
        $("dirtyNote").textContent = n ? "● " + n + " unsaved file" + (n > 1 ? "s" : "") : "";
    }
    // every save goes here: rule checks, a confirm panel (always for blocks), then POST with baseSha256 (409: conflict)
    async saveFile(path, text, opts = {}) {
        const f = await this.getFile(path);
        if (!f) return false;
        if (text === f.disk) { toast("no changes to save"); return false; }
        const hits = ruleHits(path, f.disk, text, this.ix.m);
        if (opts.source === "blocks" || hits.length) {
            const ok = await this._confirmSave(path, f.disk, text, hits, opts);
            if (!ok) return false;
        }
        return this._post(path, text, f.sha);
    }
    async _confirmSave(path, oldText, newText, hits, opts) {
        const body = el("div");
        if (opts.what) body.append(el("div", "muted small", "From the blocks of " + opts.what + ":"));
        for (const h of hits) { const w = el("div", "rulehit"); w.append(el("b", null, "⚠ Rule " + h.id + ": "), document.createTextNode(h.text), el("div", "small", h.detail)); body.append(w); }
        const pre = el("pre", "diff");
        pre.innerHTML = diffHtml(unified(oldText, newText, path));
        body.append(pre);
        const v = await modal({ title: "Save " + path + "?", body, wide: true, buttons: [{ label: "Cancel", value: null }, { label: hits.length ? "Save anyway" : "Save", value: "save", cls: hits.length ? "danger" : "primary" }] });
        return v === "save";
    }
    async _post(path, text, baseSha) {
        const f0 = this.files.get(path);
        const r = await api.save(path, f0 && f0.eol === "crlf" ? text.replace(/\n/g, "\r\n") : text, baseSha);
        if (r.status === 409) return this._conflict(path, text, r.data && r.data.sha256);
        if (!r.ok) { toast("save failed: " + ((r.data && r.data.error) || r.status), true, 6000); return false; }
        const f = this.files.get(path);
        f.disk = text; f.text = text; f.sha = r.data.sha256;
        this.blocks.saved();
        this.code.refresh();
        this._dirtyMarks();
        toast("saved " + path);
        if (r.data.model) this.setModel(r.data.model); else await this.pollModel();
        if (this.sel && this.sel.kind !== "when") this._showView(false);
        return true;
    }
    async _conflict(path, mine, diskSha) {
        const r = await api.file(path);
        const disk = r.ok ? r.data.text.replace(/\r\n/g, "\n") : "";
        const v = await modal({
            title: path + " changed on disk",
            body: `<p>Someone (or something) changed <code>${esc(path)}</code> after you opened it. Your version wasn't saved.</p><p class="muted small">Reload takes the file on disk (your edits are lost). Overwrite shows what yours would replace, then saves yours over it.</p>`,
            buttons: [{ label: "Cancel", value: null }, { label: "Reload from disk", value: "reload" }, { label: "Show diff, then overwrite…", value: "diff", cls: "danger" }],
        });
        if (v === "reload") {
            const f = this.files.get(path);
            f.disk = disk; f.text = disk; f.sha = r.data.sha256;
            this.blocks.discardAll();
            this.code.refresh();
            this._dirtyMarks();
            await this.pollModel();
            if (this.sel && this.sel.kind !== "when") this._showView(false);
            toast("reloaded " + path);
            return false;
        }
        if (v === "diff") {
            const pre = el("pre", "diff");
            pre.innerHTML = diffHtml(unified(disk, mine, path));
            const body = el("div");
            body.append(el("div", "small muted", "What the file on disk becomes if you overwrite it (− on disk now, + yours):"), pre);
            const v2 = await modal({ title: "Overwrite " + path + "?", body, wide: true, buttons: [{ label: "Cancel", value: null }, { label: "Overwrite with mine", value: "go", cls: "danger" }] });
            if (v2 === "go") return this._post(path, mine, (r.ok && r.data.sha256) || diskSha);
        }
        return false;
    }
    // the open file changed on disk (another editor, git): reload it if it's clean, else say so
    async checkDisk() {
        const p = this.code.path;
        if (!p || !this.files.has(p)) return;
        const f = this.files.get(p);
        const r = await api.file(p);
        if (!r.ok || r.data.sha256 === f.sha) { if (r.ok) this.code.showConflict(null); return; }
        const dt = r.data.text.replace(/\r\n/g, "\n");
        if (f.text === f.disk) { f.disk = f.text = dt; f.sha = r.data.sha256; this.code.refresh(); this.pollModel(); return; }
        this.code.showConflict(`<b>${esc(p)} changed on disk</b> while you have unsaved edits here.`, [
            ["Reload (lose mine)", () => { f.disk = f.text = dt; f.sha = r.data.sha256; this.code.refresh(); this.code.showConflict(null); this._dirtyMarks(); this.pollModel(); }],
            ["Keep mine", () => this.code.showConflict(null)],
        ]);
    }
    async showDiff(path) {
        const body = el("div");
        const f = path && this.files.get(path);
        if (f && f.text !== f.disk) {
            body.append(el("h4", null, "Unsaved here (vs the file on disk)"));
            const p = el("pre", "diff"); p.innerHTML = diffHtml(unified(f.disk, f.text, path)); body.append(p);
        }
        const r = await api.diff(path);
        body.append(el("h4", null, (path ? path : "Every editable file") + " vs git HEAD"));
        const p2 = el("pre", "diff");
        p2.innerHTML = r.ok ? diffHtml(r.data.diff) : `<span class="err">${esc((r.data && r.data.error) || "no diff")}</span>`;
        body.append(p2);
        await modal({ title: "Diff", body, wide: true, buttons: [{ label: "Close", value: null, cls: "primary" }] });
    }
    async revert(path) {
        if (!path) return;
        const v = await modal({ title: "Revert " + path + "?", body: `<p>Puts <code>${esc(path)}</code> back as git HEAD has it. Every change since HEAD is lost (the server keeps a backup of the current file in cache/backups/).</p>`, buttons: [{ label: "Cancel", value: null }, { label: "Revert to HEAD", value: "go", cls: "danger" }] });
        if (v !== "go") return;
        const r = await api.revert(path);
        if (!r.ok) { toast("revert failed: " + ((r.data && r.data.error) || r.status), true); return; }
        const f = this.files.get(path);
        if (f) f.text = f.disk = "\0";
        this.files.delete(path);
        this.blocks.discardAll();
        const nf = await this.getFile(path, true);
        this.code.path = null;
        await this.pollModel();
        if (this.sel && this.sel.kind !== "when") this._showView(false);
        this._dirtyMarks();
        toast("reverted " + path);
        return nf;
    }

    // ---------------------------------------------------------------- runs
    async loadRuns() { const r = await api.runs(); if (r.ok && Array.isArray(r.data)) { this.runs = r.data; this._fillRuns(); const cur = r.data.find((x) => x.status === "running"); if (cur && !this.run) this._follow(cur.id, cur); } }
    runLabel(r) {
        const c = r.cmd || "";
        if (c === "suite") return r.only && r.only.length ? "quark_tests: " + clip(r.only.join(", "), 60) : "quark_tests (whole suite)";
        if (c.startsWith("group:")) return "group " + c.slice(6);
        if (c.startsWith("cmd:")) return "quark_tests " + c.slice(4) + (r.args && r.args.length ? " " + r.args.join(" ") : "");
        if (c.startsWith("script:")) return c.slice(7);
        return c;
    }
    _fillRuns() {
        const box = this._runsBox;
        if (!box) return;
        box.textContent = "";
        const cmd = box.dataset.cmd;
        const list = this.runs.filter((r) => !cmd || r.cmd === cmd).slice(0, 8);
        if (!list.length) { box.append(el("div", "muted small", cmd ? "not run here yet" : "none yet")); return; }
        for (const r of list) {
            const row = el("div", "runh link");
            const gr = r.groupResults ? Object.values(r.groupResults) : [];
            const bad = gr.filter((x) => x.status === "failed").length;
            row.append(el("i", "dot cc-" + (r.status === "passed" ? "ok" : r.status === "failed" ? "bad" : r.status === "running" ? "run" : "skip")), el("span", "small", this.runLabel(r)), el("span", "small muted", dallas(r.startedAt) + (r.sec ? " · " + secs(r.sec) : "") + " · " + r.status + (gr.length > 1 ? ` · ${gr.length - bad}/${gr.length} groups passed` : "")));
            row.onclick = () => this._follow(r.id, r, true);
            box.append(row);
        }
    }
    async startRun(cmd, extra) {
        const r = await api.run(cmd, extra);
        if (r.status === 409) { toast("a run is going already: stop it first", true); return; }
        if (r.status === 404) { toast((r.data && r.data.error) === "no build" ? "no build: build quark_tests first" : "can't run: " + ((r.data && r.data.error) || r.status), true, 6000); return; }
        if (!r.ok) { toast("can't run: " + ((r.data && r.data.error) || r.status), true, 6000); return; }
        this._follow(r.data.id, { cmd, ...(extra && extra.args ? { args: extra.args } : {}), ...(extra && extra.groups ? { only: extra.groups } : {}) });
    }
    _setBusy(busy) { for (const b of document.querySelectorAll("#details .runbtn")) b.disabled = busy || b.dataset.empty === "1"; }
    _follow(id, rec, past = false) {
        clearTimeout(this._runT);
        this.run = { id, cmd: rec.cmd, from: 0, status: "running", partial: "", fails: [], failAt: -1 };
        this.openDock(true);
        $("runOut").textContent = "";
        $("runTitle").textContent = this.runLabel(rec);
        $("runState").textContent = past ? "" : "starting…";
        $("runState").className = "rstate";
        $("runStop").disabled = false;
        this._failCount();
        const tick = async () => {
            if (!this.run || this.run.id !== id) return;
            const r = await api.runPoll(id, this.run.from);
            if (!r.ok) { $("runState").textContent = "lost the run (" + r.status + ")"; return; }
            const x = r.data;
            if (x.output) this._appendOut(x.output);
            this.run.from = x.next !== undefined ? x.next : x.size !== undefined ? x.size : this.run.from + new TextEncoder().encode(x.output || "").length;
            if (x.more) { this._runT = setTimeout(tick, 0); return; }   // (paged: more output waiting)
            this.run.status = x.status;
            $("runTitle").textContent = this.runLabel(x);
            $("runState").textContent = x.status + (x.sec ? " · " + secs(x.sec) : "") + (x.exit !== null && x.exit !== undefined && x.status !== "running" ? " · exit " + x.exit : "");
            $("runState").className = "rstate rs-" + x.status;
            this._setBusy(x.status === "running");
            if (x.status === "running") { this._runT = setTimeout(tick, POLL_RUN_MS); return; }
            if (this.run.partial) { this._appendOut("\n"); }   // (the last line had no newline)
            $("runStop").disabled = true;
            this.loadRuns();
            if (!past) this.pollModel();   // (a finished run feeds each group's time and result)
        };
        tick();
    }
    // the output, line by line: FAIL / TIMEOUT / FAILURE(S) lines marked (their file:line opens the CHECK), group ends
    // muted; plain lines go in as one text node per chunk (a whole-suite run prints thousands)
    _appendOut(text) {
        const out = $("runOut"), run = this.run;
        const atEnd = out.scrollTop + out.clientHeight >= out.scrollHeight - 10;
        const lines = (run.partial + text).split("\n");
        run.partial = lines.pop();
        const frag = document.createDocumentFragment();
        let plain = "";
        const flush = () => { if (plain) { frag.append(document.createTextNode(plain)); plain = ""; } };
        for (const l of lines) {
            const fail = /^\s*(FAIL\b|TIMEOUT:)|\d+ FAILURE\(S\)$|^\s*\(group [\w-]+: \d+ failure/.test(l) || /^[\w-]+ rc=[1-9]/.test(l) || /^map \S+ FAILED/.test(l) || /^RESULT: FAILURES/.test(l);
            const pass = !fail && (/^all tests passed$|^RESULT: all passed/.test(l));
            const grp = !fail && !pass && /^\s*\(group [\w-]+: [\d.]+ s\)$/.test(l);
            if (!fail && !pass && !grp) { plain += l + "\n"; continue; }
            flush();
            const sp = el("span", fail ? "ofail" : pass ? "opass" : "ogrp");
            const m = fail && /FAIL\s+(\S+?):(\d+):/.exec(l);
            const at = m && m[1].replace(/\\/g, "/");
            const rel = at && (at.lastIndexOf("tests/") >= 0 ? at.slice(at.lastIndexOf("tests/")) : null);
            if (rel) {
                const i = l.indexOf(m[1]);
                const a = el("a", "olink", rel + ":" + m[2]);
                a.title = "open " + rel + ":" + m[2] + " in Code";
                a.onclick = () => this.jumpToCode(rel, +m[2]);
                sp.append(document.createTextNode(l.slice(0, i)), a, document.createTextNode(l.slice(i + m[1].length + 1 + m[2].length) + "\n"));
            } else sp.textContent = l + "\n";
            frag.append(sp);
            if (fail) run.fails.push(sp);
        }
        flush();
        out.append(frag);
        if (atEnd) out.scrollTop = out.scrollHeight;
        this._failCount();
    }
    _failCount() {
        const b = $("runFails"), n = this.run ? this.run.fails.length : 0;
        b.hidden = !n;
        b.textContent = n + " FAIL ▸";
        b.title = "jump to the next failure";
    }
    nextFailure() {
        const run = this.run;
        if (!run || !run.fails.length) return;
        this.openDock(true);
        run.failAt = (run.failAt + 1) % run.fails.length;
        const e = run.fails[run.failAt];
        for (const x of $("runOut").querySelectorAll(".ofail.cur")) x.classList.remove("cur");
        e.classList.add("cur");
        const out = $("runOut");
        out.scrollTop = e.offsetTop - out.clientHeight / 3;
        $("runFails").textContent = `${run.failAt + 1}/${run.fails.length} FAIL ▸`;
    }
    async stopRun() { if (this.run) { await api.stop(this.run.id); toast("stopping " + $("runTitle").textContent); } }
    openDock(open) { $("dock").classList.toggle("open", open); $("dockToggle").textContent = open ? "▾ run output" : "▸ run output"; }
    toggleDock() { this.openDock(!$("dock").classList.contains("open")); }

    // ---------------------------------------------------------------- side panels: drag the edge to resize, [ / ] or
    // the header buttons (or a double-click on the edge) to hide / show; kept per browser
    _panels() {
        const app = $("app");
        const get = (k) => { try { return localStorage.getItem(k); } catch { return null; } };
        const set = (k, v) => { try { localStorage.setItem(k, v); } catch { } };
        for (const side of ["l", "r"]) {
            const w = +get("te." + side + "w");
            if (w) app.style.setProperty("--" + side + "w", w + "px");
            if (get("te." + side + "c") === "1") document.body.classList.add(side + "-hidden");
            const rz = $("rz" + side.toUpperCase());
            rz.addEventListener("dblclick", () => this.togglePanel(side));
            rz.addEventListener("pointerdown", (e) => {
                if (document.body.classList.contains(side + "-hidden")) return;
                e.preventDefault();
                rz.setPointerCapture(e.pointerId);
                const box = app.getBoundingClientRect();
                document.body.classList.add("resizing");
                const move = (ev) => {
                    const px = side === "l" ? ev.clientX - box.left : box.right - ev.clientX;
                    const w = Math.round(Math.max(160, Math.min(px, box.width * 0.45)));
                    app.style.setProperty("--" + side + "w", w + "px");
                    this._lastW = w;
                };
                const up = () => { rz.removeEventListener("pointermove", move); document.body.classList.remove("resizing"); if (this._lastW) set("te." + side + "w", this._lastW); this._lastW = 0; if (this.view === "graph" && this.sel && this.sel.kind !== "when") this.graph.render(); };
                rz.addEventListener("pointermove", move);
                rz.addEventListener("pointerup", up, { once: true });
            });
        }
        $("leftToggle").onclick = () => this.togglePanel("l");
        $("rightToggle").onclick = () => this.togglePanel("r");
    }
    togglePanel(side) {
        const on = document.body.classList.toggle(side + "-hidden");
        try { localStorage.setItem("te." + side + "c", on ? "1" : "0"); } catch { }
        if (this.view === "graph" && this.sel && this.sel.kind !== "when") this.graph.render();
    }
}

window.testsEditor = new App();
