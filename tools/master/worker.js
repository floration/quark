// quark internet server list: a Cloudflare Worker with a D1 database bound as DB.
// Setup steps: tools/master/README.md.
// Every write is a POST (application/x-www-form-urlencoded, at most MAX_BODY bytes) and is refused when it carries an
// Origin header (a web page making its visitors' browsers write here); only GET reads answer with CORS "*". GET reads
// are cached briefly. Keys (BOT_KEY, ADMIN_KEY, server tokens) are compared in constant time (keyEq) and only ever
// travel in a POST body, never in a URL (URLs end up in logs and browser history).
//   POST /announce form: port, v (version)[, ip=A.B.C.D, token=T][, hidden=1]  a server says it's up (every 60 s);
//                  answers "ok <token>". At most MAX_SERVERS_PER_IP fresh entries per address.
//   POST /remove   form: port[, ip, token]                                  a server shuts down
//   A server may announce / remove only its own source IPv4 address, or, through an IPv6 path (ip=), an address it
//   holds the token for (issued by an earlier announce from that IPv4). ip= alone is never trusted.
//   GET  /list?v=VERSION                            ip:port per line, servers seen in the last 150 s
// The game checks every listed server over UDP itself, so a bogus entry just never shows up.
//   hidden=1 registers (for a token) without being listed: ranked / time trial hosts.
//
// Ranked ids: a player's game keeps a random 64-hex secret. Its Ed25519 key is derived from it (seed =
// sha256("quark-rank-ed25519-v1:" + secret)); the id is the first 32 hex chars of sha256(public key hex). Game servers
// check that id at connect (the player signs a challenge), so nobody can play under someone else's id. The secret
// goes only to this Worker. Ids before 1.17 were sha256(secret): POST /hello (or any request with the secret) moves an
// old id's rows to the new id once (id_moves).
//   POST /hello   form: secret                                  "id <id>" (and the move above)
//   POST /number  form: secret                                  "num <N>": this player's number (1.20+), handed out
//                                                                in order the first time the Worker sees a player
//                                                                (here or with a name); a new player's game takes
//                                                                "player<N>" as its default name
// Match tickets (servers, with their announce token; form also carries port, token[, ip]):
//   POST /ticket  form: kind=match, match (16 hex), ladder, map, players=id,..., keys=pub,..., sigs=sig,..., addrs=ip:port,...
//                 when a ranked match goes live; each player's key must hash to their id and have signed
//                 "quark-ticket-v2 <match> <ip:port>" (their game signs it in the match; ip:port is the server address
//                 it dialled, which must be this server's: see signedForServer), so a server can only ticket players
//                 who are really in it, on it.
//                 form: kind=race, match (16 hex), map, players=<racer id>, keys=pub, sigs=sig, addrs=ip:port   when a
//                 time trial starts; the racer's game signs "quark-ticket-v2 <match> <ip:port>" the same way, so a
//                 server can only post runs (and the name that goes with them) for a player who is really on it.
//                 answers "ticket <match>". One open match ticket per server (a new one voids the last if that has
//                 no result yet); a server opens at most one a minute.
//   POST /result  form: match, p=id:team:place,...[, left=id,...]   the server's own account of the ticketed match;
//                 left: players who left during it (first leaver first; their game can't report)
//   POST /racerun form: ticket, map, id, ms, splits, name (hex), v   a finished run the server saw, under a race ticket
//                 (the name is saved only with a run that's kept)
//   A server whose announced version is a testing build (1.19.1.42; 1.19.0.testing.42 from 1.19.0, 1.18.9-test.42 before: the testing
//   server every box runs next to production, tools/server/install.sh) gets none of these: "testing server: ranked
//   not rated" (403). Players' /report under a ticket from such a server is refused the same way. Its matches are
//   played and tested, never rated (the game server doesn't send them either: Server::rated).
// Ranked (OpenSkill / Weng-Lin Plackett-Luce ratings):
//   POST /report  form: secret, match, ladder, p=id:team:place,...[, dispute=1]   one player's account of a finished
//                 match (dispute=1: their game saw a different result than the host's p)
//                 Accepted only under a ticket from that ticket's players, once each, until TICKET_MS after it opened.
//                 The server's /result must come MIN_MATCH_MS or more after the ticket opened (sooner only when it
//                 names a leaver, so leaving in the first minute still loses), else the match is void. Rated when the server's result and the players' reports agree: every player who didn't
//                 leave reported it, or REPORT_WAIT_MS passed with at least one agreeing and nobody against. Any report
//                 against it (1.17.0, owner: "just logged and unrated") makes the match
//                 'disputed': never rated, kept as a log the owner can read with /disputes. A match still undecided
//                 when its ticket expires (TICKET_MS) is rated if the server's result is in and a player agreed with
//                 nobody against, else void. New accounts are provisional (see
//                 PROVISIONAL_GAMES).
//   POST /disputes form: key (ADMIN_KEY)                            "match ladder created result" per disputed match (the log)
//   GET  /ratings?ladder=L&ids=a,b,c                                 "id mu sigma games" per line (unknown ids: defaults)
//   GET  /leaderboard?ladder=L[&n=25]                                "rating games name" per line, best first (placed only)
// Race time trials (single player, on the ranked race and surf maps: RACE_MAPS): runs come from the game server (/racerun
//   above), kept only if they beat that player's best and no leg is faster than the map's floors allow. POST /racetime
//   (the old self-report from the racer's own game) is refused. Odd runs can still be deleted by hand:
//   DELETE FROM race_times WHERE map = '<map>' AND id = '<id>' (ids: SELECT ... JOIN names).
//   GET  /raceboard?map=M[&n=25]                                      "ms name" per line, fastest first (one map)
//   GET  /racetotal[?n=25]                                            "total_ms maps_run name" per line, fastest first:
//                                                                the race total, each racer with a run on any ranked
//                                                                race map, their best on every one summed (a map
//                                                                they haven't run counts at its defaultMs)
// Discord link, crowns and queues (1.12.0+):
//   POST /linkcode form: bot, discord (hex)                     the bot asks for a one-time code for a Discord user who
//                                                                ran /link there: "code NNNNNN" (valid LINK_CODE_MS)
//   POST /link    form: secret, code, name (hex) | secret, unlink=1   the code typed in game links that player to the
//                                                                Discord user (verified); only verified links get roles
//   GET  /crowns                                                 "board place id name discord" for the top 10 of every
//                                                                ladder and the race board ("-" = no Discord linked)
//   POST /queue   form: (secret | bot + discord), mode, action=join|leave|leaveall, name (hex)
//                 game players are their ranked id, Discord users "d:<name>" (only the bot, with BOT_KEY, may add those)
//   GET  /queue                                                  "mode count size" per queue that has anyone in it
//   GET  /filled?since=N                                         "n mode created who:namehex:discordhex,..." per filled
//                                                                queue after n = N (the game and the bot both poll this)
// PRIVACY: /crowns and /filled are public. They show a linked player's ranked id, in-game name and Discord name
// together (that's what the Discord roles and pings need); unlinking (type "unlink" in game) removes the Discord part.
// The game says so where you link (the settings screen's Discord box).
// Ladders (each its own ratings): vanilla (all weapons), vampire (laser vampire), instagib. Rows are keyed "ladder:id".
// Cron (scheduled handler, every 15 minutes): rates matches whose report wait is over, decides (rates or voids)
// matches whose ticket expired undecided, and prunes old rows.
const FRESH_MS = 150000;
const MU = 25, SIGMA = 25 / 3, BETA = SIGMA / 2, TAU = MU / 300, EPS = 1e-4;
const LADDERS = new Set(["vanilla", "vampire", "instagib"]);
const RACE_MIN_MS = 5000; // nothing finishes faster: below this it's not a real run
// The ranked race maps (the official maps with "for race" or "for surf"), by name: defaultMs, what the race total
// counts for a racer with no run on that map (its "racedefault <s>" line, or 600 s until it has one), and floors,
// the fastest plausible time per leg (start -> checkpoint 1, then between checkpoints) in ms: the straight-line gap
// between the boxes at 48 m/s. Written by
// `build/quark_tests racefloors` (paste its output over this table; quark_tests checks this copy matches the maps).
// A map taken off this table leaves its rows in race_times, unused (on no board or total) until it comes back.
export const RACE_MAPS = {
  accelerator: { defaultMs: 600000, floors: [1791, 3416, 1770, 2041, 2510, 781, 1683, 2291, 1895, 3942, 3671, 2106, 149, 750] },
  jungle: { defaultMs: 180000, floors: [1041, 1583, 2000, 1984, 1418, 1460, 631, 790, 3084, 2834, 3166, 279] },
  rooftoprun: { defaultMs: 180000, floors: [2770, 1311, 1389, 410, 3291, 1542, 1253, 2541, 252, 446, 1198, 2750] },
  solar: { defaultMs: 600000, floors: [7630, 21422, 11625, 8047, 10983, 45419, 19582, 14865, 11091, 12515, 46109, 15571, 15369, 31324, 38624] },
  surfy: { defaultMs: 600000, floors: [1607] },
};
const isRaceMap = (map) => Object.prototype.hasOwnProperty.call(RACE_MAPS, map);
const TICKET_MS = 2 * 3600000;       // a match ticket takes reports for this long
const MIN_MATCH_MS = 60000;          // no server result before this (no ranked match is over in under a minute) unless someone left
const TICKET_GAP_MS = 60000;         // a server opens at most one match ticket per this
const RACE_TICKET_MS = 12 * 3600000; // a time trial session
const RACE_SLACK_MS = 30000;         // a run can't be longer than its ticket is old (plus request delays)
// New accounts: a player's rating counts toward anyone else's only once they have finished PROVISIONAL_GAMES ticketed
// matches. Until then their own rating moves (so it settles) but an established player's doesn't move from a match
// against them, and they're left off the leaderboard and crowns. Chosen over proof of work at account creation:
// work is cheap for a farmer on a GPU and costly for a real player on a laptop, while ticketed games need a real
// server, a minute each and a server address that can only open one a minute.
// One placement threshold, everywhere: games before a ladder rating counts as placed (on the leaderboard and crowns)
// and ticketed matches before an account is established. The game's kPlacementGames (ranked.h) is the same number.
const PLACEMENT_GAMES = 5;
const PROVISIONAL_GAMES = PLACEMENT_GAMES;
const REPORT_WAIT_MS = 10 * 60000;   // after the server's result: missing reports stop holding the match up
const MAX_SERVERS_PER_IP = 8;        // fresh /announce entries one address may hold (WEB-5)
const MAX_BODY = 16384;              // bytes of form a request may carry (counted as it's read, not from Content-Length)
// Cache API: only works on a custom domain (on *.workers.dev caches.default stores nothing); the Cache-Control
// header still lets clients and proxies keep reads for these many seconds.
const CACHE_S = { "/list": 20, "/leaderboard": 60, "/raceboard": 60, "/racetotal": 60, "/crowns": 60, "/lifetime": 30, "/bench": 30, "/queue": 10, "/filled": 5, "/ratings": 10 };
const LINK_CODE_MS = 10 * 60000;

// Rate limiting: token buckets kept in memory per isolate (Cloudflare runs a few isolates per region, so the
// real limit is a small multiple of these; cheap, and enough to stop a single client hammering D1). Keyed by
// client IP, by player id for the account endpoints, and by server address for the server endpoints.
// Many players can share one address (a LAN party, a dorm, carrier NAT), so the per-IP buckets are generous and
// the per-id bucket is the real limit on a player. The Discord bot (a write carrying BOT_KEY) has its own bucket
// and spends none of its address's: one "Queue for all" is 16 POSTs.
const LIMITS = {
  ip: { burst: 240, perSec: 6 },        // every request except the bot's, per client IP (a few dozen games polling /queue, /filled)
  write: { burst: 60, perSec: 0.5 },    // also, per client IP: POST /hello /number /report /racetime /lifetime /bench /queue /link /linkcode
                                        // /disputes (not /announce /remove: they're per address and token)
  id: { burst: 10, perSec: 0.1 },       // the same writes, per player id (when they carry a secret)
  bot: { burst: 200, perSec: 5 },       // instead of ip + write: requests carrying the right BOT_KEY (/queue /linkcode)
  linkfail: { burst: 5, perSec: 1 / 60 }, // /link code attempts per client IP (a right code gives its token back)
  server: { burst: 30, perSec: 0.2 },   // /ticket /result /racerun per server address (after ip)
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
function refund(kind, key) { const b = buckets.get(kind + "|" + key); if (b) b.t = Math.min(LIMITS[kind].burst, b.t + 1); }
// A secret key or token check that takes the same time however much of a guess is right (`!==` stops at the first
// wrong character): it always runs over the whole expected key. False when no key is set.
export function keyEq(given, expected) {
  if (typeof expected !== "string" || !expected) return false;
  const a = new TextEncoder().encode(String(given ?? "")), b = new TextEncoder().encode(expected);
  let diff = a.length ^ b.length;
  for (let i = 0; i < b.length; i++) diff |= b[i] ^ (i < a.length ? a[i] : 0);
  return diff === 0;
}
// The request body as a form, read up to MAX_BODY bytes whatever Content-Length claims (a chunked or lying request
// can't make the Worker buffer more). null: too big; throws on a body that isn't a form.
async function readForm(req, max) {
  const parts = [];
  let n = 0;
  if (req.body) {
    const reader = req.body.getReader();
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      n += value.byteLength;
      if (n > max) { try { await reader.cancel(); } catch {} return null; }
      parts.push(value);
    }
  }
  const body = new Uint8Array(n);
  let o = 0;
  for (const p of parts) { body.set(p, o); o += p.byteLength; }
  return new Response(body, { headers: { "content-type": req.headers.get("content-type") || "application/x-www-form-urlencoded" } }).formData();
}
// Ranked identity from the secret (see the top of this file); kept per isolate so a busy player costs one derivation.
const keyCache = new Map();
const hexOf = (buf) => [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, "0")).join("");
const bytesOf = (hex) => new Uint8Array(hex.match(/../g).map((h) => parseInt(h, 16)));
const PKCS8_ED25519 = "302e020100300506032b657004220420";
export async function rankKeyOf(secret) {
  if (!/^[0-9a-f]{64}$/.test(secret)) return null;
  let k = keyCache.get(secret);
  if (k) return k;
  const seed = await sha256hex("quark-rank-ed25519-v1:" + secret);
  const priv = await crypto.subtle.importKey("pkcs8", bytesOf(PKCS8_ED25519 + seed), { name: "Ed25519" }, true, ["sign"]);
  const jwk = await crypto.subtle.exportKey("jwk", priv);
  const pub = hexOf(Uint8Array.from(atob(jwk.x.replace(/-/g, "+").replace(/_/g, "/") + "=".repeat((4 - jwk.x.length % 4) % 4)), (c) => c.charCodeAt(0)));
  k = { pub, id: (await sha256hex(pub)).slice(0, 32), legacy: (await sha256hex(secret)).slice(0, 32) };
  if (keyCache.size > 20000) keyCache.clear();
  keyCache.set(secret, k);
  return k;
}
async function idOf(secret) { const k = await rankKeyOf(secret); return k ? k.id : ""; }
async function ed25519Verify(pubHex, sigHex, msg) {
  if (!/^[0-9a-f]{64}$/.test(pubHex) || !/^[0-9a-f]{128}$/.test(sigHex)) return false;
  try {
    const key = await crypto.subtle.importKey("raw", bytesOf(pubHex), { name: "Ed25519" }, false, ["verify"]);
    return await crypto.subtle.verify({ name: "Ed25519" }, key, bytesOf(sigHex), new TextEncoder().encode(msg));
  } catch { return false; }
}

// The player behind a secret, after moving a pre-1.17 id's rows (ratings, account, name, Discord link, lifetime
// stats, race times) to the new id the first time this secret shows up. "" if the secret is malformed.
// The new id can already have rows (REVIEW 3.6): a match rated before this secret reached us (the other player's
// report, the cron) creates its ladder row and account. They're merged, not dropped: a ladder keeps the rating of
// the row with more games (the old one, normally: the new one has the match or two since) and counts both rows'
// games; the account keeps the most ticketed games and the earliest start. A name the new id already has is newer
// (only a server's race run saves one without the secret) and stays; links and lifetime need the secret, so the new
// id can't have them yet; race times keep the faster run.
const moved = new Set();
async function playerOf(env, secret, now) {
  const k = await rankKeyOf(secret);
  if (!k) return "";
  if (moved.has(k.legacy)) return k.id;
  const done = await env.DB.prepare("SELECT new FROM id_moves WHERE old = ?").bind(k.legacy).first();
  if (!done) {
    const old = k.legacy, id = k.id, ladderIds = [...LADDERS].map((l) => l + ":" + old);
    const q = (sql, ...a) => env.DB.prepare(sql).bind(...a);
    await env.DB.batch([
      // both rows: merge into the new one (ver + 1, so a rating computed from either row before this can't land)
      ...[...LADDERS].map((l) => q(
        "UPDATE players SET mu = CASE WHEN o.games > players.games THEN o.mu ELSE players.mu END, " +
        "sigma = CASE WHEN o.games > players.games THEN o.sigma ELSE players.sigma END, games = players.games + o.games, " +
        "updated = MAX(players.updated, o.updated), ver = MAX(players.ver, o.ver) + 1 " +
        "FROM (SELECT mu, sigma, games, updated, ver FROM players WHERE id = ?) AS o WHERE players.id = ?", l + ":" + old, l + ":" + id)),
      ...[...LADDERS].map((l) => q("UPDATE OR IGNORE players SET id = ? WHERE id = ?", l + ":" + id, l + ":" + old)),   // only the old row: it moves
      q(`DELETE FROM players WHERE id IN (${ladderIds.map(() => "?").join(",")})`, ...ladderIds),   // (merged above)
      q("UPDATE accounts SET tgames = MAX(accounts.tgames, o.tgames), created = MIN(accounts.created, o.created) " +
        "FROM (SELECT tgames, created FROM accounts WHERE id = ?) AS o WHERE accounts.id = ?", old, id),
      ...["accounts", "names", "links", "lifetime", "player_numbers"].flatMap((t) => [q(`UPDATE OR IGNORE ${t} SET id = ? WHERE id = ?`, id, old), q(`DELETE FROM ${t} WHERE id = ?`, old)]),
      q("INSERT INTO race_times (map, id, ms, splits, version, created) SELECT map, ?, ms, splits, version, created FROM race_times WHERE id = ? AND 1 " +
        "ON CONFLICT(map, id) DO UPDATE SET ms = excluded.ms, splits = excluded.splits, version = excluded.version, created = excluded.created WHERE excluded.ms < race_times.ms", id, old),
      q("DELETE FROM race_times WHERE id = ?", old),
      q("DELETE FROM queue WHERE who = ?", old),
      q("INSERT OR IGNORE INTO id_moves (old, new, moved) VALUES (?, ?, ?)", old, id, now),
    ]);
  }
  if (moved.size > 50000) moved.clear();
  moved.add(k.legacy);
  return k.id;
}
const randomToken = () => [...crypto.getRandomValues(new Uint8Array(16))].map((b) => b.toString(16).padStart(2, "0")).join("");

// (only when missing: a conflicting INSERT OR IGNORE would still use up an AUTOINCREMENT number; OR IGNORE stays for
// two first requests at once, which can cost one gap and nothing else)
const NUMBER_SQL = "INSERT OR IGNORE INTO player_numbers (id, created) SELECT ?, ? WHERE NOT EXISTS (SELECT 1 FROM player_numbers WHERE id = ?)";
// A player's number (POST /number): the next one the first time a player is seen, kept for good. null if the table
// isn't there yet (the worker deployed before migrations/0003): nothing else depends on it.
async function playerNumber(env, me, now) {
  try {
    await env.DB.prepare(NUMBER_SQL).bind(me, now, me).run();
    const r = await env.DB.prepare("SELECT n FROM player_numbers WHERE id = ?").bind(me).first();
    return r ? r.n : null;
  } catch (e) { return null; }
}

async function saveName(env, me, nameHex, now) {
  if (!/^([0-9a-f]{2}){1,20}$/.test(nameHex)) return;
  try { await env.DB.prepare(NUMBER_SQL).bind(me, now, me).run(); } catch (e) {}   // (numbered in the order players are first seen)
  const name = nameHex.match(/../g).map((h) => String.fromCharCode(parseInt(h, 16))).join("");
  if (/^[\x20-\x7e]+$/.test(name))
    await env.DB.prepare("INSERT INTO names (id, name, updated) VALUES (?, ?, ?) ON CONFLICT(id) DO UPDATE SET name = excluded.name, updated = excluded.updated")
      .bind(me, name.trim().slice(0, 20) || "player", now).run();
}

// A version that isn't a production release x.y.z: CI's testing builds, "1.19.1.42" or "1.19.0.testing.42" (from 1.19.0 on:
// anything past x.y.z) or "1.18.9-test.42" (before: a prerelease suffix); "~" marks a hidden server, "+..." is build metadata.
// Testing servers play ranked but are never rated (above, /ticket ... /racerun).
export function isTestingVersion(v) {
  const s = String(v || "").split("+")[0];
  return s.includes("-") || s.split(".").length > 3;
}

// The server that ran a match / time trial: its ip:port must have announced recently with this token.
// Returns { addr, version } (the version it announced), or null.
async function serverAuth(env, form, srcIp, now) {
  const port = parseInt(String(form.get("port") || ""), 10), token = String(form.get("token") || "");
  const ip = srcIp || ipv4(String(form.get("ip") || ""));
  if (!ip || !(port > 0 && port < 65536) || !/^[0-9a-f]{32}$/.test(token)) return null;
  const addr = ip + ":" + port;
  const row = await env.DB.prepare("SELECT token, seen, version FROM servers WHERE addr = ?").bind(addr).first();
  if (!row || !keyEq(token, row.token) || now - row.seen > FRESH_MS) return null;
  return { addr, version: String(row.version || "") };
}

// Is `dialled` (the ip:port a player's game signed) the server at `server` (its announced ip:port)? Exactly, or a LAN /
// same-machine address on the same port: 10/8, 172.16/12, 192.168/16, 100.64/10 (carrier NAT), 169.254/16, 127/8.
export function signedForServer(dialled, server) {
  const m = /^(\d{1,3}(?:\.\d{1,3}){3}):(\d{1,5})$/.exec(String(dialled || ""));
  if (!m || !ipv4(m[1])) return false;
  if (m[0] === server) return true;
  if (m[2] !== server.slice(server.lastIndexOf(":") + 1)) return false;
  const [a, b] = m[1].split(".").map(Number);
  return a === 10 || a === 127 || (a === 172 && b >= 16 && b < 32) || (a === 192 && b === 168) || (a === 100 && b >= 64 && b < 128) || (a === 169 && b === 254);
}

async function openTicket(env, form, addr, now) {
  const kind = String(form.get("kind") || ""), match = String(form.get("match") || ""), map = String(form.get("map") || "");
  if (!/^[0-9a-f]{16}$/.test(match) || !/^[a-z0-9_]{1,32}$/.test(map)) return "bad request";
  const players = String(form.get("players") || "").split(",");
  if (players.some((p) => !/^[0-9a-f]{32}$/.test(p)) || new Set(players).size !== players.length) return "bad players";
  let ladder = "";
  if (kind === "match") {
    ladder = String(form.get("ladder") || "");
    if (!LADDERS.has(ladder)) return "not a ranked ladder";
    if (players.length < 2 || players.length > 16) return "bad players";
  } else if (kind === "race") {
    if (!isRaceMap(map)) return "not a ranked race map";
    if (players.length !== 1) return "bad players";
  } else return "bad request";
  // every player vouched for this match / time trial: their key (which hashes to their id) signed its id in the game,
  // so a server can only ticket players who are really on it (a race ticket names whose time and name it may post)
  // The signature names the server the player's game dialled (REVIEW 2.2: else a server could relay the match ids of
  // a real ranked server to its own players and get their signatures for it): this server's ip:port, or, for a player
  // on its LAN or its own machine, a private / loopback address on its port (which only that server could check).
  const keys = String(form.get("keys") || "").split(","), sigs = String(form.get("sigs") || "").split(","), addrs = String(form.get("addrs") || "").split(",");
  if (!form.get("keys") || !form.get("sigs") || keys.length !== players.length || sigs.length !== players.length) return "missing signatures";
  if (!form.get("addrs") || addrs.length !== players.length) return "update the server: signatures must name it";
  for (let i = 0; i < players.length; i++) {
    if (!/^[0-9a-f]{64}$/.test(keys[i]) || (await sha256hex(keys[i])).slice(0, 32) !== players[i]) return "bad key for " + players[i];
    if (!signedForServer(addrs[i], addr)) return "signed for another server: " + players[i];
    if (!(await ed25519Verify(keys[i], sigs[i], "quark-ticket-v2 " + match + " " + addrs[i]))) return "bad signature for " + players[i];
  }
  if (kind === "match") {
    const last = await env.DB.prepare("SELECT MAX(created) AS t FROM tickets WHERE server = ? AND kind = 'match'").bind(addr).first();
    if (last && last.t && now - last.t < TICKET_GAP_MS) return "too soon";
    // a server runs one ranked match at a time: its older open ticket that never got a result is void (and so is the
    // pending match its players' reports made). One with a result stays: its reports and disputes still count.
    await env.DB.batch([
      env.DB.prepare("UPDATE matches SET status = 'void' WHERE status = 'pending' AND id IN " +
        "(SELECT id FROM tickets WHERE server = ? AND kind = 'match' AND status = 'open' AND result IS NULL)").bind(addr),
      env.DB.prepare("UPDATE tickets SET status = 'void' WHERE server = ? AND kind = 'match' AND status = 'open' AND result IS NULL").bind(addr),
    ]);
  }
  const r = await env.DB.prepare("INSERT OR IGNORE INTO tickets (id, kind, server, ladder, map, players, created, status) VALUES (?, ?, ?, ?, ?, ?, ?, 'open')")
    .bind(match, kind, addr, ladder, map, [...players].sort().join(","), now).run();
  if (!r.meta || r.meta.changes !== 1) return "ticket exists";
  return "ticket " + match;
}

async function raceRun(env, form, addr, now) {
  const ticket = String(form.get("ticket") || ""), map = String(form.get("map") || ""), me = String(form.get("id") || "");
  if (!/^[0-9a-f]{16}$/.test(ticket) || !/^[0-9a-f]{32}$/.test(me)) return "bad request";
  if (!isRaceMap(map)) return "not a ranked race map";
  const t = await env.DB.prepare("SELECT kind, server, map, players, created, status FROM tickets WHERE id = ?").bind(ticket).first();
  if (!t || t.kind !== "race" || t.server !== addr || t.status !== "open" || t.map !== map || t.players !== me) return "no ticket";
  if (now - t.created > RACE_TICKET_MS) return "ticket expired";
  const ms = parseInt(String(form.get("ms") || ""), 10);
  const splits = String(form.get("splits") || "").split(",").map((x) => parseInt(x, 10));
  if (!(ms >= RACE_MIN_MS && ms < 3600000)) return "bad time";
  if (ms > now - t.created + RACE_SLACK_MS) return "bad time"; // longer than the session it was run in
  const floors = RACE_MAPS[map].floors;
  if (!floors || splits.length !== floors.length || splits[splits.length - 1] !== ms) return "bad splits";
  // checkpoint times increase, and no leg is faster than the course allows
  if (splits.some((x, i) => !(x > 0) || x - (i ? splits[i - 1] : 0) < floors[i])) return "bad splits";
  const best = await env.DB.prepare("SELECT ms FROM race_times WHERE map = ? AND id = ?").bind(map, me).first();
  if (best && best.ms <= ms) return `not faster (best ${best.ms})`;
  await env.DB.prepare(
    "INSERT INTO race_times (map, id, ms, splits, version, created) VALUES (?, ?, ?, ?, ?, ?) " +
    "ON CONFLICT(map, id) DO UPDATE SET ms = excluded.ms, splits = excluded.splits, version = excluded.version, created = excluded.created"
  ).bind(map, me, ms, splits.join(","), String(form.get("v") || "").slice(0, 32), now).run();
  await saveName(env, me, String(form.get("name") || ""), now);   // (only with an accepted run)
  return best ? `new best (was ${best.ms})` : "first time";
}

// The race total board: every racer with a run on at least one ranked race map, ranked by the sum over every one of
// their best there or, where they have none, the map's defaultMs (owner, 2026-10-31). Done as the defaults' sum plus
// each racer's (best - default) over the maps they ran, so one grouped scan of race_times (its primary key starts
// with map) answers it. Ties: whoever set their latest counted time first.
export async function raceTotal(env, n) {
  const maps = Object.keys(RACE_MAPS);
  if (!maps.length) return "";
  n = Math.min(100, Math.max(1, n || 25));
  const all = maps.reduce((t, m) => t + RACE_MAPS[m].defaultMs, 0);
  const { results } = await env.DB.prepare(
    "WITH d(map, ms) AS (VALUES " + maps.map(() => "(?, ?)").join(", ") + ") " +
    "SELECT r.id AS id, SUM(r.ms - d.ms) AS gain, COUNT(*) AS played, MAX(r.created) AS last, COALESCE(n.name, 'player') AS name " +
    "FROM race_times r JOIN d ON d.map = r.map LEFT JOIN names n ON n.id = r.id GROUP BY r.id ORDER BY gain ASC, last ASC LIMIT ?"
  ).bind(...maps.flatMap((m) => [m, RACE_MAPS[m].defaultMs]), n).all();
  return results.map((r) => `${all + r.gain} ${r.played} ${r.name}`).join("\n");
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

// Discord link, verified: the bot hands a Discord user (who ran /link there) a one-time code; the code typed in
// game links that player. Wrong codes are rate-limited per IP (see "linkfail").
async function linkCode(env, form, now) {
  if (!keyEq(form.get("bot"), env.BOT_KEY)) return "bad request";
  const discord = hexText(String(form.get("discord") || ""), 40).trim();
  if (!discord) return "bad request";
  await env.DB.prepare("DELETE FROM link_codes WHERE discord = ? OR created < ?").bind(discord, now - LINK_CODE_MS).run();
  for (let k = 0; k < 5; k++) {
    const n = crypto.getRandomValues(new Uint32Array(1))[0] % 1000000;
    const code = String(n).padStart(6, "0");
    const r = await env.DB.prepare("INSERT OR IGNORE INTO link_codes (code, discord, created) VALUES (?, ?, ?)").bind(code, discord, now).run();
    if (r.meta && r.meta.changes === 1) return "code " + code;
  }
  return "busy";
}

async function link(env, form, now, conn) {
  const secret = String(form.get("secret") || "");
  if (!/^[0-9a-f]{64}$/.test(secret)) return "bad request";
  const me = await playerOf(env, secret, now);
  await saveName(env, me, String(form.get("name") || ""), now);
  if (String(form.get("unlink") || "") === "1") { await env.DB.prepare("DELETE FROM links WHERE id = ?").bind(me).run(); return "unlinked"; }
  const code = String(form.get("code") || "");
  if (!/^\d{6}$/.test(code)) return form.get("discord") ? "update the game: link with /link in Discord" : "bad request";
  if (!allow("linkfail", conn, now)) return "rate limited";
  const row = await env.DB.prepare("SELECT discord, created FROM link_codes WHERE code = ?").bind(code).first();
  if (!row || now - row.created > LINK_CODE_MS) return "wrong or expired code";
  refund("linkfail", conn);
  await env.DB.prepare("DELETE FROM link_codes WHERE code = ?").bind(code).run();
  await env.DB.prepare("INSERT INTO links (id, discord, updated, verified) VALUES (?, ?, ?, 1) " +
    "ON CONFLICT(id) DO UPDATE SET discord = excluded.discord, updated = excluded.updated, verified = 1")
    .bind(me, row.discord, now).run();
  return "linked " + row.discord;
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
  const me = await playerOf(env, secret, now);
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
// FPS benchmark runs (game 1.20+: main menu > BENCHMARK, src/client/benchmark.cpp): one row a finished run, from the
// player's own game with its secret. Every text field comes hex-encoded (printable ASCII only), every number is
// checked and clamped, and one player stores at most BENCH_PER_DAY a rolling day. GET /bench: the 25 newest runs.
const BENCH_PER_DAY = 30;
const BENCH_TEXT = { version: 32, build: 32, cpu: 96, gpu: 96, driver: 64, res: 24, scale: 32, aa: 32, vsync: 32, fpscap: 16, demo: 32 };
function benchText(h, max) {
  h = String(h || "");
  if (!/^([0-9a-f]{2})*$/.test(h) || h.length > max * 2) return null;
  const t = h ? h.match(/../g).map((x) => String.fromCharCode(parseInt(x, 16))).join("") : "";
  return /^[\x20-\x7e]*$/.test(t) ? t.trim() : null;
}
async function benchReport(env, form, now) {
  const secret = String(form.get("secret") || "");
  if (!/^[0-9a-f]{64}$/.test(secret)) return "bad request";
  const t = {};
  for (const [k, max] of Object.entries(BENCH_TEXT)) { t[k] = benchText(form.get(k), max); if (t[k] === null) return "bad " + k; }
  if (!/^\d+\.\d+\.\d+(\.\d+|\.testing\.\d+|-[0-9A-Za-z.]+)?$/.test(t.version)) return "bad version";
  const n = {};
  for (const [k, lo, hi] of [["avg", 0, 100000], ["low1", 0, 100000], ["low01", 0, 100000], ["min", 0, 100000], ["stdev", 0, 10000], ["seconds", 0, 86400]]) {
    const v = parseFloat(String(form.get(k) || ""));
    if (!Number.isFinite(v)) return "bad " + k;
    n[k] = Math.round(Math.min(hi, Math.max(lo, v)) * 1000) / 1000;
  }
  const frames = parseInt(String(form.get("frames") || ""), 10);
  if (!(frames > 0 && frames <= 10000000)) return "bad frames";
  if (!(n.min <= n.low01 + 0.001 && n.low01 <= n.low1 + 0.001)) return "bad numbers";   // (min <= 0.1% low <= 1% low)
  const channel = String(form.get("channel") || "") === "testing" || isTestingVersion(t.version) ? "testing" : "production";
  const me = await playerOf(env, secret, now);
  await saveName(env, me, String(form.get("name") || ""), now);
  const day = await env.DB.prepare("SELECT COUNT(*) AS c FROM bench_runs WHERE player = ? AND created > ?").bind(me, now - 86400000).first();
  if (day && day.c >= BENCH_PER_DAY) return "daily limit";
  await env.DB.prepare("INSERT INTO bench_runs (player, created, version, channel, build, cpu, gpu, driver, avg_fps, low1_fps, low01_fps, min_fps, stdev_ms, frames, seconds, res, scale, aa, vsync, fpscap, demo) " +
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
    .bind(me, now, t.version, channel, t.build, t.cpu, t.gpu, t.driver, n.avg, n.low1, n.low01, n.min, n.stdev, frames, n.seconds, t.res, t.scale, t.aa, t.vsync, t.fpscap, t.demo).run();
  return "ok";
}
// "avg low1 low01 min stdev version channel build cpuhex gpuhex namehex" lines, newest first
async function benchGet(env) {
  const { results } = await env.DB.prepare("SELECT b.*, COALESCE(n.name, 'player') AS name FROM bench_runs b LEFT JOIN names n ON n.id = b.player ORDER BY b.created DESC LIMIT 25").all();
  return results.map((r) => `${r.avg_fps} ${r.low1_fps} ${r.low01_fps} ${r.min_fps} ${r.stdev_ms} ${r.version} ${r.channel} ${textHex(r.build)} ${textHex(r.cpu)} ${textHex(r.gpu)} ${textHex(r.name)}`).join("\n");
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
      "JOIN accounts a ON a.id = substr(p.id, ?) AND a.tgames >= ? " +
      "LEFT JOIN names n ON n.id = substr(p.id, ?) LEFT JOIN links l ON l.id = substr(p.id, ?) AND l.verified = 1 " +
      "WHERE p.id >= ? AND p.id < ? AND p.games >= ? ORDER BY (p.mu - 3 * p.sigma) DESC LIMIT 10"
    ).bind(ladder.length + 2, ladder.length + 2, PROVISIONAL_GAMES, ladder.length + 2, ladder.length + 2, ladder + ":", ladder + ";", PLACEMENT_GAMES).all();
    results.forEach((r, i) => out.push(`${ladder} ${i + 1} ${r.id} ${textHex(r.name)} ${r.discord === "-" ? "-" : textHex(r.discord)}`));
  }
  for (const map of Object.keys(RACE_MAPS)) {
    const { results } = await env.DB.prepare(
      "SELECT r.id AS id, COALESCE(n.name, 'player') AS name, COALESCE(l.discord, '-') AS discord FROM race_times r " +
      "LEFT JOIN names n ON n.id = r.id LEFT JOIN links l ON l.id = r.id AND l.verified = 1 WHERE r.map = ? ORDER BY r.ms ASC, r.created ASC LIMIT 10"
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
    who = await playerOf(env, secret, now);
    await saveName(env, who, String(form.get("name") || ""), now);
    const l = await env.DB.prepare("SELECT discord FROM links WHERE id = ? AND verified = 1").bind(who).first();
    if (l) discord = l.discord;
  } else if (keyEq(form.get("bot"), env.BOT_KEY)) {
    discord = hexText(String(form.get("discord") || ""), 40).trim();
    if (!discord) return "bad request";
    who = "d:" + discord;
  } else return "bad request";
  const name = hexText(String(form.get("name") || ""), 20) || discord || "player";
  if (action === "leaveall") { await env.DB.prepare("DELETE FROM queue WHERE who = ?").bind(who).run(); return "left all"; }
  if (!(mode in QUEUE_SIZES)) return "not a queue";
  if (action === "leave") { await env.DB.prepare("DELETE FROM queue WHERE mode = ? AND who = ?").bind(mode, who).run(); return "left " + mode; }
  await env.DB.prepare("INSERT OR IGNORE INTO queue (mode, who, name, discord, joined) VALUES (?, ?, ?, ?, ?)").bind(mode, who, name, discord, now).run();
  const { results } = await env.DB.prepare("SELECT who, name, discord FROM queue WHERE mode = ? AND joined >= ? ORDER BY joined ASC").bind(mode, now - QUEUE_STALE_MS).all();
  const size = QUEUE_SIZES[mode];
  if (results.length < size) return `queued ${mode} ${results.length}/${size}`;
  const take = results.slice(0, size);
  const members = take.map((r) => `${r.who}:${textHex(r.name)}:${r.discord ? textHex(r.discord) : "-"}`).join(",");
  // atomic (WEB-8): the guard fails the batch unless every one of them is still waiting in this queue, so two joins
  // racing here fill it once
  const takers = take.map((r) => r.who);
  try {
    await env.DB.batch([
      env.DB.prepare(`INSERT INTO rate_guard (ok) VALUES ((SELECT COUNT(*) FROM queue WHERE mode = ? AND who IN (${takers.map(() => "?").join(",")})) = ?)`)
        .bind(mode, ...takers, takers.length),
      env.DB.prepare("DELETE FROM rate_guard"),
      env.DB.prepare("INSERT INTO filled (mode, members, created) VALUES (?, ?, ?)").bind(mode, members, now),
      env.DB.prepare(`DELETE FROM queue WHERE who IN (${takers.map(() => "?").join(",")})`).bind(...takers),
    ]);
  } catch (e) {
    if (String(e && e.message || e).includes("CHECK")) return `queued ${mode}`;   // someone else's join filled it
    throw e;
  }
  return `filled ${mode}`;
}

async function sha256hex(s) {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// Plackett-Luce update (openskill.js). teams: [{ players: [{mu, sigma}], rank }] (lower rank = better, ties allowed).
export function rateTeams(teams) {
  if (!teams.every((t) => t.players.every((p) => isFinite(p.mu) && isFinite(p.sigma) && p.sigma > 0))) return; // (never write NaN)
  const saved = teams.map((t) => t.players.map((p) => ({ mu: p.mu, sigma: p.sigma })));
  rateTeamsInner(teams);
  if (!teams.every((t) => t.players.every((p) => isFinite(p.mu) && isFinite(p.sigma))))
    teams.forEach((t, i) => t.players.forEach((p, j) => Object.assign(p, saved[i][j])));
}
function rateTeamsInner(teams) {
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
    `SELECT id, mu, sigma, games, ver FROM players WHERE id IN (${ids.map(() => "?").join(",")})`
  ).bind(...ids.map((id) => ladder + ":" + id)).all();
  for (const r of results) {
    const id = r.id.slice(ladder.length + 1);
    map.set(id, { id, mu: r.mu, sigma: r.sigma, games: r.games, ver: r.ver });
  }
  return map;
}

async function report(env, form, now) {
  const secret = String(form.get("secret") || ""), match = String(form.get("match") || ""), ladder = String(form.get("ladder") || "");
  if (!/^[0-9a-f]{64}$/.test(secret) || !/^[0-9a-f]{16,32}$/.test(match)) return "bad request";
  if (!LADDERS.has(ladder)) return "not a ranked ladder";
  const res = parseResult(form.get("p"));
  if (!res) return "bad result";
  const me = await playerOf(env, secret, now);
  if (!res.entries.some((e) => e.id === me)) return "not in this match";
  const t = await env.DB.prepare("SELECT kind, server, ladder, players, created, status FROM tickets WHERE id = ?").bind(match).first();
  if (!t || t.kind !== "match") return "no ticket";
  // (a ticket a testing server opened before this check existed: its matches aren't rated either)
  const srv = await env.DB.prepare("SELECT version FROM servers WHERE addr = ?").bind(t.server).first();
  if (srv && isTestingVersion(srv.version)) return "testing server: ranked not rated";
  if (t.ladder !== ladder || t.players !== res.entries.map((e) => e.id).join(",")) return "not the ticket's players";
  // (no "too soon" here: the server's result decides when the match can have ended - MIN_MATCH_MS, sooner only when
  // someone left - and a report alone rates nothing. The game sends its report once, so an early one is kept.)
  if (t.status === "open" && now - t.created > TICKET_MS) return "expired";
  if (t.status === "void") return "void";   // (before the matches row below: it would only sit there pending)
  // the reporter's display name for the leaderboard (hex-encoded printable ASCII, up to 20 chars)
  await saveName(env, me, String(form.get("name") || ""), now);
  const result = ladder + "|" + res.text, dispute = String(form.get("dispute") || "") === "1" ? 1 : 0;
  await env.DB.prepare("INSERT OR IGNORE INTO matches (id, result, players, status, created) VALUES (?, ?, ?, 'pending', ?)")
    .bind(match, result, res.entries.length, now).run();
  const m = await env.DB.prepare("SELECT status FROM matches WHERE id = ?").bind(match).first();
  if (m.status !== "pending" && m.status !== "disputed") return m.status;   // (a dispute can still turn void)
  await env.DB.prepare("INSERT OR IGNORE INTO reports (match, reporter, result, dispute) VALUES (?, ?, ?, ?)").bind(match, me, result, dispute).run();
  return finishMatch(env, match, now);
}

async function voidMatch(env, match) {
  await env.DB.batch([
    env.DB.prepare("UPDATE matches SET status = 'void' WHERE id = ?").bind(match),
    env.DB.prepare("UPDATE tickets SET status = 'void' WHERE id = ?").bind(match),
  ]);
}

// The server's own result for its ticket.
async function serverResult(env, form, addr, now) {
  const match = String(form.get("match") || "");
  if (!/^[0-9a-f]{16}$/.test(match)) return "bad request";
  const res = parseResult(form.get("p"));
  if (!res) return "bad result";
  const t = await env.DB.prepare("SELECT kind, server, ladder, players, created, status, result FROM tickets WHERE id = ?").bind(match).first();
  if (!t || t.kind !== "match" || t.server !== addr) return "no ticket";
  if (t.status !== "open") return t.status;
  if (t.players !== res.entries.map((e) => e.id).join(",")) { await voidMatch(env, match); return "void: not the ticket's players"; }
  const left = String(form.get("left") || "") ? String(form.get("left")).split(",") : [];
  const ids = res.entries.map((e) => e.id);
  if (left.some((id) => !ids.includes(id)) || new Set(left).size !== left.length || left.length >= ids.length) return "bad left";
  // no ranked match is over in under a minute, unless someone left it: leaving early doesn't escape the loss
  if ((now - t.created < MIN_MATCH_MS && !left.length) || now - t.created > TICKET_MS) { await voidMatch(env, match); return "void: outside the ticket's window"; }
  if (t.result) return "already reported";
  const result = t.ladder + "|" + res.text;
  await env.DB.batch([
    env.DB.prepare("UPDATE tickets SET result = ?, leavers = ?, resulted = ? WHERE id = ? AND result IS NULL").bind(result, left.join(","), now, match),
    env.DB.prepare("INSERT OR IGNORE INTO matches (id, result, players, status, created) VALUES (?, ?, ?, 'pending', ?)").bind(match, result, ids.length, now),
  ]);
  return finishMatch(env, match, now);
}

// What a ticketed match comes to, from the server's result and the players' reports (see /report at the top).
// Once the ticket is TICKET_MS old no report or result can come any more, so nothing stays pending: it's rated if
// the server's result is in, someone agreed and nobody objected, else void. A dispute is final: logged, never rated.
export function decideMatch(t, reports, now) {
  const players = t.players.split(","), left = new Set(t.leavers ? t.leavers.split(",") : []);
  const expired = now - t.created > TICKET_MS;
  if (!t.result) {
    if (expired) return { state: "void", text: "void: no result from the server" };
    return { state: "pending", text: reports.length < players.length ? `pending ${reports.length}/${players.length}` : "pending server" };
  }
  const byId = new Map(reports.map((r) => [r.reporter, r]));
  const against = reports.filter((r) => r.dispute || (r.result !== null && r.result !== t.result));
  const agree = reports.filter((r) => !r.dispute && (r.result === null || r.result === t.result));
  if (against.length) return { state: "disputed", text: "disputed" };   // (owner: "just logged and unrated")
  const missing = players.filter((id) => !left.has(id) && !byId.has(id));
  if (!missing.length) return { state: "rate" };
  if (agree.length >= 1 && (expired || (t.resulted && now - t.resulted >= REPORT_WAIT_MS))) return { state: "rate" };
  if (expired) return { state: "void", text: "void: no player confirmed the server's result" };
  return { state: "pending", text: `pending ${players.length - left.size - missing.length}/${players.length - left.size}` };
}

async function finishMatch(env, match, now) {
  const t = await env.DB.prepare("SELECT ladder, players, created, result, leavers, resulted, status FROM tickets WHERE id = ?").bind(match).first();
  const m = await env.DB.prepare("SELECT status FROM matches WHERE id = ?").bind(match).first();
  if (!t || !m) return "pending";
  if (m.status !== "pending" && m.status !== "disputed") return m.status;
  if (t.status === "void" && m.status === "pending") { await voidMatch(env, match); return "void"; }   // (its ticket went first)
  const { results: reports } = await env.DB.prepare("SELECT reporter, result, dispute FROM reports WHERE match = ?").bind(match).all();
  const d = decideMatch(t, reports, now);
  if (m.status === "disputed") return "disputed";   // final: logged, never rated
  if (d.state === "void") { await voidMatch(env, match); return d.text; }
  if (d.state === "disputed") {
    await env.DB.prepare("UPDATE matches SET status = 'disputed' WHERE id = ? AND status = 'pending'").bind(match).run();
    return "disputed";
  }
  if (d.state !== "rate") return d.text;
  for (let attempt = 0; attempt < 3; attempt++) {
    const r = await rateMatch(env, match, t, now, m.status);
    if (r !== "retry") return r;
  }
  return "busy: try again";
}

// One atomic batch (a D1 batch is a transaction): a guard row checks that the match is still unrated and that no
// rating changed since we read it (version column), so two matches finishing at once can't overwrite each other
// (WEB-7) and a match is rated once. The guard's CHECK fails the whole batch otherwise.
async function rateMatch(env, match, t, now, fromStatus) {
  const ladder = t.ladder;
  const res = parseResult(t.result.slice(ladder.length + 1));
  const ids = res.entries.map((e) => e.id);
  const ratings = await loadRatings(env, ladder, ids);
  const { results: acc } = await env.DB.prepare(`SELECT id, tgames FROM accounts WHERE id IN (${ids.map(() => "?").join(",")})`).bind(...ids).all();
  const tgames = new Map(acc.map((r) => [r.id, r.tgames]));
  const established = (id) => (tgames.get(id) || 0) >= PROVISIONAL_GAMES;
  const before = new Map([...ratings.values()].map((r) => [r.id, { mu: r.mu, sigma: r.sigma }]));
  const byTeam = new Map();
  for (const e of res.entries) {
    if (!byTeam.has(e.team)) byTeam.set(e.team, { players: [], rank: e.place });
    byTeam.get(e.team).players.push(ratings.get(e.id));
  }
  rateTeams([...byTeam.values()]);
  const counted = new Set();
  for (const e of res.entries) {
    // an established player only moves against established opponents (fresh accounts can't be farmed)
    const vsProvisional = res.entries.some((o) => o.team !== e.team && !established(o.id));
    if (established(e.id) && vsProvisional) Object.assign(ratings.get(e.id), before.get(e.id));
    else counted.add(e.id);
  }
  const q = (sql, ...a) => env.DB.prepare(sql).bind(...a);
  const rows = [...ratings.values()];
  const terms = ["(SELECT COUNT(*) FROM matches WHERE id = ? AND status = ?)"], args = [match, fromStatus];
  for (const r of rows) {
    if (r.ver === undefined) { terms.push("(SELECT COUNT(*) = 0 FROM players WHERE id = ?)"); args.push(ladder + ":" + r.id); }
    else { terms.push("(SELECT COUNT(*) FROM players WHERE id = ? AND ver = ?)"); args.push(ladder + ":" + r.id, r.ver); }
  }
  try {
    await env.DB.batch([
      q(`INSERT INTO rate_guard (ok) VALUES ((${terms.join(" + ")}) = ?)`, ...args, terms.length),
      q("DELETE FROM rate_guard"),
      q("UPDATE matches SET status = 'done' WHERE id = ?", match),
      q("UPDATE tickets SET status = 'done' WHERE id = ?", match),
      ...rows.map((r) => q(
        "INSERT INTO players (id, mu, sigma, games, updated, ver) VALUES (?, ?, ?, ?, ?, 1) " +
        "ON CONFLICT(id) DO UPDATE SET mu = excluded.mu, sigma = excluded.sigma, games = excluded.games, updated = excluded.updated, ver = players.ver + 1",
        ladder + ":" + r.id, r.mu, r.sigma, r.games + (counted.has(r.id) ? 1 : 0), now)),
      ...ids.map((id) => q("INSERT INTO accounts (id, tgames, created) VALUES (?, 1, ?) ON CONFLICT(id) DO UPDATE SET tgames = tgames + 1", id, now)),
    ]);
  } catch (e) {
    const st = await env.DB.prepare("SELECT status FROM matches WHERE id = ?").bind(match).first();
    if (st && st.status === "done") return "done";
    if (String(e && e.message || e).includes("CHECK")) return "retry";   // a rating moved meanwhile: read it again
    throw e;
  }
  return "done";
}

function ipv4(s) {
  if (!s || !/^\d{1,3}(\.\d{1,3}){3}$/.test(s)) return null;
  return s.split(".").every((p) => +p <= 255) ? s : null;
}

// Old rows go (cron, WEB-12), and pending matches that can be decided are: rated once their report wait is over (with
// a report in: without one it would only stay pending), and rated or voided once their ticket has expired (see
// decideMatch). Oldest first, so the same rows can't fill every run; one failing match doesn't stop the rest.
// Returns how many it rated.
export async function prune(env, now) {
  const q = (sql, ...a) => env.DB.prepare(sql).bind(...a);
  const { results: due } = await q(
    "SELECT t.id AS id FROM tickets t JOIN matches m ON m.id = t.id WHERE m.status = 'pending' AND (t.created < ? OR " +
    "(t.resulted IS NOT NULL AND t.resulted < ? AND EXISTS (SELECT 1 FROM reports r WHERE r.match = t.id))) " +
    "ORDER BY COALESCE(t.resulted, t.created) LIMIT 50", now - TICKET_MS, now - REPORT_WAIT_MS).all();
  let rated = 0;
  for (const r of due) {
    try { if ((await finishMatch(env, r.id, now)) === "done") rated++; }
    catch (e) { console.log("prune: match " + r.id + ": " + String(e && e.message || e)); }
  }
  await env.DB.batch([
    q("DELETE FROM servers WHERE seen < ?", now - 3600000),
    // tickets that never got a result: void, and so is the pending match their players' reports made
    q("UPDATE matches SET status = 'void' WHERE status = 'pending' AND id IN (SELECT id FROM tickets WHERE status = 'open' AND created < ? AND result IS NULL)", now - TICKET_MS),
    q("UPDATE tickets SET status = 'void' WHERE status = 'open' AND created < ? AND result IS NULL", now - TICKET_MS),
    q("DELETE FROM reports WHERE match IN (SELECT id FROM tickets WHERE created < ? AND status != 'open')", now - 30 * 86400000),
    q("DELETE FROM matches WHERE created < ? AND status != 'pending' AND status != 'disputed'", now - 30 * 86400000),
    q("DELETE FROM tickets WHERE created < ? AND status != 'open' AND id NOT IN (SELECT id FROM matches WHERE status = 'disputed')", now - 30 * 86400000),
    q("DELETE FROM queue WHERE joined < ?", now - QUEUE_STALE_MS),
    q("DELETE FROM filled WHERE created < ?", now - 86400000),
    q("DELETE FROM link_codes WHERE created < ?", now - LINK_CODE_MS),
    q("DELETE FROM rate_guard"),
  ]);
  return rated;
}

// Short-lived caching of public reads (WEB-6): the Cache API (per data center) plus Cache-Control for clients.
async function cached(req, u, ctx, make) {
  const ttl = CACHE_S[u.pathname];
  const cache = typeof caches !== "undefined" && ttl ? caches.default : null;
  const key = cache ? new Request(u.toString(), { method: "GET" }) : null;
  if (cache) { const hit = await cache.match(key); if (hit) return hit; }
  const res = await make();
  if (cache && res.status === 200) {
    const out = new Response(res.body, res);
    out.headers.set("cache-control", "public, max-age=" + ttl);
    if (ctx && ctx.waitUntil) ctx.waitUntil(cache.put(key, out.clone())); else await cache.put(key, out.clone());
    return out;
  }
  return res;
}

// (/disputes only reads, but it carries ADMIN_KEY: a POST like the writes, the key in the body, no CORS)
const WRITES = new Set(["/announce", "/remove", "/hello", "/number", "/report", "/racetime", "/lifetime", "/bench", "/queue", "/link", "/linkcode", "/disputes"]);
const SERVER_WRITES = new Set(["/ticket", "/result", "/racerun"]);

export default {
  async fetch(req, env, ctx) {
    const u = new URL(req.url);
    const q = (k) => u.searchParams.get(k) || "";
    const now = Date.now();
    const conn = req.headers.get("CF-Connecting-IP") || "";
    const srcIp = ipv4(conn); // the caller's own IPv4, if it came over IPv4 (can't be spoofed over TCP)
    const both = u.pathname === "/queue" || u.pathname === "/lifetime" || u.pathname === "/bench";   // (GET reads, POST writes)
    const isWrite = WRITES.has(u.pathname) && !both || SERVER_WRITES.has(u.pathname) || (req.method === "POST" && both);
    // reads may be fetched by web pages (the website's leaderboard); writes never answer cross-origin
    const text = (body, status = 200) => new Response(body, { status, headers: isWrite ? { "content-type": "text/plain" } :
      { "content-type": "text/plain", "access-control-allow-origin": "*" } });
    let form = null;
    if (isWrite) {
      if (req.method !== "POST") return text("POST only", 405);
      // a browser always says where a cross-site POST comes from; the game, servers and the bot never send Origin
      if (req.headers.get("Origin")) return text("forbidden: no cross-site writes", 403);
      if (+(req.headers.get("content-length") || 0) > MAX_BODY) return text("too big", 413);   // (early out; readForm counts)
      try { form = await readForm(req, MAX_BODY); } catch { return text("bad request", 400); }
      if (!form) return text("too big", 413);
    }
    // the bot's writes: their own bucket, none of its address's
    const isBot = !!(form && keyEq(form.get("bot"), env.BOT_KEY));
    if (!(isBot ? allow("bot", "bot", now) : allow("ip", conn, now))) return text("rate limited", 429);
    if (isWrite && !isBot && !SERVER_WRITES.has(u.pathname) && u.pathname !== "/announce" && u.pathname !== "/remove") {
      if (!allow("write", conn, now)) return text("rate limited", 429);
      const pid = await idOf(String(form.get("secret") || ""));
      if (pid && !allow("id", pid, now)) return text("rate limited", 429);
    }
    if (SERVER_WRITES.has(u.pathname)) {
      const auth = await serverAuth(env, form, srcIp, now);
      if (!auth) return text("forbidden: announce first, and send the token", 403);
      const addr = auth.addr;
      if (!allow("server", addr, now)) return text("rate limited", 429);
      if (isTestingVersion(auth.version)) return text("testing server: ranked not rated", 403);
      if (u.pathname === "/ticket") return text(await openTicket(env, form, addr, now));
      if (u.pathname === "/result") return text(await serverResult(env, form, addr, now));
      return text(await raceRun(env, form, addr, now));
    }

    if (u.pathname === "/announce" || u.pathname === "/remove") {
      // Which address: the source IPv4, or ip= from an IPv6 path. Allowed only for the source address itself
      // or with the token an earlier announce from that address was given.
      const f = (k) => String(form.get(k) || "");
      const port = parseInt(f("port"), 10);
      const ip = srcIp || ipv4(f("ip"));
      if (!ip || !(port > 0 && port < 65536)) return text("bad request", 400);
      const addr = ip + ":" + port, token = f("token");
      const row = await env.DB.prepare("SELECT token FROM servers WHERE addr = ?").bind(addr).first();
      const ownSource = srcIp === ip;
      const tokenOk = !!(row && row.token && /^[0-9a-f]{32}$/.test(token) && keyEq(token, row.token));
      if (!ownSource && !tokenOk) return text("forbidden: announce once over IPv4 to get a token", 403);
      if (u.pathname === "/remove") {
        await env.DB.prepare("DELETE FROM servers WHERE addr = ?").bind(addr).run();
        return text("removed");
      }
      if (!row) {   // a new entry: one address can't fill the list (WEB-5)
        const n = await env.DB.prepare("SELECT COUNT(*) AS n FROM servers WHERE addr >= ? AND addr < ? AND seen > ?").bind(ip + ":", ip + ";", now - FRESH_MS).first();
        if (n && n.n >= MAX_SERVERS_PER_IP) return text("too many servers from this address", 429);
      }
      const newToken = tokenOk ? token : randomToken();
      await env.DB.prepare(
        "INSERT INTO servers (addr, version, seen, token) VALUES (?, ?, ?, ?) " +
        "ON CONFLICT(addr) DO UPDATE SET version = excluded.version, seen = excluded.seen, token = excluded.token"
      ).bind(addr, (f("hidden") === "1" ? "~" : "") + f("v").slice(0, 32), now, newToken).run(); // "~": never matches /list (32: "1.19.0.testing.1234" is 19)
      return text("ok " + newToken);
    }
    if (u.pathname === "/hello") {
      const me = await playerOf(env, String(form.get("secret") || ""), now);
      if (!me) return text("bad request", 400);
      await saveName(env, me, String(form.get("name") || ""), now);
      return text("id " + me);
    }
    if (u.pathname === "/number") {
      const me = await playerOf(env, String(form.get("secret") || ""), now);
      if (!me) return text("bad request", 400);
      const n = await playerNumber(env, me, now);
      return n ? text("num " + n) : text("no numbers yet", 503);
    }
    if (u.pathname === "/report") return text(await report(env, form, now));
    if (u.pathname === "/racetime") return text("update the game: race times are sent by the game server");
    if (u.pathname === "/link") return text(await link(env, form, now, conn));
    if (u.pathname === "/linkcode") return text(await linkCode(env, form, now));
    if (u.pathname === "/lifetime" && isWrite) return text(await lifetimeReport(env, form, now));
    if (u.pathname === "/bench" && isWrite) return text(await benchReport(env, form, now));
    if (u.pathname === "/queue" && isWrite) return text(await queue(env, form, now));
    if (u.pathname === "/disputes") {
      if (!keyEq(form.get("key"), env.ADMIN_KEY)) return text("forbidden", 403);
      const { results } = await env.DB.prepare("SELECT t.id, t.ladder, t.created, t.result, t.leavers FROM tickets t JOIN matches m ON m.id = t.id WHERE m.status = 'disputed' ORDER BY t.created LIMIT 100").all();
      return text(results.map((r) => `${r.id} ${r.ladder} ${r.created} ${r.result}${r.leavers ? " left=" + r.leavers : ""}`).join("\n"));
    }
    if (req.method !== "GET") return text("bad request", 405);
    return cached(req, u, ctx, async () => {
      if (u.pathname === "/list") {
        const { results } = await env.DB.prepare(
          "SELECT addr FROM servers WHERE seen > ? AND version = ? ORDER BY seen DESC LIMIT 400"
        ).bind(now - FRESH_MS, q("v").slice(0, 32)).all();
        const per = new Map();   // (also capped here: entries from before the per-address limit)
        const out = results.filter((r) => { const ip = r.addr.split(":")[0]; per.set(ip, (per.get(ip) || 0) + 1); return per.get(ip) <= MAX_SERVERS_PER_IP; });
        return text(out.slice(0, 200).map((r) => r.addr).join("\n"));
      }
      if (u.pathname === "/leaderboard") {
        const ladder = q("ladder");
        if (!LADDERS.has(ladder)) return text("not a ranked ladder", 400);
        const n = Math.min(100, Math.max(1, parseInt(q("n"), 10) || 25));
        const { results } = await env.DB.prepare(
          "SELECT p.mu, p.sigma, p.games, COALESCE(n.name, 'player') AS name FROM players p " +
          "JOIN accounts a ON a.id = substr(p.id, ?) AND a.tgames >= ? " +
          "LEFT JOIN names n ON n.id = substr(p.id, ?) WHERE p.id >= ? AND p.id < ? AND p.games >= ? " +
          "ORDER BY (p.mu - 3 * p.sigma) DESC LIMIT ?"
        ).bind(ladder.length + 2, PROVISIONAL_GAMES, ladder.length + 2, ladder + ":", ladder + ";", PLACEMENT_GAMES, n).all();
        return text(results.map((r) => `${Math.round(Math.max(0, r.mu - 3 * r.sigma) * 40)} ${r.games} ${r.name}`).join("\n"));
      }
      if (u.pathname === "/racetotal") return text(await raceTotal(env, parseInt(q("n"), 10)));
      if (u.pathname === "/raceboard") {
        if (!isRaceMap(q("map"))) return text("not a ranked race map", 400);
        const n = Math.min(100, Math.max(1, parseInt(q("n"), 10) || 25));
        const { results } = await env.DB.prepare(
          "SELECT r.ms, COALESCE(n.name, 'player') AS name FROM race_times r LEFT JOIN names n ON n.id = r.id " +
          "WHERE r.map = ? ORDER BY r.ms ASC, r.created ASC LIMIT ?"
        ).bind(q("map"), n).all();
        return text(results.map((r) => `${r.ms} ${r.name}`).join("\n"));
      }
      if (u.pathname === "/crowns") return text(await crowns(env));
      if (u.pathname === "/lifetime") return text(await lifetimeGet(env, q("ids")));
      if (u.pathname === "/bench") return text(await benchGet(env));
      if (u.pathname === "/queue") {   // (read only: stale entries are pruned by the cron, not here - WEB-6)
        const { results } = await env.DB.prepare("SELECT mode, COUNT(*) AS n FROM queue WHERE joined >= ? GROUP BY mode").bind(now - QUEUE_STALE_MS).all();
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
    });
  },
  async scheduled(event, env, ctx) {
    ctx.waitUntil(prune(env, Date.now()));
  },
};
