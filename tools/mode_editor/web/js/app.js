// Adapted from tools/bot_editor/web/js/app.js at c7aae27. Boot and shared state: config, the catalog (the game's from
// `quark_tests exportmode`, else the snapshot of it in catalog_snapshot.js, with a badge saying which), the built-in logic texts
// (exportmode, else the repository's tools/modes/*.qlogic, else this page's fixtures), the user's modes; then the page.
import { api } from "./api.js";
import { parse } from "./qmode.js";
import { setCatalog, mergeLogic, CATALOG } from "./lang.js";
import { MODES, logicBase } from "./catalog.js";
import { ModePage } from "./modeui.js";

const $ = (id) => document.getElementById(id);

class App {
    constructor() {
        this.handlers = {};
        this.userModes = [];
        this.defaultAsts = new Map();   // "common" / mode -> parsed AST
        this.defaultErrors = new Map();
        this.defaultSource = "";
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
    // New mode: a name and the base mode it starts from
    promptNew(base) {
        return new Promise((res) => {
            const m = document.createElement("div");
            m.className = "modal";
            m.innerHTML = `<div><h3>New mode</h3><p class="muted" style="font-size:12px">It starts as the built-in base mode: same logic, every key at its default. Saved to ${this.modesDir || "custom_modes"}.</p>
              <div class="field"><span>Name</span><input id="nmName" value="my${base}" style="width:100%"></div>
              <div class="field"><span>Base mode</span><select id="nmBase">${MODES.map((x) => `<option value="${x}" ${x === base ? "selected" : ""}>${x}${CATALOG.modeInfo[x] && CATALOG.modeInfo[x].noCustom ? " (the game refuses it as a base)" : ""}</option>`).join("")}</select></div>
              <div class="row" style="justify-content:flex-end;margin-top:10px"><button data-x="0">Cancel</button><button class="primary" data-x="1">Create</button></div></div>`;
            const done = (ok) => { const r = ok ? { name: m.querySelector("#nmName").value.trim(), base: m.querySelector("#nmBase").value } : null; m.remove(); res(r); };
            m.querySelectorAll("button").forEach((b) => (b.onclick = () => done(b.dataset.x === "1")));
            m.querySelector("#nmName").onkeydown = (e) => { if (e.key === "Enter") done(true); if (e.key === "Escape") done(false); };
            document.body.append(m);
            m.querySelector("#nmName").select();
        });
    }

    async boot() {
        if (new URLSearchParams(location.search).get("embed") === "1") document.body.classList.add("embed");   // (inside the Supervisor Board)
        try { this.config = await api.config(); }
        catch (e) { document.body.innerHTML = `<div class="pad">Can't reach serve.py (${e.message}). Start it with tools/mode_editor/run.sh or run.cmd.</div>`; return; }
        $("dataDir").textContent = this.config.modesDir;
        $("dataDir").title = "your modes are saved here (the game's user data folder)";
        // the catalog
        const cb = $("catalogBadge");
        try {
            const c = await api.catalog();
            if (c.source === "game" && c.catalog) setCatalog(c.catalog);
        } catch (e) { console.warn("catalog", e); }
        cb.textContent = CATALOG.fromGame ? "game catalog" : "SNAPSHOT CATALOG";
        cb.className = "badge " + (CATALOG.fromGame ? "real" : "mock");
        cb.title = CATALOG.fromGame ? "slots, fields, actions and params from quark_tests exportmode" : `no quark_tests with exportmode: the slots, fields, actions and params come from web/js/catalog_snapshot.js, the ${CATALOG.label || "snapshot"}. A newer game build may differ; tools/mode_editor/snapshot_catalog.py refreshes it.`;
        // the built-in texts
        await this.loadDefaults();
        const sb = $("simBadge");
        sb.textContent = this.config.modesim ? "modesim" : "no modesim";
        sb.className = "badge " + (this.config.modesim ? "real" : "mock");
        sb.title = this.config.modesim ? "quark_maptool modesim: " + this.config.maptool : "quark_maptool modesim isn't available: the Sim view is off (nothing is simulated in the page)";
        await this.refreshModes();
        this.page = new ModePage(this);
        await this.page.start();
    }

    async loadDefaults() {
        let r = { source: "none", texts: {} };
        try { r = await api.defaults(); } catch (e) { console.warn("defaults", e); }
        this.defaultSource = r.source;
        for (const [name, text] of Object.entries(r.texts || {})) {
            try { this.defaultAsts.set(name, parse(text)); }
            catch (e) { this.defaultErrors.set(name, e.message); }
        }
        const db = $("defaultsBadge");
        const have = MODES.filter((m) => this.defaultAsts.has(m));
        db.textContent = `built-in texts: ${r.source} (${have.length}/13${this.defaultAsts.has("common") ? " + common" : ""})`;
        db.className = "badge " + (r.source === "game" || r.source === "repo" ? "real" : "mock");
        db.title = { game: "from quark_tests exportmode", repo: "the repository's tools/modes/*.qlogic", fixtures: "this page's fixtures (transcribed from the design, section 3) until tools/modes/*.qlogic exist" }[r.source] || "";
        if (this.defaultErrors.size) this.toast("Some built-in texts don't parse in the page: " + [...this.defaultErrors.keys()].join(", "), 8000);
    }
    // common + the base mode's built-in text (or common alone when that text isn't available yet)
    defaultFor(base) {
        return mergeLogic([this.defaultAsts.get("common"), this.defaultAsts.get(base) || this.defaultAsts.get(logicBase(base))], logicBase(base));
    }
    missingDefault(base) {
        if (this.defaultAsts.has(base)) return null;
        return `The built-in ${base} text isn't available (${this.defaultErrors.get(base) || "no tools/modes/" + base + ".qlogic yet"}): the diff and "interpreted" are against common.qlogic only, so ${base}'s own slots show as added here.`;
    }

    async refreshModes() {
        try { const r = await api.modes(); this.userModes = r.user; this.modesDir = r.dir; }
        catch (e) { this.userModes = []; }
        this.emit("modes");
    }
}

const app = new App();
window.qme = app;   // (for the console and the headless checks)
app.boot().catch((e) => { console.error(e); app.toast("Error: " + e.message, 10000); });
