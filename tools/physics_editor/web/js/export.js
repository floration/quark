// Export: save the set as a custom mode (the .qmode a save writes, merged into the file on disk by serve.py), and the
// C++ constants list (physexport) for the owner, if a set should become the game's standard movement.
import { api } from "./api.js";

const $ = (id) => document.getElementById(id);

export class ExportView {
    constructor(app) {
        this.app = app;
        $("exSave").onclick = () => { const n = $("exName").value.trim(); if (n) $("setName").value = n; app.saveSet(n || undefined); };
        // (a refresh still waiting runs first: what's copied is what the name and the set say now)
        const fresh = async () => { if (this._t) await this.refresh(); };
        $("exDownload").onclick = async () => { await fresh(); app.download(($("exName").value.trim() || "physics") + ".qmode", this.qmode || app.set.text); };
        $("exCopyQ").onclick = async () => { await fresh(); this.copy(this.qmode || app.set.text, "the .qmode text"); };
        $("exCopyC").onclick = async () => { await fresh(); this.copy(this.cpp || "", "the C++ list"); };
        // (each refresh is a merge and a physexport through the helper: typing a name or dragging a slider waits for a
        // 250 ms pause instead of starting one per keystroke / frame)
        $("exName").oninput = () => this.schedule();
        app.on("page", (p) => { if (p === "export") this.refresh(); });
        app.on("physics", () => { if (app.page === "export") this.schedule(); });
    }
    schedule() { clearTimeout(this._t); this._t = setTimeout(() => this.refresh(), 250); }
    async refresh() {
        clearTimeout(this._t); this._t = 0;
        const app = this.app;
        if (document.activeElement !== $("exName")) $("exName").value = app.set.name || app.set.saved || "";
        const name = $("exName").value.trim() || "my physics";
        $("exPath").textContent = `${app.modesDir || app.config.modes}/${name}.qmode`;
        const seq = (this.seq = (this.seq || 0) + 1);
        try {
            // what a save would write: the movement lines merged into the file as it is on disk now (or a new file)
            const exists = (app.modes || []).some((m) => m.name === name);
            const base = exists ? await api.mode(name) : (app.set.saved && app.set.saved !== name ? await api.mode(app.set.saved).then((t) => t.replace(/^custommode=.*$/m, "custommode=" + name)) : app.set.baseText ? app.set.baseText.replace(/^custommode=.*$/m, "custommode=" + name) : "");
            const m = await api.merge(base, app.physics, name);
            if (seq !== this.seq) return;
            this.qmode = m.text;
            $("exQmode").textContent = m.text;
        } catch (e) { $("exQmode").textContent = "(" + e.message + ")"; }
        const ex = await app.helper.exportSet(app.physics, name);
        if (seq !== this.seq) return;
        this.cpp = ex.cpp;
        $("exCpp").textContent = ex.cpp;
        $("exVia").textContent = ex.via;
    }
    async copy(text, what) {
        try { await navigator.clipboard.writeText(text); this.app.toast("Copied " + what); }
        catch { const ta = document.createElement("textarea"); ta.value = text; document.body.append(ta); ta.select(); document.execCommand("copy"); ta.remove(); this.app.toast("Copied " + what); }
    }
}
