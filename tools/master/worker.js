// quark internet server list: a Cloudflare Worker with a D1 database bound as DB.
// Setup steps: tools/master/README.md.
//   GET /announce?port=N&v=VERSION[&ip=A.B.C.D]  a server says it's up (every 60 s)
//   GET /remove?port=N[&ip=A.B.C.D]              a server shuts down
//   GET /list?v=VERSION                           ip:port per line, servers seen in the last 150 s
// The game checks every listed server over UDP itself, so a bogus entry just never shows up.
const FRESH_MS = 150000;

function ipv4(s) {
  if (!s || !/^\d{1,3}(\.\d{1,3}){3}$/.test(s)) return null;
  return s.split(".").every((p) => +p <= 255) ? s : null;
}

export default {
  async fetch(req, env) {
    const u = new URL(req.url);
    const q = (k) => u.searchParams.get(k) || "";
    const now = Date.now();
    // The caller's own address; IPv6 hosts pass their IPv4 (the game is IPv4) and only then is ?ip= used.
    const conn = req.headers.get("CF-Connecting-IP") || "";
    const ip = ipv4(conn) || ipv4(q("ip"));
    const text = (body, status = 200) => new Response(body, { status, headers: { "content-type": "text/plain" } });

    if (u.pathname === "/announce" || u.pathname === "/remove") {
      const port = parseInt(q("port"), 10);
      if (!ip || !(port > 0 && port < 65536)) return text("bad request", 400);
      const addr = ip + ":" + port;
      if (u.pathname === "/remove") {
        await env.DB.prepare("DELETE FROM servers WHERE addr = ?").bind(addr).run();
        return text("removed");
      }
      await env.DB.prepare(
        "INSERT INTO servers (addr, version, seen) VALUES (?, ?, ?) " +
        "ON CONFLICT(addr) DO UPDATE SET version = excluded.version, seen = excluded.seen"
      ).bind(addr, q("v").slice(0, 16), now).run();
      if (Math.random() < 0.02) await env.DB.prepare("DELETE FROM servers WHERE seen < ?").bind(now - 3600000).run();
      return text("ok");
    }
    if (u.pathname === "/list") {
      const { results } = await env.DB.prepare(
        "SELECT addr FROM servers WHERE seen > ? AND version = ? ORDER BY seen DESC LIMIT 200"
      ).bind(now - FRESH_MS, q("v").slice(0, 16)).all();
      return text(results.map((r) => r.addr).join("\n"));
    }
    return text("quark server list");
  },
};
