# What's left to do

Everything known to be unfinished, as of the end of the 1.17.0 work (after the fix branches merged on 2026-10-23). It draws on four sources:
- [FINAL_REVIEW.md](FINAL_REVIEW.md): ids like 2.8;
- the second security review's plan, [docs/FIXES_HANDOFF.md](docs/FIXES_HANDOFF.md) with [docs/REVIEW.md](docs/REVIEW.md): ids like MAP-3;
- the owner's decisions, [docs/OWNER_DECISIONS_2.md](docs/OWNER_DECISIONS_2.md) (B1-B10) and [docs/JUDGMENT_CALLS.md](docs/JUDGMENT_CALLS.md) (C1-C16);
- notes left by this session's work.

**Fixed and merged:** every Blocker, High, Medium and Low finding from the final review, and every cleanup group except 1.7 and 1.9. From the second review: NET-9, NET-10, PERF-3, PERF-8, PERF-9, PERF-11, PERF-12, PERF-13, MAP-2, MAP-3, MAP-5, MAP-10, MAP-11, MAP-13, MAP-16, MAP-17, MAP-18, ENG-2, ENG-3, COR-4 and COR-5. The owner's decisions B3 (in-game part), B4, B6-B10, C2, C7-C9, C12, C13 and C15 are in. What's still running is in section 0.

Each item has a priority:
- **P1** — do soon, on the `testing` branch.
- **P2** — worth doing.
- **P3** — nice to have, or needs a decision first.

**Owner** marks items that need your decision or your hands.

---

## 0. Finishing 1.17.0 (in this order)

| # | What | Who |
|---|---|---|
| 0.1 | **Maps (D3a, running).** C1: strict map metrics (MAP-4: loops, vertical play, balance per team, no credit for unreachable areas, grade the worst objective). C3: split `make_maps.py` into prefabs plus one file per map, output byte-identical. B1: Lodestone graded and fixed in every mode it offers, bomb included. B2: the −1..+1 s attacker/defender timing band at the worst site or point on every attack/defend map, and Midtown re-staged. Then the map report, metrics and structures docs regenerated. Not done until that branch merges. | me |
| 0.2 | **UI test (running):** `quark --uitest` clicks every button, as part of `quark_tests`. Not done until it merges. | me |
| 0.3 | **Final test run and performance check on a quiet machine.** One clean full `quark_tests` run with a private `TMPDIR`. Today's runs failed only timing checks while the machine was overloaded (interp delay, the tick-rate step-up at `rules_tests.h:563`, session phase, input backlog), and passed when re-run. Then `--bench` and the F3 lows (1% / 0.1%) against 1.16 on Midtown, Hightown and Lodestone, plus `serverbench` / `snapbench`. Check the bots' tick cost too: the new edge checks couldn't be measured on the loaded machine (maybe +5%). | me |
| 0.4 | **Asset Studio:** read the page's `replacements` collection and ship any `waiting` upload, then rebuild (`quark_tests exportassets build/assets_export`, `tools/studio/build.py`) and republish to the same URL. It's about 410 files (34 MB), so two publishes because of the 255-files-per-publish limit. New since the last publish: the exporter itself, the LOCATOR model, and new map hashes. | me |
| 0.5 | **Handover:** finish `docs/HANDOVER.md` (section 12, the state at 1.17.0, after 0.1-0.3) and bring FINAL_REVIEW.md and this file up to date. | me |
| 0.6 | **Say go.** I push the branch: it builds and tests, and it's the first real run of the Windows test job. If that job's one netcode timing check fails on real Windows, I give only that check a Windows tolerance and keep the job blocking (C10). The same commit pushed to a new `testing` branch (the first step of the restructure) publishes the signed prerelease `test-1.17.0-test.N` for 0.7. | **Owner**, then me |
| 0.7 | **Hand test before production:** go through [docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md) on that testing build, and tell me it passed. Only then is tag `v1.17.0` pushed on the default branch; it publishes only if Windows, Linux and macOS all pass. | **Owner**, then me |
| 0.8 | **Ranking server database:** run the full SQL list in `tools/master/README.md` (same as FINAL_REVIEW.md section 3) **before** deploying the worker. Nothing new was added to it today. | **Owner** |
| 0.9 | **Deploy `tools/master/worker.js`** at the same time as the game release. Old games and game servers can't use it: announces, tickets and race runs all changed. Your dispute calls are now POSTs, with the key in the body (`GET /disputes?key=` is gone; the README has the curl commands). | **Owner** |
| 0.10 | Worker settings: add the cron trigger `*/15 * * * *` and the secret `ADMIN_KEY` (a long random string). | **Owner** |
| 0.11 | **Dedicated servers:** install python3, `git pull`, and run `tools/server/install.sh` once. If players reach a server through a different forwarded port, or it runs in a container, start it with `--own-addr host:port`, or ranked ids won't work there (the log says so, the README explains it, and a game hosted from the menu shows it on the host screen). Optionally set `QUARK_ALERT_WEBHOOK` in `/etc/quark/update.env`. | **Owner** |
| 0.12 | **Discord bot:** redeploy with the new `install.sh` (secrets go in `/etc/quark-discord.env`). Rotate the bot token and `BOT_KEY`, and put the new `BOT_KEY` in the Worker's secrets too. Update its role or invite to the new permissions (README). The bot now uses the game's built-in Worker address and never reads `master.txt`; if the Worker ever moves, set `master_url` in its config. | **Owner** |
| 0.13 | **Signing key for the release and website jobs:** these jobs now run in the GitHub environments `production` and `testing`. A repository secret `QUARK_SIGNING_KEY` reaches them. If you move it into environment secrets, put it in both, or the website publish fails (by design). | **Owner** |
| 0.14 | Optional: keep today's ranked players established (the optional `INSERT INTO accounts …` in the SQL list). Without it, everyone is provisional for 5 matches. | **Owner** |
| 0.15 | **Old launchers:** launchers 1.16 and older read only the newest 50 releases. Ship production 1.17.0 before 50 testing builds pile up; after that, the game keeps every launcher current. | **Owner** |

## 1. Decided, to do after 1.17.0

| What | Notes |
|---|---|
| Dev map editor (B3) | A separate developer build only you get, never in public releases: numeric position / rotation / scale, adjustable grid snapping, per-mode spawns, routes and staged spawns, a live timing and grade readout (−1..+1 s at the worst point). It uploads maps to a `map-tests` branch; each release lists which maps, and which version of each, it takes from there (a manifest the generator and build read). The in-game editor stays for everyone. |
| GameMode refactor (C4) | Replace the ~90 `mode == X` branches with one GameMode interface per mode, on `testing`. |
| LTO (C5) | Left off. Measure on a real machine (not shared software rendering) before turning it on. |
| Repository restructure | The table below. |

**Repository restructure:**

| What | Notes |
|---|---|
| `testing` branch | All new work goes there. Each push becomes a signed prerelease (`test-<version>-test.N`), and Dependabot targets it (FINAL_REVIEW 1.9). |
| Protect the default branch (**Owner**, GitHub settings) | Require a PR, the `main-guard / from-testing` check and the build checks. No force pushes or deletions. |
| Tag rule for `v*` (**Owner**, GitHub settings) | Only you can create, move or delete production tags. |
| Environments `production` and `testing` (**Owner**, optional) | The workflow already uses them. Add deployment rules (production: tags `v*`; testing: branch `testing`), and optionally move `QUARK_SIGNING_KEY` into both. |
| Discord bot → `discord-bot` branch | `git subtree split` (steps in `tools/discordbot/MOVING.md`), then remove `tools/discordbot/` from the game branches. The cross-language test then needs splitting: the bot half runs only when `QUARK_BOT_DIR` points at a bot checkout. The branch is public, like the whole repo; the secrets stay on your server. |
| Launcher → `launcher` branch | Release builds check out that branch to build and package `quark_launcher`. The game keeps its small launcher-upgrade code. |
| Website → `site` branch | Production releases build the site from that branch plus the game's changelogs and weapon tables, then publish to `gh-pages`. |

## 2. Remaining final-review findings

Every Low finding is fixed. What's left:

| # | P | What |
|---|---|---|
| 1.7 | P3 | `docs/SIGNING.md` "One-time setup" still points at a private key file in a session scratchpad (deleted; the secret is set). |
| 1.9 | P3 | Dependabot targets the `testing` branch, which doesn't exist until the restructure. |
| 3.9 | P3 | The Worker's read cache (`caches.default`) does nothing on `*.workers.dev`; it needs a custom domain. Documented in worker.js. **Owner**, optional. |

**Cleanup:**
- clang warns about unused code gcc doesn't: `kRocketLife`, `kMaxVoteListItems` (server.cpp), `NetClient::snapRecvTime_`, `kJumpReach` (mapreport.cpp).

## 3. Second security review: what's left

| ID | P | What |
|---|---|---|
| MAP-4, 6, 8, 14 | P1 | Running in D3a (section 0.1): C1, B1, B2, C3. |
| NET-10 | P2 | Done (C12), except one part of the review: players within 20 m are always sent, with no ray check (`kVisNear`). So a hide-and-seek seeker is still sent hiders behind a wall within 20 m. Decide whether hide and seek gets a smaller radius or none. |
| COR-5 | P2 | Unicode paths are done for the client, but untested on real Windows (no Wine here). The dedicated server's own `main` still takes ANSI arguments. Check a non-ASCII Windows user folder by hand. |
| PERF-12 | P2 | Input stays one packet per tick, by choice (latency first). Batching cut the copies of each command from 4 to 2 and delayed commands. Still open from PERF-12: the F3 overlay's GPU timestamp read can stall a frame, and the shadow pass uploads its geometry twice. |
| NET-14 | P3 | Projectile ids are 16-bit and wrap: after 65,536 projectiles an id can repeat while an old one is still on a client's screen (a wrong trail or a missed explosion for a moment). |
| COR-8 | P3 | The interpolation clamp half of COR-8 (the projectile-gravity half is fixed). |
| SUP-12 | P3 | The website loads Google Fonts and has no Content-Security-Policy. Self-host the fonts and add a CSP meta tag (with the site remake). |
| §5.5 | P3 | Feedback gaps: no directional damage flash, and remote players make no sound while in the air. |
| SUP-7 | Fixed | The launcher now loads DLLs only from System32, never from the folder it was started in (Downloads). |
| B7 follow-up | P2 | The laser now reaches 24 m, but the map report's long-sightline band still uses 32 m (`kBandLong` in `tests/mapreport.cpp`), as do the Furnace notes in `make_maps.py` and `docs/MAP_STRUCTURES.md`. Changing the band changes scores, so do it with the metric work (C1). |

## 4. Decisions waiting on you (Owner)

| What | Notes |
|---|---|
| Visible map changes | Ranked maps (Anvil, Dojo, Furnace, Gantry, Temple) now have the sun straight overhead, for the mirror check. Team spawns got screens or cover so they can't see the enemy's spawns (Canal, Depot, Downtown bus shelters, Railyard, Lodestone, Midtown). Worth a look in game. |
| Knives-only modes | B4 was applied literally: after a throw, the knife is disabled for 4 s, so in a knives-only mode you can't swing for 4 s either (before, you could). Keep it, or let the swing work there. |
| Expert bots | C11 sets expert rifle DPS against a strafing target at 18. Measured today: 16.7 (42-43% of perfect aim). Keep it, or tune it up. |
| LICENSE | None yet, by your choice. |
| CLAUDE.md | Agents may not edit it. Suggested additions: after decoding an icon override, run `python3 tools/make_icon.py`; a website picture override must keep its file name and type; every qpak also carries a signed `quark_launcher` and every release and website file is signed. |

## 5. Bots

| P | What |
|---|---|
| P3 | Falls caused by an enemy's push (laser or rocket): about 3 per full playtest run, mostly on Dojo. An occasional fall from Hightown's roof showed up in some runs, not the last one. |
| P3 | Self-kills went from 8 to 10 over all 42 playtest runs (mostly rockets). Still rare. |
| P3 | Expert grapple use on Clockwork dropped (0-9 per 40 s, was 14-27) now that every pull is checked as safe. The test still requires at least one. |

## 6. Older, smaller items

| P | What |
|---|---|
| P2 | Test suite: tests that fail because `Server::start` failed (usually a port held by another run) count a failure without printing a FAIL line. Timing checks fail under heavy load; group logs and data dirs live in the shared temp dir, so run with a private `TMPDIR`. |
| P3 | Visibility rays aim at a normal-size player, not a giant's scaled box (pre-existing). |
| P3 | `make_maps.py` takes about 6 s instead of 0.7 s, because of the spawn-spot search. |
| P3 | A map size budget and an entity system for objective markers (first review G7, G8). C7 put the entity system after 1.17.0. |

## 7. Asset Studio

The exporter is done (`quark_tests exportassets`): sounds, voices, glTF models, maps, font, icon and website pictures, with overrides loaded from `assets/overrides/` and the user folder (`docs/ASSETS.md`). What's left is the rebuild and republish in 0.4, and after every push from then on.
