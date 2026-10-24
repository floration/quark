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
export const api = {
    get: (url) => req("GET", url),
    post: (url, body, type) => req("POST", url, body, type),
    config: () => req("GET", "/api/config"),
    maps: () => req("GET", "/api/maps"),
    map: (name, source) => req("GET", `/api/map?name=${encodeURIComponent(name)}&source=${encodeURIComponent(source)}`),
    brains: () => req("GET", "/api/brains"),
    brain: (name, source) => req("GET", `/api/brain?name=${encodeURIComponent(name)}${source ? "&source=" + source : ""}`),
    saveBrain: (name, text, provenance) => req("POST", `/api/brain?name=${encodeURIComponent(name)}`, { text, provenance }, "json"),
    provenance: (name, prov) => req("POST", `/api/provenance?name=${encodeURIComponent(name)}`, prov, "json"),
    checkBrain: (text) => req("POST", "/api/checkbrain", text),
    exportBrain: () => req("GET", "/api/exportbrain"),
    scenarios: () => req("GET", "/api/scenarios"),
    scenario: (name) => req("GET", `/api/scenario?name=${encodeURIComponent(name)}`),
    saveScenario: (name, s) => req("POST", `/api/scenario?name=${encodeURIComponent(name)}`, s, "json"),
    generateMaps: () => req("POST", "/api/generate-maps", {}),
};
