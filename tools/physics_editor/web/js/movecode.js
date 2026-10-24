// Code › Movement code: the game's real movement code (src/game/pmove.cpp, pmove.h, gamedefs.h, read-only), syntax
// highlighted, with the edited set worked in: every place a setting is read shows "constant base × multiplier = value"
// (changed ones highlighted, equations shown), every constant a setting scales shows its value in this set, and each
// function and step of pmoveStep has a short note on what it does. The side lists every setting (click: its uses, one
// after another) and every function / step (click: go there). The Code graph jumps here too.
import { analyzeSource, highlightCpp, FUNCTION_NOTES, FILES } from "./source.js";
import { resolve } from "./expr.js";
import { getVal, hasExpr, SWITCHES } from "./params.js";
import { fmtV } from "./exprui.js";

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

export class MoveCodeView {
    constructor(app) {
        this.app = app; this.file = "pmove.cpp"; this.sub = "qmode";
        try { this.sub = localStorage.getItem("qpe.codeSub") || "qmode"; } catch {}
        document.querySelectorAll("#codeTabs button").forEach((b) => (b.onclick = () => this.showSub(b.dataset.sub)));
        $("mcNotes").onchange = () => this.renderCode();
        $("mcOnly").onchange = () => this.renderCode();
        $("mcFind").oninput = () => { clearTimeout(this._ft); this._ft = setTimeout(() => this.find($("mcFind").value), 150); };
        $("mcFind").onkeydown = (e) => { if (e.key === "Enter") this.find($("mcFind").value, true); };
        app.on("page", (p) => { if (p === "code") this.showSub(this.sub, true); });
        app.on("physics", () => { if (app.page === "code" && this.sub === "move") this.annotate(); else this.stale = true; });
        this.showSub(this.sub, true);
    }
    showSub(sub, quiet) {
        this.sub = sub;
        try { localStorage.setItem("qpe.codeSub", sub); } catch {}
        document.querySelectorAll("#codeTabs button").forEach((b) => b.classList.toggle("on", b.dataset.sub === sub));
        document.querySelectorAll("#pg-code .sub").forEach((d) => d.classList.toggle("on", d.dataset.sub === sub));
        if (sub === "move" && this.app.page === "code") this.load().then(() => { if (this.stale) this.annotate(); });
        $("codeSubInfo").textContent = sub === "move" ? "read only: the game's code as it is in the checkout; the values are this set's" : "";
    }
    async load() {
        if (this.src) return this.src;
        if (!this._lp) this._lp = analyzeSource().then((s) => {
            this.src = s;
            for (const f of Object.values(s.files)) f.html = highlightCpp(f.text);
            this.renderFiles(); this.renderCode(); this.renderSide();
            return s;
        }).catch((e) => { $("mcCode").innerHTML = `<div class="err" style="padding:10px">${esc(e.message)}</div>`; this._lp = null; });
        return this._lp;
    }
    renderFiles() {
        $("mcFiles").innerHTML = FILES.filter((f) => this.src.files[f]).map((f) => `<button data-f="${f}" class="${f === this.file ? "on" : ""}" title="${esc(this.src.files[f].path)}">${f}</button>`).join("");
        $("mcFiles").querySelectorAll("button").forEach((b) => (b.onclick = () => { this.file = b.dataset.f; this.renderFiles(); this.renderCode(); }));
    }
    // per line: the uses of settings on it, the constant defined on it
    _lineInfo() {
        const s = this.src, f = s.files[this.file];
        const uses = this.file === "pmove.cpp" ? s.uses : [];
        const byLine = new Map();
        for (const u of uses) { if (!byLine.has(u.line)) byLine.set(u.line, []); byLine.get(u.line).push(u); }
        // constants a setting scales: from the uses (constant next to the multiplier) and the catalog's list
        const scaled = new Map();
        const add = (c, k) => { if (!c) return; if (!scaled.has(c)) scaled.set(c, new Set()); scaled.get(c).add(k); };
        for (const u of s.uses) add(u.constant, u.key);
        for (const p of this.app.cat.all) { add(p.cpp, p.key); for (const c of p.constants || []) add(c.name, p.key); }
        const defs = new Map();
        for (const [name, c] of s.constants) if (c.file === this.file && scaled.has(name)) defs.set(c.line, { ...c, keys: [...scaled.get(name)] });
        const notes = new Map();
        if (this.file === "pmove.cpp") {
            for (const fn of s.fns) if (FUNCTION_NOTES[fn.name]) notes.set(fn.start, { title: fn.name + "()", text: FUNCTION_NOTES[fn.name], kind: "fn" });
            for (const st of s.steps) notes.set(st.start, { title: "step: " + st.label, text: st.note, kind: "step" });
        }
        return { f, byLine, defs, notes };
    }
    renderCode() {
        if (!this.src) return;
        const { f, byLine, defs, notes } = this._lineInfo();
        const showNotes = $("mcNotes").checked, only = $("mcOnly").checked;
        let h = "";
        this.annLines = [];
        let skipped = 0;
        f.lines.forEach((_, i) => {
            const n = i + 1, has = byLine.has(n) || defs.has(n);
            if (showNotes && notes.has(n)) { const nt = notes.get(n); h += `<div class="mnote ${nt.kind}" data-l="${n}"><b>${esc(nt.title)}</b> ${esc(nt.text)}</div>`; }
            if (only && !has) { skipped++; return; }
            if (only && skipped) { h += `<div class="mskip">⋯ ${skipped} line${skipped > 1 ? "s" : ""}</div>`; skipped = 0; }
            // the chips go between the code and its end-of-line comment, so they stay in view next to what they annotate
            let tx = f.html[i] || " ", tail = "";
            if (has) { const j = tx.lastIndexOf('<span class="cc">//'); if (j > 0 && /\S/.test(tx.slice(0, j).replace(/<[^>]*>/g, ""))) { tail = tx.slice(j); tx = tx.slice(0, j); } }
            h += `<div class="ml${has ? " has" : ""}" data-l="${n}"><span class="mno">${n}</span><span class="mtx">${tx}</span>${has ? `<span class="mann" data-l="${n}"></span>` : ""}${tail ? `<span class="mtx mcm">${tail}</span>` : ""}</div>`;
            if (has) this.annLines.push(n);
        });
        $("mcCode").innerHTML = h;
        this.lineInfo = { byLine, defs };
        $("mcCode").querySelectorAll(".mno").forEach((e) => (e.onclick = () => this.flash(+e.parentElement.dataset.l)));
        this.annotate();
        $("mcInfo").textContent = `${f.path} · ${f.lines.length} lines · ${this.file === "pmove.cpp" ? this.src.uses.length + " reads of a setting" : defs.size + " constants a setting scales"}`;
    }
    // the chips: worked out from the set now (re-run on every change: the text stays, only the chips change)
    annotate() {
        this.stale = false;
        if (!this.src || !this.lineInfo) return;
        const app = this.app, cat = app.cat, ph = app.physics, res = resolve(ph, cat);
        const val = (k) => (k === "dash" || k === "grapple" ? ph[k] !== false : res.S[k] ?? getVal(ph, k));
        const changed = (k) => (k === "dash" || k === "grapple" ? ph[k] === false : Math.abs(val(k) - 1) > 1e-9 || hasExpr(ph, k));
        const C = this.src.constants;
        const unitOf = (p, cname) => (p && (p.cpp === cname || (p.constants || []).some((c) => c.name === cname)) ? p.unit || "" : "");
        const chip = (k, cname) => {
            const p = cat.byKey.get(k);
            if (k === "dash" || k === "grapple") return `<span class="mchip${changed(k) ? " chg" : ""}" data-k="${k}">${k}: ${val(k) ? "on" : "off"}</span>`;
            const m = val(k), info = res.info[k], c = cname && C.get(cname);
            const ex = info ? `<span class="mx" title="expr.${k}=${esc(info.text)}">${info.ok ? (info.dynamic ? "ƒ dynamic" : "ƒ") : "ƒ error"} ${esc(info.text.length > 38 ? info.text.slice(0, 36) + "…" : info.text)}</span> ` : "";
            const base = c && Number.isFinite(c.value) ? c.value : null;
            const u = unitOf(p, cname);
            const body = base != null ? `${cname} ${fmtV(base)} × ${ex}<b>${fmtV(m)}</b> = <b>${fmtV(base * m)}</b>${u ? " " + esc(u) : ""}${info && info.dynamic ? " at rest" : ""}`
                : `× ${ex}${k} <b>${fmtV(m)}</b>`;
            return `<span class="mchip${changed(k) ? " chg" : ""}${info ? " xp" : ""}" data-k="${k}" title="${esc(p ? p.label + ": " + (p.doc || "") : k)}\nclick: this setting in Values">${body}</span>`;
        };
        for (const n of this.annLines) {
            const el = document.querySelector(`#mcCode .mann[data-l="${n}"]`);
            if (!el) continue;
            let h = "";
            const us = this.lineInfo.byLine.get(n) || [];
            const seen = new Set();
            for (const u of us) { const id = u.key + "|" + u.constant; if (seen.has(id)) continue; seen.add(id); h += chip(u.key, u.constant); }
            const d = this.lineInfo.defs.get(n);
            if (d) for (const k of d.keys) h += `<span class="mdef">${chip(k, d.name)}</span>`;
            el.innerHTML = h;
            el.parentElement.classList.toggle("chg", us.some((u) => changed(u.key)) || (d && d.keys.some(changed)));
        }
        document.querySelectorAll("#mcCode .mchip").forEach((c) => (c.onclick = () => this.app.gotoSetting(c.dataset.k)));
        this.renderSide(res, val, changed);
    }
    renderSide(res, val, changed) {
        if (!this.src) return;
        const app = this.app, cat = app.cat, ph = app.physics;
        if (!res) { res = resolve(ph, cat); val = (k) => (k === "dash" || k === "grapple" ? ph[k] !== false : res.S[k]); changed = (k) => (k === "dash" || k === "grapple" ? ph[k] === false : Math.abs(val(k) - 1) > 1e-9 || hasExpr(ph, k)); }
        const count = new Map();
        for (const u of this.src.uses) count.set(u.key, (count.get(u.key) || 0) + 1);
        let h = `<h3>Settings <span class="muted">(click: its uses)</span></h3><div class="mset">`;
        for (const p of [...cat.all, ...SWITCHES.map((s) => ({ key: s.key, label: s.label }))]) {
            const n = count.get(p.key) || 0;
            const v = p.key === "dash" || p.key === "grapple" ? (val(p.key) ? "on" : "off") : "×" + fmtV(val(p.key));
            h += `<div class="msi${changed(p.key) ? " chg" : ""}${n ? "" : " unused"}" data-k="${p.key}" title="${esc(p.label)}${n ? `: read ${n} time${n > 1 ? "s" : ""} in pmove.cpp` : ": not read in pmove.cpp"}"><span>${hasExpr(ph, p.key) ? "ƒ " : ""}${p.key}</span><span class="muted">${n || "–"}</span><span class="mv">${v}</span></div>`;
        }
        h += `</div><h3>Functions and steps</h3><div class="mfn">`;
        for (const f of this.src.fns) {
            h += `<div class="mfi" data-l="${f.start}" title="${esc(FUNCTION_NOTES[f.name] || "")}">${esc(f.name)}() <span class="muted">${f.start}</span></div>`;
            if (f.name === "pmoveStep") for (const st of this.src.steps) h += `<div class="mfi st" data-l="${st.start}" title="${esc(st.note)}">· ${esc(st.label)} <span class="muted">${st.start}</span></div>`;
        }
        $("mcSide").innerHTML = h + "</div>";
        $("mcSide").querySelectorAll(".msi").forEach((d) => (d.onclick = () => this.cycleUses(d.dataset.k)));
        $("mcSide").querySelectorAll(".mfi").forEach((d) => (d.onclick = () => this.goto("pmove.cpp", +d.dataset.l)));
    }
    // every use of a setting, one after the other on each click (all of them marked)
    cycleUses(key) {
        const us = this.src.uses.filter((u) => u.key === key);
        if (!us.length) {
            const p = this.app.cat.byKey.get(key), c = p && this.src.constants.get(p.cpp);
            if (c) { this.goto(c.file, c.line); this.app.toast(`${key} isn't read in pmove.cpp; its constant ${p.cpp} is in ${c.file}`); }
            else this.app.toast(`${key} isn't read in the movement code`);
            return;
        }
        if (this.file !== "pmove.cpp") { this.file = "pmove.cpp"; this.renderFiles(); this.renderCode(); }
        this.cyc = this.cyc && this.cyc.key === key ? { key, i: (this.cyc.i + 1) % us.length } : { key, i: 0 };
        document.querySelectorAll("#mcCode .ml.mark").forEach((e) => e.classList.remove("mark"));
        for (const u of us) { const e = document.querySelector(`#mcCode .ml[data-l="${u.line}"]`); if (e) e.classList.add("mark"); }
        this.flash(us[this.cyc.i].line);
        $("mcInfo").textContent = `${key}: use ${this.cyc.i + 1} of ${us.length} (line ${us[this.cyc.i].line}${us[this.cyc.i].constant ? ", " + us[this.cyc.i].constant : ""}) · click again for the next`;
    }
    async goto(file, line) {
        if (this.app.page !== "code") this.app.showPage("code");
        if (this.sub !== "move") this.showSub("move");
        await this.load();
        if (!this.src) return;
        if (file && file !== this.file && this.src.files[file]) { this.file = file; this.renderFiles(); this.renderCode(); }
        if ($("mcOnly").checked && !document.querySelector(`#mcCode .ml[data-l="${line}"]`)) { $("mcOnly").checked = false; this.renderCode(); }
        this.flash(line);
    }
    flash(line) {
        const box = $("mcCode"), el = box.querySelector(`.mnote[data-l="${line}"]`) || box.querySelector(`.ml[data-l="${line}"]`);
        if (!el) return;
        box.scrollTop = Math.max(0, el.offsetTop - box.clientHeight / 3);
        const ln = box.querySelector(`.ml[data-l="${line}"]`);
        if (ln) { ln.classList.remove("flash"); void ln.offsetWidth; ln.classList.add("flash"); }
    }
    find(q, next) {
        if (!this.src || !q) return;
        const f = this.src.files[this.file], ql = q.toLowerCase();
        const hits = []; f.lines.forEach((l, i) => { if (l.toLowerCase().includes(ql)) hits.push(i + 1); });
        if (!hits.length) { $("mcInfo").textContent = `"${q}": not found in ${this.file}`; return; }
        this.fi = next && this.fq === q ? (this.fi + 1) % hits.length : 0; this.fq = q;
        if ($("mcOnly").checked) { $("mcOnly").checked = false; this.renderCode(); }
        this.flash(hits[this.fi]);
        $("mcInfo").textContent = `"${q}": ${this.fi + 1} of ${hits.length} (Enter: next)`;
    }
}
