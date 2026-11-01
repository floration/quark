# Internet server list (Cloudflare Worker + D1, free tier)

Public servers announce themselves here every minute. The Join screen lists them, then checks each one
directly over UDP, so it shows only servers it can reach, with their real ping. Nothing runs on a VPS.

## Deploying updates (automatic)
After the setup below, `deploy.sh` deploys this folder: it applies the D1 migrations in `migrations/` that are new,
then `worker.js`, its `DB` binding and its cron. The release workflow's `deploy-worker` job runs it after each production
release once switched on (docs/OPERATIONS.md section 3: an API token and three values in GitHub). Every schema change
from 1.17.1 on is a new numbered file in `migrations/` (and `schema.sql` updated to match); the per-version SQL lists
below are for the versions before that.

## Setup (dashboard, about 5 minutes)
1. **Create the database.** Cloudflare dashboard → **Storage & Databases → D1 SQL Database → Create**.
   Name it `quark`. Open it → **Console**, paste the contents of `schema.sql`, and run it.
2. **Create the worker.** **Compute (Workers) → Workers & Pages → Create → Create Worker** (or **Start with Hello World**).
   Name it `quark-master` → **Deploy**.
3. **Paste the code.** Open the worker → **Edit code**. Replace everything with `worker.js`, then **Deploy**.
4. **Bind the database.** Worker → **Settings → Bindings → Add → D1 database**.
   Variable name `DB`, database `quark` → **Save/Deploy**.
5. **Check it.** Open `https://quark-master.<your-subdomain>.workers.dev/list?v=0.13.0`.
   An empty page with no error means it works. `curl -d port=27500 -d v=test …/announce`, then `…/list?v=test`,
   should show your IP (writes are POST only).
6. **Point the game at it.** The URL is built into the game (`kPinnedMasterUrl` in `src/game/master.cpp`; whoever
   answers there receives every player's ranked secret, REVIEW WEB-11). To move it without a new build, put the new
   base URL as the only line of `site/master.txt` and sign that file with the release key
   (`QUARK_SIGNING_KEY=<key> build/quark_sign sign site/master.txt`, which writes `site/master.txt.sig`), then commit
   both to the `site` branch (checked out in `site/`, README "Branches"). Games ignore a `master.txt` without a valid signature. Sign it on your own machine, never in CI: every
   branch's push can run CI, so a CI-made signature would vouch for anything. `site/master.txt` (unsigned) is still
   what the website's leaderboard reads.
7. **Cron.** Worker → **Settings → Triggers → Cron Triggers → Add**: `*/15 * * * *`. It rates matches whose report
   wait is over, rates or voids matches whose ticket expired undecided, and prunes old rows (servers, tickets, matches, reports, queues, filled rows, link codes).
8. **Owner key (the dispute log).** Worker → **Settings → Variables and Secrets → Add → Secret**: `ADMIN_KEY`, a long
   random string. `POST /disputes` (form: key) lists disputed matches: the log. A disputed match is never rated and
   needs nothing from you (1.17.0; `/resolve` is gone). The key only ever goes in the POST body, never in a URL (use
   letters and digits only, e.g. `openssl rand -hex 32`): `read -rs K` (paste the key; it stays out of your shell
   history), then `curl -s -d "key=$K" https://<worker>/disputes`. (`GET /disputes?key=` is gone.)

The free tier allows 100k requests a day and 100k D1 writes a day. That is about 70 servers announcing around the clock.
For testing, the `QUARK_MASTER` environment variable overrides the URL.

## Ranked (0.24.0+)
The same Worker stores ranked ratings. To turn it on for an existing setup:
1. D1 → `quark` → **Console**: paste the `-- ranked` part of `schema.sql` (the three `CREATE TABLE` lines and the index) and run it.
2. Worker → **Edit code**: replace everything with the new `worker.js` → **Deploy**.
`https://quark-master.<you>.workers.dev/ratings?ids=` should answer with an empty page.

Each player's game makes a random key once (`quark.key` next to `quark.cfg`); their id is a hash of it, and the
key itself only goes to this Worker. After a ranked match every player's game posts its own report, and the
match is rated (OpenSkill, Plackett-Luce) only when all of them agree. Test locally: `node tools/master/test.mjs`.

## Race time trials (1.1.0+)
Solo time trial runs on the ranked race maps (`RACE_MAPS` in worker.js: the official maps with `for race` or `for surf`, each with its `racedefault` time or 600 s; regenerate the table with `build/quark_tests racefloors`) go to the same Worker. Each player keeps only their best time, along with the time at every checkpoint.
1. D1 → `quark` → **Console**: paste the `-- race time trials` part of `schema.sql` (the `race_times` table and index) and run it.
2. Worker → **Edit code**: replace everything with the new `worker.js` → **Deploy**.

`…/raceboard?map=accelerator` (one map's board) should answer with an empty page, and so should `…/racetotal` (1.19.6+: the race total, "total_ms maps_run name" per line: each racer's best on every ranked race map summed, a map they haven't run counted at its default time; no schema change).

**Checking or removing a suspicious time** (D1 console):
```sql
SELECT r.id, n.name, r.ms, r.splits, r.version, datetime(r.created / 1000, 'unixepoch') FROM race_times r
  LEFT JOIN names n ON n.id = r.id WHERE r.map = '<map>' ORDER BY r.ms LIMIT 50;
DELETE FROM race_times WHERE map = '<map>' AND id = '<id>';
```
`splits` is the run time in ms at each checkpoint, so a gap that's impossible to cover stands out.

## Testing servers are never rated (1.17)
Every dedicated-server box also runs a testing server (`tools/server/install.sh`). Its matches are played but never
rated: the game server doesn't send tickets, results or race runs (`Server::rated`), and the Worker refuses them too.
`/ticket`, `/result` and `/racerun` from a server whose announced version is a testing build (`1.19.0.testing.42`; before 1.19.0 a prerelease, `1.18.9-test.42`) answer
`testing server: ranked not rated` (403), and so does `/report` under a ticket from such a server.
Deploy: Worker → **Edit code** → replace everything with the new `worker.js` → **Deploy**. No database change.

## Server tokens, rate limits (security update)
Database: none of this was live before 1.17, so its SQL is in the one combined list under **Deploy** in
"Ranked keys, leavers, disputes, hardening (1.17)" below. Run that list, not pieces of it.

`/announce` answers `ok <token>`. A server may announce or remove its own source IPv4 address; through an IPv6
path (`?ip=`) it needs the token from an earlier IPv4 announce. Writes are rate-limited per IP and per player id
(in memory, per isolate; the per-IP buckets leave room for many players behind one address, and the Discord bot's
writes, carrying `BOT_KEY`, have their own larger bucket), and `/lifetime` adds at most 1200 kills and 1200 deaths per id per hour.

## Match tickets, server-reported race runs, verified Discord links (anti-cheat update)
What changed:
- **Ranked results need a ticket.** When a ranked match goes live, the game server (registered with the list by
  announcing, `&hidden=1` if it isn't public) opens a ticket (`POST /ticket`, with its announce token) naming the
  match id, ladder, map and players. `/report` is accepted only under an open ticket, from its players, once each,
  up to 2 hours after it opened; the server posts its own result (`POST /result`, not before 1 minute unless it
  names a leaver, so leaving in the first minute still loses) and the match is rated only when the server and every player agree. Each player's game also checks the host's result against the
  score it saw and refuses to report on a mismatch. A server opens at most one match ticket a minute.
- **Provisional accounts.** A player counts as established after 5 ticketed matches (`accounts.tgames`). Before that
  they're off the leaderboard and crowns, and an established player's rating doesn't move in a match against them,
  so fresh accounts can't be farmed for rating.
- **Race runs come from the server** (`POST /racerun`, under a race ticket for that racer), with every leg checked
  against `RACE_FLOORS` (straight-line gap between checkpoints at 48 m/s, from `build/quark_tests racefloors`).
  The race ticket carries the racer's signature of its id, like a match ticket, and a run's name is saved only
  when the run is kept, so a server can't rename or post times for a player who isn't on it.
  `POST /racetime` (the old self-report from the racer's game) is refused.
- **Discord links are verified**: `/link` in Discord gives a one-time code (`POST /linkcode`, bot only), typed in game.
  Old unverified links get no roles and no queue pings.

Database: none of this was live before 1.17, so its SQL is in the one combined list under **Deploy** in
"Ranked keys, leavers, disputes, hardening (1.17)" below. Run that list, not pieces of it.

Existing ratings and race times stay, but with an empty `accounts` table nobody is established, so the leaderboards
and crowns are empty until players finish 5 ticketed matches. The combined list ends with an optional statement that
grandfathers the players who already have ranked games (they were rated under the old, forgeable reports, so check
the top first).
Optionally clear suspicious old race times (see above), since they were self-reported. Old game versions can't
report ranked results or race times any more (they get "no ticket" / "update the game"), so release the matching
game version at the same time. When the course of Viaduct changes, re-run `build/quark_tests racefloors` and update
`RACE_FLOORS` (quark_tests fails until they match).

## Ranked keys, leavers, disputes, hardening (1.17)
What changed:
- **Ranked ids prove their key.** The id is now the first 32 hex of sha256 of an Ed25519 public key derived from the
  secret (seed = sha256(`quark-rank-ed25519-v1:` + secret)). Game servers check it at connect (the player signs the
  connect challenge), so nobody can use someone else's id (REVIEW NET-12). Old ids (sha256 of the secret) move to the
  new id the first time that secret reaches the Worker (`POST /hello`, sent by the game at start-up and retried until
  it takes, or any request with the secret): ratings, account, name, Discord link, lifetime stats and race times
  (`id_moves` records it). If the new id already has rows (a match rated before the secret arrived), they're merged:
  a ladder keeps the rating of the row with more games and adds up the games; the account keeps the most ticketed
  games and the earliest start; the faster race time stays.
  Players who never start 1.17 keep their old rows (harmless; unplaced players aren't on the boards).
- **Tickets carry every player's signature** of the match id and of the server address their game dialled (`keys`,
  `sigs`, `addrs`), so a server can only ticket players who are really in the match, on it: a signature naming
  another server (relayed from the server the player joined) is refused (`signed for another server`). The address
  must be the ticketing server's announced ip:port, or a private / loopback address on its port (LAN players).
- **Leaving loses.** The server's `/result` names who left (`left=`, first leaver first). The first leaver takes the
  loss (a 1v1 ends there; the stayer leaving afterwards isn't punished). Leavers aren't waited for.
- **Disputes are logged, never rated** (owner, 1.17.0: "just logged and unrated"). Any report against the server's
  result (`dispute=1`, sent by a game that saw a different score) makes the match `disputed`: it's never rated, and it
  stays in the log `/disputes` lists. Nobody has to settle it. A player who never reports doesn't hold
  the match up: `REPORT_WAIT_MS` (10 min) after the server's result, the cron rates it if someone agreed and nobody
  objected. Nothing stays pending: when its ticket expires (2 h) a match is rated that way or else void (no result
  from the server, or nobody confirmed it).
- **One placement threshold**: `PLACEMENT_GAMES` = 5 (the game's `kPlacementGames`), for the boards and for
  established accounts. No rating decay, no rating-banded queues (owner decision 3.4).
- **Hardening** (WEB-5 … WEB-12): writes are POST only and refused with an `Origin` header (no web page can make its
  visitors' browsers write); only reads send CORS `*`. At most 8 fresh servers per address. Reads are cached 5-60 s
  (Cache API + `cache-control`). Rating writes and queue fills are atomic (one D1 batch with a guard row, retried on
  a conflict). `GET /queue` no longer deletes. Indexes for the boards, queues and pruning; cron pruning; form size cap;
  NaN guard on ratings.
- **Privacy**: `/crowns` and `/filled` are public and show a linked player's ranked id, in-game name and Discord name
  together. The game says so in the Discord box on the settings screen and when you link; type `unlink` there to
  remove the Discord part.

Deploy (in this order, together with the matching game release, since old games announce with GET and can't sign
tickets):
1. D1 → `quark` → **Console**: run this, in this order. It is everything the live v1.16.0 database lacks (the
   "Server tokens", "Match tickets" and 1.17 changes above; checked against `schema.sql` at v1.16.0). Run it once,
   all of it: the `ALTER TABLE` lines fail if run twice ("duplicate column"), the `CREATE ... IF NOT EXISTS` lines
   don't. Then deploy the Worker (step 2) right away: the new `worker.js` needs every column here, and the old one
   ignores them.
```sql
-- server tokens, rate limits
ALTER TABLE servers ADD COLUMN token TEXT;
ALTER TABLE lifetime ADD COLUMN hour_start INTEGER NOT NULL DEFAULT 0;
ALTER TABLE lifetime ADD COLUMN hour_kills INTEGER NOT NULL DEFAULT 0;
ALTER TABLE lifetime ADD COLUMN hour_deaths INTEGER NOT NULL DEFAULT 0;
-- match tickets, provisional accounts, verified Discord links
ALTER TABLE links ADD COLUMN verified INTEGER NOT NULL DEFAULT 0;
-- signed ranked ids, leavers and disputes, atomic writes
ALTER TABLE players ADD COLUMN ver INTEGER NOT NULL DEFAULT 0;
ALTER TABLE reports ADD COLUMN result TEXT;
ALTER TABLE reports ADD COLUMN dispute INTEGER NOT NULL DEFAULT 0;
-- new tables (tickets is created with leavers / resulted: no ALTER TABLE tickets)
CREATE TABLE IF NOT EXISTS tickets (id TEXT PRIMARY KEY, kind TEXT NOT NULL, server TEXT NOT NULL, ladder TEXT NOT NULL, map TEXT NOT NULL, players TEXT NOT NULL, created INTEGER NOT NULL, status TEXT NOT NULL, result TEXT, leavers TEXT, resulted INTEGER);
CREATE INDEX IF NOT EXISTS tickets_server ON tickets (server, created);
CREATE TABLE IF NOT EXISTS accounts (id TEXT PRIMARY KEY, tgames INTEGER NOT NULL, created INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS link_codes (code TEXT PRIMARY KEY, discord TEXT NOT NULL, created INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS id_moves (old TEXT PRIMARY KEY, new TEXT NOT NULL, moved INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS rate_guard (ok INTEGER NOT NULL CHECK (ok = 1));
-- indexes for the boards, queues and pruning
CREATE INDEX IF NOT EXISTS players_skill ON players ((mu - 3 * sigma));
CREATE INDEX IF NOT EXISTS tickets_created ON tickets (created);
CREATE INDEX IF NOT EXISTS matches_status ON matches (status, created);
CREATE INDEX IF NOT EXISTS queue_joined ON queue (joined);
CREATE INDEX IF NOT EXISTS filled_created ON filled (created);
CREATE INDEX IF NOT EXISTS link_codes_created ON link_codes (created);
CREATE INDEX IF NOT EXISTS lifetime_kills ON lifetime (kills);
-- optional: keep today's players on the boards (they'd otherwise be provisional for 5 matches)
INSERT OR IGNORE INTO accounts (id, tgames, created)
  SELECT substr(id, instr(id, ':') + 1), MAX(games), MIN(updated) FROM players GROUP BY substr(id, instr(id, ':') + 1);
-- optional cleanup: 1.16 pending matches have no ticket and would never be pruned
UPDATE matches SET status = 'void' WHERE status = 'pending' AND id NOT IN (SELECT id FROM tickets);
DELETE FROM reports WHERE match NOT IN (SELECT id FROM tickets);
```
   Check: `SELECT name FROM pragma_table_info('players')` lists `ver`; `SELECT COUNT(*) FROM tickets` answers 0.
2. Worker → **Edit code**: replace everything with the new `worker.js` → **Deploy**.
3. Add the cron trigger (step 7 of Setup) and the `ADMIN_KEY` secret (step 8).
4. Tell the Discord bot's maintainer: its POSTs are unchanged, but it must not send an `Origin` header.

Check: `curl -s …/list?v=1.17.0` answers (empty is fine); `curl -s …/announce` answers `POST only`.
Test locally first: `node tools/master/test.mjs`.
