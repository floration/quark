// Server calls (serve.py). Every quark_maptool command goes through tool(); without the helper, parse and
// serialize fall back to fallback_parse.js so a map can still be viewed and simple edits saved.
import { parseQmap, serializeQmap } from "./fallback_parse.js";

// JSON.stringify writes -0 as 0, which would change a map's text on save (downtown, foundry, gantry and summit
// have -0 coordinates). This writes -0 as the number -0, which quark_maptool's JSON reader reads back as -0.
const NEG0 = "\u0001-0\u0001";
const NEG0_JSON = JSON.stringify(NEG0);
export function toJson(v) {
  const s = JSON.stringify(v, (k, x) => (x === 0 && Object.is(x, -0) ? NEG0 : x));
  return s === undefined || !s.includes(NEG0_JSON) ? s : s.split(NEG0_JSON).join("-0");
}

async function req(method, path, body, type) {
  const opt = { method, headers: {} };
  if (body !== undefined) {
    opt.body = typeof body === "string" ? body : toJson(body);
    opt.headers["Content-Type"] = type || (typeof body === "string" ? "text/plain; charset=utf-8" : "application/json");
  }
  const r = await fetch(path, opt);
  const ct = r.headers.get("Content-Type") || "";
  const data = ct.includes("json") ? await r.json().catch(() => ({ error: "bad JSON from the server" })) : await r.text();
  if (!r.ok) {
    const msg = data && data.error ? data.error : (typeof data === "string" ? data : r.statusText);
    const e = new Error(msg); e.status = r.status; e.data = data; throw e;
  }
  return data;
}

export const api = {
  info: null,
  async init() { this.info = await req("GET", "/api/info"); return this.info; },
  get hasTool() { return !!(this.info && this.info.maptool); },
  maps: () => req("GET", "/api/maps"),
  readMap: (src, name) => req("GET", `/api/map/${src}/${encodeURIComponent(name)}`),
  writeMap: (name, text, provenance) => req("PUT", `/api/map/mine/${encodeURIComponent(name)}`, { text, provenance }),
  deleteMap: (name) => req("DELETE", `/api/map/mine/${encodeURIComponent(name)}`),
  renameMap: (from, to) => req("POST", "/api/rename", { from, to }),
  provenance: () => req("GET", "/api/provenance"),
  readSidecar: (name) => req("GET", `/api/sidecar/${encodeURIComponent(name)}`),
  writeSidecar: (name, obj) => req("PUT", `/api/sidecar/${encodeURIComponent(name)}`, obj),
  clipboard: () => req("GET", "/api/clipboard"),
  setClipboard: (obj) => req("PUT", "/api/clipboard", obj),
  library: () => req("GET", "/api/library"),
  putLibrary: (name, item) => req("PUT", `/api/library/${encodeURIComponent(name)}`, item),
  deleteLibrary: (name) => req("DELETE", `/api/library/${encodeURIComponent(name)}`),
  renameLibrary: (from, to) => req("POST", "/api/library-rename", { from, to }),
  config: () => req("GET", "/api/config"),
  setConfig: (cfg) => req("PUT", "/api/config", cfg),
  launch: (what, name, mode) => req("POST", "/api/launch", { what, name, mode }),

  async tool(cmd, body) {
    if (!this.hasTool) {
      if (cmd === "parse") return parseQmap(body.text, body.name || "");
      if (cmd === "serialize") return { text: serializeQmap(body) };
      throw new Error("quark_maptool isn't there (build it, then rebuild the editor): " + cmd + " needs it");
    }
    try {
      return await req("POST", "/api/tool/" + cmd, body);
    } catch (e) {
      if (e.data && typeof e.data === "object" && e.status === 422) { const x = new Error(e.data.error || "failed"); x.data = e.data; throw x; }
      throw e;
    }
  },

  // quark_maptool bots --stream: calls onMsg(obj) per NDJSON line as it arrives. Returns {ctl(line), done: Promise}.
  streamBots(body, onMsg) {
    const self = { run: null, ctl: async (line) => self.run && req("POST", "/api/botctl/" + self.run, { line }).catch(() => {}) };
    const ac = new AbortController();
    self.abort = () => ac.abort();
    self.done = (async () => {
      const r = await fetch("/api/stream/bots", { method: "POST", body: toJson(body), headers: { "Content-Type": "application/json" }, signal: ac.signal });
      if (!r.ok) { const d = await r.json().catch(() => ({})); throw new Error(d.error || r.statusText); }
      const rd = r.body.getReader(), dec = new TextDecoder();
      let buf = "";
      for (;;) {
        const { value, done } = await rd.read();
        if (done) break;
        buf += dec.decode(value, { stream: true });
        let nl;
        while ((nl = buf.indexOf("\n")) >= 0) {
          const line = buf.slice(0, nl).trim(); buf = buf.slice(nl + 1);
          if (!line) continue;
          let o; try { o = JSON.parse(line); } catch { continue; }
          if (o.type === "run") { self.run = o.run; continue; }
          onMsg(o);
        }
      }
    })();
    return self;
  },
};
