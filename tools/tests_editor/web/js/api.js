// The server's JSON (tools/tests_editor/API.md). Every call resolves to {ok, status, data}; nothing throws on HTTP errors.

async function call(method, url, body, headers, onResponse) {
    try {
        const r = await fetch(url, { method, headers: { ...(body ? { "Content-Type": "application/json" } : {}), ...(headers || {}) }, body: body ? JSON.stringify(body) : undefined, cache: "no-store" });
        if (r.status === 304) return { ok: true, status: 304, data: null };
        let data = null;
        const t = await r.text();
        try { data = t ? JSON.parse(t) : null; } catch { data = { error: t }; }
        if (onResponse && r.ok) onResponse(r);
        return { ok: r.ok, status: r.status, data };
    } catch (e) {
        return { ok: false, status: 0, data: { error: "the tests editor server isn't answering (" + e.message + ")" } };
    }
}
const q = encodeURIComponent;
// the model's ETag: a poll (every 15 s, and on every window focus) sends it back and gets a 304 with no body while
// nothing changed, instead of the whole ~470 KB model parsed again
let modelTag = null;
export const api = {
    model: () => call("GET", "/api/model", null, modelTag ? { "If-None-Match": modelTag } : null, (r) => { modelTag = r.headers.get("ETag"); }),   // (304: data null, nothing new)
    file: (path) => call("GET", "/api/file?path=" + q(path)),
    save: (path, text, baseSha256) => call("POST", "/api/file", { path, text, baseSha256 }),
    diff: (path) => call("GET", "/api/diff" + (path ? "?path=" + q(path) : "")),
    revert: (path) => call("POST", "/api/revert", { path }),
    run: (cmd, extra) => call("POST", "/api/run", { cmd, ...(extra || {}) }),   // extra: {args: [...]} | {groups: [...]}
    importLog: (text, name) => call("POST", "/api/import-log", { text, name }),
    runPoll: (id, from) => call("GET", `/api/run?id=${q(id)}&from=${from | 0}`),
    stop: (id) => call("POST", "/api/run/stop", { id }),
    runs: () => call("GET", "/api/runs"),
    ci: () => call("GET", "/api/ci"),
    summary: () => call("GET", "/api/summary"),
    env: () => call("GET", "/api/env"),                          // what this machine can run (Xvfb, a display, build/quark)
    maps: () => call("GET", "/api/maps"),                        // each map's last release_check.sh result
    report: (id) => call("GET", "/api/run/report?id=" + q(id)),   // the UI test report a run left
    mapFile: (name, kind) => `/api/map-file?name=${q(name)}&kind=${q(kind)}`,   // (a URL: kind "shot" | "log")
};
