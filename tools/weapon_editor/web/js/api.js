// serve.py's API (the physics editor's api.js with the weapon set endpoints). Every write carries X-Quark-Editor
// (serve.py refuses writes without it).
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
    maps: () => req("GET", "/api/maps"),
    map: (name, source) => req("GET", `/api/map?name=${q(name)}&source=${q(source || "default")}`),
    sets: () => req("GET", "/api/sets"),
    set: (name) => req("GET", `/api/set?name=${q(name)}`),
    saveSet: (name, text, expect, force) => req("POST", `/api/set?name=${q(name)}`, { text, expect, force }, "json"),
    deleteSet: (name) => req("POST", `/api/set/delete?name=${q(name)}`, {}),
    modes: () => req("GET", "/api/modes"),
    mode: (name) => req("GET", `/api/mode?name=${q(name)}`),
    playMode: (set, base) => req("POST", "/api/playmode", { set, base }, "json"),
    catalog: () => req("GET", "/api/catalog"),
    check: (text) => req("POST", "/api/check", { text }, "json"),
    state: (key) => req("GET", `/api/state?key=${q(key)}`),
    saveState: (key, obj) => req("POST", `/api/state?key=${q(key)}`, obj, "json"),
    helper: (args, request, timeout) => req("POST", "/api/helper", { args, request, timeout }, "json"),
    generateMaps: () => req("POST", "/api/generate-maps", {}),
};
