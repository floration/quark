// quark internet server list: a Cloudflare Worker with a D1 database bound as DB.
// Setup steps: tools/master/README.md.
//   GET /announce?port=N&v=VERSION[&ip=A.B.C.D]  a server says it's up (every 60 s)
//   GET /remove?port=N[&ip=A.B.C.D]              a server shuts down
//   GET /list?v=VERSION                           ip:port per line, servers seen in the last 150 s
// The game checks every listed server over UDP itself, so a bogus entry just never shows up.
//
// Ranked (OpenSkill / Weng-Lin Plackett-Luce ratings):
//   POST /report  form: secret, match, ladder, p=id:team:place,...   one player's account of a finished match
//   GET  /ratings?ladder=L&ids=a,b,c                                 "id mu sigma games" per line (unknown ids: defaults)
//   GET  /leaderboard?ladder=L[&n=25]                                "rating games name" per line, best first (3+ games)
// Race time trials (single player, Viaduct):
//   POST /racetime  form: secret, map, ms, splits=ms,ms,... (time at each checkpoint), name (hex), v   kept only if
//                   it beats this player's best. The splits are stored so odd runs can be checked and deleted by
//                   hand: DELETE FROM race_times WHERE map = 'viaduct' AND id = '<id>' (ids: SELECT ... JOIN names).
//   GET  /raceboard?map=M[&n=25]                                      "ms name" per line, fastest first
// Ladders (each its own ratings): vanilla (all weapons), vampire (laser vampire), instagib. Rows are keyed "ladder:id".
// A player's id is the first 32 hex chars of sha256(secret); the secret never leaves their game except to
// here. A match counts only when every player in it has reported the exact same result; any disagreement
// voids it, and matches not fully reported within an hour are dropped. Hosts never see secrets, so a
// lying host just produces a mismatch.
const FRESH_MS = 150000;
const MU = 25, SIGMA = 25 / 3, BETA = SIGMA / 2, TAU = MU / 300, EPS = 1e-4;
const PENDING_MS = 3600000;
const LADDERS = new Set(["vanilla", "vampire", "instagib"]);
const RACE_MAPS = new Set(["viaduct"]);
const RACE_MIN_MS = 5000; // nothing finishes faster: below this it's not a real run

async function saveName(env, me, nameHex, now) {
  if (!/^([0-9a-f]{2}){1,20}$/.test(nameHex)) return;
  const name = nameHex.match(/../g).map((h) => String.fromCharCode(parseInt(h, 16))).join("");
  if (/^[\x20-\x7e]+$/.test(name))
    await env.DB.prepare("INSERT INTO names (id, name, updated) VALUES (?, ?, ?) ON CONFLICT(id) DO UPDATE SET name = excluded.name, updated = excluded.updated")
      .bind(me, name.trim().slice(0, 20) || "player", now).run();
}

async function raceTime(env, form, now) {
  const secret = String(form.get("secret") || ""), map = String(form.get("map") || "");
  if (!/^[0-9a-f]{64}$/.test(secret)) return "bad request";
  if (!RACE_MAPS.has(map)) return "not a ranked race map";
  const ms = parseInt(String(form.get("ms") || ""), 10);
  const splits = String(form.get("splits") || "").split(",").map((x) => parseInt(x, 10));
  if (!(ms >= RACE_MIN_MS && ms < 3600000)) return "bad time";
  // checkpoint times: increasing, the last one is the finish
  if (!splits.length || splits.length > 64 || splits.some((x, i) => !(x > 0) || (i && x <= splits[i - 1])) || splits[splits.length - 1] !== ms) return "bad splits";
  const me = (await sha256hex(secret)).slice(0, 32);
  await saveName(env, me, String(form.get("name") || ""), now);
  const best = await env.DB.prepare("SELECT ms FROM race_times WHERE map = ? AND id = ?").bind(map, me).first();
  if (best && best.ms <= ms) return `not faster (best ${best.ms})`;
  await env.DB.prepare(
    "INSERT INTO race_times (map, id, ms, splits, version, created) VALUES (?, ?, ?, ?, ?, ?) " +
    "ON CONFLICT(map, id) DO UPDATE SET ms = excluded.ms, splits = excluded.splits, version = excluded.version, created = excluded.created"
  ).bind(map, me, ms, splits.join(","), String(form.get("v") || "").slice(0, 16), now).run();
  return best ? `new best (was ${best.ms})` : "first time";
}

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

async function loadRatings(env, ladder, ids) {
  const map = new Map(ids.map((id) => [id, { id, mu: MU, sigma: SIGMA, games: 0 }]));
  if (!ids.length) return map;
  const { results } = await env.DB.prepare(
    `SELECT id, mu, sigma, games FROM players WHERE id IN (${ids.map(() => "?").join(",")})`
  ).bind(...ids.map((id) => ladder + ":" + id)).all();
  for (const r of results) {
    const id = r.id.slice(ladder.length + 1);
    map.set(id, { id, mu: r.mu, sigma: r.sigma, games: r.games });
  }
  return map;
}

async function report(env, form, now) {
  const secret = String(form.get("secret") || ""), match = String(form.get("match") || ""), ladder = String(form.get("ladder") || "");
  if (!/^[0-9a-f]{64}$/.test(secret) || !/^[0-9a-f]{16,32}$/.test(match)) return "bad request";
  if (!LADDERS.has(ladder)) return "not a ranked ladder";
  const res = parseResult(form.get("p"));
  if (!res) return "bad result";
  const me = (await sha256hex(secret)).slice(0, 32);
  if (!res.entries.some((e) => e.id === me)) return "not in this match";
  // the reporter's display name for the leaderboard (hex-encoded printable ASCII, up to 20 chars)
  await saveName(env, me, String(form.get("name") || ""), now);
  const result = ladder + "|" + res.text;
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
  const ratings = await loadRatings(env, ladder, res.entries.map((e) => e.id));
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
    ).bind(ladder + ":" + r.id, r.mu, r.sigma, r.games + 1, now)
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
    const text = (body, status = 200) => new Response(body, { status, headers: { "content-type": "text/plain", "access-control-allow-origin": "*" } });

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
    if (u.pathname === "/leaderboard") {
      const ladder = q("ladder");
      if (!LADDERS.has(ladder)) return text("not a ranked ladder", 400);
      const n = Math.min(100, Math.max(1, parseInt(q("n"), 10) || 25));
      const { results } = await env.DB.prepare(
        "SELECT p.mu, p.sigma, p.games, COALESCE(n.name, 'player') AS name FROM players p " +
        "LEFT JOIN names n ON n.id = substr(p.id, ?) WHERE p.id LIKE ? AND p.games >= 3 " +
        "ORDER BY (p.mu - 3 * p.sigma) DESC LIMIT ?"
      ).bind(ladder.length + 2, ladder + ":%", n).all();
      return text(results.map((r) => `${Math.round(Math.max(0, r.mu - 3 * r.sigma) * 40)} ${r.games} ${r.name}`).join("\n"));
    }
    if (u.pathname === "/racetime" && req.method === "POST") {
      return text(await raceTime(env, await req.formData(), now));
    }
    if (u.pathname === "/raceboard") {
      if (!RACE_MAPS.has(q("map"))) return text("not a ranked race map", 400);
      const n = Math.min(100, Math.max(1, parseInt(q("n"), 10) || 25));
      const { results } = await env.DB.prepare(
        "SELECT r.ms, COALESCE(n.name, 'player') AS name FROM race_times r LEFT JOIN names n ON n.id = r.id " +
        "WHERE r.map = ? ORDER BY r.ms ASC, r.created ASC LIMIT ?"
      ).bind(q("map"), n).all();
      return text(results.map((r) => `${r.ms} ${r.name}`).join("\n"));
    }
    if (u.pathname === "/ratings") {
      const ids = q("ids").split(",").filter((id) => /^[0-9a-f]{32}$/.test(id)).slice(0, 64);
      if (!LADDERS.has(q("ladder"))) return text("not a ranked ladder", 400);
      const r = await loadRatings(env, q("ladder"), [...new Set(ids)]);
      return text([...r.values()].map((p) => `${p.id} ${p.mu.toFixed(4)} ${p.sigma.toFixed(4)} ${p.games}`).join("\n"));
    }
    return text("quark server list");
  },
};
