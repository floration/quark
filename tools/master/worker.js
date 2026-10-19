// quark internet server list: a Cloudflare Worker with a D1 database bound as DB.
// Setup steps: tools/master/README.md.
//   GET /announce?port=N&v=VERSION[&ip=A.B.C.D]  a server says it's up (every 60 s)
//   GET /remove?port=N[&ip=A.B.C.D]              a server shuts down
//   GET /list?v=VERSION                           ip:port per line, servers seen in the last 150 s
// The game checks every listed server over UDP itself, so a bogus entry just never shows up.
//
// Ranked (OpenSkill / Weng-Lin Plackett-Luce ratings):
//   POST /report  form: secret, match, mode, p=id:team:place,...   one player's account of a finished match
//   GET  /ratings?ids=a,b,c                                        "id mu sigma games" per line (unknown ids: defaults)
// A player's id is the first 32 hex chars of sha256(secret); the secret never leaves their game except to
// here. A match counts only when every player in it has reported the exact same result; any disagreement
// voids it, and matches not fully reported within an hour are dropped. Hosts never see secrets, so a
// lying host just produces a mismatch.
const FRESH_MS = 150000;
const MU = 25, SIGMA = 25 / 3, BETA = SIGMA / 2, TAU = MU / 300, EPS = 1e-4;
const PENDING_MS = 3600000;

async function sha256hex(s) {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// Plackett-Luce update (openskill.js). teams: [{ players: [{mu, sigma}], rank }] (lower rank = better, ties allowed).
export function rateTeams(teams) {
  for (const t of teams) for (const p of t.players) p.sigma = Math.sqrt(p.sigma * p.sigma + TAU * TAU);
  const tr = teams.map((t) => ({
    mu: t.players.reduce((a, p) => a + p.mu, 0),
    s2: t.players.reduce((a, p) => a + p.sigma * p.sigma, 0),
    rank: t.rank,
  }));
  const c = Math.sqrt(tr.reduce((a, t) => a + t.s2 + BETA * BETA, 0));
  const sumQ = tr.map((q) => tr.reduce((a, i) => a + (i.rank >= q.rank ? Math.exp(i.mu / c) : 0), 0));
  const A = tr.map((q) => tr.filter((i) => i.rank === q.rank).length);
  tr.forEach((ti, i) => {
    const e = Math.exp(ti.mu / c);
    let omega = 0, delta = 0;
    tr.forEach((tq, q) => {
      if (tq.rank > ti.rank) return;
      const quot = e / sumQ[q];
      omega += (i === q ? 1 - quot : -quot) / A[q];
      delta += (quot * (1 - quot)) / A[q];
    });
    const gamma = Math.sqrt(ti.s2) / c;
    const Omega = (omega * ti.s2) / c, Delta = (gamma * delta * ti.s2) / (c * c);
    for (const p of teams[i].players) {
      const f = (p.sigma * p.sigma) / ti.s2;
      p.mu += f * Omega;
      p.sigma *= Math.sqrt(Math.max(1 - f * Delta, EPS));
    }
  });
}

// "id:team:place,..." -> canonical text (sorted) and entries, or null if malformed.
function parseResult(p) {
  const out = [];
  for (const part of String(p).split(",")) {
    const m = /^([0-9a-f]{32}):(\d{1,3}):(\d{1,3})$/.exec(part);
    if (!m) return null;
    out.push({ id: m[1], team: +m[2], place: +m[3] });
  }
  if (out.length < 2 || out.length > 32 || new Set(out.map((e) => e.id)).size !== out.length) return null;
  out.sort((a, b) => (a.id < b.id ? -1 : 1));
  if (new Set(out.map((e) => e.team)).size < 2) return null; // needs at least two sides
  return { entries: out, text: out.map((e) => `${e.id}:${e.team}:${e.place}`).join(",") };
}

async function loadRatings(env, ids) {
  const map = new Map(ids.map((id) => [id, { id, mu: MU, sigma: SIGMA, games: 0 }]));
  if (!ids.length) return map;
  const { results } = await env.DB.prepare(
    `SELECT id, mu, sigma, games FROM players WHERE id IN (${ids.map(() => "?").join(",")})`
  ).bind(...ids).all();
  for (const r of results) map.set(r.id, { id: r.id, mu: r.mu, sigma: r.sigma, games: r.games });
  return map;
}

async function report(env, form, now) {
  const secret = String(form.get("secret") || ""), match = String(form.get("match") || ""), mode = String(form.get("mode") || "");
  if (!/^[0-9a-f]{64}$/.test(secret) || !/^[0-9a-f]{16,32}$/.test(match) || !/^[a-z0-9]{1,16}$/.test(mode)) return "bad request";
  const res = parseResult(form.get("p"));
  if (!res) return "bad result";
  const me = (await sha256hex(secret)).slice(0, 32);
  if (!res.entries.some((e) => e.id === me)) return "not in this match";
  const result = mode + "|" + res.text;
  if (Math.random() < 0.02) await env.DB.prepare("UPDATE matches SET status = 'expired' WHERE status = 'pending' AND created < ?").bind(now - PENDING_MS).run();
  let m = await env.DB.prepare("SELECT result, players, status, created FROM matches WHERE id = ?").bind(match).first();
  if (!m) {
    await env.DB.prepare("INSERT OR IGNORE INTO matches (id, result, players, status, created) VALUES (?, ?, ?, 'pending', ?)")
      .bind(match, result, res.entries.length, now).run();
    m = await env.DB.prepare("SELECT result, players, status, created FROM matches WHERE id = ?").bind(match).first();
  }
  if (m.status !== "pending") return m.status;
  if (now - m.created > PENDING_MS) return "expired";
  if (m.result !== result) {
    await env.DB.prepare("UPDATE matches SET status = 'void' WHERE id = ?").bind(match).run();
    return "void: reports disagree";
  }
  await env.DB.prepare("INSERT OR IGNORE INTO reports (match, reporter) VALUES (?, ?)").bind(match, me).run();
  const n = (await env.DB.prepare("SELECT COUNT(*) AS n FROM reports WHERE match = ?").bind(match).first()).n;
  if (n < m.players) return `pending ${n}/${m.players}`;
  // last report in: rate it (only once, even if two reports race here)
  const claim = await env.DB.prepare("UPDATE matches SET status = 'done' WHERE id = ? AND status = 'pending'").bind(match).run();
  if (!claim.meta || claim.meta.changes !== 1) return "done";
  const ratings = await loadRatings(env, res.entries.map((e) => e.id));
  const byTeam = new Map();
  for (const e of res.entries) {
    if (!byTeam.has(e.team)) byTeam.set(e.team, { players: [], rank: e.place });
    byTeam.get(e.team).players.push(ratings.get(e.id));
  }
  rateTeams([...byTeam.values()]);
  await env.DB.batch([...ratings.values()].map((r) =>
    env.DB.prepare(
      "INSERT INTO players (id, mu, sigma, games, updated) VALUES (?, ?, ?, ?, ?) " +
      "ON CONFLICT(id) DO UPDATE SET mu = excluded.mu, sigma = excluded.sigma, games = excluded.games, updated = excluded.updated"
    ).bind(r.id, r.mu, r.sigma, r.games + 1, now)
  ));
  return "done";
}

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
    if (u.pathname === "/report" && req.method === "POST") {
      return text(await report(env, await req.formData(), now));
    }
    if (u.pathname === "/ratings") {
      const ids = q("ids").split(",").filter((id) => /^[0-9a-f]{32}$/.test(id)).slice(0, 64);
      const r = await loadRatings(env, [...new Set(ids)]);
      return text([...r.values()].map((p) => `${p.id} ${p.mu.toFixed(4)} ${p.sigma.toFixed(4)} ${p.games}`).join("\n"));
    }
    return text("quark server list");
  },
};
