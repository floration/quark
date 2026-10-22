# What's left to do

Everything known to be unfinished, as of the end of the 1.17.0 work. It draws on three sources:
- [FINAL_REVIEW.md](FINAL_REVIEW.md): ids like 2.8;
- the second security review's plan, [docs/FIXES_HANDOFF.md](docs/FIXES_HANDOFF.md) with [docs/REVIEW.md](docs/REVIEW.md): ids like MAP-3;
- notes left by this session's work.

**Fixed and merged:** every Blocker, High and Medium finding from the final review, plus NET-9, now a fixed spread pattern. None of what's below blocks 1.17.0.

Each item has a priority:
- **P1** — do soon, on the `testing` branch.
- **P2** — worth doing.
- **P3** — nice to have, or needs a decision first.

**Owner** marks items that need your decision or your hands.

---

## 0. Finishing 1.17.0 (in this order)

| # | What | Who |
|---|---|---|
| 0.1 | **Commit the last merge** (netcode and spread fixes). It's resolved, protocol 33, but uncommitted because the session was interrupted. Then confirm one clean full `quark_tests` run: the run before the interruption failed once (10 checks, a match not going live in time, the machine-load pattern) and passed the next time. | me |
| 0.2 | Mark the Medium rows Fixed in FINAL_REVIEW.md: 2.4-2.7, 3.6, 3.7, 5.2-5.4, 6.2, 6.3 (2.2, 1.4, 1.5 and 4.3 are already marked). | me |
| 0.3 | **Say go.** I push the branch, which builds and tests only; it's the first real run of the Windows test job. When it's green, I push tag `v1.17.0`, which publishes only if Windows, Linux and macOS all pass. | **Owner**, then me |
| 0.4 | **Ranking server database:** run the full SQL list in `tools/master/README.md` (same as FINAL_REVIEW.md section 3) **before** deploying the worker. | **Owner** |
| 0.5 | **Deploy `tools/master/worker.js`** at the same time as the game release. Old games and game servers can't use it: announces, tickets and race runs all changed. | **Owner** |
| 0.6 | Worker settings: add the cron trigger `*/15 * * * *` and the secret `ADMIN_KEY` (a long random string). | **Owner** |
| 0.7 | **Dedicated servers:** install python3, `git pull`, and run `tools/server/install.sh` once. If players reach a server through a different forwarded port, or it runs in a container, start it with `--own-addr host:port`, or ranked ids won't work there (the log says so). Optionally set `QUARK_ALERT_WEBHOOK` in `/etc/quark/update.env`. | **Owner** |
| 0.8 | **Discord bot:** redeploy with the new `install.sh` (secrets go in `/etc/quark-discord.env`), rotate the bot token and BOT_KEY, and update its role or invite to the new permissions (README). | **Owner** |
| 0.9 | Optional: keep today's ranked players established (the optional `INSERT INTO accounts …` in the SQL list). Without it, everyone is provisional for 5 matches. | **Owner** |
| 0.10 | **Old launchers:** launchers 1.16 and older read only the newest 50 releases. Ship production 1.17.0 before 50 testing builds pile up; after that, the game keeps every launcher current. | **Owner** |

## 1. Repository restructure (after the release)

| What | Notes |
|---|---|
| `testing` branch | All new work goes there. Each push becomes a signed prerelease (`test-<version>-test.N`), and Dependabot targets it. |
| Protect the default branch (**Owner**, GitHub settings) | Require a PR, the `main-guard / from-testing` check and the build checks. No force pushes or deletions. |
| Tag rule for `v*` (**Owner**, GitHub settings) | Only you can create, move or delete production tags. |
| Environments `production` and `testing` (**Owner**, optional) | Production deploys from `v*` tags, testing from the `testing` branch. Optionally move `QUARK_SIGNING_KEY` into both. |
| Discord bot → `discord-bot` branch | `git subtree split` (steps in `tools/discordbot/MOVING.md`), then remove `tools/discordbot/` from the game branches. The cross-language test then needs splitting: the bot half runs only when `QUARK_BOT_DIR` points at a bot checkout. The branch is public, like the whole repo; the secrets stay on your server. |
| Launcher → `launcher` branch | Release builds check out that branch to build and package `quark_launcher`. The game keeps its small launcher-upgrade code. |
| Website → `site` branch | Production releases build the site from that branch plus the game's changelogs and weapon tables, then publish to `gh-pages`. |

## 2. Remaining final-review findings (all Low or cleanup)

| # | What |
|---|---|
| 2.8 | Tourney seed-rating fetches aren't deduplicated. |
| 2.9 | The version-mismatch reply is bigger than the spoofable request: small amplification. |
| 2.10 | Stopping a hosted game can freeze the UI up to ~15 s while background threads finish. |
| 2.11 | A reconnect with a different key keeps the old ranked id. An FFA leaver who rejoins after the grace period is counted twice. |
| 3.8 | The bot's `/changelog` doesn't understand testing tags. |
| 4.4 | Holding Alt+Enter also presses Enter. |
| 4.5 | The Discord label in settings overlaps the next column. |
| 4.6 | `Ui::fit` can't shrink labels below the minimum text size. |
| 4.7 | The editor's `_new` fallback name can equal the original for 40-character names. |
| 4.8 | Overlapping editor handles: the far one can win the pick. |
| 4.9 | Scoreboard names aren't clipped, and the server-list marquee overflows on wide names. |
| 5.5 | Saved maps keep 6 significant digits while edits carry 9. |
| 5.6 | Map parsing doesn't limit the title or reset every field. |
| 6.4 | A giant can spawn inside a wall on maps not built for duel. |
| 6.5 | `addbot` during a tag match skips the team lock. |
| 1.6 | A launcher signature covers the file, not its version or channel. |
| 1.8 | Update installs keep every retired file forever. |

**Cleanup:**
- duplicate code: two caret implementations; `modeRespawns()` vs `rulesRespawnDelay`; several hex checks;
- unused code: `NetClient::reconnectToken`, `NavGraph::nextWaypoint`, `hideRounds_`, the Worker's `PENDING_MS`, the per-tick tag re-check;
- stale comments: protocol numbers, the syringe throw;
- `docs/MAP_REPORT.md` predates the grapple change;
- the console re-serialises the config every frame;
- hardening:
  - constant-time key compares;
  - `/disputes` as a POST;
  - `MAX_BODY` checks only Content-Length;
  - a domain prefix for `master.txt` signatures;
  - the bot trusts unsigned `master.txt`;
  - the bot installer's pip upgrade isn't hash-pinned;
- test gaps:
  - test.mjs never exercises shared-IP limits;
  - the "concurrent" queue test runs sequentially;
  - one wall-clock 3 s window;
  - port blocks can land in the OS's ephemeral port range.

## 3. Second security review: work not done yet

| ID | P | What |
|---|---|---|
| NET-10 | P1 | Hidden players' health, weapon, firing and bomb flags are still sent, plus projectiles from unseen players (a minor wallhack). Hide-and-seek leaks hider info during the freeze. Dead players' chat should reach only the dead. |
| PERF-3 | P1 | The lightmap bake runs on the main thread: joining a big map on a remote server can time out, and the window shows "Not Responding". Bake on a worker thread. |
| MAP-3 | P1 | Map reachability tests aren't collision-checked; unreachable should score 0. |
| MAP-2 | P1 | Not every offered mode has enough spawns. Anvil hosts the 4-player ranked instagib FFA. |
| MAP-5 | P1 | A cart-sweep test against geometry, plus a keep-clear registry in the generator. Fix the Railyard box (track 102-106 m) and the Lodestone pillar and dais. |
| MAP-4 | P1 | Metric fixes, one at a time: loops, vertical, per-team balance, worst-objective grading. Lodestone's race legs read "-1 m". Each fix may fail maps that then need a redesign (your call). |
| PERF-8 | P2 | Snapshot building allocates per client per tick. |
| PERF-9 | P2 | Visibility raycasts aren't staggered or capped (tick spikes with 32 players on Midtown). |
| PERF-11 | P2 | Main-thread hitches: map-list rescans, server-list refresh, demo writing, announcer audio swaps. |
| PERF-12, 13 | P2 | Cache `findDataPath`; compute `interpolatedPlayers` once per frame; send input once per frame; a static editor buffer; query the GPU's max texture size. |
| MAP-10, 11 | P2 | Real `stairs` and `setting` keywords instead of comments; a `version N` header; unknown optional keys warn instead of failing. |
| MAP-16, 17, 18 | P2 | Spawn sanity (facing, spacing, enemy spawns in line of sight) and a mirror check for ranked maps. |
| ENG-2 | P2 | Exception boundaries around demo and package loading and Worker-reply parsing. Map loading is done. |
| ENG-3 | P2 | Fuzz harnesses for the parsers behind a CMake option, plus an ASan/UBSan CI job. |
| COR-4 | P2 | Editor undo should work on a copy. |
| COR-5 | P2 | Unicode paths end to end: a non-ASCII Windows user folder may break data paths. |

## 4. Decisions waiting on you (Owner)

| What | Notes |
|---|---|
| Lodestone (MAP-6) | Offered in every mode but graded only for points, and bomb is unbalanced. Either grade every mode and fix the map, or offer fewer modes. |
| Midtown timing (MAP-8) | The defender lead at points D and E is outside its own 2-10 s band. Re-stage the spawns, and grade the worst point rather than the average. |
| Editor vs generator (MAP-9) | Map generation is now a build step. The remaining choice is whether editor work becomes a per-map overlay of edit ops. |
| Map ids (MAP-13) | Identify maps by sha256 instead of name + CRC32. |
| Generator split (MAP-14) | Split `make_maps.py` into prefabs plus one file per map. |
| Knife-throw switch delay | After a knife throw, your next weapon waits 0.75 s. That's what stops throw + crossbow being an instant 105. Keep it, or revert it (one line). |
| KOTH | One contester freezes the clock indefinitely. Left as is, since it's arguably normal KOTH. |
| LICENSE | None yet, by your choice. |
| LTO | Measured, but left off because the benchmark was noisy on shared software rendering. Re-measure on a real machine. |
| Windows timing check | One netcode check failed 2 of 3 runs under Wine. If it fails on real Windows CI, choose a Windows allowance or make that job non-blocking. |
| Unsigned "new" versions hidden | The launcher hides any unsigned release claiming 1.17 or later, since no real one can lack a signature. Say if you don't want that. |

## 5. Bots

| P | What |
|---|---|
| P2 | Falls and self-kills were re-checked only on Bastion after the dash fix. Run `quark_tests playtest` on all maps and fix any map where bots fall. |
| P2 | "FFA vs 3 expert bots" passes with 5 kills against a minimum of 2. The margin is thin; watch it. |
| P3 | Bots use the normal-size hull for site and hill checks, which is harmless while size modifiers are duel-only. Hide and tag bots can use the previous map's nodes until the new graph is ready. |

## 6. Older, smaller items

| P | What |
|---|---|
| P2 | The server clock `timeMs_` is 32-bit and wraps after 49.7 days of uptime; normal updates reset it long before then. |
| P2 | Projectiles move per tick rather than in fixed steps, so flight differs by about 5 cm/s between 125 and 1000 Hz. |
| P2 | Edit-session autosave serialises (~10-15 ms on Midtown) and parse-checks (~3 ms) on the tick every 5 s. |
| P2 | The client editor does a redundant full rebuild after each edit when editing alone. |
| P2 | RankService sleeps its job delays in real time while they're specified in game time (only matters under the tests' 4x clock). |
| P3 | `sfx_recorded.h` stays committed, because generating it needs ffmpeg. |
| P3 | Game modes are ~90 `mode == X` branches; a `GameMode` interface would keep each mode in one place (a big refactor). |
| P3 | Map format version, size budget and an entity system for objective markers (first review G3, G7, G8). |

## 7. Asset Studio

| P | What |
|---|---|
| P2 | The asset exporter was stopped partway and never finished: `quark_tests exportassets`, glTF models, and loading overrides from `assets/overrides/`. The Studio page lists only part of the assets and can't preview models. Redo it, then rebuild and republish the page (CLAUDE.md asks for that after every push). |
