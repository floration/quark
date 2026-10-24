// Code graph: how the values flow through the movement code. Left, the settings; middle, the steps of one 1 ms
// pmoveStep that read them, in the order they run; right, what those steps change (velocity, position, the floor
// flag, dash charges...). All of it is read from src/game/pmove.cpp as it is (source.js): an edge is a real
// mt.m[MV_*] / mt.<x> read in that step (dashed: inside a function the step calls). Changed settings and every edge
// they reach are highlighted. Drag to pan, wheel to zoom, click a step or a setting to see its code.
import { OUTPUTS } from "./source.js";
import { resolve } from "./expr.js";
import { getVal, hasExpr } from "./params.js";
import { fmtV } from "./exprui.js";

const $ = (id) => document.getElementById(id);
const css = (n) => getComputedStyle(document.body).getPropertyValue(n).trim();
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

export class CodeGraphView {
    constructor(app) {
        this.app = app; this.c = $("cgCanvas"); this.tip = $("cgTip");
        this.cam = { x: 0, y: 0, s: 1 }; this.hover = null;
        app.on("page", (p) => { if (p === "codegraph") this.enter(); });
        app.on("physics", () => { if (app.page === "codegraph") this.build(); });
        $("cgOnly").onchange = () => this.build();
        $("cgVia").onchange = () => this.build();
        $("cgFit").onclick = () => this.fit();
        app.on("theme", () => this.request());
        const c = this.c;
        c.addEventListener("pointerdown", (e) => { c.setPointerCapture(e.pointerId); this.drag = { x: e.clientX, y: e.clientY, moved: 0 }; });
        c.addEventListener("pointermove", (e) => {
            const r = c.getBoundingClientRect(), mx = e.clientX - r.left, my = e.clientY - r.top;
            if (this.drag) {
                const dx = e.clientX - this.drag.x, dy = e.clientY - this.drag.y;
                this.drag.moved += Math.abs(dx) + Math.abs(dy);
                this.cam.x -= dx / this.cam.s; this.cam.y -= dy / this.cam.s; this.drag.x = e.clientX; this.drag.y = e.clientY;
                this.request(); return;
            }
            const n = this.hit(mx, my);
            if (n !== this.hover) { this.hover = n; c.style.cursor = n ? "pointer" : "grab"; this.request(); }
            this._tip(n, mx, my);
        });
        c.addEventListener("pointerup", (e) => {
            const d = this.drag; this.drag = null;
            if (d && d.moved < 4) { const r = c.getBoundingClientRect(); this.click(this.hit(e.clientX - r.left, e.clientY - r.top)); }
        });
        c.addEventListener("pointerleave", () => { this.hover = null; this.tip.style.display = "none"; this.request(); });
        c.addEventListener("dblclick", (e) => { const r = c.getBoundingClientRect(); if (!this.hit(e.clientX - r.left, e.clientY - r.top)) this.fit(); });
        c.addEventListener("wheel", (e) => {
            e.preventDefault();
            const r = c.getBoundingClientRect(), mx = e.clientX - r.left, my = e.clientY - r.top;
            const wx = this.cam.x + mx / this.cam.s, wy = this.cam.y + my / this.cam.s;
            this.cam.s = Math.max(0.25, Math.min(4, this.cam.s * Math.exp(-e.deltaY * 0.0015)));
            this.cam.x = wx - mx / this.cam.s; this.cam.y = wy - my / this.cam.s;
            this.request();
        }, { passive: false });
        new ResizeObserver(() => this.request()).observe(c);
    }
    async enter() {
        if (!this.src) {
            const s = await this.app.moveCode.load();
            if (!s) { $("cgInfo").textContent = "the movement code isn't available (serve.py /api/source)"; return; }
            this.src = s;
        }
        this.build();
        if (!this.fitted) this.fit();
    }
    // nodes and edges from the code, laid out in three columns (settings and outputs ordered by where their edges go)
    build() {
        if (!this.src) return;
        const app = this.app, cat = app.cat, ph = app.physics, res = resolve(ph, cat);
        const via = $("cgVia").checked, only = $("cgOnly").checked;
        const changed = (k) => (k === "dash" || k === "grapple" ? ph[k] === false : Math.abs((res.S[k] ?? getVal(ph, k)) - 1) > 1e-9 || hasExpr(ph, k));
        const steps = this.src.steps.map((st, i) => ({ type: "step", id: "s:" + st.id, st, i, label: st.label, sub: `line ${st.start}–${st.end}` }));
        const setMap = new Map(), outMap = new Map(), edges = [];
        for (const s of steps) {
            const uses = [...s.st.uses, ...(via ? s.st.viaUses || [] : [])];
            const seen = new Set();
            for (const u of uses) {
                if (only && !changed(u.key)) continue;
                const id = u.key + (u.via ? "|" + u.via : "");
                if (seen.has(id)) continue; seen.add(id);
                if (!setMap.has(u.key)) setMap.set(u.key, { type: "set", id: "k:" + u.key, key: u.key, steps: new Set(), uses: [] });
                const sn = setMap.get(u.key);
                sn.steps.add(s.i); sn.uses.push(u);
                if (!edges.some((e) => e.a === sn && e.b === s && !!e.via === !!u.via)) edges.push({ a: sn, b: s, via: u.via, constant: u.constant });
            }
            for (const o of s.st.outputs) {
                const od = OUTPUTS.find((x) => x.id === o);
                if (!outMap.has(o)) outMap.set(o, { type: "out", id: "o:" + o, label: od ? od.label : o, steps: new Set() });
                outMap.get(o).steps.add(s.i);
                edges.push({ a: s, b: outMap.get(o) });
            }
        }
        // steps without a setting when filtering: kept (they show the order), dimmed
        const bary = (n) => [...n.steps].reduce((a, b) => a + b, 0) / Math.max(1, n.steps.size);
        const sets = [...setMap.values()].sort((a, b) => bary(a) - bary(b) || a.key.localeCompare(b.key));
        const outs = [...outMap.values()].sort((a, b) => bary(a) - bary(b));
        // the step boxes as wide as their longest line (the title, or "line a–b · the functions it calls()")
        const mc = (this._mc || (this._mc = document.createElement("canvas").getContext("2d")));
        let STW = 250;
        for (const s of steps) {
            mc.font = "600 12px system-ui"; const a = mc.measureText(`${s.i + 1}. ${s.label}`).width;
            mc.font = "10.5px system-ui"; const b = mc.measureText(s.sub + (s.st.calls.length ? " · " + s.st.calls.join(", ") + "()" : "")).width;
            STW = Math.max(STW, Math.ceil(Math.max(a, b)) + 20);
        }
        const SW = 200, OW = 200, GX = 150;
        const stepH = 34, setH = 22, outH = 30;
        const stepGap = Math.max(stepH + 12, (sets.length * (setH + 4)) / Math.max(1, steps.length));
        steps.forEach((s, i) => { s.x = SW + GX; s.y = i * stepGap; s.w = STW; s.h = stepH; });
        const total = steps.length ? steps[steps.length - 1].y + stepH : 0;
        const place = (list, x, w, h, gap) => {
            // ideal y: the mean of the connected steps' centres; then pushed apart in order, then centred
            let y = -1e9;
            for (const n of list) {
                const ideal = [...n.steps].reduce((a, i) => a + steps[i].y + stepH / 2, 0) / Math.max(1, n.steps.size) - h / 2;
                n.y = Math.max(ideal, y + h + gap); y = n.y; n.x = x; n.w = w; n.h = h;
            }
            const span = list.length ? list[list.length - 1].y + h - list[0].y : 0;
            if (span > total && list.length) { const off = list[0].y - (total - span) / 2; for (const n of list) n.y -= off; }
        };
        place(sets, 0, SW, setH, 4);
        place(outs, SW + GX + STW + GX, OW, outH, 10);
        for (const n of sets) {
            const p = cat.byKey.get(n.key), v = n.key === "dash" || n.key === "grapple" ? (ph[n.key] !== false ? "on" : "off") : "×" + fmtV(res.S[n.key] ?? getVal(ph, n.key));
            n.label = n.key; n.value = (hasExpr(ph, n.key) ? (res.info[n.key] && res.info[n.key].dynamic ? "ƒ dyn " : "ƒ ") : "") + v;
            n.changed = changed(n.key); n.p = p;
        }
        // what changed settings reach
        for (const e of edges) e.hot = false;
        const hotSteps = new Set();
        for (const e of edges) if (e.a.type === "set" && e.a.changed) { e.hot = true; hotSteps.add(e.b); }
        for (const e of edges) if (e.a.type === "step" && hotSteps.has(e.a)) e.hot = true;
        for (const s of steps) s.hot = hotSteps.has(s);
        for (const o of outs) o.hot = edges.some((e) => e.b === o && e.hot);
        this.nodes = [...sets, ...steps, ...outs]; this.edges = edges; this.steps = steps;
        this.bounds = { x0: -20, y0: Math.min(-20, ...this.nodes.map((n) => n.y - 30)), x1: SW + GX + STW + GX + OW + 20, y1: Math.max(total, ...this.nodes.map((n) => n.y + n.h)) + 20 };
        const nch = sets.filter((s) => s.changed).length;
        $("cgInfo").textContent = `${sets.length} settings → ${steps.length} steps of pmoveStep (src/game/pmove.cpp) → ${outs.length} kinds of state · ${nch ? nch + " changed setting" + (nch > 1 ? "s" : "") + " highlighted" : "nothing changed: change a value to see where it goes"} · drag: pan · wheel: zoom · click: the code`;
        this.request();
    }
    fit() {
        const W = this.c.clientWidth, H = this.c.clientHeight, b = this.bounds;
        if (!W || !H || !b) return;
        this.fitted = true;
        // the whole graph when its text stays readable (8 px or more); else as wide as the view, from the top (drag for the rest)
        const sw = W / (b.x1 - b.x0 + 40), sh = H / (b.y1 - b.y0 + 60);
        let sc = Math.min(sw, sh);
        if (sc < 0.72) sc = Math.max(sc, Math.min(sw, 0.72));
        this.cam.s = Math.max(0.3, Math.min(1.6, sc));
        this.cam.x = (b.x0 + b.x1) / 2 - W / 2 / this.cam.s; this.cam.y = b.y0 - 30 / this.cam.s;
        this.request();
    }
    hit(mx, my) {
        if (!this.nodes) return null;
        const x = this.cam.x + mx / this.cam.s, y = this.cam.y + my / this.cam.s;
        return this.nodes.find((n) => x >= n.x && x <= n.x + n.w && y >= n.y && y <= n.y + n.h) || null;
    }
    click(n) {
        if (!n) return;
        if (n.type === "step") this.app.moveCode.goto("pmove.cpp", n.st.start);
        else if (n.type === "set") { this.app.moveCode.goto("pmove.cpp", n.uses[0].line); setTimeout(() => this.app.moveCode.cycleUses(n.key), 50); }
        else if (n.type === "out") { const s = this.steps[[...n.steps][0]]; if (s) this.app.moveCode.goto("pmove.cpp", s.st.start); }
    }
    _tip(n, mx, my) {
        const t = this.tip;
        if (!n) { t.style.display = "none"; return; }
        let h = "";
        if (n.type === "set") {
            const p = n.p, info = resolve(this.app.physics, this.app.cat).info[n.key];
            h = `<b>${esc(n.key)}</b> ${esc(p ? p.label : "")} <span class="${n.changed ? "warn" : "muted"}">${esc(n.value)}</span>${info ? `<br><code>${esc(info.text)}</code>` : ""}<br>${esc(p ? p.doc || "" : "")}`
                + `<br><span class="muted">read in: ${[...n.steps].map((i) => esc(this.steps[i].label)).join(", ")}${n.uses.some((u) => u.via) ? " (some inside " + [...new Set(n.uses.filter((u) => u.via).map((u) => u.via + "()"))].join(", ") + ")" : ""}</span>`
                + `<br><span class="muted">scales ${[...new Set(n.uses.map((u) => u.constant).filter(Boolean))].join(", ") || "(no constant)"} · click: its code</span>`;
        } else if (n.type === "step") {
            const st = n.st;
            h = `<b>${esc(st.label)}</b> <span class="muted">pmove.cpp ${st.start}–${st.end}${st.calls.length ? " · calls " + st.calls.map((c) => c + "()").join(", ") : ""}</span><br>${esc(st.note)}<br><span class="muted">reads ${[...new Set([...st.uses, ...(st.viaUses || [])].map((u) => u.key))].join(", ") || "no setting"} · changes ${[...st.outputs].map((o) => (OUTPUTS.find((x) => x.id === o) || { label: o }).label).join(", ")} · click: its code</span>`;
        } else {
            h = `<b>${esc(n.label)}</b><br><span class="muted">changed by: ${[...n.steps].map((i) => esc(this.steps[i].label)).join(", ")}</span>`;
        }
        t.innerHTML = h; t.style.display = "block";
        const W = this.c.clientWidth, H = this.c.clientHeight;
        t.style.left = Math.min(W - t.offsetWidth - 6, mx + 14) + "px"; t.style.top = Math.min(H - t.offsetHeight - 6, my + 14) + "px";
    }
    request() { if (this.raf) return; this.raf = requestAnimationFrame(() => { this.raf = 0; this.draw(); }); }
    draw() {
        const c = this.c, dpr = window.devicePixelRatio || 1, W = c.clientWidth, H = c.clientHeight;
        if (!W || !H || !this.nodes) return;
        if (c.width !== Math.round(W * dpr) || c.height !== Math.round(H * dpr)) { c.width = Math.round(W * dpr); c.height = Math.round(H * dpr); }
        const g = c.getContext("2d");
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        g.fillStyle = css("--bg"); g.fillRect(0, 0, W, H);
        g.setTransform(dpr * this.cam.s, 0, 0, dpr * this.cam.s, -this.cam.x * this.cam.s * dpr, -this.cam.y * this.cam.s * dpr);
        const line = css("--line2"), warn = css("--warn"), acc = css("--acc"), fg = css("--fg"), fg3 = css("--fg3"), bg1 = css("--bg1"), bg2 = css("--bg2");
        const hv = this.hover;
        const near = (e) => hv && (e.a === hv || e.b === hv);
        // column titles
        g.font = "600 11px system-ui"; g.fillStyle = fg3; g.textBaseline = "alphabetic"; g.textAlign = "left";
        const tY = this.bounds.y0 + 6;
        const cols = [[0, "SETTINGS (the edited set)"], [this.steps[0] ? this.steps[0].x : 350, "PMOVESTEP, IN ORDER (1 ms)"], [(this.nodes.find((n) => n.type === "out") || { x: 750 }).x, "WHAT THEY CHANGE"]];
        for (const [x, t] of cols) g.fillText(t, x, tY);
        // the order of the steps: a spine
        g.strokeStyle = line; g.lineWidth = 2; g.setLineDash([]);
        for (let i = 1; i < this.steps.length; i++) {
            const a = this.steps[i - 1], b = this.steps[i];
            g.beginPath(); g.moveTo(a.x + a.w / 2, a.y + a.h); g.lineTo(b.x + b.w / 2, b.y); g.stroke();
        }
        // edges (the quiet ones first)
        const order = [...this.edges].sort((a, b) => (a.hot || near(a) ? 1 : 0) - (b.hot || near(b) ? 1 : 0));
        for (const e of order) {
            const x0 = e.a.x + e.a.w, y0 = e.a.y + e.a.h / 2, x1 = e.b.x, y1 = e.b.y + e.b.h / 2, mx = (x0 + x1) / 2;
            const hot = e.hot, nr = near(e);
            g.strokeStyle = nr ? acc : hot ? warn : line;
            g.globalAlpha = nr || hot ? 0.95 : hv ? 0.25 : 0.55;
            g.lineWidth = nr ? 2.2 : hot ? 1.8 : 1;
            g.setLineDash(e.via ? [4, 3] : []);
            g.beginPath(); g.moveTo(x0, y0); g.bezierCurveTo(mx, y0, mx, y1, x1, y1); g.stroke();
        }
        g.globalAlpha = 1; g.setLineDash([]);
        // nodes
        for (const n of this.nodes) {
            const isH = n === hv, rel = hv && this.edges.some((e) => (e.a === hv && e.b === n) || (e.b === hv && e.a === n));
            g.fillStyle = n.type === "step" ? bg2 : bg1;
            g.strokeStyle = isH || rel ? acc : n.changed || n.hot ? warn : line;
            g.lineWidth = isH ? 2 : n.changed || n.hot || rel ? 1.6 : 1;
            rr(g, n.x, n.y, n.w, n.h, n.type === "set" ? 11 : 6); g.fill(); g.stroke();
            g.textBaseline = "middle"; g.textAlign = "left";
            if (n.type === "set") {
                g.font = "11.5px ui-monospace, monospace"; g.fillStyle = n.changed ? warn : fg;
                g.fillText(n.label, n.x + 9, n.y + n.h / 2);
                g.textAlign = "right"; g.fillStyle = n.changed ? warn : fg3; g.fillText(n.value, n.x + n.w - 9, n.y + n.h / 2);
            } else if (n.type === "step") {
                g.font = "600 12px system-ui"; g.fillStyle = n.hot ? warn : fg;
                g.fillText(`${n.i + 1}. ${n.label}`, n.x + 9, n.y + 12);
                g.font = "10.5px system-ui"; g.fillStyle = fg3;
                g.fillText(n.sub + (n.st.calls.length ? " · " + n.st.calls.join(", ") + "()" : ""), n.x + 9, n.y + 25);
            } else {
                g.font = "12px system-ui"; g.fillStyle = n.hot ? warn : fg;
                g.fillText(n.label, n.x + 9, n.y + n.h / 2);
            }
        }
    }
}
function rr(g, x, y, w, h, r) { g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r); g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath(); }
