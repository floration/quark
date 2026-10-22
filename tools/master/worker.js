// quark internet server list: a Cloudflare Worker with a D1 database bound as DB.
// Setup steps: tools/master/README.md.
//   GET /announce?port=N&v=VERSION[&ip=A.B.C.D][&token=T]  a server says it's up (every 60 s); answers "ok <token>"
//   GET /remove?port=N[&ip=A.B.C.D][&token=T]              a server shuts down
//   A server may announce / remove only its own source IPv4 address, or, through an IPv6 path (?ip=), an
//   address it holds the token for (issued by an earlier announce from that IPv4). ?ip= alone is never trusted.
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
// Discord link, crowns and queues (1.12.0+):
//   POST /link    form: secret, discord (hex), name (hex)       links a player to a Discord name (the bot gives them roles)
//   GET  /crowns                                                 "board place id name discord" for the top 10 of every
//                                                                ladder and the race board ("-" = no Discord linked)
//   POST /queue   form: (secret | bot + discord), mode, action=join|leave|leaveall, name (hex)
//                 game players are their ranked id, Discord users "d:<name>" (only the bot, with BOT_KEY, may add those)
//   GET  /queue                                                  "mode count size" per queue that has anyone in it
//   GET  /filled?since=N                                         "n mode created who:namehex:discordhex,..." per filled
//                                                                queue after n = N (the game and the bot both poll this)
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

// Rate limiting: token buckets kept in memory per isolate (Cloudflare runs a few isolates per region, so the
// real limit is a small multiple of these; cheap, and enough to stop a single client hammering D1). Keyed by
// endpoint plus client IP, and endpoint plus player id for the account endpoints.
const LIMITS = {
  ip: { burst: 120, perSec: 2 },        // any endpoint, per client IP
  write: { burst: 20, perSec: 0.2 },    // /report /racetime /lifetime /queue /link, per client IP
  id: { burst: 10, perSec: 0.1 },       // the same, per player id
};
const buckets = new Map();
function allow(kind, key, now) {
  const L = LIMITS[kind], k = kind + "|" + key;
  let b = buckets.get(k);
  if (!b) { b = { t: L.burst, at: now }; buckets.set(k, b); }
  b.t = Math.min(L.burst, b.t + ((now - b.at) / 1000) * L.perSec);
  b.at = now;
  if (buckets.size > 50000) for (const [key2, v] of buckets) if (now - v.at > 600000) buckets.delete(key2); // forget idle clients
  if (b.t < 1) return false;
  b.t -= 1;
  return true;
}
async function idOf(secret) { return /^[0-9a-f]{64}$/.test(secret) ? (await sha256hex(secret)).slice(0, 32) : ""; }
const randomToken = () => [...crypto.getRandomValues(new Uint8Array(16))].map((b) => b.toString(16).padStart(2, "0")).join("");

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

// Queues shared by the game and the Discord bot: when a mode has enough players, they're all taken out of every
// queue and a "filled" row tells everyone (the bot pings them on Discord, the game shows it in game).
const QUEUE_SIZES = {
  "ranked-vanilla": 2, "ranked-vampire": 2, "ranked-instagib": 4, vs: 6, bomb: 6, koth: 6, payload: 6, npayload: 6,
  points: 6, duel: 2, "1v1": 2, tourney: 4, ffa: 4, race: 2, hide: 4, tag: 4,
};
const QUEUE_STALE_MS = 30 * 60000; // nobody waits forever: entries older than 30 minutes drop out
const hexText = (h, max) => /^([0-9a-f]{2})+$/.test(h || "") ? h.match(/../g).map((x) => String.fromCharCode(parseInt(x, 16))).join("").replace(/[^\x20-\x7e]/g, "").slice(0, max) : "";
const textHex = (t) => [...String(t)].map((c) => c.charCodeAt(0).toString(16).padStart(2, "0")).join("");

async function link(env, form, now) {
  const secret = String(form.get("secret") || "");
  if (!/^[0-9a-f]{64}$/.test(secret)) return "bad request";
  const me = (await sha256hex(secret)).slice(0, 32);
  await saveName(env, me, String(form.get("name") || ""), now);
  const discord = hexText(String(form.get("discord") || ""), 40).trim();
  if (!discord) { await env.DB.prepare("DELETE FROM links WHERE id = ?").bind(me).run(); return "unlinked"; }
  await env.DB.prepare("INSERT INTO links (id, discord, updated) VALUES (?, ?, ?) ON CONFLICT(id) DO UPDATE SET discord = excluded.discord, updated = excluded.updated")
    .bind(me, discord, now).run();
  return "linked " + discord;
}

// Lifetime kills / deaths: each game client adds what it got since its last report (at the end of a match, or when
// it leaves). Capped per report, and at most one report every 20 s per player, so a cheater can't inflate it fast.
const LIFETIME_MAX_PER_REPORT = 300;
const LIFETIME_MAX_PER_HOUR = 1200; // kills (and, separately, deaths) one id can add per clock hour
async function lifetimeReport(env, form, now) {
  const secret = String(form.get("secret") || "");
  if (!/^[0-9a-f]{64}$/.test(secret)) return "bad request";
  const kills = parseInt(String(form.get("kills") || "0"), 10), deaths = parseInt(String(form.get("deaths") || "0"), 10);
  if (!(kills >= 0 && deaths >= 0 && kills <= LIFETIME_MAX_PER_REPORT && deaths <= LIFETIME_MAX_PER_REPORT)) return "bad numbers";
  const me = (await sha256hex(secret)).slice(0, 32);
  await saveName(env, me, String(form.get("name") || ""), now);
  const row = await env.DB.prepare("SELECT updated, hour_start, hour_kills, hour_deaths FROM lifetime WHERE id = ?").bind(me).first();
  if (row && now - row.updated < 20000) return "too soon";
  const hour = Math.floor(now / 3600000) * 3600000;
  const sameHour = row && row.hour_start === hour;
  const hk = (sameHour ? row.hour_kills : 0) + kills, hd = (sameHour ? row.hour_deaths : 0) + deaths;
  if (hk > LIFETIME_MAX_PER_HOUR || hd > LIFETIME_MAX_PER_HOUR) return "hourly limit";
  await env.DB.prepare("INSERT INTO lifetime (id, kills, deaths, updated, hour_start, hour_kills, hour_deaths) VALUES (?, ?, ?, ?, ?, ?, ?) " +
    "ON CONFLICT(id) DO UPDATE SET kills = kills + excluded.kills, deaths = deaths + excluded.deaths, updated = excluded.updated, " +
    "hour_start = excluded.hour_start, hour_kills = excluded.hour_kills, hour_deaths = excluded.hour_deaths")
    .bind(me, kills, deaths, now, hour, hk, hd).run();
  return "ok";
}
// GET /lifetime?ids=a,b,c (ranked ids, up to 32): "id kills deaths" lines. GET /lifetime: the top 10 by kills,
// "kills deaths id namehex" lines.
async function lifetimeGet(env, ids) {
  if (ids) {
    const list = ids.split(",").filter((x) => /^[0-9a-f]{32}$/.test(x)).slice(0, 32);
    if (!list.length) return "";
    const { results } = await env.DB.prepare(`SELECT id, kills, deaths FROM lifetime WHERE id IN (${list.map(() => "?").join(",")})`).bind(...list).all();
    return results.map((r) => `${r.id} ${r.kills} ${r.deaths}`).join("\n");
  }
  const { results } = await env.DB.prepare("SELECT l.id, l.kills, l.deaths, COALESCE(n.name, 'player') AS name FROM lifetime l LEFT JOIN names n ON n.id = l.id ORDER BY l.kills DESC LIMIT 10").all();
  return results.map((r) => `${r.kills} ${r.deaths} ${r.id} ${textHex(r.name)}`).join("\n");
}

async function crowns(env) {
  const out = [];
  for (const ladder of LADDERS) {
    const { results } = await env.DB.prepare(
      "SELECT substr(p.id, ?) AS id, COALESCE(n.name, 'player') AS name, COALESCE(l.discord, '-') AS discord FROM players p " +
      "LEFT JOIN names n ON n.id = substr(p.id, ?) LEFT JOIN links l ON l.id = substr(p.id, ?) " +
      "WHERE p.id LIKE ? AND p.games >= 3 ORDER BY (p.mu - 3 * p.sigma) DESC LIMIT 10"
    ).bind(ladder.length + 2, ladder.length + 2, ladder.length + 2, ladder + ":%").all();
    results.forEach((r, i) => out.push(`${ladder} ${i + 1} ${r.id} ${textHex(r.name)} ${r.discord === "-" ? "-" : textHex(r.discord)}`));
  }
  for (const map of RACE_MAPS) {
    const { results } = await env.DB.prepare(
      "SELECT r.id AS id, COALESCE(n.name, 'player') AS name, COALESCE(l.discord, '-') AS discord FROM race_times r " +
      "LEFT JOIN names n ON n.id = r.id LEFT JOIN links l ON l.id = r.id WHERE r.map = ? ORDER BY r.ms ASC, r.created ASC LIMIT 10"
    ).bind(map).all();
    results.forEach((r, i) => out.push(`race:${map} ${i + 1} ${r.id} ${textHex(r.name)} ${r.discord === "-" ? "-" : textHex(r.discord)}`));
  }
  return out.join("\n");
}

async function queue(env, form, now) {
  const mode = String(form.get("mode") || ""), action = String(form.get("action") || "join");
  let who, discord = "";
  const secret = String(form.get("secret") || "");
  if (/^[0-9a-f]{64}$/.test(secret)) {
    who = (await sha256hex(secret)).slice(0, 32);
    await saveName(env, who, String(form.get("name") || ""), now);
    const l = await env.DB.prepare("SELECT discord FROM links WHERE id = ?").bind(who).first();
    if (l) discord = l.discord;
  } else if (env.BOT_KEY && String(form.get("bot") || "") === env.BOT_KEY) {
    discord = hexText(String(form.get("discord") || ""), 40).trim();
    if (!discord) return "bad request";
    who = "d:" + discord;
  } else return "bad request";
  const name = hexText(String(form.get("name") || ""), 20) || discord || "player";
  await env.DB.prepare("DELETE FROM queue WHERE joined < ?").bind(now - QUEUE_STALE_MS).run();
  if (action === "leaveall") { await env.DB.prepare("DELETE FROM queue WHERE who = ?").bind(who).run(); return "left all"; }
  if (!(mode in QUEUE_SIZES)) return "not a queue";
  if (action === "leave") { await env.DB.prepare("DELETE FROM queue WHERE mode = ? AND who = ?").bind(mode, who).run(); return "left " + mode; }
  await env.DB.prepare("INSERT OR IGNORE INTO queue (mode, who, name, discord, joined) VALUES (?, ?, ?, ?, ?)").bind(mode, who, name, discord, now).run();
  const { results } = await env.DB.prepare("SELECT who, name, discord FROM queue WHERE mode = ? ORDER BY joined ASC").bind(mode).all();
  const size = QUEUE_SIZES[mode];
  if (results.length < size) return `queued ${mode} ${results.length}/${size}`;
  const take = results.slice(0, size);
  const members = take.map((r) => `${r.who}:${textHex(r.name)}:${r.discord ? textHex(r.discord) : "-"}`).join(",");
  await env.DB.prepare("INSERT INTO filled (mode, members, created) VALUES (?, ?, ?)").bind(mode, members, now).run();
  for (const r of take) await env.DB.prepare("DELETE FROM queue WHERE who = ?").bind(r.who).run();
  return `filled ${mode}`;
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
    const conn = req.headers.get("CF-Connecting-IP") || "";
    const srcIp = ipv4(conn); // the caller's own IPv4, if it came over IPv4 (can't be spoofed over TCP)
    const text = (body, status = 200) => new Response(body, { status, headers: { "content-type": "text/plain", "access-control-allow-origin": "*" } });
    if (!allow("ip", conn, now)) return text("rate limited", 429);
    const WRITES = new Set(["/report", "/racetime", "/lifetime", "/queue", "/link"]);
    let form = null;
    if (req.method === "POST" && WRITES.has(u.pathname)) {
      if (!allow("write", conn, now)) return text("rate limited", 429);
      form = await req.formData();
      const pid = await idOf(String(form.get("secret") || ""));
      if (pid && !allow("id", pid, now)) return text("rate limited", 429);
    }

    if (u.pathname === "/announce" || u.pathname === "/remove") {
      // Which address: the source IPv4, or ?ip= from an IPv6 path. Allowed only for the source address itself
      // or with the token an earlier announce from that address was given.
      const port = parseInt(q("port"), 10);
      const ip = srcIp || ipv4(q("ip"));
      if (!ip || !(port > 0 && port < 65536)) return text("bad request", 400);
      const addr = ip + ":" + port, token = q("token");
      const row = await env.DB.prepare("SELECT token FROM servers WHERE addr = ?").bind(addr).first();
      const ownSource = srcIp === ip;
      const tokenOk = !!(row && row.token && /^[0-9a-f]{32}$/.test(token) && row.token === token);
      if (!ownSource && !tokenOk) return text("forbidden: announce once over IPv4 to get a token", 403);
      if (u.pathname === "/remove") {
        await env.DB.prepare("DELETE FROM servers WHERE addr = ?").bind(addr).run();
        return text("removed");
      }
      const newToken = tokenOk ? token : randomToken();
      await env.DB.prepare(
        "INSERT INTO servers (addr, version, seen, token) VALUES (?, ?, ?, ?) " +
        "ON CONFLICT(addr) DO UPDATE SET version = excluded.version, seen = excluded.seen, token = excluded.token"
      ).bind(addr, q("v").slice(0, 16), now, newToken).run();
      if (Math.random() < 0.02) await env.DB.prepare("DELETE FROM servers WHERE seen < ?").bind(now - 3600000).run();
      return text("ok " + newToken);
    }
    if (u.pathname === "/list") {
      const { results } = await env.DB.prepare(
        "SELECT addr FROM servers WHERE seen > ? AND version = ? ORDER BY seen DESC LIMIT 200"
      ).bind(now - FRESH_MS, q("v").slice(0, 16)).all();
      return text(results.map((r) => r.addr).join("\n"));
    }
    if (u.pathname === "/report" && req.method === "POST") {
      return text(await report(env, form, now));
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
      return text(await raceTime(env, form, now));
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
    if (u.pathname === "/link" && req.method === "POST") return text(await link(env, form, now));
    if (u.pathname === "/crowns") return text(await crowns(env));
    if (u.pathname === "/lifetime" && req.method === "POST") return text(await lifetimeReport(env, form, now));
    if (u.pathname === "/lifetime") return text(await lifetimeGet(env, q("ids")));
    if (u.pathname === "/queue" && req.method === "POST") return text(await queue(env, form, now));
    if (u.pathname === "/queue") {
      await env.DB.prepare("DELETE FROM queue WHERE joined < ?").bind(now - QUEUE_STALE_MS).run();
      const { results } = await env.DB.prepare("SELECT mode, COUNT(*) AS n FROM queue GROUP BY mode").all();
      return text(results.map((r) => `${r.mode} ${r.n} ${QUEUE_SIZES[r.mode] || 0}`).join("\n"));
    }
    if (u.pathname === "/filled") {
      const since = parseInt(q("since"), 10) || 0;
      const { results } = await env.DB.prepare("SELECT id, mode, members, created FROM filled WHERE id > ? AND created > ? ORDER BY id ASC LIMIT 50")
        .bind(since, now - 3600000).all();
      return text(results.map((r) => `${r.id} ${r.mode} ${r.created} ${r.members}`).join("\n"));
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
