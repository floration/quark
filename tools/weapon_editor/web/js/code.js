// Code tab: the set's .qweapons text (the source of truth: Weapon / Blocks / Graph edits rewrite it, an edit here
// re-reads it), highlighted, problems in the gutter and listed (the page's checks, replaced by the game's wpncheck /
// checkweapons when there is one). Side: "try a hit" (run the hit / kill slot with sensors you set, see the outputs
// and the lines that ran) and the reference (sensors, outputs, builtins, fields).

import { highlightLine, print } from "./dsl.js";
import { highlightSets, runSlot, symbols, slotCat } from "./ast.js";
import { WEAPONS } from "./catalog.js";
import { $, esc, el, fmtNum } from "./util.js";

const LH = 19;

export class CodeView {
    constructor(app) {
        this.app = app;
        this.ta = $("codeTa"); this.hl = $("codeHl"); this.gut = $("gutter"); this.scroll = $("codeScroll");
        this.ta.addEventListener("input", () => { this._editing = true; this.app.setText(this.ta.value, "code"); this._editing = false; });
        this.ta.addEventListener("keydown", (e) => { if (e.key === "Tab") { e.preventDefault(); document.execCommand("insertText", false, "  "); } });
        this.scroll.addEventListener("scroll", () => { this.gut.scrollTop = this.scroll.scrollTop; });
        $("codeTidy").onclick = () => { if (!this.app.parseOk) { this.app.toast("fix the error first", 2500); return; } this.app.setText(print(this.app.ast), "tidy"); };
        this.trial = { slot: "hit", weapon: "rifle", s: {} };
        this.trace = new Set();
    }
    refresh(from) {
        if (from !== "code") this.load();
        else this._paint();
        this._side();
    }
    liveUpdate() { this._paint(); this._side(); }
    load() {
        const t = this.app.text;
        if (this.ta.value !== t) {
            const s = this.ta.selectionStart, e = this.ta.selectionEnd;
            this.ta.value = t;
            if (document.activeElement === this.ta) this.ta.setSelectionRange(Math.min(s, t.length), Math.min(e, t.length));
        }
        const s = this.app.set;
        $("codeFile").textContent = s.saved ? `custom_weapons/${s.saved}.qweapons${this.app.dirty ? " (unsaved changes)" : ""}` : "not saved yet (Save writes custom_weapons/<name>.qweapons)";
        this._paint();
    }
    _paint() {
        const text = this.ta.value;
        const known = highlightSets(this.app.ast);
        const lines = text.split("\n");
        this.hl.innerHTML = lines.map((l) => highlightLine(l, known)).join("\n") + "\n";
        const probs = this.app.allProblems();
        $("codeVia").textContent = this.app.gameProblems ? this.app.gameProblems.via : "the page (the game's check runs when the helper or quark_tests has it)";
        const byLine = new Map();
        for (const x of probs) if (x.line) { const cur = byLine.get(x.line); if (!cur || x.severity === "error") byLine.set(x.line, x.severity); }
        let g = "";
        for (let i = 1; i <= lines.length; i++) { const lv = byLine.get(i); g += lv ? `<span class="${lv === "error" ? "e" : "w"}">${i}</span>\n` : (this.trace.has(i) ? `<span class="ok">${i}</span>` : i) + "\n"; }
        this.gut.innerHTML = g;
        this.scroll.querySelectorAll(".errline").forEach((x) => x.remove());
        for (const [line, lv] of byLine) { const d = document.createElement("div"); d.className = "errline " + lv; d.style.top = (8 + (line - 1) * LH) + "px"; this.scroll.append(d); }
        this.ta.style.height = Math.max(this.scroll.clientHeight, 16 + lines.length * LH) + "px";
        this.ta.style.width = Math.max(this.scroll.clientWidth, this.hl.scrollWidth) + "px";
        const pb = $("codeProblems");
        pb.innerHTML = "";
        if (!probs.length) pb.innerHTML = `<div class="info">no problems${this.app.gameProblems ? " (checked by " + esc(this.app.gameProblems.via) + ")" : ""}</div>`;
        for (const x of probs) {
            const d = el("div", x.severity === "error" ? "error" : "warning");
            d.textContent = (x.line ? `line ${x.line}${x.col ? ":" + x.col : ""}: ` : "") + x.msg;
            if (x.line) d.onclick = () => this.goto(x.line);
            pb.append(d);
        }
    }
    goto(line) {
        const lines = this.ta.value.split("\n");
        let off = 0; for (let i = 0; i < line - 1 && i < lines.length; i++) off += lines[i].length + 1;
        this.ta.focus(); this.ta.setSelectionRange(off, off + (lines[line - 1] || "").length);
        this.scroll.scrollTop = Math.max(0, (line - 4) * LH);
    }

    // ---- side: try a hit, reference
    _side() {
        const app = this.app, box = $("codeSide");
        if (this._editingSide) return;
        const t = this.trial, cat = slotCat(t.slot);
        const S = symbols(app.ast);
        box.innerHTML = "";
        box.append(el("h3", null, "Try a hit"));
        const top = el("div", "row");
        top.innerHTML = `<select id="trSlot"><option value="hit">hit</option><option value="kill">kill</option></select><select id="trW">${WEAPONS.map((w) => `<option>${w}</option>`).join("")}</select>`;
        box.append(top);
        top.querySelector("#trSlot").value = t.slot; top.querySelector("#trW").value = t.weapon;
        top.querySelector("#trSlot").onchange = (e) => { t.slot = e.target.value; this._side(); };
        top.querySelector("#trW").onchange = (e) => { t.weapon = e.target.value; this._side(); };
        const row = app.stats.rows.find((r) => r.weapon === t.weapon && r.mode === "primary");
        const defs = { damage: row ? +row.body.toFixed(3) : 10, dist: 10, splash: 1, victimHealth: app.health, shooterHealth: app.health, victimSpeed: 0, shooterSpeed: 0, time: 30, streak: 1, kills: 1 };
        const sens = el("div", "sens");
        const sensors = cat ? cat.sensors.filter((s) => s.name !== "weapon") : [];
        const fields = t.slot === "hit" ? [{ name: "damage", type: "f32", doc: "the damage the game worked out (the damage output starts here)" }, ...sensors] : sensors;
        for (const s of fields) {
            const lab = el("span", "small", esc(s.name)); lab.title = s.doc || "";
            let inp;
            if (s.type === "bool") { inp = el("input"); inp.type = "checkbox"; inp.checked = !!(t.s[s.name] ?? false); inp.onchange = () => { t.s[s.name] = inp.checked; this._run(); }; }
            else { inp = el("input"); inp.type = "number"; inp.step = "any"; inp.value = t.s[s.name] ?? defs[s.name] ?? 0; inp.oninput = () => { t.s[s.name] = +inp.value; this._run(); }; }
            sens.append(lab, inp);
        }
        box.append(sens);
        this.outEl = el("div", "out"); box.append(this.outEl);
        this._defs = defs;
        this._run();
        // reference
        box.append(el("h3", null, "Reference"));
        const ref = el("div", "ref");
        let h = "";
        for (const sl of app.cat.slots) {
            h += `<div><b>slot ${esc(sl.name)}</b> <span>${esc(sl.when || "")}</span></div>`;
            for (const s of sl.sensors) h += `<div>&nbsp; ${esc(s.name)}: ${esc(s.type)}${s.unit ? " " + esc(s.unit) : ""} <span>${esc(s.doc || "")}</span></div>`;
            for (const s of sl.outputs) h += `<div>&nbsp; <b>${esc(s.name)}</b> = ${esc(String(s.default))} <span>(output) ${esc(s.doc || "")}</span></div>`;
        }
        h += `<div style="margin-top:6px"><b>builtins</b> ${app.cat.builtins.map((b) => esc(b.sig || b.name)).join(" · ")}</div>`;
        h += `<div style="margin-top:6px"><b>fields</b> <span>&lt;weapon&gt;.&lt;field&gt; (any slot or curve can read them):</span> ${[...new Set(app.cat.fields.map((f) => f.field))].map(esc).join(" ")}</div>`;
        h += `<div style="margin-top:6px"><b>weapons</b> ${WEAPONS.join(" ")}</div>`;
        h += `<div style="margin-top:6px"><span>${S.slots.size} slot${S.slots.size === 1 ? "" : "s"}, ${S.curves.size} curve${S.curves.size === 1 ? "" : "s"}, ${S.params.size} param${S.params.size === 1 ? "" : "s"} in this set</span></div>`;
        ref.innerHTML = h;
        box.append(ref);
    }
    _run() {
        const app = this.app, t = this.trial;
        const s = { ...this._defs, ...t.s };
        const r = runSlot(app.ast, t.slot, t.weapon, { ...s, weapon: t.weapon }, symbols(app.ast), app.values);
        this.trace = new Set(r.trace);
        const o = r.outputs;
        this.outEl.textContent = r.slot ? `slot ${r.slot} ran (${r.trace.length} line${r.trace.length === 1 ? "" : "s"}, green in the gutter)\n` + Object.entries(o).map(([k, v]) => `${k} = ${typeof v === "number" ? fmtNum(v) : v}`).join("\n")
            : `no ${t.slot} slot for ${t.weapon}: the game's own result\n` + Object.entries(o).map(([k, v]) => `${k} = ${typeof v === "number" ? fmtNum(v) : v}`).join("\n");
        if (t.slot === "hit" && r.slot) this.outEl.textContent += `\n(${fmtNum(s.damage)} -> ${fmtNum(o.damage)}: x${s.damage ? fmtNum(o.damage / s.damage) : "–"})`;
        this._paintGutterOnly();
    }
    _paintGutterOnly() {
        const lines = this.ta.value.split("\n").length;
        const probs = this.app.allProblems(), byLine = new Map();
        for (const x of probs) if (x.line) byLine.set(x.line, x.severity);
        let g = "";
        for (let i = 1; i <= lines; i++) { const lv = byLine.get(i); g += lv ? `<span class="${lv === "error" ? "e" : "w"}">${i}</span>\n` : (this.trace.has(i) ? `<span class="ok">${i}</span>` : i) + "\n"; }
        this.gut.innerHTML = g;
    }
}
