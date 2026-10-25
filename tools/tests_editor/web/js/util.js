// Small shared helpers: DOM, text, time (Dallas), a line diff. No DOM in the pure parts (diffLines, unified).

export const $ = (id) => document.getElementById(id);
export function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined && text !== null) e.textContent = text;
    return e;
}
export const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
export const clip = (s, n) => (s && s.length > n ? s.slice(0, n - 1) + "…" : s || "");

// ---- time: everything is shown in Dallas time (America/Chicago)
const TF = new Intl.DateTimeFormat("en-US", { timeZone: "America/Chicago", month: "short", day: "numeric", hour: "numeric", minute: "2-digit" });
const TFS = new Intl.DateTimeFormat("en-US", { timeZone: "America/Chicago", hour: "numeric", minute: "2-digit", second: "2-digit" });
export function dallas(iso, withSec = false) {
    if (!iso) return "";
    const d = new Date(iso);
    if (isNaN(d)) return String(iso);
    return (withSec ? TFS : TF).format(d) + " CT";
}
export function secs(s) {
    if (s === null || s === undefined || !isFinite(s)) return "";
    if (s < 0.5) return "<1 s";   // (never "0 s": a group that ran took some time)
    if (s < 60) return Math.round(s) + " s";
    const m = Math.floor(s / 60), r = Math.round(s % 60);
    return m + " min" + (r ? " " + r + " s" : "");
}

// ---- line diff (prefix/suffix trim, then an LCS table on the changed middle; capped: a huge middle is one hunk)
export function diffLines(a, b) {
    const A = typeof a === "string" ? a.split("\n") : a, B = typeof b === "string" ? b.split("\n") : b;
    let p = 0;
    while (p < A.length && p < B.length && A[p] === B[p]) p++;
    let s = 0;
    while (s < A.length - p && s < B.length - p && A[A.length - 1 - s] === B[B.length - 1 - s]) s++;
    const am = A.slice(p, A.length - s), bm = B.slice(p, B.length - s);
    const ops = [];   // {t: " " | "-" | "+", a: line no in A (1-based) | null, b: line no in B | null, text}
    for (let i = 0; i < p; i++) ops.push({ t: " ", a: i + 1, b: i + 1, text: A[i] });
    if (am.length * bm.length > 4e6) {
        am.forEach((t, i) => ops.push({ t: "-", a: p + i + 1, b: null, text: t }));
        bm.forEach((t, i) => ops.push({ t: "+", a: null, b: p + i + 1, text: t }));
    } else {
        const n = am.length, m = bm.length;
        const L = Array.from({ length: n + 1 }, () => new Uint32Array(m + 1));
        for (let i = n - 1; i >= 0; i--) for (let j = m - 1; j >= 0; j--) L[i][j] = am[i] === bm[j] ? L[i + 1][j + 1] + 1 : Math.max(L[i + 1][j], L[i][j + 1]);
        let i = 0, j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && am[i] === bm[j]) { ops.push({ t: " ", a: p + i + 1, b: p + j + 1, text: am[i] }); i++; j++; }
            else if (i < n && (j >= m || L[i + 1][j] >= L[i][j + 1])) { ops.push({ t: "-", a: p + i + 1, b: null, text: am[i] }); i++; }
            else { ops.push({ t: "+", a: null, b: p + j + 1, text: bm[j] }); j++; }
        }
    }
    for (let k = 0; k < s; k++) ops.push({ t: " ", a: A.length - s + k + 1, b: B.length - s + k + 1, text: A[A.length - s + k] });
    return ops;
}
// unified diff text with `ctx` lines of context ("" when equal)
export function unified(a, b, path = "file", ctx = 3) {
    const ops = diffLines(a, b);
    const ch = ops.map((o, i) => (o.t !== " " ? i : -1)).filter((i) => i >= 0);
    if (!ch.length) return "";
    const out = [`--- a/${path}`, `+++ b/${path}`];
    let k = 0;
    while (k < ch.length) {
        let lo = Math.max(0, ch[k] - ctx), hi = Math.min(ops.length - 1, ch[k] + ctx);
        while (k + 1 < ch.length && ch[k + 1] - ctx <= hi + 1) { k++; hi = Math.min(ops.length - 1, ch[k] + ctx); }
        const seg = ops.slice(lo, hi + 1);
        const a0 = seg.find((o) => o.a)?.a ?? 1, b0 = seg.find((o) => o.b)?.b ?? 1;
        out.push(`@@ -${a0},${seg.filter((o) => o.t !== "+").length} +${b0},${seg.filter((o) => o.t !== "-").length} @@`);
        for (const o of seg) out.push(o.t + o.text);
        k++;
    }
    return out.join("\n");
}
// the added / removed lines only (for the rule checks)
export function changedLines(a, b) {
    const ops = diffLines(a, b);
    return { added: ops.filter((o) => o.t === "+"), removed: ops.filter((o) => o.t === "-") };
}

// a diff as coloured HTML lines
export function diffHtml(text) {
    if (!text) return '<span class="muted">no changes</span>';
    return text.split("\n").map((l) => {
        const c = l.startsWith("+++") || l.startsWith("---") ? "dh" : l.startsWith("@@") ? "dhunk" : l[0] === "+" ? "dadd" : l[0] === "-" ? "ddel" : "";
        return `<span class="${c}">${esc(l) || " "}</span>`;
    }).join("");
}

export function toast(msg, bad = false, ms = 3500) {
    let t = $("toast");
    t.textContent = msg;
    t.className = "toast" + (bad ? " bad" : "");
    t.hidden = false;
    clearTimeout(t._h);
    t._h = setTimeout(() => (t.hidden = true), ms);
}

// a modal: returns a promise of the clicked button's value (Esc / backdrop = null)
export function modal({ title, body, buttons, wide }) {
    return new Promise((resolve) => {
        const bg = el("div", "modal");
        const box = el("div", "modal-box" + (wide ? " wide" : ""));
        box.append(el("h3", null, title));
        if (typeof body === "string") { const d = el("div"); d.innerHTML = body; box.append(d); } else if (body) box.append(body);
        const row = el("div", "modal-btns");
        const done = (v) => { bg.remove(); document.removeEventListener("keydown", key, true); resolve(v); };
        for (const b of buttons) {
            const e = el("button", b.cls || "", b.label);
            e.dataset.value = b.value;
            e.onclick = () => done(b.value);
            row.append(e);
        }
        box.append(row);
        bg.append(box);
        bg.addEventListener("pointerdown", (e) => { if (e.target === bg) done(null); });
        const key = (e) => { if (e.key === "Escape") { e.stopPropagation(); done(null); } };
        document.addEventListener("keydown", key, true);
        document.body.append(bg);
        const prim = row.querySelector(".primary");
        if (prim) prim.focus();
    });
}
