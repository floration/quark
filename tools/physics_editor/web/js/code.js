// Code: the set's raw .qmode text, editable, two-way with Values (an edit here is parsed as the game parses it,
// CustomRules::fromText with its clamps; a Values edit rewrites only the movement lines here). Problems show at
// their line. The gamemode editor's section (from a `logic` line on) is shown below, read only.
import { highlight, lineKinds, mergeQmode, isLogicLine, parseQmode } from "./qmode.js";

const $ = (id) => document.getElementById(id);

function splitLogic(text) {
    const lines = text.split("\n");
    let off = 0;
    for (const l of lines) { if (isLogicLine(l.replace(/\r$/, ""))) return [text.slice(0, off), text.slice(off)]; off += l.length + 1; }
    return [text, ""];
}

export class CodeView {
    constructor(app) {
        this.app = app;
        this.ta = $("codeTa"); this.hl = $("codeHl"); this.gut = $("gutter"); this.scroll = $("codeScroll");
        this.ta.addEventListener("input", () => this._input());
        this.ta.addEventListener("keydown", (e) => {
            if (e.key === "Tab") { e.preventDefault(); document.execCommand("insertText", false, "  "); }
        });
        this.scroll.addEventListener("scroll", () => { this.gut.scrollTop = this.scroll.scrollTop; });
        $("codeFormat").onclick = () => { this.app.setCodeText(mergeQmode(this.app.set.text, this.app.physics, this.app.set.name, this.app.knownMove)); this.load(); };
        app.on("physics", (from) => { if (from !== "code") this.load(); });
        app.on("page", (p) => { if (p === "code") this.load(); });
        this.load();
    }
    load() {
        const [head, logic] = splitLogic(this.app.set.text);
        this.logic = logic;
        if (this.ta.value !== head) {
            const s = this.ta.selectionStart, e = this.ta.selectionEnd;
            this.ta.value = head;
            if (document.activeElement === this.ta) this.ta.setSelectionRange(Math.min(s, head.length), Math.min(e, head.length));
        }
        $("logicBox").style.display = logic ? "" : "none";
        $("logicPre").textContent = logic;
        const s = this.app.set;
        $("codeFile").textContent = s.saved ? `${this.app.modesDir || "custom_modes"}/${s.saved}.qmode` : "not saved yet (Save writes custom_modes/<name>.qmode)";
        this._paint(this.app.codeParse && this.app.codeParse.lines && this.app.codeParse.lines.join("\n") === this.app.set.text ? this.app.codeParse : parseQmode(this.app.set.text, this.app.cat));
    }
    _input() {
        let head = this.ta.value;
        if (this.logic && head && !head.endsWith("\n")) head += "\n";
        const p = this.app.setCodeText(head + this.logic);
        this._paint(p);
    }
    _paint(p) {
        const text = this.ta.value;
        this.hl.innerHTML = highlight(text) + "\n";
        const n = text.split("\n").length;
        const byLine = new Map();
        const probs = [...p.problems];
        // edits to lines that aren't movement: a save keeps the file's own line (only movement lines are written)
        const base = this.app.set.baseText;
        if (base) {
            const others = (t) => { const h = splitLogic(t)[0], k = lineKinds(h); return h.split("\n").filter((l, i) => k[i] === "other").map((l) => l.replace(/\r$/, "")).join("\n"); };
            if (others(base) !== others(text)) probs.push({ line: 0, level: "info", msg: "a line that isn't movement was changed here: a save keeps the file's own (only the movement lines are written; mode, weapons, health and the rest are the gamemode editor's)" });
        }
        for (const x of probs) if (x.line) { const cur = byLine.get(x.line); if (!cur || x.level === "error") byLine.set(x.line, x.level); }
        let g = "";
        for (let i = 1; i <= n; i++) { const lv = byLine.get(i); g += lv ? `<span class="${lv === "error" ? "e" : "w"}">${i}</span>\n` : i + "\n"; }
        this.gut.innerHTML = g;
        // error bands
        this.scroll.querySelectorAll(".errline").forEach((x) => x.remove());
        for (const [line, lv] of byLine) {
            if (lv === "info") continue;
            const d = document.createElement("div"); d.className = "errline " + lv; d.style.top = (8 + (line - 1) * 19) + "px";
            this.scroll.append(d);
        }
        // height: the textarea grows with its text (the scroll box scrolls)
        this.ta.style.height = Math.max(this.scroll.clientHeight, 16 + n * 19) + "px";
        this.ta.style.width = Math.max(this.scroll.clientWidth, this.hl.scrollWidth) + "px";
        const pb = $("codeProblems");
        pb.innerHTML = "";
        if (!probs.length) pb.innerHTML = `<div class="info">no problems: the game reads ${p.name ? "'" + p.name + "'" : "this"} the same way</div>`;
        for (const x of probs) {
            const d = document.createElement("div"); d.className = x.level;
            d.textContent = (x.line ? `line ${x.line}: ` : "") + x.msg;
            if (x.line) d.onclick = () => this.goto(x.line);
            pb.append(d);
        }
    }
    goto(line) {
        const lines = this.ta.value.split("\n");
        let off = 0; for (let i = 0; i < line - 1 && i < lines.length; i++) off += lines[i].length + 1;
        this.ta.focus(); this.ta.setSelectionRange(off, off + (lines[line - 1] || "").length);
        this.scroll.scrollTop = Math.max(0, (line - 4) * 19);
    }
}
