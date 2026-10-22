// quark internet server list: a Cloudflare Worker with a D1 database bound as DB.
// Setup steps: tools/master/README.md.
// Every write is a POST (application/x-www-form-urlencoded) and is refused when it carries an Origin header (a web
// page making its visitors' browsers write here); only GET reads answer with CORS "*". GET reads are cached briefly.
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
// Match tickets (servers, with their announce token; form also carries port, token[, ip]):
//   POST /ticket  form: kind=match, match (16 hex), ladder, map, players=id,..., keys=pub,..., sigs=sig,...
//                 when a ranked match goes live; each player's key must hash to their id and have signed
//                 "quark-ticket-v1 <match>" (their game signs it in the match), so a server can only ticket players
//                 who are really in it.
//                 form: kind=race, match (16 hex), map, players=<racer id>, keys=pub, sigs=sig   when a time trial
//                 starts; the racer's game signs "quark-ticket-v1 <match>" the same way, so a server can only post runs
//                 (and the name that goes with them) for a player who is really on it.
//                 answers "ticket <match>". One open match ticket per server (a new one voids the last if that has
//                 no result yet); a server opens at most one a minute.
//   POST /result  form: match, p=id:team:place,...[, left=id,...]   the server's own account of the ticketed match;
//                 left: players who left during it (first leaver first; their game can't report)
//   POST /racerun form: ticket, map, id, ms, splits, name (hex), v   a finished run the server saw, under a race ticket
//                 (the name is saved only with a run that's kept)
// Ranked (OpenSkill / Weng-Lin Plackett-Luce ratings):
//   POST /report  form: secret, match, ladder, p=id:team:place,...[, dispute=1]   one player's account of a finished
//                 match (dispute=1: their game saw a different result than the host's p)
//                 Accepted only under a ticket from that ticket's players, once each, MIN_MATCH_MS to TICKET_MS after
//                 it opened. Rated when the server's result and the players' reports agree: every player who didn't
//                 leave reported it, or REPORT_WAIT_MS passed with at least one agreeing and nobody against. A report
//                 against it makes a dispute (not rated; the owner settles it with /resolve), unless no player agrees
//                 with the server and two or more contradict it: then it's void. A match still undecided when its
//                 ticket expires (TICKET_MS) is rated if the server's result is in and a player agreed with nobody
//                 against, else void. New accounts are provisional (see
//                 PROVISIONAL_GAMES).
//   POST /resolve form: key (ADMIN_KEY), match, action=rate|void                    the owner settles a dispute
//   GET  /disputes?key=ADMIN_KEY                                    "match ladder created result" per open dispute
//   GET  /ratings?ladder=L&ids=a,b,c                                 "id mu sigma games" per line (unknown ids: defaults)
//   GET  /leaderboard?ladder=L[&n=25]                                "rating games name" per line, best first (placed only)
// Race time trials (single player, Viaduct): runs come from the game server (/racerun above), kept only if they
//   beat that player's best and no leg is faster than RACE_FLOORS allows. POST /racetime (the old self-report from
//   the racer's own game) is refused. Odd runs can still be deleted by hand:
//   DELETE FROM race_times WHERE map = 'viaduct' AND id = '<id>' (ids: SELECT ... JOIN names).
//   GET  /raceboard?map=M[&n=25]                                      "ms name" per line, fastest first
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
const PENDING_MS = 3600000;
const LADDERS = new Set(["vanilla", "vampire", "instagib"]);
const RACE_MAPS = new Set(["viaduct"]);
const RACE_MIN_MS = 5000; // nothing finishes faster: below this it's not a real run
// Fastest plausible time per leg (start -> checkpoint 1, then between checkpoints), in ms: the straight-line gap
// between the boxes at 48 m/s. Made by `build/quark_tests racefloors` (quark_tests checks this copy matches).
const RACE_FLOORS = {
  viaduct: [792, 606, 485, 625, 250, 928, 1333, 341, 1818, 979, 1022, 1583],
};
const TICKET_MS = 2 * 3600000;       // a match ticket takes reports for this long
const MIN_MATCH_MS = 60000;          // ...but not before this: no ranked match is over in under a minute
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
const MAX_BODY = 16384;              // bytes of form a request may carry
const CACHE_S = { "/list": 20, "/leaderboard": 60, "/raceboard": 60, "/crowns": 60, "/lifetime": 30, "/queue": 10, "/filled": 5, "/ratings": 10 };
const LINK_CODE_MS = 10 * 60000;

// Rate limiting: token buckets kept in memory per isolate (Cloudflare runs a few isolates per region, so the
// real limit is a small multiple of these; cheap, and enough to stop a single client hammering D1). Keyed by
// client IP, by player id for the account endpoints, and by server address for the server endpoints.
// Many players can share one address (a LAN party, a dorm, carrier NAT), so the per-IP buckets are generous and
// the per-id bucket is the real limit on a player. The Discord bot (a write carrying BOT_KEY) has its own bucket
// and spends none of its address's: one "Queue for all" is 16 POSTs.
const LIMITS = {
  ip: { burst: 240, perSec: 6 },        // any endpoint, per client IP (a few dozen games polling /queue, /filled)
  write: { burst: 60, perSec: 0.5 },    // player writes (/hello /report /lifetime /queue /link ...), per client IP
  id: { burst: 10, perSec: 0.1 },       // the same, per player id
  bot: { burst: 200, perSec: 5 },       // writes with BOT_KEY (/queue /linkcode from the Discord bot), instead of ip + write
  linkfail: { burst: 5, perSec: 1 / 60 }, // /link code attempts per client IP (a right code gives its token back)
  server: { burst: 30, perSec: 0.2 },   // /ticket /result /racerun per server address
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
      ...[...LADDERS].map((l) => q("UPDATE OR IGNORE players SET id = ? WHERE id = ?", l + ":" + id, l + ":" + old)),
      q(`DELETE FROM players WHERE id IN (${ladderIds.map(() => "?").join(",")})`, ...ladderIds),   // (the new id had its own already)
      ...["accounts", "names", "links", "lifetime"].flatMap((t) => [q(`UPDATE OR IGNORE ${t} SET id = ? WHERE id = ?`, id, old), q(`DELETE FROM ${t} WHERE id = ?`, old)]),
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

async function saveName(env, me, nameHex, now) {
  if (!/^([0-9a-f]{2}){1,20}$/.test(nameHex)) return;
  const name = nameHex.match(/../g).map((h) => String.fromCharCode(parseInt(h, 16))).join("");
  if (/^[\x20-\x7e]+$/.test(name))
    await env.DB.prepare("INSERT INTO names (id, name, updated) VALUES (?, ?, ?) ON CONFLICT(id) DO UPDATE SET name = excluded.name, updated = excluded.updated")
      .bind(me, name.trim().slice(0, 20) || "player", now).run();
}

// The server that ran a match / time trial: its ip:port must have announced recently with this token.
async function serverAuth(env, form, srcIp, now) {
  const port = parseInt(String(form.get("port") || ""), 10), token = String(form.get("token") || "");
  const ip = srcIp || ipv4(String(form.get("ip") || ""));
  if (!ip || !(port > 0 && port < 65536) || !/^[0-9a-f]{32}$/.test(token)) return null;
  const addr = ip + ":" + port;
  const row = await env.DB.prepare("SELECT token, seen FROM servers WHERE addr = ?").bind(addr).first();
  if (!row || row.token !== token || now - row.seen > FRESH_MS) return null;
  return addr;
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
    if (!RACE_MAPS.has(map)) return "not a ranked race map";
    if (players.length !== 1) return "bad players";
  } else return "bad request";
  // every player vouched for this match / time trial: their key (which hashes to their id) signed its id in the game,
  // so a server can only ticket players who are really on it (a race ticket names whose time and name it may post)
  const keys = String(form.get("keys") || "").split(","), sigs = String(form.get("sigs") || "").split(",");
  if (!form.get("keys") || !form.get("sigs") || keys.length !== players.length || sigs.length !== players.length) return "missing signatures";
  for (let i = 0; i < players.length; i++) {
    if (!/^[0-9a-f]{64}$/.test(keys[i]) || (await sha256hex(keys[i])).slice(0, 32) !== players[i]) return "bad key for " + players[i];
    if (!(await ed25519Verify(keys[i], sigs[i], "quark-ticket-v1 " + match))) return "bad signature for " + players[i];
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
  if (!RACE_MAPS.has(map)) return "not a ranked race map";
  const t = await env.DB.prepare("SELECT kind, server, map, players, created, status FROM tickets WHERE id = ?").bind(ticket).first();
  if (!t || t.kind !== "race" || t.server !== addr || t.status !== "open" || t.map !== map || t.players !== me) return "no ticket";
  if (now - t.created > RACE_TICKET_MS) return "ticket expired";
  const ms = parseInt(String(form.get("ms") || ""), 10);
  const splits = String(form.get("splits") || "").split(",").map((x) => parseInt(x, 10));
  if (!(ms >= RACE_MIN_MS && ms < 3600000)) return "bad time";
  if (ms > now - t.created + RACE_SLACK_MS) return "bad time"; // longer than the session it was run in
  const floors = RACE_FLOORS[map];
  if (!floors || splits.length !== floors.length || splits[splits.length - 1] !== ms) return "bad splits";
  // checkpoint times increase, and no leg is faster than the course allows
  if (splits.some((x, i) => !(x > 0) || x - (i ? splits[i - 1] : 0) < floors[i])) return "bad splits";
  const best = await env.DB.prepare("SELECT ms FROM race_times WHERE map = ? AND id = ?").bind(map, me).first();
  if (best && best.ms <= ms) return `not faster (best ${best.ms})`;
  await env.DB.prepare(
    "INSERT INTO race_times (map, id, ms, splits, version, created) VALUES (?, ?, ?, ?, ?, ?) " +
    "ON CONFLICT(map, id) DO UPDATE SET ms = excluded.ms, splits = excluded.splits, version = excluded.version, created = excluded.created"
  ).bind(map, me, ms, splits.join(","), String(form.get("v") || "").slice(0, 16), now).run();
  await saveName(env, me, String(form.get("name") || ""), now);   // (only with an accepted run)
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

// Discord link, verified: the bot hands a Discord user (who ran /link there) a one-time code; the code typed in
// game links that player. Wrong codes are rate-limited per IP (see "linkfail").
async function linkCode(env, form, now) {
  if (!env.BOT_KEY || String(form.get("bot") || "") !== env.BOT_KEY) return "bad request";
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
  for (const map of RACE_MAPS) {
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
  } else if (env.BOT_KEY && String(form.get("bot") || "") === env.BOT_KEY) {
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
  const t = await env.DB.prepare("SELECT kind, ladder, players, created, status FROM tickets WHERE id = ?").bind(match).first();
  if (!t || t.kind !== "match") return "no ticket";
  if (t.ladder !== ladder || t.players !== res.entries.map((e) => e.id).join(",")) return "not the ticket's players";
  if (now - t.created < MIN_MATCH_MS) return "too soon";
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
  if (now - t.created < MIN_MATCH_MS || now - t.created > TICKET_MS) { await voidMatch(env, match); return "void: outside the ticket's window"; }
  if (t.result) return "already reported";
  const left = String(form.get("left") || "") ? String(form.get("left")).split(",") : [];
  const ids = res.entries.map((e) => e.id);
  if (left.some((id) => !ids.includes(id)) || new Set(left).size !== left.length || left.length >= ids.length) return "bad left";
  const result = t.ladder + "|" + res.text;
  await env.DB.batch([
    env.DB.prepare("UPDATE tickets SET result = ?, leavers = ?, resulted = ? WHERE id = ? AND result IS NULL").bind(result, left.join(","), now, match),
    env.DB.prepare("INSERT OR IGNORE INTO matches (id, result, players, status, created) VALUES (?, ?, ?, 'pending', ?)").bind(match, result, ids.length, now),
  ]);
  return finishMatch(env, match, now);
}

// What a ticketed match comes to, from the server's result and the players' reports (see /report at the top).
// Once the ticket is TICKET_MS old no report or result can come any more, so nothing stays pending: it's rated if
// the server's result is in, someone agreed and nobody objected, else void (a dispute still waits for the owner).
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
  if (against.length) {
    if (agree.length === 0 && against.length >= 2) return { state: "void", text: "void: the players contradict the server" };
    return { state: "disputed", text: "disputed" };
  }
  const missing = players.filter((id) => !left.has(id) && !byId.has(id));
  if (!missing.length) return { state: "rate" };
  if (agree.length >= 1 && (expired || (t.resulted && now - t.resulted >= REPORT_WAIT_MS))) return { state: "rate" };
  if (expired) return { state: "void", text: "void: no player confirmed the server's result" };
  return { state: "pending", text: `pending ${players.length - left.size - missing.length}/${players.length - left.size}` };
}

async function finishMatch(env, match, now, force = false) {
  const t = await env.DB.prepare("SELECT ladder, players, created, result, leavers, resulted, status FROM tickets WHERE id = ?").bind(match).first();
  const m = await env.DB.prepare("SELECT status FROM matches WHERE id = ?").bind(match).first();
  if (!t || !m) return "pending";
  if (m.status !== "pending" && m.status !== "disputed") return m.status;
  if (t.status === "void" && m.status === "pending") { await voidMatch(env, match); return "void"; }   // (its ticket went first)
  const { results: reports } = await env.DB.prepare("SELECT reporter, result, dispute FROM reports WHERE match = ?").bind(match).all();
  let d = force ? (t.result ? { state: "rate" } : { state: "pending", text: "pending server" }) : decideMatch(t, reports, now);
  if (m.status === "disputed" && !force && d.state !== "void") return "disputed";   // only the owner settles it, or more dissent voids it
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

const WRITES = new Set(["/announce", "/remove", "/hello", "/report", "/racetime", "/lifetime", "/queue", "/link", "/linkcode", "/resolve"]);
const SERVER_WRITES = new Set(["/ticket", "/result", "/racerun"]);

export default {
  async fetch(req, env, ctx) {
    const u = new URL(req.url);
    const q = (k) => u.searchParams.get(k) || "";
    const now = Date.now();
    const conn = req.headers.get("CF-Connecting-IP") || "";
    const srcIp = ipv4(conn); // the caller's own IPv4, if it came over IPv4 (can't be spoofed over TCP)
    const isWrite = WRITES.has(u.pathname) && u.pathname !== "/queue" && u.pathname !== "/lifetime" || SERVER_WRITES.has(u.pathname) ||
                    (req.method === "POST" && (u.pathname === "/queue" || u.pathname === "/lifetime"));
    // reads may be fetched by web pages (the website's leaderboard); writes never answer cross-origin
    const text = (body, status = 200) => new Response(body, { status, headers: isWrite ? { "content-type": "text/plain" } :
      { "content-type": "text/plain", "access-control-allow-origin": "*" } });
    let form = null;
    if (isWrite) {
      if (req.method !== "POST") return text("POST only", 405);
      // a browser always says where a cross-site POST comes from; the game, servers and the bot never send Origin
      if (req.headers.get("Origin")) return text("forbidden: no cross-site writes", 403);
      if (+(req.headers.get("content-length") || 0) > MAX_BODY) return text("too big", 413);
      try { form = await req.formData(); } catch { return text("bad request", 400); }
    }
    // the bot's writes: their own bucket, none of its address's
    const isBot = !!(form && env.BOT_KEY && String(form.get("bot") || "") === env.BOT_KEY);
    if (!(isBot ? allow("bot", "bot", now) : allow("ip", conn, now))) return text("rate limited", 429);
    if (isWrite && !isBot && !SERVER_WRITES.has(u.pathname) && u.pathname !== "/announce" && u.pathname !== "/remove") {
      if (!allow("write", conn, now)) return text("rate limited", 429);
      const pid = await idOf(String(form.get("secret") || ""));
      if (pid && !allow("id", pid, now)) return text("rate limited", 429);
    }
    if (SERVER_WRITES.has(u.pathname)) {
      const addr = await serverAuth(env, form, srcIp, now);
      if (!addr) return text("forbidden: announce first, and send the token", 403);
      if (!allow("server", addr, now)) return text("rate limited", 429);
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
      const tokenOk = !!(row && row.token && /^[0-9a-f]{32}$/.test(token) && row.token === token);
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
      ).bind(addr, (f("hidden") === "1" ? "~" : "") + f("v").slice(0, 16), now, newToken).run(); // "~": never matches /list
      return text("ok " + newToken);
    }
    if (u.pathname === "/hello") {
      const me = await playerOf(env, String(form.get("secret") || ""), now);
      if (!me) return text("bad request", 400);
      await saveName(env, me, String(form.get("name") || ""), now);
      return text("id " + me);
    }
    if (u.pathname === "/report") return text(await report(env, form, now));
    if (u.pathname === "/racetime") return text("update the game: race times are sent by the game server");
    if (u.pathname === "/link") return text(await link(env, form, now, conn));
    if (u.pathname === "/linkcode") return text(await linkCode(env, form, now));
    if (u.pathname === "/lifetime" && isWrite) return text(await lifetimeReport(env, form, now));
    if (u.pathname === "/queue" && isWrite) return text(await queue(env, form, now));
    if (u.pathname === "/resolve") {   // the owner settles a dispute: rate it as the server reported it, or void it
      if (!env.ADMIN_KEY || String(form.get("key") || "") !== env.ADMIN_KEY) return text("forbidden", 403);
      const match = String(form.get("match") || ""), action = String(form.get("action") || "");
      const m = await env.DB.prepare("SELECT status FROM matches WHERE id = ?").bind(match).first();
      if (!m || m.status !== "disputed") return text("not a dispute");
      if (action === "void") { await voidMatch(env, match); return text("void"); }
      if (action === "rate") return text(await finishMatch(env, match, now, true));
      return text("action=rate|void");
    }
    if (u.pathname === "/disputes") {
      if (!env.ADMIN_KEY || q("key") !== env.ADMIN_KEY) return text("forbidden", 403);
      const { results } = await env.DB.prepare("SELECT t.id, t.ladder, t.created, t.result, t.leavers FROM tickets t JOIN matches m ON m.id = t.id WHERE m.status = 'disputed' ORDER BY t.created LIMIT 100").all();
      return text(results.map((r) => `${r.id} ${r.ladder} ${r.created} ${r.result}${r.leavers ? " left=" + r.leavers : ""}`).join("\n"));
    }
    if (req.method !== "GET") return text("bad request", 405);
    return cached(req, u, ctx, async () => {
      if (u.pathname === "/list") {
        const { results } = await env.DB.prepare(
          "SELECT addr FROM servers WHERE seen > ? AND version = ? ORDER BY seen DESC LIMIT 400"
        ).bind(now - FRESH_MS, q("v").slice(0, 16)).all();
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
      if (u.pathname === "/raceboard") {
        if (!RACE_MAPS.has(q("map"))) return text("not a ranked race map", 400);
        const n = Math.min(100, Math.max(1, parseInt(q("n"), 10) || 25));
        const { results } = await env.DB.prepare(
          "SELECT r.ms, COALESCE(n.name, 'player') AS name FROM race_times r LEFT JOIN names n ON n.id = r.id " +
          "WHERE r.map = ? ORDER BY r.ms ASC, r.created ASC LIMIT ?"
        ).bind(q("map"), n).all();
        return text(results.map((r) => `${r.ms} ${r.name}`).join("\n"));
      }
      if (u.pathname === "/crowns") return text(await crowns(env));
      if (u.pathname === "/lifetime") return text(await lifetimeGet(env, q("ids")));
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
