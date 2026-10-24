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
            // an unsaved edit of the default would show as "default#<hash>" in the game's label: call it "editor"
            const t = this.brain.currentText();
            return t == null ? t : t.replace(/^brain "default"$/m, 'brain "editor"');
        }
        return this.brainText.get(name);
    }
}

const app = new App();
window.qbe = app;   // (for the console and the headless checks)
app.boot().catch((e) => { console.error(e); app.toast("Error: " + e.message, 10000); });
