// Blocks: the scenario as a stack of input blocks on a timeline (drag from the palette, drag to reorder, drag back
// to the palette to delete). Each block is a stretch of time with what is held; they compile to the scenario's steps
// (INTERFACE.md: a step holds until the next). A preset or a loaded scenario is read back into blocks (a step that
// no block describes becomes a raw "step" block, so nothing is lost). { } JSON shows and edits the scenario itself.

const KEYS = { "": [0, 0], W: [127, 0], S: [-127, 0], A: [0, -127], D: [0, 127], WA: [127, -127], WD: [127, 127], SA: [-127, -127], SD: [-127, 127] };
const keysOf = (f, s) => Object.keys(KEYS).find((k) => KEYS[k][0] === (f | 0) && KEYS[k][1] === (s | 0)) ?? null;
const keyLabel = (k) => (k ? k.split("").join("+") : "nothing");
const PRESS = 30;   // ms a "press" holds the button

export const KINDS = {
    move:      { label: "Hold keys",          cat: "move",   fields: { keys: "W", ms: 500 }, desc: "hold movement keys" },
    wait:      { label: "Wait",               cat: "wait",   fields: { ms: 500 }, desc: "let go of everything" },
    turn:      { label: "Turn",               cat: "look",   fields: { keys: "W", deg: 90, ms: 500 }, desc: "turn by deg over the time (+ = left)" },
    look:      { label: "Look at",            cat: "look",   fields: { yaw: 0, pitch: 0 }, desc: "set the view (instant; yaw 0 = -z, -90 = +x; pitch + = up)" },
    jump:      { label: "Jump",               cat: "button", fields: { keys: "W", press: 30, ms: 700 }, desc: "press jump (held for press ms), then hold keys" },
    holdjump:  { label: "Hold jump (bhop)",   cat: "button", fields: { keys: "W", ms: 2000 }, desc: "jump held: hop at every landing" },
    edgejump:  { label: "Jump at the edge",   cat: "button", fields: { keys: "W", ms: 1500 }, desc: "press jump on the last ms the floor holds you" },
    dash:      { label: "Dash",               cat: "button", fields: { keys: "W", pitch: 0, press: 20, ms: 400 }, desc: "press dash (held for press ms; pitch: aim up / down)" },
    dashjump:  { label: "Dash jump",          cat: "button", fields: { keys: "W", ms: 700 }, desc: "dash, jump 40 ms into it" },
    superdash: { label: "Super dash",         cat: "button", fields: { keys: "W", pitch: -40, ms: 700 }, desc: "dash aimed down with jump" },
    grapple:   { label: "Grapple",            cat: "button", fields: { keys: "W", pitch: 45, ms: 1500 }, desc: "hold the grapple, aimed up by pitch (empty: keep the current view)" },
    crouch:    { label: "Crouch",             cat: "button", fields: { keys: "W", ms: 800 }, desc: "hold crouch (the game ignores it since 1.17)" },
    strafe:    { label: "Strafe-bot hops",    cat: "bot",    fields: { side: "D", ms: 3000, swap: "swing" }, desc: "jump held, A / D at the best air-strafe angle; swap: off, at each landing, or weave 25 degrees either side of the course" },
    step:      { label: "Step",               cat: "wait",   fields: { ms: 100, forward: 0, side: 0, jump: false, dash: false, hook: false, crouch: false, strafeBot: false, strafeSwitch: false, jumpAtEdge: false, yawRate: 0, pitchRate: 0, yaw: null, pitch: null, extra: null }, desc: "a raw step (any fields)" },
};
const CAT_ORDER = [["move", "move"], ["look", "view"], ["button", "buttons"], ["bot", "strafe bot"], ["wait", "time"]];

let nextId = 1;
export const newBlock = (kind, f = {}) => ({ id: nextId++, kind, ...JSON.parse(JSON.stringify(KINDS[kind].fields)), ...f });

const pressOf = (b, d) => Math.max(1, b.press == null || b.press === "" ? d : b.press | 0);

// blocks -> steps (and the duration)
export function compile(start, blocks, tailMs = 600) {
    const steps = [];
    let t = 0, look = null;
    const push = (s) => { if (look) { Object.assign(s, look); look = null; } steps.push(s); };
    for (const b of blocks) {
        const [f, s] = KEYS[b.keys] || [0, 0];
        const ms = Math.max(0, b.ms | 0);
        switch (b.kind) {
            case "look": look = { yaw: +b.yaw || 0, pitch: +b.pitch || 0 }; continue;
            case "move": push({ atMs: t, forward: f, side: s }); break;
            case "wait": push({ atMs: t, forward: 0, side: 0 }); break;
            case "turn": push({ atMs: t, forward: f, side: s, yawRate: ms ? +(b.deg / (ms / 1000)).toFixed(3) : 0 }); break;
            case "jump": { const pr = pressOf(b, PRESS); push({ atMs: t, forward: f, side: s, jump: true }); if (ms > pr) steps.push({ atMs: t + pr, forward: f, side: s }); break; }
            case "holdjump": push({ atMs: t, forward: f, side: s, jump: true }); break;
            case "edgejump": push({ atMs: t, forward: f, side: s, jumpAtEdge: true }); break;
            case "crouch": push({ atMs: t, forward: f, side: s, crouch: true }); break;
            case "dash": { const pr = pressOf(b, 20); push({ atMs: t, forward: f, side: s, dash: true, ...(b.pitch ? { pitch: +b.pitch } : {}) }); if (ms > pr) steps.push({ atMs: t + pr, forward: f, side: s, ...(b.pitch ? { pitch: 0 } : {}) }); break; }
            case "dashjump": push({ atMs: t, forward: f, side: s, dash: true }); steps.push({ atMs: t + 40, forward: f, side: s, jump: true }); if (ms > 70) steps.push({ atMs: t + 70, forward: f, side: s }); break;
            case "superdash": push({ atMs: t, forward: f, side: s, pitch: +b.pitch || -40, dash: true, jump: true }); if (ms > 40) steps.push({ atMs: t + 40, forward: f, side: s, pitch: 0 }); break;
            case "grapple": push({ atMs: t, forward: f, side: s, hook: true, ...(b.pitch != null && b.pitch !== "" ? { pitch: +b.pitch } : {}) }); break;
            case "strafe": push({ atMs: t, forward: 0, side: b.side === "A" ? -127 : 127, jump: true, strafeBot: true, ...(b.swap === "land" ? { strafeSwitch: "land" } : b.swap === "swing" || b.swap === true ? { strafeSwitch: true } : {}) }); break;
            case "step": {
                const st = { ...(b.extra || {}), atMs: t, forward: b.forward | 0, side: b.side | 0 };
                for (const k of ["jump", "dash", "hook", "crouch", "strafeBot", "strafeSwitch", "jumpAtEdge"]) if (b[k]) st[k] = b[k];
                if (+b.yawRate) st.yawRate = +b.yawRate;
                if (+b.pitchRate) st.pitchRate = +b.pitchRate;
                if (b.yaw != null && b.yaw !== "") st.yaw = +b.yaw;
                if (b.pitch != null && b.pitch !== "") st.pitch = +b.pitch;
                push(st); break;
            }
        }
        t += ms;
    }
    if (look) steps.push({ atMs: t, forward: 0, side: 0, ...look });
    return { steps, durationMs: Math.max(1, t + tailMs) };
}

// steps -> blocks (what compile makes is read back as those blocks; anything else as raw steps)
export function decompile(sc) {
    const st = [...(sc.steps || [])].sort((a, b) => a.atMs - b.atMs);
    const out = [];
    const end = sc.durationMs || 0;
    let i = 0;
    const plain = (s) => !s.jump && !s.dash && !s.hook && !s.crouch && !s.strafeBot && !s.jumpAtEdge && !s.yawRate && !s.pitchRate;
    const until = (j) => (j < st.length ? st[j].atMs : Math.max(end - 600, st[st.length - 1] ? st[st.length - 1].atMs + 100 : 0));
    let lastEnd = 0;
    while (i < st.length) {
        const s = st[i], k = keysOf(s.forward, s.side), nx = st[i + 1];
        if (s.atMs > lastEnd) out.push(newBlock("wait", { ms: s.atMs - lastEnd }));
        const lookOf = () => (s.yaw != null || (s.pitch != null && !s.dash && !s.hook)) ? out.push(newBlock("look", { yaw: s.yaw ?? 0, pitch: s.pitch ?? 0 })) : 0;
        let b = null, used = 1;
        if (k !== null && s.strafeBot && s.jump && !s.forward) b = newBlock("strafe", { side: s.side < 0 ? "A" : "D", swap: s.strafeSwitch === "land" ? "land" : s.strafeSwitch ? "swing" : "off" });
        else if (k !== null && s.dash && s.jump && s.pitch != null && s.pitch < -5) { b = newBlock("superdash", { keys: k, pitch: s.pitch }); if (nx && nx.atMs - s.atMs <= 60 && keysOf(nx.forward, nx.side) === k && plain(nx)) used = 2; }
        else if (k !== null && s.dash && !s.jump && nx && nx.jump && nx.atMs - s.atMs <= 60 && keysOf(nx.forward, nx.side) === k && !nx.dash) {
            b = newBlock("dashjump", { keys: k }); used = 2;
            const n2 = st[i + 2]; if (n2 && n2.atMs - nx.atMs <= 60 && keysOf(n2.forward, n2.side) === k && plain(n2)) used = 3;
        }
        else if (k !== null && s.dash && !s.jump && !s.hook) { b = newBlock("dash", { keys: k, pitch: s.pitch || 0 }); if (nx && nx.atMs - s.atMs <= 60 && keysOf(nx.forward, nx.side) === k && plain(nx)) { used = 2; b.press = nx.atMs - s.atMs; } }
        else if (k !== null && s.jump && !s.dash && !s.hook && !s.strafeBot) {
            if (nx && nx.atMs - s.atMs <= 60 && keysOf(nx.forward, nx.side) === k && plain(nx)) { b = newBlock("jump", { keys: k, press: nx.atMs - s.atMs }); used = 2; }
            else b = newBlock("holdjump", { keys: k });
        }
        else if (k !== null && s.hook && !s.jump && !s.dash) b = newBlock("grapple", { keys: k, pitch: s.pitch ?? null });
        else if (k !== null && s.crouch && !s.jump && !s.dash) b = newBlock("crouch", { keys: k });
        else if (k !== null && s.jumpAtEdge && !s.jump) b = newBlock("edgejump", { keys: k });
        else if (k !== null && s.yawRate && plain({ ...s, yawRate: 0 })) b = newBlock("turn", { keys: k });
        else if (k !== null && plain(s)) b = newBlock(k ? "move" : "wait", k ? { keys: k } : {});
        if (b && b.kind !== "dash" && b.kind !== "grapple" && b.kind !== "superdash") lookOf();
        else if (b && (s.yaw != null)) out.push(newBlock("look", { yaw: s.yaw, pitch: 0 }));
        if (!b) b = rawBlock(s);
        const t1 = until(i + used);
        b.ms = Math.max(b.kind === "jump" || b.kind === "dash" ? (b.press || 30) + 1 : 0, t1 - s.atMs);
        if (b.kind === "turn") b.deg = +((s.yawRate || 0) * b.ms / 1000).toFixed(2);
        out.push(b);
        lastEnd = s.atMs + b.ms;
        i += used;
    }
    return { blocks: out, tailMs: Math.max(0, end - lastEnd) };
}

const STEP_KNOWN = new Set(["atMs", "forward", "side", "jump", "dash", "hook", "crouch", "strafeBot", "strafeSwitch", "jumpAtEdge", "yawRate", "pitchRate", "yaw", "pitch"]);
function rawBlock(s) {
    const extra = {};
    for (const [k, v] of Object.entries(s)) if (!STEP_KNOWN.has(k)) extra[k] = v;
    return newBlock("step", { forward: s.forward | 0, side: s.side | 0, jump: s.jump || false, dash: s.dash || false, hook: s.hook || false, crouch: s.crouch || false,
        strafeBot: s.strafeBot || false, strafeSwitch: s.strafeSwitch || false, jumpAtEdge: s.jumpAtEdge || false, yawRate: s.yawRate || 0, pitchRate: s.pitchRate || 0,
        yaw: s.yaw ?? null, pitch: s.pitch ?? null, extra: Object.keys(extra).length ? extra : null });
}
// every step as its own block: lossless for any scenario
export function rawBlocks(sc) {
    const st = [...(sc.steps || [])].sort((a, b) => a.atMs - b.atMs);
    const out = [];
    let last = 0;
    st.forEach((s, i) => {
        if (s.atMs > last) out.push(newBlock("wait", { ms: s.atMs - last }));
        const b = rawBlock(s);
        b.ms = (i + 1 < st.length ? st[i + 1].atMs : Math.max(s.atMs, (sc.durationMs || 0) - 600)) - s.atMs;
        out.push(b); last = s.atMs + b.ms;
    });
    return { blocks: out, tailMs: Math.max(0, (sc.durationMs || 0) - last) };
}

// a scenario as blocks: the readable blocks when they compile back to exactly the same steps, else every step as a raw
// block (lossless)
export function toBlocks(sc) {
    const d = decompile(sc);
    const c = compile(sc.start, d.blocks, d.tailMs);
    if (stepsEqual(c.steps, sc.steps || []) && c.durationMs === Math.max(1, sc.durationMs | 0)) return d;
    return { ...rawBlocks(sc), raw: true };
}

const isIdle = (s) => !s.forward && !s.side && !s.jump && !s.dash && !s.hook && !s.crouch && !s.strafeBot && !s.jumpAtEdge && !s.yawRate && !s.pitchRate && s.yaw == null && s.pitch == null;
export function stepsEqual(a, b) {
    // (idle steps before the first real one change nothing: nothing is held before a scenario's first step)
    const lead = (st) => { const o = [...st].sort((x, y) => x.atMs - y.atMs); while (o.length && isIdle(o[0])) o.shift(); return o; };
    const norm = (st) => JSON.stringify(lead(st).map((s) => {
        const o = {};
        for (const k of Object.keys(s).sort()) {
            const v = s[k];
            if (v === false || v == null || ((k === "forward" || k === "side" || k.endsWith("Rate")) && !v)) continue;
            o[k] = typeof v === "number" ? +v.toFixed(3) : v;
        }
        return o;
    }));
    return norm(a) === norm(b);
}

// ---------------------------------------------------------------- the view
export class BlocksView {
    constructor(app, el) {
        this.app = app; this.el = el; this.sel = null;
        el.innerHTML = `<div class="blocks"><div class="bk-palette" id="bkPal"></div><div class="bk-main">
            <div class="cbar"><button id="bkRun" class="primary">▶ Run</button><button id="bkJson" title="the scenario as JSON (editable)">{ } JSON</button><button id="bkClear" class="danger">Clear</button>
            <span class="grow"></span><span class="muted small">drag blocks from the left into the stack · drag a block back to the left to delete it · times in ms</span></div>
            <div class="bk-ws" id="bkWs"></div>
            <div><div class="bk-tl"><canvas id="bkTl"></canvas></div><div class="bk-json" id="bkJsonBox"><textarea id="bkJsonTa" spellcheck="false"></textarea><div class="st" id="bkJsonSt"></div></div></div>
            </div></div>`;
        this.pal = el.querySelector("#bkPal"); this.ws = el.querySelector("#bkWs"); this.tl = el.querySelector("#bkTl");
        this._palette();
        el.querySelector("#bkRun").onclick = () => app.run();
        el.querySelector("#bkJson").onclick = (e) => { el.querySelector("#bkJsonBox").classList.toggle("on"); e.target.classList.toggle("on"); this._json(); };
        el.querySelector("#bkClear").onclick = () => { this.blocks = []; this._commit(); };
        const ta = el.querySelector("#bkJsonTa");
        ta.oninput = () => {
            const st = el.querySelector("#bkJsonSt");
            try {
                const sc = JSON.parse(ta.value);
                if (!sc || !Array.isArray(sc.steps) || !sc.start) throw new Error("needs start and steps");
                st.textContent = "ok"; st.className = "st ok";
                this._fromScenario(sc); this._renderStack(); this.app.setScenario(sc, "json");
            } catch (e) { st.textContent = e.message; st.className = "st err"; }
        };
        app.on("scenario", (from) => { if (from !== "blocks") { this._fromScenario(app.scenario); this._renderStack(); } this._json(); this._timeline(); });
        app.on("results", () => this._timeline());
        app.on("page", (p) => { if (p === "blocks") this._timeline(); });
        new ResizeObserver(() => this._timeline()).observe(this.tl);
        this._fromScenario(app.scenario); this._renderStack();
    }
    _fromScenario(sc) {
        const d = toBlocks(sc);
        this.blocks = d.blocks; this.tailMs = d.tailMs;
        this.start = JSON.parse(JSON.stringify(sc.start || {}));
        this.name = sc.name || "";
    }
    scenario() {
        const c = compile(this.start, this.blocks, this.tailMs ?? 600);
        return { name: this.name || "my scenario", start: this.start, durationMs: c.durationMs, steps: c.steps };
    }
    _commit() { this._renderStack(); this.app.setScenario(this.scenario(), "blocks"); this._json(); }
    _json() {
        const box = this.el.querySelector("#bkJsonBox");
        if (!box.classList.contains("on") || document.activeElement === box.querySelector("textarea")) return;
        box.querySelector("textarea").value = JSON.stringify(this.app.scenario, null, 1);
    }
    _palette() {
        this.pal.innerHTML = "";
        for (const [cat, title] of CAT_ORDER) {
            const h = document.createElement("div"); h.className = "bk-ph"; h.textContent = title; this.pal.append(h);
            for (const [kind, k] of Object.entries(KINDS)) {
                if (k.cat !== cat) continue;
                const b = document.createElement("div");
                b.className = `bk-block bk-pitem c-${cat}`; b.title = k.desc;
                b.innerHTML = `<span class="bk-kw">${k.label}</span>`;
                b.addEventListener("pointerdown", (e) => this._drag(e, { kind }, b));
                this.pal.append(b);
            }
        }
    }
    _hat() {
        const s = this.start, p = s.pos || [0, 0, 0];
        const d = document.createElement("div");
        d.className = "bk-hathead";
        const num = (v, cls, w = 60, step = "0.1") => `<input type="number" class="${cls}" value="${v ?? ""}" step="${step}" style="width:${w}px">`;
        d.innerHTML = `<span class="bk-title">▶ start</span>
            <label>name <input class="h-name" value="" style="width:150px"></label>
            <label>pos ${num(p[0], "h-x")} ${num(p[1], "h-y")} ${num(p[2], "h-z")}</label>
            <label>yaw ${num(s.yaw ?? 0, "h-yaw", 56, "1")}</label><label>pitch ${num(s.pitch ?? 0, "h-pitch", 50, "1")}</label>
            <label>speed ${num(s.vel ? Math.hypot(s.vel[0], s.vel[2]).toFixed(2) : 0, "h-vel", 56)} m/s</label>
            <label>on floor <select class="h-ground"><option value="">auto</option><option value="1">yes</option><option value="0">no</option></select></label>
            <label>then ${num(this.tailMs ?? 600, "h-tail", 60, "50")} ms</label>`;
        d.querySelector(".h-name").value = this.name;
        d.querySelector(".h-ground").value = s.onGround == null ? "" : s.onGround ? "1" : "0";
        d.addEventListener("change", () => {
            const g = (c) => d.querySelector("." + c).value;
            this.name = g("h-name");
            this.start = { ...this.start, pos: [+g("h-x") || 0, +g("h-y") || 0, +g("h-z") || 0], yaw: +g("h-yaw") || 0, pitch: +g("h-pitch") || 0 };
            const v = +g("h-vel") || 0, yr = this.start.yaw * Math.PI / 180;
            if (v) this.start.vel = [-Math.sin(yr) * v, 0, -Math.cos(yr) * v]; else delete this.start.vel;
            const gr = g("h-ground"); if (gr === "") delete this.start.onGround; else this.start.onGround = gr === "1";
            this.tailMs = Math.max(0, +g("h-tail") || 0);
            this._commit();
        });
        return d;
    }
    _renderStack() {
        this.ws.innerHTML = "";
        const hat = document.createElement("div"); hat.className = "bk-hat";
        hat.append(this._hat());
        const stack = document.createElement("div"); stack.className = "bk-stack"; this.stack = stack;
        if (!this.blocks.length) stack.innerHTML = `<div class="bk-empty">drop blocks here</div>`;
        let t = 0;
        for (const b of this.blocks) { stack.append(this._blockEl(b, t)); t += b.kind === "look" ? 0 : b.ms | 0; }
        hat.append(stack);
        const tot = document.createElement("div"); tot.className = "muted small"; tot.style.marginTop = "6px";
        tot.textContent = `${this.blocks.length} blocks · ${((t + (this.tailMs ?? 600)) / 1000).toFixed(2)} s in all`;
        hat.append(tot);
        this.ws.append(hat);
        this._timeline();
    }
    _blockEl(b, t0) {
        const k = KINDS[b.kind];
        const d = document.createElement("div");
        d.className = `bk-block c-${k.cat}` + (this.sel === b.id ? " sel" : "");
        d.dataset.id = b.id; d.title = k.desc;
        let h = `<span class="bk-kw">${k.label}</span>`;
        const keysSel = () => `<select data-f="keys">${Object.keys(KEYS).map((x) => `<option value="${x}" ${x === b.keys ? "selected" : ""}>${keyLabel(x)}</option>`).join("")}</select>`;
        const n = (f, step = 1, w = 62) => `<input type="number" data-f="${f}" value="${b[f] ?? ""}" step="${step}" style="width:${w}px">`;
        const cb = (f, lab) => `<label><input type="checkbox" data-f="${f}" ${b[f] ? "checked" : ""}>${lab}</label>`;
        switch (b.kind) {
            case "look": h += `<label>yaw ${n("yaw")}</label><label>pitch ${n("pitch")}</label>`; break;
            case "wait": h += `<label>${n("ms", 10)} ms</label>`; break;
            case "turn": h += `${keysSel()}<label>by ${n("deg")}°</label><label>over ${n("ms", 10)} ms</label>`; break;
            case "dash": h += `${keysSel()}<label>pitch ${n("pitch", 1, 50)}</label><label>press ${n("press", 1, 46)}</label><label>${n("ms", 10)} ms</label>`; break;
            case "jump": h += `${keysSel()}<label>press ${n("press", 1, 46)}</label><label>${n("ms", 10)} ms</label>`; break;
            case "superdash": case "grapple": h += `${keysSel()}<label>pitch ${n("pitch", 1, 50)}</label><label>${n("ms", 10)} ms</label>`; break;
            case "strafe": h += `<select data-f="side"><option ${b.side === "A" ? "selected" : ""}>A</option><option ${b.side !== "A" ? "selected" : ""}>D</option></select><label>swap <select data-f="swap">${[["off", "off"], ["land", "at landings"], ["swing", "weave"]].map(([v, l]) => `<option value="${v}" ${(b.swap === true ? "swing" : b.swap || "off") === v ? "selected" : ""}>${l}</option>`).join("")}</select></label><label>${n("ms", 10)} ms</label>`; break;
            case "step": h += `<label>fwd ${n("forward", 1, 52)}</label><label>side ${n("side", 1, 52)}</label>${cb("jump", "jump")}${cb("dash", "dash")}${cb("hook", "hook")}${cb("crouch", "crouch")}${cb("strafeBot", "strafe bot")}<label>swap <select data-f="strafeSwitch">${[["", "off"], ["land", "at landings"], ["swing", "weave"]].map(([v, l]) => `<option value="${v}" ${(b.strafeSwitch === true ? "swing" : b.strafeSwitch || "") === v ? "selected" : ""}>${l}</option>`).join("")}</select></label>${cb("jumpAtEdge", "edge jump")}<label>yaw/s ${n("yawRate", 1, 56)}</label><label>pitch/s ${n("pitchRate", 1, 50)}</label><label>yaw ${n("yaw", 1, 50)}</label><label>pitch ${n("pitch", 1, 50)}</label><label>${n("ms", 10)} ms</label>`; break;
            default: h += `${keysSel()}<label>${n("ms", 10)} ms</label>`;
        }
        h += `<span class="bk-t">@${(t0 / 1000).toFixed(2)} s</span><button class="x" title="delete">✕</button>`;
        d.innerHTML = h;
        d.querySelectorAll("[data-f]").forEach((inp) => {
            inp.addEventListener("pointerdown", (e) => e.stopPropagation());
            inp.addEventListener("change", () => {
                const f = inp.dataset.f;
                b[f] = inp.type === "checkbox" ? inp.checked : inp.tagName === "SELECT" ? inp.value : inp.value === "" ? null : +inp.value;
                if (f === "strafeSwitch") b[f] = inp.value === "swing" ? true : inp.value || false;
                if (f === "ms") b.ms = Math.max(0, Math.min(60000, b.ms | 0));
                this._commit();
            });
        });
        d.querySelector(".x").addEventListener("pointerdown", (e) => e.stopPropagation());
        d.querySelector(".x").onclick = () => { this.blocks = this.blocks.filter((x) => x !== b); this._commit(); };
        d.addEventListener("pointerdown", (e) => { this.sel = b.id; this._drag(e, { block: b }, d); });
        return d;
    }
    // drag a palette item (a new block) or a block (reorder / delete): a ghost follows, the stack shows where it goes
    _drag(e, what, src) {
        if (e.button !== 0) return;
        e.preventDefault();
        const x0 = e.clientX, y0 = e.clientY;
        let ghost = null, mark = null, idx = -1, overPal = false;
        const move = (ev) => {
            if (!ghost) {
                if (Math.hypot(ev.clientX - x0, ev.clientY - y0) < 5) return;
                ghost = src.cloneNode(true); ghost.classList.add("bk-ghost"); ghost.style.width = src.offsetWidth + "px";
                document.body.append(ghost);
                if (what.block) src.classList.add("bk-dragging");
            }
            ghost.style.left = ev.clientX + 8 + "px"; ghost.style.top = ev.clientY - 10 + "px";
            const pr = this.pal.getBoundingClientRect();
            overPal = ev.clientX < pr.right && ev.clientX > pr.left;
            this.pal.classList.toggle("bk-trash", overPal && !!what.block);
            if (mark) { mark.remove(); mark = null; }
            idx = -1;
            if (!overPal) {
                const kids = [...this.stack.querySelectorAll(":scope > .bk-block")];
                idx = kids.length;
                for (let i = 0; i < kids.length; i++) { const r = kids[i].getBoundingClientRect(); if (ev.clientY < r.top + r.height / 2) { idx = i; break; } }
                mark = document.createElement("div"); mark.className = "bk-insert";
                if (idx < kids.length) this.stack.insertBefore(mark, kids[idx]); else this.stack.append(mark);
            }
        };
        const up = () => {
            window.removeEventListener("pointermove", move); window.removeEventListener("pointerup", up);
            this.pal.classList.remove("bk-trash");
            if (mark) mark.remove();
            if (!ghost) { if (what.block) this._renderStack(); return; }
            ghost.remove();
            if (what.block) {
                const i = this.blocks.indexOf(what.block);
                if (overPal) { this.blocks.splice(i, 1); this._commit(); return; }
                if (idx >= 0) { this.blocks.splice(i, 1); if (idx > i) idx--; this.blocks.splice(idx, 0, what.block); this._commit(); return; }
                this._renderStack();
            } else if (idx >= 0) {
                const nb = newBlock(what.kind);
                this.blocks.splice(idx, 0, nb); this.sel = nb.id; this._commit();
            }
        };
        window.addEventListener("pointermove", move); window.addEventListener("pointerup", up);
    }
    // the timeline: each block as a stretch of time, the edited run's horizontal speed over it
    _timeline() {
        const c = this.tl;
        if (!c.clientWidth || this.app.page !== "blocks") return;
        const dpr = window.devicePixelRatio || 1, W = c.clientWidth, H = c.clientHeight;
        if (c.width !== Math.round(W * dpr) || c.height !== Math.round(H * dpr)) { c.width = Math.round(W * dpr); c.height = Math.round(H * dpr); }
        const g = c.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, W, H);
        const sc = this.app.scenario, dur = sc.durationMs || 1, X = (ms) => 4 + (W - 8) * ms / dur;
        const css = getComputedStyle(this.el);
        const col = { move: css.getPropertyValue("--c-move"), button: css.getPropertyValue("--c-button"), look: css.getPropertyValue("--c-look"), bot: css.getPropertyValue("--c-bot"), wait: css.getPropertyValue("--c-wait") };
        let t = 0;
        g.font = "10px system-ui"; g.textBaseline = "middle";
        for (const b of this.blocks) {
            if (b.kind === "look") { g.fillStyle = col.look; g.fillRect(X(t) - 1, 2, 2, 22); continue; }
            const x0 = X(t), x1 = X(t + (b.ms | 0));
            g.fillStyle = col[KINDS[b.kind].cat] || col.wait; g.globalAlpha = this.sel === b.id ? 0.9 : 0.55;
            g.fillRect(x0, 4, Math.max(1, x1 - x0 - 1), 18); g.globalAlpha = 1;
            g.fillStyle = css.getPropertyValue("--fg");
            if (x1 - x0 > 30) g.fillText(KINDS[b.kind].label.slice(0, Math.floor((x1 - x0) / 6)), x0 + 3, 13);
            t += b.ms | 0;
        }
        // ruler
        g.fillStyle = css.getPropertyValue("--fg3"); g.strokeStyle = css.getPropertyValue("--line");
        const stepS = dur > 8000 ? 1 : dur > 3000 ? 0.5 : 0.1;
        for (let s = 0; s * 1000 <= dur; s += stepS) { const x = X(s * 1000); g.fillRect(x, 24, 1, 4); if (Math.round(s / stepS) % (stepS < 0.5 ? 5 : 2) === 0) g.fillText(s.toFixed(stepS < 1 ? 1 : 0) + "s", x + 2, 32); }
        // speed of the runs
        const runs = this.app.results && this.app.results.runs;
        if (runs) {
            let top = 1; for (const r of runs) for (const p of r.trace) top = Math.max(top, p.hspeed);
            for (const r of runs.slice(0, 2)) {
                g.strokeStyle = r.color; g.lineWidth = 1.4; g.beginPath();
                r.trace.forEach((p, i) => { const x = X(p.t * 1000), y = H - 2 - (H - 40) * p.hspeed / top; i ? g.lineTo(x, y) : g.moveTo(x, y); });
                g.stroke();
            }
            g.fillStyle = css.getPropertyValue("--fg3"); g.fillText(`hspeed (top ${top.toFixed(1)} m/s)`, W - 130, H - 8);
        }
    }
}
