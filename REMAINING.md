# What's left to do

Everything known to be unfinished, as of the 1.17.0 release (2026-10-23). It draws on four sources:
- [FINAL_REVIEW.md](FINAL_REVIEW.md): ids like 2.8;
- the second security review's plan, [docs/FIXES_HANDOFF.md](docs/FIXES_HANDOFF.md) with [docs/REVIEW.md](docs/REVIEW.md): ids like MAP-3;
- the owner's decisions, [docs/OWNER_DECISIONS_2.md](docs/OWNER_DECISIONS_2.md) (B1-B10) and [docs/JUDGMENT_CALLS.md](docs/JUDGMENT_CALLS.md) (C1-C16);
- notes left by this session's work.

**Fixed and merged:** every Blocker, High, Medium and Low finding from the final review, and every cleanup group except 1.7 and 1.9. From the second review: NET-9, NET-10, PERF-3, PERF-8, PERF-9, PERF-11, PERF-12, PERF-13, MAP-2, MAP-3, MAP-5, MAP-10, MAP-11, MAP-13, MAP-16, MAP-17, MAP-18, ENG-2, ENG-3, COR-4 and COR-5. The owner's decisions B3 (in-game part), B4, B6-B10, C2, C7-C9, C12, C13 and C15 are in. What's left from the release is in section 0.

Each item has a priority:
- **P1** — do soon, on the `testing` branch.
- **P2** — worth doing.
- **P3** — nice to have, or needs a decision first.

**Owner** marks items that need your decision or your hands.

---

## 0. After 1.17.0 shipped (2026-10-23)

1.17.0 is released (tag `v1.17.0` on `b64ce4b`), and the website, the Worker and D1, the game server and the bot are on it. What happened is in docs/HANDOVER.md sections 0 and 12. Done from the old list: the maps and server work (0.1), the UI test (0.2), the Asset Studio republish (0.4), the handover (0.5), the release (0.6, 0.7; the testing-build step was skipped by the owner's call), the SQL, the Worker, the cron and `ADMIN_KEY` (0.8-0.10, 0.14), the server box and bot with new keys (0.11, 0.12), and shipping before 50 testing builds (0.15).

Still open from it:

| # | What | Who |
|---|---|---|
| 0.3 | **Performance check on a quiet machine with real hardware:** `--bench` and the F3 lows (1% / 0.1%) against 1.16 on Midtown, Hightown and Lodestone, plus `serverbench` / `snapbench` and the bots' tick cost (the new edge checks may add about 5%). New since: drawing the stuck knives, bolts and syringes (0.1 ms in the draw function; 3-5 fps lost under software GL in the worst case). | me, on the owner's machine or a quiet runner |
| 0.13 | The signing key reaches the `production` and `testing` environments as a repository secret. If you move it into environment secrets, put it in both, or the website publish fails (by design). | **Owner**, only if moving it |
| 0.16 | **Automatic Worker deploys** are not switched on (`deploy-worker` skips): an API token and three values in GitHub, docs/OPERATIONS.md section 3. Until then, update the Worker by hand when a version changes it. | **Owner**, optional |
| 0.17 | **The bot's Discord role and invite** may need the permissions the 1.17 bot uses (tools/discordbot/README.md): check that leaderboard roles and the testing channel work. | **Owner** |
| 0.18 | **Quick dash** (duel, blades): it gives a third charge with the normal 1.2 s cooldown. Should it get a shorter cooldown again? | **Owner** |

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
| MAP-4, 6, 8, 14 | Fixed | Done in D3a: C1, B1, B2, C3. |
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

The exporter is done (`quark_tests exportassets`): sounds, voices, glTF models, maps, font, icon and website pictures, with overrides loaded from `assets/overrides/` and the user folder (`docs/ASSETS.md`). It was rebuilt and republished for 1.17.0; from now on, after every push (CLAUDE.md). The page host needs explicit types for `.gltf` and `.qmap` files, and one publish takes at most 255 files (docs/HANDOVER.md section 0).
