# Final review before 1.17.0

A read-only review of everything going into 1.17.0 (`origin/claude/compassionate-ritchie-07ah6i` = v1.16.0 → `HEAD`), done in six areas. Each finding has a severity, where it is, what goes wrong, and its status.

Severity: **Blocker** = the release can't go out or breaks for everyone. **High** = a real bug players or server operators will hit. **Medium** = a bug in a narrower case, or a security hole that needs effort to use. **Low** = minor. **Clean** = tidiness (dead code, duplicates, stale comments).

Status: **Fixed** (commit in this branch), **Open** (not fixed yet), **Won't fix** (with reason).

## Summary

| Area | Blocker | High | Medium | Low | Clean items |
|---|---|---|---|---|---|
| 1. Release pipeline, launcher, server scripts | 1 (fixed) | 2 (fixed) | 1 | 2 | 3 |
| 2. Netcode, server, ranked | | 2 | 5 | 4 | 1 group |
| 3. Ranking server, Discord bot | | 3 | 4 | 1 | 1 group |
| 4. Client | | 1 | 2 | 6 | 1 group |
| 5. Maps, map code, tests | | 1 | 3 | 2 | 1 group |
| 6. Game rules, movement, bots | | 1 | 2 | 2 | 1 group |

**Every Blocker and High finding is fixed** (the release blocker and server-update problems in 9601569; the rest in the commits named in each row), and every Medium finding. The Low and Clean items are open. The open Medium items are worth doing on the `testing` branch after this release; none of them blocks it.

---

## 1. Release pipeline, launcher, server scripts

| # | Sev | Where | Finding | Status |
|---|---|---|---|---|
| 1.1 | Blocker | `.github/workflows/release.yml` windows / windows-tests | Maps are now generated at build time and aren't in git. The Windows test job only got the test exe, so on a fresh checkout every map test fails ("maps >= 20") and production would never publish. | **Fixed** (9601569): the maps travel with the test exe. |
| 1.2 | High | `tools/server/autoupdate.sh` | A server installed by an older `install.sh` updates itself with its old script (fine, if python3 is installed), but afterwards the new script runs under the old unit: it can't create `/var/lib/quark`, exits 1 every minute with no alert, and never updates again. | **Fixed** (9601569): under an old unit it logs "run install.sh once" and exits 0. |
| 1.3 | High | `tools/server/install.sh` | Re-running `install.sh` on an old install stopped the old server before building; a failed build left the server down. | **Fixed** (9601569): the old units are stopped only after the new build succeeds. |
| 1.4 | Medium | release.yml / launcher | Launchers 1.16 and older fetch 50 releases per page (the new one 300). After 50+ testing prereleases since the last production release, an old launcher sees no production release and stops updating. | **Fixed** (launcher, owner decision: no pruning, every release stays playable): the new launcher reads every page until a short one, both channels; pre-signing releases are listed as UNSIGNED and install only on the player's pick + confirmation. Launchers <= 1.16 still read only their first 50: they need a production release among the newest 50 to move to 1.17+ (after that the game keeps launchers current). |
| 1.5 | Low | `src/launcher/main.cpp:389` | An old `launcher.cfg` pinned to an unsigned (pre-1.17) version counts as "latest", so the launcher silently moves that player to the newest version. | **Fixed**: the pin is kept (unsigned versions are listed now); installed = plays it, else the launcher asks for confirmation, never auto-installs or moves to the newest. |
| 1.6 | Low | launcher signature | The signature covers the file only, not its version or channel: any validly signed package passes as any release. | **Fixed**: the manifest (`quark-release.txt` + `.sig`) now lists every package's sha256, and the launcher installs a signed package only when that manifest verifies and names the release's tag, version, channel and the downloaded package's hash (a testing build's late macOS package has its own `quark-release-macos.txt`). Unsigned (<= 1.16) releases still install after confirmation. Test: `quark_tests launcher` (testManifest). |
| 1.7 | Clean | `docs/SIGNING.md` | "One-time setup" still points at a private key file in a session scratchpad (now deleted; the secret is set). | Open |
| 1.8 | Clean | `src/launcher/main.cpp:180` `swapInstall` | Copies every old file the package lacks into the new install, so retired files live forever; with user data moved out, only `quark.cfg` needs keeping. | Open |
| 1.9 | Clean | `.github/dependabot.yml` | Targets the `testing` branch, which doesn't exist until the restructure. | Open (resolved by creating `testing`). |

Checked and fine: the production path (meta job finds the default branch from the event, needs `changelogs/1.17.0.md`, signs and verifies with the built-in key, publishes, deploys pages without re-triggering); a 1.16 launcher installs 1.17.0 (ignores `test-*` tags, `.sig` and the manifest) and the 1.17 game then upgrades that launcher; the new launcher verifies before extracting, swaps atomically with rollback, never loops or downgrades.

**Before tagging:** push the branch first and wait for green (the Windows test job has never run on real Windows), and on each dedicated server install python3 and re-run `tools/server/install.sh` after a `git pull`.

## 2. Netcode, server, ranked, signing

| # | Sev | Where | Finding | Status |
|---|---|---|---|---|
| 2.1 | High | `server.cpp` dropPlayer / handleConnect / sendEvent | A ranked leaver is only recorded when the reconnect grace runs out. Two other paths drop a live ranked player without it: a fresh connect from the same address (new nonce, no token) and the unacked-event cap. The loser can dodge the loss: the match goes unrated. | **Fixed** (be1ff99): every non-shutdown drop of a live ranked player counts as leaving (once per id); new sessions during a live ranked match spectate. Test: `testRankedLeaverPaths`. |
| 2.2 | Medium | `ranked.cpp` rankConnectMessage / rankTicketMessage | Signed messages don't name the server, so a malicious server can relay a player's connect challenge and ticket signatures to a real ranked server and play under their id. | **Fixed**: both messages name the server address the player's game dialled (anything the real server puts in its challenge a relay can forward; only the address the player chose can't be). The server takes a connect signature only for its own address (`--own-addr`, else its port and loopback from this machine, an interface address, or its public IPv4 looked up at start / from UPnP); the Worker takes a ticket signature only for the ticketing server's ip:port (or a private / loopback address on its port). Protocol 32. Tests: `testRankedRelay`, test.mjs "signed for another server". |
| 2.3 | High | `server.cpp:236` vs `src/server/main.cpp` | Master replies are only drained in `Server::run()`, which dedicated servers don't use: failed race tickets never clear, "no match ticket" / leaderboard messages never reach players, and the reply queue grows forever. | **Fixed** (6ef6be8): replies drain in `frame()` (hosted and dedicated); the queue keeps the newest 64. Test: `testMasterReplies`. |
| 2.4 | Medium | `server.cpp:68-102` mapInfo | Caches misses for any name (a player can add thousands per second with `callvote map <random>`), and stamps the shipped file while `Map::load` reads your maps folder first (user maps never refresh; an edited official map keeps the official ranked bits). | **Fixed** (74baa4a): misses aren't cached, the cache is capped, and it stamps the file `Map::load` opens. |
| 2.5 | Medium | `server.cpp:1737` vs `config.cpp:47` | The new rewind cap (ping + snapshot interval + 50 + 30 ms) is below what `interp_ms` (up to 200) needs: those players' hitscan is clamped short and misses movers. | **Fixed** (6a19f4f): clients report a fixed interp delay; the cap covers up to 200 ms. |
| 2.6 | Medium | `netclient.h:163` | The client's `renderTime_` / snapshot times are still float seconds of server uptime: 8 ms error after 18 h, 31 ms after 3 days, feeding interpolation and the lag-compensation view tick. | **Fixed** (67e14ce): the client render clock is double. |
| 2.7 | Medium | `proc.cpp:27` | Pipes aren't close-on-exec, so concurrent children inherit each other's pipe ends (curl waits for stdin EOF; two POSTs can deadlock; a browser can hold an HTTP thread). The SIGPIPE swap is process-wide and racy. | **Fixed** (11f9978): close-on-exec pipes via posix_spawn; SIGPIPE ignored once at start. |
| 2.8 | Low | `server.cpp:672, 2567` | Tourney seed-rating fetches aren't deduplicated: rotating keys can queue ~150 HTTP jobs a second. | Open |
| 2.9 | Low | `server.cpp:577` | The version-mismatch reject (34 bytes) answers an 11-byte spoofable connect before the cookie check: ~3x amplification. | Open |
| 2.10 | Low | `server.cpp:205` stop() | Leaving or stopping a hosted game joins the reachability and seeding threads on the UI thread: up to ~15 s freeze. | Open |
| 2.11 | Low | `server.cpp:713` restorePlayer | A reconnect matched by token with a different key overwrites the key but keeps the old ranked id. An FFA leaver who rejoins after the grace is counted twice. | Open |
| 2.12 | Clean | server.cpp / server.h | `vote_.caller = caller;` twice; `#include <thread>` twice; comment says protocol 29 where it's 30+; initial values repeat named constants (chat/map tokens, catch-up, verify budget); hex checks duplicated (`hexLen` vs `rankVerify`'s `isHex`, netclient); `NetClient::reconnectToken()` unused; `setRanked` scans the maps folder on the tick instead of the cached list; master.txt signatures share the release key format with no domain prefix (safe only because of the https check). | Open |

## 3. Ranking server (Cloudflare Worker) and Discord bot

| # | Sev | Where | Finding | Status |
|---|---|---|---|---|
| 3.1 | High | `worker.js:672` rate limits | The bot's POSTs all come from one IP and share the per-IP write bucket (20 burst, then 1 per 5 s). One "Queue for all" is 16 POSTs, so a second press or a few `/queue` / `/link` uses get refused. Players behind one shared address hit the same limit and lose `/report` / `/lifetime`, which the game doesn't retry. | **Fixed** (361560c): BOT_KEY writes get their own bucket (200 burst, 5/s); per-IP buckets sized for shared addresses; per-id limits unchanged. |
| 3.2 | High | `worker.js:214, 239` openTicket / raceRun | Race tickets carry no player signature, and `raceRun` saves the name before checking the run is faster: any announced (even hidden) server can rename any player on the boards and plant a race time under their id. Verified. | **Fixed** (b2ad249): race tickets carry the racer's signature (server asks the racer to sign, Worker verifies); the name is saved only for an accepted run. |
| 3.3 | High | `worker.js:613, 477` prune / report | A match whose players never report stays `pending` forever (ticket never pruned); the cron's `LIMIT 50` without ORDER BY can keep re-picking the same stuck rows; one throwing `finishMatch` skips every later delete; `prune` returns rows selected, not rated. | **Fixed** (97c3c6a): expired matches are rated or voided, oldest first; failures don't stop the rest; returns the rated count. |
| 3.4 | Medium | `worker.js:212` | A server's next ticket voids its previous match even if that already has a result, so a late dispute is silently dropped (while the cron still rates it). | **Fixed** (7553d16) |
| 3.5 | Medium | `worker.js:503` serverResult | Results within the first 60 s are voided, so leaving a ranked match in the first minute escapes the leaver's loss. | **Fixed** (c3c076d): early results naming a leaver count; early reports are kept. |
| 3.6 | Medium | `worker.js:158` id migration | If the new id already has rows, the old rating/account rows are dropped instead of merged; `/hello` is sent once at start-up with no retry, so a failed one followed by a rating loses the old rating. | **Fixed**: rows the new id already has are merged (a ladder keeps the rating of the row with more games and adds both rows' games, ver + 1; the account keeps MAX(tgames), MIN(created); names / links / lifetime / race times as before); the game retries `/hello` on the ranked thread (10 s doubling to 15 min) until the Worker answers `id <id>`, then records it in the config (`rank_hello`) and doesn't send it again. Tests: test.mjs "merged, not dropped", `testHelloRetry`. |
| 3.7 | Medium | `tools/master/README.md` | The 1.17 deploy step lists only the last SQL block, but none of the three are live yet; run alone it fails. | **Fixed**: the 1.17 deploy step is the combined list below (checked: applied to the v1.16.0 `schema.sql` it gives every table, column and index of today's); the two earlier sections point to it instead of carrying their own SQL. 2.2 and 3.6 need no new columns. |
| 3.8 | Low | `bot.py` /changelog | Looks up `v1.2.3-test.4`, but testing builds are tagged `test-<ver>-test.N`, and the changelog file is named by the base version, so it always says "no changelog" for testing builds. | **Fixed**: `/changelog 1.17.0-test.3` (or `test-1.17.0-test.3`) looks up the release tagged `test-1.17.0-test.3`, else `changelogs/1.17.0.md`, titled "(testing)". Test: test_quarkdata `test_changelog_testing`. |
| 3.9 | Clean | worker / bot | `PENDING_MS` unused; LIMITS comment lists old endpoints; `caches.default` likely does nothing on `*.workers.dev` (read caching needs a custom domain); `MAX_BODY` only checks Content-Length; `/disputes` takes ADMIN_KEY in the query string with CORS `*` (should be POST); keys compared with `!==`; the bot reads unsigned `master.txt` and would send BOT_KEY wherever it points; test.mjs gives each call its own IP (bot / NAT limits untested) and runs the "concurrent" queue race sequentially; install.sh upgrades pip without a hash. | **Fixed** except `caches.default` (documented in worker.js: works only on a custom domain): keys compared in constant time (`keyEq`); `/disputes` is a POST with the key in the body, no CORS; `MAX_BODY` counted on the bytes read; `PENDING_MS` gone, LIMITS comment current (8ac0df4); the bot pins the Worker URL (config `master_url`, default = the game's built-in one) and never reads master.txt (81a94cf); test.mjs covers shared-address limits, a truly interleaved queue race and rated cron counts (3b94f0d); install.sh no longer upgrades pip. |

Checked and fine: nothing that writes accepts GET; the bot and the game send no Origin header; every game and bot call matches the Worker's parameters and replies; every column the Worker uses exists in `schema.sql`; the cron's rating is idempotent and safe next to live requests; the bot's tests (29) and the Worker's tests pass.

### Database changes for the owner (live v1.16.0 → 1.17.0), in this order, before deploying `worker.js`
```sql
ALTER TABLE servers ADD COLUMN token TEXT;
ALTER TABLE lifetime ADD COLUMN hour_start INTEGER NOT NULL DEFAULT 0;
ALTER TABLE lifetime ADD COLUMN hour_kills INTEGER NOT NULL DEFAULT 0;
ALTER TABLE lifetime ADD COLUMN hour_deaths INTEGER NOT NULL DEFAULT 0;
ALTER TABLE links ADD COLUMN verified INTEGER NOT NULL DEFAULT 0;
ALTER TABLE players ADD COLUMN ver INTEGER NOT NULL DEFAULT 0;
ALTER TABLE reports ADD COLUMN result TEXT;
ALTER TABLE reports ADD COLUMN dispute INTEGER NOT NULL DEFAULT 0;
CREATE TABLE IF NOT EXISTS tickets (id TEXT PRIMARY KEY, kind TEXT NOT NULL, server TEXT NOT NULL, ladder TEXT NOT NULL, map TEXT NOT NULL, players TEXT NOT NULL, created INTEGER NOT NULL, status TEXT NOT NULL, result TEXT, leavers TEXT, resulted INTEGER);
CREATE INDEX IF NOT EXISTS tickets_server ON tickets (server, created);
CREATE TABLE IF NOT EXISTS accounts (id TEXT PRIMARY KEY, tgames INTEGER NOT NULL, created INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS link_codes (code TEXT PRIMARY KEY, discord TEXT NOT NULL, created INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS id_moves (old TEXT PRIMARY KEY, new TEXT NOT NULL, moved INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS rate_guard (ok INTEGER NOT NULL CHECK (ok = 1));
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

## 4. Client (game, font, console, editor)

| # | Sev | Where | Finding | Status |
|---|---|---|---|---|
| 4.1 | High | `game.cpp:2349` `/kick` completion | Completes player names, but the server's `kick` takes a numeric id: `atoi("Bob")` is 0, so `/kick Bob` kicks slot 0, usually the host. The ranked-participant check also tests slot 0. | **Fixed** (54465d7): the server resolves `kick <name or id>` (id, exact name, unique prefix; refuses ambiguous / unknown). Test: `testKickByName`. |
| 4.2 | Medium | `game.cpp:2350` `/addbot` completion | Offers skill names as the first argument, but the server reads `[a\|b] [0-3]`: `/addbot EXPERT` adds a default-skill bot. | **Fixed** (d8da980): completion offers a / b then the skill; the server accepts 0-3 or the skill name and refuses unknown skills. |
| 4.3 | Medium | `src/launcher/selfupdate.cpp:207` startFromGame | The launcher-upgrade thread is detached; quitting quickly can kill it between the two renames, leaving a launcher missing on Windows. | **Fixed**: the thread is kept and joined at game exit (`finishFromGame`, up to 2 s); each swap's rename pair (and each shortcut / programs step) runs under a gate that refuses new steps once shutdown began; skipped for --screenshot / --bench / --edscript / --textheavy / shot runs. |
| 4.4 | Low | `game.cpp:496` Alt+Enter | Holding Alt+Enter sends key repeats, which fall through as Enter (submits chat / activates the focused button). | Open |
| 4.5 | Low | `game.cpp:3157` settings | "DISCORD CODE (/link) - PUBLIC" overlaps the next column at 1280x720; a linked name is wider still. | Open: clip to the column. |
| 4.6 | Low | `ui.cpp:37` `Ui::fit` | Floors at 0.5 but the renderer clamps every scale up to 1.3, so labels needing more shrink still overflow. | Open |
| 4.7 | Low | `editor.cpp:234` | For a 40-character map name, the `_new` fallback is truncated back to the same name (saving overwrites the broken file it means to protect); an empty / invalid name falls back to "mymap" and can overwrite it. | Open |
| 4.8 | Low | `editor.cpp:943` | With 12 handles drawn over everything, a near-axis view puts a far handle on top of a near one and the far one wins the pick. | Open: tie-break toward the camera. |
| 4.9 | Low | `game.cpp:2987`, `:3387` | Scoreboard names aren't clipped (long name + "(DEAD)" runs into RATING); the server-list marquee sizes by "M" while "W" is wider. | Open |
| 4.10 | Clean | client | Two caret implementations (`Ui::caret`, `Game::drawCaret`); `isValidMapName` vs `validMapName` with different messages in the editor; `Vertex2D` fields still named `outline, glyph`; `drawConsole` re-serialises the config every frame while the console is open; `make_font_atlas.py` `{TPU}.0f` and a wrong "row 7" docstring; the game posts `/hello` on every start (needed once). | Open |

Checked and fine: caret index = byte index (input is ASCII-only); completion edge cases; the SDF font's atlas, UVs and kerning ranges; frustum culling and lightmap bucketing; debug flags are command-line only (nothing passes them to the game); no new per-frame file IO.

## 5. Maps, map code, test suite

| # | Sev | Where | Finding | Status |
|---|---|---|---|---|
| 5.1 | High | `map.cpp:731, 742`, `editor.cpp:823` | Map edits only accept materials 0-7, but every shipped map has trim (8) and clip (9) brushes. In the editor, nudging a trim/clip wall is refused and deleting one can't be undone; clip is the out-of-bounds seal, so this can open a map. The material cycle (`% 8`) silently turns them into ordinary materials. | **Fixed** (cb5a991): every edit op, undo and group accepts all 10 materials; the editor cycles through TRIM and CLIP (INVISIBLE WALL) and draws clip see-through violet. Test: set / insert / delete + undo of trim and clip on bastion and orbital. |
| 5.2 | Medium | `map.h:25-41`, `map.cpp:533, 725, 735, 978` | Two sets of limit constants (`kMaxMap*` and `kMapMax*`) from two separate fixes. The real cap is 60000 brushes in total on every path, so the 65535 checks never fire, and `b ins` compares the total against the "authored" constant. Parser / edit literals (256, 16, 64, 128, 512, 4096, 1024) repeat the constants; light intensity 0 is refused by parse/edit but allowed by `validate()`. | **Fixed** (231480d): one set of constants, dead checks gone, one name validator. |
| 5.3 | Medium | `CMakeLists.txt` copy_maps, `map.cpp:158` | Maps removed from the generator stay in `build/maps` forever (copy, never prune) and fail the suite locally (stale crucible / duel / sprint: 9 failures). A stray `.qmap` in `maps/` is also embedded as an official (ranked) map. CI builds fresh, so it passes there. | **Fixed** (87b8ffb): only generated + declared maps are copied and embedded; crucible retired. |
| 5.4 | Medium | tests: `maps` vs `edit` / `ranked` groups | Forked test groups share one temp maps folder; the `edit` and `ranked` groups write `zz_*` maps while the `maps` group lists and grades every map. Passes by timing; with more cores the map report fails on a `zz_` map. | **Fixed** (1b941e4): one data dir per forked test group; zz_ maps skipped. |
| 5.5 | Low | `map.cpp:965` serialize | Saves write 6 significant digits while edits carry 9: an edit at x=1000.125 reloads as 1000.12, so a client that downloads the edited map collides differently from one that applied the edits (grid-snapped edits are exact). | Open: `%.9g`. |
| 5.6 | Low | `map.cpp:409, 399` parseText | No title limit at parse (edits cap it at 40); `parseText` doesn't reset title / sun / palette / ceiling / void, and a failed parse leaves fields from the bad text (harmless today: callers use fresh maps). | Open |
| 5.7 | Clean | maps / tests | `isValidMapName` only forwards to `validMapName` (its comment describes the old rule; two callers print the old message); `mapNameChar` duplicates the name rule; `docs/MAP_REPORT.md` predates the grapple change (exit 28 m/s, doc says 24); `.gitignore` lacks `/maps/*.tmp`; port blocks above 32768 overlap the OS ephemeral range (small flake risk); one wall-clock 3 s window in the 200-part edit test. | Open |

Checked and fine: every test function is called from some group; no literal port bypasses `TP()` and the parallel groups' port ranges don't overlap; `make_maps.py` output is deterministic.

## 6. Game rules, movement, bots

The owner's section-5 answers are all implemented as decided (overtime, syringe only where the dead respawn, shared throw cooldown reset by syringe heals, hull scaling with matching client prediction, no grapple brake, head box + flat 50 sniper, hide-and-seek rotation and scoring, red headshot numbers, 200 seed for unrated). The bots' background navigation build copies the map, so a map change mid-build is safe; per-thread rules have no stale path.

| # | Sev | Where | Finding | Status |
|---|---|---|---|---|
| 6.1 | High | `server.cpp:284-300` checkTickRate | Two merged changes: "tick rate only changes between rounds" removed the quick undo of a failed step-up. A step-up made in a round break that doesn't hold now runs late ticks for the whole next round (frame-time spikes, the top priority). | **Fixed** (925a9fa): a step-up that doesn't hold is undone at once in any phase; other step-downs still wait for a break. Test: `testRateUndoStepUp`. |
| 6.2 | Medium | `gamemodes.cpp:956` rulesReset | Players who joined mid tag match are forced onto team A at the next match, skipping the 1v1 / duel seat limit and team balance (a vote to 1v1 can end up with three players). | **Fixed** (2622612): late joiners are auto-teamed after the reset. |
| 6.3 | Medium | `gamemodes.cpp:780, 835` | Checkpoint time bonuses are added to a deadline already in the past during overtime: "+60 s" at 20 s into overtime gives 40 s, after 60 s nothing. | **Fixed** (1521b69): bonuses add to max(deadline, now). |
| 6.4 | Low | `server.cpp:1246-1262` spawns | If no spawn fits a giant's hull, the fallback picks a random one that may be blocked (anvil's a/b spawns, some on furnace and summit; duel on clockwork is fine). | Open |
| 6.5 | Low | `server.cpp:2047` addbot | Admin `addbot` during a tag match skips the team lock; a bot reusing an eliminated bot's name is marked out at once. | Open |
| 6.6 | Clean | rules / bots | The per-tick tag re-check (`tagWasOut`) can't trigger for humans any more (string compares every tick); `hideRounds_` set, never read, wrong comment; `NavGraph::nextWaypoint` unused; `#include <cmath>` twice in customrules.cpp; `modeRespawns()` duplicates `rulesRespawnDelay`'s list; bots use the unscaled hull for site / hill checks; hide / tag bots can pick goals from the previous map's nodes until the new graph is ready; `timeMs_` is still 32-bit (wraps after 49.7 days); a syringe comment describes a case that needs FIRE held; projectiles still integrate per tick (~5 cm/s difference between 125 and 1000 Hz, pre-existing). | Open |

Checked and fine (test groups run): rules, it, weapons, syringe, points, cartchecks, custom, nav, bots / aim.
