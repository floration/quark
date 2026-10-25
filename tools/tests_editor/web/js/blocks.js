// Blocks view: drag-and-drop logic blocks over the model, styled like the bot editor's. A CI job (When / If / Needs /
// Runs on + its steps), a group or command (its calls, par / alone), quark_tests (the par and alone lists), a
// function (its CHECKs, read-only), a script (its steps, read-only). Edits collect in a draft; "Review & save" turns
// the draft into a text patch on the file (patch.js), shows the diff and saves it.

import { el, clip, secs } from "./util.js";
import { itemKey, jobLanes, LANES, sharedPorts, portsOf } from "./model.js";
import * as P from "./patch.js";

const clone = (x) => JSON.parse(JSON.stringify(x));

export class BlocksView {
    constructor(root, app) {
        this.app = app; this.root = root;
        root.classList.add("blocks");
        this.palette = el("div", "bk-palette");
        this.right = el("div", "bk-right");
        this.draftBar = el("div", "bk-draftbar");
        this.ws = el("div", "bk-ws");
        this.right.append(this.draftBar, this.ws);
        root.append(this.palette, this.right);
        this.drafts = new Map();   // selection key -> draft
        this._drag = null;
        document.addEventListener("pointermove", (e) => this._dragMove(e));
        document.addEventListener("pointerup", (e) => this._dragEnd(e));
        document.addEventListener("keydown", (e) => { if (e.key === "Escape" && this._drag) this._dragCancel(); });
    }

    async show(sel) {
        this.sel = sel;
        const key = this.key = this._key(sel);
        const path = this._path(sel);
        this.file = path ? await this.app.getFile(path) : null;
        if (this.key !== key) return;   // (another selection came in while the file loaded)
        this.render();
    }
    _key(sel) { return !sel ? "" : sel.kind === "item" ? itemKey(sel.item) : sel.kind === "suite" ? "suite:" + sel.suite.id : sel.kind === "fn" ? "fn:" + sel.fn.name : sel.kind; }
    _path(sel) {
        if (!sel) return null;
        if (sel.kind === "item" && (sel.item.kind === "job" || sel.item.kind === "group" || sel.item.kind === "shortcut" || sel.item.kind === "tool")) return sel.item.file;
        if (sel.kind === "suite" && sel.suite.id === "quark_tests") return sel.suite.file;
        return null;
    }
    // edits only on a file whose Code buffer is clean (the model's line numbers are the file on disk's)
    get editable() { return this.file && this.file.text === this.file.disk; }
    saved() { this.drafts.delete(this.key); }
    discardAll() { this.drafts.clear(); }
    hasDrafts() { return [...this.drafts.values()].some((d) => d.changed); }

    // ---------------------------------------------------------------- render
    render() {
        const sel = this.sel;
        this.palette.textContent = ""; this.ws.textContent = ""; this.draftBar.textContent = ""; this.draftBar.hidden = true;
        if (!sel) return;
        this.canvas = el("div", "bk-canvas");
        this.ws.append(this.canvas);
        if (this._path(sel) && this.file && !this.editable) this.canvas.append(el("div", "bk-lock", "This file has unsaved edits in Code. Save or revert them there first: the blocks edit the file as it is on disk."));
        if (sel.kind === "item" && sel.item.kind === "job") this._job(sel.item);
        else if (sel.kind === "item" && (sel.item.kind === "group" || sel.item.kind === "shortcut" || sel.item.kind === "tool")) this._group(sel.item);
        else if (sel.kind === "suite" && sel.suite.id === "quark_tests") this._lists(sel.suite);
        else if (sel.kind === "suite" && sel.suite.kind === "ci") this._workflow(sel.suite);
        else if (sel.kind === "fn") this._fn(sel.fn);
        else this._steps(sel.kind === "suite" ? sel.suite : sel.item._suite, sel.kind === "item" ? sel.item : null);
        this._renderDraftBar();
    }
    _draft(make) {
        let d = this.drafts.get(this.key);
        if (!d) { d = { ...make(), changed: false }; d.initial = this._sig(d); this.drafts.set(this.key, d); }
        return d;
    }
    _sig(d) { const { changed, initial, ...rest } = d; return JSON.stringify(rest, (k, v) => (k === "open" ? undefined : v)); }
    _changed() {
        const d = this.drafts.get(this.key);
        if (d) d.changed = this._sig(d) !== d.initial;
        this.render();
    }
    _renderDraftBar() {
        const d = this.drafts.get(this.key);
        const bar = this.draftBar;
        if (!d || !d.changed) return;
        bar.hidden = false;
        bar.append(el("span", "bk-dirty", "● block edits not saved"));
        let text = null, err = null;
        try { text = this._apply(d); } catch (e) { err = e.message; }
        if (err) bar.append(el("span", "err small", "can't patch the file: " + err));
        else {
            const hits = P.ruleHits(this.file.path, this.file.disk, text, this.app.ix.m);
            for (const h of hits) { const w = el("span", "bk-rule small", "⚠ " + h.detail); w.title = h.text; bar.append(w); }
        }
        bar.append(el("span", "grow"));
        const rev = el("button", "primary", "Review & save");
        rev.disabled = !!err;
        rev.onclick = () => this.app.saveFile(this.file.path, text, { source: "blocks", what: this._what() });
        const dis = el("button", null, "Discard");
        dis.onclick = () => { this.drafts.delete(this.key); this.render(); };
        bar.append(rev, dis);
    }
    _what() { const s = this.sel; return s.kind === "item" ? s.item.kind + " " + (s.item.jobId || s.item.name) : s.kind === "suite" ? s.suite.title + " lists" : ""; }
    _apply(d) {
        const s = this.sel, disk = this.file.disk;
        if (s.kind === "item" && s.item.kind === "job") {
            const j = s.item;
            const stepEdits = {};
            d.steps.forEach((x) => { if (x.orig !== undefined && x.fields) stepEdits[x.orig] = x.fields; });
            const draft = { steps: d.steps.map((x) => (x.orig !== undefined ? x.orig : x.neu)), stepEdits };
            if (d.if !== (j.if || "")) draft.if = d.if;
            if (JSON.stringify(d.needs) !== JSON.stringify(j.needs || [])) draft.needs = d.needs;
            if (d.runsOn !== (j.runsOn || "")) draft.runsOn = d.runsOn;
            return P.applyJobDraft(disk, j, draft);
        }
        if (s.kind === "item") {
            const it = s.item;
            let t = P.writeSequence(disk, it, d.seq, this._isFn);
            if (it.kind === "group" && d.list !== it.list) t = P.moveGroup(t, it, d.list);
            return t;
        }
        if (s.kind === "suite") return P.writeLists(disk, { par: d.par, alone: d.alone });
        return disk;
    }
    get _isFn() { const f = this.app.ix.m.functions || {}; return (n) => !!f[n]; }

    // ---------------------------------------------------------------- pieces
    _hat(cls, titleParts) {
        const w = el("div", "bk-hat " + (cls || ""));
        const h = el("div", "bk-hathead");
        for (const p of titleParts) h.append(typeof p === "string" ? el("span", "bk-title", p) : p);
        w.append(h);
        return w;
    }
    _row(label, ...kids) {
        const r = el("div", "bk-line");
        r.append(el("span", "bk-kw", label), ...kids);
        return r;
    }
    _jump(file, line, label) {
        const a = el("button", "bk-mini bk-jump", label || "line " + line + " ↗");
        a.title = "show it in Code";
        a.onclick = (e) => { e.stopPropagation(); this.app.jumpToCode(file, line); };
        return a;
    }
    _stack(id, items, accept, renderItem, emptyText) {
        const s = el("div", "bk-stack");
        s._stack = { id, items, accept };
        items.forEach((it, i) => {
            const b = renderItem(it, i);
            b._drag = { stack: s._stack, index: i, kind: accept };
            s.append(b);
        });
        if (!items.length) s.append(el("div", "bk-empty", emptyText || "drop blocks here"));
        return s;
    }
    _grip(b) {
        const g = el("span", "bk-grip", "⋮⋮");
        g.title = "drag to move; drop on the palette to delete";
        b.prepend(g);
        b.addEventListener("pointerdown", (e) => this._dragStart(e, b));
    }
    _x(onclick, title = "remove") { const x = el("button", "bk-mini bk-x", "×"); x.title = title; x.onclick = (e) => { e.stopPropagation(); onclick(); }; return x; }
    _palHead(title, sub) { this.palette.append(el("div", "bk-ph", title)); if (sub) this.palette.append(el("div", "bk-phsub muted small", sub)); }
    _palItem(kind, label, make, cls, title) {
        const p = el("div", "bk-block bk-stmt bk-pitem " + (cls || ""));
        p.append(el("span", "bk-fn", label));
        if (title) p.title = title;
        p._drag = { palette: true, kind, make };
        p.addEventListener("pointerdown", (e) => this._dragStart(e, p));
        this.palette.append(p);
        return p;
    }

    // ---------------------------------------------------------------- a CI job
    _job(j) {
        const ed = this.editable;
        const d = this._draft(() => ({ if: j.if || "", needs: [...(j.needs || [])], runsOn: j.runsOn || "", steps: (j.steps || []).map((_, i) => ({ orig: i, fields: null })) }));
        const suite = j._suite;
        const jobsById = new Map((suite.items || []).map((x) => [x.jobId, x]));
        // palette: steps (the actions this workflow already uses: pinned)
        this._palHead("Steps", "drag into the job; drop a step here to delete it");
        this._palItem("step", "run: a shell step", () => ({ neu: { name: "new step", run: "echo hello" } }), "c-step");
        const uses = new Map();
        for (const jj of suite.items || []) for (const s of jj.steps || []) if (s.uses && !uses.has(s.uses.split("@")[0])) uses.set(s.uses.split("@")[0], s);
        for (const [name, s] of uses) this._palItem("step", "uses " + name.replace(/^actions\//, ""), () => ({ neu: { name: s.name && !s.name.startsWith("uses ") ? s.name : undefined, uses: s.uses, with: name.endsWith("checkout") ? { "persist-credentials": "false" } : undefined } }), "c-uses", s.uses);
        // the job
        const hat = this._hat("c-job", [el("span", "bk-kw", "job"), j.name && j.name !== j.jobId ? j.name + " (" + j.jobId + ")" : j.jobId, this._jump(j.file, j.line)]);
        // When
        const lanes = jobLanes(j, jobsById);
        const when = el("div", "bk-chips");
        for (const w of suite.when || []) { const c = el("span", "chip c-when", w.trigger); c.title = w.text; when.append(c); }
        when.append(el("span", "muted small", "→ " + [...lanes].map((l) => (LANES.find((x) => x.id === l) || {}).title || l).join(", ")));
        hat.append(this._row("When", when));
        // If
        const ifIn = el("input", "bk-text mono");
        ifIn.value = d.if; ifIn.placeholder = "(always: no if)"; ifIn.disabled = !ed;
        ifIn.onchange = () => { d.if = ifIn.value.trim(); this._changed(); };
        const ifRow = this._row("If", ifIn);
        if (j.ifText) ifRow.append(el("div", "bk-cap muted small", j.ifText));
        hat.append(ifRow);
        // Needs
        const needs = el("div", "bk-chips");
        for (const n of d.needs) {
            const c = el("span", "chip c-need" + (jobsById.has(n) ? "" : " bad"), n);
            if (!jobsById.has(n)) c.title = "no job " + n;
            if (ed) c.append(this._x(() => { d.needs = d.needs.filter((x) => x !== n); this._changed(); }, "drop this need"));
            needs.append(c);
        }
        if (ed) {
            const add = el("select", "bk-sel");
            add.append(new Option("+ job", ""));
            for (const x of suite.items || []) if (x.jobId !== j.jobId && !d.needs.includes(x.jobId)) add.append(new Option(x.jobId, x.jobId));
            add.onchange = () => { if (add.value) { d.needs.push(add.value); this._changed(); } };
            needs.append(add);
        }
        if (!d.needs.length && !ed) needs.append(el("span", "muted small", "none: starts with the run"));
        hat.append(this._row("Needs", needs));
        // Runs on
        const ro = el("input", "bk-text mono");
        ro.value = d.runsOn; ro.disabled = !ed; ro.size = 16;
        ro.onchange = () => { d.runsOn = ro.value.trim(); this._changed(); };
        const roRow = this._row("Runs on", ro);
        if (j.timeoutMin) roRow.append(el("span", "muted small", "timeout " + j.timeoutMin + " min"));
        if (j.matrix) roRow.append(el("span", "chip", "matrix " + Object.entries(j.matrix).map(([k, v]) => k + ": " + [].concat(v).join(", ")).join("; ")));
        hat.append(roRow);
        // Steps
        hat.append(el("div", "bk-sub", "Steps (in order)"));
        hat.append(this._stack("steps", d.steps, "step", (x, i) => this._stepBlock(j, d, x, i, ed), "no steps: drag one from the palette"));
        this.canvas.append(hat);
    }
    _stepBlock(j, d, x, i, ed) {
        const orig = x.orig !== undefined ? j.steps[x.orig] : null;
        const f = { name: orig ? orig.name : x.neu.name, if: orig ? orig.if : x.neu.if, run: orig ? orig.run : x.neu.run, uses: orig ? orig.uses : x.neu.uses, ...(x.fields || {}), ...(x.neu || {}) };
        if (x.fields) for (const k in x.fields) f[k] = x.fields[k];
        const b = el("div", "bk-block bk-stmt " + (f.uses ? "c-uses" : "c-step") + (x.neu ? " new" : "") + (x.fields ? " edited" : ""));
        const row = el("div", "bk-row");
        row.append(el("span", "bk-n", String(i + 1)), el("span", "bk-name", clip(f.name || (f.uses ? "uses " + f.uses.split("@")[0] : "step"), 70)));
        if (f.if) { const c = el("span", "chip c-cond", "if " + clip(f.if, 40)); c.title = f.if; row.append(c); }
        if (orig) row.append(this._jump(j.file, orig.line));
        if (ed) {
            const e = el("button", "bk-mini", x.open ? "done" : "✎");
            e.title = "edit name / if / run";
            e.onclick = (ev) => { ev.stopPropagation(); x.open = !x.open; this.render(); };
            row.append(e, this._x(() => { d.steps.splice(i, 1); this._changed(); }, "delete this step"));
        }
        b.append(row);
        if (f.uses) b.append(el("div", "bk-code mono small", "uses " + f.uses));
        if (f.run && !x.open) b.append(el("div", "bk-code mono small", clip(String(f.run).split("\n").filter((l) => l.trim()).slice(0, 2).join("  ⏎ "), 110)));
        if (x.open && ed) {
            const form = el("div", "bk-form");
            const field = (label, key, multi) => {
                const inp = el(multi ? "textarea" : "input", "bk-text mono");
                inp.value = f[key] || "";
                if (multi) inp.rows = Math.min(12, Math.max(2, (f[key] || "").split("\n").length));
                inp.onchange = () => {
                    const v = multi ? inp.value.replace(/\s+$/, "") : inp.value.trim();
                    if (x.neu) { x.neu[key] = v || undefined; }
                    else { x.fields = x.fields || {}; x.fields[key] = v === "" ? null : v; if ((orig[key] || "") === v) delete x.fields[key]; if (!Object.keys(x.fields).length) x.fields = null; }
                    this._changed();
                };
                inp.addEventListener("pointerdown", (e) => e.stopPropagation());
                const r = el("label", "bk-field"); r.append(el("span", "muted small", label), inp); form.append(r);
            };
            field("name", "name"); field("if", "if");
            if (!f.uses) field("run", "run", true);
            else form.append(el("div", "muted small", "uses / with: edit in Code"));
            b.append(form);
        }
        if (ed) this._grip(b);
        return b;
    }
    _workflow(suite) {
        const hat = this._hat("c-job", [el("span", "bk-kw", "workflow"), suite.title, this._jump(suite.file, suite.line)]);
        const when = el("div", "bk-col");
        for (const w of suite.when || []) { const r = el("div", "bk-line"); r.append(el("span", "chip c-when", w.trigger), el("span", null, w.text), this._jump(suite.file, w.line)); when.append(r); }
        hat.append(this._row("When", when));
        const jobs = el("div", "bk-col");
        for (const j of suite.items || []) {
            const b = el("div", "bk-block bk-stmt c-job click");
            const r = el("div", "bk-row");
            r.append(el("span", "bk-name", j.jobId), el("span", "muted small", (j.needs || []).length ? "needs " + j.needs.join(", ") : "starts with the run"));
            if (j.if) { const c = el("span", "chip c-cond", "if " + clip(j.ifText || j.if, 50)); c.title = j.if; r.append(c); }
            b.append(r);
            b.onclick = () => this.app.select({ kind: "item", item: j });
            jobs.append(b);
        }
        hat.append(this._row("Jobs", jobs));
        this.canvas.append(hat);
        this.palette.append(el("div", "bk-ph", "Pick a job"), el("div", "muted small bk-phsub", "Open a job to edit its if, needs, runs-on and steps as blocks."));
    }

    // ---------------------------------------------------------------- a group / command
    _group(it) {
        const fns = this.app.ix.m.functions || {};
        const seq0 = this.file ? P.entrySequence(this.file.disk, it, this._isFn) : null;
        const tool = it.kind === "tool";   // (a tool command: C++ around a call or two, not a list of tests: read-only)
        const ed = this.editable && !!seq0 && !tool;
        const d = this._draft(() => ({ seq: seq0 ? seq0.seq.map((s) => ({ ...s })) : (it.calls || []).map((c) => ({ kind: "call", fn: c.fn, args: c.args })), list: it.list || null }));
        if (tool) {
            this.palette.append(el("div", "bk-ph", "Read-only"), el("div", "muted small bk-phsub", "A tool command isn't a list of tests: its body is C++ (arguments, loops, output). Change it in Code."));
            return this._groupHat(it, d, seq0, ed);
        }
        // palette: every test function
        this._palHead("Test functions", `drag into the ${it.kind === "group" ? "group" : "command"}, or + to add at the end`);
        const q = el("input", "bk-text"); q.type = "search"; q.placeholder = "filter functions"; q.value = this._fq || "";
        this.palette.append(q);
        const list = el("div", "bk-plist");
        this.palette.append(list);
        const freePort = () => { let m = 28000; for (const g of this.app.ix.groups.values()) for (const p of portsOf(g)) m = Math.max(m, p); return m + 1; };
        const fill = () => {
            list.textContent = "";
            const f = (this._fq || "").toLowerCase();
            const names = Object.keys(fns).sort().filter((n) => !f || n.toLowerCase().includes(f));
            for (const n of names.slice(0, 400)) {
                const fn = fns[n];
                const make = () => ({ kind: "call", fn: n, args: /uint16_t/.test(fn.args || "") ? `TP(${freePort()})` : "", prefix: /^\w+Tests$/.test(n) && n !== "serverTests" ? "g_fail += " : "" });
                const p = el("div", "bk-block bk-stmt bk-pitem c-call");
                p.append(el("span", "bk-fn", n));
                if (fn.args) p.append(el("span", "muted small", "(" + clip(fn.args, 22) + ")"));
                if (ed) { const plus = el("button", "bk-mini", "+"); plus.title = "add at the end"; plus.onclick = (e) => { e.stopPropagation(); d.seq.push(make()); this._changed(); }; p.append(plus); }
                p.title = (fn.doc || n) + `\n${(fn.checks || []).length} checks · ${fn.file}:${fn.line}`;
                p._drag = { palette: true, kind: "call", make };
                p.addEventListener("pointerdown", (e) => this._dragStart(e, p));
                list.append(p);
            }
            if (names.length > 400) list.append(el("div", "muted small", names.length - 400 + " more: filter"));
        };
        q.oninput = () => { this._fq = q.value; fill(); };
        fill();
        this._groupHat(it, d, seq0, ed);
    }
    _groupHat(it, d, seq0, ed) {
        const parts = [el("span", "bk-kw", it.kind === "group" ? "group" : it.kind === "tool" ? "tool" : "command"), it.name, this._jump(it.file, it.line)];
        const hat = this._hat("c-group", parts);
        if (it.kind === "group") {
            const seg = el("span", "seg");
            for (const l of ["par", "alone"]) {
                const b = el("button", d.list === l ? "on" : "", l === "par" ? "par: in parallel" : "alone: one at a time");
                b.disabled = !ed;
                b.onclick = () => { d.list = l; this._changed(); };
                seg.append(b);
            }
            const r = this._row("Runs", seg);
            if (d.list !== it.list && d.list === "alone") r.append(el("span", "warn small", "tools/release_check.sh keeps its own alone list: add it there too"));
            hat.append(r);
        }
        const cmd = el("code", "chip", it.run ? it.run.cmd : "");
        const meta = this._row("How", cmd);
        if (it.timing) meta.append(el("span", "muted small", secs(it.timing.sec) + " (" + it.timing.source + ")"));
        hat.append(meta);
        const sp = it.kind === "group" ? sharedPorts(this.app.ix).get(it.name) : null;
        if (sp) hat.append(this._row("Ports", el("span", "warn small", "⚠ shares " + sp.map((s) => "TP(" + s.port + ") with " + s.with.join(", ")).join("; "))));
        if (!seq0) hat.append(el("div", "warn small", "can't find this entry's { ... } body: edit it in Code"));
        hat.append(el("div", "bk-sub", "Calls (in order)"));
        hat.append(this._stack("seq", d.seq, "call", (s, i) => this._callBlock(it, d, s, i, ed), "no calls: drag a test function in"));
        this.canvas.append(hat);
    }
    _callBlock(it, d, s, i, ed) {
        const fns = this.app.ix.m.functions || {};
        const b = el("div", "bk-block bk-stmt " + (s.kind === "call" ? "c-call" : "c-code") + (s.orig === undefined ? " new" : ""));
        const row = el("div", "bk-row");
        if (s.kind === "call") {
            if (s.prefix) row.append(el("span", "muted mono small", s.prefix.trim()));
            const fn = el("span", "bk-fn" + (fns[s.fn] ? " link" : " err"), s.fn);
            if (fns[s.fn]) { fn.title = "open " + s.fn; fn.onclick = (e) => { e.stopPropagation(); this.app.select({ kind: "fn", fn: fns[s.fn] }); }; }
            else fn.title = "no such function in tests/";
            row.append(fn, el("span", "bk-p", "("));
            const a = el("input", "bk-arg mono");
            a.value = s.args || "";
            a.size = Math.max(3, a.value.length + 1);
            a.disabled = !ed;
            a.oninput = () => (a.size = Math.max(3, a.value.length + 1));
            a.onchange = () => { s.args = a.value; this._changed(); };
            a.addEventListener("pointerdown", (e) => e.stopPropagation());
            row.append(a, el("span", "bk-p", ")"));
            if (fns[s.fn]) row.append(el("span", "muted small", (fns[s.fn].checks || []).length + " checks"));
        } else {
            row.append(el("span", "bk-code mono small", clip(s.code.replace(/\s+/g, " "), 90)));
            row.title = s.code;
        }
        if (ed) row.append(this._x(() => { d.seq.splice(i, 1); this._changed(); }));
        b.append(row);
        if (ed) this._grip(b);
        return b;
    }

    // ---------------------------------------------------------------- quark_tests: the two lists
    _lists(suite) {
        const ix = this.app.ix;
        const ent = this.file ? P.listEntries(this.file.disk) : null;
        const ed = this.editable && !!ent;
        const d = this._draft(() => ({ par: ent ? ent.par.map((e) => e.name) : [], alone: ent ? ent.alone.map((e) => e.name) : [] }));
        this._palHead("Groups", "drag a group between par and alone, or reorder (par: longest first sets the wall time)");
        if (ed) {
            const name = el("input", "bk-text mono"); name.placeholder = "new group name";
            const add = el("button", null, "+ group in par");
            add.onclick = () => { const n = name.value.trim(); if (!/^[\w-]+$/.test(n) || d.par.includes(n) || d.alone.includes(n)) { name.classList.add("bad"); return; } d.par.push(n); this._changed(); };
            this.palette.append(name, add);
        }
        const groupBlock = (n, i, list) => {
            const g = ix.groups.get(n);
            const b = el("div", "bk-block bk-stmt c-group" + (g ? "" : " new"));
            const r = el("div", "bk-row");
            const nm = el("span", "bk-name link", n);
            nm.onclick = (e) => { e.stopPropagation(); if (g) this.app.select({ kind: "item", item: g }); };
            r.append(nm);
            if (g) r.append(el("span", "muted small", (g.calls || []).length + " calls" + (g.timing ? " · " + secs(g.timing.sec) : "")));
            if (g && g.list !== list) r.append(el("span", "chip c-cond", "was " + g.list));
            if (!g) r.append(el("span", "chip", "new (empty)"));
            const lint = g ? (ix.itemLint.get(itemKey(g)) || []).length : 0;
            if (lint) r.append(el("span", "warn small", "⚠ " + lint));
            if (ed && !g) r.append(this._x(() => { d[list].splice(i, 1); this._changed(); }));
            b.append(r);
            if (ed) this._grip(b);
            return b;
        };
        const jobs = ((suite.lists || []).find((l) => l.name === "par") || {}).jobs;   // (from main(): "3 per core; Windows: 1 per core")
        const par = this._hat("c-lane", [el("span", "bk-kw", "par"), `in parallel (one process each, ${jobs || "several at once"})`]);
        par.append(this._stack("par", d.par, "group", (n, i) => groupBlock(n, i, "par"), "no par groups"));
        const alone = this._hat("c-lane", [el("span", "bk-kw", "then alone"), "one at a time, on an idle machine"]);
        alone.append(this._stack("alone", d.alone, "group", (n, i) => groupBlock(n, i, "alone"), "no alone groups"));
        const wrap = el("div", "bk-cols");
        wrap.append(par, alone);
        this.canvas.append(wrap);
    }

    // ---------------------------------------------------------------- read-only: a function, a script
    _fn(f) {
        const ix = this.app.ix;
        const hat = this._hat("c-fn", [el("span", "bk-kw", "function"), f.name + "(" + (f.args || "") + ")", this._jump(f.file, f.line, f.file.split("/").pop() + ":" + f.line + " ↗")]);
        if (f.doc) hat.append(el("div", "bk-doc", f.doc));
        const used = el("div", "bk-chips");
        for (const u of f.usedBy || []) {
            const it = [...ix.items.values()].find((x) => x.id === u);
            const c = el("span", "chip c-need link", u.replace(/^group:/, "group ").replace(/^cmd:/, "cmd "));
            if (it) c.onclick = () => this.app.select({ kind: "item", item: it });
            used.append(c);
        }
        if (!(f.usedBy || []).length) used.append(el("span", "warn small", "no group or command runs it"));
        hat.append(this._row("Run by", used));
        const calls = el("div", "bk-chips");
        for (const c of f.calls || []) { const g = ix.m.functions[c]; const x = el("span", "chip c-call link", c); if (g) x.onclick = () => this.app.select({ kind: "fn", fn: g }); calls.append(x); }
        if (!(f.calls || []).length) calls.append(el("span", "muted small", "no other test functions"));
        hat.append(this._row("Calls", calls));
        hat.append(el("div", "bk-sub", `CHECKs (${(f.checks || []).length}): edit them in Code`));
        const st = el("div", "bk-stack ro");
        for (const c of f.checks || []) {
            const b = el("div", "bk-block bk-stmt c-check");
            const r = el("div", "bk-row");
            r.append(el("span", "bk-kw", "CHECK"), el("span", "bk-code mono", clip(c.expr, 120)), this._jump(f.file, c.line));
            r.title = c.expr;
            b.append(r);
            st.append(b);
        }
        if (!(f.checks || []).length) st.append(el("div", "bk-empty", "no CHECKs in this function (it may check through the functions it calls)"));
        hat.append(st);
        this.canvas.append(hat);
        this.palette.append(el("div", "bk-ph", "Read-only"), el("div", "muted small bk-phsub", "A function's checks are C++: change them in Code (the jump links go to each line). Its callers are edited on their group."));
    }
    _steps(suite, selItem) {
        const hat = this._hat("c-step", [el("span", "bk-kw", suite.kind), suite.title, this._jump(suite.file, suite.line || 1)]);
        const when = el("div", "bk-chips");
        for (const w of suite.when || []) { const c = el("span", "chip c-when", w.trigger); when.append(c, el("span", "small", w.text)); }
        hat.append(this._row("When", when));
        hat.append(el("div", "bk-sub", "Steps (in order)"));
        const st = el("div", "bk-stack ro");
        (suite.items || []).forEach((s, i) => {
            const b = el("div", "bk-block bk-stmt c-step" + (s === selItem ? " sel" : ""));
            const r = el("div", "bk-row");
            r.append(el("span", "bk-n", String(i + 1)), el("span", "bk-name", s.name), this._jump(s.file || suite.file, s.line));
            b.append(r);
            if (s.text) b.append(el("pre", "bk-code mono small", String(s.text).split("\n").slice(0, 6).join("\n") + (String(s.text).split("\n").length > 6 ? "\n…" : "")));
            st.append(b);
        });
        hat.append(st);
        this.canvas.append(hat);
        this.palette.append(el("div", "bk-ph", "Read-only"), el("div", "muted small bk-phsub", "A script's steps are shell: edit them in Code."));
    }

    // ---------------------------------------------------------------- drag and drop
    _dragStart(e, b) {
        if (e.button !== 0 || e.target.closest("input,select,button,textarea,.link")) return;
        if (!this.editable) return;
        e.preventDefault();
        this._drag = { el: b, src: b._drag, sx: e.clientX, sy: e.clientY, active: false };
    }
    _dragMove(e) {
        const d = this._drag;
        if (!d) return;
        if (!d.active) {
            if (Math.abs(e.clientX - d.sx) + Math.abs(e.clientY - d.sy) < 5) return;
            d.active = true;
            const r = d.el.getBoundingClientRect();
            d.dx = e.clientX - r.left; d.dy = e.clientY - r.top;
            d.ghost = d.el.cloneNode(true);
            d.ghost.classList.add("bk-ghost");
            d.ghost.style.width = r.width + "px";
            document.body.append(d.ghost);
            if (!d.src.palette) d.el.classList.add("bk-dragging");
            document.body.classList.add("bk-isdragging");
        }
        d.ghost.style.left = e.clientX - d.dx + "px";
        d.ghost.style.top = e.clientY - d.dy + "px";
        this._clearHint();
        const t = this._target(e, d);
        d.target = t;
        if (t && t.trash) this.palette.classList.add("bk-trash");
        else if (t) {
            const line = el("div", "bk-insert");
            const kids = [...t.el.children].filter((c) => c._drag);
            if (t.index < kids.length) t.el.insertBefore(line, kids[t.index]); else t.el.append(line);
            this._hint = line;
        }
    }
    _target(e, d) {
        const under = document.elementFromPoint(e.clientX, e.clientY);
        if (!under) return null;
        if (this.palette.contains(under)) return d.src.palette ? null : { trash: true };
        let n = under.closest(".bk-stack");
        if (!n || !n._stack || n._stack.accept !== d.src.kind) {
            // (near a stack but not on it: the nearest one that takes this kind)
            const stacks = [...this.ws.querySelectorAll(".bk-stack")].filter((s) => s._stack && s._stack.accept === d.src.kind);
            n = stacks.find((s) => { const r = s.getBoundingClientRect(); return e.clientX >= r.left - 40 && e.clientX <= r.right + 40 && e.clientY >= r.top - 20 && e.clientY <= r.bottom + 20; });
            if (!n) return null;
        }
        const kids = [...n.children].filter((c) => c._drag);
        let index = kids.length;
        for (let i = 0; i < kids.length; i++) { const r = kids[i].getBoundingClientRect(); if (e.clientY < r.top + r.height / 2) { index = i; break; } }
        return { el: n, stack: n._stack, index };
    }
    _clearHint() { if (this._hint) { this._hint.remove(); this._hint = null; } this.palette.classList.remove("bk-trash"); }
    _dragCancel() { const d = this._drag; this._drag = null; if (d && d.ghost) d.ghost.remove(); if (d) d.el.classList.remove("bk-dragging"); document.body.classList.remove("bk-isdragging"); this._clearHint(); }
    _dragEnd() {
        const d = this._drag;
        if (!d) return;
        this._dragCancel();
        if (!d.active) return;
        const t = d.target;
        if (!t) return;
        const src = d.src;
        if (t.trash) { src.stack.items.splice(src.index, 1); this._changed(); return; }
        const item = src.palette ? src.make() : src.stack.items[src.index];
        let at = t.index;
        if (!src.palette) {
            if (src.stack === t.stack && src.index < at) at--;
            src.stack.items.splice(src.index, 1);
        }
        t.stack.items.splice(at, 0, item);
        this._changed();
    }
}
