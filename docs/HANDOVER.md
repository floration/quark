# Handover: quark after 1.17.0

This document is for whoever continues quark after 1.17.0. That is the owner, working in a local Claude Code project, or a fresh session. It covers:
- how the project is laid out;
- how it is built, tested and released;
- what runs where;
- the rules the owner has set;
- what is still open.

Read [CLAUDE.md](../CLAUDE.md) first: it holds the standing rules, and any session working on quark loads it automatically.

---

## 1. What quark is

A fast, minimal FPS on a custom C++17 engine:
- OpenGL 3.3;
- custom UDP netcode with client prediction, lag compensation and delta snapshots;
- runs on Windows, Linux and macOS.

**Executables:**

| Executable | What it is |
|---|---|
| `quark` | The game, plus the menu, map editor, demo player, launcher upgrade and UI test |
| `quark_server` | The dedicated server |
| `quark_tests` | The headless test suite |
| `quark_launcher` | The installer/updater |

**Around the game:**
- a ranking server: a Cloudflare Worker with a D1 database;
- a Discord bot;
- a website on GitHub Pages;
- the Asset Studio page, where the owner replaces AI-made assets.

## 2. Repository layout

| Path | What it holds |
|---|---|
| `src/game/` | Shared simulation: movement (`pmove`), maps (`map`), the server and its rules (`server`, `gamemodes`, `customrules`), netcode (`protocol`, `netclient`), bots, ranked, demos (`demo`), `gamedefs.h` (weapons, modes, protocol number) |
| `src/client/` | Renderer, UI, menus, HUD, settings (`config`), keybinds (`binds`), audio, the map editor, lightmaps, the UI test |
| `src/server/main.cpp` | The dedicated server's entry point |
| `src/launcher/` | The launcher: qpak install, signature checks, channels, self-update |
| `src/common/` | Small shared pieces: sha256, UTF-8 paths, buffers, profiling |
| `tests/` | `quark_tests`: the main `tests.cpp`, plus `server_tests.h`, `rules_tests.h`, `mapreport.cpp` (the map grader), `walkgrid.h` (reachability), asset tests |
| `tools/make_maps.py` | Generates every official map at build time. `maps/*.qmap` is a build artifact and never committed. |
| `tools/embed_maps.py`, `embed_overrides.py` | Embed maps and owner asset overrides into the binary |
| `tools/pack.py`, `tools/sign/` | Release packaging (`quark-<platform>.qpak`) and Ed25519 signing |
| `tools/master/` | The ranking server: `worker.js`, `schema.sql`, `test.mjs`, README with the deployment steps and the SQL to run per version |
| `tools/server/` | Dedicated server install (`install.sh`), auto-update (`autoupdate.sh`), alerts |
| `tools/discordbot/` | The Discord bot (`bot.py`, `quarkdata.py`, `install.sh`, README) |
| `tools/studio/` | The Asset Studio page source and its file builder |
| `site/` | The website (deployed by production releases) |
| `assets/` | Fonts, sounds, voice, icons; `assets/overrides/` holds the owner's replacement assets |
| `changelogs/<VERSION>.md` | One per release, shown by the website, the launcher, the release notes and the bot |
| `docs/` | See section 10 |

## 3. Build and test

Building:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build/quark_tests                 # everything (forks one process per group; netcode timing groups run alone)
build/quark_tests <group>         # one group: bots, maps, movement, modes, hidden, race, votes, edit, ranked,
                                  # ranked2, rules, assets, server, netcode, tickrate, ...
build/quark_tests mapreport       # writes docs/MAP_REPORT.md
```
- Linux needs the X11/GL dev packages listed in the README; python3 is needed at build time.
- Windows releases are built with MinGW-w64.
- Client changes can be checked headless:
  ```
  Xvfb :99 &
  DISPLAY=:99 build/quark --host ffa --map bastion --screenshot out.png
  ```
- The UI test: `build/quark --uitest <report.json>`. It clicks every button on every screen and checks what each one does.

**Timing-sensitive tests:** the netcode, tick-rate and interpolation-delay groups fail on a loaded machine. Run the full suite on an idle machine with a private `TMPDIR` before calling a failure real. If one netcode timing check fails on the real Windows runner, the owner's decision (C10) is to give only that check a Windows-specific tolerance and keep the job blocking.

## 4. Branches and releases

**Branches** (the restructure the owner asked for, done right after 1.17.0 ships):

| Branch | What it is |
|---|---|
| `testing` | Where all new work lands. Every push publishes a signed prerelease `test-<VERSION>-test.<N>`. Dependabot targets it. |
| Default branch (protected) | Updated only by a PR from `testing`. Production releases come only from a `v<VERSION>` tag on it. |
| `launcher` | The launcher's code. It should almost never change; release builds check it out to package `quark_launcher`. |
| `site` | The website source. Production releases build the site from it, plus the changelogs and weapon tables, into `gh-pages`. |
| `discord-bot` | The bot (moved out with `git subtree split`; steps in `tools/discordbot/MOVING.md`). Its secrets live only on the bot's server. |
| `gh-pages` | The published website. Written by CI only. |
| `map-tests` | Later: maps uploaded by the owner's dev map editor (section 9). |

**Release flow:**
1. Work is committed on `testing`, with `VERSION` bumped and `changelogs/<VERSION>.md` written.
2. The push to `testing` runs CI: Linux tests, Windows tests, macOS build, sanitizers. It publishes the signed testing prerelease (Windows + Linux; macOS files follow).
3. **The owner tests that build by hand with [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md).** Nothing goes further until the owner says it passed.
4. A PR from `testing` to the default branch; the owner merges it.
5. The owner tags it: `git tag v<VERSION> <merge commit> && git push origin v<VERSION>`. CI publishes the production release only when Windows, Linux and macOS all pass; then it deploys the website. Dedicated servers update to it by themselves.

Workflows are in `.github/workflows/`:
- `release.yml` holds every build and publish job.
- `main-guard.yml` makes sure the default branch only takes PRs from `testing`.
- Actions are pinned by commit SHA, with `persist-credentials: false`. Write permissions exist only on the release and pages jobs.

**The owner's push rule:** commit locally as you go, but never push until the owner says so. They batch fixes into fewer releases.

## 5. Signing

[SIGNING.md](SIGNING.md) has the details. In short:
- Ed25519 (Monocypher) with the release key, held as the `QUARK_SIGNING_KEY` repository secret. CI must have it for the release, release-testing and pages jobs.
- Every published file has a `.sig`. That covers each `quark-<platform>.qpak`, the release manifest `quark-release.txt` (sha256 of each asset) and `SHA256SUMS`. The launcher inside each qpak is signed too.
- From 1.17.0 on, everything published must be signed (owner decision B6). The launcher hides any unsigned release that claims version 1.17.0 or newer. Releases 1.16.x and older install without a signature and without a warning.
- The ranking server's address file `site/master.txt` is also signed. Only a file signed with the release key can move the game to a new ranking server.

## 6. What runs where (and what the owner deploys by hand)

| Piece | Where | How it updates |
|---|---|---|
| Ranking server | Cloudflare Worker + D1 database. Address in `site/master.txt`. | By hand. First run the SQL listed in `tools/master/README.md` for the version in the D1 console, then paste `worker.js` and deploy. Cron trigger `*/15 * * * *`. Secrets: `ADMIN_KEY` and `BOT_KEY`. Deploy at the same time as a game release that changes ranked. |
| Dedicated servers | Linux boxes (the Oracle one), installed with `tools/server/install.sh` | Automatic, to **production** releases only. `autoupdate.sh` checks every minute, verifies the signed manifest, builds that exact commit, and restarts between matches. Logs: `journalctl -t quark-update`; failures at crit priority, optionally sent to `QUARK_ALERT_WEBHOOK`. Behind a different forwarded port or in a container, run with `--own-addr host:port`. |
| Discord bot | The bot's Linux box, installed with `install.sh` | By hand. Secrets in `/etc/quark-discord.env`. After 1.17.0 it lives on the `discord-bot` branch. |
| Website | GitHub Pages (`gh-pages`) | Automatic, on each production release |
| Asset Studio | https://claude.ai/artifact/JuDQckiZY15XUHz1xbD3pC | Rebuilt and republished after each push (CLAUDE.md "Asset Studio"). Replacements the owner uploads are pulled into `assets/overrides/` before each push. |

**Owner steps for 1.17.0 specifically** (REMAINING.md section 0):
1. Run the 1.17 SQL list before deploying the Worker.
2. Deploy the Worker together with the release.
3. Add the cron trigger and `ADMIN_KEY`.
4. On each dedicated server: `git pull` and run `install.sh` once.
5. Redeploy the bot and rotate its token and `BOT_KEY`.
6. Ship production before 50 testing prereleases pile up, because launchers 1.16 and older only read the newest 50 releases.

## 7. Rules the owner has set (all in CLAUDE.md)

- **Performance and latency first.** Judge frame rate by 1% and 0.1% lows. Anything that can be made faster or lower-latency should be. For example, input is sent every tick, not batched.
- **Weapons and movement** in `gamedefs.h` / `pmove.cpp` change only when the owner specifies them.
- **Maps:**
  - each map is built for one mode and offered for that mode plus FFA; Lodestone is offered in every mode;
  - maps are generated by `tools/make_maps.py`; never hand-edit or commit `.qmap` files;
  - every generated map must score ≥ 7 in every category; grade strictly and never loosen a metric to pass a map;
  - attack/defend maps must have attackers and defenders reach every site or point within −1..+1 s, graded at the worst site or point;
  - human-made maps run every check but don't fail the run; their numbers are reported for balancing.
- **Formats change only with converters**, in this and every future version:
  - the map format: a step in `kMapUpgrades` plus a test; players' maps are converted at startup, with originals kept in `maps/old/v<N>/`;
  - demos: a converter for each older protocol, with originals in `demos/old/<version>/`.
- **Updates delete only retired game files.** Never configs, user maps, demos, screenshots or anything the player made.
- **Ranked lock:** the live-match lock applies only to 1v1 vanilla, 1v1 laser vampire and FFA instagib.
- **Human checklist before production:** [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md).

## 8. Protocol and compatibility notes

- `kProtocolVersion` (gamedefs.h) is 37 for 1.17.0. Any wire change bumps it, and old and new can't play together.
- `kOldestDemoProtocol` and the demo converter (`src/game/demo.cpp`, `kRecordFormatSince = 24`) decide which demos play directly and which are converted. A snapshot layout change needs a converter step.
- Snapshot projectile bits: 1 new, 2 velocity, 4 lob, 8 stuck, 16 out of sight.
- Maps are identified by the sha256 of their content: the ranked allowlist, light caches (`cache/<sha256>.qlm`), downloads (`maps/downloads/<sha256>.qmap`) and edit-session chains.
- Map format version 2: `version`, `stairs`, `setting` and `handmade` keywords. Unknown optional keys only warn; gameplay keys stay strict.

## 9. Planned after 1.17.0 (owner decisions)

- **Repository restructure** (section 4): the `testing` branch, branch protection and the `v*` tag rule (owner, in GitHub settings), and moving the launcher, site and Discord bot to their own branches.
- **Dev-only advanced map editor (B3).** A separate developer build only the owner gets, never in public releases. It should have:
  - numeric position, rotation and scale entry;
  - adjustable grid snapping;
  - per-mode spawn, route and staged-spawn editing;
  - a live timing and grade readout using the −1..+1 s band at the worst point.

  It uploads maps to the `map-tests` branch. Each release then lists which maps, and which version of each, come from `map-tests`. That needs a manifest (map name + commit) read by the generator and the build. The normal in-game editor stays for everyone.
- **The owner will hand-make every map** later. Hand-made maps ship by un-ignoring them in `.gitignore` (`!/maps/<name>.qmap`). Their scores show up in the map report for balancing.
- **GameMode refactor (C4).** Replace the roughly 90 `mode == X` branches with one GameMode interface per mode, on `testing`.
- **LTO (C5):** measure on a real machine before turning it on.

## 10. Documentation index

| File | What it is |
|---|---|
| [CLAUDE.md](../CLAUDE.md) | Standing rules for any session |
| [README.md](../README.md) | Build, hosting, dedicated servers, controls, fonts |
| [REMAINING.md](../REMAINING.md) | Everything known to be unfinished, with priorities |
| [FINAL_REVIEW.md](../FINAL_REVIEW.md) | The pre-1.17.0 review and the status of each finding |
| [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) | The owner's hand test of each testing build |
| [OWNER_DECISIONS.md](OWNER_DECISIONS.md), [OWNER_ANSWERS.md](OWNER_ANSWERS.md), [OWNER_DECISIONS_2.md](OWNER_DECISIONS_2.md), [JUDGMENT_CALLS.md](JUDGMENT_CALLS.md) | Every design decision the owner made for 1.17.0, with their words |
| [REVIEW.md](REVIEW.md), [FIXES_HANDOFF.md](FIXES_HANDOFF.md) | The second security/quality review and its fix plan |
| [SIGNING.md](SIGNING.md) | Release signing and key handling |
| [ASSETS.md](ASSETS.md) | The asset export, the Asset Studio, overrides |
| [MAP_STRUCTURES.md](MAP_STRUCTURES.md), [MAP_METRICS.md](MAP_METRICS.md), [MAP_REPORT.md](MAP_REPORT.md) | Why each map is built the way it is, how maps are measured, the latest scores |
| `tools/master/README.md`, `tools/discordbot/README.md` | Deploying the ranking server and the bot |
| [AUDIT.md](AUDIT.md), [RELEASE_PLAN.md](RELEASE_PLAN.md) | History from the road to 1.0.0 |

## 11. Open items at handover

See [REMAINING.md](../REMAINING.md) for the full list. The final state of 1.17.0 (tests, performance, maps) is recorded in section 12 below once the final check is done.

## 12. State at 1.17.0

*(Filled in at the end of the final check: test results, performance figures, map scores, what was pushed.)*
