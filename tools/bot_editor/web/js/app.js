// Boot and shared state: config, maps, brains, the helper; the three pages wire themselves to this.
import { api } from "./api.js";
import { createHelper } from "./helper.js";
import { parseQmap, applyHelperParse } from "./qmap.js";
import { parse } from "./dsl.js";
import { setCatalog, numberIds } from "./ast.js";
import { SimPage } from "./sim.js";
import { BrainPage } from "./brainui.js";
import { BatchPage } from "./batch.js";

const $ = (id) => document.getElementById(id);

class App {
    constructor() {
        this.handlers = {};
        this.maps = [];
        this.map = null;
        this.brainList = [];          // [{name, source: "default"|"user"}]
        this.brainText = new Map();   // name -> text (saved versions)
        this.brainAst = new Map();    // name -> parsed (or null)
    }
    on(e, f) { (this.handlers[e] || (this.handlers[e] = [])).push(f); }
    emit(e, ...a) { for (const f of this.handlers[e] || []) f(...a); }

    toast(msg, ms = 2500) {
        const t = $("toast");
        t.textContent = msg; t.style.display = "block";
        clearTimeout(this._tt); this._tt = setTimeout(() => (t.style.display = "none"), ms);
    }
    download(name, text, type = "text/plain") {
        const a = document.createElement("a");
        a.href = URL.createObjectURL(new Blob([text], { type }));
        a.download = name; a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 2000);
    }
    pickFile(accept) {
        return new Promise((res) => {
            const f = $("fileIn");
            f.accept = accept; f.value = "";
            f.onchange = () => { const file = f.files[0]; if (!file) return res(null); file.text().then((text) => res({ name: file.name, text })); };
            f.click();
        });
    }
    prompt(title, value, help = "") {
        return new Promise((res) => {
            const m = document.createElement("div");
            m.className = "modal";
            m.innerHTML = `<div><h3></h3><p class="muted" style="font-size:12px"></p><input style="width:100%"><div class="row" style="justify-content:flex-end;margin-top:10px"><button data-x="0">Cancel</button><button class="primary" data-x="1">OK</button></div></div>`;
            m.querySelector("h3").textContent = title;
            m.querySelector("p").textContent = help;
            const inp = m.querySelector("input");
            inp.value = value || "";
            const done = (ok) => { m.remove(); res(ok ? inp.value.trim() : null); };
            m.querySelectorAll("button").forEach((b) => (b.onclick = () => done(b.dataset.x === "1")));
            inp.onkeydown = (e) => { if (e.key === "Enter") done(true); if (e.key === "Escape") done(false); };
            document.body.append(m);
            inp.focus(); inp.select();
        });
    }

    // A choice in a modal: buttons [{k, label, primary?, danger?}]; resolves with the chosen k (Escape: null)
    choose(title, text, buttons) {
        return new Promise((res) => {
            const m = document.createElement("div");
            m.className = "modal";
            m.innerHTML = `<div><h3></h3><p style="font-size:12px;white-space:pre-wrap"></p><div class="row" style="justify-content:flex-end;margin-top:10px"></div></div>`;
            m.querySelector("h3").textContent = title;
            m.querySelector("p").textContent = text;
            const done = (k) => { removeEventListener("keydown", key, true); m.remove(); res(k); };
            const key = (e) => { if (e.key === "Escape") { e.preventDefault(); done(null); } };
            addEventListener("keydown", key, true);
            const row = m.querySelector(".row");
            for (const b of buttons) {
                const e = document.createElement("button");
                e.textContent = b.label;
                if (b.primary) e.className = "primary"; else if (b.danger) e.className = "danger";
                e.onclick = () => done(b.k);
                row.append(e);
            }
            document.body.append(m);
            (row.querySelector(".primary") || row.lastChild).focus();
        });
    }
    // a file name asked for until it's valid (letters, digits, _ . -; not reserved) or cancelled
    async promptName(title, value, help, reserved = ["default"]) {
        for (let v = value; ;) {
            const n = await this.prompt(title, v, help);
            if (n == null || n === "") return null;
            if (/^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$/.test(n) && !n.includes("..") && !reserved.includes(n)) return n;
            v = n; help = `“${n}” can't be used: letters, digits, _ . - (starting with a letter or digit, no “..”${reserved.length ? ", not " + reserved.map((x) => "“" + x + "”").join(" / ") : ""}).`;
        }
    }

    async boot() {
        if (new URLSearchParams(location.search).get("embed") === "1") document.body.classList.add("embed");   // (inside the project manager)
        try { this.config = await api.config(); }
        catch (e) { document.body.innerHTML = `<div class="pad">Can't reach serve.py (${e.message}). Start it with tools/bot_editor/run.sh or run.cmd.</div>`; return; }
        this.helper = await createHelper(this.config);
        const hb = $("helperBadge");
        hb.textContent = this.helper.kind === "real" ? "quark_maptool" : "MOCK SIMULATION";
        hb.className = "badge " + (this.helper.kind === "real" ? "real" : "mock");
        hb.title = this.helper.kind === "real" ? this.config.maptool : "quark_maptool wasn't found: runs use a stand-in simulation in the page (not the game's bots). Build quark_maptool or pass --maptool.";
        $("dataDir").textContent = this.config.dataDir;
        $("dataDir").title = "the game's user data folder: maps " + this.config.userMaps + ", brains " + this.config.bots;
        // the exportbrain catalog (types, units, docs) when quark_tests can give it
        if (this.config.quarkTests) api.exportBrain().then((x) => { if (x && x.catalog) { setCatalog(x.catalog); this.emit("catalog"); } }).catch(() => {});
        await this.refreshBrains();
        await this.refreshMaps();
        this.sim = new SimPage(this);
        this.brain = new BrainPage(this);
        this.batch = new BatchPage(this);
        document.querySelectorAll("#tabs button").forEach((b) => (b.onclick = () => this.showPage(b.dataset.page)));
        const want = new URLSearchParams(location.search).get("page");
        if (want) this.showPage(want);
        await this.sim.start();
        await this.brain.start();
        this.batch.start();
    }

    showPage(p) {
        document.querySelectorAll("#tabs button").forEach((b) => b.classList.toggle("on", b.dataset.page === p));
        document.querySelectorAll(".page").forEach((s) => s.classList.toggle("on", s.id === p));
        this.page = p;
        this.emit("page", p);
    }

    async refreshMaps() {
        let r = await api.maps();
        if (!r.maps.some((m) => m.source === "default")) {
            this.toast("No built maps: generating them with tools/make_maps.py…", 8000);
            try { await api.generateMaps(); r = await api.maps(); } catch (e) { this.toast("Couldn't generate the maps: " + e.message, 8000); }
        }
        this.maps = r.maps;
        this.emit("maps");
    }
    async loadMap(name, source) {
        const text = await api.map(name, source);
        const m = parseQmap(text, name);
        m.text = text; m.source = source;
        // the game's own geometry when the helper is here (a few ms; the page's reader if it fails)
        if (this.helper && this.helper.kind === "real") {
            try {
                const r = await api.post("/api/helper", { args: ["parse"], request: { text, name }, timeout: 30 });
                if (r && r.code === 0) applyHelperParse(m, r.answer);
            } catch {}
        }
        this.map = m;
        this.emit("map", m);
        return m;
    }

    async refreshBrains() {
        const r = await api.brains();
        this.brainList = [];
        if (r.default.exists) this.brainList.push({ name: "default", source: "default" });
        for (const b of r.user) this.brainList.push({ name: b.name, source: "user" });
        this.brainsDir = r.dir;
        if (!this.brainText.has("default") && r.default.exists) await this.loadBrainText("default");
        this.emit("brains");
    }
    async loadBrainText(name) {
        const src = this.brainList.find((b) => b.name === name);
        const text = await api.brain(name, src && src.source === "user" ? "user" : undefined);
        this.brainText.set(name, text);
        try { this.brainAst.set(name, numberIds(JSON.parse(JSON.stringify(parse(text))))); } catch { this.brainAst.set(name, null); }
        return text;
    }
    defaultAst() { return this.brainAst.get("default"); }

    // A user brain only overrides what it writes: the brain a bot runs is the default with these declarations replaced.
    effectiveAst(name) {
        if (name === "(editor)" && this.brain) return this.brain.mergedWithDefault();
        const a = this.brainAst.get(name), d = this.defaultAst();
        if (!a || !d || name === "default") return a || d;
        const keys = new Map(a.decls.map((x) => [x.kind + ":" + (x.kind === "slot" ? x.name + (x.mode || "") : x.name), x]));
        return { ...d, decls: [...d.decls.map((x) => keys.get(x.kind + ":" + (x.kind === "slot" ? x.name + (x.mode || "") : x.name)) || x), ...a.decls.filter((x) => !d.decls.some((y) => y.kind === x.kind && y.name === x.name && y.mode === x.mode))] };
    }
    // {param: [4 values]} for the mock simulation
    paramsOf(name) {
        const a = this.effectiveAst(name);
        const out = {};
        if (a) for (const d of a.decls) if (d.kind === "param") out[d.name] = d.values.length === 4 ? d.values : [d.values[0], d.values[0], d.values[0], d.values[0]];
        return out;
    }
    brainTextFor(name) {
        if (name === "(editor)" && this.brain) {
            // the game's label (name#hash) names the file open in the editor; an unsaved edit of the default would
            // show as "default#<hash>": call it "editor"
            const t = this.brain.currentText(), n = this.brain.name === "default" ? "editor" : this.brain.name;
            return t == null ? t : t.replace(/^brain "[^"\n]*"$/m, `brain "${n}"`);
        }
        return this.brainText.get(name);
    }
}

// Colour theme: "auto" follows the system; the choice is this browser's own convenience (localStorage)
const THEMES = ["auto", "light", "dark"];
const sysLight = matchMedia("(prefers-color-scheme: light)");
function applyTheme(t) {
    document.documentElement.dataset.theme = t === "auto" ? (sysLight.matches ? "light" : "dark") : t;
    const b = $("themeBtn");
    if (b) b.textContent = { auto: "◐ auto", light: "☀ light", dark: "☾ dark" }[t];
}
let theme = "auto";
try { theme = THEMES.includes(localStorage.getItem("qbe.theme")) ? localStorage.getItem("qbe.theme") : "auto"; } catch {}
applyTheme(theme);
sysLight.addEventListener("change", () => { applyTheme(theme); window.dispatchEvent(new Event("qbe-theme")); });
$("themeBtn").onclick = () => { theme = THEMES[(THEMES.indexOf(theme) + 1) % 3]; try { localStorage.setItem("qbe.theme", theme); } catch {} applyTheme(theme); window.dispatchEvent(new Event("qbe-theme")); };

const app = new App();
window.qbe = app;   // (for the console and the headless checks)
app.boot().catch((e) => { console.error(e); app.toast("Error: " + e.message, 10000); });
