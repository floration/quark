// Adapted from tools/bot_editor/web/js/api.js at c7aae27: serve.py's API for modes. Every write carries
// X-Quark-Editor (serve.py refuses writes without it, and a page on another site can't add it without a preflight).
async function req(method, url, body, type) {
    const opt = { method, headers: { "X-Quark-Editor": "1" } };
    if (body !== undefined) {
        if (typeof body === "string" && type !== "json") { opt.body = body; opt.headers["Content-Type"] = "text/plain; charset=utf-8"; }
        else { opt.body = JSON.stringify(body); opt.headers["Content-Type"] = "application/json"; }
    }
    const r = await fetch(url, opt);
    const ct = r.headers.get("Content-Type") || "";
    const data = ct.includes("json") ? await r.json() : await r.text();
    if (!r.ok) { const e = new Error((data && data.error) || (typeof data === "string" ? data : r.statusText)); e.status = r.status; e.data = data; throw e; }
    return data;
}
const q = encodeURIComponent;
export const api = {
    get: (url) => req("GET", url),
    post: (url, body, type) => req("POST", url, body, type),
    config: () => req("GET", "/api/config"),
    catalog: () => req("GET", "/api/catalog"),            // {source: "game", catalog} | {source: "builtin"}
    defaults: () => req("GET", "/api/defaults"),          // {source, texts: {common, ffa, ...}, missing: [...]}
    modes: () => req("GET", "/api/modes"),                // {builtin: [...], user: [{name, mtime, size}], dir}
    mode: (name) => req("GET", `/api/mode?name=${q(name)}`),   // {name, text, mtime, sha256, path}
    // {text, baseSha256, baseMtime, provenance, overwrite}: 409 when the file changed on disk since it was loaded
    saveMode: (name, body) => req("POST", `/api/mode?name=${q(name)}`, body, "json"),
    checkMode: (text) => req("POST", "/api/checkmode", text),
    provenance: (name, prov) => req("POST", `/api/provenance?name=${q(name)}`, prov, "json"),
    provenanceOf: (name) => req("GET", `/api/provenance?name=${q(name)}`),   // {item: {origin, by, date, tool, note} | null}
    // rename / duplicate / delete one of the user's modes: {ok, text?, sha256?, mtime?, backup?}; 409 when it changed on disk
    modeOp: (op, name, to, baseSha256) => req("POST", `/api/mode/${op}?name=${q(name)}${to ? "&to=" + q(to) : ""}`, { baseSha256 }, "json"),
    backups: () => req("GET", "/api/backups"),            // {backups: [{name, n, file, mtime, size, exists}], dir, keep}
    // a backup (n: 1 = <name>.qmode.bak) back as <to>.qmode: {ok, name, text, sha256, mtime}; 409 when <to> exists
    restore: (name, n, to, overwrite) => req("POST", `/api/mode/restore?name=${q(name)}`, { n, to, overwrite: !!overwrite }, "json"),
    brains: () => req("GET", "/api/brains"),              // {brains: [name], dir}
    scenarios: () => req("GET", "/api/scenarios"),        // {scenarios: [{name, scenario}], dir}
    saveScenario: (name, sc) => req("POST", `/api/scenario?name=${q(name)}`, sc, "json"),
    deleteScenario: (name) => req("POST", `/api/scenario/delete?name=${q(name)}`, {}, "json"),
    run: (id) => req("GET", `/api/run/${id}`),            // {id, done, code, lines, started, args}: a run still known to serve.py
    modesimStatus: () => req("GET", "/api/modesim/status"),
    maps: () => req("GET", "/api/maps"),
    state: (key) => req("GET", `/api/state?key=${q(key)}`),
    saveState: (key, obj) => req("POST", `/api/state?key=${q(key)}`, obj, "json"),
};
