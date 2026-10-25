// Code view: a plain textarea under a highlighted overlay. Only the lines in view (plus a margin) are highlighted and
// numbered, so a 7,000-line tests.cpp scrolls and types as fast as a short file. Light colouring for C++, YAML, sh.

import { el, esc } from "./util.js";
import { langOf } from "./model.js";

export const LH = 18;          // line height (px): keep in step with .code-* in style.css
const PAD = 8, MARGIN = 30;    // top padding; lines highlighted above / below the view

// ---------------------------------------------------------------- highlighters: (line, state) -> [html, state]
const CPP_KW = new Set("if else for while do return break continue switch case default struct class enum union namespace using typedef template typename const constexpr static auto inline extern volatile mutable explicit operator new delete this sizeof true false nullptr try catch throw public private protected virtual override final noexcept static_assert decltype".split(" "));
const CPP_T = new Set("void int bool char float double long short unsigned signed size_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t std string vector function map set pid_t FILE".split(" "));
const SH_KW = new Set("if then else elif fi for in do done while until case esac function return exit export local set shift break continue".split(" "));
const span = (c, t) => `<span class="${c}">${esc(t)}</span>`;

function cppLine(l, st) {
    let out = "", i = 0;
    if (st.block) {
        const e = l.indexOf("*/");
        if (e < 0) return [span("tc", l), st];
        out += span("tc", l.slice(0, e + 2)); i = e + 2; st = {};
    }
    if (/^\s*#/.test(l)) return [span("tpp", l), st];
    const re = /(\/\/.*$)|(\/\*)|("(?:\\.|[^"\\])*"?)|('(?:\\.|[^'\\])*'?)|(\b\d[\w.']*)|([A-Za-z_]\w*)|(\s+)|([^\w\s])/gy;
    re.lastIndex = i;
    let m;
    while (re.lastIndex < l.length && (m = re.exec(l))) {
        const t = m[0];
        if (m[1]) out += span("tc", t);
        else if (m[2]) { const e = l.indexOf("*/", re.lastIndex); if (e < 0) { out += span("tc", l.slice(m.index)); return [out, { block: true }]; } out += span("tc", l.slice(m.index, e + 2)); re.lastIndex = e + 2; }
        else if (m[3] || m[4]) out += span("ts", t);
        else if (m[5]) out += span("tn", t);
        else if (m[6]) {
            if (t === "CHECK") out += span("tchk", t);
            else if (CPP_KW.has(t)) out += span("tk", t);
            else if (CPP_T.has(t)) out += span("tt", t);
            else if (/^test[A-Z]\w*$|^\w+Tests$/.test(t)) out += span("tfn", t);
            else if (l[re.lastIndex] === "(") out += span("tf", t);
            else out += esc(t);
        } else out += esc(t);
    }
    return [out, st];
}
function shLine(l, st, ind = "") {
    let out = "";
    const re = /(#.*$)|("(?:\\.|[^"\\])*"?)|('[^']*'?)|(\$\{?[\w#?@*!-]+\}?|\$\(|\$\{\{)|(\b\d+\b)|([A-Za-z_][\w.-]*)|(\s+)|(\S)/gy;
    let m, first = true;
    re.lastIndex = 0;
    while (re.lastIndex < l.length && (m = re.exec(l))) {
        const t = m[0];
        if (m[1] && (m.index === 0 || /\s/.test(l[m.index - 1]))) out += span("tc", t);
        else if (m[1]) out += esc(t);
        else if (m[2] || m[3]) out += span("ts", t);
        else if (m[4]) out += span("tv", t);
        else if (m[5]) out += span("tn", t);
        else if (m[6]) { out += SH_KW.has(t) ? span("tk", t) : first ? span("tf", t) : esc(t); first = false; }
        else { out += esc(t); if (/[|;&]/.test(t)) first = true; }
        if (m[6] || m[2] || m[3]) first = false;
    }
    return [out, st];
}
function yamlLine(l, st) {
    if (st.block !== undefined) {
        if (!l.trim() || /^\s*/.exec(l)[0].length > st.block) return shLine(l, st);
        st = {};
    }
    let out = "", rest = l;
    const k = /^(\s*)(- )?([\w.-]+|"[^"]*"|'[^']*')(:)(?=\s|$)/.exec(l);
    if (k) {
        out += esc(k[1]) + (k[2] ? span("tp", "- ") : "") + span("tkey", k[3]) + span("tp", ":");
        rest = l.slice(k[0].length);
        if (/^\s*[|>][-+]?\s*(#.*)?$/.test(rest)) {
            const ind = k[1].length + (k[2] ? 2 : 0);
            return [out + yamlVal(rest), { block: ind }];
        }
    } else {
        const d = /^(\s*)(- )/.exec(l);
        if (d) { out += esc(d[1]) + span("tp", "- "); rest = l.slice(d[0].length); }
    }
    return [out + yamlVal(rest), st];
}
function yamlVal(v) {
    let out = "";
    const re = /(\s#.*$|^#.*$)|(\$\{\{.*?\}\})|("(?:\\.|[^"\\])*"?)|('[^']*'?)|(\b(?:true|false|null)\b)|(\b\d[\w.]*\b)|([^\s$"'#]+|\s+|.)/gy;
    let m;
    re.lastIndex = 0;
    while (re.lastIndex < v.length && (m = re.exec(v))) {
        const t = m[0];
        out += m[1] ? span("tc", t) : m[2] ? span("tx", t) : m[3] || m[4] ? span("ts", t) : m[5] ? span("tk", t) : m[6] ? span("tn", t) : /@[0-9a-f]{40}/.test(t) ? span("tsha", t) : esc(t);
    }
    return out;
}
function cmakeLine(l, st) {
    if (/^\s*#/.test(l)) return [span("tc", l), st];
    return [esc(l).replace(/\b(add_test|add_executable|target_\w+|set_tests_properties|if|endif|else|set|option)\b/g, '<span class="tk">$1</span>').replace(/(\$\{[^}]*\})/g, '<span class="tv">$1</span>'), st];
}
const HL = { cpp: cppLine, yaml: yamlLine, sh: shLine, cmake: cmakeLine };

// state at the start of each line (C++ block comments, YAML block scalars): recomputed after an edit
function lineStates(lines, lang) {
    const st = new Array(lines.length);
    let s = {};
    if (lang === "cpp") {
        for (let i = 0; i < lines.length; i++) {
            st[i] = s;
            const l = lines[i];
            if (s.block) { if (l.includes("*/")) s = {}; else continue; }
            if (l.includes("/*")) { const a = l.lastIndexOf("/*"), b = l.lastIndexOf("*/"); if (a > b && !/\/\/.*\/\*/.test(l) && !/"[^"]*\/\*[^"]*"/.test(l)) s = { block: true }; }
        }
    } else if (lang === "yaml") {
        for (let i = 0; i < lines.length; i++) {
            st[i] = s;
            const l = lines[i];
            if (s.block !== undefined) { if (!l.trim() || /^\s*/.exec(l)[0].length > s.block) continue; s = {}; }
            const k = /^(\s*)(- )?[\w.-]+:\s*[|>][-+]?\s*(#.*)?$/.exec(l);
            if (k) s = { block: k[1].length + (k[2] ? 2 : 0) };
        }
    } else st.fill(s);
    return st;
}

// ---------------------------------------------------------------- the view
export class CodeView {
    constructor(root, app) {
        this.app = app;
        this.root = root;
        root.classList.add("code");
        this.bar = el("div", "code-bar");
        this.pathEl = el("span", "code-path mono");
        this.dirtyEl = el("span", "code-dirty", "");
        this.statusEl = el("span", "code-status muted small grow");
        this.saveBtn = el("button", "primary", "Save");
        this.saveBtn.title = "Save (Ctrl+S)";
        this.diffBtn = el("button", null, "Diff");
        this.diffBtn.title = "your unsaved edits, and the file vs git HEAD";
        this.revertBtn = el("button", null, "Revert");
        this.revertBtn.title = "put the file back as git HEAD has it";
        this.bar.append(this.pathEl, this.dirtyEl, this.statusEl, this.saveBtn, this.diffBtn, this.revertBtn);
        this.body = el("div", "code-body");
        this.gutter = el("div", "code-gutter");
        this.gin = el("div", "code-gin");
        this.gutter.append(this.gin);
        this.scroll = el("div", "code-scroll");
        this.sizer = el("div", "code-sizer");
        this.range = el("div", "code-range");
        this.hl = el("pre", "code-hl");
        this.ta = el("textarea", "code-ta");
        this.ta.spellcheck = false;
        this.ta.title = "Tab indents; Esc then Tab leaves the editor";
        this.ta.setAttribute("wrap", "off");
        this.ta.setAttribute("autocomplete", "off");
        this.ta.setAttribute("autocapitalize", "off");
        this.ta.setAttribute("aria-label", "code");
        this.sizer.append(this.range, this.hl, this.ta);
        this.scroll.append(this.sizer);
        this.body.append(this.gutter, this.scroll);
        this.conflict = el("div", "code-conflict");
        this.conflict.hidden = true;
        root.append(this.bar, this.conflict, this.body);
        this.path = null; this.lines = [""]; this.states = [{}]; this.lang = "cpp"; this.marks = new Map();
        this._raf = 0;
        this.scroll.addEventListener("scroll", () => this._schedule(), { passive: true });
        this.ta.addEventListener("input", () => this._edited());
        this.ta.addEventListener("keydown", (e) => this._key(e));
        this.saveBtn.onclick = () => this.save();
        this.diffBtn.onclick = () => this.app.showDiff(this.path);
        this.revertBtn.onclick = () => this.app.revert(this.path);
        new ResizeObserver(() => this._schedule()).observe(this.scroll);
    }

    // show a file (from the app's cache), its range [a, b] highlighted and in view
    async open(path, a, b) {
        const f = await this.app.getFile(path);
        if (!f) { this.pathEl.textContent = path; this.statusEl.textContent = "can't open this file"; return; }
        if (this.path !== path) {
            if (this.path) { const old = this.app.files.get(this.path); if (old) old.scrollTop = this.scroll.scrollTop; }
            this.path = path;
            this.lang = langOf(path);
            this._setText(f.text);
            this.scroll.scrollTop = f.scrollTop || 0;
        } else if (this.ta.value !== f.text) this._setText(f.text);
        this.setRange(a, b);
        this._bar();
    }
    // the file's text changed under us (a save / a reload): keep the scroll
    refresh() {
        if (!this.path) return;
        const f = this.app.files.get(this.path);
        if (f && f.text !== this.ta.value) { const top = this.scroll.scrollTop; this._setText(f.text); this.scroll.scrollTop = top; }
        this._bar();
    }
    _setText(t) {
        this.ta.value = t;
        this.lines = t.split("\n");
        this.states = lineStates(this.lines, this.lang);
        this._size();
        this._render();
    }
    setRange(a, b) {
        this.rangeA = a || 0; this.rangeB = b || a || 0;
        if (a) {
            this.range.style.display = "block";
            this.range.style.top = PAD + (a - 1) * LH + "px";
            this.range.style.height = (this.rangeB - a + 1) * LH + "px";
            const h = this.scroll.clientHeight || 600;
            const top = PAD + (a - 1) * LH, bottom = PAD + this.rangeB * LH;
            if (top < this.scroll.scrollTop || bottom > this.scroll.scrollTop + h) this.scroll.scrollTop = Math.max(0, top - Math.min(120, h / 4));
        } else this.range.style.display = "none";
        this._render();
    }
    gotoLine(n) { this.setRange(n, n); const i = this.lines.slice(0, n - 1).reduce((s, l) => s + l.length + 1, 0); this.ta.focus({ preventScroll: true }); this.ta.setSelectionRange(i, i); }

    _size() {
        let cols = 0;
        for (const l of this.lines) if (l.length > cols) cols = l.length;
        const h = PAD * 2 + this.lines.length * LH;
        this.sizer.style.height = h + "px";
        this.sizer.style.width = `calc(${cols + 6}ch + 24px)`;
        this.ta.style.height = h + "px";
    }
    _edited() {
        const f = this.app.files.get(this.path);
        const n0 = this.lines.length;
        this.lines = this.ta.value.split("\n");
        if (f) f.text = this.ta.value;
        clearTimeout(this._st);
        if (this.lang === "cpp" || this.lang === "yaml") this._st = setTimeout(() => { this.states = lineStates(this.lines, this.lang); this._render(); }, 120);
        if (this.lines.length !== n0) this.states = lineStates(this.lines, this.lang);
        this._size();
        this._render();
        this._bar();
        this.app.codeEdited(this.path);
    }
    _schedule() { if (!this._raf) this._raf = requestAnimationFrame(() => { this._raf = 0; this._render(); }); }
    _render() {
        const top = this.scroll.scrollTop, h = this.scroll.clientHeight || 800;
        const a = Math.max(0, Math.floor((top - PAD) / LH) - MARGIN), b = Math.min(this.lines.length, Math.ceil((top + h) / LH) + MARGIN);
        const hlf = HL[this.lang] || shLine;
        let html = "", g = "";
        for (let i = a; i < b; i++) {
            const st = this.states[i] || {};
            html += hlf(this.lines[i], { ...st })[0] + "\n";
            const m = this.marks.get(i + 1);
            g += m ? `<span class="${m}">${i + 1}</span>\n` : i + 1 + "\n";
        }
        this.hl.style.top = PAD + a * LH + "px";
        this.hl.innerHTML = html;
        this.gin.style.transform = `translateY(${PAD + a * LH - top}px)`;
        this.gin.innerHTML = g;
    }
    setMarks(marks) { this.marks = marks || new Map(); this._render(); }
    _bar() {
        const f = this.app.files.get(this.path);
        this.pathEl.textContent = this.path || "";
        const dirty = !!(f && f.text !== f.disk);
        this.dirtyEl.textContent = dirty ? "● unsaved" : "";
        this.dirtyEl.title = dirty ? "edited here, not saved yet" : "";
        this.saveBtn.disabled = !dirty;
        this.statusEl.textContent = `${this.lines.length} lines` + (this.rangeA ? ` · lines ${this.rangeA}-${this.rangeB}` : "");
    }
    showConflict(html, actions) {
        this.conflict.hidden = !html;
        this.conflict.textContent = "";
        if (!html) return;
        const t = el("span", "grow"); t.innerHTML = html;
        this.conflict.append(t);
        for (const [label, fn, cls] of actions) { const b = el("button", cls || "", label); b.onclick = fn; this.conflict.append(b); }
    }

    _key(e) {
        if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") { e.preventDefault(); this.save(); return; }
        const out = this._tabOut;   // (Esc, then Tab / Shift+Tab: leave the editor, as a keyboard user expects)
        this._tabOut = e.key === "Escape";
        if (e.key === "Tab" && out) return;
        if (e.key === "Tab" && !e.ctrlKey && !e.altKey) {
            e.preventDefault();
            const ind = this.lang === "cpp" ? "    " : "  ";
            this.ta.setRangeText(ind, this.ta.selectionStart, this.ta.selectionEnd, "end");
            this._edited();
        } else if (e.key === "Enter" && !e.ctrlKey && !e.altKey && !e.metaKey) {   // keep the indent
            const v = this.ta.value, s = this.ta.selectionStart;
            const line = v.slice(v.lastIndexOf("\n", s - 1) + 1, s);
            e.preventDefault();
            this.ta.setRangeText("\n" + /^\s*/.exec(line)[0], s, this.ta.selectionEnd, "end");
            this._edited();
            // keep the caret in view (the textarea itself doesn't scroll)
            const row = v.slice(0, s).split("\n").length;
            const y = PAD + row * LH;
            if (y + LH > this.scroll.scrollTop + this.scroll.clientHeight) this.scroll.scrollTop = y + LH - this.scroll.clientHeight + 20;
        }
    }
    save() { if (this.path) this.app.saveFile(this.path, this.ta.value, { source: "code" }); }
}
