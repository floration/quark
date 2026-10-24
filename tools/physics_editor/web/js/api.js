// serve.py's API. Every write carries X-Quark-Editor (serve.py refuses writes without it).
async function req(method, url, body, type) {
    const opt = { method, headers: { "X-Quark-Editor": "1" } };
    if (body !== undefined) {
        if (typeof body === "string" && type !== "json") { opt.body = body; opt.headers["Content-Type"] = "text/plain; charset=utf-8"; }
        else { opt.body = JSON.stringify(body); opt.headers["Content-Type"] = "application/json"; }
    }
    const r = await fetch(url, opt);
    const ct = r.headers.get("Content-Type") || "";
    const data = ct.includes("json") ? await r.json() : await r.text();
    if (!r.ok) throw new Error((data && data.error) || (typeof data === "string" ? data : r.statusText));
    return data;
}
const q = encodeURIComponent;
export const api = {
    get: (url) => req("GET", url),
    post: (url, body, type) => req("POST", url, body, type),
    config: () => req("GET", "/api/config"),
    maps: () => req("GET", "/api/maps"),
    map: (name, source) => req("GET", `/api/map?name=${q(name)}&source=${q(source || "default")}`),
    modes: () => req("GET", "/api/modes"),
    mode: (name) => req("GET", `/api/mode?name=${q(name)}`),
    saveMode: (name, body) => req("POST", `/api/mode?name=${q(name)}`, body, "json"),
    merge: (text, physics, name) => req("POST", "/api/merge", { text, physics, name }, "json"),
    scenarios: () => req("GET", "/api/scenarios"),
    scenario: (name) => req("GET", `/api/scenario?name=${q(name)}`),
    saveScenario: (name, s) => req("POST", `/api/scenario?name=${q(name)}`, s, "json"),
    state: (key) => req("GET", `/api/state?key=${q(key)}`),
    saveState: (key, obj) => req("POST", `/api/state?key=${q(key)}`, obj, "json"),
    helper: (args, request, timeout) => req("POST", "/api/helper", { args, request, timeout }, "json"),
    generateMaps: () => req("POST", "/api/generate-maps", {}),
};
