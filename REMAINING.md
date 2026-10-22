# What's left to fix

Everything known to be unfinished after the 1.17.0 work, from three sources:
- the final review: [FINAL_REVIEW.md](FINAL_REVIEW.md), ids like 2.4;
- the second security review's plan: [docs/FIXES_HANDOFF.md](docs/FIXES_HANDOFF.md) with [docs/REVIEW.md](docs/REVIEW.md), ids like MAP-3;
- notes left by the work streams this session.

Every Blocker and High finding is fixed. Nothing here blocks the 1.17.0 release.

Each item has a priority:
- **P1:** do soon, on the `testing` branch.
- **P2:** worth doing.
- **P3:** nice to have, or needs a decision first.

**Owner** marks an item that needs your decision or your hands.

---

## 0. Before and right after the 1.17.0 release (your hands)

| What | Notes |
|---|---|
| Say go for the push | Push the branch first. That only builds and tests, and it's the first real run of the Windows test job. When it's green, push tag `v1.17.0`. |
| Database changes on the ranking server | Run the full list in FINAL_REVIEW.md section 3, in order, **before** deploying the new `worker.js`. |
| Deploy `tools/master/worker.js` | Deploy it at the same time as the game release. Older games and game servers can't use it: announces, tickets and race runs all changed. |
| Cron trigger and admin key | Add `*/15 * * * *` under Worker → Settings → Triggers. Add a secret `ADMIN_KEY`, a long random string, used for `/disputes` and `/resolve`. |
| Dedicated servers | Install python3, `git pull`, then run `tools/server/install.sh` once on each server. Optionally set `QUARK_ALERT_WEBHOOK` in `/etc/quark/update.env`. |
| Discord bot | Redeploy it with the new `install.sh`, which puts its secrets in `/etc/quark-discord.env`. Rotate the bot token and BOT_KEY afterwards. Re-invite it or edit its role so it has the new permissions (README); it no longer needs Mention Everyone. |
| Optional: keep today's ranked players on the boards | Run the optional `INSERT INTO accounts …` from section 3. Without it, everyone is provisional for 5 matches. |
| Asset Studio | No uploads were waiting. The page still shows the old defaults (see section 7). |

## 1. Repository restructure (next, after the release)

| What | Notes |
|---|---|
| Create the `testing` branch | All new work goes there. Each push becomes a signed prerelease (`test-<version>-test.N`). Dependabot also targets it. |
| Protect the default branch (Owner, in GitHub settings) | Require a PR, require the `main-guard / from-testing` check plus the build checks, and block force pushes and deletions. |
| Tag rule for `v*` (Owner, in GitHub settings) | Only you can create, move or delete production tags. |
| Environments `production` and `testing` (Owner, optional) | Production deploys from `v*` tags, testing from the `testing` branch. Optionally move `QUARK_SIGNING_KEY` into both. |
| Move the Discord bot to its own `discord-bot` branch | Use `git subtree split` (steps in `tools/discordbot/MOVING.md`), then remove `tools/discordbot/` from the game branches. The C++ cross-language test then needs splitting: the bot half runs only when `QUARK_BOT_DIR` points at a bot checkout. The branch stays public, as the whole repo is. |
| Move the launcher to its own `launcher` branch | Release builds then check out that branch to build `quark_launcher` and package it. The game keeps its small launcher-upgrade code. |
| Move the website source to its own `site` branch | Production releases build the site from that branch plus the game's changelogs and weapon tables, then publish to `gh-pages`. |
| Sign `site/master.txt` (Owner, only if the ranking server ever moves) | The address is built into the game; a signed `master.txt` is the only override (docs/SIGNING.md). |

## 2. Open findings from the final review

### P1
| # | What |
|---|---|
| 2.2 | Signed connect and ticket messages don't name the server, so a malicious server can relay a player's signatures to a real ranked server and play under their id. Include the server's identity, and have the Worker check it. |
| 2.4 | The server's map-info cache keeps every name it's asked about, so `callvote map <random>` grows it forever. It also checks the shipped file while maps load from your maps folder first. |
| 2.5 | The new hit-rewind cap is below what `interp_ms` up to 200 needs, so those players' shots are rewound too little and miss moving targets. |
| 2.6 | The client's render time is still float seconds of server uptime: 8 ms off after 18 h, 31 ms after 3 days. Make it double. |
| 2.7 | Child-process pipes aren't close-on-exec, so two web requests at once can hang each other. The SIGPIPE swap is racy. |
| 3.6 | Ranked id migration drops the old rating if the new id already has rows, and `/hello` isn't retried. |
| 3.7 | The README's deploy SQL is incomplete. Replace it with the list in FINAL_REVIEW.md section 3. |
| 4.3 | The launcher-upgrade thread the game starts is detached, so quitting at the wrong moment can leave a Windows launcher missing. |
| 5.2 | Two sets of map limit constants, with dead checks and repeated literals. Keep one set. |
| 5.3 | Maps removed from the generator stay in `build/maps` and fail local test runs. A stray `.qmap` is embedded as official (ranked). |
| 5.4 | Forked test groups share one maps folder; the map report can grade a test's temporary map. |
| 6.2 | Players who joined mid tag match are forced onto team A next match, skipping 1v1 / duel seat limits. |
| 6.3 | Checkpoint time bonuses are cut short during overtime. |
| 1.4 | Old launchers read only 50 releases. Prune old testing prereleases, keeping about 10. |

### P2
| # | What |
|---|---|
| 2.8 | Tourney seed-rating fetches aren't deduplicated. |
| 2.9 | The version-mismatch reply is bigger than the spoofable request: small amplification. |
| 2.10 | Stopping a hosted game can freeze the UI up to ~15 s while it waits for background threads. |
| 2.11 | A reconnect with a different key keeps the old ranked id; an FFA leaver who rejoins after the grace is counted twice. |
| 3.8 | The bot's `/changelog` doesn't understand testing tags. |
| 4.4 | Holding Alt+Enter also presses Enter. |
| 4.5 | The Discord label in settings overlaps the next column. |
| 4.6 | `Ui::fit` can't shrink labels below the renderer's minimum text size. |
| 4.7 | The editor's `_new` fallback name can equal the original for 40-character names. |
| 4.8 | Overlapping editor handles: the far one can win the pick. |
| 4.9 | Scoreboard names aren't clipped, and the server-list marquee overflows on wide names. |
| 5.5 | Saved maps keep 6 significant digits while edits carry 9, so a downloaded edited map can differ by a hair. |
| 5.6 | Map parsing doesn't limit the title or reset every field. |
| 6.4 | A giant can spawn inside a wall on maps not built for duel. |
| 6.5 | `addbot` during a tag match skips the team lock. |
| 1.5 | An old pinned launcher version silently moves to the newest. |
| 1.8 | Update installs keep every retired file forever. |

### Cleanup (all the "Clean" rows in FINAL_REVIEW.md)
- Duplicates:
  - `vote_.caller` set twice, and two `#include` lines repeated;
  - `isValidMapName` / `validMapName`;
  - two caret implementations;
  - `modeRespawns()` vs `rulesRespawnDelay`;
  - several hex checks.
- Unused code:
  - `NetClient::reconnectToken`, `NavGraph::nextWaypoint` and `hideRounds_`;
  - `PENDING_MS` in the Worker;
  - the per-tick tag re-check.
- Stale comments and docs:
  - protocol numbers;
  - the syringe throw comment;
  - the SIGNING.md scratchpad note;
  - `docs/MAP_REPORT.md`, which predates the grapple change.
- Per-frame and per-tick waste:
  - the console re-serialises the config every frame;
  - `setRanked` scans the maps folder on the tick.
- Small hardening:
  - constant-time key compares;
  - `/disputes` should be a POST;
  - `MAX_BODY` checks only Content-Length;
  - a domain prefix for `master.txt` signatures;
  - the bot sends BOT_KEY wherever unsigned `master.txt` points;
  - the pip upgrade in the bot installer isn't hash-pinned.
- Tests:
  - test.mjs never exercises shared-IP limits;
  - the "concurrent" queue test runs sequentially;
  - one wall-clock 3 s window;
  - port blocks can land in the OS's ephemeral port range.

## 3. Second security review: batches not done yet (docs/FIXES_HANDOFF.md)

Done this session, and not repeated here: batch 1, most of batch 2, the handshake (NET-1/2/4), the rewind cap (NET-5), reliable-event overflow (NET-11), the server clock (PERF-5 server side), the lightmap prepass (PERF-2), bot navigation (PERF-10), Windows tests (ENG-1), the MSVC removal (ENG-4), per-thread rules (COR-6), map edits (MAP-12), maps as a build artifact (part of MAP-9), and most of the supply-chain and Worker items.

| ID | P | What |
|---|---|---|
| NET-9 | P1 | Shot spread seeds come from the client, so a cheat can pick perfect spreads. Use a server per-player counter plus a per-match salt. |
| NET-10 | P1 | Hidden players' health, weapon, firing and bomb flags are still sent, along with projectiles from unseen players (a minor wallhack). Hide-and-seek leaks hider info during freeze. Dead players' chat should reach only the dead. |
| PERF-3 | P1 | The lightmap bake runs on the main thread. A big map on a remote server can time out the join, and the window shows "Not Responding". Bake on a worker thread. |
| PERF-8 | P2 | Snapshot building allocates per client per tick. Reuse buffers. |
| PERF-9 | P2 | Visibility raycasts aren't staggered or capped (tick spikes with 32 players on Midtown). |
| PERF-11 | P2 | Main-thread hitches: the map list rescans, the server-list refresh, demo writing, announcer audio swaps. |
| PERF-12, 13 | P2 | Cache `findDataPath`; compute `interpolatedPlayers` once per frame; send input once per frame; static editor buffer; query the GPU's max texture size for the atlas. |
| MAP-3 | P1 | Map reachability tests aren't collision-checked; unreachable should score 0. |
| MAP-2 | P1 | Not every offered mode has enough spawns. Anvil hosts the 4-player ranked instagib FFA. |
| MAP-5 | P1 | A cart-sweep test against geometry, plus a keep-clear registry in the generator. Fix the Railyard box at track 102-106 m and the Lodestone pillar and dais. |
| MAP-4 | P1 | Metric fixes, one at a time: loops, vertical, per-team balance, worst-objective grading. Lodestone's race legs still read "-1 m". Each fix may fail maps that then need redesign (your call). |
| MAP-10, 11 | P2 | Real `stairs` and `setting` keywords instead of comments; a `version N` header; unknown optional keys warn instead of failing. |
| MAP-16, 17, 18 | P2 | Spawn sanity: facing, spacing, enemy spawns in line of sight; a mirror check for ranked maps. |
| ENG-2 | P2 | Exception boundaries around demo / pak loading and Worker-reply parsing. Map loading is done. |
| ENG-3 | P2 | Fuzz harnesses for the parsers (packets, maps, rules, packages) behind a CMake option, plus an ASan/UBSan CI job. |
| ENG-5 | P2 | Fully hermetic tests (see 5.4). Ports and the data folder are mostly done. |
| COR-4 | P2 | Editor undo should work on a copy. |
| COR-5 | P2 | Unicode paths: use `std::filesystem::path` end to end. Today a non-ASCII Windows user folder may break data paths. |

## 4. Decisions waiting on you (Owner)

| What | Notes |
|---|---|
| MAP-6 Lodestone | It's offered in every mode but graded only for points; bomb is unbalanced. Either grade every mode and fix the map, or offer fewer modes. |
| MAP-8 Midtown timing | The defender lead at points D and E is outside its own 2-10 s band. Re-stage the spawns. Also grade the worst point instead of the average. |
| MAP-9, rest | Editor work as a per-map overlay of edit ops on top of the generated map, so editing and regenerating don't fight. Generation is already a build step. |
| MAP-13 | Identify maps by sha256 instead of name + CRC32. |
| MAP-14 | Split `make_maps.py` into prefabs plus one file per map. |
| Knife throw switch delay | After a knife throw, your next weapon now waits 0.75 s; this is what stops the throw + crossbow instant 105. Keep it or revert it (one line). |
| KOTH | One contester freezes the clock indefinitely. Left as is: arguably normal KOTH. |
| LICENSE | None yet, by your choice. Contributors have no stated terms until there is one. |
| LTO | Measured, but left off: the bench was noisy on shared software rendering. Re-measure on a real machine. |
| Windows jitter check | One netcode timing check failed 2 of 3 runs under Wine. If it fails on real Windows CI, choose a Windows-specific allowance or a non-blocking job. |

## 5. Bots

| P | What |
|---|---|
| P2 | Falls and self-kills were only re-checked on Bastion after the dash fix. Run a playtest of all maps (`quark_tests playtest`) and fix any map where bots fall. |
| P2 | "FFA vs 3 expert bots" passes with 5 kills against a minimum of 2. The margin is thin; watch it. |
| P3 | Bots use the normal-size hull for site and hill checks (harmless while size modifiers are duel-only). Hide and tag bots may use the previous map's nodes until the new graph is ready. |

## 6. Older, smaller items

| P | What |
|---|---|
| P2 | The server clock `timeMs_` is 32-bit and wraps after 49.7 days of uptime. Normal restarts and updates reset it long before then. |
| P2 | Projectiles move per tick, not in fixed steps: about 5 cm per second of flight differs between 125 and 1000 Hz. |
| P2 | Edit-session autosave still serialises (~10-15 ms on Midtown) and parse-checks (~3 ms) on the tick every 5 s. Move it off the tick. |
| P2 | The client editor does a redundant full rebuild after each edit when editing alone (`Game::edChanged`). |
| P3 | `sfx_recorded.h` stays committed, because generating it needs ffmpeg. |
| P3 | Game modes are ~90 `mode == X` branches. A `GameMode` interface would keep each mode in one place (big refactor). |
| P3 | Map identity, format version and size budget (first review G3, G4, G7); an entity system for objective markers (G8). |

## 7. Asset Studio

| P | What |
|---|---|
| P2 | The asset exporter was stopped partway and never finished: `quark_tests exportassets`, glTF models, and loading overrides from `assets/overrides/`. The Studio page lists only some assets and can't preview the 3D models. Redo it, then rebuild and republish the page (CLAUDE.md asks for that after every push). |
